#ifndef slic3r_PublishDialog_hpp_
#define slic3r_PublishDialog_hpp_

#include "I18N.hpp"

#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/dialog.h>
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "Widgets/StepCtrl.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ProgressBar.hpp"

// [INTENT] PublishDialog orchestrates the publish workflow UI: slicing, packing, uploading, and the
// [INTENT] final handoff to the publish web page.
// [STATE] It caches the visible step rail, progress text, error text, cancel button, and a non-owning
// [STATE] Plater* back-pointer so status updates can be forwarded to the publish pipeline.
// [EVENT] The public methods mirror publish-phase transitions, progress callbacks, and cancel/close
// [EVENT] requests coming from the surrounding modal workflow.
// [UNITY] This should become a modal controller plus progress overlay/stepper bound to an async publish
// [UNITY] service, with UI updates marshaled onto the main thread.
// [PORTING_HAZARD:P2] The wx version assumes the dialog can remain alive while queued publish events
// [PORTING_HAZARD:P2] and reentrant UI updates are in flight, so ownership and lifetime must be explicit.
namespace Slic3r { namespace GUI {

// [STATE] PublishStep is the user-visible finite-state model for the publish wizard and also drives the
// [STATE] fallback progress percentages shown when the backend does not report exact completion.
enum PublishStep {
    STEP_SLICING = 0,
    STEP_PACKING,
    STEP_UPLOADING,
    STEP_FILL_INFO,
    STEP_PUBLISH_COUNT,
};

class PublishDialog : public DPIDialog
{
public:
    PublishDialog(Plater* plater = nullptr);

    // [THREAD] These callbacks are expected to run on the UI thread; they directly mutate widgets and
    // [THREAD] optionally yield back to wx while a long-running publish operation is active.
    bool UpdateStatus(wxString& msg, int percent = -1, bool yeild = true);
    void Pulse(wxString& msg, bool& skip);
    // [STATE] Step changes update the step rail, text, and heuristic progress milestones for each phase.
    void SetPublishStep(PublishStep step, bool yeild = false, int percent = -1);
    // [EVENT] Kicks off publishing by resetting the visible state and queueing EVT_PUBLISH into Plater.
    void start_slicing();
    // [STATE] Reset returns the dialog to a reusable publish-ready state.
    void reset();
    bool was_cancelled() { return m_was_cancelled; }
    // [EVENT] Cancellation latches local state and routes through the close path so the owner can stop
    // [EVENT] the publish pipeline through the same event channel as window dismissal.
    void cancel();

protected:
    // [STATE] Left-side container for the step rail and its background styling.
    wxPanel* m_step_panel;
    // [STATE] Visual stepper that reflects the current publish phase.
    ::StepIndicator* m_publish_steps;
    // [STATE] Static helper text describing the operation.
    wxStaticText* m_text_note;
    // [STATE] Live status line for progress and phase messages.
    wxStaticText* m_text_progress;
    // [STATE] Primary progress indicator for determinate and heuristic completion.
    ProgressBar* m_progress;
    // [STATE] Cancel affordance shown while the publish workflow is active.
    Button* m_btn_cancel;
    // [STATE] Error/status footer used to surface validation or publish failures.
    wxStaticText* m_text_errors;
    // [STATE] Non-owning bridge back to the plater; lifetime must exceed queued publish events.
    Plater* m_plater{nullptr};
    // [STATE] Sticky cancel flag that suppresses later updates after the user aborts publishing.
    bool m_was_cancelled{false};

    wxBoxSizer* create_publish_step_sizer();
    // [EVENT] Translates a window-close request into the publish-stop event for Plater.
    void on_close(wxCloseEvent& event);
    // [UNITY] DPI changes only require relayout/refresh because the UI uses DIP-aware widgets rather
    // [UNITY] than custom raster resources that need reuploading.
    void on_dpi_changed(const wxRect& suggested_rect) override;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_BedShapeDialog_hpp_ */
