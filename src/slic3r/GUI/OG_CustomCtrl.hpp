#ifndef slic3r_OG_CustomCtrl_hpp_
#define slic3r_OG_CustomCtrl_hpp_

// [INTENT] This header declares OG_CustomCtrl, a custom wxPanel that renders and manages
// interactive UI elements for OptionsGroup (labels, edit boxes, checkboxes, etc.) using
// direct wxDC drawing instead of standard wxWidgets controls. This allows for highly
// customized layout, blinking icons, mode-dependent visibility, and pixel-perfect positioning.

#include <wx/stattext.h>
#include <wx/settings.h>

#include <map>
#include <functional>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "OptionsGroup.hpp"
#include "I18N.hpp"

// Translate the ifdef
#ifdef __WXOSX__
#define wxOSX true
#else
#define wxOSX false
#endif

namespace Slic3r { namespace GUI {

// [INTENT] OG_CustomCtrl is a custom wxPanel that renders and manages
// interactive UI elements for OptionsGroup (labels, edit boxes, checkboxes, etc.) using
// direct wxDC drawing instead of standard wxWidgets controls. This allows for highly
// customized layout, blinking icons, mode-dependent visibility, and pixel-perfect positioning.
// [STATE] Key state variables:
// - m_font: font used for text rendering.
// - m_v_gap, m_h_gap: vertical/horizontal gaps between elements.
// - m_em_unit: unit size for scaling.
// - m_bmp_mode_sz, m_bmp_blinking_sz: sizes of mode and blinking bitmaps.
// - m_max_win_width: maximum width of the control window.
// - ctrl_lines: vector of CtrlLine structs representing each line of UI elements.
// [EVENT] Event handlers:
// - OnPaint: triggered on paint events, calls render for each CtrlLine.
// - OnMotion, OnLeftDown, OnLeaveWin: mouse event handlers for interaction.
// [UNITY] Unity replacement: A custom UI Toolkit VisualElement with custom USS styling
// and a MonoBehaviour controller for layout and event handling. Each CtrlLine could map
// to a sub-VisualElement with custom drawing via UI Toolkit's custom VisualElement or
// IMGUI for precise control. Alternatively, use a Canvas with UIElements and a C# script
// that handles drawing via OnGUI or custom meshes.
// [PORTING_HAZARD:P2] Custom wxDC drawing requires complete redesign in Unity; must map to UI Toolkit custom VisualElement or IMGUI, which
// may impact performance and require careful layout management.
class OG_CustomCtrl : public wxPanel
{
    wxFont m_font;
    int    m_v_gap;
    int    m_v_gap2;
    int    m_h_gap;
    int    m_em_unit;

    wxSize m_bmp_mode_sz;
    wxSize m_bmp_blinking_sz;

    int m_max_win_width{0};

    // [INTENT] CtrlLine represents a single line of UI elements within the custom control.
    // It holds layout dimensions, visibility flags, and rendering data for a Line from OptionsGroup.
    // [STATE] Key members:
    // - width, height: dimensions of the line.
    // - ctrl: back-pointer to parent OG_CustomCtrl (nullable after destruction).
    // - og_line: reference to the original Line definition.
    // - draw_just_act_buttons, draw_mode_bitmap, is_visible, is_focused: rendering/interaction flags.
    // - rects_undo_icon, rects_edit_icon: hit-test rectangles for icons.
    // [EVENT] render() method draws the line using wxDC.
    // [UNITY] Map to a UI Toolkit VisualElement or a custom drawn IMGUI control.
    struct CtrlLine
    {
        wxCoord        width{wxDefaultCoord};
        wxCoord        height{wxDefaultCoord};
        OG_CustomCtrl* ctrl{nullptr};
        const Line&    og_line;

        bool draw_just_act_buttons{false};
        bool draw_mode_bitmap{true};
        bool is_visible{true};
        bool is_focused{false};

        CtrlLine(wxCoord height, OG_CustomCtrl* ctrl, const Line& og_line, bool draw_just_act_buttons = false, bool draw_mode_bitmap = true);
        ~CtrlLine() { ctrl = nullptr; }

        int  get_max_win_width();
        void correct_items_positions();
        void msw_rescale();
        void update_visibility(ConfigOptionMode mode);

        void render_separator(wxDC& dc, wxCoord v_pos);

        void    render(wxDC& dc, wxCoord h_pos, wxCoord v_pos);
        wxCoord draw_text(
            wxDC& dc, wxPoint pos, const wxString& text, const wxColour* color, int width, bool is_url = false, bool is_main = false);
        wxPoint draw_blinking_bmp(wxDC& dc, wxPoint pos, bool is_blinking);
        wxPoint draw_act_bmps(
            wxDC& dc, wxPoint pos, const wxBitmap& bmp_undo_to_sys, const wxBitmap& bmp_undo, bool is_blinking, size_t rect_id = 0);
        wxCoord draw_edit_bmp(wxDC& dc, wxPoint pos, const wxBitmap& bmp_edit);
        bool    launch_browser() const;
        bool    is_separator() const { return og_line.is_separator(); }

        std::vector<wxRect> rects_undo_icon;
        std::vector<wxRect> rects_undo_to_sys_icon;
        std::vector<wxRect> rects_edit_icon;
        wxRect              rect_label;
    };

    std::vector<CtrlLine> ctrl_lines;

public:
    OG_CustomCtrl(wxWindow*          parent,
                  OptionsGroup*      og,
                  const wxPoint&     pos  = wxDefaultPosition,
                  const wxSize&      size = wxDefaultSize,
                  const wxValidator& val  = wxDefaultValidator,
                  const wxString&    name = wxEmptyString);
    ~OG_CustomCtrl() {}

    // [EVENT] Paint handler: triggers rendering of all CtrlLines via wxDC.
    void OnPaint(wxPaintEvent&);
    // [EVENT] Mouse motion handler: updates focus and tooltip.
    void OnMotion(wxMouseEvent& event);
    // [EVENT] Left mouse button down: handles click actions (edit, undo, etc.).
    void OnLeftDown(wxMouseEvent& event);
    // [EVENT] Mouse leave window: clears focus.
    void OnLeaveWin(wxMouseEvent& event);

    // [INTENT] Initialize CtrlLine objects from OptionsGroup lines.
    void init_ctrl_lines();
    // [INTENT] Update visibility of all CtrlLines based on mode (e.g., simple/advanced/expert).
    // Returns true if any visibility changed.
    bool update_visibility(ConfigOptionMode mode);
    // [INTENT] Correct window position for a Field within a Line.
    void correct_window_position(wxWindow* win, const Line& line, Field* field = nullptr);
    // [INTENT] Correct widget position for a Field within a Line.
    void correct_widgets_position(wxSizer* widget, const Line& line, Field* field = nullptr);
    // [INTENT] Compute and store the maximum window width needed.
    void init_max_win_width();
    // [INTENT] Set the maximum window width (used by parent OptionsGroup).
    void set_max_win_width(int max_win_width);
    // [INTENT] Get the current maximum window width.
    int get_max_win_width() { return m_max_win_width; }

    // BBS
    // [INTENT] Get the width needed for title column (Bambu Studio specific).
    int get_title_width();
    // BBS
    // [INTENT] Adjust item positions after layout changes (Bambu Studio specific).
    void fixup_items_positions();

    // [INTENT] Rescale UI elements for DPI changes.
    void msw_rescale();
    // [INTENT] Update system colors (e.g., when theme changes).
    void sys_color_changed();

    // [INTENT] Calculate position for a given Line and optional Field.
    wxPoint get_pos(const Line& line, Field* field = nullptr);
    // [INTENT] Calculate height for a given Line.
    int get_height(const Line& line);

    // [STATE] Pointer to the OptionsGroup that owns this control.
    OptionsGroup* opt_group;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_OG_CustomCtrl_hpp_ */
