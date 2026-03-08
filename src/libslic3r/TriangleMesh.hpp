#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

// [INTENT] Central 3D mesh representation for OrcaSlicer.
// TriangleMesh wraps an indexed_triangle_set (vertex + face arrays) from the
// admesh library and layers cached statistics, transformation helpers, and
// repair facilities on top.  Every imported model, support body, primitive
// solid, and convex-hull lives as a TriangleMesh at some point in the pipeline.
// [COUPLING] Depends on admesh (stl.h) for the low-level stl_file / stl_vertex
// / stl_triangle_vertex_indices types.  Downstream consumers: PrintObject
// (model geometry), TriangleMeshSlicer (Z-slice engine), TreeSupport, Fill,
// and Arachne all receive TriangleMesh or indexed_triangle_set references.

#include "libslic3r.h"
#include <admesh/stl.h>
#include <functional>
#include <vector>
#include "BoundingBox.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "ExPolygon.hpp"
#include "Format/STL.hpp"
namespace Slic3r {

class TriangleMesh;
class TriangleMeshSlicer;
struct Groove;

// [INTENT] Accumulates per-mesh repair statistics produced by the admesh
// repair pipeline (trianglemesh_repair_on_import).  Consumers display these
// counts in the UI to inform the user that the model was modified on import.
// [STATE] All counters start at 0 (default-initialised).  A mesh that needed
// no repairs has all zeros; repaired() returns false.
// [HAZARD H392] facets_added is commented out — stl_fill_holes() was disabled
// because it "does more harm than good".  If a target language re-enables hole
// filling the struct layout and merge() arithmetic must be updated.
struct RepairedMeshErrors
{
    // How many edges were united by merging their end points with some other end points in epsilon neighborhood?
    int edges_fixed = 0;
    // How many degenerate faces were removed?
    int degenerate_facets = 0;
    // How many faces were removed during fixing? Includes degenerate_faces and disconnected faces.
    int facets_removed = 0;
    // New faces could only be created with stl_fill_holes() and we ditched stl_fill_holes(), because mostly it does more harm than good.
    // int          facets_added             = 0;
    // How many facets were revesed? Faces are reversed by admesh while it connects patches of triangles togeter and a flipped triangle is
    // encountered. Also the facets are reversed when a negative volume is corrected by flipping all facets.
    int facets_reversed = 0;
    // Edges shared by two triangles, oriented incorrectly.
    int backwards_edges = 0;

    void clear() { *this = RepairedMeshErrors(); }

    // [INTENT] Combine repair statistics from multiple mesh sources (e.g. when
    // merging two meshes into one).  Simple element-wise addition.
    void merge(const RepairedMeshErrors& rhs)
    {
        this->edges_fixed += rhs.edges_fixed;
        this->degenerate_facets += rhs.degenerate_facets;
        this->facets_removed += rhs.facets_removed;
        this->facets_reversed += rhs.facets_reversed;
        this->backwards_edges += rhs.backwards_edges;
    }

    bool repaired() const
    {
        return degenerate_facets > 0 || edges_fixed > 0 || facets_removed > 0 || facets_reversed > 0 || backwards_edges > 0;
    }
};

// [INTENT] Cached mesh metrics stored alongside geometry so that callers can
// query bounding box, volume, part count, and manifold-ness without iterating
// all vertices or faces.  Kept in sync by TriangleMesh member functions.
// [STATE] volume = -1.f is the uninitialised / unknown sentinel.  After
// fill_initial_stats() or any transform that updates stats, volume is either
// a real non-negative value (closed manifold) or 0 / negative (degenerate).
// [HAZARD H393] volume = -1.f sentinel collides with "genuinely negative volume
// (inside-out mesh)".  Callers that test (volume < 0) cannot distinguish
// uninitialised stats from a mesh that needs flipping without also checking
// whether stats have been computed.  Target language should use Option/Maybe.
// [HAZARD H394] TriangleMeshStats is serialised as raw binary by the Cereal
// specialisation at the bottom of this file (loadBinary / saveBinary).  Any
// struct layout change — adding a field, reordering, compiler padding difference
// between platforms — will silently corrupt previously serialised undo/redo
// stacks and project cache files with no version check.
struct TriangleMeshStats
{
    // Mesh metrics.
    uint32_t   number_of_facets = 0;
    stl_vertex max              = stl_vertex::Zero();
    stl_vertex min              = stl_vertex::Zero();
    stl_vertex size             = stl_vertex::Zero();
    // [STATE] -1.f = uninitialised sentinel (see HAZARD H393).
    float volume          = -1.f;
    int   number_of_parts = 0;

    // Mesh errors, remaining.
    // [STATE] open_edges == 0 ⟹ manifold mesh.
    int open_edges = 0;

    // Mesh errors, fixed.
    RepairedMeshErrors repaired_errors;

    void clear() { *this = TriangleMeshStats(); }

    // [INTENT] Produce a combined stats block for two meshes that have been
    // spatially merged.  Bounding box is the union; volume is additive;
    // parts and open_edges are summed.  Returns one of the two operands
    // unchanged when the other is empty (identity element for merging).
    // [HAZARD H395] Merged volume = sum of individual volumes.  This is only
    // correct if both meshes are closed manifolds.  Open meshes have
    // inaccurate volumes; merging two inaccurate values propagates error.
    TriangleMeshStats merge(const TriangleMeshStats& rhs) const
    {
        if (this->number_of_facets == 0)
            return rhs;
        else if (rhs.number_of_facets == 0)
            return *this;
        else {
            TriangleMeshStats out;
            out.number_of_facets = this->number_of_facets + rhs.number_of_facets;
            out.min              = this->min.cwiseMin(rhs.min);
            out.max              = this->max.cwiseMax(rhs.max);
            out.size             = out.max - out.min;
            out.number_of_parts  = this->number_of_parts + rhs.number_of_parts;
            out.open_edges       = this->open_edges + rhs.open_edges;
            out.volume           = this->volume + rhs.volume;
            out.repaired_errors.merge(rhs.repaired_errors);
            return out;
        }
    }

    bool manifold() const { return open_edges == 0; }
    bool repaired() const { return repaired_errors.repaired(); }
};

// [INTENT] Primary 3D mesh class.  Wraps indexed_triangle_set (from admesh:
// a pair of vertex array + face-index array) and attaches:
//   • cached bounding box / volume / part-count (m_stats)
//   • import-origin shift for undo/redo alignment (m_init_shift)
//   • repair, transform, split, merge, slice, projection, and hull operations.
//
// [STATE] `its` (indexed_triangle_set) is PUBLIC — any caller can mutate the
// geometry directly.  m_stats is PRIVATE and kept in sync only by TriangleMesh
// member functions.  Direct mutation of `its` bypasses stats and produces stale
// cached data.
// [HAZARD H396] `its` is a public member.  External code that writes to
// its.vertices or its.indices without calling the appropriate TriangleMesh
// update method leaves m_stats inconsistent (wrong bounding box, wrong volume,
// wrong open_edges count).  Target language should enforce encapsulation.
// [MEMORY] Vertex and face arrays live on the heap via std::vector inside
// indexed_triangle_set.  TriangleMesh is move-constructible (vector move),
// so passing by value is cheap after a move.
// [CONCURRENCY] No internal synchronisation.  Instances are not safe to share
// across threads for writing; read-only access is safe if no writes occur.
class TriangleMesh
{
public:
    TriangleMesh() = default;
    TriangleMesh(const std::vector<Vec3f>& vertices, const std::vector<Vec3i32>& faces);
    TriangleMesh(std::vector<Vec3f>&& vertices, const std::vector<Vec3i32>&& faces);
    explicit TriangleMesh(const indexed_triangle_set& M);
    explicit TriangleMesh(indexed_triangle_set&& M, const RepairedMeshErrors& repaired_errors = RepairedMeshErrors());
    void clear()
    {
        this->its.clear();
        this->m_stats.clear();
    }

    // [INTENT] Populate mesh from an admesh stl_file struct; optionally run the
    // 4-phase admesh repair pipeline.  Returns false on failure.
    bool from_stl(stl_file& stl, bool repair = true);

    // [INTENT] Read an STL file from disk, convert to indexed_triangle_set,
    // and optionally repair.  Progress callback supports cancellation.
    bool ReadSTLFile(const char* input_file, bool repair = true, ImportstlProgressFn stlFn = nullptr, int custom_header_length = 80);
    bool write_ascii(const char* output_file) const;
    bool write_binary(const char* output_file) const;

    // [INTENT] Return volume, computing it from geometry if the cached value
    // is the -1.f uninitialised sentinel.  Side-effect: updates m_stats.volume.
    // [HAZARD H397] volume() is non-const and mutates m_stats.  Concurrent
    // const-qualified access via stats() while volume() runs on another thread
    // is a data race.
    float volume();
    void  WriteOBJFile(const char* output_file) const;

    // [INTENT] Uniform and per-axis scale.  Both overloads update m_stats
    // (min/max/size/volume) so cached stats remain valid after scaling.
    void scale(float factor);
    void scale(const Vec3f& versor);

    void translate(float x, float y, float z);
    void translate(const Vec3f& displacement);
    void rotate(float angle, const Axis& axis);
    void rotate(float angle, const Vec3d& axis);
    void rotate_x(float angle) { this->rotate(angle, X); }
    void rotate_y(float angle) { this->rotate(angle, Y); }
    void rotate_z(float angle) { this->rotate(angle, Z); }

    // [INTENT] Mirror along a coordinate axis.  Updates vertex data AND
    // swaps/negates the corresponding min/max stats components so the bounding
    // box remains correct.
    // [HAZARD H398] mirror() reverses the handedness of all triangles (reflected
    // geometry has flipped winding order).  The implementation must also negate
    // the volume — verify this is done in the .cpp before porting.
    void mirror(const Axis axis);
    void mirror_x() { this->mirror(X); }
    void mirror_y() { this->mirror(Y); }
    void mirror_z() { this->mirror(Z); }

    // [INTENT] Apply an arbitrary affine transform.  When fix_left_handed=true
    // and det(rotation block) < 0 the triangle winding is flipped to restore
    // outward-facing normals.  Volume is updated by multiplying by |det|.
    // [HAZARD H399] Non-uniform or shear transforms do not update m_stats.size
    // correctly (size is computed from axis-aligned bounding box which is
    // recomputed, but volume scaling by det(rotation) is only an approximation
    // for shear transforms — shear changes volume non-trivially).  [UNCLEAR]
    void transform(const Transform3d& t, bool fix_left_handed = false);
    void transform(const Matrix3d& t, bool fix_left_handed = false);

    // Flip triangles, negate volume.
    void flip_triangles();
    void align_to_origin();
    void rotate(double angle, Point* center);

    // [INTENT] Decompose into disconnected patches; each patch becomes its own
    // TriangleMesh.  Parts with negative volume are flipped to positive.
    // [MEMORY] Returns vector by value — O(n) copy of the face/vertex data.
    // Large meshes with many parts produce significant allocation traffic.
    std::vector<TriangleMesh> split() const;
    void                      merge(const TriangleMesh& mesh);

    // [INTENT] Project all triangles onto the Z=0 plane and union the resulting
    // polygons.  WARNING: the source code comments note this is "extremely slow,
    // use for tiny meshes only".
    // [HAZARD H400] horizontal_projection() calls union_ex on all projected
    // triangles — O(n log n) Clipper operation.  On large meshes this is
    // prohibitively slow.  Any port must retain the "tiny meshes only" warning
    // or replace with a faster algorithm.
    ExPolygons horizontal_projection() const;

    // 2D convex hull of a 3D mesh projected into the Z=0 plane.
    Polygon       convex_hull() const;
    BoundingBoxf3 bounding_box() const;

    // Returns the bbox of this TriangleMesh transformed by the given transformation
    BoundingBoxf3 transformed_bounding_box(const Transform3d& trafo) const;
    // [INTENT] Variant that clips the mesh at world_min_z before computing the
    // bounding box.  Used for computing the footprint of objects that have been
    // raised off the build plate.
    BoundingBoxf3 transformed_bounding_box(const Transform3d& trafo, double world_min_z) const;

    // Return the size of the mesh in coordinates.
    Vec3d size() const { return m_stats.size.cast<double>(); }
    /// Return the center of the related bounding box.
    Vec3d center() const { return this->bounding_box().center(); }

    // [INTENT] Compute the convex hull of this mesh as a new TriangleMesh via
    // qhull.  Note: qhull "quite often" produces non-manifold output — the
    // assertion that the hull is manifold is commented out in the .cpp.
    // [HAZARD H401] its_convex_hull() via qhull may produce non-manifold output.
    // The function proceeds silently; downstream manifold-requiring code may
    // misbehave.  Target language should validate hull manifold-ness explicitly.
    TriangleMesh convex_hull_3d() const;

    // Slice this mesh at the provided Z levels and return the vector
    std::vector<ExPolygons> slice(const std::vector<double>& z) const;

    // [INTENT] facets_count() asserts that the cached count matches the actual
    // vector size — a debug-mode sanity check that stats are in sync.
    size_t facets_count() const
    {
        assert(m_stats.number_of_facets == this->its.indices.size());
        return m_stats.number_of_facets;
    }
    bool empty() const { return this->facets_count() == 0; }
    bool repaired() const;
    bool is_splittable() const;
    // Estimate of the memory occupied by this structure, important for keeping an eye on the Undo / Redo stack allocation.
    size_t memsize() const;

    // Used by the Undo / Redo stack, legacy interface. As of now there is nothing cached at TriangleMesh,
    // but we may decide to cache some data in the future (for example normals), thus we keep the interface in place.
    // Release optional data from the mesh if the object is on the Undo / Redo stack only. Returns the amount of memory released.
    size_t release_optional() { return 0; }
    // Restore optional data possibly released by release_optional().
    void restore_optional() {}

    const TriangleMeshStats& stats() const { return m_stats; }

    // [INTENT] m_init_shift stores the original import translation offset so
    // that undo/redo operations can reconstruct the mesh's placement.
    // [COUPLING] Set by Model loading code; read back by undo/redo serialiser.
    void  set_init_shift(const Vec3d& offset) { m_init_shift = offset; }
    Vec3d get_init_shift() const { return m_init_shift; }

    // [STATE] Public geometry storage.  See HAZARD H396 — direct mutation
    // bypasses m_stats synchronisation.
    indexed_triangle_set its;

private:
    // [STATE] Cached mesh metrics.  Must be updated whenever `its` is mutated.
    TriangleMeshStats m_stats;
    // [STATE] Import-origin offset used by undo/redo alignment.  Not part of
    // the geometric transform; it is metadata about where the mesh came from.
    Vec3d m_init_shift{0.0, 0.0, 0.0};
};

// [INTENT] Compressed Sparse Row (CSR) index from vertex ID to the set of face
// IDs incident on that vertex.  Constructed once from an indexed_triangle_set
// and used for neighbourhood queries (e.g. edge collapse, normal averaging,
// support contact detection).
// [MEMORY] m_vertex_to_face_start is size (V+1); m_vertex_faces_all is size 3F
// (each face contributes one entry per vertex).  Total: O(V + F) ints.
// [CONCURRENCY] Immutable after create() — safe for concurrent read.
// [HAZARD H402] VertexFaceIndex is built from a specific indexed_triangle_set
// snapshot.  If `its` is mutated after the index is created the index becomes
// stale and returns incorrect results.  There is no invalidation mechanism.
struct VertexFaceIndex
{
public:
    using iterator = std::vector<size_t>::const_iterator;

    VertexFaceIndex(const indexed_triangle_set& its) { this->create(its); }
    VertexFaceIndex() {}

    // [INTENT] Build the CSR index in a scatter-gather pattern:
    //   1. Count faces per vertex (histogram).
    //   2. Prefix-sum to produce start offsets.
    //   3. Scatter face IDs into m_vertex_faces_all using start[] as cursors
    //      (increments offsets in-place as it fills).
    //   4. Restore start[] by shifting back by one position.
    void create(const indexed_triangle_set& its);
    void clear()
    {
        m_vertex_to_face_start.clear();
        m_vertex_faces_all.clear();
    }

    // Iterators of face indices incident with the input vertex_id.
    iterator begin(size_t vertex_id) const throw() { return m_vertex_faces_all.begin() + m_vertex_to_face_start[vertex_id]; }
    iterator end(size_t vertex_id) const throw() { return m_vertex_faces_all.begin() + m_vertex_to_face_start[vertex_id + 1]; }
    // Vertex incidence.
    size_t count(size_t vertex_id) const throw() { return m_vertex_to_face_start[vertex_id + 1] - m_vertex_to_face_start[vertex_id]; }

    // [INTENT] Range-for support: for (size_t fi : vfi[vertex_id]) { ... }
    const Range<iterator> operator[](size_t vertex_id) const { return {begin(vertex_id), end(vertex_id)}; }

private:
    // [STATE] Size V+1 prefix-sum array.  Entry [v] = start of face list for
    // vertex v; entry [v+1] = exclusive end.
    std::vector<size_t> m_vertex_to_face_start;
    // [STATE] Flat list of face indices, grouped by vertex.  Size = 3 * F.
    std::vector<size_t> m_vertex_faces_all;
};

// [INTENT] Build a per-face, per-edge unique edge ID table.  Two neighbouring
// faces share the same edge ID even if their edge vertices appear in opposite
// order (i.e. orientation-independent matching).  Used by TriangleMeshSlicer to
// chain cut lines into closed polygons across adjacent faces.
// [COUPLING] Output Vec3i32 per face: component k = edge ID for the edge
// opposite vertex k (i.e. edge between vertex[(k+1)%3] and vertex[(k+2)%3]).
// [HAZARD H403] The variant that takes face_mask skips masked faces.  Edges on
// the boundary between masked and unmasked faces are treated as open (ID = -1 or
// unassigned), which means slice lines on that boundary are not chained.
// [HAZARD H404] its_face_edge_ids_impl has a known "FIXME" code path for
// non-manifold same-orientation edges ("smells") that assigns the edge
// arbitrarily to the first two faces found, silently ignoring the third+.
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set& its);
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set& its, std::function<void()> throw_on_cancel_callback);
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set& its, const std::vector<bool>& face_mask);
// Having the face neighbors available, assign unique edge IDs to face edges for chaining of polygons over slices.
std::vector<Vec3i32> its_face_edge_ids(const indexed_triangle_set& its,
                                       std::vector<Vec3i32>&       face_neighbors,
                                       bool                        assign_unbound_edges = false,
                                       int*                        num_edges            = nullptr);

// [INTENT] For each face, list the indices of the up-to-3 neighboring faces
// (sharing an edge).  -1 if the edge is open (boundary).  Ignores face
// orientation — a face is a neighbour regardless of winding order mismatch.
// _par variant uses parallel sort for performance on large meshes.
// [HAZARD H405] its_face_neighbors_par uses a parallel sort which requires
// a thread pool.  If the target environment does not support std::execution or
// TBB, the _par variant must be replaced with the sequential version.
std::vector<Vec3i32> its_face_neighbors(const indexed_triangle_set& its);
std::vector<Vec3i32> its_face_neighbors_par(const indexed_triangle_set& its);

// After applying a transformation with negative determinant, flip the faces to keep the transformed mesh volume positive.
void its_flip_triangles(indexed_triangle_set& its);

// [INTENT] Deduplicate vertices by sorting a (position → old_index) map and
// remapping face indices.  Reduces memory and enables correct manifold checks.
// WARNING: comment notes "will happily create non-manifolds if more than two
// faces share the same vertex position or more than two faces share the same
// edge position".
// [HAZARD H406] its_merge_vertices() produces non-manifold meshes when 3+ faces
// share the same vertex position (T-junctions, star vertices from Boolean ops).
// Callers must be aware that the result may still fail manifold checks even
// after deduplication.
int its_merge_vertices(indexed_triangle_set& its, bool shrink_to_fit = true);

// Remove degenerate faces, return number of faces removed.
int its_remove_degenerate_faces(indexed_triangle_set& its, bool shrink_to_fit = true);

// Remove vertices, which none of the faces references. Return number of freed vertices.
int its_compactify_vertices(indexed_triangle_set& its, bool shrink_to_fit = true);

// store part of index triangle set
bool its_store_triangle(const indexed_triangle_set& its, const char* obj_filename, size_t triangle_index);
bool its_store_triangles(const indexed_triangle_set& its, const char* obj_filename, const std::vector<size_t>& triangles);

// [INTENT] Split an indexed_triangle_set into disconnected components via
// union-find on the face adjacency graph.  Returns one ITS per component.
std::vector<indexed_triangle_set> its_split(const indexed_triangle_set& its);
std::vector<indexed_triangle_set> its_split(const indexed_triangle_set& its, std::vector<Vec3i32>& face_neighbors);

// Number of disconnected patches (faces are connected if they share an edge, shared edge defined with 2 shared vertex indices).
size_t its_number_of_patches(const indexed_triangle_set& its);
size_t its_number_of_patches(const indexed_triangle_set& its, const std::vector<Vec3i32>& face_neighbors);
// Same as its_number_of_patches(its) > 1, but faster.
bool its_is_splittable(const indexed_triangle_set& its);
bool its_is_splittable(const indexed_triangle_set& its, const std::vector<Vec3i32>& face_neighbors);

// Calculate number of unconnected face edges. There should be no unconnected edge in a manifold mesh.
size_t its_num_open_edges(const indexed_triangle_set& its);
size_t its_num_open_edges(const std::vector<Vec3i32>& face_neighbors);

// Shrink the vectors of its.vertices and its.faces to a minimum size by reallocating the two vectors.
void its_shrink_to_fit(indexed_triangle_set& its);

// [INTENT] Collect projected 2D points from the portion of a mesh above z after
// applying matrix/transform m.  Used as input for 2D convex hull calculations.
void its_collect_mesh_projection_points_above(const indexed_triangle_set& its, const Matrix3f& m, const float z, Points& all_pts);
void its_collect_mesh_projection_points_above(const indexed_triangle_set& its, const Transform3f& t, const float z, Points& all_pts);

// Calculate 2D convex hull of a transformed and clipped mesh. Uses the function above.
Polygon its_convex_hull_2d_above(const indexed_triangle_set& its, const Matrix3f& m, const float z);
Polygon its_convex_hull_2d_above(const indexed_triangle_set& its, const Transform3f& t, const float z);

// [INTENT] Local vertex-in-triangle lookup helpers.  Return the local index
// (0/1/2) of a global vertex or edge within a specific triangle, or -1 if not
// found.  Used for neighbourhood traversal in edge-collapse and slice-chain
// algorithms.
inline int its_triangle_vertex_index(const stl_triangle_vertex_indices& triangle_indices, int vertex_idx)
{
    return vertex_idx == triangle_indices[0] ? 0 : vertex_idx == triangle_indices[1] ? 1 : vertex_idx == triangle_indices[2] ? 2 : -1;
}

inline Vec2i32 its_triangle_edge(const stl_triangle_vertex_indices& triangle_indices, int edge_idx)
{
    int next_edge_idx = (edge_idx == 2) ? 0 : edge_idx + 1;
    return {triangle_indices[edge_idx], triangle_indices[next_edge_idx]};
}

// Index of an edge inside triangle.
inline int its_triangle_edge_index(const stl_triangle_vertex_indices& triangle_indices, const Vec2i32& triangle_edge)
{
    return triangle_edge(0) == triangle_indices[0] && triangle_edge(1) == triangle_indices[1] ? 0 :
           triangle_edge(0) == triangle_indices[1] && triangle_edge(1) == triangle_indices[2] ? 1 :
           triangle_edge(0) == triangle_indices[2] && triangle_edge(1) == triangle_indices[0] ? 2 :
                                                                                                -1;
}

// [INTENT] Check whether two triangles reference the same set of vertex indices
// in any rotation or reflection order (not checking actual positions).
// [HAZARD H407] its_triangle_vertex_the_same() performs O(1) index comparisons
// but only covers the 6 permutations of 3 vertices.  It does NOT check vertex
// positions — two triangles at different positions that happen to have the same
// vertex indices will incorrectly compare as equal.  Intended for degenerate
// face detection, not general triangle equality.
// juedge whether two triangles has the same vertices
inline bool its_triangle_vertex_the_same(const stl_triangle_vertex_indices& triangle_indices_1,
                                         const stl_triangle_vertex_indices& triangle_indices_2)
{
    bool ret = false;
    if (triangle_indices_1[0] == triangle_indices_2[0]) {
        if ((triangle_indices_1[1] == triangle_indices_2[1]) && (triangle_indices_1[2] == triangle_indices_2[2]))
            ret = true;
        else if ((triangle_indices_1[1] == triangle_indices_2[2]) && (triangle_indices_1[2] == triangle_indices_2[1]))
            ret = true;
    } else if (triangle_indices_1[0] == triangle_indices_2[1]) {
        if ((triangle_indices_1[1] == triangle_indices_2[0]) && (triangle_indices_1[2] == triangle_indices_2[2]))
            ret = true;
        else if ((triangle_indices_1[1] == triangle_indices_2[2]) && (triangle_indices_1[2] == triangle_indices_2[0]))
            ret = true;
    } else if (triangle_indices_1[0] == triangle_indices_2[2]) {
        if ((triangle_indices_1[1] == triangle_indices_2[0]) && (triangle_indices_1[2] == triangle_indices_2[1]))
            ret = true;
        else if ((triangle_indices_1[1] == triangle_indices_2[1]) && (triangle_indices_1[2] == triangle_indices_2[0]))
            ret = true;
    }

    return ret;
}

using its_triangle = std::array<stl_vertex, 3>;

// [INTENT] Retrieve the three vertex positions of a face as a fixed-size array.
// Convenience wrapper that avoids repeated index dereferencing at call sites.
inline its_triangle its_triangle_vertices(const indexed_triangle_set& its, size_t face_id)
{
    return {its.vertices[its.indices[face_id](0)], its.vertices[its.indices[face_id](1)], its.vertices[its.indices[face_id](2)]};
}

// [INTENT] Compute the cross product of two edges — unnormalised face normal.
// Direction gives winding (outward vs inward); magnitude = 2 * face area.
inline stl_normal its_unnormalized_normal(const indexed_triangle_set& its, size_t face_id)
{
    its_triangle tri = its_triangle_vertices(its, face_id);
    return (tri[1] - tri[0]).cross(tri[2] - tri[0]);
}

// [INTENT] Compute the signed volume of the mesh using the divergence theorem
// (sum of signed tetrahedra from the origin to each face).
// [HAZARD H408] its_volume() uses vertices.front() as the reference point; for
// open (non-manifold) meshes the result is numerically incorrect but no error
// is reported.  Callers must check manifold() before trusting the volume.
float its_volume(const indexed_triangle_set& its);
float its_average_edge_length(const indexed_triangle_set& its);

// [INTENT] Append all faces and vertices of B into A, offset B's vertex indices
// by A's current vertex count.  Does NOT merge or deduplicate shared vertices.
void its_merge(indexed_triangle_set& A, const indexed_triangle_set& B);
void its_merge(indexed_triangle_set& A, const std::vector<Vec3f>& triangles);
void its_merge(indexed_triangle_set& A, const Pointf3s& triangles);

// [INTENT] Per-face outward normals.  Returns one Vec3f per face.
// face_normal / face_normal_normalized: low-level helpers operating on a raw
// 3-vertex array (used by repair pipeline and rendering).
std::vector<Vec3f> its_face_normals(const indexed_triangle_set& its);
inline Vec3f       face_normal(const stl_vertex vertex[3]) { return (vertex[1] - vertex[0]).cross(vertex[2] - vertex[1]).normalized(); }
inline Vec3f       face_normal_normalized(const stl_vertex vertex[3]) { return face_normal(vertex).normalized(); }
inline Vec3f       its_face_normal(const indexed_triangle_set& its, const stl_triangle_vertex_indices face)
{
    const stl_vertex vertices[3]{its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]]};
    return face_normal_normalized(vertices);
}
inline Vec3f its_face_normal(const indexed_triangle_set& its, const int face_idx) { return its_face_normal(its, its.indices[face_idx]); }

// [INTENT] Procedural primitive generators.  Each returns an
// indexed_triangle_set representing an analytically defined solid.  Used for
// support pillars, snap-fits, nozzle-clearance objects, and UI visualisations.
// [HAZARD H409] its_make_snap() uses a while(!is_approx(...)) loop over a
// floating-point angle step.  If the step size doesn't converge to the
// stop angle exactly (due to float rounding), this loop runs forever.
// [HAZARD H410] its_make_groove_plane() takes cur_groove_vertices by non-const
// reference and appends to it as a side effect.  Callers must pre-allocate or
// expect the vector to grow.
indexed_triangle_set its_make_cube(double x, double y, double z);
indexed_triangle_set its_make_prism(float width, float length, float height);
indexed_triangle_set its_make_cylinder(double r, double h, double fa = (2 * PI / 180));
indexed_triangle_set its_make_cone(double r, double h, double fa = (2 * PI / 180));
indexed_triangle_set its_make_frustum(double r, double h, double fa = (2 * PI / 180));
indexed_triangle_set its_make_torus(double r, double h, double fa);
indexed_triangle_set its_make_frustum_dowel(double r, double h, int sectorCount);
indexed_triangle_set its_make_pyramid(float base, float height);
indexed_triangle_set its_make_sphere(double radius, double fa);
// [HAZARD H409] See above — infinite-loop risk in float convergence loop.
indexed_triangle_set its_make_snap(double r, double h, float space_proportion = 0.25f, float bulge_proportion = 0.125f);
indexed_triangle_set its_make_groove_plane(const Groove& cur_groove, float rotate_radius, std::vector<Vec3d>& cur_groove_vertices);

// [INTENT] Compute the 3D convex hull of a point cloud or mesh using qhull.
// Returns an indexed_triangle_set.
// [HAZARD H401] See TriangleMesh::convex_hull_3d — qhull may produce
// non-manifold output; the manifold assertion is disabled in the .cpp.
indexed_triangle_set        its_convex_hull(const std::vector<Vec3f>& pts);
inline indexed_triangle_set its_convex_hull(const indexed_triangle_set& its) { return its_convex_hull(its.vertices); }

// [INTENT] Convenience wrappers that construct a TriangleMesh directly from the
// its_make_* primitives above.  Used throughout the codebase to build simple
// solids without a separate stats-fill step (the TriangleMesh(ITS&&) constructor
// calls fill_initial_stats).
inline TriangleMesh make_cube(double x, double y, double z) { return TriangleMesh(its_make_cube(x, y, z)); }
inline TriangleMesh make_prism(float width, float length, float height) { return TriangleMesh(its_make_prism(width, length, height)); }
inline TriangleMesh make_cylinder(double r, double h, double fa = (2 * PI / 180)) { return TriangleMesh{its_make_cylinder(r, h, fa)}; }
inline TriangleMesh make_cone(double r, double h, double fa = (2 * PI / 180)) { return TriangleMesh(its_make_cone(r, h, fa)); }
inline TriangleMesh make_pyramid(float base, float height) { return TriangleMesh(its_make_pyramid(base, height)); }
inline TriangleMesh make_sphere(double rho, double fa = (2 * PI / 90)) { return TriangleMesh(its_make_sphere(rho, fa)); }
inline TriangleMesh make_torus(double r, double h, double fa = (PI / 60)) { return TriangleMesh(its_make_torus(r, h, fa)); }

// [INTENT] Write an indexed_triangle_set to an STL file in ASCII or binary
// format.  The binary writer swaps bytes on big-endian platforms via
// big_endian_reverse_quads() called on a stack copy of the count variable.
// [HAZARD H411] its_write_stl_binary() calls big_endian_reverse_quads() on
// the nfaces local variable (stack memory) before writing it.  This is correct
// but unusual — the function modifies then reads a stack copy, not the original
// vector.  Target language implementations must replicate this byte-swap for
// cross-platform STL compatibility.
bool        its_write_stl_ascii(const char*                                     file,
                                const char*                                     label,
                                const std::vector<stl_triangle_vertex_indices>& indices,
                                const std::vector<stl_vertex>&                  vertices);
inline bool its_write_stl_ascii(const char* file, const char* label, const indexed_triangle_set& its)
{
    return its_write_stl_ascii(file, label, its.indices, its.vertices);
}
bool        its_write_stl_binary(const char*                                     file,
                                 const char*                                     label,
                                 const std::vector<stl_triangle_vertex_indices>& indices,
                                 const std::vector<stl_vertex>&                  vertices);
inline bool its_write_stl_binary(const char* file, const char* label, const indexed_triangle_set& its)
{
    return its_write_stl_binary(file, label, its.indices, its.vertices);
}

inline BoundingBoxf3 bounding_box(const TriangleMesh& m) { return m.bounding_box(); }
// [INTENT] Compute bounding box directly from an indexed_triangle_set without a
// TriangleMesh wrapper.  O(V) scan; no cached stats available here.
inline BoundingBoxf3 bounding_box(const indexed_triangle_set& its)
{
    if (its.vertices.empty())
        return {};

    Vec3f bmin = its.vertices.front(), bmax = its.vertices.front();

    for (const Vec3f& p : its.vertices) {
        bmin = p.cwiseMin(bmin);
        bmax = p.cwiseMax(bmax);
    }

    return {bmin.cast<double>(), bmax.cast<double>()};
}

} // namespace Slic3r

// [INTENT] Cereal serialisation specialisation for TriangleMesh.
// Saves/loads the TriangleMeshStats struct as raw binary followed by the
// index and vertex arrays via Cereal's standard archive.
// [HAZARD H394] (repeated from TriangleMeshStats above) Raw binary layout of
// TriangleMeshStats is NOT versioned.  Any struct change (field add/remove,
// reorder, alignment difference between compilers or platforms) silently
// corrupts existing serialised data.  A target language port must introduce an
// explicit version tag and migration logic.
// [COUPLING] Uses const_cast to call loadBinary on a const stats reference —
// the stats() accessor returns const& but load needs a mutable pointer.
// This const_cast is safe at runtime but relies on the internal layout of
// TriangleMeshStats matching exactly what was saved.
// Serialization through the Cereal library
#include <cereal/access.hpp>
namespace cereal {
template<class Archive> struct specialize<Archive, Slic3r::TriangleMesh, cereal::specialization::non_member_load_save>
{};
template<class Archive> void load(Archive& archive, Slic3r::TriangleMesh& mesh)
{
    archive.loadBinary(reinterpret_cast<char*>(const_cast<Slic3r::TriangleMeshStats*>(&mesh.stats())), sizeof(Slic3r::TriangleMeshStats));
    archive(mesh.its.indices, mesh.its.vertices);
}
template<class Archive> void save(Archive& archive, const Slic3r::TriangleMesh& mesh)
{
    archive.saveBinary(reinterpret_cast<const char*>(&mesh.stats()), sizeof(Slic3r::TriangleMeshStats));
    archive(mesh.its.indices, mesh.its.vertices);
}
} // namespace cereal

#endif
