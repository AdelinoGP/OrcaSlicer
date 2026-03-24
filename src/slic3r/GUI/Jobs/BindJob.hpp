#ifndef __BindJob_HPP__
#define __BindJob_HPP__

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "Job.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

// [INTENT] Encapsulates the asynchronous bind workflow that talks to the Bambu network agent while the UI asynchronously tracks
// progress/messages. [STATE] Holds the canonical printer ID/IP/links so retries and status reports stay tied to the correct device. [UNITY]
// Mirror this using a `BindJobController : MonoBehaviour` that owns a coroutine (UnityWebRequest) plus event hooks and state fields
// serialized in a `ScriptableObject`. [PORTING_HAZARD:P3] Tight coupling to `wxWindow`/`wxPostEvent` means Unity must gate UI updates
// through its dispatcher to avoid cross-thread exceptions.
class BindJob : public Job
{
    wxWindow* m_event_handle{nullptr};
    // [STATE] Primary `wxWindow` that receives every progress/fail/success event so the job never posts to a stale dispatcher.
    // [THREAD] Written on the UI thread before the worker starts; updated/cleared before destruction to avoid post-termination crashes.
    std::function<void()> m_success_fun{nullptr};
    // [EVENT] Optional callback invoked after the success event so controllers can log, close dialogs, or trigger follow-on actions.
    // [UNITY] Replace with a `UnityEvent` that the `BindJobController` exposes so Unity UI reacts whenever the coroutine completes.
    std::string m_dev_id;
    // [STATE] Printer serial/device identifier used in every request so retries stay bound to the correct machine object.
    std::string m_dev_ip;
    // [STATE][PORTING_HAZARD:P3] IP is often read from the DNS cache; Unity must rehydrate it from the latest `PrinterDevice` metadata so
    // it doesn’t drift during networking.
    std::string m_sec_link;
    // [STATE] Security link (token) persisted for the current bind operation, replayed on worker retries.
    std::string m_ssdp_version;
    // [STATE] Discovery protocol version baked into telemetry so the cloud knows which handshake path to expect.
    bool m_job_finished{false};
    // [STATE] Tracks job completion so the scheduler can skip redundant `process` calls.
    int m_print_job_completed_id = 0;
    // [STATE] Holds the final print job ID emitted by the bind response so UI can navigate directly to the new job.
    bool m_improved{false};
    // [STATE] Toggle indicating whether the improved binding API path should be used.

public:
    BindJob(std::string dev_id, std::string dev_ip, std::string sec_link, std::string ssdp_version);

    int status_range() const { return 100; }

    bool is_finished() { return m_job_finished; }

    // [EVENT] Allows external callers to attach a finalization callback that runs after the worker's success event.
    // [UNITY] Expose `BindJobController.onSuccessUnityEvent` and invoke it from the dispatcher.
    void on_success(std::function<void()> success);
    // [STATE] Progress/status helper that owns the percent/value pair shared with the UI control.
    // [EVENT] Posts `EVT_BIND_UPDATE_MESSAGE` through `wxPostEvent` so the progress bar updates safely on the GUI thread.
    // [UNITY] Replace with a dispatcher that emits `UnityEvent<float,string>` and updates the `BindJobController` progress fields.
    void update_status(Ctl& ctl, int st, const std::string& msg);
    // [INTENT] Drives the asynchronous bind coroutine and marshals stage transitions back to UI events.
    // [THREAD] Runs inside the job worker; it must never touch UI controls directly.
    void process(Ctl& ctl) override;
    // [INTENT] Cleanup hook executed once the worker finishes; rethrows exceptions so dispatchers can bubble them into the UI.
    // [UNITY] Fire this through a coroutine `finally` block and notify `BindJobController` consumers via a `TaskCompletionSource` pattern.
    void finalize(bool canceled, std::exception_ptr& eptr) override;
    // [THREAD] Safely sets the window that receives posted events; callers run on the UI thread before enqueueing the job.
    // [PORTING_HAZARD:P3] Guard against posting to destroyed dialogs when Unity rewrites this as a VisualElement controller.
    void set_event_handle(wxWindow* hanle);
    // [EVENT] Emits failure metadata so the UI can show localized text, hints, and retry affordances.
    // [UNITY] Mirror with `BindJobController.onBindFail.Invoke(code, info)` plus a dispatcher guard.
    void post_fail_event(int code, std::string info);
    // [STATE] Toggles which bind path (improved vs legacy) the worker uses before it starts.
    void set_improved(bool improved) { m_improved = improved; };
};

// [EVENT] Global events dispatched by the background work so any window can observe progress/failure/success without holding tight references.
// [THREAD] Always fired via `wxPostEvent` to stay on the main GUI thread.
// [UNITY] Map to `UnityEvent`s on the `BindJobController` that Unity UI Toolkit listeners can bind through VisualElement callbacks.
wxDECLARE_EVENT(EVT_BIND_UPDATE_MESSAGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_BIND_MACHINE_SUCCESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_BIND_MACHINE_FAIL, wxCommandEvent);
}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
