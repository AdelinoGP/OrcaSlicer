#ifndef slic3r_GUI_AnimaController_hpp_
#define slic3r_GUI_AnimaController_hpp_

#include "../wxExtensions.hpp"
#include "Label.hpp"

// [INTENT] Compact animated status icon: the widget owns a tiny frame cache and exposes a simple play/stop/enable surface instead of a full
// animation timeline. [STATE] m_images caches the playback frames, m_image_enable is the steady-state icon, and
// m_current_frame/m_ivt/m_size define the fixed playback contract. [EVENT] The cpp binds timer and mouse events; this header marks the
// class as a command-driven surface rather than a passive bitmap container. [THREAD] Playback is UI-thread only through wxTimer ownership,
// so destruction and state changes must stay on the main thread. [UNITY] Map to a small Image/RawImage controller with a sprite swap or
// coroutine-driven frame advance, keeping click forwarding on the UI event bridge. [PORTING_HAZARD:P3] The implementation assumes a ready
// bitmap set and a fixed frame loop; Unity needs explicit empty-list guards and a data-driven frame count.
class AnimaIcon : public wxPanel
{
public:
    AnimaIcon(wxWindow* parent, wxWindowID id, std::vector<std::string> img_list, std::string img_enable, int ivt = 1000);
    ~AnimaIcon();

    void Play();
    void Stop();
    void Enable();
    bool IsPlaying() const { return IsRunning(); };
    bool IsRunning() const;

private:
    wxBitmap              m_image_enable;
    wxStaticBitmap*       m_bitmap{nullptr};
    std::vector<wxBitmap> m_images;
    wxTimer*              m_timer;
    int                   m_current_frame = 0;
    int                   m_ivt;
    int                   m_size;
};

#endif // !slic3r_GUI_AnimaController_hpp_
