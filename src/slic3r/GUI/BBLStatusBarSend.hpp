#ifndef BBLSTATUSBARSEND_HPP
#define BBLSTATUSBARSEND_HPP

#include <wx/panel.h>
#include <wx/stattext.h>

#include <memory>
#include <string>
#include <functional>
#include <string>
#include <wx/hyperlink.h>

#include "Jobs/ProgressIndicator.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"

class wxTimer;
class wxGauge;
class wxButton;
class wxTimerEvent;
class wxStatusBar;
class wxWindow;
class wxFrame;
class wxString;
class wxFont;

namespace Slic3r {

// [INTENT] Host the send-progress UI row inside the BBL status bar, exposing gauge/cancel controls and error badges tied
// to the send pipeline.
// [UNITY] Port this as a VisualElement row (ProgressBar + Label + Button) managed by a MonoBehaviour that mirrors `ProgressIndicator` callbacks.
class BBLStatusBarSend : public ProgressIndicator
{
    // [STATE] Deliberately point at the owning panel (`m_self`) because Perl glue wanted a wxPanel, not a pure ProgressIndicator. Unity can
    // hold the root VisualElement explicitly.
    wxPanel* m_self; // we cheat! It should be the base class but: perl!
    // [STATE] Gauge widget backed by the same spinning/cancel state as the base ProgressIndicator.
    wxGauge* m_prog;
    // [STATE] Link pair used to show error info; toggled when network issues occur.
    Label*      m_link_show_error;
    wxBoxSizer* m_sizer_status_text;
    // [STATE] Error badges reuse static bitmaps that sit next to the cancel/gauge area.
    wxStaticBitmap* m_static_bitmap_show_error;
    wxBitmap        m_bitmap_show_error_close;
    wxBitmap        m_bitmap_show_error_open;
    // [STATE] Cancel button stays visible only when a cancellable job is running.
    Button* m_cancelbutton;
    // [STATE] Primary status strings and percent labels so the panel can describe send progress per job.
    wxStaticText* m_status_text;
    wxStaticText* m_stext_percent;
    wxBoxSizer*   m_sizer;
    wxBoxSizer*   m_sizer_eline;
    wxWindow*     block_left;
    wxWindow*     block_right;

public:
    // [EVENT] Constructor wires the gauge/cancel controls into ProgressIndicator so external send jobs can report through the inherited interface.
    BBLStatusBarSend(wxWindow* parent = nullptr, int id = -1);
    ~BBLStatusBarSend() = default;

    int get_progress() const;
    // [STATE] Expose the current gauge percentage; the UI thread reads this when reporting to telemetry or logging.
    // if the argument is less than 0 it shows the last state or
    // pulses if no state was set before.
    void set_prog_block();
    // [STATE] Enter a pulsing state when total progress is unknown, just like `wxGauge::Pulse`.
    void set_progress(int) override;
    // [STATE] Update gauge & percent text; this method is called on the worker via ProgressIndicator and lifts the values back onto the UI
    // thread. [UNITY] Mirror this by setting `ProgressBar.value` and binding a `TextMeshPro` percent label in Unity, using
    // `MainThreadDispatcher` for thread hopping.
    int  get_range() const override;
    void set_range(int = 100) override;
    void clear_percent() override;
    // [EVENT] Network/send errors trigger a transient inline message, so the UI posts wx events for hover/click handling instead of modal
    // dialogs. [PORTING_HAZARD:P3] Unity will need a toast controller or popup that hooks into the same cancelable queue instead of
    // `wxShowTextMessage`.
    void show_error_info(wxString msg, int code, wxString description, wxString extra) override;
    void show_progress(bool);
    // [STATE] Toggle between the error+progress stack so only the relevant row is visible.
    void start_busy(int = 100);
    // [STATE] Busy mode pulses the spinner and disables cancel until the backend task signals completion.
    void stop_busy();
    // [STATE] Switch `m_cancelbutton` state to match the job while keeping the same sizer layout.
    void        set_cancel_callback_fina(BBLStatusBarSend::CancelFn ccb);
    inline bool is_busy() const { return m_busy; }
    void        set_cancel_callback(CancelFn = CancelFn()) override;
    // [THREAD][PORTING_HAZARD:P2] Callers (usually workers) attach callbacks here; they must be marshaled back to the UI thread before invocation.
    inline void reset_cancel_callback() { set_cancel_callback(); }
    wxPanel*    get_panel();
    // [UNITY] Expose this panel as the VisualElement root for the send bar container so Unity can toggle it without hitting the base
    // ProgressIndicator class.
    bool is_english_text(wxString str);
    bool format_text(wxStaticText* dc, int width, const wxString& text, wxString& multiline_text);
    void set_status_text(const wxString& txt);
    // [STATE] Public helpers to adjust the status/percent text without re-creating the sizer weights.
    // [UNITY] These map to `Label.text` and `TextMeshPro` fields on the send status VisualElement, bound via the same controller.
    void set_percent_text(const wxString& txt);
    void msw_rescale();
    // [PORTING_HAZARD:P3] On Windows the timer/bitmap DPI requires manual scaling; Unity should prefer canvas scalers rather than
    // OS-specific macros.
    void     set_status_text(const std::string& txt);
    void     set_status_text(const char* txt) override;
    wxString get_status_text() const;
    void     set_font(const wxFont& font);
    void     set_object_info(const wxString& txt);
    void     set_slice_info(const wxString& txt);
    void     show_slice_info(bool show);
    bool     is_slice_info_shown();
    bool     update_status(wxString& msg, bool& was_cancel, int percent = -1, bool yield = true);
    // [EVENT][THREAD] Clients call this every tick, yielding to the UI loop so the progress bar stays responsive; the worker thread must
    // marshal to the UI thread before mutating state.
    void reset();
    // Temporary methods to satisfy Perl side
    void show_cancel_button();
    void hide_cancel_button();
    void change_button_label(wxString name);

    void disable_cancel_button();
    void enable_cancel_button();

    void cancel();
    // [THREAD][PORTING_HAZARD:P2] Cancel requests bubble through `CancelFn`; Unity should hook into `CancellationTokenSource` and dispatch
    // back to the UI thread before mutating UX.

private:
    // [STATE] Tracks whether the inline error row is currently visible so repeated events can be ignored.
    bool m_show_error_info_state = false;
    // [STATE] ProgressIndicator busy flag that hides the cancel button until the send job makes progress.
    bool m_busy = false;
    // [STATE] Guard to show friendly language when cancellation already happened.
    bool m_was_cancelled = false;
    // [THREAD] Callback invoked when the cancel button fires; keep this on the UI thread before drilling into send jobs.
    CancelFn m_cancel_cb;
    // [THREAD] Finalizer callback that reroutes to backend jobs; Unity should tie this to `CancellationTokenSource.Token.Register`.
    CancelFn m_cancel_cb_fina;
};

namespace GUI {
using Slic3r::BBLStatusBarSend;
}

wxDECLARE_EVENT(EVT_SHOW_ERROR_INFO_SEND, wxCommandEvent);
wxDECLARE_EVENT(EVT_SHOW_ERROR_FAIL_SEND, wxCommandEvent);
} // namespace Slic3r

#endif // BBLSTATUSBAR_HPP
