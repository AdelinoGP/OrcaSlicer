// [INTENT] Provides the top-level entry points for performing CSG boolean
//          operations over a range of CSGPart objects using either CGAL or
//          mcut as the underlying boolean engine.  Also provides a validation
//          function (check_csgmesh_booleans) that pre-screens each mesh for
//          emptiness, non-manifold topology, and self-intersection before
//          committing to an expensive boolean evaluation.
//
// [STATE] All state is local to each call; no module-level mutable state.
//         The opstack (std::stack<Frame>) models the Push/Pop CSG tree
//         structure; it must be empty at entry and contain exactly one frame
//         at exit.
//
// [CONCURRENCY] get_cgalptrs / get_mcutptrs use execution::for_each (TBB)
//               to convert all CSGParts to CGAL/mcut meshes in parallel.
//               The serial reduce loop (perform_csgmesh_booleans_cgal /
//               _mcut) then applies operations in CSG-tree order.
//               [HAZARD H967] There is no parallelism guard on the reduce
//               loop itself; the TBB mesh-build phase writes into a
//               std::vector<CGALMeshPtr> that is then read in a separate
//               serial loop.  This is safe because TBB for_each completes
//               before the serial loop begins.  However, the index variable
//               `csgidx` in the serial loop is NOT bounds-checked against
//               cgalmeshes.size().  If csgrange.size() changes between the
//               parallel build and serial reduce (impossible with const range,
//               but unsafe after a refactor that relaxes constness),
//               out-of-bounds access results.  Severity: P3/Low.
//
// [COUPLING] Heavy: MeshBoolean.hpp (CGAL and mcut bindings), CSGMesh.hpp,
//            ExecutionTBB.hpp.  The commented-out include of ExecutionSeq.hpp
//            (line 10) signals that the sequential fallback is untested.
//
// [HAZARD H968] Both perform_csg() overloads silently do nothing when dst or
//               src is null (lines 80-81 for CGAL, 119-121 for mcut).  A null
//               src is expected for Push/Pop sentinels, but a null dst at a
//               non-Push step indicates a previous boolean failed silently
//               (e.g., CGAL threw and was caught).  The result mesh is then
//               incomplete with no error propagated to the caller.
//               Severity: P2/Medium.
//
// [HAZARD H969] check_csgmesh_booleans uses execution::for_each (TBB) to
//               write into shared `fail_reason` and `fail_part_name` variables
//               without synchronisation (lines 266-303).  Multiple threads can
//               race on these two fields simultaneously.  In practice, the
//               first failure short-circuits further useful work but the race
//               still constitutes UB under the C++ memory model.
//               Severity: P1/High.
//
// [HAZARD H970] perform_csgmesh_booleans_cgal line 193: the serial loop calls
//               perform_csg(get_operation(csgpart), ...) with the live
//               csgpart's operation — but for a Push part, `op` was already
//               overridden to Union (line 188) and the stack frame was pushed.
//               The direct call on line 193 therefore operates on the *push*
//               sentinel's mesh (which is empty) using the push's *original*
//               operation (Union or Difference) rather than Union.  The net
//               effect is that perform_csg on an empty src is a no-op (guarded
//               at line 80/81), so the bug is latent rather than observable —
//               but the double `op` variable creates a misleading divergence
//               between what the comment says and what the code does.
//               Severity: P3/Low.
//
// [HAZARD H971] mcut overload does NOT check does_bound_a_volume or
//               does_self_intersect (only emptiness is checked at line 346).
//               Callers using use_mcut=true get a weaker validity gate than
//               the CGAL path; non-manifold inputs passed to mcut booleans
//               will produce corrupt output or crash inside the mcut library.
//               Severity: P2/Medium.

#ifndef PERFORMCSGMESHBOOLEANS_HPP
#define PERFORMCSGMESHBOOLEANS_HPP

#include <stack>
#include <vector>

#include "CSGMesh.hpp"

#include "libslic3r/Execution/ExecutionTBB.hpp"
// #include "libslic3r/Execution/ExecutionSeq.hpp"  // [HAZARD] sequential fallback untested
#include "libslic3r/MeshBoolean.hpp"

namespace Slic3r { namespace csg {
enum class BooleanFailReason { OK, MeshEmpty, NotBoundAVolume, SelfIntersect, NoIntersection };

// [INTENT] Convert a CSGPart to a CGALMesh, applying the part's transform.
//          Can be overridden (ADL) for CSGPart types that cache CGAL meshes.
// [MEMORY] Returns a new heap-allocated CGALMesh; caller owns via unique_ptr.
// [STATE] mesh is copied (its_transform modifies a local copy).
template<class CSGPartT> MeshBoolean::cgal::CGALMeshPtr get_cgalmesh(const CSGPartT& csgpart)
{
    const indexed_triangle_set* its = csg::get_mesh(csgpart);
    indexed_triangle_set        dummy;

    if (!its)
        its = &dummy;

    MeshBoolean::cgal::CGALMeshPtr ret;

    indexed_triangle_set m = *its;
    its_transform(m, get_transform(csgpart), true);

    try {
        ret = MeshBoolean::cgal::triangle_mesh_to_cgal(m);
    } catch (...) {
        // [HAZARD H968] errors ignored, ret remains null — caller gets no
        //               error signal; incomplete boolean result.
        ret = nullptr;
    }

    return ret;
}

// [INTENT] Convert a CSGPart to a McutMesh (mcut boolean engine), applying transform.
// [MEMORY] Returns a new heap-allocated McutMesh; caller owns via unique_ptr.
template<class CSGPartT> MeshBoolean::mcut::McutMeshPtr get_mcutmesh(const CSGPartT& csgpart)
{
    const indexed_triangle_set* its = csg::get_mesh(csgpart);
    indexed_triangle_set        dummy;

    if (!its)
        its = &dummy;

    MeshBoolean::mcut::McutMeshPtr ret;

    indexed_triangle_set m = *its;
    its_transform(m, get_transform(csgpart), true);

    try {
        ret = MeshBoolean::mcut::triangle_mesh_to_mcut(m);
    } catch (...) {
        // [HAZARD H968] errors ignored — same silent-null hazard as CGAL path.
        ret = nullptr;
    }

    return ret;
}

namespace detail_cgal {

using MeshBoolean::cgal::CGALMeshPtr;

// [INTENT] Apply one CSG boolean (Union/Difference/Intersection) to dst in-place.
// [STATE] Modifies dst. src is consumed.
// [HAZARD H968] dst or src null → no-op; no error propagated.
inline void perform_csg(CSGType op, CGALMeshPtr& dst, CGALMeshPtr& src)
{
    if (!dst && op == CSGType::Union && src) {
        dst = std::move(src);
        return;
    }

    if (!dst || !src)
        return; // [HAZARD H968] silent no-op on null

    switch (op) {
    case CSGType::Union: MeshBoolean::cgal::plus(*dst, *src); break;
    case CSGType::Difference: MeshBoolean::cgal::minus(*dst, *src); break;
    case CSGType::Intersection: MeshBoolean::cgal::intersect(*dst, *src); break;
    }
}

// [INTENT] Parallel conversion of all parts in csgrange to CGALMeshPtr.
// [CONCURRENCY] TBB for_each; result vector pre-sized to csgrange.size().
//               Index mapping is stable because csgrange is const.
template<class Ex, class It> std::vector<CGALMeshPtr> get_cgalptrs(Ex policy, const Range<It>& csgrange)
{
    std::vector<CGALMeshPtr> ret(csgrange.size());
    execution::for_each(policy, size_t(0), csgrange.size(), [&csgrange, &ret](size_t i) {
        auto it = csgrange.begin();
        std::advance(it, i); // O(N) advance for non-random-access iterators — O(N²) total
        auto& csgpart = *it;
        ret[i]        = get_cgalmesh(csgpart);
    });

    return ret;
}

} // namespace detail_cgal

namespace detail_mcut {

using MeshBoolean::mcut::McutMeshPtr;

// [INTENT] Apply one CSG boolean using mcut engine.
// [HAZARD H968] dst or src null → no-op.
inline void perform_csg(CSGType op, McutMeshPtr& dst, McutMeshPtr& src)
{
    if (!dst && op == CSGType::Union && src) {
        dst = std::move(src);
        return;
    }

    if (!dst || !src)
        return; // [HAZARD H968] silent no-op on null

    switch (op) {
    case CSGType::Union: MeshBoolean::mcut::do_boolean(*dst, *src, "UNION"); break;
    case CSGType::Difference: MeshBoolean::mcut::do_boolean(*dst, *src, "A_NOT_B"); break;
    case CSGType::Intersection: MeshBoolean::mcut::do_boolean(*dst, *src, "INTERSECTION"); break;
    }
}

// [INTENT] Parallel conversion of all parts to McutMeshPtr.
// [CONCURRENCY] Same TBB pattern as get_cgalptrs.
template<class Ex, class It> std::vector<McutMeshPtr> get_mcutptrs(Ex policy, const Range<It>& csgrange)
{
    std::vector<McutMeshPtr> ret(csgrange.size());
    execution::for_each(policy, size_t(0), csgrange.size(), [&csgrange, &ret](size_t i) {
        auto it = csgrange.begin();
        std::advance(it, i); // O(N) advance for non-RA iterators
        auto& csgpart = *it;
        ret[i]        = get_mcutmesh(csgpart);
    });

    return ret;
}

} // namespace detail_mcut

// [INTENT] Walk the CSG range in sequence, applying Push/Pop stack operations
//          and reducing with CGAL booleans.  Parallel mesh conversion is done
//          first, then the serial reduce follows CSG-tree order.
// [STATE] opstack: one Frame per Push; top frame accumulates the running result.
// [HAZARD H967] csgidx not bounds-checked against cgalmeshes.size().
// [HAZARD H970] Push part: op re-assigned to Union but original op still used
//               in line 193 call — effectively a no-op for empty sentinel mesh.
template<class It> void perform_csgmesh_booleans_cgal(MeshBoolean::cgal::CGALMeshPtr& cgalm, const Range<It>& csgrange)
{
    using MeshBoolean::cgal::CGALMesh;
    using MeshBoolean::cgal::CGALMeshPtr;
    using namespace detail_cgal;

    struct Frame
    {
        CSGType     op;
        CGALMeshPtr cgalptr;
        explicit Frame(CSGType csgop = CSGType::Union)
            : op{csgop}, cgalptr{MeshBoolean::cgal::triangle_mesh_to_cgal(indexed_triangle_set{})}
        {}
    };

    std::stack opstack{std::vector<Frame>{}};

    opstack.push(Frame{});

    // [CONCURRENCY] Parallel mesh conversion phase.
    std::vector<CGALMeshPtr> cgalmeshes = get_cgalptrs(ex_tbb, csgrange);

    size_t csgidx = 0;
    for (auto& csgpart : csgrange) {
        auto         op      = get_operation(csgpart);
        CGALMeshPtr& cgalptr = cgalmeshes[csgidx++]; // [HAZARD H967] no bounds check

        if (get_stack_operation(csgpart) == CSGStackOp::Push) {
            opstack.push(Frame{op});
            op = CSGType::Union; // override for sentinel — [HAZARD H970]
        }

        Frame* top = &opstack.top();

        // [HAZARD H970] uses get_operation(csgpart) again (not local `op`), so
        //               for Push parts this uses the original pre-override op.
        perform_csg(get_operation(csgpart), top->cgalptr, cgalptr);

        if (get_stack_operation(csgpart) == CSGStackOp::Pop) {
            CGALMeshPtr src   = std::move(top->cgalptr);
            auto        popop = opstack.top().op;
            opstack.pop();
            CGALMeshPtr& dst = opstack.top().cgalptr;
            perform_csg(popop, dst, src);
        }
    }

    cgalm = std::move(opstack.top().cgalptr);
}

// [INTENT] Same CSG reduce walk using mcut boolean engine.
// [HAZARD H971] mcut path: weaker validation gate (no manifold/self-intersect check).
// [HAZARD H968] Null meshes silently skipped.
template<class It> void perform_csgmesh_booleans_mcut(MeshBoolean::mcut::McutMeshPtr& mcutm, const Range<It>& csgrange)
{
    using MeshBoolean::mcut::McutMesh;
    using MeshBoolean::mcut::McutMeshPtr;
    using namespace detail_mcut;

    struct Frame
    {
        CSGType     op;
        McutMeshPtr mcutptr;
        explicit Frame(CSGType csgop = CSGType::Union)
            : op{csgop}, mcutptr{MeshBoolean::mcut::triangle_mesh_to_mcut(indexed_triangle_set{})}
        {}
    };

    std::stack opstack{std::vector<Frame>{}};

    opstack.push(Frame{});

    std::vector<McutMeshPtr> McutMeshes = get_mcutptrs(ex_tbb, csgrange);

    size_t csgidx = 0;
    for (auto& csgpart : csgrange) {
        auto         op      = get_operation(csgpart);
        McutMeshPtr& mcutptr = McutMeshes[csgidx++]; // [HAZARD H967] no bounds check

        if (get_stack_operation(csgpart) == CSGStackOp::Push) {
            opstack.push(Frame{op});
            op = CSGType::Union; // [HAZARD H970] same double-op pattern
        }

        Frame* top = &opstack.top();

        perform_csg(get_operation(csgpart), top->mcutptr, mcutptr);

        if (get_stack_operation(csgpart) == CSGStackOp::Pop) {
            McutMeshPtr src   = std::move(top->mcutptr);
            auto        popop = opstack.top().op;
            opstack.pop();
            McutMeshPtr& dst = opstack.top().mcutptr;
            perform_csg(popop, dst, src);
        }
    }

    mcutm = std::move(opstack.top().mcutptr);
}

// [INTENT] Validate all CSG parts before attempting boolean: checks for
//          empty mesh, non-manifold (does_not_bound_a_volume), and
//          self-intersection.  Calls visitor fn for each failing part.
//          Returns (fail_reason, fail_part_name, first_failing_iterator).
// [CONCURRENCY] TBB parallel validation — [HAZARD H969] unsynchronised
//               writes to fail_reason/fail_part_name across threads.
template<class It, class Visitor>
std::tuple<BooleanFailReason, std::string, It> check_csgmesh_booleans(const Range<It>& csgrange, Visitor&& vfn)
{
    using namespace detail_cgal;
    BooleanFailReason        fail_reason = BooleanFailReason::OK;
    std::string              fail_part_name;
    std::vector<CGALMeshPtr> cgalmeshes(csgrange.size());
    // [HAZARD H969] fail_reason and fail_part_name written from multiple TBB
    //               threads without synchronisation — data race UB.
    auto check_part = [&csgrange, &cgalmeshes, &fail_reason, &fail_part_name](size_t i) {
        auto it = csgrange.begin();
        std::advance(it, i);
        auto& csgpart = *it;
        auto  m       = get_cgalmesh(csgpart);

        // mesh can be nullptr if this is a stack push or pull
        if (!get_mesh(csgpart) && get_stack_operation(csgpart) != CSGStackOp::Continue) {
            cgalmeshes[i] = MeshBoolean::cgal::triangle_mesh_to_cgal(indexed_triangle_set{});
            return;
        }

        try {
            if (!m || MeshBoolean::cgal::empty(*m)) {
                BOOST_LOG_TRIVIAL(info) << "check_csgmesh_booleans fails! mesh " << i << "/" << csgrange.size()
                                        << " is empty, cannot do boolean!";
                fail_reason    = BooleanFailReason::MeshEmpty; // [HAZARD H969] race
                fail_part_name = csgpart.name;                 // [HAZARD H969] race
                return;
            }

            if (!MeshBoolean::cgal::does_bound_a_volume(*m)) {
                BOOST_LOG_TRIVIAL(info) << "check_csgmesh_booleans fails! mesh " << i << "/" << csgrange.size()
                                        << " does_bound_a_volume is false, cannot do boolean!";
                fail_reason    = BooleanFailReason::NotBoundAVolume; // [HAZARD H969] race
                fail_part_name = csgpart.name;                       // [HAZARD H969] race
                return;
            }

            if (MeshBoolean::cgal::does_self_intersect(*m)) {
                BOOST_LOG_TRIVIAL(info) << "check_csgmesh_booleans fails! mesh " << i << "/" << csgrange.size()
                                        << " does_self_intersect is true, cannot do boolean!";
                fail_reason    = BooleanFailReason::SelfIntersect; // [HAZARD H969] race
                fail_part_name = csgpart.name;                     // [HAZARD H969] race
                return;
            }
        } catch (...) {
            return;
        }

        cgalmeshes[i] = std::move(m);
    };
    execution::for_each(ex_tbb, size_t(0), csgrange.size(), check_part);

    It ret = csgrange.end();
    for (size_t i = 0; i < csgrange.size(); ++i) {
        if (!cgalmeshes[i]) {
            auto it = csgrange.begin();
            std::advance(it, i);
            vfn(it);

            if (ret == csgrange.end())
                ret = it;
        }
    }

    return {fail_reason, fail_part_name, ret};
}

// [INTENT] Two-overload facade: CGAL validation (default) or mcut validation.
// [HAZARD H971] mcut branch skips manifold + self-intersect checks.
template<class It> std::tuple<BooleanFailReason, std::string, It> check_csgmesh_booleans(const Range<It>& csgrange, bool use_mcut = false)
{
    if (!use_mcut)
        return check_csgmesh_booleans(csgrange, [](auto&) {});
    else {
        using namespace detail_mcut;
        BooleanFailReason fail_reason = BooleanFailReason::OK;
        std::string       fail_part_name;

        std::vector<McutMeshPtr> McutMeshes(csgrange.size());
        // [HAZARD H969] same unsynchronised write pattern as CGAL path.
        auto check_part = [&csgrange, &McutMeshes, &fail_reason, &fail_part_name](size_t i) {
            auto it = csgrange.begin();
            std::advance(it, i);
            auto& csgpart = *it;
            auto  m       = get_mcutmesh(csgpart);

            // mesh can be nullptr if this is a stack push or pull
            if (!get_mesh(csgpart) && get_stack_operation(csgpart) != CSGStackOp::Continue) {
                McutMeshes[i] = MeshBoolean::mcut::triangle_mesh_to_mcut(indexed_triangle_set{});
                return;
            }

            try {
                if (!m || MeshBoolean::mcut::empty(*m)) {
                    fail_reason    = BooleanFailReason::MeshEmpty;
                    fail_part_name = csgpart.name;
                    return;
                }
                // [HAZARD H971] does_bound_a_volume and does_self_intersect
                //               are NOT checked for mcut path.
            } catch (...) {
                return;
            }

            McutMeshes[i] = std::move(m);
        };
        execution::for_each(ex_tbb, size_t(0), csgrange.size(), check_part);
        return {fail_reason, fail_part_name, csgrange.end()};
    }
}

// [INTENT] Convenience wrapper: convert a CSG range to a single CGALMesh.
template<class It> MeshBoolean::cgal::CGALMeshPtr perform_csgmesh_booleans(const Range<It>& csgparts)
{
    auto ret = MeshBoolean::cgal::triangle_mesh_to_cgal(indexed_triangle_set{});
    if (ret)
        perform_csgmesh_booleans_cgal(ret, csgparts);
    return ret;
}

// [INTENT] Convenience wrapper: convert a CSG range to a single McutMesh.
template<class It> MeshBoolean::mcut::McutMeshPtr perform_csgmesh_booleans_mcut(const Range<It>& csgparts)
{
    auto ret = MeshBoolean::mcut::triangle_mesh_to_mcut(indexed_triangle_set{});
    if (ret)
        perform_csgmesh_booleans_mcut(ret, csgparts);
    return ret;
}

}} // namespace Slic3r::csg

#endif // PERFORMCSGMESHBOOLEANS_HPP
