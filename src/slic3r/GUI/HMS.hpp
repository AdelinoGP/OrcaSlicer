#ifndef slic3r_HMS_hpp_
#define slic3r_HMS_hpp_

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StepCtrl.hpp"
#include "BitmapCache.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Thread.hpp"
#include "nlohmann/json.hpp"
#include <mutex>

namespace Slic3r {

class MachineObject;

namespace GUI {

// [INTENT] Bridges remote HMS metadata into the GUI so dialogs can deliver device-specific help without re-querying the service on every
// failure. [UNITY] Equivalent to a singleton `ScriptableObject` that wraps `UnityWebRequest` downloads and caches `Texture2D` icons plus
// localized strings exposed to UI Toolkit popups. [PORTING_HAZARD:P2] Relies on synchronous `wxImage`/`wxString` state and blocking file
// I/O on the calling thread; Unity must replicate the cache with async/await and migrate textures to `Texture2D`/`Sprite` before pushing
// updates to the main loop.

// [STATE] These constants define the names of bundled JSON assets and query tags that the Unity importer will need to mirror (e.g.,
// Addressable labels or StreamingAssets paths).
// [UNITY] Make these the same string constants stored in a `ScriptableObject` so Unity can reference them when loading localization bundles
// or Addressables.
#define HMS_INFO_FILE "hms.json"
#define QUERY_HMS_INFO "query_hms_info"
#define QUERY_HMS_ACTION "query_hms_action"

// [INTENT] Wraps HMS metadata lookups so the GUI can ask for device-specific messages without re-requesting the cloud bundle.
// [UNITY] Map to a `ScriptableObject` controller that exposes the cache to UI Toolkit via `LocalizationTable`/`Sprite` data.
class HMSQuery
{
protected:
    std::unordered_map<string, json>
        m_hms_info_jsons; // [STATE] caches HMS info JSON per device prefix so repeated errors reuse the same bundle. [UNITY] Mirror this as
                          // a Dictionary<string, TextAsset> that drives localized data stored in a `ScriptableObject`.
    std::unordered_map<string, json>
        m_hms_action_jsons; // [STATE] caches HMS action JSON keyed by device prefix for each inquiry. [UNITY] Use the same Dictionary to
                            // populate Unity events that synthesize button descriptions.
    std::unordered_map<wxString, wxImage>
        m_hms_local_images;         // [STATE][PORTING_HAZARD:P2] stores wxImage icons that Unity must translate to Texture2D/Sprite via
                                    // Addressables, so keep the same key names for sprite lookups.
    mutable std::mutex m_hms_mutex; // [THREAD] guards caches when UI panels and background lookups run in parallel.

    std::unordered_map<string, time_t>
        m_cloud_hms_last_update_time; // [STATE] tracks the last cloud refresh per device type to throttle backend calls. [UNITY] Use
                                      // DateTime/Stopwatch based throttling around UnityWebRequest coroutines.

public:
    HMSQuery() {}
    ~HMSQuery() { clear_hms_info(); };

public:
    // [INTENT] Clears cached JSON/images/language data when the active device or error context switches.
    // [THREAD] Acquire `m_hms_mutex` while draining the caches so background downloads and UI dialogs cannot read half-evicted data.
    // [UNITY] Clear the ScriptableObject cache and fire an event that UI Toolkit dialogs can observe to refresh their localized text.
    void clear_hms_info();

    // [EVENT] Called by UI error handlers to map MachineObject or device-id + error-code into localized guidance.
    // [UNITY] Equivalent to `LocalizationTable.GetLocalizedString` followed by a `VisualElement` update on the main thread.
    wxString query_hms_msg(const MachineObject* obj, const std::string& long_error_code);
    wxString query_hms_msg(const std::string& dev_id, const std::string& long_error_code);

    // [EVENT] determines whether monitoring dialogs freeze or show hints.
    // [STATE] reads `m_hms_info_jsons` under `m_hms_mutex` so fatal vs recoverable states stay stable as new bundles arrive.
    // [THREAD] The UI thread and worker downloads both call this function, so the mutex ensures the cache is in a known state.
    // [PORTING_HAZARD:P2] Unity must replicate the detection logic in a `DiagnosticService` to avoid toggling hints mid-download.
    bool is_internal_error(const MachineObject* obj, int print_error);
    // [STATE] reuses cached JSON for the message text and only refreshes when a new bundle merge finishes.
    // [THREAD] Lock `m_hms_mutex` before reading so localization requests do not race the downloader.
    // [UNITY] Drive a `LocalizedStringEvent` in UI Toolkit using the same JSON data.
    wxString query_print_error_msg(const MachineObject* obj, int print_error);
    wxString query_print_error_msg(const std::string& dev_id, int print_error);
    // [EVENT] returns action button ids so UI can render clickable tips; callers pass an integer vector to surface the mapped buttons.
    // [STATE] The caller-owned `button_action` vector mirrors the cached JSON order so Unity can rehydrate the same button layout.
    // [PORTING_HAZARD:P3] Unity must translate the numeric IDs into `UnityEvent`/`VisualElement` callbacks instead of wx command IDs.
    // [UNITY] Map each action ID to a UI Toolkit `Button` + `ClickEvent` pair (or `Command` pattern) so the layout matches the original hints.
    wxString query_print_image_action(const MachineObject* obj, int print_error, std::vector<int>& button_action);

    // [STATE][PORTING_HAZARD:P3] Local icon lookup against stored wxImages; Unity must load the same atlas/sprites via Addressables.
    // [UNITY] Load the same textures through Addressables or `Resources.Load<Texture2D>` and cache them in a `Dictionary<string, Sprite>`.
    wxImage query_image_from_local(const wxString& image_name);

public:
    static std::string hms_language_code();
    static std::string build_query_params(std::string& lang);

private:
    // [INTENT] Ensures the JSON bundle for a given device type exists by loading/copying/downloading it so queries can proceed offline.
    // [UNITY] Kick off an async Addressables/TextAsset load before a dialog attempts to read the data.
    void init_hms_info(const std::string& dev_type_id);
    // [PORTING_HAZARD:P3] Moves resources from the bundled data dir into the user's local area; Unity needs StreamingAssets + persistent
    // data equivalents.
    // [UNITY] Copy `StreamingAssets` JSON to `Application.persistentDataPath` (or use Addressables) if the bundle is missing.
    void copy_from_data_dir_to_local();
    // [THREAD][PORTING_HAZARD:P2] Hits the HMS service to refresh JSON by hms_type and dev_id_type; Unity should run this in a coroutine
    // with cancellation.
    // [UNITY] Mirror with `UnityWebRequest` coroutines + `CancellationTokenSource` so the UI thread remains responsive.
    int download_hms_related(const std::string& hms_type, const std::string& dev_id_type, json* receive_json);
    // [STATE] Reads disk copies of the HMS JSON into the in-memory maps.
    // [UNITY] Parse `TextAsset` contents fetched from `StreamingAssets` or `Addressables` before deserializing to `Dictionary`.
    int load_from_local(const std::string& hms_type, const std::string& dev_id_type, json* receive_json, std::string& version_info);
    // [STATE][PORTING_HAZARD:P3] Persists merged JSON back to disk, including language/version metadata.
    // [UNITY] Use `File.WriteAllText(Path.Combine(Application.persistentDataPath, ...))` and update the `ScriptableObject` copy.
    int save_to_local(std::string lang, std::string hms_type, std::string dev_id_type, json save_json);
    // [STATE] Resolves the file path for a given HMS type/language combination for reuse in Unity assets.
    // [UNITY] Build the path with `Application.streamingAssetsPath` so Unity logs can locate the same bundle.
    std::string get_hms_file(std::string hms_type, std::string lang = std::string("en"), std::string dev_id_type = "");

    // [STATE] Helper that translates a `MachineObject` into the serial-prefix key used by the cache maps.
    // [UNITY] Pull the key from Unity's `PrinterDevice` metadata before hitting the `Dictionary`.
    string get_dev_id_type(const MachineObject* obj) const;
    // [STATE] Reads from the cached maps and fallback language strings to return dialog text.
    // [UNITY] The fallback logic should feed into Unity's `Localization` package to keep text consistent.
    wxString _query_hms_msg(const string& dev_id_type, const string& long_error_code, const string& lang_code = std::string("en"));

    bool _is_internal_error(const string& dev_id_type, const string& long_error_code, const string& lang_code = std::string("en"));
    // [UNITY] Hook into Unity's `DiagnosticService` to suppress or surface errors uniquely on the main thread.
    wxString _query_error_msg(const string&      dev_id_type,
                              const std::string& long_error_code,
                              const std::string& lang_code = std::string("en"));
    // [PORTING_HAZARD:P3] Converts codes into button/action hints for wx dialogs; Unity must map these IDs to Button callbacks.
    // [UNITY] Map hints to `[UnityEvent]`/`VisualElement` button callbacks provided by a `HmsActionController`.
    wxString _query_error_image_action(const string& dev_id_type, const std::string& long_error_code, std::vector<int>& button_action);
};

// [INTENT] Records the version of the local HMS bundle so the Unity importer can detect stale metadata.
// [STATE] The version counter guards downloads so we only refresh when the asset changes.
// [PORTING_HAZARD:P3] Unity has to keep the ScriptableObject copy in sync with the cached JSON to avoid stale information.
// [UNITY] Surface the version on a `ScriptableObject` asset so the Unity loader can compare it to the embedded data.
int get_hms_info_version(std::string& version);

// [INTENT] Returns the knowledge-base URL for a given HMS code so GUI panels can link into the help center.
// [EVENT] Invoked by help buttons, so the Unity adapter must raise a `VisualElement` click event that calls `Application.OpenURL`.
// [UNITY] Feed this URL into `Application.OpenURL` for UI Toolkit dialogs wired to the help button.
// [PORTING_HAZARD:P3] Help URLs must be resolved off the main thread before dispatching to Unity's UI to prevent blocking.
std::string get_hms_wiki_url(std::string code);

// [EVENT] Provides fallback error text when no HMS entry exists for the requested code.
// [STATE] The fallback map lives in the localization catalog and must stay aligned with `get_error_message` outputs.
// [PORTING_HAZARD:P3] Unity needs to track missing-code telemetry instead of relying on wxWidgets defaults.
// [UNITY] The fallback text should be stored in a Unity `LocalizationTable` entry used by dialog tooltips.
std::string get_error_message(int error_code);

} // namespace GUI
} // namespace Slic3r

#endif
