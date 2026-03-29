#include "StaticGroup.hpp"

// [INTENT] Lightweight group-box variant that inherits the labeled frame chrome from LabeledStaticBox
// and layers a badge only when the caller asks for it.
// [STATE] The badge bitmap is lazily created and cleared, so the control carries a tiny cached overlay
// state instead of keeping the asset alive permanently.
// [UNITY] Port as a retained panel with a title row plus an optional top-right overlay icon driven by
// explicit state, not by immediate-mode paint callbacks.
StaticGroup::StaticGroup(wxWindow* parent, wxWindowID id, const wxString& label) : LabeledStaticBox(parent, label)
{
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour("#CECECE");
}

// [EVENT] This is the public state toggle for the badge overlay; it mutates cached bitmap state and
// requests a repaint only when the visible badge presence actually changes.
// [PORTING_HAZARD:P3] The bitmap object is recreated from a named asset on demand, so a Unity port should
// resolve the icon through a shared asset cache instead of reloading per toggle.
void StaticGroup::ShowBadge(bool show)
{
    if (show && badge.name() != "badge") {
        badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !badge.name().empty()) {
        badge = ScalableBitmap{};
        Refresh();
    }
}

// [INTENT] Preserve the parent border/label paint path and then draw the badge at the far right edge of
// the group header.
// [STATE] The badge position is derived from the current size and label height, so the overlay tracks the
// control geometry rather than owning separate layout state.
// [UNITY] Use a layout-driven overlay anchored to the header container's right edge; do not depend on a
// custom DC draw pass for the final badge placement.
void StaticGroup::DrawBorderAndLabel(wxDC& dc)
{
    LabeledStaticBox::DrawBorderAndLabel(dc);
    if (badge.bmp().IsOk()) {
        auto s = badge.bmp().GetScaledSize();
        dc.DrawBitmap(badge.bmp(), GetSize().x - s.x, std::max(0, m_pos.y) + m_label_height / 2);
    }
}
