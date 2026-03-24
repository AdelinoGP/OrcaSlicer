#include "BitmapCache.hpp"

#include "libslic3r/Utils.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <boost/nowide/cstdio.hpp>
#include <boost/filesystem.hpp>

#ifdef __WXGTK2__
// Broken alpha workaround
#include <wx/mstream.h>
#include <wx/rawbmp.h>
#endif /* __WXGTK2__ */
#include <GL/glew.h>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
#include "3DScene.hpp"

namespace Slic3r { namespace GUI {

BitmapCache::BitmapCache()
{
    // [STATE] Record the highest screen scale factor now so the shared cache can deliver Retina-ready textures without per-call jitter.
    // Note: win->GetContentScaleFactor() is not used anymore here because it tends to
    // return bogus results quite often (such as 1.0 on Retina or even 0.0).
    // [STATE] Retain the highest global screen scale once so the shared cache keeps Retina bitmaps crisp instead of oscillating per window.
    // We're using the max scaling factor across all screens because it's very likely to be good enough.
    m_scale = mac_max_scaling_factor();
#endif
}

void BitmapCache::clear()
{
    // [STATE] Evict every cached `wxBitmap` so their backing textures can be destroyed before the next render pass.
    // [THREAD] Must run on the wx/GPU thread so the GL context still owns the resources when we delete them.
    // [UNITY] In Unity this mirrors clearing a `Dictionary<string, Texture2D>` while releasing each `Texture2D` through `Object.Destroy` on
    // the main loop.
    for (std::pair<const std::string, wxBitmap*>& bitmap : m_map)
        delete bitmap.second;

    m_map.clear();
}

static wxBitmap wxImage_to_wxBitmap_with_alpha(wxImage&& image, float scale = 1.0f)
{
    // [INTENT] Normalize wxImage -> wxBitmap transfer while preserving alpha channels and, on Apple, the retina scale hints.
    // [UNITY] Equivalent to `Texture2D.CreateExternalTexture` with `TextureFormat.RGBA32` followed by `Apply`, so Unity textures stay
    // pixel-perfect. [PORTING_HAZARD:P2] `wxBitmap` scale hints depend on the OS backend; Unity will need to keep track of device scale
    // separately.
#ifdef __WXGTK2__
    // Broken alpha workaround
    wxMemoryOutputStream stream;
    image.SaveFile(stream, wxBITMAP_TYPE_PNG);
    wxStreamBuffer* buf = stream.GetOutputStreamBuffer();
    return wxBitmap::NewFromPNGData(buf->GetBufferStart(), buf->GetBufferSize());
#else
#ifdef __APPLE__
    // This is a c-tor native to Mac OS. We need to let the Mac OS wxBitmap implementation
    // know that the image may already be scaled appropriately for Retina,
    // and thereby that it's not supposed to upscale it.
    // Contrary to intuition, the `scale` argument isn't "please scale this to such and such"
    // but rather "the wxImage is sized for backing scale such and such".
    return wxBitmap(std::move(image), -1, scale);
#else
    return wxBitmap(std::move(image));
#endif
#endif
}

wxBitmap* BitmapCache::insert(const std::string& bitmap_key, size_t width, size_t height)
{
    // [INTENT] Populate or resize the shared bitmap entry so every GUI widget reuses the same backend storage instead of reallocating.
    // [STATE] `m_map` acts as the cache of atlas-like bitmaps keyed by derived descriptors, shrinking all bitmaps to the requested size
    // whenever necessary. [THREAD] Runs on the wx/GPU thread to avoid mixing bitmap creation with worker threads because wxWidgets is not
    // thread safe. [UNITY] Map this to a `Dictionary<string, Texture2D>` lookup that either resizes the existing `Texture2D` or creates a
    // new one via `Texture2D.Resize`.
    wxBitmap* bitmap = nullptr;
    auto      it     = m_map.find(bitmap_key);
    if (it == m_map.end()) {
        bitmap = new wxBitmap(width, height
#ifdef __WXGTK3__
                              ,
                              32
#endif
        );
#ifdef __APPLE__
        // Contrary to intuition, the `scale` argument isn't "please scale this to such and such"
        // but rather "the wxImage is sized for backing scale such and such".
        // So, We need to let the Mac OS wxBitmap implementation
        // know that the image may already be scaled appropriately for Retina,
        // and thereby that it's not supposed to upscale it.
        bitmap->CreateScaled(width, height, -1, m_scale);
#endif
        m_map[bitmap_key] = bitmap;
    } else {
        bitmap = it->second;
        if (size_t(bitmap->GetWidth()) != width || size_t(bitmap->GetHeight()) != height)
            bitmap->Create(width, height);
    }
#if defined(WIN32) || defined(__APPLE__)
    // Not needed or harmful for GTK2 and GTK3.
    bitmap->UseAlpha();
#endif
    return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string& bitmap_key, const wxBitmap& bmp)
{
    wxBitmap* bitmap = nullptr;
    auto      it     = m_map.find(bitmap_key);
    if (it == m_map.end()) {
        bitmap            = new wxBitmap(bmp);
        m_map[bitmap_key] = bitmap;
    } else {
        bitmap  = it->second;
        *bitmap = bmp;
    }
    return bitmap;
}

wxBitmap* BitmapCache::insert(const std::string& bitmap_key, const wxBitmap& bmp, const wxBitmap& bmp2)
{
    // Copying the wxBitmaps is cheap as the bitmap's content is reference counted.
    const wxBitmap bmps[2] = {bmp, bmp2};
    return this->insert(bitmap_key, bmps, bmps + 2);
}

wxBitmap* BitmapCache::insert(const std::string& bitmap_key, const wxBitmap& bmp, const wxBitmap& bmp2, const wxBitmap& bmp3)
{
    // Copying the wxBitmaps is cheap as the bitmap's content is reference counted.
    const wxBitmap bmps[3] = {bmp, bmp2, bmp3};
    return this->insert(bitmap_key, bmps, bmps + 3);
}

wxBitmap* BitmapCache::insert(const std::string& bitmap_key, const wxBitmap* begin, const wxBitmap* end)
{
    // [INTENT] Stitch multiple bitmaps side by side so callers can treat a sequence of icons as one cached texture row.
    // [STATE] The derived `width`/`height` and resulting `bitmap_key` encode layout metadata that keeps the cache coherent.
    // [UNITY] Equivalent to assembling a `Texture2D` atlas row using `SetPixels32` and storing it in a `Dictionary<string, Texture2D>`.
    size_t width  = 0;
    size_t height = 0;
    for (const wxBitmap* bmp = begin; bmp != end; ++bmp) {
#ifdef __APPLE__
        width += bmp->GetScaledWidth();
        height = std::max<size_t>(height, bmp->GetScaledHeight());
#else
        width += bmp->GetWidth();
        height = std::max<size_t>(height, bmp->GetHeight());
#endif
    }

#ifdef __WXGTK2__
    // Broken alpha workaround
    wxImage image(width, height);
    image.InitAlpha();
    // [PORTING_HAZARD:P3] GTK2 forces us to manually copy each pixel/alpha channel because `wxAlphaPixelData` is the only way to access
    // bits safely. Fill in with a white color.
    memset(image.GetData(), 0x0ff, width * height * 3);
    // Fill in with full transparency.
    memset(image.GetAlpha(), 0, width * height);
    size_t x = 0;
    for (const wxBitmap* bmp = begin; bmp != end; ++bmp) {
        if (bmp->GetWidth() > 0) {
            if (bmp->GetDepth() == 32) {
                wxAlphaPixelData data(*const_cast<wxBitmap*>(bmp));
                // FIXME The following method is missing from wxWidgets 3.1.1.
                //  It looks like the wxWidgets 3.0.3 called the wrapped bitmap's UseAlpha().
                // data.UseAlpha();
                if (data) {
                    for (int r = 0; r < bmp->GetHeight(); ++r) {
                        wxAlphaPixelData::Iterator src(data);
                        src.Offset(data, 0, r);
                        unsigned char* dst_pixels = image.GetData() + (x + r * width) * 3;
                        unsigned char* dst_alpha  = image.GetAlpha() + x + r * width;
                        for (int c = 0; c < bmp->GetWidth(); ++c, ++src) {
                            *dst_pixels++ = src.Red();
                            *dst_pixels++ = src.Green();
                            *dst_pixels++ = src.Blue();
                            *dst_alpha++  = src.Alpha();
                        }
                    }
                }
            } else if (bmp->GetDepth() == 24) {
                wxNativePixelData data(*const_cast<wxBitmap*>(bmp));
                if (data) {
                    for (int r = 0; r < bmp->GetHeight(); ++r) {
                        wxNativePixelData::Iterator src(data);
                        src.Offset(data, 0, r);
                        unsigned char* dst_pixels = image.GetData() + (x + r * width) * 3;
                        unsigned char* dst_alpha  = image.GetAlpha() + x + r * width;
                        for (int c = 0; c < bmp->GetWidth(); ++c, ++src) {
                            *dst_pixels++ = src.Red();
                            *dst_pixels++ = src.Green();
                            *dst_pixels++ = src.Blue();
                            *dst_alpha++  = wxALPHA_OPAQUE;
                        }
                    }
                }
            }
        }
        x += bmp->GetWidth();
    }
    return this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image)));

#else

    // [THREAD] wxMemoryDC operations must stay on the main thread because they target the GUI context.
    // [UNITY] Map this to `Texture2D` copying on the main thread, e.g., `Texture2D.SetPixels` over the atlas texture.
    wxBitmap*  bitmap = this->insert(bitmap_key, width, height);
    wxMemoryDC memDC;
    memDC.SelectObject(*bitmap);
    memDC.SetBackground(*wxTRANSPARENT_BRUSH);
    memDC.Clear();
    size_t x = 0;
    for (const wxBitmap* bmp = begin; bmp != end; ++bmp) {
        if (bmp->GetWidth() > 0)
            memDC.DrawBitmap(*bmp, x, 0, true);
#ifdef __APPLE__
        // we should "move" with step equal to non-scaled width
        x += bmp->GetScaledWidth();
#else
        x += bmp->GetWidth();
#endif
    }
    memDC.SelectObject(wxNullBitmap);
    return bitmap;

#endif
}

wxBitmap* BitmapCache::insert_raw_rgba(
    const std::string& bitmap_key, unsigned width, unsigned height, const unsigned char* raw_data, const bool grayscale /* = false*/)
{
    // [INTENT] Construct a cached RGBA bitmap from raw bytes so dynamically generated overlays can reuse GPU memory consistently.
    // [UNITY] Equivalent to `Texture2D.LoadRawTextureData` + `Apply` on the main thread and storing it in a shared `Dictionary<string, Texture2D>`.
    wxImage image(width, height);
    image.InitAlpha();
    unsigned char* rgb    = image.GetData();
    unsigned char* alpha  = image.GetAlpha();
    unsigned int   pixels = width * height;
    for (unsigned int i = 0; i < pixels; ++i) {
        *rgb++   = *raw_data++;
        *rgb++   = *raw_data++;
        *rgb++   = *raw_data++;
        *alpha++ = *raw_data++;
    }

    if (grayscale)
        image = image.ConvertToGreyscale(m_gs, m_gs, m_gs);

    return this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image), m_scale));
}

wxBitmap* BitmapCache::load_png(const std::string& bitmap_name,
                                unsigned           width,
                                unsigned           height,
                                const bool         grayscale /* = false*/,
                                const float        scale_in_center /* = 0*/) // BBS: support resize by fill border
{
    // [INTENT] Load PNG icons once per key and store them in `m_map` so all consumers share identical bitmaps.
    // [THREAD] Hits disk synchronously, so Unity needs to replicate this via `Addressables.LoadAssetAsync<Texture2D>` and then enqueue a
    // main-thread update. [PORTING_HAZARD:P2] Blocking I/O and wxWidgets image decoding happen on the GUI thread here, which conflicts with
    // Unity's asynchronous load model. [UNITY] Equivalent to caching textures keyed by `<name>-wX-hY-gs` inside a `Dictionary<string,
    // Texture2D>` with `TextureImporter` resizing.
    std::string bitmap_key = bitmap_name + (height != 0 ? "-h" + std::to_string(height) : "-w" + std::to_string(width)) +
                             (grayscale ? "-gs" : "");

    auto it = m_map.find(bitmap_key);
    if (it != m_map.end())
        return it->second;

    wxImage image;
    if (!image.LoadFile(Slic3r::GUI::from_u8(Slic3r::var(bitmap_name + ".png")), wxBITMAP_TYPE_PNG) || image.GetWidth() == 0 ||
        image.GetHeight() == 0)
        return nullptr;

    if (height == 0 && width == 0)
        height = image.GetHeight();

    if (height != 0 && unsigned(image.GetHeight()) != height)
        width = unsigned(0.5f + float(image.GetWidth()) * height / image.GetHeight());
    else if (width != 0 && unsigned(image.GetWidth()) != width)
        height = unsigned(0.5f + float(image.GetHeight()) * width / image.GetWidth());

    if (height != 0 && width != 0) {
        // BBS: support resize by fill border
        if (scale_in_center > 0)
            image.Resize({(int) width, (int) height}, {(int) (width - image.GetWidth()) / 2, (int) (height - image.GetHeight()) / 2});
        else
            image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);
    }

    if (grayscale)
        image = image.ConvertToGreyscale(m_gs, m_gs, m_gs);

    return this->insert(bitmap_key, wxImage_to_wxBitmap_with_alpha(std::move(image)));
}

NSVGimage* BitmapCache::nsvgParseFromFileWithReplace(const char*                               filename,
                                                     const char*                               units,
                                                     float                                     dpi,
                                                     const std::map<std::string, std::string>& replaces)
{
    // [INTENT] Load and preprocess SVG data so the cache can rasterize theme-aware icons with forced replacements.
    // [PORTING_HAZARD:P2] The manual fopen/fread/malloc chain must be replaced with Unity's file API, and the Vector Graphics package needs
    // to parse the SVG. [THREAD] This I/O path blocks, so Unity must offload it and only marshal results back to the render thread.
    std::string str;
    FILE*       fp = NULL;
    size_t      size;
    char*       data  = NULL;
    NSVGimage*  image = NULL;

    fp = boost::nowide::fopen(filename, "rb");
    if (!fp)
        goto error;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = (char*) malloc(size + 1);
    if (data == NULL)
        goto error;
    if (fread(data, 1, size, fp) != size)
        goto error;
    data[size] = '\0'; // Must be null terminated.
    fclose(fp);

    if (replaces.empty())
        image = nsvgParse(data, units, dpi);
    else {
        str.assign(data);
        for (auto val : replaces)
            boost::replace_all(str, val.first, val.second);
        image = nsvgParse(str.data(), units, dpi);
    }
    free(data);
    return image;

error:
    if (fp)
        fclose(fp);
    if (data)
        free(data);
    if (image)
        nsvgDelete(image);
    return NULL;
}

wxBitmap* BitmapCache::load_svg(const std::string& bitmap_name,
                                unsigned           target_width,
                                unsigned           target_height,
                                const bool         grayscale /* = false*/,
                                const bool         dark_mode /* = false*/,
                                const std::string& new_color /*= ""*/,
                                const float        scale_in_center /* = 0*/)
{
    // [INTENT] Rasterize vector icons once per variant so the cache always serves matching textures for the UI.
    // [STATE] Keys include scale, dark mode, grayscale, and new color overrides, ensuring each variant has a unique cache entry.
    // [UNITY] In Unity this maps to Vector Graphics `SceneInfo` + `Sprite` creation stored in a `Dictionary<string, Sprite>` keyed by variant
    // metadata. [PORTING_HAZARD:P2] Replacing colors via string substitution needs to be rethought as shader/material overrides in Unity.

    std::string bitmap_key = bitmap_name +
                             (target_height != 0 ? "-h" + std::to_string(target_height) : "-w" + std::to_string(target_width)) +
                             (m_scale != 1.0f ? "-s" + float_to_string_decimal_point(m_scale) : "") + (dark_mode ? "-dm" : "") +
                             (grayscale ? "-gs" : "") + new_color;

    auto it = m_map.find(bitmap_key);
    if (it != m_map.end())
        return it->second;

    // map of color replaces
    std::map<std::string, std::string> replaces;
    replaces["\"#0x00AE42\""] = "\"#009688\"";
    replaces["\"#00FF00\""]   = "\"#52c7b8\"";
    if (dark_mode) {
        replaces["\"#262E30\""] = "\"#EFEFF0\"";
        replaces["\"#323A3D\""] = "\"#B3B3B5\"";
        replaces["\"#808080\""] = "\"#818183\"";
        // replaces["\"#ACACAC\""] = "\"#54545A\"";
        replaces["\"#CECECE\""] = "\"#54545B\"";
        replaces["\"#6B6B6B\""] = "\"#818182\"";
        replaces["\"#909090\""] = "\"#FFFFFF\"";
        replaces["\"#00FF00\""] = "\"#FF0000\"";
        replaces["\"#009688\""] = "\"#00675b\"";
        replaces["\"#F1F1F1\""] = "\"#36363B\"";
        replaces["#DBDBDB"]     = "#4A4A51"; // ORCA border color
        replaces["#F0F0F1"]     = "#333337"; // ORCA disabled background color
        replaces["#262E30"]     = "#EFEFF0"; // ORCA
    } else {
        replaces["#949494"] = "#7C8282"; // ORCA replace icon line color for light theme
    }

    if (strstr(bitmap_name.c_str(), "toggle_on") != NULL && dark_mode) // ORCA only replace color of toggle button
        replaces["#009688"] = "#00675b";

    if (!new_color.empty())
        replaces["\"#009688\""] = "\"" + new_color + "\"";

    NSVGimage* image = nullptr;
    if (strstr(bitmap_name.c_str(), "printer_thumbnail") == NULL) {
        image = nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, replaces);
    } else {
        std::map<std::string, std::string> temp_replaces;
        image = nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, temp_replaces);
    }

    if (image == nullptr)
        return nullptr;

    if (target_height == 0 && target_width == 0)
        target_height = image->height;

    target_height != 0 ? target_height *= m_scale : target_width *= m_scale;

    float svg_scale = target_height != 0 ? (float) target_height / image->height :
                      target_width != 0  ? (float) target_width / image->width :
                                           1;

    int width    = (int) (svg_scale * image->width + 0.5f);
    int height   = (int) (svg_scale * image->height + 0.5f);
    int n_pixels = width * height;
    if (n_pixels <= 0) {
        ::nsvgDelete(image);
        return nullptr;
    }

    NSVGrasterizer* rast = ::nsvgCreateRasterizer();
    if (rast == nullptr) {
        ::nsvgDelete(image);
        return nullptr;
    }

    std::vector<unsigned char> data(n_pixels * 4, 0);
    // BBS: support resize by fill border
    if (scale_in_center > 0 && scale_in_center < svg_scale) {
        int w = (int) (image->width * scale_in_center);
        int h = (int) (image->height * scale_in_center);
        ::nsvgRasterize(rast, image, 0, 0, scale_in_center, data.data() + int(height - h) / 2 * width * 4 + int(width - w) / 2 * 4, w, h,
                        width * 4);
    } else
        ::nsvgRasterize(rast, image, 0, 0, svg_scale, data.data(), width, height, width * 4);
    ::nsvgDeleteRasterizer(rast);
    ::nsvgDelete(image);

    return this->insert_raw_rgba(bitmap_key, width, height, data.data(), grayscale);
}

wxBitmap* BitmapCache::load_svg2(const std::string&              bitmap_name,
                                 unsigned                        target_width,
                                 unsigned                        target_height,
                                 const bool                      grayscale /* = false*/,
                                 const bool                      dark_mode /* = false*/,
                                 const std::vector<std::string>& array_new_color /*= vector<std::string>()*/,
                                 const float                     scale_in_center /* = 0*/)
{
    // [INTENT] Special-case SVG loader that can recolor two elements simultaneously for themed icons.
    // [UNITY] Replace this with Vector Graphics + shader tinting or material overrides when recreating Bayer icons.
    // [PORTING_HAZARD:P3] The two-color override loop will need careful mapping to Unity materials since the Vector Graphics importer does
    // not expose per-node replacements.

    std::map<std::string, std::string> replaces;
    if (array_new_color.size() == 2) {
        replaces["#D9D9D9"]            = array_new_color[0];
        replaces["fill-opacity=\"1.0"] = array_new_color[1];
    }

    NSVGimage* image = nullptr;
    image            = nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, replaces);

    if (image == nullptr)
        return nullptr;

    if (target_height == 0 && target_width == 0)
        target_height = image->height;

    target_height != 0 ? target_height *= m_scale : target_width *= m_scale;

    float svg_scale = target_height != 0 ? (float) target_height / image->height :
                      target_width != 0  ? (float) target_width / image->width :
                                           1;

    int width    = (int) (svg_scale * image->width + 0.5f);
    int height   = (int) (svg_scale * image->height + 0.5f);
    int n_pixels = width * height;
    if (n_pixels <= 0) {
        ::nsvgDelete(image);
        return nullptr;
    }

    NSVGrasterizer* rast = ::nsvgCreateRasterizer();
    if (rast == nullptr) {
        ::nsvgDelete(image);
        return nullptr;
    }

    std::vector<unsigned char> data(n_pixels * 4, 0);
    // BBS: support resize by fill border
    if (scale_in_center > 0 && scale_in_center < svg_scale) {
        int w = (int) (image->width * scale_in_center);
        int h = (int) (image->height * scale_in_center);
        ::nsvgRasterize(rast, image, 0, 0, scale_in_center, data.data() + int(height - h) / 2 * width * 4 + int(width - w) / 2 * 4, w, h,
                        width * 4);
    } else
        ::nsvgRasterize(rast, image, 0, 0, svg_scale, data.data(), width, height, width * 4);
    ::nsvgDeleteRasterizer(rast);
    ::nsvgDelete(image);

    const unsigned char* raw_data = data.data();
    wxImage              wx_image(width, height);
    wx_image.InitAlpha();
    unsigned char* rgb    = wx_image.GetData();
    unsigned char* alpha  = wx_image.GetAlpha();
    unsigned int   pixels = width * height;
    for (unsigned int i = 0; i < pixels; ++i) {
        *rgb++   = *raw_data++;
        *rgb++   = *raw_data++;
        *rgb++   = *raw_data++;
        *alpha++ = *raw_data++;
    }

    if (grayscale)
        wx_image = wx_image.ConvertToGreyscale(m_gs, m_gs, m_gs);
    auto result = new wxBitmap(wxImage_to_wxBitmap_with_alpha(std::move(wx_image), m_scale));
    return result;
}

// we make scaled solid bitmaps only for the cases, when its will be used with scaled SVG icon in one output bitmap
wxBitmap BitmapCache::mksolid(size_t        width,
                              size_t        height,
                              unsigned char r,
                              unsigned char g,
                              unsigned char b,
                              unsigned char transparency,
                              bool          suppress_scaling /* = false*/,
                              size_t        border_width /*= 0*/,
                              bool          dark_mode /* = false*/)
{
    // [INTENT] Create solid color bitmaps once so panels can reuse them rather than repainting every time.
    // [UNITY] Equivalent to making a `Texture2D` with `SetPixels32` and caching it in a manager.

    double scale = suppress_scaling ? 1.0f : m_scale;
    width *= scale;
    height *= scale;

    wxImage image(width, height);
    image.InitAlpha();
    unsigned char* imgdata  = image.GetData();
    unsigned char* imgalpha = image.GetAlpha();
    for (size_t i = 0; i < width * height; ++i) {
        *imgdata++  = r;
        *imgdata++  = g;
        *imgdata++  = b;
        *imgalpha++ = transparency;
    }

    // Add border, make white/light spools easier to see
    if (border_width > 0) {
        // Restrict to width of image
        if (border_width > height)
            border_width = height - 1;
        if (border_width > width)
            border_width = width - 1;

        auto px_data = (uint8_t*) image.GetData();
        auto a_data  = (uint8_t*) image.GetAlpha();

        for (size_t x = 0; x < width; ++x) {
            for (size_t y = 0; y < height; ++y) {
                if (x < border_width || y < border_width || x >= (width - border_width) || y >= (height - border_width)) {
                    const size_t idx     = (x + y * width);
                    const size_t idx_rgb = (x + y * width) * 3;
                    px_data[idx_rgb] = px_data[idx_rgb + 1] = px_data[idx_rgb + 2] = dark_mode ? 245u : 110u;
                    a_data[idx]                                                    = 255u;
                }
            }
        }
    }

    return wxImage_to_wxBitmap_with_alpha(std::move(image), scale);
}

bool BitmapCache::parse_color(const std::string& scolor, unsigned char* rgb_out)
{
    // [INTENT] Decode #RRGGBBAA or #RRGGBB so code can set palette colors through strings.
    // [UNITY] Replace this with `ColorUtility.TryParseHtmlString` and copy the float values to `Color32`.

    if (scolor.size() == 9) {
        unsigned char rgba[4];
        parse_color4(scolor, rgba);
        rgb_out[0] = rgba[0];
        rgb_out[1] = rgba[1];
        rgb_out[2] = rgba[2];
        return true;
    }
    rgb_out[0] = rgb_out[1] = rgb_out[2] = 0;
    if (scolor.size() != 7 || scolor.front() != '#')
        return false;
    const char* c = scolor.data() + 1;
    for (size_t i = 0; i < 3; ++i) {
        int digit1 = hex_digit_to_int(*c++);
        int digit2 = hex_digit_to_int(*c++);
        if (digit1 == -1 || digit2 == -1)
            return false;
        rgb_out[i] = (unsigned char) (digit1 * 16 + digit2);
    }

    return true;
}

bool BitmapCache::parse_color4(const std::string& scolor, unsigned char* rgba_out)
{
    // [INTENT] Accept both RGB and RGBA hex strings and expose raw byte values for alpha overrides.
    // [PORTING_HAZARD:P3] Hex parsing assumes ASCII digits with uppercase letters, so Unity needs to sanitize inputs too.

    rgba_out[0] = rgba_out[1] = rgba_out[2] = 0;
    rgba_out[3]                             = 255;
    if ((scolor.size() != 7 && scolor.size() != 9) || scolor.front() != '#')
        return false;
    const char* c = scolor.data() + 1;
    for (size_t i = 0; i < scolor.size() / 2; ++i) {
        int digit1 = hex_digit_to_int(*c++);
        int digit2 = hex_digit_to_int(*c++);
        if (digit1 == -1 || digit2 == -1)
            return false;
        rgba_out[i] = (unsigned char) (digit1 * 16 + digit2);
    }
    return true;
}
// BBS Replace svg green with the specified colour
bool BitmapCache::load_from_svg_file_change_color(
    const std::string& filename, unsigned width, unsigned height, ImTextureID& texture_id, const char* hexColor)
{
    // [INTENT] Load an SVG, recolor a hard-coded green shape, and upload it as an OpenGL texture ID for ImGui use.
    // [OPENGL] Manages GL texture binding/state changes explicitly, so Unity needs to map this to `Texture2D` creation plus
    // `Graphics.CopyTexture` or `CommandBuffer`. [PORTING_HAZARD:P2] The raw `glGenTextures`/`glTexImage2D` pattern must be encapsulated in
    // a `GLTexture` wrapper and executed on the GL thread.

    NSVGimage* image = nsvgParseFromFile(filename.c_str(), "px", 96.0f);
    if (image == nullptr) {
        return false;
    }
    char temp_color[8];
    strncpy(temp_color, hexColor, 7);
    temp_color[7]             = '\0';
    unsigned int change_color = nsvg__parseColorHex(temp_color);
    change_color |= (unsigned int) (1.0f * 255) << 24; // opacity
    unsigned int green_color = 0xFF889600;             // #009688
    for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
        // find green color
        if (shape->fill.color == green_color) {
            shape->fill.color = change_color;
        }
    }

    float scale = (float) width / image->width;

    int n_pixels = width * height;

    if (n_pixels <= 0) {
        nsvgDelete(image);
        return false;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast == nullptr) {
        nsvgDelete(image);
        return false;
    }
    std::vector<unsigned char> data(n_pixels * 4, 0);
    nsvgRasterize(rast, image, 0, 0, scale, data.data(), width, height, width * 4);

    bool           compress = false;
    GLint          last_texture;
    unsigned       m_image_texture{0};
    unsigned char* pixels = (unsigned char*) (&data[0]);

    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_image_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_image_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    texture_id = (ImTextureID) (intptr_t) m_image_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    return true;
}
}} // namespace Slic3r::GUI
