#include "LabeledStaticBox.hpp"
#include "libslic3r/Utils.hpp"
#include "../GUI.hpp"
#include "../GUI_Utils.hpp"
#include "Label.hpp"

LabeledStaticBox::LabeledStaticBox() : state_handler(this)
{
    // [INTENT] Retained static-box shell with a painted border and label strip.
    // [STATE] Cache the shared palette, corner geometry, and label font so the widget can repaint without re-deriving theme data.
    // [UNITY] Map this to a retained panel/container with a themed border and a separate title Text element, not a single native group box.
    m_radius         = 3;
    m_border_width   = 1;
    m_font           = Label::Head_14;
    text_color       = StateColor(std::make_pair(0x363636, (int) StateColor::Normal), std::make_pair(0x6B6B6B, (int) StateColor::Disabled));
    background_color = StateColor(std::make_pair(0xFFFFFF, (int) StateColor::Normal), std::make_pair(0xF0F0F1, (int) StateColor::Disabled));
    border_color     = StateColor(std::make_pair(0xDBDBDB, (int) StateColor::Normal), std::make_pair(0xDBDBDB, (int) StateColor::Disabled));
}

LabeledStaticBox::LabeledStaticBox(wxWindow* parent, const wxString& label, const wxPoint& pos, const wxSize& size, long style)
    : LabeledStaticBox()
{ Create(parent, label, pos, size, style); }

bool LabeledStaticBox::Create(wxWindow* parent, const wxString& label, const wxPoint& pos, const wxSize& size, long style)
{
    // [STATE] Borderless style collapses the outline entirely; the label measurement is cached because the paint path reads it directly.
    // [EVENT] Paint is owned locally via wxEVT_PAINT; StateHandler then keeps the colors synced with enable/disable state.
    // [PORTING_HAZARD:P3] macOS margin fixes and paint-style differences mean the Unity port needs explicit padding/theme rules per platform.
    if (style & wxBORDER_NONE)
        m_border_width = 0;
    wxStaticBox::Create(parent, wxID_ANY, label, pos, size, style);
#ifdef __WXOSX__
    Slic3r::GUI::staticbox_remove_margin(this);
#endif

    m_label = label;
    m_scale = FromDIP(100) / 100.f;
    m_pos   = this->GetPosition();

    int tW, tH, descent, externalLeading;
    // empty label sets m_label_height as 0 that causes extra spacing at top
    GetTextExtent(m_label.IsEmpty() ? "Orca" : m_label, &tW, &tH, &descent, &externalLeading, &m_font);
    m_label_height = tH - externalLeading;
    m_label_width  = tW;

    Bind(wxEVT_PAINT, ([this](wxPaintEvent e) {
             wxPaintDC dc(this);
             PickDC(dc);
         }));

    state_handler.attach({&text_color, &background_color, &border_color});
    state_handler.update_binds();
#ifndef __WXOSX__
    SetBackgroundStyle(wxBG_STYLE_PAINT);
#endif
    SetBackgroundColour(background_color.colorForStates(state_handler.states()));
    SetForegroundColour(text_color.colorForStates(state_handler.states()));
    SetBorderColor(border_color.colorForStates(state_handler.states()));
    SetCanFocus(false);
    DisableFocusFromKeyboard();
    return true;
}

void LabeledStaticBox::SetCornerRadius(int radius)
{
    this->m_radius = radius;
    Refresh();
}

void LabeledStaticBox::SetBorderWidth(int width)
{
    this->m_border_width = width;
    Refresh();
}

void LabeledStaticBox::SetBorderColor(StateColor const& color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void LabeledStaticBox::SetFont(wxFont set_font)
{
    // [STATE] Font changes invalidate the cached label extents, which control both the text background width and the top inset.
    m_font = set_font;

    int tW, tH, descent, externalLeading;
    // empty label sets m_label_height as 0 that causes extra spacing at top
    GetTextExtent(m_label.IsEmpty() ? "Orca" : m_label, &tW, &tH, &descent, &externalLeading, &m_font);
    m_label_height = tH - externalLeading;
    m_label_width  = tW;

    Refresh();
}

bool LabeledStaticBox::Enable(bool enable)
{
    // [EVENT] Enable changes are surfaced as a custom event so higher-level views can react to disabled groups as a distinct UI state.
    bool result = this->wxStaticBox::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
        this->SetForegroundColour(text_color.colorForStates(state_handler.states()));
        this->SetBorderColor(border_color.colorForStates(state_handler.states()));
    }
    return result;
}

void LabeledStaticBox::PickDC(wxDC& dc)
{
    // [OPENGL] None here; this is a CPU paint path with a Windows double-buffer fallback to avoid flicker.
    // [UNITY] Preserve the offscreen buffer as a retained layout pass plus normal UI repaint, not as a DC-specific draw routine.
#ifdef __WXMSW__
    wxSize size = GetSize();
    if (size.x <= 0 || size.y <= 0)
        return;
    wxMemoryDC memdc(&dc);
    if (!memdc.IsOk()) {
        DrawBorderAndLabel(dc);
        return;
    }
    wxBitmap bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.SetBackground(wxBrush(GetBackgroundColour()));
    memdc.Clear();
    {
        wxGCDC dc2(memdc);
        DrawBorderAndLabel(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    DrawBorderAndLabel(dc);
#endif
}

void LabeledStaticBox::DrawBorderAndLabel(wxDC& dc)
{
    // [INTENT] Paint the full group-box body, then overdraw a label cutout so the title appears embedded in the top border.
    // [STATE] The label background and rounded border both depend on the current theme colors and the measured label width/height.
    // fill full background
    dc.SetBackground(wxBrush(background_color.colorForStates(0)));
    dc.Clear();

    wxSize wSz = GetSize();

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(border_color.colorForStates(state_handler.states()), m_border_width, wxPENSTYLE_SOLID));
    dc.DrawRoundedRectangle( // Border
        std::max(0, m_pos.x), std::max(0, m_pos.y) + m_label_height * .5, wSz.GetWidth(), wSz.GetHeight() - m_label_height * .5,
        m_radius * m_scale);

    if (!m_label.IsEmpty()) {
        dc.SetFont(m_font);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(background_color.colorForStates(0)));
        dc.DrawRectangle(wxRect(7 * m_scale, 0, m_label_width + 7 * m_scale, m_label_height)); // text background
        // [PORTING_HAZARD:P2] Long labels can overflow the reserved strip; Unity needs layout-driven clipping/ellipsis instead of this
        // fixed-width draw.
        dc.SetTextForeground(text_color.colorForStates(state_handler.states()));
        dc.DrawText(m_label, wxPoint(10 * m_scale, 0));
    }
}

void LabeledStaticBox::GetBordersForSizer(int* borderTop, int* borderOther) const
{
    // [UNITY] The border offsets are part of layout contract; the Unity container should expose equivalent padding so sibling sizers line up.
    wxStaticBox::GetBordersForSizer(borderTop, borderOther);
#ifdef __WXOSX__
    *borderOther = 5; // Make sure macOS uses the same border padding as other platforms
#endif
}
