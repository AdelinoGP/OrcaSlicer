#include "BindJob.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include "slic3r/GUI/DeviceCore/DevManager.h"

namespace Slic3r {
namespace GUI {

// [EVENT] Bind job exposes progress/success/fail events that the UI panels hook into via `Bind`/`Connect`.
// [THREAD] Events are always marshaled back to the same `wxWindow` (m_event_handle) so the UI sees state changes only on the main thread.
// [UNITY] Equivalent to exposing `UnityEvent<string>`/`UnityEvent<int>` on a `BindJobController` MonoBehaviour connected to the main dispatcher.
wxDEFINE_EVENT(EVT_BIND_UPDATE_MESSAGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_BIND_MACHINE_SUCCESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_BIND_MACHINE_FAIL, wxCommandEvent);

// [STATE] Short-lived translation buffers describe the current stage to show on the UI progress bar.
// [PORTING_HAZARD:P3] Unicode macros rely on the wxLocale during binding, so Unity must rehydrate localized strings from `LocalizationSettings`.
static auto waiting_auth_str = _u8L("Logging in");
static auto login_failed_str = _u8L("Login failed");

// [INTENT] Capture the printer identity/credentials so the worker thread can replay them when binding to the Bambu service.
// [STATE] Stored fields become the canonical request parameters for retries and for the UI that watches `m_event_handle`.
BindJob::BindJob(std::string dev_id, std::string dev_ip, std::string sec_link, std::string ssdp_version)
    : m_dev_id(dev_id), m_dev_ip(dev_ip), m_sec_link(sec_link), m_ssdp_version(ssdp_version)
{
    ;
}

void BindJob::on_success(std::function<void()> success) { m_success_fun = success; }

// [STATE] `update_status` keeps the UI progress percentage and message in sync with the current binding step.
// [EVENT] It wraps `wxPostEvent` so the UI can bind `EVT_BIND_UPDATE_MESSAGE` and refresh its progress bar on the main thread.
// [THREAD] This helper is safe to call from the background network callback because `wxPostEvent` marshals into the UI thread before the
// handler runs. [UNITY] Replace with `MainThreadDispatcher.Enqueue(() => progressEvent.Invoke(percent, message))` so Unity UI stays on the
// main loop. [PORTING_HAZARD:P3] Events posted to a destroyed window can crash, so Unity must guard the dispatcher against tear-down while
// the network task still runs.
void BindJob::update_status(Ctl& ctl, int st, const std::string& msg)
{
    ctl.update_status(st, msg);
    wxCommandEvent event(EVT_BIND_UPDATE_MESSAGE);
    event.SetString(msg);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}

// [INTENT] Drive the asynchronous binding workflow, translating agent progress into UI events while keeping network work on a background
// thread. [THREAD] This runs on the job worker, so we rely on `wxPostEvent` (`update_status`) to marshal UI updates to the main thread.
// [UNITY] Equivalent to a `UnityWebRequest` coroutine that reports progress through `BindJobController` events on the main dispatcher.
void BindJob::process(Ctl& ctl)
{
    int         result_code = 0;
    std::string result_info;

    /* display info */
    auto msg          = waiting_auth_str;
    int  curr_percent = 0;

    // [STATE] Shared `NetworkAgent` from the app hosts the HTTP bindings we need; bail out if initialization failed.
    NetworkAgent* m_agent = wxGetApp().getAgent();
    if (!m_agent) {
        return;
    }

    // [STATE] The timezone string keeps server-side logs consistent with the client clock.
    // [PORTING_HAZARD:P3] Unity ports must mirror `DateTimeKind.Local` vs `Utc` differences when recalculating these offsets.
    wxDateTime::TimeZone tz(wxDateTime::Local);
    long                 offset   = tz.GetOffset();
    std::string          timezone = get_timezone_utc_hm(offset);

    // [STATE] SSDP version goes with the bind request so the cloud understands which discovery protocol is in use.
    m_agent->track_update_property("ssdp_version", m_ssdp_version, "string");
    // [EVENT] `bind` drives stage-of-login callbacks on worker threads, which we forward to `update_status`.
    // [THREAD] The lambda must not touch UI controls directly; it simply updates state and posts events through `update_status`.
    // [UNITY] Replace this pattern with a `UnityWebRequest` coroutine that reports to `BindJobController.ProgressChanged`.
    int result = m_agent->bind(m_dev_ip, m_dev_id, m_sec_link, timezone, m_improved,
                               [this, &ctl, &curr_percent, &msg, &result_code, &result_info](int stage, int code, std::string info) {
                                   result_code = code;
                                   result_info = info;

                                   if (stage == BindJobStage::LoginStageConnect) {
                                       curr_percent = 15;
                                       msg          = _u8L("Logging in");
                                   } else if (stage == BindJobStage::LoginStageLogin) {
                                       curr_percent = 30;
                                       msg          = _u8L("Logging in");
                                   } else if (stage == BindJobStage::LoginStageWaitForLogin) {
                                       curr_percent = 45;
                                       msg          = _u8L("Logging in");
                                   } else if (stage == BindJobStage::LoginStageGetIdentify) {
                                       curr_percent = 60;
                                       msg          = _u8L("Logging in");
                                   } else if (stage == BindJobStage::LoginStageWaitAuth) {
                                       curr_percent = 80;
                                       msg          = _u8L("Logging in");
                                   } else if (stage == BindJobStage::LoginStageFinished) {
                                       curr_percent = 100;
                                       msg          = _u8L("Logging in");
                                   } else {
                                       msg = _u8L("Logging in");
                                   }

                                   if (code != 0) {
                                       msg = _u8L("Login failed");
                                       if (code == BAMBU_NETWORK_ERR_TIMEOUT) {
                                           msg += _u8L("Please check the printer network connection.");
                                       }
                                   }
                                   update_status(ctl, curr_percent, msg);
                               });

    // [INTENT] Non-zero result means the bind request failed before the server ever confirmed the login.
    // [PORTING_HAZARD:P3] Unity must translate these codes into friendly UI strings and keep the job alive long enough to show them.
    if (result < 0) {
        BOOST_LOG_TRIVIAL(info) << "login: result = " << result;

        if (result_code == BAMBU_NETWORK_ERR_BIND_ECODE_LOGIN_REPORT_FAILED ||
            result_code == BAMBU_NETWORK_ERR_BIND_GET_PRINTER_TICKET_TIMEOUT) {
            int error_code;

            try {
                error_code         = stoi(result_info);
                wxString error_msg = wxGetApp().get_hms_query()->query_print_error_msg(m_dev_id, error_code);
                result_info        = error_msg.ToStdString();
            } catch (...) {
                ;
            }
        }

        // [EVENT] Post the failure details as soon as we detect them so the dialog can show a retry/cancel option.
        // [STATE] `result_info` often contains localized text from HMS error helpers, so Unity must rehydrate the same customer-friendly message.
        post_fail_event(result_code, result_info);
        return;
    }

    // [STATE] When binding succeeds we refresh the DeviceManager cache so the UI list shows the new printer immediately.
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        BOOST_LOG_TRIVIAL(error) << "login: dev is null";
        post_fail_event(result_code, result_info);
        return;
    }
    dev->update_user_machine_list_info();

    // [EVENT] Signal to the status panel that the bind completed so it can trigger a DeviceManager refresh in the UI.
    // [UNITY] Unity equivalent: invoke `MainThreadDispatcher.Enqueue(() => BindJobController.onSuccess.Invoke())` so listeners know the
    // printer is bound.
    wxCommandEvent event(EVT_BIND_MACHINE_SUCCESS);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
    return;
}

// [INTENT] Mirror the job framework finalize hook so completion paths log or rethrow any worker exception without leaking.
// [THREAD] Called back once the worker finishes; UI updates should already be delivered via posted events before this runs.
// [UNITY] Equivalent to the coroutine `BindJobController` invoking a final `TaskCompletionSource` response on the main dispatcher.
// [PORTING_HAZARD:P3] Unity needs to keep its async binder from swallowing exceptions during grounded tear-downs even when canceling.
void BindJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
        eptr = nullptr;
    } catch (...) {
        eptr = std::current_exception();
    }

    if (canceled || eptr)
        return;
}

// [INTENT] Store the main-window `wxWindow` so that all progress/fail/success events target the same dispatcher.
// [THREAD] The caller usually runs on the UI thread before scheduling the job.
void BindJob::set_event_handle(wxWindow* hanle) { m_event_handle = hanle; }

void BindJob::post_fail_event(int code, std::string info)
{
    // [EVENT] Post failure metadata so UI can surface the error, tooltip, and retry/cancel affordances.
    // [UNITY] Equivalent to firing a `BindJobController.onFail` event with `(code, info)` to unblock popups.
    wxCommandEvent event(EVT_BIND_MACHINE_FAIL);
    event.SetInt(code);
    event.SetString(info);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}

}} // namespace Slic3r::GUI
