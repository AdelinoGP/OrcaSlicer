#ifndef BOOSTTHREADWORKER_HPP
#define BOOSTTHREADWORKER_HPP

#include <boost/variant.hpp>

#include "Worker.hpp"

#include <libslic3r/Thread.hpp>
#include <boost/log/trivial.hpp>

#include "ThreadSafeQueue.hpp"
#include "slic3r/GUI/GUI.hpp"

namespace Slic3r { namespace GUI {

// [INTENT][THREAD] Starts a boost::thread worker that receives Job entries
// from the UI/main thread and pushes status/finalization messages back so the
// wxWidgets ProgressIndicator and dialogs only touch the UI thread.
// [UNITY] Replace with a background Task/JobSystem worker combined with a
//         UnityMainThreadDispatcher MonoBehaviour (or SynchronizationContext) to
//         marshal status updates, `ProgressIndicator` mimic, and `std::promise`
//         callbacks onto the main loop.
// [PORTING_HAZARD:P2] The worker directly updates wxProgressIndicator and
//                      job state; Unity ports must avoid touching non-Unity
//                      objects from background threads and ensure callbacks
//                      run the frame loop.
class BoostThreadWorker : public Worker, private Job::Ctl
{
    struct JobEntry // Goes into worker and also out of worker as a finalize msg
    {
        std::shared_ptr<Job> job;
        bool                 canceled = false;
        std::exception_ptr   eptr     = nullptr;
        // [STATE] Tracks per-Job cancellation/errors so the main thread can decide
        //         whether to show failure dialogs or continue queuing.
    };

    // A message data for status updates. Only goes from worker to main thread.
    struct StatusInfo
    {
        int         status;
        std::string msg;
    };
    // [EVENT] Serialized status code + description used by the main thread to
    //          refresh the shared ProgressIndicator and keep the UI responsive.

    // An arbitrary callback to be called on the main thread. Only from worker
    // to main thread.
    struct MainThreadCallData
    {
        std::function<void()> fn;
        std::promise<void>    promise;
        // [THREAD] Promise-based bridge that lets the worker wait for the UI
        //          thread to finish a synchronous callback before continuing.
    };

    struct EmptyMessage
    {};

    class WorkerMessage
    {
    public:
        enum MsgType { Empty, Status, Finalize, MainThreadCall };

    private:
        boost::variant<EmptyMessage, StatusInfo, JobEntry, MainThreadCallData> m_data;

    public:
        WorkerMessage() = default;
        WorkerMessage(int s, std::string txt) : m_data{StatusInfo{s, std::move(txt)}} {}
        WorkerMessage(JobEntry&& entry) : m_data{std::move(entry)} {}
        WorkerMessage(MainThreadCallData fn) : m_data{std::move(fn)} {}

        int get_type() const { return m_data.which(); }

        void deliver(BoostThreadWorker& runner);
        // [EVENT] Polymorphic payload describing either status, finalization,
        //          or main-thread callbacks, dispatched after the worker loop.
    };

    using JobQueue     = ThreadSafeQueueSPSC<JobEntry>;
    using MessageQueue = ThreadSafeQueueSPSC<WorkerMessage>;
    // [THREAD] Single-producer single-consumer queues keep locking simple between
    //          the UI thread and worker thread, so Unity ports can emulate them
    //          with ConcurrentQueue + main-thread dispatchers.

    boost::thread                      m_thread;
    std::atomic<bool>                  m_running{false}, m_canceled{false};
    std::shared_ptr<ProgressIndicator> m_progress;
    JobQueue                           m_input_queue;  // from main thread to worker
    MessageQueue                       m_output_queue; // form worker to main thread
    std::string                        m_name;
    // [STATE] Thread flags, queue pointers, and ProgressIndicator reference are
    //         the critical state determining whether the worker is active.

    void run();
    // [THREAD] Worker entrypoint that spins on `m_input_queue`, executes jobs,
    //          and enqueues messages for the UI.

    bool join(int timeout_ms = 0);
    // [THREAD] Joins the boost::thread safely and avoids UI stalls when waiting.

protected:
    // Implement Job::Ctl interface:

    void update_status(int st, const std::string& msg = "") override;
    // [EVENT][THREAD] Updates queue-databound status values; main thread pulls
    //                 these via `process_events()` so wxWidgets stays on task.

    bool was_canceled() const override { return m_canceled.load(); }

    std::future<void> call_on_main_thread(std::function<void()> fn) override;
    // [THREAD][UNITY] Enqueues `fn` for the UI thread and returns a future; Unity
    //                  equivalents would queue a delegate into the
    //                  `UnityMainThreadDispatcher`/`SynchronizationContext` and
    //                  `await` it via `TaskCompletionSource`.

public:
    explicit BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri, boost::thread::attributes& attr, const char* name = "");
    // [INTENT] Construct the worker with explicit boost attributes so each
    //           job can expose thread affinity information to logging/debug UI.

    explicit BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri, boost::thread::attributes&& attr, const char* name = "")
        : BoostThreadWorker{std::move(pri), attr, name}
    {}

    explicit BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri, const char* name = "") : BoostThreadWorker{std::move(pri), {}, name}
    {}

    ~BoostThreadWorker();

    BoostThreadWorker(const BoostThreadWorker&)            = delete;
    BoostThreadWorker(BoostThreadWorker&&)                 = delete;
    BoostThreadWorker& operator=(const BoostThreadWorker&) = delete;
    BoostThreadWorker& operator=(BoostThreadWorker&&)      = delete;

    // [STATE][THREAD] Non-copyable worker ensures only one boost::thread owns the
    //                 queues and progress indicator.
    bool push(std::shared_ptr<Job> job) override;
    // [EVENT] Queues a job for background execution and primes the worker loop.

    bool is_idle() const override
    {
        // The assumption is that jobs can only be queued from a single main
        // thread from which this method is also called. And the output
        // messages are also processed only in this calling thread. In that
        // case, if the input queue is empty, it will remain so during this
        // function call. If the worker thread is also not running and the
        // output queue is already processed, we can safely say that the
        // worker is dormant.
        return m_input_queue.empty() && !m_running.load() && m_output_queue.empty();
    }
    // [STATE] Relies on queue emptiness and `m_running` to determine safe idle
    //         windows so the main thread can schedule other actors.

    void cancel() override { m_canceled.store(true); }
    // [THREAD] Cancellation flag toggled from UI thread but read by worker loop.
    void cancel_all() override
    {
        m_input_queue.clear();
        cancel();
    }
    // [EVENT] Clearing the queue prevents new work from launching while still
    //         allowing the currently running job to finish gracefully.

    ProgressIndicator*       get_pri() { return m_progress.get(); }
    const ProgressIndicator* get_pri() const { return m_progress.get(); }

    void clear_percent() override
    {
        if (m_progress) {
            m_progress->clear_percent();
        }
    }

    void show_error_info(const std::string& msg, int code, const std::string& description, const std::string& extra) override
    {
        if (m_progress) {
            m_progress->show_error_info(from_u8(msg), code, from_u8(description), from_u8(extra));
        }
    }

    void process_events() override;
    // [EVENT][THREAD] Pumps pending `WorkerMessage`s on the UI thread so status
    //                 updates and finalizers can run without race conditions.
    bool wait_for_current_job(unsigned timeout_ms = 0) override;
    // [THREAD] Blocks the main thread until the current job finishes or the
    //         timeout expires to keep UI feedback in sync.
    bool wait_for_idle(unsigned timeout_ms = 0) override;
    // [THREAD] Waits for both queues to drain so callers can safely tear down.
};

}} // namespace Slic3r::GUI

#endif // BOOSTTHREADWORKER_HPP
