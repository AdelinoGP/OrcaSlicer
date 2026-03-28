#ifndef slic3r_UpgradePanel_hpp_
#define slic3r_UpgradePanel_hpp_

#include <wx/panel.h>
#include <slic3r/GUI/Widgets/Button.hpp>
#include "Widgets/ProgressBar.hpp"
#include <slic3r/GUI/DeviceManager.hpp>
#include <slic3r/GUI/Widgets/ScrolledWindow.hpp>
#include <slic3r/GUI/StatusPanel.hpp>
#include "ReleaseNote.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] These panel classes assemble the firmware-upgrade dashboard: one root scroller, one machine card, and reusable accessory rows.
// [UNITY] Port this as a scrollable retained view with reusable row prefabs plus modal confirmation overlays.
// [PORTING_HAZARD:P2] The UI is built from many optional wx panels that are created, hidden, and destroyed dynamically as device
// capabilities change.
class uiDeviceUpdateVersion;

// [INTENT] Compact accessory card for extension-board firmware/status details.
// [STATE] Owns the visible text fields and new-version badge bitmap for one accessory row.
// [UNITY] Map this to a reusable row prefab bound to a single accessory model.
class ExtensionPanel : public wxPanel
{
public:
    wxStaticText*   m_staticText_ext;
    wxStaticText*   m_staticText_ext_val;
    wxStaticText*   m_staticText_ext_ver;
    wxStaticText*   m_staticText_ext_ver_val;
    wxStaticText*   m_staticText_ext_sn_val;
    ScalableBitmap  upgrade_green_icon;
    wxStaticBitmap* m_ext_new_version_img;

    ExtensionPanel(wxWindow*       parent,
                   wxWindowID      id    = wxID_ANY,
                   const wxPoint&  pos   = wxDefaultPosition,
                   const wxSize&   size  = wxDefaultSize,
                   long            style = wxTAB_TRAVERSAL,
                   const wxString& name  = wxEmptyString);
    ~ExtensionPanel();
    void msw_rescale();
};

// [INTENT] Compact accessory card for AMS and AMS-Hub firmware/status details.
// [STATE] Owns the label fields, beta/new-version indicator, and row bitmap for one AMS-like device.
// [UNITY] Map this to a reusable list item prefab with a bound version/status view-model.
class AmsPanel : public wxPanel
{
public:
    wxStaticText*   m_staticText_ams_model_id;
    wxStaticText*   m_staticText_ams;
    wxStaticText*   m_staticText_ams_sn_val;
    wxStaticText*   m_staticText_ams_ver_val;
    wxStaticText*   m_staticText_beta_version;
    wxStaticBitmap* m_ams_new_version_img;
    ScalableBitmap  upgrade_green_icon;

    AmsPanel(wxWindow*       parent,
             wxWindowID      id    = wxID_ANY,
             const wxPoint&  pos   = wxDefaultPosition,
             const wxSize&   size  = wxDefaultSize,
             long            style = wxTAB_TRAVERSAL,
             const wxString& name  = wxEmptyString);
    ~AmsPanel();

    void msw_rescale();
};

// [INTENT] Specialization of the AMS card for extra AMS hardware variants.
// [UNITY] Same prefab as AmsPanel, but instantiated from a different discovery path.
class ExtraAmsPanel : public AmsPanel
{
public:
    ExtraAmsPanel(wxWindow*       parent,
                  wxWindowID      id    = wxID_ANY,
                  const wxPoint&  pos   = wxDefaultPosition,
                  const wxSize&   size  = wxDefaultSize,
                  long            style = wxTAB_TRAVERSAL,
                  const wxString& name  = wxEmptyString);
};

WX_DEFINE_ARRAY(AmsPanel*, AmsPanelHash);
// [INTENT] Build and update the full machine-upgrade card for one selected printer.
// [STATE] Holds the live machine snapshot, module/version payloads, visibility toggles, and upgrade-confirm dialogs.
// [EVENT] Owns upgrade/release-note handlers plus a set of show/hide helpers that keep paired rows in sync.
// [UNITY] Port as a retained dashboard controller with child row views and explicit state transitions for upgrade progress.
// [PORTING_HAZARD:P2] The card mixes data shaping, dynamic subpanel creation, and command dispatch into one widget tree.
class MachineInfoPanel : public wxPanel
{
protected:
    wxPanel*        m_panel_caption;
    wxStaticBitmap* m_upgrade_status_img;
    wxStaticText*   m_caption_text;
    wxStaticBitmap* m_printer_img;
    wxStaticText*   m_staticText_model_id;
    wxStaticText*   m_staticText_model_id_val;
    wxStaticText*   m_staticText_sn;
    wxStaticText*   m_staticText_sn_val;
    wxStaticBitmap* m_ota_new_version_img;
    wxStaticText*   m_staticText_ver;
    wxStaticText*   m_staticText_ver_val;
    wxStaticText*   m_staticText_beta_version;
    wxStaticLine*   m_staticline;
    wxStaticBitmap* m_ams_img;
    AmsPanel*       m_ahb_panel;
    wxStaticLine*   m_staticline2;
    ExtraAmsPanel*  m_extra_ams_panel;
    wxStaticBitmap* m_extra_ams_img;
    wxStaticBitmap* m_ext_img;
    ExtensionPanel* m_ext_panel;

    wxFlexGridSizer* m_ams_info_sizer;

    /* ams info */
    bool        m_last_ams_show = true;
    wxBoxSizer* m_ams_sizer;

    /* extension info */
    bool        m_last_ext_show = true;
    wxBoxSizer* m_ext_sizer;

    /* extra_ams info */
    bool        m_last_extra_ams_show = true;
    wxBoxSizer* m_extra_ams_sizer;

    /* air_pump info*/
    wxBoxSizer*            m_air_pump_sizer      = nullptr;
    wxStaticBitmap*        m_air_pump_img        = nullptr;
    wxStaticLine*          m_air_pump_line_above = nullptr;
    uiDeviceUpdateVersion* m_air_pump_version    = nullptr;

    /* cutting module info*/
    wxBoxSizer*            m_cutting_sizer      = nullptr;
    wxStaticBitmap*        m_cutting_img        = nullptr;
    wxStaticLine*          m_cutting_line_above = nullptr;
    uiDeviceUpdateVersion* m_cutting_version    = nullptr;

    /* laser info*/
    wxBoxSizer*            m_laser_sizer      = nullptr;
    wxStaticBitmap*        m_lazer_img        = nullptr;
    wxStaticLine*          m_laser_line_above = nullptr;
    uiDeviceUpdateVersion* m_laser_version    = nullptr;

    /* fire extinguish*/
    wxBoxSizer*            m_extinguish_sizer      = nullptr;
    wxStaticBitmap*        m_extinguish_img        = nullptr;
    wxStaticLine*          m_extinguish_line_above = nullptr;
    uiDeviceUpdateVersion* m_extinguish_version    = nullptr;

    // [STATE] Upgrade progress area shown while firmware is updating or after completion.
    // [UNITY] This becomes a right-rail status block with a progress bar, result text, and retry affordance.
    wxBoxSizer*     m_upgrading_sizer;
    wxStaticText*   m_staticText_upgrading_info;
    ProgressBar*    m_upgrade_progress;
    wxStaticText*   m_staticText_upgrading_percent;
    wxStaticBitmap* m_upgrade_retry_img;
    wxStaticText*   m_staticText_release_note;
    Button*         m_button_upgrade_firmware;

    // [STATE] Helper-created accessory rows and cached bitmaps reused across refreshes.
    wxPanel*                    create_caption_panel(wxWindow* parent);
    AmsPanelHash                m_amspanel_list;
    std::vector<ExtraAmsPanel*> m_extra_ams_panel_list;

    ScalableBitmap m_img_ext;
    ScalableBitmap m_img_monitor_ams;
    ScalableBitmap m_img_extra_ams;
    ScalableBitmap m_img_printer;
    ScalableBitmap m_img_air_pump;
    ScalableBitmap m_img_cutting;
    ScalableBitmap m_img_laser;
    ScalableBitmap m_img_extinguish;
    ScalableBitmap upgrade_gray_icon;
    ScalableBitmap upgrade_green_icon;
    ScalableBitmap upgrade_yellow_icon;
    int            last_status     = -1;
    std::string    last_status_str = "";

    std::string m_last_laser_product_name = "";

    SecondaryCheckDialog* confirm_dlg = nullptr;

    // [EVENT] Command dispatch helpers for firmware upgrade, release-note display, and confirmation flows.
    void upgrade_firmware_internal();
    void on_show_release_note(wxMouseEvent& event);
    void confirm_upgrade(MachineObject* obj = nullptr);

public:
    MachineInfoPanel(wxWindow*       parent,
                     wxWindowID      id    = wxID_ANY,
                     const wxPoint&  pos   = wxDefaultPosition,
                     const wxSize&   size  = wxDefaultSize,
                     long            style = wxTAB_TRAVERSAL,
                     const wxString& name  = wxEmptyString);
    ~MachineInfoPanel();

    void on_sys_color_changed();
    void update_printer_imgs(MachineObject* obj);
    void init_bitmaps();
    void rescale_bitmaps();

    Button* get_btn() { return m_button_upgrade_firmware; }

    void msw_rescale();
    void update(MachineObject* obj);
    void update_version_text(MachineObject* obj);
    void update_ams_ext(MachineObject* obj);
    void show_status(int status, std::string upgrade_status_str = "");
    void show_ams(bool show = false, bool force_update = false);
    void show_ext(bool show = false, bool force_update = false);
    void show_extra_ams(bool show = false, bool force_update = false);

    void on_upgrade_firmware(wxCommandEvent& event);
    void on_consisitency_upgrade_firmware(wxCommandEvent& event);

    // [STATE] Current machine snapshot and firmware payloads that drive the rendered card.
    MachineObject* m_obj{nullptr};
    FirmwareInfo   m_ota_info;
    FirmwareInfo   m_ams_info;

    bool is_upgrading = false;

    // [STATE] Current upgrade-mode variant for the confirm button/command path.
    enum PanelType {
        ptUndef,
        ptPushPanel,
        ptOtaPanel,
        ptAmsPanel,
    } panel_type;

private:
    void createAirPumpWidgets(wxBoxSizer* main_left_sizer);
    void createCuttingWidgets(wxBoxSizer* main_left_sizer);
    void createLaserWidgets(wxBoxSizer* main_left_sizer);
    void createExtinguishWidgets(wxBoxSizer* main_left_sizer);

    void update_air_pump(MachineObject* obj);
    void update_cut(MachineObject* obj);
    void update_laszer(MachineObject* obj);
    void update_extinguish(MachineObject* obj);

    void show_air_pump(bool show = true);
    void show_cut(bool show = true);
    void show_laszer(bool show = true);
    void show_extinguish(bool show = true);
};

// enum UpgradeMode {
//     umPushUpgrading,
//     umSelectOtaVerUpgrading,
//     umSelectAmsVerUpgrading,
// };
// static UpgradeMode upgrade_mode;

// [INTENT] Root container for the upgrade screen: it hosts the scrollable list of machine cards.
// [STATE] Tracks the active machine card, delayed refresh state, and force/consistency hint dialogs.
// [UNITY] Port as a scroll view with a single lazily-created machine card prefab and modal prompt layer.
// [PORTING_HAZARD:P3] The root panel mixes data refresh timing with dialog gating, so the Unity controller needs explicit state flags.
class UpgradePanel : public wxPanel
{
protected:
    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer*       m_machine_list_sizer;
    MachineInfoPanel* m_push_upgrade_panel{nullptr};

    // [STATE] Debug-only firmware selector gate retained from the wx UI; Unity should not expose it in release builds.
    bool enable_select_firmware = false;
    bool m_need_update          = false;
    // [STATE] Hint-dialog suppression flags that avoid repeating force/consistency prompts.
    DevFirmwareUpgradingState last_forced_hint_status      = DevFirmwareUpgradingState::DC;
    DevFirmwareUpgradingState last_consistency_hint_status = DevFirmwareUpgradingState::DC;
    int                       last_status;
    bool                      m_show_forced_hint      = true;
    bool                      m_show_consistency_hint = true;
    SecondaryCheckDialog*     force_dlg{nullptr};
    SecondaryCheckDialog*     consistency_dlg{nullptr};

public:
    UpgradePanel(wxWindow*      parent,
                 wxWindowID     id    = wxID_ANY,
                 const wxPoint& pos   = wxDefaultPosition,
                 const wxSize&  size  = wxDefaultSize,
                 long           style = wxTAB_TRAVERSAL);
    ~UpgradePanel();
    void clean_push_upgrade_panel();
    void msw_rescale();
    bool Show(bool show = true) override;

    void refresh_version_and_firmware(MachineObject* obj);
    void update(MachineObject* obj);
    void show_status(int status);
    void on_sys_color_changed();

    MachineObject* m_obj{nullptr};
};

}} // namespace Slic3r::GUI

#endif
