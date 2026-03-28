#include "AnimaController.hpp"

#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#ifdef __APPLE__
#include "libslic3r/MacUtils.hpp"
#endif

// [INTENT] Small animated status icon widget: it swaps between cached bitmap frames on a wxTimer and exposes a manual enable state.
// [STATE] Owns the frame cache, the enabled-state bitmap, the timer, and the current frame index; the control is fixed-size by
// construction. [UNITY] Map to a compact Image/RawImage controller with a coroutine or Update-driven sprite frame swap, not a full
// animation timeline. [PORTING_HAZARD:P3] The current implementation assumes a ready bitmap and a fixed 4-frame loop; Unity will need
// explicit null/length guards and a data-driven frame count.
AnimaIcon::AnimaIcon(wxWindow* parent, wxWindowID id, std::vector<std::string> img_list, std::string img_enable, int ivt)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize), m_ivt(ivt)
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    SetBackgroundColour((wxColour(255, 255, 255)));
    m_size = 25;

    // [STATE] Frame assets are pre-scaled up front so the widget can reuse them without recalculating size during timer ticks.
    for (const auto& filename : img_list)
        m_images.emplace_back(create_scaled_bitmap(filename, this, m_size));
    m_image_enable = create_scaled_bitmap(img_enable, this, m_size - 8);

    // [STATE] The first frame is the visible surface; later timer ticks only replace the bitmap payload.
    if (!m_images.empty())
        m_bitmap = new wxStaticBitmap(this, wxID_ANY, m_images[0], wxDefaultPosition, wxSize(FromDIP(m_size), FromDIP(m_size)));

    m_timer = new wxTimer();
    m_timer->SetOwner(this);

    // [EVENT] Timer ticks drive frame advancement on the UI thread; this is a lightweight state flip, not a worker-thread animation source.
    // [UNITY] Replace with a frame-advance callback on the main thread or a coroutine that yields for the configured interval.
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (m_timer->IsRunning() && !m_images.empty()) {
            m_current_frame = (m_current_frame + 1) % 4;
            m_bitmap->SetBitmap(m_images[m_current_frame]);
        }
    });

    // [EVENT] Mouse input is re-posted from the inner bitmap to the parent panel so callers can bind a single click target.
    // [PORTING_HAZARD:P2] This event rebroadcast relies on wx event bubbling semantics; Unity will need explicit hit-target forwarding.
    m_bitmap->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxMouseEvent evt(wxEVT_LEFT_DOWN);
        evt.SetEventObject(this);
        wxPostEvent(this, evt);
    });

    // [STATE] Cursor feedback is coupled to the timer state: hand cursor when idle, arrow while playing.
    m_bitmap->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        if (!m_timer->IsRunning())
            SetCursor(wxCursor(wxCURSOR_HAND));
        else
            SetCursor(wxCursor(wxCURSOR_ARROW));
        e.Skip();
    });
    m_bitmap->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        SetCursor(wxCursor(wxCURSOR_ARROW));
        e.Skip();
    });
    sizer->Add(m_bitmap, 0, wxALIGN_CENTER, 0);
    SetSizer(sizer);
    SetSize(wxSize(FromDIP(m_size), FromDIP(m_size)));
    SetMaxSize(wxSize(FromDIP(m_size), FromDIP(m_size)));
    SetMinSize(wxSize(FromDIP(m_size), FromDIP(m_size)));
    Layout();
    Fit();
}

// [INTENT] Stop and release the timer before destruction so the widget does not keep firing after the panel is torn down.
// [THREAD] The timer is UI-thread owned; explicit stop/delete keeps shutdown deterministic in wxWidgets.
AnimaIcon::~AnimaIcon()
{
    if (m_timer) {
        m_timer->Stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

// [INTENT] Start the frame loop using the configured interval.
// [UNCLEAR] The unconditional branch suggests this was once feature-gated; the current hypothesis is that playback should always start when called.
void AnimaIcon::Play()
{
    if (true)
        m_timer->Start(m_ivt);
}

// [INTENT] Pause the loop without clearing the current frame, so Enable() can still swap in the steady-state icon.
void AnimaIcon::Stop() { m_timer->Stop(); }

// [INTENT] Replace the animated frame with the enabled-state bitmap once the control is no longer in motion.
// [UNITY] This is a sprite-state swap, so the equivalent is a separate idle sprite or icon variant on the same view.
void AnimaIcon::Enable()
{
    if (m_bitmap) {
        m_bitmap->SetBitmap(m_image_enable);
    }
}

// [STATE] Running state is delegated to wxTimer, which makes the timer the authoritative playback flag.
bool AnimaIcon::IsRunning() const { return m_timer ? m_timer->IsRunning() : false; }
