#ifndef slic3r_GUI_RemovableDriveManager_hpp_
#define slic3r_GUI_RemovableDriveManager_hpp_

#include <vector>
#include <string>

#include <boost/thread.hpp>
#include <mutex>
#include <condition_variable>

// Custom wxWidget events
#include "Event.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Small value object for removable-media identity and display text.
// [STATE] `path` is the stable comparison key; `name` is UI-facing label text and may differ by platform.
// [UNITY] Map to a serializable drive descriptor used by a service/controller, then bind it into a ListView or dropdown.
struct DriveData
{
    std::string name;
    std::string path;

    void clear()
    {
        name.clear();
        path.clear();
    }
    bool empty() const { return path.empty(); }
};

inline bool operator<(const DriveData& lhs, const DriveData& rhs) { return lhs.path < rhs.path; }
inline bool operator>(const DriveData& lhs, const DriveData& rhs) { return lhs.path > rhs.path; }
inline bool operator==(const DriveData& lhs, const DriveData& rhs) { return lhs.path == rhs.path; }

// [EVENT] wx events report drive-set changes and eject completion back to the GUI layer.
// [UNITY] Replace with C# events or a main-thread dispatcher that publishes cached drive updates.
using RemovableDriveEjectEvent = Event<std::pair<DriveData, bool>>;
wxDECLARE_EVENT(EVT_REMOVABLE_DRIVE_EJECTED, RemovableDriveEjectEvent);

using RemovableDrivesChangedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_REMOVABLE_DRIVES_CHANGED, RemovableDrivesChangedEvent);

#if __APPLE__
// Callbacks on device plug / unplug work reliably on OSX.
#define REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
#endif // __APPLE__

// [INTENT] Own the lifecycle for drive discovery, eject, and update notifications.
// [STATE] Initialization is external and must establish the callback target before any event emission.
// [UNITY] Model this as a long-lived platform service injected into a main-thread UI controller.
// [PORTING_HAZARD:P2] Platform behavior diverges: macOS uses notifications, Windows can callback from volume events, and Unix/OSX eject may block.
class RemovableDriveManager
{
public:
    // [INTENT] Register the UI callback target and start OS-specific monitoring.
    // [THREAD] May spin up a worker thread on non-callback platforms; any emitted wx events must return to the GUI thread.
    RemovableDriveManager()                             = default;
    RemovableDriveManager(RemovableDriveManager const&) = delete;
    void operator=(RemovableDriveManager const&)        = delete;
    ~RemovableDriveManager() { /*assert(! m_initialized);*/ }

    // [INTENT] Start monitoring and remember the target window for event delivery.
    // [THREAD] On polling platforms this may start background work; the callback target must outlive pending updates.
    // Start the background thread and register this window as a target for update events.
    // Register for OSX notifications.
    void init(wxEvtHandler* callback_evt_handler);
    // [INTENT] Tear down background work and OS callbacks before the UI target goes away.
    // [THREAD] Stop the worker thread before releasing the callback handler.
    // Stop the background thread of the removable drive manager, so that no new updates will be sent out.
    // Deregister OSX notifications.
    void shutdown();

    // [INTENT] Resolve the best removable-media mount path for a candidate file path.
    // [STATE] Returns the cached/remapped path if the input sits on removable storage; otherwise returns empty.
    // Returns path to a removable media if it exists, prefering the input path.
    std::string get_removable_drive_path(const std::string& path);
    bool        is_path_on_removable_drive(const std::string& path) { return this->get_removable_drive_path(path) == path; }

    // [INTENT] Remember the export target and validate that it is on removable media before ejecting.
    // [STATE] Updates `m_last_save_path` and resets `m_exporting_finished` for the next export sequence.
    // Verify whether the path provided is on removable media. If so, save the path for further eject and return true, otherwise return false.
    bool set_and_verify_last_save_path(const std::string& path);
    // [INTENT] Eject the drive associated with the current export target.
    // [THREAD] Blocking on Unix/OSX and deferred completion on Windows mean the caller must tolerate asynchronous completion.
    // [UNCLEAR] Completion timing is platform-dependent; hypothesis: callers rely on the event, not the return value, as the final success
    // signal. Eject drive of a file set by set_and_verify_last_save_path(). On Unix / OSX, the function blocks and sends out the
    // EVT_REMOVABLE_DRIVE_EJECTED event on success. On Windows, the function does not block, and the eject is detected in the background
    // thread.
    void eject_drive();

    // [INTENT] Provide a cached status snapshot for enable/disable decisions in the UI.
    // [STATE] This is a no-side-effect query that should not trigger enumeration.
    // Status is used to retrieve info for showing UI buttons.
    // Status is called every time when change of UI buttons is possible therefore should not perform update.
    struct RemovableDrivesStatus
    {
        bool has_removable_drives{false};
        bool has_eject{false};
    };
    RemovableDrivesStatus status();

    // [INTENT] Refresh the drive cache and emit change/eject events when the snapshot changes.
    // [THREAD] This entry point is shared by UI calls, worker polling, and OS callbacks, so the internal mutexes serialize reentry.
    // [UNITY] Move this logic behind a native plugin or platform service and marshal results to the main thread before touching UI state.
    // Enumerates current drives and sends out wxWidget events on change or eject.
    // Called by each public method, by the background thread and from RemovableDriveManagerMM::on_device_unmount OSX notification handler.
    // Not to be called manually.
    // Public to be accessible from RemovableDriveManagerMM::on_device_unmount OSX notification handler.
    // It would be better to make this method private and friend to RemovableDriveManagerMM, but RemovableDriveManagerMM is an ObjectiveC class.
    void update();
    // [STATE] Explicitly clears the export gate after a successful export path has been established.
    void set_exporting_finished(bool b) { m_exporting_finished = b; }
#ifdef _WIN32
    // [EVENT] Win32 volume arrival/removal is bridged into the shared update path.
    // Called by Win32 Volume arrived / detached callback.
    void volumes_changed();
#endif // _WIN32

private:
    // [STATE] Lifecycle and callback ownership state for the service shell.
    bool          m_initialized{false};
    wxEvtHandler* m_callback_evt_handler{nullptr};

#ifndef REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS
    // [THREAD] Background polling worker for platforms without native mount callbacks.
    // [STATE] `m_stop` and the stop condition coordinate shutdown with the worker loop.
    // [UNITY] Replace with a cancellable job/task plus main-thread completion postback.
    // Worker thread, worker thread synchronization and callbacks to the UI thread.
    void                    thread_proc();
    boost::thread           m_thread;
    std::condition_variable m_thread_stop_condition;
    mutable std::mutex      m_thread_stop_mutex;
    bool                    m_stop{false};
#ifdef _WIN32
    std::atomic<bool> m_wakeup{false};
#endif /* _WIN32 */
#endif // REMOVABLE_DRIVE_MANAGER_OS_CALLBACKS

    // [INTENT] Enumerate the current removable drive set from platform APIs.
    // [UNITY] This belongs in a native platform adapter; the Unity side should only consume the resulting snapshot.
    // Called from update() to enumerate removable drives.
    std::vector<DriveData> search_for_removable_drives() const;

    // [STATE] Cached, sorted snapshot of currently detected removable drives.
    // m_current_drives is guarded by m_drives_mutex
    // sorted ascending by path
    std::vector<DriveData> m_current_drives;
    mutable std::mutex     m_drives_mutex;
    // [THREAD] Prevents overlapping refreshes when multiple callbacks race into update().
    // Locking the update() function to avoid that the function is executed multiple times.
    mutable std::mutex m_inside_update_mutex;

    // [STATE] Tracks the last path selected for export so eject() can resolve the target drive later.
    // [UNCLEAR] The member is only meaningful after set_and_verify_last_save_path(); initialization elsewhere should make that invariant
    // explicit. Returns drive path (same as path in DriveData) if exists otherwise empty string.
    std::string get_removable_drive_from_path(const std::string& path);
    // Returns iterator to a drive in m_current_drives with path equal to m_last_save_path or end().
    std::vector<DriveData>::const_iterator find_last_save_path_drive_data() const;
    // Set with set_and_verify_last_save_path() to a removable drive path to be ejected.
    std::string m_last_save_path;
    // [STATE] Gate that must be true before the queued eject completion is considered successful.
    // [UNCLEAR] The header does not show where this is initialized; hypothesis: init() or the verify path sets the first authoritative value.
    // Verifies that exporting was finished so drive can be ejected.
    // Set false by set_and_verify_last_save_path() that is called just before exporting.
    bool m_exporting_finished;
#if __APPLE__
    // [INTENT] Bridge into the Objective-C mount notification layer on macOS.
    // [PORTING_HAZARD:P2] The native Objective-C helper and callback ownership do not map 1:1 to Unity, so the platform adapter likely
    // needs a separate plugin boundary.
    void register_window_osx();
    void unregister_window_osx();
    void list_devices(std::vector<DriveData>& out) const;
    // not used as of now
    void eject_device(const std::string& path);
    // Opaque pointer to RemovableDriveManagerMM
    void*          m_impl_osx;
    boost::thread* m_eject_thread{nullptr};
    void           eject_thread_finish();
#endif
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_RemovableDriveManager_hpp_
