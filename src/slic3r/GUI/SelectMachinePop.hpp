#ifndef slic3r_GUI_SelectMachinePop_hpp_
#define slic3r_GUI_SelectMachinePop_hpp_

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
#include <wx/srchctrl.h>

#include "ReleaseNote.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Plater.hpp"
#include "BBLStatusBar.hpp"
#include "BBLStatusBarSend.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "Widgets/PopupWindow.hpp"
#include <wx/simplebook.h>
#include <wx/hashmap.h>

namespace Slic3r { namespace GUI {

// [INTENT] This header declares the send-print popup boundary: a floating device picker,
// row-level printer status widgets, and the rename/bind affordances that feed the rest of the
// network-print flow.
// [UNITY] Port as a non-modal floating controller with recycled row views and explicit
// focus-loss dismissal instead of wxPopupWindow + manual mouse forwarding.
// [PORTING_HAZARD:P2] The popup is tightly coupled to custom-painted rows, timer refreshes,
// and cross-widget command events, so the Unity version needs a clearer controller/view split.

enum PrinterState { OFFLINE, IDLE, BUSY, LOCK, IN_LAN };

enum PrinterBindState { NONE, ALLOW_BIND, ALLOW_UNBIND };

wxDECLARE_EVENT(EVT_FINISHED_UPDATE_MACHINE_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_WILL_DISMISS_MACHINE_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_WINDOWS_POSITION, wxCommandEvent);
wxDECLARE_EVENT(EVT_DISSMISS_MACHINE_LIST, wxCommandEvent);
wxDECLARE_EVENT(EVT_CONNECT_LAN_PRINT, wxCommandEvent);
wxDECLARE_EVENT(EVT_EDIT_PRINT_NAME, wxCommandEvent);
wxDECLARE_EVENT(EVT_UNBIND_MACHINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_BIND_MACHINE, wxCommandEvent);

// [STATE] These macros define the popup's fixed DPI-scaled footprint and status colors used by
// the custom-painted rows below.
// [UNITY] Use shared theme constants or ScriptableObject color tokens rather than hard-coded
// wxColour macros.
#define SELECT_MACHINE_POPUP_SIZE wxSize(FromDIP(216), FromDIP(364))
#define SELECT_MACHINE_LIST_SIZE wxSize(FromDIP(212), FromDIP(360))
#define SELECT_MACHINE_ITEM_SIZE wxSize(FromDIP(190), FromDIP(35))
#define SELECT_MACHINE_GREY900 wxColour(38, 46, 48)
#define SELECT_MACHINE_GREY600 wxColour(144, 144, 144)
#define SELECT_MACHINE_GREY400 wxColour(206, 206, 206)
#define SELECT_MACHINE_BRAND wxColour(0, 150, 136)
#define SELECT_MACHINE_REMIND wxColour(255, 111, 0)
#define SELECT_MACHINE_LIGHT_GREEN wxColour(219, 253, 231)

class MachineObjectPanel : public wxPanel
{
private:
    // [STATE] Row-local hover and visibility flags drive manual painting and shortcut affordances.
    bool m_is_my_devices{false};
    bool m_show_edit{false};
    bool m_show_bind{false};
    bool m_hover{false};
    bool m_is_macos_special_version{false};

    PrinterBindState m_bind_state;
    PrinterState     m_state;

    ScalableBitmap m_unbind_img;
    ScalableBitmap m_edit_name_img;
    ScalableBitmap m_select_unbind_img;

    ScalableBitmap m_printer_status_offline;
    ScalableBitmap m_printer_status_busy;
    ScalableBitmap m_printer_status_idle;
    ScalableBitmap m_printer_status_lock;
    ScalableBitmap m_printer_in_lan;

    // [STATE] Non-owning pointer to the printer model backing the rendered row.
    MachineObject* m_info;

protected:
    wxStaticBitmap* m_bitmap_info;
    wxStaticBitmap* m_bitmap_bind;

public:
    // [INTENT] Render and interact with a single printer row inside the popup list.
    // [UNITY] Replace with a pooled list item prefab or UI Toolkit VisualElement that binds a
    // printer DTO and a small set of command buttons.
    MachineObjectPanel(wxWindow*       parent,
                       wxWindowID      id    = wxID_ANY,
                       const wxPoint&  pos   = wxDefaultPosition,
                       const wxSize&   size  = wxDefaultSize,
                       long            style = wxTAB_TRAVERSAL,
                       const wxString& name  = wxEmptyString);

    ~MachineObjectPanel();

    void show_bind_dialog();
    void set_printer_state(PrinterState state);
    void show_printer_bind(bool show, PrinterBindState state);
    void show_edit_printer_name(bool show);
    void update_machine_info(MachineObject* info, bool is_my_devices = false);

protected:
    void OnPaint(wxPaintEvent& event);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
    void on_mouse_enter(wxMouseEvent& evt);
    void on_mouse_leave(wxMouseEvent& evt);
    void on_mouse_left_up(wxMouseEvent& evt);
};

class MachinePanel
{
public:
    wxString            mIndex;
    MachineObjectPanel* mPanel;
};

class PinCodePanel : public wxPanel
{
public:
    // [INTENT] Provide the two shortcut rows used to connect via PIN code or direct connection.
    // [UNITY] Model as button-like list entries with icon + label rather than a painted panel.
    PinCodePanel(wxWindow*      parent,
                 int            type,
                 wxWindowID     winid = wxID_ANY,
                 const wxPoint& pos   = wxDefaultPosition,
                 const wxSize&  size  = wxDefaultSize);
    ~PinCodePanel() {};

    ScalableBitmap m_bitmap;
    bool           m_hover{false};
    int            m_type{0};

    void OnPaint(wxPaintEvent& event);
    void render(wxDC& dc);
    void doRender(wxDC& dc);

    void on_mouse_enter(wxMouseEvent& evt);
    void on_mouse_leave(wxMouseEvent& evt);
    void on_mouse_left_up(wxMouseEvent& evt);
};

class SelectMachinePopup : public PopupWindow
{
public:
    // [INTENT] Own the popup shell, live device lists, search filter, and refresh lifecycle.
    // [THREAD] Owns a worker thread pointer for print-info fetches, so teardown must respect the
    // UI/background boundary and avoid dispatching into a destroyed popup.
    // [UNITY] Replace with a controller that owns a retained filtered collection and marshals
    // background refresh results back onto the main thread.
    SelectMachinePopup(wxWindow* parent);
    ~SelectMachinePopup();

    // PopupWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow* focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

    void update_machine_list(wxCommandEvent& event);
    void start_ssdp(bool on_off);
    bool was_dismiss() { return m_dismiss; }

private:
    // [STATE] The popup maintains separate counts and row pools for my devices vs. other devices,
    // plus a dismissal latch and the current print-info payload.
    int                        m_my_devices_count{0};
    int                        m_other_devices_count{0};
    PinCodePanel*              m_panel_ping_code{nullptr};
    PinCodePanel*              m_panel_direct_connection{nullptr};
    wxWindow*                  m_placeholder_panel{nullptr};
    HyperLink*                 m_hyperlink{nullptr}; // ORCA
    Label*                     m_ping_code_text{nullptr};
    wxStaticBitmap*            m_img_ping_code{nullptr};
    wxBoxSizer*                m_sizer_body{nullptr};
    wxBoxSizer*                m_sizer_my_devices{nullptr};
    wxBoxSizer*                m_sizer_other_devices{nullptr};
    wxBoxSizer*                m_sizer_search_bar{nullptr};
    wxSearchCtrl*              m_search_bar{nullptr};
    wxScrolledWindow*          m_scrolledWindow{nullptr};
    wxWindow*                  m_panel_body{nullptr};
    wxTimer*                   m_refresh_timer{nullptr};
    std::vector<MachinePanel*> m_user_list_machine_panel;
    std::vector<MachinePanel*> m_other_list_machine_panel;
    boost::thread*             get_print_info_thread{nullptr};
    std::shared_ptr<int>       m_token      = std::make_shared<int>(0);
    std::string                m_print_info = "";
    bool                       m_dismiss{false};

    // [STATE] These maps mirror the bound/free printer lists that the popup reuses when sorting
    // and filtering rows.
    std::map<std::string, MachineObject*> m_bind_machine_list;
    std::map<std::string, MachineObject*> m_free_machine_list;

private:
    // [EVENT] Manual left-click handling and timer refreshes drive the popup's custom dismissal
    // and refresh behavior instead of standard list control events.
    void OnLeftUp(wxMouseEvent& event);
    void on_timer(wxTimerEvent& event);

    void      update_other_devices();
    void      update_user_devices();
    bool      search_for_printer(MachineObject* obj);
    void      on_dissmiss_win(wxCommandEvent& event);
    wxWindow* create_title_panel(wxString text);
};

class EditDevNameDialog : public DPIDialog
{
public:
    // [INTENT] Modal rename dialog for a single printer/device entry.
    // [UNITY] Port as a simple dialog controller with a text input, validation state, and confirm
    // button enabled only when the edited name passes the same checks.
    EditDevNameDialog(Plater* plater = nullptr);
    ~EditDevNameDialog();

    void set_machine_obj(MachineObject* obj);
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_edit_name(wxCommandEvent& e);

    Button*        m_button_confirm{nullptr};
    TextInput*     m_textCtr{nullptr};
    wxStaticText*  m_static_valid{nullptr};
    MachineObject* m_info{nullptr};
};

}} // namespace Slic3r::GUI

#endif
