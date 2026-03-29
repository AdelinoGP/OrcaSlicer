#ifndef slic3r_GUI_RADIOBOX_hpp_
#define slic3r_GUI_RADIOBOX_hpp_

#include "../wxExtensions.hpp"

#include <wx/tglbtn.h>

namespace Slic3r { namespace GUI {

// [INTENT] Bitmap-backed toggle surrogate for a radio-style on/off choice that
// mirrors state into wxBitmapToggleButton rather than a native radio group.
// [UNITY] Port as a retained icon toggle (Image + Button/Toggle) with explicit
// pressed, disabled, and rescale states instead of swapping sprites in paint.
// [PORTING_HAZARD:P3] The control is visually radio-like but functionally a
// toggle button, so selection semantics must stay single-source-of-truth.
class RadioBox : public wxBitmapToggleButton
{
public:
    RadioBox(wxWindow* parent);

public:
    // [STATE] Setter funnels through update() so the bitmap pair stays aligned
    // with the inherited toggle state and any enabled/disabled variation.
    void SetValue(bool value) override;
    bool GetValue();
    // [STATE] Rebuilds cached scaled sprites when DPI or theme scale changes.
    void Rescale();
    // [EVENT] Keep the inherited enable/disable behavior but expose it through
    // the widget boundary so callers can treat the control as a standalone unit.
    bool Disable() { return wxBitmapToggleButton::Disable(); }
    bool Enable() { return wxBitmapToggleButton::Enable(); }

private:
    // [INTENT] Synchronize the three visual states with the current toggle and
    // enabled flags.
    void update();

private:
    // [STATE] DPI-scaled art for on/off/disabled representations.
    ScalableBitmap m_on;
    ScalableBitmap m_off;
    ScalableBitmap m_ban;
};

}} // namespace Slic3r::GUI

#endif // !slic3r_GUI_CheckBox_hpp_
