#ifndef CSGMESH_HPP
#define CSGMESH_HPP

// [INTENT] Core type definitions and default interface functions for the CSG
// (Constructive Solid Geometry) mesh system. Defines the CSGPart concept —
// an object carrying a mesh pointer, a transformation, a CSG operation type
// (Union/Difference/Intersection), and a stack operation for grouping.
//
// A collection of CSGPartT objects represents a CSG tree in linearized form,
// using a bracket-based Push/Pop stack protocol to express parenthetical
// sub-expressions without requiring recursive data structures.
//
// [COUPLING] Consumed by: PerformCSGMeshBooleans.hpp (CGAL/mcut booleans),
// SliceCSGMesh.hpp (slicer), VoxelizeCSGMesh.hpp (OpenVDB voxelization),
// ModelToCSGMesh.hpp (Model → CSGPart conversion). All use template-based
// duck typing — any type that provides get_operation/get_mesh/get_transform/
// get_stack_operation overloads is a valid CSGPartT.
//
// [MEMORY] CSGPart holds an AnyPtr<const indexed_triangle_set> — a
// type-erased pointer that may own (unique_ptr) or borrow (raw ptr) the mesh.
// Ownership semantics depend on how the AnyPtr was constructed.
//
// [HAZARD] H959 — The Push/Pop stack protocol is implicit and unchecked.
// A collection with a Push that has no matching Pop (or vice versa) will
// silently produce wrong geometry — no assertion or exception guards this.
// Severity: P1/High
//
// [HAZARD] H960 — get_operation / get_mesh / get_transform are free function
// templates with default implementations accessing `.operation`, `.its_ptr`,
// `.trafo` directly. Any CSGPartT that uses different field names but does not
// provide custom overloads will silently compile (ADL resolution) but access
// non-existent members or wrong fields. Severity: P2/Medium

#include <libslic3r/AnyPtr.hpp>
#include <admesh/stl.h>

namespace Slic3r { namespace csg {

// [INTENT] Supported CSG operation types.
// Union     — add mesh volume to accumulated result
// Difference — subtract mesh volume from accumulated result
// Intersection — retain only the overlapping volume
enum class CSGType { Union, Difference, Intersection };

// [INTENT] Stack protocol for expressing grouped CSG sub-expressions.
// Push: push current state; the group's CSG op applies to the entire
//       sub-result when it is popped.
// Pop:  pop and combine the sub-result with the parent using the stored op.
// Continue: no stack change, apply operation directly to current top.
//
// Example: CUBE1 - (CUBE2 + CUBE3) is encoded as:
//   CUBE1: {Union, Continue}
//   CUBE2: {Difference, Push}   <- Push means "start a group, op=Difference applies to group"
//   CUBE3: {Union, Pop}         <- Pop merges group into parent
enum class CSGStackOp { Push, Continue, Pop };

// [INTENT] Free-function interface — default implementations access members
// directly. Override via ADL for custom CSGPartT types.
// [HAZARD] H960 — silent member access on non-conforming types; see header.
template<class CSGPartT> CSGType get_operation(const CSGPartT& part) { return part.operation; }

template<class CSGPartT> CSGStackOp get_stack_operation(const CSGPartT& part) { return part.stack_operation; }

// [INTENT] Returns a raw (non-owning) pointer to the underlying mesh.
// May return nullptr for sentinel Push/Pop parts that carry no mesh.
template<class CSGPartT> const indexed_triangle_set* get_mesh(const CSGPartT& part) { return part.its_ptr.get(); }

// [INTENT] Returns the 4x4 float transform applied to the mesh before any
// CSG operation. Identity means no transform.
template<class CSGPartT> Transform3f get_transform(const CSGPartT& part) { return part.trafo; }

// [INTENT] Default CSG part type. Holds all fields needed for the CSG
// pipeline: mesh pointer (owned or borrowed), transform, CSG operation,
// stack operation, and a debug name.
//
// [MEMORY] its_ptr uses AnyPtr which can hold:
//   - unique_ptr<const indexed_triangle_set>  (owned copy)
//   - raw const indexed_triangle_set*          (borrowed, caller must ensure lifetime)
//   - shared_ptr<const indexed_triangle_set>   (shared ownership)
// The caller must choose the appropriate ownership mode. Incorrect raw
// pointer use leads to dangling pointer access.
struct CSGPart
{
    AnyPtr<const indexed_triangle_set> its_ptr;
    Transform3f                        trafo;
    CSGType                            operation;
    CSGStackOp                         stack_operation;
    std::string                        name;

    CSGPart(AnyPtr<const indexed_triangle_set> ptr = {}, CSGType op = CSGType::Union, const Transform3f& tr = Transform3f::Identity())
        : its_ptr{std::move(ptr)}, operation{op}, stack_operation{CSGStackOp::Continue}, trafo{tr}
    {}
};

// Prusa
//  [INTENT] Returns true iff every part in the collection uses Union.
//  Used as a fast path in some callers to skip boolean evaluation.
template<class Cont> bool is_all_positive(const Cont& csgmesh)
{
    bool is_all_pos = std::all_of(csgmesh.begin(), csgmesh.end(),
                                  [](auto& part) { return csg::get_operation(part) == csg::CSGType::Union; });

    return is_all_pos;
}

// Prusa
//  [INTENT] Merge all Union parts into a single indexed_triangle_set without
//  performing any boolean operations. Useful as a fast approximation when
//  the model has no negative/intersection volumes, or as a fallback when
//  boolean ops fail.
//  [MEMORY] Allocates a new indexed_triangle_set; mesh copies are made via
//  its_transform (which deep-copies the mesh data).
template<class Cont> indexed_triangle_set csgmesh_merge_positive_parts(const Cont& csgmesh)
{
    indexed_triangle_set m;
    for (auto& csgpart : csgmesh) {
        auto                        op    = csg::get_operation(csgpart);
        const indexed_triangle_set* pmesh = csg::get_mesh(csgpart);
        if (pmesh && op == csg::CSGType::Union) {
            indexed_triangle_set mcpy = *pmesh;
            its_transform(mcpy, csg::get_transform(csgpart), true);
            its_merge(m, mcpy);
        }
    }

    return m;
}

}} // namespace Slic3r::csg

#endif // CSGMESH_HPP
