#ifndef slic3r_GUI_DropDown_hpp_
#define slic3r_GUI_DropDown_hpp_

#include <boost/date_time/posix_time/posix_time.hpp>
#include <wx/stattext.h>
#include "../wxExtensions.hpp"
#include "StateHandler.hpp"
#include "PopupWindow.hpp"

#define DD_NO_CHECK_ICON 0x0001
#define DD_NO_TEXT 0x0002
#define DD_STYLE_MASK 0x0003

#define DD_ITEM_STYLE_SPLIT_ITEM 0x0001 // ----text----, text with horizontal line arounds
#define DD_ITEM_STYLE_DISABLED 0x0002   // ----text----, text with horizontal line arounds

wxDECLARE_EVENT(EVT_DISMISS, wxCommandEvent);

class DropDown : public PopupWindow
{
public:
    // [INTENT] Popup selector boundary for grouped items: it owns the transient popup chrome, but the item vector stays caller-owned and is
    // reused by nested submenus. [UNITY] Map this to a retained dropdown root with a separate popup submenu presenter and explicit
    // dismissal/selection state.
    struct Item
    {
        wxString text;
        wxString text_static_tips; // display static tips for TextInput.eg.PrinterInfoBox
        wxBitmap icon;
        wxBitmap icon_textctrl; // display icon for TextInput.eg.PrinterInfoBox
        void*    data{nullptr}; // [STATE] Non-owning payload pointer; the popup only forwards it with selection events.
        wxString group{};
        wxString alias{};
        wxString tip{};
        int      flag{0};
        int      style{0}; // the style of item
    };

private:
    std::vector<Item>& items;    // [STATE] Caller-owned model; nested submenus and the popup root share the same backing storage.
                                 // [PORTING_HAZARD:P2] Unity needs an explicit model object because this header assumes reference stability
                                 // across submenu clones.
    size_t   count = 0;          // [STATE] Cached visible item count after filtering/group measurement.
    wxString group;              // [STATE] Active group label for submenu expansion and hover routing.
    bool     need_sync  = false; // [STATE] Marks deferred item/selection synchronization after mutations.
    int      selection  = -1;    // [STATE] Current committed selection index, mirrored into nested popups.
    int      hover_item = -1;    // [STATE] Hot row index used for hover highlight and submenu activation.

    DropDown* subDropDown{nullptr};  // [STATE] Lazily created child popup for grouped branches; parent owns dismissal choreography.
                                     // [PORTING_HAZARD:P2] The nested popup hierarchy depends on raw pointers and direct dismissal calls,
                                     // which Unity should replace with explicit controller ownership.
    DropDown* mainDropDown{nullptr}; // [STATE] Back-link to the root popup so submenu hover/selection can propagate upward.

    double radius                  = 0;     // [STATE] Rounded-corner styling for custom paint.
    bool   use_content_width       = false; // [STATE] Auto-size popup width to content metrics instead of the trigger width.
    bool   limit_max_content_width = false; // [STATE] Clamp content-based width to avoid overexpansion.
    bool   align_icon              = false; // [STATE] Keeps icon/text alignment consistent with paired text controls.
    bool   text_off                = false; // [STATE] Suppresses text painting when the dropdown is acting as an icon-only selector.

    wxSize textSize; // [STATE] Measured text bounds used by popup layout and scrolling math.
    wxSize iconSize; // [STATE] Measured icon bounds used by row height/spacing.
    wxSize rowSize;  // [STATE] Final row metrics shared by rendering, hover hit-testing, and submenu positioning.

    StateHandler   state_handler;             // [STATE] Centralized visual state machine for hover/pressed/disabled paint variants.
    StateColor     text_color;                // [STATE] Theme-aware text palette.
    StateColor     border_color;              // [STATE] Theme-aware popup border palette.
    StateColor     selector_border_color;     // [STATE] Theme-aware selection outline palette.
    StateColor     selector_background_color; // [STATE] Theme-aware highlighted row background palette.
    ScalableBitmap check_bitmap;              // [STATE] Optional checkmark glyph reused across root and submenu popups.
    ScalableBitmap arrow_bitmap;              // [STATE] Submenu arrow glyph for grouped rows.

    bool                     pressedDown = false; // [STATE] Tracks mouse capture/press lifecycle for click-versus-drag behavior.
    boost::posix_time::ptime dismissTime;         // [STATE] Timestamp used to suppress immediate reopen after dismissal.
    wxPoint                  offset;              // [STATE] Scroll offset for long lists; x is unused in the current layout.
    wxPoint                  dragStart;           // [STATE] Pointer origin for drag-to-scroll and gesture disambiguation.

public:
    DropDown(std::vector<Item>& items);

    DropDown(wxWindow* parent, std::vector<Item>& items, long style = 0);

    void Create(wxWindow* parent, long style = 0);

public:
    void Invalidate(bool clear = false);

    int GetSelection() const { return selection; }

    // [INTENT] Updates the committed selection and forwards it to any active submenu mirror.
    void SetSelection(int n);

    wxString GetValue() const;
    void     SetValue(const wxString& value);

public:
    void SetCornerRadius(double radius);

    void SetBorderColor(StateColor const& color);

    void SetSelectorBorderColor(StateColor const& color);

    void SetTextColor(StateColor const& color);

    void SetSelectorBackgroundColor(StateColor const& color);

    void SetUseContentWidth(bool use, bool limit_max_content_width = false);

    void SetAlignIcon(bool align);

public:
    void Rescale();

    // [INTENT] Tracks whether the popup has remained dismissed long enough to allow a fresh open gesture.
    bool HasDismissLongTime();

protected:
    void Dismiss() override;

    void OnDismiss() override;

private:
    // [OPENGL] No GL here; the paint path is CPU-side wxDC drawing and must stay in the UI thread.
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    // [INTENT] Draws the popup body, selected row, hover state, and nested submenu affordances.
    void render(wxDC& dc);

    int hoverIndex();

    int selectedItem();

    friend class ComboBox;
    // [INTENT] Computes row sizing and group boundaries before popup placement.
    void messureSize();
    // [INTENT] Anchors the popup relative to its parent trigger or submenu row.
    void autoPosition();

    // some useful events
    // [EVENT] Mouse handlers implement press, drag, hover, wheel, and submenu dismissal routing.
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseCaptureLost(wxMouseCaptureLostEvent& event);
    void mouseMove(wxMouseEvent& event);
    void mouseWheelMoved(wxMouseEvent& event);

    // [EVENT] Emits the selection change as a command event to the owning control.
    void sendDropDownEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_DropDown_hpp_
