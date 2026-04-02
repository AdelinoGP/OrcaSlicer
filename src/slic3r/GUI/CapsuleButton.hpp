#ifndef CAPSULE_BUTTON_HPP
#define CAPSULE_BUTTON_HPP

#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {
/*
 [INTENT]
 CapsuleButton is a custom-drawn, stateful toggle button widget.
 It features a rounded "capsule" shape, an icon, and a label.
 Used for toggling settings like "Lock aspect ratio" or "Sync to printer".

 [STATE]
 - m_selected: Whether the button is currently in its active/on state.
 - m_hovered: Internal tracking of mouse hover for visual feedback.

 [EVENT]
 Translates mouse clicks on itself, its label, or its icon into a single wxEVT_BUTTON event sent to the parent.

 [UNITY]
 Map to a custom VisualElement (UI Toolkit) with:
 - USS for rounded borders (border-radius) and state-based coloring.
 - A child Label and Image (Sprite).
 - Clickable property or PointerDown events for state toggling.
*/
class CapsuleButton : public wxPanel
{
public:
    CapsuleButton(wxWindow* parent, wxWindowID id, const wxString& label, bool selected);
    void Select(bool selected);
    bool IsSelected() const { return m_selected; }

protected:
    void OnPaint(wxPaintEvent& event);

private:
    void OnEnterWindow(wxMouseEvent& event);
    void OnLeaveWindow(wxMouseEvent& event);
    void UpdateStatus();

    wxBitmapButton* m_btn;
    Label*          m_label;

    wxBitmap tag_on_bmp;
    wxBitmap tag_off_bmp;

    bool m_hovered;
    bool m_selected;
};
}} // namespace Slic3r::GUI

#endif