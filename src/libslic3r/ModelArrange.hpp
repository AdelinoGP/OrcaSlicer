#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include <libslic3r/Arrange.hpp>
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {
// [INTENT] ModelArrange bridges generic 2D arrangement (arrangement::arrange)
// with mutable Model / ModelInstance state used by the print pipeline.
// [COUPLING] Hard-coupled to Model, DynamicPrintConfig, and arrangement::*
// types; this header is the integration seam between geometry packing and model state.
using ModelInstancePtrs = std::vector<ModelInstance*>;

using arrangement::ArrangePolygon;
using arrangement::ArrangePolygons;
using arrangement::ArrangeParams;
using arrangement::InfiniteBed;
using arrangement::CircleBed;

// [INTENT] Callback for virtual-bed policies (multi-plate workflows) so callers
// can reject, remap, or post-process out-of-bed placements.
using VirtualBedFn = std::function<void(arrangement::ArrangePolygon&)>;

[[noreturn]] inline void throw_if_out_of_bed(arrangement::ArrangePolygon& ap)
{
    throw Slic3r::RuntimeError("Objects could not fit on the bed; bed_idx==" + std::to_string(ap.bed_idx));
}

ArrangePolygons get_arrange_polys(const Model& model, ModelInstancePtrs& instances);
ArrangePolygon  get_arrange_poly(const Model& model);
bool            apply_arrange_polys(ArrangePolygons& polys, ModelInstancePtrs& instances, VirtualBedFn);

void duplicate(Model& model, ArrangePolygons& copies, VirtualBedFn);
void duplicate_objects(Model& model, size_t copies_num);

template<class TBed>
bool arrange_objects(Model& model, const TBed& bed, const ArrangeParams& params, VirtualBedFn vfn = throw_if_out_of_bed)
{
    // [STATE] Collects raw ModelInstance* pointers then mutates those instances via
    // apply_arrange_polys(); this is an in-place layout rewrite, not a pure transform.
    ModelInstancePtrs instances;
    auto&&            input = get_arrange_polys(model, instances);
    // [COUPLING] Delegates optimization/search to arrangement::arrange(), then translates
    // the result back into ModelInstance transforms.
    arrangement::arrange(input, bed, params);

    return apply_arrange_polys(input, instances, vfn);
}

template<class TBed>
void duplicate(Model& model, size_t copies_num, const TBed& bed, const ArrangeParams& params, VirtualBedFn vfn = throw_if_out_of_bed)
{
    // [INTENT] Build N identical arrange polygons from current model extents, then let
    // arrangement::arrange() solve placement before materializing copies in the Model.
    ArrangePolygons copies(copies_num, get_arrange_poly(model));
    arrangement::arrange(copies, bed, params);
    duplicate(model, copies, vfn);
}

template<class TBed>
void duplicate_objects(Model& model, size_t copies_num, const TBed& bed, const ArrangeParams& params, VirtualBedFn vfn = throw_if_out_of_bed)
{
    duplicate_objects(model, copies_num);
    arrange_objects(model, bed, params, vfn);
}

template<class T> struct PtrWrapper
{
    T* ptr;

    explicit PtrWrapper(T* p) : ptr{p} {}

    arrangement::ArrangePolygon get_arrange_polygon(const Slic3r::DynamicPrintConfig& config = Slic3r::DynamicPrintConfig()) const
    {
        // [COUPLING] Requires wrapped type T to expose get_arrange_polygon(); this is a
        // compile-time structural contract, not an explicit interface.
        arrangement::ArrangePolygon ap;
        ptr->get_arrange_polygon(&ap, config);
        return ap;
    }

    void apply_arrange_result(const Vec2d& t, double rot, int item_id)
    {
        // [STATE] Mutates wrapped object transform and ordering metadata.
        ptr->apply_arrange_result(t, rot);
        ptr->arrange_order = item_id;
    }
};

template<class T> arrangement::ArrangePolygon get_arrange_poly(T obj, const DynamicPrintConfig& config = DynamicPrintConfig());

template<> arrangement::ArrangePolygon get_arrange_poly(ModelInstance* inst, const DynamicPrintConfig& config);

ArrangePolygon get_instance_arrange_poly(ModelInstance* instance, const DynamicPrintConfig& config);
} // namespace Slic3r

#endif // MODELARRANGE_HPP
