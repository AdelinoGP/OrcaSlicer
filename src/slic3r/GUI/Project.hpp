#ifndef slic3r_Project_hpp_
#define slic3r_Project_hpp_

#include "Tabbook.hpp"
#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

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

#include "nlohmann/json.hpp"
#include "slic3r/Utils/json_diff.hpp"

#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"
#include "Auxiliary.hpp"

#define AUFILE_GREY700 wxColour(107, 107, 107)
#define AUFILE_GREY500 wxColour(158, 158, 158)
#define AUFILE_GREY300 wxColour(238, 238, 238)
#define AUFILE_GREY200 wxColour(248, 248, 248)
#define AUFILE_BRAND wxColour(0, 150, 136)
#define AUFILE_BRAND_TRANSPARENT wxColour("#E5F0EE") // ORCA color with %10 opacity
//#define AUFILE_PICTURES_SIZE wxSize(FromDIP(300), FromDIP(300))
//#define AUFILE_PICTURES_PANEL_SIZE wxSize(FromDIP(300), FromDIP(340))
#define AUFILE_PICTURES_SIZE wxSize(FromDIP(168), FromDIP(168))
#define AUFILE_PICTURES_PANEL_SIZE wxSize(FromDIP(168), FromDIP(208))
#define AUFILE_SIZE wxSize(FromDIP(168), FromDIP(168))
#define AUFILE_PANEL_SIZE wxSize(FromDIP(168), FromDIP(208))
#define AUFILE_TEXT_HEIGHT FromDIP(40)
#define AUFILE_ROUNDING FromDIP(5)

namespace Slic3r { namespace GUI {

// [INTENT] Represents one auxiliary asset entry exposed by the project page's web UI.
// [STATE] The browser/editor bridge serializes path, display name, and human-readable size into JSON for the embedded page.

struct project_file{
    std::string filepath;
    std::string filename;
    std::string size;
};

// [INTENT] Hosts the project details workflow: an embedded web page plus a native auxiliary editor panel.
// [STATE] Owns the web browser, auxiliary panel, current project URL, root directory, and a monotonically increasing sequence id
// used to correlate script messages.
// [EVENT] Bridges webview navigation and script messages back into wx events, then posts reload/update events to itself.
// [THREAD] Reload path discovery and metadata assembly run on a background thread, but UI updates are marshaled back through
// wx CallAfter / posted events.
// [UNITY] Likely maps to a UI Toolkit panel with a dedicated web-content surface or embedded webview plugin plus a C# controller
// that owns the native bridge and routes page events into the project inspector state.
// [PORTING_HAZARD:P2] The current flow mixes browser scripting, native file scanning, and delayed UI swaps; Unity needs an explicit
// bridge layer to preserve message ordering and async reload behavior.
class ProjectPanel : public wxPanel
{
private:
    // [STATE] Indicates whether the embedded web page has finished its initial handshake.
    bool       m_web_init_completed = {false};
    // [STATE] Guards duplicate reloads so the browser/editor swap is only triggered once per refresh cycle.
    bool       m_reload_already = {false};

    // [STATE] Embedded browser surface that renders the project HTML UI.
    wxWebView* m_browser = {nullptr};
    // [STATE] Native auxiliary editor panel shown when the web page requests project-info editing.
    AuxiliaryPanel*   m_auxiliary{nullptr};
    // [STATE] Local file:// entrypoint for the bundled project page.
    wxString   m_project_home_url;
    // [STATE] Root path used to resolve project data assets and browser content.
    wxString   m_root_dir;
    // [STATE] Monotonic id used to tag messages sent to the page so stale updates can be ignored.
    static inline int m_sequence_id = 8000;

    // [INTENT] Toggles between the web summary view and the native auxiliary editor.
    void show_info_editor(bool show);
    

public:
    // [INTENT] Builds the browser-backed project panel and wires the native/web event bridge.
    ProjectPanel(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    // [INTENT] Releases the project panel's browser/editor ownership; lifetime is handled by wx parent destruction.
    ~ProjectPanel();

    
    // [EVENT] Vetoes external navigation and opens http/https links in the system browser.
    void onWebNavigating(wxWebViewEvent& evt);
    // [EVENT] Handles self-posted reload requests after auxiliary file scanning completes.
    // [THREAD] Spawns the background scan that gathers model/profile metadata before returning to the UI thread.
    void on_reload(wxCommandEvent& evt);
    // [STATE] Rescales embedded controls to the current DPI and forwards the change to the auxiliary panel.
    void on_size(wxSizeEvent &event);
    // [EVENT] Receives the initial navigation-complete signal from the browser.
    void on_navigated(wxWebViewEvent& event);
   
    // [UNITY] In Unity this becomes a rect/layout refresh on the project inspector panel and any embedded web surface.
    void msw_rescale();
    // [INTENT] Rebuilds model/project data and triggers a reload of the browser-visible JSON payload.
    // [THREAD] Kicks off the event path that eventually posts back to the UI thread.
    void update_model_data();
    // [INTENT] Clears the browser-visible model section by sending a JS command to the embedded page.
    void clear_model_info();
    // [INTENT] Shows the auxiliary native editor surface.
    void init_auxiliary() { m_auxiliary->init_auxiliary(); }

    // [INTENT] wx show/hide hook that refreshes data before the panel becomes visible.
    bool Show(bool show);
    // [EVENT] Handles JSON messages emitted by the page's script layer.
    void OnScriptMessage(wxWebViewEvent& evt);
    // [INTENT] Executes a JS snippet in the embedded web view.
    void RunScript(std::string content);

    // [INTENT] Enumerates project auxiliary folders and returns JSON-ready metadata for the web UI.
    // [THREAD] Performs filesystem scanning and file-size reads off the UI thread.
    std::map<std::string, std::vector<json>> Reload(wxString aux_path);
    // [INTENT] Formats raw byte counts into the MB string shown in the project UI.
    std::string formatBytes(unsigned long bytes);
    // [INTENT] Loads an image file and base64-encodes it for inline browser display.
    wxString to_base64(std::string path);
};

wxDECLARE_EVENT(EVT_PROJECT_RELOAD, wxCommandEvent);
}} // namespace Slic3r::GUI

#endif
