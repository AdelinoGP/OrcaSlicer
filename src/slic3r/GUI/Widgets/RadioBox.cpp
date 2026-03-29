#include "RadioBox.hpp"

#include "../wxExtensions.hpp"

namespace Slic3r { namespace GUI {
// [INTENT] This control is a bitmap-backed radio/toggle surrogate: it keeps the current value in the wxBitmapToggleButton base
// and swaps the visual state between on/off/disabled art instead of drawing a native radio circle.
// [UNITY] Port as a small retained toggle component with three sprites (on/off/disabled) and a single bool backing model.
RadioBox::RadioBox(wxWindow* parent)
    : wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_on(this, "radio_on", 18)
    , m_off(this, "radio_off", 18)
    , m_ban(this, "radio_ban", 18)
{
    // [STATE] The widget inherits its enabled/value state from wxBitmapToggleButton; the bitmap cache is only a visual mirror.
    // SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    if (parent)
        SetBackgroundColour(parent->GetBackgroundColour());
    // Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
    SetSize(m_on.GetBmpSize());
    SetMinSize(m_on.GetBmpSize());
    update();
}

// [EVENT] External callers can change the logical selection through SetValue(); the visual bitmap must be refreshed immediately
// so the toggle never lags behind the stored bool.
void RadioBox::SetValue(bool value)
{
    wxBitmapToggleButton::SetValue(value);
    update();
}

// [INTENT] Read back the logical toggle value from the base class; the bitmap selection is derived state.
bool RadioBox::GetValue() { return wxBitmapToggleButton::GetValue(); }

// [STATE] Rescaling rebuilds the cached art at the current DPI size and re-applies the fitted bounds.
// [UNITY] In Unity this would be a sprite swap plus a rect transform size update tied to DPI/scale changes.
void RadioBox::Rescale()
{
    m_on.msw_rescale();
    m_off.msw_rescale();
    SetSize(m_on.GetBmpSize());
    update();
}

// [INTENT] Keep the displayed bitmap synchronized with enabled/value state.
// [PORTING_HAZARD:P3] The disabled state is encoded as a third bitmap instead of a separate interactable flag in the view layer.
void RadioBox::update()
{
    if (IsEnabled()) {
        SetBitmap((GetValue() ? m_on : m_off).bmp());
    } else {
        SetBitmap(m_ban.bmp());
    }
}

}} // namespace Slic3r::GUI
