#ifndef libslic3r_MeshBoolean_hpp_
#define libslic3r_MeshBoolean_hpp_

// [INTENT] Public API for mesh boolean operations in OrcaSlicer. Three backends:
//   1. igl (Eigen-based): `minus`, `self_union` operating on EigenMesh
//      (pair<MatrixXd vertices, MatrixXi faces>). Uses libigl+CGAL under the hood.
//   2. cgal (CGAL Surface Mesh): `minus`, `plus`, `intersect` on CGALMesh or
//      indexed_triangle_set/TriangleMesh. Also provides `segment`, `merge`,
//      `does_self_intersect`, `does_bound_a_volume`.
//   3. mcut (MCUT library): `do_boolean_single`, `do_boolean`, `make_boolean`
//      on McutMesh. Handles multi-component meshes by splitting and recombining.
//
// [COUPLING] igl backend: libigl + CGAL EPEC kernel. cgal backend: CGAL Surface Mesh
// PMP library. mcut backend: MCUT library (mcut.h). All backends defined in MeshBoolean.cpp.
//
// [MEMORY]
//   EigenMesh: pair of Eigen::MatrixXd/MatrixXi — heap-allocated contiguous blocks.
//   CGALMeshPtr: unique_ptr with custom CGALMeshDeleter — CGAL heap allocation.
//   McutMeshPtr: unique_ptr with custom McutMeshDeleter — heap allocation.
//
// [CONCURRENCY] No thread safety guarantees. All operations are single-threaded.
// CGAL boolean operations internally use exact arithmetic (EPEC kernel) which is
// deterministic but expensive. mcut operations may throw on failure.
//
// [HAZARD H1016] (P1/High): `triangle_mesh_to_eigen(const TriangleMesh&)` in .cpp uses
// `mesh.its.vertices.front().data()` to create an Eigen::Map. If `vertices` is empty,
// `.front()` is UB (accessing the element before begin on an empty vector). No empty-mesh
// check precedes the call. Callers passing empty meshes crash silently.
//
// [HAZARD H1017] (P1/High): CGAL boolean operations (`_cgal_do`) use `try_catch_signal`
// to catch SIGSEGV and SIGFPE from CGAL internal crashes. This is a signal-handling
// workaround — not all CGAL failures are catchable this way, and signal handlers in
// C++ have very restricted allowed operations. A port cannot rely on this safety net.
//
// [HAZARD H1018] (P1/High): `triangle_mesh_to_cgal(TriangleMesh)` throws
// `Slic3r::RuntimeError("Mesh not watertight")` for open (non-closed) meshes.
// All callers using the TriangleMesh overload must be exception-safe. The
// indexed_triangle_set overload does NOT throw — callers that switch from ITS to
// TriangleMesh overload silently gain exception semantics.
//
// [HAZARD H1019] (P2/Medium): `merge_mcut_meshes` converts both McutMesh objects to
// TriangleMesh and back through indexed_triangle_set. This round-trip is used as the
// fallback when mcut's boolean fails. The conversion is expensive and potentially lossy
// (double→float→double coordinate truncation for vertices).
//
// [HAZARD H1020] (P2/Medium): `do_boolean_single` finds `booleanOpts[boolean_opts]` via
// iterator `it = booleanOpts.find(boolean_opts)` and then unconditionally dereferences
// `it->second` without checking `it != booleanOpts.end()`. An unknown boolean_opts string
// produces UB (dereference of end iterator). No input validation.
//
// [HAZARD H1021] (P2/Medium): `do_boolean` (multi-component) round-trips McutMesh through
// TriangleMesh→its_split→McutMesh per part. For large multi-component meshes this
// allocates O(N_parts × mesh_size) memory and performs O(N_src × N_cut) boolean pairs.
// For typical use (1-3 components) this is acceptable, but adversarial inputs
// (many disconnected shells) can cause quadratic time/memory blowup.

#include <memory>
#include <exception>

#include <libslic3r/TriangleMesh.hpp>
#include <Eigen/Geometry>

namespace Slic3r { namespace MeshBoolean {

// [INTENT] Eigen-based mesh representation for the igl backend.
// First: Nx3 double matrix of vertex positions (world-space mm).
// Second: Mx3 int matrix of triangle indices.
// [HAZARD H1016] See module header — `.front()` UB on empty vertices.
using EigenMesh = std::pair<Eigen::MatrixXd, Eigen::MatrixXi>;

TriangleMesh eigen_to_triangle_mesh(const EigenMesh& emesh);
EigenMesh    triangle_mesh_to_eigen(const TriangleMesh& mesh);

void minus(EigenMesh& A, const EigenMesh& B);
void self_union(EigenMesh& A);

void minus(TriangleMesh& A, const TriangleMesh& B);
void self_union(TriangleMesh& mesh);

namespace cgal {

// [INTENT] Opaque wrapper around CGAL::Surface_mesh<EpicKernel::Point_3>.
// Uses exact predicates, inexact constructions kernel (EPIC).
// [HAZARD H1017] Boolean ops use try_catch_signal for SIGSEGV/SIGFPE.
struct CGALMesh;
struct CGALMeshDeleter
{
    void operator()(CGALMesh* ptr);
};
using CGALMeshPtr = std::unique_ptr<CGALMesh, CGALMeshDeleter>;

CGALMeshPtr clone(const CGALMesh& m);

void save_CGALMesh(const std::string& fname, const CGALMesh& cgal_mesh);

// [HAZARD H1018] Does NOT throw for open meshes — uses the ITS overload which
// just adds faces directly without watertight check.
CGALMeshPtr triangle_mesh_to_cgal(const std::vector<stl_vertex>& V, const std::vector<stl_triangle_vertex_indices>& F);

inline CGALMeshPtr triangle_mesh_to_cgal(const indexed_triangle_set& M) { return triangle_mesh_to_cgal(M.vertices, M.indices); }
// [HAZARD H1018] This overload DOES throw RuntimeError("Mesh not watertight")
// for non-closed meshes via the template specialisation that calls orient_to_bound_a_volume.
inline CGALMeshPtr triangle_mesh_to_cgal(const TriangleMesh& M) { return triangle_mesh_to_cgal(M.its); }

TriangleMesh         cgal_to_triangle_mesh(const CGALMesh& cgalmesh);
indexed_triangle_set cgal_to_indexed_triangle_set(const CGALMesh& cgalmesh);

// Do boolean mesh difference with CGAL bypassing igl.
// All operations throw Slic3r::HardCrash on SIGSEGV/SIGFPE and
// Slic3r::RuntimeError on boolean failure.
void minus(TriangleMesh& A, const TriangleMesh& B);
void plus(TriangleMesh& A, const TriangleMesh& B);
void intersect(TriangleMesh& A, const TriangleMesh& B);

void minus(indexed_triangle_set& A, const indexed_triangle_set& B);
void plus(indexed_triangle_set& A, const indexed_triangle_set& B);
void intersect(indexed_triangle_set& A, const indexed_triangle_set& B);

void minus(CGALMesh& A, CGALMesh& B);
void plus(CGALMesh& A, CGALMesh& B);
void intersect(CGALMesh& A, CGALMesh& B);

bool does_self_intersect(const TriangleMesh& mesh);
bool does_self_intersect(const CGALMesh& mesh);

// BBS
//  [INTENT] SDF-based mesh segmentation using CGAL. Returns up to segment_number
//  separate TriangleMeshes, each representing a semantic part of src.
//  Open holes in each segment are triangulate-hole filled.
std::vector<TriangleMesh> segment(const TriangleMesh& src, double smoothing_alpha = 0.5, int segment_number = 5);
TriangleMesh              merge(std::vector<TriangleMesh> meshes);

bool does_bound_a_volume(const CGALMesh& mesh);
bool empty(const CGALMesh& mesh);
} // namespace cgal

namespace mcut {

// [INTENT] MCUT mesh representation — arrays of vertex coordinates (double x3),
// face index arrays (uint32_t), and face sizes (uint32_t per face, always 3).
// Maintains triangle topology (faceSizesArray is all-3s).
struct McutMesh;
struct McutMeshDeleter
{
    void operator()(McutMesh* ptr);
};
using McutMeshPtr = std::unique_ptr<McutMesh, McutMeshDeleter>;
bool empty(const McutMesh& mesh);

McutMeshPtr  triangle_mesh_to_mcut(const indexed_triangle_set& M);
TriangleMesh mcut_to_triangle_mesh(const McutMesh& mcutmesh);

// [INTENT] Perform a boolean operation on a single-component src vs single-component cut.
// boolean_opts must be one of: "A_NOT_B", "B_NOT_A", "UNION", "INTERSECTION".
// Returns true on success, false on mcut failure (except UNION which falls back to merge).
// [HAZARD H1020] No validation of boolean_opts — unknown string causes UB (end iterator deref).
// do boolean and save result to srcMesh
// return true if sucessful
bool do_boolean_single(McutMesh& srcMesh, const McutMesh& cutMesh, const std::string& boolean_opts);

// [INTENT] Multi-component wrapper: splits both src and cut into connected components,
// runs do_boolean_single for each pair, merges results. Handles the MCUT limitation
// of only supporting single-component inputs.
// [HAZARD H1021] O(N_src × N_cut) boolean pairs — quadratic blowup for many-shell meshes.
// do boolean of mesh with multiple volumes and save result to srcMesh
// Both srcMesh and cutMesh may have multiple volumes.
void do_boolean(McutMesh& srcMesh, const McutMesh& cutMesh, const std::string& boolean_opts);

// [INTENT] Convenience wrapper: convert TriangleMeshes to McutMesh, run do_boolean,
// convert result back. dst_mesh receives the result mesh(es).
// do boolean and convert result to TriangleMesh
void make_boolean(const TriangleMesh&        src_mesh,
                  const TriangleMesh&        cut_mesh,
                  std::vector<TriangleMesh>& dst_mesh,
                  const std::string&         boolean_opts);
} // namespace mcut

}} // namespace Slic3r::MeshBoolean
#endif // libslic3r_MeshBoolean_hpp_
