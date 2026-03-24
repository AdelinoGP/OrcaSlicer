#include "HMS.hpp"

#include "DeviceManager.hpp"
#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevUtil.h"

#include <boost/log/trivial.hpp>

static const char* HMS_PATH           = "hms";
static const char* HMS_LOCAL_IMG_PATH = "hms/local_image";

// [STATE] The local HMS info holds preloaded device prefixes so we only copy static assets for known bundles.
static unordered_set<string> package_dev_id_types{"094", "239", "093", "22E"};

namespace Slic3r { namespace GUI {

// [INTENT] Coordinate HMS (Host Machine Support) message acquisition, local caching, and device-specific lookups to drive the error hint
// layer. [STATE] Global caches such as `m_hms_info_jsons`, `m_hms_action_jsons`, and `m_hms_local_images` back the per-device workflows
// documented here. [UNITY] Unity port should embed this logic in a ScriptableObject-backed HMSService with async UnityWebRequest jobs
// updating main-thread state. [PORTING_HAZARD:P2] Relies on wxWidgets globals (`wxGetApp()`, AppConfig) and the nonstandard `Slic3r::Http`
// helper, so the port must provide analogous dependency injection hooks.

int get_hms_info_version(std::string& version)
{
    // [STATE] Guard read-only config state (AppConfig, stealth mode) before contacting HMS.
    // [THREAD] Uses `Slic3r::Http` synchronously so the calling UI thread blocks until the request completes; [UNITY] dispatch an async
    // UnityWebRequest plus a main-thread continuation before updating UI; [PORTING_HAZARD:P3] Any `boost::format` or
    // `HMSQuery::build_query_params` expectations must be replaced with platform-standard URL builders.
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return -1;
    if (config->get_stealth_mode())
        return -1;
    std::string hms_host = config->get_hms_host();
    if (hms_host.empty()) {
        BOOST_LOG_TRIVIAL(error) << "hms_host is empty";
        return -1;
    }
    int result = -1;
    version    = "";
    std::string  lang;
    std::string  query_params = HMSQuery::build_query_params(lang);
    std::string  url          = (boost::format("https://%1%/GetVersion.php?%2%") % hms_host % query_params).str();
    Slic3r::Http http         = Slic3r::Http::get(url);
    http.timeout_max(10)
        .on_complete([&result, &version](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                if (j.contains("ver")) {
                    version = DevJsonValParser::get_longlong_val(j["ver"]);
                }
            } catch (...) {
                ;
            }
        })
        .on_error([&result](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "get_hms_info_version: body = " << body << ", status = " << status << ", error = " << error;
            result = -1;
        })
        .perform_sync();
    return result;
}

// Note:  Download the HMS payload of the requested type into receive_json, merging cloud/local caches.
// [INTENT] Synchronize remote HMS info/action bundles with local JSON caches so UI helpers can read device-specific messages offline.
// [STATE] Tracks `local_version`, `lang`, and `to_save_local` to avoid redundant writes and include query parameters.
// [EVENT] HTTP completion/error handlers mutate `receive_json` and log/propagate diagnostic states.
// [THREAD] `perform_sync()` blocks the caller; [UNITY] wrap this in a UnityWebRequest coroutine (with retries) while marshaling results back
// to main-thread state. [PORTING_HAZARD:P2] Relies on `wxGetApp()` for config and `json` layout assumptions (e.g., `result`, `data`, `version`).
int HMSQuery::download_hms_related(const std::string& hms_type, const std::string& dev_id_type, json* receive_json)
{
    std::string local_version = "0";
    load_from_local(hms_type, dev_id_type, receive_json, local_version);

    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return -1;
    if (config->get_stealth_mode())
        return -1;

    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang;
    std::string query_params = HMSQuery::build_query_params(lang);
    std::string url;
    if (hms_type.compare(QUERY_HMS_INFO) == 0) {
        url = (boost::format("https://%1%/query.php?%2%") % hms_host % query_params).str();
    } else if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
        url = (boost::format("https://%1%/hms/GetActionImage.php?") % hms_host).str();
    }

    if (!local_version.empty()) {
        url += (url.find('?') != std::string::npos ? "&" : "?") + (boost::format("v=%1%") % local_version).str();
    }

    if (!dev_id_type.empty()) {
        url += (url.find('?') != std::string::npos ? "&" : "?") + (boost::format("d=%1%") % dev_id_type).str();
    }

    bool to_save_local = false;
    json j;

    BOOST_LOG_TRIVIAL(info) << "hms: download url = " << url;
    Slic3r::Http http = Slic3r::Http::get(url);
    http.on_complete([this, receive_json, hms_type, &to_save_local, &j, &local_version](std::string body, unsigned status) {
            try {
                j = json::parse(body);
                if (j.contains("result")) {
                    if (j["result"] == 0 && j.contains("data")) {
                        if (!j.contains("ver")) {
                            return;
                        }

                        const std::string& remote_ver = DevJsonValParser::get_longlong_val(j["ver"]);
                        if (remote_ver <= local_version) {
                            return;
                        }
                        (*receive_json)["version"] = remote_ver;

                        if (hms_type.compare(QUERY_HMS_INFO) == 0) {
                            (*receive_json) = j["data"];
                            to_save_local   = true;
                        } else if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
                            (*receive_json)["data"] = j["data"];
                            to_save_local           = true;
                        }
                    } else if (j["result"] == 201) {
                        BOOST_LOG_TRIVIAL(info) << "HMSQuery: HMS info is the latest version";
                    } else {
                        BOOST_LOG_TRIVIAL(info) << "HMSQuery: update hms info error = " << j["result"].get<int>();
                    }
                }
            } catch (...) {
                ;
            }
        })
        .timeout_max(20)
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "HMSQuery: update hms info error = " << error << ", body = " << body << ", status = " << status;
        })
        .perform_sync();

    if (to_save_local && !receive_json->empty()) {
        save_to_local(lang, hms_type, dev_id_type, j);
    }
    return 0;
}

static void _copy_dir(const fs::path& from_dir, const fs::path& to_dir) /* copy and override with local files*/
{
    // [INTENT] Mirror shipped HMS assets into the local data directory so the offline cache can be seeded when network downloads fail.
    // [THREAD] Recursive filesystem walks run synchronously; [UNITY] run this work in a background Task/Job that copies StreamingAssets to
    // Application.persistentDataPath; [PORTING_HAZARD:P3] blocking filesystem work on the main thread will stutter Unity's UI thread.
    try {
        if (!fs::exists(from_dir)) {
            return;
        }

        if (!fs::exists(to_dir)) {
            fs::create_directory(to_dir);
        }

        for (const auto& entry : fs::directory_iterator(from_dir)) {
            const fs::path& source_path   = entry.path();
            const fs::path& relative_path = fs::relative(source_path, from_dir);
            const fs::path& dest_path     = to_dir / relative_path;

            if (fs::is_regular_file(source_path)) {
                if (fs::exists(dest_path)) {
                    fs::remove(dest_path);
                }

                copy_file(source_path, dest_path);
            } else if (fs::is_directory(source_path)) {
                _copy_dir(source_path, dest_path);
            }
        }
    } catch (...) {}
}

void HMSQuery::copy_from_data_dir_to_local()
{
    const fs::path& from_dir = fs::path(Slic3r::resources_dir()) / HMS_PATH;
    const fs::path& to_dir   = fs::path(Slic3r::data_dir()) / HMS_PATH;
    // [INTENT] Seed the writable data directory with packaged HMS JSON so offline lookups have starting data.
    // [UNITY] Similar to copying StreamingAssets/HMS into Application.persistentDataPath on first run.
    _copy_dir(from_dir, to_dir);
}

int HMSQuery::load_from_local(const std::string& hms_type, const std::string& dev_id_type, json* load_json, std::string& load_version)
{
    // [STATE] Reads and parses cached HMS JSON, populating `load_json` and the per-device version string; watches for missing dirs and
    // resets to "0" on failure. [UNITY] Mirror this with Unity's JsonUtility + File IO under Application.persistentDataPath, exposing
    // version data to the HMSService. [PORTING_HAZARD:P2] Relies on `data_dir()`/`boost::filesystem` and `DevJsonValParser`, so the Unity
    // version needs safe path helpers and error handling around JSON structure.
    if (data_dir().empty()) {
        load_version = "0";
        BOOST_LOG_TRIVIAL(error) << "HMS: load_from_local, data_dir() is empty";
        return -1;
    }
    std::string filename   = get_hms_file(hms_type, HMSQuery::hms_language_code(), dev_id_type);
    auto        hms_folder = (boost::filesystem::path(data_dir()) / "hms");
    if (!fs::exists(hms_folder))
        fs::create_directory(hms_folder);

    std::string   dir_str = (hms_folder / filename).make_preferred().string();
    std::ifstream json_file(encode_path(dir_str.c_str()));
    try {
        if (json_file.is_open()) {
            const json& j = json::parse(json_file);
            if (hms_type.compare(QUERY_HMS_INFO) == 0) {
                if (j.contains("data")) {
                    (*load_json) = j["data"];
                }
            } else if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
                if (j.contains("data")) {
                    (*load_json)["data"] = j["data"];
                }
            }

            if (j.contains("version")) {
                load_version = DevJsonValParser::get_longlong_val(j["version"]);
            } else if (j.contains("ver")) {
                load_version = DevJsonValParser::get_longlong_val(j["ver"]);
            } else {
                BOOST_LOG_TRIVIAL(warning) << "HMS: load_from_local, no version info";
            }

            return 0;
        }
    } catch (...) {
        load_version = "0";
        BOOST_LOG_TRIVIAL(error) << "HMS: load_from_local failed";
        return -1;
    }
    load_version = "0";
    return 0;
}

int HMSQuery::save_to_local(std::string lang, std::string hms_type, std::string dev_id_type, json save_json)
{
    // [INTENT] Serialize the HMS JSON to persistent storage so repeated lookups jumpstart from the latest cloud data.
    // [EVENT] Called lazily after new data arrives, so replicating this in Unity means invoking a File Write job immediately after the
    // WebRequest completes.
    // [UNITY] Use `File.WriteAllText` (or `UnityWebRequest` + `File.WriteAllBytes` on a background Task) under
    // `Application.persistentDataPath` so the cached JSON mirrors the desktop layout; [PORTING_HAZARD:P3] guard against missing directories
    // and blocked I/O on the main thread.
    if (data_dir().empty()) {
        BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local, data_dir() is empty";
        return -1;
    }
    std::string filename   = get_hms_file(hms_type, lang, dev_id_type);
    auto        hms_folder = (boost::filesystem::path(data_dir()) / "hms");
    if (!fs::exists(hms_folder))
        fs::create_directory(hms_folder);
    std::string   dir_str = (hms_folder / filename).make_preferred().string();
    std::ofstream json_file(encode_path(dir_str.c_str()));
    if (json_file.is_open()) {
        json_file << std::setw(4) << save_json << std::endl;
        json_file.close();
        return 0;
    }
    BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local failed";
    return -1;
}

std::string HMSQuery::hms_language_code()
{
    // [STATE] Normalize the current language code (fallback to English for unsupported locales or stealth mode). Unity should mirror this
    // with Locales from PlayerSettings.
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        // set language code to en by default
        return "en";
    std::string lang_code = wxGetApp().app_config->get_language_code();
    if (lang_code.compare("uk") == 0 || lang_code.compare("cs") == 0 || lang_code.compare("ru") == 0) {
        BOOST_LOG_TRIVIAL(info) << "HMS: using english for lang_code = " << lang_code;
        return "en";
    } else if (lang_code.empty()) {
        // set language code to en by default
        return "en";
    }
    return lang_code;
}

std::string HMSQuery::build_query_params(std::string& lang)
{
    std::string lang_code = HMSQuery::hms_language_code();
    // [STATE] Echo the resolved language back via `lang` so callers can persist it when writing JSON files.
    // [UNITY] Mirror this logic by reading `LocalizationSettings.SelectedLocale` before caching the value in Service state so the
    // downstream cache files stay consistent.
    lang                     = lang_code;
    std::string query_params = (boost::format("lang=%1%") % lang_code).str();
    return query_params;
}

std::string HMSQuery::get_hms_file(std::string hms_type, std::string lang, std::string dev_id_type)
{
    // [INTENT] Map HMS type/dev_id to the correct local filename so caching/saving uses predictable paths for Unity's Resource cache.
    // [PORTING_HAZARD:P3] Unity ports must treat `dev_id_type` prefixes as the key for HMS lookups instead of relying on wxWidgets-specific
    // logic. [UNITY] Mirror these filenames inside a ScriptableObject table so Unity caches reference paths under similar keys.
    if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
        return (boost::format("hms_action_%1%.json") % dev_id_type).str();
    }
    // return hms filename
    return (boost::format("hms_%1%_%2%.json") % lang % dev_id_type).str();
}

// [INTENT] Find localized HMS intro text for the device attached to `obj` using cached JSON and the long error code.
// [STATE] Relies on `m_hms_info_jsons` being primed via `init_hms_info` to avoid repeated downloads.
// [UNITY] Unity should expose this as HMSService.GetMessage(deviceId, errorCode) that queries a ScriptableObject-backed dictionary.
wxString HMSQuery::query_hms_msg(const MachineObject* obj, const std::string& long_error_code)
{
    if (!obj) {
        return wxEmptyString;
    }

    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return wxEmptyString;
    const std::string& lang_code = HMSQuery::hms_language_code();
    return _query_hms_msg(get_dev_id_type(obj), long_error_code, lang_code);
}

// [INTENT] Same lookup path but keyed directly by `dev_id` when no MachineObject is available.
wxString HMSQuery::query_hms_msg(const std::string& dev_id, const std::string& long_error_code)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return wxEmptyString;
    const std::string& lang_code = HMSQuery::hms_language_code();
    return _query_hms_msg(dev_id.substr(0, 3), long_error_code, lang_code);
}

string HMSQuery::get_dev_id_type(const MachineObject* obj) const
{
    // [STATE] Returns the three-digit prefix that the cloud HMS JSON uses to index device metadata.
    if (obj) {
        return obj->get_dev_id().substr(0, 3);
    }

    return string();
}

wxString HMSQuery::_query_hms_msg(const string& dev_id_type, const string& long_error_code, const string& lang_code)
{
    // [INTENT] Use cached JSON to match `long_error_code` to localized intro text, logging gaps for debugging.
    // [STATE] `m_hms_info_jsons[dev_id_type]` is refreshed via `init_hms_info`, so missing keys log errors.
    // [UNITY] Cache these dictionaries inside `HmsService` and expose `GetMessage` so UI code always reads from the warmed map before
    // falling back to downloads.
    if (long_error_code.empty()) {
        return wxEmptyString;
    }

    init_hms_info(dev_id_type);
    auto iter = m_hms_info_jsons.find(dev_id_type);
    if (iter == m_hms_info_jsons.end()) {
        BOOST_LOG_TRIVIAL(error) << "there are no hms info for the device";
        return wxEmptyString;
    }

    const json& m_hms_info_json = iter->second;
    if (!m_hms_info_json.is_object()) {
        BOOST_LOG_TRIVIAL(error) << "the hms info is not a valid json object";
        return wxEmptyString;
    }

    const json& device_hms_json = m_hms_info_json.value("device_hms", json());
    if (device_hms_json.is_null() || !device_hms_json.is_object()) {
        BOOST_LOG_TRIVIAL(error) << "there are no valid json object named device_hms";
        return wxEmptyString;
    }

    const json& device_hms_msg_json = device_hms_json.value(lang_code, json());
    if (device_hms_msg_json.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "hms: query_hms_msg, do not contains lang_code = " << lang_code;
        if (lang_code.empty()) /*traverse all if lang_code is empty*/
        {
            for (const auto& lang_item : device_hms_json) {
                for (const auto& msg_item : lang_item) {
                    if (msg_item.is_object()) {
                        const std::string& error_code = msg_item.value("ecode", json()).get<std::string>();
                        if (boost::to_upper_copy(error_code) == long_error_code && msg_item.contains("intro")) {
                            BOOST_LOG_TRIVIAL(info) << "retry without lang_code successed.";
                            return wxString::FromUTF8(msg_item["intro"].get<std::string>());
                        }
                    }
                }
            }
        }

        return wxEmptyString;
    }

    for (const auto& item : device_hms_msg_json) {
        if (item.is_object()) {
            const std::string& error_code = item.value("ecode", json()).get<std::string>();
            if (boost::to_upper_copy(error_code) == long_error_code && item.contains("intro")) {
                return wxString::FromUTF8(item["intro"].get<std::string>());
            }
        }
    }

    BOOST_LOG_TRIVIAL(error) << "hms: query_hms_msg, do not contains valid message, lang_code = " << lang_code
                             << " long_error_code = " << long_error_code;
    return wxEmptyString;
}

bool HMSQuery::_is_internal_error(const string& dev_id_type, const string& error_code, const string& lang_code)
{
    // [INTENT] Mark certain errors as "internal" so UI layers skip extra HMS dialog processing.
    // [STATE] The cache in `m_hms_info_jsons` drives this boolean so once a dev_id/error pair is flagged, the UI avoids extra dialogs.
    // [UNITY] Unity should expose this as `HmsService.IsInternalError(devId, errorCode)` backed by the cached dictionary before showing HMS
    // guidance. [PORTING_HAZARD:P3] The hex-string path assumes zero-padded `sprintf` output; replicate that formatting when translating
    // the print error conversion.
    init_hms_info(dev_id_type);
    auto iter = m_hms_info_jsons.find(dev_id_type);
    if (iter == m_hms_info_jsons.end()) {
        return false;
    }

    const json& m_hms_info_json = iter->second;
    if (m_hms_info_json.contains("device_error")) {
        if (m_hms_info_json["device_error"].contains(lang_code)) {
            for (auto item = m_hms_info_json["device_error"][lang_code].begin(); item != m_hms_info_json["device_error"][lang_code].end();
                 item++) {
                if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == error_code) {
                    if (item->contains("intro")) {
                        return wxString::FromUTF8((*item)["intro"].get<std::string>()).IsEmpty();
                    }
                }
            }
        } else {
            // return first language
            if (!m_hms_info_json["device_error"].empty()) {
                for (auto lang : m_hms_info_json["device_error"]) {
                    for (auto item = lang.begin(); item != lang.end(); item++) {
                        if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == error_code) {
                            if (item->contains("intro")) {
                                return wxString::FromUTF8((*item)["intro"].get<std::string>()).IsEmpty();
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

wxString HMSQuery::_query_error_msg(const std::string& dev_id_type, const std::string& error_code, const std::string& lang_code)
{
    // [INTENT] Look up a localized error message using cached device maps; fall back to logging when the lang_code is missing.
    // [STATE] Relies on the warmed `m_hms_info_jsons` cache so repeated error queries stay fast and predictable across lang choices.
    // [UNITY] Unity should provide `HmsService.GetErrorMessage(devId, code)` that wraps this dictionary and returns a `string` for UI
    // labels. [PORTING_HAZARD:P2] Mirrors a `nlohmann::json` structure (device_error/lang/ecode) so the port must preserve the same
    // hierarchy or the lookup silently fails.
    init_hms_info(dev_id_type);
    auto iter = m_hms_info_jsons.find(dev_id_type);
    if (iter == m_hms_info_jsons.end()) {
        return wxEmptyString;
    }

    const json& m_hms_info_json = iter->second;
    if (m_hms_info_json.contains("device_error")) {
        if (m_hms_info_json["device_error"].contains(lang_code)) {
            for (auto item = m_hms_info_json["device_error"][lang_code].begin(); item != m_hms_info_json["device_error"][lang_code].end();
                 item++) {
                if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == error_code) {
                    if (item->contains("intro")) {
                        return wxString::FromUTF8((*item)["intro"].get<std::string>());
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "hms: query_error_msg, not found error_code = " << error_code;
        } else {
            BOOST_LOG_TRIVIAL(error) << "hms: query_error_msg, do not contains lang_code = " << lang_code;
            // return first language
            if (!m_hms_info_json["device_error"].empty()) {
                for (auto lang : m_hms_info_json["device_error"]) {
                    for (auto item = lang.begin(); item != lang.end(); item++) {
                        if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == error_code) {
                            if (item->contains("intro")) {
                                return wxString::FromUTF8((*item)["intro"].get<std::string>());
                            }
                        }
                    }
                }
            }
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "device_error is not exists";
        return wxEmptyString;
    }

    return wxEmptyString;
}

wxString HMSQuery::_query_error_image_action(const std::string& dev_id_type,
                                             const std::string& long_error_code,
                                             std::vector<int>&  button_action)
{
    // [INTENT] Fetch the HMS button actions and optional image for a device error so UI can render suggested fixes.
    // [STATE] Reads `m_hms_action_jsons`, which is refreshed in `init_hms_info`, so the cache must be warmed prior to calling this.
    // [EVENT] Fills `button_action` so the higher-level widget can show actionable buttons alongside the image.
    // [UNITY] Port this as `HmsService.GetActionImage` that returns a `Sprite` plus a `List<int>` for Unity UI buttons and optionally runs
    // `MainThreadDispatcher` updates. [PORTING_HAZARD:P3] The `json` structure (data/ecode/device/actions/image) must stay aligned;
    // stripping any key may drop an action silently.
    init_hms_info(dev_id_type);

    auto iter = m_hms_action_jsons.find(dev_id_type);
    if (iter == m_hms_action_jsons.end()) {
        return wxEmptyString;
    }

    const json& m_hms_action_json = iter->second;
    if (m_hms_action_json.contains("data")) {
        for (auto item = m_hms_action_json["data"].begin(); item != m_hms_action_json["data"].end(); item++) {
            if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == long_error_code) {
                if (item->contains("device") && (boost::to_upper_copy((*item)["device"].get<std::string>()) == dev_id_type ||
                                                 (*item)["device"].get<std::string>() == "default")) {
                    if (item->contains("actions")) {
                        for (auto item_actions = (*item)["actions"].begin(); item_actions != (*item)["actions"].end(); item_actions++) {
                            button_action.emplace_back(item_actions->get<int>());
                        }
                    }
                    if (item->contains("image")) {
                        return wxString::FromUTF8((*item)["image"].get<std::string>());
                    }
                }
            }
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "data is not exists";
        return wxEmptyString;
    }
    return wxEmptyString;
}

bool HMSQuery::is_internal_error(const MachineObject* obj, int print_error)
{
    // [INTENT] Hex-encode `print_error` before delegating to `_is_internal_error`, keeping the API convenient for print-error callers.
    // [EVENT] Invoked by print-error reporting so the UI can guard against duplicate dialogs.
    // [UNITY] Wrap in `HmsService.ReportPrintError` and dispatch results via `UnityMainThreadDispatcher` before showing HMS hints.
    // [PORTING_HAZARD:P3] The synchronous `sprintf` assumes an 8-digit hex string; keep the format stable if the error space grows.
    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    std::string lang_code = HMSQuery::hms_language_code();
    return _is_internal_error(get_dev_id_type(obj), std::string(buf), lang_code);
}

// [INTENT] Convert a print error to localized text so UI layers can label errors without hardcoding mappings.
// [EVENT] Called directly by print error dialogs so the returned string can populate the HMS hint label.
// [UNITY] Map this to `HmsService.QueryPrintError` and cache the string in a ScriptableObject keyed by device and hex error.
// [PORTING_HAZARD:P3] Synchronous `sprintf` + `json` lookups run on the caller thread; Unity should reuse cached `HmsInfo` to prevent UI blocks.
wxString HMSQuery::query_print_error_msg(const MachineObject* obj, int print_error)
{
    if (!obj) {
        return wxEmptyString;
    }

    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    std::string lang_code = HMSQuery::hms_language_code();
    return _query_error_msg(get_dev_id_type(obj), std::string(buf), lang_code);
}

// [INTENT] Provide a dev_id-based variant for contexts where no MachineObject pointer is available (e.g., launchers or logs).
// [EVENT] Used by log analysis or background checks where the UI still needs the localized error tip.
// [UNITY] Mirror this call with a lightweight helper that passes the stored dev_id string through Unity's `HmsService` APIs.
// [PORTING_HAZARD:P3] Reuses the same `sprintf` path as the MachineObject overload so keep their formatting in sync to avoid mismatched
// cache keys.
wxString HMSQuery::query_print_error_msg(const std::string& dev_id, int print_error)
{
    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    std::string lang_code = HMSQuery::hms_language_code();
    return _query_error_msg(dev_id.substr(0, 3), std::string(buf), lang_code);
}

wxString HMSQuery::query_print_image_action(const MachineObject* obj, int print_error, std::vector<int>& button_action)
{
    // [INTENT] Retrieve HMS action images + button IDs so dialogs can show visual guidance and actionable steps.
    // [STATE] Reads both `m_hms_action_jsons` and the `stealth_mode` flag to decide whether an image is safe to show.
    // [EVENT] Feeds image names and `button_action` codes back to the caller so the UI can render the HMS suggestions immediately.
    // [UNITY] Unity should return a `Sprite` plus `List<int>` and handle `stealth_mode` via `PlayerPrefs` before presenting online-hosted
    // assets.
    // [PORTING_HAZARD:P3] This path assumes `wxGetApp().app_config` exists and that image names resolve to local files; Unity must
    // guard against null configs and missing textures.
    if (!obj) {
        return wxEmptyString;
    }

    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    // The first three digits of SN number
    const auto result = _query_error_image_action(get_dev_id_type(obj), std::string(buf), button_action);
    if (wxGetApp().app_config->get_stealth_mode() && result.Contains("http")) {
        return wxEmptyString;
    }
    return result;
}

wxImage HMSQuery::query_image_from_local(const wxString& image_name)
{
    // [INTENT] Return cached HMS images from the local folder; `m_hms_local_images` lazily caches the directory contents.
    // [STATE] Maintains a dictionary of already loaded `wxImage` objects so the same texture is reused across different UI hints.
    // [UNITY] Unity should map this to a `Dictionary<string, Texture2D>` that reads `Application.persistentDataPath/hms` once per image.
    // [PORTING_HAZARD:P3] `wxImage` semantics differ from Unity textures, so guard against missing files and make sure the cache stays in
    // sync with disk.
    if (image_name.empty() || image_name.Contains("http")) {
        return wxImage();
    }

    if (m_hms_local_images.empty()) {
        const fs::path& local_img_dir = fs::path(Slic3r::data_dir()) / HMS_LOCAL_IMG_PATH;
        if (fs::exists(local_img_dir)) {
            for (const auto& entry : fs::directory_iterator(local_img_dir)) {
                const fs::path& image_path              = entry.path();
                const fs::path& image_name              = fs::relative(image_path, local_img_dir);
                m_hms_local_images[image_name.string()] = wxImage(wxString::FromUTF8(image_path.string()));
            }
        }
    }

    auto iter = m_hms_local_images.find(image_name);
    if (iter != m_hms_local_images.end()) {
        return iter->second;
    }

    return wxImage();
}

void HMSQuery::clear_hms_info()
{
    // [STATE] Reset all in-memory HMS caches, typically triggered when device selections change or the service resets.
    // [THREAD] Guarded by `m_hms_mutex` so clearing happens without data races against download threads.
    // [UNITY] Unity should clear its cached dictionaries and timestamps during similar configuration changes (e.g., `HmsService.ClearCache`).
    std::unique_lock unique_lock(m_hms_mutex);
    m_hms_info_jsons.clear();
    m_hms_action_jsons.clear();
    m_cloud_hms_last_update_time.clear();
}

void HMSQuery::init_hms_info(const std::string& dev_type_id)
{
    // [STATE] Guarded by `m_hms_mutex`, this routine primes local caches, copies static data, and throttles cloud downloads via
    // `m_cloud_hms_last_update_time`. [THREAD] Uses `std::unique_lock` to protect concurrent accesses from UI and background status checks.
    // [UNITY] Equivalent to a Unity `HmsService.InitInfo` that copies StreamingAssets and keeps a `Dictionary<string, json>` cache.
    // [PORTING_HAZARD:P2] Relies on `time(nullptr)` to gate downloads (1 minute/1 day), so maintain the same cooldown logic when porting.
    std::unique_lock unique_lock(m_hms_mutex);
    if (package_dev_id_types.count(dev_type_id) != 0) {
        /*the local one only load once*/
        if (m_hms_info_jsons.count(dev_type_id) == 0) {
            std::string load_version;
            load_from_local(QUERY_HMS_INFO, dev_type_id, &m_hms_info_jsons[dev_type_id], load_version); /*load from local first*/
            if (load_version.empty() || load_version == "0") {
                copy_from_data_dir_to_local(); // STUDIO-9512
                load_from_local(QUERY_HMS_INFO, dev_type_id, &m_hms_info_jsons[dev_type_id],
                                load_version); /*copy files to local, and retry load*/
            }
        }

        if (m_hms_action_jsons.count(dev_type_id) == 0) {
            std::string load_version;
            load_from_local(QUERY_HMS_ACTION, dev_type_id, &m_hms_action_jsons[dev_type_id], load_version); /*load from local first*/
            if (load_version.empty() || load_version == "0") {
                copy_from_data_dir_to_local(); // STUDIO-9512
                load_from_local(QUERY_HMS_ACTION, dev_type_id, &m_hms_action_jsons[dev_type_id],
                                load_version); /*copy files to local, and retry load*/
            }
        }
    }

    /*download from cloud*/
    time_t info_last_update_time = m_cloud_hms_last_update_time[dev_type_id];

    /* check hms is valid or not */
    bool retry = false;
    if (m_hms_info_jsons[dev_type_id].empty() || m_hms_action_jsons[dev_type_id].empty()) {
        retry = time(nullptr) - info_last_update_time > (60 * 1); // retry after 1 minute
    }

    if (time(nullptr) - info_last_update_time > (60 * 60 * 24) || retry) /*do not update in one day to reduce waiting*/
    {
        download_hms_related(QUERY_HMS_INFO, dev_type_id, &m_hms_info_jsons[dev_type_id]);
        download_hms_related(QUERY_HMS_ACTION, dev_type_id, &m_hms_action_jsons[dev_type_id]);
        m_cloud_hms_last_update_time[dev_type_id] = time(nullptr);
    }
}

std::string get_hms_wiki_url(std::string error_code)
{
    // [INTENT] Build the online HMS wiki link for the current error/device so the UI can offer a "Learn More" action.
    // [STATE] Reads the selected machine to add the device identifier when available.
    // [EVENT] Invoked when the user taps the HMS "Learn More" button, so the returned URL drives `wxLaunchDefaultBrowser`/Unity
    // `Application.OpenURL`. [UNITY] Unity should compose the same URL and call `Application.OpenURL` on the main thread, storing it in
    // `HmsService.LastWikiUrl`. [PORTING_HAZARD:P3] Relies on `wxGetApp()` and `MachineObject` APIs, so Unity must port the same getters
    // before invoking this helper.
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return "";
    if (config->get_stealth_mode())
        return "";

    std::string hms_host  = wxGetApp().app_config->get_hms_host();
    std::string lang_code = HMSQuery::hms_language_code();
    std::string url       = (boost::format("https://%1%/index.php?e=%2%&s=device_hms&lang=%3%") % hms_host % error_code % lang_code).str();

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return url;
    MachineObject* obj = dev->get_selected_machine();
    if (!obj)
        return url;

    if (!obj->get_dev_id().empty()) {
        url = (boost::format("https://%1%/index.php?e=%2%&d=%3%&s=device_hms&lang=%4%") % hms_host % error_code % obj->get_dev_id() %
               lang_code)
                  .str();
    }
    return url;
}

std::string get_error_message(int error_code)
{
    // [INTENT] Query the remote HMS API for a localized error string and code display.
    // [STATE] Pulls `hms_host` and language from `AppConfig` so the resulting string matches the selected device/settings.
    // [EVENT] Triggered when an error report needs textual context, so the returned string drives the HMS label or notification text.
    // [UNITY] Port this to `HmsService.FetchErrorMessage` using `UnityWebRequest` with a callback that updates the UI dispatcher.
    // [PORTING_HAZARD:P3] The synchronous `Http` call and `std::sprintf` assumption mean Unity must honor timeouts and avoid blocking the
    // main thread. [THREAD] Uses `Slic3r::Http` synchronously; Unity should map this to an async UnityWebRequest that updates UI on completion.
    if (wxGetApp().app_config->get_stealth_mode())
        return "";

    char        buf[64];
    std::string result_str = "";
    std::sprintf(buf, "%08X", error_code);
    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string get_lang = wxGetApp().app_config->get_language_code();

    std::string url = (boost::format("https://%1%/query.php?lang=%2%&e=%3%") % hms_host % get_lang % buf).str();

    Slic3r::Http http = Slic3r::Http::get(url);
    http.header("accept", "application/json")
        .timeout_max(10)
        .on_complete([get_lang, &result_str](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                if (j.contains("result")) {
                    if (j["result"].get<int>() == 0) {
                        if (j.contains("data")) {
                            json jj = j["data"];
                            if (jj.contains("device_error")) {
                                if (jj["device_error"].contains(get_lang)) {
                                    if (jj["device_error"][get_lang].size() > 0) {
                                        if (!jj["device_error"][get_lang][0]["intro"].empty() ||
                                            !jj["device_error"][get_lang][0]["ecode"].empty()) {
                                            std::string error_info = jj["device_error"][get_lang][0]["intro"].get<std::string>();
                                            std::string error_code = jj["device_error"][get_lang][0]["ecode"].get<std::string>();
                                            error_code.insert(4, " ");
                                            result_str = from_u8(error_info).ToStdString() + "[" + error_code + "]";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (...) {
                ;
            }
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << boost::format("[BBL ErrorMessage]: status=%1%, error=%2%, body=%3%") % status % error % body;
        })
        .perform_sync();

    return result_str;
}

}} // namespace Slic3r::GUI
