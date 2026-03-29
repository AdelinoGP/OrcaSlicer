#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "Scrollbar.hpp"
#include "ScrolledWindow.hpp"

// [INTENT] MyScrollbar is a hand-rolled replacement for wxScrollBar that paints
// a thin thumb, tip caps, and margin gutters around a companion ScrolledWindow.
// [STATE] It owns only view geometry/cache state; scrolling itself is delegated
// back to the non-owning m_scrolledWindow pointer.
// [UNITY] Port as a retained ScrollRect-style controller with a custom thumb
// VisualElement that pushes normalized scroll offsets into the content view.
MyScrollbar::MyScrollbar(wxWindow*       parent,
                         wxWindowID      id,
                         wxPoint         position,
                         wxSize          size,
                         ScrolledWindow* scrolledWindow,
                         long            direction,
                         int             scrollbarWidth,
                         int             tipLength)
    : wxPanel(parent, id, position, size)
{
    m_parent    = parent;
    m_direction = direction;
    m_tipLength = tipLength;
    if (m_direction == wxVSCROLL)
        m_virtualDim = size.GetHeight();
    else
        m_virtualDim = size.GetWidth(); // default values

    m_actualDim = GetSize().GetHeight();

    m_viewStartInScrollUnits = 0;
    m_viewStartInPixels      = 0;
    m_minSliderSize          = 30;

    m_scrollbarWidth = scrollbarWidth;
    m_marginWidth    = scrollbarWidth;

    m_tipColor       = m_parent->GetBackgroundColour(); // default value. Can be changed with SetTipColour
    m_marginColor    = m_parent->GetBackgroundColour(); // default value. Can be changed with SetMarginColour
    m_scrollbarColor = wxColour(255, 255, 255);         // default value. Can be changed with SetScrollbarColour
    m_scrolledWindow = scrolledWindow;

    Bind(wxEVT_PAINT, &MyScrollbar::OnPaint, this);
    Bind(wxEVT_SIZE, &MyScrollbar::OnSize, this);
    Bind(wxEVT_ERASE_BACKGROUND, &MyScrollbar::OnEraseBackground, this);
    Bind(wxEVT_LEFT_DOWN, &MyScrollbar::OnMouseLeftDown, this);
    Bind(wxEVT_LEFT_UP, &MyScrollbar::OnMouseLeftUp, this);
    Bind(wxEVT_MOTION, &MyScrollbar::OnMouseMove, this);
    Bind(wxEVT_MOUSEWHEEL, &MyScrollbar::OnMouseWheel, this);
}

void MyScrollbar::SetViewStart(int start)
{
    // [STATE] The scroll thumb is driven from the external content viewport; the
    // widget stores both the logical scroll-unit offset and the pixel-equivalent.
    m_viewStartInScrollUnits = start;
    m_viewStartInPixels      = start;
}

void MyScrollbar::SetTipColor(wxColour color) { m_tipColor = color; }

void MyScrollbar::SetMarginColor(wxColour color) { m_marginColor = color; }

void MyScrollbar::SetScrollbarColor(wxColour color) { m_scrollbarColor = color; }

void MyScrollbar::SetScrollbarTip(int len) { m_tipLength = len; }

void MyScrollbar::SetVirtualDim(int pixelsPerUnit, int noUnits)
{
    // [STATE] Virtual dimension is the content span in pixels; changing it forces
    // a repaint so the thumb geometry is recalculated against the new range.
    m_virtualDim    = pixelsPerUnit * noUnits;
    m_pixelsPerUnit = pixelsPerUnit;
    Refresh();
}

// [EVENT] Size changes recalculate the visible track and whether the opposite
// margin must be reserved for a second scrollbar.
void MyScrollbar::OnSize(wxSizeEvent& WXUNUSED(event))
{
    if (m_direction == wxVSCROLL) {
        m_actualDim = GetSize().GetHeight();
        if (m_actualDim < m_virtualDim)
            m_marginWidth = GetSize().GetWidth();
        else
            m_marginWidth = 0;
    } else {
        m_actualDim = GetSize().GetWidth();
        if (m_actualDim < m_virtualDim)
            m_marginWidth = GetSize().GetHeight();
        else
            m_marginWidth = 0;
    }
}

void MyScrollbar::OnPaint(wxPaintEvent& event)
// [INTENT] Paint the track in one of two orientations and derive the thumb from
// the current viewport fraction. The math is intentionally immediate-mode and
// clamps the thumb to a minimum size for usability.
// [PORTING_HAZARD:P2] Unity should not copy this pixel math verbatim; instead,
// compute a normalized scroll ratio and let layout/rendering size the thumb.
// This scrollbar is either a vertical one or an horizontal one
{
    int scrollbarStart, scrollbarLength, scrollbarSide;
    // wxBufferedPaintDC dc(this);
    wxClientDC dc(this);
    PrepareDC(dc);
    int actualDim = m_actualDim;

    // m_marginWidth = 10;
    // m_scrollbarWidth = 10;

    if (m_direction == wxVSCROLL && actualDim < m_virtualDim) // if actualDim >= m_virtualDim, the scrollbar is hidden, no need to paint it
    {
        scrollbarSide   = (m_marginWidth - m_scrollbarWidth) / 2;
        scrollbarStart  = (m_pixelsPerUnit * actualDim * m_viewStartInScrollUnits) / (float) m_virtualDim +
                          0.5; // + 0.5 is a cheap way to round the float value
        scrollbarLength = (actualDim * actualDim) / (float) m_virtualDim + 0.5;

        if (scrollbarLength < m_minSliderSize) {
            scrollbarStart  = (actualDim - m_minSliderSize) *
                                  (m_viewStartInScrollUnits * m_pixelsPerUnit / (float) (m_virtualDim - actualDim)) +
                              0.5;
            scrollbarLength = m_minSliderSize;
        }

        if (scrollbarStart < 0) // the mouse is out of the panel
            scrollbarStart = 0;
        else if (scrollbarStart + scrollbarLength >= actualDim)
            scrollbarStart = actualDim - scrollbarLength;

        // draw the upper and lower tips
        dc.SetPen(m_tipColor);
        dc.SetBrush(m_tipColor);
        dc.DrawRectangle(scrollbarSide, 0, m_scrollbarWidth, m_tipLength);                       // upper tip
        dc.DrawRectangle(scrollbarSide, actualDim - m_tipLength, m_scrollbarWidth, m_tipLength); // lower tip

        // draw the margins
        dc.SetPen(m_marginColor);
        dc.SetBrush(m_marginColor);
        dc.DrawRectangle(scrollbarSide, m_tipLength, m_scrollbarWidth, scrollbarStart + m_tipLength); // above the scrollbar
        dc.DrawRectangle(scrollbarSide, scrollbarStart + scrollbarLength - m_tipLength, m_scrollbarWidth,
                         actualDim - scrollbarLength - scrollbarStart);               // below the scrollbar
        dc.DrawRectangle(0, 0, scrollbarSide, actualDim);                             // left side of the scrollbar
        dc.DrawRectangle(m_marginWidth - scrollbarSide, 0, scrollbarSide, actualDim); // right side of the scrollbar

        // draw the scrollbar
        dc.SetPen(m_scrollbarColor);
        dc.SetBrush(m_scrollbarColor);
        dc.DrawRectangle(scrollbarSide, scrollbarStart + m_tipLength, m_scrollbarWidth, scrollbarLength - 2 * m_tipLength);
    } else if (m_direction == wxHSCROLL &&
               actualDim < m_virtualDim) // if actualDim >= m_virtualDim, the scrollbar is hidden, no need to paint it
    {
        if (m_scrolledWindow->IsBothDirections())
            actualDim -= m_marginWidth; // must take into account the right margin

        scrollbarSide   = (m_marginWidth - m_scrollbarWidth) / 2;
        scrollbarStart  = (m_pixelsPerUnit * actualDim * m_viewStartInScrollUnits) / (float) m_virtualDim +
                          0.5; // + 0.5 is a cheap way round the float value
        scrollbarLength = (actualDim * actualDim) / (float) m_virtualDim + 0.5;

        if (scrollbarLength < m_minSliderSize) {
            scrollbarStart  = (actualDim - m_minSliderSize) *
                                  (m_viewStartInScrollUnits * m_pixelsPerUnit / (float) (m_virtualDim - actualDim)) +
                              0.5;
            scrollbarLength = m_minSliderSize;
        }

        if (scrollbarStart < 0) // the mouse is out of the panel
            scrollbarStart = 0;
        else if (scrollbarStart + scrollbarLength >= actualDim)
            scrollbarStart = actualDim - scrollbarLength;

        // draw the left and right tips
        dc.SetPen(m_tipColor);
        dc.SetBrush(m_tipColor);
        dc.DrawRectangle(0, scrollbarSide, m_tipLength, m_scrollbarWidth);                       // left tip
        dc.DrawRectangle(actualDim - m_tipLength, scrollbarSide, m_tipLength, m_scrollbarWidth); // right tip

        // draw the margins
        dc.SetPen(m_marginColor);
        dc.SetBrush(m_marginColor);
        dc.DrawRectangle(m_tipLength, scrollbarSide, scrollbarStart + m_tipLength, m_scrollbarWidth); // left of the scrollbar
        dc.DrawRectangle(scrollbarStart + scrollbarLength - m_tipLength, scrollbarSide, actualDim - scrollbarLength - scrollbarStart,
                         m_scrollbarWidth);                                           // right of the scrollbar
        dc.DrawRectangle(0, 0, actualDim, scrollbarSide);                             // above the scrollbar
        dc.DrawRectangle(0, m_marginWidth - scrollbarSide, actualDim, scrollbarSide); // below the scrollbar
        dc.DrawRectangle(actualDim, 0, m_marginWidth, m_marginWidth);                 // lower right corner of the scrolled window

        // draw the scrollbar
        dc.SetPen(m_scrollbarColor);
        dc.SetBrush(m_scrollbarColor);
        dc.DrawRectangle(scrollbarStart + m_tipLength, scrollbarSide, scrollbarLength - 2 * m_tipLength, m_scrollbarWidth);
    }
}

void MyScrollbar::OnEraseBackground(wxEraseEvent& event)
{
    // [EVENT] Suppress wxWidgets' default background erase so the custom-drawn
    // track does not flicker between drag and resize repaints.
    // necessary to avoid automatic background erasing
    SetBackgroundColour(wxColor(255, 255, 255));
}

void MyScrollbar::OnMouseLeftDown(wxMouseEvent& event)
{
    // [EVENT] Mouse-down decides whether dragging starts on the thumb or in the
    // empty track; drag capture keeps the gesture alive outside the panel.
    int scrollbarStart, scrollbarLength, eventPos;

    if (m_direction == wxVSCROLL)
        eventPos = event.m_y;
    else
        eventPos = event.m_x;

    int actualDim   = m_actualDim;
    scrollbarStart  = (m_pixelsPerUnit * actualDim * m_viewStartInScrollUnits) / (float) m_virtualDim +
                      0.5; // + 0.5 is a cheap way to round the float value
    scrollbarLength = MAX((actualDim * actualDim) / (float) m_virtualDim + 0.5, m_minSliderSize);

    m_previousMouse = eventPos;
    /*eventPos >= scrollbarStart &&*/
    if (eventPos < scrollbarStart + scrollbarLength)
        m_mouseLocation = ON_SCROLLBAR;
    else
        m_mouseLocation = BEFORE_SCROLLBAR;

    CaptureMouse();
}

void MyScrollbar::OnMouseLeftUp(wxMouseEvent& event)
{
    // [EVENT] Releasing the mouse ends the drag gesture and clears hit-state so
    // the next click can reclassify the cursor position.
    if (HasCapture()) {
        ReleaseMouse();
    }
    m_mouseLocation = NOWHERE;
}

void MyScrollbar::OnMouseMove(wxMouseEvent& event)
{
    // [EVENT] Dragging mutates the cached viewport offsets, then forwards the
    // new position to ScrolledWindow::Scroll so the content pane stays in sync.
    // [STATE] m_previousMouse, m_viewStartInPixels, and m_viewStartInScrollUnits
    // are the only persistent inputs to the thumb position.
    int dMotion, actualDim;
    if (!event.Dragging())
        return; // button is not pressed, the user doesn't scroll

    actualDim = m_actualDim;

    if (m_direction == wxVSCROLL && m_mouseLocation == ON_SCROLLBAR) {
        dMotion = event.m_y - m_previousMouse;
        m_viewStartInPixels += (dMotion * m_virtualDim) / (float) actualDim + 0.5; // in pixels

        if (m_viewStartInPixels < 0)
            m_viewStartInPixels = 0;
        else if (m_viewStartInPixels + actualDim >= m_virtualDim)
            m_viewStartInPixels = m_virtualDim - actualDim;

        m_viewStartInScrollUnits = m_viewStartInPixels / (float) m_pixelsPerUnit + 0.5; // in scroll units
        m_scrolledWindow->Scroll(-1, m_viewStartInScrollUnits);                         // -1 means no change in this direction
        SetViewStart(m_viewStartInScrollUnits);
        m_previousMouse = event.m_y;
    } else if (m_direction == wxHSCROLL && m_mouseLocation == ON_SCROLLBAR) {
        if (m_scrolledWindow->IsBothDirections())
            actualDim -= m_marginWidth; // must take into account the right margin

        dMotion = event.m_x - m_previousMouse;
        m_viewStartInPixels += (dMotion * m_virtualDim) / (float) actualDim + 0.5; // in pixels

        if (m_viewStartInPixels < 0)
            m_viewStartInPixels = 0;
        else if (m_viewStartInPixels + actualDim >= m_virtualDim)
            m_viewStartInPixels = m_virtualDim - actualDim;

        m_viewStartInScrollUnits = m_viewStartInPixels / (float) m_pixelsPerUnit + 0.5; // in scroll units
        m_scrolledWindow->Scroll(m_viewStartInScrollUnits, -1);                         // -1 means no change in this direction
        SetViewStart(m_viewStartInScrollUnits);
        m_previousMouse = event.m_x;
    }
    Refresh();
    Update();
}

void MyScrollbar::OnMouseWheel(wxMouseEvent& event)
{
    // [EVENT] Wheel deltas are translated into a fixed motion quantum instead of
    // raw device units, which keeps the custom scrollbar behavior consistent.
    // [UNITY] This should become a scroll-wheel bridge that updates the same
    // normalized scroll model used by the thumb-drag path.
    int dMotion, actualDim;
    if (event.GetWheelRotation() > 0)
        // dMotion = -m_pixelsPerUnit;
        dMotion = -SCROLL_D_MOTION;
    else
        // dMotion = m_pixelsPerUnit;
        dMotion = SCROLL_D_MOTION;

    // m_previousMouse += dMotion;
    actualDim = m_actualDim;

    if (m_direction == wxVSCROLL) {
        m_viewStartInPixels += (dMotion * m_virtualDim) / (float) actualDim + 0.5; // in pixels

        if (m_viewStartInPixels < 0)
            m_viewStartInPixels = 0;
        else if (m_viewStartInPixels + actualDim >= m_virtualDim)
            m_viewStartInPixels = m_virtualDim - actualDim;

        m_viewStartInScrollUnits = m_viewStartInPixels / (float) m_pixelsPerUnit + 0.5; // in scroll units
        m_scrolledWindow->Scroll(-1, m_viewStartInScrollUnits);                         // -1 means no change in this direction
        SetViewStart(m_viewStartInScrollUnits);
    } else {
        dMotion = -dMotion;
        if (m_scrolledWindow->IsBothDirections())
            actualDim -= m_marginWidth; // must take into account the right margin

        m_viewStartInPixels += (dMotion * m_virtualDim) / (float) actualDim + 0.5; // in pixels

        if (m_viewStartInPixels < 0)
            m_viewStartInPixels = 0;
        else if (m_viewStartInPixels + actualDim >= m_virtualDim)
            m_viewStartInPixels = m_virtualDim - actualDim;

        m_viewStartInScrollUnits = m_viewStartInPixels / (float) m_pixelsPerUnit + 0.5; // in scroll units
        m_scrolledWindow->Scroll(m_viewStartInScrollUnits, -1);                         // -1 means no change in this direction
        SetViewStart(m_viewStartInScrollUnits);
    }
    Refresh();
    Update();
}
