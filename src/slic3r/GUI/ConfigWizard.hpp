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
 Main class for the multi-page configuration wizard.
 Manages the lifecycle of vendor profile discovery, printer selection,
 and initial application settings.

 [STATE]
 - RunReason: Why the wizard was triggered (first run, update, user-requested).
 - StartPage: Which page to show first.
 - priv* p: Pimpl containing the actual wizard pages and the temporary PresetBundle.

 [UNITY]
 - MonoBehaviour (ConfigWizardController) for handling dialog flow.
 - Map to a multi-step UI Toolkit or uGUI wizard.
 - Use a ScriptableObject to store the temporary configuration being built.

 [PORTING_HAZARD:P2]
 The wizard logic is heavily dependent on the Pimpl (priv) structure which
 manages complex wxWidgets page transitions. In Unity, this should be
 refactored into a clear State Machine or navigation controller.
*/
class ConfigWizard : public DPIDialog
{
public:
    // [INTENT] Defines the reason for triggering the configuration wizard.
    enum RunReason {
        RR_DATA_EMPTY,    // No or empty datadir
        RR_DATA_LEGACY,   // Pre-updating datadir
        RR_DATA_INCOMPAT, // Incompatible datadir - Slic3r downgrade situation
        RR_USER,          // User requested the Wizard from the menus
    };

    // [INTENT] Defines the initial page for the wizard to display.
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

    // [INTENT] Starts the wizard and returns true if finished successfully.
    bool run(RunReason reason, StartPage start_page = SP_WELCOME);

    static const wxString& name(const bool from_menu = false);

protected:
    // [EVENT] Handles DPI scaling adjustments.
    void on_dpi_changed(const wxRect& suggested_rect) override;
    // [EVENT] Handles system color theme changes (e.g., light/dark mode switch).
    void on_sys_color_changed() override;

private:
    struct priv;
    // [STATE] Pointer to private implementation (pimpl) for encapsulating state and UI components.
    std::unique_ptr<priv> p;

    friend struct ConfigWizardPage;
};

} // namespace GUI
} // namespace Slic3r

#endif
