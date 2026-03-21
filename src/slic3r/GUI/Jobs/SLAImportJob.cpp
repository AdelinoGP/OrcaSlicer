#include "SLAImportJob.hpp"

#include "libslic3r/Format/SL1.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/NotificationManager.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/filename.h>

namespace Slic3r { namespace GUI {

class SLAImportJob::priv
{
public:
    Plater* plater;

    Sel sel = Sel::modelAndProfile;

    indexed_triangle_set mesh;
    DynamicPrintConfig   profile;
    wxString             path;
    Vec2i32              win = {2, 2};
    std::string          err;
    ConfigSubstitutions  config_substitutions;

    const SLAImportJobView* import_dlg;

    priv(Plater* plt, const SLAImportJobView* view) : plater{plt}, import_dlg{view} {}
};

// [INTENT] Storage bridge between the import dialog and the background job. `mesh`, `profile`, and `path` are populated on the UI thread
// before the worker runs so the job only works on POD data and can be safely re-used via `reset()`.
// [STATE] `sel` and `config_substitutions` capture which portion of the SLA archive we care about and any preset overrides the dialog might
// have requested.

SLAImportJob::SLAImportJob(const SLAImportJobView* view) : p{std::make_unique<priv>(wxGetApp().plater(), view)} { prepare(); }

SLAImportJob::~SLAImportJob() = default;

// [INTENT] Worker entry point that loads SLA archives off the main thread while caching errors/status for finalize().
// [THREAD] Process runs on the job thread; use `Ctl` to shuttle progress back to the UI instead of direct wx calls.
// [UNITY] Map this to a MonoBehaviour `SlaImportController` that awaits `Task.Run(import_sla_archive)` and broadcasts progress through
// `UnityEvent<float>`. [PORTING_HAZARD:P2] `import_sla_archive` mutates libslic3r geometries/configs, so Unity must keep it on a worker
// task and forward results via a dispatcher.
void SLAImportJob::process(Ctl& ctl)
{
    auto statustxt = _u8L("Importing SLA archive");
    ctl.update_status(0, statustxt);

    auto progr = [&ctl, &statustxt](int s) {
        // [EVENT] Progress callback raises `Ctl` notifications and honors cancellations so the UI thread can observe the percent and stop if needed.
        if (s < 100)
            ctl.update_status(int(s), statustxt);
        return !ctl.was_canceled();
    };

    if (p->path.empty())
        return;

    std::string path = p->path.ToUTF8().data();
    try {
        switch (p->sel) {
        case Sel::modelAndProfile:
        case Sel::modelOnly: p->config_substitutions = import_sla_archive(path, p->win, p->mesh, p->profile, progr); break;
        case Sel::profileOnly: p->config_substitutions = import_sla_archive(path, p->profile); break;
        }
    } catch (MissingProfileError&) {
        p->err = _L("The SLA archive doesn't contain any presets. "
                    "Please activate some SLA printer preset first before importing that SLA archive.")
                     .ToStdString();
    } catch (std::exception& ex) {
        p->err = ex.what();
    }

    ctl.update_status(100, ctl.was_canceled() ? _u8L("Importing canceled.") : _u8L("Importing done."));
}

void SLAImportJob::reset()
{
    // [STATE] Clear cached selection/mesh data so the job can restart with fresh dialog inputs, reusing the base SLA profile as a fallback.
    p->sel     = Sel::modelAndProfile;
    p->mesh    = {};
    p->profile = p->plater->sla_print().full_print_config();
    p->win     = {2, 2};
    p->path.Clear();
}

void SLAImportJob::prepare()
{
    reset();

    // [EVENT] Copy dialog inputs on the main thread so the worker only sees sanitized STL paths, selection flags, and window sizes.
    auto path = p->import_dlg->get_path();
    auto nm   = wxFileName(path);
    p->path   = !nm.Exists(wxFILE_EXISTS_REGULAR) ? "" : nm.GetFullPath();
    p->sel    = p->import_dlg->get_selection();
    p->win    = p->import_dlg->get_marchsq_windowsize();
    p->config_substitutions.clear();
}

void SLAImportJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    // [THREAD] Runs back on the UI thread so it can safely interact with NotificationManager, presets, and the object list.
    // [EVENT] Finalize dispatches warnings, reloads presets, and pushes mesh data to the UI after the worker completes.
    // Ignore the arrange result if aborted.
    if (canceled || eptr)
        return;

    if (!p->err.empty()) {
        // [EVENT] Errors captured during the import are shown after the worker completes so the UI thread drives message boxes.
        show_error(p->plater, p->err);
        p->err = "";
        return;
    }

    std::string name = wxFileName(p->path).GetName().ToUTF8().data();

    if (p->profile.empty()) {
        p->plater->get_notification_manager()->push_notification(NotificationType::CustomNotification,
                                                                 NotificationManager::NotificationLevel::WarningNotificationLevel,
                                                                 _L("The imported SLA archive did not contain any presets. "
                                                                    "The current SLA presets were used as fallback.")
                                                                     .ToStdString());
    }

    // [PORTING_HAZARD:P3] Unity needs to swap in its own modal/topology for this warning instead of calling NotificationManager directly.

    if (p->sel != Sel::modelOnly) {
        if (p->profile.empty())
            p->profile = p->plater->sla_print().full_print_config();

        // [STATE] Guard against multi-volume objects so we do not corrupt SLA slicing expectations when applying new presets.
        const ModelObjectPtrs& objects = p->plater->model().objects;
        for (auto object : objects)
            if (object->volumes.size() > 1) {
                // [EVENT] Show blocking info dialog and return early so the UI thread can re-open the object list safely.
                Slic3r::GUI::show_info(nullptr,
                                       _(L("You cannot load SLA project with a multi-part object on the bed")) + "\n\n" +
                                           _(L("Please check your object list before preset changing.")),
                                       _(L("Attention!")));
                return;
            }

        DynamicPrintConfig config = {};
        config.apply(SLAFullPrintConfig::defaults());
        config += std::move(p->profile);

        // [EVENT] Load the imported configuration so menus, presets, and notifications see the new values immediately.
        wxGetApp().preset_bundle->load_config_model(name, std::move(config));
        wxGetApp().load_current_presets();
    }

    if (!p->mesh.empty()) {
        // [EVENT] Importing the mesh populates the sidebar list and triggers any GLCanvas refresh needed for the new data.
        bool is_centered = false;
        p->plater->sidebar().obj_list()->load_mesh_object(TriangleMesh{std::move(p->mesh)}, name, is_centered);
    }

    if (!p->config_substitutions.empty())
        // [EVENT] Display substitution details so the user knows which presets changed during the import.
        show_substitutions_info(p->config_substitutions, p->path.ToUTF8().data());

    reset();
}

}} // namespace Slic3r::GUI
