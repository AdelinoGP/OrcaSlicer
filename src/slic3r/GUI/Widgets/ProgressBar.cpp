#include "ProgressBar.hpp"
#include "../I18N.hpp"
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include "Label.hpp"

wxDEFINE_EVENT(wxCUSTOMEVT_SET_TEMP_FINISH, wxCommandEvent);
BEGIN_EVENT_TABLE(ProgressBar, wxWindow)
EVT_PAINT(ProgressBar::paintEvent)
END_EVENT_TABLE()

// [INTENT] This widget is a retained, custom-painted progress bar with optional percentage text and a latched disable message.
// [STATE] `m_step`, `m_max`, `m_disable`, and the palette/radius caches drive both the fill geometry and the overlay label.
// [UNITY] Port this as a retained fill-bar prefab with a separate centered text element and a view-model for disabled status.
// [PORTING_HAZARD:P2] Rendering and state mutation are intertwined in the paint path, so Unity should separate value updates from draw-time
// clamping.
ProgressBar::ProgressBar(wxWindow* parent, wxWindowID id, int max, const wxPoint& pos, const wxSize& size, bool shown)
{
    m_shownumber = shown;
    SetBackgroundColour(wxColour(255, 255, 255));

    if (size.y >= miniHeight) {
        m_miniHeight = size.y;
    } else {
        m_miniHeight = miniHeight;
    }

    m_max    = max;
    m_radius = m_miniHeight / 2;
    wxSize temp_size(size.x, m_miniHeight);

    SetFont(Label::Head_12);
    create(parent, id, pos, temp_size);
}

ProgressBar::~ProgressBar() {}

void ProgressBar::create(wxWindow* parent, wxWindowID id, const wxPoint& pos, wxSize& size)
{
    wxWindow::Create(parent, id, pos, size);
    // [INTENT] The native window creation is the only live part of this helper; the commented sizer experiment is dead layout history.
    // [STATE] The widget keeps its own fixed height and rounded-corner radius instead of participating in a complex child hierarchy.
    // m_static_info = new wxStaticText(this, wxID_ANY,wxT(""),wxPoint(this->padding, 20), wxSize(GetSize().GetWidth() - this->padding * 3,
    // -1), wxST_ELLIPSIZE_END); m_static_info->Wrap(-1);

    /* wxBoxSizer *m_sizer_body  = new wxBoxSizer(wxHORIZONTAL);

      auto m_progress_bk = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
      m_progress_bk->SetBackgroundColour(wxColour(238, 130, 238));
      StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38,
      166, 154), StateColor::Hovered), std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));

      wxBoxSizer *m_sizer_progress= new wxBoxSizer(wxHORIZONTAL);

      auto m_progress = new wxPanel(m_progress_bk, wxID_ANY, wxDefaultPosition, wxSize(50, -1), wxTAB_TRAVERSAL);
      m_progress->SetBackgroundColour(wxColour(128, 0, 255));

      m_sizer_progress->Add(m_progress, 0, wxEXPAND, 0);

      m_progress_bk->SetSizer(m_sizer_progress);
      m_progress_bk->Layout();
      m_sizer_progress->Fit(m_progress_bk);
      m_sizer_body->Add(m_progress_bk, 1, wxEXPAND, 0);

      this->SetSizer(m_sizer_body);
      this->Layout();*/
}

void ProgressBar::SetRadius(double radius)
{
    m_radius = radius;
    Refresh();
}

void ProgressBar::SetProgressForedColour(wxColour colour)
{
    m_progress_background_colour = colour;
    Refresh();
}

void ProgressBar::SetProgressBackgroundColour(wxColour colour)
{
    m_progress_colour = colour;
    Refresh();
}

void ProgressBar::Rescale() { ; }

void ProgressBar::ShowNumber(bool shown)
{
    m_shownumber = shown;
    Refresh();
}

// [STATE] `Disable()` latches an alternate message and color until the next explicit progress update clears the disabled state.
// [PORTING_HAZARD:P3] The disable flag is implicitly reset by unrelated setters, so callers depend on this widget's side effects.
void ProgressBar::Disable(wxString text)
{
    if (m_disable)
        return;
    m_disable_text = text;
    m_disable      = true;
    Refresh();
}

void ProgressBar::SetValue(int step)
{
    m_disable = false;
    SetProgress(step);
}

void ProgressBar::Reset()
{
    m_step = 0;
    SetValue(0);
}

void ProgressBar::SetProgress(int step)
{
    if (step < 0)
        return;
    if (m_disable == false && m_step == step) {
        return;
    }

    m_disable = false;
    m_step    = step;
    Refresh();
}

void ProgressBar::SetMinSize(const wxSize& size)
{
    // [STATE] Height changes also re-derive the rounded-corner radius, so the fill geometry follows the widget's minimum vertical budget.
    if (size.y >= miniHeight) {
        m_miniHeight = size.y;
    } else {
        return;
    }

    m_radius = m_miniHeight / 2.4;
    wxWindow::SetMinSize({size.x, m_miniHeight});
    // SetSize(size);
    SetRadius(m_radius);
}

void ProgressBar::paintEvent(wxPaintEvent& evt)
{
    // [EVENT] Paints are routed through wxPaintDC; all visual state is recomputed from the cached members on demand.
    wxPaintDC dc(this);
    render(dc);
}

// [INTENT] Rendering is a two-stage draw: optional Windows buffering, then the actual bar fill and centered text.
// [UNITY] Replace this with a prefab that uses a clipped fill image plus a dedicated label so layout is not derived during paint.
void ProgressBar::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void ProgressBar::doRender(wxDC& dc)
{
    if (m_step >= m_max)
        m_step = m_max;
    wxSize size = GetSize();
    // [STATE] The full-width background, the clipped fill width, and the optional overlay all depend on the cached progress ratio.
    dc.SetPen(wxPen(m_progress_background_colour, 1));
    dc.SetBrush(wxBrush(m_progress_background_colour));
    if (m_radius == 0) {
        dc.DrawRectangle(0, 0, size.x, size.y);
    } else {
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, m_radius);
    }

    // draw progress
    if (m_disable) {
        m_proportion = float(size.x * float(this->m_step) / float(this->m_max));
        if (m_proportion < m_radius * 2 && m_proportion != 0) {
            m_proportion = m_radius * 2;
        }

        dc.SetPen(wxPen(m_progress_colour_disable, 1));
        dc.SetBrush(wxBrush(m_progress_colour_disable));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, m_proportion, size.y);
        } else {
            dc.DrawRoundedRectangle(0, 0, m_proportion, size.y, m_radius);
        }

        dc.SetFont(::Label::Head_12);
        auto textSize = dc.GetMultiLineTextExtent(m_disable_text);
        dc.SetTextForeground(wxColour(144, 144, 144));
        auto pt = wxPoint();
        pt.x    = (size.x - textSize.x) / 2;
        pt.y    = (size.y - textSize.y) / 2;
        dc.DrawText(m_disable_text, pt);

    } else {
        m_proportion = float(size.x * float(this->m_step) / float(this->m_max));
        if (m_proportion < m_radius * 2 && m_proportion != 0) {
            m_proportion = m_radius * 2;
        }

        dc.SetPen(wxPen(m_progress_colour, 1));
        dc.SetBrush(wxBrush(m_progress_colour));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, m_proportion, size.y);
        } else {
            dc.DrawRoundedRectangle(0, 0, m_proportion, size.y, m_radius);
        }

        dc.SetFont(GetFont());
        auto textSize = dc.GetMultiLineTextExtent(wxString("000%"));
        dc.SetTextForeground(wxColour(144, 144, 144));
        auto pt = wxPoint();
        pt.x    = (size.x - textSize.x) / 2;
        pt.y    = (size.y - textSize.y) / 2;

        auto text = wxString("");
        if (m_step < 10) {
            text = wxString::Format("%d", m_step);
        } else {
            text = wxString::Format("%d", m_step);
        }

        if (m_shownumber) {
            dc.DrawText(text + wxString("%"), pt);
        }
    }
}

void ProgressBar::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    // [INTENT] Native size changes are intentionally delegated to wxWindow; the widget itself only reacts through its cached radius/height state.
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}
