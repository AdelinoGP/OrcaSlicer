#include "ThermalPreconditioningDialog.hpp"
#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "DeviceCore/DevManager.h"

namespace Slic3r { namespace GUI {

// [EVENT] The dialog is driven entirely by wx event dispatch: OK closes it and the timer refreshes the countdown.
// [UNITY] Port this as a modal controller with an onClick close button and a repeating UI-thread tick instead of a native timer object.
BEGIN_EVENT_TABLE(ThermalPreconditioningDialog, wxDialog)
EVT_BUTTON(wxID_OK, ThermalPreconditioningDialog::on_ok_clicked)
END_EVENT_TABLE()

// [INTENT] Modal thermal-preconditioning status window: it shows the remaining bed-preheat time before print start.
// [STATE] Owns the selected device id, a one-second refresh timer, and three widgets that are rebuilt in create_ui().
// [THREAD] All state reads happen on the UI thread; the timer callback polls DeviceManager instead of using a worker thread.
// [PORTING_HAZARD:P2] stage_curr == 58 is a protocol magic value, and get_my_machine() is assumed to succeed without a null check.
// [UNITY] Replace this with a retained modal panel bound to printer-state data plus a scheduled UI tick/coroutine.
ThermalPreconditioningDialog::ThermalPreconditioningDialog(wxWindow* parent, std::string dev_id, const wxString& remaining_time)
    : wxDialog(parent,
               wxID_ANY,
               _L("Thermal Preconditioning for first layer optimization"),
               wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_dev_id(dev_id)
{
    // Apply dark-mode-friendly background for the dialog
    SetBackgroundColour(StateColor::darkModeColorFor(wxColour(255, 255, 255)));
    create_ui();
    m_refresh_timer = new wxTimer(this);
    this->Bind(wxEVT_TIMER, &ThermalPreconditioningDialog::on_timer, this);
    m_refresh_timer->Start(1000);

    // Set remaining time
    m_remaining_time_label->SetLabelText(_L("Remaining time: Calculating..."));

    Layout();
    // Set dialog size and position
    SetSize(wxSize(FromDIP(400), FromDIP(200)));
    wxGetApp().UpdateDlgDarkUI(this);
    CentreOnScreen();
}

// [STATE] The wxTimer is owned by the dialog; stop it before delete so the event loop cannot fire after teardown.
ThermalPreconditioningDialog::~ThermalPreconditioningDialog()
{
    if (m_refresh_timer && m_refresh_timer->IsRunning()) {
        m_refresh_timer->Stop();
        delete m_refresh_timer;
        m_refresh_timer = nullptr;
    }
}

// [INTENT] Build a small, centered status layout with a countdown label, explanation copy, and a single confirm action.
// [UNITY] This maps cleanly to a vertical layout group with a bound text field and a themed primary button.
void ThermalPreconditioningDialog::create_ui()
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    // Remaining time label
    m_remaining_time_label = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxFont time_font       = m_remaining_time_label->GetFont();
    time_font.SetPointSize(14);
    time_font.SetWeight(wxFONTWEIGHT_BOLD);
    m_remaining_time_label->SetFont(time_font);
    m_remaining_time_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour(50, 58, 61)));

    // Explanation text
    m_explanation_label = new wxStaticText(this, wxID_ANY,
                                           _L("The heated bed's thermal preconditioning helps optimize the first layer print quality. "
                                              "Printing will start once preconditioning is complete."),
                                           wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_explanation_label->Wrap(FromDIP(350));
    m_explanation_label->SetForegroundColour(StateColor::darkModeColorFor(wxColour(50, 58, 61)));

    m_ok_button = new wxButton(this, wxID_OK, _L("OK"));
#ifdef __WXMAC__
    m_ok_button->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
    m_ok_button->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
#else
    m_ok_button->SetBackgroundColour(StateColor::darkModeColorFor(wxColour("#B6F34F")));
    m_ok_button->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#000000")));
    m_ok_button->SetMinSize(wxSize(FromDIP(80), FromDIP(32)));
    m_ok_button->SetMaxSize(wxSize(FromDIP(80), FromDIP(32)));
#endif

    // Layout
    main_sizer->Add(0, 0, 1, wxEXPAND);
    main_sizer->Add(m_remaining_time_label, 0, wxALIGN_CENTER);
    main_sizer->Add(m_explanation_label, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 1, wxEXPAND);
    main_sizer->Add(m_ok_button, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, FromDIP(20));

    SetSizer(main_sizer);
}

// [EVENT] OK is treated as a simple modal dismissal; there is no extra commit path or deferred action.
void ThermalPreconditioningDialog::on_ok_clicked(wxCommandEvent& event) { EndModal(wxID_OK); }

// [INTENT] Recompute the visible countdown from the active machine state and rewrite the status label.
// [STATE] Negative remaining time is treated as "unknown" and leaves the label empty until a valid estimate arrives.
// [UNITY] A Unity port should bind this to a presenter/view-model that exposes the formatted text rather than calling device APIs from the view.
void ThermalPreconditioningDialog::update_thermal_remaining_time()
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;
    MachineObject* m_obj = dev->get_my_machine(m_dev_id);

    int      remaining_seconds = m_obj->get_stage_remaining_seconds();
    wxString remaining_time;
    if (remaining_seconds >= 0) {
        int minutes    = remaining_seconds / 60;
        int seconds    = remaining_seconds % 60;
        remaining_time = wxString::Format(_L("Remaining time: %dmin%ds"), minutes, seconds);
    }

    if (m_remaining_time_label)
        m_remaining_time_label->SetLabelText(remaining_time);

    Layout();
}

// [THREAD] wxTimer events arrive on the dialog's UI thread; the callback only polls state and stops itself when the stage ends.
// [UNCLEAR] stage_curr == 58 appears to be the thermal-preconditioning phase code; keep that knowledge centralized in the Unity port.
void ThermalPreconditioningDialog::on_timer(wxTimerEvent& event)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;
    MachineObject* m_obj = dev->get_my_machine(m_dev_id);

    if (IsShown() && m_obj && m_obj->stage_curr == 58) {
        update_thermal_remaining_time();
    } else {
        m_refresh_timer->Stop();
    }
}

}} // namespace Slic3r::GUI
