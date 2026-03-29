#ifndef slic3r_GUI_StaticBox_hpp_
#define slic3r_GUI_StaticBox_hpp_

#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

#include <wx/window.h>

// [INTENT] Declaration boundary for a skinned container that paints its own rounded-rect chrome, optional
// gradient fill, and a badge overlay while leaving the hosted content to the parent layout.
// [STATE] The retained style model is corner radius, border width/style, three StateColor palettes, and the
// optional badge bitmap; StateHandler owns the visual-state binding to those palettes.
// [EVENT] Creation, palette setters, and ShowBadge() all refresh the control after rebinding state colors,
// while paint/erase handlers are the only rendering callbacks.
// [UNITY] Port as a retained panel or VisualElement with a custom draw component plus layered border/fill/
// badge visuals and a separate style model.
// [PORTING_HAZARD:P2] The Windows offscreen-bitmap path and runtime gradient compositing are immediate-mode
// painting details, so Unity should not try to reproduce them with stock container chrome.
class StaticBox : public wxWindow
{
public:
    StaticBox();

    StaticBox(wxWindow*      parent,
              wxWindowID     id    = wxID_ANY,
              const wxPoint& pos   = wxDefaultPosition,
              const wxSize&  size  = wxDefaultSize,
              long           style = 0);

    bool Create(wxWindow*      parent,
                wxWindowID     id    = wxID_ANY,
                const wxPoint& pos   = wxDefaultPosition,
                const wxSize&  size  = wxDefaultSize,
                long           style = 0);

    void SetCornerRadius(double radius);

    void SetBorderWidth(int width);

    void SetBorderColor(StateColor const& color);

    void SetBorderColorNormal(wxColor const& color);

    void SetBorderStyle(wxPenStyle style);

    void SetBackgroundColor(StateColor const& color);

    void SetBackgroundColorNormal(wxColor const& color);

    void SetBackgroundColor2(StateColor const& color);

    static wxColor GetParentBackgroundColor(wxWindow* parent);

    void ShowBadge(bool show);

protected:
    void eraseEvent(wxEraseEvent& evt);

    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    virtual void doRender(wxDC& dc);

protected:
    // [STATE] `state_handler` is a non-owning bridge bound to the three palette objects; `badge` is lazily
    // materialized overlay chrome so the bitmap only exists when explicitly shown.
    double         radius;
    int            border_width = 1;
    wxPenStyle     border_style = wxPENSTYLE_SOLID;
    StateHandler   state_handler;
    StateColor     border_color;
    StateColor     background_color;
    StateColor     background_color2;
    ScalableBitmap badge;

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_StaticBox_hpp_
