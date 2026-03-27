#ifndef slic3r_GUI_ReleaseNote_hpp_
#define slic3r_GUI_ReleaseNote_hpp_

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
#include <wx/event.h>
#include <wx/hyperlink.h>
#include <wx/richtext/richtextctrl.h>

#include "AmsMappingPopup.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include <wx/hashmap.h>
#include <wx/webview.h>

#include "Jobs/Worker.hpp"

namespace Slic3r { namespace GUI {

// [EVENT] Custom wxCommandEvent hooks let the dialog family route confirm/cancel/retry/update actions back to their owners.
// [UNITY] Model these as modal controllers with explicit button callbacks rather than relying on wx event bubbling.
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_RETRY, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_SECONDARY_CHECK_RESUME, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_NOZZLE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_TEXT_MSG, wxCommandEvent);
wxDECLARE_EVENT(EVT_ERROR_DIALOG_BTN_CLICKED, wxCommandEvent);

// [INTENT] Release note modal that renders update text for the current printer/plater context.
// [STATE] Keeps the short summary label plus a scrollable release-note surface; the owned widgets are null-initialized and built at runtime.
// [UNITY] UI Toolkit modal panel with a rich-text/scroll view region and a controller-backed DPI/layout hook.
// [PORTING_HAZARD:P3] The wxScrolledWindow-based note surface and dynamic DPI relayout need a deliberate layout port.
class ReleaseNoteDialog : public DPIDialog
{
public:
    ReleaseNoteDialog(Plater* plater = nullptr);
    ~ReleaseNoteDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_release_note(wxString release_note, std::string version);

    Label*            m_text_up_info{nullptr};
    wxScrolledWindow* m_vebview_release_note{nullptr};
};

// [INTENT] Companion update dialog that shows plugin/package metadata loaded from a JSON file.
// [STATE] Tracks the summary label, tip text, and a scrollable content area for the update details.
// [UNITY] Modal dialog with a data-backed detail pane; load JSON into a ScriptableObject/view-model before binding to the view.
class UpdatePluginDialog : public DPIDialog
{
public:
    UpdatePluginDialog(wxWindow* parent = nullptr);
    ~UpdatePluginDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_info(std::string json_path);

    Label*            m_text_up_info{nullptr};
    Label*            operation_tips{nullptr};
    wxScrolledWindow* m_vebview_release_note{nullptr};
};

// [INTENT] Version update dialog that can render release notes in an embedded web view, run injected scripts, and expose download/skip
// controls. [STATE] Holds the current URL, HTML source, browser-facing widgets, and the release-note scroll/simplebook fallback. [EVENT]
// WebView load/title/error handlers keep the content shell synchronized with the loaded release-note page. [UNITY] UI Toolkit dialog plus
// an embedded WebView plugin for the HTML branch, or an external browser fallback for the open-in-browser action. [PORTING_HAZARD:P2] The
// dialog mixes web content, script injection, and manual content swapping between web and static views.
class UpdateVersionDialog : public DPIDialog
{
public:
    UpdateVersionDialog(wxWindow* parent = nullptr);
    ~UpdateVersionDialog();

    wxWebView*               CreateTipView(wxWindow* parent);
    void                     OnLoaded(wxWebViewEvent& event);
    void                     OnTitleChanged(wxWebViewEvent& event);
    void                     OnError(wxWebViewEvent& event);
    bool                     ShowReleaseNote(std::string content);
    void                     RunScript(std::string script);
    void                     on_dpi_changed(const wxRect& suggested_rect) override;
    void                     update_version_info(wxString release_note, wxString version);
    std::vector<std::string> splitWithStl(std::string str, std::string pattern);

    wxStaticBitmap*   m_brand{nullptr};
    Label*            m_text_up_info{nullptr};
    wxWebView*        m_vebview_release_note{nullptr};
    wxSimplebook*     m_simplebook_release_note{nullptr};
    wxScrolledWindow* m_scrollwindows_release_note{nullptr};
    wxBoxSizer*       sizer_text_release_note{nullptr};
    Label*            m_staticText_release_note{nullptr};
    wxStaticBitmap*   m_bitmap_open_in_browser;
    Button*           m_button_skip_version;
    CheckBox*         m_cb_stable_only;
    Button*           m_button_download;
    Button*           m_button_cancel;
    std::string       url_line;
    std::string       html_source;
};

// [INTENT] Generic secondary confirmation frame with a configurable button matrix and optional "don't show again" persistence.
// [STATE] Stores the selected button mode, checkbox state, per-button color palette, and the message surface.
// [EVENT] Button presses are manually repackaged into custom events and posted to the owning window.
// [UNITY] Modal confirmation canvas/panel with a data-driven button row and a callback relay to the parent controller.
class SecondaryCheckDialog : public DPIFrame
{
private:
    wxWindow* event_parent{nullptr};

public:
    enum VisibleButtons { // ORCA VisibleButtons instead ButtonStyle
        ONLY_CONFIRM       = 0,
        CONFIRM_AND_CANCEL = 1,
        CONFIRM_AND_DONE   = 2,
        CONFIRM_AND_RETRY  = 3,
        CONFIRM_AND_RESUME = 4,
        DONE_AND_RETRY     = 5,
        MAX_STYLE_NUM      = 6
    };
    SecondaryCheckDialog(wxWindow*           parent,
                         wxWindowID          id                   = wxID_ANY,
                         const wxString&     title                = wxEmptyString,
                         enum VisibleButtons btn_style            = CONFIRM_AND_CANCEL, // ORCA VisibleButtons instead ButtonStyle
                         const wxPoint&      pos                  = wxDefaultPosition,
                         const wxSize&       size                 = wxDefaultSize,
                         long                style                = wxCLOSE_BOX | wxCAPTION,
                         bool                not_show_again_check = false);
    void update_text(wxString text);
    void on_show();
    void on_hide();
    void update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    void update_title_style(wxString                             title,
                            SecondaryCheckDialog::VisibleButtons style,
                            wxWindow*                            parent = nullptr); // ORCA VisibleButtons instead ButtonStyle
    void post_event(wxCommandEvent&& event);
    void rescale();
    ~SecondaryCheckDialog();
    void on_dpi_changed(const wxRect& suggested_rect);
    void msw_rescale();

    StateColor        btn_bg_green;
    StateColor        btn_bg_white;
    Label*            m_staticText_release_note{nullptr};
    wxBoxSizer*       m_sizer_main;
    wxScrolledWindow* m_vebview_release_note{nullptr};
    Button*           m_button_ok{nullptr};
    Button*           m_button_retry{nullptr};
    Button*           m_button_cancel{nullptr};
    Button*           m_button_fn{nullptr};
    Button*           m_button_resume{nullptr};
    wxCheckBox*       m_show_again_checkbox;
    VisibleButtons    m_button_style; // ORCA VisibleButtons instead ButtonStyle
    bool              not_show_again         = false;
    std::string       show_again_config_text = "";
};

// [INTENT] Error/reporting frame for print-time failures, recovery prompts, and network-fetched help imagery.
// [STATE] Owns a wxWebRequest handle, button registry, message labels, and the button-style list used to rebuild the prompt.
// [EVENT] Web request state changes and custom button events drive the recovery flow.
// [THREAD] UI thread owns the request object; async request callbacks must marshal back to the frame before changing widgets.
// [UNITY] Modal diagnostic dialog with an async image fetcher plus a structured button grid and callback-based retry/stop actions.
// [PORTING_HAZARD:P2] The web request/image path and dynamic button matrix are more than a simple static alert port.
class PrintErrorDialog : public DPIFrame
{
private:
    wxWindow* event_parent{nullptr};

public:
    enum PrintErrorButton : int {
        RESUME_PRINTING                = 2,
        RESUME_PRINTING_DEFECTS        = 3,
        RESUME_PRINTING_PROBELM_SOLVED = 4,
        STOP_PRINTING                  = 5,
        CHECK_ASSISTANT                = 6,
        FILAMENT_EXTRUDED              = 7,
        RETRY_FILAMENT_EXTRUDED        = 8,
        CONTINUE                       = 9,
        LOAD_VIRTUAL_TRAY              = 10,
        OK_BUTTON                      = 11,
        FILAMENT_LOAD_RESUME           = 12,
        JUMP_TO_LIVEVIEW,

        NO_REMINDER_NEXT_TIME        = 23,
        IGNORE_NO_REMINDER_NEXT_TIME = 25,
        // LOAD_FILAMENT = 26*/
        IGNORE_RESUME         = 27,
        PROBLEM_SOLVED_RESUME = 28,
        TURN_OFF_FIRE_ALARM   = 29,

        RETRY_PROBLEM_SOLVED = 34,
        STOP_DRYING          = 35,
        REMOVE_CLOSE_BTN     = 39, // special case, do not show close button

        ERROR_BUTTON_COUNT
    };
    PrintErrorDialog(wxWindow*       parent,
                     wxWindowID      id    = wxID_ANY,
                     const wxString& title = wxEmptyString,
                     const wxPoint&  pos   = wxDefaultPosition,
                     const wxSize&   size  = wxDefaultSize,
                     long            style = wxCLOSE_BOX | wxCAPTION);
    void update_text_image(const wxString& text, const wxString& error_code, const wxString& image_url);
    void on_show();
    void on_hide();
    void update_title_style(wxString title, std::vector<int> style, wxWindow* parent = nullptr);
    void post_event(wxCommandEvent& event);
    void post_event(wxCommandEvent&& event);
    void rescale();
    ~PrintErrorDialog();
    void on_dpi_changed(const wxRect& suggested_rect);
    void msw_rescale();
    void init_button(PrintErrorButton style, wxString buton_text);
    void init_button_list();
    void on_webrequest_state(wxWebRequestEvent& evt);

    wxWebRequest           web_request;
    wxStaticBitmap*        m_error_prompt_pic_static;
    Label*                 m_staticText_release_note{nullptr};
    Label*                 m_staticText_error_code{nullptr};
    wxBoxSizer*            m_sizer_main;
    wxBoxSizer*            m_sizer_button;
    wxScrolledWindow*      m_vebview_release_note{nullptr};
    std::map<int, Button*> m_button_list;
    std::vector<int>       m_used_button;
};

// [INTENT] Small value object describing a single confirmation line and optional wiki link for the send-confirmation flow.
// [STATE] Carries severity, message text, and a help URL without any widget ownership.
struct ConfirmBeforeSendInfo
{
public:
    enum InfoLevel { Normal = 0, Warning = 1 };
    InfoLevel level;
    wxString  text;
    wxString  wiki_url;
    ConfirmBeforeSendInfo(const wxString& txt, const wxString& url = wxEmptyString, InfoLevel lev = Normal)
        : text(txt), wiki_url(url), level(lev)
    {}
};

// [INTENT] Confirmation dialog shown before sending to a printer or similar external target.
// [STATE] Tracks a list-like message area, button set, and the persisted "don't show again" toggle.
// [EVENT] Exposes show/hide and button mutation helpers so callers can tailor the prompt to the workflow state.
// [UNITY] Reusable modal confirmation panel with a dynamic text list, optional warning styling, and persistence-backed suppression.
class ConfirmBeforeSendDialog : public DPIDialog
{
public:
    enum VisibleButtons { // ORCA VisibleButtons instead ButtonStyle
        ONLY_CONFIRM       = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM      = 2
    };
    ConfirmBeforeSendDialog(wxWindow*           parent,
                            wxWindowID          id                   = wxID_ANY,
                            const wxString&     title                = wxEmptyString,
                            enum VisibleButtons btn_style            = CONFIRM_AND_CANCEL, // ORCA VisibleButtons instead ButtonStyle
                            const wxPoint&      pos                  = wxDefaultPosition,
                            const wxSize&       size                 = wxDefaultSize,
                            long                style                = wxCLOSE_BOX | wxCAPTION,
                            bool                not_show_again_check = false);
    void     update_text(wxString text);
    void     update_text(std::vector<ConfirmBeforeSendInfo> texts, bool enable_warning_clr = true);
    void     on_show();
    void     on_hide();
    void     update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    void     rescale();
    void     on_dpi_changed(const wxRect& suggested_rect);
    void     show_update_nozzle_button(bool show = false);
    void     hide_button_ok();
    void     edit_cancel_button_txt(const wxString& txt, bool switch_green = false);
    void     disable_button_ok();
    void     enable_button_ok();
    wxString format_text(wxString str, int warp);

    ~ConfirmBeforeSendDialog();

protected:
    wxBoxSizer*       m_sizer_main;
    wxScrolledWindow* m_vebview_release_note{nullptr};
    Label*            m_staticText_release_note{nullptr};
    Button*           m_button_ok;
    Button*           m_button_cancel;
    Button*           m_button_update_nozzle;
    wxCheckBox*       m_show_again_checkbox;
    bool              not_show_again         = false;
    std::string       show_again_config_text = "";
};

// [INTENT] Printer/cloud onboarding dialog that gathers IP, access code, serial number, and model metadata before testing connectivity.
// [STATE] Carries the staged instructional text, current machine object, the worker thread, timer countdown, and the selected model map.
// [EVENT] Text changes, OK/cancel actions, timer ticks, and worker-completion notifications all feed the validation flow.
// [THREAD] `workerThreadFunc` performs the probe off the UI thread and posts results back through custom events/CallAfter-style bridges.
// [UNITY] Wizard-like UI Toolkit flow with async/await network checks, main-thread marshaling for result updates, and a cancellation token
// for retries. [PORTING_HAZARD:P1] This dialog crosses UI/network/thread boundaries and depends on timed close behavior, so the port needs
// a real async state machine.
class InputIpAddressDialog : public DPIDialog
{
public:
    wxString comfirm_before_check_text;
    wxString comfirm_before_enter_text;
    wxString comfirm_after_enter_text;
    wxString comfirm_last_enter_text;

    std::shared_ptr<InputIpAddressDialog> token_;
    boost::thread*                        m_thread{nullptr};

    std::string m_ip;
    wxWindow*   m_step_icon_panel3{nullptr};
    Label*      m_tip0{nullptr};
    Label*      m_tip1{nullptr};
    Label*      m_tip2{nullptr};
    Label*      m_tip3{nullptr};
    Label*      m_tip4{nullptr};
    InputIpAddressDialog(wxWindow* parent = nullptr);
    ~InputIpAddressDialog();

    MachineObject*                     m_obj{nullptr};
    wxPanel*                           ip_input_top_panel{nullptr};
    wxPanel*                           ip_input_bot_panel{nullptr};
    Button*                            m_button_ok{nullptr};
    Button*                            m_button_manual_setup{nullptr};
    Label*                             m_tips_ip{nullptr};
    Label*                             m_tips_access_code{nullptr};
    Label*                             m_tips_sn{nullptr};
    Label*                             m_tips_modelID{nullptr};
    Label*                             m_test_right_msg{nullptr};
    Label*                             m_test_wrong_msg{nullptr};
    TextInput*                         m_input_ip{nullptr};
    TextInput*                         m_input_access_code{nullptr};
    TextInput*                         m_input_printer_name{nullptr};
    TextInput*                         m_input_sn{nullptr};
    ComboBox*                          m_input_modelID{nullptr};
    wxStaticBitmap*                    m_img_help{nullptr};
    wxStaticBitmap*                    m_img_step1{nullptr};
    wxStaticBitmap*                    m_img_step2{nullptr};
    wxStaticBitmap*                    m_img_step3{nullptr};
    HyperLink*                         m_trouble_shoot{nullptr}; // ORCA
    wxTimer*                           closeTimer{nullptr};
    int                                closeCount{3};
    bool                               m_show_access_code{false};
    bool                               m_need_input_sn{true};
    int                                m_result;
    int                                current_input_index{0};
    std::shared_ptr<BBLStatusBarSend>  m_status_bar;
    std::unique_ptr<Worker>            m_worker;
    std::map<std::string, std::string> m_models_map; // display_name -> model_id

    void switch_input_panel(int index);
    void on_cancel();
    void update_title(wxString title);
    void set_machine_obj(MachineObject* obj);
    void update_test_msg(wxString msg, bool connected);
    bool isIp(std::string ipstr);
    void check_ip_address_failed(int result);
    void on_check_ip_address_failed(wxCommandEvent& evt);
    void on_ok(wxMouseEvent& evt);
    void on_send_retry();
    void update_test_msg_event(wxCommandEvent& evt);
    void post_update_test_msg(std::weak_ptr<InputIpAddressDialog> w, wxString text, bool beconnect);
    void workerThreadFunc(std::string str_ip, std::string str_access_code, std::string sn, std::string model_id, std::string name);
    void OnTimer(wxTimerEvent& event);
    void on_text(wxCommandEvent& evt);
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

// [INTENT] Lightweight confirmation frame used when a send operation fails and the app needs a short recovery choice.
// [UNITY] Small modal dialog or toast-style confirm panel with a controller callback.
class SendFailedConfirm : public DPIDialog
{
public:
    SendFailedConfirm(wxWindow* parent = nullptr);
    ~SendFailedConfirm() {};

    // void on_ok(wxMouseEvent &evt);
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

wxDECLARE_EVENT(EVT_CLOSE_IPADDRESS_DLG, wxCommandEvent);
wxDECLARE_EVENT(EVT_CHECKBOX_CHANGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_ENTER_IP_ADDRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_CHECK_IP_ADDRESS_FAILED, wxCommandEvent);
wxDECLARE_EVENT(EVT_CHECK_IP_ADDRESS_LAYOUT, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif
