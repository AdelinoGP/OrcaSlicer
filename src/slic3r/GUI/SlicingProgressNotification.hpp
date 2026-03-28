#ifndef slic3r_GUI_SlicingProgressNotification_hpp_
#define slic3r_GUI_SlicingProgressNotification_hpp_

#include "DailyTips.hpp"
#include "NotificationManager.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] UI-thread-owned progress notification for the slicing workflow: it drives a small
// state machine, swaps between progress/cancelled/completed presentations, and embeds the
// DailyTips panel directly inside the notification chrome.
// [STATE] The object owns transient notification state (percentage, sidebar-collapsed fading,
// export availability, print-info text, and the cancel callback) plus the nested DailyTipsPanel.
// [EVENT] Button handlers and external setters all feed the same state machine; the manager calls
// update/render from the notification lifecycle rather than from an independent widget tree.
// [THREAD] The notification is UI-thread-owned; the cancel callback and late-state updates must
// re-enter through the notification manager rather than mutate rendering state from workers.
// [UNITY] Port as a screen-space HUD controller with explicit notification states and a reusable
// child panel for DailyTips instead of a custom-painted native notification window.
// [PORTING_HAZARD:P2] Rendering, lifecycle, and user actions are coupled in one overlay class, and
// the fade rules depend on sidebar visibility, so the Unity port should preserve those transitions
// in a retained controller rather than splitting them across independent views.
class NotificationManager::SlicingProgressNotification : public NotificationManager::PopNotification
{
public:
    // Inner state of notification, Each state changes bahaviour of the notification
    enum class SlicingProgressState {
        SP_NO_SLICING, // hidden
        SP_BEGAN,      // still hidden but allows to go to SP_PROGRESS state. This prevents showing progress after slicing was canceled.
        SP_PROGRESS,   // never fades outs, no close button, has cancel button
        SP_CANCELLED,  // fades after 10 seconds, simple message
        // SP_BEFORE_COMPLETED,  // to keep displaying DailyTips for 3 seconds
        SP_COMPLETED // Has export hyperlink and print info, fades after 20 sec if sidebar is shown, otherwise no fade out
    };
    SlicingProgressNotification(const NotificationData& n,
                                NotificationIDProvider& id_provider,
                                wxEvtHandler*           evt_handler,
                                std::function<bool()>   callback)
        : PopNotification(n, id_provider, evt_handler)
        , m_cancel_callback(callback)
        , m_dailytips_panel(new DailyTipsPanel(true, DailyTipsLayout::Vertical))
    { set_progress_state(SlicingProgressState::SP_NO_SLICING); }
    void                 set_percentage(float percent) { m_percentage = percent; }
    DailyTipsPanel*      get_dailytips_panel() { return m_dailytips_panel; }
    SlicingProgressState get_progress_state() { return m_sp_state; }
    // sets text of notification - call after setting progress state
    void set_status_text(const std::string& text);
    // sets cancel button callback
    void set_cancel_callback(std::function<bool()> callback) { m_cancel_callback = callback; }
    bool has_cancel_callback() const { return m_cancel_callback != nullptr; }
    // sets SlicingProgressState, negative percent means canceled, returns true if state was set succesfully.
    bool set_progress_state(float percent);
    // sets SlicingProgressState, percent is used only at progress state. Returns true if state was set succesfully.
    bool set_progress_state(SlicingProgressState state, float percent = 0.f);
    // sets additional string of print info and puts notification into Completed state.
    void set_print_info(const std::string& info);
    // sets fading if in Completed state.
    void set_sidebar_collapsed(bool collapsed);
    // Calls inherited update_state and ensures Estate goes to hidden not closing.
    bool update_state(bool paused, const int64_t delta) override;
    // Switch between technology to provide correct text.
    void set_fff(bool b) { m_is_fff = b; }
    void set_export_possible(bool b) { m_export_possible = b; }
    void on_change_color_mode(bool is_dark) override;

protected:
    void init() override;
    void render(GLCanvas3D& canvas, float initial_y, bool move_from_overlay, float overlay_width, float right_margin) override;
    /* PARAMS: pos is relative to screen */
    void render_text(const ImVec2& pos);
    void render_bar(const ImVec2& pos, const ImVec2& size);
    void render_cancel_button(const ImVec2& pos, const ImVec2& size);
    void render_close_button(const ImVec2& pos, const ImVec2& size);
    void render_dailytips_panel(const ImVec2& pos, const ImVec2& size);
    void render_show_dailytips(const ImVec2& pos);

    void on_show_dailytips();
    void on_cancel_button();
    int  get_duration() override;

protected:
    ImVec2  m_window_pos;
    float   m_percentage{0.0f};
    int64_t m_before_complete_start;
    // [STATE] Cancel callback is optional and may veto duplicate cancellation attempts.
    std::function<bool()> m_cancel_callback;
    // [STATE] This enum is the notification's primary mode switch.
    SlicingProgressState m_sp_state{SlicingProgressState::SP_PROGRESS};
    // [STATE] Sidebar visibility changes the completion fade behavior.
    bool m_sidebar_collapsed{false};
    // [STATE] When true, the progress view can surface an export link before completion.
    bool m_export_possible{false};
    // [STATE] Embedded child panel that keeps the DailyTips content alive inside the overlay.
    DailyTipsPanel* m_dailytips_panel{nullptr};

    /* currently not used */
    bool        m_has_print_info{false};
    std::string m_print_info;
    bool        m_is_fff{true};
};

}} // namespace Slic3r::GUI

#endif
