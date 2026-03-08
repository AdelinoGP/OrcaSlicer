#ifndef PRUSASLICER_AABBMESH_H
#define PRUSASLICER_AABBMESH_H

// [INTENT] AABBMesh couples a raw indexed_triangle_set pointer with three
//          acceleration structures: an AABB tree for ray-casting and nearest-
//          point queries, a VertexFaceIndex for per-vertex face look-ups, and
//          a face-neighbor index for topological traversal.  It is the primary
//          3-D spatial query interface used by SLA support generation, seam
//          placement, bridge detection, and build-volume clash testing.
//
// [COUPLING] AABBMesh does NOT own the triangle mesh — it holds a raw pointer
//            (m_tm) to an externally owned indexed_triangle_set or TriangleMesh.
//            Callers must ensure the source mesh outlives every AABBMesh that
//            references it.  This is an implicit lifetime contract with no
//            runtime enforcement (H738).
//
// [CONCURRENCY] The query methods (query_ray_hit, query_ray_hits,
//               squared_distance) are const and read-only — safe for concurrent
//               reads from multiple threads if the underlying AABB tree is fully
//               built.  The init() call is NOT thread-safe; construction must
//               complete before sharing across threads.

#include <memory>
#include <vector>

#include <libslic3r/Point.hpp>
#include <libslic3r/TriangleMesh.hpp>

// There is an implementation of a hole-aware raycaster that was eventually
// not used in production version. It is now hidden under following define
// for possible future use.
// #define SLIC3R_HOLE_RAYCASTER

#ifdef SLIC3R_HOLE_RAYCASTER
#include "libslic3r/SLA/Hollowing.hpp"
#endif

struct indexed_triangle_set;

namespace Slic3r {

class TriangleMesh;

// An index-triangle structure coupled with an AABB index to support ray
// casting and other higher level operations.
class AABBMesh
{
    // [INTENT] Pimpl pattern isolating the libigl/AABBTreeIndirect dependency.
    //          AABBImpl wraps Tree3f and the triangle-ray epsilon.  The pimpl
    //          allows the header to remain clean of igl:: types and avoids
    //          recompilation of all users when the tree implementation changes.
    class AABBImpl;

    // [STATE] Raw (non-owning) pointer to the source mesh.  The mesh MUST
    //         outlive this object.  No shared_ptr or weak_ptr guard is used.
    //         (H738 — dangling pointer if mesh is destroyed first)
    const indexed_triangle_set* m_tm;

    // [STATE] Heap-allocated AABB acceleration structure (pimpl).
    //         unique_ptr used for pimpl; copy constructor explicitly deep-copies.
    std::unique_ptr<AABBImpl> m_aabb;

    // [STATE] vertex-face index — maps each vertex index to the set of face
    //         indices that share it.  Built at construction via its{} constructor
    //         of VertexFaceIndex.  Used for normal smoothing and neighborhood
    //         queries by SLA support.
    VertexFaceIndex m_vfidx; // vertex-face index

    // [STATE] face-neighbor index — for face i, m_fnidx[i] holds the three
    //         adjacent face indices (-1 if boundary).  Computed by
    //         its_face_neighbors() at construction.  Used by support algorithms
    //         to walk across the mesh surface.
    std::vector<Vec3i32> m_fnidx; // face-neighbor index

#ifdef SLIC3R_HOLE_RAYCASTER
    // This holds a copy of holes in the mesh. Initialized externally
    // by load_mesh setter.
    std::vector<sla::DrainHole> m_holes;
#endif

    // [INTENT] Internal templated init, shared by both constructors.
    //          Delegates to m_aabb->init() with the triangle mesh pointer and
    //          epsilon flag.  Template parameter M is either indexed_triangle_set
    //          or TriangleMesh — both provide the .its member.
    template<class M> void init(const M& mesh, bool calculate_epsilon);

public:
    // calculate_epsilon ... calculate epsilon for triangle-ray intersection from an average triangle edge length.
    // If set to false, a default epsilon is used, which works for "reasonable" meshes.
    // [INTENT] Primary constructor.  calculate_epsilon=true derives the ray-
    //          triangle intersection epsilon from average edge length (l² × 1e-6),
    //          which avoids false misses on meshes with very large triangles.
    //          Use false (default) for meshes with "normal" proportions.
    //          (H739 — very large meshes with default epsilon may miss near-
    //          surface rays; very small meshes with calculated epsilon may produce
    //          self-intersection artefacts)
    explicit AABBMesh(const indexed_triangle_set& tmesh, bool calculate_epsilon = false);
    explicit AABBMesh(const TriangleMesh& mesh, bool calculate_epsilon = false);

    // [INTENT] Copy constructor and copy-assignment perform a deep copy of
    //          m_aabb (new AABBImpl(*other.m_aabb)) but a SHALLOW copy of m_tm —
    //          the raw pointer is copied, not the triangle data.  Both the
    //          original and the copy point to the same mesh.  (H740)
    AABBMesh(const AABBMesh& other);
    AABBMesh& operator=(const AABBMesh&);

    // [INTENT] Move constructor/assignment use compiler-generated defaults
    //          (= default).  Safe because unique_ptr move-transfers ownership.
    AABBMesh(AABBMesh&& other);
    AABBMesh& operator=(AABBMesh&& other);

    ~AABBMesh();

    // [INTENT] Accessors forwarding directly to m_tm->vertices / m_tm->indices.
    //          No bounds checking on the indexed overloads — caller is responsible.
    const std::vector<Vec3f>&   vertices() const;
    const std::vector<Vec3i32>& indices() const;
    const Vec3f&                vertices(size_t idx) const;
    const Vec3i32&              indices(size_t idx) const;

    // Result of a raycast
    // [INTENT] Value-semantic result object.  Default-constructed (m_t = infty,
    //          m_face_id = -1) represents "no hit".  is_hit() returns true only
    //          when face_id >= 0 AND t is finite.  is_inside() tests whether the
    //          ray direction and the face normal point the same way (dot > 0),
    //          which distinguishes entry from exit hits in closed manifolds.
    //
    // [HAZARD H741] is_inside() uses raw dot product without normalizing dir —
    //          callers must supply unit-length dir (asserted in query_ray_hit but
    //          not in query_ray_hits).
    class hit_result
    {
        // m_t holds a distance from m_source to the intersection.
        double          m_t       = infty();
        int             m_face_id = -1;
        const AABBMesh* m_mesh    = nullptr;
        Vec3d           m_dir     = Vec3d::Zero();
        Vec3d           m_source  = Vec3d::Zero();
        Vec3d           m_normal  = Vec3d::Zero();
        friend class AABBMesh;

        // A valid object of this class can only be obtained from
        // IndexedMesh::query_ray_hit method.
        explicit inline hit_result(const AABBMesh& em) : m_mesh(&em) {}

    public:
        // This denotes no hit on the mesh.
        static inline constexpr double infty() { return std::numeric_limits<double>::infinity(); }

        explicit inline hit_result(double val = infty()) : m_t(val) {}

        inline double       distance() const { return m_t; }
        inline const Vec3d& direction() const { return m_dir; }
        inline const Vec3d& source() const { return m_source; }
        // [INTENT] Reconstructs the 3-D hit position lazily.  Not cached — recomputes every call.
        inline Vec3d position() const { return m_source + m_dir * m_t; }
        inline int   face() const { return m_face_id; }
        // [INTENT] is_valid() tests that this result was produced by a real
        //          AABBMesh query (m_mesh != nullptr), not default-constructed.
        inline bool is_valid() const { return m_mesh != nullptr; }
        // [INTENT] is_hit() is the primary "did the ray hit something?" test.
        //          Both face_id >= 0 AND finite t must hold — igl can set t = inf
        //          for no-hit even when face_id is written.
        inline bool is_hit() const { return m_face_id >= 0 && !std::isinf(m_t); }

        inline const Vec3d& normal() const
        {
            assert(is_valid());
            return m_normal;
        }

        // [INTENT] Returns true when the hit face normal points in the same
        //          direction as the ray (dot > 0), meaning the ray is exiting the
        //          mesh interior.  Used by SLA support code to determine if a
        //          cast ray origin is inside the model.
        // [HAZARD H741] relies on dir being unit-length; no normalization guard here.
        inline bool is_inside() const { return is_hit() && normal().dot(m_dir) > 0; }
    };

#ifdef SLIC3R_HOLE_RAYCASTER
    // Inform the object about location of holes
    // creates internal copy of the vector
    void load_holes(const std::vector<sla::DrainHole>& holes) { m_holes = holes; }

    // Iterates over hits and holes and returns the true hit, possibly
    // on the inside of a hole.
    // This function is currently not used anywhere, it was written when the
    // holes were subtracted on slices, that is, before we started using CGAL
    // to actually cut the holes into the mesh.
    // [INTENT] Dead code path guarded by SLIC3R_HOLE_RAYCASTER.  Implements a
    //          merge-sort-style sweep over object hits and hole cylinder
    //          intersections to find the "true" first hit accounting for drain
    //          holes.  Algorithm uses two-pass nesting counters (dry_run) to
    //          determine whether the ray source starts inside a hole or mesh.
    hit_result filter_hits(const std::vector<AABBMesh::hit_result>& obj_hits) const;
#endif

    // [INTENT] First-hit ray query.  Returns a single hit_result — the closest
    //          intersection along the ray.  Internally delegates to
    //          AABBTreeIndirect::intersect_ray_first_hit().  The result has
    //          m_face_id = -1 and m_t = inf when there is no intersection.
    // [HAZARD H742] dir MUST be unit-length.  An assert checks this in debug
    //          builds only.  A non-unit dir will produce incorrect t distances.
    // Casting a ray on the mesh, returns the distance where the hit occures.
    hit_result query_ray_hit(const Vec3d& s, const Vec3d& dir) const;

    // [INTENT] All-hits ray query.  Returns every intersection sorted by t,
    //          with exact-duplicate t values removed.  Used by SLA support and
    //          hollowing algorithms that need inside/outside transitions (entry/
    //          exit pairs).  Sorting is O(k log k) where k is hit count.
    // [HAZARD H743] Duplicate removal uses exact float equality (a.t == b.t),
    //          which removes only bit-for-bit identical values — near-duplicate
    //          hits from mesh edges shared by two triangles may still appear as
    //          two distinct entries, causing spurious entry/exit pairs.
    // Casts a ray on the mesh and returns all hits
    std::vector<hit_result> query_ray_hits(const Vec3d& s, const Vec3d& dir) const;

    // [INTENT] Returns squared Euclidean distance from point p to the closest
    //          triangle surface.  Out-params i (face index) and c (closest point
    //          coordinates) are populated.  Used by SLA support to compute safe
    //          pillar-to-surface clearances.
    double squared_distance(const Vec3d& p, int& i, Vec3d& c) const;
    // [INTENT] Convenience overload discarding face index and closest point.
    inline double squared_distance(const Vec3d& p) const
    {
        int   i;
        Vec3d c;
        return squared_distance(p, i, c);
    }

    // [INTENT] Computes the normalized face normal for a given face index using
    //          its_unnormalized_normal() and then .normalized().  The result is
    //          in world space assuming the mesh has not been transformed (mesh
    //          transforms are applied in the caller before building AABBMesh).
    Vec3d normal_by_face_id(int face_id) const;

    // [INTENT] Exposes the raw triangle mesh pointer for callers that need
    //          direct vertex/index access alongside AABB queries.
    const indexed_triangle_set* get_triangle_mesh() const { return m_tm; }

    // [INTENT] Accessors for the two precomputed topological indices.
    //          vertex_face_index: per-vertex list of incident face indices.
    //          face_neighbor_index: per-face triple of adjacent face indices.
    const VertexFaceIndex&      vertex_face_index() const { return m_vfidx; }
    const std::vector<Vec3i32>& face_neighbor_index() const { return m_fnidx; }
};

} // namespace Slic3r

#endif // INDEXEDMESH_H
