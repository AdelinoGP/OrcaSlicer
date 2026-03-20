#ifndef PrintJOB_HPP
#define PrintJOB_HPP

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include "libslic3r/PrintConfig.hpp"
#include "Job.hpp"
#include "slic3r/GUI/DeviceCore/DevStorage.h"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

class Plater;

#define PRINT_JOB_SENDING_TIMEOUT 25

// [STATE] Timeout in seconds before the PrintJob tears down a stalled network transfer and alerts the UI about the failure.

// [STATE] Captures which plate and temporary 3MF artifacts the PrintJob will use, keeping the plater selection and disk locations aligned
// with the task.
class PrintPrepareData
{
public:
    bool     is_from_plater = true;
    int      plate_idx;
    fs::path _3mf_path;
    fs::path _3mf_config_path;
    fs::path _temp_path;
    PrintPrepareData() { plate_idx = 0; }
};

// [STATE] Mirrors plate-count, current index, and bed-type awareness so PrintJob can present accurate previews and summaries to the UI controller.
class PlateListData
{
public:
    bool    is_valid        = false;
    int     plate_count     = 0;
    int     cur_plate_index = 0;
    BedType bed_type        = BedType::btDefault;
    // [PORTING_HAZARD:P3] Relies on `BedType` from libslic3r; Unity needs a shared enum to mirror bed sizing and default profiles.
};

// [INTENT] Orchestrates network or SD-card print delivery for a selected project by pairing device metadata, per-plate settings, and
// connection hooks. [UNITY] Replace with a `MonoBehaviour` or `ScriptableObject` `PrintJobController` that schedules `Task.Run` uploads and
// posts success/failure back to Unity UI via a singleton `MainThreadDispatcher`. [OPENGL] This file does not drive GL rendering; progress
// is surfaced through higher-level panels that subscribe to the job's events.
class PrintJob : public Job
{
    // [THREAD] Worker-to-UI callbacks capture the UI actions fired from the Job base once the background upload completes.
    std::function<void()> m_success_fun{nullptr};
    std::string           m_dev_id;
    bool                  m_job_finished{false};
    int                   m_print_job_completed_id = 0;
    wxString              m_completed_evt_data;
    std::function<void()> m_enter_ip_address_fun_fail{nullptr};
    std::function<void()> m_enter_ip_address_fun_success{nullptr};
    Plater*               m_plater;

public:
    // [STATE] Keeps the selected plate, 3MF paths, and intermediate config so the job can replay the exact transfer sequence from the plater.
    PrintPrepareData job_data;
    // [STATE] Mirrors how many plates exist and which index is currently active so UI lists can stay in sync with the worker job.
    PlateListData plate_data;

    void prepare();
    PrintJob(std::string dev_id = "");

    std::string m_project_name;
    std::string m_dev_ip;
    std::string m_ftp_folder;
    std::string m_access_code;
    std::string task_bed_type;
    std::string task_nozzle_mapping;
    std::string task_ams_mapping;
    std::string task_ams_mapping2;
    std::string task_ams_mapping_info;
    std::string task_nozzles_info;
    std::string connection_type;
    std::string m_print_type;
    std::string m_dst_path;

    bool m_is_calibration_task = false;

    int m_print_from_sdc_plate_idx = 0;

    // [STATE] Determines credential/path options that drive the UI switches for SSL, AMS, and SD card handling inside the print workflow.
    bool m_local_use_ssl{true};
    bool m_local_use_ssl_for_ftp{true};
    bool task_bed_leveling;
    bool task_flow_cali;
    bool task_vibration_cali;
    bool task_record_timelapse;
    bool task_layer_inspect;
    bool cloud_print_only{false};
    bool has_sdcard{false};
    bool could_emmc_print{false};
    bool task_use_ams{true};

    // [PORTING_HAZARD:P2] DevStorage tightly couples to wxWidgets device APIs; Unity needs a cross-platform storage provider and may
    // require native plugins to get SD/MMC state.
    DevStorage::SdcardState sdcard_state = DevStorage::SdcardState::NO_SDCARD;
    bool                    task_ext_change_assist{false};

    int auto_bed_leveling{0};
    int auto_flow_cali{0};
    int auto_offset_cali{0};

    // [INTENT] Captures the bed/nozzle/x-y tilt calibration options chosen in the dialog so the worker thread can include them in the payload.
    void set_print_config(std::string bed_type,
                          bool        bed_leveling,
                          bool        flow_cali,
                          bool        vabration_cali,
                          bool        record_timelapse,
                          bool        layer_inspect,
                          bool        ext_change_assist,
                          int         auto_bed_levelingt,
                          int         auto_flow_calit,
                          int         auto_offset_calit)
    {
        task_bed_type          = bed_type;
        task_bed_leveling      = bed_leveling;
        task_flow_cali         = flow_cali;
        task_vibration_cali    = vabration_cali;
        task_record_timelapse  = record_timelapse;
        task_layer_inspect     = layer_inspect;
        task_ext_change_assist = ext_change_assist;

        auto_bed_leveling = auto_bed_levelingt;
        auto_flow_cali    = auto_flow_calit;
        auto_offset_cali  = auto_offset_calit;
    }

    int status_range() const { return 100; }

    // [STATE] Mirrors whether the background upload already reported completion so we do not double-fire events.
    bool is_finished() { return m_job_finished; }
    // [EVENT] Stores the ID/data that will later be posted to the wx event queue to update the PrintJob panel.
    void set_print_job_finished_event(int event_id, wxString evt_data = wxEmptyString)
    {
        m_print_job_completed_id = event_id;
        m_completed_evt_data     = evt_data;
    }
    // [EVENT] Called by panels to refresh UI once the job completes.
    void on_success(std::function<void()> success);
    // [THREAD] `process` runs on the worker/Job thread; it marshals all network/storage calls and uses `finish()` to signal the UI.
    void process(Ctl& ctl) override;
    // [EVENT] Called after `process()` either completes or throws so the UI can re-enable buttons and surf the result message.
    void finalize(bool canceled, std::exception_ptr& e) override;
    void set_project_name(std::string name);
    void set_dst_name(std::string path);
    // [EVENT] The UI registers these to pop IP dialogs when the device refuses the connection.
    void on_check_ip_address_fail(std::function<void()> func);
    void on_check_ip_address_success(std::function<void()> func);
    // void connect_to_local_mqtt();
    // [PORTING_HAZARD:P3] Uses `wxString` and wxHTTP helpers; Unity should expose similar HTTP error formatting through a shared networking
    // helper to avoid wxWidgets types.
    wxString    get_http_error_msg(unsigned int status, std::string body);
    std::string truncate_string(const std::string& str, size_t maxLength);
    void        set_calibration_task(bool is_calibration);

    // [PORTING_HAZARD:P3] Mixed use of boost filesystem, wxString, and the legacy Job wiring means the Unity port should replace this with
    // a dedicated FileTransferService plus `UnityEvent` wrappers rather than copying the C++ event IDs verbatim.
};

}} // namespace Slic3r::GUI

#endif
