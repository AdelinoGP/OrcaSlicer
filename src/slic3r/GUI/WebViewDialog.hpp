#ifndef slic3r_WebViewDialog_hpp_
#define slic3r_WebViewDialog_hpp_

#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include <wx/webview.h>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>

namespace Slic3r {

class NetworkAgent;

namespace GUI {

// [INTENT] WebViewPanel is the retained browser-host shell for embedded web flows, debug tooling, and login/status bridging.
// [STATE] It owns the browser widget, toolbar/menu affordances, info bar, login timer, and the last/response JS snippets that survive
// across page interactions. [EVENT] The declaration surface exposes navigation, webview, menu, timer, and close handlers that all route
// back to one UI-thread controller. [THREAD] The timer and queued response path are the only explicit cross-boundary hooks; Unity should
// keep them behind a main-thread web host service. [UNITY] Map this to a persistent browser-host MonoBehaviour plus a typed JS-command
// router and a separate debug/tool menu panel. [PORTING_HAZARD:P2] The page can directly drive app/login/network behavior, so the view host
// and command execution path must be separated in Unity.
class WebViewPanel : public wxPanel
{
public:
    WebViewPanel(wxWindow* parent);
    virtual ~WebViewPanel();

    // [INTENT] Entry point for controller-driven navigation into the embedded page.
    void load_url(wxString& url);

    // [STATE] These handlers keep browser chrome, command state, and selection/edit affordances synchronized with the current page.
    void UpdateState();
    void OnIdle(wxIdleEvent& evt);
    void OnUrl(wxCommandEvent& evt);
    void OnBack(wxCommandEvent& evt);
    void OnForward(wxCommandEvent& evt);
    void OnStop(wxCommandEvent& evt);
    void OnReload(wxCommandEvent& evt);
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnScriptResponseMessage(wxCommandEvent& evt);
    void OnViewSourceRequest(wxCommandEvent& evt);
    void OnViewTextRequest(wxCommandEvent& evt);
    void OnToolsClicked(wxCommandEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnCut(wxCommandEvent& evt);
    void OnCopy(wxCommandEvent& evt);
    void OnPaste(wxCommandEvent& evt);
    void OnUndo(wxCommandEvent& evt);
    void OnRedo(wxCommandEvent& evt);
    void OnMode(wxCommandEvent& evt);
    void RunScript(const wxString& javascript);
    void OnRunScriptString(wxCommandEvent& evt);
    void OnRunScriptInteger(wxCommandEvent& evt);
    void OnRunScriptDouble(wxCommandEvent& evt);
    void OnRunScriptBool(wxCommandEvent& evt);
    void OnRunScriptObject(wxCommandEvent& evt);
    void OnRunScriptArray(wxCommandEvent& evt);
    void OnRunScriptDOM(wxCommandEvent& evt);
    void OnRunScriptUndefined(wxCommandEvent& evt);
    void OnRunScriptNull(wxCommandEvent& evt);
    void OnRunScriptDate(wxCommandEvent& evt);
    void OnRunScriptMessage(wxCommandEvent& evt);
    void OnRunScriptCustom(wxCommandEvent& evt);
    void OnAddUserScript(wxCommandEvent& evt);
    void OnSetCustomUserAgent(wxCommandEvent& evt);
    void OnClearSelection(wxCommandEvent& evt);
    void OnDeleteSelection(wxCommandEvent& evt);
    void OnSelectAll(wxCommandEvent& evt);
    void OnLoadScheme(wxCommandEvent& evt);
    void OnUseMemoryFS(wxCommandEvent& evt);
    void OnEnableContextMenu(wxCommandEvent& evt);
    void OnEnableDevTools(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    // [THREAD] Timer ownership is explicit so login refresh stops with the panel and does not outlive the browser host.
    wxTimer* m_LoginUpdateTimer{nullptr};
    void     OnFreshLoginStatus(wxTimerEvent& event);

public:
    // [UNITY] These are host-to-page data pushes; Unity should feed them through a view-model or message bus rather than raw JS strings.
    void SendRecentList(int images);
    void SetLoginPanelVisibility(bool bshow);
    void SendDesignStaffpick(bool on);
    void OpenModelDetail(std::string id, NetworkAgent* agent);
    void SendLoginInfo();
    void ShowNetpluginTip();

    // [INTENT] Helper methods wrap backend-specific URL and content fetch behavior used by the embedded experience.
    void get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback);
    int  get_model_mall_detail_url(std::string* url, std::string id);

    void update_mode();

private:
    // [STATE] Browser and chrome widgets are owned by the panel and are rebuilt only with the host.
    wxWebView*  m_browser;
    wxBoxSizer* bSizer_toolbar;
    wxButton*   m_button_back;
    wxButton*   m_button_forward;
    wxButton*   m_button_stop;
    wxButton*   m_button_reload;
    wxTextCtrl* m_url;
    wxButton*   m_button_tools;

    wxMenu*     m_tools_menu;
    wxMenuItem* m_tools_handle_navigation;
    wxMenuItem* m_tools_handle_new_window;
    wxMenuItem* m_edit_cut;
    wxMenuItem* m_edit_copy;
    wxMenuItem* m_edit_paste;
    wxMenuItem* m_edit_undo;
    wxMenuItem* m_edit_redo;
    wxMenuItem* m_edit_mode;
    wxMenuItem* m_scroll_line_up;
    wxMenuItem* m_scroll_line_down;
    wxMenuItem* m_scroll_page_up;
    wxMenuItem* m_scroll_page_down;
    wxMenuItem* m_script_string;
    wxMenuItem* m_script_integer;
    wxMenuItem* m_script_double;
    wxMenuItem* m_script_bool;
    wxMenuItem* m_script_object;
    wxMenuItem* m_script_array;
    wxMenuItem* m_script_dom;
    wxMenuItem* m_script_undefined;
    wxMenuItem* m_script_null;
    wxMenuItem* m_script_date;
    wxMenuItem* m_script_message;
    wxMenuItem* m_script_custom;
    wxMenuItem* m_selection_clear;
    wxMenuItem* m_selection_delete;
    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;

    // [STATE] The info bar is used for page-load errors and debug messages, not as a primary app dialog.
    wxInfoBar*    m_info;
    wxStaticText* m_info_text;

    // [STATE] Zoom and script caches persist across navigation so the tools menu can replay the last snippet.
    long m_zoomFactor;

    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;

    DECLARE_EVENT_TABLE()
};

// [INTENT] SourceViewDialog is the transient read-only inspector for the browser tools menu.
// [UNITY] Use a modal/popup read-only text panel instead of spawning another browser instance.
class SourceViewDialog : public wxDialog
{
public:
    SourceViewDialog(wxWindow* parent, wxString source);
};

} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_Tab_hpp_ */
