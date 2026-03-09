// [INTENT] Voxel grid utilities for the InterlockingGenerator feature.
// Provides a 3D voxel grid abstraction over Slic3r's coord_t coordinate space.
// Key operations: walking line segments, polygon boundaries, and filled polygon
// areas to identify which voxel cells they intersect.  Also provides dilation
// (morphological expansion) of cell sets via configurable kernels.
//
// Origin: CuraEngine (Ultimaker B.V.), ported into OrcaSlicer.  AGPLv3+.
//
// [STATE] VoxelUtils is effectively stateless except for `cell_size_` (set at
// construction, never mutated).  All walk methods take a callback — callers
// accumulate state.
//
// [MEMORY] `DilationKernel::relative_cells_` is computed and stored in the
// constructor (O(kernel_x * kernel_y * kernel_z) entries).  For large kernels
// this can be significant.
//
// [CONCURRENCY] All walk methods are const and re-entrant provided the
// `process_cell_func` callback itself is thread-safe.
//
// [COUPLING] Depends on Slic3r Polygon/ExPolygon types and the Fill subsystem
// (Fill::new_from_type(ipAlignedRectilinear) used in spreadDotsArea).
//
// [HAZARD H986 P2] `walkLine` inner `while(true)` loop has no explicit
// iteration limit.  A degenerate cell_size_ of 0 in any dimension would cause
// integer division by zero in `toGridCoord` and an infinite loop in `walkLine`.
// The only guard is the `assert(dim < 3)` which fires only in debug builds.
//
// [HAZARD H987 P3] `walkPolygons` docstring warns that voxels may be processed
// multiple times (at polygon vertex corners).  Callers using non-idempotent
// process_cell_func will silently double-count corner cells.
//
// [HAZARD H988 P2] `toGridCoord` formula `coord / cell_size - (coord < 0)` is a
// manual floor-divide.  For negative coordinates this subtracts 1 from the
// truncated result, which is correct for two's-complement signed integers but
// relies on implementation-defined behaviour for non-two's-complement targets
// (pre-C++20 technically UB; C++20 mandates two's complement).

// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_VOXEL_UTILS_H
#define UTILS_VOXEL_UTILS_H

#include <functional>

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

// [INTENT] 3D grid coordinate type — aliases Vec3crd (int32_t triplet).
// [HAZARD H989 P2] GridPoint3 is just a typedef for Vec3crd.  There is no
// type distinction between a grid-space coordinate and a world-space
// coord_t coordinate.  Mixing them compiles silently and produces wrong voxel
// lookups.  Callers must manually track which space they are in.
using GridPoint3 = Vec3crd;

/*!
 * Class for holding the relative positiongs wrt a reference cell on which to perform a dilation.
 */
// [INTENT] Kernel descriptor for morphological dilation of voxel cell sets.
// Stores pre-computed relative offsets for CUBE, DIAMOND, and PRISM shapes.
// [MEMORY] `relative_cells_` is computed eagerly in constructor; size is
// O(kernel_x * kernel_y * kernel_z).  Large kernels with CUBE type expand
// quadratically with depth.
// [HAZARD H990 P2] No validation that kernel_size components are odd.  An even
// kernel produces a slightly off-center dilation (asymmetric around input cell).
struct DilationKernel
{
    /*!
     * A cubic kernel checks all voxels in a cube around a reference voxel.
     *  _____
     * |\ ___\
     * | |    |
     *  \|____|
     *
     * A diamond kernel uses a manhattan distance to create a diamond shape around a reference voxel.
     *  /|\
     * /_|_\
     * \ | /
     *  \|/
     *
     * A prism kernel is diamond in XY, but extrudes straight in Z around a reference voxel.
     *   / \
     *  /   \
     * |\   /|
     * | \ / |
     * |  |  |
     *  \ | /
     *   \|/
     */
    enum class Type { CUBE, DIAMOND, PRISM };
    GridPoint3              kernel_size_; //!< Size of the kernel in number of voxel cells
    Type                    type_;
    std::vector<GridPoint3> relative_cells_; //!< All offset positions relative to some reference cell which is to be dilated

    DilationKernel(GridPoint3 kernel_size, Type type);
};

/*!
 * Utility class for walking over a 3D voxel grid.
 *
 * Contains the math for intersecting voxels with lines, polgons, areas, etc.
 */
// [INTENT] Voxel grid walking utility.
// `cell_size_` defines the size of one voxel in world (coord_t) units.
// All methods are const — grid configuration is immutable after construction.
// [HAZARD H986 P2] Zero cell_size_ in any dimension causes division by zero
// in toGridCoord and an infinite loop in walkLine.
class VoxelUtils
{
public:
    using grid_coord_t = coord_t;

    Vec3crd cell_size_;

    VoxelUtils(Vec3crd cell_size) : cell_size_(cell_size) {}

    /*!
     * Process voxels which a line segment crosses.
     *
     * \param start Start point of the line
     * \param end End point of the line
     * \param process_cell_func Function to perform on each cell the line crosses
     * \return Whether executing was stopped short as indicated by the \p cell_processing_function
     */
    bool walkLine(Vec3crd start, Vec3crd end, const std::function<bool(GridPoint3)>& process_cell_func) const;

    /*!
     * Process voxels which the line segments of a polygon crosses.
     *
     * \warning Voxels may be processed multiple times!
     *
     * \param polys The polygons to walk
     * \param z The height at which the polygons occur
     * \param process_cell_func Function to perform on each voxel cell
     * \return Whether executing was stopped short as indicated by the \p cell_processing_function
     */
    bool walkPolygons(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const;

    /*!
     * Process voxels near the line segments of a polygon.
     * For each voxel the polygon crosses we process each of the offset voxels according to the kernel.
     *
     * \warning Voxels may be processed multiple times!
     *
     * \param polys The polygons to walk
     * \param z The height at which the polygons occur
     * \param process_cell_func Function to perform on each voxel cell
     * \return Whether executing was stopped short as indicated by the \p cell_processing_function
     */
    bool walkDilatedPolygons(const ExPolygon&                       polys,
                             coord_t                                z,
                             const DilationKernel&                  kernel,
                             const std::function<bool(GridPoint3)>& process_cell_func) const;
    bool walkDilatedPolygons(const ExPolygons&                      polys,
                             coord_t                                z,
                             const DilationKernel&                  kernel,
                             const std::function<bool(GridPoint3)>& process_cell_func) const
    {
        for (const auto& poly : polys) {
            if (!walkDilatedPolygons(poly, z, kernel, process_cell_func)) {
                return false;
            }
        }

        return true;
    }

private:
    /*!
     * \warning the \p polys is assumed to be translated by half the cell_size in xy already
     */
    bool _walkAreas(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const;

public:
    /*!
     * Process all voxels inside the area of a polygons object.
     *
     * \warning The voxels along the area are not processed. Thin areas might not process any voxels at all.
     *
     * \param polys The area to fill
     * \param z The height at which the polygons occur
     * \param process_cell_func Function to perform on each voxel cell
     * \return Whether executing was stopped short as indicated by the \p cell_processing_function
     */
    bool walkAreas(const ExPolygon& polys, coord_t z, const std::function<bool(GridPoint3)>& process_cell_func) const;

    /*!
     * Process all voxels inside the area of a polygons object.
     * For each voxel inside the polygon we process each of the offset voxels according to the kernel.
     *
     * \warning The voxels along the area are not processed. Thin areas might not process any voxels at all.
     *
     * \param polys The area to fill
     * \param z The height at which the polygons occur
     * \param process_cell_func Function to perform on each voxel cell
     * \return Whether executing was stopped short as indicated by the \p cell_processing_function
     */
    bool walkDilatedAreas(const ExPolygon&                       polys,
                          coord_t                                z,
                          const DilationKernel&                  kernel,
                          const std::function<bool(GridPoint3)>& process_cell_func) const;
    bool walkDilatedAreas(const ExPolygons&                      polys,
                          coord_t                                z,
                          const DilationKernel&                  kernel,
                          const std::function<bool(GridPoint3)>& process_cell_func) const
    {
        for (const auto& poly : polys) {
            if (!walkDilatedAreas(poly, z, kernel, process_cell_func)) {
                return false;
            }
        }

        return true;
    }

    /*!
     * Dilate with a kernel.
     *
     * Extends the \p process_cell_func, so that for each cell we process nearby cells as well.
     *
     * Apply this function to a process_cell_func to create a new process_cell_func which applies the effect to nearby voxels as well.
     *
     * \param kernel The offset positions relative to the input of \p process_cell_func
     * \param process_cell_func Function to perform on each voxel cell
     */
    std::function<bool(GridPoint3)> dilate(const DilationKernel& kernel, const std::function<bool(GridPoint3)>& process_cell_func) const;

    GridPoint3 toGridPoint(const Vec3crd& point) const
    {
        return GridPoint3(toGridCoord(point.x(), 0), toGridCoord(point.y(), 1), toGridCoord(point.z(), 2));
    }

    grid_coord_t toGridCoord(const coord_t& coord, const size_t dim) const
    {
        assert(dim < 3);
        return coord / cell_size_[dim] - (coord < 0);
    }

    Vec3crd toLowerCorner(const GridPoint3& location) const
    {
        return Vec3crd(toLowerCoord(location.x(), 0), toLowerCoord(location.y(), 1), toLowerCoord(location.z(), 2));
    }

    coord_t toLowerCoord(const grid_coord_t& grid_coord, const size_t dim) const
    {
        assert(dim < 3);
        return grid_coord * cell_size_[dim];
    }

    /*!
     * Returns a rectangular polygon equal to the cross section of a voxel cell at coordinate \p p
     */
    Polygon toPolygon(const GridPoint3 p) const
    {
        Polygon ret;
        Vec3crd c = toLowerCorner(p);
        ret.append({c.x(), c.y()});
        ret.append({c.x() + cell_size_.x(), c.y()});
        ret.append({c.x() + cell_size_.x(), c.y() + cell_size_.y()});
        ret.append({c.x(), c.y() + cell_size_.y()});
        return ret;
    }
};

} // namespace Slic3r

#endif // UTILS_VOXEL_UTILS_H
