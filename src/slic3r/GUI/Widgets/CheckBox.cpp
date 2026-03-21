#include "CheckBox.hpp"

#include "../wxExtensions.hpp"

// [INTENT] Derive from a bitmap toggle so we can swap themed sprites for checked, half, and unchecked visuals while keeping sizing
// consistent. [UNITY] Unity port should mirror this with a UI Toolkit Toggle + MonoBehaviour that swaps Sprite assets (on/half/off +
// focus/disabled) and feeds back to the same state model.
CheckBox::CheckBox(wxWindow* parent, int id)
    : wxBitmapToggleButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_on(this, "check_on", 18)
    , m_half(this, "check_half", 18)
    , m_off(this, "check_off", 18)
    , m_on_disabled(this, "check_on_disabled", 18)
    , m_half_disabled(this, "check_half_disabled", 18)
    , m_off_disabled(this, "check_off_disabled", 18)
    , m_on_focused(this, "check_on_focused", 18)
    , m_half_focused(this, "check_half_focused", 18)
    , m_off_focused(this, "check_off_focused", 18)
{
    // SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    if (parent)
        SetBackgroundColour(parent->GetBackgroundColour());
    // [EVENT] Bound toggle events drop the half-check flag and drive an immediate repaint on the UI thread.
    // [THREAD] Unity should trigger this handler from its main-thread dispatcher so sprite swaps stay consistent.
    Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) {
        m_half_checked = false;
        update();
        e.Skip();
    });
#ifdef __WXOSX__ // State not fully implement on MacOS
    Bind(wxEVT_SET_FOCUS, &CheckBox::updateBitmap, this);
    Bind(wxEVT_KILL_FOCUS, &CheckBox::updateBitmap, this);
    Bind(wxEVT_ENTER_WINDOW, &CheckBox::updateBitmap, this);
    Bind(wxEVT_LEAVE_WINDOW, &CheckBox::updateBitmap, this);
#endif
    SetSize(m_on.GetBmpSize());
    SetMinSize(m_on.GetBmpSize());
    update();
}

// [STATE] Mirrors Toggle.isOn semantics by updating on changes only and rerendering immediately.
// [THREAD] This code assumes it always executes on the UI thread so the bitmap swaps don't race with input events.
void CheckBox::SetValue(bool value)
{
    if (wxBitmapToggleButton::GetValue() != value) {
        wxBitmapToggleButton::SetValue(value);
        update();
    }
}

void CheckBox::SetHalfChecked(bool value)
{
    // [STATE] Controls the indeterminate visual state so it can override the normal checked/unchecked icons.
    m_half_checked = value;
    update();
}

// [STATE] Recomputes every scalable bitmap when the DPI context changes so the control stays crisp across zoom levels.
void CheckBox::Rescale()
{
    m_on.msw_rescale();
    m_half.msw_rescale();
    m_off.msw_rescale();
    m_on_disabled.msw_rescale();
    m_half_disabled.msw_rescale();
    m_off_disabled.msw_rescale();
    m_on_focused.msw_rescale();
    m_half_focused.msw_rescale();
    m_off_focused.msw_rescale();
    SetSize(m_on.GetBmpSize());
    update();
}

// [STATE] Decides which bitmap to show by combining half-checked and boolean state and updates focus/disabled variants accordingly.
// [PORTING_HAZARD:P3] Unity ports must re-create the focus/hover bitmap layering because wxWidgets `SetBitmapCurrent`/`SetBitmapFocus` have
// no direct counterpart; use style swaps or `SpriteRenderer` textures on `UI Toolkit` elements.
// [UNITY] Mirror this with a UI Toolkit Toggle where the VisualElement background sprite is driven by a MonoBehaviour that watches both
// `isOn` and an indeterminate flag.
void CheckBox::update()
{
    SetBitmapLabel((m_half_checked ? m_half : GetValue() ? m_on : m_off).bmp());
    SetBitmapDisabled((m_half_checked ? m_half_disabled : GetValue() ? m_on_disabled : m_off_disabled).bmp());
#ifdef __WXMSW__
    SetBitmapFocus((m_half_checked ? m_half_focused : GetValue() ? m_on_focused : m_off_focused).bmp());
#endif
    SetBitmapCurrent((m_half_checked ? m_half_focused : GetValue() ? m_on_focused : m_off_focused).bmp());
#ifdef __WXOSX__
    wxCommandEvent e(wxEVT_UPDATE_UI);
    updateBitmap(e);
#endif
}

#ifdef __WXMSW__

// [STATE] Ensures Windows uses the default bitmap state; other platforms do not rely on this.
CheckBox::State CheckBox::GetNormalState() const { return State_Normal; }

#endif

#ifdef __WXOSX__

// [EVENT] OSX reroutes activation events through `Enable` so focus/hover states stay accurate; Unity should treat this like a manual
// `Selectable.interactable` update.
bool CheckBox::Enable(bool enable)
{
    bool result = wxBitmapToggleButton::Enable(enable);
    if (result) {
        m_disable = !enable;
        wxCommandEvent e(wxEVT_ACTIVATE);
        updateBitmap(e);
    }
    return result;
}

wxBitmap CheckBox::DoGetBitmap(State which) const
{
    if (m_disable) {
        return wxBitmapToggleButton::DoGetBitmap(State_Disabled);
    }
    if (m_focus) {
        return wxBitmapToggleButton::DoGetBitmap(State_Current);
    }
    return wxBitmapToggleButton::DoGetBitmap(which);
}

// [EVENT] OSX hover/focus events flow through this helper to keep `m_hover`/`m_focus` in sync with wx's enter/leave events.
// [PORTING_HAZARD:P3] Unity will need manual `IPointerEnterHandler`/`IPointerExitHandler` wiring because there is no global enter/leave helper.
void CheckBox::updateBitmap(wxEvent& evt)
{
    evt.Skip();
    if (evt.GetEventType() == wxEVT_ENTER_WINDOW) {
        m_hover = true;
    } else if (evt.GetEventType() == wxEVT_LEAVE_WINDOW) {
        m_hover = false;
    } else {
        if (evt.GetEventType() == wxEVT_SET_FOCUS) {
            m_focus = true;
        } else if (evt.GetEventType() == wxEVT_KILL_FOCUS) {
            m_focus = false;
        }
        wxMouseEvent e;
        if (m_hover)
            OnEnterWindow(e);
        else
            OnLeaveWindow(e);
    }
}

#endif
