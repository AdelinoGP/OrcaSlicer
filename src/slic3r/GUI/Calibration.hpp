#ifndef slic3r_GUI_Calibration_hpp_
#define slic3r_GUI_Calibration_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Plater.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StepCtrl.hpp"
#include "Widgets/CheckBox.hpp"


// [INTENT]
// CalibrationDialog provides a modal interface for selecting and executing 
// various printer calibration procedures (e.g., leveling, vibration compensation).
// It tracks selection state and reflects real-time progress from the MachineObject.
namespace Slic3r { namespace GUI {

class CalibrationDialog : public DPIDialog
{
private:
    // [STATE] map of parameter keys to checkbox widgets for tracking selected calibration steps.
    std::map<std::string, ::CheckBox*> m_checkbox_list;

    // [STATE] pointers to the container windows for each calibration option.
    wxWindow* select_xcam_cali { nullptr };
    wxWindow* select_bed_leveling { nullptr };
    wxWindow* select_vibration { nullptr };
    wxWindow* select_motor_noise { nullptr };
    wxWindow* select_nozzle_cali{ nullptr };
    wxWindow* select_heatbed_cali{ nullptr };
    wxWindow* select_clumppos_cali{ nullptr };
    // [INTENT] helper to create a labeled checkbox option row.
    wxWindow* create_check_option(wxString title, wxWindow *parent, wxString tooltip, std::string param);

public:
    // [UNITY] DPIDialog maps to a Canvas-based modal prefab in Unity.
    CalibrationDialog(Plater *plater = nullptr);
    ~CalibrationDialog();
    // [EVENT] standard DPI change override.
    void on_dpi_changed(const wxRect &suggested_rect) override;

    // [STATE] StepIndicator shows current progress of the calibration workflow.
    StepIndicator *m_calibration_flow;
    // [STATE] Button for triggering or reflecting the final state of calibration.
    Button *       m_calibration_btn;
    // [STATE] Reference to the printer/machine object being calibrated.
    MachineObject *m_obj = nullptr;

    // [STATE] cache of the last known stage list to optimize UI updates.
    std::vector<int> last_stage_list_info; 
    int              m_state{0};
    // [INTENT] refreshes the dialogs UI elements based on the current MachineObject state.
    // [UNITY] Use a reactive data binding or a Refresh() call on the MonoBehaviour.
    void             update_cali(MachineObject *obj);
    // [INTENT] checks if the list of calibration stages has changed on the machine.
    bool             is_stage_list_info_changed(MachineObject *obj);
    // [EVENT] handles the start calibration command dispatch.
    void             on_start_calibration(wxMouseEvent &event);
    // [INTENT] sets the active machine object for this dialog.
    void             update_machine_obj(MachineObject *obj);
    // [EVENT] standard window show override with dark mode theme update.
    bool             Show(bool show) override;
};

}} // namespace Slic3r::GUI

#endif
