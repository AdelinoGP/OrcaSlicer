// [INTENT]
// This header file declares the `SendJob` class, a background job for sending a
// G-code file to the printer's SD card without starting a print. It is similar
// to `PrintJob` but specialized for file transfer.
//
// [UNITY]
// In a Unity port, this class would be replaced by a C# class that manages the
// file transfer process using async/await Tasks. The member variables would
// become fields in that C# class.

#ifndef SendJOB_HPP
#define SendJOB_HPP

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "slic3r/GUI/DeviceCore/DevStorage.h"
#include "Job.hpp"
#include "PrintJob.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI {

class Plater;

typedef std::function<void(int status, int code, std::string msg)> OnUpdateStatusFn;
typedef std::function<bool()>                                      WasCancelledFn;

class SendJob : public Job
{
    // [THREAD] This class runs on a background worker thread.
    // [PORTING_HAZARD:P2] wxWindow* reference prevents direct background-thread UI manipulation.
    // [STATE] Data about the print job, gathered from the Plater.
    PrintPrepareData job_data;
    // [STATE] The ID of the target device.
    std::string m_dev_id;
    // [STATE] A flag indicating whether the job has finished.
    bool m_job_finished{false};
    // [STATE] The event ID to be posted upon completion.
    int m_print_job_completed_id = 0;
    // [STATE] A flag for a check-only mode.
    bool m_is_check_mode{false};
    // [STATE] A flag to continue after checking.
    bool m_check_and_continue{false};
    // [EVENT] A callback function to be executed upon successful completion.
    std::function<void()> m_success_fun{nullptr};
    // [EVENT] A callback function to be executed if the IP address check fails.
    std::function<void(int)> m_enter_ip_address_fun_fail{nullptr};
    // [EVENT] A callback function to be executed if the IP address check succeeds.
    std::function<void()> m_enter_ip_address_fun_success{nullptr};
    // [STATE] A pointer to the Plater.
    Plater* m_plater;

public:
    // [INTENT] Gathers the necessary file paths from the Plater.
    void prepare();
    SendJob(std::string dev_id = "");

    // [STATE] The name of the project.
    std::string m_project_name;
    // [STATE] The IP address of the target device.
    std::string m_dev_ip;
    // [STATE] The access code for the target device.
    std::string m_access_code;
    // [STATE] The bed type for the print job.
    std::string task_bed_type;
    // [STATE] The AMS mapping for the print job.
    std::string task_ams_mapping;
    // [STATE] The connection type ("lan" or "cloud").
    std::string connection_type;

    // [STATE] Flags for connection and print settings.
    bool m_local_use_ssl_for_ftp{true};
    bool m_local_use_ssl{true};
    bool cloud_print_only{false};
    bool has_sdcard{false};
    bool task_use_ams{true};

    // [STATE] The state of the printer's SD card.
    DevStorage::SdcardState sdcard_state = DevStorage::SdcardState::NO_SDCARD;

    // [STATE] A pointer to the parent window.
    wxWindow* m_parent{nullptr};

    int status_range() const { return 100; }

    // [INTENT] Parses an HTTP error response.
    wxString get_http_error_msg(unsigned int status, std::string body);
    void     set_check_mode() { m_is_check_mode = true; };
    void     check_and_continue() { m_check_and_continue = true; };
    bool     is_finished() { return m_job_finished; }
    void     process(Ctl& ctl) override;
    // [INTENT] Sets a callback for successful completion.
    void on_success(std::function<void()> success);
    // [INTENT] Sets a callback for IP address check failure.
    void on_check_ip_address_fail(std::function<void(int)> func);
    // [INTENT] Sets a callback for IP address check success.
    void on_check_ip_address_success(std::function<void()> func);
    void finalize(bool canceled, std::exception_ptr&) override;
    void set_project_name(std::string name);
};

}} // namespace Slic3r::GUI

#endif
