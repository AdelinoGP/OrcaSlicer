// [INTENT] This header defines dialogs for managing network plugins, including download, update, and restart prompts.
// It handles user interaction for plugin lifecycle events.
#ifndef slic3r_GUI_NetworkPluginDialog_hpp_
#define slic3r_GUI_NetworkPluginDialog_hpp_

#include "GUI_Utils.hpp"
#include "MsgDialog.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/Button.hpp"
#include "slic3r/Utils/bambu_networking.hpp"
#include <wx/collpane.h>

namespace Slic3r { namespace GUI {

// [INTENT] Dialog for informing the user about missing, outdated, or corrupted network plugins
// and providing options to download, skip, or ignore.
// [UNITY] Map to a custom UI Toolkit Document (UXML) for the dialog layout,
// with a C# MonoBehaviour controlling its logic and state.
class NetworkPluginDownloadDialog : public DPIDialog
{
public:
    // [INTENT] Defines the operational mode of the dialog (e.g., missing plugin, update available).
    enum class Mode { MissingPlugin, UpdateAvailable, CorruptedPlugin };

    // [INTENT] Constructor for the download dialog.
    // [EVENT] Initializes UI based on mode and error messages.
    NetworkPluginDownloadDialog(wxWindow*          parent,
                                Mode               mode,
                                const std::string& current_version = "",
                                const std::string& error_message   = "",
                                const std::string& error_details   = "");

    // [INTENT] Returns the version selected by the user in the ComboBox.
    // [STATE] Depends on `m_version_combo`.
    std::string get_selected_version() const;

    // [INTENT] Defines the result codes for dialog actions (e.g., download, skip).
    enum ResultCode {
        RESULT_DOWNLOAD     = wxID_OK,
        RESULT_SKIP         = wxID_CANCEL,
        RESULT_REMIND_LATER = wxID_APPLY,
        RESULT_SKIP_VERSION = wxID_IGNORE,
        RESULT_DONT_ASK     = wxID_ABORT
    };

protected:
    // [INTENT] Handles DPI changes, adapting the dialog's layout and scaling.
    // [EVENT] Called when system DPI changes.
    // [UNITY] Unity UI scales automatically. This method's logic would be integrated into responsive UI layout.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [INTENT] Creates the UI elements specific to a missing plugin scenario.
    void create_missing_plugin_ui();
    // [INTENT] Creates the UI elements specific to an update available scenario.
    void create_update_available_ui(const std::string& current_version);
    // [INTENT] Populates and configures the version selection ComboBox.
    void setup_version_selector();
    // [INTENT] Event handler for the download button.
    // [EVENT] wxCommandEvent
    void on_download(wxCommandEvent& evt);
    // [INTENT] Event handler for the skip button.
    // [EVENT] wxCommandEvent
    void on_skip(wxCommandEvent& evt);
    // [INTENT] Event handler for the "Remind Later" button.
    // [EVENT] wxCommandEvent
    void on_remind_later(wxCommandEvent& evt);
    // [INTENT] Event handler for the "Skip Version" button.
    // [EVENT] wxCommandEvent
    void on_skip_version(wxCommandEvent& evt);
    // [INTENT] Event handler for the "Don't Ask Again" button.
    // [EVENT] wxCommandEvent
    void on_dont_ask(wxCommandEvent& evt);

    // [STATE] Current mode of the dialog.
    Mode m_mode;
    // [STATE] Pointer to the ComboBox for version selection.
    // [UNITY] UI Toolkit Dropdown or custom Listview.
    ComboBox* m_version_combo{nullptr};
    // [STATE] Pointer to the collapsible pane for displaying error details.
    // [UNITY] UI Toolkit Foldout.
    wxCollapsiblePane* m_details_pane{nullptr};
    // [STATE] Stores the error message to display.
    std::string m_error_message;
    // [STATE] Stores detailed error information.
    std::string m_error_details;
    // [STATE] List of available plugin versions.
    std::vector<NetworkLibraryVersionInfo> m_available_versions;
};

// [INTENT] Dialog for prompting the user to restart the application after a plugin update.
// [UNITY] Map to a custom UI Toolkit Document (UXML) for a simple dialog,
// with a C# MonoBehaviour controlling its logic.
class NetworkPluginRestartDialog : public DPIDialog
{
public:
    // [INTENT] Constructor for the restart dialog.
    NetworkPluginRestartDialog(wxWindow* parent);

    // [INTENT] Returns whether the user chose to restart immediately.
    // [STATE] Depends on `m_restart_now`.
    bool should_restart_now() const { return m_restart_now; }

protected:
    // [INTENT] Handles DPI changes.
    // [EVENT] Called when system DPI changes.
    // [UNITY] Unity UI scales automatically.
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    // [STATE] Flag indicating if the user wants to restart now.
    bool m_restart_now{false};
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_NetworkPluginDialog_hpp_
