#ifndef slic3r_WebDialytipDialog_hpp_
#define slic3r_WebDialytipDialog_hpp_

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
#include <wx/tbarbase.h>
#include "wx/textctrl.h"

#include "GUI_App.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] This dialog hosts the bundled plugin-download web flow and routes page commands into GUI_App actions.
// [UNITY] Port this as a retained WebView host panel with a typed command router and main-thread service callbacks.
// [PORTING_HAZARD:P1] The web page can trigger install/restart/file-open behavior, so the message surface is privileged rather than informational.
class DownPluginFrame : public wxDialog
{
public:
    DownPluginFrame(GUI_App* pGUI);
    virtual ~DownPluginFrame();

    // [INTENT] Load and refresh the hosted page, keep the webview focused, and reflect browser state back into the dialog shell.
    void load_url(wxString& url);

    void UpdateState();
    void OnIdle(wxIdleEvent& evt);
    // void OnClose(wxCloseEvent &evt);

    // [EVENT] These callbacks mirror the embedded browser lifecycle: navigation, load, title, fullscreen, errors, and script messages.
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);

    // [EVENT] Script responses are routed back through wxCommandEvent after the webview callback returns.
    void OnScriptResponseMessage(wxCommandEvent& evt);
    // [UNITY] This should become a browser-script bridge that accepts typed commands and emits typed responses.
    void RunScript(const wxString& javascript);

    // [INTENT] Download the plugin archive, install it, and publish progress back into the hosted page.
    int DownloadPlugin();
    int InstallPlugin();
    // [STATE] Progress reporting is collapsed into a percent/status callback, which makes this dialog the live status owner.
    int ShowPluginStatus(int status, int percent, bool& cancel);

private:
    // [STATE] The dialog owns the app back-pointer, staging config, and browser control; the browser is deleted manually in the destructor.
    GUI_App*  m_MainPtr;
    AppConfig m_appconfig_new;

    wxWebView* m_browser;

#if wxUSE_WEBVIEW_IE
    wxMenuItem* m_script_object_el;
    wxMenuItem* m_script_date_el;
    wxMenuItem* m_script_array_el;
#endif
    // [STATE] Script echo fields preserve the last command/response pair for debugging or UI convenience.
    wxString m_javascript;
    wxString m_response_js;

    // DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
