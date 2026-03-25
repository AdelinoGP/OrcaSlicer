#ifndef slic3r_BitmapComboBox_hpp_
#define slic3r_BitmapComboBox_hpp_

#include <wx/bmpcbox.h>
#include <wx/gdicmn.h>

#include "GUI_Utils.hpp"

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------
namespace Slic3r { namespace GUI {

// [INTENT] BitmapComboBox is a specialized wxBitmapComboBox used for preset lists on Sidebar and Tabs.
// [INTENT] It provides platform-specific adjustments for bitmap scaling and drawing to support Retina displays on macOS
// [INTENT] and custom drawing on Windows.
// [STATE] Inherits internal state from wxBitmapComboBox (item count, selection, bitmaps).
// [UNITY] Replace with a UI Toolkit VisualElement that uses a custom USS style for dropdown items.
// [UNITY] Use Sprite/Texture assets for bitmaps, and implement custom drawing via a custom VisualElement or a custom style.
// [PORTING_HAZARD:P2] Platform-specific overrides (#ifdef _WIN32, __APPLE__) require conditional Unity implementation.
// [PORTING_HAZARD:P3] Retina bitmap scaling logic on macOS is non-trivial; may need custom DPI-aware sprite loading.
class BitmapComboBox : public wxBitmapComboBox
{
public:
    BitmapComboBox(wxWindow*       parent,
                   wxWindowID      id        = wxID_ANY,
                   const wxString& value     = wxEmptyString,
                   const wxPoint&  pos       = wxDefaultPosition,
                   const wxSize&   size      = wxDefaultSize,
                   int             n         = 0,
                   const wxString  choices[] = NULL,
                   long            style     = 0);
    ~BitmapComboBox();

#ifdef _WIN32
    // [INTENT] Append item without bitmap (Windows-specific overload).
    int Append(const wxString& item);
#endif
    // [INTENT] Append item with bitmap, delegates to base class.
    int Append(const wxString& item, const wxBitmap& bitmap) { return wxBitmapComboBox::Append(item, bitmap); }

protected:
#ifdef __APPLE__
    // [INTENT] Override OnAddBitmap to handle Retina-scaled bitmaps without changing control item size.
    // [INTENT] This ensures bitmaps are drawn at the correct logical size.
    // [EVENT] Called when a bitmap is added to the combo box.
    bool OnAddBitmap(const wxBitmap& bitmap) override;
    // [INTENT] Override OnDrawItem to draw bitmaps with unscaled size on Retina displays.
    // [EVENT] Called during item painting.
    void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const override;
#endif

#ifdef _WIN32
    // [INTENT] Override MSWOnDraw for custom Windows drawing of combo box items.
    // [EVENT] Called during Windows paint message.
    bool MSWOnDraw(WXDRAWITEMSTRUCT* item) override;
    // [INTENT] Helper to draw background for Windows combo box items.
    void DrawBackground_(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const;

public:
    // [INTENT] Rescale bitmaps after DPI change on Windows.
    void Rescale();
#endif
};

}} // namespace Slic3r::GUI
#endif
