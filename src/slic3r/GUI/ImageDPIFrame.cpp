#include "ImageDPIFrame.hpp"

#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/dcmemory.h>
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "PartPlate.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include <chrono>
#include "wxExtensions.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace Slic3r { namespace GUI {
// [STATE] polling cadence for the overlay timer so the frame remains responsive but does not hog UI events.
#define ANIMATION_REFRESH_INTERVAL 20
// [INTENT] Build a lightweight DPI preview overlay that tracks the cursor and provides contextual imagery to the main frame.
ImageDPIFrame::ImageDPIFrame()
    : DPIFrame(static_cast<wxWindow*>(wxGetApp().mainframe),
               wxID_ANY,
               "",
               wxDefaultPosition,
               wxDefaultSize,
               !wxCAPTION | !wxCLOSE_BOX | wxBORDER_NONE)
{
    m_image_px = 240;
    // [STATE] Keep the overlay bitmap at a fixed pixel count so the preview stays square across DPI shifts.
    int width = 270;
    // SetTransparent(0);
    SetMinSize(wxSize(FromDIP(width), -1));
    SetMaxSize(wxSize(FromDIP(width), -1));
    // [STATE] Locking the width avoids jitter when other windows resize; only the bitmap panel can grow/shrink.
    SetBackgroundColour(wxColour(255, 255, 255, 255));
#ifdef __APPLE__
    SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
#endif

    // [EVENT] Draw a custom border because the overlay lacks native chrome; keep this fast so the timer thread stays on the UI queue.
    // ORCA add border
    Bind(wxEVT_PAINT, [this](wxPaintEvent& evt) {
        wxPaintDC dc(this);
        dc.SetPen(StateColor::darkModeColorFor(wxColour("#DBDBDB")));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
    });

    m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_title = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_title->SetFont(Label::Head_14);
    m_title->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#262E30")));
    m_title->SetMaxSize(wxSize(FromDIP(width), -1));

    auto image_sizer = new wxBoxSizer(wxVERTICAL);
    auto imgsize     = FromDIP(width);
    // [UNITY] This bitmap acts like a Texture2D sprite in Unity; replace it with a UI Toolkit VisualElement that swaps Texture2Ds loaded
    // via UnityWebRequest + Graphics.Blit.
    m_bitmap = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("printer_preview_C13", this, m_image_px), wxDefaultPosition,
                                  FromDIP(wxSize(m_image_px, m_image_px)), 0);
    image_sizer->Add(m_bitmap, 0, wxALIGN_CENTER | wxALL, FromDIP(10));
    m_sizer_main->Add(m_title, 0, wxALIGN_CENTER | wxTOP | wxLEFT | wxRIGHT, FromDIP(10));
    m_sizer_main->Add(image_sizer, FromDIP(0), wxALIGN_CENTER, FromDIP(0));

    wxGetApp().UpdateDarkUI(this); // ORCA fix white bg on dark mode

    // [EVENT] Closing the frame simply triggers hide so the cached bitmap survives until the timer destroys it.
    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) { on_hide(); });
    SetSizer(m_sizer_main);
    Layout();
    Fit();
    init_timer();
}

ImageDPIFrame::~ImageDPIFrame()
{
    // [STATE] Base DPIFrame handles the timer/child destruction, so no extra cleanup is necessary; Unity would stop its coroutine via OnDisable.
}

bool ImageDPIFrame::Show(bool show)
{
    Layout();
    return DPIFrame::Show(show);
}

void ImageDPIFrame::set_bitmap(const wxBitmap& bit_map)
{
    if (&bit_map && bit_map.IsOk()) {
        // [STATE] Refresh the preview texture while preserving the sizer layout so the overlay remains steady.
        m_bitmap->SetBitmap(bit_map);
        // [UNITY] Mirror this by uploading a Texture2D and assigning it to a VisualElement Image/RawImage before calling SetTexture.
    }
}

void ImageDPIFrame::set_title(const wxString& title)
{
    // [STATE] Titles can be toggled without re-creating controls because the overlay often reuses the same frame for multiple hints.
    m_title->Show(!title.empty());
    if (!title.empty())
        m_title->SetLabel(title);
    Layout();
}

void ImageDPIFrame::on_dpi_changed(const wxRect& suggested_rect)
{
    // [UNCLEAR] DPI change hook currently stubs out rescaling; the intent is to refresh the overlay when monitor scaling shifts.
    // m_image->Rescale();
    // m_bitmap->Rescale();
    // [UNITY] Unity would recompute RectTransform scaling via a CanvasScaler/Display listener since wxRect hints are unavailable.
}

void ImageDPIFrame::sys_color_changed()
{
    // [EVENT] Theme refresh events reroute to the app-level helper so the overlay respects dark-mode palettes.
    wxGetApp().UpdateDarkUI(this);
}

void ImageDPIFrame::init_timer()
{
    // [INTENT][THREAD] Create a main-thread timer so the overlay can fade in/out based on mouse proximity without blocking the render loop.
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    Bind(wxEVT_TIMER, &ImageDPIFrame::on_timer, this);
}

void ImageDPIFrame::on_timer(wxTimerEvent& event)
{
    // [EVENT] Periodic timer keeps the overlay queued with mouse movement updates so the hint hides automatically when the cursor drifts.
    if (!IsShown()) {              // after 1s  to show Frame
        if (m_timer_count >= 20) { // ORCA show frame faster to maatch time with tooltips
            Show();
            Raise();
        }
        m_timer_count++;
    } else {
        // [PORTING_HAZARD:P2] Polling wxGetMousePosition directly is unsafe for background threads; Unity will need to marshal its
        // Input.mousePosition via MainThreadDispatcher.
        wxPoint mouse_pos   = wxGetMousePosition();
        wxRect  window_rect = GetScreenRect();

        wxPoint center(window_rect.x + window_rect.width / 2, window_rect.y + window_rect.height / 2);
        int     half_width  = window_rect.width / 2;
        int     half_height = window_rect.height / 2;
        wxRect  expanded_rect(center.x - half_width * 2, center.y - half_height * 2, window_rect.width * 2, window_rect.height * 2);
        // [UNITY] Mimic this logic with a UI Toolkit Panel that checks Input.mousePosition inside a Coroutine running on the main thread.

        if (!expanded_rect.Contains(mouse_pos)) {
            on_hide();
        }
    }
}

void ImageDPIFrame::on_show()
{
    // [STATE] Reset and restart the timer so the overlay always shows for the configured delay before hiding again.
    if (IsShown()) {
        on_hide();
    }
    if (m_refresh_timer) {
        m_timer_count = 0;
        m_refresh_timer->Start(ANIMATION_REFRESH_INTERVAL);
        // [UNITY] Start a coroutine/InvokeRepeating that checks Input.mousePosition and accumulates delta-time until the overlay expires.
    }
}

void ImageDPIFrame::on_hide()
{
    // [STATE] Stop the timer and hide the overlay, then push focus back to the main frame so tooltips do not steal input.
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
    }
    if (IsShown()) {
        Hide();
        if (wxGetApp().mainframe != nullptr) {
            // [PORTING_HAZARD:P3] Unity does not expose wxGetApp, so porters must keep a singleton MainFrameController reference to manage
            // focus swaps.
            wxGetApp().mainframe->Show();
            wxGetApp().mainframe->Raise();
        }
    }
} // namespace GUI
} // namespace Slic3r
