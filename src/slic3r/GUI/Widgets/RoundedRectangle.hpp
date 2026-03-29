#ifndef slic3r_GUI_ROUNDEDRECTANGLE_hpp_
#define slic3r_GUI_ROUNDEDRECTANGLE_hpp_

#include "../wxExtensions.hpp"

// [INTENT] Declaration boundary for a tiny custom-painted wxWindow that renders a rounded rectangle as either a filled
//          swatch or an outline-only frame.
// [STATE] The widget retains only style inputs — color, radius, and the integer mode flag — and recomputes geometry from
//         the current client size on each paint instead of caching any layout or bitmap state.
// [EVENT] OnPaint is the sole exposed handler, so the runtime behavior is entirely driven by wxWidgets paint invalidation.
// [UNITY] Port this as a retained VisualElement/custom graphic with rounded-corner styling for fill mode and an explicit
//         outline renderer for border-only mode.
// [PORTING_HAZARD:P3] The mode is an untyped int rather than an enum, so the Unity port should preserve the two render
//                     branches explicitly and avoid collapsing them into one generic shape style.
class RoundedRectangle : public wxWindow
{
public:
    RoundedRectangle(wxWindow* parent, wxColour col, wxPoint pos, wxSize size, double radius, int type = 0);
    ~RoundedRectangle() {};

private:
    double   m_radius;
    int      m_type;
    wxColour m_color;

public:
    void OnPaint(wxPaintEvent& evt);
    DECLARE_EVENT_TABLE()
};
#endif // !slic3r_GUI_RoundedRectangle_hpp_
