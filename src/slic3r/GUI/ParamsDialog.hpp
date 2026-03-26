// [INTENT] This header defines the interfaces for ParamsDialog and Filamentinformation classes.
// [INTENT] ParamsDialog is a modal dialog for editing filament parameters.

#ifndef slic3r_GUI_ParamsDialog_hpp_
#define slic3r_GUI_ParamsDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Custom event used to signal filament modification events.
wxDECLARE_EVENT(EVT_MODIFY_FILAMENT, SimpleEvent);

// [INTENT] Data structure holding information about the filament being edited.
class Filamentinformation : public wxObject
{
public:
    std::string filament_id;
    std::string filament_name;
};

class ParamsPanel;

// [INTENT] ParamsDialog is a modal dialog inheriting from DPIDialog,
// orchestrating the display and interaction with the ParamsPanel.
// [STATE] Manages the ID of the filament currently being edited (m_editing_filament_id).
class ParamsDialog : public DPIDialog
{
public:
    ParamsDialog(wxWindow* parent);

    // [INTENT] Accessor for the underlying panel.
    ParamsPanel* panel() { return m_panel; }

    // [INTENT] Displays the dialog in a modal manner.
    void Popup();

    // [INTENT] Sets the ID of the filament being modified.
    void set_editing_filament_id(std::string id) { m_editing_filament_id = id; }

protected:
    // [EVENT] Handles DPI scaling changes.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    std::string       m_editing_filament_id;   // [STATE] The ID of the filament being edited.
    ParamsPanel*      m_panel;                 // [STATE] Pointer to the panel managing the UI components.
    wxWindowDisabler* m_winDisabler = nullptr; // [STATE] Manages disabling of other windows while the dialog is active.
};

}} // namespace Slic3r::GUI

// [UNITY] ParamsDialog maps to a Unity UI Modal Dialog (Canvas + GraphicRaycaster).
// [UNITY] Filamentinformation maps to a C# ScriptableObject or POCO data structure for filament metadata.
// [PORTING_HAZARD:P2] wxWindowDisabler behavior requires mapping to Unity's modal window logic or blocking UI input to background elements.
// [PORTING_HAZARD:P3] DPIDialog base class requires custom DPI-aware scaling for all child UI elements in Unity.

#endif