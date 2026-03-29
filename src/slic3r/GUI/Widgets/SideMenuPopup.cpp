#include "SideMenuPopup.hpp"
#include "Label.hpp"

#include <wx/display.h>
#include <wx/dcgraph.h>
#include "../GUI_App.hpp"

wxBEGIN_EVENT_TABLE(SidePopup, PopupWindow)
EVT_PAINT(SidePopup::paintEvent)
wxEND_EVENT_TABLE()

// [INTENT] Construct a borderless popup that can host a vertical stack of side buttons.
// [UNITY] A retained popup container should own the layout instead of reconstructing it on each show.
SidePopup::SidePopup(wxWindow* parent)
    : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
}

SidePopup::~SidePopup() { ; }

// [EVENT] Dismissal updates the app-wide side-menu popup flag before delegating to the base popup lifecycle.
// [PORTING_HAZARD:P3] This relies on global app state, so the Unity port needs an equivalent shared visibility signal.
void SidePopup::OnDismiss()
{
    Slic3r::GUI::wxGetApp().set_side_menu_popup_status(false);
    PopupWindow::OnDismiss();
}

// [EVENT] Mouse handling is entirely delegated to PopupWindow; this class only preserves the specialization point.
bool SidePopup::ProcessLeftDown(wxMouseEvent& event) { return PopupWindow::ProcessLeftDown(event); }
// [STATE] Visibility toggling is likewise delegated, keeping popup lifecycle centralized in the base class.
bool SidePopup::Show(bool show) { return PopupWindow::Show(show); }

// [INTENT] Rebuild the popup size and screen position from the current button list and the anchor widget.
// [STATE] The popup width is the maximum child min width; height is the sum of child heights.
// [PORTING_HAZARD:P2] Positioning clamps against the current display geometry and applies an Apple-specific x-offset correction.
void SidePopup::Popup(wxWindow* focus)
{
    Create();
    auto drect       = wxDisplay(GetParent()).GetGeometry();
    int  screenwidth = drect.x + drect.width;
    // int screenwidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X,NULL);

    int max_width = 0;

    for (auto btn : btn_list) {
        max_width = std::max(btn->GetMinSize().x, max_width);
    }
    if (focus) {
        wxPoint pos = focus->ClientToScreen(wxPoint(0, -6));

#ifdef __APPLE__
        pos.x = pos.x - FromDIP(20);
#endif // __APPLE__

        if (pos.x + max_width > screenwidth)
            Position({pos.x - (pos.x + max_width - screenwidth), pos.y}, {0, focus->GetSize().y + 12});
        else
            Position(pos, {0, focus->GetSize().y + 12});
    }
    Slic3r::GUI::wxGetApp().set_side_menu_popup_status(true);
    PopupWindow::Popup();
}

// [INTENT] Materialize the popup content as a vertical sizer using the current button set.
// [STATE] Each button is resized to the shared max width so the popup reads as one aligned menu column.
// [UNITY] Equivalent behavior is a rebuilt VerticalLayoutGroup/ListView item column with a measured preferred width.
void SidePopup::Create()
{
    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    int max_width = 0;
    int height    = 0;
    for (auto btn : btn_list) {
        max_width = std::max(btn->GetMinSize().x, max_width);
    }

    for (auto btn : btn_list) {
        wxSize size = btn->GetMinSize();
        height += size.y;
        size.x = max_width;
        btn->SetMinSize(size);
        btn->SetSize(size);
        sizer->Add(btn, 0, 0, 0);
    }

    SetSize(wxSize(max_width, height));

    SetSizer(sizer, true);

    Layout();
    Refresh();
}

// [OPENGL] None; this is a transparent paint pass for the popup background/holes.
// [INTENT] Paint a transparent rectangle so the popup frame does not draw its own solid chrome.
void SidePopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    wxSize    size = GetSize();
    dc.SetBrush(wxTransparentColour);
    dc.DrawRectangle(0, 0, size.x, size.y);
}

// [STATE] The popup retains a caller-owned button list; buttons are appended before Create()/Popup() lays them out.
void SidePopup::append_button(SideButton* btn) { btn_list.push_back(btn); }
