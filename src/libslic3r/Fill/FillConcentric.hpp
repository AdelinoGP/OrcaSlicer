// [INTENT] Declares the FillConcentric class: inward-spiraling concentric ring infill.
// Two overloads of _fill_surface_single are provided:
//   1. Polylines overload — classic fixed-width concentric rings. Used for normal infill,
//      solid surfaces, and low-density concentric where Arachne is not needed.
//   2. ThickPolylines overload — Arachne variable-width concentric rings. Used when
//      params.use_arachne = true (solid surfaces that need gap-filling at thin regions).
//
// The fundamental algorithm is iterative inward offsetting of the ExPolygon contour.
// Each offset step shrinks all polygons by `distance` (= spacing / density), producing
// nested rings until no area remains. Rings are ordered outermost-first.
//
// [COUPLING] Depends on FillBase (multiline_fill, loop_clipping), ClipperUtils (offset2_ex,
//            union_pt_chained_outside_in), Arachne/WallToolPaths (Arachne overload only).
// [STATE] Stateless except for inherited Fill members (spacing, loop_clipping, print_config, etc.).
// [CONCURRENCY] No shared mutable state; safe to use from multiple TBB threads.

#ifndef slic3r_FillConcentric_hpp_
#define slic3r_FillConcentric_hpp_

#include "FillBase.hpp"

namespace Slic3r {

// [INTENT] Concentric ring infill. Rings spiral inward from the perimeter offset to the
// center of the ExPolygon. The outermost ring is closest to the perimeter; the innermost
// is the last ring that fits. This pattern provides good isotropy for top/bottom surfaces.
//
// [STATE] Inherits Fill::spacing, Fill::loop_clipping, Fill::print_config,
//         Fill::print_object_config. All mutable state is per-call local.
// [MEMORY] Clone via default copy constructor (shallow). Shared print_config/print_object_config
//          pointers are non-owning; FillConcentric must not outlive the PrintConfig.
class FillConcentric : public Fill
{
public:
    ~FillConcentric() override = default;

    // [INTENT] Returns false — concentric rings never self-intersect by construction
    // (each ring is strictly inside the previous one after offset).
    bool is_self_crossing() override { return false; }

protected:
    // [INTENT] Default copy constructor — shallow clone. Used by the factory (Fill::new_from_type).
    Fill* clone() const override { return new FillConcentric(*this); };

    // [INTENT] Polylines overload: classic fixed-width concentric ring algorithm.
    // Steps:
    //   1. Compute distance = scaled_spacing / density (adjusted for full solid fill).
    //   2. Contract ExPolygon by half the multiline spacing to avoid perimeter overlap.
    //   3. Iterative offset2_ex loop: shrink by (distance + min_spacing/2), then
    //      re-expand by min_spacing/2. This two-pass offset removes thin slivers while
    //      ensuring rings maintain their target width.
    //   4. union_pt_chained_outside_in: order all rings from outermost to innermost.
    //   5. Split each ring into a Polyline at the point nearest to the last endpoint
    //      (greedy nearest-neighbor chaining to minimize travel).
    //   6. multiline_fill: Orca extension — offset lines if multiline > 1.
    //   7. clip_end: remove a tiny segment at the end of each polyline to prevent
    //      the extruder dwelling exactly at the seam point.
    //
    // [HAZARD H311] `union_pt_chained_outside_in` uses Clipper polygon union + tree ordering.
    // For degenerate input (very thin regions), it may merge rings that should remain separate,
    // producing incorrect G-code. The Arachne overload handles this better.
    //
    // [COUPLING] Calls multiline_fill() from FillBase.cpp.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    // [INTENT] ThickPolylines overload: Arachne variable-width concentric rings.
    // Used when params.use_arachne == true (solid top/bottom with adaptive line widths).
    // Invokes Arachne::WallToolPaths with hardcoded parameters:
    //   min_bead_width = 0.85 × min_nozzle_diameter
    //   min_feature_size = 0.25 × min_nozzle_diameter
    //   wall_transition_length = 1.0 × min_nozzle_diameter
    //   wall_transition_angle = 10°
    //   wall_distribution_count = 1
    // This produces variable-width thick polylines that can fill thin perimeter regions
    // where fixed-width rings would not fit.
    //
    // [HAZARD H312] For non-solid fills (params.density < 0.9999), falls back to the
    // Polylines overload and wraps results in to_thick_polylines(). The Arachne path
    // is only taken for solid fills. This asymmetry means Arachne is never used for
    // low-density concentric infill patterns.
    //
    // [COUPLING] Requires print_config and print_object_config non-null (asserted).
    // Calls Arachne::WallToolPaths::getToolPaths() — expensive O(N log N) Arachne computation.
    // [MEMORY] loops (WallToolPaths result) is a local vector; all_extrusions holds raw
    // pointers into loops — loops must not be modified/destroyed while all_extrusions is in use.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              ThickPolylines&                thick_polylines_out) override;

    // [INTENT] Returns true — concentric rings MUST NOT be reordered by the G-code engine.
    // The outermost-first ordering (from union_pt_chained_outside_in) is critical for
    // adhesion: outer rings anchor the path before inner rings are printed.
    // Reordering would produce random seam positions and adhesion issues.
    bool no_sort() const override { return true; }
};

} // namespace Slic3r

#endif // slic3r_FillConcentric_hpp_
