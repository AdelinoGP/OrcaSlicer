// [ANNOTATED]
// [INTENT]
// This file implements the `FillBedJob`, a background job that automatically
// duplicates a selected object to fill the available space on the print bed.
// It solves a bin-packing problem to efficiently arrange the objects.
//
// The process is divided into three main stages, typical for a `Job`-derived class:
// 1. `prepare()`: Executed on the main thread before the job starts. It gathers
//    all necessary information from the `Plater`, including the object to be
//    duplicated, the positions of other objects on the bed, the bed shape, and
//    any exclusion zones. It prepares `ArrangePolygon` objects which represent
//    the footprints of the objects for the arrangement algorithm. It also
//    estimates the number of copies that can fit on the bed.
// 2. `process()`: Executed on a worker thread. This method calls the core
//    arrangement algorithm (from `libslic3r/ModelArrange.hpp`) which uses a
//    heuristic approach (likely similar to libnest2d) to find optimal positions
//    for the new object instances. It uses callbacks to report progress and check
//    for cancellation.
// 3. `finalize()`: Executed on the main thread after the job completes. It takes
//    the arrangement results (new positions and transformations) and applies them
//    to the `Model`, creating new `ModelInstance` or `ModelObject` objects.
//    Finally, it updates the `Plater` to reflect the changes in the 3D view and
//    the object list.
//
// [UNITY]
// In Unity, this functionality would be implemented as a C# script that triggers
// an asynchronous operation (e.g., using the C# Job System or `async/await`).
// - The `prepare` stage would involve collecting `GameObject`s and their `Mesh`
//   footprints from the scene.
// - The `process` stage would run a C# implementation of the bin-packing
//   algorithm. This could be a port of the C++ code or a library like
//   `Unity.Collections.BinPacking`. The job would operate on native arrays of
//   positions and polygon data for performance.
// - The `finalize` stage would be a callback on the main thread that instantiates
//   new `GameObject` prefabs based on the results and sets their `Transform`
//   properties.

#include "FillBedJob.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "libnest2d/common.hpp"

#include <numeric>

namespace Slic3r { namespace GUI {

// BBS: add partplate related logic
// [INTENT] Gather all necessary data from the Plater before starting the
// arrangement. This includes the object to be duplicated, the positions of
// other objects, the bed shape, and exclusion zones. It prepares the data for
// the `process` method.
// [THREAD] This method is called on the main UI thread via `ctl.call_on_main_thread()`
// from the `process` method.
void FillBedJob::prepare()
{
    // [STATE] Get the list of part plates to handle multi-plate arrangements.
    PartPlateList& plate_list = m_plater->get_partplate_list();

    m_locked.clear();
    m_selected.clear();
    m_unselected.clear();
    m_bedpts.clear();

    // [STATE] Initialize arrangement parameters from the application config.
    params = init_arrange_params(m_plater);

    m_object_idx = m_plater->get_selected_object_idx();
    if (m_object_idx == -1)
        return;

    // select current plate at first
    int sel_id = m_plater->get_selection().get_instance_idx();
    sel_id     = std::max(sel_id, 0);

    int sel_ret = plate_list.select_plate_by_obj(m_object_idx, sel_id);
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__
                             << boost::format(":select plate obj_id %1%, ins_id %2%, ret %3%}") % m_object_idx % sel_id % sel_ret;

    PartPlate*  plate           = plate_list.get_curr_plate();
    Model&      model           = m_plater->model();
    BoundingBox plate_bb        = plate->get_bounding_box_crd();
    int         plate_cols      = plate_list.get_plate_cols();
    int         cur_plate_index = plate->get_index();

    ModelObject* model_object = m_plater->model().objects[m_object_idx];
    if (model_object->instances.empty())
        return;

    // [STATE] Get global configuration to access arrangement settings.
    const Slic3r::DynamicPrintConfig& global_config = wxGetApp().preset_bundle->full_config();
    m_selected.reserve(model_object->instances.size());
    // [INTENT] Iterate through all objects and instances in the model to classify
    // them into three groups for the arrangement algorithm:
    // - `m_selected`: The instances of the selected object to be duplicated. This
    //   serves as the template.
    // - `m_unselected`: Other objects that are on the bed and can be moved.
    // - `m_locked`: Objects that are outside the current plate and should not be moved.
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx) {
            bool selected = (oidx == m_object_idx);

            // [INTENT] `ArrangePolygon` is a wrapper around the object's 2D footprint
            // used by the arrangement algorithm.
            ArrangePolygon ap    = get_instance_arrange_poly(mo->instances[inst_idx], global_config);
            BoundingBox    ap_bb = ap.transformed_poly().contour.bounding_box();
            ap.name              = mo->name;

            if (selected) {
                if (mo->instances[inst_idx]->printable) {
                    ++ap.priority;
                    ap.itemid = m_selected.size();
                    m_selected.emplace_back(ap);
                } else {
                    if (plate_bb.contains(ap_bb)) {
                        ap.bed_idx = 0;
                        ap.itemid  = m_unselected.size();
                        ap.row     = cur_plate_index / plate_cols;
                        ap.col     = cur_plate_index % plate_cols;
                        ap.translation(X) -= bed_stride_x(m_plater) * ap.col;
                        ap.translation(Y) += bed_stride_y(m_plater) * ap.row;
                        m_unselected.emplace_back(ap);
                    } else {
                        ap.bed_idx = PartPlateList::MAX_PLATES_COUNT;
                        ap.itemid  = m_locked.size();
                        m_locked.emplace_back(ap);
                    }
                }
            } else {
                if (plate_bb.contains(ap_bb)) {
                    ap.bed_idx = 0;
                    ap.itemid  = m_unselected.size();
                    ap.row     = cur_plate_index / plate_cols;
                    ap.col     = cur_plate_index % plate_cols;
                    ap.translation(X) -= bed_stride_x(m_plater) * ap.col;
                    ap.translation(Y) += bed_stride_y(m_plater) * ap.row;
                    m_unselected.emplace_back(ap);
                } else {
                    ap.bed_idx = PartPlateList::MAX_PLATES_COUNT;
                    ap.itemid  = m_locked.size();
                    m_locked.emplace_back(ap);
                }
            }
        }
    }
    /*
    for (ModelInstance *inst : model_object->instances)
        if (inst->printable) {
            ArrangePolygon ap = get_arrange_poly(inst);
            // Existing objects need to be included in the result. Only
            // the needed amount of object will be added, no more.
            ++ap.priority;
            m_selected.emplace_back(ap);
        }*/

    if (m_selected.empty())
        return;

    bool enable_wrapping = global_config.option<ConfigOptionBool>("enable_wrapping_detection")->value;
    // add the virtual object into unselect list if has
    double scaled_exclusion_gap = scale_(1);
    // [STATE] Prepare exclusion areas on the build plate where objects cannot be placed.
    plate_list.preprocess_exclude_areas(params.excluded_regions, enable_wrapping, 1, scaled_exclusion_gap);
    plate_list.preprocess_exclude_areas(m_unselected, enable_wrapping);

    m_bedpts = get_bed_shape(*m_plater->config());

    auto& objects = m_plater->model().objects;
    /*BoundingBox bedbb = get_extents(m_bedpts);

    for (size_t idx = 0; idx < objects.size(); ++idx)
        if (int(idx) != m_object_idx)
            for (ModelInstance *mi : objects[idx]->instances) {
                ArrangePolygon ap = get_arrange_poly(mi);
                auto ap_bb = ap.transformed_poly().contour.bounding_box();

                if (ap.bed_idx == 0 && !bedbb.contains(ap_bb))
                    ap.bed_idx = arrangement::UNARRANGED;

                m_unselected.emplace_back(ap);
            }*/
    if (auto wt = get_wipe_tower_arrangepoly(*m_plater))
        m_unselected.emplace_back(std::move(*wt));

    double sc = scaled<double>(1.) * scaled(1.);

    // [INTENT] Estimate the number of items that can fit on the bed by comparing
    // the available area with the area of the selected object's footprint.
    auto      polys      = offset_ex(m_selected.front().poly, params.min_obj_distance / 2);
    ExPolygon poly       = polys.empty() ? m_selected.front().poly : polys.front();
    double    poly_area  = poly.area() / sc;
    double    unsel_area = std::accumulate(m_unselected.begin(), m_unselected.end(), 0.,
                                           [cur_plate_index](double s, const auto& ap) {
                                            // BBS: m_unselected instance is in the same partplate
                                            return s + (ap.bed_idx == cur_plate_index) * ap.poly.area();
                                            // return s + (ap.bed_idx == 0) * ap.poly.area();
                                        }) /
                        sc;

    double fixed_area = unsel_area + m_selected.size() * poly_area;
    double bed_area   = Polygon{m_bedpts}.area() / sc;

    // This is the maximum number of items, the real number will always be close but less.
    int needed_items = (bed_area - fixed_area) / poly_area;

    // int sel_id = m_plater->get_selection().get_instance_idx();
    //  if the selection is not a single instance, choose the first as template
    // sel_id = std::max(sel_id, 0);
    ModelInstance* mi          = model_object->instances[sel_id];
    ArrangePolygon template_ap = get_instance_arrange_poly(mi, global_config);

    int    obj_idx;
    double offset_base, offset;
    bool   was_one_instance;
    if (m_instances) {
        obj_idx          = m_plater->get_selected_object_idx();
        offset_base      = m_plater->canvas3D()->get_size_proportional_to_max_bed_size(0.05);
        offset           = offset_base;
        was_one_instance = model_object->instances.size() == 1;
    }

    // [INTENT] Create placeholder `ArrangePolygon` objects for the new instances
    // to be created. These will be filled with position data by the arrangement algorithm.
    // The `setter` lambda is a callback that will be used in the `finalize` step
    // to actually create the new `ModelInstance` or `ModelObject`.
    for (int i = 0; i < needed_items; ++i, offset += offset_base) {
        ArrangePolygon ap = template_ap;
        ap.poly           = m_selected.front().poly;
        ap.bed_idx        = PartPlateList::MAX_PLATES_COUNT;
        ap.itemid         = -1;
        ap.setter         = [this, mi, offset](const ArrangePolygon& p) {
            ModelObject* mo = m_plater->model().objects[m_object_idx];
            ModelObject* obj;
            if (m_instances) {
                ModelInstance* model_instance = mo->instances.back();
                Vec3d          offset_vec     = model_instance->get_offset() + Vec3d(offset, offset, 0.0);
                mo->add_instance(offset_vec, model_instance->get_scaling_factor(), model_instance->get_rotation(),
                                         model_instance->get_mirror());
                obj = mo;
            } else {
                ModelObject* newObj = m_plater->model().add_object(*mo);
                newObj->name        = mo->name + " " + std::to_string(p.itemid);
                obj                 = newObj;
            }
            for (ModelInstance* newInst : obj->instances) {
                newInst->apply_arrange_result(p.translation.cast<double>(), p.rotation);
            }
            // m_plater->sidebar().obj_list()->paste_objects_into_list({m_plater->model().objects.size()-1});
        };
        m_selected.emplace_back(ap);
    }

    m_status_range = m_selected.size();

    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    // BBS: remove logic for unselected object
    /*double stride = bed_stride(m_plater);
    for (auto &p : m_unselected)
        if (p.bed_idx > 0)
            p.translation(X) -= p.bed_idx * stride;*/
}

// [INTENT] This is the main worker method for the job. It invokes the
// arrangement algorithm to find the optimal placement for the objects.
// [THREAD] This method is executed on a worker thread. It must not directly
// access any UI elements. It uses `ctl.call_on_main_thread()` to run the
// `prepare()` method on the main thread before starting the arrangement.
void FillBedJob::process(Ctl& ctl)
{
    auto statustxt = _u8L("Filling");
    // [THREAD] Run the prepare method on the main thread and wait for it to finish.
    ctl.call_on_main_thread([this] { prepare(); }).wait();
    ctl.update_status(0, statustxt);

    if (m_object_idx == -1 || m_selected.empty())
        return;

    // [STATE] Update arrangement parameters based on the current configuration.
    update_arrange_params(params, m_plater->config(), m_selected);
    m_bedpts = get_shrink_bedpts(m_plater->config(), params);

    auto&                             partplate_list = m_plater->get_partplate_list();
    auto&                             print          = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    const Slic3r::DynamicPrintConfig& global_config  = wxGetApp().preset_bundle->full_config();
    PresetBundle*                     preset_bundle  = wxGetApp().preset_bundle;
    const bool                        is_bbl         = wxGetApp().preset_bundle->is_bbl_vendor();
    if (is_bbl && params.avoid_extrusion_cali_region && global_config.opt_bool("scan_first_layer"))
        partplate_list.preprocess_nonprefered_areas(m_unselected, MAX_NUM_PLATES);

    update_selected_items_inflation(m_selected, m_plater->config(), params);
    update_unselected_items_inflation(m_unselected, m_plater->config(), params);

    // [EVENT] Set up a stop condition callback to allow the job to be canceled
    // from the UI.
    bool do_stop         = false;
    params.stopcondition = [&ctl, &do_stop]() { return ctl.was_canceled() || do_stop; };

    // [EVENT] Set up a progress indicator callback to update the status bar in the UI.
    params.progressind = [this, &ctl, &statustxt](unsigned st, std::string str = "") {
        if (st > 0)
            ctl.update_status(st * 100 / status_range(), statustxt + " " + str);
    };

    params.on_packed = [&do_stop](const ArrangePolygon& ap) { do_stop = ap.bed_idx > 0 && ap.priority == 0; };
    // final align用的是凸包，在有fixed item的情况下可能找到的参考点位置是错的，这里就不做了。见STUDIO-3265
    params.do_final_align = !is_bbl;

    if (m_selected.size() > 100) {
        // too many items, just find grid empty cells to put them
        Vec2f step = unscaled<float>(get_extents(m_selected.front().poly).size()) +
                     Vec2f(m_selected.front().brim_width, m_selected.front().brim_width);
        std::vector<Vec2f> empty_cells = Plater::get_empty_cells(step);
        size_t             n           = std::min(m_selected.size(), empty_cells.size());
        for (size_t i = 0; i < n; i++) {
            m_selected[i].translation = scaled<coord_t>(empty_cells[i]);
            m_selected[i].bed_idx     = 0;
        }
        for (size_t i = n; i < m_selected.size(); i++) {
            m_selected[i].bed_idx = -1;
        }
    } else
        // [INTENT] Call the core arrangement algorithm.
        arrangement::arrange(m_selected, m_unselected, m_bedpts, params);

    // finalize just here.
    ctl.update_status(100, ctl.was_canceled() ? _u8L("Bed filling canceled.") : _u8L("Bed filling done."));
}

// [INTENT] The constructor for the FillBedJob.
// [STATE] The `instances` parameter determines whether to create new instances
// of the selected object or new objects.
FillBedJob::FillBedJob(bool instances) : m_plater{wxGetApp().plater()}, m_instances{instances} {}

// [INTENT] This method is called after the `process` method has finished. It
// applies the results of the arrangement to the `Model`.
// [THREAD] This method is executed on the main UI thread, so it is safe to
// interact with the `Plater`, `Model`, and other UI elements.
void FillBedJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    // Ignore the arrange result if aborted.
    if (canceled || eptr)
        return;

    if (m_object_idx == -1)
        return;

    ModelObject* model_object = m_plater->model().objects[m_object_idx];
    if (model_object->instances.empty())
        return;

    // BBS: partplate
    PartPlateList& plate_list = m_plater->get_partplate_list();
    int            plate_cols = plate_list.get_plate_cols();
    int            cur_plate  = plate_list.get_curr_plate_index();

    size_t inst_cnt = model_object->instances.size();

    int added_cnt = std::accumulate(m_selected.begin(), m_selected.end(), 0,
                                    [](int s, auto& ap) { return s + int(ap.priority == 0 && ap.bed_idx == 0); });

    int oldSize = m_plater->model().objects.size();

    if (added_cnt > 0) {
        // BBS: adjust the selected instances
        // [INTENT] Iterate through the arranged polygons and apply the new
        // positions and transformations. This is where the `setter` lambda
        // from the `prepare` method is called.
        for (ArrangePolygon& ap : m_selected) {
            if (ap.bed_idx != 0) {
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__
                                         << boost::format(":skipped: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx %
                                                unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
                /*if (ap.itemid == -1)*/
                continue;
                ap.bed_idx = plate_list.get_plate_count();
            } else
                ap.bed_idx = cur_plate;

            if (m_selected.size() <= 100) {
                ap.row = ap.bed_idx / plate_cols;
                ap.col = ap.bed_idx % plate_cols;
                ap.translation(X) += bed_stride_x(m_plater) * ap.col;
                ap.translation(Y) -= bed_stride_y(m_plater) * ap.row;
            }

            ap.apply();

            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__
                                     << boost::format(":selected: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx %
                                            unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y));
        }

        int  newSize  = m_plater->model().objects.size();
        auto obj_list = m_plater->sidebar().obj_list();
        // [EVENT] Update the UI to show the newly created objects.
        for (size_t i = oldSize; i < newSize; i++) {
            obj_list->add_object_to_list(i, true, true, false);
            obj_list->update_printable_state(i, 0);
        }

        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": paste_objects_into_list";

        /*for (ArrangePolygon& ap : m_selected) {
            if (ap.bed_idx != arrangement::UNARRANGED && (ap.priority != 0 || ap.bed_idx == 0))
                ap.apply();
        }*/

        // model_object->ensure_on_bed();
        // BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": model_object->ensure_on_bed()";

        if (m_instances) { // && wxGetApp().app_config->get("auto_arrange") == "true") {
            m_plater->set_prepare_state(Job::PREPARE_STATE_MENU);
            m_plater->arrange();
        }
        // [EVENT] Trigger a plater update to redraw the 3D scene.
        m_plater->update();
    }
}

}} // namespace Slic3r::GUI
