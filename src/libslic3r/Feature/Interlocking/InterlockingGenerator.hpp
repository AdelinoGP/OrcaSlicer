// [INTENT] Generates interlocking beam microstructure between adjacent mesh
// regions printed in different materials.  The structure alternates horizontal
// beams 90° between even and odd layer groups, mechanically interlocking the
// two materials at their interface.
//
// Algorithm overview:
//   1. Build a voxel grid over the print object with cell_size = 2*beam_width XY,
//      2*beam_layer_count Z.
//   2. Mark voxels that contain boundary cells of each material (shell voxels).
//   3. Intersect the two sets to find cells containing both materials.
//   4. Optionally remove cells near air (air_filtering) to keep structure hidden.
//   5. For thin strips: expand one material's area into the other's thin strip
//      so that microstructure can adhere (handleThinAreas).
//   6. Generate beam polygons (generateMicrostructure) and apply to layer slices
//      (applyMicrostructureToOutlines).
//
// Origin: CuraEngine (Ultimaker B.V.), ported into OrcaSlicer.  AGPLv3+.
//
// [CONCURRENCY] The public static entry point `generate_interlocking_structure`
// iterates over region pairs sequentially.  Each `InterlockingGenerator` instance
// modifies layer slices in-place via `PrintObject::get_layer` — not thread-safe
// if called for overlapping region pairs concurrently.
//
// [COUPLING] Tightly coupled to PrintObject, LayerRegion, and VoxelUtils.
// Changes to PrintObject layer access patterns will require updates here.
//
// [MEMORY] Per-call allocations:
//   - Two unordered_sets of GridPoint3 (one per mesh) — bounded by voxel count.
//   - One vector<ExPolygons> per layer (computeUnionedVolumeRegions).
//   - generateMicrostructure: small fixed-size (2x2 ExPolygons).
//   - applyMicrostructureToOutlines: two vectors of ExPolygons per interlocking
//     layer — can be large for tall prints with many layers.
//
// [HAZARD H994 P2] `GridPoint3` hash (in InterlockingGenerator.cpp, std::hash
// specialization) uses int multiplication with a fixed prime and casts to
// `int` before converting to `size_t`.  For large coordinate values the
// intermediate `result * prime` overflows `int` — signed integer overflow is
// UB in C++.  The hash produces poor distribution for large coord values and
// may cause subtle performance degradation or collisions.
//
// [HAZARD H995 P2] `cell_size.z()` is set to `2 * beam_layer_count` where
// `beam_layer_count` is a `coord_t` (int32_t) derived from config.  For the
// interlocking Z dimension this is a layer *count*, not a physical distance in
// nm.  The voxel Z coordinate is used as a raw layer index throughout — this
// conflates the layer-count space with the coord_t world space and is a
// refactoring hazard.
//
// [HAZARD H996 P3] `ignored_gap_` is `const coord_t` with value 100u (100 nm).
// The `u` suffix casts to unsigned before assigning to signed coord_t — benign
// for this value but the mismatch is a style hazard.

// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef INTERLOCKING_GENERATOR_HPP
#define INTERLOCKING_GENERATOR_HPP

#include "libslic3r/Print.hpp"
#include "VoxelUtils.hpp"

namespace Slic3r {

/*!
 * Class for generating an interlocking structure between two adjacent models of a different extruder.
 *
 * The structure consists of horizontal beams of the two materials interlaced.
 * In the z direction the direction of these beams is alternated with 90*.
 *
 * Example with two materials # and O
 * Even beams:      Odd beams:
 * ######           ##OO##OO
 * OOOOOO           ##OO##OO
 * ######           ##OO##OO
 * OOOOOO           ##OO##OO
 *
 * One material of a single cell of the structure looks like this:
 *                    .-*-.
 *                .-*       *-.
 *               |*-.           *-.
 *               |    *-.           *-.
 *            .-* *-.     *-.           *-.
 *        .-*         *-.     *-.       .-*|
 *    .-*           .-*   *-.     *-.-*    |
 *   |*-.       .-*     .-*   *-.   |   .-*
 *   |    *-.-*     .-*           *-|-*
 *    *-.   |   .-*
 *        *-|-*
 *
 * We set up a voxel grid of (2*beam_w,2*beam_w,2*beam_h) and mark all the voxels which contain both meshes.
 * We then remove all voxels which also contain air, so that the interlocking pattern will not be visible from the outside.
 * We then generate and combine the polygons for each voxel and apply those areas to the outlines ofthe meshes.
 */
class InterlockingGenerator
{
public:
    /*!
     * Generate an interlocking structure between each two adjacent meshes.
     */
    static void generate_interlocking_structure(PrintObject* print_object);

private:
    /*!
     * Generate an interlocking structure between two meshes
     */
    void generateInterlockingStructure() const;

    /*!
     * Private class for storing some variables used in the computation of the interlocking structure between two meshes.
     * \param region_a_index The first region
     * \param region_b_index The second region
     * \param rotation The angle by which to rotate the interlocking pattern
     * \param cell_size The size of a voxel cell in (coord_t, coord_t, layer_count)
     * \param beam_layer_count The number of layers for the height of the beams
     * \param interface_dilation The thicknening kernel for the interface
     * \param air_dilation The thickening kernel applied to air so that cells near the outside of the model won't be generated
     * \param air_filtering Whether to fully remove all of the interlocking cells which would be visible on the outside (i.e. touching air).
     * If no air filtering then those cells will be cut off in the middle of a beam.
     */
    InterlockingGenerator(PrintObject&          print_object,
                          const size_t          region_a_index,
                          const size_t          region_b_index,
                          const coord_t         beam_width,
                          const coord_t         boundary_avoidance,
                          const float           rotation,
                          const Vec3crd&        cell_size,
                          const coord_t         beam_layer_count,
                          const DilationKernel& interface_dilation,
                          const DilationKernel& air_dilation,
                          const bool            air_filtering)
        : print_object(print_object)
        , region_a_index(region_a_index)
        , region_b_index(region_b_index)
        , beam_width(beam_width)
        , boundary_avoidance(boundary_avoidance)
        , vu(cell_size)
        , rotation(rotation)
        , cell_size(cell_size)
        , beam_layer_count(beam_layer_count)
        , interface_dilation(interface_dilation)
        , air_dilation(air_dilation)
        , air_filtering(air_filtering)
    {}

    /*! Given two polygons, return the parts that border on air, and grow 'perpendicular' up to 'detect' distance.
     *
     * \param a The first polygon.
     * \param b The second polygon.
     * \param detec The expand distance. (Not equal to offset, but a series of small offsets and differences).
     * \return A pair of polygons that repressent the 'borders' of a and b, but expanded 'perpendicularly'.
     */
    std::pair<ExPolygons, ExPolygons> growBorderAreasPerpendicular(const ExPolygons& a, const ExPolygons& b, const coord_t& detect) const;

    /*! Special handling for thin strips of material.
     *
     * Expand the meshes into each other where they need it, namely when a thin strip of material needs to be attached.
     * \param has_all_meshes Only do this special handling if there's actually microstructure nearby that needs to be adhered to.
     */
    void handleThinAreas(const std::unordered_set<GridPoint3>& has_all_meshes) const;

    /*!
     * Compute the voxels overlapping with the shell of both models.
     * This includes the walls, but also top/bottom skin.
     *
     * \param kernel The dilation kernel to give the returned voxel shell more thickness
     * \return The shell voxels for mesh a and those for mesh b
     */
    std::vector<std::unordered_set<GridPoint3>> getShellVoxels(const DilationKernel& kernel) const;

    /*!
     * Compute the voxels overlapping with the shell of some layers.
     * This includes the walls, but also top/bottom skin.
     *
     * \param layers The layer outlines for which to compute the shell voxels
     * \param kernel The dilation kernel to give the returned voxel shell more thickness
     * \param[out] cells The output cells which elong to the shell
     */
    void addBoundaryCells(const std::vector<ExPolygons>& layers, const DilationKernel& kernel, std::unordered_set<GridPoint3>& cells) const;

    /*!
     * Compute the regions occupied by both models.
     *
     * A morphological close is performed so that we don't register small gaps between the two models as being separate.
     * \return layer_regions The computed layer regions
     */
    std::vector<ExPolygons> computeUnionedVolumeRegions() const;

    /*!
     * Generate the polygons for the beams of a single cell
     * \return cell_area_per_mesh_per_layer The output polygons for each beam
     */
    std::vector<std::vector<ExPolygons>> generateMicrostructure() const;

    /*!
     * Change the outlines of the meshes with the computed interlocking structure.
     *
     * \param cells The cells where we want to apply the interlocking structure.
     * \param layer_regions The total volume of the two meshes combined (and small gaps closed)
     */
    void applyMicrostructureToOutlines(const std::unordered_set<GridPoint3>& cells, const std::vector<ExPolygons>& layer_regions) const;

    static const coord_t ignored_gap_ =
        100u; //!< Distance between models to be considered next to each other so that an interlocking structure will be generated there

    PrintObject&  print_object;
    const size_t  region_a_index;
    const size_t  region_b_index;
    const coord_t beam_width;
    const coord_t boundary_avoidance;

    const VoxelUtils vu;

    const float          rotation;
    const Vec3crd        cell_size;
    const coord_t        beam_layer_count;
    const DilationKernel interface_dilation;
    const DilationKernel air_dilation;
    // Whether to fully remove all of the interlocking cells which would be visible on the outside. If no air filtering then those cells
    // will be cut off midway in a beam.
    const bool air_filtering;
};

} // namespace Slic3r

#endif // INTERLOCKING_GENERATOR_HPP
