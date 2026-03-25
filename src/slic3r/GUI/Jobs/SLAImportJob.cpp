// [INTENT]
// This file implements the `SLAImportJob` class, a background job for importing
// models and print profiles from SLA archives (`.sl1`, `.sl1s`, `.zip`). This
// job is initiated from the `SLAImportDialog`.
//
// The import process is handled in a worker thread to avoid blocking the UI,
// especially when processing large models. The workflow is as follows:
// 1. The `SLAImportDialog` gathers the file path and import options from the user.
// 2. An `SLAImportJob` is created, and its `prepare()` method is called on the
//    main thread to copy the necessary data from the dialog.
// 3. The `process()` method runs on a worker thread. It calls the
//    `import_sla_archive` function from `libslic3r/Format/SL1.hpp` to extract
//    the mesh and/or profile data from the archive. Progress is reported to the
//    UI through a callback.
// 4. The `finalize()` method runs on the main thread after the job is complete.
//    It handles any errors, loads the imported model into the Plater, applies
//    the new print profile, and displays notifications to the user.
//
// [UNITY]
// In a Unity port, this functionality would be managed by a C# script that uses
// an async Task to handle the file import.
// - The `SLAImportDialog` would be a UI panel.
// - The `process` logic would be an async method that calls a C# version of the
//   `import_sla_archive` function. This would likely involve using a C# library
//   for unzipping archives (e.g., `System.IO.Compression`) and parsing the
//   contained files.
// - The `finalize` logic would be the continuation of the async Task on the main
//   thread, where the imported `Mesh` is assigned to a `GameObject` and the print
//   profile data is loaded into a `ScriptableObject` or a settings class.
//
// [PORTING_HAZARD:P2]
// - This job system relies heavily on `wxWidgets` job-management classes (`Ctl`, `Worker`).
// - A direct replacement for the threading model will be needed (C# `Task` / `Task.Run` is recommended).
// - The `import_sla_archive` and all its sub-dependencies are C++ logic that will require a full C# port or C++-to-C# wrapper.

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

// [INTENT] The `priv` class is a pimpl (pointer to implementation) that holds
// the internal state of the `SLAImportJob`. This separates the job's data from
// its interface and helps to keep the header file clean.
// [STATE] This class holds all the data needed for the import job, including
// pointers to the Plater and the import dialog, the user's selections, the
// resulting mesh and profile, and any error messages.
class SLAImportJob::priv
{
public:
    Plater* plater;

    // [STATE] The user's selection from the dialog (e.g., import model, profile, or both).
    Sel sel = Sel::modelAndProfile;

    // [STATE] The imported triangle mesh.
    indexed_triangle_set mesh;
    // [STATE] The imported print profile.
    DynamicPrintConfig profile;
    // [STATE] The path to the SLA archive file.
    wxString path;
    // [STATE] The window size for the marching squares algorithm, determined by the quality setting.
    Vec2i32 win = {2, 2};
    // [STATE] Any error message that occurred during the import.
    std::string err;
    // [STATE] Any configuration substitutions that were made during the import.
    ConfigSubstitutions config_substitutions;

    // [STATE] A pointer to the import dialog view to get the user's selections.
    const SLAImportJobView* import_dlg;

    priv(Plater* plt, const SLAImportJobView* view) : plater{plt}, import_dlg{view} {}
};

SLAImportJob::SLAImportJob(const SLAImportJobView* view) : p{std::make_unique<priv>(wxGetApp().plater(), view)} { prepare(); }

SLAImportJob::~SLAImportJob() = default;

// [INTENT] The main worker method for the job. It calls the `import_sla_archive`
// function to extract data from the SLA file.
// [THREAD] This method is executed on a worker thread.
void SLAImportJob::process(Ctl& ctl)
{
    auto statustxt = _u8L("Importing SLA archive");
    ctl.update_status(0, statustxt);

    // [EVENT] A callback passed to the import function to report progress and
    // check for cancellation.
    auto progr = [&ctl, &statustxt](int s) {
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

// [INTENT] Resets the job's state to its default values, allowing the job object
// to be reused.
void SLAImportJob::reset()
{
    p->sel     = Sel::modelAndProfile;
    p->mesh    = {};
    p->profile = p->plater->sla_print().full_print_config();
    p->win     = {2, 2};
    p->path.Clear();
}

// [INTENT] Prepares the job by copying the necessary data from the import dialog.
// [THREAD] This method is called on the main UI thread before the job starts.
void SLAImportJob::prepare()
{
    reset();

    // [STATE] Copy the user's selections from the dialog to the job's private state.
    auto path = p->import_dlg->get_path();
    auto nm   = wxFileName(path);
    p->path   = !nm.Exists(wxFILE_EXISTS_REGULAR) ? "" : nm.GetFullPath();
    p->sel    = p->import_dlg->get_selection();
    p->win    = p->import_dlg->get_marchsq_windowsize();
    p->config_substitutions.clear();
}

// [INTENT] This method is called on the main UI thread after the job finishes.
// It applies the imported data to the application.
// [THREAD] This method is executed on the main UI thread.
void SLAImportJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    // Ignore the arrange result if aborted.
    if (canceled || eptr)
        return;

    // [EVENT] If an error occurred, show it to the user.
    if (!p->err.empty()) {
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

    if (p->sel != Sel::modelOnly) {
        if (p->profile.empty())
            p->profile = p->plater->sla_print().full_print_config();

        const ModelObjectPtrs& objects = p->plater->model().objects;
        for (auto object : objects)
            if (object->volumes.size() > 1) {
                Slic3r::GUI::show_info(nullptr,
                                       _(L("You cannot load SLA project with a multi-part object on the bed")) + "\n\n" +
                                           _(L("Please check your object list before preset changing.")),
                                       _(L("Attention!")));
                return;
            }

        DynamicPrintConfig config = {};
        config.apply(SLAFullPrintConfig::defaults());
        config += std::move(p->profile);

        // [EVENT] Load the imported profile into the application.
        wxGetApp().preset_bundle->load_config_model(name, std::move(config));
        wxGetApp().load_current_presets();
    }

    // [EVENT] If a model was imported, add it to the Plater.
    if (!p->mesh.empty()) {
        bool is_centered = false;
        p->plater->sidebar().obj_list()->load_mesh_object(TriangleMesh{std::move(p->mesh)}, name, is_centered);
    }

    // [EVENT] If any configuration substitutions were made, show them to the user.
    if (!p->config_substitutions.empty())
        show_substitutions_info(p->config_substitutions, p->path.ToUTF8().data());

    reset();
}

}} // namespace Slic3r::GUI
