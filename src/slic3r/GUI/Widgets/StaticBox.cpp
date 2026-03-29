#include "StaticBox.hpp"
#include "../GUI.hpp"
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

// [INTENT] StaticBox is a skinned container, not a data-entry control: it owns a paint-only rounded rect,
// optional gradient fill, and an overlay badge while delegating visual-state resolution to StateHandler.
// [STATE] The retained styling inputs are corner radius, border width/style, three StateColor palettes,
// and the optional badge bitmap; Create() also inherits a parent-derived background color.
// [EVENT] The wxWidgets surface is intentionally tiny: state changes trigger Refresh(), and the only live
// event path is EVT_PAINT plus a limited erase handler for the transparent-background workaround.
// [UNITY] Port as a retained panel/VisualElement with a custom draw component and a separate style model
// for border/fill/badge state, rather than a stock button or group box.
// [PORTING_HAZARD:P2] Gradient fills and badge compositing are done during paint with immediate-mode DC math,
// so Unity should replace them with explicit layered visuals instead of trying to mirror the pixel loop.

BEGIN_EVENT_TABLE(StaticBox, wxWindow)

// catch paint events
// EVT_ERASE_BACKGROUND(StaticBox::eraseEvent)
EVT_PAINT(StaticBox::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

StaticBox::StaticBox() : state_handler(this), radius(8)
{ border_color = StateColor(std::make_pair(0xF0F0F1, (int) StateColor::Disabled), std::make_pair(0xCECECE, (int) StateColor::Normal)); }

StaticBox::StaticBox(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style) : StaticBox()
{ Create(parent, id, pos, size, style); }

bool StaticBox::Create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
{
    // [INTENT] Creation wires the state bridge once, then derives a background that matches the parent or
    // nested StaticBox chain so the skinned border can blend into surrounding chrome.
    if (style & wxBORDER_NONE)
        border_width = 0;
    wxWindow::Create(parent, id, pos, size, style);
    state_handler.attach({&border_color, &background_color, &background_color2});
    state_handler.update_binds();
    SetBackgroundColour(GetParentBackgroundColor(parent));
    return true;
}

void StaticBox::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void StaticBox::SetBorderStyle(wxPenStyle style)
{
    border_style = style;
    Refresh();
}

void StaticBox::SetBorderWidth(int width)
{
    border_width = width;
    Refresh();
}

void StaticBox::SetBorderColor(StateColor const& color)
{
    if (border_color != color) {
        // [EVENT] Palette updates refresh the StateHandler bindings before repaint so state-specific colors
        // stay in sync with hover/disable/focus changes.
        border_color = color;
        state_handler.update_binds();
        Refresh();
    }
}

void StaticBox::SetBorderColorNormal(wxColor const& color)
{
    border_color.setColorForStates(color, 0);
    Refresh();
}

void StaticBox::SetBackgroundColor(StateColor const& color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void StaticBox::SetBackgroundColorNormal(wxColor const& color)
{
    background_color.setColorForStates(color, 0);
    Refresh();
}

void StaticBox::SetBackgroundColor2(StateColor const& color)
{
    background_color2 = color;
    state_handler.update_binds();
    Refresh();
}

wxColor StaticBox::GetParentBackgroundColor(wxWindow* parent)
{
    // [INTENT] Prefer the ancestor StaticBox blend when nesting skinned containers; otherwise inherit the
    // direct parent background so the outer chrome remains visually continuous.
    if (auto box = dynamic_cast<StaticBox*>(parent)) {
        if (box->background_color.count() > 0) {
            if (box->background_color2.count() == 0)
                return box->background_color.defaultColor();
            auto s = box->background_color.defaultColor();
            auto e = box->background_color2.defaultColor();
            int  r = (s.Red() + e.Red()) / 2;
            int  g = (s.Green() + e.Green()) / 2;
            int  b = (s.Blue() + e.Blue()) / 2;
            return wxColor(r, g, b);
        }
    }
    if (parent)
        return parent->GetBackgroundColour();
    return *wxWHITE;
}

void StaticBox::ShowBadge(bool show)
{
    // [STATE] The badge is optional overlay chrome, lazily materialized so the control does not keep the
    // bitmap alive unless the caller explicitly requests it.
    if (show && badge.name() != "badge") {
        badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !badge.name().empty()) {
        badge = ScalableBitmap{};
        Refresh();
    }
}

void StaticBox::eraseEvent(wxEraseEvent& evt)
{
    // for transparent background, but not work
#ifdef __WXMSW__
    wxDC*      dc   = evt.GetDC();
    wxSize     size = GetSize();
    wxClientDC dc2(GetParent());
    dc->Blit({0, 0}, size, &dc2, GetPosition());
#endif
}

void StaticBox::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void StaticBox::render(wxDC& dc)
{
    // [OPENGL] none; this is pure device-context rendering. The Windows path uses an offscreen bitmap only
    // to preserve rounded corners when the native DC path cannot composite cleanly.
#ifdef __WXMSW__
    if (radius == 0) {
        doRender(dc);
        return;
    }

    wxSize size = GetSize();
    if (size.x <= 0 || size.y <= 0)
        return;
    wxMemoryDC memdc(&dc);
    if (!memdc.IsOk()) {
        doRender(dc);
        return;
    }
    wxBitmap bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    // memdc.Blit({0, 0}, size, &dc, {0, 0});
    memdc.SetBackground(wxBrush(GetBackgroundColour()));
    memdc.Clear();
    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void StaticBox::doRender(wxDC& dc)
{
    // [INTENT] Draw the retained style state into the current client rect, choosing either a solid/outlined
    // rounded rectangle or a vertical gradient, then paint the badge last as foreground chrome.
    wxSize size   = GetSize();
    int    states = state_handler.states();
    if (background_color2.count() == 0) {
        if ((border_width && border_color.count() > 0) || background_color.count() > 0) {
            wxRect rc(0, 0, size.x, size.y);
            if (border_width && border_color.count() > 0) {
                if (dc.GetContentScaleFactor() == 1.0) {
                    int d  = floor(border_width / 2.0);
                    int d2 = floor(border_width - 1);
                    rc.x += d;
                    rc.width -= d2;
                    rc.y += d;
                    rc.height -= d2;
                } else {
                    int d = 1;
                    rc.x += d;
                    rc.width -= d;
                    rc.y += d;
                    rc.height -= d;
                }
                dc.SetPen(wxPen(border_color.colorForStates(states), border_width, border_style));
            } else {
                dc.SetPen(wxPen(background_color.colorForStates(states)));
            }
            if (background_color.count() > 0)
                dc.SetBrush(wxBrush(background_color.colorForStates(states)));
            else
                dc.SetBrush(wxBrush(GetBackgroundColour()));
            if (radius == 0) {
                dc.DrawRectangle(rc);
            } else {
                dc.DrawRoundedRectangle(rc, radius - border_width);
            }
        }
    } else {
        wxColor start = background_color.colorForStates(states);
        wxColor stop  = background_color2.colorForStates(states);
        int     r = start.Red(), g = start.Green(), b = start.Blue();
        int     dr = (int) stop.Red() - r, dg = (int) stop.Green() - g, db = (int) stop.Blue() - b;
        int     lr = 0, lg = 0, lb = 0;
        for (int y = 0; y < size.y; ++y) {
            dc.SetPen(wxPen(wxColor(r, g, b)));
            dc.DrawLine(0, y, size.x, y);
            lr += dr;
            while (lr >= size.y) {
                ++r, lr -= size.y;
            }
            while (lr <= -size.y) {
                --r, lr += size.y;
            }
            lg += dg;
            while (lg >= size.y) {
                ++g, lg -= size.y;
            }
            while (lg <= -size.y) {
                --g, lg += size.y;
            }
            lb += db;
            while (lb >= size.y) {
                ++b, lb -= size.y;
            }
            while (lb <= -size.y) {
                --b, lb += size.y;
            }
        }
    }

    if (badge.bmp().IsOk()) {
        auto s = badge.bmp().GetScaledSize();
        dc.DrawBitmap(badge.bmp(), size.x - s.x, 0);
    }
}
