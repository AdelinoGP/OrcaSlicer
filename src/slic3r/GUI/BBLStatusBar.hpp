#ifndef BBLSTATUSBAR_HPP
#define BBLSTATUSBAR_HPP

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <memory>
#include <string>
#include <functional>
#include <string>

#include "Jobs/ProgressIndicator.hpp"

class wxTimer;
class wxGauge;
class wxButton;
class wxTimerEvent;
class wxStatusBar;
class wxWindow;
class wxFrame;
class wxString;
class wxFont;

namespace Slic3r {

// [INTENT][STATE][UNITY] BBL-specific status area borrows `ProgressIndicator` hooks to show job progress, cancel affordance, and
// informational labels for a Bambu build page; Unity can mirror it as a VisualElement `Panel` with a `Slider`, `Label`, and `Button` trio
// bound through `MainThreadDispatcher` to keep the overlay in sync.
class BBLStatusBar : public ProgressIndicator
{
    wxPanel* m_self; // [STATE][PORTING_HAZARD:P3] Legacy Perl/Ux mix-in uses this panel as the real container instead of the base class;
                     // Unity should own a dedicated VisualElement container for the same content.
    wxGauge*      m_prog;              // [STATE] gauge widget representing numeric progress shared with the job worker thread.
    wxButton*     m_cancelbutton;      // [STATE][EVENT] cancel button wired to the callback that the kitten job sets.
    wxStaticText* m_status_text;       // [STATE] dynamic message shared with job updates and error dispatching.
    wxStaticText* m_object_info;       // [STATE] object slice info label cached for previews.
    wxStaticText* m_slice_info;        // [STATE] slice count label whose visibility toggles.
    wxBoxSizer*   m_slice_info_sizer;  // [STATE] layout owner that hides/shows slice info.
    wxBoxSizer*   m_object_info_sizer; // [STATE] layout owner for object info.
    wxBoxSizer*   m_sizer;             // [STATE] overall layout so panel matches the host status bar.
public:
    BBLStatusBar(wxWindow* parent = nullptr, int id = -1);
    ~BBLStatusBar() = default;

    // [STATE] Used by renderers seeking the last gauge position so the Unity slider can stay glued to the same value.
    int get_progress() const;
    // [EVENT][THREAD] Called by slicer jobs on the GUI thread to update the gauge (negative values show pulse) so Unity can dispatch via
    // `UnityMainThreadDispatcher` before mutating the slider.
    void set_progress(int) override;
    // [STATE] Exposes the gauge range for jobs that normalize progress; keeps the UI slider bounds stable.
    int get_range() const override;
    // [STATE] Adjusts the gauge maximum so the `set_progress` scale matches the background task.
    void set_range(int = 100) override;
    // [STATE] Clears percent text and hides gauge labels when no job is running; Unity equivalent hides the slider.
    void clear_percent() override;
    // [EVENT][PORTING_HAZARD:P2] Renders error info text spilled from worker threads; Unity should convert the payload to a toast before
    // resetting the gauge state.
    void show_error_info(wxString msg, int code, wxString description, wxString extra) override;
    // [STATE][EVENT] Shows or hides the progress bar, mirroring the worker's intent for `Start()`/`Stop()` flows.
    void show_progress(bool);
    // [STATE][EVENT] Starts the busy spinner/animation and sets `m_busy` so the UI forbids new jobs.
    void start_busy(int = 100);
    // [STATE][EVENT] Stops the busy mode so new operations may start again.
    void stop_busy();
    // [STATE] Reports whether the busy spinner is currently driving the UI.
    inline bool is_busy() const { return m_busy; }
    // [EVENT][THREAD][UNITY] Stores the cancel callback executed when the UI cancel button fires; Unity should keep a managed `Action` and
    // invoke it on the main thread.
    void        set_cancel_callback(CancelFn = CancelFn()) override;
    inline void reset_cancel_callback() { set_cancel_callback(); }
    // [STATE] Exposes the underlying panel so the BBL page can slot this widget into the layout; Unity should expose the `VisualElement`
    // for layout too.
    wxPanel* get_panel();
    // [STATE][UNITY] Updates the central status message displayed to users; Unity should bind a `Label.text` to this state.
    void set_status_text(const wxString& txt);
    void set_status_text(const std::string& txt);
    void set_status_text(const char* txt) override;
    // [STATE] Allows the controller to read back the last status message for logging or fallback hints.
    wxString get_status_text() const;
    // [STATE] Pushes a font change through the status text so Unity can sync fonts via `FontAsset` while respecting DPI scaling.
    void set_font(const wxFont& font);
    // [STATE][UNITY] Stores object topology info so the object label synchronizes across sessions; Unity can use secondary `Label` nodes.
    void set_object_info(const wxString& txt);
    // [STATE][UNITY] Stores slice count info for the dedicated label; Unity may bind a `Label` to this field.
    void set_slice_info(const wxString& txt);
    // [EVENT][STATE] Toggles the slice info label visibility so the BBL page can hide it when not needed; Unity should toggle
    // `VisualElement.visible`.
    void show_slice_info(bool show);
    // [STATE] Queries whether slice info is showing to keep layout decisions consistent with the worker state machine.
    bool is_slice_info_shown();

    // Temporary methods to satisfy Perl side
    // [EVENT][STATE] Forces the cancel button to show/hide for legacy workflows; Unity should enable/disable the `Button.interactable` flag.
    void show_cancel_button();
    void hide_cancel_button();

private:
    bool     m_busy = false; // [STATE] busy flag that gates `start_busy`/`stop_busy` toggles.
    CancelFn m_cancel_cb;    // [STATE][EVENT] Stored lambda executed when cancel button fires.
};

namespace GUI {
    using Slic3r::BBLStatusBar;
}

} // namespace Slic3r

#endif // BBLSTATUSBAR_HPP
