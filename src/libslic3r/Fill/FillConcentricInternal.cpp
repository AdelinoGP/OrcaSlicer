// [INTENT] FillConcentricInternal.cpp — Arachne-based concentric infill for internal regions.
//
// This is a fundamentally different code path from FillConcentric:
// - Uses `Arachne::WallToolPaths` to generate variable-width concentric bead paths,
//   handling narrow regions that fixed-offset concentric loops cannot fill.
// - Produces `ThickPolyline` output (variable width per segment), converted to
//   ExtrusionEntities via `variable_width()`.
// - Does NOT call `_fill_surface_single()`; instead overrides `fill_surface_extrusion()`.
// - Iterates `this->no_overlap_expolygons` (pre-set by the fill engine).
//
// Pipeline:
//   no_overlap_expolygons → WallToolPaths → VariableWidthLines → ThickPolylines
//   → seam rotation (nearest-neighbour) → loop_clipping → reorder_by_shortest_traverse
//   → variable_width() → ExtrusionEntityCollection → out
//
// [COUPLING] Arachne::WallToolPaths (getToolPaths, VariableWidthLines, ExtrusionLine),
//            VariableWidth (to_thick_polyline, variable_width), ClipperUtils,
//            ShortestPath (reorder_by_shortest_traverse).
// [STATE] Reads: this->no_overlap_expolygons, this->print_config, this->print_object_config,
//         this->loop_clipping, this->spacing.
// [CONCURRENCY] No shared mutable state. Thread-safe per call.

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../VariableWidth.hpp"
#include "Arachne/WallToolPaths.hpp"

#include "FillConcentricInternal.hpp"
#include <libslic3r/ShortestPath.hpp>

namespace Slic3r {

// [INTENT] `fill_surface_extrusion` — main entry point for FillConcentricInternal.
// Processes each expolygon in `this->no_overlap_expolygons` independently, generating
// Arachne variable-width concentric paths and appending an ExtrusionEntityCollection to `out`.
//
// [HAZARD H391] `no rotation is supported for this infill pattern` (comment on line 21).
//   `expolygon.rotate()` is never called. Any caller that sets `Fill::angle != 0` for
//   FillConcentricInternal will have its angle silently ignored.
// [HAZARD H392] This function bypasses the standard `_fill_surface_single()` pipeline:
//   no call to `multiline_fill()`, `connect_infill()`, or `intersection_pl()`. A port
//   that routes this class through the standard pipeline will produce wrong output.
// [HAZARD H394] `assert(this->print_config != nullptr && this->print_object_config != nullptr)`
//   is a debug-only check. In release builds, a null config pointer causes UB when
//   `print_config->nozzle_diameter` is accessed. The fill engine must always set these
//   before calling fill_surface_extrusion.
void FillConcentricInternal::fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out)
{
    assert(this->print_config != nullptr && this->print_object_config != nullptr);

    ThickPolylines thick_polylines_out;

    for (size_t i = 0; i < this->no_overlap_expolygons.size(); ++i) {
        ExPolygon& expolygon = this->no_overlap_expolygons[i];

        // no rotation is supported for this infill pattern
        // [HAZARD H391] See above — angle is silently ignored.
        // [INTENT] `loops_count` = max(bbox_width, bbox_height) / min_spacing + 1.
        // This is the maximum number of concentric rings that could fit in the largest
        // dimension. Arachne will generate at most this many rings; narrow features will
        // terminate earlier. The +1 ensures at least one pass is always attempted.
        Point   bbox_size   = expolygon.contour.bounding_box().size();
        coord_t min_spacing = params.flow.scaled_spacing();

        coord_t  loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
        Polygons polygons    = to_polygons(expolygon);

        // [INTENT] Arachne wall toolpath parameters — tuned for infill quality:
        //   min_bead_width   = 0.85 × nozzle_diameter  — minimum bead width before infill is skipped.
        //   min_feature_size = 0.25 × nozzle_diameter  — minimum feature size to attempt filling.
        //   wall_transition_length = 0.4 mm — length over which bead width transitions.
        //   wall_transition_angle  = 10°    — angle threshold for bead-width transitions.
        //   wall_distribution_count = 1      — distribute width change to innermost 1 wall only.
        // [HAZARD H395] All WallToolPathsParams values except `min_bead_width` and `min_feature_size`
        //   are hardcoded constants not exposed to the user. `wall_transition_length=0.4` assumes
        //   typical extrusion rates; for very slow or very fast printers these may be non-optimal.
        double                       min_nozzle_diameter = *std::min_element(print_config->nozzle_diameter.values.begin(),
                                                                             print_config->nozzle_diameter.values.end());
        Arachne::WallToolPathsParams input_params;
        input_params.min_bead_width                   = 0.85 * min_nozzle_diameter;
        input_params.min_feature_size                 = 0.25 * min_nozzle_diameter;
        input_params.wall_transition_length           = 0.4;
        input_params.wall_transition_angle            = 10;
        input_params.wall_transition_filter_deviation = 0.25 * min_nozzle_diameter;
        input_params.wall_distribution_count          = 1;

        // [INTENT] Construct WallToolPaths for the expolygon.
        // Both `bead_width_0` and `bead_width_x` are set to `min_spacing` (uniform width).
        // `0` as the last parameter suppresses additional inset beyond the loop count.
        Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, input_params);

        // [INTENT] `getToolPaths()` runs the Arachne skeletal trapezoidation and
        // returns variable-width lines grouped by ring index (innermost first).
        std::vector<Arachne::VariableWidthLines> loops = wallToolPaths.getToolPaths();
        // [INTENT] Flatten all rings from all loop levels into a single list
        // for unified seam rotation and path ordering.
        std::vector<const Arachne::ExtrusionLine*> all_extrusions;
        for (Arachne::VariableWidthLines& loop : loops) {
            if (loop.empty())
                continue;
            for (const Arachne::ExtrusionLine& wall : loop)
                all_extrusions.emplace_back(&wall);
        }

        // Split paths using a nearest neighbor search.
        // [INTENT] For each ExtrusionLine, convert to a ThickPolyline.
        // For closed paths (is_closed), rotate the start point to the nearest point
        // to `last_pos` (the last endpoint of the previous path) to minimise the
        // travel distance between consecutive rings (seam alignment).
        // [HAZARD H396] `firts_poly_idx` is a typo for `first_poly_idx`. The variable
        //   is used correctly, but the typo should be noted for a port.
        size_t firts_poly_idx = thick_polylines_out.size();
        Point  last_pos(0, 0);
        for (const Arachne::ExtrusionLine* extrusion : all_extrusions) {
            if (extrusion->empty())
                continue;

            ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
            // [INTENT] Closed ring handling:
            // 1. Remove the duplicated last point (Arachne closed rings end at start).
            // 2. Assert width array has matching size (2 widths per segment → 2N for N points).
            // 3. Rotate start point to the nearest point to last_pos for seam minimisation.
            //    `nearest_point_index()` finds the index of the closest point in the ring.
            // 4. Re-close the ring by appending the (new) first point at the end.
            if (extrusion->is_closed && thick_polyline.points.front() == thick_polyline.points.back() &&
                thick_polyline.width.front() == thick_polyline.width.back()) {
                thick_polyline.points.pop_back();
                assert(thick_polyline.points.size() * 2 == thick_polyline.width.size());
                int nearest_idx = last_pos.nearest_point_index(thick_polyline.points);
                // [HAZARD H397] `std::rotate(thick_polyline.width.begin(), ..., 2 * nearest_idx, ...)`.
                //   `width` has 2 entries per point (start and end width of each segment). Rotating
                //   by `2 * nearest_idx` is correct only when nearest_idx < points.size(). If
                //   nearest_point_index() returns points.size() (edge case), rotating by 2 * size
                //   is a no-op, which is correct — but the assert above doesn't guard this path.
                std::rotate(thick_polyline.points.begin(), thick_polyline.points.begin() + nearest_idx, thick_polyline.points.end());
                std::rotate(thick_polyline.width.begin(), thick_polyline.width.begin() + 2 * nearest_idx, thick_polyline.width.end());
                thick_polyline.points.emplace_back(thick_polyline.points.front());
            }
            thick_polylines_out.emplace_back(std::move(thick_polyline));
        }

        // [INTENT] `clip_end(loop_clipping)` shortens each path at its trailing end to
        // prevent the extruder from dwelling exactly on the ring-close seam point,
        // which would cause over-extrusion at the seam. Paths too short after clipping
        // are removed (is_valid() returns false if the path has < 2 points).
        // clip the paths to prevent the extruder from getting exactly on the first point of the loop
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

        // [INTENT] Reorder thick_polylines_out by shortest-traverse order to minimise
        // total travel distance across all rings. This is the Arachne-path-specific
        // equivalent of chain_polylines used in other infill types.
        reorder_by_shortest_traverse(thick_polylines_out);
    }

    // [INTENT] Allocate an ExtrusionEntityCollection with no_sort=true.
    // `variable_width()` converts ThickPolylines → ExtrusionPaths/Loops with the correct
    // extrusion_role and flow (derived from params.flow with spacing override).
    // The collection is no-sort because reorder_by_shortest_traverse already found the
    // optimal order; re-sorting would destroy this ordering.
    ExtrusionEntityCollection* coll_nosort = new ExtrusionEntityCollection();
    coll_nosort->no_sort                   = this->no_sort(); // can be sorted inside the pass

    if (!thick_polylines_out.empty()) {
        // [INTENT] `params.flow.with_spacing(this->spacing)` adjusts the flow to use
        // the infill spacing (not the wall spacing), ensuring correct extrusion width
        // for the variable-width conversion.
        // [HAZARD H398] `new_flow = params.flow.with_spacing(float(this->spacing))`.
        //   `this->spacing` is in mm (unscaled). `with_spacing` expects a float. If
        //   `this->spacing` is very large or negative (misconfiguration), the derived
        //   flow object will produce extreme extrusion multipliers. No clamping exists.
        Flow                      new_flow = params.flow.with_spacing(float(this->spacing));
        ExtrusionEntityCollection gap_fill;
        variable_width(thick_polylines_out, params.extrusion_role, new_flow, gap_fill.entities);
        coll_nosort->append(std::move(gap_fill.entities));
    }

    // [INTENT] Append the collection to `out` only if non-empty; otherwise delete to
    // avoid memory leaks. This is a manual ownership pattern — a port should use
    // std::unique_ptr to avoid needing the manual delete.
    // [HAZARD H399] If an exception is thrown between `new ExtrusionEntityCollection()`
    //   and the `out.push_back(coll_nosort)` branch, `coll_nosort` leaks. No RAII
    //   guard wraps the allocation.
    if (!coll_nosort->entities.empty())
        out.push_back(coll_nosort);
    else
        delete coll_nosort;
}

} // namespace Slic3r
