// [ANNOTATED]
// [INTENT] Declaration boundary for the keyboard shortcuts dialog: it owns the left-rail category buttons, the generated shortcut-page
// dataset, and the `wxSimplebook` used to swap between categories.
// [STATE] `m_full_shortcuts` stores the static shortcut catalog, `m_hash_selector` caches the custom tab-row widgets by index, and
// `m_pages`/`m_simplebook` keep the generated page tree alive for the modal dialog lifetime.
// [EVENT] User clicks on either the custom tab shell or its label are normalized into `EVT_PREFERENCES_SELECT_TAB`, which then drives page
// selection and selected-tab restyling through `OnSelectTabel`.
// [UNITY] Port as a retained modal/panel with a serialized shortcut-category model, a list of selectable tab items, and a bound page view
// instead of constructing and restyling raw wx widgets manually.
// [PORTING_HAZARD:P2] The public surface is small, but the dialog depends on cached widget pointers, `wxSimplebook`, and imperative style
// changes; Unity should split tab state from the view objects so DPI/theme refreshes do not depend on mutating live controls.

#ifndef slic3r_GUI_KBShortcutsDialog_hpp_
#define slic3r_GUI_KBShortcutsDialog_hpp_

#include <wx/wx.h>
#include <map>
#include <vector>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include <wx/simplebook.h>

namespace Slic3r { namespace GUI {

class Select
{
public:
    // [STATE] Logical tab index used to map click events to the generated shortcut page.
    int m_index;
    // [STATE] Root clickable row for one left-rail tab.
    wxWindow* m_tab_button;
    // [STATE] Child text widget restyled alongside the row when selection changes.
    wxWindow* m_tab_text;
};
WX_DECLARE_HASH_MAP(int, Select*, wxIntegerHash, wxIntegerEqual, SelectHash);

// [INTENT] Modal browser for grouped keyboard shortcuts.
// [UNITY] Replace with a retained dialog controller plus a data-driven tab/page view.
class KBShortcutsDialog : public DPIDialog
{
    typedef std::pair<std::string, std::string>                 Shortcut;
    typedef std::vector<Shortcut>                               Shortcuts;
    typedef std::pair<std::pair<wxString, wxString>, Shortcuts> ShortcutsItem;
    typedef std::vector<ShortcutsItem>                          ShortcutsVec;

    // [STATE] Eagerly built shortcut groups used to generate every page in the dialog.
    ShortcutsVec m_full_shortcuts;
    // [STATE] Header/logo bitmap assets cached for DPI-aware dialog rendering.
    // [UNITY] Replace with UnityEngine.Texture2D or UnityEngine.Sprite in a UnityEngine.UI.Image.
    ScalableBitmap m_logo_bmp;
    // [STATE] Header image widget shown above the shortcut content.
    // [UNITY] Replace with UnityEngine.UI.Image.
    wxStaticBitmap* m_header_bitmap;
    // [STATE] Generated shortcut pages, one per top-level shortcut category.
    std::vector<wxPanel*> m_pages;

public:
    KBShortcutsDialog();
    // [INTENT] Builds one custom left-rail tab row and registers its widgets in `m_hash_selector`.
    wxWindow* create_button(int id, wxString text);
    // [EVENT] Handles the normalized tab-selection event and updates both the current page and tab styling.
    // [UNITY] Use a selected-index binding or button `onClick` listeners instead of a custom wx command event.
    void OnSelectTabel(wxCommandEvent& event);
    // [STATE] Container for the custom left-rail tab widgets.
    wxPanel* m_panel_selects;
    // [STATE] Layout branch that hosts the active page area.
    wxBoxSizer* m_sizer_right;
    // [STATE] wx page-switcher that owns one generated page per shortcut category.
    // [UNITY] Replace with a tab view or a retained page stack bound to selected tab state.
    // [PORTING_HAZARD:P2] `wxSimplebook` selection and child lifetime are central to the dialog's behavior.
    wxSimplebook* m_simplebook;
    // [STATE] Root horizontal body layout joining the tab rail and page content.
    wxBoxSizer* m_sizer_body;
    // [STATE] Lookup table for selected/unselected restyling without traversing the widget tree.
    SelectHash m_hash_selector;

protected:
    // [THREAD] Runs on the UI thread to rescale cached bitmaps and relayout the dialog after DPI changes.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [STATE] Populates `m_full_shortcuts` from the app/platform-specific shortcut definitions.
    void fill_shortcuts();
    // [INTENT] Builds the static header area shown above each shortcut page.
    wxPanel* create_header(wxWindow* parent, const wxFont& bold_font);
    // [INTENT] Builds one scrollable shortcut page from a single `ShortcutsItem` entry.
    wxPanel* create_page(wxWindow* parent, const ShortcutsItem& shortcuts, const wxFont& font, const wxFont& bold_font);
};

}} // namespace Slic3r::GUI

#endif
