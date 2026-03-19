#ifndef slic3r_InstanceCheck_hpp_
#define slic3r_InstanceCheck_hpp_

#include "Event.hpp"

#if _WIN32
#include <windows.h>
#endif //_WIN32

#include <string>

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
// [INTENT] Global instance check function
// [PORTING_HAZARD:P3] Platform-specific IPC mechanisms (file locks, named pipes, etc.) require a robust cross-platform solution in C#
// (e.g., FileSystemWatcher + Lockfiles or socket IPC).
// [INTENT] Global instance check function
// [PORTING_HAZARD:P3] Platform-specific IPC mechanisms (file locks, named pipes, etc.) require a robust cross-platform solution in C#
// (e.g., FileSystemWatcher + Lockfiles or socket IPC).
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
// [UNITY] Use a global EventBus or C# delegates for cross-instance messaging.
wxDECLARE_EVENT(EVT_LOAD_MODEL_OTHER_INSTANCE, LoadFromOtherInstanceEvent);
wxDECLARE_EVENT(EVT_START_DOWNLOAD_OTHER_INSTANCE, StartDownloadOtherInstanceEvent);

using InstanceGoToFrontEvent = SimpleEvent;
// [UNITY] Use EventBus for 'Bring to Front' logic.
wxDECLARE_EVENT(EVT_INSTANCE_GO_TO_FRONT, InstanceGoToFrontEvent);

// [INTENT] Handles incoming messages from other instances
// [PORTING_HAZARD:P3] Platform-specific IPC (D-Bus, Win32, Cocoa). Needs a custom native plugin for Unity.
class OtherInstanceMessageHandler
{
public:
    OtherInstanceMessageHandler()                                   = default;
    OtherInstanceMessageHandler(OtherInstanceMessageHandler const&) = delete;
    void operator=(OtherInstanceMessageHandler const&)              = delete;
    ~OtherInstanceMessageHandler() { assert(!m_initialized); }

    // [INTENT] Initialize listening mechanism
    void init(wxEvtHandler* callback_evt_handler);
    // [INTENT] Shutdown listening
    void shutdown(MainFrame* main_frame);

    // [INTENT] Process incoming message from other instance
    void handle_message(const std::string& message);
#ifdef __APPLE__
    // [INTENT] Handle message about lockfile closure
    void handle_message_other_closed();
#endif //__APPLE__
#ifdef _WIN32
    static void init_windows_properties(MainFrame* main_frame, size_t instance_hash);
#endif // WIN32
private:
    // [STATE] Initialization status
    bool m_initialized{false};
    // [STATE] Callback handler for events
    // [UNITY] Needs a C# event handler or UnityEvent
    wxEvtHandler* m_callback_evt_handler{nullptr};

#ifdef BACKGROUND_MESSAGE_LISTENER
    // [THREAD] Worker thread for Linux D-Bus communication
    // [UNITY] Use C# Task/async-await or Unity Job System
    boost::thread           m_thread;
    std::condition_variable m_thread_stop_condition;
    mutable std::mutex      m_thread_stop_mutex;
    bool                    m_stop{false};
    bool                    m_start{true};

    // [INTENT] Background listener thread
    void listen();
#endif // BACKGROUND_MESSAGE_LISTENER

#if __APPLE__
    // implemented at InstanceCheckMac.mm
    void register_for_messages(const std::string& version_hash);
    void unregister_for_messages();
    // Opaque pointer to RemovableDriveManagerMM
    void* m_impl_osx;

public:
    void bring_instance_forward();
#endif //__APPLE__
};
} // namespace GUI
} // namespace Slic3r
#endif // slic3r_InstanceCheck_hpp_
