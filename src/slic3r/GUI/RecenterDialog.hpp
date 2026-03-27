#ifndef slic3r_GUI_RecenterDialog_hpp_
#define slic3r_GUI_RecenterDialog_hpp_

#include "GUI_Utils.hpp"
#include <wx/statbmp.h>
#include "Widgets/Button.hpp"
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {
// [INTENT] Modal confirmation dialog used when the user needs to home axes before movement continues.
// [STATE] Keeps a cached scalable home icon plus two localized text fragments that the cpp file wraps and paints manually.
// [EVENT] Declares the paint, confirm, close, and DPI-change handlers that drive the dialog's lifecycle.
// [UNITY] Port as a modal confirmation panel/controller with a shared icon asset, explicit button callbacks, and a scale-change relayout
// hook. [PORTING_HAZARD:P2] The rendering and text flow are split between owner-drawn paint logic and DPI-sensitive relayout, so the Unity
// version should replace this with a layout-driven view instead of preserving the pixel-measurement behavior.
class RecenterDialog : public DPIDialog
{
private:
    // [STATE] Child widgets are owned by the wx dialog; the pointers are only handles for the static prompt text and icon container.
    wxStaticText*   m_staticText_hint;
    wxStaticBitmap* m_bitmap_home;
    // [STATE] DPI-aware bitmap cache refreshed when the dialog scale changes.
    ScalableBitmap m_home_bmp;
    // [STATE] Localized prompt fragments assembled at paint time.
    wxString hint1;
    wxString hint2;

    // [EVENT] Builds the cached bitmap for the current DPI scale.
    void init_bitmap();
    // [EVENT] Owner-drawn paint entry point.
    void OnPaint(wxPaintEvent& event);
    // [INTENT] Draws the prompt text and icon into the fixed header area.
    void render(wxDC& dc);
    // [EVENT] Primary action: home axes and dismiss the dialog.
    void on_button_confirm(wxCommandEvent& event);
    // [EVENT] Secondary action: close the dialog without confirming.
    void on_button_close(wxCommandEvent& event);
    // [STATE] Recreates scale-dependent resources and relays out the dialog after a DPI change.
    void on_dpi_changed(const wxRect& suggested_rect) override;

public:
    RecenterDialog(wxWindow*       parent,
                   wxWindowID      id    = wxID_ANY,
                   const wxString& title = wxEmptyString,
                   const wxPoint&  pos   = wxDefaultPosition,
                   const wxSize&   size  = wxDefaultSize,
                   long            style = wxCLOSE_BOX | wxCAPTION);

    ~RecenterDialog();
};
}} // namespace Slic3r::GUI

#endif
