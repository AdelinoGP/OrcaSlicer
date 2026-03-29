#pragma once
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// [INTENT] Hit-testing states for the custom scrollbar's drag and wheel logic.
enum { BEFORE_SCROLLBAR, ON_SCROLLBAR, AFTER_SCROLLBAR, NOWHERE };
// [STATE] Wheel and drag motion are quantized to a fixed small delta instead of
// raw device units, which keeps the thumb motion independent of OS scroll steps.
#define SCROLL_D_MOTION 4

class ScrolledWindow;

// [INTENT] MyScrollbar is a thin custom-drawn wxPanel that mirrors a
// ScrolledWindow's viewport with a manually painted thumb, tip caps, and track.
// [STATE] It caches the current virtual extent, visible extent, thumb colors,
// mouse drag origin, and scroll-unit offset so paint and gesture handlers can
// stay in sync without querying the content view every frame.
// [EVENT] Mouse and paint handlers are bound in the implementation to translate
// clicks, drags, and wheel motion into ScrolledWindow::Scroll calls.
// [UNITY] Port as a retained scroll controller with a custom thumb VisualElement
// and a shared normalized scroll model for both drag and wheel input.
// [PORTING_HAZARD:P2] The current API exposes pixel math, non-owning view state,
// and direction-specific margin handling that should not be copied literally.
class MyScrollbar : public wxPanel
{
public:
    MyScrollbar(wxWindow*       parent,
                wxWindowID      id,
                wxPoint         position,
                wxSize          size,
                ScrolledWindow* scrolledWindow,
                long            direction,
                int             scrollbarWidth,
                int             tipLength = 0);
    void SetViewStart(int start);
    void SetTipColor(wxColour color);
    void SetMarginColor(wxColour color);
    void SetScrollbarColor(wxColour color);
    void SetScrollbarTip(int len);
    void SetVirtualDim(int pixelsPerUnit, int noUnits);

private:
    long            m_direction;              // [STATE] wxVSCROLL or wxHSCROLL determines geometry and drag axis.
    int             m_virtualDim;             // [STATE] Total content span in pixels.
    int             m_actualDim;              // [STATE] Visible track length in the current orientation.
    int             m_pixelsPerUnit;          // [STATE] Scroll-unit to pixel conversion used to sync with content.
    int             m_viewStartInPixels;      // [STATE] Cached viewport origin expressed in pixels.
    int             m_viewStartInScrollUnits; // [STATE] Cached viewport origin expressed in scroll units.
    int             m_scrollbarWidth;         // [STATE] Thumb thickness along the short axis.
    int             m_marginWidth;            // [STATE] Track margin reserved for the non-scrolling gutter.
    int             m_previousMouse;          // [STATE] Last mouse coordinate used to compute drag delta.
    int             m_mouseLocation;          // [STATE] Hit-test result for drag classification.
    int             m_tipLength;              // [STATE] End-cap length used when painting the track.
    int             m_minSliderSize;          // [STATE] Minimum thumb size to preserve usability.
    wxColour        m_tipColor;               // [STATE] End-cap paint color.
    wxColour        m_marginColor;            // [STATE] Track/background paint color.
    wxColour        m_scrollbarColor;         // [STATE] Thumb paint color.
    ScrolledWindow* m_scrolledWindow;         // [STATE] Non-owning content view that receives scroll updates.

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& WXUNUSED(event));
    void OnEraseBackground(wxEraseEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
};
