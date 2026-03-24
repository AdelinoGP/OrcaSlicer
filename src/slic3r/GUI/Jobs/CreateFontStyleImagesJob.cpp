#include "CreateFontStyleImagesJob.hpp"

// rasterization of ExPoly
#include "libslic3r/SLA/AGGRaster.hpp"
#include "slic3r/GUI/3DScene.hpp" // ::glsafe

// ability to request new frame after finish rendering
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

using namespace Slic3r;
using namespace Slic3r::Emboss;
using namespace Slic3r::GUI;
using namespace Slic3r::GUI::Emboss;

CreateFontStyleImagesJob::CreateFontStyleImagesJob(StyleManager::StyleImagesData&& input)
    : m_input(std::move(input)), m_width(0), m_height(0)
{
    // [INTENT] Bundle the caller-provided text/styles into a rasterization job that yields a unified atlas for GUI previews.
    // [STATE] `m_input` owns text, style metadata, PPM, and the result container while `m_width`/`m_height` track atlas bounds.
    // [THREAD] Construction happens on the UI thread before dispatch to the background job queue so the main loop stays responsive.
    assert(m_input.result != nullptr);
    assert(!m_input.styles.empty());
    assert(!m_input.text.empty());
    assert(m_input.max_size.x() > 1);
    assert(m_input.max_size.y() > 1);
    assert(m_input.ppm > 1e-5);
}

void CreateFontStyleImagesJob::process(Ctl& ctl)
{
    // [INTENT] Convert each requested font style into polygons, then plan the atlas before touching GL.
    // [STATE] `name_shapes`, `scales`, and `m_images` are worker-thread scratch buffers that the job owns until finalize().
    // [THREAD] This method runs on a background worker, so it avoids GUI/GL calls and reports only through the result container.
    // create shapes and calc size (bounding boxes)
    std::vector<ExPolygons> name_shapes(m_input.styles.size());
    std::vector<double>     scales(m_input.styles.size());
    m_images = std::vector<StyleManager::StyleImage>(m_input.styles.size());

    auto was_canceled = []() { return false; };
    for (auto& item : m_input.styles) {
        size_t      index  = &item - &m_input.styles.front();
        ExPolygons& shapes = name_shapes[index];
        shapes             = text2shapes(item.font, m_input.text.c_str(), item.prop, was_canceled);

        // create image description
        StyleManager::StyleImage& image = m_images[index];
        // [STATE] Store each style’s bounding box so we can record geometry once (without yet touching GL).
        BoundingBox& bounding_box = image.bounding_box;
        for (const ExPolygon& shape : shapes)
            bounding_box.merge(BoundingBox(shape.contour.points));
        for (ExPolygon& shape : shapes)
            shape.translate(-bounding_box.min);

        // calculate conversion from FontPoint to screen pixels by size of font
        double scale  = get_text_shape_scale(item.prop, *item.font.font_file) * m_input.ppm;
        scales[index] = scale;
        // [STATE] `scales` ties glyph bounds back to real-world mm so Unity can compute `pixelsPerUnit` consistently.

        // double scale = font_prop.size_in_mm * SCALING_FACTOR;
        BoundingBoxf bb2(bounding_box.min.cast<double>(), bounding_box.max.cast<double>());
        bb2.scale(scale);
        image.tex_size.x = std::ceil(bb2.max.x() - bb2.min.x());
        image.tex_size.y = std::ceil(bb2.max.y() - bb2.min.y());

        // crop image width
        if (image.tex_size.x > m_input.max_size.x())
            image.tex_size.x = m_input.max_size.x();
        // crop image height
        if (image.tex_size.y > m_input.max_size.y())
            image.tex_size.y = m_input.max_size.y();
    }

    // arrange bounding boxes
    int offset_y = 0;
    m_width      = 0;
    for (StyleManager::StyleImage& image : m_images) {
        image.offset.y() = offset_y;
        offset_y += image.tex_size.y + 1;
        if (m_width < image.tex_size.x)
            m_width = image.tex_size.x;
    }
    m_height = offset_y;
    for (StyleManager::StyleImage& image : m_images) {
        const Point&  o = image.offset;
        const ImVec2& s = image.tex_size;
        // [STATE] UV0/UV1 normalize atlas slots so the renderer knows where to sample each glyph within the shared texture.
        image.uv0 = ImVec2(o.x() / (double) m_width, o.y() / (double) m_height);
        image.uv1 = ImVec2((o.x() + s.x) / (double) m_width, (o.y() + s.y) / (double) m_height);
    }

    // Set up result
    // [STATE] `m_pixels` becomes the RGBA atlas buffer that finalize() uploads to the GPU.
    m_pixels = std::vector<unsigned char>(4 * m_width * m_height, {255});

    // upload sub textures
    for (StyleManager::StyleImage& image : m_images) {
        sla::Resolution                  resolution(image.tex_size.x, image.tex_size.y);
        size_t                           index     = &image - &m_images.front();
        double                           pixel_dim = SCALING_FACTOR / scales[index];
        sla::PixelDim                    dim(pixel_dim, pixel_dim);
        double                           gamma = 1.;
        std::unique_ptr<sla::RasterBase> r     = sla::create_raster_grayscale_aa(resolution, dim, gamma);
        for (const ExPolygon& shape : name_shapes[index])
            r->draw(shape);

        // copy rastered data to pixels
        // [EVENT] This encoder lambda fires once per raster and writes into the shared atlas buffer for finalize() to consume.
        sla::RasterEncoder encoder = [&offset = image.offset, &pix = m_pixels, w = m_width,
                                      h = m_height](const void* ptr, size_t width, size_t height, size_t num_components) {
            // bigger value create darker image
            unsigned char gray_level = 1;
            size_t        size{static_cast<size_t>(w * h)};
            assert((offset.x() + width) <= (size_t) w);
            assert((offset.y() + height) <= (size_t) h);
            const unsigned char* ptr2 = (const unsigned char*) ptr;
            for (size_t x = 0; x < width; ++x)
                for (size_t y = 0; y < height; ++y) {
                    size_t index = (offset.y() + y) * w + offset.x() + x;
                    assert(index < size);
                    if (index >= size)
                        continue;
                    pix[4 * index + 3] = ptr2[y * width + x] / gray_level;
                }
            return sla::EncodedRaster();
        };
        r->encode(encoder);
    }
}

void CreateFontStyleImagesJob::finalize(bool canceled, std::exception_ptr&)
{
    if (canceled)
        return;
    // [THREAD] finalize runs back on the UI/GL thread after the worker completes so it can safely talk to OpenGL.
    // [OPENGL] This block owns the generated texture and uploads `m_pixels` into a normal 2D texture atlas.
    // [PORTING_HAZARD:P2] Unity will need to marshal this work through a `MainThreadDispatcher` before creating the Texture2D.
    // upload texture on GPU
    GLuint tex_id;
    GLenum target = GL_TEXTURE_2D, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    GLint  level = 0, border = 0;
    glsafe(::glGenTextures(1, &tex_id));
    glsafe(::glBindTexture(target, tex_id));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GLint w = m_width, h = m_height;
    glsafe(::glTexImage2D(target, level, GL_RGBA, w, h, border, format, type, (const void*) m_pixels.data()));
    // [UNITY] Equivalent job would create a `Texture2D` (RGBA32), call `SetPixels` with `m_pixels`, then `Apply()` on the main thread.

    // set up texture id
    void* texture_id = (void*) (intptr_t) tex_id;
    for (StyleManager::StyleImage& image : m_images)
        image.texture_id = texture_id;

    // move to result
    // [STATE] Move computed styles/images back to the requester so downstream UI draws from the atlas without copying.
    m_input.result->styles = std::move(m_input.styles);
    m_input.result->images = std::move(m_images);

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // show rendered texture
    // [EVENT] Tell the 3D canvas to repaint so the new atlas appears; this hooks into the render loop after the job finalizes.
    // [PORTING_HAZARD:P3] Unity will need to map this to its single render loop instead of calling `schedule_extra_frame` on wx canvases.
    // [UNITY] Unity would push `Canvas3D` to `RenderTexture` and trigger `camera.Render()` or a `Canvas.ForceUpdateCanvases()` call.
    wxGetApp().plater()->canvas3D()->schedule_extra_frame(0);
}
