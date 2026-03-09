// [INTENT] Converts a ModelObject (with its ModelVolume collection) into a
//          flat sequence of CSGPart objects suitable for boolean operations or
//          slicing.  Controls which volume types to include (positive/negative/
//          drill-holes) and whether to split splittable meshes into per-shell
//          sub-parts wrapped in Push/Pop stack sentinels.
//
// [STATE] Stateless utility — all state lives in the ModelObject and the
//         output iterator.  Returns a bool indicating whether any splittable
//         volumes were encountered.
//
// [COUPLING] Heavy: depends on ModelObject, ModelVolume, TriangleMesh,
//            MeshSplitImpl, SLA/Hollowing (for DrainHole — currently dead code),
//            and CSGMesh.hpp.  Any change to ModelVolume type classification
//            (is_model_part / is_negative_volume) must be mirrored here.
//
// [MEMORY] For non-split volumes: CSGPart stores a raw pointer to
//          vol->mesh().its — lifetime is bounded by the ModelObject.
//          For split volumes: each shell allocates a new ITS via unique_ptr
//          inside the lambda, ownership transferred into CSGPart.
//
// [HAZARD H964] The drill-hole export block (lines 74–85) is entirely
//               commented out.  The `mpartsDrillHoles` flag in ModelParts
//               and the `do_drillholes` variable are dead code — no drain
//               holes are ever exported to the CSG stream.  Any caller that
//               sets `mpartsDrillHoles` in parts_to_include receives silently
//               incomplete geometry (no drill holes subtracted).
//               Severity: P2/Medium.
//
// [HAZARD H965] Non-split path (line 64): `CSGPart part{&(vol->mesh().its), ...}`
//               stores a raw const pointer to the ITS inside ModelVolume.  If
//               the ModelObject is modified (volume mesh replaced, mesh loaded
//               lazily) after the CSG range is built but before it is consumed,
//               every non-split CSGPart in the range has a dangling pointer.
//               No lifetime token enforces the constraint.
//               Severity: P1/High.
//
// [HAZARD H966] The Split path emits a Push sentinel with the *volume's*
//               operation (Union or Difference) followed by sub-shells all
//               tagged CSGType::Union.  If the volume is negative
//               (is_negative_volume()), the Push is tagged Difference but
//               each inner shell is Union — meaning the sub-shells are unioned
//               first, then the group is subtracted from the parent.  This is
//               the intended semantics, but it means a split negative volume
//               is NOT equivalent to the unsplit (single-mesh) negative volume
//               if the shells overlap: overlapping shells in the Union group
//               will cancel each other before the Difference is applied, rather
//               than being directly subtracted.  Severity: P2/Medium.

#ifndef MODELTOCSGMESH_HPP
#define MODELTOCSGMESH_HPP

#include "CSGMesh.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/SLA/Hollowing.hpp"
#include "libslic3r/MeshSplitImpl.hpp"

namespace Slic3r { namespace csg {

// [INTENT] Flags to select which parts to export from Model into a csg part collection.
// These flags can be chained with the | operator
enum ModelParts {
    mpartsPositive   = 1, // Include positive parts
    mpartsNegative   = 2, // Include negative parts
    mpartsDrillHoles = 4, // Include drill holes — [HAZARD H964] dead code, block is commented out
    mpartsDoSplits   = 8, // Split each splitable mesh and export as a union of csg parts
};

// [INTENT] Converts all qualifying ModelVolumes of a ModelObject into CSGPart
//          elements written to the output iterator `out`.
// [COUPLING] trafo is applied to every volume transform: (trafo * vol->get_matrix()).
// [STATE] Returns true if any splittable volume was encountered.
template<class OutIt>
bool model_to_csgmesh(const ModelObject& mo,
                      const Transform3d& trafo, // Applies to all exported parts
                      OutIt              out,   // Output iterator
                      // values of ModelParts OR-ed
                      int parts_to_include = mpartsPositive)
{
    bool do_positives         = parts_to_include & mpartsPositive;
    bool do_negatives         = parts_to_include & mpartsNegative;
    bool do_drillholes        = parts_to_include & mpartsDrillHoles; // [HAZARD H964] never used below
    bool do_splits            = parts_to_include & mpartsDoSplits;
    bool has_splitable_volume = false;

    for (const ModelVolume* vol : mo.volumes) {
        if (vol && vol->mesh_ptr() && ((do_positives && vol->is_model_part()) || (do_negatives && vol->is_negative_volume()))) {
            if (do_splits && its_is_splittable(vol->mesh().its)) {
                // [STATE] Emit Push sentinel with group operation, then all shells
                //         as Union sub-parts, then Pop sentinel.
                // [HAZARD H966] Split negative volume: shells unioned before subtract —
                //               may differ from direct subtraction if shells overlap.
                CSGPart part_begin{{}, vol->is_model_part() ? CSGType::Union : CSGType::Difference};
                part_begin.stack_operation = CSGStackOp::Push;
                *out                       = std::move(part_begin);
                ++out;

                // [MEMORY] Each shell ITS allocated on heap; ownership in CSGPart unique_ptr.
                its_split(vol->mesh().its, SplitOutputFn{[&out, &vol, &trafo](indexed_triangle_set&& its) {
                              if (its.empty())
                                  return;

                              CSGPart part{std::make_unique<indexed_triangle_set>(std::move(its)), CSGType::Union,
                                           (trafo * vol->get_matrix()).cast<float>()};

                              *out = std::move(part);
                              ++out;
                          }});

                CSGPart part_end{{}};
                part_end.stack_operation = CSGStackOp::Pop;
                *out                     = std::move(part_end);
                ++out;
                has_splitable_volume = true;
            } else {
                // [MEMORY] Raw pointer into ModelVolume — [HAZARD H965] dangling if vol mutated.
                CSGPart part{&(vol->mesh().its), vol->is_model_part() ? CSGType::Union : CSGType::Difference,
                             (trafo * vol->get_matrix()).cast<float>()};
                part.name = vol->name;
                *out      = std::move(part);
                ++out;
            }
        }
    }

    // [HAZARD H964] Drill-hole export block permanently commented out.
    // if (do_drillholes) {
    //    sla::DrainHoles drainholes = sla::transformed_drainhole_points(mo, trafo);
    //
    //    for (const sla::DrainHole &dhole : drainholes) {
    //        CSGPart part{std::make_unique<const indexed_triangle_set>(
    //                         dhole.to_mesh()),
    //                     CSGType::Difference};
    //
    //        *out = std::move(part);
    //        ++out;
    //    }
    //}

    return has_splitable_volume;
}

}} // namespace Slic3r::csg

#endif // MODELTOCSGMESH_HPP
