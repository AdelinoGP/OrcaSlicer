#ifndef slic3r_TaskManager_hpp_
#define slic3r_TaskManager_hpp_

#include "DeviceManager.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/log/trivial.hpp>

// [INTENT] Throttled print/send scheduler for queued device jobs.
// [STATE] The header carries per-task status, per-batch policy, and the manager's worker/cache ownership.
// [THREAD] Worker-thread scheduling and callback fan-out cross back into UI-facing observers.
// [UNITY] Model this as an async job-queue service plus a main-thread event channel, not a widget-owned controller.
// [PORTING_HAZARD:P2] Boost thread ownership, mutable shared task objects, and callback reentry need a deliberate C# service split.

namespace Slic3r {

enum TaskState {
    TS_PENDING = 0,
    TS_SENDING,
    TS_SEND_COMPLETED,
    TS_SEND_CANCELED,
    TS_SEND_FAILED,
    TS_PRINTING,
    /* queray in Machine Object: IDLE, PREPARE, RUNNING, PAUSE, FINISH, FAILED, SLICING */
    TS_PRINT_SUCCESS,
    TS_PRINT_FAILED,
    TS_REMOVED,
    TS_IDLE,
};

std::string get_task_state_enum_str(TaskState ts);

class TaskStateInfo
{
public:
    static int                                                g_task_info_id;
    typedef std::function<void(TaskState state, int percent)> StateChangedFn;

    // [INTENT] Per-job state capsule with progress, cancel, and metadata fields.
    // [STATE] This object is mutated as a job moves through send/print lifecycle states.
    // [EVENT] State changes and cancellation callbacks are the main notification surfaces.
    // [UNITY] Mirror this as a job view-model DTO with explicit progress/cancel events.
    TaskStateInfo(const PrintParams param);

    TaskStateInfo() { task_info_id = ++TaskStateInfo::g_task_info_id; }

    TaskState state() { return m_state; }
    void      set_state(TaskState ts)
    {
        BOOST_LOG_TRIVIAL(trace) << "TaskStateInfo set state = " << get_task_state_enum_str(ts);
        m_state = ts;
        if (m_state_changed_fn) {
            m_state_changed_fn(m_state, m_sending_percent);
        }
    }
    PrintParams get_params() { return m_params; }

    PrintParams& params() { return m_params; }

    std::string get_job_id() { return profile_id; }

    void update_sending_percent(int percent)
    {
        m_sending_percent = percent;
        update();
    }
    void set_sent_time(std::chrono::system_clock::time_point time)
    {
        sent_time = time;
        update();
    }
    void set_state_changed_fn(StateChangedFn fn)
    {
        m_state_changed_fn = fn;
        update();
    }
    void set_cancel_fn(WasCancelledFn fn) { cancel_fn = fn; }

    void set_task_name(std::string name) { m_task_name = name; }
    void set_device_name(std::string name) { m_device_name = name; }
    void set_job_id(std::string job_id) { m_job_id = job_id; }

    void update()
    {
        if (m_state_changed_fn) {
            m_state_changed_fn(m_state, m_sending_percent);
        }
    }

    void cancel();
    bool is_canceled() { return m_cancel; }

    std::string get_device_name() { return m_device_name; };
    std::string get_task_name() { return m_task_name; };
    std::string get_sent_time()
    {
        std::time_t time     = std::chrono::system_clock::to_time_t(sent_time);
        std::tm*    timeInfo = std::localtime(&time);

        std::stringstream ss;
        ss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
        std::string str = ss.str();
        return str;
    };

    /* sending timelapse */
    // [STATE] Public payload copied from the originating print job plus upload/session metadata.
    // [UNCLEAR] `sent_time` and `profile_id` are shared across task types; hypothesis: they are reused for both upload and timelapse history rows.
    std::chrono::system_clock::time_point sent_time;
    WasCancelledFn                        cancel_fn;
    OnUpdateStatusFn                      update_status_fn;
    OnWaitFn                              wait_fn;
    std::string                           thumbnail_url;
    std::string                           start_time;
    std::string                           end_time;
    std::string                           profile_id;
    int                                   task_info_id;

private:
    bool           m_cancel;
    TaskState      m_state;
    std::string    m_task_name;
    std::string    m_device_name;
    PrintParams    m_params;
    int            m_sending_percent;
    std::string    m_job_id;
    StateChangedFn m_state_changed_fn;
};

class TaskSettings
{
public:
    // [STATE] Scheduler policy: interval and concurrency limits are shared by every batch.
    // [UNCLEAR] The stale inline comment says "60 seconds" while the value is 180; hypothesis: seconds are intended and the comment is outdated.
    int sending_interval{180}; /* sending a job every 60 seconds */
    int max_sending_at_same_time{1};
};

class TaskGroup
{
public:
    std::vector<TaskStateInfo*> tasks;
    TaskSettings                settings;

    // [INTENT] Group jobs under one scheduling policy so the manager can gate dispatch by time and concurrency.
    // [UNITY] This becomes queue policy on the service layer, not part of the view tree.
    TaskGroup(TaskSettings s) : settings(s) {}

    void append(TaskStateInfo* task) { this->tasks.push_back(task); }

    // [INTENT] Decide whether a task can start under the batch's time/concurrency policy.
    // [UNITY] Keep this as service-side scheduling logic; the UI should only observe the result.
    bool need_schedule(std::chrono::system_clock::time_point last, TaskStateInfo* task);
};

class TaskManager
{
public:
    static int MaxSendingAtSameTime;
    static int SendingInterval;
    // [INTENT] Own the queue, worker loop, and network-agent bridge for device sends.
    // [THREAD] The background scheduler and per-send threads coordinate through mutex-protected caches.
    // [EVENT] UI code consumes task snapshots and the multi-send-limit event instead of polling raw internals.
    TaskManager(NetworkAgent* agent);

    // [INTENT] Snapshot job requests into scheduler state and kick off send processing.
    int start_print(const std::vector<PrintParams>& params, TaskSettings* settings = nullptr);

    // [THREAD] Lifecycle control for the background scheduler thread.
    static void set_max_send_at_same_time(int count);

    // [THREAD] Start/stop the worker loop that arbitrates dispatch across the cached queues.
    void start();
    void stop();

    // [STATE] Return a local view of the current queue for UI inspection.
    std::map<int, TaskStateInfo*> get_local_task_list();

    /* curr_page is start with 0 */
    // [STATE] Paginated snapshot for remote/task-list views; callers should treat the copy as stale immediately.
    std::map<std::string, TaskStateInfo> get_task_list(int curr_page, int page_count, int& total);

    // [STATE] Query a single device/task state from the manager's cache.
    TaskState query_task_state(std::string dev_id);

private:
    // [THREAD] Internal scheduler tick that enforces the send interval and concurrency limits.
    int schedule(TaskStateInfo* task);

    boost::thread m_scedule_thread;

    // [STATE] Cached batches and active task lists are shared mutable scheduling state.
    std::vector<TaskGroup> m_cache_map;
    std::mutex             m_map_mutex;
    /* sending task list */
    std::vector<TaskStateInfo*> m_scedule_list;
    std::vector<boost::thread*> m_sending_thread_list;
    std::mutex                  m_scedule_mutex;
    bool                        m_started{false};
    NetworkAgent*               m_agent{nullptr};

    std::chrono::system_clock::time_point last_sent_timestamp;
};

// [EVENT] Emitted when the scheduler hits the maximum concurrent-send limit.
// [UNITY] Surface this as a UI-thread notification/toast from the service layer.
wxDECLARE_EVENT(EVT_MULTI_SEND_LIMIT, wxCommandEvent);
} // namespace Slic3r

#endif
