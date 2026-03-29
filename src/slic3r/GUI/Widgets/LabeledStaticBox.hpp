#ifndef slic3r_GUI_LabeledStaticBox_hpp_
#define slic3r_GUI_LabeledStaticBox_hpp_

#include <wx/window.h>
#include <wx/dc.h>
#include <wx/dcgraph.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/statbox.h>
#include <wx/pen.h>

#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/StateHandler.hpp"

// [INTENT] Retained themed container that replaces the native static-box chrome with custom border and label drawing.
// [STATE] Border radius/width, cached font/label metrics, and the color/state handler keep the frame aligned across DPI and enable-state
// changes. [EVENT] The setter surface and `Enable()` are the mutation points; they are expected to invalidate paint and update layout
// padding. [UNITY] Port as a retained group-box/container panel with serialized style properties and an explicit custom-draw pass.
// [PORTING_HAZARD:P2] `wxStaticBox` normally owns some of the spacing/label behavior; Unity must measure the label and padding explicitly.
// [UNCLEAR] `PickDC()` likely selects a platform-appropriate drawing context path; confirm the cpp before replacing the paint backend.
class LabeledStaticBox : public wxStaticBox
{
public:
    LabeledStaticBox();

    LabeledStaticBox(wxWindow*       parent,
                     const wxString& label = wxEmptyString,
                     const wxPoint&  pos   = wxDefaultPosition,
                     const wxSize&   size  = wxDefaultSize,
                     long            style = 0);

    bool Create(wxWindow*       parent,
                const wxString& label = wxEmptyString,
                const wxPoint&  pos   = wxDefaultPosition,
                const wxSize&   size  = wxDefaultSize,
                long            style = 0);

    void SetCornerRadius(int radius);

    void SetBorderWidth(int width);

    void SetBorderColor(StateColor const& color);

    void SetFont(wxFont set_font);

    bool Enable(bool enable) override;

private:
    // [INTENT] Choose the drawing context used by the custom chrome renderer.
    void PickDC(wxDC& dc);

protected:
    // [STATE] Theme and geometry caches shared by the border/label paint path and the sizer padding override.
    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   background_color;
    int          m_border_width;
    int          m_radius;
    wxFont       m_font;
    wxString     m_label;
    int          m_label_height;
    int          m_label_width;
    float        m_scale;
    wxPoint      m_pos;

    // [INTENT] Draw the custom border, background, and label instead of delegating to the native static-box chrome.
    virtual void DrawBorderAndLabel(wxDC& dc);
    // [INTENT] Adjust outer padding so the custom label and border reserve space comparable to the stock control.
    void GetBordersForSizer(int* borderTop, int* borderOther) const override;
};

#endif // !slic3r_GUI_LabeledStaticBox_hpp_
