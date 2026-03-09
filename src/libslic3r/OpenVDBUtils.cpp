// [INTENT] Implementation of mesh↔OpenVDB FloatGrid SDF conversions and SDF rebuild.
//
// [ALGORITHM: mesh_to_grid]
//   1. Split mesh into connected components (its_split).
//   2. Filter out components with volume < EPSILON (degenerate/flat parts).
//   3. For each surviving component, call openvdb::tools::meshToVolume to produce a SDF.
//   4. CSG-union all sub-grids into a single grid.
//   5. Rebuild the SDF with levelSetRebuild to repair any band corruption from union.
//   6. FALLBACK: if its_split returned empty, convert the original unsplit mesh directly.
//   7. Store voxel_scale in grid metadata for use by grid_to_mesh.
//
// [ALGORITHM: grid_to_mesh]
//   1. Run marching cubes (volumeToMesh) to produce Vec3s points + Vec3I triangles + Vec4I quads.
//   2. Read voxel_scale from grid metadata (silent default=1.0 on error).
//   3. Scale vertices back to original mesh space by dividing by voxel_scale.
//   4. Split quads into two triangles each.
//
// [ALGORITHM: redistance_grid]
//   1. Call levelSetRebuild with new iso/ext/int parameters.
//   2. Copy all metadata from input grid to new grid (preserves voxel_scale).
//
// [MEMORY] TriangleMeshDataAdapter is a lightweight adapter struct — holds a const
// reference to the ITS and the voxel_scale. No heap allocation.
// meshparts (from its_split) is a vector of indexed_triangle_set copies — O(N) total.
//
// [CONCURRENCY] openvdb::initialize() at top of each function serialises concurrent
// callers. Not thread-safe on first call but idempotent afterwards (internal mutex).
// Parallel callers to mesh_to_grid should call openvdb::initialize() at startup.
//
// [COUPLING] its_split, its_volume from TriangleMesh.hpp.
// openvdb::tools::meshToVolume, volumeToMesh, levelSetRebuild, csgUnion from OpenVDB.

#define NOMINMAX
#include "OpenVDBUtils.hpp"

#ifdef _MSC_VER
// Suppress warning C4146 in OpenVDB: unary minus operator applied to unsigned type, result still unsigned
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include <openvdb/tools/MeshToVolume.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetRebuild.h>

// #include "MTUtils.hpp"

namespace Slic3r {

// [INTENT] OpenVDB mesh adapter — presents an indexed_triangle_set to OpenVDB's
// meshToVolume in "index space" (scaled by voxel_scale to control resolution).
// OpenVDB index space: 1 unit = 1 voxel. By scaling mesh coordinates by voxel_scale
// before passing to meshToVolume, we effectively increase the voxel density.
// [MEMORY] Holds a const reference to ITS — caller must ensure ITS outlives adapter.
class TriangleMeshDataAdapter
{
public:
    const indexed_triangle_set& its;
    float                       voxel_scale;

    size_t polygonCount() const { return its.indices.size(); }
    size_t pointCount() const { return its.vertices.size(); }
    size_t vertexCount(size_t) const { return 3; }

    // Return position pos in local grid index space for polygon n and vertex v
    // The actual mesh will appear to openvdb as scaled uniformly by voxel_size
    // And the voxel count per unit volume can be affected this way.
    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const
    {
        auto          vidx = size_t(its.indices[n](Eigen::Index(v)));
        Slic3r::Vec3d p    = its.vertices[vidx].cast<double>() * voxel_scale;
        pos                = {p.x(), p.y(), p.z()};
    }

    TriangleMeshDataAdapter(const indexed_triangle_set& m, float voxel_sc = 1.f) : its{m}, voxel_scale{voxel_sc} {};
};

// TODO: Do I need to call initialize? Seems to work without it as well but the
// docs say it should be called ones. It does a mutex lock-unlock sequence all
// even if was called previously.
// [HAZARD H1012] openvdb::initialize() called unconditionally on every invocation.
// Should be called once at application startup. Performance overhead: mutex lock/unlock
// on every mesh_to_grid call even when already initialised.
openvdb::FloatGrid::Ptr mesh_to_grid(const indexed_triangle_set&     mesh,
                                     const openvdb::math::Transform& tr,
                                     float                           voxel_scale,
                                     float                           exteriorBandWidth,
                                     float                           interiorBandWidth,
                                     int                             flags)
{
    openvdb::initialize(); // [HAZARD H1012]

    std::vector<indexed_triangle_set> meshparts = its_split(mesh);

    // Filter out degenerate/near-zero-volume parts (e.g. flat triangles, open shells)
    auto it = std::remove_if(meshparts.begin(), meshparts.end(), [](auto& m) { return its_volume(m) < EPSILON; });

    meshparts.erase(it, meshparts.end());

    openvdb::FloatGrid::Ptr grid;
    for (auto& m : meshparts) {
        auto subgrid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(TriangleMeshDataAdapter{m, voxel_scale}, tr, exteriorBandWidth,
                                                                        interiorBandWidth, flags);

        // CSG union of all connected components into single grid
        if (grid && subgrid)
            openvdb::tools::csgUnion(*grid, *subgrid);
        else if (subgrid)
            grid = std::move(subgrid);
    }

    if (grid) {
        // Rebuild SDF to repair band corruption from csgUnion
        grid = openvdb::tools::levelSetRebuild(*grid, 0., exteriorBandWidth, interiorBandWidth);
    } else if (meshparts.empty()) {
        // [INTENT] Fallback: its_split returned empty (degenerate mesh) —
        // convert the original mesh directly without splitting.
        // NOTE: this branch is only reached if its_split returned empty.
        // If its_split returned non-empty but volume filtering removed all parts,
        // grid remains nullptr and the `grid->insertMeta(...)` below crashes.
        // [HAZARD H1013] See header — null crash path if volume filter removes all.
        grid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(TriangleMeshDataAdapter{mesh}, tr, exteriorBandWidth, interiorBandWidth,
                                                                flags);
    }

    // Store voxel_scale in grid metadata so grid_to_mesh can invert the scaling.
    // [HAZARD H1013] If grid is null (all parts filtered by volume), this crashes.
    grid->insertMeta("voxel_scale", openvdb::FloatMetadata(voxel_scale));

    return grid;
}

// [INTENT] Marching-cubes conversion from FloatGrid SDF to indexed_triangle_set.
// Reads voxel_scale from grid metadata to undo the scaling applied in mesh_to_grid.
// [HAZARD H1014] voxel_scale metadata read wrapped in bare catch(...){} — silent fallback
// to scale=1.0. Any exception (key not found, type mismatch) is silently ignored.
// If grid was created with voxel_scale=2.0, the output vertices are at 2x their correct
// positions with no warning.
indexed_triangle_set grid_to_mesh(const openvdb::FloatGrid& grid, double isovalue, double adaptivity, bool relaxDisorientedTriangles)
{
    openvdb::initialize(); // [HAZARD H1012]

    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> triangles;
    std::vector<openvdb::Vec4I> quads;

    openvdb::tools::volumeToMesh(grid, points, triangles, quads, isovalue, adaptivity, relaxDisorientedTriangles);

    float scale = 1.;
    try {
        scale = grid.template metaValue<float>("voxel_scale");
    } catch (...) {} // [HAZARD H1014] silently defaults to scale=1.0

    indexed_triangle_set ret;
    ret.vertices.reserve(points.size());
    ret.indices.reserve(triangles.size() + quads.size() * 2);

    // Undo voxel scaling — divide by voxel_scale to return to original mesh space.
    for (auto& v : points)
        ret.vertices.emplace_back(to_vec3f(v) / scale);
    for (auto& v : triangles)
        ret.indices.emplace_back(to_vec3i(v));
    // Split quads into two triangles: (0,1,2) and (2,3,0)
    for (auto& quad : quads) {
        ret.indices.emplace_back(quad(0), quad(1), quad(2));
        ret.indices.emplace_back(quad(2), quad(3), quad(0));
    }

    return ret;
}

// [INTENT] Repair/rebuild the SDF band of an existing FloatGrid using new iso/range
// parameters. Used after boolean operations that may corrupt the narrow band.
// Copies all metadata (including voxel_scale) from input grid to rebuilt grid.
// [HAZARD H1015] `float(er)` and `float(ir)` silently truncate double→float.
// For very large band widths (er/ir > ~3.4e38) this wraps to infinity or NaN.
// Typical usage (er=3., ir=3.) is fine; but callers should be aware of the truncation.
openvdb::FloatGrid::Ptr redistance_grid(const openvdb::FloatGrid& grid, double iso, double er, double ir)
{
    auto new_grid = openvdb::tools::levelSetRebuild(grid, float(iso), float(er), float(ir)); // [HAZARD H1015]

    // Copies voxel_scale metadata, if it exists.
    new_grid->insertMeta(*grid.deepCopyMeta());

    return new_grid;
}

} // namespace Slic3r
