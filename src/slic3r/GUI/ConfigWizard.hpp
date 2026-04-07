// [ANNOTATED]
#ifndef slic3r_ConfigWizard_hpp_
#define slic3r_ConfigWizard_hpp_

#include <memory>

#include <wx/dialog.h>

#include "GUI_Utils.hpp"

namespace Slic3r {

class PresetBundle;
class PresetUpdater;

namespace GUI {

/*
 [INTENT]
 Declaration boundary for the onboarding/configuration wizard dialog.
 This shell exposes the run entry point, the startup reason, and the initial
 page selection while hiding the actual page graph and temporary preset state
 behind a private implementation.

 [STATE]
 - RunReason: Launch context for first-run, upgrade, downgrade, or manual entry.
 - StartPage: Optional deep-link into a specific stage of the flow.
 - priv p: Dialog-owned implementation containing the page controllers, staged
   PresetBundle mutations, and navigation state declared in ConfigWizard_private.hpp.

 [EVENT]
 - run() drives the modal wizard lifecycle and returns whether the user finished
   the flow successfully.
 - on_dpi_changed() and on_sys_color_changed() forward shell-level environment
   changes to the private page tree.

 [UNITY]
 - Port this as a retained wizard controller plus explicit page/state model.
 - Keep the temporary configuration bundle separate from the rendered views.

 [PORTING_HAZARD:P2]
 The header looks simple, but almost all behavior is funneled through the pimpl
 and friend page types. Unity should replace that hidden wx-centric navigation
 contract with an explicit state machine and owned data model rather than
 mirroring the private page coupling.
*/
class ConfigWizard : public DPIDialog
{
public:
    // [INTENT] Reason the app opened the wizard; controls copy, gating, and follow-up actions.
    enum RunReason {
        RR_DATA_EMPTY,    // No or empty datadir
        RR_DATA_LEGACY,   // Pre-updating datadir
        RR_DATA_INCOMPAT, // Incompatible datadir - Slic3r downgrade situation
        RR_USER,          // User requested the Wizard from the menus
    };

    // [INTENT] Optional entry page override used when callers jump directly into a subsection of the flow.
    enum StartPage {
        SP_WELCOME,
        SP_PRINTERS,
        SP_FILAMENTS,
        SP_MATERIALS,
        SP_CUSTOM,
    };

    ConfigWizard(wxWindow* parent);
    ConfigWizard(ConfigWizard&&)                 = delete;
    ConfigWizard(const ConfigWizard&)            = delete;
    ConfigWizard& operator=(ConfigWizard&&)      = delete;
    ConfigWizard& operator=(const ConfigWizard&) = delete;
    ~ConfigWizard();

    // [EVENT] Starts the modal flow and reports whether the wizard committed its staged configuration.
    bool run(RunReason reason, StartPage start_page = SP_WELCOME);

    // [INTENT] Shared localized wizard title; `from_menu` selects the user-invoked wording.
    static const wxString& name(const bool from_menu = false);

protected:
    // [EVENT] Handles DPI scaling adjustments.
    void on_dpi_changed(const wxRect& suggested_rect) override;
    // [EVENT] Handles system color theme changes (e.g., light/dark mode switch).
    void on_sys_color_changed() override;

private:
    struct priv;
    // [STATE] Dialog-owned private implementation containing the page stack, staged preset bundle, and navigation logic.
    std::unique_ptr<priv> p;

    // [STATE] Page classes reach into the private implementation instead of using a public controller API.
    friend struct ConfigWizardPage;
};

} // namespace GUI
} // namespace Slic3r

#endif
