#ifndef slic3r_MarkdownTip_hpp_
#define slic3r_MarkdownTip_hpp_

#include <wx/popupwin.h>
#include <wx/timer.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

// [INTENT] A transient popup window that renders markdown tips using a wxWebView.
// It manages loading local/remote markdown files, formatting them with HTML/CSS,
// and handling the popup lifecycle (show/hide with timers).
// [UNITY] Replace with a Unity UI Toolkit `VisualElement` acting as a popup,
// using a dedicated WebGL/WebView component or a text-rendering system for markdown.
// [PORTING_HAZARD:P2] WebView implementations in Unity can be brittle compared to wxWidgets.

class MarkdownTip : public wxPopupTransientWindow
{
public:
    // [EVENT] Static entry point for triggering a tip.
    static bool ShowTip(std::string const& tip, std::string const& tooltip, wxPoint pos);

    static void ExitTip();

    static void Reload();

    static void Recreate(wxWindow* parent);

    static wxWindow* AttachTo(wxWindow* parent);

    static wxWindow* DetachFrom(wxWindow* parent);

private:
    // [STATE] Manages singleton instance lifecycle.
    static MarkdownTip* markdownTip(bool create = true);

    MarkdownTip();

    ~MarkdownTip();

    void LoadStyle();

    bool ShowTip(wxPoint pos, std::string const& tip, std::string const& tooltip);

    std::string LoadTip(std::string const& tip, std::string const& tooltip);

    void RunScript(std::string const& script);

private:
    wxWebView* CreateTipView(wxWindow* parent);

    // [EVENT] WebView callbacks.
    void OnLoaded(wxWebViewEvent& event);

    void OnTitleChanged(wxWebViewEvent& event);

    void OnError(wxWebViewEvent& event);

    void OnTimer(wxTimerEvent& event);

private:
    // [STATE] The web view instance and rendering state.
    wxWebView*  _tipView = nullptr;
    std::string _lastTip;
    std::string _pendingScript = " ";
    std::string _language;
    wxPoint     _requestPos;
    double      _lastHeight = 0;
    wxTimer*    _timer      = nullptr;
    bool        _hide       = false;
    bool        _data_dir   = false;
};

}} // namespace Slic3r::GUI

#endif
