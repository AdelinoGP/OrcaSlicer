#ifndef slic3r_GUI_PrivacyUpdateDialog_hpp_
#define slic3r_GUI_PrivacyUpdateDialog_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/WebView.hpp"
#include <wx/webview.h>
#include <wx/progdlg.h>
#include <wx/simplebook.h>

// [INTENT] PrivacyUpdateDialog displays privacy policy updates and release notes using a webview.
// [STATE] Contains main sizer, webview, static text, buttons, markdown text, host URL.
// [EVENT] EVT_PRIVACY_UPDATE_CONFIRM and EVT_PRIVACY_UPDATE_CANCEL events for user actions.
// [UNITY] Replace wxWebView with Unity UI Toolkit WebView or HTML-like UI Document. Buttons with UI Toolkit Button.
// [PORTING_HAZARD:P2] wxWebView dependency; Unity lacks built-in webview, may need third-party solution.

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_PRIVACY_UPDATE_CONFIRM, wxCommandEvent);
wxDECLARE_EVENT(EVT_PRIVACY_UPDATE_CANCEL, wxCommandEvent);

class PrivacyUpdateDialog : public DPIDialog
{
public:
    enum VisibleButtons { // ORCA VisibleButtons instead ButtonStyle 
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    PrivacyUpdateDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        enum VisibleButtons btn_style = CONFIRM_AND_CANCEL, // ORCA VisibleButtons instead ButtonStyle 
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxPD_APP_MODAL| wxCAPTION
    );
    wxWebView* CreateTipView(wxWindow* parent);
    void OnNavigating(wxWebViewEvent& event);
    bool ShowReleaseNote(std::string content);
    void RunScript(std::string script);
    void set_text(std::string str) { m_mkdown_text = str; };
    void on_show();
    void on_hide();
    void update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    void rescale();
    ~PrivacyUpdateDialog();
    void on_dpi_changed(const wxRect& suggested_rect);

    wxBoxSizer* m_sizer_main;
    wxWebView* m_vebview_release_note{ nullptr };
    Label* m_staticText_release_note{ nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    std::string m_mkdown_text;
    std::string m_host_url;
};

}} // namespace Slic3r::GUI

#endif
