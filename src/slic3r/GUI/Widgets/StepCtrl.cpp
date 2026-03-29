#include "StepCtrl.hpp"
#include "Label.hpp"

#include <wx/dc.h>
#include <wx/pen.h>

wxDEFINE_EVENT(EVT_STEP_CHANGING, wxCommandEvent);
wxDEFINE_EVENT(EVT_STEP_CHANGED, wxCommandEvent);

BEGIN_EVENT_TABLE(StepCtrl, StepCtrlBase)
EVT_LEFT_DOWN(StepCtrl::mouseDown)
EVT_MOTION(StepCtrl::mouseMove)
EVT_LEFT_UP(StepCtrl::mouseUp)
EVT_MOUSE_CAPTURE_LOST(StepCtrl::mouseCaptureLost)
END_EVENT_TABLE()

// [INTENT] StepCtrlBase is the shared state/event core for the stepper widgets:
// it owns the ordered step labels, tip text, selection index, and palette state
// that the derived controls render differently.
// [UNITY] Port this as one retained model/controller with derived views for the
// horizontal step rail, vertical indicator, and filament variant instead of
// separate ad-hoc wxWindow subclasses.
StepCtrlBase::StepCtrlBase(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : StaticBox(parent, id, pos, size, style)
    , font_tip(Label::Body_14)
    , clr_bar(0xACACAC)
    , clr_step(0xACACAC)
    , clr_text(std::make_pair(0x009688, (int) StateColor::Checked), std::make_pair(0x6B6B6B, (int) StateColor::Normal))
    , clr_tip(0x828280)
{
    SetFont(Label::Body_14);
    border_color      = StateColor(*wxLIGHT_GREY);
    StaticBox::radius = 0;
    // wxString reason;
    // IsTransparentBackgroundSupported(&reason);
}

StepCtrlBase::~StepCtrlBase() {}

int StepCtrlBase::GetSelection() const { return step; }

// [STATE] Selection changes are gated through EVT_STEP_CHANGING so handlers can
// veto invalid transitions before the cached index mutates.
// [PORTING_HAZARD:P2] Unity should preserve the vetoable transition contract;
// a direct property setter would silently drop this behavior.
void StepCtrlBase::SelectItem(int item)
{
    if (item == step || item < -1 || item >= steps.size() || !sendStepCtrlEvent(true))
        return;
    step = item;
    sendStepCtrlEvent();
    Refresh();
}

void StepCtrlBase::Idle()
{
    if (step != -1) {
        step = -1;
        sendStepCtrlEvent();
        Refresh();
    }
}

bool StepCtrlBase::SetTipFont(wxFont const& font)
{
    font_tip = font;
    return true;
}

void StepCtrlBase::SetHint(wxString hint) { this->hint = hint; }

int StepCtrlBase::AppendItem(const wxString& item, wxString const& tip)
{
    steps.push_back(item);
    tips.push_back(tip);
    return steps.size() - 1;
}

void StepCtrlBase::DeleteAllItems()
{
    steps.clear();
    tips.clear();
    if (step >= 0) {
        step = -1;
        sendStepCtrlEvent();
    }
}

unsigned int StepCtrlBase::GetCount() const { return steps.size(); }

wxString StepCtrlBase::GetItemText(unsigned int item) const { return item < steps.size() ? steps[item] : wxString{}; }

int StepCtrlBase::GetItemUseText(wxString txt) const
{
    for (int i = 0; i < steps.size(); i++) {
        if (steps[i] == txt) {
            return i;
        } else {
            continue;
        }
    }
    // [UNCLEAR] Hypothesis: callers treat index 0 as a fallback "first step"
    // when a lookup misses. The Unity port should confirm whether a miss should
    // instead return -1 and surface an explicit not-found state.
    return 0;
}

void StepCtrlBase::SetItemText(unsigned int item, wxString const& value)
{
    if (item >= steps.size())
        return;
    steps[item] = value;
}

bool StepCtrlBase::sendStepCtrlEvent(bool changing)
{
    // [EVENT] Both event variants carry the current index through wxCommandEvent
    // so parent panels can listen without grabbing widget internals.
    wxCommandEvent event(changing ? EVT_STEP_CHANGING : EVT_STEP_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(step);
    GetEventHandler()->ProcessEvent(event);
    return true;
}

/* StepCtrl */

// [INTENT] StepCtrl is the interactive horizontal picker: it renders a bar,
// selectable circles, and a draggable thumb bitmap that snaps to discrete
// steps.
// [STATE] Drag position is tracked separately from the committed selection so
// the control can preview a potential target while the mouse is captured.
// [UNITY] Port as a retained segmented control with a drag handle and explicit
// pointer-capture state in a MonoBehaviour or UI Toolkit controller.
StepCtrl::StepCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : StepCtrlBase(parent, id, pos, size, style), bmp_thumb(this, "step_thumb", 36)
{
    StaticBox::border_width = 3;
    radius                  = radius * bmp_thumb.GetBmpHeight() / 36;
    bar_width               = bar_width * bmp_thumb.GetBmpHeight() / 36;
}

void StepCtrl::Rescale()
{
    bmp_thumb.msw_rescale();
    radius    = radius * bmp_thumb.GetBmpHeight() / 36;
    bar_width = bar_width * bmp_thumb.GetBmpHeight() / 36;
}

// [EVENT] The click path distinguishes thumb dragging from bar clicks: thumb
// presses start a capture/drag session, while bar clicks jump one step left or
// right.
// [PORTING_HAZARD:P2] The manual hit-test math depends on the current item
// count and control width; Unity should derive hit zones from layout rather than
// recreating the raw pixel arithmetic.
void StepCtrl::mouseDown(wxMouseEvent& event)
{
    wxPoint pt;
    event.GetPosition(&pt.x, &pt.y);
    wxSize size      = GetSize();
    int    itemWidth = size.x / steps.size();
    wxRect rcBar     = {0, (size.y - 60) / 2, size.x, 60};
    int    circleX   = itemWidth / 2 + itemWidth * step;
    wxRect rcThumb   = {{circleX, size.y / 2}, bmp_thumb.GetBmpSize()};
    rcThumb.x -= rcThumb.width / 2;
    rcThumb.y -= rcThumb.height / 2;
    if (rcThumb.Contains(pt)) {
        pos_thumb   = wxPoint{circleX, size.y / 2};
        drag_offset = pos_thumb - pt;
        if (!HasCapture())
            CaptureMouse();
    } else if (rcBar.Contains(pt)) {
        if (pt.x < circleX) {
            if (step > 0)
                SelectItem(step - 1);
        } else {
            if (step < steps.size() - 1)
                SelectItem(step + 1);
        }
    }
}

// [STATE] During a drag, pos_thumb.y temporarily stores the hovered target
// index while pos_thumb.x follows the cursor; Refresh() is used to preview the
// snap target before mouse release commits it.
void StepCtrl::mouseMove(wxMouseEvent& event)
{
    if (pos_thumb == wxPoint{0, 0})
        return;
    wxPoint pt;
    event.GetPosition(&pt.x, &pt.y);
    pos_thumb.x      = pt.x + drag_offset.x;
    wxSize size      = GetSize();
    int    itemWidth = size.x / steps.size();
    int    index     = pos_thumb.x / itemWidth;
    if (index < 0)
        index = 0;
    else if (index >= steps.size())
        index = steps.size() - 1;
    if (index != pos_thumb.y) {
        pos_thumb.y = index;
        Refresh();
    }
}

// [EVENT] Mouse release finalizes the snapped index and clears capture state.
void StepCtrl::mouseUp(wxMouseEvent& event)
{
    if (pos_thumb == wxPoint{0, 0})
        return;
    wxSize size      = GetSize();
    int    itemWidth = size.x / steps.size();
    int    index     = pos_thumb.x / itemWidth;
    if (index < 0)
        index = 0;
    else if (index >= steps.size())
        index = steps.size() - 1;
    pos_thumb = {0, 0};
    SelectItem(index);
    if (HasCapture())
        ReleaseMouse();
}

// [EVENT] Capture loss is normalized into the same release path so the model
// cannot stay stuck in a pseudo-drag state after an OS-level interruption.
void StepCtrl::mouseCaptureLost(wxMouseCaptureLostEvent& event)
{
    wxMouseEvent evt;
    mouseUp(evt);
}

// [INTENT] The render path paints one horizontal rail, one circle per step, and
// optional hint/tip labels above and below the active item.
// [UNITY] Port as a custom-drawn segmented bar with a draggable handle and text
// labels driven by a shared step model; the bitmap thumb is decoration, not the
// source of truth.
void StepCtrl::doRender(wxDC& dc)
{
    if (steps.empty())
        return;
    StaticBox::doRender(dc);

    wxSize size   = GetSize();
    int    states = state_handler.states();

    int    itemWidth = size.x / steps.size();
    wxRect rcBar     = {itemWidth / 2, (size.y - bar_width) / 2, size.x - itemWidth, bar_width};

    dc.SetPen(wxPen(clr_bar.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_bar.colorForStates(states)));
    dc.DrawRectangle(rcBar);
    int circleX = itemWidth / 2;
    int circleY = size.y / 2;
    dc.SetPen(wxPen(clr_step.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_step.colorForStates(states)));
    if (!hint.empty()) {
        dc.SetFont(font_tip);
        dc.SetTextForeground(clr_tip.colorForStates(states));
        wxSize sz = dc.GetTextExtent(hint);
        dc.DrawText(hint, dc.GetCharWidth(), circleY - FromDIP(20) - sz.y);
    }
    for (int i = 0; i < steps.size(); ++i) {
        bool check = (pos_thumb == wxPoint{0, 0} ? step : pos_thumb.y) == i;
        dc.DrawEllipse(circleX - radius, circleY - radius, radius * 2, radius * 2);
        dc.SetFont(GetFont());
        dc.SetTextForeground(clr_text.colorForStates(states | (check ? StateColor::Checked : 0)));
        wxSize sz = dc.GetTextExtent(steps[i]);
        dc.DrawText(steps[i], circleX - sz.x / 2, circleY + 20);
        if (check) {
            dc.SetFont(font_tip);
            dc.SetTextForeground(clr_tip.colorForStates(states));
            wxSize sz = dc.GetTextExtent(tips[i]);
            dc.DrawText(tips[i], circleX - sz.x / 2, circleY - 20 - sz.y);
            sz = bmp_thumb.GetBmpSize();
            dc.DrawBitmap(bmp_thumb.bmp(), circleX - sz.x / 2, circleY - sz.y / 2);
        }
        circleX += itemWidth;
    }
}

/* StepIndicator */

// [INTENT] StepIndicator reuses the same ordered step model but changes the
// visual grammar into a vertical wizard progress strip with completed, active,
// and pending states.
// [PORTING_HAZARD:P3] The control mixes text wrapping, completion icons, and
// column sizing in a single immediate-mode paint pass, so Unity should split
// layout from draw state.
StepIndicator::StepIndicator(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : StepCtrlBase(parent, id, pos, size, style), bmp_ok(this, "step_ok", 12)
{
    SetFont(Label::Body_12);
    font_tip = Label::Body_10;
    clr_bar  = 0xE1E1E1;
    clr_step = StateColor(std::make_pair(0xACACAC, (int) StateColor::Disabled), std::make_pair(0x009688, 0));
    clr_text = StateColor(std::make_pair(0xACACAC, (int) StateColor::Disabled), std::make_pair(0x323A3D, (int) StateColor::Checked),
                          std::make_pair(0x6B6B6B, 0));
    clr_tip  = *wxWHITE;
    StaticBox::border_width = 0;
    radius                  = bmp_ok.GetBmpHeight() / 2;
    bar_width               = bmp_ok.GetBmpHeight() / 20;
    if (bar_width < 2)
        bar_width = 2;
}

void StepIndicator::Rescale()
{
    bmp_ok.msw_rescale();
    radius    = bmp_ok.GetBmpHeight() / 2;
    bar_width = bmp_ok.GetBmpHeight() / 20;
    if (bar_width < 2)
        bar_width = 2;
}

void StepIndicator::SelectNext() { SelectItem(step + 1); }

// [INTENT] The vertical painter is a wizard-like summary rail: it shows the
// first and last labels with wrapping, places circular milestones down a spine,
// and swaps the circle content between a number and a completed icon.
// [UNITY] Port as a retained vertical stepper/list with per-row completion state
// and a shared spine element rather than reproducing the per-pixel offsets.
void StepIndicator::doRender(wxDC& dc)
{
    if (steps.empty())
        return;

    StaticBox::doRender(dc);

    wxSize size = GetSize();

    int states = state_handler.states();
    if (!IsEnabled()) {
        states = clr_step.Disabled;
    }

    int textWidth = size.x - radius * 5;
    dc.SetFont(GetFont());
    wxString firstLine;
    if (step == 0)
        dc.SetFont(GetFont().Bold());
    wxSize   firstLineSize = Label::split_lines(dc, textWidth, steps.front(), firstLine);
    wxString lastLine;
    if (step == steps.size() - 1)
        dc.SetFont(GetFont().Bold());
    wxSize lastLineSize = Label::split_lines(dc, textWidth, steps.back(), lastLine);
    int    firstPadding = std::max(0, firstLineSize.y / 2 - radius);
    int    lastPadding  = std::max(0, lastLineSize.y / 2 - radius);

    wxRect rcBar     = {radius * 2 - bar_width / 2, radius * 2 + firstPadding, bar_width, size.y - radius * 6 - firstPadding - lastPadding};
    int    itemWidth = steps.size() == 1 ? size.y : rcBar.height / (steps.size() - 1);

    // Draw thin bar stick
    dc.SetPen(wxPen(clr_bar.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_bar.colorForStates(states)));
    dc.DrawRectangle(rcBar);

    int circleX = radius * 2;
    int circleY = radius * 3 + firstPadding;
    dc.SetPen(wxPen(clr_step.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_step.colorForStates(states)));
    for (int i = 0; i < steps.size(); ++i) {
        bool disabled = step > i;
        bool checked  = step == i;
        // Draw circle point & texts in it
        dc.DrawEllipse(circleX - radius, circleY - radius, radius * 2, radius * 2);
        // Draw content ( icon or text ) in circle
        if (disabled) {
            wxSize sz = bmp_ok.GetBmpSize();
            dc.DrawBitmap(bmp_ok.bmp(), circleX - radius, circleY - radius);
        } else {
            dc.SetFont(font_tip);
            dc.SetTextForeground(clr_tip.colorForStates(states));
            auto tip = tips[i];
            if (tip.IsEmpty())
                tip.append(1, wchar_t(L'0' + i + 1));
            wxSize sz = dc.GetTextExtent(tip);
            dc.DrawText(tip, circleX - sz.x / 2, circleY - sz.y / 2 + 1);
        }
        // Draw step text
        dc.SetTextForeground(clr_text.colorForStates(states | (disabled ? StateColor::Disabled : checked ? StateColor::Checked : 0)));
        dc.SetFont(checked ? GetFont().Bold() : GetFont());
        wxString text;
        wxSize   textSize;
        if (i == 0) {
            text     = firstLine;
            textSize = firstLineSize;
        } else if (i == steps.size() - 1) {
            text     = lastLine;
            textSize = lastLineSize;
        } else {
            textSize = Label::split_lines(dc, textWidth, steps[i], text);
        }
        dc.DrawText(text, circleX + radius * 3, circleY - (textSize.y / 2));
        circleY += itemWidth;
    }
}

/* FilamentStepIndicator */

// [INTENT] FilamentStepIndicator is a specialized vertical stepper for the
// filament-load workflow. It keeps the same selection model but adds a fixed
// "Loading" banner and slot-sensitive metadata.
// [STATE] m_slot_information is a lightweight external label cache for the
// active filament slot; the current file does not consume it in rendering yet,
// so Unity should confirm whether it is a future overlay or a dead field.
// [PORTING_HAZARD:P2] This class is already diverging from StepIndicator, so a
// Unity port should factor the common wizard rail before layering filament-only
// banner/content behavior.
FilamentStepIndicator::FilamentStepIndicator(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : StepCtrlBase(parent, id, pos, size, style), bmp_ok(this, "step_ok", 12)
{
    static Slic3r::GUI::BitmapCache cache;
    // bmp_extruder = *cache.load_png("filament_load_extruder", FromDIP(300), FromDIP(200), false, false);
    SetFont(Label::Body_12);
    font_tip = Label::Body_12;
    clr_bar  = 0xE1E1E1;
    clr_step = StateColor(std::make_pair(0xACACAC, (int) StateColor::Disabled), std::make_pair(0x009688, 0));
    clr_text = StateColor(std::make_pair(0xACACAC, (int) StateColor::Disabled), std::make_pair(0x323A3D, (int) StateColor::Checked),
                          std::make_pair(0x6B6B6B, 0));
    clr_tip  = *wxWHITE;
    StaticBox::border_width = 0;
    radius                  = 9;
    bar_width               = 0;
}

void FilamentStepIndicator::Rescale()
{
    bmp_ok.msw_rescale();
    radius    = bmp_ok.GetBmpHeight() / 2;
    bar_width = bmp_ok.GetBmpHeight() / 20;
    if (bar_width < 2)
        bar_width = 2;
}

void FilamentStepIndicator::SelectNext() { SelectItem(step + 1); }

// [INTENT] The filament renderer preserves the step-rail semantics but adds a
// leading status header and a tighter layout that is optimized for the load
// wizard page.
// [UNITY] Port the banner as a separate retained header element and keep the
// step list as a data-driven vertical component; do not hard-code the text
// offsets into the control body.
void FilamentStepIndicator::doRender(wxDC& dc)
{
    if (steps.empty())
        return;

    StaticBox::doRender(dc);

    wxSize size = GetSize();

    int states = state_handler.states();
    if (!IsEnabled()) {
        states = clr_step.Disabled;
    }

    dc.SetFont(::Label::Head_16);
    dc.SetTextForeground(wxColour(0, 150, 136));
    int    circleX = 20;
    int    circleY = 20;
    wxSize sz      = dc.GetTextExtent(L"Loading");
    dc.DrawText(L"Loading", circleX, circleY);

    dc.SetFont(::Label::Body_13);

    // dc.DrawBitmap(bmp_extruder, FromDIP(250), circleY);
    circleY += sz.y;

    int textWidth = size.x - radius * 5;
    dc.SetFont(GetFont());
    wxString firstLine;
    if (step == 0)
        dc.SetFont(GetFont().Bold());
    wxSize   firstLineSize = Label::split_lines(dc, textWidth, steps.front(), firstLine);
    wxString lastLine;
    if (step == steps.size() - 1)
        dc.SetFont(GetFont().Bold());
    wxSize lastLineSize = Label::split_lines(dc, textWidth, steps.back(), lastLine);
    int    firstPadding = std::max(0, firstLineSize.y / 2 - radius);
    int    lastPadding  = std::max(0, lastLineSize.y / 2 - radius);

    int itemWidth = radius * 3;

    // Draw thin bar stick
    dc.SetPen(wxPen(clr_bar.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_bar.colorForStates(states)));
    // dc.DrawRectangle(rcBar);

    circleX += radius;
    circleY += radius * 3 + firstPadding;
    dc.SetPen(wxPen(clr_step.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_step.colorForStates(states)));
    for (int i = 0; i < steps.size(); ++i) {
        bool disabled = step > i;
        bool checked  = step == i;
        // Draw circle point & texts in it
        dc.DrawEllipse(circleX - radius, circleY - radius, radius * 2, radius * 2);
        // Draw content ( icon or text ) in circle
        if (disabled) {
            wxSize sz = bmp_ok.GetBmpSize();
            dc.DrawBitmap(bmp_ok.bmp(), circleX - sz.x / 2, circleY - sz.y / 2);
        } else {
            dc.SetFont(font_tip);
            dc.SetTextForeground(clr_tip.colorForStates(states));
            auto tip = tips[i];
            if (tip.IsEmpty())
                tip.append(1, wchar_t(L'0' + i + 1));
            wxSize sz = dc.GetTextExtent(tip);
            dc.DrawText(tip, circleX - sz.x / 2, circleY - sz.y / 2 + 1);
        }
        // Draw step text
        dc.SetTextForeground(clr_text.colorForStates(states | (disabled ? StateColor::Disabled : checked ? StateColor::Checked : 0)));
        dc.SetFont(checked ? GetFont().Bold() : GetFont());
        wxString text;
        wxSize   textSize;
        if (i == 0) {
            text     = firstLine;
            textSize = firstLineSize;
        } else if (i == steps.size() - 1) {
            text     = lastLine;
            textSize = lastLineSize;
        } else {
            textSize = Label::split_lines(dc, textWidth, steps[i], text);
        }
        dc.DrawText(text, circleX + radius * 1.5, circleY - (textSize.y / 2));
        circleY += itemWidth;
    }
}

// [STATE] Slot information is cached through this setter so later render passes
// can reference it without changing the selection model.
void FilamentStepIndicator::SetSlotInformation(wxString slot) { this->m_slot_information = slot; }
