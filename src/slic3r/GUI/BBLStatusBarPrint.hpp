#ifndef BBLStatusBarPrint_HPP
#define BBLStatusBarPrint_HPP

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

class BBLStatusBarPrint : public ProgressIndicator
{
    // [INTENT][UNITY][PORTING_HAZARD:P3] Print-only overlay that reuses the ProgressIndicator base while exposing BBL-specific gauge, error
    // link, and cancel controls; Unity needs a Canvas/VisualElement layout with ProgressBar + Button + MainThreadDispatcher hooks to mirror
    // this flow instead of wxWidgets event loops.
    wxPanel* m_self; // [STATE][PORTING_HAZARD:P3] cheat pointer to the owning wxPanel (ProgressIndicator should own this, but the BBL
                     // overlay manages it manually); Unity must own the VisualElement/MonoBehaviour lifetime explicitly.
    wxGauge* m_prog; // [STATE][UNITY] gauge caching the numeric progress; maps to a UI Toolkit ProgressBar or Canvas Image fill amount.
    Label*   m_link_show_error; // [EVENT][UNITY] hyperlink that dispatches EVT_SHOW_ERROR_INFO; Unity should use a Label/Button pair routed
                                // through the dispatcher to show error dialogs.
    wxBoxSizer*     m_sizer_status_text;        // [STATE] layout cache for status/percent text blocks.
    wxStaticBitmap* m_static_bitmap_show_error; // [STATE][PORTING_HAZARD:P3] toggled icon around the error link; Unity needs sprite
                                                // swapping or atlas references.
    wxBitmap m_bitmap_show_error_close;
    wxBitmap m_bitmap_show_error_open;
    Button*  m_cancelbutton; // [STATE][EVENT][UNITY] cancel button retained to toggle callbacks; Unity equivalent is a Button whose onClick
                             // triggers the cancel callbacks on the UI thread.
    wxStaticText* m_status_text;   // [STATE][UNITY] cached status label that keeps the last text in sync with the gauge display.
    wxPanel*      top_panel;       // [STATE] container for gauge + buttons that must stay on the main UI thread.
    wxStaticText* m_stext_percent; // [STATE] tag used to mirror numeric percentages across the status layout.
    wxBoxSizer*   m_sizer;
    wxBoxSizer*   m_sizer_eline;
    wxWindow*     block_left;
    wxWindow*     block_right;

public:
    BBLStatusBarPrint(wxWindow* parent = nullptr, int id = -1);
    ~BBLStatusBarPrint() = default;

    // [STATE][UNITY] exposes the current gauge value so UI sync logic (and Unity progress bindings) can read it without re-querying the
    // underlying gauge.
    int get_progress() const;
    // if the argument is less than 0 it shows the last state or
    // pulses if no state was set before.
    // [STATE][THREAD] toggles the gauge into indeterminate mode and should run on the UI thread before worker progress resumes.
    void set_prog_block();
    // [STATE][EVENT][THREAD][UNITY] updates the stored percent and refreshes the link/label state; Unity needs to marshal this to the UI
    // thread (MainThreadDispatcher) and drive a ProgressBar VisualElement.
    void set_progress(int) override;
    // [STATE] reports the gauge range to keep Unity's ProgressBar scale in sync.
    int get_range() const override;
    // [STATE] resets the gauge ceiling so range-based progress calculations stay correct.
    void set_range(int = 100) override;
    // [STATE][UNITY] clears the percent label so the Unity percent text can restart a fresh caption.
    void clear_percent() override;
    // [EVENT][UNITY][PORTING_HAZARD:P3] shows a linked error dialog via EVT_SHOW_ERROR_INFO; Unity must trigger a popup via
    // VisualElement/Modal window on the main thread.
    void show_error_info(wxString msg, int code, wxString description, wxString extra) override;
    // [STATE] toggles the visibility of the progress label/meter for this print-specific panel.
    void show_progress(bool);
    // [STATE][UNITY] activate/deactivate the indeterminate busy animation; Unity analog is a pulsing ProgressBar or Loading overlay bound
    // to this controller.
    void start_busy(int = 100);
    // [STATE] stops the busy animation after the job finishes or cancels.
    void stop_busy();
    // [EVENT][THREAD] registers the final CancelFn invoked when the job requests cancellation; Unity should capture this via
    // CancellationTokenSource + MainThreadDispatcher.
    void        set_cancel_callback_fina(BBLStatusBarPrint::CancelFn ccb);
    inline bool is_busy() const { return m_busy; }
    // [EVENT] sets or clears the cancel callback that the cancel button triggers.
    void        set_cancel_callback(CancelFn = CancelFn()) override;
    inline void reset_cancel_callback() { set_cancel_callback(); }
    // [STATE][UNITY] exposes the owning panel so the binding layer (wx or Unity UI) can place it within the status strip.
    wxPanel* get_panel();
    bool     is_english_text(wxString str);
    bool     format_text(wxStaticText* dc, int width, const wxString& text, wxString& multiline_text);
    // [STATE][UNITY] updates the primary status label so UI Toolkit VisualElements or Unity Canvas labels can reflect the current message.
    void set_status_text(const wxString& txt);
    // [STATE] keeps the percent label text aligned with the gauge value.
    void set_percent_text(const wxString& txt);
    // [PORTING_HAZARD:P3] Windows-specific DPI rescale helper; Unity must rely on its own DPI/Canvas scaling behavior.
    void     msw_rescale();
    void     set_status_text(const std::string& txt);
    void     set_status_text(const char* txt) override;
    wxString get_status_text() const;
    void     set_font(const wxFont& font);
    void     set_object_info(const wxString& txt);
    void     set_slice_info(const wxString& txt);
    void     show_slice_info(bool show);
    bool     is_slice_info_shown();
    // [EVENT][THREAD][UNITY] updates status text and progress on the main thread; Unity must dispatch these updates through a
    // MainThreadDispatcher when invoked from background workers.
    bool update_status(wxString& msg, bool& was_cancel, int percent = -1, bool yield = true);
    // [STATE] resets gauge, busy states, and cancel callbacks so the next print task can start with known state.
    void reset();
    // Temporary methods to satisfy Perl side
    // [STATE][EVENT][UNITY] helpers that toggle the cancel button and update its label; Unity should enable/disable its Button and swap its text.
    void show_cancel_button();
    void hide_cancel_button();
    void change_button_label(wxString name);

    // [STATE] allow cancel button to be temporarily disabled when a cancel is already in flight; Unity should disable the Button component.
    void disable_cancel_button();
    void enable_cancel_button();

private:
    bool     m_show_error_info_state = false; // [STATE] tracks whether error info is currently visible.
    bool     m_busy                  = false; // [STATE] busy indicator flag; Unity should mirror this as a pulsing overlay Boolean.
    bool     m_was_cancelled         = false; // [STATE] remembers if cancel was already requested to avoid repeated firing.
    CancelFn m_cancel_cb;                     // [EVENT] primary cancel callback triggered via the button.
    CancelFn m_cancel_cb_fina;                // [EVENT] final cancel handler invoked at job teardown.
};

namespace GUI {
using Slic3r::BBLStatusBarPrint;
}

// [EVENT][PORTING_HAZARD:P3] custom wxCommandEvent used to surface error information; Unity port needs a dedicated event bus or Command
// pattern to avoid polling.
wxDECLARE_EVENT(EVT_SHOW_ERROR_INFO, wxCommandEvent);

} // namespace Slic3r

#endif // BBLSTATUSBAR_HPP
