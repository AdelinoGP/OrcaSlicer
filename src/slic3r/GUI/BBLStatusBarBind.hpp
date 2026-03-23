#ifndef BBLStatusBarBind_HPP
#define BBLStatusBarBind_HPP

#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/simplebook.h>

#include <memory>
#include <string>
#include <functional>
#include <string>
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
class BBLStatusBarBind : public ProgressIndicator
{
    // [INTENT] Bridge the BBL job runner/progress instrumentation to the status bar UI that Plater/Monitor panels own.
    // [PORTING_HAZARD:P3] wxWidgets owns the panel tree, so we cheat by holding a raw wxPanel pointer instead of deriving from it; Unity
    // needs explicit MonoBehaviour ownership rather than inheriting from a panel.
    wxPanel * m_self; // we cheat! It should be the base class but: perl!
    // [STATE] Gauge cached so repeated progress updates can animate without rebuilding the ctrl (Unity analog: keep a ProgressBar +
    // TMP_Text and refresh its value).
    wxGauge * m_prog;
    // [STATE] Cancel button hidden/shown per job; subscribe to the cancel callbacks to keep the UI and job synced.
    Button *       m_cancelbutton;
    wxStaticText * m_status_text;
    wxStaticText * m_stext_percent;
    wxBoxSizer *   m_sizer;
    wxBoxSizer *   m_sizer_eline;

public:
    BBLStatusBarBind(wxWindow* parent = nullptr, int id = -1);
    ~BBLStatusBarBind() = default;

    int get_progress() const;
    // if the argument is less than 0 it shows the last state or
    // pulses if no state was set before.
    void set_prog_block();
    // [EVENT] Called by the worker in `Jobs::ProgressIndicator` to update the UI thread gauge; marshal to the main-thread before mutating controls.
    void set_progress(int) override;
    int  get_range() const override;
    void set_range(int = 100) override;
    void clear_percent() override;
    void show_error_info(wxString msg, int code, wxString description, wxString extra) override;
    void show_progress(bool);
    void start_busy(int = 100);
    void stop_busy();
    // [UNITY] Attach the status bar to Unity via a `MonoBehaviour` that exposes a `ProgressBar`, `Label`, and `Button`, wiring the cancel
    // event to `CancellationTokenSource`. [EVENT] The final cancel callback is registered by Perl/QML to clean up job rows; preserve the
    // callback separately to avoid double-cancelling from other UI paths. [PORTING_HAZARD:P3] Perl legacy glue doubles up cancellation
    // hooks; Unity should unify cancel tokens via a single `CancellationTokenSource` and avoid stacked delegates.
    void        set_cancel_callback_fina(BBLStatusBarBind::CancelFn ccb);
    inline bool is_busy() const { return m_busy; }
    // [EVENT] Primary cancel hook for the job progress indicator; typically wired to the cancel button `Click` event.
    void        set_cancel_callback(CancelFn = CancelFn()) override;
    inline void reset_cancel_callback() { set_cancel_callback(); }
    wxPanel *    get_panel();
    void        set_status_text(const wxString& txt);
    // [UNITY] Double-bind the percent label to the same ScriptableObject that drives the progress gauge so Unity UI can stay in sync.
    void set_percent_text(const wxString& txt);
    // [PORTING_HAZARD:P2] Windows-specific DPI scaling hook; Unity should rely on `CanvasScaler`/`Screen.dpi` instead of this MSW rescale path.
    void msw_rescale();
    void set_status_text(const std::string& txt);
    void set_status_text(const char* txt) override;
    // [STATE] Mirrors the gauge/label text so the Unity StatusBar can render the same strings when replaying the last update.
    wxString get_status_text() const;
    void     set_font(const wxFont& font);
    void     set_object_info(const wxString& txt);
    void     set_slice_info(const wxString& txt);
    // [STATE] Show/hide the supplemental slice info label; Unity should toggle the VisualElement visibility alongside slice metadata updates.
    void show_slice_info(bool show);
    bool is_slice_info_shown();
    // [STATE][THREAD] `yield` determines whether the status update pumps `wxYield` so long-running jobs don't freeze; Unity replacement
    // should dispatch to `MainThreadDispatcher` with `await Task.Yield()` when `yield` is true.
    bool update_status(wxString& msg, bool& was_cancel, int percent = -1, bool yield = true);
    // [UNITY] Reset the Unity-side status overlay (labels, gauge, cancel button) so repeat jobs start from a clean state.
    void reset();

    // Temporary methods to satisfy Perl side
    // [UNITY] Mirror these with `Button.gameObject.SetActive(true/false)` to keep the Unity cancel control in sync with the native tooltip.
    void show_cancel_button();
    void hide_cancel_button();

private:
    // [STATE] Tracks whether the gauge is busy or animating shy of progress values.
    bool m_busy = false;
    // [STATE] Whether the user has already requested cancellation for this job.
    bool m_was_cancelled = false;
    // [EVENT] Bound to the cancel button; Unity should wire the equivalent delegate to `CancellationTokenSource.Cancel()`.
    CancelFn m_cancel_cb;
    // [EVENT] Called when the higher-level Perl UI layer finalizes its cleanup; keep it separate so Unity can keep the cancel button
    // disabled during disposal.
    CancelFn m_cancel_cb_fina;
};

namespace GUI {
using Slic3r::BBLStatusBarBind;
}

} // namespace Slic3r

#endif // BBLSTATUSBAR_HPP
