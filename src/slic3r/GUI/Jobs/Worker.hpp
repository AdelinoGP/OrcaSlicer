#ifndef PRUSALSICER_WORKER_HPP
#define PRUSALSICER_WORKER_HPP

#include <memory>

#include "Job.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Interface for a background job processor.
// Jobs are run sequentially on a dedicated thread, managed from the main UI thread.
// [UNITY] Use a C# queue-based task processor, e.g., a custom `MonoBehaviour` using `Task` or `Coroutine`.
// [PORTING_HAZARD:P2] Managing thread-safety between C++ worker thread logic and Unity's main thread API access requires a robust marshaling layer.
class Worker
{
public:
    // [INTENT] Enqueue a job for execution.
    // [STATE] The worker maintains a job queue internally.
    virtual bool push(std::shared_ptr<Job> job) = 0;

    // [INTENT] Check worker status.
    virtual bool is_idle() const = 0;

    // [INTENT] Trigger graceful cancellation of current job.
    // [THREAD] This is called from the UI thread, but interacts with background thread state.
    virtual void cancel() = 0;

    // [INTENT] Clear queue and cancel current job.
    virtual void cancel_all() = 0;

    // [INTENT] Update status or finalize jobs.
    // [THREAD] This must be called from the main thread (e.g., UI update hook).
    // [UNITY] This might be mapped to `Update()` in a `MonoBehaviour` or a periodic timer.
    virtual void process_events() = 0;

    // [INTENT] Synchronous waiting for job completion.
    // [PORTING_HAZARD:P1] Synchronous wait on UI thread blocks the UI in both C++ and Unity.
    // Must reimplement as `async/await` in C#.
    virtual bool wait_for_current_job(unsigned timeout_ms = 0) = 0;

    // [INTENT] Synchronous waiting for queue depletion.
    virtual bool wait_for_idle(unsigned timeout_ms = 0) = 0;

    // [INTENT] Shutdown worker thread.
    virtual ~Worker() = default;
};

template<class Fn> constexpr bool IsProcessFn = std::is_invocable_v<Fn, Job::Ctl&>;
template<class Fn> constexpr bool IsFinishFn  = std::is_invocable_v<Fn, bool, std::exception_ptr&>;

// Helper function to use the worker with arbitrary functors.
template<class ProcessFn, class FinishFn, class = std::enable_if_t<IsProcessFn<ProcessFn>>, class = std::enable_if_t<IsFinishFn<FinishFn>>>
bool queue_job(Worker& w, ProcessFn fn, FinishFn finishfn)
{
    struct LambdaJob : Job
    {
        ProcessFn fn;
        FinishFn  finishfn;

        LambdaJob(ProcessFn pfn, FinishFn ffn) : fn{std::move(pfn)}, finishfn{std::move(ffn)} {}

        void process(Ctl& ctl) override { fn(ctl); }
        void finalize(bool canceled, std::exception_ptr& eptr) override { finishfn(canceled, eptr); }
    };

    auto j = std::make_shared<LambdaJob>(std::move(fn), std::move(finishfn));
    return w.push(std::move(j));
}

template<class ProcessFn, class = std::enable_if_t<IsProcessFn<ProcessFn>>> bool queue_job(Worker& w, ProcessFn fn)
{
    return queue_job(w, std::move(fn), [](bool, std::exception_ptr&) {});
}

inline bool queue_job(Worker& w, std::shared_ptr<Job> j) { return w.push(std::move(j)); }

// Replace the current job queue with a new job. The signature is the same
// as for queue_job(). This cancels all jobs and
// will not wait. The new job will begin after the queue cancels properly.
// Note that this can be called from the UI thread and will not block it if
// the jobs take longer to cancel.
template<class... Args> bool replace_job(Worker& w, Args&&... args)
{
    w.cancel_all();
    return queue_job(w, std::forward<Args>(args)...);
}

// Cancel the current job and wait for it to actually be stopped.
inline bool stop_current_job(Worker& w, unsigned timeout_ms = 0)
{
    w.cancel();
    return w.wait_for_current_job(timeout_ms);
}

// Cancel all pending jobs including current one and wait until the worker
// becomes idle.
inline bool stop_queue(Worker& w, unsigned timeout_ms = 0)
{
    w.cancel_all();
    return w.wait_for_idle(timeout_ms);
}

}} // namespace Slic3r::GUI

#endif // WORKER_HPP
