// [INTENT]
// This file implements the `UpgradeNetworkJob` class, a background job that
// handles the downloading and installation of the networking plugins. These
// plugins provide functionalities for communicating with printers over the
// network (e.g., for sending print jobs, monitoring).
//
// The job performs the following steps in a worker thread:
// 1. **Downloading**: It calls `wxGetApp().download_plugin()` to download the
//    plugin package (e.g., "networking_plugins.zip") from the server. It uses a
//    callback to report download progress to the UI.
// 2. **Installing**: After a successful download, it calls
//    `wxGetApp().install_plugin()` to unzip and install the plugin files into
//    the appropriate directory. It also reports installation progress.
//
// Throughout the process, the job sends wx events (`EVT_UPGRADE_UPDATE_MESSAGE`,
// `EVT_UPGRADE_NETWORK_SUCCESS`, etc.) to the UI thread to update the progress
// and notify of success or failure. This allows the UI to remain responsive
// and provide feedback to the user.
//
// [UNITY]
// In a Unity port, this functionality would be managed by a C# class using
// async/await Tasks.
// - Downloading would be handled using `UnityWebRequest`.
// - Installation would involve unzipping the downloaded file using a C# library
//   (e.g., `System.IO.Compression`) and moving the files to the correct location
//   within the application's data directory.
// - Progress updates and completion notifications would be handled through C#
//   events or by binding UI elements to properties of the C# class.

#include "UpgradeNetworkJob.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_UPGRADE_UPDATE_MESSAGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPGRADE_NETWORK_SUCCESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_DOWNLOAD_NETWORK_FAILED, wxCommandEvent);
wxDEFINE_EVENT(EVT_INSTALL_NETWORK_FAILED, wxCommandEvent);

// [INTENT] Constructs the UpgradeNetworkJob, setting the default plugin name.
UpgradeNetworkJob::UpgradeNetworkJob()
{
    name         = "plugins";
    package_name = "networking_plugins.zip";
}

// [INTENT] Sets a callback function to be executed upon successful completion.
void UpgradeNetworkJob::on_success(std::function<void()> success) { m_success_fun = success; }

// [INTENT] Updates the status of the job and sends an event to the UI thread.
void UpgradeNetworkJob::update_status(Ctl& ctl, int st, const std::string& msg)
{
    BOOST_LOG_TRIVIAL(info) << "UpgradeNetworkJob: percent = " << st << "msg = " << msg;
    ctl.update_status(st, msg);
    // [EVENT] Post an event to the UI thread to update the progress display.
    wxCommandEvent event(EVT_UPGRADE_UPDATE_MESSAGE);
    event.SetString(msg);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}

// [INTENT] The main worker method for the job. It downloads and installs the networking plugins.
// [THREAD] This method is executed on a worker thread.
void UpgradeNetworkJob::process(Ctl& ctl)
{
    // downloading
    int result = 0;

    AppConfig* app_config = wxGetApp().app_config;
    if (!app_config)
        return;

    BOOST_LOG_TRIVIAL(info) << "[UpgradeNetworkJob process]: enter";

    // get temp path
    fs::path target_file_path = (fs::temp_directory_path() / package_name);
    fs::path tmp_path         = target_file_path;
    auto     path_str         = tmp_path.string() + wxString::Format(".%d%s", get_current_pid(), ".tmp").ToStdString();
    tmp_path                  = fs::path(path_str);

    BOOST_LOG_TRIVIAL(info) << "UpgradeNetworkJob: save netowrk_plugin to " << tmp_path.string();

    auto cancel_fn    = [&ctl]() { return ctl.was_canceled(); };
    int  curr_percent = 0;
    // [INTENT] Download the plugin package.
    result = wxGetApp().download_plugin(
        name, package_name,
        [this, &ctl, &curr_percent](int state, int percent, bool& cancel) {
            if (state == InstallStatusNormal) {
                update_status(ctl, percent, _u8L("Downloading"));
            } else if (state == InstallStatusDownloadFailed) {
                update_status(ctl, percent, _u8L("Download failed"));
            } else {
                update_status(ctl, percent, _u8L("Downloading"));
            }
            curr_percent = percent;
        },
        cancel_fn);

    if (ctl.was_canceled()) {
        update_status(ctl, 0, _u8L("Canceled"));
        wxCommandEvent event(wxEVT_CLOSE_WINDOW);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    if (result < 0) {
        update_status(ctl, 0, _u8L("Download failed"));
        wxCommandEvent event(EVT_DOWNLOAD_NETWORK_FAILED);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    // [INTENT] Install the downloaded plugin package.
    result = wxGetApp().install_plugin(
        name, package_name,
        [this, &ctl](int state, int percent, bool& cancel) {
            if (state == InstallStatusInstallCompleted) {
                update_status(ctl, percent, _u8L("Installed successfully"));
            } else {
                update_status(ctl, percent, _u8L("Installing"));
            }
        },
        cancel_fn);

    if (ctl.was_canceled()) {
        update_status(ctl, 0, _u8L("Canceled"));
        wxCommandEvent event(wxEVT_CLOSE_WINDOW);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    if (result != 0) {
        update_status(ctl, 0, _u8L("Install failed"));
        wxCommandEvent event(EVT_INSTALL_NETWORK_FAILED);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    // [EVENT] Notify the UI of successful completion.
    wxCommandEvent event(EVT_UPGRADE_NETWORK_SUCCESS);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
    BOOST_LOG_TRIVIAL(info) << "[UpgradeNetworkJob process]: exit";
    return;
}

// [INTENT] This method is called on the main UI thread after the job finishes.
// It is used for cleanup and exception handling.
// [THREAD] This method is executed on the main UI thread.
void UpgradeNetworkJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
        eptr = nullptr;
    } catch (...) {
        eptr = std::current_exception();
    }

    if (canceled || eptr)
        return;
}

// [INTENT] Sets the UI window handle to which events will be posted.
void UpgradeNetworkJob::set_event_handle(wxWindow* hanle) { m_event_handle = hanle; }

}} // namespace Slic3r::GUI
