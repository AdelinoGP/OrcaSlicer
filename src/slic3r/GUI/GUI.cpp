#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "I18N.hpp"

#include "libslic3r/LocalesUtils.hpp"
#ifdef __APPLE__
#include "slic3r/Utils/MacDarkMode.hpp"
#endif
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/any.hpp>

#if __APPLE__
#import <IOKit/pwr_mgt/IOPMLib.h>
#elif _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "boost/nowide/convert.hpp"
#endif

#include "AboutDialog.hpp"
#include "MsgDialog.hpp"
#include "format.hpp"

#include "WebUserLoginDialog.hpp"

#include "libslic3r/Print.hpp"

namespace Slic3r {

class AppConfig;

namespace GUI {

#if __APPLE__
IOPMAssertionID assertionID;
#endif

// [INTENT] Central utilities for the GUI layer: dialog helpers, config mutators, platform bridges, and error plumbing shared across the
// wxApp. [STATE] Holds the limited cross-cutting caches (e.g., assertion handle, string prefixes) that do not belong to a single window but
// any controller may query. [UNITY] Mirror this file with a static Unity UI service (MonoBehaviour + ScriptableObject config store +
// VisualElement helpers) that exposes the same helpers to downstream panels.

#if __APPLE__
IOPMAssertionID assertionID;
#endif

// [INTENT] Prevents system sleep/screensaver during long operations.
// [UNITY] Use Screen.sleepTimeout = SleepTimeout.NeverSleep;
void disable_screensaver()
{
#if __APPLE__
    CFStringRef               reasonForActivity = CFSTR("Slic3r");
    [[maybe_unused]] IOReturn success           = IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                                                                              reasonForActivity, &assertionID);
// ignore result: success == kIOReturnSuccess
#elif _WIN32
    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS); // [PORTING_HAZARD:P1] Platform-specific API
#endif
}

// [INTENT] Re-enables system sleep/screensaver.
// [UNITY] Use Screen.sleepTimeout = SleepTimeout.SystemSetting;
void enable_screensaver()
{
#if __APPLE__
    IOPMAssertionRelease(assertionID);
#elif _WIN32
    SetThreadExecutionState(ES_CONTINUOUS); // [PORTING_HAZARD:P1] Platform-specific API
#endif
}

// [INTENT] Checks if the application is currently being debugged.
// [UNITY] Use UnityEngine.Debug.isDebugBuild.
bool debugged()
{
#ifdef _WIN32
    return IsDebuggerPresent() == TRUE; // [PORTING_HAZARD:P1] Platform-specific API
#else
    return false;
#endif /* _WIN32 */
}

// [INTENT] Triggers a breakpoint if a debugger is attached.
// [UNITY] Use UnityEngine.Debug.Break().
void break_to_debugger()
{
#ifdef _WIN32
    if (IsDebuggerPresent())
        DebugBreak(); // [PORTING_HAZARD:P1] Platform-specific API
#endif                /* _WIN32 */
}

// [INTENT] Provide the localized control key label used by menus and buttons on each platform for consistency.
// [STATE] Caches the prefix string so repeated shortcut markup reuses the same prefix instance instead of re-instantiating it.
// [UNITY] Replace with a Unity shortcut helper (UI Toolkit `Label` bound to `InputAction.asset` names) while respecting Command vs Ctrl semantics.
const std::string& shortkey_ctrl_prefix()

{
    static const std::string str =
#ifdef __APPLE__
        u8"\u2318+" // "⌘+" (Mac Command+)
#else
        _u8L("Ctrl+")
#endif
        ;
    return str;
}

// [INTENT] Surface the alternate modifier key label (Alt/Option) that matches the running platform so the tooltip copy feels native.
// [STATE] Stores the string once per run to avoid repeated std::string allocations when building UI hints.
// [UNITY] Mirror this with Unity's input label provider so `KeyCode.LeftAlt` vs `KeyCode.LeftCommand` display correctly in menus.
const std::string& shortkey_alt_prefix()
{
    static const std::string str =
#ifdef __APPLE__
        u8"\u2325+" // "⌥+" (Mac Option+)
#else
        _u8L("Alt+")
#endif
        ;
    return str;
}

// [INTENT] Pushes a user change from the GUI controls into the shared DynamicPrintConfig so the model and the subsequent render/preset
// logic stay in sync. [STATE] Updates option vectors and caches for enumeration/string/point types that are reused by tabs and preview
// panels. [EVENT] Called from event handlers tied to sliders, combo boxes, and dialog Apply buttons; it is the destination of those
// callbacks. [THREAD] Assumes it runs on the main wxWidgets thread because it mutates config state that the UI and renderers assume to be
// consistent. [UNITY] Replace with a ConfigService MonoBehaviour that accepts `SerializedObject` updates and writes into a
// `ScriptableObject` `SettingsModel` for all panels. [PORTING_HAZARD:P2] Translating the boost::any + ConfigOption union requires building
// a versatile converter layer in C# that mirrors the wide enum of option types. opt_index = 0, by the reason of zero-index in
// ConfigOptionVector by default (in case only one element)
void change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index /*= 0*/)
{
    try {
        if (config.def()->get(opt_key)->type == coBools && config.def()->get(opt_key)->nullable) {
            const auto v       = boost::any_cast<unsigned char>(value);
            auto       vec_new = std::make_unique<ConfigOptionBoolsNullable>(1, v);
            if (v == ConfigOptionBoolsNullable::nil_value()) {
                vec_new->set_at_to_nil(0);
            }

            config.option<ConfigOptionBoolsNullable>(opt_key)->set_at(vec_new.get(), opt_index, 0);
            return;
        }

        const ConfigOptionDef* opt_def = config.def()->get(opt_key);
        switch (opt_def->type) {
        case coFloatOrPercent: {
            std::string str     = boost::any_cast<std::string>(value);
            bool        percent = false;
            if (str.back() == '%') {
                str.pop_back();
                percent = true;
            }
            double val = std::stod(str); // locale-dependent (on purpose - the input is the actual content of the field)
            config.set_key_value(opt_key, new ConfigOptionFloatOrPercent(val, percent));
            break;
        }
        case coFloatsOrPercents: {
            std::string str     = boost::any_cast<std::string>(value);
            bool        percent = false;
            if (str.back() == '%') {
                str.pop_back();
                percent = true;
            }
            double val     = std::stod(str); // locale-dependent (on purpose - the input is the actual content of the field)
            auto   vec_new = std::make_unique<ConfigOptionFloatOrPercent>(val, percent);
            config.option<ConfigOptionFloatsOrPercents>(opt_key)->set_at(vec_new.get(), opt_index, opt_index);
            break;
        }
        case coPercent: config.set_key_value(opt_key, new ConfigOptionPercent(boost::any_cast<double>(value))); break;
        case coFloat: {
            double& val = config.opt_float(opt_key);
            val         = boost::any_cast<double>(value);
            break;
        }
        case coPercents: {
            auto vec_new = std::make_unique<ConfigOptionPercent>(boost::any_cast<double>(value));
            config.option<ConfigOptionPercents>(opt_key)->set_at(vec_new.get(), opt_index, opt_index);
            break;
        }
        case coFloats: {
            auto vec_new = std::make_unique<ConfigOptionFloat>(boost::any_cast<double>(value));
            config.option<ConfigOptionFloats>(opt_key)->set_at(vec_new.get(), opt_index, opt_index);
            break;
        }
        case coString: config.set_key_value(opt_key, new ConfigOptionString(boost::any_cast<std::string>(value))); break;
        case coStrings: {
            if (opt_key == "compatible_prints" || opt_key == "compatible_printers") {
                config.option<ConfigOptionStrings>(opt_key)->values = boost::any_cast<std::vector<std::string>>(value);
            } else if (config.def()->get(opt_key)->gui_flags.compare("serialized") == 0) {
                std::string              str = boost::any_cast<std::string>(value);
                std::vector<std::string> values{};
                if (!str.empty()) {
                    if (str.back() == ';')
                        str.pop_back();
                    // Split a string to multiple strings by a semi - colon.This is the old way of storing multi - string values.
                    // Currently used for the post_process config value only.
                    boost::split(values, str, boost::is_any_of(";"));
                    if (values.size() == 1 && values[0] == "")
                        values.resize(0);
                }
                config.option<ConfigOptionStrings>(opt_key)->values = values;
            } else {
                auto vec_new = std::make_unique<ConfigOptionString>(boost::any_cast<std::string>(value));
                config.option<ConfigOptionStrings>(opt_key)->set_at(vec_new.get(), opt_index, 0);
            }
        } break;
        case coBool: config.set_key_value(opt_key, new ConfigOptionBool(boost::any_cast<bool>(value))); break;
        case coBools: {
            auto vec_new = std::make_unique<ConfigOptionBool>(boost::any_cast<unsigned char>(value) != 0);
            config.option<ConfigOptionBools>(opt_key)->set_at(vec_new.get(), opt_index, 0);
            break;
        }
        case coInt: config.set_key_value(opt_key, new ConfigOptionInt(boost::any_cast<int>(value))); break;
        case coInts: {
            auto vec_new = std::make_unique<ConfigOptionInt>(boost::any_cast<int>(value));
            config.option<ConfigOptionInts>(opt_key)->set_at(vec_new.get(), opt_index, 0);
        } break;
        case coEnum: {
            auto* opt = opt_def->default_value.get()->clone();
            opt->setInt(boost::any_cast<int>(value));
            config.set_key_value(opt_key, opt);
        } break;
        // BBS
        case coEnums: {
            auto vec_new = std::make_unique<ConfigOptionEnumsGeneric>(std::vector<int>{boost::any_cast<int>(value)});
            if (config.has(opt_key))
                config.option<ConfigOptionEnumsGeneric>(opt_key)->set_at(vec_new.get(), opt_index, 0);
        } break;
        case coPoint: {
            config.set_key_value(opt_key, new ConfigOptionPoint(boost::any_cast<Vec2d>(value)));
        } break;
        case coPoints: {
            if (opt_key == "printable_area" || opt_key == "bed_exclude_area" || opt_key == "thumbnails" ||
                opt_key == "wrapping_exclude_area") {
                config.option<ConfigOptionPoints>(opt_key)->values = boost::any_cast<std::vector<Vec2d>>(value);
                break;
            }
            auto vec_new = std::make_unique<ConfigOptionPoint>(boost::any_cast<Vec2d>(value));
            config.option<ConfigOptionPoints>(opt_key)->set_at(vec_new.get(), opt_index, 0);
        } break;
        case coNone: break;
        default: break;
        }
    } catch (const std::exception& e) {
        wxLogError(format_wxstr("Internal error when changing value for %1%: %2%", opt_key, e.what()));
    }
}

// [INTENT] Marshals error reporting back to the GUI thread so asynchronous jobs can report fatal states without racing dialogs.
// [EVENT] Invoked from background workers, job callbacks, and config validation code when fatal states occur.
// [THREAD] `CallAfter` guarantees the dialog shows on the main wxWidgets thread, mirroring Unity's main-thread dispatcher requirement.
// [UNITY] Replace with `MainThreadDispatcher.Instance.Enqueue(() => UIMessage.ShowError(...))` to keep the dialog modal in Unity.
void show_error(wxWindow* parent, const wxString& message, bool monospaced_font)
{
    wxGetApp().CallAfter([=] {
        ErrorDialog msg(parent, message, monospaced_font);
        msg.ShowModal();
    });
}

// [INTENT] Wraps the wxString overload so legacy callers with C strings can reuse the same thread-safe dialog logic.
// [UNITY] All platform bindings should funnel into the same main-thread dispatcher regardless of string type.
void show_error(wxWindow* parent, const char* message, bool monospaced_font)
{
    assert(message);
    show_error(parent, wxString::FromUTF8(message), monospaced_font);
}

void show_error_id(int id, const std::string& message)
{
    // [INTENT] Useful when async tasks report errors keyed to a window ID instead of direct pointers.
    // [EVENT] Converts numeric IDs back into wxWindows before reusing the main dialog path.
    auto* parent = id != 0 ? wxWindow::FindWindowById(id) : nullptr;
    show_error(parent, message);
}

// [INTENT] Presents an informational dialog on the main thread with consistent app naming so user messages never look stale.
// [EVENT] Called from success paths, config loads, or job completions that want to notify the user before continuing.
// [UNITY] Replace with UI Toolkit `DialogWindow.ShowAsync` on the main thread while composing `AppName` in code.
void show_info(wxWindow* parent, const wxString& message, const wxString& title)
{
    // wxMessageDialog msg_wingow(parent, message, wxString(SLIC3R_APP_NAME " - ") + (title.empty() ? _L("Notice") : title), wxOK |
    // wxICON_INFORMATION);
    MessageDialog msg_wingow(parent, message, wxString(SLIC3R_APP_FULL_NAME " - ") + (title.empty() ? _L("Notice") : title),
                             wxOK | wxICON_INFORMATION);
    msg_wingow.ShowModal();
}

// [INTENT] C-string helper that defers to the wide-string entry so we never duplicate modal behavior.
void show_info(wxWindow* parent, const char* message, const char* title)
{
    assert(message);
    show_info(parent, wxString::FromUTF8(message), title ? wxString::FromUTF8(title) : wxString());
}

// [INTENT] Shows warning dialogs invoked from validation hooks without disabling the rest of the UI.
// [EVENT] Used by config loaders or job responses that want to inform users but keep retrying.
// [UNITY] Use a `ModalWindow` on Unity's UI Toolkit + `IGenericDialogService.ShowWarningAsync` to mirror this behavior.
void warning_catcher(wxWindow* parent, const wxString& message)
{
    MessageDialog msg(parent, message, _L("Warning"), wxOK | wxICON_WARNING);
    msg.ShowModal();
}

static wxString bold(const wxString& str) { return wxString::Format("<b>%s</b>", str); };

static wxString bold_string(const wxString& str) { return wxString::Format("<b>\"%s\"</b>", str); };

// [INTENT] Builds the HTML table that lists which configuration entries were rewritten during import/upgrade flows.
// [STATE] Reads the `ConfigOptionDef` metadata to describe both old and new values, so the fallback preview can render consistent tooltips.
// [EVENT] Called when preset/config loads complete and the migration path wants to show the user what changed.
// [UNITY] Map to a Unity `ScrollableText` or `UI Toolkit` `Label` fed by a data object generated from `ConfigOptionDef` so the upgrade
// popup matches wx behavior. [PORTING_HAZARD:P3] Unity lacks native HTML widgets, so the table string must be converted into stylized text
// (e.g., `RichTextField`).
static void add_config_substitutions(const ConfigSubstitutions& conf_substitutions, wxString& changes)
{
    changes += "<table>";
    for (const ConfigSubstitution& conf_substitution : conf_substitutions) {
        wxString               new_val;
        const ConfigOptionDef* def = conf_substitution.opt_def;
        if (!def)
            continue;
        switch (def->type) {
        case coEnum: {
            const std::vector<std::string>& labels = def->enum_labels;
            const std::vector<std::string>& values = def->enum_values;
            int                             val    = conf_substitution.new_value->getInt();

            bool is_infill = def->opt_key == "top_surface_pattern" || def->opt_key == "bottom_surface_pattern" ||
                             def->opt_key == "internal_solid_infill_pattern" || def->opt_key == "support_base_pattern" ||
                             def->opt_key == "support_interface_pattern" || def->opt_key == "ironing_pattern" ||
                             def->opt_key == "support_ironing_pattern" || def->opt_key == "sparse_infill_pattern";

            // Each infill doesn't use all list of infill declared in PrintConfig.hpp.
            // So we should "convert" val to the correct one
            if (is_infill) {
                for (const auto& key_val : *def->enum_keys_map)
                    if ((int) key_val.second == val) {
                        auto it = std::find(values.begin(), values.end(), key_val.first);
                        if (it == values.end())
                            break;
                        auto idx = it - values.begin();
                        new_val  = wxString("\"") + values[idx] + "\"" + " (" + from_u8(_utf8(labels[idx])) + ")";
                        break;
                    }
                if (new_val.IsEmpty()) {
                    assert(false);
                    new_val = _L("Undefined");
                }
            } else
                new_val = wxString("\"") + values[val] + "\"" + " (" + from_u8(_utf8(labels[val])) + ")";
            break;
        }
        case coEnums: {
            const std::vector<std::string>& labels = def->enum_labels;
            const std::vector<std::string>& values = def->enum_values;
            std::string                     val    = conf_substitution.new_value->serialize();
            new_val                                = wxString("\"") + from_u8(_utf8(val)) + "\"";
            break;
        }
        case coBool: new_val = conf_substitution.new_value->getBool() ? "true" : "false"; break;
        case coBools:
            if (conf_substitution.new_value->nullable())
                for (const char v : static_cast<const ConfigOptionBoolsNullable*>(conf_substitution.new_value.get())->values)
                    new_val += std::string(v == ConfigOptionBoolsNullable::nil_value() ? "nil" : v ? "true" : "false") + ", ";
            else
                for (const char v : static_cast<const ConfigOptionBools*>(conf_substitution.new_value.get())->values)
                    new_val += std::string(v ? "true" : "false") + ", ";
            if (!new_val.empty())
                new_val.erase(new_val.begin() + new_val.size() - 2, new_val.end());
            break;
        default: assert(false);
        }

        changes += format_wxstr("<tr><td><b>\"%1%\" (%2%)</b></td><td>: ", def->opt_key, _(def->label)) +
                   format_wxstr(_L("%1% was replaced with %2%"), bold_string(conf_substitution.old_value), bold(new_val)) + "</td></tr>";
    }
    changes += "</table>";
}

static wxString substitution_message(const wxString& changes)
{
    return _L("The configuration may be generated by a newer version of OrcaSlicer.") + " " +
           _L("Some values have been replaced. Please check them:") + "\n" + changes + "\n";
}

void show_substitutions_info(const PresetsConfigSubstitutions& presets_config_substitutions)
{
    // [INTENT] Alerts the user when preset files from newer versions contain fields that were rewritten or dropped.
    // [STATE] Uses the `changes` HTML to describe what each preset migration touched so users can verify them.
    // [EVENT] Triggered after config imports finish and a `PresetsConfigSubstitutions` record is created.
    // [UNITY] Replace with a Unity `Dialog` that binds to a `MigrationSummary` model provided by the translation service.
    wxString changes;

    auto preset_type_name = [](Preset::Type type) {
        switch (type) {
        case Preset::TYPE_PRINT: return _L("Process");
        // BBS: remove TYPE_SLA_PRINT
        case Preset::TYPE_FILAMENT: return _L("Filament");
        // BBS: remove TYPE_SLA_MATERIAL
        case Preset::TYPE_PRINTER: return _L("Machine");
        // BBS: remove TYPE_PHYSICAL_PRINTER
        default: assert(false); return wxString();
        }
    };

    for (const PresetConfigSubstitutions& substitution : presets_config_substitutions) {
        changes += "\n\n" + format_wxstr("%1% : %2%", preset_type_name(substitution.preset_type), bold_string(substitution.preset_name));
        if (!substitution.preset_file.empty())
            changes += format_wxstr(" (%1%)", substitution.preset_file);

        add_config_substitutions(substitution.substitutions, changes);
    }

    InfoDialog msg(nullptr, _L("Configuration package was loaded, but some values were not recognized."), substitution_message(changes),
                   true);
    msg.ShowModal();
}

void show_substitutions_info(const ConfigSubstitutions& config_substitutions, const std::string& filename)
{
    // [INTENT] Similar migration feedback path for single configuration files.
    // [UNITY] Unity port should show the same message box but route through a `LocalizationService` + `DialogService`.
    wxString changes = "\n";
    add_config_substitutions(config_substitutions, changes);

    InfoDialog msg(nullptr,
                   format_wxstr(_L("Configuration file \"%1%\" was loaded, but some values were not recognized."), from_u8(filename)),
                   substitution_message(changes), true);
    msg.ShowModal();
}

// [INTENT] Turns a wxComboCtrl into a checklist so multiple boolean flags can share a single dropdown.
// [STATE] Holds the per-item flags inside the hidden popup list; `combochecklist_get_flags` reads them back as a bit mask.
// [EVENT] Binds to wxEVT_* events on the popup to capture when the user toggles selections.
// [THREAD] Must run on the UI/Main thread since combo control events may fire asynchronously from the render loop.
// [UNITY] Replace with a UI Toolkit `MultiSelect` `ListView` + `Toggle` rows mapping to ScriptableObject bool array and propagate toggles
// via `Binding`. [PORTING_HAZARD:P3] The Windows-specific sizing quirks (popup animation and mouse capture) have no direct Unity
// equivalent, so manual layout adjustments are needed.
void create_combochecklist(wxComboCtrl* comboCtrl, const std::string& text, const std::string& items)
{
    if (comboCtrl == nullptr)
        return;
    wxGetApp().UpdateDarkUI(comboCtrl);

    wxCheckListBoxComboPopup* popup = new wxCheckListBoxComboPopup;
    if (popup != nullptr) {
        // FIXME If the following line is removed, the combo box popup list will not react to mouse clicks.
        //  On the other side, with this line the combo box popup cannot be closed by clicking on the combo button on Windows 10.
        comboCtrl->UseAltPopupWindow();

        int max_width = 0;

        // the following line messes up the popup size the first time it is shown on wxWidgets 3.1.3
//		comboCtrl->EnablePopupAnimation(false);
#ifdef _WIN32
        popup->SetFont(comboCtrl->GetFont());
#endif // _WIN32
        comboCtrl->SetPopupControl(popup);
        wxString title = from_u8(text);
        max_width      = std::max(max_width, 60 + comboCtrl->GetTextExtent(title).x);
        popup->SetStringValue(title);
        popup->Bind(wxEVT_CHECKLISTBOX, [popup](wxCommandEvent& evt) { popup->OnCheckListBox(evt); });
        popup->Bind(wxEVT_LISTBOX, [popup](wxCommandEvent& evt) { popup->OnListBoxSelection(evt); });
        popup->Bind(wxEVT_KEY_DOWN, [popup](wxKeyEvent& evt) { popup->OnKeyEvent(evt); });
        popup->Bind(wxEVT_KEY_UP, [popup](wxKeyEvent& evt) { popup->OnKeyEvent(evt); });

        std::vector<std::string> items_str;
        boost::split(items_str, items, boost::is_any_of("|"), boost::token_compress_off);

        // each item must be composed by 2 parts
        assert(items_str.size() % 2 == 0);

        for (size_t i = 0; i < items_str.size(); i += 2) {
            wxString label = from_u8(items_str[i]);
            max_width      = std::max(max_width, 60 + popup->GetTextExtent(label).x);
            popup->Append(label);
            popup->Check(i / 2, items_str[i + 1] == "1");
        }

        comboCtrl->SetMinClientSize(wxSize(max_width, -1));
        wxGetApp().UpdateDarkUI(popup);
    }
}

// [STATE] Reads the checked state bits from the popup checklist as a bitmask so the owning dialog can save it into its model state.
// [UNITY] Map this to reading the `ListView.Selection` of toggles in Unity's UI Toolkit and compressing it into the same flag mask the
// legacy config expects.
unsigned int combochecklist_get_flags(wxComboCtrl* comboCtrl)
{
    unsigned int flags = 0;

    wxCheckListBoxComboPopup* popup = wxDynamicCast(comboCtrl->GetPopupControl(), wxCheckListBoxComboPopup);
    if (popup != nullptr) {
        for (unsigned int i = 0; i < popup->GetCount(); ++i) {
            if (popup->IsChecked(i))
                flags |= 1 << i;
        }
    }

    return flags;
}

// [EVENT] Invoked when a dialog restores saved flags, rechecking the matching entries to keep the UI in sync.
// [THREAD] Must run on the main thread (wxWidgets) because direct calls mutate combo list state.
void combochecklist_set_flags(wxComboCtrl* comboCtrl, unsigned int flags)
{
    wxCheckListBoxComboPopup* popup = wxDynamicCast(comboCtrl->GetPopupControl(), wxCheckListBoxComboPopup);
    if (popup != nullptr) {
        for (unsigned int i = 0; i < popup->GetCount(); ++i) {
            popup->Check(i, (flags & (1 << i)) != 0);
        }
    }
}

// [INTENT] Exposes the global AppConfig instance to utility functions that cannot rely on a specific window pointer.
// [STATE] This is a shallow accessor to keep modules from pulling the entire wxApp singleton directly.
// [UNITY] Replace with `AppState.Instance.Config` in Unity so the ScriptableObject asset stays in sync.
AppConfig* get_app_config() { return wxGetApp().app_config; }

// [INTENT] Converts UTF-8 strings from the backend into wxStrings that the GUI can show.
// [STATE] Serves as a key translation point for data flowing from config/preset files into text labels.
// [UNITY] A Unity port should ensure JSON/UTF-8 strings flow into `string` properties without reshaping.
wxString from_u8(const std::string& str) { return wxString::FromUTF8(str.c_str()); }

std::string into_u8(const wxString& str)
{
    auto buffer_utf8 = str.utf8_str();
    return std::string(buffer_utf8.data());
}

wxString from_path(const boost::filesystem::path& path)
{
#ifdef _WIN32
    return wxString(path.string<std::wstring>());
#else
    return from_u8(path.string<std::string>());
#endif
}

// [INTENT] Surface platform path conversion helpers so the GUI can feed wxNative paths into backend logic consistently.
// [UNITY] Replace with `System.IO.Path` helpers + `Application.persistentDataPath` rewriting.
boost::filesystem::path into_path(const wxString& str) { return boost::filesystem::path(str.wx_str()); }

// [INTENT] Shows the About dialog that summarizes the current app version and build metadata for the user.
// [UNITY] Map to a Unity `AboutPanel` that reads from `BuildInfo` stored in a ScriptableObject.
void about()
{
    AboutDialog dlg;
    dlg.ShowModal();
}

// [INTENT] Launches the bundled Web/Cloud login flow without stealing the caller's focus.
// [EVENT] Currently used by menus and machine connect flows that need to ensure authentication state.
// [UNITY] Replace with a `WebView` panel and coroutine-based `LoginFlow` service that hooks into Unity's `Authentication` module.
void login()
{
    // LoginDialog dlg;
    // dlg.ShowModal();

    ZUserLogin dlg;
    dlg.run();
}

void desktop_open_datadir_folder()
{
    // [INTENT] Launches the native file manager at the user's data directory so they can inspect configs/presets.
    // [EVENT] Bound to menu entries and toolbar shortcuts for quick access to logs/presets.
    // [PORTING_HAZARD:P2] Each platform uses different CLI commands and environment sanitization, so Unity must call `Application.OpenURL`
    // or `Process.Start` equivalents with platform guards.

    const auto path = data_dir();
#ifdef _WIN32
    const wxString widepath = from_u8(path);
    const wchar_t* argv[]   = {L"explorer", widepath.GetData(), nullptr};
    ::wxExecute(const_cast<wchar_t**>(argv), wxEXEC_ASYNC, nullptr);
#elif __APPLE__
    const char* argv[] = {"open", path.data(), nullptr};
    ::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr);
#else
    const char* argv[] = {"xdg-open", path.data(), nullptr};

    // Check if we're running in an AppImage container, if so, we need to remove AppImage's env vars,
    // because they may mess up the environment expected by the file manager.
    // Mostly this is about LD_LIBRARY_PATH, but we remove a few more too for good measure.
    if (wxGetEnv("APPIMAGE", nullptr)) {
        // We're running from AppImage
        wxEnvVariableHashMap env_vars;
        wxGetEnvMap(&env_vars);

        env_vars.erase("APPIMAGE");
        env_vars.erase("APPDIR");
        env_vars.erase("LD_LIBRARY_PATH");
        env_vars.erase("LD_PRELOAD");
        env_vars.erase("UNION_PRELOAD");

        wxExecuteEnv exec_env;
        exec_env.env = std::move(env_vars);

        wxString owd;
        if (wxGetEnv("OWD", &owd)) {
            // This is the original work directory from which the AppImage image was run,
            // set it as CWD for the child process:
            exec_env.cwd = std::move(owd);
        }

        ::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, &exec_env);
    } else {
        // Looks like we're NOT running from AppImage, we'll make no changes to the environment.
        ::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, nullptr);
    }
#endif
}

void desktop_open_any_folder(const std::string& path)
{
    // [INTENT] Opens any folder (often after Save/Export) in the platform file manager to show generated artifacts.
    // [STATE] Normalizes the path to the containing folder so on Linux and AppImage we don't try to open files directly.
    // [UNITY] Map to `Application.OpenURL` or `System.Diagnostics.Process.Start` after converting to `Application.dataPath` style and
    // dispatching on the main thread.

#ifdef _WIN32
    const wxString widepath = from_u8(path);
    ::wxExecute(L"explorer /select," + widepath, wxEXEC_ASYNC, nullptr);
#elif __APPLE__
    openFolderForFile(from_u8(path));
#else

    // Orca#6449: Open containing dir instead of opening the file directly.
    std::string             new_path = path;
    boost::filesystem::path p(new_path);
    if (!fs::is_directory(p)) {
        new_path = p.parent_path().string();
    }
    const char* argv[] = {"xdg-open", new_path.data(), nullptr};

    // Check if we're running in an AppImage container, if so, we need to remove AppImage's env vars,
    // because they may mess up the environment expected by the file manager.
    // Mostly this is about LD_LIBRARY_PATH, but we remove a few more too for good measure.
    if (wxGetEnv("APPIMAGE", nullptr)) {
        // We're running from AppImage
        wxEnvVariableHashMap env_vars;
        wxGetEnvMap(&env_vars);

        env_vars.erase("APPIMAGE");
        env_vars.erase("APPDIR");
        env_vars.erase("LD_LIBRARY_PATH");
        env_vars.erase("LD_PRELOAD");
        env_vars.erase("UNION_PRELOAD");

        wxExecuteEnv exec_env;
        exec_env.env = std::move(env_vars);

        wxString owd;
        if (wxGetEnv("OWD", &owd)) {
            // This is the original work directory from which the AppImage image was run,
            // set it as CWD for the child process:
            exec_env.cwd = std::move(owd);
        }

        ::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, &exec_env);
    } else {
        // Looks like we're NOT running from AppImage, we'll make no changes to the environment.
        ::wxExecute(const_cast<char**>(argv), wxEXEC_ASYNC, nullptr, nullptr);
    }
#endif
}

} // namespace GUI
} // namespace Slic3r
