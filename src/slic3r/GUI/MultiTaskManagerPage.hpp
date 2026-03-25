#ifndef slic3r_MultiTaskManagerPage_hpp_
#define slic3r_MultiTaskManagerPage_hpp_

#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "MultiMachine.hpp"
#include "DeviceManager.hpp"
#include "TaskManager.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "Widgets/PopupWindow.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] These constants define fixed dimensions and paddings used for layout and sizing of UI elements
//          within the task manager pages, specifically for task list items and their columns.
// [UNITY] These values would typically be represented as C# constants or assigned directly in UI Toolkit USS files,
//          potentially as custom properties to allow for theme-based adjustments.
#define CLOUD_TASK_ITEM_MAX_WIDTH 1100
#define TASK_ITEM_MAX_WIDTH 900
#define TASK_LEFT_PADDING_LEFT 15
#define TASK_LEFT_PRINTABLE 40
#define TASK_LEFT_PRO_NAME 180
#define TASK_LEFT_DEV_NAME 150
#define TASK_LEFT_PRO_STATE 170
#define TASK_LEFT_PRO_INFO 230
#define TASK_LEFT_SEND_TIME 180

// [INTENT] MultiTaskItem is a custom wxWindow control that represents a single task item within a list
//          of print jobs (both local and cloud-based). It displays task-specific information, status,
//          and interactive buttons, handling its own rendering and mouse events.
// [UNITY] This class would be reimplemented as a custom UI Toolkit VisualElement or a MonoBehaviour-driven UI component.
//          It would encapsulate the presentation of a single task entry, using data binding to reflect
//          the task's state from a C# data model. Its custom drawing would be replaced by UI Toolkit's
//          styling and custom drawing APIs, and event handling by Unity's EventSystem.
// [PORTING_HAZARD:P2] Extensive custom rendering (`paintEvent`, `doRender`) and direct wxWidgets event binding
//                     require a complete redesign for Unity's UI Toolkit.
class MultiTaskItem : public DeviceItem
{
public:
    // [INTENT] Constructor for initializing a MultiTaskItem with parent, machine object, and task type.
    MultiTaskItem(wxWindow* parent, MachineObject* obj, int type);
    // [INTENT] Destructor for MultiTaskItem.
    ~MultiTaskItem() {};

    // [EVENT] Event handler for mouse entering the window area.
    void OnEnterWindow(wxMouseEvent& evt);
    // [EVENT] Event handler for mouse leaving the window area.
    void OnLeaveWindow(wxMouseEvent& evt);
    // [EVENT] Event handler for device selection changes.
    void OnSelectedDevice(wxCommandEvent& evt);
    // [EVENT] Event handler for left mouse button down.
    void OnLeftDown(wxMouseEvent& evt);
    // [EVENT] Event handler for mouse motion.
    void OnMove(wxMouseEvent& evt);

    // [RENDERING] Handles the wxPaintEvent for the custom control.
    void paintEvent(wxPaintEvent& evt);
    // [RENDERING] Provides a platform-specific rendering pipeline (double-buffered on MSW).
    void render(wxDC& dc);
    // [RENDERING] Performs the actual custom drawing of the task item content.
    void doRender(wxDC& dc);
    // [RENDERING] Draws text on a wxDC, truncating with ellipses if it exceeds maxWidth.
    void DrawTextWithEllipsis(wxDC& dc, const wxString& text, int maxWidth, int left, int top = 0);
    // [EVENT] Posts a custom wxCommandEvent.
    void post_event(wxCommandEvent&& event);
    // [INTENT] Overrides base class method to handle size changes.
    virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO);

    // [STATE] Flag indicating if the mouse cursor is currently hovering over the item.
    // [UNITY] A boolean field in a C# controller, potentially mapped to UI Toolkit's `:hover` pseudo-class for styling.
    bool m_hover{false};
    // [INTENT] Utility function to format remaining time into a human-readable string.
    // [UNITY] Could be a static helper method in C#.
    wxString get_left_time(int mc_left_time);

    // [STATE] Bitmap for disabled checkbox state.
    // [UNITY] `Texture2D` or `Sprite` asset referenced by a UI Toolkit `Image` VisualElement.
    ScalableBitmap m_bitmap_check_disable;
    // [STATE] Bitmap for unfocused checkbox state.
    // [UNITY] `Texture2D` or `Sprite` asset referenced by a UI Toolkit `Image` VisualElement.
    ScalableBitmap m_bitmap_check_off;
    // [STATE] Bitmap for checked checkbox state.
    // [UNITY] `Texture2D` or `Sprite` asset referenced by a UI Toolkit `Image` VisualElement.
    ScalableBitmap m_bitmap_check_on;

    // [STATE] Current percentage of sending progress (for local tasks).
    // [UNITY] An integer field in a C# data model, bound to a UI Toolkit ProgressBar or custom progress indicator.
    int m_sending_percent{0};
    // [STATE] Type of the task: 0 for local, 1 for cloud.
    // [UNITY] An enum or integer field in a C# data model, used for conditional UI logic.
    int m_task_type{0}; // 0-local 1-cloud
    // [STATE] Project name associated with the task.
    // [UNITY] A string field in a C# data model, bound to a UI Toolkit `Label` VisualElement.
    wxString m_project_name;
    // [STATE] Device name associated with the task.
    // [UNITY] A string field in a C# data model, bound to a UI Toolkit `Label` VisualElement.
    wxString m_dev_name;
    // [STATE] Device ID associated with the task.
    // [UNITY] A string field in a C# data model.
    std::string m_dev_id;
    // [STATE] Pointer to TaskStateInfo object, providing detailed task state.
    // [UNITY] A reference to a C# task data model object.
    TaskStateInfo* task_obj{nullptr};
    // [STATE] Job ID for cloud tasks.
    // [UNITY] A string field in a C# data model.
    std::string m_job_id;
    // std::string  m_sent_time; // [UNCLEAR] Appears commented out or unused.

    // [STATE] Pointer to the "Resume" button control.
    // [UNITY] Reference to a UI Toolkit `Button` VisualElement.
    Button* m_button_resume{nullptr};
    // [STATE] Pointer to the "Cancel" button control.
    // [UNITY] Reference to a UI Toolkit `Button` VisualElement.
    Button* m_button_cancel{nullptr};
    // [STATE] Pointer to the "Pause" button control.
    // [UNITY] Reference to a UI Toolkit `Button` VisualElement.
    Button* m_button_pause{nullptr};
    // [STATE] Pointer to the "Stop" button control.
    // [UNITY] Reference to a UI Toolkit `Button` VisualElement.
    Button* m_button_stop{nullptr};

    // [INTENT] Updates the visibility and state of action buttons based on task status.
    void update_info();
    // [INTENT] Initiates the pause action for the task.
    void onPause();
    // [INTENT] Initiates the resume action for the task.
    void onResume();
    // [INTENT] Initiates the stop action for the task.
    void onStop();
    // [INTENT] Initiates the cancel action for the task.
    void onCancel();
};

// [INTENT] LocalTaskManagerPage manages and displays a list of local print tasks.
//          It provides controls for selecting tasks, sorting, and bulk actions (e.g., cancel all).
// [UNITY] This would be a dedicated UI Toolkit Document (UIDocument) or a MonoBehaviour controller managing a panel
//          with a ListView of LocalTaskItem elements (derived from MultiTaskItem).
// [PORTING_HAZARD:P2] Extensive use of wxWidgets panels, sizers, and custom controls for layout and interaction.
class LocalTaskManagerPage : public wxPanel
{
public:
    // [INTENT] Constructor for LocalTaskManagerPage, initializing UI components and layout.
    LocalTaskManagerPage(wxWindow* parent);
    // [INTENT] Destructor for LocalTaskManagerPage.
    ~LocalTaskManagerPage() {};

    // [INTENT] Updates the displayed information for all task items on the page.
    void update_page();
    // [INTENT] Refreshes the list of local tasks, potentially clearing existing items and re-populating.
    void refresh_user_device(bool clear = false);
    // [INTENT] Controls the visibility of the page.
    bool Show(bool show);
    // [INTENT] Handles the 'cancel all' action for selected local tasks.
    void cancel_all(wxCommandEvent& evt);
    // [INTENT] Rescales UI elements on MSW platforms (DPI awareness).
    // [PORTING_HAZARD:P1] Platform-specific DPI scaling.
    void msw_rescale();

private:
    // [STATE] Sort criteria for task items.
    // [UNITY] A C# class managing sorting logic for the ListView's data source.
    SortItem m_sort;
    // [STATE] Map of task IDs to MultiTaskItem pointers for currently displayed tasks.
    // [UNITY] A `List<LocalTaskData>` or `Dictionary<int, LocalTaskData>` in a C# controller, bound to a ListView.
    std::map<int, MultiTaskItem*> m_task_items;
    // [STATE] Flag for sorting device names (ascending/descending).
    // [UNITY] Boolean in a C# controller.
    bool device_name_big{true};
    // [STATE] Flag for sorting device states (ascending/descending).
    // [UNITY] Boolean in a C# controller.
    bool device_state_big{true};
    // [STATE] Flag for sorting send time (ascending/descending).
    // [UNITY] Boolean in a C# controller.
    bool device_send_time{true};

    // [STATE] Main panel for the page.
    // [UNITY] Reference to a root `VisualElement` or a `Panel` in UI Toolkit.
    wxPanel* m_main_panel{nullptr};
    // [STATE] Main vertical sizer for layout.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_main_sizer{nullptr};
    // [STATE] Vertical sizer for the entire page.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* page_sizer{nullptr};
    // [STATE] Vertical sizer for the task list.
    // [UNITY] Corresponding layout properties in USS for the ListView container.
    wxBoxSizer* m_sizer_task_list{nullptr};
    // [STATE] Scrolled window containing the task items.
    // [UNITY] A UI Toolkit `ListView` or `ScrollView`.
    wxScrolledWindow* m_task_list{nullptr};
    // [STATE] Static text for displaying selected item count.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_selected_num{nullptr};

    // table head
    // [STATE] Panel for the table header row.
    // [UNITY] A `VisualElement` with horizontal layout and children for each header button.
    wxPanel* m_table_head_panel{nullptr};
    // [STATE] Horizontal sizer for the table header.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_table_head_sizer{nullptr};
    // [STATE] Checkbox for selecting/deselecting all tasks.
    // [UNITY] A UI Toolkit `Toggle` VisualElement.
    CheckBox* m_select_checkbox{nullptr};
    // [STATE] Button for "Task Name" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_task_name{nullptr};
    // [STATE] Button for "Device Name" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_printer_name{nullptr};
    // [STATE] Button for "Task Status" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_status{nullptr};
    // [STATE] Button for "Info" column header.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_info{nullptr};
    // [STATE] Button for "Sent Time" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_send_time{nullptr};
    // [STATE] Button for "Actions" column header.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_action{nullptr};

    // ctrl button for all
    // [STATE] Number of selected items.
    // [UNITY] An integer field, displayed in a `Label` VisualElement.
    int m_sel_number{0};
    // [STATE] Panel for control buttons (e.g., stop all).
    // [UNITY] A `VisualElement` acting as a container for control buttons.
    wxPanel* m_ctrl_btn_panel{nullptr};
    // [STATE] Horizontal sizer for control buttons.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_btn_sizer{nullptr};
    // [STATE] Button to stop all tasks.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* btn_stop_all{nullptr};
    // [STATE] Static text for selected count.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_sel_text{nullptr};

    // tip when no device
    // [STATE] Static text to display when no tasks are present.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_tip_text{nullptr};
};

// [INTENT] CloudTaskManagerPage manages and displays a list of cloud print tasks,
//          including historical tasks. It provides pagination, sorting, and bulk actions.
// [UNITY] This would be a dedicated UI Toolkit Document (UIDocument) or a MonoBehaviour controller managing a panel
//          with a ListView of CloudTaskItem elements (derived from MultiTaskItem),
//          along with pagination controls and a dedicated data manager for cloud tasks.
// [PORTING_HAZARD:P2] Extensive use of wxWidgets panels, sizers, timers, and custom controls for layout and interaction.
class CloudTaskManagerPage : public wxPanel
{
public:
    // [INTENT] Constructor for CloudTaskManagerPage, initializing UI components, layout, and pagination.
    CloudTaskManagerPage(wxWindow* parent);
    // [INTENT] Destructor for CloudTaskManagerPage, specifically stopping and deleting the flipping timer.
    ~CloudTaskManagerPage();

    // [INTENT] Updates the displayed information for all task items on the page.
    void update_page();
    // [INTENT] Refreshes the list of cloud tasks, potentially clearing existing items and re-populating,
    //          considering current pagination settings.
    void refresh_user_device(bool clear = false);
    // [INTENT] Converts a UTC time string to a formatted date string.
    // [UNITY] A static C# helper method or extension method for `System.DateTime`.
    std::string utc_time_to_date(std::string utc_time);
    // [INTENT] Controls the visibility of the page.
    bool Show(bool show);
    // [INTENT] Updates the displayed page number in the UI.
    void update_page_number();
    // [INTENT] Starts a timer, likely for refreshing content or enabling buttons after a delay.
    void start_timer();
    // [EVENT] Timer event handler.
    void on_timer(wxTimerEvent& event);

    // [INTENT] Handles the 'pause all' action for selected cloud tasks.
    void pause_all(wxCommandEvent& evt);
    // [INTENT] Handles the 'resume all' action for selected cloud tasks.
    void resume_all(wxCommandEvent& evt);
    // [INTENT] Handles the 'stop all' action for selected cloud tasks.
    void stop_all(wxCommandEvent& evt);

    // [INTENT] Enables or disables all control buttons.
    void enable_buttons(bool enable);
    // [INTENT] Handles the event when a page number is entered by the user.
    void page_num_enter_evt();

    // [INTENT] Rescales UI elements on MSW platforms (DPI awareness).
    // [PORTING_HAZARD:P1] Platform-specific DPI scaling.
    void msw_rescale();

private:
    // [STATE] Sort criteria for task items.
    // [UNITY] A C# class managing sorting logic for the ListView's data source.
    SortItem m_sort;
    // [STATE] Flag for sorting device names (ascending/descending).
    // [UNITY] Boolean in a C# controller.
    bool device_name_big{true};
    // [STATE] Flag for sorting device states (ascending/descending).
    // [UNITY] Boolean in a C# controller.
    bool device_state_big{true};
    // [STATE] Flag for sorting send time (ascending/descending).
    // [UNITY] Boolean in a C# controller.
    bool device_send_time{true};

    // [STATE] Map of job IDs to MultiTaskItem pointers for currently displayed tasks.
    // [UNITY] A `List<CloudTaskData>` or `Dictionary<string, CloudTaskData>` in a C# controller, bound to a ListView.
    std::map<std::string, MultiTaskItem*> m_task_items;

    // [STATE] Main panel for the page.
    // [UNITY] Reference to a root `VisualElement` or a `Panel` in UI Toolkit.
    wxPanel* m_main_panel{nullptr};
    // [STATE] Vertical sizer for the entire page.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* page_sizer{nullptr};
    // [STATE] Vertical sizer for the task list.
    // [UNITY] Corresponding layout properties in USS for the ListView container.
    wxBoxSizer* m_sizer_task_list{nullptr};
    // [STATE] Main vertical sizer for layout.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_main_sizer{nullptr};
    // [STATE] Scrolled window containing the task items.
    // [UNITY] A UI Toolkit `ListView` or `ScrollView`.
    wxScrolledWindow* m_task_list{nullptr};
    // [STATE] Static text for displaying selected item count.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_selected_num{nullptr};

    // Flipping pages
    // [STATE] Current page number for pagination.
    // [UNITY] An integer field in a C# controller, bound to a UI Toolkit `Label` and used for data fetching.
    int m_current_page{0};
    // [STATE] Total number of pages.
    // [UNITY] An integer field in a C# controller.
    int m_total_page{0};
    // [STATE] Total count of cloud tasks.
    // [UNITY] An integer field in a C# controller.
    int m_total_count{0};
    // [STATE] Number of items to display per page.
    // [UNITY] An integer field in a C# controller.
    int m_count_page_item{10};
    // [STATE] Flag indicating if there's a previous page.
    // [UNITY] Boolean in a C# controller, controlling Button enabled state.
    bool prev{false};
    // [STATE] Flag indicating if there's a next page.
    // [UNITY] Boolean in a C# controller, controlling Button enabled state.
    bool next{false};
    // [STATE] Button for navigating to the last page.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* btn_last_page{nullptr};
    // [STATE] Button for navigating to the next page.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* btn_next_page{nullptr};
    // [STATE] Static text displaying current page number and total pages.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* st_page_number{nullptr};
    // [STATE] Horizontal sizer for flipping page controls.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_flipping_page_sizer{nullptr};
    // [STATE] Vertical sizer for flipping page panel.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_page_sizer{nullptr};
    // [STATE] Panel containing pagination controls.
    // [UNITY] A `VisualElement` container for pagination UI.
    wxPanel* m_flipping_panel{nullptr};
    // [STATE] Timer used for pagination-related delays or refresh.
    // [UNITY] C# `Timer` object or Unity's Coroutine/async system.
    wxTimer* m_flipping_timer{nullptr};
    // [STATE] Text input for directly entering a page number.
    // [UNITY] A UI Toolkit `TextField` VisualElement.
    TextInput* m_page_num_input{nullptr};
    // [STATE] Button to confirm page number input.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_page_num_enter{nullptr};

    // table head
    // [STATE] Panel for the table header row.
    // [UNITY] A `VisualElement` with horizontal layout and children for each header button.
    wxPanel* m_table_head_panel{nullptr};
    // [STATE] Horizontal sizer for the table header.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_table_head_sizer{nullptr};
    // [STATE] Checkbox for selecting/deselecting all tasks.
    // [UNITY] A UI Toolkit `Toggle` VisualElement.
    CheckBox* m_select_checkbox{nullptr};
    // [STATE] Button for "Task Name" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_task_name{nullptr};
    // [STATE] Button for "Device Name" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_printer_name{nullptr};
    // [STATE] Button for "Task Status" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_status{nullptr};
    // [STATE] Button for "Info" column header.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_info{nullptr};
    // [STATE] Button for "Sent Time" column header (also for sorting).
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_send_time{nullptr};
    // [STATE] Button for "Actions" column header.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* m_action{nullptr};

    // ctrl button for all
    // [STATE] Number of selected items.
    // [UNITY] An integer field, displayed in a `Label` VisualElement.
    int m_sel_number;
    // [STATE] Panel for control buttons (e.g., pause all, resume all, stop all).
    // [UNITY] A `VisualElement` acting as a container for control buttons.
    wxPanel* m_ctrl_btn_panel{nullptr};
    // [STATE] Horizontal sizer for control buttons.
    // [UNITY] Corresponding layout properties in USS.
    wxBoxSizer* m_btn_sizer{nullptr};
    // [STATE] Button to pause all tasks.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* btn_pause_all{nullptr};
    // [STATE] Button to resume all tasks.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* btn_continue_all{nullptr};
    // [STATE] Button to stop all tasks.
    // [UNITY] A UI Toolkit `Button` VisualElement.
    Button* btn_stop_all{nullptr};
    // [STATE] Static text for selected count.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_sel_text{nullptr};

    // tip when no device
    // [STATE] Static text to display when no tasks are present.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_tip_text{nullptr};
    // [STATE] Static text to display while loading tasks.
    // [UNITY] A UI Toolkit `Label` VisualElement.
    wxStaticText* m_loading_text{nullptr};
};

}} // namespace Slic3r::GUI

#endif
