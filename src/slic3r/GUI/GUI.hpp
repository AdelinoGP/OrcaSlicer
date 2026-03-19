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

// [INTENT] Namespace providing global UI-related helper functions, platform-specific shortcuts, and string conversion utilities.
namespace GUI {

// [PORTING_HAZARD:P2] No direct equivalent; requires native plugin for OS-level power management.
void disable_screensaver();
void enable_screensaver();
// [PORTING_HAZARD:P2] Platform-specific; map to System.Diagnostics for C# debugging.
bool debugged();
void break_to_debugger();

// [PORTING_HAZARD:P1] Maps to Unity `Input` system or `EventSystem` mapping strings.
extern const std::string& shortkey_ctrl_prefix();
extern const std::string& shortkey_alt_prefix();

// [UNITY] Maps to a ScriptableObject or Singleton for global application state.
extern AppConfig* get_app_config();

// [UNITY] Maps to Unity MenuBar (Unity Editor) or custom UI Toolkit menu component (Runtime).
extern void add_menus(wxMenuBar* menu, int event_preferences_changed, int event_language_change);

// [UNITY] Maps to Data-binding (UI Toolkit) or direct property manipulation in a ViewModel pattern.
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index = 0);

// [UNITY] Maps to UI Toolkit/uGUI dialog components or EditorUtility.DisplayDialog (Editor).
void        show_error(wxWindow* parent, const wxString& message, bool monospaced_font = false);
void        show_error(wxWindow* parent, const char* message, bool monospaced_font = false);
inline void show_error(wxWindow* parent, const std::string& message, bool monospaced_font = false)
{
    show_error(parent, message.c_str(), monospaced_font);
}
void        show_error_id(int id, const std::string& message); // For Perl
void        show_info(wxWindow* parent, const wxString& message, const wxString& title = wxString());
void        show_info(wxWindow* parent, const char* message, const char* title = nullptr);
inline void show_info(wxWindow* parent, const std::string& message, const std::string& title = std::string())
{
    show_info(parent, message.c_str(), title.c_str());
}
void warning_catcher(wxWindow* parent, const wxString& message);
void show_substitutions_info(const PresetsConfigSubstitutions& presets_config_substitutions);
void show_substitutions_info(const ConfigSubstitutions& config_substitutions, const std::string& filename);

// [UNITY] Reimplement as UI Toolkit Checkbox list dialogs.
void create_combochecklist(wxComboCtrl* comboCtrl, const std::string& text, const std::string& items);

// [UNITY] Reimplement as UI Toolkit Checkbox list state accessors.
unsigned int combochecklist_get_flags(wxComboCtrl* comboCtrl);
void         combochecklist_set_flags(wxComboCtrl* comboCtrl, unsigned int flags);

// [UNITY] Generally handled by native C# System.Text.Encoding and System.IO.Path.
wxString                from_u8(const std::string& str);
std::string             into_u8(const wxString& str);
wxString                from_path(const boost::filesystem::path& path);
boost::filesystem::path into_path(const wxString& str);

// [UNITY] Map to UI Toolkit dialogs.
extern void about();
extern void login();
// [UNITY] Map to Application.OpenURL or System.Diagnostics.Process.Start (OS dependent).
extern void desktop_open_datadir_folder();
extern void desktop_open_any_folder(const std::string& path);
} // namespace GUI
} // namespace Slic3r

#endif
