// [INTENT] Declares FillConcentricInternal: the Arachne-based concentric infill variant
// used for support interface layers and other "internal" concentric regions.
//
// Key differences from FillConcentric:
// - Overrides `fill_surface_extrusion()` (not `_fill_surface_single()`). It produces
//   variable-width ExrusionEntities directly, not fixed-width Polylines.
// - Uses `Arachne::WallToolPaths` to generate the concentric loops with adaptive bead
//   widths, handling narrow regions that FillConcentric's fixed-offset approach cannot.
// - `no_sort()` returns true — the output ExrusionEntityCollection is pre-sorted by
//   Arachne's nearest-neighbour seam rotation; no further sorting is applied.
// - `clone()` is protected — only `Layer` (friend) can create instances.
// - No `CorrectionAngle`, `DensityAdjust`, or spacing heuristics — all width control
//   is delegated to Arachne via `WallToolPathsParams`.
//
// [COUPLING] Arachne::WallToolPaths (getToolPaths), VariableWidth (variable_width,
//            to_thick_polyline), ClipperUtils, ShortestPath (reorder_by_shortest_traverse).
// [STATE] Reads `this->no_overlap_expolygons` (set by the fill engine before call),
//         `this->print_config`, `this->print_object_config`, `this->loop_clipping`,
//         `this->spacing`.
// [CONCURRENCY] No shared mutable state. One instance per layer region. Thread-safe per call.
//
// [HAZARD H391] `no_rotation is supported for this infill pattern` — the comment in the
//   .cpp confirms rotation is unsupported. `expolygon.rotate()` is never called. Any
//   caller that sets `Fill::angle` to a non-zero value expects rotation; FillConcentricInternal
//   silently ignores it. Callers must not set non-zero angle for this fill type.
// [HAZARD H392] `fill_surface_extrusion()` bypasses the standard `_fill_surface_single()`
//   pipeline. It does NOT call `multiline_fill()`, `connect_infill()`, or `intersection_pl()`.
//   A port that routes FillConcentricInternal through the standard pipeline will produce
//   incorrect results. The Arachne path is a completely separate code path.

#ifndef slic3r_FillConcentricInternal_hpp_
#define slic3r_FillConcentricInternal_hpp_

#include "FillBase.hpp"

namespace Slic3r {

// [INTENT] FillConcentricInternal: Arachne-based variable-width concentric infill.
// Generates concentric rings using `WallToolPaths`, yielding adaptive bead widths
// for narrow regions. Output is `ThickPolyline` → `variable_width()` → ExtrusionEntities.
// Used for support interface layers and similar thin-shell regions.
class FillConcentricInternal : public Fill
{
public:
    ~FillConcentricInternal() override = default;

    // [INTENT] Main entry point. Produces variable-width extrusion entities directly,
    // bypassing the standard Polyline-based fill pipeline.
    // See .cpp for full hazard annotations.
    void fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out) override;

    bool is_self_crossing() override { return false; }

protected:
    // [INTENT] clone() is protected — FillConcentricInternal instances are only created
    // by the fill engine; no user-facing factory access.
    Fill* clone() const override { return new FillConcentricInternal(*this); };

    // [INTENT] no_sort() = true — Arachne's nearest-neighbour seam rotation pre-orders
    // the output. Returning true tells the outer fill engine not to re-sort.
    // [HAZARD H393] If the fill engine is changed to always sort regardless of no_sort(),
    //   the pre-ordered Arachne output will be re-permuted, breaking seam placement.
    bool no_sort() const override { return true; }

    // [INTENT] friend Layer — allows Layer to construct FillConcentricInternal directly.
    friend class Layer;
};

} // namespace Slic3r

#endif // slic3r_FillConcentricInternal_hpp_
