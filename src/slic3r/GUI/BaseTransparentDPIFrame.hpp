#ifndef _BaseTransparentDPIFrame_H_
#define _BaseTransparentDPIFrame_H_

#include <future>
#include <thread>
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"

class Button;
class CheckBox;
namespace Slic3r { namespace GUI {
class CapsuleButton;

// [INTENT] Base class for semi-transparent overlay frames with optional timed disappearance and movement animations.
// [UNITY] Maps to a UI Overlay system. Can be implemented as a MonoBehaviour with a CanvasGroup for transparency
//         and a simple tweening system (like LeanTween, DOTween, or Unity's Built-in Animation system) for transitions.
class BaseTransparentDPIFrame : public Slic3r::GUI::DPIFrame
{
public:
    enum DisappearanceMode {
        None,
        TimedDisappearance // unit: second
    };

    BaseTransparentDPIFrame(wxWindow*         parent,
                            int               win_width,
                            wxPoint           dialog_pos,
                            int               ok_button_width,
                            wxString          win_text,
                            wxString          ok_text,
                            wxString          cancel_text        = "",
                            DisappearanceMode disappearance_mode = DisappearanceMode::None);
    ~BaseTransparentDPIFrame() override;
    void on_dpi_changed(const wxRect& suggested_rect) override;
    // [EVENT] Primary show hook invoked by parent frames to refresh animation state and restart the fade timer.
    void on_show();
    // [EVENT] Complementary hide hook that collapses the animation state and pauses the timer-driven transitions.
    void on_hide();
    // [STATE] Resets the fade counter so repeated exposures keep the overlay visible for the configured duration.
    void clear_timer_count();
    // [EVENT] Overrides wxWindow::Show to gate the semi-transparent overlay and keep timers in sync with visibility.
    bool Show(bool show = true) override;
    // [EVENT] Full-screen notification resets layout so the overlay stays centered despite DPI changes.
    void on_full_screen(IntEvent&);
    // [INTENT] Hook for subclasses to commit the primary confirmation action while closing the overlay.
    virtual void deal_ok();
    // [INTENT] Hook for subclasses to handle cancellation while ensuring decorations hide cleanly.
    virtual void deal_cancel();
    // [EVENT] Handles animation steps triggered by the timer.
    virtual void on_timer(wxTimerEvent& event);
    // [INTENT] Move the overlay toward a target position before fading, mirroring common tooltip motion patterns.
    void set_target_pos_and_gradual_disappearance(wxPoint pos);
    // [UNITY] Translate to animating a RectTransform's anchored position before driving a CanvasGroup fade via coroutine.
    // [EVENT] Starts the timed disappearance either after idle time or an explicit mouse-out.
    void call_start_gradual_disappearance();
    // [INTENT] Forces the overlay back to its initial state so it can be reused without reconstructing the widget.
    void restart();

protected:
    // [STATE] Accept and cancel buttons that surface user intent for the overlay action.
    Button* m_button_ok = nullptr;
    // [STATE] Cancel button used when the overlay needs to be dismissed without committing changes.
    Button* m_button_cancel = nullptr;
    // [STATE] Transient hint label rendered inside the overlay, usually showing the action description.
    Label*            m_finish_text = nullptr;
    DisappearanceMode m_timed_disappearance_mode;
    // [STATE] Tracks the progress of the self-closing animation.
    float m_timer_count = 0;
    // [THREAD] UI timer for animations. Unity equivalent would be a Coroutine or Update loop logic.
    wxTimer* m_refresh_timer{nullptr};
    // [PORTING_HAZARD:P3] Unity needs to replace this wxTimer with an Update-driven coroutine because there is no direct equivalent.
    int m_disappearance_second = 2500; // ANIMATION_REFRESH_INTERVAL 20  unit ms: m_disappearance_second * ANIMATION_REFRESH_INTERVAL
    // [STATE] Flags whether the overlay should move toward a target before applying the fade-out.
    bool m_move_to_target_gradual_disappearance = false;
    // [STATE] Destination used when the overlay translates before disappearing.
    wxPoint m_target_pos;

private:
    // [STATE] Sizer owning the layout of the overlay so size transitions stay smooth.
    wxBoxSizer* m_sizer_main{nullptr};
    // [PORTING_HAZARD:P3] Unity will need to express this layout via RectTransform + LayoutElement instead of wxSizer.
    // [STATE] Maximum allowed size for the overlay, used when animating scale changes.
    wxSize m_max_size;
    // [STATE] Per-tick increment for pseudo-scaling during the animation.
    wxSize m_step_size;
    // [STATE] Current offset applied every animation tick when moving toward the target.
    wxPoint m_step_pos;
    // [STATE] Starting position captured when the overlay was made visible.
    wxPoint m_start_pos;
    // [STATE] Duration of the move animation, measured in timer ticks.
    float m_time_move{6.0f};
    // [STATE] Duration governing gradual scaling/fade transitions.
    float m_time_gradual_and_scale{100.0f};
    // [STATE] Initial alpha used when the overlay is shown to provide a soft-start appearance.
    int m_init_transparent{220};
    // [STATE] Transparency delta applied per animation tick.
    int m_step_transparent;
    // [STATE] Current stage of the animation (0: Normal, 1: Gradual disappearance).
    int m_display_stage = 0; // 0 normal //1 gradual
    // [STATE] Tracks whether the overlay has a valid entry point to show again without a full rebuild.
    bool m_enter_window_valid{true};

private:
    // [EVENT] Sets up the gradual disappearance path, invoked once per timer cycle when the overlay is idle.
    void start_gradual_disappearance();
    // [EVENT] Connects the underlying wxTimer so animation ticks fire on the UI thread.
    void init_timer();
    // [STATE] Calculates the transparency step count whenever the overlay size or animation duration changes.
    void calc_step_transparent();
    // [EVENT] Ensures closing the overlay cleans up timers and layout associations.
    void on_close();
    // [EVENT] Toggles the sizer visibility while keeping the layout hierarchy intact.
    void show_sizer(wxSizer* sizer, bool show);
    // [EVENT] Hides all child widgets when entering the fade stage.
    void hide_all();
    // [EVENT] Invoked when the timer hits the fade threshold to move into gradual disappearance.
    void begin_gradual_disappearance();
    // [EVENT] Combines translation and fade when the overlay is tasked with moving before hiding.
    void begin_move_to_target_and_gradual_disappearance();
};
}} // namespace Slic3r::GUI
#endif // _STEP_MESH_DIALOG_H_
