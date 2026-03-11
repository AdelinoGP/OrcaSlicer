// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.
//
// [INTENT] DistanceField.cpp — discrete unsupported-area tracker for Lightning infill.
// The constructor rasterizes unsupported overhangs into sample cells ranked by distance from
// the boundary; `update()` erases those cells when a new Lightning branch makes them supported.
// Layer::generateNewTrees() repeatedly alternates between `tryGetNextPoint()` and `update()`.
//
// [COUPLING] Uses `sample_grid_pattern()` from FillRectilinear for sampling, Clipper offsets to
// sanitize input polygons before sampling, and TBB to parallelize the per-sample distance-to-
// boundary computation.
// [CONCURRENCY] Only the constructor is parallelized; the resulting vectors are sized up front so
//               each TBB worker writes to a unique index. `update()` is intentionally single-threaded.

#include "DistanceField.hpp" //Class we're implementing.
#include "../FillRectilinear.hpp"
#include "../../ClipperUtils.hpp"

#include <tbb/parallel_for.h>

#ifdef LIGHTNING_DISTANCE_FIELD_DEBUG_OUTPUT
#include "../../SVG.hpp"
#endif

namespace Slic3r::FillLightning {

// [INTENT] Heuristic converting branch support radius -> sample grid spacing.
// Smaller values sample more densely (better fidelity, slower construction); larger values risk
// missing narrow unsupported slivers. The chosen 6 means roughly six grid cells fit inside the
// support radius.
// [HAZARD] Hardcoded heuristic. No printer/profile setting exposes it, so Lightning density tuning
// cannot directly trade construction cost against support fidelity.
constexpr coord_t radius_per_cell_size = 6;

#ifdef LIGHTNING_DISTANCE_FIELD_DEBUG_OUTPUT
void export_distance_field_to_svg(const std::string&                               path,
                                  const Polygons&                                  outline,
                                  const Polygons&                                  overhang,
                                  const std::list<DistanceField::UnsupportedCell>& unsupported_points,
                                  const Points&                                    points = {})
{
    coordf_t    stroke_width = scaled<coordf_t>(0.01);
    BoundingBox bbox         = get_extents(outline);

    bbox.offset(SCALED_EPSILON);
    SVG svg(path, bbox);
    svg.draw_outline(outline, "green", stroke_width);
    svg.draw_outline(overhang, "blue", stroke_width);

    for (const DistanceField::UnsupportedCell& cell : unsupported_points)
        svg.draw(cell.loc, "cyan", coord_t(stroke_width));

    for (const Point& pt : points)
        svg.draw(pt, "red", coord_t(stroke_width));
}
#endif

DistanceField::DistanceField(const coord_t&     radius,
                             const Polygons&    current_outline,
                             const BoundingBox& current_outlines_bbox,
                             const Polygons&    current_overhang)
    : m_cell_size(radius / radius_per_cell_size), m_supporting_radius(radius), m_unsupported_points_bbox(current_outlines_bbox)
{
    // [STATE] Cache squared radius once so later inside-circle tests in update() stay integer-only.
    m_supporting_radius2 = Slic3r::sqr(int64_t(radius));
    // [INTENT] Sample the unsupported overhang polygons with a regular world-aligned lattice.
    // `offset2_ex(..., -cell/2, +cell/2)` first removes dangling 1-pixel features that break the
    // grid sampler's OUTER_LOW assumptions, then slightly re-expands so the sampled shape roughly
    // matches the original overhang extent.
    // [HAZARD] Very thin overhangs narrower than `m_cell_size` can vanish in this cleanup step,
    //          meaning the Lightning planner never sees them as unsupported.
    const BoundingBox overhang_bbox = get_extents(current_overhang);
    ExPolygons        expolys       = offset2_ex(union_ex(current_overhang), -m_cell_size / 2,
                                                 m_cell_size /
                                                     2); // remove dangling lines which causes sample_grid_pattern crash (fails the OUTER_LOW assertions)
    for (const ExPolygon& expoly : expolys) {
        const Points sampled_points               = sample_grid_pattern(expoly, m_cell_size, overhang_bbox);
        const size_t unsupported_points_prev_size = m_unsupported_points.size();
        m_unsupported_points.resize(unsupported_points_prev_size + sampled_points.size());

        // [CONCURRENCY] Each worker computes one sampled cell's distance to the nearest contour edge
        // and writes into a unique slot (`unsupported_points_prev_size + sp_idx`), so no locking is required.
        // [INTENT] Distance to boundary approximates how "deep" inside the unsupported island the
        // point lies; later sorting serves deeper interior cells before boundary-adjacent cells.
        tbb::parallel_for(tbb::blocked_range<size_t>(0, sampled_points.size()),
                          [&self = *this, &expoly = std::as_const(expoly), &sampled_points = std::as_const(sampled_points),
                           &unsupported_points_prev_size = std::as_const(unsupported_points_prev_size)](
                              const tbb::blocked_range<size_t>& range) -> void {
                              for (size_t sp_idx = range.begin(); sp_idx < range.end(); ++sp_idx) {
                                  const Point& sp = sampled_points[sp_idx];
                                  // Find a squared distance to the source expolygon boundary.
                                  double d2 = std::numeric_limits<double>::max();
                                  for (size_t icontour = 0; icontour <= expoly.holes.size(); ++icontour) {
                                      const Polygon& contour = icontour == 0 ? expoly.contour : expoly.holes[icontour - 1];
                                      if (contour.size() > 2) {
                                          Point prev = contour.points.back();
                                          for (const Point& p2 : contour.points) {
                                              d2   = std::min(d2, Line::distance_to_squared(sp, prev, p2));
                                              prev = p2;
                                          }
                                      }
                                  }
                                  self.m_unsupported_points[unsupported_points_prev_size + sp_idx] = {sp, coord_t(std::sqrt(d2))};
                                  assert(self.m_unsupported_points_bbox.contains(sp));
                              }
                          }); // end of parallel_for
    }
    // [INTENT] Stable sort keeps cells with similar distance in a deterministic pseudo-random spread
    // (hash modulo prime), preventing visible directional bias while still prioritizing interior cells.
    // Cells whose distance differs by more than one support radius are strictly ordered by distance.
    // [HAZARD] The PointHash modulo tie-break is heuristic rather than geometrically meaningful; ports
    // that replace it with insertion order may generate different but still valid branch layouts.
    std::stable_sort(m_unsupported_points.begin(), m_unsupported_points.end(),
                     [&radius](const UnsupportedCell& a, const UnsupportedCell& b) {
                         constexpr coord_t prime_for_hash = 191;
                         return std::abs(b.dist_to_boundary - a.dist_to_boundary) > radius ?
                                    a.dist_to_boundary < b.dist_to_boundary :
                                    (PointHash{}(a.loc) % prime_for_hash) < (PointHash{}(b.loc) % prime_for_hash);
                     });

    m_unsupported_points_erased.resize(m_unsupported_points.size());
    std::fill(m_unsupported_points_erased.begin(), m_unsupported_points_erased.end(), false);

    // [INTENT] Build dense grid-address lookup after sorting. The lookup stores vector indices, so
    // it must be initialized AFTER the final order is known.
    m_unsupported_points_grid.initialize(m_unsupported_points,
                                         [&self = std::as_const(*this)](const Point& p) -> Point { return self.to_grid_point(p); });

    // Because the distance between two points is at least one axis equal to m_cell_size, every cell
    // in m_unsupported_points_grid contains exactly one point.
    assert(m_unsupported_points.size() == m_unsupported_points_grid.size());

#ifdef LIGHTNING_DISTANCE_FIELD_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_distance_field_to_svg(debug_out_path("FillLightning-DistanceField-%d.svg", iRun++), current_outline, current_overhang,
                                     m_unsupported_points);
    }
#endif
}

void DistanceField::update(const Point& to_node, const Point& added_leaf)
{
    // [INTENT] Model the newly added branch as a capsule: a circle around `added_leaf` plus the
    // swept rectangle from `to_node` to `added_leaf`. Every sampled cell inside that capsule is now supported.
    Vec2d v      = (added_leaf - to_node).cast<double>();
    auto  l2     = v.squaredNorm();
    Vec2d extent = Vec2d(-v.y(), v.x()) * m_supporting_radius / sqrt(l2);

    BoundingBox grid;
    {
        Point diagonal(m_supporting_radius, m_supporting_radius);
        Point iextent(extent.cast<coord_t>());
        grid = BoundingBox(added_leaf - diagonal, added_leaf + diagonal);
        grid.merge(to_node - iextent);
        grid.merge(to_node + iextent);
        grid.merge(added_leaf - iextent);
        grid.merge(added_leaf + iextent);

        // [INTENT] Clip the candidate search box to the sampled field extent before converting to
        // local grid coordinates. This keeps the later row/column loops bounded and avoids negative indices.
        grid.min.x() = std::max(grid.min.x(), m_unsupported_points_bbox.min.x());
        grid.min.y() = std::max(grid.min.y(), m_unsupported_points_bbox.min.y());
        grid.max.x() = std::min(grid.max.x(), m_unsupported_points_bbox.max.x());
        grid.max.y() = std::min(grid.max.y(), m_unsupported_points_bbox.max.y());

        grid.min = this->to_grid_point(grid.min);
        grid.max = this->to_grid_point(grid.max);
    }

    Point grid_addr;
    Point grid_loc;
    for (grid_addr.y() = grid.min.y(); grid_addr.y() <= grid.max.y(); ++grid_addr.y()) {
        for (grid_addr.x() = grid.min.x(); grid_addr.x() <= grid.max.x(); ++grid_addr.x()) {
            grid_loc = this->from_grid_point(grid_addr);
            // Test inside a circle at the new leaf.
            if ((grid_loc - added_leaf).cast<int64_t>().squaredNorm() > m_supporting_radius2) {
                // Not inside a circle at the end of the new leaf.
                // Test inside a rotated rectangle.
                Vec2d  vx = (grid_loc - to_node).cast<double>();
                double d  = v.dot(vx);
                if (d >= 0 && d <= l2) {
                    d = extent.dot(vx);
                    if (d < -1. || d > 1.)
                        // Not inside a rotated rectangle.
                        continue;
                }
            }
            // Inside a circle at the end of the new leaf, or inside a rotated rectangle.
            // Remove unsupported leafs at this grid location.
            if (const size_t cell_idx = m_unsupported_points_grid.find_cell_idx(grid_addr); cell_idx != std::numeric_limits<size_t>::max()) {
                const UnsupportedCell& cell = m_unsupported_points[cell_idx];
                if ((cell.loc - added_leaf).cast<int64_t>().squaredNorm() <= m_supporting_radius2) {
                    // [STATE] Mark both the dense-grid entry and the parallel erased bitmap so future
                    // tryGetNextPoint() scans skip this cell and size()-style queries stay accurate.
                    m_unsupported_points_erased[cell_idx] = true;
                    m_unsupported_points_grid.mark_erased(grid_addr);
                }
            }
        }
    }
}

#if 0
void DistanceField::update(const Point &to_node, const Point &added_leaf)
{
    const Point supporting_radius_point(m_supporting_radius, m_supporting_radius);
    const BoundingBox grid(this->to_grid_point(added_leaf - supporting_radius_point), this->to_grid_point(added_leaf + supporting_radius_point));

    for (coord_t grid_y = grid.min.y(); grid_y <= grid.max.y(); ++grid_y) {
        for (coord_t grid_x = grid.min.x(); grid_x <= grid.max.x(); ++grid_x) {
            if (auto it = m_unsupported_points_grid.find({grid_x, grid_y}); it != m_unsupported_points_grid.end()) {
                std::list<UnsupportedCell>::iterator &list_it = it->second;
                UnsupportedCell                      &cell    = *list_it;
                if ((cell.loc - added_leaf).cast<int64_t>().squaredNorm() <= m_supporting_radius2) {
                    m_unsupported_points.erase(list_it);
                    m_unsupported_points_grid.erase(it);
                }
            }
        }
    }
}
#endif

} // namespace Slic3r::FillLightning
