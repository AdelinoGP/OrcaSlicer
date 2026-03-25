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
    int       m_index;
    wxWindow* m_tab_button;
    wxWindow* m_tab_text;
};
WX_DECLARE_HASH_MAP(int, Select*, wxIntegerHash, wxIntegerEqual, SelectHash);

// [INTENT] Manages the keyboard shortcuts UI dialog, allowing users to view shortcuts.
// [UNITY] Replace with a custom Unity Dialog MonoBehaviour (e.g., UI Toolkit UIDocument or uGUI Canvas based Dialog).
// [PORTING_HAZARD:P2] Requires complete UI reimplementation; wxWidgets specific sizers need replacement by Unity Layout Groups or Flex
// layout in UI Toolkit.
class KBShortcutsDialog : public DPIDialog
{
    typedef std::pair<std::string, std::string>                 Shortcut;
    typedef std::vector<Shortcut>                               Shortcuts;
    typedef std::pair<std::pair<wxString, wxString>, Shortcuts> ShortcutsItem;
    typedef std::vector<ShortcutsItem>                          ShortcutsVec;

    // [STATE] Stores the shortcut data structure populated on initialization.
    ShortcutsVec m_full_shortcuts;
    // [STATE] Logo bitmap. [UNITY] Replace with UnityEngine.Texture2D or UnityEngine.Sprite in a UnityEngine.UI.Image.
    ScalableBitmap m_logo_bmp;
    // [STATE] Header image. [UNITY] Replace with UnityEngine.UI.Image.
    wxStaticBitmap*       m_header_bitmap;
    std::vector<wxPanel*> m_pages;

public:
    KBShortcutsDialog();
    wxWindow* create_button(int id, wxString text);
    // [EVENT] Handles tab selection. [UNITY] Use UI event system (e.g., button.onClick listeners).
    void OnSelectTabel(wxCommandEvent& event);
    // [STATE] Selection panel, right sizer, simplebook container, body sizer, hash selector.
    // [UNITY] Replace these wxWidgets components with their UI Toolkit or uGUI counterparts.
    // [PORTING_HAZARD:P2] wxSimplebook needs to be mapped to a tab view or a Multi-View UI system in Unity.
    wxPanel*      m_panel_selects;
    wxBoxSizer*   m_sizer_right;
    wxSimplebook* m_simplebook;
    wxBoxSizer*   m_sizer_body;
    SelectHash    m_hash_selector;

protected:
    // [THREAD] This dialog handles GUI events; must be accessed or updated only on the Main/UI thread in Unity.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void     fill_shortcuts();
    wxPanel* create_header(wxWindow* parent, const wxFont& bold_font);
    wxPanel* create_page(wxWindow* parent, const ShortcutsItem& shortcuts, const wxFont& font, const wxFont& bold_font);
};

}} // namespace Slic3r::GUI

#endif
