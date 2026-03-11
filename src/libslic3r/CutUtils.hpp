#ifndef slic3r_CutUtils_hpp_
#define slic3r_CutUtils_hpp_

#include "enum_bitmask.hpp"
#include "Point.hpp"
#include "Model.hpp"

#include <vector>

namespace Slic3r {

// [INTENT] High-level split/cut orchestration helpers used by GUI "Cut" workflows.
//           This API wraps ModelObject slicing operations (plane, contour, groove)
//           and returns new ModelObject fragments with optional post-processing.
// [COUPLING] Tight coupling to Model / ModelObject internals, Transform3d math,
//            and downstream mesh-cut implementations in CutUtils.cpp and CutSurface.
// [STATE] The Cut instance owns a temporary Model (`m_model`) used as a workspace.
//         Calls to perform_* mutate this workspace and object lists in-place.
// [MEMORY] Returned references point to containers owned by `m_model`; callers must
//          not outlive the Cut object. Destructor explicitly clears model objects.
// [CONCURRENCY] Not thread-safe: mutable members (`m_model`, attribute flags) are
//               mutated through perform_* and post_process without synchronization.
using ModelObjectPtrs = std::vector<ModelObject*>;

enum class ModelObjectCutAttribute : int {
    KeepUpper,
    KeepLower,
    KeepAsParts,
    FlipUpper,
    FlipLower,
    PlaceOnCutUpper,
    PlaceOnCutLower,
    CreateDowels,
    InvalidateCutInfo
};
using ModelObjectCutAttributes = enum_bitmask<ModelObjectCutAttribute>;
ENABLE_ENUM_BITMASK_OPERATORS(ModelObjectCutAttribute);

class Cut
{
    // [STATE] Workspace model cloned/derived from the source object; persists for the
    //         lifetime of this Cut command so chained post-processing can reuse it.
    Model m_model;
    // [STATE] Instance index selects which ModelInstance transform to cut against.
    int m_instance;
    // [INTENT] Cutting frame: plane/contour is expressed in this coordinate system.
    const Transform3d m_cut_matrix;
    // [INTENT] Bitmask driving which fragments survive and how they are transformed.
    ModelObjectCutAttributes m_attributes;

    void post_process(ModelObject* object, ModelObjectPtrs& objects, bool keep, bool place_on_cut, bool flip);
    void post_process(ModelObject* upper_object, ModelObject* lower_object, ModelObjectPtrs& objects);
    void finalize(const ModelObjectPtrs& objects);

public:
    Cut(const ModelObject*       object,
        int                      instance,
        const Transform3d&       cut_matrix,
        ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                              ModelObjectCutAttribute::KeepAsParts);
    // [MEMORY] Explicit clear guards against stale object ownership when Cut is a
    // short-lived command helper repeatedly constructed by UI actions.
    ~Cut() { m_model.clear_objects(); }

    struct Groove
    {
        // [INTENT] Parametric groove profile for joinery cuts: depth/width define
        //          the notch body; flap fields define side taper and mating slack.
        // [HAZARD] Floats are compared/consumed with tolerances in implementation;
        //          small unit-conversion mistakes can create non-manifold fragments.
        float depth{0.f};
        float width{0.f};
        float flaps_angle{0.f};
        float angle{0.f};
        float depth_init{0.f};
        float width_init{0.f};
        float flaps_angle_init{0.f};
        float angle_init{0.f};
        float depth_tolerance{0.1f};
        float width_tolerance{0.1f};
    };

    struct Part
    {
        // [STATE] UI selection state copied into compute stage to decide per-part
        //         retention in contour-based cuts.
        bool selected;
        bool is_modifier;
    };

    // [INTENT] Plane-based bisection path (single split plane).
    const ModelObjectPtrs& perform_with_plane();
    // [INTENT] User-selected contour partitioning with optional dowel generation.
    const ModelObjectPtrs& perform_by_contour(std::vector<Part> parts, int dowels_count);
    // [INTENT] Joinery groove cut path; may keep parts independent for assembly.
    const ModelObjectPtrs& perform_with_groove(const Groove& groove, const Transform3d& rotation_m, bool keep_as_parts = false);

}; // namespace Cut

} // namespace Slic3r

#endif /* slic3r_CutUtils_hpp_ */
