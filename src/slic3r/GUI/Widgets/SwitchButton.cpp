#include "SwitchButton.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"

#include "../wxExtensions.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/WxFontUtils.hpp"
#ifdef __APPLE__
#include "libslic3r/MacUtils.hpp"
#endif

#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

// [INTENT] This file implements two closely related switch controls: a skinned bitmap toggle and a
// custom-drawn two-segment board. Both are retained-state widgets that translate clicks into a
// single selection change, rather than exposing a generic control surface.
// [STATE] The bitmap toggle caches paired on/off labels and state-colored track/text/thumb palettes;
// the board caches left/right labels plus enabled/selection flags that drive all painting.
// [EVENT] wxEVT_TOGGLEBUTTON, paint, mouse-down, enter/leave, and wxCUSTOMEVT_SWITCH_POS form the
// entire interaction surface. Unity should model this as a retained segmented-switch controller with
// explicit selection callbacks, not as immediate-mode drawing.
// [UNITY] Use a custom MonoBehaviour or UI Toolkit control that owns two labeled segments and a
// controller-driven selected index. The visual state should be authored from data-bound sprites and
// text, with hit-testing routed through standard UI event callbacks.
// [PORTING_HAZARD:P2] The current implementation relies on wx bitmap generation, font metric probing,
// and offscreen DC composition for every rescale; Unity should replace that with a layout-driven
// control to avoid coupling behavior to pixel metrics.
wxDEFINE_EVENT(wxCUSTOMEVT_SWITCH_POS, wxCommandEvent);

SwitchButton::SwitchButton(wxWindow* parent, wxWindowID id)
    : wxBitmapToggleButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT)
    , m_on(this, "toggle_on", 16)
    , m_off(this, "toggle_off", 16)
    , text_color(std::pair{0xfffffe, (int) StateColor::Checked}, std::pair{0x6B6B6B, (int) StateColor::Normal})
    , track_color(0xD9D9D9)
    , thumb_color(std::pair{0x009688, (int) StateColor::Checked}, std::pair{0xD9D9D9, (int) StateColor::Normal})
{
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) {
        update();
        e.Skip();
    });
    SetFont(Label::Body_12);
    Rescale();
}

void SwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
    labels[0] = lbl_on;
    labels[1] = lbl_off;
    Rescale();
}

void SwitchButton::SetTextColor(StateColor const& color)
{
    text_color = color;
    Rescale();
}

void SwitchButton::SetTextColor2(StateColor const& color)
{
    text_color2 = color;
    Rescale();
}

void SwitchButton::SetTrackColor(StateColor const& color)
{
    track_color = color;
    Rescale();
}

void SwitchButton::SetThumbColor(StateColor const& color)
{
    thumb_color = color;
    Rescale();
}

void SwitchButton::SetValue(bool value)
{
    if (value != GetValue()) {
        wxBitmapToggleButton::SetValue(value);
        update();
    }
}

bool SwitchButton::SetBackgroundColour(const wxColour& colour)
{
    if (wxBitmapToggleButton::SetBackgroundColour(colour)) {
        Rescale();
        return true;
    }

    return false;
}

void SwitchButton::Rescale()
{
    // [INTENT] Rebuild the on/off bitmaps from the current labels, colors, and platform scale so the
    // toggle remains visually self-contained.
    // [STATE] The method depends on the cached labels, track/thumb palettes, and current parent
    // background; any of those inputs changing invalidates the bitmap cache.
    // [UNITY] In Unity, this should become a layout recalculation plus sprite/text refresh, not a
    // redraw into a cached bitmap.
    if (labels[0].IsEmpty()) {
        m_on.msw_rescale();
        m_off.msw_rescale();
    } else {
        wxBitmapToggleButton::SetBackgroundColour(StaticBox::GetParentBackgroundColor(GetParent()));
#ifdef __WXOSX__
        auto scale = Slic3r::GUI::mac_max_scaling_factor();
        int  BS    = (int) scale;
#else
        constexpr int BS = 1;
#endif
        wxSize     thumbSize;
        wxSize     trackSize;
        wxClientDC dc(this);
#ifdef __WXOSX__
        dc.SetFont(dc.GetFont().Scaled(scale));
#endif
        wxSize textSize[2];
        {
            textSize[0] = dc.GetTextExtent(labels[0]);
            textSize[1] = dc.GetTextExtent(labels[1]);
        }
        float fontScale = 0;
        {
            thumbSize = textSize[0];
            auto size = textSize[1];
            if (size.x > thumbSize.x)
                thumbSize.x = size.x;
            else
                size.x = thumbSize.x;
            thumbSize.x += BS * 12;
            thumbSize.y += BS * 6;
            trackSize.x   = thumbSize.x + size.x + BS * 10;
            trackSize.y   = thumbSize.y + BS * 2;
            auto maxWidth = GetMaxWidth();
#ifdef __WXOSX__
            maxWidth *= scale;
#endif
            if (trackSize.x > maxWidth) {
                fontScale = float(maxWidth) / trackSize.x;
                thumbSize.x -= (trackSize.x - maxWidth) / 2;
                trackSize.x = maxWidth;
            }
        }
        for (int i = 0; i < 2; ++i) {
            wxMemoryDC memdc(&dc);
#ifdef __WXMSW__
            wxBitmap bmp(trackSize.x, trackSize.y);
            memdc.SelectObject(bmp);
            memdc.SetBackground(wxBrush(GetBackgroundColour()));
            memdc.Clear();
#else
            wxImage image(trackSize);
            image.InitAlpha();
            memset(image.GetAlpha(), 0, trackSize.GetWidth() * trackSize.GetHeight());
            wxBitmap bmp(std::move(image));
            memdc.SelectObject(bmp);
#endif
            memdc.SetFont(dc.GetFont());
            if (fontScale) {
                memdc.SetFont(dc.GetFont().Scaled(fontScale));
                textSize[0] = memdc.GetTextExtent(labels[0]);
                textSize[1] = memdc.GetTextExtent(labels[1]);
            }
            auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
            {
#ifdef __WXMSW__
                wxGCDC dc2(memdc);
#else
                wxDC& dc2(memdc);
#endif
                dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
                dc2.SetPen(wxPen(track_color.colorForStates(state)));
                dc2.DrawRoundedRectangle(wxRect({0, 0}, trackSize), trackSize.y / 2);
                dc2.SetBrush(wxBrush(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
                dc2.SetPen(wxPen(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
                dc2.DrawRoundedRectangle(wxRect({i == 0 ? BS : (trackSize.x - thumbSize.x - BS), BS}, thumbSize), thumbSize.y / 2);
            }
            memdc.SetTextForeground(text_color.colorForStates(state ^ StateColor::Checked));
            auto text_y = BS + (thumbSize.y - textSize[0].y) / 2;
#ifdef __APPLE__
            if (Slic3r::is_mac_version_15()) {
                text_y -= FromDIP(2);
            }
#endif
            memdc.DrawText(labels[0], {BS + (thumbSize.x - textSize[0].x) / 2, text_y});
            memdc.SetTextForeground(text_color2.count() == 0 ? text_color.colorForStates(state) : text_color2.colorForStates(state));
            auto text_y_1 = BS + (thumbSize.y - textSize[1].y) / 2;
#ifdef __APPLE__
            if (Slic3r::is_mac_version_15()) {
                text_y_1 -= FromDIP(2);
            }
#endif
            memdc.DrawText(labels[1], {trackSize.x - thumbSize.x - BS + (thumbSize.x - textSize[1].x) / 2, text_y_1});
            memdc.SelectObject(wxNullBitmap);
#ifdef __WXOSX__
            bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
#endif
            (i == 0 ? m_off : m_on).bmp() = bmp;
        }
    }
    SetSize(m_on.GetBmpSize());
    update();
}

void SwitchButton::update()
{
    // [EVENT] Propagate the logical toggle value into the active bitmap selection.
    SetBitmap((GetValue() ? m_on : m_off).bmp());
}

SwitchBoard::SwitchBoard(wxWindow* parent, wxString leftL, wxString right, wxSize size)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, size)
{
    // [INTENT] Present a two-option, fixed-size switch surface that paints its own selected side and
    // emits a custom selection event on click.
    // [STATE] Selection is stored as two booleans plus an enabled gate; the control assumes a fixed
    // width so its labels can be centered within each half.
    // [UNITY] This maps cleanly to a retained segmented control or toggle group with one selected index
    // and one disabled flag.
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    SetBackgroundColour(*wxWHITE);
    leftLabel  = leftL;
    rightLabel = right;

    SetMinSize(size);
    SetMaxSize(size);

    Bind(wxEVT_PAINT, &SwitchBoard::paintEvent, this);
    Bind(wxEVT_LEFT_DOWN, &SwitchBoard::on_left_down, this);

    Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
}

void SwitchBoard::updateState(wxString target)
{
    // [EVENT] External callers use string tokens to drive the selected side; empty clears the
    // selection and any other value resolves to left/right.
    // [PORTING_HAZARD:P3] Stringly-typed control updates are fragile for a Unity port; this should be a
    // typed enum or integer index in the next implementation.
    if (target.empty()) {
        if (!switch_left && !switch_right) {
            return;
        }

        switch_left  = false;
        switch_right = false;
    } else {
        if (target == "left") {
            if (switch_left && !switch_right) {
                return;
            }

            switch_left  = true;
            switch_right = false;
        } else if (target == "right") {
            if (!switch_left && switch_right) {
                return;
            }

            switch_left  = false;
            switch_right = true;
        }
    }

    Refresh();
}

void SwitchBoard::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void SwitchBoard::render(wxDC& dc)
{
    // [OPENGL] None here; the widget is CPU-painted through wxDC, with a Windows-specific offscreen
    // buffer to reduce flicker.
    // [PORTING_HAZARD:P2] The offscreen bitmap path is platform-specific presentation logic that should
    // disappear in Unity in favor of a single retained visual tree.
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

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

void SwitchBoard::doRender(wxDC& dc)
{
    // [INTENT] Draw the inactive base, the active half, and the centered labels using the current
    // selection and enable state.
    // [STATE] The active color and text contrast are derived from switch_left/switch_right/is_enable.
    // [UNITY] Use a styled segmented button pair with a selected-state overlay, not manual rounded-rect
    // painting.
    wxColour disable_color = wxColour(0xCECECE);

    dc.SetPen(*wxTRANSPARENT_PEN);

    if (is_enable) {
        dc.SetBrush(wxBrush(0xeeeeee));
    } else {
        dc.SetBrush(disable_color);
    }
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 8);

    /*left*/
    if (switch_left) {
        is_enable ? dc.SetBrush(wxBrush(wxColour(0, 150, 136))) : dc.SetBrush(disable_color);
        dc.DrawRoundedRectangle(0, 0, GetSize().x / 2, GetSize().y, 8);
    }

    if (switch_left) {
        dc.SetTextForeground(*wxWHITE);
    } else {
        dc.SetTextForeground(0x333333);
    }

    dc.SetFont(::Label::Body_13);
    Slic3r::GUI::WxFontUtils::get_suitable_font_size(0.6 * GetSize().GetHeight(), dc);

    auto left_txt_size = dc.GetTextExtent(leftLabel);
    dc.DrawText(leftLabel, wxPoint((GetSize().x / 2 - left_txt_size.x) / 2, (GetSize().y - left_txt_size.y) / 2));

    /*right*/
    if (switch_right) {
        if (is_enable) {
            dc.SetBrush(wxBrush(wxColour(0, 150, 136)));
        } else {
            dc.SetBrush(disable_color);
        }
        dc.DrawRoundedRectangle(GetSize().x / 2, 0, GetSize().x / 2, GetSize().y, 8);
    }

    auto right_txt_size = dc.GetTextExtent(rightLabel);
    if (switch_right) {
        dc.SetTextForeground(*wxWHITE);
    } else {
        dc.SetTextForeground(0x333333);
    }
    dc.DrawText(rightLabel, wxPoint((GetSize().x / 2 - right_txt_size.x) / 2 + GetSize().x / 2, (GetSize().y - right_txt_size.y) / 2));
}

void SwitchBoard::on_left_down(wxMouseEvent& evt)
{
    // [EVENT] Mouse clicks choose the left or right half, optionally auto-disable the widget, then
    // broadcast the new index via wxCUSTOMEVT_SWITCH_POS.
    // [PORTING_HAZARD:P2] The control couples hit testing, state mutation, and event emission in one
    // handler; Unity should split these into pointer routing, state update, and callback dispatch.
    if (!is_enable) {
        return;
    }
    int  index = -1;
    auto pos   = ClientToScreen(evt.GetPosition());
    auto rect  = ClientToScreen(wxPoint(0, 0));

    if (pos.x > 0 && pos.x < rect.x + GetSize().x / 2) {
        switch_left  = true;
        switch_right = false;
        index        = 1;
    } else {
        switch_left  = false;
        switch_right = true;
        index        = 0;
    }

    if (auto_disable_when_switch) {
        is_enable = false; // make it disable while switching
    }
    Refresh();

    wxCommandEvent event(wxCUSTOMEVT_SWITCH_POS);
    event.SetInt(index);
    wxPostEvent(this, event);
}

void SwitchBoard::Enable()
{
    // [STATE] Enable/Disable are explicit gates for both rendering and click handling; they also
    // trigger a refresh so the gray disabled palette appears immediately.
    if (is_enable == true) {
        return;
    }

    is_enable = true;
    Refresh();
}

void SwitchBoard::Disable()
{
    // [STATE] Disable mirrors Enable and keeps the cached selection intact while suppressing input.
    if (is_enable == false) {
        return;
    }

    is_enable = false;
    Refresh();
}
