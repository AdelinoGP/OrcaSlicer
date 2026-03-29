#ifndef slic3r_GUI_ImageSwitchButton_hpp_
#define slic3r_GUI_ImageSwitchButton_hpp_

#include "../wxExtensions.hpp"
#include "StateColor.hpp"
#include "StateHandler.hpp"
#include "Button.hpp"

#include <wx/tglbtn.h>

// [INTENT] A compact retained toggle button that swaps between on/off images and labels while owning its own hover/press feedback.
// [STATE] The control keeps bitmap copies, a boolean on/off value, cached text extents, and label pairs so it can repaint without outside
// help. [UNITY] Port this as a small prefab with an Image, TextMeshPro label, and explicit pointer-state handling in a custom controller.
// [PORTING_HAZARD:P2] Size is recomputed from wxDC text measurement and manual centering, so Unity needs a layout-driven rule instead of
// paint-time geometry.
class ImageSwitchButton : public StaticBox
{
public:
    ImageSwitchButton(wxWindow* parent, ScalableBitmap& img_on, ScalableBitmap& img_off, long style = 0);

    void SetLabels(wxString const& lbl_on, wxString const& lbl_off);
    void SetImages(ScalableBitmap& img_on, ScalableBitmap& img_off);
    void SetTextColor(StateColor const& color);
    void SetValue(bool value);
    void SetPadding(int padding);

    bool GetValue() { return m_on_off; }
    void Rescale();

private:
    // [STATE] Cached size measurement for the active label; the implementation refreshes it whenever text, image, or padding changes.
    void messureSize();
    // [EVENT] Paint and mouse handlers live here because the button is self-contained and re-emits a command event after flipping state.
    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void sendButtonEvent();

    DECLARE_EVENT_TABLE()

private:
    ScalableBitmap m_on;                // [STATE] Image used when the toggle is enabled.
    ScalableBitmap m_off;               // [STATE] Image used when the toggle is disabled.
    bool           m_on_off;            // [STATE] Current toggle value.
    int            m_padding;           // [STATE] Manual spacing used during local centering.
    bool           pressedDown = false; // [STATE] Pointer-capture state for click recognition.
    bool           hover       = false; // [STATE] Hover feedback flag.

    wxSize textSize; // [STATE] Cached label extent used by render().
    wxSize minSize;  // [STATE] Minimum size cache retained for layout parity.

    wxString   labels[2];  // [STATE] On/off labels and tooltip source text.
    StateColor text_color; // [STATE] Per-state text color palette.
};

// [INTENT] A fan-specific variant of the image toggle that keeps the same click semantics but can swap to fan/air-condition presentation
// text. [STATE] In addition to the base toggle state, it carries a speed value and freeform text label used by the status panel. [UNITY]
// Model this as the same retained toggle prefab with an alternate view-state enum for fan-specific labels/icons. [PORTING_HAZARD:P3] The
// renderer contains special-case spacing for literal strings like "Fan" and "Air Condition", which is brittle to port as-is.
class FanSwitchButton : public StaticBox
{
public:
    FanSwitchButton(wxWindow* parent, ScalableBitmap& img_on, ScalableBitmap& img_off, long style = 0);
    void SetLabels(wxString const& lbl_on, wxString const& lbl_off);
    void SetImages(ScalableBitmap& img_on, ScalableBitmap& img_off);
    void SetTextColor(StateColor const& color);
    void SetValue(bool value);
    void SetPadding(int padding);

    bool GetValue() { return m_on_off; }
    void Rescale();
    void setFanValue(int val);

    void UseTextFan();
    void UseTextAirCondition();

private:
    // [STATE] Keeps text measurement in sync with the active fan label; the current cpp also relies on manual string checks in render().
    void messureSize();
    // [EVENT] Same paint/mouse lifecycle as ImageSwitchButton, but the draw path is specialized for fan-status copy.
    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseEnterWindow(wxMouseEvent& event);
    void mouseLeaveWindow(wxMouseEvent& event);
    void sendButtonEvent();

    void SetText(const wxString& text);

    DECLARE_EVENT_TABLE()

private:
    ScalableBitmap m_on;                // [STATE] Enabled-state image.
    ScalableBitmap m_off;               // [STATE] Disabled-state image.
    bool           m_on_off;            // [STATE] Toggle value used by the shared click path.
    int            m_padding;           // [STATE] Legacy spacing knob, retained for parity with the shared toggle.
    bool           pressedDown = false; // [STATE] Pointer capture while the mouse is down.
    bool           hover       = false; // [STATE] Hover feedback flag.

    wxSize textSize; // [STATE] Cached label extent used by render().
    wxSize minSize;  // [STATE] Minimum size cache retained for layout parity.
    int    m_speed;  // [STATE] Fan speed percentage retained for the status panel, even though the current render path does not draw it.

    wxString labels[2]; // [STATE] On/off labels used for tooltip and legacy text flow.

    wxString   m_text;     // [STATE] Optional presentation text such as "Fan" or "Air Condition".
    StateColor text_color; // [STATE] Per-state text color palette.
};

#endif // !slic3r_GUI_SwitchButton_hpp_
