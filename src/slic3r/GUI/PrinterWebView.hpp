// [ANNOTATED]
// [INTENT] Declares the embedded printer web panel and its minimal navigation/authentication control surface.
// [STATE] Holds browser ownership plus deferred navigation and one-shot API key injection flags.
// [UNITY] Treat this as an interface boundary around an eventual platform-specific browser solution.
// [PORTING_HAZARD:P2] Public methods are tightly coupled to wx lifecycle and wxWebView event types.

#ifndef slic3r_PrinterWebView_hpp_
#define slic3r_PrinterWebView_hpp_

#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include <wx/webview.h>
#include <wx/string.h>

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

namespace Slic3r { namespace GUI {

class PrinterWebView : public wxPanel
{
public:
    PrinterWebView(wxWindow* parent);
    virtual ~PrinterWebView();

    void load_url(wxString& url, wxString apikey = "");
    void UpdateState();
    void OnClose(wxCloseEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnLoaded(wxWebViewEvent& evt);
    void reload();
    void update_mode();

    bool Show(bool show = true) override;

private:
    void SendAPIKey();

    wxWebView* m_browser;
    long       m_zoomFactor;
    wxString   m_apikey;
    bool       m_apikey_sent;

    wxString m_url_deferred;

    // DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
