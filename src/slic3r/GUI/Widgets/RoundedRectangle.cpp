#include "RoundedRectangle.hpp"
#include "../wxExtensions.hpp"
#include <wx/dcgraph.h>
#include <wx/dcclient.h>

// [INTENT] Tiny custom-drawn window that renders a rounded rectangle as either a filled panel or an outline-only frame.
// [STATE] The constructor snapshots immutable style inputs into m_type, m_color, and m_radius; the widget itself has no
//         interactive state beyond its current size.
// [UNITY] Map this to a retained VisualElement with background/border radius styling for the filled case, or a custom
//         graphic/outline renderer when the border-only mode is required.
BEGIN_EVENT_TABLE(RoundedRectangle, wxWindow)
EVT_PAINT(RoundedRectangle::OnPaint)
END_EVENT_TABLE()

// [EVENT] The paint handler is the only lifecycle hook; wxWidgets delivers invalidation through EVT_PAINT and the control
//         redraws from its current bounds instead of caching a bitmap.
// [PORTING_HAZARD:P3] type is an ad-hoc mode flag rather than a typed render style, so Unity should preserve the two modes
//                     explicitly to avoid conflating filled and outline-only rendering.
RoundedRectangle::RoundedRectangle(wxWindow* parent, wxColour col, wxPoint pos, wxSize size, double radius, int type)
    : wxWindow(parent, wxID_ANY, pos, size, wxBORDER_NONE)
{
    SetBackgroundColour(wxColour(255, 255, 255));
    m_type   = type;
    m_color  = col;
    m_radius = radius;
}

// [INTENT] Paint a rounded rectangle that fills the control's full client rect using the configured color and radius.
// [STATE] m_type selects between a filled solid shape and an outline-only shape; the geometry is recomputed from the
//         current window size each paint, so no extra layout cache is maintained here.
void RoundedRectangle::OnPaint(wxPaintEvent& evt)
{
    // draw RoundedRectangle
    if (m_type == 0) {
        wxPaintDC dc(this);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(m_color));
        dc.DrawRoundedRectangle(0, 0, GetSize().GetWidth(), GetSize().GetHeight(), m_radius);
    }

    // draw RoundedRectangle only board
    if (m_type == 1) {
        wxPaintDC dc(this);
        dc.SetPen(m_color);
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(0, 0, GetSize().GetWidth(), GetSize().GetHeight(), m_radius);
    }
}
