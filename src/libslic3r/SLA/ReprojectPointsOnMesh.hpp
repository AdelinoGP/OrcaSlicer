// [INTENT] Utility header for re-snapping SLA support points and drain holes
// onto the nearest mesh surface after the mesh geometry has changed (e.g.
// after model transformations or mesh repair).  Header-only; no .cpp.
#ifndef REPROJECTPOINTSONMESH_HPP
#define REPROJECTPOINTSONMESH_HPP

#include "libslic3r/Point.hpp"
#include "SupportPoint.hpp"
#include "Hollowing.hpp"
#include "IndexedMesh.hpp"
#include "libslic3r/Model.hpp"

#include <tbb/parallel_for.h>

namespace Slic3r { namespace sla {

// [STATE] pos<Pt>: accessor/mutator pair for the position field of SupportPoint
// and DrainHole. Templates allow a single reproject function to handle both types.
// [MEMORY] pos() cast: reads float, casts to double for IndexedMesh query; writes
// result back cast to float. Precision loss on the round-trip is intentional (SLA
// point resolution does not require sub-micron accuracy).
template<class Pt> Vec3d pos(const Pt& p) { return p.pos.template cast<double>(); }
template<class Pt> void  pos(Pt& p, const Vec3d& pp) { p.pos = pp.cast<float>(); }

// [INTENT] Project every point in 'pts' onto the nearest surface of 'mesh' using
// the AABB tree's squared-distance query. Runs in parallel via TBB.
// [CONCURRENCY] Each TBB task writes to pts[idx] exclusively — no two tasks share
// an index, so no data race.  However pts must NOT be resized during iteration.
// [COUPLING] Depends on IndexedMesh::squared_distance (AABB tree, libIGL back-end).
template<class PointType> void reproject_support_points(const IndexedMesh& mesh, std::vector<PointType>& pts)
{
    tbb::parallel_for(size_t(0), pts.size(), [&mesh, &pts](size_t idx) {
        int   junk;
        Vec3d new_pos;
        mesh.squared_distance(pos(pts[idx]), junk, new_pos);
        pos(pts[idx], new_pos);
    });
}

// [HAZARD H920] NULL-CHECK AFTER DEREFERENCE (P1/High)
// object->sla_support_points and sla_drain_holes are accessed on line 30-31 BEFORE
// the null-check `if (!object ...)` on line 33.  If object == nullptr, the two
// boolean initializations are undefined behaviour.  The null-check must be the
// first statement.
// [INTENT] Convenience wrapper: re-snaps both support points and drain holes for
// a ModelObject onto its current raw mesh in one call.
// [COUPLING] Builds a full TriangleMesh from raw_mesh() — allocates and copies the
// mesh solely for the reprojection.  For large models this is expensive.
inline void reproject_points_and_holes(ModelObject* object)
{
    bool has_sppoints = !object->sla_support_points.empty(); // [HAZARD H920] UB if object==nullptr
    bool has_holes    = !object->sla_drain_holes.empty();    // [HAZARD H920] UB if object==nullptr

    if (!object || (!has_holes && !has_sppoints))
        return;

    TriangleMesh rmsh = object->raw_mesh();
    IndexedMesh  emesh{rmsh};

    if (has_sppoints)
        reproject_support_points(emesh, object->sla_support_points);

    if (has_holes)
        reproject_support_points(emesh, object->sla_drain_holes);
}

}} // namespace Slic3r::sla
#endif // REPROJECTPOINTSONMESH_HPP
