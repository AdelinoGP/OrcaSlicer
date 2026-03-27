#include "PublishDialog.hpp"
#include "GUI_App.hpp"

#include <wx/wx.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include "wx/evtloop.h"

#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"

// [INTENT] Modal publish progress dialog for the GUI-side upload flow.
// [STATE] It owns the visible step indicator, progress bar, status text, and cancel flag while
// [STATE] delegating the actual slicing/upload work back to Plater via EVT_PUBLISH messages.
// [UNITY] This maps best to a modal progress panel or overlay driven by a coroutine/async state
// [UNITY] machine, not a blocking dialog that pumps the UI event loop manually.
// [PORTING_HAZARD:P2] The implementation relies on wx event-loop reentry (`YieldFor`) during a
// [PORTING_HAZARD:P2] long-running workflow, which can create subtle reentrancy and lifecycle bugs.
static const wxColour TEXT_LIGHT_GRAY = wxColour(107, 107, 107);

namespace Slic3r { namespace GUI {

static wxString PUBLISH_STEP_STRING[STEP_COUNT] = {_L("Slice all plate to obtain time and filament estimation"),
                                                   _L("Packing project data into 3MF file"), _L("Uploading 3MF"),
                                                   _L("Jump to model publish web page")};

static wxString NOTE_STRING = _L("Note: The preparation may take several minutes. Please be patient.");

PublishDialog::PublishDialog(Plater* plater)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Publish"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
{
    this->SetSize(wxSize(FromDIP(540), FromDIP(400)));

    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* m_main_sizer = new wxBoxSizer(wxVERTICAL);

    m_main_sizer->Add(0, FromDIP(30), 0, wxEXPAND, 0);

    m_step_panel           = new wxPanel(this, wxID_ANY);
    wxBoxSizer* step_sizer = create_publish_step_sizer();
    m_step_panel->SetSizer(step_sizer);
    m_step_panel->SetBackgroundColour(wxColour(248, 248, 248));

    m_main_sizer->Add(m_step_panel, 1, wxEXPAND, 0);

    m_main_sizer->Add(0, FromDIP(20), 0, wxEXPAND, 0);

    m_text_note = new wxStaticText(this, wxID_ANY, NOTE_STRING, wxDefaultPosition, wxDefaultSize, 0);
    m_text_note->SetFont(Label::Body_14);
    m_text_note->SetForegroundColour(TEXT_LIGHT_GRAY);
    m_text_note->Wrap(-1);
    m_main_sizer->Add(m_text_note, 0, wxALL | wxEXPAND, 0);
    m_main_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    wxStaticLine* m_staticline = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_main_sizer->Add(m_staticline, 0, wxALL | wxEXPAND, FromDIP(0));
    m_main_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    wxBoxSizer* m_progress_text_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_text_progress                   = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_text_progress->Wrap(-1);
    m_text_progress->SetFont(Label::Body_12);
    m_text_progress->SetForegroundColour(TEXT_LIGHT_GRAY);

    m_progress_text_sizer->Add(FromDIP(20), 0, 0, wxEXPAND | wxALL, 0);
    m_progress_text_sizer->Add(m_text_progress, 1, wxALL | wxEXPAND, 0);
    m_main_sizer->Add(m_progress_text_sizer, 0, wxALL | wxEXPAND, 0);

    wxBoxSizer* m_progress_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_progress_sizer->Add(FromDIP(20), 0, 0, wxEXPAND | wxALL, 0);

    m_progress = new ProgressBar(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize);
    m_progress->SetHeight(FromDIP(8));
    m_progress->SetFont(Label::Head_10);
    m_progress_sizer->Add(m_progress, 1, wxALIGN_CENTER_VERTICAL | wxALL, 0);

    m_progress_sizer->Add(FromDIP(20), 0, 0, wxEXPAND, 0);

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_progress_sizer->Add(m_btn_cancel, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    StateColor text_color(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Hovered),
                          std::pair<wxColour, int>(TEXT_LIGHT_GRAY, StateColor::Normal));
    m_btn_cancel->SetFont(Label::Body_12);
    m_btn_cancel->SetBackgroundColor(btn_bg_green);
    m_btn_cancel->SetBorderColor(wxColour(0, 150, 136));
    m_btn_cancel->SetTextColor(text_color);
    m_btn_cancel->SetSize(wxSize(FromDIP(60), FromDIP(20)));
    m_btn_cancel->SetMinSize(wxSize(FromDIP(60), FromDIP(20)));
    m_btn_cancel->SetCornerRadius(FromDIP(10));

    m_progress_sizer->Add(FromDIP(20), 0, 0, wxEXPAND | wxALL, 0);

    m_main_sizer->Add(m_progress_sizer, 0, wxEXPAND, FromDIP(0));

    wxBoxSizer* m_bottom_sizer;
    m_bottom_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_bottom_sizer->Add(FromDIP(20), 0, 0, wxEXPAND, 0);

    m_text_errors = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_text_errors->Wrap(-1);
    m_text_errors->SetFont(Label::Body_12);
    m_text_errors->SetForegroundColour(wxColour(255, 111, 0));
    m_bottom_sizer->Add(m_text_errors, 1, wxALL, 0);

    m_main_sizer->Add(m_bottom_sizer, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    top_sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);
    top_sizer->Add(m_main_sizer, 1, wxALL | wxEXPAND, 0);
    top_sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);

    this->SetSizer(top_sizer);
    this->Layout();

    this->Centre(wxBOTH);

    // [EVENT] Cancel is immediate UI state, then forwarded into the close path so the plater side
    // [EVENT] can receive the publish-stop notification through the same event channel.
    m_btn_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { this->cancel(); });

    // [EVENT] Close-window requests are translated into EVT_PUBLISH stop signals for the owning
    // [EVENT] Plater instance instead of destroying the dialog synchronously here.
    Bind(wxEVT_CLOSE_WINDOW, &PublishDialog::on_close, this);
    wxGetApp().UpdateDlgDarkUI(this);
}

void PublishDialog::cancel()
{
    // [STATE] Cancel is sticky and suppresses later status updates from the publish pipeline.
    m_was_cancelled = true;
    m_btn_cancel->Enable(false);
    m_text_progress->SetLabelText(_L("Publish was canceled"));
    wxCloseEvent evt;
    this->on_close(evt);
}

void PublishDialog::start_slicing()
{
    // [EVENT] Publishing starts by switching the local step UI, then notifying Plater through a
    // [EVENT] queued event so the workflow continues outside the dialog object.
    SetPublishStep(PublishStep::STEP_SLICING);

    wxCommandEvent* evt = new wxCommandEvent(EVT_PUBLISH);
    evt->SetInt(EVT_PUBLISHING_START);
    wxQueueEvent(m_plater, evt);
}

bool PublishDialog::UpdateStatus(wxString& msg, int percent, bool yield)
{
    // [THREAD] This is written like a UI-thread callback; it mutates widgets directly and optionally
    // [THREAD] yields to the active wx loop instead of marshaling across a worker-thread boundary.
    // [UNCLEAR] Hypothesis: publish callbacks are expected to arrive on the UI thread after the
    // [UNCLEAR] worker posts progress through the dialog/plater event chain.
    if (m_was_cancelled)
        return false;

    if (percent >= 0)
        m_progress->SetValue(percent);
    m_text_progress->SetLabelText(msg);

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    return true;
}

void PublishDialog::Pulse(wxString& msg, bool& skip)
{
    // [THREAD] Same reentrant UI-progress pattern as UpdateStatus; `skip` mirrors the cancel flag so
    // [THREAD] the caller can abort a long operation without directly touching widget ownership.
    if (!msg.IsEmpty())
        m_text_progress->SetLabelText(msg);
    wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    skip = m_was_cancelled;
}

void PublishDialog::SetPublishStep(PublishStep step, bool yield, int percent)
{
    // [STATE] StepIndicator is the visible workflow model; progress defaults are heuristic milestones
    // [STATE] for packing/uploading when the backend does not supply an exact percentage.
    m_publish_steps->SelectItem((int) step);
    if (step == PublishStep::STEP_SLICING) {
        m_text_progress->SetLabelText(_L("Slicing Plate 1"));
        if (percent > 0)
            m_progress->SetValue(percent);
        else
            m_progress->SetValue(0);
    } else if (step == PublishStep::STEP_PACKING) {
        m_text_progress->SetLabelText(_L("Packing data to 3MF"));
        if (percent > 0)
            m_progress->SetValue(percent);
        else
            m_progress->SetValue(70);
    } else if (step == PublishStep::STEP_UPLOADING) {
        m_text_progress->SetLabelText(_L("Uploading data"));
        if (percent > 0)
            m_progress->SetValue(percent);
        else
            m_progress->SetValue(85);
    } else if (step == PublishStep::STEP_FILL_INFO) {
        m_text_progress->SetLabelText(_L("Jump to webpage"));
        m_progress->SetValue(100);
    }

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
}

wxBoxSizer* PublishDialog::create_publish_step_sizer()
{
    // [INTENT] Build the left-side step rail as a simple vertical stack centered inside the dialog.
    // [UNITY] A Unity port would likely use a ListView or vertical stepper component with a shared
    // [UNITY] state model rather than manual sizer arithmetic.
    auto sizer = new wxBoxSizer(wxHORIZONTAL);

    sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);

    auto middle_sizer = new wxBoxSizer(wxVERTICAL);

    m_publish_steps = new StepIndicator(m_step_panel, wxID_ANY);
    StateColor bg_color(std::pair<wxColour, int>(wxColour(248, 248, 248), StateColor::Normal));
    m_publish_steps->SetBackgroundColor(bg_color);
    m_publish_steps->SetFont(Label::Body_14);

    for (int i = 0; i < (int) PublishStep::STEP_PUBLISH_COUNT; i++) {
        m_publish_steps->AppendItem(PUBLISH_STEP_STRING[i]);
    }

    middle_sizer->Add(0, FromDIP(30), 0, wxEXPAND, 0);
    middle_sizer->Add(m_publish_steps, 1, wxALL | wxEXPAND, 0);
    middle_sizer->Add(0, FromDIP(30), 0, wxEXPAND, 0);

    sizer->Add(middle_sizer, 1, wxALL | wxEXPAND, 0);

    sizer->Add(FromDIP(30), 0, 0, wxEXPAND, 0);

    return sizer;
}

void PublishDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    // [INTENT] DPI changes only need a relayout/refresh because this dialog is composed from DIP-aware
    // [INTENT] controls and does not own custom raster resources.
    Fit();
    Refresh();
}

void PublishDialog::on_close(wxCloseEvent& event)
{
    // [EVENT] Closing the dialog emits a publish-stop command to the owning Plater; the close event
    // [EVENT] itself is intentionally not consumed here because shutdown is coordinated elsewhere.
    wxCommandEvent* evt = new wxCommandEvent(EVT_PUBLISH);
    evt->SetInt(EVT_PUBLISHING_STOP);
    wxQueueEvent(m_plater, evt);
}

void PublishDialog::reset()
{
    // [STATE] Reset restores the dialog to its initial publish-ready state for reuse across runs.
    m_btn_cancel->Enable();
    m_was_cancelled = false;
    SetPublishStep(PublishStep::STEP_SLICING);
    m_text_errors->Hide();
}

}} // namespace Slic3r::GUI
