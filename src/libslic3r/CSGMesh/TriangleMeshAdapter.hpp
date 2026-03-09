// [INTENT] Provides ADL (Argument-Dependent Lookup) overloads so that
//          indexed_triangle_set, indexed_triangle_set*, TriangleMesh, and
//          TriangleMesh* can all be used directly as "CSG parts" in the
//          generic CSG algorithms (slice_csgmesh_ex, perform_csgmesh_booleans,
//          voxelize_csgmesh, etc.) without wrapping them in CSGPart.
//
// [INTENT] All four types are treated as unconditional Union parts with
//          identity transform.  They cannot represent Difference or
//          Intersection, and they cannot push/pop the CSG stack.
//
// [COUPLING] Depends only on CSGMesh.hpp (CSGType, CSGStackOp, Transform3f)
//            and TriangleMesh.hpp.  Zero runtime overhead — all functions are
//            inline and trivially inlinable.
//
// [STATE] All functions are stateless and pure (no side effects).
//
// [HAZARD H976] get_mesh(const TriangleMesh* const part) at line 83 returns
//               &part->its — a pointer into the TriangleMesh object.  If the
//               caller passes a null pointer, the dereference is UB.  There
//               is no null check.  The value-type overload (TriangleMesh&) is
//               safe by reference; only the pointer overload is hazardous.
//               A port should add an explicit null check or use std::optional.
//               Severity: P2/Medium.
//
// [HAZARD H977] None of these adapters support transforms.  Any code that
//               needs to use a positioned TriangleMesh as a CSG part (e.g.,
//               a mesh at world-space position) must manually compose the
//               transform into the ITS before calling — or wrap in a CSGPart.
//               Using these adapters for transformed meshes silently produces
//               geometry at the origin regardless of the mesh's intended
//               position.  Severity: P2/Medium (correctness, silent).

#ifndef TRIANGLEMESHADAPTER_HPP
#define TRIANGLEMESHADAPTER_HPP

#include "CSGMesh.hpp"

#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r { namespace csg {

// [INTENT] Default overloads for indexed_triangle_set to be usable as a plain
// CSGPart with an implicit union operation.

// --- indexed_triangle_set (value) ---

inline CSGType get_operation(const indexed_triangle_set& part)
{
    return CSGType::Union; // always Union; no way to express Difference/Intersection
}

inline CSGStackOp get_stack_operation(const indexed_triangle_set& part)
{
    return CSGStackOp::Continue; // never pushes or pops the CSG stack
}

inline const indexed_triangle_set* get_mesh(const indexed_triangle_set& part) { return &part; }

inline Transform3f get_transform(const indexed_triangle_set& part)
{
    return Transform3f::Identity(); // [HAZARD H977] no transform supported
}

// --- indexed_triangle_set* (pointer) ---

inline CSGType get_operation(const indexed_triangle_set* const part) { return CSGType::Union; }

inline CSGStackOp get_stack_operation(const indexed_triangle_set* const part) { return CSGStackOp::Continue; }

inline const indexed_triangle_set* get_mesh(const indexed_triangle_set* const part)
{
    return part; // may be null — callers that pass null get a null mesh (safe in CSG machinery)
}

inline Transform3f get_transform(const indexed_triangle_set* const part)
{
    return Transform3f::Identity(); // [HAZARD H977] no transform supported
}

// --- TriangleMesh (value) ---

inline CSGType get_operation(const TriangleMesh& part) { return CSGType::Union; }

inline CSGStackOp get_stack_operation(const TriangleMesh& part) { return CSGStackOp::Continue; }

inline const indexed_triangle_set* get_mesh(const TriangleMesh& part) { return &part.its; }

inline Transform3f get_transform(const TriangleMesh& part)
{
    return Transform3f::Identity(); // [HAZARD H977] no transform supported
}

// --- TriangleMesh* (pointer) ---

inline CSGType get_operation(const TriangleMesh* const part) { return CSGType::Union; }

inline CSGStackOp get_stack_operation(const TriangleMesh* const part) { return CSGStackOp::Continue; }

// [HAZARD H976] No null check before dereferencing `part`.
inline const indexed_triangle_set* get_mesh(const TriangleMesh* const part)
{
    return &part->its; // [HAZARD H976] UB if part == nullptr
}

inline Transform3f get_transform(const TriangleMesh* const part)
{
    return Transform3f::Identity(); // [HAZARD H977] no transform supported
}

}} // namespace Slic3r::csg

#endif // TRIANGLEMESHADAPTER_HPP
