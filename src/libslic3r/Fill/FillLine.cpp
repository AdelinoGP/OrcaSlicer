// [INTENT] Implements FillLine::_fill_surface_single: the oscillating-line infill driver.
//
// Algorithm:
//   1. Rotate expolygon by -direction.first (work in axis-aligned frame).
//   2. Compute spacings: _min_spacing (density=1.0), _line_spacing (adjusted for density),
//      _diagonal_distance, _line_oscillation.
//   3. For solid infill: adjust line spacing to fit an integer number of lines.
//      For sparse: extend the bounding box to align with adjacent layers.
//   4. Generate scan-lines (Lines) at x = min..max step _line_spacing.
//   5. Clip lines against expolygon (offset +0.02mm to include boundary lines).
//   6. Extend each clipped polyline's endpoints outward by extra = 30% of _min_spacing.
//   7. chain_polylines to order by nearest-neighbor.
//   8. Connect adjacent chains: if endpoint distance satisfies _can_connect() AND the
//      connecting segment lies inside expolygon_off (offset by _min_spacing/2), merge.
//   9. Rotate all output polylines back by +direction.first.
//
// [COUPLING] ClipperUtils (intersection_pl, offset, offset_ex), ShortestPath (chain_polylines),
//            FillBase (_adjust_solid_spacing, align_to_grid).
// [MEMORY] All polylines allocated on the heap. No persistent state beyond the call.
// [CONCURRENCY] Writes _min_spacing, _line_spacing, _diagonal_distance, _line_oscillation
//               to member variables — not thread-safe if the same FillLine instance is shared.

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"

#include "FillLine.hpp"

namespace Slic3r {

void FillLine::_fill_surface_single(const FillParams&              params,
                                    unsigned int                   thickness_layers,
                                    const std::pair<float, Point>& direction,
                                    ExPolygon                      expolygon,
                                    Polylines&                     polylines_out)
{
    // rotate polygons so that we can work with vertical lines here
    // [INTENT] Rotate into the infill frame so all generated lines are axis-aligned.
    // Rotated back at the end.
    expolygon.rotate(-direction.first);

    // [INTENT] Compute base spacings. _min_spacing is the per-extrusion spacing at 100% density.
    // _line_spacing scales it up for lower densities.
    this->_min_spacing = scale_(this->spacing);
    assert(params.density > 0.0001f && params.density <= 1.f);
    this->_line_spacing = coord_t(coordf_t(this->_min_spacing) / params.density);
    // [INTENT] _diagonal_distance: max Y-gap (in scaled units) between two consecutive polyline
    // endpoints to attempt a direct connection (no travel move). 2 × _line_spacing is heuristic.
    this->_diagonal_distance = this->_line_spacing * 2;
    // [INTENT] _line_oscillation: how much odd lines deviate from vertical.
    // = _line_spacing - _min_spacing. For density=1.0, this is 0 (no oscillation).
    // For density<1.0, odd lines lean left/right by this amount.
    this->_line_oscillation  = this->_line_spacing - this->_min_spacing; // only for Line infill
    BoundingBox bounding_box = expolygon.contour.bounding_box();

    // define flow spacing according to requested density
    if (params.density > 0.9999f && !params.dont_adjust) {
        // [INTENT] Solid fill: adjust _line_spacing so lines fit evenly in the bounding box.
        // This avoids a fractional gap at the right edge.
        this->_line_spacing = this->_adjust_solid_spacing(bounding_box.size()(0), this->_line_spacing);
        // [INTENT] Update the public `spacing` in mm to match the adjusted value.
        this->spacing = unscale<double>(this->_line_spacing);
    } else {
        // extend bounding box so that our pattern will be aligned with other layers
        // Transform the reference point to the rotated coordinate system.
        // [INTENT] For sparse infill, the bounding box must be layer-aligned so lines
        // from different layers share the same X grid. align_to_grid snaps bounding_box.min
        // to the nearest grid origin for the given spacing and direction.
        bounding_box.merge(
            align_to_grid(bounding_box.min, Point(this->_line_spacing, this->_line_spacing), direction.second.rotated(-direction.first)));
    }

    // generate the basic pattern
    // [INTENT] Generate one scan-line per X column, from bounding_box.min.x to max.x.
    // Each line spans the full Y extent of the bounding box.
    coord_t x_max = bounding_box.max(0) + SCALED_EPSILON;
    Lines   lines;
    for (coord_t x = bounding_box.min(0); x <= x_max; x += this->_line_spacing)
        lines.push_back(this->_line(lines.size(), x, bounding_box.min(1), bounding_box.max(1)));

    // clip paths against a slightly larger expolygon, so that the first and last paths
    // are kept even if the expolygon has vertical sides
    // the minimum offset for preventing edge lines from being clipped is SCALED_EPSILON;
    // however we use a larger offset to support expolygons with slightly skewed sides and
    // not perfectly straight
    // FIXME Vojtech: Update the intersecton function to work directly with lines.
    // [INTENT] Convert Lines → Polylines so intersection_pl can operate on them.
    // This is a known inefficiency (FIXME): Lines could be clipped directly.
    Polylines polylines_src;
    polylines_src.reserve(lines.size());
    for (Lines::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        polylines_src.push_back(Polyline());
        Points& pts = polylines_src.back().points;
        pts.reserve(2);
        pts.push_back(it->a);
        pts.push_back(it->b);
    }
    // [INTENT] Clip against expolygon offset by +0.02mm. The small outward offset ensures
    // lines touching the exact expolygon boundary are not clipped to zero length.
    // [HAZARD H361] scale_(0.02) = 20 nm. For expolygons with very fine features (< 20 nm),
    // the offset could merge adjacent contours. In practice print features are > 0.1mm so
    // this is safe, but the hardcoded constant is undocumented.
    Polylines polylines = intersection_pl(std::move(polylines_src), offset(expolygon, scale_(0.02)));

    // FIXME Vojtech: This is only performed for horizontal lines, not for the vertical lines!
    // [INTENT] Extend each clipped polyline's endpoints outward by extra (30% of min_spacing).
    // This makes the extruded lines slightly overlap the perimeter, improving bonding.
    // The extension is along the Y axis (bottom endpoint moves down, top moves up).
    //
    // [HAZARD H359 — see .hpp] This extension is only applied to the Y axis. For diagonal
    // (odd-index, oscillated) lines, the X component is not extended. The FIXME confirms
    // this is a known gap in the implementation. The effect is a slightly shorter extrusion
    // at the tips of diagonal lines compared to straight lines.
    const float INFILL_OVERLAP_OVER_SPACING = 0.3f;
    // How much to extend an infill path from expolygon outside?
    coord_t extra = coord_t(floor(this->_min_spacing * INFILL_OVERLAP_OVER_SPACING + 0.5f));
    for (Polylines::iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++it_polyline) {
        Point* first_point = &it_polyline->points.front();
        Point* last_point  = &it_polyline->points.back();
        // [INTENT] Sort endpoints so first_point is the lower Y endpoint (bottom).
        if (first_point->y() > last_point->y())
            std::swap(first_point, last_point);
        first_point->y() -= extra;
        last_point->y() += extra;
    }

    // [INTENT] Record the current output size so we can rotate only the newly added polylines.
    size_t n_polylines_out_old = polylines_out.size();

    // connect lines
    if (!polylines.empty()) { // prevent calling leftmost_point() on empty collections
        // offset the expolygon by max(min_spacing/2, extra)
        // [INTENT] Expand the expolygon by _min_spacing/2 to create a slightly larger
        // containment region for connection-segment testing. A connecting segment is only
        // accepted if it lies entirely within expolygon_off.
        //
        // [HAZARD H362] If offset_ex returns empty (can happen for very thin regions where
        // the Minkowski sum would invert the polygon), expolygon_off remains default-constructed
        // (empty). All subsequent expolygon_off.contains() calls return false, so no connections
        // are made. The result is correct (no invalid connections) but less optimal (more travels).
        ExPolygon expolygon_off;
        {
            ExPolygons expolygons_off = offset_ex(expolygon, this->_min_spacing / 2);
            if (!expolygons_off.empty()) {
                // When expanding a polygon, the number of islands could only shrink. Therefore the offset_ex shall generate exactly one
                // expanded island for one input island.
                assert(expolygons_off.size() == 1);
                std::swap(expolygon_off, expolygons_off.front());
            }
        }
        // [INTENT] Chain clipped polylines in nearest-neighbor order to minimize travel.
        bool first = true;
        for (Polyline& polyline : chain_polylines(std::move(polylines))) {
            if (!first) {
                // Try to connect the lines.
                Points&      pts_end     = polylines_out.back().points;
                const Point& first_point = polyline.points.front();
                const Point& last_point  = pts_end.back();
                // Distance in X, Y.
                const Vector distance = last_point - first_point;
                // TODO: we should also check that both points are on a fill_boundary to avoid
                // connecting paths on the boundaries of internal regions
                // [INTENT] Only connect if (1) spacing/oscillation geometry matches (_can_connect),
                // AND (2) the direct segment is fully inside the slightly-expanded expolygon.
                // This prevents connecting lines across holes or thin walls.
                //
                // [HAZARD H363] expolygon_off.contains(Line) tests only whether the line endpoints
                // are inside expolygon_off, not the full segment. If the two endpoints are inside but
                // the segment crosses a hole, the connection would incorrectly bridge the hole.
                // In practice the _can_connect() X-gap check limits this to adjacent lines (1 step apart)
                // where such crossings are rare, but the check is geometrically incomplete.
                if (this->_can_connect(std::abs(distance(0)), std::abs(distance(1))) &&
                    expolygon_off.contains(Line(last_point, first_point))) {
                    // Append the polyline.
                    pts_end.insert(pts_end.end(), polyline.points.begin(), polyline.points.end());
                    continue;
                }
            }
            // The lines cannot be connected.
            polylines_out.emplace_back(std::move(polyline));
            first = false;
        }
    }

    // paths must be rotated back
    // [INTENT] Rotate all newly added polylines back to world coordinates.
    // Translation is not needed (expolygon was only rotated, not translated).
    for (Polylines::iterator it = polylines_out.begin() + n_polylines_out_old; it != polylines_out.end(); ++it) {
        // No need to translate, the absolute position is irrelevant.
        // it->translate(- direction.second(0), - direction.second(1));
        it->rotate(direction.first);
    }
}

} // namespace Slic3r
