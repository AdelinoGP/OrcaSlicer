#ifndef slic3r_ConfigWizard_hpp_
#define slic3r_ConfigWizard_hpp_

#include <memory>

#include <wx/dialog.h>

#include "GUI_Utils.hpp"

namespace Slic3r {

class PresetBundle;
class PresetUpdater;

namespace GUI {

// [INTENT] Main class responsible for orchestrating the initial configuration of the slicer, guiding the user through setting up printers,
// filaments, etc. [UNITY] MonoBehaviour for handling UI dialog logic, likely using Unity UI Toolkit for cross-platform layout.
class ConfigWizard : public DPIDialog // [UNITY] Base class for DPI-aware Dialogs, map to custom Unity BaseDialog or Panel
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
