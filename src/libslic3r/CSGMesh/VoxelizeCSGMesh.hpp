// [INTENT] Converts a CSG-part range into a combined OpenVDB voxel grid by:
//   1. Converting each CSGPart's mesh to a VoxelGrid in parallel (TBB).
//   2. Reducing the per-part grids in serial CSG-tree order (Union/Difference/
//      Intersection, honouring Push/Pop stack sentinels).
//
// [STATE] Parallel phase: grids[] vector, one VoxelGridPtr per CSGPart.
//         Serial reduce phase: opstack (std::stack<Frame>) mirrors CSG tree
//         nesting.  Base frame created with an empty grid at entry.
//
// [CONCURRENCY] Parallel voxelization is safe as each thread writes to a
//               distinct element of grids[].  The serial reduce is single-
//               threaded (grid_union/grid_difference/grid_intersection mutate
//               in-place and are not thread-safe on a shared grid).
//
// [COUPLING] OpenVDBUtils.hpp (mesh_to_grid, grid_union/difference/intersection,
//            clone, is_grid_empty), CSGMesh.hpp, ExecutionTBB.hpp.
//            VoxelizeParams == MeshToGridParams — any change to grid resolution
//            semantics in OpenVDBUtils is silently inherited.
//
// [HAZARD H978] get_voxelgrid() at line 23: params.trafo is mutated
//               in-place (`params.trafo(params.trafo() * get_transform(csgpart))`)
//               for each CSGPart.  The params object is passed by value, so
//               the mutation is local — BUT it is called from TBB for_each
//               (parallel).  Each lambda invocation receives its own `params`
//               copy (captured by value at line 67), so this is safe as
//               written.  However, if a future refactor changes the capture to
//               `[&]`, all threads will race on the shared `params` copy.
//               Severity: P2/Medium (latent race, safe today).
//
// [HAZARD H979] `statusfn()(-1)` is used as a cancellation probe (lines 68,
//               84).  The contract for statusfn returning -1 is "signal
//               cancellation check" — a truthy return from statusfn(-1) means
//               "cancel".  If statusfn is null, the `&&` short-circuit
//               prevents the call (line 68: `if (params.statusfn() && ...)`).
//               However, the check inside the serial loop (line 84) is
//               repeated, which means after a cancel signal in the parallel
//               phase, the serial loop also exits early — leaving opstack with
//               more than one frame.  `opstack.top().grid` is then returned
//               from the cancelled partial state rather than an empty grid,
//               producing a partially-processed result with no error code.
//               Severity: P2/Medium.
//
// [HAZARD H980] detail::perform_csg() for the Voxel case (lines 33-53) does
//               NOT guard against dst==nullptr (unlike the CGAL/mcut
//               equivalents).  If mesh_to_grid returns null (e.g., OpenVDB
//               allocation failure), the call to grid_union/grid_difference/
//               grid_intersection will dereference a null VoxelGridPtr,
//               producing a crash rather than a silent skip.
//               Severity: P1/High.
//
// [HAZARD H981] Union branch (line 40-42): if dst is not empty, grid_union is
//               called even if src is empty.  grid_union with an empty src
//               should be a no-op, but OpenVDB's grid_union modifies dst in
//               place and the behaviour for an empty source is implementation-
//               defined.  A port must verify the edge-case semantics of the
//               target language's voxel union primitive.
//               Severity: P3/Low.

#ifndef VOXELIZECSGMESH_HPP
#define VOXELIZECSGMESH_HPP

#include <functional>
#include <stack>

#include "CSGMesh.hpp"
#include "libslic3r/OpenVDBUtils.hpp"
#include "libslic3r/Execution/ExecutionTBB.hpp"

namespace Slic3r { namespace csg {

using VoxelizeParams = MeshToGridParams;

// [INTENT] Convert a single CSGPart to a VoxelGrid, incorporating its transform.
//          Can be overridden (ADL) for CSGPart types that cache voxel grids.
// [HAZARD H978] params mutated in-place (local copy) — safe per-call, brittle if
//               capture changes to reference.
template<class CSGPartT> VoxelGridPtr get_voxelgrid(const CSGPartT& csgpart, VoxelizeParams params)
{
    const indexed_triangle_set* its = csg::get_mesh(csgpart);
    VoxelGridPtr                ret;

    params.trafo(params.trafo() * csg::get_transform(csgpart));

    if (its)
        ret = mesh_to_grid(*its, params);

    return ret;
}

namespace detail {

// [INTENT] Apply one CSG boolean on two VoxelGridPtr operands in-place.
// [HAZARD H980] No null-guard on dst; crashes if dst is null.
// [HAZARD H981] Union with empty src: behaviour implementation-defined in OpenVDB.
inline void perform_csg(CSGType op, VoxelGridPtr& dst, VoxelGridPtr& src)
{
    if (!dst || !src) // [HAZARD H980] only guards src, not dst alone
        return;

    switch (op) {
    case CSGType::Union:
        if (is_grid_empty(*dst) && !is_grid_empty(*src))
            dst = clone(*src); // replace empty dst with clone of src
        else
            grid_union(*dst, *src); // [HAZARD H981] empty-src edge case
        break;
    case CSGType::Difference: grid_difference(*dst, *src); break;
    case CSGType::Intersection: grid_intersection(*dst, *src); break;
    }
}

} // namespace detail

// [INTENT] Main entry: voxelize all CSGParts and reduce in CSG-tree order.
// [CONCURRENCY] Phase 1 (voxelization): TBB parallel over all parts.
//               Phase 2 (reduce): serial, must preserve CSG ordering.
// [HAZARD H978] params captured by value per lambda — safe.
// [HAZARD H979] Early-exit on cancel leaves partial state in opstack.
template<class It> VoxelGridPtr voxelize_csgmesh(const Range<It>& csgrange, const VoxelizeParams& params = {})
{
    using namespace detail;

    VoxelGridPtr ret;

    std::vector<VoxelGridPtr> grids(csgrange.size());

    // [CONCURRENCY] Parallel voxelization: each thread writes to grids[csgidx].
    execution::for_each(
        ex_tbb, size_t(0), csgrange.size(),
        [&](size_t csgidx) {
            if (params.statusfn() && params.statusfn()(-1)) // [HAZARD H979] cancel probe
                return;

            auto it = csgrange.begin();
            std::advance(it, csgidx); // O(N) for non-RA iterators — O(N²) total
            auto& csgpart = *it;
            grids[csgidx] = get_voxelgrid(csgpart, params); // params by value — [HAZARD H978]
        },
        execution::max_concurrency(ex_tbb));

    size_t csgidx = 0;
    struct Frame
    {
        CSGType      op = CSGType::Union;
        VoxelGridPtr grid;
    };
    std::stack opstack{std::vector<Frame>{}};

    // [STATE] Base frame: empty voxel grid as the identity for Union.
    opstack.push({CSGType::Union, mesh_to_grid({}, params)});

    for (auto& csgpart : csgrange) {
        if (params.statusfn() && params.statusfn()(-1)) // [HAZARD H979] partial result on cancel
            break;

        auto& partgrid = grids[csgidx++]; // no bounds check — safe as long as range unchanged

        auto op = get_operation(csgpart);

        if (get_stack_operation(csgpart) == CSGStackOp::Push) {
            opstack.push({op, mesh_to_grid({}, params)});
            op = CSGType::Union;
        }

        Frame* top = &opstack.top();

        perform_csg(get_operation(csgpart), top->grid, partgrid);

        if (get_stack_operation(csgpart) == CSGStackOp::Pop) {
            VoxelGridPtr popgrid = std::move(top->grid);
            auto         popop   = opstack.top().op;
            opstack.pop();
            VoxelGridPtr& grid = opstack.top().grid;
            perform_csg(popop, grid, popgrid);
        }
    }

    ret = std::move(opstack.top().grid);

    return ret;
}

}} // namespace Slic3r::csg

#endif // VOXELIZECSGMESH_HPP
