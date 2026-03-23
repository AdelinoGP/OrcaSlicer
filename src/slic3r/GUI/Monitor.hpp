#ifndef slic3r_Monitor_hpp_
#define slic3r_Monitor_hpp_

#include "Tabbook.hpp"
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/grid.h>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/statline.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/gbsizer.h>
#include <wx/statbox.h>
#include <wx/tglbtn.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/webrequest.h>
#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/MonitorBasePanel.h"
#include "slic3r/GUI/StatusPanel.hpp"
#include "slic3r/GUI/UpgradePanel.hpp"
#include "slic3r/GUI/HMSPanel.hpp"
#include "slic3r/GUI/AmsWidgets.hpp"
#include "Widgets/SideTools.hpp"
#include "SelectMachinePop.hpp"

namespace Slic3r { namespace GUI {

class MediaFilePanel;

class AddMachinePanel : public wxPanel
{
public:
    // [INTENT] Provide a placeholder that prompts users to register a machine when none exist.
protected:
    Button*         m_button_add_machine;
    wxStaticText*   m_staticText_add_machine;
    wxStaticBitmap* m_bitmap_empty;

    // [EVENT] Activated by the add button; forwards the request to DeviceManager to create a new MachineObject.
    void on_add_machine(wxCommandEvent& event);

public:
    AddMachinePanel(wxWindow*       parent,
                    wxWindowID      id    = wxID_ANY,
                    const wxPoint&  pos   = wxDefaultPosition,
                    const wxSize&   size  = wxDefaultSize,
                    long            style = wxTAB_TRAVERSAL,
                    const wxString& name  = wxEmptyString);
    ~AddMachinePanel();

    void msw_rescale();
};

class MonitorPanel : public wxPanel
{
    // [INTENT] Host the multi-tab machine status surface (status/media/update/HMS) while keeping shared controls in sync.
private:
    // [STATE] Tabbook orchestrates which of the five PrinterTab panes is visible and shares resize data with the sizer.
    // [UNITY] Replace with an UI Toolkit TabView + VisualElement containers that mirror the PrinterTab index.
    Tabbook* m_tabpanel{nullptr};
    wxSizer* m_main_sizer{nullptr};

    AddMachinePanel* m_status_add_machine_panel;
    StatusPanel*     m_status_info_panel;
    MediaFilePanel*  m_media_file_panel;
    UpgradePanel*    m_upgrade_panel;
    HMSPanel*        m_hms_panel;

    /* side tools */
    SideTools*      m_side_tools{nullptr};
    wxStaticBitmap* m_bitmap_printer_type;
    wxStaticBitmap* m_bitmap_arrow;
    wxStaticText*   m_staticText_printer_name;
    wxStaticBitmap* m_bitmap_wifi_signal;
    wxBoxSizer*     m_side_tools_sizer;
    // [STATE] The popup tracks the machine selection state and lifecycle so the UI never loses focus after a switch.
    // [PORTING_HAZARD:P3] Unity will need an explicit `PopupWindow` controller instead of wxWidgets built-in transient behavior.
    SelectMachinePopup m_select_machine;

    /* images */
    // [STATE] Cached bitmaps enable ping-tone updates without reloading disk assets; mimic via Unity Sprite Atlas.
    wxBitmap m_signal_strong_img;
    wxBitmap m_signal_middle_img;
    wxBitmap m_signal_weak_img;
    wxBitmap m_signal_no_img;
    wxBitmap m_printer_img;
    wxBitmap m_arrow_img;

    int      last_wifi_signal = -1;     // [STATE] Last signal strength so we only animate when the level transitions.
    int      last_status;               // [STATE] Holds the previously shown status code used by show_status() to gate notifications.
    bool     m_initialized{false};      // [STATE] Guards multi-stage init_bitmap/init_tabpanel calls from running twice.
    bool     update_flag{false};        // [STATE] Gate that external callers toggle to pause/resume the timer without destroying it.
    wxTimer* m_refresh_timer = nullptr; // [THREAD] wxTimer events drop back onto the UI thread; match with Unity's Dispatcher/Coroutine.
    // [PORTING_HAZARD:P2] wxTimer implicitly ties the refresh cadence to the event loop so Unity must explicitly schedule main-thread
    // coroutines when paused.

public:
    MonitorPanel(wxWindow*      parent,
                 wxWindowID     id    = wxID_ANY,
                 const wxPoint& pos   = wxDefaultPosition,
                 const wxSize&  size  = wxDefaultSize,
                 long           style = wxTAB_TRAVERSAL);
    ~MonitorPanel();

    // [STATE] Enumerates the fixed tab indices that drive status versus media panes and new HMS/updating drills.
    enum PrinterTab { PT_STATUS = 0, PT_MEDIA = 1, PT_UPDATE = 2, PT_HMS = 3, PT_DEBUG = 4, PT_MAX_NUM = 5 };

    // [INTENT] Prepare caches and start the per-machine refresh pipeline in a controlled order.
    void      init_bitmap();
    void      init_timer();
    void      init_tabpanel();
    Tabbook*  get_tabpanel() { return m_tabpanel; };
    void      set_default();
    wxWindow* create_side_tools();

    // [EVENT] Hooked from wxSystemSettings to repaint the wifi/status colors when the OS palette changes.
    void on_sys_color_changed();
    // [INTENT] Mirror DPI/font rescaling inherited from wxFrame; matches msw dialogs in GUI_App.
    void msw_rescale();

    StatusPanel* get_status_panel() { return m_status_info_panel; };
    // [EVENT] Called from the popup to swap the active machine; keeps the monitor in sync with global DeviceManager state.
    void select_machine(std::string machine_sn);
    // [EVENT] Timer/refresh loop that polls machine metadata and pushes updates into all child panes.
    void on_timer(wxTimerEvent& event);
    void on_select_printer(wxCommandEvent& event);
    // [EVENT] Mouse click on the printer glyph mimics the 'SelectMachine' pop-up trigger.
    void on_printer_clicked(wxMouseEvent& event);
    void on_size(wxSizeEvent& event);

    /* update apis */
    // [INTENT] Lift all child panels when new telemetry arrives; ties back into DeviceManager refresh flows.
    // void update_ams(MachineObject* obj);
    void update_all();

    // [EVENT] Triggered after state change to push HMS badges and network notes to the HMS tab.
    void update_hms_tag();
    // [INTENT] Mirror wxWindow::Show so that toggling visibility also starts/stops refresh_timer.
    bool Show(bool show);

    // [EVENT] Central status dispatcher; uses `last_status` to avoid duplicate UI chatter.
    void show_status(int status);

    std::string get_string_from_tab(PrinterTab tab);

    MachineObject* obj{nullptr}; // [STATE] currently focused machine; feed this into the MonitorModel scriptable asset.
    std::string    last_conn_type =
        "undedefined"; // [STATE]/[UNITY] Drives the connection badge text; map to a ScriptableObject field for Unity UI updates.

    void stop_update() { update_flag = false; };
    void start_update() { update_flag = true; };

    // [EVENT] Navigation helpers exposed to external controls (e.g., status toolbar) so they can focus the desired tab.
    void jump_to_HMS();
    void jump_to_LiveView();
    // [INTENT]/[UNITY] Keep the footer in sync with network/version data; Unity should update a TextMeshPro label on the same event pull.
    void update_network_version_footer();
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
