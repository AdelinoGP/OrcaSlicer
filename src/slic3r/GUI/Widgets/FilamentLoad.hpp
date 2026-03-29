#ifndef slic3r_GUI_FILAMENTLOAD_hpp_
#define slic3r_GUI_FILAMENTLOAD_hpp_

#include "../wxExtensions.hpp"
#include "StaticBox.hpp"
#include "StepCtrl.hpp"
#include "AMSControl.hpp"
#include "../DeviceManager.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include <wx/simplebook.h>
#include <wx/hyperlink.h>
#include <wx/animate.h>
#include <wx/dynarray.h>

namespace Slic3r { namespace GUI {

// [INTENT] Retained filament-change wizard shell that swaps between load, unload, and VT-load step indicators without destroying the
// underlying book pages.
// [STATE] The control keeps three child step widgets plus AMS/slot identity and capability flags so the visible workflow can be rebuilt
// when the machine context changes.
// [UNITY] Port this as a state-driven wizard panel with retained subviews and a typed step dataset instead of page-index branching.
// [PORTING_HAZARD:P2] wxSimplebook selection is driven by implicit page numbers in the cpp, so the Unity port should make workflow states
// explicit rather than mirroring the current hidden page contract.
class FilamentLoad : public wxSimplebook
{
public:
    FilamentLoad(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);

protected:
    // [STATE] These child indicators are reused across workflow modes and should become retained child controllers in Unity rather than
    // transient pages.
    ::FilamentStepIndicator* m_filament_load_steps    = {nullptr};
    ::FilamentStepIndicator* m_filament_unload_steps  = {nullptr};
    ::FilamentStepIndicator* m_filament_vt_load_steps = {nullptr};
    // [STATE] Slot identity and extrusion mode feed the step-list rebuild and the label shown by each indicator.
    int  m_ams_id     = {1};
    int  m_slot_id    = {1};
    bool is_extrusion = false;

public:
    // [STATE] Public step labels and AMS capability enums seed the workflow dataset; `m_is_none_ams_mode` looks like a fallback-model
    // hook, but the cpp does not make its exact use explicit.
    std::map<FilamentStep, wxString> FILAMENT_CHANGE_STEP_STRING;
    AMSModel                         m_ams_model{AMSModel::GENERIC_AMS};
    AMSModel                         m_ext_model{AMSModel::AMS_LITE};
    AMSModel                         m_is_none_ams_mode{AMSModel::AMS_LITE};

    // [INTENT] Change the active machine capability set so subsequent step rebuilds reflect the current AMS/extruder pairing.
    void SetAmsModel(AMSModel mode, AMSModel ext_mode)
    {
        m_ams_model = mode;
        m_ext_model = ext_mode;
    };

    // [EVENT] External workflow code advances the visible step and page selection through this seam.
    void SetFilamentStep(FilamentStep item_idx, FilamentStepType f_type);
    // [INTENT] Update the user-facing tip/banner for AMS presence and the current filament-change context.
    void ShowFilamentTip(bool hasams = true);

    // [INTENT] Regenerate the step lists from capability flags and the current filament-change scenario.
    void SetupSteps(bool is_extrusion_exist);

    // [STATE] This legacy switch clears the retained step content for a no-filament mode without tearing down the book.
    void show_nofilament_mode(bool show);
    // [STATE] Keep the active AMS/slot identity in sync with the host workflow.
    void updateID(int ams_id, int slot_id)
    {
        m_ams_id  = ams_id;
        m_slot_id = slot_id;
    };
    // [STATE] Extrusion mode changes which step dataset is shown.
    void SetExt(bool ext) { is_extrusion = ext; };

    // [STATE] Size/color fan-out helpers keep all retained child indicators visually consistent with the parent container.
    void set_min_size(const wxSize& minSize);
    void set_max_size(const wxSize& maxSize);
    void set_background_color(const wxColour& colour);
};

}} // namespace Slic3r::GUI

#endif // !slic3r_GUI_filamentload_hpp_
