#ifndef SLIC3R_GUI_BITMAP_CACHE_HPP
#define SLIC3R_GUI_BITMAP_CACHE_HPP

#include <map>
#include <vector>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <imgui/imgui.h>

#include "libslic3r/Color.hpp"
struct NSVGimage;

namespace Slic3r { namespace GUI {

// [INTENT] Centralized store for themed wxBitmaps so GUI widgets share Retina-aware textures and generated overlays.
// [STATE] Owns the name -> wxBitmap map plus the grayscale (`m_gs`) and scale (`m_scale`) hints so lookups stay consistent across windows.
// [UNITY] Mirrors a `Dictionary<string, Texture2D>` + pooling MonoBehaviour to keep textures alive on the main thread without leaking GPU memory.
class BitmapCache
{
public:
    BitmapCache();
    ~BitmapCache() { clear(); }
    // [INTENT] Release cached textures before the GL context tears down so wxWidgets can recreate them safely.
    void clear();
    // [STATE] Exposes the highest detected scale so consumers reuse Retina-sized icons without requerying each window.
    double scale() { return m_scale; }

    // [STATE] One-shot GUI-thread lookups keep the returned bitmap stable for the calling widget.
    wxBitmap* find(const std::string& name)
    {
        auto it = m_map.find(name);
        return (it == m_map.end()) ? nullptr : it->second;
    }
    const wxBitmap* find(const std::string& name) const { return const_cast<BitmapCache*>(this)->find(name); }

    // [INTENT] Allocate or resize a cached bitmap entry so repeated resizes reuse GPU memory instead of thrashing the renderer.
    // [THREAD] Must stay on the GUI thread because wxWidgets bitmap creation is not thread safe.
    // [UNITY] Mirror this with `Texture2D.Resize` inside a pooling controller.
    wxBitmap* insert(const std::string& name, size_t width, size_t height);
    wxBitmap* insert(const std::string& name, const wxBitmap& bmp);
    wxBitmap* insert(const std::string& name, const wxBitmap& bmp, const wxBitmap& bmp2);
    wxBitmap* insert(const std::string& name, const wxBitmap& bmp, const wxBitmap& bmp2, const wxBitmap& bmp3);
    wxBitmap* insert(const std::string& name, const std::vector<wxBitmap>& bmps)
    {
        return this->insert(name, &bmps.front(), &bmps.front() + bmps.size());
    }
    wxBitmap* insert(const std::string& name, const wxBitmap* begin, const wxBitmap* end);
    // [INTENT] Accept raw RGBA bytes so dynamically generated overlays can reuse GPU memory consistently.
    // [UNITY] Map to Texture2D.LoadRawTextureData + Apply stored inside a shared Dictionary<string, Texture2D>.
    wxBitmap* insert_raw_rgba(
        const std::string& bitmap_key, unsigned width, unsigned height, const unsigned char* raw_data, const bool grayscale = false);

    // BBS: support resize by fill border  (scale_in_center)
    // Load png from resources/icons. bitmap_key is given without the .png suffix. Bitmap will be rescaled to provided height/width if
    // nonzero.
    // [INTENT] Decode PNG assets once per descriptor and reuse the decoded bitmap across the UI.
    // [THREAD] Performs blocking disk reads on the GUI thread, so Unity must copy the descriptor and continue on an async path before
    // returning to the main loop.
    // [PORTING_HAZARD:P2] Blocking I/O + wxWidgets image decoding contradict Unity's async asset model.
    // [UNITY] Replace with Addressables.LoadAssetAsync<Texture2D> plus a main-thread dispatcher that caches results keyed by <name>-wX-hY-gs.
    wxBitmap* load_png(const std::string& bitmap_key,
                       unsigned           width           = 0,
                       unsigned           height          = 0,
                       const bool         grayscale       = false,
                       const float        scale_in_center = 0.f);

    // [INTENT] Preprocess SVG blobs so themed icons can swap colors before rasterizing.
    // [THREAD] Blocks on disk I/O on the GUI thread, so Unity must marshal via async file APIs.
    // [PORTING_HAZARD:P2] Manual fopen/fread/malloc chains need replacement with Unity's file APIs + Vector Graphics parsing.
    static NSVGimage* nsvgParseFromFileWithReplace(const char*                               filename,
                                                   const char*                               units,
                                                   float                                     dpi,
                                                   const std::map<std::string, std::string>& replaces);
    // Load svg from resources/icons. bitmap_key is given without the .svg suffix. SVG will be rasterized to provided height/width.
    // [INTENT] Rasterize SVG icons into cached bitmaps featuring scale, dark mode, and color overrides.
    // [STATE] Composite cache keys lock in scale, dark mode, grayscale, and color override metadata.
    // [PORTING_HAZARD:P2] Color replacements rely on string substitution; Unity must replace them with shader/material overrides or Vector
    // Graphics edits. [UNITY] Replace with Vector Graphics `SceneInfo` + `Sprite` creation stored in a `Dictionary<string, Sprite>` keyed
    // by variant metadata.
    wxBitmap* load_svg(const std::string& bitmap_key,
                       unsigned           width           = 0,
                       unsigned           height          = 0,
                       const bool         grayscale       = false,
                       const bool         dark_mode       = false,
                       const std::string& new_color       = "",
                       const float        scale_in_center = 0.f);
    // Load background image of semi transparent material with color,
    // [INTENT] Provide a special-case SVG loader that recolors multiple elements for themed icons.
    // [UNITY] Replace this with Vector Graphics + shader/material tint overrides so Unity can drive two-color variants.
    // [PORTING_HAZARD:P3] Per-node color replacements must become shader inputs because the Vector Graphics importer lacks node-level hooks.
    wxBitmap* load_svg2(const std::string&              bitmap_key,
                        unsigned                        width           = 0,
                        unsigned                        height          = 0,
                        const bool                      grayscale       = false,
                        const bool                      dark_mode       = false,
                        const std::vector<std::string>& array_new_color = std::vector<std::string>(),
                        const float                     scale_in_center = 0.0f);

    // [INTENT] Create solid-color bitmaps once so panels reuse them rather than repainting every frame.
    // [UNITY] Replace with Texture2D creation + SetPixels32 cached by a color/material manager.
    wxBitmap mksolid(size_t        width,
                     size_t        height,
                     unsigned char r,
                     unsigned char g,
                     unsigned char b,
                     unsigned char transparency,
                     bool          suppress_scaling = false,
                     size_t        border_width     = 0,
                     bool          dark_mode        = false);
    wxBitmap mksolid(size_t              width,
                     size_t              height,
                     const unsigned char rgb[3],
                     bool                suppress_scaling = false,
                     size_t              border_width     = 0,
                     bool                dark_mode        = false)
    {
        return mksolid(width, height, rgb[0], rgb[1], rgb[2], wxALPHA_OPAQUE, suppress_scaling, border_width, dark_mode);
    }
    wxBitmap mksolid(
        size_t width, size_t height, const ColorRGB& rgb, bool suppress_scaling = false, size_t border_width = 0, bool dark_mode = false)
    {
        return mksolid(width, height, rgb.r_uchar(), rgb.g_uchar(), rgb.b_uchar(), wxALPHA_OPAQUE, suppress_scaling, border_width,
                       dark_mode);
    }
    wxBitmap mkclear(size_t width, size_t height) { return mksolid(width, height, 0, 0, 0, wxALPHA_TRANSPARENT); }

    // [INTENT] Decode #RRGGBB and #RRGGBBAA strings so UI theming code can pass color overrides as hex.
    // [UNITY] Replace with ColorUtility.TryParseHtmlString and copy the resulting Color32 into your texture/material controller.
    static bool parse_color(const std::string& scolor, unsigned char* rgb_out);
    // [INTENT] Accept both RGB and RGBA hex strings and expose raw byte values for alpha overrides.
    // [PORTING_HAZARD:P3] Hex parsing assumes ASCII uppercase letters, so Unity must sanitize inputs too.
    static bool parse_color4(const std::string& scolor, unsigned char* rgba_out);

    // [INTENT] Load an SVG, tint one shape, and upload it as an OpenGL texture for ImGui widgets.
    // [OPENGL] Binds raw textures and issues glTexImage2D, so Unity has to wrap this flow inside Texture2D/CommandBuffer helpers on the GL
    // thread. [PORTING_HAZARD:P2] The raw glGenTextures/glTexImage2D pattern needs encapsulation behind a GLTexture helper executed on the
    // GPU thread.
    static bool load_from_svg_file_change_color(
        const std::string& filename, unsigned width, unsigned height, ImTextureID& texture_id, const char* hexColor);

private:
    // [STATE] Cache map that owns every wxBitmap so the UI shares textures instead of rebuilding them per widget.
    std::map<std::string, wxBitmap*> m_map;
    // [STATE] Shared grayscale conversion weight so dark-mode icons stay consistent in all panels.
    double m_gs = 0.2; // value, used for image.ConvertToGreyscale(m_gs, m_gs, m_gs)
    // [STATE] Global scale factor ensures every icon respects Retina scaling without re-probing each window.
    double m_scale = 1.0; // value, used for correct scaling of SVG icons on Retina display
};

}} // namespace Slic3r::GUI

#endif // SLIC3R_GUI_BITMAP_CACHE_HPP
