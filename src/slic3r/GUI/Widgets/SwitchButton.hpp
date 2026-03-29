#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include "../wxExtensions.hpp"
#include "StateColor.hpp"

#include <wx/tglbtn.h>
#include "Label.hpp"
#include "Button.hpp"

// [INTENT] SwitchButton is a bitmap-backed toggle with separate on/off art, dual text labels, and custom state-color styling for the track,
// thumb, and label text. [STATE] It owns two scalable bitmaps plus four palette/label fields that mirror the current toggle value and
// rescale logic. [EVENT] Value changes, background-color overrides, and the private update() path keep the rendered art,
// wxBitmapToggleButton state, and custom labels synchronized. [UNITY] Port as a retained Toggle with a custom graphic child or a two-state
// segmented control controller, backed by a shared visual-state model for track/thumb/text colors. [PORTING_HAZARD:P2] The control mixes
// button semantics with bespoke bitmap rendering, so a stock Unity toggle will not preserve its look without a custom renderer.

wxDECLARE_EVENT(wxCUSTOMEVT_SWITCH_POS, wxCommandEvent);

class SwitchButton : public wxBitmapToggleButton
{
public:
    SwitchButton(wxWindow* parent = NULL, wxWindowID id = wxID_ANY);

public:
    void SetLabels(wxString const& lbl_on, wxString const& lbl_off);

    void SetTextColor(StateColor const& color);

    void SetTextColor2(StateColor const& color);

    void SetTrackColor(StateColor const& color);

    void SetThumbColor(StateColor const& color);

    void SetValue(bool value) override;

    void Rescale();

    bool SetBackgroundColour(const wxColour& colour) override;

private:
    void update();

private:
    ScalableBitmap m_on;
    ScalableBitmap m_off;

    wxString   labels[2];
    StateColor text_color;
    StateColor text_color2;
    StateColor track_color;
    StateColor thumb_color;
};

class SwitchBoard : public wxWindow
{
public:
    // [INTENT] SwitchBoard is a lightweight wrapper around the switch UI: it stores the left/right labels, current left/right selection
    // flags, and an optional external payload used to drive printer state. [STATE] The exposed booleans act like a mini view-model for the
    // active side, while client_data is a non-owning bridge (currently documented as MachineObject*) into the larger status panel. [EVENT]
    // Mouse-driven rendering and updateState() mutate the selection flags, and Enable/Disable gate whether the board can react to
    // interaction. [UNITY] Port as a retained panel/controller with an explicit selection enum and typed view-model payload instead of a
    // raw void* bridge. [PORTING_HAZARD:P2] The combination of custom painting, raw client_data, and manual enable/disable flow means the
    // Unity version needs a structured presenter rather than a direct widget swap.
    SwitchBoard(wxWindow* parent = NULL, wxString leftL = "", wxString right = "", wxSize size = wxDefaultSize);
    wxString leftLabel;
    wxString rightLabel;

    void updateState(wxString target);

    bool switch_left{false};
    bool switch_right{false};
    bool is_enable{true};

    void* client_data = nullptr; /*MachineObject* in StatusPanel*/

public:
    void Enable();
    void Disable();
    bool IsEnabled() { return is_enable; };

    void  SetClientData(void* data) { client_data = data; };
    void* GetClientData() { return client_data; };

    void SetAutoDisableWhenSwitch() { auto_disable_when_switch = true; };

protected:
    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
    void on_left_down(wxMouseEvent& evt);

private:
    bool auto_disable_when_switch = false;
};

#endif // !slic3r_GUI_SwitchButton_hpp_
