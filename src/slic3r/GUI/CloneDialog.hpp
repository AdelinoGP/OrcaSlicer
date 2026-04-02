#ifndef slic3r_GUI_SingleChoice_hpp_
#define slic3r_GUI_SingleChoice_hpp_

#include "GUI_Utils.hpp"
#include "Plater.hpp"
#include "Selection.hpp"

#include "Widgets/Button.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ProgressBar.hpp"

namespace Slic3r { namespace GUI {

/*
 [INTENT]
 CloneDialog is a modal dialog for duplicating selected 3D objects/instances.
 It allows the user to specify a count and optionally auto-arrange the plate after cloning.

 [STATE]
 - m_count: Number of copies requested.
 - m_arrange_cb: Persistent preference for auto-arrangement.
 - m_cancel_process: Latch to interrupt the iterative cloning loop.

 [UNITY]
 Map to a modal UI Toolkit panel or a prefab-based dialog.
 The iterative cloning process should be moved to a Coroutine or async Task to avoid blocking the main thread.

 [PORTING_HAZARD:P2]
 The current implementation uses clipboard-based cloning in a loop on the UI thread with wxYield().
 Unity should use direct Prefab instantiation or Object.Instantiate() for cloning.
 The progress bar and cancellation logic should be handled through a standard Unity async progress pattern.
*/
class CloneDialog : public DPIDialog
{
public:
    CloneDialog(wxWindow* parent = nullptr);
    ~CloneDialog();

private:
    SpinInput*   m_count_spin;
    int          m_count;
    CheckBox*    m_arrange_cb;
    Plater*      m_plater;
    ProgressBar* m_progress;
    AppConfig*   m_config;
    bool         m_cancel_process;

    void on_dpi_changed(const wxRect& suggested_rect) override {}
};
}} // namespace Slic3r::GUI

#endif