// [INTENT] FillHoneycomb.cpp — implementation of 2D regular hexagonal honeycomb infill.
// Generates zig-zag polylines that trace the edges of a regular hexagonal grid.
// The hex grid is constructed in a rotated coordinate system (aligned to the infill
// direction), then rotated back to world coordinates.
//
// Pattern generation strategy:
//   - One polyline per x-column (at spacing `distance` apart) snakes through the full
//     bounding-box height, visiting 5 Y-points per hex period.
//   - Alternating half-columns are interleaved by swapping ax[0]/ax[1] and reversing
//     point order to produce a continuous zig-zag instead of a disconnected set.
//
// [HAZARD H342] The inner loop generates 5 points per hex period but does NOT check
// whether the computed points are still within the bounding box. Points computed with
// `y + m.y_short + m.hex_side + ...` may exceed `bounding_box.max(1)` for the last
// iteration. They are clipped by `intersection_pl()` later — correct, but wasteful.
//
// [HAZARD H343] `p.simplify(5 * spacing)` uses unscaled `spacing` (same issue as H338
// in Fill3DHoneycomb). At spacing=0.4mm, tolerance = 2.0 scaled units ≈ 0.002 μm.
// The simplification is effectively a no-op. This is a latent bug present since the
// PrusaSlicer origin.
//
// [CONCURRENCY] All hex geometry is derived from the cached `CacheData`. The cache
// lookup and insert are not mutex-protected. Not thread-safe for shared instances.
// [COUPLING] Uses multiline_fill() (FillBase), intersection_pl() (ClipperUtils),
//            chain_or_connect_infill() (ShortestPath/FillBase).

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"

#include "FillHoneycomb.hpp"

namespace Slic3r {

// [INTENT] Main FillHoneycomb entry point. Orchestrates:
//   1. Cache lookup (or compute) hex geometry for (density, spacing).
//   2. Rotate bounding box to infill direction; align to hex grid module.
//   3. For each x-column, generate a zig-zag polyline tracing hex edges.
//   4. Rotate each polyline back to world coordinates.
//   5. Simplify (effectively no-op — see H343).
//   6. multiline_fill() for Orca multi-extrusion passes.
//   7. intersection_pl() to clip to ExPolygon.
//   8. chain_or_connect_infill() for G-code travel ordering.
//
// [STATE] Reads: this->spacing, direction (from Fill::_layer_angle → 60°-per-layer rotation).
//         Reads/writes: this->cache (lazy population; not thread-safe).
//         Writes: polylines_out (appended).
//
// [HAZARD H344] The direction.second (Point) is ignored — only direction.first (angle)
// is used. The second component is a translation that some fill patterns use for phase
// alignment. FillHoneycomb ignores it, relying entirely on the bounding-box alignment
// to `hex_width × pattern_height` grid for inter-layer phase coherence.
void FillHoneycomb::_fill_surface_single(const FillParams&              params,
                                         unsigned int                   thickness_layers,
                                         const std::pair<float, Point>& direction,
                                         ExPolygon                      expolygon,
                                         Polylines&                     polylines_out)
{
    // [INTENT] Lazy-compute hex geometry and cache it for this (density, spacing) pair.
    // cache hexagons math
    CacheID         cache_id(params.density, this->spacing);
    Cache::iterator it_m = this->cache.find(cache_id);
    if (it_m == this->cache.end()) {
        it_m         = this->cache.insert(it_m, std::pair<CacheID, CacheData>(cache_id, CacheData()));
        CacheData& m = it_m->second;
        // [INTENT] min_spacing accounts for multiline passes: total material width = spacing × multiline.
        coord_t min_spacing = coord_t(scale_(this->spacing)) * params.multiline;
        // [INTENT] distance = spacing between parallel hex edges (half hex inner diameter).
        m.distance = coord_t(min_spacing / params.density);
        // [INTENT] hex_side = hex edge length. For a regular hex: side = inner_radius / (sqrt(3)/2).
        m.hex_side         = coord_t(m.distance / (sqrt(3) / 2));
        m.hex_width        = m.distance * 2; // $m->{hex_width} == $m->{hex_side} * sqrt(3);
        coord_t hex_height = m.hex_side * 2;
        // [INTENT] pattern_height = full Y period = 2×hex_side + 2×y_short = hex_height + hex_side.
        m.pattern_height = hex_height + m.hex_side;
        m.y_short        = coord_t(m.distance * sqrt(3) / 3);
        m.x_offset       = min_spacing / 2;
        m.y_offset       = coord_t(m.x_offset * sqrt(3) / 3);
        m.hex_center     = Point(m.hex_width / 2, m.hex_side);
    }
    CacheData& m = it_m->second;

    Polylines all_polylines;
    {
        // [INTENT] Align the bounding box to the hex grid in the rotated (infill-direction)
        // coordinate system, so the pattern tiles consistently across layers.
        // adjust actual bounding box to the nearest multiple of our hex pattern
        // and align it so that it matches across layers

        BoundingBox bounding_box = expolygon.contour.bounding_box();
        {
            // [INTENT] Rotate the bounding box polygon by the infill direction angle,
            // then re-derive an axis-aligned bounding box in the rotated space.
            // rotate bounding box according to infill direction
            Polygon bb_polygon = bounding_box.polygon();
            bb_polygon.rotate(direction.first, m.hex_center);
            bounding_box = bb_polygon.bounding_box();

            // [INTENT] Snap the bounding box min to a (hex_width × pattern_height) grid
            // for cross-layer phase coherence. Comment notes that world-coordinate
            // alignment (not object-local) is used — acceptable for visual quality.
            // extend bounding box so that our pattern will be aligned with other layers
            // $bounding_box->[X1] and [Y1] represent the displacement between new bounding box offset and old one
            // The infill is not aligned to the object bounding box, but to a world coordinate system. Supposedly good enough.
            bounding_box.merge(align_to_grid(bounding_box.min, Point(m.hex_width, m.pattern_height)));
        }

        // [INTENT] Generate one polyline per x-column. Each column contains two half-columns
        // (ax[0] and ax[1]) offset by m.distance. The zig-zag is created by:
        //   1. Swapping ax[0] and ax[1] after each half-column.
        //   2. Reversing point order for the second half-column to maintain snake direction.
        //
        // [HAZARD H342] Y-loop does not check against bounding_box.max(1); overshoot clipped
        // by intersection_pl().
        coord_t x = bounding_box.min(0);
        while (x <= bounding_box.max(0)) {
            Polyline p;
            coord_t  ax[2] = {x + m.x_offset, x + m.distance - m.x_offset};
            for (size_t i = 0; i < 2; ++i) {
                std::reverse(p.points.begin(), p.points.end()); // turn first half upside down
                for (coord_t y = bounding_box.min(1); y <= bounding_box.max(1); y += m.y_short + m.hex_side + m.y_short + m.hex_side) {
                    // [INTENT] 5 points per hex period trace the two slanted sides and
                    // the two short horizontal segments of the hex edge.
                    p.points.push_back(Point(ax[1], y + m.y_offset));
                    p.points.push_back(Point(ax[0], y + m.y_short - m.y_offset));
                    p.points.push_back(Point(ax[0], y + m.y_short + m.hex_side + m.y_offset));
                    p.points.push_back(Point(ax[1], y + m.y_short + m.hex_side + m.y_short - m.y_offset));
                    p.points.push_back(Point(ax[1], y + m.y_short + m.hex_side + m.y_short + m.hex_side + m.y_offset));
                }
                ax[0] = ax[0] + m.distance;
                ax[1] = ax[1] + m.distance;
                std::swap(ax[0], ax[1]); // draw symmetrical pattern
                x += m.distance;
            }
            // [INTENT] Rotate polyline back from infill-direction space to world coordinates.
            p.rotate(-direction.first, m.hex_center);
            p.simplify(5 * spacing); // simplify to 5x line width  [HAZARD H343: unscaled spacing, ~no-op]
            all_polylines.push_back(p);
        }
    }
    // Apply multiline offset if needed
    multiline_fill(all_polylines, params, spacing);

    // [INTENT] Clip to ExPolygon boundary and connect for G-code travel efficiency.
    all_polylines = intersection_pl(std::move(all_polylines), expolygon);
    chain_or_connect_infill(std::move(all_polylines), expolygon, polylines_out, this->spacing, params);
}

} // namespace Slic3r
