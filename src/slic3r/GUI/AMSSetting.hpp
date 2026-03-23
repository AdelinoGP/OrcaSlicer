#ifndef slic3r_AMSSettingDialog_hpp_
#define slic3r_AMSSettingDialog_hpp_

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"

#include "slic3r/GUI/DeviceCore/DevFilaAmsSetting.h"

#define AMS_SETTING_DEF_COLOUR wxColour(255, 255, 255)
#define AMS_SETTING_GREY800 wxColour(50, 58, 61)
#define AMS_SETTING_GREY700 wxColour(107, 107, 107)
#define AMS_SETTING_GREY200 wxColour(248, 248, 248)
#define AMS_SETTING_BODY_WIDTH FromDIP(380)
#define AMS_SETTING_BUTTON_SIZE wxSize(FromDIP(150), FromDIP(24))
#define AMS_F1_SUPPORT_INSERTION_UPDATE_DEFAULT std::string("00.00.07.89")
// [STATE] Palette constants anchor the AMS dialog theme so Unity must supply matching Color tokens when re-creating the window.

class AnimaIcon;
class ComboBox;
namespace Slic3r { namespace GUI {

class AMSSettingTypePanel;
// [UNITY] The UI Toolkit port should mirror this namespace with a dedicated VisualElement dialog bound to an AMS ScriptableObject settings
// asset. [PORTING_HAZARD:P2] Relies on `wxDPIDialog`/`wxCommandEvent` binding on the main thread; Unity must recreate the event flow via
// the Input System or UI Toolkit event bindings.
class AMSSetting : public DPIDialog
{
public:
    // [INTENT] Primary AMS dialog that maps MachineObject status into checkboxes, firmware labels, and intro art for the UI.
    // [STATE] Holds toggle caches tied to the last loaded MachineObject so Unity can keep the same set of bools in its ViewModel.
    AMSSetting(wxWindow*      parent,
               wxWindowID     id,
               const wxPoint& pos   = wxDefaultPosition,
               const wxSize&  size  = wxDefaultSize,
               long           style = wxDEFAULT_DIALOG_STYLE);
    ~AMSSetting();

public:
    // [EVENT] Called when the machine selection changes to rebind the toggles and image; Unity needs to hook the selection change event to
    // refresh this panel.
    void UpdateByObj(MachineObject* obj);

protected:
    void create();

    void update_ams_img(MachineObject* obj);
    void update_starting_read_mode(bool selected);
    void update_remain_mode(bool selected);
    void update_switch_filament(bool selected);
    void update_insert_material_read_mode(MachineObject* obj);
    void update_insert_material_read_mode(bool selected, std::string version);
    void update_air_printing_detection(MachineObject* obj);

    void update_firmware_switching_status();

    // event handlers
    // [PORTING_HAZARD:P2] Wx-specific `CheckBox` wiring and `wxCommandEvent` handlers assume millimeter-level DPI scaling; Unity must
    // handle DPI updates on its own layout pass. [EVENT] Bound to checkbox toggles, these run solely on the wxWidgets thread and drive the
    // DeviceCore state.
    void on_insert_material_read(wxCommandEvent& event);
    void on_starting_read(wxCommandEvent& event);
    void on_remain(wxCommandEvent& event);
    void on_switch_filament(wxCommandEvent& event);
    void on_air_print_detect(wxCommandEvent& event);
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    // [STATE] Tracks the current machine so updates can push settings back into DeviceCore.
    MachineObject* m_obj{nullptr};

    wxStaticText* m_static_ams_settings = nullptr;

    // [STATE] Prevents reentrant firmware updates when toggles trigger each other.
    bool                 m_switching = false;
    AMSSettingTypePanel* m_ams_type;
    // AMSSettingArrangeAMSOrder* m_ams_arrange_order;

    wxStaticBitmap* m_am_img;
    std::string     m_ams_img_name;

    // [UNITY] Each checkbox+label mirrors a VisualElement with Toggle and Inline Label so Unity can animate the same tips.
    wxPanel*      m_panel_body;
    wxPanel*      m_panel_Insert_material;
    CheckBox*     m_checkbox_Insert_material_auto_read;
    wxStaticText* m_title_Insert_material_auto_read;
    Label*        m_tip_Insert_material_line1;
    Label*        m_tip_Insert_material_line2;
    Label*        m_tip_Insert_material_line3;

    CheckBox*     m_checkbox_starting_auto_read;
    wxStaticText* m_title_starting_auto_read;
    Label*        m_tip_starting_line1;
    Label*        m_tip_starting_line2;

    CheckBox*     m_checkbox_remain;
    wxStaticText* m_title_remain;
    Label*        m_tip_remain_line1;

    CheckBox*     m_checkbox_switch_filament;
    wxStaticText* m_title_switch_filament;
    Label*        m_tip_switch_filament_line1;

    CheckBox*     m_checkbox_air_print;
    wxStaticText* m_title_air_print;
    Label*        m_tip_air_print_line;

    wxStaticText* m_tip_ams_img;
    // [UNITY] Automating demarcation requires wiring the Button to a Unity `Button` component with a `Command` for the `DeviceCore` call.
    Button* m_button_auto_demarcate;

    wxBoxSizer* m_sizer_Insert_material_tip_inline;
    wxBoxSizer* m_sizer_starting_tip_inline;
    wxBoxSizer* m_sizer_remain_inline;
    wxBoxSizer* m_sizer_switch_filament_inline;
    wxBoxSizer* m_sizer_remain_block;
};

class AMSSettingTypePanel : public wxPanel
{
public:
    // [INTENT] Presents firmware choices and switching tips for AMS systems inside the parent dialog.
    // [UNITY] Render it as a UI Toolkit VisualElement with a `PopupField` / `Button` combo and a `ScriptableObject` firmware list.
    AMSSettingTypePanel(wxWindow* parent, AMSSetting* setting_dlg);
    ~AMSSettingTypePanel();

public:
    // [STATE] Refreshes the firmware dropdown when the machine selection changes so the Unity binding can update its `ListView`.
    void Update(const MachineObject* obj);

private:
    void CreateGui();
    // [EVENT] The combo box change handler pushes an AMS firmware ID into DeviceCore; Unity should dispatch this via a main-thread callback.
    void OnAmsTypeChanged(wxCommandEvent& event);

private:
    // [THREAD] `std::weak_ptr` keeps a non-owning ref to the firmware switcher so the UI thread can check for active switches without
    // blocking. [PORTING_HAZARD:P3] Unity needs to guard these pointers because the underlying `DevAmsSystemFirmwareSwitch` is owned by
    // worker contexts.
    std::weak_ptr<DevAmsSystemFirmwareSwitch> m_ams_firmware_switch;

    // [STATE] Cached selection index + firmware map describe the dropdown state; Unity can mirror this with an `ObservableCollection`.
    int                                                                       m_ams_firmware_current_idx{-1};
    std::unordered_map<int, DevAmsSystemFirmwareSwitch::DevAmsSystemFirmware> m_ams_firmwares;

    // widgets
    AMSSetting* m_setting_dlg;
    ComboBox*   m_type_combobox;
    Label*      m_switching_tips;
    AnimaIcon*  m_switching_icon;
};

#if 0
class AMSSettingArrangeAMSOrder : public wxPanel
{
public:
    AMSSettingArrangeAMSOrder(wxWindow* parent);

public:
    void Update(const MachineObject* obj);
    void Rescale() { m_btn_rearrange->msw_rescale(); Layout(); };

private:
    void CreateGui();
    void OnBtnRearrangeClicked(wxCommandEvent& event);

private:
    std::weak_ptr<DevAmsSystemFirmwareSwitch> m_ams_firmware_switch;
    ScalableButton* m_btn_rearrange;
};
#endif

}} // namespace Slic3r::GUI

#endif
