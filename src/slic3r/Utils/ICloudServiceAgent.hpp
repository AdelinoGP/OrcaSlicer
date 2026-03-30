#ifndef __I_CLOUD_SERVICE_AGENT_HPP__
#define __I_CLOUD_SERVICE_AGENT_HPP__

#include "bambu_networking.hpp"
#include "../../libslic3r/ProjectTask.hpp"
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>

/**
 * [INTENT] ICloudServiceAgent - Interface for authentication and cloud service operations.
 *
 * This interface encapsulates all cloud-related functionality including authentication:
 * - Lifecycle methods for agent initialization
 * - User session management (login/logout)
 * - Token access for dependent agents (IPrinterAgent)
 * - Login UI command builders for WebView integration
 * - Server connectivity and subscription management
 * - Settings synchronization (presets upload/download)
 * - Cloud user services (messages, tasks, firmware)
 * - Model mall and publishing
 * - Analytics and telemetry
 * - Ratings and reviews
 *
 * Implementations:
 * - OrcaCloudServiceAgent: Native implementation for Orca Cloud (includes OAuth PKCE)
 * - BBLCloudServiceAgent: Wrapper around Bambu Lab's proprietary DLL
 *
 * Token Sharing Pattern:
 * IPrinterAgent receives an ICloudServiceAgent instance via set_cloud_agent() to
 * access tokens for cloud-relay operations without coupling to a specific auth
 * implementation.
 *
 * [UNITY] In Unity, this should be split into a set of specialized services (IAuthService, ISyncService,
 * IAnalyticsService, etc.) coordinated by a central CloudProvider. Implement using async/await and
 * UnityWebRequest, with ScriptableObjects for configuration and persistent state.
 *
 * [PORTING_HAZARD:P1] This interface is a "Mega-Facade" that couples many unrelated domains.
 * Re-implementing it 1:1 in C# will carry this technical debt. Recommend breaking it up into
 * smaller, single-responsibility services.
 */
namespace Slic3r {

class ICloudServiceAgent
{
public:
    virtual ~ICloudServiceAgent() = default;

    // [COUPLING] This interface deliberately mixes auth, cloud sync, printer metadata, publishing, and analytics in
    // one facade so `NetworkAgent` can swap implementations without changing GUI call sites. A port may want to split
    // these responsibilities, but today the wide surface area is the compatibility contract.

    // ========================================================================
    // Lifecycle Methods
    // ========================================================================
    /**
     * [INTENT] Initialize the logging backend for the agent.
     * Call after set_config_dir() so logs have a destination.
     */
    virtual int init_log() = 0;

    /**
     * [STATE] Provide the writable configuration directory for storing auth state.
     * Must be called before start().
     */
    virtual int set_config_dir(std::string config_dir) = 0;

    /**
     * Register the client certificate file for TLS authentication.
     * May be unused by some implementations (e.g., OrcaCloudServiceAgent).
     */
    virtual int set_cert_file(std::string folder, std::string filename) = 0;

    /**
     * [STATE] Set the country code for region-specific backend selection.
     */
    virtual int set_country_code(std::string country_code) = 0;

    /**
     * [INTENT] Start the agent, performing any expensive initialization.
     * Typically regenerates PKCE bundles and attempts silent sign-in.
     */
    virtual int start() = 0;

    // ========================================================================
    // User Session Management
    // ========================================================================
    /**
     * [EVENT] Authenticate the user with the provided JSON payload.
     *
     * Supported formats:
     * 1. Traditional: {"username": "...", "password": "..."}
     * 2. WebView/OAuth: {"command": "user_login", "data": {...}}
     * 3. Token format: {"data": {"token": "...", "refresh_token": "...", "user": {...}}}
     *
     * [UNITY] In C#, use a Task<AuthResult> or a dedicated login event.
     * On completion, invokes the registered OnUserLoginFn callback.
     */
    virtual int change_user(std::string user_info) = 0;

    /**
     * [STATE] Check whether a valid authenticated session exists.
     */
    virtual bool is_user_login() = 0;

    /**
     * [EVENT] Terminate the current session.
     * @param request If true, also notify the backend to invalidate the session.
     */
    virtual int user_logout(bool request = false) = 0;

    /**
     * [STATE] Return the backend-generated user ID for the current session.
     */
    virtual std::string get_user_id() = 0;

    /**
     * [STATE] Return the display name for the current user.
     */
    virtual std::string get_user_name() = 0;

    /**
     * [STATE] Return the avatar URL/path for the current user.
     */
    virtual std::string get_user_avatar() = 0;

    /**
     * [STATE] Return the nickname for the current user.
     */
    virtual std::string get_user_nickname() = 0;

    // ========================================================================
    // Login UI Support
    // ========================================================================
    /**
     * [INTENT] Build a JSON command for the WebView login flow.
     * Contains backend URL, API key, and PKCE parameters.
     * [UNITY] Map to a script-injection or URL-parameter builder for an embedded web-view control.
     */
    virtual std::string build_login_cmd() = 0;

    /**
     * [INTENT] Build a JSON command for WebView logout.
     */
    virtual std::string build_logout_cmd() = 0;

    /**
     * [STATE] Return a JSON snapshot of the active session (user info, no tokens).
     * Used by WebView to display current user state.
     */
    virtual std::string build_login_info() = 0;

    // ========================================================================
    // Token Access (for dependent agents)
    // ========================================================================
    /**
     * [STATE] Return the current access token for API calls.
     * Cloud and printer agents use this for Authorization headers.
     */
    // [STATE] Access/refresh tokens live behind the interface so printer agents can borrow cloud credentials for relay
    // APIs without owning the login flow themselves.
    virtual std::string get_access_token() const = 0;

    /**
     * [STATE] Return the current refresh token (if available).
     */
    virtual std::string get_refresh_token() const = 0;

    /**
     * [INTENT] Ensure the access token is fresh, refreshing if necessary.
     * Call before making API requests to avoid 401 errors.
     *
     * @param reason Descriptive string for logging (e.g., "connect_server")
     * @return true if the token is fresh or was successfully refreshed
     * [THREAD] [PORTING_HAZARD:P2] This is often called synchronously before network requests.
     * In Unity, use an async/await interceptor or a Task-based refresh flow to avoid blocking.
     */
    virtual bool ensure_token_fresh(const std::string& reason) = 0;

    // ========================================================================
    // Server Connectivity
    // ========================================================================
    /**
     * [STATE] Return the base hostname for cloud API calls (varies by region).
     * Helpful for diagnostics and when building browser URLs.
     */
    virtual std::string get_cloud_service_host() = 0;

    /**
     * [INTENT] Return the login URL for the cloud service.
     * @param language Optional language code (e.g., "en-US", "zh-CN") for localized login page.
     *                 If empty, returns the default (non-localized) login URL.
     * @return The full URL to the login page, or a local file:// URL for native implementations.
     */
    virtual std::string get_cloud_login_url(const std::string& language = "") = 0;

    /**
     * [INTENT] Perform a health check against the configured backend.
     * Updates is_server_connected() state and triggers OnServerConnectedFn.
     * [THREAD] Usually triggers an async background check.
     */
    virtual int connect_server() = 0;

    /**
     * [STATE] Return whether the server is currently reachable.
     */
    virtual bool is_server_connected() = 0;

    /**
     * [INTENT] Force a server state recheck, clearing any cached state.
     */
    virtual int refresh_connection() = 0;

    /**
     * [INTENT] Subscribe to a logical module (e.g., "printer", "user").
     * [EVENT] Typically sets up a WebSocket or MQTT subscription.
     */
    virtual int start_subscribe(std::string module) = 0;

    /**
     * [INTENT] Stop listening to a formerly subscribed module.
     */
    virtual int stop_subscribe(std::string module) = 0;

    /**
     * [INTENT] Subscribe to push streams for specific device identifiers.
     */
    virtual int add_subscribe(std::vector<std::string> dev_list) = 0;

    /**
     * [INTENT] Remove device-level subscriptions.
     */
    virtual int del_subscribe(std::vector<std::string> dev_list) = 0;

    /**
     * [STATE] Enable or disable multi-machine mode.
     */
    virtual void enable_multi_machine(bool enable) = 0;

    // ========================================================================
    // Settings Synchronization
    // ========================================================================
    /**
     * [INTENT] Fetch all presets owned by the logged-in user.
     * @param user_presets Map populated with [type][setting_id] = json
     * [PORTING_HAZARD:P2] Synchronous return of network data. Use Task<PresetMap> in C#.
     */
    virtual int get_user_presets(std::map<std::string, std::map<std::string, std::string>>* user_presets) = 0;

    /**
     * [INTENT] Request a new preset identifier from the server.
     */
    virtual std::string request_setting_id(std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code) = 0;

    /**
     * [INTENT] Update or create a preset with a known setting_id.
     */
    virtual int put_setting(std::string                         setting_id,
                            std::string                         name,
                            std::map<std::string, std::string>* values_map,
                            unsigned int*                       http_code) = 0;

    /**
     * [INTENT] Trigger bulk download of user presets.
     * [THREAD] Uses progress and cancel callbacks for long-running fetch.
     */
    virtual int get_setting_list(std::string bundle_version, ProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr) = 0;

    /**
     * [INTENT] Enhanced preset sync with per-item validation.
     */
    virtual int get_setting_list2(std::string    bundle_version,
                                  CheckFn        chk_fn,
                                  ProgressFn     pro_fn    = nullptr,
                                  WasCancelledFn cancel_fn = nullptr) = 0;

    /**
     * [INTENT] Delete a remote preset.
     */
    virtual int delete_setting(std::string setting_id) = 0;

    // ========================================================================
    // Cloud User Services
    // ========================================================================
    /**
     * [INTENT] Retrieve inbox/notification messages.
     */
    virtual int get_my_message(int type, int after, int limit, unsigned int* http_code, std::string* http_body) = 0;

    /**
     * [INTENT] Check for pending task reports.
     */
    virtual int check_user_task_report(int* task_id, bool* printable) = 0;

    /**
     * [INTENT] Fetch aggregated print statistics.
     */
    virtual int get_user_print_info(unsigned int* http_code, std::string* http_body) = 0;

    /**
     * [INTENT] Query user's tasks/prints.
     */
    virtual int get_user_tasks(TaskQueryParams params, std::string* http_body) = 0;

    /**
     * [INTENT] Fetch firmware information for a printer.
     */
    virtual int get_printer_firmware(std::string dev_id, unsigned* http_code, std::string* http_body) = 0;

    /**
     * [INTENT] Get plate index for a cloud task.
     */
    virtual int get_task_plate_index(std::string task_id, int* plate_index) = 0;

    /**
     * [INTENT] Retrieve extended user profile info.
     */
    virtual int get_user_info(int* identifier) = 0;

    /**
     * [INTENT] Fetch subtask information.
     */
    virtual int get_subtask_info(std::string subtask_id, std::string* task_json, unsigned int* http_code, std::string* http_body) = 0;

    /**
     * [INTENT] Retrieve slicing job info.
     */
    virtual int get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string* slice_json) = 0;

    /**
     * [INTENT] Query binding status for multiple devices.
     */
    virtual int query_bind_status(std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body) = 0;

    /**
     * [INTENT] Update printer name in cloud profile.
     */
    virtual int modify_printer_name(std::string dev_id, std::string dev_name) = 0;

    // ========================================================================
    // Model Mall & Publishing
    // ========================================================================
    /**
     * [INTENT] Request live camera streaming URL.
     * [THREAD] Uses an async callback for the URL.
     */
    virtual int get_camera_url(std::string dev_id, std::function<void(std::string)> callback) = 0;

    /**
     * [INTENT] Fetch staff-picked designs from model mall.
     */
    virtual int get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback) = 0;

    /**
     * [INTENT] Run multi-stage publishing workflow.
     * [THREAD] [PORTING_HAZARD:P2] Multi-stage workflow with status callbacks.
     * Unity port should use a dedicated PublishState machine or a series of Tasks.
     */
    virtual int start_publish(PublishParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, std::string* out) = 0;

    /**
     * [INTENT] Get model publish URL.
     */
    virtual int get_model_publish_url(std::string* url) = 0;

    /**
     * [INTENT] Fetch publishing subtask information.
     */
    virtual int get_subtask(BBLModelTask* task, OnGetSubTaskFn getsub_fn) = 0;

    /**
     * [INTENT] Get model mall home URL.
     */
    virtual int get_model_mall_home_url(std::string* url) = 0;

    /**
     * [INTENT] Build model detail page URL.
     */
    virtual int get_model_mall_detail_url(std::string* url, std::string id) = 0;

    /**
     * [INTENT] Retrieve user's model mall profile.
     */
    virtual int get_my_profile(std::string token, unsigned int* http_code, std::string* http_body) = 0;

    // ========================================================================
    // Analytics & Tracking
    // ========================================================================
    /**
     * [STATE] Enable/disable telemetry.
     */
    virtual int track_enable(bool enable) = 0;

    /**
     * [INTENT] Delete telemetry files.
     */
    virtual int track_remove_files() = 0;

    /**
     * [EVENT] Report a custom analytics event.
     */
    virtual int track_event(std::string evt_key, std::string content) = 0;

    /**
     * [INTENT] Set telemetry headers.
     */
    virtual int track_header(std::string header) = 0;

    /**
     * [STATE] Update a tracked user property.
     */
    virtual int track_update_property(std::string name, std::string value, std::string type = "string") = 0;

    /**
     * [STATE] Read a tracked user property.
     */
    virtual int track_get_property(std::string name, std::string& value, std::string type = "string") = 0;

    /**
     * [STATE] Check if tracking is enabled.
     */
    virtual bool get_track_enable() = 0;

    // ========================================================================
    // Ratings & Reviews
    // ========================================================================
    /**
     * [INTENT] Submit a review for a marketplace design.
     */
    virtual int put_model_mall_rating(int                      design_id,
                                      int                      score,
                                      std::string              content,
                                      std::vector<std::string> images,
                                      unsigned int&            http_code,
                                      std::string&             http_error) = 0;

    /**
     * [INTENT] Get OSS configuration for image uploads.
     */
    virtual int get_oss_config(std::string& config, std::string country_code, unsigned int& http_code, std::string& http_error) = 0;

    /**
     * [INTENT] Upload rating images to OSS.
     */
    virtual int put_rating_picture_oss(std::string&  config,
                                       std::string&  pic_oss_path,
                                       std::string   model_id,
                                       int           profile_id,
                                       unsigned int& http_code,
                                       std::string&  http_error) = 0;

    /**
     * [INTENT] Poll for rating result.
     */
    virtual int get_model_mall_rating_result(int job_id, std::string& rating_result, unsigned int& http_code, std::string& http_error) = 0;

    // ========================================================================
    // Extra Features
    // ========================================================================
    /**
     * [STATE] Set additional HTTP headers for all requests.
     */
    virtual int set_extra_http_header(std::map<std::string, std::string> extra_headers) = 0;

    /**
     * [STATE] Get the studio info URL.
     */
    virtual std::string get_studio_info_url() = 0;

    /**
     * [INTENT] Fetch MakerWorld user preferences.
     */
    virtual int get_mw_user_preference(std::function<void(std::string)> callback) = 0;

    /**
     * [INTENT] Retrieve MakerWorld "For You" list.
     */
    virtual int get_mw_user_4ulist(int seed, int limit, std::function<void(std::string)> callback) = 0;

    /**
     * [STATE] Return the version of the cloud service implementation.
     */
    virtual std::string get_version() = 0;

    // ========================================================================
    // Callback Registration
    // ========================================================================
    /**
     * [EVENT] Register the login status callback.
     * Called after change_user() finishes or when the session expires.
     */
    virtual int set_on_user_login_fn(OnUserLoginFn fn) = 0;

    /**
     * [EVENT] Register server connection status callback.
     */
    virtual int set_on_server_connected_fn(OnServerConnectedFn fn) = 0;

    /**
     * [EVENT] Register HTTP error callback.
     */
    virtual int set_on_http_error_fn(OnHttpErrorFn fn) = 0;

    /**
     * [EVENT] Provide country code getter callback.
     */
    virtual int set_get_country_code_fn(GetCountryCodeFn fn) = 0;

    /**
     * [THREAD] Provide main thread queue callback.
     * [PORTING_HAZARD:P1] This callback is used to marshal background operations back to the
     * UI thread. In Unity, use a SynchronizationContext or a MainThreadDispatcher.
     */
    virtual int set_queue_on_main_fn(QueueOnMainFn fn) = 0;
};

} // namespace Slic3r

#endif // __I_CLOUD_SERVICE_AGENT_HPP__
