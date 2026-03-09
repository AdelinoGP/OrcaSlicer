// [INTENT] Slices a CSG-tree range into per-layer ExPolygon collections.
//          Each CSGPart contributes its mesh sliced at the given z-grid; the
//          resulting slices are combined with Union/Difference/Intersection
//          according to the CSG operation, respecting Push/Pop stack sentinels
//          for grouped sub-trees.
//
// [STATE] opstack (std::stack<Frame>) tracks the running per-layer slice
//         accumulator for each nesting level.  One Frame per Push sentinel.
//         The base frame is pushed before the loop and the result extracted
//         after.
//
// [CONCURRENCY] TBB used in two places:
//   (a) Per-layer merge_slices after each part (via execution::for_each
//       over nonempty_indices).
//   (b) Post-processing union_ex pass on the final result.
//   The loop itself is serial, so Push/Pop ordering is guaranteed correct.
//   [HAZARD H972] The merge_slices lambda (line 91) captures `top` as a raw
//                 pointer to opstack.top().  If the for_each lambda is
//                 executed in a TBB context where another thread pushes to
//                 opstack concurrently (not possible here because the outer
//                 loop is serial), the pointer would dangle.  This is safe
//                 as written but brittle: any future refactor that
//                 parallelises the outer loop (e.g. per-part TBB) will cause
//                 a dangling-pointer data race on `top`.
//                 Severity: P2/Medium.
//
// [HAZARD H973] nonempty_indices is shared across all CSGParts in the loop
//               body without clearing between parts — collect_nonempty_indices
//               clears it at the start (line 39).  The merge_slices for_each
//               captures `top` by pointer (not value).  If slicegrid has
//               zero entries and the CSG range is non-empty, no merging occurs
//               silently; the result is empty ExPolygons with no error.
//               Severity: P3/Low.
//
// [HAZARD H974] The final union_ex pass (line 123) applies union_ex to each
//               layer's ExPolygon collection.  The comment "TODO: verify if
//               this part can be omitted or not" indicates the semantics are
//               uncertain.  Omitting it could produce overlapping ExPolygons
//               that downstream clipper operations handle differently.
//               Including it is O(N log N) per layer but may alter the
//               geometry if the input has touching/overlapping regions from
//               the merge steps.  The ambiguity is unresolved.
//               Severity: P2/Medium.
//
// [HAZARD H975] merge_slices for CSGType::Union appends all source ExPolygons
//               directly to target without checking for duplicates or calling
//               union_ex.  Two Union parts at the same z-level produce
//               overlapping ExPolygons in the accumulator (they are only
//               unioned at the very end).  Difference and Intersection call
//               the Clipper functions immediately.  This asymmetry means Union
//               CSG scenes may hold large numbers of overlapping polygons in
//               memory mid-loop, while Difference/Intersection are resolved
//               per-part.  Memory usage scales with number of Union parts ×
//               polygon count, not just total polygon count.
//               Severity: P2/Medium (memory; not a correctness issue).

#ifndef SLICECSGMESH_HPP
#define SLICECSGMESH_HPP

#include "CSGMesh.hpp"

#include <stack>

#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Execution/ExecutionTBB.hpp"

namespace Slic3r { namespace csg {

namespace detail {

// [INTENT] Apply one CSG boolean to target[i] in-place, using source[i].
// [STATE] Modifies target[i]. Union: appends; Diff/Intersect: replaces.
// [HAZARD H975] Union appends without immediate resolution — overlapping polygons accumulate.
inline void merge_slices(csg::CSGType op, size_t i, std::vector<ExPolygons>& target, std::vector<ExPolygons>& source)
{
    switch (op) {
    case CSGType::Union:
        for (ExPolygon& expoly : source[i])
            target[i].emplace_back(std::move(expoly)); // [HAZARD H975] no immediate union_ex
        break;
    case CSGType::Difference: target[i] = diff_ex(target[i], source[i]); break;
    case CSGType::Intersection: target[i] = intersection_ex(target[i], source[i]); break;
    }
}

// [INTENT] Populate `indices` with the layer indices that need merging.
//          For Intersection, all layers must be processed (even empty ones,
//          to avoid keeping uninitialized slices).  For other ops, only
//          non-empty source layers matter.
// [STATE] Clears and rebuilds `indices` on each call.
inline void collect_nonempty_indices(csg::CSGType                   op,
                                     const std::vector<float>&      slicegrid,
                                     const std::vector<ExPolygons>& slices,
                                     std::vector<size_t>&           indices)
{
    indices.clear();
    for (size_t i = 0; i < slicegrid.size(); ++i) {
        if (op == CSGType::Intersection || !slices[i].empty())
            indices.emplace_back(i);
    }
}

} // namespace detail

// [INTENT] Main entry: slice all CSGParts and reduce with CSG operations per layer.
// [CONCURRENCY] Serial outer loop (CSG-tree order); parallel inner merge (TBB).
// [STATE] Returns vector<ExPolygons> of size slicegrid.size().
// [COUPLING] slice_mesh_ex, diff_ex, intersection_ex, union_ex, ExecutionTBB.
template<class ItCSG>
std::vector<ExPolygons> slice_csgmesh_ex(
    const Range<ItCSG>&          csgrange,
    const std::vector<float>&    slicegrid,
    const MeshSlicingParamsEx&   params,
    const std::function<void()>& throw_on_cancel = [] {})
{
    using namespace detail;

    struct Frame
    {
        CSGType                 op;
        std::vector<ExPolygons> slices;
    };

    std::stack opstack{std::vector<Frame>{}};

    MeshSlicingParamsEx params_cpy       = params;
    auto                trafo            = params.trafo;
    auto                nonempty_indices = reserve_vector<size_t>(slicegrid.size());

    opstack.push({CSGType::Union, std::vector<ExPolygons>(slicegrid.size())});

    for (const auto& csgpart : csgrange) {
        const indexed_triangle_set* its = csg::get_mesh(csgpart);

        auto op = get_operation(csgpart);

        if (get_stack_operation(csgpart) == CSGStackOp::Push) {
            opstack.push({op, std::vector<ExPolygons>(slicegrid.size())});
            op = CSGType::Union;
        }

        Frame* top = &opstack.top(); // [HAZARD H972] raw pointer — safe only in serial loop

        if (its) {
            params_cpy.trafo               = trafo * csg::get_transform(csgpart).template cast<double>();
            std::vector<ExPolygons> slices = slice_mesh_ex(*its, slicegrid, params_cpy, throw_on_cancel);

            assert(slices.size() == slicegrid.size());

            collect_nonempty_indices(op, slicegrid, slices, nonempty_indices);

            // [CONCURRENCY] TBB parallel merge; top captured as pointer.
            // [HAZARD H972] `top` must remain valid for duration of for_each.
            execution::for_each(
                ex_tbb, nonempty_indices.begin(), nonempty_indices.end(),
                [op, &slices, &top](size_t i) { merge_slices(op, i, top->slices, slices); }, execution::max_concurrency(ex_tbb));
        }

        if (get_stack_operation(csgpart) == CSGStackOp::Pop) {
            std::vector<ExPolygons> popslices = std::move(top->slices);
            auto                    popop     = opstack.top().op;
            opstack.pop();
            std::vector<ExPolygons>& prev_slices = opstack.top().slices;

            collect_nonempty_indices(popop, slicegrid, popslices, nonempty_indices);

            execution::for_each(
                ex_tbb, nonempty_indices.begin(), nonempty_indices.end(),
                [&popslices, &prev_slices, popop](size_t i) { merge_slices(popop, i, prev_slices, popslices); },
                execution::max_concurrency(ex_tbb));
        }
    }

    std::vector<ExPolygons> ret = std::move(opstack.top().slices);

    // [HAZARD H974] TODO comment in original: unclear if this pass is required.
    // Removes micro-polygons below SCALED_EPSILON^2 area and calls union_ex.
    // [HAZARD H975] This is the only place Union CSG parts get actually unioned.
    execution::for_each(
        ex_tbb, ret.begin(), ret.end(),
        [](ExPolygons& slice) {
            auto it = std::remove_if(slice.begin(), slice.end(),
                                     [](const ExPolygon& p) { return p.area() < double(SCALED_EPSILON) * double(SCALED_EPSILON); });

            // Hopefully, ExPolygons are moved, not copied to new positions
            // and that is cheap for expolygons
            slice.erase(it, slice.end());
            slice = union_ex(slice);
        },
        execution::max_concurrency(ex_tbb));

    return ret;
}

}} // namespace Slic3r::csg

#endif // SLICECSGMESH_HPP
