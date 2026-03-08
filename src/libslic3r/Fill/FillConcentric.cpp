// [INTENT] Implements FillConcentric: inward-spiraling concentric ring infill pattern.
// Two algorithm paths:
//   1. Polylines path (classic, fixed-width): iterative inward polygon offset.
//   2. ThickPolylines path (Arachne, variable-width): only for solid fills (density > 99.99%).
//
// The classic algorithm is an iterative Minkowski erosion: repeatedly shrink the ExPolygon
// by the ring spacing until no area remains. Rings are ordered outermost-first for adhesion.
//
// [COUPLING] Depends on:
//   - ClipperUtils: offset_ex, offset2_ex, union_pt_chained_outside_in, to_polygons
//   - FillBase: multiline_fill, loop_clipping, _adjust_solid_spacing
//   - Arachne/WallToolPaths: Arachne variable-width rings for solid fill path
//   - VariableWidth: to_thick_polyline()
//   - ShortestPath: reorder_by_shortest_traverse()
//
// [CONCURRENCY] No shared mutable state. Thread-safe per-fill-region call.
// [STATE] Inherits Fill::spacing, Fill::loop_clipping, Fill::print_config, Fill::print_object_config.

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../VariableWidth.hpp"
#include "Arachne/WallToolPaths.hpp"

#include "FillConcentric.hpp"
#include <libslic3r/ShortestPath.hpp>

namespace Slic3r {

// [INTENT] Classic (fixed-width) concentric ring fill algorithm.
// Produces Polylines (equal-width rings) suitable for normal infill and medium-density fills.
// Algorithm:
//   Step 1: Compute ring pitch = scaled_spacing * multiline (total width per ring set).
//           For solid fills (density > 99.99%), adjust spacing to fill the bounding box exactly.
//   Step 2: Contract the ExPolygon by half the multiline offset to create breathing room
//           between the outermost ring and the perimeter (avoids excessive overlap).
//   Step 3: Iterative offset2_ex loop:
//             offset2_ex(last, -(distance + min_spacing/2), +min_spacing/2)
//           The two-pass offset: first erode by (distance + half_spacing) to advance the ring
//           and remove thin slivers, then dilate back by half_spacing to restore width.
//           This is equivalent to Clipper's standard "remove thin islands" offset strategy.
//   Step 4: union_pt_chained_outside_in orders all collected loops outermost-to-innermost.
//   Step 5: Greedy nearest-neighbor chaining: each ring is split at the point nearest to
//           the last printed endpoint, minimizing travel between rings.
//   Step 6: multiline_fill: Orca extension — if params.multiline > 1, offset each ring
//           to produce parallel inner/outer copies. Applied before clip_end.
//   Step 7: clip_end: remove loop_clipping mm from the end of each polyline to prevent
//           the extruder dwelling exactly at the ring seam.
//
// [STATE] Mutates polylines_out by appending new paths. Does not modify 'this'.
// [MEMORY] loops is a local Polygons; contracted is a local ExPolygons. Both freed on return.
//
// [HAZARD H313] The iterative offset2_ex loop makes no attempt to limit iteration count.
// For very large bounding boxes with very small spacing (e.g., tiny spacing, huge part),
// the loop may run thousands of iterations. In practice, the inner ExPolygons become
// empty within O(BBox / spacing) iterations, which is bounded by layer resolution.
//
// [HAZARD H314] `union_pt_chained_outside_in` relies on Clipper's polygon union and parent-
// child tree ordering. For ExPolygons with holes, if a hole survives the offset chain, it
// will be treated as an outer ring and printed in the wrong direction. Clipper handles the
// winding correctly for well-formed inputs, but degenerate geometry may produce misordering.
//
// [HAZARD H315] `split_at_index(last_pos.nearest_point_index(loop.points))`: iterates all
// loop points to find the nearest to last_pos. For rings with many points (complex outlines),
// this is O(N_points) per ring, and there are O(BBox/spacing) rings — O(N_rings × N_points)
// total. For large solid fills this is a measurable hotspot.
//
// [HAZARD H316] `this->spacing` is a `double` in mm. The assignment
// `this->spacing = unscale<double>(distance)` updates the inherited member for the solid-fill
// adjustment case. This mutation persists for the lifetime of this Fill instance — if clone()
// is called after this, the clone inherits the adjusted spacing, not the original.
void FillConcentric::_fill_surface_single(const FillParams&              params,
                                          unsigned int                   thickness_layers,
                                          const std::pair<float, Point>& direction,
                                          ExPolygon                      expolygon,
                                          Polylines&                     polylines_out)
{
    // no rotation is supported for this infill pattern
    BoundingBox bounding_box = expolygon.contour.bounding_box();

    // [INTENT] min_spacing: the total width of one "multiline" ring set (scaled).
    // For multiline == 1 (default), this equals the single ring spacing.
    coord_t min_spacing = scale_(this->spacing) * params.multiline;
    // [INTENT] distance: spacing between ring centers (pitch). For sparse infill (density < 1),
    // this is larger than min_spacing, leaving gaps between ring sets.
    coord_t distance = coord_t(min_spacing / params.density);

    // [INTENT] For solid fills (density ≈ 100%), adjust distance to exactly tile the bounding box.
    // This ensures the innermost ring is not a stub and avoids a gap at the center.
    // [HAZARD H316] This assignment mutates this->spacing, persisting for the lifetime of
    // this Fill instance (see note above).
    if (params.density > 0.9999f && !params.dont_adjust) {
        distance      = this->_adjust_solid_spacing(bounding_box.size()(0), distance);
        this->spacing = unscale<double>(distance);
    }

    // [INTENT] Contract ExPolygon by half the total multiline width to create clearance between
    // the outermost ring and the perimeter. For multiline == 1, offset = 0 (no contraction).
    // For multiline > 1, the outer rings would otherwise overlap the perimeter by half the
    // multiline line width.
    // [MEMORY] contracted is a local ExPolygons; ownership transferred to 'last' below.
    ExPolygons contracted = offset_ex(expolygon, -float(scale_(0.5 * (params.multiline - 1) * this->spacing)));

    // [INTENT] Initial ring set = the contracted polygon boundary.
    Polygons loops = to_polygons(contracted);

    // [INTENT] Iterative inward offset. Each iteration:
    //   - Erodes the shape by (distance + min_spacing/2) to advance inward by one ring pitch
    //     and remove any thin slivers smaller than min_spacing/2.
    //   - Re-expands by min_spacing/2 to restore ring width (canonical "open" morphological
    //     operation at scale distance).
    // Loop terminates when the shape shrinks to nothing (last.empty()).
    ExPolygons last{std::move(contracted)};
    while (!last.empty()) {
        last = offset2_ex(last, -(distance + min_spacing / 2), +min_spacing / 2);
        append(loops, to_polygons(last));
    }

    // [INTENT] Reorder all collected rings from outermost to innermost using Clipper's
    // polygon union + parent-child tree walk. Outermost-first printing improves adhesion:
    // outer rings anchor the path before inner rings are printed.
    // [HAZARD H314] See header comment for potential misordering with holed polygons.
    loops = union_pt_chained_outside_in(loops);

    // [INTENT] Split each ring into a Polyline, starting at the point nearest to the last
    // printed endpoint to minimize travel moves between rings.
    // [HAZARD H315] O(N_points) per ring (nearest_point_index linear scan).
    size_t iPathFirst = polylines_out.size();
    Point  last_pos(0, 0);
    for (const Polygon& loop : loops) {
        polylines_out.emplace_back(loop.split_at_index(last_pos.nearest_point_index(loop.points)));
        last_pos = polylines_out.back().last_point();
    }

    // [INTENT] Orca multiline extension: generate additional parallel ring offsets when
    // params.multiline > 1. Expands each ring inward/outward to fill multiline spacing.
    // Apply multiline offset if needed
    multiline_fill(polylines_out, params, spacing);

    // [INTENT] Clip the end of each polyline by loop_clipping to prevent the extruder from
    // dwelling at the seam. Also removes degenerate (too-short) polylines that would produce
    // blobs. Uses an in-place compaction (index j) to avoid N² erase-from-middle calls.
    // Keep valid paths only.
    size_t j = iPathFirst;
    for (size_t i = iPathFirst; i < polylines_out.size(); ++i) {
        polylines_out[i].clip_end(this->loop_clipping);
        if (polylines_out[i].is_valid()) {
            if (j < i)
                polylines_out[j] = std::move(polylines_out[i]);
            ++j;
        }
    }
    if (j < polylines_out.size())
        polylines_out.erase(polylines_out.begin() + j, polylines_out.end());
    // TODO: return ExtrusionLoop objects to get better chained paths,
    //  otherwise the outermost loop starts at the closest point to (0, 0).
    //  We want the loops to be split inside the G-code generator to get optimum path planning.
}

// [INTENT] Arachne variable-width concentric ring fill. Used exclusively for solid fills
// (params.density > 0.9999) when params.use_arachne == true.
// Invokes the full Arachne WallToolPaths pipeline to produce variable-width ExtrusionLines
// that adapt to thin regions where fixed-width rings would not fit.
//
// For non-solid fills (density ≤ 0.9999), falls back to the Polylines overload and wraps
// the result via to_thick_polylines() with a fixed width.
//
// Arachne parameters (hardcoded):
//   min_bead_width = 0.85 × min_nozzle_diameter   (minimum printable line width)
//   min_feature_size = 0.25 × min_nozzle_diameter  (minimum gap to generate a ring in)
//   wall_transition_length = 1.0 × min_nozzle_diameter (transition zone for width changes)
//   wall_transition_angle = 10° (angle threshold for width transitions)
//   wall_distribution_count = 1 (equal distribution of excess width to inner/outer walls)
//
// [STATE] Does not mutate 'this'. Appends to thick_polylines_out.
// [COUPLING] Calls Arachne::WallToolPaths (full Arachne medial axis engine).
//            Calls reorder_by_shortest_traverse() (ShortestPath.hpp) for travel optimization.
//
// [HAZARD H317] `loops_count = max(bbox_x, bbox_y) / min_spacing + 1` over-estimates the
// number of rings needed. Arachne will compute the actual count from geometry, but the
// over-estimate wastes computation in WallToolPaths for large areas with small spacing.
//
// [HAZARD H318] `polygons = offset(expolygon, min_spacing/2)` pre-expands the ExPolygon
// before passing to WallToolPaths. This expansion is the Arachne convention (Arachne
// expects outlines expanded by half a bead width). If removed or applied twice, Arachne
// will produce rings at the wrong radius.
//
// [HAZARD H312] Non-solid fills do NOT use Arachne — they fall through to the Polylines
// overload. The resulting thick polylines have uniform (not variable) width. This is an
// intentional design simplification: variable-width infill for non-solid patterns would
// require much more complex per-region parameter tuning.
//
// [MEMORY] `loops` (WallToolPaths result) owns the ExtrusionLine objects.
// `all_extrusions` holds raw pointers into `loops`. If `loops` is destroyed before
// `all_extrusions` is processed, all pointers become dangling. Current code is safe
// because `loops` lives until end-of-scope.
void FillConcentric::_fill_surface_single(const FillParams&              params,
                                          unsigned int                   thickness_layers,
                                          const std::pair<float, Point>& direction,
                                          ExPolygon                      expolygon,
                                          ThickPolylines&                thick_polylines_out)
{
    assert(params.use_arachne);
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    // no rotation is supported for this infill pattern
    Point   bbox_size   = expolygon.contour.bounding_box().size();
    coord_t min_spacing = scaled<coord_t>(this->spacing);

    if (params.density > 0.9999f && !params.dont_adjust) {
        // [INTENT] Estimate ring count as max(width, height) / spacing + 1 (generous upper bound).
        // Arachne will compute the real count from geometry; this is just the WallToolPaths limit.
        // [HAZARD H317] Over-estimate; see comment above.
        coord_t loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        // [INTENT] Expand expolygon by half a bead width before passing to Arachne.
        // Arachne convention: outlines are the center-lines of the print area boundary.
        // [HAZARD H318] This half-spacing expansion must match WallToolPaths expectations exactly.
        Polygons polygons = offset(expolygon, float(min_spacing) / 2.f);

        // [INTENT] Compute minimum nozzle diameter across all nozzles to set Arachne constraints.
        // This produces the narrowest acceptable bead width for gap-filling rings.
        double                       min_nozzle_diameter = *std::min_element(print_config->nozzle_diameter.values.begin(),
                                                                             print_config->nozzle_diameter.values.end());
        Arachne::WallToolPathsParams input_params;
        input_params.min_bead_width                   = 0.85 * min_nozzle_diameter;
        input_params.min_feature_size                 = 0.25 * min_nozzle_diameter;
        input_params.wall_transition_length           = 1.0 * min_nozzle_diameter;
        input_params.wall_transition_angle            = 10;
        input_params.wall_transition_filter_deviation = 0.25 * min_nozzle_diameter;
        input_params.wall_distribution_count          = 1;

        // [INTENT] WallToolPaths computes the Arachne medial-axis-based variable-width rings.
        // bead_width_0 == bead_width_x == min_spacing (all rings same nominal width).
        // loops_count: upper bound on ring count (see H317).
        // 0: inset_count = 0 (no additional inset loops outside of concentric rings).
        Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, input_params);

        // [INTENT] getToolPaths() triggers the full Arachne computation.
        // Returns a vector<VariableWidthLines>, one entry per Arachne "bead" (ring).
        // [MEMORY] loops owns the ExtrusionLine data. all_extrusions borrows raw pointers.
        std::vector<Arachne::VariableWidthLines>   loops = wallToolPaths.getToolPaths();
        std::vector<const Arachne::ExtrusionLine*> all_extrusions;
        for (Arachne::VariableWidthLines& loop : loops) {
            if (loop.empty())
                continue;
            for (const Arachne::ExtrusionLine& wall : loop)
                all_extrusions.emplace_back(&wall);
        }

        // [INTENT] Convert ExtrusionLines to ThickPolylines. For closed rings (is_closed = true),
        // find the split point nearest to last_pos to minimize travel. For open rings, use as-is.
        // Split paths using a nearest neighbor search.
        size_t firts_poly_idx = thick_polylines_out.size();
        Point  last_pos(0, 0);
        for (const Arachne::ExtrusionLine* extrusion : all_extrusions) {
            if (extrusion->empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
            if (extrusion->is_closed)
                thick_polyline.start_at_index(last_pos.nearest_point_index(thick_polyline.points));
            thick_polylines_out.emplace_back(std::move(thick_polyline));
            last_pos = thick_polylines_out.back().last_point();
        }

        // [INTENT] clip_end: same seam-prevention logic as the Polylines overload.
        // In-place compaction (index j) avoids O(N²) erase-from-middle.
        // Keep valid paths only.
        size_t j = firts_poly_idx;
        for (size_t i = firts_poly_idx; i < thick_polylines_out.size(); ++i) {
            thick_polylines_out[i].clip_end(this->loop_clipping);
            if (thick_polylines_out[i].is_valid()) {
                if (j < i)
                    thick_polylines_out[j] = std::move(thick_polylines_out[i]);
                ++j;
            }
        }
        if (j < thick_polylines_out.size())
            thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());

        // [INTENT] Reorder ThickPolylines by shortest-path traversal to minimize travel moves.
        // Unlike the Polylines overload which uses per-ring split-point tracking, the Arachne
        // path uses a global shortest-traverse pass after all rings are collected.
        reorder_by_shortest_traverse(thick_polylines_out);
    } else {
        // [INTENT] Non-solid fill fallback: use the classic Polylines algorithm and wrap
        // the result in ThickPolylines with a fixed width (min_spacing).
        // [HAZARD H312] No Arachne is used for non-solid fills; see header comment.
        Polylines polylines;
        this->_fill_surface_single(params, thickness_layers, direction, expolygon, polylines);
        append(thick_polylines_out, to_thick_polylines(std::move(polylines), min_spacing));
    }
}

} // namespace Slic3r
