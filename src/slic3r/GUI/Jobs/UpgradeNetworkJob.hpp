#ifndef __UpgradeNetworkJob_HPP__
#define __UpgradeNetworkJob_HPP__

// [INTENT]
// Declares `UpgradeNetworkJob` which manages networking plugin download/installation.
//
// [UNITY]
// In a Unity port:
// - Replace `UpgradeNetworkJob` with an `UpgradeNetworkManager` MonoBehaviour.
// - Replace wxWidgets events/handlers with C# Actions or UnityEvents.
//
// [PORTING_HAZARD:P2]
// - `wxWindow* m_event_handle` is the central mechanism for GUI thread communication;
//   in Unity, this must be replaced by direct invocation on a MonoBehaviour or
//   a dedicated UI Event system.
// - File system dependencies (`boost::filesystem`) must be ported to C# standard
//   IO APIs (`System.IO`).

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <functional>
#include "Job.hpp"
#include <wx/window.h>

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

enum PluginInstallStatus {
    InstallStatusNormal            = 0,
    InstallStatusDownloadFailed    = 1,
    InstallStatusDownloadCompleted = 2,
    InstallStatusUnzipFailed       = 3,
    InstallStatusInstallCompleted  = 4,
};

typedef std::function<void(int status, int percent, bool& cancel)> InstallProgressFn;

class UpgradeNetworkJob : public Job
{
    wxWindow*             m_event_handle{nullptr};
    std::function<void()> m_success_fun{nullptr};
    bool                  m_job_finished{false};
    int                   m_print_job_completed_id = 0;

    InstallProgressFn pro_fn{nullptr};

protected:
    std::string name;
    std::string package_name;

public:
    UpgradeNetworkJob();

    int status_range() const { return 100; }

    bool is_finished() { return m_job_finished; }

    void on_success(std::function<void()> success);
    void update_status(Ctl& ctl, int st, const std::string& msg);
    void process(Ctl& ctl) override;
    void finalize(bool canceled, std::exception_ptr& e) override;
    void set_event_handle(wxWindow* hanle);
};

wxDECLARE_EVENT(EVT_UPGRADE_UPDATE_MESSAGE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UPGRADE_NETWORK_SUCCESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_DOWNLOAD_NETWORK_FAILED, wxCommandEvent);
wxDECLARE_EVENT(EVT_INSTALL_NETWORK_FAILED, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
