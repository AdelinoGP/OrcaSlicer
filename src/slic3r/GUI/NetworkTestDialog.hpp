// [INTENT] This header declares the NetworkTestDialog class, which provides an interface for network connectivity testing within the GUI.
// It defines the UI elements, test job types, and methods for managing network tests and displaying results.
#ifndef slic3r_GUI_NetworkTestDialog_hpp_
#define slic3r_GUI_NetworkTestDialog_hpp_

#include <wx/wx.h>          // [PORTING_HAZARD:P3] Direct inclusion of wx/wx.h may indicate a broad dependency.
#include <boost/thread.hpp> // [THREAD] Required for boost::thread member variables.

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include <slic3r/GUI/Widgets/Button.hpp>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/button.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/grid.h> // [UNCLEAR] Unused header? Only wx/dialog needed.
#include <wx/dialog.h>
#include <wx/srchctrl.h> // [UNCLEAR] Unused header?
#include <wx/stattext.h>
#include <iomanip>
#include <sys/timeb.h> // [PORTING_HAZARD:P3] System-specific time functions, might need cross-platform alternatives.
#include <time.h>
#include <vector>
#include <algorithm>

namespace Slic3r { namespace GUI {

// [INTENT] Enumeration defining different types of network test jobs.
enum TestJob { TEST_BING_JOB = 0, TEST_ORCA_JOB = 1, TEST_PING_JOB, TEST_JOB_MAX };

// [INTENT] NetworkTestDialog class: a dialog window for performing and displaying network connectivity tests.
// It manages the UI for starting tests, showing system information, and logging results.
// [UNITY] This class would map to a C# class inheriting from MonoBehaviour, controlling a UI Toolkit Document (UXML)
// that defines the dialog's visual structure. UI elements like buttons, static text, and text controls would be
// replaced by their UI Toolkit counterparts (Button, Label, TextField).
class NetworkTestDialog : public DPIDialog
{
protected:
    // [STATE] Button for starting multi-threaded tests.
    // [UNITY] UI Toolkit Button.
    Button* btn_start;
    // [STATE] Button for starting single-threaded tests.
    // [UNITY] UI Toolkit Button.
    Button* btn_start_sequence;
    // [STATE] Button for exporting the log.
    // [UNITY] UI Toolkit Button.
    Button* btn_download_log;
    // [STATE] Static text for basic info.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_basic_info;
    // [STATE] Static text for version title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_version_title;
    // [STATE] Static text for version value.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_version_val;
    // [STATE] Static text for system info title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* txt_sys_info_title;
    // [STATE] Static text for system info value.
    // [UNITY] UI Toolkit Label.
    wxStaticText* txt_sys_info_value;
    // [STATE] Static text for DNS info title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* txt_dns_info_title;
    // [STATE] Static text for DNS info value.
    // [UNITY] UI Toolkit Label.
    wxStaticText* txt_dns_info_value;
    // [STATE] Button for testing GitHub link.
    // [UNITY] UI Toolkit Button.
    Button* btn_link;
    // [STATE] Static text for GitHub link title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_link_title;
    // [STATE] Static text for GitHub link value.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_link_val;
    // [STATE] Button for testing Bing link.
    // [UNITY] UI Toolkit Button.
    Button* btn_bing;
    // [STATE] Static text for Bing link title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_bing_title;
    // [STATE] Static text for Bing link value.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_bing_val;
    // [STATE] Static text for ping title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_ping_title;
    // [STATE] Static text for ping value.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_ping_value;
    // [STATE] Static text for result log title.
    // [UNITY] UI Toolkit Label.
    wxStaticText* text_result;
    // [STATE] Text control for displaying logs.
    // [UNITY] UI Toolkit TextField (multi-line).
    wxTextCtrl* txt_log;

    // [INTENT] Methods for creating different sections (sizers) of the dialog UI.
    // [UNITY] These would translate to methods that instantiate and arrange UI Toolkit VisualElements.
    wxBoxSizer* create_top_sizer(wxWindow* parent);
    wxBoxSizer* create_info_sizer(wxWindow* parent);
    wxBoxSizer* create_content_sizer(wxWindow* parent);
    wxBoxSizer* create_result_sizer(wxWindow* parent);

    // [STATE] Array of pointers to boost::thread objects, one for each test job.
    // [THREAD] Manages background threads for network tests.
    // [PORTING_HAZARD:P1] Direct usage of `boost::thread` for UI-related background tasks.
    // This will require significant redesign using C# `Task` Parallel Library or `async/await` patterns.
    boost::thread* test_job[TEST_JOB_MAX];
    // [STATE] Pointer to a boost::thread for sequential job execution.
    // [THREAD] Background thread for sequential tests.
    // [PORTING_HAZARD:P1] Same as `test_job`.
    boost::thread* m_sequence_job{nullptr};
    // [STATE] Boolean array to track if a specific test job is currently running.
    // [THREAD] Used for thread synchronization and preventing concurrent test starts.
    bool m_in_testing[TEST_JOB_MAX];
    // [STATE] Flag to indicate if a download operation should be cancelled.
    bool m_download_cancel = false;
    // [STATE] Flag to indicate if the dialog is closing.
    // [THREAD] Used to signal background threads to terminate.
    bool m_closing = false;

    // [INTENT] Binds all UI events to their respective handlers.
    // [UNITY] Event subscriptions would be set up using UI Toolkit's event system or C# delegates.
    void init_bind();

public:
    // [INTENT] Constructor for NetworkTestDialog.
    // [UNITY] Corresponds to the initialization logic within a C# MonoBehaviour's `Awake()` or `Start()` methods
    // after the UI Document is loaded. Parameters like `wxWindowID`, `wxString`, `wxPoint`, `wxSize`, `long style`
    // are wxWidgets specific and will be handled by UI Toolkit's layout and style system.
    NetworkTestDialog(wxWindow*       parent,
                      wxWindowID      id    = wxID_ANY,
                      const wxString& title = wxEmptyString,
                      const wxPoint&  pos   = wxDefaultPosition,
                      const wxSize&   size  = wxSize(605, 375),
                      long            style = wxDEFAULT_DIALOG_STYLE);

    // [INTENT] Destructor. Cleans up resources, particularly joining running threads.
    // [THREAD] Ensures graceful termination of background threads.
    ~NetworkTestDialog();

    // [INTENT] Handles DPI change events to adjust dialog scaling.
    // [UNITY] Unity UI Toolkit handles DPI scaling automatically, so this method's content would likely be removed
    // or integrated into custom responsive layout logic if specific adaptations are required.
    void on_dpi_changed(const wxRect& suggested_rect);

    // [INTENT] Sets default states and values for UI elements.
    void set_default();
    // [INTENT] Retrieves the application version string.
    wxString get_studio_version();
    // [INTENT] Retrieves operating system information.
    wxString get_os_info();
    // [INTENT] Retrieves DNS server information.
    wxString get_dns_info();

    // [INTENT] Starts all network tests concurrently.
    void start_all_job();
    // [INTENT] Starts all network tests sequentially.
    // [THREAD] Initiates a new thread for sequential test execution.
    void start_all_job_sequence();
    // [INTENT] Starts the Bing network test.
    // [THREAD] Initiates a new thread for the Bing test.
    void start_test_bing_thread();
    // [INTENT] Starts the GitHub network test.
    // [THREAD] Initiates a new thread for the GitHub test.
    void start_test_github_thread();
    // [INTENT] Placeholder for a ping test.
    // [THREAD] Initiates a new thread for the ping test.
    void start_test_ping_thread();

    // [INTENT] Generic method to start a network test for a given URL.
    // [THREAD] This method performs HTTP requests which can block. It's intended to be called from a background thread.
    void start_test_url(TestJob job, wxString name, wxString url);

    // [INTENT] Event handler for window close event.
    // [EVENT] Cleans up resources and joins threads.
    void on_close(wxCloseEvent& event);

    // [INTENT] Updates the status of network tests and logs information to the UI.
    // [THREAD] This method is thread-safe as it queues a `wxCommandEvent` to be processed on the UI thread.
    // [UNITY] Would require a mechanism for safe cross-thread UI updates, e.g., using `UnityMainThreadDispatcher`.
    void update_status(int job_id, wxString info);
};

}} // namespace Slic3r::GUI

#endif
