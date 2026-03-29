#ifndef slic3r_GUI_SideButton_hpp_
#define slic3r_GUI_SideButton_hpp_

#include <wx/stattext.h>
#include <wx/vlbox.h>
#include <wx/combo.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

// [INTENT] SideButton is a skinned wxWindow that behaves like a button but owns its own icon/text layout and state-colored chrome.
// [UNITY] Port as a retained button controller with explicit icon-label layout and a custom painter rather than stock button styling.
// [PORTING_HAZARD:P2] The control mixes hover/press/disable state with layout math, so Unity should keep state and geometry in one view-model.

class SideButton : public wxWindow
{
public:
    // [STATE] Horizontal text placement is an explicit layout mode, not a derived consequence of alignment anchors.
    enum EHorizontalOrientation : unsigned char { HO_Left, HO_Center, HO_Right, Num_Horizontal_Orientations };

    SideButton(wxWindow* parent, wxString text, wxString icon = "", long style = 0, int iconSize = 0);

    // [STATE] Corner, padding, and icon offsets all feed paint-time geometry and minimum-size calculation.
    void SetCornerRadius(double radius);

    // BBS set enable array
    void SetCornerEnable(const std::vector<bool>& enable);

    // [STATE] Text alignment is owned by the widget and directly affects the hit surface and label placement.
    void SetTextLayout(EHorizontalOrientation orient, int margin = 15);

    void SetLayoutStyle(int style);

    void SetLabel(const wxString& label);

    bool SetForegroundColour(wxColour const& colour) override;

    bool SetBackgroundColour(wxColour const& color) override;

    bool SetBottomColour(wxColour const& color);

    // [STATE] External min-size requests override the content-derived size until the caller changes it again.
    void SetMinSize(const wxSize& size) override;

    void SetBorderColor(StateColor const& color);

    void SetForegroundColor(StateColor const& color);

    void SetBackgroundColor(StateColor const& color);

    bool Enable(bool enable = true);

    void Rescale();

    void SetExtraSize(const wxSize& size);

    void SetIconOffset(const int offset);

private:
    // [STATE] Cached text, size, and palette state drive both paint output and minimum-size negotiation.
    wxSize            textSize;
    wxSize            minSize;
    ScalableBitmap    icon;
    double            radius;
    wxSize            extra_size;
    int               icon_offset;
    std::vector<bool> radius_enable;

    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   background_color;
    wxColour     bottom_color;

    bool pressedDown  = false;
    int  layout_style = 0;

    EHorizontalOrientation text_orientation;
    int                    text_margin;

    // [EVENT] Paint and mouse handlers translate wx events into custom draw and activation behavior.
    void paintEvent(wxPaintEvent& evt);

    void dorender(wxDC& dc, wxDC& text_dc);

    void messureSize();

    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);

    // [EVENT] Converts the internal click state into a wx command event for parent handlers.
    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};
#endif // !slic3r_GUI_Button_hpp_
