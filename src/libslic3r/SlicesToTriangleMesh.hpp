#ifndef SLICESTOTRIANGLEMESH_HPP
#define SLICESTOTRIANGLEMESH_HPP

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

// [INTENT] Reconstruct a watertight-ish triangle shell from ordered layer cross-sections,
// primarily for debugging, preview, and geometry post-processing paths.
// [COUPLING] Expects ExPolygon slice topology and Z stepping semantics produced by the slicing pipeline.
// [STATE] Appends/replaces triangles inside `mesh`; caller controls object lifetime and reuse.
void slices_to_mesh(indexed_triangle_set& mesh, const std::vector<ExPolygons>& slices, double zmin, double lh, double ilh);

inline indexed_triangle_set slices_to_mesh(const std::vector<ExPolygons>& slices, double zmin, double lh, double ilh)
{
    // [MEMORY] Value-return overload builds into a local mesh and relies on NRVO/move elision
    // to avoid copying large triangle buffers.
    indexed_triangle_set out;
    slices_to_mesh(out, slices, zmin, lh, ilh);

    return out;
}

} // namespace Slic3r

#endif // SLICESTOTRIANGLEMESH_HPP
