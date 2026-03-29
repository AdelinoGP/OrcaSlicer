#pragma once
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/splitter.h>
#include "Scrollbar.hpp"

class MyScrollbar;

// [INTENT] Retained viewport wrapper that binds a wxScrolled content area to custom right/bottom scrollbars and splitters.
// [STATE] `m_userPanel` is the hosted content surface; the scrollbar and splitter pointers mirror scroll chrome; `m_marginWidth` and
// `m_bothDirections` control layout/visibility behavior. [EVENT] Wheel, size, and scroll callbacks coordinate input routing, relayout, and
// scroll feedback into the wrapped content. [UNITY] Port as one retained scroll-container controller with a content viewport plus separate
// scrollbar prefabs, driven by a shared normalized scroll model. [PORTING_HAZARD:P2] The manual splitter/scrollbar coupling and
// SetViewStart mirroring are behavior, not decoration; a naive ScrollRect replacement will miss the chrome synchronization rules.
class ScrolledWindow : public wxScrolled<wxWindow>
{
public:
    // [INTENT] Construct the composite scroller with caller-provided size/style and optional chrome sizing hints.
    ScrolledWindow(wxWindow*  parent,
                   wxWindowID id,
                   wxPoint    position,
                   wxSize     size,
                   long       style,
                   int        marginWidth    = 0,
                   int        scrollbarWidth = 4,
                   int        tipLength      = 0);
    // [EVENT] Forward mouse-wheel deltas into the shared scroll model.
    void OnMouseWheel(wxMouseEvent& event);
    // [STATE] Update the scrollbar tip color without changing the scroll model.
    void SetTipColor(wxColour color);
    // [EVENT] Force a repaint/relayout pass for the wrapped viewport and chrome.
    void Refresh();
    // [STATE] Propagate background styling into the content container.
    void SetBackgroundColour(wxColour color);

    // [STATE] Margin and scrollbar appearance setters mutate retained chrome state, not content state.
    void SetMarginColor(wxColour color);
    void SetScrollbarColor(wxColour color);
    void SetScrollbarTip(int len);
    // [STATE] Virtual size defines the scroll extent mirrored into the custom scrollbar geometry.
    virtual void SetVirtualSize(int x, int y);
    virtual void SetVirtualSize(wxSize& size);
    // [STATE] Expose the hosted user panel for layout/embedding code.
    wxPanel* GetPanel() { return m_userPanel; }
    // wxSplitterWindow* GetVerticalSplitter() { return m_verticalSplitter; }
    // wxSplitterWindow* GetHorizontalSplitter() { return m_horizontalSplitter; }
    // [STATE] True when both axes are active and both custom scrollbars may be shown.
    bool IsBothDirections() { return m_bothDirections; }
    // [INTENT] Override scrollbar configuration so the scrolled window can keep its own chrome and view state synchronized.
    virtual void SetScrollbars(
        int pixelsPerUnitX, int pixelsPerUnitY, int noUnitsX, int noUnitsY, int xPos = 0, int yPos = 0, bool noRefresh = false);

private:
    // [STATE] The panel targeted by the scrolled window; ownership stays with this composite.
    wxPanel* m_userPanel;
    // [STATE] Backing wxScrolled content window that receives the virtual size and position updates.
    wxWindow* m_scroll_win;
    // [STATE] Custom scrollbar instances mirror the vertical and horizontal scroll axes.
    MyScrollbar* m_rightScrollbar;
    MyScrollbar* m_bottomScrollbar;
    // wxSplitterWindow* m_verticalSplitter;
    // [STATE] Layout splitter used to carve out chrome; kept as a generic window in this branch.
    wxWindow* m_verticalSplitter;
    // [STATE] Horizontal splitter coordinating the embedded scroll chrome and viewport.
    wxSplitterWindow* m_horizontalSplitter;
    // [STATE] Cached chrome inset used when computing visible viewport geometry.
    int m_marginWidth;
    // [STATE] Tracks whether the viewport exposes both scroll directions.
    bool m_bothDirections;

    // [EVENT] Size changes drive scrollbar visibility, splitter math, and viewport relayout.
    void OnSize(wxSizeEvent& WXUNUSED(event));
    // [EVENT] Scrollbar notifications feed back into the wrapped wxScrolled window.
    void OnScroll(wxScrollWinEvent& event);
};
