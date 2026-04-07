// [INTENT] Declares the emboss preview job that rasterizes each font style into one atlas so the UI can show style chips without
// blocking the main thread.
// [STATE] The job owns the caller-provided style/image request, tracks the atlas dimensions, and caches both raw RGBA pixels and per-style
// UV descriptors until finalize() publishes the result.
// [THREAD] `process()` runs on the worker thread to build geometry/raster data, while `finalize()` returns to the UI/GL thread to create the
// OpenGL texture consumed by the emboss UI.
// [UNITY] Port this as a two-stage pipeline: a background Task/Job computes glyph atlas bytes, then a main-thread controller creates a
// `Texture2D` plus sprite/UV metadata for the retained emboss-style picker.
// [PORTING_HAZARD:P2] The current contract depends on OpenGL texture creation in `finalize()` and a shared glyph cache reachable from the
// Orca worker system, so Unity will need explicit main-thread marshalling and a replacement font rasterization path.
#ifndef slic3r_CreateFontStyleImagesJob_hpp_
#define slic3r_CreateFontStyleImagesJob_hpp_

#include <vector>
#include <string>
#include <libslic3r/Emboss.hpp>
#include "slic3r/Utils/EmbossStyleManager.hpp"
#include "Job.hpp"

namespace Slic3r::GUI::Emboss {

/// <summary>
/// Create texture with name of styles written by its style
/// NOTE: Access to glyph cache is possible only from job
/// </summary>
class CreateFontStyleImagesJob : public Job
{
    // [STATE] Input bundle describing which font styles to render and where the finished atlas metadata should be written.
    StyleManager::StyleImagesData m_input;

    // Output data
    // texture size
    int m_width, m_height;
    // texture data
    std::vector<unsigned char> m_pixels;
    // descriptors of sub textures
    std::vector<StyleManager::StyleImage> m_images;

public:
    // [EVENT] Constructed on the UI side before dispatch into the shared background worker.
    CreateFontStyleImagesJob(StyleManager::StyleImagesData&& input);
    // [THREAD] Worker-thread stage that rasterizes each requested style and packs the atlas bookkeeping.
    void process(Ctl& ctl) override;
    // [OPENGL][THREAD] UI/GL-thread stage that uploads the atlas and publishes the completed style images back to the caller.
    void finalize(bool canceled, std::exception_ptr&) override;
};

} // namespace Slic3r::GUI::Emboss

#endif // slic3r_CreateFontStyleImagesJob_hpp_
