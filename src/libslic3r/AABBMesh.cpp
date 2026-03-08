#include "AABBMesh.hpp"
#include <Execution/ExecutionTBB.hpp>

#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include <numeric>

#ifdef SLIC3R_HOLE_RAYCASTER
#include <libslic3r/SLA/Hollowing.hpp>
#endif

namespace Slic3r {

// [INTENT] AABBImpl is the concrete pimpl body that holds:
//   - m_tree: the AABBTreeIndirect::Tree3f (float-coordinate 3-D AABB tree
//             over the mesh triangles)
//   - m_triangle_ray_epsilon: the epsilon used for Möller–Trumbore ray-triangle
//             intersection to guard against numerical misses at triangle edges.
//
// [MEMORY] The Tree3f internally allocates a flat node array (implicit binary
//          heap layout).  For a mesh with N triangles the tree uses O(N) nodes.
//          No incremental update is supported — the tree is static after init().
//
// [COUPLING] AABBImpl is only ever accessed through AABBMesh methods.  It is
//            never exposed outside this translation unit.
class AABBMesh::AABBImpl
{
private:
    AABBTreeIndirect::Tree3f m_tree;
    // [STATE] Ray-triangle intersection epsilon.  Default 1e-6; when
    //         calculate_epsilon=true it is scaled by l² (average edge length
    //         squared), making it proportional to the mesh scale.
    double m_triangle_ray_epsilon;

public:
    // [INTENT] Builds the AABB tree over the indexed triangle set.
    //          If calculate_epsilon, derives epsilon from average edge length:
    //            epsilon = 1e-6 * l²
    //          This scales with mesh size — important for very large meshes
    //          (buildings, terrain) where the default 1e-6 is too tight.
    // [HAZARD H739] Very large meshes with default epsilon may miss near-surface
    //          rays (self-intersection artefacts).  Very small meshes with
    //          calculated epsilon may produce false hits too eagerly.
    void init(const indexed_triangle_set& its, bool calculate_epsilon)
    {
        m_triangle_ray_epsilon = 0.000001;
        if (calculate_epsilon) {
            // Calculate epsilon from average triangle edge length.
            double l = its_average_edge_length(its);
            if (l > 0)
                m_triangle_ray_epsilon = 0.000001 * l * l;
        }
        m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices);
    }

    // [INTENT] First-hit overload — delegates to
    //          AABBTreeIndirect::intersect_ray_first_hit, which uses unordered
    //          left-before-right traversal (H719 from AABBTreeIndirect).
    //          The single igl::Hit out-param is populated only when a hit is
    //          found; callers check hit.t for infinity.
    void intersect_ray(const indexed_triangle_set& its, const Vec3d& s, const Vec3d& dir, igl::Hit& hit)
    {
        AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices, m_tree, s, dir, hit, m_triangle_ray_epsilon);
    }

    // [INTENT] All-hits overload — delegates to
    //          AABBTreeIndirect::intersect_ray_all_hits, which exhaustively
    //          traverses the tree and returns every intersection unsorted.
    //          Caller (query_ray_hits) is responsible for sorting and dedup.
    void intersect_ray(const indexed_triangle_set& its, const Vec3d& s, const Vec3d& dir, std::vector<igl::Hit>& hits)
    {
        AABBTreeIndirect::intersect_ray_all_hits(its.vertices, its.indices, m_tree, s, dir, hits, m_triangle_ray_epsilon);
    }

    // [INTENT] Nearest-point query.  Returns squared distance; populates i
    //          (closest face index) and closest (1×3 Eigen row for closest point
    //          coordinates).  The type mismatch between Vec3d and Eigen::Matrix<
    //          double,1,3> is resolved by explicit cast on the way in and Vec3d
    //          assignment on the way out.
    // [HAZARD H744] The 1×3 Eigen::Matrix row-vector layout vs column-vector
    //          Vec3d — the conversion is explicit but fragile if EIGEN_DEFAULT_TO_
    //          ROW_MAJOR is changed globally.
    double squared_distance(const indexed_triangle_set& its, const Vec3d& point, int& i, Eigen::Matrix<double, 1, 3>& closest)
    {
        size_t idx_unsigned = 0;
        Vec3d  closest_vec3d(closest);
        double dist = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(its.vertices, its.indices, m_tree, point, idx_unsigned,
                                                                                 closest_vec3d);
        i           = int(idx_unsigned);
        closest     = closest_vec3d;
        return dist;
    }
};

// [INTENT] Shared init body.  Both constructors set m_tm before calling init(),
//          so m_aabb->init() receives the already-set m_tm pointer.
//          Template M is indexed_triangle_set or TriangleMesh — both expose
//          the required members through the same init() signature since
//          m_aabb->init(*m_tm, calculate_epsilon) uses m_tm directly.
template<class M> void AABBMesh::init(const M& mesh, bool calculate_epsilon)
{
    // Build the AABB accelaration tree
    m_aabb->init(*m_tm, calculate_epsilon);
}

// [INTENT] Constructor from raw indexed_triangle_set.  Stores pointer (non-
//          owning), allocates AABBImpl on heap, builds VertexFaceIndex and
//          face-neighbor index, then calls init() to build the AABB tree.
//          [HAZARD H738] m_tm holds a raw pointer — mesh lifetime must exceed
//          this object's lifetime.
AABBMesh::AABBMesh(const indexed_triangle_set& tmesh, bool calculate_epsilon)
    : m_tm(&tmesh), m_aabb(new AABBImpl()), m_vfidx{tmesh}, m_fnidx{its_face_neighbors(tmesh)}
{
    init(tmesh, calculate_epsilon);
}

// [INTENT] Constructor from TriangleMesh wrapper.  Extracts the .its member
//          pointer for m_tm.  TriangleMesh::its is the canonical storage for
//          the indexed triangle set inside the higher-level TriangleMesh class.
AABBMesh::AABBMesh(const TriangleMesh& mesh, bool calculate_epsilon)
    : m_tm(&mesh.its), m_aabb(new AABBImpl()), m_vfidx{mesh.its}, m_fnidx{its_face_neighbors(mesh.its)}
{
    init(mesh, calculate_epsilon);
}

// [INTENT] Defaulted destructor must be defined in the .cpp (not the header)
//          because AABBImpl is an incomplete type at the point of the header
//          — the unique_ptr<AABBImpl> destructor must see the full definition.
AABBMesh::~AABBMesh() {}

// [INTENT] Copy constructor performs a deep copy of AABBImpl (new AABBImpl(*))
//          but a SHALLOW copy of m_tm (raw pointer copied).  The copied object
//          shares the same triangle data but owns its own AABB tree.
// [HAZARD H740] Both objects hold the same m_tm pointer.  If the source is
//          destroyed or the triangle set is moved, the copy's m_tm becomes
//          dangling.  There is no reference-counted guard.
AABBMesh::AABBMesh(const AABBMesh& other)
    : m_tm(other.m_tm), m_aabb(new AABBImpl(*other.m_aabb)), m_vfidx{other.m_vfidx}, m_fnidx{other.m_fnidx}
{}

// [INTENT] Copy-assignment mirrors the copy constructor: replaces m_aabb with
//          a fresh deep copy, copies the raw m_tm pointer, and copies the two
//          topological indices.
// [HAZARD H740] Same raw-pointer sharing as copy constructor.
AABBMesh& AABBMesh::operator=(const AABBMesh& other)
{
    m_tm = other.m_tm;
    m_aabb.reset(new AABBImpl(*other.m_aabb));
    m_vfidx = other.m_vfidx;
    m_fnidx = other.m_fnidx;

    return *this;
}

// [INTENT] Move assignment and move constructor are compiler-defaulted:
//          unique_ptr<AABBImpl> transfers ownership cleanly, raw pointer
//          transfers without copy.  The moved-from object has m_tm = nullptr
//          and m_aabb = nullptr — any access after move is UB.
AABBMesh& AABBMesh::operator=(AABBMesh&& other) = default;

AABBMesh::AABBMesh(AABBMesh&& other) = default;

const std::vector<Vec3f>& AABBMesh::vertices() const { return m_tm->vertices; }

const std::vector<Vec3i32>& AABBMesh::indices() const { return m_tm->indices; }

const Vec3f& AABBMesh::vertices(size_t idx) const
{
    // [HAZARD H745] No bounds check — UB on out-of-range idx in release builds.
    return m_tm->vertices[idx];
}

const Vec3i32& AABBMesh::indices(size_t idx) const
{
    // [HAZARD H745] No bounds check — UB on out-of-range idx in release builds.
    return m_tm->indices[idx];
}

// [INTENT] Computes the normalized face normal by:
//   1. Calling its_unnormalized_normal() which cross-products two triangle edges.
//   2. Casting the result to double precision.
//   3. Calling .normalized() (Eigen unit-vector normalization).
// [HAZARD H746] Degenerate triangles (zero-area, collinear vertices) produce a
//          zero cross-product.  .normalized() on a zero vector returns NaN in
//          Eigen by default, propagating silently to callers (seam placer,
//          support generation).
Vec3d AABBMesh::normal_by_face_id(int face_id) const { return its_unnormalized_normal(*m_tm, face_id).cast<double>().normalized(); }

// [INTENT] First-hit ray query.  Asserts unit-length dir (debug only), then
//          delegates to AABBImpl::intersect_ray (single-hit overload).
//          The igl::Hit result is converted to a hit_result value object:
//            - m_t  ← double(hit.t)   (float-to-double widening)
//            - m_normal / m_face_id populated only when hit.t is finite/non-NaN
//
// [HAZARD H742] dir must be unit-length.  The assert is debug-only.  Passing a
//          non-unit dir silently produces incorrect t distances in release builds.
//
// [HAZARD H747] float → double cast of hit.t: igl uses float internally.
//          For very large meshes (t > ~16M units) the float precision loss
//          (~7 significant digits) is noticeable; use double-precision ray
//          casting if sub-mm accuracy is needed at large scales.
AABBMesh::hit_result AABBMesh::query_ray_hit(const Vec3d& s, const Vec3d& dir) const
{
    assert(is_approx(dir.norm(), 1.));
    igl::Hit hit{-1, -1, 0.f, 0.f, 0.f};
    hit.t = std::numeric_limits<float>::infinity();

#ifdef SLIC3R_HOLE_RAYCASTER
    if (!m_holes.empty()) {
        // If there are holes, the hit_results will be made by
        // query_ray_hits (object) and filter_hits (holes):
        return filter_hits(query_ray_hits(s, dir));
    }
#endif

    m_aabb->intersect_ray(*m_tm, s, dir, hit);
    hit_result ret(*this);
    ret.m_t      = double(hit.t);
    ret.m_dir    = dir;
    ret.m_source = s;
    if (!std::isinf(hit.t) && !std::isnan(hit.t)) {
        ret.m_normal  = this->normal_by_face_id(hit.id);
        ret.m_face_id = hit.id;
    }

    return ret;
}

// [INTENT] All-hits ray query.  Calls the vector overload of
//          AABBImpl::intersect_ray, then:
//   1. Sorts hits by t (ascending).
//   2. Removes exact-duplicate t values using std::unique + erase idiom.
//   3. Converts each igl::Hit to a hit_result value object.
//
// [HAZARD H743] Duplicate removal is exact float equality (a.t == b.t).
//          Near-duplicate hits from a ray grazing a shared mesh edge may
//          produce two distinct t values with a difference of ~1 ULP, causing
//          spurious extra entry/exit pairs in inside/outside counting logic.
//
// [HAZARD H747] Same float→double cast issue as query_ray_hit.
std::vector<AABBMesh::hit_result> AABBMesh::query_ray_hits(const Vec3d& s, const Vec3d& dir) const
{
    std::vector<AABBMesh::hit_result> outs;
    std::vector<igl::Hit>             hits;
    m_aabb->intersect_ray(*m_tm, s, dir, hits);

    // The sort is necessary, the hits are not always sorted.
    std::sort(hits.begin(), hits.end(), [](const igl::Hit& a, const igl::Hit& b) { return a.t < b.t; });

    // Remove duplicates. They sometimes appear, for example when the ray is cast
    // along an axis of a cube due to floating-point approximations in igl (?)
    hits.erase(std::unique(hits.begin(), hits.end(), [](const igl::Hit& a, const igl::Hit& b) { return a.t == b.t; }), hits.end());

    //  Convert the igl::Hit into hit_result
    outs.reserve(hits.size());
    for (const igl::Hit& hit : hits) {
        outs.emplace_back(AABBMesh::hit_result(*this));
        outs.back().m_t      = double(hit.t);
        outs.back().m_dir    = dir;
        outs.back().m_source = s;
        if (!std::isinf(hit.t) && !std::isnan(hit.t)) {
            outs.back().m_normal  = this->normal_by_face_id(hit.id);
            outs.back().m_face_id = hit.id;
        }
    }

    return outs;
}

#ifdef SLIC3R_HOLE_RAYCASTER
// [INTENT] Dead-code path (SLIC3R_HOLE_RAYCASTER not defined in production).
//          Merges object hits with hole cylinder intersections using a
//          two-pointer sweep over sorted hit sequences.  A two-pass approach
//          (dry_run=1 first, dry_run=0 second) initialises nesting counters:
//          on the first pass the algorithm walks all events to determine if the
//          ray SOURCE is already nested inside a hole or the mesh; on the second
//          pass it walks again with the corrected initial nesting to find the
//          true first visible surface.
//
// [HAZARD H748] The pointer-arithmetic advance at the end of the loop:
//          "if (is_hole && next_hole_hit++ == &hole_isects.back())"
//          post-increments next_hole_hit past .back(), leaving it one past the
//          last element rather than nullptr if the condition triggers on the
//          final element.  The next loop iteration checks "next_hole_hit" which
//          is now a dangling past-the-end pointer — UB.  Same pattern for
//          next_mesh_hit.  This is likely the reason the feature was abandoned.
AABBMesh::hit_result IndexedMesh::filter_hits(const std::vector<AABBMesh::hit_result>& object_hits) const
{
    assert(!m_holes.empty());
    hit_result out(*this);

    if (object_hits.empty())
        return out;

    const Vec3d& s   = object_hits.front().source();
    const Vec3d& dir = object_hits.front().direction();

    // A helper struct to save an intersetion with a hole
    struct HoleHit
    {
        HoleHit(float t_p, const Vec3d& normal_p, bool entry_p) : t(t_p), normal(normal_p), entry(entry_p) {}
        float t;
        Vec3d normal;
        bool  entry;
    };
    std::vector<HoleHit> hole_isects;
    hole_isects.reserve(m_holes.size());

    auto sf   = s.cast<float>();
    auto dirf = dir.cast<float>();

    // Collect hits on all holes, preserve information about entry/exit
    for (const sla::DrainHole& hole : m_holes) {
        std::array<std::pair<float, Vec3d>, 2> isects;
        if (hole.get_intersections(sf, dirf, isects)) {
            // Ignore hole hits behind the source
            if (isects[0].first > 0.f)
                hole_isects.emplace_back(isects[0].first, isects[0].second, true);
            if (isects[1].first > 0.f)
                hole_isects.emplace_back(isects[1].first, isects[1].second, false);
        }
    }

    // Holes can intersect each other, sort the hits by t
    std::sort(hole_isects.begin(), hole_isects.end(), [](const HoleHit& a, const HoleHit& b) { return a.t < b.t; });

    // Now inspect the intersections with object and holes, in the order of
    // increasing distance. Keep track how deep are we nested in mesh/holes and
    // pick the correct intersection.
    // This needs to be done twice - first to find out how deep in the structure
    // the source is, then to pick the correct intersection.
    int hole_nested   = 0;
    int object_nested = 0;
    for (int dry_run = 1; dry_run >= 0; --dry_run) {
        hole_nested   = -hole_nested;
        object_nested = -object_nested;

        bool              is_hole       = false;
        bool              is_entry      = false;
        const HoleHit*    next_hole_hit = hole_isects.empty() ? nullptr : &hole_isects.front();
        const hit_result* next_mesh_hit = &object_hits.front();

        while (next_hole_hit || next_mesh_hit) {
            if (next_hole_hit && next_mesh_hit) // still have hole and obj hits
                is_hole = (next_hole_hit->t < next_mesh_hit->m_t);
            else
                is_hole = next_hole_hit; // one or the other ran out

            // Is this entry or exit hit?
            is_entry = is_hole ? next_hole_hit->entry : !next_mesh_hit->is_inside();

            if (!dry_run) {
                if (!is_hole && hole_nested == 0) {
                    // This is a valid object hit
                    return *next_mesh_hit;
                }
                if (is_hole && !is_entry && object_nested != 0) {
                    // This holehit is the one we seek
                    out.m_t      = next_hole_hit->t;
                    out.m_normal = next_hole_hit->normal;
                    out.m_source = s;
                    out.m_dir    = dir;
                    return out;
                }
            }

            // Increase/decrease the counter
            (is_hole ? hole_nested : object_nested) += (is_entry ? 1 : -1);

            // Advance the respective pointer
            // [HAZARD H748] Post-increment past .back() produces dangling pointer
            if (is_hole && next_hole_hit++ == &hole_isects.back())
                next_hole_hit = nullptr;
            if (!is_hole && next_mesh_hit++ == &object_hits.back())
                next_mesh_hit = nullptr;
        }
    }

    // if we got here, the ray ended up in infinity
    return out;
}
#endif

// [INTENT] Public squared_distance wrapper.  Converts Vec3d to Eigen 1×3 row
//          vector (pp), calls AABBImpl::squared_distance which populates cc,
//          then converts cc back to Vec3d (c).  The type juggling is needed
//          because AABBTreeIndirect's API uses Eigen row-vector layout.
// [HAZARD H744] See AABBImpl::squared_distance — row-vs-column vector layout
//          sensitivity.
double AABBMesh::squared_distance(const Vec3d& p, int& i, Vec3d& c) const
{
    double                      sqdst = 0;
    Eigen::Matrix<double, 1, 3> pp    = p;
    Eigen::Matrix<double, 1, 3> cc;
    sqdst = m_aabb->squared_distance(*m_tm, pp, i, cc);
    c     = cc;
    return sqdst;
}

} // namespace Slic3r
