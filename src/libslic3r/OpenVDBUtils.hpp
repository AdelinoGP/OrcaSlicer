#ifndef OPENVDBUTILS_HPP
#define OPENVDBUTILS_HPP

// [INTENT] Thin adapter layer between OrcaSlicer's TriangleMesh / indexed_triangle_set
// and OpenVDB FloatGrid level-set volumes. Provides three operations:
//   1. mesh_to_grid: convert a triangle mesh to a signed-distance-field (SDF) FloatGrid.
//   2. grid_to_mesh: convert a FloatGrid SDF back to a triangle mesh via marching cubes.
//   3. redistance_grid: rebuild a FloatGrid SDF from scratch to repair corrupted bands.
//
// [COUPLING] Hard dependency on OpenVDB library (openvdb::FloatGrid, openvdb::math::Transform,
// openvdb::tools::meshToVolume, volumeToMesh, levelSetRebuild, csgUnion).
// Thin include wrapper only — all implementation in OpenVDBUtils.cpp.
//
// [MEMORY] FloatGrid::Ptr is a shared_ptr<FloatGrid>. Ownership follows shared_ptr semantics.
// Callers may hold references to the same grid without copying.
//
// [CONCURRENCY] OpenVDB's openvdb::initialize() is NOT thread-safe on first call but
// idempotent after. The TODO comment in the .cpp acknowledges this.
// mesh_to_grid and grid_to_mesh are NOT thread-safe due to openvdb::initialize().
//
// [HAZARD H1012] (P2/Medium): `openvdb::initialize()` is called inside BOTH mesh_to_grid
// and grid_to_mesh on every invocation. The OpenVDB docs say it should be called ONCE
// at startup. The internal implementation does a mutex lock on every call regardless,
// adding synchronisation overhead to every mesh<->grid conversion. The TODO comment in
// OpenVDBUtils.cpp acknowledges this but it has not been fixed.
//
// [HAZARD H1013] (P2/Medium): If `its_split` returns an empty vector (e.g. fully degenerate
// mesh) AND the volume filter also removes all parts, `grid` remains nullptr. The fallback
// branch `else if (meshparts.empty())` catches the case where its_split returned empty —
// but if its_split returned non-empty and the volume filter emptied it, the outer if(grid)
// branch is skipped AND the else-if branch is skipped, leaving grid==nullptr. The subsequent
// `grid->insertMeta(...)` call at line 84 unconditionally dereferences grid — crash.
//
// [HAZARD H1014] (P2/Medium): `grid_to_mesh` silently swallows all exceptions from
// `grid.metaValue<float>("voxel_scale")` with `catch(...) {}` — any metadata read error
// returns scale=1.0 with no warning. If the grid was created with a non-1.0 voxel_scale
// (e.g. 2.0 for higher resolution), the silently-defaulted scale=1.0 produces mesh vertices
// at wrong (doubled) positions.
//
// [HAZARD H1015] (P2/Medium): `redistance_grid` silently truncates `double` ext_range and
// int_range parameters to `float` for `levelSetRebuild`. If the caller passes double values
// that exceed float precision (or uses very large band widths), the truncation produces
// wrong band widths with no diagnostic.

#include <libslic3r/TriangleMesh.hpp>

#ifdef _MSC_VER
// Suppress warning C4146 in include/gmp.h(2177,31): unary minus operator applied to unsigned type, result still unsigned
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include <openvdb/openvdb.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

namespace Slic3r {

// [INTENT] Conversion helpers between OpenVDB Vec3s/Vec3I and Slic3r Vec3f/Vec3i32.
// All inline — no allocation.
inline Vec3f   to_vec3f(const openvdb::Vec3s& v) { return Vec3f{v.x(), v.y(), v.z()}; }
inline Vec3d   to_vec3d(const openvdb::Vec3s& v) { return to_vec3f(v).cast<double>(); }
inline Vec3i32 to_vec3i(const openvdb::Vec3I& v) { return Vec3i32{int(v[0]), int(v[1]), int(v[2])}; }

// Here voxel_scale defines the scaling of voxels which affects the voxel count.
// 1.0 value means a voxel for every unit cube. 2 means the model is scaled to
// be 2x larger and the voxel count is increased by the increment in the scaled
// volume, thus 4 times. This kind a sampling accuracy selection is not
// achievable through the Transform parameter. (TODO: or is it?)
// The resulting grid will contain the voxel_scale in its metadata under the
// "voxel_scale" key to be used in grid_to_mesh function.
//
// [INTENT] Convert an indexed triangle set to an OpenVDB signed-distance-field grid.
// Splits mesh into connected components first; each part is converted separately and
// CSG-unioned into a single grid.
// [HAZARD H1013] See module header — null dereference if volume-filtered parts == empty.
// [HAZARD H1012] See module header — openvdb::initialize() called on every invocation.
openvdb::FloatGrid::Ptr mesh_to_grid(const indexed_triangle_set&     mesh,
                                     const openvdb::math::Transform& tr                = {},
                                     float                           voxel_scale       = 1.f,
                                     float                           exteriorBandWidth = 3.0f,
                                     float                           interiorBandWidth = 3.0f,
                                     int                             flags             = 0);

// [INTENT] Convert an OpenVDB SDF FloatGrid back to a triangle mesh via marching cubes
// (volumeToMesh). Quads are triangulated by splitting into two triangles.
// [HAZARD H1014] voxel_scale metadata read failures silently default to 1.0.
indexed_triangle_set grid_to_mesh(const openvdb::FloatGrid& grid,
                                  double                    isovalue                  = 0.0,
                                  double                    adaptivity                = 0.0,
                                  bool                      relaxDisorientedTriangles = true);

// [INTENT] Rebuild the SDF band of an existing FloatGrid to repair gaps or
// corruption from boolean operations. Uses levelSetRebuild with new iso/range params.
// Copies metadata (including voxel_scale) from the input grid to the new grid.
// [HAZARD H1015] double→float truncation for er/ir parameters.
openvdb::FloatGrid::Ptr redistance_grid(const openvdb::FloatGrid& grid, double iso, double ext_range = 3., double int_range = 3.);

} // namespace Slic3r

#endif // OPENVDBUTILS_HPP
