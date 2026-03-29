#ifndef slic3r_GUI_ProgressBar_hpp_
#define slic3r_GUI_ProgressBar_hpp_

#include <wx/window.h>
#include "../wxExtensions.hpp"

class ProgressBar : public wxWindow
{
public:
    // [INTENT] Declaration boundary for a custom-painted progress bar with optional numeric overlay and a latched disabled-message mode.
    // [STATE] Cached step/max/radius/proportion values drive the fill geometry, while palette fields and `m_disable_text` control the overlay.
    // [UNITY] Port this as a retained fill-bar prefab with a separate label and a view-model for disabled presentation state.
    // [PORTING_HAZARD:P2] Geometry is derived from widget size and paint-time state, so Unity should separate value updates from draw/layout.
    ProgressBar();
    ProgressBar(wxWindow*      parent,
                wxWindowID     id    = wxID_ANY,
                int            max   = 100,
                const wxPoint& pos   = wxDefaultPosition,
                const wxSize&  size  = wxDefaultSize,
                bool           shown = false);

    void create(wxWindow* parent, wxWindowID id, const wxPoint& pos, wxSize& size);

    ~ProgressBar();

public:
    // [STATE] Overlay and progress-value caches consumed by the paint path.
    bool      m_shownumber = {false};
    int       m_disable    = {false};
    int       m_max        = {100};
    int       m_step       = {0};
    int       m_miniHeight = {0};
    const int miniHeight   = {14};
    double    m_radius     = {7};
    double    m_proportion = {0};
    // [STATE] Color palette for the background, enabled fill, and disabled fill variants.
    wxColour m_progress_background_colour = {233, 233, 233};
    wxColour m_progress_colour            = {0, 150, 136};
    wxColour m_progress_colour_disable    = {255, 111, 0};
    // [STATE] Latched alternate message shown while the bar is disabled.
    wxString m_disable_text;

public:
    // [EVENT] Public mutators refresh the control immediately; `SetValue()` also clears the latched disable mode.
    void ShowNumber(bool shown);
    void Disable(wxString text);
    void SetValue(int step);
    void Reset();
    void SetProgress(int step);
    void SetRadius(double radius);
    void SetProgressForedColour(wxColour colour);
    void SetProgressBackgroundColour(wxColour colour);
    void Rescale();
    // [STATE] Height changes also re-derive radius so rounded corners stay proportional to the minimum vertical budget.
    void SetHeight(int height)
    {
        m_minHeight = height;
        m_radius    = m_minHeight / 2;
        SetSize(GetSize().x, height);
    }
    virtual void SetMinSize(const wxSize& size) override;

protected:
    // [EVENT] Paints are routed through wxPaintEvent and re-rendered from cached members on demand.
    void paintEvent(wxPaintEvent& evt);
    // [INTENT] Rendering is split into a buffered wrapper and the actual fill/text draw routine.
    void render(wxDC& dc);
    void doRender(wxDC& dc);
    // [INTENT] Native size changes are delegated to wxWindow; the widget only updates its own cached geometry.
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_ProgressBar_hpp_
