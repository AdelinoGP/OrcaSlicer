// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.
//
// [INTENT] DistanceField.hpp — discrete coverage field used by Lightning infill tree growth.
// The class samples unsupported overhang area on a regular grid, stores one `UnsupportedCell`
// per sample point, ranks cells by distance-to-boundary, and then lets Layer::generateNewTrees()
// consume cells in that priority order while marking nearby cells as newly supported.
//
// Core contract with Layer.cpp:
//   1. Constructor: current_overhang polygons -> sampled cells sorted by interior priority.
//   2. tryGetNextPoint(): yields the next cell not yet erased by an earlier branch.
//   3. update(): erases cells covered by a newly added branch segment.
//
// [COUPLING] Depends on `FillRectilinear::sample_grid_pattern()` for regular polygon sampling,
//            `BoundingBox` / `Point` / `Polygon` core geometry types, and on `Layer` assuming
//            that the order of `m_unsupported_points` is stable across repeated scans.
// [STATE] `m_unsupported_points` + `m_unsupported_points_erased` encode the mutable frontier of
//         unsupported area. `UnsupportedPointsGrid` mirrors that frontier in grid-address space so
//         `update()` can erase cells in O(covered_cells) rather than searching the whole vector.
// [CONCURRENCY] Construction uses `tbb::parallel_for` to compute each sample's distance to the
//               nearest boundary, but after construction the object is mutated single-threadedly by
//               Layer::generateNewTrees(). `tryGetNextPoint()` and `update()` are not synchronized.
// [HAZARD] The field stores exactly one sample per grid cell. If sampling density changes or points
//          are inserted off-grid, `UnsupportedPointsGrid::initialize()` assertions fail because the
//          1-cell -> 1-point mapping is a hard invariant.

#ifndef LIGHTNING_DISTANCE_FIELD_H
#define LIGHTNING_DISTANCE_FIELD_H

#include "../../BoundingBox.hpp"
#include "../../Point.hpp"
#include "../../Polygon.hpp"

// #define LIGHTNING_DISTANCE_FIELD_DEBUG_OUTPUT

namespace Slic3r::FillLightning {

// [INTENT] 2D field of discrete unsupported locations for Lightning infill.
// Each sample cell carries its center point and the distance to the current overhang boundary;
// Layer.cpp uses that distance as a heuristic to support deep interior cells before edge cells.
class DistanceField
{
public:
    // [INTENT] Constructor samples `current_overhang` on a regular grid, computes each sample's
    // distance to the nearest polygon boundary, sorts cells by that distance, and builds the
    // grid-address lookup used by `update()`.
    // [COUPLING] `current_outlines_bbox` must be the bounding box of the infill outlines, not just
    // the overhang, because `to_grid_point()` and `from_grid_point()` use it as the canonical
    // origin shared with Layer::SparseNodeGrid.
    // [HAZARD] `radius` also determines `m_cell_size` (`radius / radius_per_cell_size` in .cpp).
    // Very small radii collapse many samples onto the same grid cell; very large radii under-sample
    // the overhang and produce sparse trees. There is no validation here.
    DistanceField(const coord_t&     radius,
                  const Polygons&    current_outline,
                  const BoundingBox& current_outlines_bbox,
                  const Polygons&    current_overhang);

    // [INTENT] Linear scan for the next still-live unsupported cell. `start_idx` lets the caller
    // resume scanning from the last returned location so the total work across the full growth loop
    // is amortized O(N_cells) rather than O(N_cells^2).
    // [STATE] Read-only: only consults `m_unsupported_points_erased` and writes to the output params.
    // [HAZARD] Output parameters are raw pointers and are unguarded; callers must pass non-null
    //          storage for both or crash in release builds.
    bool tryGetNextPoint(Point* out_unsupported_location, size_t* out_unsupported_cell_idx, const size_t start_idx = 0) const
    {
        for (size_t point_idx = start_idx; point_idx < m_unsupported_points.size(); ++point_idx) {
            if (!m_unsupported_points_erased[point_idx]) {
                *out_unsupported_cell_idx = point_idx;
                *out_unsupported_location = m_unsupported_points[point_idx].loc;
                return true;
            }
        }

        return false;
    }

    // [INTENT] Marks cells as supported after Layer adds a new branch from `to_node` to `added_leaf`.
    // The implementation approximates the branch influence as a capsule (segment + end circle)
    // and erases every sampled cell inside that capsule.
    // [STATE] Mutates both `m_unsupported_points_erased` and `m_unsupported_points_grid`.
    // [COUPLING] Layer assumes erased cells disappear from future `tryGetNextPoint()` scans but that
    // surviving indices remain stable, so this method marks/filters cells in place rather than removing them.
    void update(const Point& to_node, const Point& added_leaf);

protected:
    // [STATE] Grid spacing for unsupported sample points. Derived once from supporting radius.
    coord_t m_cell_size;

    // [STATE] Branch support radius in scaled coordinates, plus squared form for fast distance tests.
    coord_t m_supporting_radius;
    int64_t m_supporting_radius2;

    // [STATE] Represents one sampled unsupported location.
    // `dist_to_boundary` is a priority heuristic, not an exact geometric signed distance field.
    struct UnsupportedCell
    {
        // The position of the center of this cell.
        Point loc;
        // How far this cell is removed from the ``current_outline`` polygon, the edge of the infill area.
        coord_t dist_to_boundary;
    };

    // [STATE] Dense storage of all sampled cells in priority order, plus a parallel erased bitmap.
    // The vector order is intentionally stable after construction so Layer can carry an index cursor.
    std::vector<UnsupportedCell> m_unsupported_points;
    std::vector<bool>            m_unsupported_points_erased;

    // [STATE] Canonical origin/range for the grid-address transform. Kept const after construction.
    const BoundingBox m_unsupported_points_bbox;

    // [INTENT] Dense 2D lookup table from grid address -> index into `m_unsupported_points`.
    // This avoids hashing during update() and lets the field erase cells by visiting only the
    // capsule's covered bounding box.

    class UnsupportedPointsGrid
    {
    public:
        UnsupportedPointsGrid() = default;
        // [INTENT] Builds the dense grid lookup once after the constructor finishes sampling.
        // [HAZARD] Assumes one unique unsupported cell per grid address; duplicate samples assert.
        void initialize(const std::vector<UnsupportedCell>& unsupported_points, const std::function<Point(const Point&)>& map_cell_to_grid)
        {
            if (unsupported_points.empty())
                return;

            BoundingBox unsupported_points_bbox;
            for (const UnsupportedCell& cell : unsupported_points)
                unsupported_points_bbox.merge(cell.loc);

            m_size       = unsupported_points.size();
            m_grid_range = BoundingBox(map_cell_to_grid(unsupported_points_bbox.min), map_cell_to_grid(unsupported_points_bbox.max));
            m_grid_size  = m_grid_range.size() + Point::Ones();

            m_data.assign(m_grid_size.y() * m_grid_size.x(), std::numeric_limits<size_t>::max());
            m_data_erased.assign(m_grid_size.y() * m_grid_size.x(), true);

            for (size_t cell_idx = 0; cell_idx < unsupported_points.size(); ++cell_idx) {
                const size_t flat_idx = map_to_flat_array(map_cell_to_grid(unsupported_points[cell_idx].loc));
                assert(m_data[flat_idx] == std::numeric_limits<size_t>::max());
                m_data[flat_idx]        = cell_idx;
                m_data_erased[flat_idx] = false;
            }
        }

        size_t size() const { return m_size; }

        // [INTENT] Query the still-live cell at a grid address. Returns max() if the address is
        // outside the dense range or if the cell was previously erased.
        size_t find_cell_idx(const Point& grid_addr)
        {
            if (!m_grid_range.contains(grid_addr))
                return std::numeric_limits<size_t>::max();

            if (const size_t flat_idx = map_to_flat_array(grid_addr); !m_data_erased[flat_idx]) {
                assert(m_data[flat_idx] != std::numeric_limits<size_t>::max());
                return m_data[flat_idx];
            }

            return std::numeric_limits<size_t>::max();
        }

        // [STATE] Marks a dense-grid entry as erased and decrements `m_size`.
        // [HAZARD] Caller must ensure the address was previously live; debug asserts guard this but
        // release mode trusts the caller.
        void mark_erased(const Point& grid_addr)
        {
            assert(m_grid_range.contains(grid_addr));
            if (!m_grid_range.contains(grid_addr))
                return;

            const size_t flat_idx = map_to_flat_array(grid_addr);
            assert(!m_data_erased[flat_idx] && m_data[flat_idx] != std::numeric_limits<size_t>::max());
            assert(m_size != 0);

            m_data_erased[flat_idx] = true;
            --m_size;
        }

    private:
        size_t m_size = 0;

        BoundingBox m_grid_range;
        Point       m_grid_size;

        std::vector<size_t> m_data;
        std::vector<bool>   m_data_erased;

        // [INTENT] Row-major flattening from 2D grid address into `m_data` / `m_data_erased`.
        inline size_t map_to_flat_array(const Point& loc) const
        {
            const Point  offset_loc = loc - m_grid_range.min;
            const size_t flat_idx   = m_grid_size.x() * offset_loc.y() + offset_loc.x();
            assert(offset_loc.x() >= 0 && offset_loc.y() >= 0);
            assert(flat_idx < size_t(m_grid_size.y() * m_grid_size.x()));
            return flat_idx;
        }
    };

    UnsupportedPointsGrid m_unsupported_points_grid;

    // [INTENT] Convert world-space scaled coordinates into dense grid coordinates relative to
    // `m_unsupported_points_bbox.min`. Used for both unsupported-cell lookup and the update sweep.
    Point to_grid_point(const Point& point) const { return (point - m_unsupported_points_bbox.min) / m_cell_size; }

    // [INTENT] Inverse of `to_grid_point()` for grid-cell centers expressed in this field's local frame.
    Point from_grid_point(const Point& point) const { return point * m_cell_size + m_unsupported_points_bbox.min; }

#ifdef LIGHTNING_DISTANCE_FIELD_DEBUG_OUTPUT
    friend void export_distance_field_to_svg(const std::string&                               path,
                                             const Polygons&                                  outline,
                                             const Polygons&                                  overhang,
                                             const std::list<DistanceField::UnsupportedCell>& unsupported_points,
                                             const Points&                                    points);
#endif
};

} // namespace Slic3r::FillLightning

#endif // LIGHTNING_DISTANCE_FIELD_H
