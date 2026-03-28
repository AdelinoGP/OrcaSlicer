#ifndef slic3r_GUI_TabButton_hpp_
#define slic3r_GUI_TabButton_hpp_

#include "wxExtensions.hpp"
#include "Widgets/StaticBox.hpp"

class TabButton : public StaticBox
{
    // [STATE] Cached text/icon metrics and visual toggles drive the custom paint/layout path; the button owns its bitmaps,
    // while pressedDown/show_new_tag mirror transient interaction state.
    wxSize         textSize;
    wxSize         minSize;
    wxSize         paddingSize;
    ScalableBitmap icon;
    ScalableBitmap newtag_img;

    StateColor text_color;
    StateColor border_color;
    bool       pressedDown  = false;
    bool       show_new_tag = false;

public:
    // [INTENT] Custom-painted tab/rail button with icon, label, and optional "new" badge.
    // [UNITY] Map to a retained UI Toolkit Button/Toggle with icon+label visuals and parent-owned selection state.
    // [PORTING_HAZARD:P2] Click handling is implicit through wx paint/mouse events rather than a standard toggle model.
    TabButton();

    TabButton(wxWindow* parent, wxString text, ScalableBitmap& icon, long style = 0, int iconSize = 0);

    bool Create(wxWindow* parent, wxString text, ScalableBitmap& icon, long style = 0, int iconSize = 0);

    // [STATE] These mutators keep cached measurements, colors, and bitmaps in sync with the current label and DPI state.
    // [UNITY] Mirror this with data-bound properties on a retained tab/toggle row that invalidates layout on change.
    void SetLabel(const wxString& label) override;

    void SetMinSize(const wxSize& size) override;

    void SetPaddingSize(const wxSize& size);

    const wxSize& GetPaddingSize();

    void SetTextColor(StateColor const& color);

    void SetBorderColor(StateColor const& color);

    void SetBGColor(StateColor const& color);

    void SetBitmap(ScalableBitmap& bitmap);

    // [INTENT] Enable() keeps the button's clickable affordance aligned with parent selection logic; Rescale() refreshes
    // bitmap sizing and cached layout when DPI or icon size changes.
    bool Enable(bool enable = true);

    void Rescale();

    void ShowNewTag(bool tag = false)
    {
        show_new_tag = tag;
        Refresh();
    };
    bool GetShowNewTag() const { return show_new_tag; };

private:
    // [EVENT] Paint and mouse handlers translate wx input into button-press visuals and command-event forwarding.
    // [UNITY] Recreate this as a controller that raises selection clicks through standard UI event dispatch.
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
