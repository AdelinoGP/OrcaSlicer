#ifndef slic3r_GUI_hpp_
#define slic3r_GUI_hpp_

namespace boost {
class any;
}
namespace boost::filesystem {
class path;
}

#include <wx/string.h>

#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"

class wxWindow;
class wxMenuBar;
class wxComboCtrl;
class wxFileDialog;
class wxTopLevelWindow;

namespace Slic3r {

// [INTENT][UNITY] Namespace providing global UI-related helper functions, platform-specific shortcuts, menu/toolbar wiring, and GL/preview
// coordination so Unity’s MainMenu + RenderTexture controllers can hook into the same helpers.
namespace GUI {

// [PORTING_HAZARD:P2] No direct equivalent; requires native plugin for OS-level power management.
// [STATE][THREAD][UNITY] Suppresses the OS screensaver while prints or slices are actively running; must execute on the main UI thread so
// Unity can mirror the same `Screen.sleepTimeout` behavior.
void disable_screensaver();
// [STATE][THREAD][UNITY] Restores OS heuristics once the long-running operation completes so Unity can flip `Screen.sleepTimeout` back to default.
void enable_screensaver();
// [PORTING_HAZARD:P2] Platform-specific; map to System.Diagnostics for C# debugging.
// [THREAD] Intended to run on the UI/main thread before hitting debugger breaks so worker slices stay paused cleanly.
bool debugged();
void break_to_debugger();

// [PORTING_HAZARD:P1] Maps to Unity `Input` system or `EventSystem` mapping strings.
// [STATE][INTENT][UNITY] Caches the current accelerator prefixes so menus and tooltips stay synchronized with platform conventions,
// mirroring Input System shortcuts.
extern const std::string& shortkey_ctrl_prefix();
extern const std::string& shortkey_alt_prefix();

// [STATE][UNITY] Exposes the singleton `AppConfig` pointer so both legacy and Unity pipes read from the same serialized settings model.
extern AppConfig* get_app_config();

// [EVENT][OPENGL][UNITY] Registers the wxMenuBar entries, preference/language command IDs, and GL preview refresh hooks so Unity can layer
// a UI Toolkit menu tree and trigger RenderTexture updates when the menu structure changes.
extern void add_menus(wxMenuBar* menu, int event_preferences_changed, int event_language_change);

// [STATE][EVENT][THREAD] Mutates `DynamicPrintConfig` caches and emits option-change handlers while running on the UI thread before slicing
// workers read the new value.
// [UNITY] Maps to Data-binding (UI Toolkit) or direct property manipulation in a ViewModel pattern.
// [PORTING_HAZARD:P3] Option observers assume `DynamicPrintConfig` is updated synchronously on the main thread and the preview refresh
// time-sensitive, so Unity must guard against multi-threaded mutation while keeping RenderTexture updates in sync.
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

// [EVENT][THREAD][UNITY] Routed from validators and CLI error handlers so the message box shows on the UI thread with the preserved font
// preference flag; Unity should raise an equivalent VisualElement dialog or `EditorUtility.DisplayDialog` when running in-editor.
void        show_error(wxWindow* parent, const wxString& message, bool monospaced_font = false);
void        show_error(wxWindow* parent, const char* message, bool monospaced_font = false);
inline void show_error(wxWindow* parent, const std::string& message, bool monospaced_font = false)
{
    show_error(parent, message.c_str(), monospaced_font);
}
// [EVENT][THREAD][UNITY] Shims the Perl callback surface, so Unity must invoke a `DialogService.RaiseFromCode` with the same numeric ID.
void show_error_id(int id, const std::string& message); // For Perl
// [EVENT][THREAD][UNITY] Populates info dialogs used by the config wizards; Unity should marshal via main-thread coroutines.
void        show_info(wxWindow* parent, const wxString& message, const wxString& title = wxString());
void        show_info(wxWindow* parent, const char* message, const char* title = nullptr);
inline void show_info(wxWindow* parent, const std::string& message, const std::string& title = std::string())
{
    show_info(parent, message.c_str(), title.c_str());
}
// [EVENT][STATE][THREAD] Wraps unexpected state so warning dialogs always carry context; Unity can mirror via a `WarningDialog`
// MonoBehaviour that runs on the main thread.
void warning_catcher(wxWindow* parent, const wxString& message);
// [STATE][THREAD][UNITY] Displays the current substitution stacks for presets/config on the UI thread and lets the diagnostics dialog share
// the same ScriptableObject metadata.
void show_substitutions_info(const PresetsConfigSubstitutions& presets_config_substitutions);
// [STATE][THREAD][UNITY] Reuses the same dialog logic when a file-specific substitution overrides the base, so Unity can lock in the
// serialized source/target pair for the overlay panel while keeping the update on the UI thread.
void show_substitutions_info(const ConfigSubstitutions& config_substitutions, const std::string& filename);

// [STATE][EVENT][THREAD] Builds checkbox list state backed by a bitmask string so Unity can present the same selections without losing
// contextual text. Unity must run this on the main thread (matching wxWidgets) and reimplement as UI Toolkit Checkbox list dialogs with the
// same bitmask semantics.
void create_combochecklist(wxComboCtrl* comboCtrl, const std::string& text, const std::string& items);

// [STATE] Reads the checkbox bitmask so other parts of the UI know which entries remain checked.
// [THREAD] The caller assumes this runs on the GUI thread to avoid wxComboCtrl races.
unsigned int combochecklist_get_flags(wxComboCtrl* comboCtrl);
// [STATE] Writes the checkbox bitmask when the caller updates the combo selection.
// [THREAD] Must execute on the UI thread to keep wxComboCtrl in a safe state.
void combochecklist_set_flags(wxComboCtrl* comboCtrl, unsigned int flags);

// [INTENT] Keeps a single conversion boundary between wxString and UTF-8 so other UI helpers rely on consistent encoding.
// [UNITY] Generally handled by native C# System.Text.Encoding and System.IO.Path.
wxString                from_u8(const std::string& str);
std::string             into_u8(const wxString& str);
wxString                from_path(const boost::filesystem::path& path);
boost::filesystem::path into_path(const wxString& str);

// [EVENT][INTENT][UNITY] Fired from Help/About menu; Unity will render the same dialogs through a `DialogService` controller.
extern void about();
// [EVENT][UNITY] Bridges the legacy login sequence (OAuth tokens and telemetry) so Unity can present the same modal with the existing cloud flows.
extern void login();
// [EVENT][THREAD][UNITY] Launches the saved user data directory (reports, logs, presets); Unity should pool the same path cache and perform
// the launch on the main thread via `SynchronizationContext.Post` before calling `Process.Start`. [PORTING_HAZARD:P3] Depending on the OS,
// Unity may lack the exact preset path logic; reusing AppConfig's `user_data_dir` keeps functionality aligned.
extern void desktop_open_datadir_folder();
// [EVENT][THREAD] Accepts any arbitrary path (used by Quick Access commands); keep the path resolution and the UI thread launch guard so
// that Unity can translate it to `Application.OpenURL` safely.
extern void desktop_open_any_folder(const std::string& path);
} // namespace GUI
} // namespace Slic3r

#endif
