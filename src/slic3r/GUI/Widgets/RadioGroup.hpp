#ifndef slic3r_GUI_RADIOGROUP_hpp_
#define slic3r_GUI_RADIOGROUP_hpp_

#include "../wxExtensions.hpp"

#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

#include <vector>
#include <string>

#include "Button.hpp"

// [INTENT] Composite radio-style selector that owns both the icon and text
// widgets for each option so the whole group behaves like one closed selection
// control instead of a loose set of independent buttons.
// [UNITY] Port as a retained radio-list component with a single selected index,
// row/column layout, and a group-level selection-changed event.
// [PORTING_HAZARD:P3] Hover and focus are split across sibling controls in wx,
// so a Unity version should keep each option as one interactive row.
class RadioGroup : public wxPanel
{
public:
    RadioGroup();

    RadioGroup(wxWindow*                    parent,
               const std::vector<wxString>& labels        = {"1", "2", "3"},
               long                         direction     = wxHORIZONTAL,
               int                          row_col_limit = -1);

    // [INTENT] Build or rebuild the paired bitmap/text option rows and derive
    // the layout from the provided labels and grid direction.
    // [STATE] Replaces the cached label list, child widget arrays, and item
    // count in one pass so selection state can be recomputed from the model.
    void Create(wxWindow*                    parent,
                const std::vector<wxString>& labels        = {"1", "2", "3"},
                long                         direction     = wxHORIZONTAL,
                int                          row_col_limit = -1);

    // [STATE] Exposes the derived selected index used by the keyboard and
    // pointer handlers.
    int GetSelection();

    // [EVENT] Selection updates focus ownership, refresh every icon from the
    // derived visual state, and emit the radio-box selection event.
    void SetSelection(int index, bool focus = false);

    // [EVENT] Keyboard navigation wraps within the group so it acts like one
    // closed radio list.
    void SelectNext(bool focus = true);

    // [EVENT] Keyboard navigation wraps within the group so it acts like one
    // closed radio list.
    void SelectPrevious(bool focus = true);

    // [EVENT] Keep the base panel enable state and child interactability in
    // sync, then broadcast the enable-change event to hosts.
    bool Enable(bool enable = true) override;

    // [STATE] Mirrors the cached enabled flag instead of querying children.
    bool IsEnabled();

    // [EVENT] Convenience wrapper for callers that need the standard wx-style
    // disabled state.
    bool Disable();

    // [EVENT] Apply a shared tooltip to both halves of the option row.
    void SetRadioTooltip(int i, wxString tooltip);

private:
    // [STATE] Option labels are the source data for row construction and the
    // string payload of emitted selection events.
    std::vector<wxString> m_labels;
    // [STATE] Bitmap and text children are owned by the panel and rebuilt from
    // the label list.
    std::vector<wxStaticBitmap*> m_radioButtons;
    std::vector<Button*>         m_labelButtons;

    // [STATE] Current selection, item count, focus flag, and enabled flag drive
    // all visual state transitions.
    int  m_selectedIndex;
    int  m_item_count;
    bool m_focused;
    bool m_enabled;

    // [STATE] Cached theme colors used to derive focus and text appearance.
    StateColor m_focus_color;
    StateColor m_text_color;

    // [STATE] DPI-scaled sprites for selected, hover, and disabled states.
    ScalableBitmap m_on;
    ScalableBitmap m_off;
    ScalableBitmap m_on_hover;
    ScalableBitmap m_off_hover;
    ScalableBitmap m_disabled;

    // [INTENT] Derive the correct icon for a row from selection, hover, and
    // enabled state rather than storing per-item visual state separately.
    void SetRadioIcon(int i, bool hover);
};

#endif // !slic3r_GUI_RADIOGROUP_hpp_
