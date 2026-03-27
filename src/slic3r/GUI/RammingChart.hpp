// Orca: This file is ported from latest PrusaSlicer

#ifndef RAMMING_CHART_H_
#define RAMMING_CHART_H_

#include <vector>
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

wxDECLARE_EVENT(EVT_WIPE_TOWER_CHART_CHANGED, wxCommandEvent);

// [EVENT] The chart broadcasts a command event after curve/volume recomputation so the wipe-tower workflow can
// refresh dependent UI and summaries without polling the widget.
// [UNITY] Model this as a custom controller event or C# callback from a dedicated chart component, not as a generic
// form control.
class Chart : public wxWindow
{
public:
    // [INTENT] Constructor seeds the chart with an editable control-point set, chart bounds, and sampling scale.
    // [STATE] It establishes the initial plot rectangle, visible math-space range, and derived line cache.
    Chart(wxWindow*                                   parent,
          wxRect                                      rect,
          const std::vector<std::pair<float, float>>& initial_buttons,
          int                                         ramming_speed_size,
          float                                       sampling,
          int                                         scale_unit = 10)
        : wxWindow(parent, wxID_ANY, rect.GetTopLeft(), rect.GetSize()), scale_unit(scale_unit), legend_side(5 * scale_unit)
    {
        SetBackgroundColour(*wxWHITE);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_rect       = wxRect(wxPoint(legend_side, 0), rect.GetSize() - wxSize(legend_side, legend_side));
        visible_area = wxRect2DDouble(0.0, 0.0, sampling * ramming_speed_size, 60.);
        m_buttons.clear();
        if (initial_buttons.size() > 0)
            for (const auto& pair : initial_buttons)
                m_buttons.push_back(wxPoint2DDouble(pair.first, pair.second));
        recalculate_line();
    }
    // [STATE] Range updates quantize X to quarter-second steps, clamp the visible area, and rebuild the derived curve.
    void set_xy_range(float x, float y)
    {
        x = int(x / 0.25) * 0.25;
        if (x >= 0)
            visible_area.SetRight(x);
        if (y >= 0)
            visible_area.SetBottom(y);
        recalculate_line();
    }
    // [STATE] These accessors expose the derived summary state consumed by the owning dialog.
    float get_volume() const { return m_total_volume; }
    float get_time() const { return visible_area.m_width; }

    // [INTENT] Sampling and point extraction helpers expose the curve model for export or downstream UI.
    std::vector<float>                   get_ramming_speed(float sampling) const; // returns sampled ramming speed
    std::vector<std::pair<float, float>> get_buttons() const;                     // returns buttons position

    // [OPENGL] The implementation is immediate-mode painting through wxDC, not retained GPU geometry.
    // [UNITY] Port as a custom chart renderer that draws the curve, axes, and point labels from cached model data.
    void draw();

    // [EVENT] Mouse handlers implement hit-testing, drag initiation, drag updates, and point insertion/removal.
    // [PORTING_HAZARD:P2] The current design mixes input, curve mutation, and redraw triggers inside the widget.
    void mouse_clicked(wxMouseEvent& event);
    void mouse_right_button_clicked(wxMouseEvent& event);
    void mouse_moved(wxMouseEvent& event);
    void mouse_double_clicked(wxMouseEvent& event);
    void mouse_left_window(wxMouseEvent&)
    {
        m_dragged = nullptr;
        SetCursor(wxNullCursor);
    }
    void mouse_released(wxMouseEvent&)
    {
        m_dragged = nullptr;
        SetCursor(wxNullCursor);
    }
    void paint_event(wxPaintEvent&) { draw(); }
    DECLARE_EVENT_TABLE()

private:
    // [STATE] These toggles encode chart behavior assumptions: fixed horizontal axis, spline interpolation, and the
    // optional manual point-edit mode used by the interactive workflow.
    static const bool fixed_x                    = true;
    static const bool splines                    = true;
    static const bool manual_points_manipulation = false;
    int               side                       = 10; // side of draggable button

    const int scale_unit;
    int       legend_side;

    class ButtonToDrag
    {
    public:
        bool operator<(const ButtonToDrag& a) const { return m_pos.m_x < a.m_pos.m_x; }
        ButtonToDrag(wxPoint2DDouble pos) : m_pos{pos} {};
        wxPoint2DDouble get_pos() const { return m_pos; }
        void            move(double x, double y)
        {
            m_pos.m_x += x;
            m_pos.m_y += y;
        }

    private:
        wxPoint2DDouble m_pos; // position in math coordinates
    };

    // [STATE] Screen/math transforms keep all interaction and rendering aligned to the cached plot rectangle.
    // [UNITY] Preserve these as explicit coordinate-conversion helpers in the Unity controller to avoid mixing UI
    // pixels with chart-space data.
    wxPoint math_to_screen(const wxPoint2DDouble& math) const
    {
        wxPoint screen;
        screen.x = (math.m_x - visible_area.m_x) * (m_rect.GetWidth() / visible_area.m_width);
        screen.y = (math.m_y - visible_area.m_y) * (m_rect.GetHeight() / visible_area.m_height);
        screen.y *= -1;
        screen += m_rect.GetLeftBottom();
        return screen;
    }
    wxPoint2DDouble screen_to_math(const wxPoint& screen) const
    {
        wxPoint2DDouble math = screen;
        math -= m_rect.GetLeftBottom();
        math.m_y *= -1;
        math.m_x *= visible_area.m_width / m_rect.GetWidth(); // scales to [0;1]x[0,1]
        math.m_y *= visible_area.m_height / m_rect.GetHeight();
        return (math + visible_area.GetLeftTop());
    }

    // [STATE] Hit-testing is done against the cached plot rectangle and current point radius.
    int which_button_is_clicked(const wxPoint& point) const
    {
        if (!m_rect.Contains(point))
            return -1;
        for (unsigned int i = 0; i < m_buttons.size(); ++i) {
            wxRect rect(math_to_screen(m_buttons[i].get_pos()) - wxPoint(side / 2., side / 2.),
                        wxSize(side, side)); // bounding rectangle of this button
            if (rect.Contains(point))
                return i;
        }
        return (-1);
    }

    // [INTENT] Derived-data rebuilders synchronize the cached polyline, sampled volume, and parent notification.
    void recalculate_line();
    void recalculate_volume();

    // [STATE] The chart keeps both source control points and derived raster/sample caches in memory.
    wxRect                    m_rect; // rectangle on screen the chart is mapped into (screen coordinates)
    wxPoint                   m_previous_mouse;
    std::vector<ButtonToDrag> m_buttons;
    std::vector<int>          m_line_to_draw;
    wxRect2DDouble            visible_area;
    ButtonToDrag*             m_dragged      = nullptr;
    float                     m_total_volume = 0.f;
    bool                      m_uniform      = false;
};

#endif // RAMMING_CHART_H_
