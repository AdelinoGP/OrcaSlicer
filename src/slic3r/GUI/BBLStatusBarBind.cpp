#include "BBLStatusBarBind.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include "wx/evtloop.h"
#include <wx/gdicmn.h>
#include "GUI_App.hpp"

#include "I18N.hpp"

#include <iostream>

namespace Slic3r {

// [INTENT] Bridge this BBL-specific status overlay into the ProgressIndicator contract so downloads/prints show a dedicated gauge row.
// [STATE] Tracks gauge value, percent label text, cancel visibility, and the busy flag inside a DPI-aware panel.
// [UNITY] Mirror this with a UI Toolkit VisualElement row (ProgressBar + Label + Button) anchored to the bottom status bar and updated via
// a MainThreadDispatcher so Unity UI stays on the main thread.

BBLStatusBarBind::BBLStatusBarBind(wxWindow* parent, int id)
    : m_self{new wxPanel(parent, id == -1 ? wxID_ANY : id)}, m_sizer(new wxBoxSizer(wxHORIZONTAL))
{
    // [INTENT] Compose the gauge + percent text row when binding to a parent status area.
    // [STATE] `m_prog` and `m_stext_percent` carry the displayed level + percent string, and `m_sizer` keeps DPI-aware spacing.
    // [UNITY] Map to a UI Toolkit VisualElement row with a `ProgressBar`, a `Label`, and DPI-aware layout; mimic `FromDIP` with
    // ScaleFactor-aware constants.

    m_self->SetBackgroundColour(wxColour(255, 255, 255));
    m_self->SetMinSize(wxSize(m_self->FromDIP(450), m_self->FromDIP(30)));

    // [STATE] Gauge occupies a fixed DIP width so the BBL progress meter stays a consistent physical size; Unity should tie this to a
    // CanvasScaler+Layout element.
    m_prog = new wxGauge(m_self, wxID_ANY, 100, wxDefaultPosition, wxSize(m_self->FromDIP(400), m_self->FromDIP(6)), wxGA_HORIZONTAL);
    m_prog->SetValue(0);

    m_stext_percent = new wxStaticText(m_self, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_stext_percent->SetForegroundColour(wxColour(107, 107, 107));
    m_stext_percent->SetFont(::Label::Body_13);
    m_stext_percent->Wrap(-1);
    // [STATE] Grey `Label::Body_13` keeps percent text legible without overpowering the bar, and Wrap ensures long messages stay on a single line.

    m_sizer->Add(m_prog, 1, wxALIGN_CENTER, 0);
    m_sizer->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer->Add(m_stext_percent, 1, wxALIGN_CENTER, 0);

    m_self->SetSizer(m_sizer);
    m_self->Layout();
    m_sizer->Fit(m_self);
}

void BBLStatusBarBind::set_prog_block()
{
    // [INTENT] Legacy hook to switch the gauge into block/pulse mode before showing explicit values.
    // [UNCLEAR] The method is currently a no-op, so Unity should either mimic the intended pulsing or drop the hook.
}

int BBLStatusBarBind::get_progress() const { return m_prog->GetValue(); }

void BBLStatusBarBind::set_progress(int val)
{
    // [THREAD] Layout changes and gauge updates must happen on the UI thread because they mutate wxWidgets controls.
    set_prog_block();

    if (val < 0)
        return;

    if (!m_sizer->IsShown(m_prog)) {
        // [STATE] Lazily reveal the gauge and cancel button so the row stays compact when idle.
        m_sizer->Show(m_prog);
        m_sizer->Show(m_cancelbutton);
    }
    m_prog->SetValue(val);
    // [STATE] Keep the percent label in sync with the numeric value (used in Unity to drive Text content).
    set_percent_text(wxString::Format("%d%%", val));
    m_sizer->Layout();
}

int BBLStatusBarBind::get_range() const { return m_prog->GetRange(); }

void BBLStatusBarBind::set_range(int val)
{
    if (val != m_prog->GetRange()) {
        m_prog->SetRange(val);
    }
}

void BBLStatusBarBind::clear_percent()
{
    // [INTENT] Clear the textual percent display when a job resets; currently unused but left for future clarity.
}

void BBLStatusBarBind::show_error_info(wxString msg, int code, wxString description, wxString extra)
{
    // [UNCLEAR] Placeholder for showing error details; the BBL flow may never hit this, so Unity should audit whether it needs a dedicated
    // error panel.
}

void BBLStatusBarBind::show_progress(bool show)
{
    if (show) {
        // [STATE] Showing the gauge keeps the BBL overlay visible while work is active.
        m_sizer->Show(m_prog);
        m_sizer->Layout();
    } else {
        // [STATE] The hidden branch leaves the gauge present but not visible so a future show is fast.
        // m_sizer->Hide(m_prog);
        m_sizer->Layout();
    }
}

void BBLStatusBarBind::start_busy(int rate)
{
    // [STATE] busy flag means a BBL job is active; show the gauge and cancel button so users can abort.
    // [UNITY] Unity should turn on the ProgressBar and cancel Button in one go instead of recreating the panel.
    m_busy = true;
    show_progress(true);
    show_cancel_button();
}

void BBLStatusBarBind::stop_busy()
{
    // [STATE] Reset busy flag, hide progress/cancel visuals, and zero the gauge for reuse.
    show_progress(false);
    hide_cancel_button();
    m_prog->SetValue(0);
    m_sizer->Layout();
    m_busy = false;
}

void BBLStatusBarBind::set_cancel_callback_fina(BBLStatusBarBind::CancelFn ccb)
{
    m_cancel_cb_fina = ccb;
    // [EVENT] Show/hide the cancel button based on whether the final cancel callback is wired.
    // [PORTING_HAZARD:P3] `m_cancelbutton` may still be null in this bind class, so Unity needs a safe guard before toggling the button.
    if (ccb) {
        m_sizer->Show(m_cancelbutton);
    } else {
        m_sizer->Hide(m_cancelbutton);
    }
}

void BBLStatusBarBind::set_cancel_callback(BBLStatusBarBind::CancelFn ccb)
{
    /*  m_cancel_cb = ccb;
      if (ccb) {
          m_sizer->Show(m_cancelbutton);
      }
      else {
          m_sizer->Hide(m_cancelbutton);
      }
      m_sizer->Layout();*/
    // [UNCLEAR] This override is intentionally disabled; ensure Unity wires CancelFn into the button and layout updates instead of relying
    // on stale code. [PORTING_HAZARD:P3] The commented section shows how visibility toggles were handled, so the Unity port should honor
    // the same event semantics.
}

wxPanel* BBLStatusBarBind::get_panel() { return m_self; }

void BBLStatusBarBind::set_status_text(const wxString& txt)
{
    // [STATE] `m_status_text` is the verbose text label, but the previous lines are commented out so no text is rendered today.
    // [UNCLEAR] Unity should decide whether to render these messages or drop the extra label entirely.
}

void BBLStatusBarBind::set_percent_text(const wxString& txt)
{
    // [STATE] Keep the percent label sync'd with the gauge so the user sees both numeric and graphical cues.
    m_stext_percent->SetLabelText(txt);
}

void BBLStatusBarBind::set_status_text(const std::string& txt) { this->set_status_text(txt.c_str()); }

void BBLStatusBarBind::set_status_text(const char* txt) { this->set_status_text(wxString::FromUTF8(txt)); }

void BBLStatusBarBind::msw_rescale()
{
    // [STATE] Called when the system DPI changes so the gauge/cancel controls stay readable.
    // [PORTING_HAZARD:P2] Unity must also track DPI/scale changes (CanvasScaler or UI Toolkit) to avoid mismatched button sizes.
    set_prog_block();
    m_cancelbutton->SetMinSize(wxSize(m_self->FromDIP(56), m_self->FromDIP(24)));
}

wxString BBLStatusBarBind::get_status_text() const { return m_status_text->GetLabelText(); }

bool BBLStatusBarBind::update_status(wxString& msg, bool& was_cancel, int percent, bool yield)
{
    // [INTENT] Refresh the textual status, gauge value, and cancellation flag for the BBL job.
    // [THREAD] Must run on the UI thread because it mutates widgets and yields the event loop.
    // [PORTING_HAZARD:P2] `YieldFor` re-enters the wx loop; Unity should replace this with a main-thread await/pump sequence.
    set_status_text(msg);

    if (percent >= 0)
        this->set_progress(percent);

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    was_cancel = m_was_cancelled;
    return true;
}

void BBLStatusBarBind::reset()
{
    // [STATE] Reset text, cancellation flag, and progress so the next job starts from a clean slate.
    set_status_text("");
    m_was_cancelled = false;
    set_progress(0);
}

void BBLStatusBarBind::set_font(const wxFont& font)
{
    // [STATE] Apply the font to the panel so the gauge + percent label respect the requested typography.
    m_self->SetFont(font);
}

void BBLStatusBarBind::show_cancel_button()
{
    // [EVENT] Make the cancel control visible so users can abort the job while the status row is busy.
    m_sizer->Show(m_cancelbutton);
    m_sizer->Layout();
}

void BBLStatusBarBind::hide_cancel_button()
{
    // [EVENT] Hide the cancel button when the job finishes so the row stays compact.
    m_sizer->Hide(m_cancelbutton);
    m_sizer->Layout();
}

} // namespace Slic3r
