#include "NotificationProgressIndicator.hpp"
#include "slic3r/GUI/NotificationManager.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Adapt the generic job `ProgressIndicator` interface onto the GUI-owned `NotificationManager` so background work pushes updates
// into the toast-style HUD. [STATE] Keeps a raw pointer to the single `NotificationManager` instance rather than owning its own widget
// tree, so updates flow through the shared manager. [THREAD] Methods on this adapter may be invoked from worker jobs, so every caller must
// marshal through `NotificationManager` (main thread) before mutating UI state. [UNITY] Replace with `NotificationHUDController :
// MonoBehaviour` that holds a `Slider/Text` combo and consumes `MainThreadDispatcher.Schedule(() => UpdateProgress())`.
NotificationProgressIndicator::NotificationProgressIndicator(NotificationManager* nm) : m_nm{nm} {}

void NotificationProgressIndicator::clear_percent()
{
    // [UNCLEAR] The native layer never clears an in-progress notification, so Unity must explicitly hide the HUD when jobs finish instead
    // of relying on this stub. [PORTING_HAZARD:P3] Leaving the previous percentage visible risks confusing users during rapid job restarts,
    // so implement a hide call in the Unity HUD.
}

void NotificationProgressIndicator::show_error_info(wxString msg, int code, wxString description, wxString extra)
{
    // [INTENT] Surface rich error metadata when available so downstream UI can highlight error codes and descriptions.
    // [UNITY] Map to `NotificationHUDController.ShowErrorToast(string title, string details, string extraData)` so the Canvas can show the
    // code and strings. [PORTING_HAZARD:P3] The old implementation never forwards these strings, so Unity must explicitly pull them through
    // to avoid losing failure context.
}

void NotificationProgressIndicator::set_range(int range)
{
    // [STATE] Alters the shared `NotificationManager` slider bounds rather than keeping local state, guaranteeing a single source of truth
    // for the HUD range. [EVENT] Informs the notification layer when jobs change their estimated work units so the visual progress bar
    // reflects the new domain.
    m_nm->progress_indicator_set_range(range);
}

void NotificationProgressIndicator::set_cancel_callback(CancelFn fn)
{
    m_cancelfn = std::move(fn);
    // [STATE] Caches the callback for reuse, because the manager may need to re-bind the cancel button whenever a job resets its progress.
    // [EVENT] The notification HUD wires this callback into the Cancel button, so it fires on the main thread but jumps back to the worker.
    // [THREAD] Requires a thread-safe transition because the UI thread invokes the callback while the job runs on a worker.
    // [UNITY] Replace with a `CancellationTokenSource` generated in the Unity controller; the HUD button should call `cts.Cancel()` to
    // mirror this hook.
    m_nm->progress_indicator_set_cancel_callback(m_cancelfn);
}

void NotificationProgressIndicator::set_progress(int pr)
{
    if (!pr)
        set_cancel_callback(m_cancelfn);

    // [EVENT] Progress updates stream through the notification manager, keeping the HUD slider, status, and spinner aligned with job ticks.
    // [PORTING_HAZARD:P3] `pr == 0` rebinds the cancel callback, so the Unity port must also rearm its cancel listener when progress restarts.
    m_nm->progress_indicator_set_progress(pr);
}

void NotificationProgressIndicator::set_status_text(const char* msg)
{
    // [STATE] Syncs the textual status message with the manager-managed overlay so the HUD always displays the most recent job hint.
    // [UNITY] Call `NotificationHUDController.SetStatus(string)` so a TextMeshProUGUI element shows the exact string.
    m_nm->progress_indicator_set_status_text(msg);
}

int NotificationProgressIndicator::get_range() const
{
    // [STATE] Queries the manager for the canonical range so this adapter never replicates slider limits.
    return m_nm->progress_indicator_get_range();
}

}} // namespace Slic3r::GUI
