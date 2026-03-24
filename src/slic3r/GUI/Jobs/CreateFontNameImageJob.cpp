#include "CreateFontNameImageJob.hpp"

#include "libslic3r/Emboss.hpp"
// rasterization of ExPoly
#include "libslic3r/SLA/AGGRaster.hpp"

#include "slic3r/Utils/WxFontUtils.hpp"
#include "slic3r/GUI/3DScene.hpp" // ::glsafe

// ability to request new frame after finish rendering
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include "wx/fontenum.h"

#include <boost/log/trivial.hpp>

using namespace Slic3r;
using namespace Slic3r::GUI;

// [INTENT] Background job that turns a font name + user text into a bitmap fragment for the GUI texture atlas.
// [STATE] `FontImageData` (text, texture slot, gray level, cancel flags) binds the UI request data this job honors.

const std::string CreateFontImageJob::default_text = "AaBbCc 123";
// [STATE] fallback snippet used when user text is empty or invalid so the preview texture always contains glyph outlines.

CreateFontImageJob::CreateFontImageJob(FontImageData&& input) : m_input(std::move(input))
{
    // [INTENT] Ensure the request parameters are sane before worker threads execute any runtime behavior.
    assert(wxFontEnumerator::IsValidFacename(m_input.font_name));
    assert(m_input.gray_level > 0 && m_input.gray_level < 255);
    assert(m_input.texture_id != 0);
}

void CreateFontImageJob::process(Ctl& ctl)
{
    // [INTENT] Worker thread body: rasterize the requested glyphs into `m_result` while respecting cancellation.
    // [THREAD] Runs on a background job queue; uses `Ctl` plus the shared `cancel` flag to synchronize with the UI.
    if (!wxFontEnumerator::IsValidFacename(m_input.font_name))
        return;
    // Select font
    wxFont wx_font(wxFontInfo().FaceName(m_input.font_name).Encoding(m_input.encoding));
    if (!wx_font.IsOk())
        return;

    std::unique_ptr<Emboss::FontFile> font_file = WxFontUtils::create_font_file(wx_font);
    if (font_file == nullptr)
        return;

    Emboss::FontFileWithCache font_file_with_cache(std::move(font_file));
    // use only first line of text
    std::string& text = m_input.text;
    if (text.empty())
        text = default_text; // copy

    size_t enter_pos = text.find('\n');
    if (enter_pos < text.size()) {
        // text start with enter
        if (enter_pos == 0)
            return;
        // exist enter, soo delete all after enter
        text = text.substr(0, enter_pos);
    }

    // [THREAD] Double-check both the worker controller and shared cancel flag so UI aborts propagate before expensive raster work.
    std::function<bool()> was_canceled = [&ctl, cancel = m_input.cancel]() -> bool {
        if (ctl.was_canceled())
            return true;
        if (cancel->load())
            return true;
        return false;
    };

    FontProp fp; // create default font parameters
    // [INTENT] Build vector outlines from font glyphs; the cancel lambda ensures we can abort before expensive rasterization.
    ExPolygons shapes = Emboss::text2shapes(font_file_with_cache, text.c_str(), fp, was_canceled);

    // select some character from font e.g. default text
    if (shapes.empty())
        shapes = Emboss::text2shapes(font_file_with_cache, default_text.c_str(), fp, was_canceled);
    if (shapes.empty()) {
        m_input.cancel->store(true);
        return;
    }

    // normalize height of font
    BoundingBox bounding_box;
    // [STATE] compute the tight bounds so we can scale the glyphs into the requested texture height.
    for (const ExPolygon& shape : shapes)
        bounding_box.merge(BoundingBox(shape.contour.points));
    if (bounding_box.size().x() < 1 || bounding_box.size().y() < 1) {
        m_input.cancel->store(true);
        return;
    }
    double       scale = m_input.size.y() / (double) bounding_box.size().y();
    BoundingBoxf bb2(bounding_box.min.cast<double>(), bounding_box.max.cast<double>());
    bb2.scale(scale);
    Vec2d size_f = bb2.size();
    // [STATE] `m_tex_size` is cropped to ensure the glyph fits inside the UI atlas entry.
    m_tex_size = Point(std::ceil(size_f.x()), std::ceil(size_f.y()));
    // crop image width
    if (m_tex_size.x() > m_input.size.x())
        m_tex_size.x() = m_input.size.x();
    if (m_tex_size.y() > m_input.size.y())
        m_tex_size.y() = m_input.size.y();

    // Set up result
    unsigned bit_count = 4; // RGBA
    // [STATE] `m_result` is prefilled with opaque white so any unsupported sections remain blank instead of stale data.
    m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * bit_count, {255});

    // [PORTING_HAZARD:P3] This depends on the Emboss + SLA rasterizers; Unity will need a CPU-path or compute shader to reproduce the same
    // anti-aliasing.
    sla::Resolution                  resolution(m_tex_size.x(), m_tex_size.y());
    double                           pixel_dim = SCALING_FACTOR / scale;
    sla::PixelDim                    dim(pixel_dim, pixel_dim);
    double                           gamma = 1.;
    std::unique_ptr<sla::RasterBase> r     = sla::create_raster_grayscale_aa(resolution, dim, gamma);
    for (ExPolygon& shape : shapes)
        shape.translate(-bounding_box.min);
    for (const ExPolygon& shape : shapes)
        r->draw(shape);

    // copy rastered data to pixels
    // [UNITY] In Unity this would mirror `byte[]` production consumed by `Texture2D.LoadRawTextureData` plus a main-thread `Apply()` call.
    sla::RasterEncoder encoder = [&pix = m_result, w = m_tex_size.x(), h = m_tex_size.y(),
                                  gray_level = m_input.gray_level](const void* ptr, size_t width, size_t height, size_t num_components) {
        size_t               size{static_cast<size_t>(w * h)};
        const unsigned char* ptr2 = (const unsigned char*) ptr;
        // [STATE] `gray_level` scales the brightness so user controls (contrast slider) reflect in the preview.
        for (size_t x = 0; x < width; ++x)
            for (size_t y = 0; y < height; ++y) {
                size_t index = y * w + x;
                assert(index < size);
                if (index >= size)
                    continue;
                pix[3 + 4 * index] = ptr2[y * width + x] / gray_level;
            }
        return sla::EncodedRaster();
    };
    r->encode(encoder);
}

void CreateFontImageJob::finalize(bool canceled, std::exception_ptr&)
{
    // [THREAD] finalize is invoked on the UI renderer thread so the OpenGL context is valid for texture uploads.
    // [OPENGL] This block mirrors a Direct GPU write; Unity should do `Texture2D.LoadRawTextureData + Apply()` on the main thread instead.
    if (m_input.count_opened_font_files)
        --(*m_input.count_opened_font_files);
    if (canceled || m_input.cancel->load())
        return;

    *m_input.is_created = true;

    // Exist result bitmap with preview?
    // (not valid input. e.g. not loadable font)
    if (m_result.empty()) {
        // TODO: write text cannot load into texture
        // [INTENT] White fallback to ensure GPU texture gets cleared instead of holding stale pixel data.
        m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * 4, {255});
    }

    // upload texture on GPU
    // [OPENGL] writes the job result into a shared texture atlas row; `yoffset` ties the slot to the job index.
    // [UNITY] In Unity replicate with a `Texture2D` atlas slice using `SetPixels` + `Apply` or `Graphics.CopyTexture`.
    // [PORTING_HAZARD:P2] Unity has no direct `glTexSubImage2D`, so keep the atlas management in C# (e.g., Texture2D + RenderTexture stack).
    const GLenum target = GL_TEXTURE_2D;
    glsafe(::glBindTexture(target, m_input.texture_id));

    GLsizei w = m_tex_size.x(), h = m_tex_size.y();
    GLint   xoffset = 0; // align to left
    // GLint xoffset = m_input.size.x() - m_tex_size.x(); // align right
    GLint yoffset = m_input.size.y() * m_input.index;
    glsafe(::glTexSubImage2D(target, m_input.level, xoffset, yoffset, w, h, m_input.format, m_input.type, m_result.data()));

    // clear rest of texture
    // [STATE] zeroing the unused portion prevents artifacts from previous font previews in the same row.
    std::vector<unsigned char> empty_data(xoffset * h * 4, {0});
    glsafe(::glTexSubImage2D(target, m_input.level, 0, yoffset, xoffset, h, m_input.format, m_input.type, empty_data.data()));

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // show rendered texture
    // [EVENT] request a redraw so the freshly uploaded texture appears in the UI; Unity needs to trigger its redraw job (e.g.,
    // `Camera.Render` or `Canvas` invalidation).
    wxGetApp().plater()->canvas3D()->schedule_extra_frame(0);

    // [EVENT] Trace logging for telemetry so we know which name previews were generated.
    BOOST_LOG_TRIVIAL(info) << "Generate Preview font('" << m_input.font_name << "' id:" << m_input.index << ") "
                            << "with text: '" << m_input.text << "' "
                            << "texture_size " << m_input.size.x() << " x " << m_input.size.y();
}
