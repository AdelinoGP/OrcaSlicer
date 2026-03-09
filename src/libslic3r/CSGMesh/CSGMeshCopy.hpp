// [INTENT] Provides shallow and deep copy utilities for CSG part ranges,
//          plus a structural equality predicate (is_same). Shallow copy shares
//          mesh pointers (ref-counted via AnyPtr if available); deep copy
//          allocates independent indexed_triangle_set clones.
//
// [COUPLING] Depends on CSGMesh.hpp for CSGPart, AnyPtr, get_operation/
//            get_mesh/get_transform/get_stack_operation ADL accessors.
//            Consumers include PerformCSGMeshBooleans, VoxelizeCSGMesh, etc.
//
// [MEMORY] Shallow copy: mesh ownership is shared — the output range must not
//          outlive the source mesh collection.  Deep copy: each CSGPart in out
//          owns a unique_ptr<const indexed_triangle_set> heap allocation.
//
// [HAZARD H961] copy_csgrange_shallow uses if-constexpr to detect CSGPart
//               compatibility and obtain a shared pointer.  For non-CSGPart
//               input types that pass a raw pointer via get_mesh(), the copy
//               wraps it in AnyPtr<const indexed_triangle_set>{ptr} — a
//               non-owning wrapper.  If the source range is destroyed before
//               the copy, every mesh pointer in the copy dangles silently.
//               Severity: P1/High.
//
// [HAZARD H962] copy_csgrange_deep omits the stack_operation on the emitted
//               CSGPart when get_mesh() returns nullptr (empty push/pop
//               sentinel parts): it still calls cpy.stack_operation =
//               get_stack_operation(part) (line 47), so this is actually
//               correct.  However, the deep copy allocates a new
//               indexed_triangle_set(*meshptr) — for very large meshes this
//               can silently consume 2× the mesh memory with no upper-bound
//               check.  Severity: P3/Low.
//
// [HAZARD H963] is_same() uses get_mesh(*itA) == get_mesh(*itB) — pointer
//               equality, NOT structural equality.  Two ranges that contain
//               copies of the same geometry but at different addresses will
//               always compare unequal even if they represent identical CSG
//               scenes.  Callers expecting content equality will get false
//               negatives.  Severity: P2/Medium.

#ifndef CSGMESHCOPY_HPP
#define CSGMESHCOPY_HPP

#include "CSGMesh.hpp"

namespace Slic3r { namespace csg {

// [INTENT] Copy a csg range but for the meshes, only copy the pointers. If the copy
// is made from a CSGPart compatible object, and the pointer is a shared one,
// it will be copied with reference counting.
//
// [MEMORY] Output range shares mesh memory with source; source must outlive output.
// [COUPLING] Relies on AnyPtr::get_shared_cpy() for ref-counted sharing.
template<class It, class OutIt> void copy_csgrange_shallow(const Range<It>& csgrange, OutIt out)
{
    for (const auto& part : csgrange) {
        CSGPart cpy{{}, get_operation(part), get_transform(part)};

        cpy.stack_operation = get_stack_operation(part);

        // [STATE] if-constexpr branch: prefer shared (ref-counted) copy when
        // source is a CSGPart; otherwise fall through to raw-pointer wrap.
        if constexpr (std::is_convertible_v<decltype(part), const CSGPart&>) {
            if (auto shptr = part.its_ptr.get_shared_cpy()) {
                cpy.its_ptr = shptr;
            }
        }

        // [HAZARD H961] Non-CSGPart source: wraps raw pointer — see file header.
        if (!cpy.its_ptr)
            cpy.its_ptr = AnyPtr<const indexed_triangle_set>{get_mesh(part)};

        *out = std::move(cpy);
        ++out;
    }
}

// [INTENT] Copy the csg range, allocating new meshes for each part.
// [MEMORY] Each output CSGPart owns its indexed_triangle_set via unique_ptr.
// [HAZARD H962] Large meshes: allocates full copy of every ITS — may double
//               peak memory usage with no bound check.
template<class It, class OutIt> void copy_csgrange_deep(const Range<It>& csgrange, OutIt out)
{
    for (const auto& part : csgrange) {
        CSGPart cpy{{}, get_operation(part), get_transform(part)};

        if (auto meshptr = get_mesh(part)) {
            cpy.its_ptr = std::make_unique<const indexed_triangle_set>(*meshptr);
        }

        cpy.stack_operation = get_stack_operation(part);

        *out = std::move(cpy);
        ++out;
    }
}

// [INTENT] Structural equality check for two CSG ranges: same size, same
//          operation sequence, same transform (approx), and same mesh pointer.
// [HAZARD H963] Pointer equality for meshes — NOT content equality.
//               See file header.
template<class ItA, class ItB> bool is_same(const Range<ItA>& A, const Range<ItB>& B)
{
    bool ret = true;

    size_t s = A.size();

    if (B.size() != s)
        ret = false;

    size_t i   = 0;
    auto   itA = A.begin();
    auto   itB = B.begin();
    for (; ret && i < s; ++itA, ++itB, ++i) {
        ret = ret && get_mesh(*itA) == get_mesh(*itB) && get_operation(*itA) == get_operation(*itB) &&
              get_stack_operation(*itA) == get_stack_operation(*itB) && get_transform(*itA).isApprox(get_transform(*itB));
    }

    return ret;
}

}} // namespace Slic3r::csg

#endif // CSGCOPY_HPP
