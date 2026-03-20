#include "OrientJob.hpp"

#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] OrientJob drives the auto-orientation of selected model instances before printing, balancing selection, locked plates, and
// notification feedback. [PORTING_HAZARD:P2] orientation::orient mutates wx-based meshes and relies on synchronous callbacks; Unity will
// need a pre-processing job plus synchronized transform application to match these side effects.
// [STATE] m_selected/m_unselected/m_unprintable are reset so previous runs' orientation meshes don't leak into the next job invocation.
// [INTENT] Pre-reserve space based on current model counts to keep per-instance orientation passes performant.
void OrientJob::clear_input()
{
    const Model& model = m_plater->model();

    size_t count = 0, cunprint = 0; // To know how much space to reserve
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;

    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_selected.reserve(count);
    m_unselected.reserve(count);
    m_unprintable.reserve(cunprint);
}

// [INTENT] Gate orientation candidates by selection, per-plate locking, and the optional single-plate override so we only mutate what the
// user expects. [STATE] `obj_sel` snapshot plus `only_one_plate` determine whether we iterate all model instances or restrict the scope to
// the active plate. BBS: add only one plate mode and lock logic
void OrientJob::prepare_selection(std::vector<bool> obj_sel, bool only_one_plate)
{
    Model&         model      = m_plater->model();
    PartPlateList& plate_list = m_plater->get_partplate_list();
    // OrientMeshs selected_in_lock, unselect_in_lock;
    bool selected_is_locked = false;

    // [EVENT] We iterate every flagged object instance so the UI's selection drives which meshes get queued for orientation.

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < obj_sel.size(); ++oidx) {
        bool         selected = obj_sel[oidx];
        ModelObject* mo       = model.objects[oidx];

        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx) {
            ModelInstance* mi = mo->instances[inst_idx];
            OrientMesh&&   om = get_orient_mesh(mi);

            bool locked = false;
            if (!only_one_plate) {
                int plate_index = plate_list.find_instance(oidx, inst_idx);
                if ((plate_index >= 0) && (plate_index < plate_list.get_plate_count())) {
                    if (plate_list.is_locked(plate_index)) {
                        // [STATE] skip instances on locked plates but remember locked selections to warn before auto-orienting everything.
                        if (selected) {
                            // selected_in_lock.emplace_back(std::move(om));
                            selected_is_locked = true;
                        }
                        // else
                        //     unselect_in_lock.emplace_back(std::move(om));
                        continue;
                    }
                }
            }
            auto& cont = mo->printable ? (selected ? m_selected : m_unselected) : m_unprintable;
            // [STATE] printable vs unprintable state and selection determines the bucket so apply() only runs on valid parts.

            cont.emplace_back(std::move(om));
        }
    }

    // If the selection was empty orient everything
    if (m_selected.empty()) {
        if (!selected_is_locked) {
            // [STATE] fallback to orient every unselected printable mesh when the UI selection was empty.
            m_selected.swap(m_unselected);
            // m_unselected.insert(m_unselected.begin(), unselect_in_lock.begin(), unselect_in_lock.end());
        } else {
            // [EVENT] Emit a warning when locked plates suppress auto-orientation so the user receives feedback.
            m_plater->get_notification_manager()
                ->push_notification(NotificationType::BBLPlateInfo, NotificationManager::NotificationLevel::WarningNotificationLevel,
                                    into_u8(_L("All the selected objects are on a locked plate.\nCannot auto-orient these objects.")));
        }
    }
}

// [EVENT] triggered when the user chooses full-selection mode; mirrors the UI selection before handing off to worker logic.
void OrientJob::prepare_selected()
{
    clear_input();

    Model& model = m_plater->model();

    std::vector<bool> obj_sel(model.objects.size(), false);

    for (auto& s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = !s.second.empty();

    // BBS: add only one plate mode
    prepare_selection(obj_sel, false);
}

// [STATE] When only-one-plate mode is active we only operate on the current plate's instances, checking for locks before scheduling them.
// [EVENT] This path runs when the user chooses "Only current plate" from the Prepare menu.
// BBS: prepare current part plate for orienting
void OrientJob::prepare_partplate()
{
    clear_input();

    PartPlateList& plate_list = m_plater->get_partplate_list();
    PartPlate*     plate      = plate_list.get_curr_plate();
    assert(plate != nullptr);

    if (plate->empty()) {
        // no instances on this plate
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": no instances in current plate!");

        return;
    }

    // [STATE] enforce lock status by warning and skipping orientation when the current plate is read-only.
    if (plate->is_locked()) {
        m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                                                                NotificationManager::NotificationLevel::WarningNotificationLevel,
                                                                into_u8(_L("This plate is locked.\nCannot auto-orient on this plate.")));
        return;
    }

    Model& model = m_plater->model();

    std::vector<bool> obj_sel(model.objects.size(), false);

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx) {
            obj_sel[oidx] = plate->contain_instance(oidx, inst_idx);
        }
    }

    prepare_selection(obj_sel, true);
}

// BBS: add partplate logic
// [EVENT] prepare() runs once per job invocation to select the right subset before the worker runs inside process().
void OrientJob::prepare()
{
    int state = m_plater->get_prepare_state();
    m_plater->get_notification_manager()->bbl_close_plateinfo_notification();
    if (state == Job::JobPrepareState::PREPARE_STATE_DEFAULT) {
        // only_on_partplate = false;
        prepare_selected();
    } else if (state == Job::JobPrepareState::PREPARE_STATE_MENU) {
        // only_on_partplate = true;   // only arrange items on current plate
        prepare_partplate();
    }
}

void OrientJob::process(Ctl& ctl)
{
    // [INTENT] process() runs on the job thread, orchestrating preparation, orientation calculation, and UI progress updates.
    static const auto arrangestr = _u8L("Orienting...");

    ctl.update_status(0, arrangestr);
    // [THREAD] prepare() needs to run on the main UI thread to safely read selection/plate state before the background orientation work.
    ctl.call_on_main_thread([this] { prepare(); }).wait();
    ;

    auto start = std::chrono::steady_clock::now();

    // [OPENGL] Grab the per-canvas orient settings (min-volume vs min-area) because GPU/viewport preferences influence the orientation heuristic.
    const GLCanvas3D::OrientSettings& settings = m_plater->canvas3D()->get_orient_settings();

    // [STATE] `orientation::OrientParams` captures the active heuristics (min area vs volume) plus cancellation hooks for the worker.
    orientation::OrientParams     params;
    orientation::OrientParamsArea params_area;
    if (settings.min_area) {
        memcpy(&params, &params_area, sizeof(params));
        params.min_volume = false;
    } else {
        params.min_volume = true;
    }

    auto count = unsigned(m_selected.size() + m_unprintable.size());
    // [THREAD] allow the worker to exit early when the user cancels via the controller.
    params.stopcondition = [&ctl]() { return ctl.was_canceled(); };

    // [EVENT] report incremental progress to the UI thread for the status bar and cancels.
    params.progressind = [this, count, &ctl](unsigned st, std::string orientstr) {
        st += m_unprintable.size();
        if (st > 0)
            ctl.update_status(int(st / float(count) * 100), _u8L("Orienting") + " " + orientstr);
    };

    // [PORTING_HAZARD:P2] `orientation::orient` runs a CPU-heavy search that mutates `m_selected`; Unity should replace this with a
    // background Job System pass producing rotation deltas before applying them on the main thread. [UNITY] Model rotation can map to a
    // `MonoBehaviour` coroutine wrapping a `JobHandle` chain (Burst-ready orient pass + main-thread `Transform` updates) with progress fed
    // to a UI Toolkit status strip.
    orientation::orient(m_selected, m_unselected, params);

    auto time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);

    std::stringstream ss;
    if (!m_selected.empty())
        ss << std::fixed << std::setprecision(3) << "Orient " << m_selected.back().name << " in " << time_elapsed.count() << " seconds. "
           << "Orientation: " << m_selected.back().orientation.transpose() << "; v,phi: " << m_selected.back().axis.transpose() << ", "
           << m_selected.back().angle << "; euler: " << m_selected.back().euler_angles.transpose();

    // finalize just here.
    // [EVENT] finalize the progress UI and log the human-readable summary so callers know the run is done or canceled.
    ctl.update_status(100, ctl.was_canceled() ? _u8L("Orienting canceled.") : _u8L(ss.str().c_str()));
    // [UNITY] Unity replacements should mirror the `show_status_message` call with a UI Toolkit toast or status bar entry tied to the
    // orientation coroutine.
    wxGetApp().plater()->show_status_message(ctl.was_canceled() ? "Orienting canceled." : ss.str());
}

OrientJob::OrientJob() : m_plater{wxGetApp().plater()} {}

void OrientJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    // [THREAD] finalize executes after the worker completes; it needs to handle exceptions on the job thread before posting transforms back
    // to the UI.
    try {
        if (eptr)
            std::rethrow_exception(eptr);
        eptr = nullptr;
    } catch (...) {
        eptr = std::current_exception();
    }

    // Ignore the arrange result if aborted.
    if (canceled || eptr)
        return;

    // [UNITY] Orientations are applied immediately to the instances; in Unity this should map to updating each `GameObject`'s `Transform`
    // after the orientation job completes so the renderer sees the rotated mesh.
    for (OrientMesh& mesh : m_selected) {
        mesh.apply();
    }

    m_plater->update();

    // BBS
    // wxGetApp().obj_manipul()->set_dirty();
}

// [INTENT] Package the instance data and setter callback needed later to rotate it back on the UI thread.
// [UNITY] In a Unity port this corresponds to building a struct with Transform references and target rotation deltas.
orientation::OrientMesh OrientJob::get_orient_mesh(ModelInstance* instance)
{
    using OrientMesh = orientation::OrientMesh;
    OrientMesh om;
    auto       obj = instance->get_object();
    om.name        = obj->name;
    om.mesh        = obj->mesh(); // don't know the difference to obj->raw_mesh(). Both seem OK
    // [STATE] capture either the per-object support angle override or the global preset value so orientation respects support settings.
    if (obj->config.has("support_threshold_angle"))
        om.overhang_angle = obj->config.opt_int("support_threshold_angle");
    else {
        const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
        om.overhang_angle                        = config.opt_int("support_threshold_angle");
    }

    om.setter = [instance](const OrientMesh& p) {
        instance->rotate(p.rotation_matrix);
        instance->get_object()->invalidate_bounding_box();
        instance->get_object()->ensure_on_bed();
    };
    return om;
}

}} // namespace Slic3r::GUI
