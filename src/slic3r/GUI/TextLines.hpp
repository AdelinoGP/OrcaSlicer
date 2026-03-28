#ifndef slic3r_TextLines_hpp_
#define slic3r_TextLines_hpp_

#include <vector>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/Emboss.hpp>
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"

namespace Slic3r {
class ModelVolume;
typedef std::vector<ModelVolume*> ModelVolumePtrs;
} // namespace Slic3r

namespace Slic3r::GUI {
// [INTENT] TextLinesModel is the declaration boundary for embossed-text preview data: it owns the chosen contour list and the reusable
// GLModel cache, while the cpp rebuilds the geometry from sliced model volumes. [STATE] `m_lines` is the selected contour per text row;
// `m_model` holds the cached preview mesh so repeated renders do not reslice. [THREAD] The heavy contour extraction/mesh assembly happens
// in `init()`'s implementation and should stay off the UI thread in Unity. [UNITY] Model this as a ScriptableObject-backed geometry cache
// plus a worker/job service that emits a finished Mesh for a render-only view. [PORTING_HAZARD:P2] The class mixes preview-cache ownership
// with geometry creation, so a Unity port should split the cache from the builder/service.
class TextLinesModel
{
public:
    /// <summary>
    /// Initialize model and lines
    /// </summary>
    /// <param name="text_tr">Transformation of text volume inside object (aka inside of instance)</param>
    /// <param name="volumes_to_slice">Vector of volumes to be sliced</param>
    /// <param name="style_manager">Contain Font file, size and align</param>
    /// <param name="count_lines">Count lines of embossed text(for veritcal alignment)</param>
    // [INTENT] Rebuild the contour cache and preview mesh from the active font, selected volumes, and line count.
    // [THREAD] This is CPU-heavy and slices model volumes; Unity should run it as a background job/coroutine and marshal results back.
    void init(const Transform3d&              text_tr,
              const ModelVolumePtrs&          volumes_to_slice,
              /*const*/ Emboss::StyleManager& style_manager,
              unsigned                        count_lines);

    // [INTENT] Draw the cached preview geometry in the text's world transform.
    // [UNITY] Keep rendering separate from geometry generation: a MeshRenderer/custom material path should consume the cached mesh only.
    void render(const Transform3d& text_world);

    // [STATE] `is_init()` reflects whether the cached preview mesh is valid.
    bool is_init() const { return m_model.is_initialized(); }
    // [STATE] Reset clears both the selected contours and the cached mesh so the next init starts from a clean preview state.
    void reset()
    {
        m_model.reset();
        m_lines.clear();
    }
    // [STATE] Read-only access for downstream UI and geometry consumers that need the selected contour rows.
    const Slic3r::Emboss::TextLines& get_lines() const { return m_lines; }

    // [INTENT] Convert font metrics into the embossed line spacing used by the slicer preview.
    // [UNITY] Keep this as a pure helper so layout math can be shared without depending on the render/cache lifetime.
    static double calc_line_height_in_mm(const Slic3r::Emboss::FontFile& ff, const FontProp& fp); // return lineheight in mm
private:
    Slic3r::Emboss::TextLines m_lines;

    // [STATE] Cached GLModel preview for the visualization mesh; it is rebuilt by init() and consumed by render().
    GLModel m_model;

    // [STATE] Vertical offset used to center text rows during slicing; this mirrors Emboss.cpp's ascent placement heuristic.
    const double ascent_ratio_offset = 1 / 3.;
};

} // namespace Slic3r::GUI
#endif // slic3r_TextLines_hpp_
