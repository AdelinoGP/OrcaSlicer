#pragma once

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

namespace Slic3r {

class MachineObject;
namespace GUI {

// [INTENT] Modal countdown dialog shown while a machine is thermally preconditioning; it stays on the UI thread and closes from
// [INTENT] the same interaction flow that confirms readiness.
// [STATE] Owns the selected device id, refresh timer, and the label/bitmap widgets that display the current countdown state.
// [EVENT] Constructor builds the dialog, the OK button dismisses it, and the wxTimer tick re-queries remaining time.
// [THREAD] The timer callback must remain on the UI thread; any DeviceManager polling implied by update_thermal_remaining_time()
// [THREAD] should not be moved off-thread without preserving dialog lifetime and close ordering.
// [UNITY] Map this to a modal overlay controller with a scheduled tick (eg. coroutine/InvokeRepeating) plus bound text fields.
// [PORTING_HAZARD:P2] The dialog assumes timer-driven state refresh and immediate close behavior, so Unity needs explicit lifetime
// [PORTING_HAZARD:P2] ownership for the countdown and dismiss actions.
class ThermalPreconditioningDialog : public wxDialog
{
public:
    // [INTENT] Seed the modal with the target device and the first formatted remaining-time string.
    ThermalPreconditioningDialog(wxWindow* parent, std::string dev_id, const wxString& remaining_time);
    ~ThermalPreconditioningDialog();

    // [STATE] Refreshes the displayed countdown text from the selected machine/device state.
    void update_thermal_remaining_time();

private:
    // [INTENT] Build the static layout and bind the dialog-local controls.
    void create_ui();
    // [EVENT] OK button handler that terminates the modal flow.
    void on_ok_clicked(wxCommandEvent& event);
    // [EVENT] Timer tick that keeps the countdown text in sync with the machine state.
    void on_timer(wxTimerEvent& event);

    // [STATE] Device id used to look up the machine whose preconditioning progress is shown.
    std::string m_dev_id;
    // [STATE] UI-thread timer that drives countdown refreshes while the modal is open.
    wxTimer* m_refresh_timer;
    // [STATE] Live remaining-time text shown in the body copy.
    wxStaticText* m_remaining_time_label;
    // [STATE] Explanatory helper text for the thermal-preconditioning wait state.
    wxStaticText* m_explanation_label;
    // [STATE] Confirm/dismiss control for the modal.
    wxButton* m_ok_button;
    // [STATE] Decorative title icon for the dialog header.
    wxStaticBitmap* m_title_bitmap;

    DECLARE_EVENT_TABLE()
};

} // namespace GUI
} // namespace Slic3r
