#include "ImageSwitchButton.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"
#include "../wxExtensions.hpp"

#include "slic3r/GUI/I18N.hpp"
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(ImageSwitchButton, StaticBox)

EVT_LEFT_DOWN(ImageSwitchButton::mouseDown)
EVT_ENTER_WINDOW(ImageSwitchButton::mouseEnterWindow)
EVT_LEAVE_WINDOW(ImageSwitchButton::mouseLeaveWindow)
EVT_LEFT_UP(ImageSwitchButton::mouseReleased)
EVT_PAINT(ImageSwitchButton::paintEvent)

END_EVENT_TABLE()

BEGIN_EVENT_TABLE(FanSwitchButton, StaticBox)

EVT_LEFT_DOWN(FanSwitchButton::mouseDown)
EVT_ENTER_WINDOW(FanSwitchButton::mouseEnterWindow)
EVT_LEAVE_WINDOW(FanSwitchButton::mouseLeaveWindow)
EVT_LEFT_UP(FanSwitchButton::mouseReleased)
EVT_PAINT(FanSwitchButton::paintEvent)

END_EVENT_TABLE()

static const wxColour DEFAULT_HOVER_COL = wxColour(0, 150, 136);
static const wxColour DEFAULT_PRESS_COL = wxColour(238, 238, 238);

// [INTENT] ImageSwitchButton is a retained two-state image toggle used by the status/dashboard UI.
// [STATE] It owns paired on/off bitmaps, label variants, hover/press flags, and cached text extents so repainting can stay local.
// [EVENT] wxWidgets mouse and paint events are translated into a command-style click event after the control flips its own boolean state.
// [UNITY] Port this as a compact prefab with an Image and TextMeshPro label plus explicit pointer-enter/down/up handlers.
// [PORTING_HAZARD:P2] The control's visible size is derived from immediate wxDC measurement, so Unity needs layout-driven sizing instead of
// paint-time recomputation.
ImageSwitchButton::ImageSwitchButton(wxWindow* parent, ScalableBitmap& img_on, ScalableBitmap& img_off, long style)
    : text_color(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), std::make_pair(*wxBLACK, (int) StateColor::Normal))
{
    radius           = 0;
    m_padding        = 0;
    m_on             = img_on;
    m_off            = img_off;
    background_color = StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled),
                                  std::make_pair(DEFAULT_PRESS_COL, (int) StateColor::Pressed),
                                  std::make_pair(*wxWHITE, (int) StateColor::Normal));
    border_color     = StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled),
                                  std::make_pair(DEFAULT_HOVER_COL, (int) StateColor::Focused),
                                  std::make_pair(DEFAULT_HOVER_COL, (int) StateColor::Hovered),
                                  std::make_pair(*wxWHITE, (int) StateColor::Normal));

    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);

    messureSize();
    Refresh();
}

void ImageSwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
    labels[0]     = lbl_on;
    labels[1]     = lbl_off;
    auto fina_txt = GetValue() ? labels[0] : labels[1];
    if (GetToolTipText() != fina_txt)
        SetToolTip(fina_txt);
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetImages(ScalableBitmap& img_on, ScalableBitmap& img_off)
{
    m_on  = img_on;
    m_off = img_off;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetTextColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetValue(bool value)
{
    m_on_off = value;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetPadding(int padding)
{
    m_padding = padding;
    messureSize();
    Refresh();
}

void ImageSwitchButton::messureSize()
{
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    textSize = dc.GetTextExtent(GetValue() ? labels[0] : labels[1]);
}

void ImageSwitchButton::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void ImageSwitchButton::render(wxDC& dc)
{
    StaticBox::render(dc);
    int    states = state_handler.states();
    wxSize size   = GetSize();

    wxSize          szIcon;
    wxSize          szContent = textSize;
    ScalableBitmap& icon      = GetValue() ? m_on : m_off;

    int content_height = icon.GetBmpHeight() + textSize.y + m_padding;

    wxPoint pt = wxPoint((size.x - icon.GetBmpWidth()) / 2, (size.y - content_height) / 2);
    if (icon.bmp().IsOk()) {
        dc.DrawBitmap(icon.bmp(), pt);
        pt.y += m_padding + icon.GetBmpHeight();
    }
    pt.x = (size.x - textSize.x) / 2;
    dc.SetFont(GetFont());
    if (!IsEnabled())
        dc.SetTextForeground(text_color.colorForStates(StateColor::Disabled));
    else
        dc.SetTextForeground(text_color.colorForStates(states));

    auto fina_txt = GetValue() ? labels[0] : labels[1];
    if (dc.GetTextExtent(fina_txt).x > size.x) {
        wxString forment_txt = wxEmptyString;
        for (auto i = 0; i < fina_txt.length(); i++) {
            forment_txt = fina_txt.SubString(0, i) + "...";
            if (dc.GetTextExtent(forment_txt).x > size.x) {
                pt.x = (size.x - dc.GetTextExtent(forment_txt).x) / 2;
                dc.DrawText(forment_txt, pt);
                break;
            }
        }
    } else {
        dc.DrawText(fina_txt, pt);
    }
}

void ImageSwitchButton::Rescale() { messureSize(); }

void ImageSwitchButton::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void ImageSwitchButton::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        m_on_off = !m_on_off;
        Refresh();
        sendButtonEvent();
    }
}

void ImageSwitchButton::mouseEnterWindow(wxMouseEvent& event)
{
    if (!hover) {
        hover = true;
        Refresh();
    }
}

void ImageSwitchButton::mouseLeaveWindow(wxMouseEvent& event)
{
    if (hover) {
        hover = false;
        Refresh();
    }
}

void ImageSwitchButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}

// [INTENT] FanSwitchButton reuses the same toggle mechanics but specializes the label/image presentation for fan controls.
// [STATE] It adds an explicit fan-speed value and a freeform text label, so the status panel can switch between fan-centric copy modes.
// [EVENT] The click/hover pipeline matches ImageSwitchButton, but the rendered text path is tuned for the dashboard's fan and air-condition
// variants. [UNITY] Model this as the same retained image-toggle prefab with a second presentation mode driven by a view-state enum.
// [PORTING_HAZARD:P3] The fan label path contains hand-tuned text placement for specific strings, which will need a more declarative layout
// rule in Unity.
FanSwitchButton::FanSwitchButton(wxWindow* parent, ScalableBitmap& img_on, ScalableBitmap& img_off, long style)
    : text_color(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), std::make_pair(*wxBLACK, (int) StateColor::Normal))
{
    radius           = 0;
    m_padding        = 0;
    m_speed          = 0;
    m_on             = img_on;
    m_off            = img_off;
    background_color = StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled),
                                  std::make_pair(DEFAULT_PRESS_COL, (int) StateColor::Pressed),
                                  std::make_pair(*wxWHITE, (int) StateColor::Normal));
    border_color     = StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled),
                                  std::make_pair(DEFAULT_HOVER_COL, (int) StateColor::Focused),
                                  std::make_pair(DEFAULT_HOVER_COL, (int) StateColor::Hovered),
                                  std::make_pair(*wxWHITE, (int) StateColor::Normal));

    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);

    messureSize();
    Refresh();
}

void FanSwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
    labels[0]     = lbl_on;
    labels[1]     = lbl_off;
    auto fina_txt = GetValue() ? labels[0] : labels[1];
    SetToolTip(fina_txt);
    messureSize();
    Refresh();
}

void FanSwitchButton::SetImages(ScalableBitmap& img_on, ScalableBitmap& img_off)
{
    m_on  = img_on;
    m_off = img_off;
    messureSize();
    Refresh();
}

void FanSwitchButton::SetTextColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
    messureSize();
    Refresh();
}

void FanSwitchButton::SetValue(bool value)
{
    m_on_off = value;
    messureSize();
    Refresh();
}

void FanSwitchButton::SetPadding(int padding)
{
    m_padding = padding;
    messureSize();
    Refresh();
}

void FanSwitchButton::messureSize()
{
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    textSize = dc.GetTextExtent(GetValue() ? labels[0] : labels[1]);
}

void FanSwitchButton::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void FanSwitchButton::render(wxDC& dc)
{
    StaticBox::render(dc);
    int    states = state_handler.states();
    wxSize size   = GetSize();

    wxSize          szIcon;
    wxSize          szContent = textSize;
    ScalableBitmap& icon      = GetValue() ? m_on : m_off;

    // int content_height = icon.GetBmpHeight() + textSize.y + m_padding;

    wxPoint pt = wxPoint(FromDIP(10), (size.y - icon.GetBmpHeight()) / 2);

    if (icon.bmp().IsOk()) {
        dc.DrawBitmap(icon.bmp(), pt);
    }

    if (!m_text.empty()) {
        if (m_text == _L("Fan")) {
            dc.SetFont(::Label::Head_15);
            pt.x += icon.GetBmpWidth() + FromDIP(9);
        } else if (m_text == _L("Air Condition")) {
            dc.SetFont(::Label::Head_14);
            pt.x += icon.GetBmpWidth() + FromDIP(6);
        }

        auto text_size = dc.GetMultiLineTextExtent(m_text);
        pt.y           = (size.y - text_size.GetHeight()) / 2;
        // dc.SetTextForeground(0x6b6b6b);
        dc.DrawText(m_text, pt);
    }

    // int content_height = icon.GetBmpHeight() + textSize.y + m_padding;
    /*int content_height = m_padding;

    wxPoint pt = wxPoint((size.x - icon.GetBmpWidth()) / 2, (size.y - content_height) / 2);

    pt.x = (size.x - textSize.x) / 2;
    dc.SetFont(GetFont());
    if (!IsEnabled())
        dc.SetTextForeground(text_color.colorForStates(StateColor::Disabled));
    else
        dc.SetTextForeground(text_color.colorForStates(states));

    auto fina_txt = GetValue() ? labels[0] : labels[1];
    if (dc.GetTextExtent(fina_txt).x > size.x) {
        wxString forment_txt = wxEmptyString;
        for (auto i = 0; i < fina_txt.length(); i++) {
            forment_txt = fina_txt.SubString(0, i) + "...";
            if (dc.GetTextExtent(forment_txt).x > size.x) {
                pt.x = (size.x - dc.GetTextExtent(forment_txt).x) / 2;
                dc.DrawText(forment_txt, wxPoint(pt.x, content_height));
                break;
            }
        }
    }
    else {
        dc.DrawText(fina_txt,  wxPoint(pt.x, content_height));
    }

    pt = wxPoint((size.x - icon.GetBmpWidth()) / 2, content_height + textSize.y);
    if (icon.bmp().IsOk()) {
        dc.DrawBitmap(icon.bmp(), pt);
        pt.y += m_padding + icon.GetBmpHeight();
    }

    auto speed = wxString::Format("%d%%", m_speed);

    dc.SetFont(GetFont());
    if (!IsEnabled())
        dc.SetTextForeground(text_color.colorForStates(StateColor::Disabled));
    else
        dc.SetTextForeground(text_color.colorForStates(states));

    pt.x = (size.x - dc.GetTextExtent(speed).x) / 2;
    pt.y += FromDIP(1);
    dc.DrawText(speed, pt);*/
}

void FanSwitchButton::Rescale() { messureSize(); }

void FanSwitchButton::setFanValue(int val)
{
    m_speed = val;
    Refresh();
}

void FanSwitchButton::UseTextFan() { SetText(_L("Fan")); }
void FanSwitchButton::UseTextAirCondition() { SetText(_L("Air Condition")); }

void FanSwitchButton::SetText(const wxString& text)
{
    if (m_text != text) {
        m_text = text;
        Refresh();
    }
}

void FanSwitchButton::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void FanSwitchButton::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        // m_on_off = !m_on_off;
        Refresh();
        sendButtonEvent();
    }
}

void FanSwitchButton::mouseEnterWindow(wxMouseEvent& event)
{
    if (!hover) {
        hover = true;
        Refresh();
    }
}

void FanSwitchButton::mouseLeaveWindow(wxMouseEvent& event)
{
    if (hover) {
        hover = false;
        Refresh();
    }
}

void FanSwitchButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
