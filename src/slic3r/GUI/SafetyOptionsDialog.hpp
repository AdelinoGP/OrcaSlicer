#ifndef slic3r_GUI_SafetyOptionsDialog_hpp_
#define slic3r_GUI_SafetyOptionsDialog_hpp_

#include <wx/wx.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/tipwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/ComboBox.hpp"

// Previous definitions
class SwitchBoard;

namespace Slic3r { namespace GUI {

// [INTENT] Modal safety settings dialog for printer-door and idle-heating protections.
// [STATE] Keeps a scrollable form, live checkboxes/labels, a switch board, and a non-owning MachineObject* snapshot in sync.
// [EVENT] update_options()/update_machine_obj() refresh the view from printer state, while Show() and on_dpi_changed() reflow the dialog
// lifecycle. [UNITY] Model this as a scrollable modal settings panel with a controller-backed printer state model and an in-panel transient
// toast/overlay. [PORTING_HAZARD:P2] The dialog mixes local UI toggles with live device capability checks and a wxPopupWindow + wxTimer
// feedback path.
class SafetyOptionsDialog : public DPIDialog
{
protected:
    // [STATE] Scroll container for the form sections; owns layout, not printer state.
    wxScrolledWindow* m_scrollwindow;

    // [STATE] Live controls mirroring printer safety toggles and explanatory text.
    CheckBox* m_cb_open_door;
    CheckBox* m_cb_idel_heating_protection;
    Label*    m_text_open_door;
    Label*    m_text_idel_heating_protection;
    Label*    m_text_idel_heating_protection_caption;
    // [STATE] SwitchBoard groups the open-door option; Unity likely needs a paired toggle row rather than a bespoke widget.
    SwitchBoard* m_open_door_switch_board;
    // [STATE] Optional container for the idle-heating section; nullable until the UI is assembled.
    wxPanel* m_idel_heating_container{nullptr};

    // [STATE] Transient warning toast shown when idle heating protection is unavailable.
    // [THREAD] Timer callback is UI-thread driven; do not treat it as background work.
    wxPopupWindow* m_idel_heating_toast{nullptr};
    wxTimer        m_idel_heating_toast_timer;
    // [STATE] Feature gate for the unavailable-state toast and checkbox behavior.
    bool m_idel_protect_unavailable{false};

    wxBoxSizer* create_settings_group(wxWindow* parent);
    // [STATE] Legacy print-halt flag; likely a modal result latch rather than persistent printer state.
    bool print_halt = false;

public:
    // [INTENT] Build and own the dialog chrome and its live printer-bound controls.
    SafetyOptionsDialog(wxWindow* parent);
    ~SafetyOptionsDialog();
    void on_dpi_changed(const wxRect& suggested_rect) override;

    // [STATE] Non-owning printer object pointer; the dialog reads and writes through it.
    MachineObject* obj{nullptr};

    // [EVENT] Refresh the visible toggles from a new printer object.
    void update_options(MachineObject* obj_);
    // [EVENT] Swap the backing machine object and resync dependent controls.
    void update_machine_obj(MachineObject* obj_);
    // [INTENT] Preserve modal semantics while allowing the dialog to re-show/hide and resync its state.
    bool Show(bool show) override;

private:
    // [INTENT] Synchronize the open-door toggle and any dependent explanatory text from printer state.
    void updateOpenDoorCheck(MachineObject* obj);
    // [INTENT] Synchronize idle-heating protection state, including unavailable-state messaging.
    void updateIdelHeatingProtect(MachineObject* obj);
    // [EVENT] Show the transient unavailable-state toast on the UI thread.
    void show_idel_heating_toast(const wxString& text);
};

}} // namespace Slic3r::GUI

#endif
