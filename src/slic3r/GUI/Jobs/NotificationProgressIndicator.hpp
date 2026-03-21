#ifndef NOTIFICATIONPROGRESSINDICATOR_HPP
#define NOTIFICATIONPROGRESSINDICATOR_HPP

#include "ProgressIndicator.hpp"

namespace Slic3r { namespace GUI {

class NotificationManager;

class NotificationProgressIndicator : public ProgressIndicator
{
    // [STATE] Keeps a back-pointer to the NotificationManager that owns the indicator so callbacks can be reported back to the originating job.
    NotificationManager* m_nm = nullptr;
    // [STATE][EVENT] Stores the latest cancel callback provided by the job so we can forward UI cancel presses asynchronously.
    CancelFn m_cancelfn;

public:
    // [INTENT][THREAD] Construct and register this widget on the UI/NotificationManager thread so it can post progress updates safely.
    explicit NotificationProgressIndicator(NotificationManager* nm);

    // [STATE] Resets the current UI percent when the owning job reports a restart or completion event.
    void clear_percent() override;
    // [EVENT][THREAD] Receives error signals (often from worker threads) and marshals them through NotificationManager to the UI dialog;
    // guard for wxString encoding.
    void show_error_info(wxString msg, int code, wxString description, wxString extra) override;
    // [STATE][EVENT] Updates the cached range; called prior to many `set_progress` updates so the UI can scale correctly.
    void set_range(int range) override;
    // [EVENT][THREAD][PORTING_HAZARD:P2] Binds a cancel callback that may be invoked by user input; Unity port must wire this to the
    // job/system cancel token rather than wxWidgets CancelFn semantics.
    void set_cancel_callback(CancelFn = CancelFn()) override;
    // [STATE][THREAD][UNITY] Bubbles progress values back to NotificationManager; Unity should map this to a UI Toolkit `ProgressBar` bound
    // to a `ScriptableObject` state updated via `SynchronizationContext.Post`.
    void set_progress(int pr) override;
    // [STATE][EVENT][UNITY] Updates the textual status shown beneath the spinner; Unity should treat this as a localized `Label` string
    // updated on the main thread with the same UTF-8 bytes.
    void set_status_text(const char*) override; // utf8 char array
    // [STATE] Mirrors the range requested by the job so callers outside the indicator can inspect it without touching the UI control.
    int get_range() const override;
};

}} // namespace Slic3r::GUI

#endif // NOTIFICATIONPROGRESSINDICATOR_HPP
