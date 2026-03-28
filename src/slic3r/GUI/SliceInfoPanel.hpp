#ifndef slic3r_SliceInfoPanel_hpp_
#define slic3r_SliceInfoPanel_hpp_

#include "slic3r/GUI/MonitorBasePanel.h"
#include "libslic3r/ProjectTask.hpp"
#include "DeviceManager.hpp"
#include "GUI.hpp"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/webrequest.h>
#include "Widgets/PopupWindow.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Floating detail popup for slice metadata and thumbnail inspection.
// [STATE] The popup is ephemeral UI, owned by the panel, and dismissed by focus/click routing rather than modeless reuse.
// [UNITY] Map this to a floating overlay controller (tooltip/popover style) with explicit close semantics and pointer-capture handling.
class SliceInfoPopup : public PopupWindow
{
public:
    SliceInfoPopup(wxWindow* parent, wxBitmap bmp = wxNullBitmap, BBLSliceInfo* info = nullptr);
    virtual ~SliceInfoPopup() {}

    // PopupWindow virtual methods are all overridden to log them
    virtual void Popup(wxWindow* focus = NULL) wxOVERRIDE;
    virtual void OnDismiss() wxOVERRIDE;
    virtual bool ProcessLeftDown(wxMouseEvent& event) wxOVERRIDE;
    virtual bool Show(bool show = true) wxOVERRIDE;

private:
    // [STATE] Scrollable content surface for the popup body; its lifetime is tied to the popup window instance.
    wxScrolledWindow* m_panel;
    // [STATE] Borrowed slice-info snapshot backing the popup content; callers must keep the pointed data valid while visible.
    BBLSliceInfo* m_info{nullptr};

    // [EVENT] Pointer and focus handlers keep the transient popup synchronized with hover and dismissal state.
    void OnMouse(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnSetFocus(wxFocusEvent& event);
    void OnKillFocus(wxFocusEvent& event);

private:
    wxDECLARE_ABSTRACT_CLASS(SliceInfoPopup);
    wxDECLARE_EVENT_TABLE();
};

// [INTENT] Summary panel for slice results, thumbnail previews, and detail popups.
// [STATE] Holds async thumbnail request state plus the transient popups used for hover inspection.
// [THREAD] `wxWebRequest` callbacks may arrive asynchronously, so image updates must be treated as UI-thread completion events.
// [UNITY] Recreate as a retained summary card with async preview loading and a separate popover controller for detailed slice info.
class SliceInfoPanel : public wxPanel
{
private:
protected:
    // [THREAD] Async thumbnail fetch pipeline; completion handlers must marshal back to the UI thread before mutating widgets.
    wxWebRequest web_request;
    // [STATE] Hover thumbnail popup lifetime is shared so the panel can reopen/refresh without recreating the underlying controller.
    std::shared_ptr<ImageTransientPopup> m_thumbnail_popup;
    // [STATE] Detail popup showing full slice information; shared ownership keeps the transient overlay alive while visible.
    std::shared_ptr<SliceInfoPopup> m_slice_info_popup;

    // [STATE] Cached thumbnail image used for the summary tile and popup refreshes.
    wxImage m_thumbnail_img;

    // [STATE] Layout and icon widgets for the summary strip; Unity should replace these with a composed card layout rather than ad-hoc sizers.
    wxBoxSizer*     m_item_top_sizer;
    wxStaticBitmap* m_bmp_item_thumbnail;
    wxStaticBitmap* m_bmp_item_prediction;
    wxStaticBitmap* m_bmp_item_print;
    wxStaticText*   m_text_item_prediction;
    wxStaticBitmap* m_bmp_item_cost;
    wxStaticText*   m_text_item_cost;
    wxGridSizer*    m_filament_info_sizer;
    wxStaticText*   m_text_plate_index;

public:
    SliceInfoPanel(wxWindow*       parent,
                   wxBitmap&       prediction,
                   wxBitmap&       cost,
                   wxBitmap&       print,
                   wxWindowID      id    = wxID_ANY,
                   const wxPoint&  pos   = wxDefaultPosition,
                   const wxSize&   size  = wxDefaultSize,
                   long            style = wxTAB_TRAVERSAL,
                   const wxString& name  = wxEmptyString);
    ~SliceInfoPanel();

    void SetImages(wxBitmap& prediction, wxBitmap& cost, wxBitmap& printing);

    // [EVENT] Subtask button click dispatches into print-related workflows from the summary panel.
    void on_subtask_print(wxCommandEvent& evt);
    // [EVENT] Hover enter/leave controls the thumbnail popup lifecycle and preview affordances.
    void on_thumbnail_enter(wxMouseEvent& event);
    void on_thumbnail_leave(wxMouseEvent& event);

    // [EVENT] Container-level hover handling keeps the summary card and child widgets in sync.
    void on_mouse_enter(wxMouseEvent& event);
    void on_mouse_leave(wxMouseEvent& event);

    // [THREAD] Web request completion updates thumbnail state and must guard against stale async responses.
    void on_webrequest_state(wxWebRequestEvent& evt);
    // [STATE] Applies a fresh slice-info model to the cached images, popup content, and display labels.
    void update(BBLSliceInfo* info);
    // [UNITY] DPI rescaling should become a layout rebuild / asset-scale pass in the Unity port rather than per-widget manual sizing.
    void msw_rescale();
};

}} // namespace Slic3r::GUI
#endif
