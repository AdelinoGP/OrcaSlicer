// [INTENT] This file implements the NetworkTestDialog, a GUI component for testing network connectivity.
// It provides functionalities such as starting network tests (multi-threaded and single-threaded),
// displaying basic system and network information, and logging test results.
#include "NetworkTestDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/AppConfig.hpp"
#include <boost/asio/ip/address.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp> // [THREAD] Required for boost::thread

namespace Slic3r { namespace GUI {

// [EVENT] Custom event declaration for updating test results.
wxDECLARE_EVENT(EVT_UPDATE_RESULT, wxCommandEvent);

// [EVENT] Custom event definition for updating test results.
wxDEFINE_EVENT(EVT_UPDATE_RESULT, wxCommandEvent);

// [STATE] Static wxString for "N/A" display.
static wxString NA_STR = _L("N/A");

// [INTENT] Constructor for the NetworkTestDialog. Initializes the UI layout and binds event handlers.
// [UNITY] The dialog itself would map to a UI Toolkit Document (UXML) and a custom C# MonoBehaviour.
// wxWidgets sizers will translate to UI Toolkit's USS flexbox layout.
NetworkTestDialog::NetworkTestDialog(
    wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
    : DPIDialog(parent,
                wxID_ANY,
                from_u8((boost::format(_utf8(L("Network Test")))).str()),
                wxDefaultPosition,
                wxSize(1000, 700),
                /*wxCAPTION*/ wxDEFAULT_DIALOG_STYLE | wxMAXIMIZE_BOX | wxMINIMIZE_BOX | wxRESIZE_BORDER)
{
    // [STATE] Sets the background color of the dialog.
    this->SetBackgroundColour(wxColour(255, 255, 255));

    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    // [INTENT] Main sizer for vertical layout of dialog components.
    wxBoxSizer* main_sizer;
    main_sizer = new wxBoxSizer(wxVERTICAL);

    // [INTENT] Creates and adds the top section of the dialog (buttons for starting tests).
    wxBoxSizer* top_sizer = create_top_sizer(this);
    main_sizer->Add(top_sizer, 0, wxEXPAND, 5);

    // [INTENT] Creates and adds the information section of the dialog (basic info, version, DNS).
    wxBoxSizer* info_sizer = create_info_sizer(this);
    main_sizer->Add(info_sizer, 0, wxEXPAND, 5);

    // [INTENT] Creates and adds the content section of the dialog (specific test buttons and their results).
    wxBoxSizer* content_sizer = create_content_sizer(this);
    main_sizer->Add(content_sizer, 0, wxEXPAND, 5);

    // [INTENT] Creates and adds the result logging section.
    wxBoxSizer* result_sizer = create_result_sizer(this);
    main_sizer->Add(result_sizer, 1, wxEXPAND, 5);

    // [INTENT] Sets default values for UI elements.
    set_default();

    // [INTENT] Binds event handlers to UI elements.
    init_bind();

    this->SetSizer(main_sizer);
    this->Layout();

    this->Centre(wxBOTH);
    // [INTENT] Updates the dialog for dark UI theme.
    wxGetApp().UpdateDlgDarkUI(this);
}

// [INTENT] Creates the top sizer containing buttons for starting network tests.
// [UNITY] Buttons would be UI Toolkit Buttons, layouts managed by USS flexbox.
wxBoxSizer* NetworkTestDialog::create_top_sizer(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    auto line_sizer = new wxBoxSizer(wxHORIZONTAL);
    // [STATE] Button to start multi-threaded network test.
    btn_start = new Button(this, _L("Start Test Multi-Thread"));
    btn_start->SetStyle(ButtonStyle::Confirm, ButtonType::Window);
    line_sizer->Add(btn_start, 0, wxALL, 5);

    // [STATE] Button to start single-threaded network test.
    btn_start_sequence = new Button(this, _L("Start Test Single-Thread"));
    btn_start_sequence->SetStyle(ButtonStyle::Regular, ButtonType::Window);

    line_sizer->Add(btn_start_sequence, 0, wxALL, 5);

    // [STATE] Button to download the log.
    btn_download_log = new Button(this, _L("Export Log"));
    btn_download_log->SetStyle(ButtonStyle::Regular, ButtonType::Window);
    line_sizer->Add(btn_download_log, 0, wxALL, 5);
    btn_download_log->Hide();

    // [EVENT] Binds the multi-thread start button to `start_all_job`.
    btn_start->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { start_all_job(); });
    // [EVENT] Binds the single-thread start button to `start_all_job_sequence`.
    btn_start_sequence->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { start_all_job_sequence(); });
    sizer->Add(line_sizer, 0, wxEXPAND, 5);
    return sizer;
}

// [INTENT] Creates the information sizer displaying basic system and version details.
// [UNITY] wxStaticText would be UI Toolkit Labels.
wxBoxSizer* NetworkTestDialog::create_info_sizer(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    // [STATE] Static text for "Basic Info".
    text_basic_info = new wxStaticText(this, wxID_ANY, _L("Basic Info"), wxDefaultPosition, wxDefaultSize, 0);
    text_basic_info->Wrap(-1);
    sizer->Add(text_basic_info, 0, wxALL, 5);

    wxBoxSizer* version_sizer = new wxBoxSizer(wxHORIZONTAL);
    // [STATE] Static text for "OrcaSlicer Version:".
    text_version_title = new wxStaticText(this, wxID_ANY, _L("OrcaSlicer Version:"), wxDefaultPosition, wxDefaultSize, 0);
    text_version_title->Wrap(-1);
    version_sizer->Add(text_version_title, 0, wxALL, 5);

    wxString text_version = get_studio_version();
    // [STATE] Static text for displaying the OrcaSlicer version.
    text_version_val = new wxStaticText(this, wxID_ANY, text_version, wxDefaultPosition, wxDefaultSize, 0);
    text_version_val->Wrap(-1);
    version_sizer->Add(text_version_val, 0, wxALL, 5);
    sizer->Add(version_sizer, 1, wxEXPAND, 5);

    wxBoxSizer* sys_sizer = new wxBoxSizer(wxHORIZONTAL);

    // [STATE] Static text for "System Version:".
    txt_sys_info_title = new wxStaticText(this, wxID_ANY, _L("System Version:"), wxDefaultPosition, wxDefaultSize, 0);
    txt_sys_info_title->Wrap(-1);
    sys_sizer->Add(txt_sys_info_title, 0, wxALL, 5);

    // [STATE] Static text for displaying the system OS information.
    txt_sys_info_value = new wxStaticText(this, wxID_ANY, get_os_info(), wxDefaultPosition, wxDefaultSize, 0);
    txt_sys_info_value->Wrap(-1);
    sys_sizer->Add(txt_sys_info_value, 0, wxALL, 5);

    sizer->Add(sys_sizer, 1, wxEXPAND, 5);

    wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);
    // [STATE] Static text for "DNS Server:".
    txt_dns_info_title = new wxStaticText(this, wxID_ANY, _L("DNS Server:"), wxDefaultPosition, wxDefaultSize, 0);
    txt_dns_info_title->Wrap(-1);
    txt_dns_info_title->Hide();
    line_sizer->Add(txt_dns_info_title, 0, wxALL, 5);

    // [STATE] Static text for displaying the DNS server information.
    txt_dns_info_value = new wxStaticText(this, wxID_ANY, get_dns_info(), wxDefaultPosition, wxDefaultSize, 0);
    txt_dns_info_value->Hide();
    line_sizer->Add(txt_dns_info_value, 0, wxALL, 5);
    sizer->Add(line_sizer, 1, wxEXPAND, 5);

    return sizer;
}

// [INTENT] Creates the content sizer with buttons for specific network tests (e.g., GitHub, Bing).
// [UNITY] Buttons would be UI Toolkit Buttons. wxFlexGridSizer would translate to USS flexbox layout.
wxBoxSizer* NetworkTestDialog::create_content_sizer(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    wxFlexGridSizer* grid_sizer;
    grid_sizer = new wxFlexGridSizer(0, 3, 0, 0);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    grid_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    // [STATE] Button to test connection to OrcaSlicer (GitHub).
    btn_link = new Button(this, _L("Test OrcaSlicer (GitHub)"));
    btn_link->SetStyle(ButtonStyle::Regular, ButtonType::Window);
    grid_sizer->Add(btn_link, 0, wxEXPAND | wxALL, 5);

    // [STATE] Static text for "Test OrcaSlicer (GitHub):".
    text_link_title = new wxStaticText(this, wxID_ANY, _L("Test OrcaSlicer (GitHub):"), wxDefaultPosition, wxDefaultSize, 0);
    text_link_title->Wrap(-1);
    grid_sizer->Add(text_link_title, 0, wxALIGN_RIGHT | wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // [STATE] Static text for displaying the result of the GitHub test.
    text_link_val = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    text_link_val->Wrap(-1);
    grid_sizer->Add(text_link_val, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // [STATE] Button to test connection to bing.com.
    btn_bing = new Button(this, _L("Test bing.com"));
    btn_bing->SetStyle(ButtonStyle::Regular, ButtonType::Window);
    grid_sizer->Add(btn_bing, 0, wxEXPAND | wxALL, 5);

    // [STATE] Static text for "Test bing.com:".
    text_bing_title = new wxStaticText(this, wxID_ANY, _L("Test bing.com:"), wxDefaultPosition, wxDefaultSize, 0);

    text_bing_title->Wrap(-1);
    grid_sizer->Add(text_bing_title, 0, wxALIGN_RIGHT | wxALL | wxALIGN_CENTER_VERTICAL, 5);

    // [STATE] Static text for displaying the result of the Bing test.
    text_bing_val = new wxStaticText(this, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    text_bing_val->Wrap(-1);
    grid_sizer->Add(text_bing_val, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    sizer->Add(grid_sizer, 1, wxEXPAND, 5);

    // [EVENT] Binds the GitHub test button to `start_test_github_thread`.
    btn_link->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { start_test_github_thread(); });

    // [EVENT] Binds the Bing test button to `start_test_bing_thread`.
    btn_bing->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { start_test_bing_thread(); });

    return sizer;
}
// [INTENT] Creates the result sizer for displaying log information.
// [UNITY] wxStaticText and wxTextCtrl would be UI Toolkit Labels and TextAreas.
wxBoxSizer* NetworkTestDialog::create_result_sizer(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    // [STATE] Static text for "Log Info".
    text_result = new wxStaticText(this, wxID_ANY, _L("Log Info"), wxDefaultPosition, wxDefaultSize, 0);
    text_result->Wrap(-1);
    sizer->Add(text_result, 0, wxALL, 5);

    // [STATE] Multi-line text control for displaying test logs.
    txt_log = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    sizer->Add(txt_log, 1, wxALL | wxEXPAND, 5);
    return sizer;
}

// [INTENT] Destructor for NetworkTestDialog. Joins any running threads to ensure proper shutdown.
// [THREAD] Joins `test_job` threads.
NetworkTestDialog::~NetworkTestDialog()
{
    // [INTENT] The explicit body of the destructor is inlined to avoid an empty body.
    // This is often a sign that there might be resources to clean up.
    // [PORTING_HAZARD:P3] Ensuring proper thread shutdown and resource deallocation in Unity
    // will require careful mapping, possibly using C# Tasks and cancellation tokens.
    ;
}

// [INTENT] Initializes all event bindings for the dialog.
void NetworkTestDialog::init_bind()
{
    // [EVENT] Binds the custom EVT_UPDATE_RESULT event to an anonymous lambda function.
    // This lambda updates the UI with test results and appends information to the log.
    Bind(EVT_UPDATE_RESULT, [this](wxCommandEvent& evt) {
        // [INTENT] Update specific UI elements based on the job ID.
        if (evt.GetInt() == TEST_ORCA_JOB) {
            text_link_val->SetLabelText(evt.GetString());
        } else if (evt.GetInt() == TEST_BING_JOB) {
            text_bing_val->SetLabelText(evt.GetString());
        }

        // [INTENT] Formats the current time for the log entry.
        std::time_t       t        = std::time(0);
        std::tm*          now_time = std::localtime(&t);
        std::stringstream buf;
        buf << std::put_time(now_time, "%a %b %d %H:%M:%S");
        wxString info = wxString::Format("%s:", buf.str()) + evt.GetString() + "\n";
        try {
            // [INTENT] Appends the formatted log information to the text control.
            txt_log->AppendText(info);
        } catch (std::exception& e) {
            // [INTENT] Logs exceptions during log printing.
            BOOST_LOG_TRIVIAL(error) << "Unkown Exception in print_log, exception=" << e.what();
            return;
        } catch (...) {
            // [INTENT] Catches and logs generic exceptions during log printing.
            BOOST_LOG_TRIVIAL(error) << "Unkown Exception in print_log";
            return;
        }
        return;
    });

    // [EVENT] Binds the window close event to `on_close` method.
    Bind(wxEVT_CLOSE_WINDOW, &NetworkTestDialog::on_close, this);
}

// [INTENT] Retrieves and returns the operating system information.
wxString NetworkTestDialog::get_os_info()
{
    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);
    std::string os_version       = (boost::format("%1%.%2%.%3%") % major % minor % micro).str();
    wxString    text_sys_version = wxGetOsDescription() + wxString::Format("%d.%d.%d", major, minor, micro);
    return text_sys_version;
}

// [INTENT] Retrieves and returns the DNS server information. Currently returns "N/A".
wxString NetworkTestDialog::get_dns_info() { return NA_STR; }

// [INTENT] Starts both GitHub and Bing network tests in separate threads (multi-threaded).
// [THREAD] Calls `start_test_github_thread` and `start_test_bing_thread`.
void NetworkTestDialog::start_all_job()
{
    start_test_github_thread();
    start_test_bing_thread();
}

// [INTENT] Starts GitHub and Bing network tests sequentially in a single new boost thread.
// [THREAD] Creates a new `boost::thread` to run tests sequentially.
// [PORTING_HAZARD:P2] Direct `boost::thread` usage will need to be replaced with C# `Task` or `async/await` patterns.
void NetworkTestDialog::start_all_job_sequence()
{
    m_sequence_job = new boost::thread([this] {
        update_status(-1, "start_test_sequence");
        start_test_url(TEST_BING_JOB, "Bing", "http://www.bing.com");
        if (m_closing)
            return; // [STATE] Check if dialog is closing to prevent further operations.
        start_test_url(TEST_ORCA_JOB, "OrcaSlicer(GitHub)", "https://github.com/OrcaSlicer/OrcaSlicer");
        if (m_closing)
            return; // [STATE] Check if dialog is closing.
        update_status(-1, "end_test_sequence");
    });
}

// [INTENT] Starts a network test for a given URL, updates status, and logs results.
// [THREAD] This function performs synchronous HTTP requests, which could block the calling thread.
// [PORTING_HAZARD:P2] The `perform_sync()` call for HTTP requests should be replaced with async operations in Unity.
// [EVENT] Uses `update_status` to dispatch UI updates from a non-UI thread.
void NetworkTestDialog::start_test_url(TestJob job, wxString name, wxString url)
{
    // [STATE] Mark job as in testing.
    m_in_testing[job] = true;
    wxString info     = wxString::Format("test %s start...", name);

    update_status(job, info);

    // [INTENT] Performs an HTTP GET request.
    Slic3r::Http http = Slic3r::Http::get(url.ToStdString());
    info              = wxString::Format("[test %s]: url=%s", name, url);

    update_status(-1, info);

    int result = -1;
    http.timeout_max(10)
        // [EVENT] Callback for successful HTTP request completion.
        .on_complete([this, &result](std::string body, unsigned status) {
            try {
                if (status == 200) {
                    result = 0;
                }
            } catch (...) {
                ;
            }
        })
        // [EVENT] Callback for IP resolution.
        .on_ip_resolve([this, name, job](std::string ip) {
            wxString ip_report = wxString::Format("test %s ip resolved = %s", name, ip);
            update_status(job, ip_report);
        })
        // [EVENT] Callback for HTTP request error.
        .on_error([this, name, job](std::string body, std::string error, unsigned int status) {
            wxString info = wxString::Format("status=%u, body=%s, error=%s", status, body, error);
            this->update_status(job, wxString::Format("test %s failed", name));
            this->update_status(-1, info);
        })
        .perform_sync(); // [THREAD] Synchronous call.
    if (result == 0) {
        update_status(job, wxString::Format("test %s ok", name));
    }
    // [STATE] Mark job as not in testing.
    m_in_testing[job] = false;
}

// [INTENT] Placeholder for a ping test thread. Currently does nothing beyond marking state.
// [THREAD] Creates a new `boost::thread`.
// [PORTING_HAZARD:P3] Incomplete implementation, requires actual ping logic.
void NetworkTestDialog::start_test_ping_thread()
{
    test_job[TEST_PING_JOB] = new boost::thread([this] {
        m_in_testing[TEST_PING_JOB] = true;

        m_in_testing[TEST_PING_JOB] = false;
    });
}
// [INTENT] Starts the GitHub network test in a new boost thread.
// [THREAD] Creates a new `boost::thread` for the GitHub test.
// [PORTING_HAZARD:P2] Direct `boost::thread` usage.
void NetworkTestDialog::start_test_github_thread()
{
    // [STATE] Prevents starting the test if it's already running.
    if (m_in_testing[TEST_ORCA_JOB])
        return;
    test_job[TEST_ORCA_JOB] = new boost::thread([this] {
        // [INTENT] Calls the generic URL test function.
        start_test_url(TEST_ORCA_JOB, "OrcaSlicer(GitHub)", "https://github.com/OrcaSlicer/OrcaSlicer");
    });
}
// [INTENT] Starts the Bing network test in a new boost thread.
// [THREAD] Creates a new `boost::thread` for the Bing test.
// [PORTING_HAZARD:P2] Direct `boost::thread` usage.
void NetworkTestDialog::start_test_bing_thread()
{
    test_job[TEST_BING_JOB] = new boost::thread([this] {
        // [INTENT] Calls the generic URL test function.
        start_test_url(TEST_BING_JOB, "Bing", "http://www.bing.com");
    });
}

// [INTENT] Handles the window close event. Sets flags and joins running threads.
// [EVENT] wxEVT_CLOSE_WINDOW handler.
// [THREAD] Joins `test_job` threads to ensure they complete before the dialog closes.
void NetworkTestDialog::on_close(wxCloseEvent& event)
{
    // [STATE] Flags to indicate download cancellation and dialog closing.
    m_download_cancel = true;
    m_closing         = true;
    for (int i = 0; i < TEST_JOB_MAX; i++) {
        if (test_job[i]) {
            test_job[i]->join(); // [THREAD] Blocks until thread finishes.
            test_job[i] = nullptr;
        }
    }

    event.Skip();
}

// [INTENT] Returns the current studio (OrcaSlicer) version.
wxString NetworkTestDialog::get_studio_version() { return wxString(SoftFever_VERSION); }

// [INTENT] Sets the default state for various UI elements and member variables.
void NetworkTestDialog::set_default()
{
    // [STATE] Initializes `test_job` and `m_in_testing` arrays.
    for (int i = 0; i < TEST_JOB_MAX; i++) {
        test_job[i]     = nullptr;
        m_in_testing[i] = false;
    }

    // [STATE] Initializes `m_sequence_job`.
    m_sequence_job = nullptr;

    // [STATE] Sets initial text for version, system info, DNS, and test results.
    text_version_val->SetLabelText(get_studio_version());
    txt_sys_info_value->SetLabelText(get_os_info());
    txt_dns_info_value->SetLabelText(get_dns_info());
    text_link_val->SetLabelText(NA_STR);
    text_bing_val->SetLabelText(NA_STR);
    // [STATE] Initializes flags.
    m_download_cancel = false;
    m_closing         = false;
}

// [INTENT] Handles DPI changes, which would typically involve adjusting UI element sizes and positions.
// Currently an empty implementation.
// [UNITY] Unity UI scales automatically. This method's logic would be integrated into responsive UI layout if custom DPI handling is needed.
void NetworkTestDialog::on_dpi_changed(const wxRect& suggested_rect) { ; }

// [INTENT] Updates the UI status and log through a custom event, marshaling data to the UI thread.
// [THREAD] This method is designed to be called from non-UI threads.
// [EVENT] Creates and queues a custom `wxCommandEvent` (`EVT_UPDATE_RESULT`) to be processed on the UI thread.
// [UNITY] Would map to `UnityMainThreadDispatcher` or similar pattern for marshaling UI updates from background threads.
void NetworkTestDialog::update_status(int job_id, wxString info)
{
    auto evt = new wxCommandEvent(EVT_UPDATE_RESULT, this->GetId());
    evt->SetString(info);
    evt->SetInt(job_id);
    wxQueueEvent(this, evt); // [THREAD] Queues event for UI thread processing.
}

}} // namespace Slic3r::GUI
