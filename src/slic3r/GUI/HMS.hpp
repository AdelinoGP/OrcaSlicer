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
#define HMS_INFO_FILE "hms.json"
#define QUERY_HMS_INFO "query_hms_info"
#define QUERY_HMS_ACTION "query_hms_action"

// [INTENT] Wraps HMS metadata lookups so the GUI can ask for device-specific messages without re-requesting the cloud bundle.
// [UNITY] Map to a `ScriptableObject` controller that exposes the cache to UI Toolkit via `LocalizationTable`/`Sprite` data.
class HMSQuery
{
protected:
    std::unordered_map<string, json>
        m_hms_info_jsons; // [STATE] caches HMS info JSON per device prefix so repeated errors reuse the same bundle.
    std::unordered_map<string, json> m_hms_action_jsons; // [STATE] caches HMS action JSON keyed by device prefix for each inquiry.
    std::unordered_map<wxString, wxImage>
        m_hms_local_images; // [STATE][PORTING_HAZARD:P2] stores wxImage icons that Unity must translate to Texture2D/Sprite via Addressables.
    mutable std::mutex m_hms_mutex; // [THREAD] guards caches when UI panels and background lookups run in parallel.

    std::unordered_map<string, time_t>
        m_cloud_hms_last_update_time; // [STATE] tracks the last cloud refresh per device type to throttle backend calls.

public:
    HMSQuery() {}
    ~HMSQuery() { clear_hms_info(); };

public:
    // [INTENT] Clears cached JSON/images/language data when the active device or error context switches.
    void clear_hms_info();

    // [EVENT] Called by UI error handlers to map MachineObject or device-id + error-code into localized guidance.
    wxString query_hms_msg(const MachineObject* obj, const std::string& long_error_code);
    wxString query_hms_msg(const std::string& dev_id, const std::string& long_error_code);

    bool     is_internal_error(const MachineObject* obj,
                               int                  print_error); // [EVENT] determines whether monitoring dialogs freeze or show hints.
    wxString query_print_error_msg(const MachineObject* obj, int print_error); // [STATE] reuses cached JSON for the message text.
    wxString query_print_error_msg(const std::string& dev_id, int print_error);
    wxString query_print_image_action(const MachineObject* obj,
                                      int                  print_error,
                                      std::vector<int>& button_action); // [EVENT] returns action button ids so UI can render clickable tips.

    // [STATE][PORTING_HAZARD:P3] Local icon lookup against stored wxImages; Unity must load the same atlas/sprites via Addressables.
    wxImage query_image_from_local(const wxString& image_name);

public:
    static std::string hms_language_code();
    static std::string build_query_params(std::string& lang);

private:
    // [INTENT] Ensures the JSON bundle for a given device type exists by loading/copying/downloading it so queries can proceed offline.
    void init_hms_info(const std::string& dev_type_id);
    // [PORTING_HAZARD:P3] Moves resources from the bundled data dir into the user's local area; Unity needs StreamingAssets + persistent
    // data equivalents.
    void copy_from_data_dir_to_local();
    // [THREAD][PORTING_HAZARD:P2] Hits the HMS service to refresh JSON by hms_type and dev_id_type; Unity should run this in a coroutine
    // with cancellation.
    int download_hms_related(const std::string& hms_type, const std::string& dev_id_type, json* receive_json);
    // [STATE] Reads disk copies of the HMS JSON into the in-memory maps.
    int load_from_local(const std::string& hms_type, const std::string& dev_id_type, json* receive_json, std::string& version_info);
    // [STATE][PORTING_HAZARD:P3] Persists merged JSON back to disk, including language/version metadata.
    int save_to_local(std::string lang, std::string hms_type, std::string dev_id_type, json save_json);
    // [STATE] Resolves the file path for a given HMS type/language combination for reuse in Unity assets.
    std::string get_hms_file(std::string hms_type, std::string lang = std::string("en"), std::string dev_id_type = "");

    // [STATE] Helper that translates a `MachineObject` into the serial-prefix key used by the cache maps.
    string get_dev_id_type(const MachineObject* obj) const;
    // [STATE] Reads from the cached maps and fallback language strings to return dialog text.
    wxString _query_hms_msg(const string& dev_id_type, const string& long_error_code, const string& lang_code = std::string("en"));

    bool     _is_internal_error(const string& dev_id_type, const string& long_error_code, const string& lang_code = std::string("en"));
    wxString _query_error_msg(const string&      dev_id_type,
                              const std::string& long_error_code,
                              const std::string& lang_code = std::string("en"));
    // [PORTING_HAZARD:P3] Converts codes into button/action hints for wx dialogs; Unity must map these IDs to Button callbacks.
    wxString _query_error_image_action(const string& dev_id_type, const std::string& long_error_code, std::vector<int>& button_action);
};

// [INTENT] Records the version of the local HMS bundle so the Unity importer can detect stale metadata.
int get_hms_info_version(std::string& version);

// [INTENT] Returns the knowledge-base URL for a given HMS code so GUI panels can link into the help center.
std::string get_hms_wiki_url(std::string code);

// [EVENT] Provides fallback error text when no HMS entry exists for the requested code.
std::string get_error_message(int error_code);

} // namespace GUI
} // namespace Slic3r

#endif
