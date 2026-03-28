#ifndef slic3r_WebGuideDialog_hpp_
#define slic3r_WebGuideDialog_hpp_

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
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

// [INTENT] Web-based onboarding wizard that embeds a browser page, receives JS/navigation events, and writes
// the selected region, filament, and plugin choices back into app config / preset state.
// [STATE] Retains the browser widget, page selection, first-run completion flags, transient JS response state,
// and user-facing import settings across the wizard lifecycle.
// [EVENT] The handlers below form a bidirectional bridge: webview events drive page transitions and script
// responses, while native callbacks push status and install results back into the embedded page.
// [THREAD] Profile loading is kicked off on a background thread and later reconciled with UI/webview state;
// Unity should replace this with a modal browser host plus a cancellable async import service.
// [UNITY] Use a WebView host for the onboarding page, but move filesystem/config mutation into a typed C#
// controller plus main-thread marshaling service.
// [PORTING_HAZARD:P2] This class mixes navigation, persistence, plugin installation, and script execution in one
// object; that coupling will need to be broken before a clean Unity port.
class GuideFrame : public DPIDialog
{
public:
    GuideFrame(GUI_App* pGUI, long style = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU);
    virtual ~GuideFrame();

    enum GuidePage { BBL_WELCOME, BBL_REGION, BBL_MODELS, BBL_FILAMENTS, BBL_FILAMENT_ONLY, BBL_MODELS_ONLY } m_page;

    // Web Function
    void     load_url(wxString& url);
    wxString SetStartPage(GuidePage startpage = BBL_WELCOME, bool load = true);

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

    // Logic
    bool IsFirstUse();

    // Model - Machine - Filaments
    int LoadProfileData();
    int SaveProfileData();
    int LoadProfileFamily(std::string strVendor, std::string strFilePath);
    int SaveProfile();
    int GetFilamentInfo(std::string VendorDirectory, json& pFilaList, std::string filepath, std::string& sVendor, std::string& sType);

    bool apply_config(AppConfig* app_config, PresetBundle* preset_bundle, const PresetUpdater* updater, bool& apply_keeped_changes);
    bool run();

    void        StrReplace(std::string& strBase, std::string strSrc, std::string strDes);
    std::string w2s(wxString sSrc);
    void        GetStardardFilePath(std::string& FilePath);
    bool        LoadFile(std::string jPath, std::string& sContent);

    // install plugin
    int DownloadPlugin();
    int InstallPlugin();
    int ShowPluginStatus(int status, int percent, bool& cancel);

    void on_dpi_changed(const wxRect& suggested_rect) {}

private:
    // [STATE] Non-owning app back-pointer plus the scratch AppConfig snapshot being assembled during the wizard.
    GUI_App*  m_MainPtr;
    AppConfig m_appconfig_new;

    // [STATE] Browser host and a small native control surface used by the wizard page.
    wxWebView* m_browser;
    wxButton*  m_TestBtn;

    // [STATE] Current web wizard section and resource lookup roots for bundle / vendor content.
    wxString m_SectionName;

    bool                    orca_bundle_rsrc;
    boost::filesystem::path vendor_dir;
    boost::filesystem::path rsrc_vendor_dir;

    // [STATE] First-run completion and teardown guards; the worker thread is retained until startup loading ends.
    bool           bFirstComplete{false};
    bool           m_destroy{false};
    boost::thread* m_load_task{nullptr};

    // [STATE] Wizard choices that feed the saved profile and onboarding decision tree.
    bool        PrivacyUse;
    bool        StealthMode;
    std::string m_Region;

    bool InstallNetplugin;
    bool network_plugin_ready{false};

    // [STATE] Cached filament metadata loaded from the guide content / vendor bundle.
    json        m_OrcaFilaList;
    std::string m_OrcaFilaLibPath;

#if wxUSE_WEBVIEW_IE
    // [STATE] IE-only debug/script helpers exposed for the embedded page's script-object inspection paths.
    wxMenuItem* m_script_object_el;
    wxMenuItem* m_script_date_el;
    wxMenuItem* m_script_array_el;
#endif
    // [STATE] Last executed JavaScript snippet and response payload used to round-trip web commands.
    wxString m_javascript;
    wxString m_response_js;

    // [STATE] User-agent override and filament-editing context for the page / profile sync flow.
    wxString    m_bbl_user_agent;
    std::string m_editing_filament_id;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
