#ifndef slic3r_GUI_CheckBox_hpp_
#define slic3r_GUI_CheckBox_hpp_

#include "../wxExtensions.hpp"

#include <wx/tglbtn.h>

// [INTENT] Offer a themed bitmap toggle that cycles through checked, half-checked, and unchecked sprites with consistent sizing.
// [UNITY] Unity should map this to a UI Toolkit Toggle + MonoBehaviour that swaps `Texture2D`/`Sprite` assets for each logical state and
// forwards the `indeterminate` flag to the same controller.
class CheckBox : public wxBitmapToggleButton
{
public:
    CheckBox(wxWindow* parent, int id = wxID_ANY);

    // [STATE] Mirrors the toggle value so the visual sprite never lags behind the boolean state.
    void SetValue(bool value) override;

    // [STATE] Controls the 3-state feedback that overrides the boolean visuals temporarily.
    void SetHalfChecked(bool value = true);

    // [STATE] Forces every scalable bitmap to re-load when DPI or zoom factors change.
    void Rescale();

#ifdef __WXOSX__
    // [EVENT] OSX relays `Enable`/focus changes through this override so hover/focus bitmaps stay accurate. Unity will need
    // manual `Selectable.interactable`/focus handling for the same effect.
    virtual bool Enable(bool enable = true) wxOVERRIDE;
#endif

protected:
#ifdef __WXMSW__
    // [STATE] Windows expects `State_Normal`, so pin the default bitmap even when wxWidgets tries to shift focus visuals.
    virtual State GetNormalState() const wxOVERRIDE;
#endif

#ifdef __WXOSX__
    // [EVENT] OSX reroutes focus/hover events through `DoGetBitmap`, so Unity will need manual `ISelectHandler`/pointer wiring.
    virtual wxBitmap DoGetBitmap(State which) const wxOVERRIDE;

    // [EVENT] Keeps the hover/focus flags in sync with wxWidgets events for this toggle.
    // [PORTING_HAZARD:P3] Unity needs explicit `IPointerEnterHandler`/`IPointerExitHandler` wiring because wxWidgets routes those states
    // here. [UNITY] Replace this helper with a custom EventTrigger/MonoBehaviour combo that updates hover and focus sprites.
    void updateBitmap(wxEvent& evt);

    // [STATE] OSX caches hover/focus/disable so it knows which bitmap to show during event replay.
    bool m_disable = false;
    bool m_hover   = false;
    bool m_focus   = false;
#endif

private:
    // [STATE] Centralizes which bitmap variant to show (label/disabled/focus) whenever the toggle state changes.
    // [PORTING_HAZARD:P3] Unity must replace `SetBitmapLabel/Disabled/Current` with manual sprite swaps because there are no direct
    // equivalents. [UNITY] Replay this by swapping `Texture2D` assets on the VisualElement background in a custom MonoBehaviour on the
    // Toggle.
    void update();

    // [STATE] Sprite cache for every checked/half/unchecked variant so toggling stays snappy.
    ScalableBitmap m_on;
    ScalableBitmap m_half;
    ScalableBitmap m_off;
    ScalableBitmap m_on_disabled;
    ScalableBitmap m_half_disabled;
    ScalableBitmap m_off_disabled;
    ScalableBitmap m_on_focused;
    ScalableBitmap m_half_focused;
    ScalableBitmap m_off_focused;
    // [STATE] Tracks the indeterminate flag that overrides `GetValue()` and forces half-check visuals.
    bool m_half_checked = false;
};

#endif // !slic3r_GUI_CheckBox_hpp_
