#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/webview.h>

// [INTENT] Provides a unified wrapper around wxWebView with platform-specific configurations
// (WebView2 on Windows, WebKit on macOS/Linux), handling theme sync, JS bridging, and fallback behaviors.
// [UNITY] Unity lacks a built-in full-featured browser control. Requires a third-party plugin 
// (e.g., Vuplex 3D WebView) to embed web content, run JS, and handle message passing.
// [PORTING_HAZARD:P2] WebView dependency and JS interop vary heavily between platforms. Unity implementation
// will need to map these static helpers to the chosen WebView plugin's lifecycle and async evaluation methods.
class WebView
{
public:
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url);
#if wxUSE_WEBVIEW_EDGE
    static bool CheckWebViewRuntime();
    static bool DownloadAndInstallWebViewRuntime();
#endif
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    static void RecreateAll();
};

#endif // !slic3r_GUI_WebView_hpp_
