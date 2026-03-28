#pragma once
#ifndef slic3r_ZWebUserLogin_HEAD_
#define slic3r_ZWebUserLogin_HEAD_

#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_IE
#include "wx/msw/webview_ie.h"
#endif
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
#include <wx/frame.h>
#include "wx/timer.h"
#include <wx/tbarbase.h>
#include "wx/textctrl.h"

namespace Slic3r { namespace GUI {

// [INTENT] Modal login host for the embedded web-auth flow or the fallback error page shown when network/plugin setup fails.
// [UNITY] Port this as a modal Canvas/UIPanel shell that hosts a browser surface plus a typed command bridge for login completion.
// [PORTING_HAZARD:P2] The dialog mixes navigation, script callbacks, timeout polling, and modal teardown, so Unity needs an explicit state machine.
class ZUserLogin : public wxDialog
{
public:
    ZUserLogin();
    virtual ~ZUserLogin();

    void load_url(wxString& url);

    std::string w2s(wxString sSrc);

    void UpdateState();
    void OnIdle(wxIdleEvent& evt);
    // void OnClose(wxCloseEvent &evt);

    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);

    void OnScriptResponseMessage(wxCommandEvent& evt);
    void RunScript(const wxString& javascript);

    bool m_networkOk;
    bool ShowErrorPage();

    bool run();

    static int web_sequence_id;

private:
    // [STATE] UI-thread timer used for timeout / polling style updates while the modal login shell is alive.
    wxTimer* m_timer{nullptr};
    void     OnTimer(wxTimerEvent& event);

private:
    // [STATE] Browser host and target URL are retained for navigation retries, fallback page display, and script-driven transitions.
    wxString   TargetUrl;
    wxWebView* m_browser;

    // [STATE] Script bridge payloads and auth/session hints remembered across navigation and response callbacks.
    std::string m_AutotestToken;
    int         m_loopback_port{0};

#if wxUSE_WEBVIEW_IE
    wxMenuItem* m_script_object_el;
    wxMenuItem* m_script_date_el;
    wxMenuItem* m_script_array_el;
#endif
    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;

    // [STATE] The browser user-agent is customized so the remote login page sees a stable, app-specific identity.
    wxString m_bbl_user_agent;

    // [EVENT] The event table fans webview navigation, loading, title/fullscreen, and script-message events into dialog state transitions.
    // [THREAD] All handlers here are expected to run on the UI thread; any async completion needs marshaling before mutating modal state.
    DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif
