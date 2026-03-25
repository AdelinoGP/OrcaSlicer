// [INTENT]
// This file implements the `SendJob` class, a background job for sending a
// G-code file to the printer's SD card without starting a print. It is similar
// to `PrintJob` but is specialized for file transfer.
//
// The job handles the connection to the printer (primarily over LAN) and the
// file upload process. It provides feedback to the user through status updates
// and handles various error conditions.
//
// The core logic is in the `process()` method, which runs on a worker thread. It
// performs the following steps:
// 1. Gathers file paths in `prepare()`.
// 2. Validates that a valid G-code file exists for the selected plate.
// 3. Populates a `PrintParams` struct with the necessary information.
// 4. Calls the `start_send_gcode_to_sdcard` method on the `NetworkAgent` to
//    initiate the file transfer.
// 5. Uses callbacks to monitor progress and handle cancellation and errors.
// 6. Upon successful completion, it posts an event to the main UI thread.
//
// [UNITY]
// In a Unity port, this would be a C# class that uses an async Task to manage
// the file transfer.
// - Networking would be handled using a C# FTP client or a custom implementation
//   for the printer's file transfer protocol.
// - UI updates would be managed through data binding or events.

#include "SendJob.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/format.hpp"

namespace Slic3r { namespace GUI {

static auto check_gcode_failed_str      = _u8L("Abnormal print file data. Please slice again.");
static auto printjob_cancel_str         = _u8L("Task canceled.");
static auto timeout_to_upload_str       = _u8L("Upload task timed out. Please check the network status and try again.");
static auto failed_in_cloud_service_str = _u8L("Cloud service connection failed. Please try again.");
static auto file_is_not_exists_str      = _u8L("Print file not found. Please slice again.");
static auto file_over_size_str          = _u8L(
    "The print file exceeds the maximum allowable size (1GB). Please simplify the model and slice again.");
static auto print_canceled_str    = _u8L("Task canceled.");
static auto send_print_failed_str = _u8L("Failed to send the print job. Please try again.");
static auto upload_ftp_failed_str = _u8L("Failed to upload file to ftp. Please try again.");

static auto desc_network_error     = _u8L("Check the current status of the bambu server by clicking on the link above.");
static auto desc_file_too_large    = _u8L("The size of the print file is too large. Please adjust the file size and try again.");
static auto desc_fail_not_exist    = _u8L("Print file not found, please slice it again and send it for printing.");
static auto desc_upload_ftp_failed = _u8L("Failed to upload print file to FTP. Please check the network status and try again.");

static auto sending_over_lan_str   = _u8L("Sending print job over LAN");
static auto sending_over_cloud_str = _u8L("Sending print job through cloud service");

SendJob::SendJob(std::string dev_id) : m_plater{wxGetApp().plater()}, m_dev_id(dev_id)
{
    m_print_job_completed_id = m_plater->get_send_finished_event();
}

// [INTENT] Gathers the necessary file paths from the Plater.
// [THREAD] This method is called on the main UI thread from `process()`.
void SendJob::prepare()
{
    m_plater->get_print_job_data(&job_data);
    std::string temp_file              = Slic3r::resources_dir() + "/check_access_code.txt";
    auto        check_access_code_path = temp_file.c_str();
    BOOST_LOG_TRIVIAL(trace) << "sned_job: check_access_code_path = " << check_access_code_path;
    job_data._temp_path = fs::path(check_access_code_path);
}

// [INTENT] Parses an HTTP error response and returns a formatted error message.
wxString SendJob::get_http_error_msg(unsigned int status, std::string body)
{
    int         code = 0;
    std::string error;
    std::string message;
    wxString    result;
    if (status >= 400 && status < 500)
        try {
            json j = json::parse(body);
            if (j.contains("code")) {
                if (!j["code"].is_null())
                    code = j["code"].get<int>();
            }
            if (j.contains("error")) {
                if (!j["error"].is_null())
                    error = j["error"].get<std::string>();
            }
            if (j.contains("message")) {
                if (!j["message"].is_null())
                    message = j["message"].get<std::string>();
            }
        } catch (...) {
            ;
        }
    else if (status == 503) {
        return _L("Service Unavailable");
    } else {
        wxString unkown_text = _L("Unknown Error.");
        unkown_text += wxString::Format("status=%u, body=%s", status, body);
        return unkown_text;
    }

    BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

    result = wxString::Format("code=%u, error=%s", code, from_u8(error));
    return result;
}

// [INTENT] A helper function to format a size in bytes into a human-readable string (KB, MB).
inline std::string get_transform_string(int bytes)
{
    float ms = (float) bytes / 1024.0f / 1024.0f;
    float ks = (float) bytes / 1024.0f;
    char  buffer[32];
    if (ms > 0)
        ::sprintf(buffer, "%.1fM", ms);
    else if (ks > 0)
        ::sprintf(buffer, "%.1fK", ks);
    else
        ::sprintf(buffer, "%.1fK", ks);
    return buffer;
}

// [INTENT] The main worker method for the SendJob. It orchestrates the process of
// sending a G-code file to the printer's SD card.
// [THREAD] This method is executed on a worker thread.
void SendJob::process(Ctl& ctl)
{
    PrintParams   params;
    std::string   msg;
    int           curr_percent = 10;
    NetworkAgent* agent        = wxGetApp().getAgent();
    AppConfig*    config       = wxGetApp().app_config;
    int           result       = -1;
    std::string   http_body;

    if (this->connection_type == "lan") {
        msg = _u8L("Sending print job over LAN");
    } else {
        msg = _u8L("Sending print job through cloud service");
    }

    ctl.call_on_main_thread([this] { prepare(); }).wait();
    ctl.update_status(0, msg);
    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    // [STATE] Find a plate with a valid G-code file to send.
    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        if (job_data.plate_idx == PLATE_ALL_IDX) {
            // all plate
            for (int index = 0; index < total_plate_num; index++) {
                PartPlate* plate_n = m_plater->get_partplate_list().get_plate(index);
                if (plate_n && plate_n->is_valid_gcode_file()) {
                    plate = plate_n;
                    break;
                }
            }
        } else {
            plate = m_plater->get_partplate_list().get_curr_plate();
        }
        if (plate == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "can not find plate with valid gcode file when sending to print, plate_index="
                                     << job_data.plate_idx;
            ctl.update_status(curr_percent, check_gcode_failed_str);
            return;
        }
    }

    /* check gcode is valid */
    if (!plate->is_valid_gcode_file()) {
        ctl.update_status(curr_percent, check_gcode_failed_str);
        return;
    }

    if (ctl.was_canceled()) {
        ctl.update_status(curr_percent, printjob_cancel_str);
        return;
    }

    // [STATE] Populate the `PrintParams` struct with the necessary information for the file transfer.
    std::string project_name   = wxGetApp().plater()->get_project_name().ToUTF8().data();
    int         curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

    params.dev_id       = m_dev_id;
    params.project_name = m_project_name + ".gcode.3mf";
    params.preset_name  = wxGetApp().preset_bundle->prints.get_selected_preset_name();

    if (wxGetApp().plater()->using_exported_file())
        params.filename = wxGetApp().plater()->get_3mf_filename();
    else
        params.filename = job_data._3mf_path.string();

    params.config_filename = job_data._3mf_config_path.string();

    params.plate_index     = curr_plate_idx;
    params.ams_mapping     = this->task_ams_mapping;
    params.connection_type = this->connection_type;
    params.task_use_ams    = this->task_use_ams;

    // local print access
    params.dev_ip           = m_dev_ip;
    params.username         = "bblp";
    params.password         = m_access_code;
    params.use_ssl_for_ftp  = m_local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt = m_local_use_ssl;
    wxString    error_text;
    std::string msg_text;

    const int StagePercentPoint[(int) PrintingStageFinished + 1] = {
        20,  // PrintingStageCreate
        30,  // PrintingStageUpload
        99,  // PrintingStageWaiting
        99,  // PrintingStageRecord
        99,  // PrintingStageSending
        100, // PrintingStageFinished
        100  // PrintingStageFinished
    };

    // [EVENT] A callback function to receive status updates during the file transfer.
    auto update_fn = [this, &ctl, &msg, &curr_percent, &error_text, StagePercentPoint](int stage, int code, std::string info) {
        if (stage == SendingPrintJobStage::PrintingStageCreate) {
            if (this->connection_type == "lan") {
                msg = _u8L("Sending G-code file over LAN");
            } else {
                msg = _u8L("Sending G-code file to SD card");
            }
        } else if (stage == SendingPrintJobStage::PrintingStageUpload) {
            if (code >= 0 && code <= 100 && !info.empty()) {
                if (this->connection_type == "lan") {
                    msg = _u8L("Sending G-code file over LAN");
                } else {
                    msg = _u8L("Sending G-code file to SD card");
                }
                if (!info.empty()) {
                    msg += format("(%s)", info);
                }
            }
        } else if (stage == SendingPrintJobStage::PrintingStageFinished) {
            msg = format(_u8L("Successfully sent. Close current page in %s s"), info);
        } else {
            if (this->connection_type == "lan") {
                msg = _u8L("Sending G-code file over LAN");
            } else {
                msg = _u8L("Sending G-code file over LAN");
            }
        }

        // update current percnet
        if (stage >= 0 && stage <= (int) PrintingStageFinished) {
            curr_percent = StagePercentPoint[stage];
            if ((stage == SendingPrintJobStage::PrintingStageUpload) && (code > 0 && code <= 100)) {
                curr_percent = (StagePercentPoint[stage + 1] - StagePercentPoint[stage]) * code / 100 + StagePercentPoint[stage];
            }
        }

        // get errors
        if (code > 100 || code < 0 || stage == SendingPrintJobStage::PrintingStageERROR) {
            if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE) {
                m_plater->update_print_error_info(code, desc_file_too_large, info);
            } else if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
                m_plater->update_print_error_info(code, desc_fail_not_exist, info);
            } else if (code == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || code == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
                m_plater->update_print_error_info(code, desc_upload_ftp_failed, info);
            } else {
                m_plater->update_print_error_info(code, desc_network_error, info);
            }
        } else {
            ctl.update_status(curr_percent, msg);
        }
    };

    // [EVENT] A callback function to check if the job has been canceled.
    auto cancel_fn = [&ctl]() { return ctl.was_canceled(); };

    // [INTENT] Start the file transfer process.
    if (params.connection_type != "lan") {
        if (params.dev_ip.empty())
            params.comments = "no_ip";
        else if (this->cloud_print_only)
            params.comments = "low_version";
        else if (!this->has_sdcard)
            params.comments = "no_sdcard";
        else if (params.password.empty())
            params.comments = "no_password";

        if (!params.password.empty() && !params.dev_ip.empty() && this->has_sdcard) {
            // try to send local with record
            BOOST_LOG_TRIVIAL(info) << "send_job: try to send gcode to printer";
            ctl.update_status(curr_percent, _u8L("Sending G-code file over LAN"));
            result = agent->start_send_gcode_to_sdcard(params, update_fn, cancel_fn, nullptr);
            if (result == BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED) {
                params.comments = "upload_failed";
            } else {
                params.comments = (boost::format("failed(%1%)") % result).str();
            }
            if (result < 0) {
                // try to send with cloud
                BOOST_LOG_TRIVIAL(info) << "send_job: try to send gcode file to printer";
                ctl.update_status(curr_percent, _u8L("Sending G-code file over LAN"));
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << "send_job: try to send gcode file to printer";
            ctl.update_status(curr_percent, _u8L("Sending G-code file over LAN"));
        }
    } else {
        switch (this->sdcard_state) {
        case DevStorage::SdcardState::NO_SDCARD:
            ctl.update_status(curr_percent, _u8L("Storage needs to be inserted before sending to printer."));
            return;
        case DevStorage::SdcardState::HAS_SDCARD_ABNORMAL:
            if (this->has_sdcard) {
                // means the sdcard is abnormal but can be used option is enabled
                ctl.update_status(curr_percent, _u8L("Sending G-code file over LAN, but the Storage in the printer is abnormal and "
                                                     "print-issues may be caused by this."));
                result = agent->start_send_gcode_to_sdcard(params, update_fn, cancel_fn, nullptr);
                break;
            }
            ctl.update_status(
                curr_percent,
                _u8L("The Storage in the printer is abnormal. Please replace it with a normal Storage before sending to printer."));
            return;
        case DevStorage::SdcardState::HAS_SDCARD_READONLY:
            ctl.update_status(
                curr_percent,
                _u8L("The Storage in the printer is read-only. Please replace it with a normal Storage before sending to printer."));
            return;
        case DevStorage::SdcardState::HAS_SDCARD_NORMAL:
            ctl.update_status(curr_percent, _u8L("Sending G-code file over LAN"));
            result = agent->start_send_gcode_to_sdcard(params, update_fn, cancel_fn, nullptr);
            break;
        default: ctl.update_status(curr_percent, _u8L("Encountered an unknown error with the Storage status. Please try again.")); return;
        }
    }

    if (ctl.was_canceled()) {
        ctl.update_status(curr_percent, printjob_cancel_str);
        return;
    }

    // [INTENT] Handle the result of the file transfer.
    if (result < 0) {
        curr_percent = -1;

        if (result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
            msg_text = file_is_not_exists_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE || result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE) {
            msg_text = file_over_size_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_CHECK_MD5_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SP_CHECK_MD5_FAILED) {
            msg_text = failed_in_cloud_service_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_GET_NOTIFICATION_TIMEOUT ||
                   result == BAMBU_NETWORK_ERR_PRINT_SP_GET_NOTIFICATION_TIMEOUT) {
            msg_text = timeout_to_upload_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
            msg_text = upload_ftp_failed_str;
        } else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            msg_text = print_canceled_str;
        } else {
            msg_text = send_print_failed_str;
        }

        if (result != BAMBU_NETWORK_ERR_CANCELED) {
            ctl.show_error_info(msg_text, 0, "", "");
        }
        BOOST_LOG_TRIVIAL(error) << "send_job: failed, result = " << result;

    } else {
        BOOST_LOG_TRIVIAL(error) << "send_job: send ok.";
        // [EVENT] Post an event to notify the UI that the file has been successfully sent.
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        evt->SetString(from_u8(params.project_name));
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

// [INTENT] Sets a callback function to be executed upon successful completion of the job.
void SendJob::on_success(std::function<void()> success) { m_success_fun = success; }

// [INTENT] Sets a callback function to be executed if the IP address check fails.
void SendJob::on_check_ip_address_fail(std::function<void(int)> func) { m_enter_ip_address_fun_fail = func; }

// [INTENT] Sets a callback function to be executed if the IP address check succeeds.
void SendJob::on_check_ip_address_success(std::function<void()> func) { m_enter_ip_address_fun_success = func; }

// [INTENT] This method is called on the main UI thread after the job finishes.
// It is used for cleanup and exception handling.
// [THREAD] This method is executed on the main UI thread.
void SendJob::finalize(bool canceled, std::exception_ptr& eptr)
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

void SendJob::set_project_name(std::string name) { m_project_name = name; }

}} // namespace Slic3r::GUI
