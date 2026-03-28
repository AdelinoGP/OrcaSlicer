#ifndef slic3r_GUI_AxisCtrlButton_hpp_
#define slic3r_GUI_AxisCtrlButton_hpp_

#include <wx/stattext.h>
#include <wx/vlbox.h>
#include <wx/combo.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

// [INTENT] Retained radial jog button: the class owns the painted sector geometry, hover state,
// and command-event bridge for XY axis nudges plus home.
// [UNITY] Best fit is a custom retained control (UI Toolkit VisualElement or IMGUI) with shared
// sector-hit geometry and a typed click payload instead of a raw wx command event.
// [PORTING_HAZARD:P2] The visible rings and the active hit sectors are coupled, so any reimplementation
// must keep the geometry model, paint path, and hit-testing rules in lockstep.
class AxisCtrlButton : public wxWindow
{
    // [STATE] Minimum size and cached geometry parameters are derived from the current widget size;
    // they define the outer/inner ring radii, blank gap, and center point used by both paint and hit-test.
    wxSize  minSize;
    double  stretch;
    double  r_outer;
    double  r_inner;
    double  r_home;
    double  r_blank;
    double  gap;
    wxPoint center;

    // [STATE] StateHandler keeps the color sets in sync with wx widget state transitions
    // (enabled/disabled/hovered/pressed) so the control can repaint without manual branch logic.
    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   background_color;
    StateColor   inner_background_color;

    // [STATE] The icon is a rescaled, caller-supplied bitmap drawn in the center home target.
    ScalableBitmap m_icon;

    // [STATE] Press tracking suppresses hover recalculation while the mouse is captured.
    bool pressedDown = false;

    // [STATE] The current sector is both the hover highlight and the click payload.
    unsigned char last_pos;
    unsigned char current_pos;
    enum CurrentPos {
        OUTER_UP    = 0,
        OUTER_LEFT  = 1,
        OUTER_DOWN  = 2,
        OUTER_RIGHT = 3,
        INNER_UP    = 4,
        INNER_LEFT  = 5,
        INNER_DOWN  = 6,
        INNER_RIGHT = 7,
        INNER_HOME  = 8,
        UNDEFINED   = 9
    };

public:
    // [INTENT] Construct the control with a caller-owned icon and default widget style.
    AxisCtrlButton(wxWindow* parent, ScalableBitmap& icon, long style = 0);

    // [INTENT] Keep external min-size requests in sync with the radial geometry cache.
    void SetMinSize(const wxSize& size) override;

    // [INTENT] Update the themed text color palette and refresh the control.
    void SetTextColor(StateColor const& color);

    // [INTENT] Update the themed border color palette and refresh the control.
    void SetBorderColor(StateColor const& color);

    // [INTENT] Update the outer-ring fill palette and refresh the control.
    void SetBackgroundColor(StateColor const& color);

    // [INTENT] Update the inner-ring fill palette and refresh the control.
    void SetInnerBackgroundColor(StateColor const& color);

    // [INTENT] Swap the centered icon bitmap without changing the control geometry.
    void SetBitmap(ScalableBitmap& bmp);

    // [INTENT] Repaint after a DPI or theme change; geometry is recomputed lazily through size state.
    void Rescale();

private:
    // [INTENT] Recompute cached geometry from the current size scaling factor.
    void updateParams();

    // [EVENT] wxEVT_PAINT entry point for the custom radial rendering path.
    void paintEvent(wxPaintEvent& evt);

    // [OPENGL] Not applicable; the control renders via wxGraphicsContext on the CPU.
    void render(wxDC& dc);

    // [EVENT] Pointer press captures mouse ownership and starts a command gesture.
    void mouseDown(wxMouseEvent& event);
    // [EVENT] Pointer release ends capture and emits the selected sector if the pointer stayed inside.
    void mouseReleased(wxMouseEvent& event);
    // [EVENT] Motion updates the hovered sector while the mouse is not captured.
    void mouseMoving(wxMouseEvent& event);

    // [EVENT] Emit the clicked sector as a command-style event payload for the parent widget.
    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};
#endif // !slic3r_GUI_Button_hpp_
