// [ANNOTATED]
// [INTENT] Declaration of the Linux-specific desktop integration dialog and static management helpers.
// [STATE] Static methods provide global access to integration status and operations.
// [UNITY] Map to a C# ModalDialog controller and a platform-specific DesktopIntegrationService.
// [PORTING_HAZARD:P2] Linux-only module. Platform-specific logic should be decoupled from the UI.

#ifdef __linux__
#ifndef slic3r_DesktopIntegrationDialog_hpp_
#define slic3r_DesktopIntegrationDialog_hpp_

#include <wx/dialog.h>

namespace Slic3r { namespace GUI {
class DesktopIntegrationDialog : public wxDialog
{
public:
    DesktopIntegrationDialog(wxWindow* parent);
    DesktopIntegrationDialog(DesktopIntegrationDialog&&)                 = delete;
    DesktopIntegrationDialog(const DesktopIntegrationDialog&)            = delete;
    DesktopIntegrationDialog& operator=(DesktopIntegrationDialog&&)      = delete;
    DesktopIntegrationDialog& operator=(const DesktopIntegrationDialog&) = delete;
    ~DesktopIntegrationDialog();

    // methods that actually do / undo desktop integration. Static to be accesible from anywhere.

    // returns true if path to OrcaSlicer.desktop is stored in App Config and existence of desktop file.
    // Does not check if desktop file leads to this binary or existence of icons and viewer desktop file.
    static bool is_integrated();
    // true if appimage
    static bool integration_possible();
    // Creates Desktop files and icons for both PrusaSlicer and GcodeViewer.
    // Stores paths into App Config.
    // Rewrites if files already existed.
    static void perform_desktop_integration();
    // Deletes Desktop files and icons for both PrusaSlicer and GcodeViewer at paths stored in App Config.
    static void undo_desktop_intgration();

    static void perform_downloader_desktop_integration(std::string url_prefix);
    static void undo_downloader_registration();

private:
};
}} // namespace Slic3r::GUI

#endif // slic3r_DesktopIntegrationDialog_hpp_
#endif // __linux__
