#include "BitmapComboBox.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>
#include <wx/odcombo.h>
#include <wx/listbook.h>
#include <wx/window.h>

#ifdef __WINDOWS__
#include <wx/msw/dcclient.h>
#include <wx/msw/private.h>
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif //_MSW_DARK_MODE
#endif //__WINDOWS__

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

// A workaround for a set of issues related to text fitting into gtk widgets:
// See e.g.: https://github.com/prusa3d/PrusaSlicer/issues/4584
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <glib-2.0/glib-object.h>
    #include <pango-1.0/pango/pango-layout.h>
    #include <gtk/gtk.h>
#endif

using Slic3r::GUI::format_wxstr;

#define BORDER_W 10

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------

namespace Slic3r { namespace GUI {

// [INTENT] Keep the preset combo entries aligned by forcing the same bitmap/text row size even when wxBitmap scales differently per
// platform. [UNITY] Map this to a UI Toolkit `ListView` or `PopupField` item template where the icon and text widths are bounded by the
// VisualElement layout. [PORTING_HAZARD:P2] Unity must reckon with `EditorGUIUtility.pixelsPerPoint` instead of relying on the wx scaling
// metadata that the overrides below manipulate.
/* For PresetComboBox we use bitmaps that are created from images that are already scaled appropriately for Retina
 * (Contrary to the intuition, the `scale` argument for Bitmap's constructor doesn't mean
 * "please scale this to such and such" but rather
 * "the wxImage is already sized for backing scale such and such". )
 * Unfortunately, the constructor changes the size of wxBitmap too.
 * Thus We need to use unscaled size value for bitmaps that we use
 * to avoid scaled size of control items.
 * For this purpose control drawing methods and
 * control size calculation methods (virtual) are overridden.
 **/

// [INTENT] Keep the preset combo box consistent with the rest of the app while still supporting owner-drawn icons/text.
// [STATE] Locks in the app-normal font and Windows-only text-change suppression so selection events stay predictable on the main thread.
// [UNITY] Translate to a UI Toolkit `PopupField` or `ListView` where a MonoBehaviour manages fonts, margins, and platform-specific
// selection callbacks. [PORTING_HAZARD:P3] Windows hacks like `EnableTextChangedEvents(false)` must be replaced with explicit deduplication
// in Unity because Unity lacks that event hook.
BitmapComboBox::BitmapComboBox(wxWindow*       parent,
                               wxWindowID      id /* = wxID_ANY*/,
                               const wxString& value /* = wxEmptyString*/,
                               const wxPoint&  pos /* = wxDefaultPosition*/,
                               const wxSize&   size /* = wxDefaultSize*/,
                               int             n /* = 0*/,
                               const wxString  choices[] /* = NULL*/,
                               long            style /* = 0*/)
    : wxBitmapComboBox(parent, id, value, pos, size, n, choices, style)
{
    SetFont(Slic3r::GUI::wxGetApp().normal_font());
#ifdef _WIN32
    // Workaround for ignoring CBN_EDITCHANGE events, which are processed after the content of the combo box changes, so that
    // the index of the item inside CBN_EDITCHANGE may no more be valid.
    EnableTextChangedEvents(false);
    wxGetApp().UpdateDarkUI(this);
    if (!HasFlag(wxCB_READONLY))
        wxTextEntry::SetMargins(0, 0);
#endif /* _WIN32 */
}

BitmapComboBox::~BitmapComboBox() {}

#ifdef __APPLE__
// [INTENT] Guarantee the first bitmap determines the row height so Retina assets stay aligned with the rest of the list.
// [STATE] Caches `m_usedImgSize` from the first `wxBitmap` and forces the control to recalculate its best size.
// [THREAD] Runs on the UI thread because it mutates `wxWindow` sizes and invalidates layout.
// [UNITY] Unity should precompute the first `Texture2D` dimensions on the main thread and signal the VisualElement resolver to update the
// `height` of each item template. [PORTING_HAZARD:P2] wxBitmap exposes `GetScaledWidth`/`GetScaledHeight`, so Unity must mirror
// DPI-awareness by consulting `EditorGUIUtility.pixelsPerPoint` when measuring textures.
bool BitmapComboBox::OnAddBitmap(const wxBitmap& bitmap)
{
    if (bitmap.IsOk()) {
        // we should use scaled! size values of bitmap
        int width  = (int) bitmap.GetScaledWidth();
        int height = (int) bitmap.GetScaledHeight();

        if (m_usedImgSize.x < 0) {
            // If size not yet determined, get it from this image.
            m_usedImgSize.x = width;
            m_usedImgSize.y = height;

            // Adjust control size to vertically fit the bitmap
            wxWindow* ctrl = GetControl();
            ctrl->InvalidateBestSize();
            wxSize newSz = ctrl->GetBestSize();
            wxSize sz    = ctrl->GetSize();
            if (newSz.y > sz.y)
                ctrl->SetSize(sz.x, newSz.y);
            else
                DetermineIndent();
        }

        wxCHECK_MSG(width == m_usedImgSize.x && height == m_usedImgSize.y, false, "you can only add images of same size");

        return true;
    }

    return false;
}

// [EVENT] Owner-draw callback that paints the cached bitmap and text for each entry so the selection rectangle matches the measured size.
// [STATE] The cached `m_usedImgSize` and `m_imgAreaWidth` keep icons/text aligned, preventing layout jumps during redraws.
// [UNITY] Replace this with a UI Toolkit `ListView` item template bound to an `Image`/`Label` pair, using the first texture's size to pad the row.
void BitmapComboBox::OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const
{
    const wxBitmap& bmp = *(static_cast<wxBitmap*>(m_bitmaps[item]));
    if (bmp.IsOk()) {
        // we should use scaled! size values of bitmap
        wxCoord w = bmp.GetScaledWidth();
        wxCoord h = bmp.GetScaledHeight();

        const int imgSpacingLeft = 4;

        // Draw the image centered
        dc.DrawBitmap(bmp, rect.x + (m_usedImgSize.x - w) / 2 + imgSpacingLeft, rect.y + (rect.height - h) / 2, true);
    }

    wxString text = GetString(item);
    if (!text.empty())
        dc.DrawText(text, rect.x + m_imgAreaWidth + 1, rect.y + (rect.height - dc.GetCharHeight()) / 2);
}
#endif

#ifdef _WIN32

// [INTENT] Create a tiny placeholder bitmap so MSW owner-draw combo has valid `wxBitmapRefData` even when no icon is attached.
// [STATE] Fills the combo with a zero-width bitmap and then assigns it via `DoSetItemBitmap` so the control's internal cache stays sane.
// [UNITY] In Unity this would translate to creating a default `Texture2D` (or null) and letting the VisualElement layout ignore it until
// real assets load. [PORTING_HAZARD:P3] Directly manipulating `m_width` in `wxBitmapRefData` can't be mirrored in Unity; a port should keep
// track of empty placeholder textures on its own.
int BitmapComboBox::Append(const wxString& item)
{
    // Workaround for a correct rendering of the control without Bitmap (under MSW):
    // 1. We should create small Bitmap to fill Bitmaps RefData,
    //   ! in this case wxBitmap.IsOK() return true.
    // 2. But then set width to 0 value for no using of bitmap left and right spacing
    // 3. Set this empty bitmap to the at list one item and BitmapCombobox will be recreated correct

    wxBitmap bitmap(1, int(1.6 * wxGetApp().em_unit() + 1));
    {
        // bitmap.SetWidth(0); is depricated now
        // so, use next code
        bitmap.UnShare(); // AllocExclusive();
        bitmap.GetGDIImageData()->m_width = 0;
    }

    OnAddBitmap(bitmap);
    const int n = wxComboBox::Append(item);
    if (n != wxNOT_FOUND)
        DoSetItemBitmap(n, bitmap);
    return n;
}

enum OwnerDrawnComboBoxPaintingFlags {
    ODCB_PAINTING_DISABLED = 0x0004,
};

// [EVENT] WM_DRAWITEM handler that interprets selection/disabled state so the owner-draw combo paints the right highlight.
// [STATE] Pulls flags from `WXDRAWITEMSTRUCT` to adjust `wxODCB_PAINTING_SELECTED`/`DISABLED` and preserve the current text value.
// [THREAD] Fired on the UI thread; deviating to a worker thread would break the GDI context.
// [UNITY] Unity equivalents would use a VisualElement `Selectable` style with `PseudoStates`, driven by the MonoBehaviour tracking
// selection. [PORTING_HAZARD:P3] This low-level GDI handling has no counterpart in Unity, so the port must reimplement selection highlights
// through styles rather than direct DC draws.
bool BitmapComboBox::MSWOnDraw(WXDRAWITEMSTRUCT* item)
{
    LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT) item;
    int              pos        = lpDrawItem->itemID;

    // Draw default for item -1, which means 'focus rect only'
    if (pos == -1)
        return false;

    int flags = 0;
    if (lpDrawItem->itemState & ODS_COMBOBOXEDIT)
        flags |= wxODCB_PAINTING_CONTROL;
    if (lpDrawItem->itemState & ODS_SELECTED)
        flags |= wxODCB_PAINTING_SELECTED;
    if (lpDrawItem->itemState & ODS_DISABLED)
        flags |= ODCB_PAINTING_DISABLED;

    wxPaintDCEx dc(this, lpDrawItem->hDC);
    wxRect      rect = wxRectFromRECT(lpDrawItem->rcItem);

    DrawBackground_(dc, rect, pos, flags);

    wxString text;

    if (flags & wxODCB_PAINTING_CONTROL) {
        // Don't draw anything in the editable selection field.
        // if (!HasFlag(wxCB_READONLY))
        //    return true;

        pos = GetSelection();
        // Skip drawing if there is nothing selected.
        if (pos < 0)
            return true;

        text = GetValue();
    } else {
        text = GetString(pos);
    }

    wxBitmapComboBoxBase::DrawItem(dc, rect, pos, text, flags);

    return true;
}

// [STATE] Draws highlights using the app theme colors so selection/disabled flags appear consistent with the rest of the UI.
// [UNITY] In Unity this would be a `VisualElement` style state (`:selected`, `:disabled`) controlled by the MonoBehaviour selection
// tracker. [PORTING_HAZARD:P3] Relies on `wxGetApp().get_highlight_default_clr()` and `get_window_default_clr()`, which must be re-mapped
// to Unity's color palette.
void BitmapComboBox::DrawBackground_(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const
{
    if (flags & wxODCB_PAINTING_SELECTED) {
        const int vSizeDec = 0; // Vertical size reduction of selection rectangle edges

        dc.SetTextForeground(wxGetApp().get_label_highlight_clr());

        wxColour selCol = wxGetApp().get_highlight_default_clr();
        dc.SetPen(selCol);
        dc.SetBrush(selCol);
        dc.DrawRectangle(rect.x, rect.y + vSizeDec, rect.width, rect.height - (vSizeDec * 2));
    } else {
        dc.SetTextForeground(flags & ODCB_PAINTING_DISABLED ? wxColour(108, 108, 108) : wxGetApp().get_label_clr_default());

        wxColour selCol = flags & ODCB_PAINTING_DISABLED ?
                              // #ifdef _MSW_DARK_MODE
                              // wxRGBToColour(NppDarkMode::GetSofterBackgroundColor()) :
                              // #else
                              wxGetApp().get_highlight_default_clr() :
                              // #endif
                              wxGetApp().get_window_default_clr();
        dc.SetPen(selCol);
        dc.SetBrush(selCol);
        dc.DrawRectangle(rect);
    }
}

// [INTENT] Rebuild the combo entries so DPI/resolution changes reflow the owner-drawn bitmaps with recalculated sizes.
// [STATE] Keeps the currently selected string while repopulating the list to avoid losing focus.
// [THREAD] Must be invoked on the UI thread because it touches the control contents directly.
// [UNITY] Unity ports should respond to `CanvasScaler` or UI Toolkit DPI events by refreshing the item templates instead of brute-force
// reappend. [PORTING_HAZARD:P3] Clearing and re-adding items to recompute sizes is expensive; Unity should only do this when `Screen.dpi`
// actually changes and should share `Texture2D` references.
void BitmapComboBox::Rescale()
{
    // Next workaround: To correct scaling of a BitmapCombobox
    // we need to refill control with new bitmaps
    const wxString        selection = this->GetValue();
    std::vector<wxString> items;
    for (size_t i = 0; i < GetCount(); i++)
        items.push_back(GetString(i));

    this->Clear();
    for (const wxString& item : items)
        Append(item);
    this->SetValue(selection);
}
#endif

}} // namespace Slic3r::GUI
