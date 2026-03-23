#ifndef slic3r_HMSPanel_hpp_
#define slic3r_HMSPanel_hpp_

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <map>
#include <string>
#include <slic3r/GUI/Widgets/Button.hpp>
#include <slic3r/GUI/DeviceManager.hpp>
#include <slic3r/GUI/Widgets/ScrolledWindow.hpp>
#include <slic3r/GUI/StatusPanel.hpp>
#include <wx/html/htmlwin.h>

#include "DeviceCore/DevHMS.h"

namespace Slic3r { namespace GUI {

// [INTENT] Row widget that renders a single HMS notification with severity icons, HTML summaries, and actionable links.
// [UNITY] Translate to a UI Toolkit VisualElement row with Images, TMP Labels, and a WebView or UnityWebRequest hyperlink handler.
// [PORTING_HAZARD:P2] Depends on wxHtmlWindow and preloaded wxBitmap assets, so Unity needs explicit Texture2D caching and an embedded
// browser solution.
class HMSNotifyItem : public wxPanel
{
    DevHMSItem& m_hms_item;
    // [STATE] Keeps a live reference to the backend DevHMS record that drives icon severity and hyperlink payloads.
    std::string m_url;
    std::string dev_id;
    std::string long_error_code;
    // [STATE] Holds auxiliary metadata so callbacks open the correct target and error codes remain visible.

    wxPanel*        m_panel_hms;
    wxStaticBitmap* m_bitmap_notify;
    wxStaticBitmap* m_bitmap_arrow;
    wxStaticText*   m_hms_content;
    wxHtmlWindow*   m_html;
    wxPanel*        m_staticline;
    // [STATE] Panel controls with HTML renderer and separator so we can refresh just the changed row.

    wxBitmap m_img_notify_lv1;
    wxBitmap m_img_notify_lv2;
    wxBitmap m_img_notify_lv3;
    wxBitmap m_img_arrow;
    // [STATE] Preloaded bitmaps per severity level to keep repaint time consistent; re-cache in Unity as Texture2D assets.

    void      init_bitmaps();
    wxBitmap& get_notify_bitmap();
    // [INTENT] Centralized helper to warm up severity sprites and minimize repeated loads during layout.

public:
    HMSNotifyItem(const std::string& dev_id, wxWindow* parent, DevHMSItem& item);
    ~HMSNotifyItem();

    // [EVENT] DPI scaling hook raised by wxWidgets; left empty because Unity handles scaling separately.
    void msw_rescale() {}
};

// [INTENT] Container that lists HMS alerts for the selected MachineObject, coordinating scrollable rows and status badges.
// [UNITY] Should become a ScrollView/VisualElement list bound to a ScriptableObject HMS cache; use a GraphicRaycaster overlay for the
// panel. [PORTING_HAZARD:P3] wxScrolledWindow virtualization and wxHtmlWindow hyperlinks have no direct Unity counterpart, so pool rows and
// wrap WebView calls carefully.
class HMSPanel : public wxPanel
{
protected:
    wxScrolledWindow* m_scrolledWindow;
    // [STATE] Scroll area that hosts each HMSNotifyItem row; in Unity this is a ScrollView with pooled items.
    wxBoxSizer* m_top_sizer;
    // [STATE] Sizer managing stacked notification rows.

    int last_status;
    // [STATE] Last displayed HMS status code to avoid redundant UI refreshes when the backend kicks in repeatedly.

    void append_hms_panel(const std::string& dev_id, DevHMSItem& item);
    // [EVENT] Handles each incoming HMS item by instantiating a row and scheduling it inside the scrolled area.
    void delete_hms_panels();
    // [INTENT] Tears down rows before rebuilding so stale widgets do not leak prior severity states.

public:
    HMSPanel(wxWindow*      parent,
             wxWindowID     id    = wxID_ANY,
             const wxPoint& pos   = wxDefaultPosition,
             const wxSize&  size  = wxDefaultSize,
             long           style = wxTAB_TRAVERSAL);
    ~HMSPanel();

    void msw_rescale() {}

    bool Show(bool show = true) override;
    // [EVENT] Called by surrounding tabs/menu actions to toggle visibility; may require invalidating the GL preview when the overlay changes.

    // [THREAD] DeviceManager dispatches update() off the UI thread, so implementations must marshal to the main thread before touching widgets.
    void update(MachineObject* obj_);

    // [STATE] Updates the top banner badge when HMS status transitions occur.
    void show_status(int status);

    // [EVENT] Explicit user action to clear any currently shown HMS tag.
    void clear_hms_tag();

    MachineObject* obj{nullptr};
    // [STATE] Currently observed machine whose HMS data populates the list.
    std::map<std::string, DevHMSItem> temp_hms_list;
    // [STATE] Staging bucket that diffs newly fetched HMS rows from existing ones before UI mutation.
};

wxDECLARE_EVENT(EVT_ALREADY_READ_HMS, wxCommandEvent);
// [EVENT] Notifies listeners that the current HMS tag has already been acknowledged so redundant popups are suppressed.

}} // namespace Slic3r::GUI

#endif
