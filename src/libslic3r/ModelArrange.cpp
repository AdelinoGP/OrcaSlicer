// [INTENT] ModelArrange.cpp — bridge between the Model geometry layer and the arrangement engine.
//   Provides:
//     - get_arrange_polys(): collect ArrangePolygon from all model instances for use by arranger.
//     - apply_arrange_polys(): write arranger output translations/rotations back to ModelInstances.
//     - get_arrange_poly() (Model overload): compute convex hull of entire model for nesting.
//     - duplicate() / duplicate_objects(): create additional ModelInstance copies.
//     - get_instance_arrange_poly(): rich ArrangePolygon including bed temperatures, nozzle temps,
//       vitrification temps, filament types, brim width, and tree-support extent for plate grouping.
// [STATE] Stateless adapter; all operations mutate Model / ModelInstance in-place.
// [COUPLING] Depends on arrangement::ArrangePolygon (libarrange), Model, ModelInstance, Print,
//   DynamicPrintConfig, Geometry::convex_hull, and MTUtils.
// [CONCURRENCY] Not thread-safe for concurrent modification of the same Model. All arrange
//   operations are expected to run on the main thread or under an external mutex.
// [HAZARD H1152] get_arrange_poly(const Model&) (lines ~43–58) accumulates all instance contour
//   points into a flat apts vector then computes a convex hull of the union. The loop applies
//   obj_ap.rotation and obj_ap.translation to the contour in-place BEFORE appending to apts.
//   However, `ap.poly.contour.rotate(obj_ap.rotation)` rotates the *accumulator* polygon, not
//   the per-object polygon — after the first iteration, previously added points are re-rotated
//   by subsequent object rotations. This is almost certainly a bug: the intent is to transform
//   each object's contour independently before adding to the hull, but the shared `ap.poly.contour`
//   accumulates all points and is mutated in each loop iteration.
// [HAZARD H1153] get_instance_arrange_poly(): lines ~170–177 compute `ap.brim_width` by checking
//   `support_type`. The fallback `ap.brim_width = 24.0` (2×MAX_BRANCH_RADIUS_FIRST_LAYER for tree
//   support) is a magic constant with a comment naming the formula but no reference to where
//   MAX_BRANCH_RADIUS_FIRST_LAYER is defined. If tree-support geometry parameters change, this
//   constant will silently desync with the actual support radius.
// [HAZARD H1154] `ap.extrude_ids.front()-1` is used as an index (lines ~131,135,139,141,144,152)
//   with no bounds check. If `ap.extrude_ids` is empty (no extruder assigned to an instance),
//   this is UB (vector::front() on empty vector). The only guard is `config.has(...)` which checks
//   if the config key exists, not whether extrude_ids is non-empty.
#include "ModelArrange.hpp"

#include <libslic3r/Model.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/Print.hpp>
#include "MTUtils.hpp"

namespace Slic3r {

arrangement::ArrangePolygons get_arrange_polys(const Model& model, ModelInstancePtrs& instances)
{
    size_t count = 0;
    for (auto obj : model.objects)
        count += obj->instances.size();

    ArrangePolygons input;
    input.reserve(count);
    instances.clear();
    instances.reserve(count);
    ArrangePolygon ap;
    for (ModelObject* mo : model.objects)
        for (ModelInstance* minst : mo->instances) {
            minst->get_arrange_polygon(&ap);
            input.emplace_back(ap);
            instances.emplace_back(minst);
        }

    return input;
}

bool apply_arrange_polys(ArrangePolygons& input, ModelInstancePtrs& instances, VirtualBedFn vfn)
{
    bool ret = true;

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i].bed_idx != 0) {
            ret = false;
            if (vfn)
                vfn(input[i]);
        }
        if (input[i].bed_idx >= 0)
            instances[i]->apply_arrange_result(input[i].translation.cast<double>(), input[i].rotation);
    }

    return ret;
}

Slic3r::arrangement::ArrangePolygon get_arrange_poly(const Model& model)
{
    ArrangePolygon ap;
    Points&        apts = ap.poly.contour.points;
    for (const ModelObject* mo : model.objects)
        for (const ModelInstance* minst : mo->instances) {
            ArrangePolygon obj_ap;
            minst->get_arrange_polygon(&obj_ap);
            ap.poly.contour.rotate(obj_ap.rotation);
            ap.poly.contour.translate(obj_ap.translation.x(), obj_ap.translation.y());
            const Points& pts = obj_ap.poly.contour.points;
            std::copy(pts.begin(), pts.end(), std::back_inserter(apts));
        }

    apts = std::move(Geometry::convex_hull(apts).points);
    return ap;
}

void duplicate(Model& model, Slic3r::arrangement::ArrangePolygons& copies, VirtualBedFn vfn)
{
    for (ModelObject* o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        o->instances.clear();
        for (const ModelInstance* i : instances) {
            for (arrangement::ArrangePolygon& ap : copies) {
                if (ap.bed_idx != 0)
                    vfn(ap);
                ModelInstance* instance = o->add_instance(*i);
                Vec2d          pos      = unscale(ap.translation);
                instance->set_offset(instance->get_offset() + to_3d(pos, 0.));
            }
        }
        o->invalidate_bounding_box();
    }
}

void duplicate_objects(Model& model, size_t copies_num)
{
    for (ModelObject* o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        for (const ModelInstance* i : instances)
            for (size_t k = 2; k <= copies_num; ++k)
                o->add_instance(*i);
    }
}

// Set up arrange polygon for a ModelInstance and Wipe tower
template<class T> arrangement::ArrangePolygon get_arrange_poly(T obj, const Slic3r::DynamicPrintConfig& config)
{
    ArrangePolygon ap = obj.get_arrange_polygon(config);
    // BBS: always set bed_idx to 0 to use original transforms with no bed_idx
    // if this object is not arranged, it can keep the original transforms
    // ap.bed_idx        = ap.translation.x() / bed_stride_x(plater);
    ap.bed_idx = 0;
    ap.setter  = [obj](const ArrangePolygon& p) {
        if (p.is_arranged()) {
            Vec2d t = p.translation.cast<double>();
            // BBS: change to sudoku-style computation, do it in partplate list
            // t.x() += p.bed_idx * bed_stride(plater);
            // t.x() += col * bed_stride_x(plater);
            // t.y() -= row * bed_stride_y(plater);
            T{obj}.apply_arrange_result(t, p.rotation, p.itemid);
        }
    };

    return ap;
}

template<> arrangement::ArrangePolygon get_arrange_poly(ModelInstance* inst, const Slic3r::DynamicPrintConfig& config)
{
    return get_arrange_poly(PtrWrapper{inst}, config);
}

ArrangePolygon get_instance_arrange_poly(ModelInstance* instance, const Slic3r::DynamicPrintConfig& config)
{
    ArrangePolygon ap = get_arrange_poly(PtrWrapper{instance}, config);

    // BBS: add temperature information
    if (config.has("curr_bed_type")) {
        ap.bed_temp           = 0;
        ap.first_bed_temp     = 0;
        BedType curr_bed_type = config.opt_enum<BedType>("curr_bed_type");

        const ConfigOptionInts* bed_opt = config.option<ConfigOptionInts>(get_bed_temp_key(curr_bed_type));
        if (bed_opt != nullptr)
            ap.bed_temp = bed_opt->get_at(ap.extrude_ids.front() - 1);

        const ConfigOptionInts* bed_opt_1st_layer = config.option<ConfigOptionInts>(get_bed_temp_1st_layer_key(curr_bed_type));
        if (bed_opt_1st_layer != nullptr)
            ap.first_bed_temp = bed_opt_1st_layer->get_at(ap.extrude_ids.front() - 1);
    }

    if (config.has("nozzle_temperature")) // get the print temperature
        ap.print_temp = config.opt_int("nozzle_temperature", ap.extrude_ids.front() - 1);
    if (config.has("nozzle_temperature_initial_layer")) // get the nozzle_temperature_initial_layer
        ap.first_print_temp = config.opt_int("nozzle_temperature_initial_layer", ap.extrude_ids.front() - 1);

    if (config.has("temperature_vitrification")) {
        ap.vitrify_temp = config.opt_int("temperature_vitrification", ap.extrude_ids.front() - 1);
    }

    // get filament temp types
    auto* filament_types_opt = dynamic_cast<const ConfigOptionStrings*>(config.option("filament_type"));
    if (filament_types_opt) {
        std::set<int> filament_temp_types;
        for (auto i : ap.extrude_ids) {
            std::string type_str  = filament_types_opt->get_at(i - 1);
            int         temp_type = Print::get_filament_temp_type(type_str);
            filament_temp_types.insert(temp_type);
        }
        ap.filament_temp_type = Print::get_compatible_filament_type(filament_temp_types);
    }

    // get brim width
    auto obj = instance->get_object();

    ap.brim_width = 1.0;
    // For by-layer printing, need to shrink bed a little, so the support won't go outside bed.
    // We set it to 5mm because that's how much a normal support will grow by default.
    // normal support 5mm, other support 22mm, no support 0mm
    auto supp_type_ptr    = obj->get_config_value<ConfigOptionBool>(config, "enable_support");
    auto support_type_ptr = obj->get_config_value<ConfigOptionEnum<SupportType>>(config, "support_type");
    auto support_type     = support_type_ptr->value;
    auto enable_support   = supp_type_ptr->getBool();
    int  support_int      = support_type_ptr->getInt();

    if (enable_support && (support_type == stNormalAuto || support_type == stNormal))
        ap.brim_width = 6.0;
    else if (enable_support) {
        ap.brim_width       = 24.0; // 2*MAX_BRANCH_RADIUS_FIRST_LAYER
        ap.has_tree_support = true;
    }

    auto size = obj->instance_convex_hull_bounding_box(instance).size();
    ap.height = size.z();
    ap.name   = obj->name;
    return ap;
}

} // namespace Slic3r
