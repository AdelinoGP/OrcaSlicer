#include "SpinInput.hpp"
#include "Label.hpp"
#include "Button.hpp"
#include "TextCtrl.h"

#include <wx/dcgraph.h>

// [INTENT] SpinInput composes a labeled numeric field from a text box plus up/down buttons and keeps
// the value clamped to a caller-supplied range while emitting the same spin-style command event on every change.
// [STATE] The control owns the current numeric value, min/max/step bounds, repeat-delta acceleration, and state-colored
// text/label/background palettes that must stay in sync with the child widgets.
// [EVENT] Keyboard arrows, Enter/focus loss, mouse clicks, mouse-wheel input, and the timer-driven auto-repeat all
// funnel through the same value-update path so the text field and spinner stay consistent.
// [UNITY] Port as a retained numeric stepper made from a TextInput, two icon buttons, and a shared value model; emit a
// typed "value changed" callback rather than relying on wxCommandEvent propagation.
// [PORTING_HAZARD:P2] The current interaction model depends on mouse capture plus a UI timer for press-and-hold repeat,
// which does not map 1:1 to stock Unity UI widgets.

BEGIN_EVENT_TABLE(SpinInput, StaticBox)

EVT_KEY_DOWN(SpinInput::keyPressed)
// EVT_MOUSEWHEEL(SpinInput::mouseWheelMoved)

EVT_PAINT(SpinInput::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

SpinInput::SpinInput()
    : label_color(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), std::make_pair(0x6B6B6B, (int) StateColor::Normal))
    , text_color(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), std::make_pair(0x262E30, (int) StateColor::Normal))
{
    radius           = 0;
    border_width     = 1;
    border_color     = StateColor(std::make_pair(0xDBDBDB, (int) StateColor::Disabled), std::make_pair(0x009688, (int) StateColor::Hovered),
                                  std::make_pair(0xDBDBDB, (int) StateColor::Normal));
    background_color = StateColor(std::make_pair(0xF0F0F1, (int) StateColor::Disabled), std::make_pair(*wxWHITE, (int) StateColor::Normal));
}

SpinInput::SpinInput(wxWindow*      parent,
                     wxString       text,
                     wxString       label,
                     const wxPoint& pos,
                     const wxSize&  size,
                     long           style,
                     int            min,
                     int            max,
                     int            initial,
                     const int&     step)
    : SpinInput()
{ Create(parent, text, label, pos, size, style, min, max, initial, step); }

void SpinInput::Create(wxWindow*      parent,
                       wxString       text,
                       wxString       label,
                       const wxPoint& pos,
                       const wxSize&  size,
                       long           style,
                       int            min,
                       int            max,
                       int            initial,
                       int            step)
{
    StaticBox::Create(parent, wxID_ANY, pos, size);
    SetFont(Label::Body_12);
    wxWindow::SetLabel(label);
    state_handler.attach({&label_color, &text_color});
    state_handler.update_binds();
    text_ctrl = new TextCtrl(this, wxID_ANY, text, {20, 4}, wxDefaultSize, style | wxBORDER_NONE | wxTE_PROCESS_ENTER,
                             wxTextValidator(wxFILTER_DIGITS));
    text_ctrl->SetFont(Label::Body_14);
    text_ctrl->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
    text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());
    state_handler.attach_child(text_ctrl);
    text_ctrl->Bind(wxEVT_KILL_FOCUS, &SpinInput::onTextLostFocus, this);
    text_ctrl->Bind(wxEVT_TEXT_ENTER, &SpinInput::onTextEnter, this);
    text_ctrl->Bind(wxEVT_KEY_DOWN, &SpinInput::keyPressed, this);
    text_ctrl->Bind(wxEVT_RIGHT_DOWN, [this](auto& e) {}); // disable context menu
    button_inc = createButton(true);
    button_dec = createButton(false);
    delta      = 0;
    timer.Bind(wxEVT_TIMER, &SpinInput::onTimer, this);

    // [STATE] The initial text can override the numeric seed, but the stored value is always normalized through the
    // range clamp before it is pushed back into the child text control.
    long initialFromText;
    if (text.ToLong(&initialFromText))
        initial = initialFromText;
    SetRange(min, max);
    SetValue(initial);
    SetStep(step);
    messureSize();
}

void SpinInput::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void SpinInput::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void SpinInput::SetLabelColor(StateColor const& color)
{
    label_color = color;
    state_handler.update_binds();
}

void SpinInput::SetTextColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
}

void SpinInput::SetSize(wxSize const& size)
{
    StaticBox::SetSize(size);
    Rescale();
}

void SpinInput::SetValue(const wxString& text)
{
    long value;
    if (text.ToLong(&value))
        SetValue(value);
}

void SpinInput::SetValue(int value)
{
    if (value < min)
        value = min;
    else if (value > max)
        value = max;
    this->val = value;
    text_ctrl->SetValue(wxString::FromDouble(value));
}

int SpinInput::GetValue() const { return val; }

void SpinInput::SetRange(int min, int max)
{
    this->min = min;
    this->max = max;
}

void SpinInput::DoSetToolTipText(wxString const& tip)
{
    wxWindow::DoSetToolTipText(tip);
    text_ctrl->SetToolTip(tip);
}

void SpinInput::Rescale()
{
    button_inc->Rescale();
    button_dec->Rescale();
    messureSize();
}

bool SpinInput::Enable(bool enable)
{
    bool result = text_ctrl->Enable(enable) && wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
        text_ctrl->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
        text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
        button_inc->Enable(enable);
        button_dec->Enable(enable);
    }
    return result;
}

void SpinInput::paintEvent(wxPaintEvent& evt)
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
void SpinInput::render(wxDC& dc)
{
    StaticBox::render(dc);
    int    states = state_handler.states();
    wxSize size   = GetSize();
    // draw seperator of buttons
    wxPoint pt = button_inc->GetPosition();
    pt.y       = size.y / 2;
    dc.SetPen(wxPen(border_color.defaultColor()));
    dc.DrawLine(pt, pt + wxSize{button_inc->GetSize().x - 2, 0});
    // draw label
    auto label = GetLabel();
    if (!label.IsEmpty()) {
        pt.x = size.x - labelSize.x - 5;
        pt.y = (size.y - labelSize.y) / 2;
        dc.SetFont(GetFont());
        dc.SetTextForeground(label_color.colorForStates(states));
        dc.DrawText(label, pt);
    }
}

void SpinInput::messureSize()
{
    // [INTENT] Layout is computed manually so the text field, spinner arrows, and trailing label all fit inside the
    // same skinned container instead of relying on a sizer hierarchy.
    // [STATE] The cached labelSize drives the right edge reservation; resizing also updates the control's minimum size
    // so parent layouts cannot collapse the numeric field below its usable width.
    wxSize size     = GetSize();
    wxSize textSize = text_ctrl->GetSize();
    int    h        = textSize.y + 8;
    if (size.y < h) {
        size.y = h;
    }
    wxSize minSize = size;
    minSize.x      = GetMinWidth();
    StaticBox::SetSize(size);
    SetMinSize(size);
    wxSize btnSize = {14, (size.y - 4) / 2};
    btnSize.x      = btnSize.x * btnSize.y / 10;
    wxClientDC dc(this);
    labelSize  = dc.GetMultiLineTextExtent(GetLabel());
    textSize.x = size.x - labelSize.x - btnSize.x - 16;
    text_ctrl->SetSize(textSize);
    text_ctrl->SetPosition({6 + btnSize.x, (size.y - textSize.y) / 2});
    button_inc->SetSize(btnSize);
    button_dec->SetSize(btnSize);
    button_inc->SetPosition({3, size.y / 2 - btnSize.y - 1});
    button_dec->SetPosition({3, size.y / 2 + 1});
}

Button* SpinInput::createButton(bool inc)
{
    // [EVENT] The arrow buttons are lightweight command surfaces: left-down starts auto-repeat, double-click performs
    // one immediate step, and button release stops the repeat loop and reselects the typed value.
    // [UNITY] This is best modeled as two press-and-hold buttons wired to a shared repeat controller rather than as a
    // generic repeat-button prefab.
    auto btn = new Button(this, "", inc ? "spin_inc" : "spin_dec", wxBORDER_NONE, 6);
    btn->SetCornerRadius(0);
    btn->DisableFocusFromKeyboard();
    btn->Bind(wxEVT_LEFT_DOWN, [=](auto& e) {
        delta = inc ? 1 : -1;
        SetValue(val + delta * step);
        text_ctrl->SetFocus();
        if (!btn->HasCapture())
            btn->CaptureMouse();
        delta *= 8;
        timer.Start(100);
        sendSpinEvent();
    });
    btn->Bind(wxEVT_LEFT_DCLICK, [=](auto& e) {
        delta = inc ? 1 : -1;
        if (!btn->HasCapture())
            btn->CaptureMouse();
        SetValue(val + delta * step);
        sendSpinEvent();
    });
    btn->Bind(wxEVT_LEFT_UP, [=](auto& e) {
        if (btn->HasCapture())
            btn->ReleaseMouse();
        timer.Stop();
        text_ctrl->SelectAll();
        delta = 0;
    });
    return btn;
}

void SpinInput::onTimer(wxTimerEvent& evnet)
{
    // [STATE] Auto-repeat ramps down the synthetic delta first, then applies the step once the acceleration has settled
    // back to +/-1 so long presses feel responsive without jumping too quickly.
    if (delta < -1 || delta > 1) {
        delta /= 2;
        return;
    }
    SetValue(val + delta * step);
    sendSpinEvent();
}

void SpinInput::onTextLostFocus(wxEvent& event)
{
    // [EVENT] Losing focus is treated like an implicit commit: stop repeat motion, release any held button capture, and
    // re-run the enter handler so typed text is normalized before the event escapes the widget.
    timer.Stop();
    for (auto* child : GetChildren())
        if (auto btn = dynamic_cast<Button*>(child))
            if (btn->HasCapture())
                btn->ReleaseMouse();
    wxCommandEvent e;
    onTextEnter(e);
    // pass to outer
    event.SetId(GetId());
    ProcessEventLocally(event);
    e.Skip();
}

void SpinInput::onTextEnter(wxCommandEvent& event)
{
    // [INTENT] Enter commits the field contents if they parse cleanly; otherwise the previous value is restored by
    // reusing the cached numeric state.
    long value;
    if (!text_ctrl->GetValue().ToLong(&value)) {
        value = val;
    }
    if (value != val) {
        SetValue(value);
        sendSpinEvent();
    }
    event.SetId(GetId());
    ProcessEventLocally(event);
}

void SpinInput::mouseWheelMoved(wxMouseEvent& event)
{
    auto delta = event.GetWheelRotation() < 0 ? 1 : -1;
    SetValue(val + delta * step);
    sendSpinEvent();
    text_ctrl->SetFocus();
}

void SpinInput::keyPressed(wxKeyEvent& event)
{
    // [EVENT] Arrow-key stepping mirrors the spinner buttons so keyboard and pointer input share the same clamp and
    // event-dispatch path.
    switch (event.GetKeyCode()) {
    case WXK_UP:
    case WXK_DOWN:
        long value;
        if (!text_ctrl->GetValue().ToLong(&value)) {
            value = val;
        }
        if (event.GetKeyCode() == WXK_DOWN && value - step >= min) {
            value = value - step;
        } else if (event.GetKeyCode() == WXK_UP && value + step <= max) {
            value = value + step;
        }
        if (value != val) {
            SetValue(value);
            sendSpinEvent();
        }
        break;
    default: event.Skip(); break;
    }
}

void SpinInput::sendSpinEvent()
{
    // [EVENT] The control emits a wxEVT_SPINCTRL as its external contract; downstream code should treat this as the
    // single source of truth for value-change notifications.
    wxCommandEvent event(wxEVT_SPINCTRL, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
