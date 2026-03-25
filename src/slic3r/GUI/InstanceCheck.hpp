// [INTENT]
// This header defines the mechanism for enforcing a single instance of OrcaSlicer.
// It provides a platform-agnostic function `instance_check` that is called at startup.
// If another instance is running, it passes the command line arguments to it and exits.
// This is achieved using platform-specific IPC mechanisms (named pipes on Windows,
// D-Bus on Linux, and a custom URL scheme or file-based locking on macOS).
// The `OtherInstanceMessageHandler` class is responsible for receiving and processing
// messages from other instances, such as requests to load models or bring the
// window to the front.
//
// [UNITY]
// In Unity, this would be implemented as a C# script on a singleton GameObject that
// persists across scenes. On startup, it would attempt to create a named `Mutex`.
// If the mutex is already owned, it would send the command line arguments to the
// existing instance via a local network socket or other IPC mechanism and then exit.
// The primary instance's message handler would listen for these connections and
// use a main-thread dispatcher to execute actions like loading models.
// A native plugin might be required for robust cross-platform IPC, especially
// if relying on OS-specific features beyond simple mutexes is desired.

// [INTENT]
// This header defines the mechanism for enforcing a single instance of OrcaSlicer.
// It provides a platform-agnostic function `instance_check` that is called at startup.
// If another instance is running, it passes the command line arguments to it and exits.
// This is achieved using platform-specific IPC mechanisms (named pipes on Windows,
// D-Bus on Linux, and a custom URL scheme or file-based locking on macOS).
// The `OtherInstanceMessageHandler` class is responsible for receiving and processing
// messages from other instances, such as requests to load models or bring the
// window to the front.
//
// [UNITY]
// In Unity, this would be implemented as a C# script on a singleton GameObject that
// persists across scenes. On startup, it would attempt to create a named `Mutex`.
// If the mutex is already owned, it would send the command line arguments to the
// existing instance via a local network socket or other IPC mechanism and then exit.
// The primary instance's message handler would listen for these connections and
// use a main-thread dispatcher to execute actions like loading models.
// A native plugin might be required for robust cross-platform IPC, especially
// if relying on OS-specific features beyond simple mutexes is desired.
#ifndef slic3r_InstanceCheck_hpp_
#define slic3r_InstanceCheck_hpp_

#include "Event.hpp"

#if _WIN32
#include <windows.h>
#endif //_WIN32

#include <string>
#include <vector>

#include <boost/filesystem.hpp>

#if __linux__
#include <boost/thread.hpp>
#include <mutex>
#include <condition_variable>
#endif // __linux__

namespace Slic3r {
// checks for other running instances and sends them argv,
// if there is --single-instance argument or AppConfig is set to single_instance=1
// returns true if this instance should terminate
// [INTENT] Gate single-instance enforcement before the main GUI comes up. This function is called very early in startup to
// decide if this process should continue or pass its arguments to an existing instance and then exit.
// [STATE] `argc/argv` and `app_config_single_instance` choose the lockfile path, handshake port, and early-exit result.
// [EVENT] If another instance is found, the current instance's arguments are marshalled into a message and sent, triggering events in the
// other instance (see below). [THREAD] This runs before the UI thread spins up and may block on named mutexes or filesystem locks. [UNITY]
// Map to a singleton MonoBehaviour that registers `Application.wantsToQuit`, maintains a named `Mutex`, and uses a main-thread dispatcher
// for messaging. [PORTING_HAZARD:P3] Platform IPC uses OS-specific lockfiles/named pipes; Unity needs a unified cross-platform
// Mutex/lock-file watcher plus a dispatcher for incoming data.
bool instance_check(int argc, char** argv, bool app_config_single_instance);

#if __APPLE__
// apple implementation of inner functions of instance_check
// in InstanceCheckMac.mm
void send_message_mac(const std::string& msg, const std::string& version);
void send_message_mac_closing(const std::string& msg, const std::string& version);

bool unlock_lockfile(const std::string& name, const std::string& path);
#endif //__APPLE__

namespace GUI {

class MainFrame;

#if __linux__
#define BACKGROUND_MESSAGE_LISTENER
#endif // __linux__

// [EVENT] Events for inter-instance communication
using LoadFromOtherInstanceEvent      = Event<std::vector<boost::filesystem::path>>;
using StartDownloadOtherInstanceEvent = Event<std::vector<std::string>>;
// [STATE] Payloads contain the queued model paths or download URLs that the original instance requested.
// [UNITY] Use a global EventBus or C# delegates for cross-instance messaging.
wxDECLARE_EVENT(EVT_LOAD_MODEL_OTHER_INSTANCE, LoadFromOtherInstanceEvent);
wxDECLARE_EVENT(EVT_START_DOWNLOAD_OTHER_INSTANCE, StartDownloadOtherInstanceEvent);

using InstanceGoToFrontEvent = SimpleEvent;
// [STATE] Raised after the handshake so the main window can be focused.
// [UNITY] Use EventBus for 'Bring to Front' logic.
wxDECLARE_EVENT(EVT_INSTANCE_GO_TO_FRONT, InstanceGoToFrontEvent);
// [THREAD] Events reroute from platform callbacks onto the UI thread before invoking frame methods.

// [INTENT] Handles incoming messages from other instances. A single handler is instantiated in the main application frame.
// [UNITY] This entire class and its platform-specific implementations will need to be replaced by a native C# or C++ plugin for Unity that
// handles the low-level OS IPC. The plugin would then raise C# events that the Unity application can subscribe to.
// [PORTING_HAZARD:P3] Platform-specific IPC (D-Bus, Win32, Cocoa). Needs a custom native plugin for Unity.
class OtherInstanceMessageHandler
{
public:
    OtherInstanceMessageHandler()                                   = default;
    OtherInstanceMessageHandler(OtherInstanceMessageHandler const&) = delete;
    void operator=(OtherInstanceMessageHandler const&)              = delete;
    ~OtherInstanceMessageHandler() { assert(!m_initialized); }

    // [INTENT] Initialize listening mechanism.
    // [THREAD] Always invoked on the UI thread because it wires wxEvtHandler callbacks.
    void init(wxEvtHandler* callback_evt_handler);
    // [INTENT] Shutdown listening.
    // [THREAD] Called during UI teardown so worker threads stop before MainFrame is destroyed.
    void shutdown(MainFrame* main_frame);

    // [INTENT] Process incoming message from other instance.
    // [THREAD] Raised from platform callbacks or worker threads; marshals back to wxQueueEvent before hitting UI state.
    // [UNITY] Mirror with MainThreadDispatcher + C# event handler to keep Unity on the main thread.
    void handle_message(const std::string& message);
#ifdef __APPLE__
    // [INTENT] Handle message about lockfile closure.
    void handle_message_other_closed();
#endif //__APPLE__
#ifdef _WIN32
    static void init_windows_properties(MainFrame* main_frame, size_t instance_hash);
#endif // WIN32
private:
    // [STATE] Initialization status.
    bool m_initialized{false};
    // [STATE] Callback handler for events.
    // [UNITY] In Unity, this would be replaced by C# events (e.g., `public event Action<string> OnMessageReceived;`) or `UnityEvent`
    // instances that other scripts can subscribe to. The native plugin would invoke these events.
    wxEvtHandler* m_callback_evt_handler{nullptr};

#ifdef BACKGROUND_MESSAGE_LISTENER
    // [THREAD] Worker thread for Linux D-Bus communication.
    // [UNITY] Use C# Task/async-await or Unity Job System.
    boost::thread           m_thread;
    std::condition_variable m_thread_stop_condition;
    mutable std::mutex      m_thread_stop_mutex;
    // [STATE] Signals the listener loop to exit, flipped from `shutdown`.
    bool m_stop{false};
    // [STATE] Tracks whether the listener owns the linux connection yet.
    bool m_start{true};

    // [INTENT] Background listener thread.
    // [THREAD] Linux D-Bus listener loop that runs until `m_stop` flips; Unity would run this as a Task/async loop.
    void listen();
#endif // BACKGROUND_MESSAGE_LISTENER

#if __APPLE__
    // implemented at InstanceCheckMac.mm
    // [PORTING_HAZARD:P3] Cocoa/Objective-C callbacks drive this registration; a Unity port needs a native plugin to observe the same notifications.
    void register_for_messages(const std::string& version_hash);
    // [PORTING_HAZARD:P3] Requires tearing down the Objective-C listener before the plugin is unloaded.
    void unregister_for_messages();
    // Opaque pointer to RemovableDriveManagerMM
    // [STATE] Bridges to the Objective-C helper that observes other-instance notifications.
    void* m_impl_osx;

public:
    // [EVENT] Called when another instance finishes handshake so we focus/raise the main window.
    // [UNITY] Mirror with a window controller that sets `Application.runInBackground` and raises the main View.
    void bring_instance_forward();
#endif //__APPLE__
};
} // namespace GUI
} // namespace Slic3r
#endif // slic3r_InstanceCheck_hpp_
