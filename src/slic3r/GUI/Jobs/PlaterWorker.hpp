// [INTENT]
// ... (omitted) ...
// [PORTING_HAZARD:P1] The direct reliance on wxWidgets event loop (wxEVT_IDLE, wxEVT_PAINT) for processing worker thread results is highly
// coupled to the UI framework and must be replaced by a C# event/async-await mechanism in Unity.
//
// The key components are:

// - `PlaterWorker`: A template class that wraps a `Worker` instance. It ensures
//   that the UI thread continuously processes messages from the worker thread
//   by hooking into the `wxEVT_IDLE` and `wxEVT_PAINT` events.
// - `PlaterJob`: A nested wrapper class that decorates any `Job` submitted to
//   the `PlaterWorker`. Its main purposes are:
//   - To ensure the UI thread is woken up to process status updates from the
//     job, providing a responsive UI.
//   - To add detailed logging for job execution times (process and finalize stages).
//   - To provide centralized exception handling and display error messages to
//     the user.
//   - To show a busy cursor while the job is running.
//
// [UNITY]
// The concept of a dedicated `PlaterWorker` would not be necessary in Unity.
// The core job management would be handled by the C# Job System or async/await.
// - The UI responsiveness (waking up the UI thread) is handled automatically by
//   Unity's main loop and `async/await`'s main thread synchronization context.
// - Job logging and exception handling would be implemented in a C# wrapper
//   class or using AOP (Aspect-Oriented Programming) techniques with attributes.
// - A busy cursor would be managed by a global UI state controller that listens
//   for "job started" and "job ended" events.

#ifndef PLATERWORKER_HPP
#define PLATERWORKER_HPP

#include <map>
#include <chrono>

#include "Worker.hpp"
#include "BusyCursorJob.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] A template for a worker class that is specialized for the Plater.
// It manages a worker thread for background processing and ensures that the
// Plater's UI remains responsive by processing events during idle time.
// [THREAD] This class owns a `Worker` instance and manages its lifecycle.
// [EVENT] It binds to `wxEVT_IDLE` and `wxEVT_PAINT` to continuously process
// events from the worker thread.
// [UNITY] This class would be replaced by a C# singleton or a script on a
// persistent GameObject that manages the lifecycle of background tasks and
// provides a central point for starting and monitoring jobs.
template<class WorkerSubclass> class PlaterWorker : public Worker
{
    // [STATE] The actual worker instance that manages the job queue and worker thread.
    WorkerSubclass m_w;
    // [STATE] A pointer to the Plater window, used for posting UI update events.
    wxWindow* m_plater;

    // [INTENT] A wrapper job that adds plater-specific functionality to any
    // submitted job, such as logging, error handling, and UI responsiveness.
    // [UNITY] This pattern could be implemented in Unity using a decorator or
    // a base job class that includes common functionality like logging and
    // error handling.
    class PlaterJob : public Job
    {
        // [STATE] The actual job to be executed.
        std::shared_ptr<Job> m_job;
        // [STATE] A pointer to the Plater window for UI updates.
        wxWindow* m_plater;
        // [STATE] The duration of the `process` stage, for logging.
        long long m_process_duration; // [ms]

    public:
        // [INTENT] This method executes the wrapped job's `process` method on a
        // worker thread. It also wraps the `Ctl` object to ensure the UI thread
        // is woken up for status updates.
        // [THREAD] This method runs on a worker thread.
        void process(Ctl& c) override
        {
            // [INTENT] A wrapper for the `Ctl` object that ensures the UI thread
            // is woken up whenever a status update is sent from the worker thread.
            // This prevents the UI from appearing frozen during long operations.
            // [EVENT] Calls `wxWakeUpIdle()` to force the UI thread to process events.
            struct WakeUpCtl : Ctl
            {
                Ctl& ctl;
                WakeUpCtl(Ctl& c) : ctl{c} {}

                void update_status(int st, const std::string& msg = "") override
                {
                    ctl.update_status(st, msg);
                    wxWakeUpIdle();
                }

                bool was_canceled() const override { return ctl.was_canceled(); }

                std::future<void> call_on_main_thread(std::function<void()> fn) override
                {
                    auto ftr = ctl.call_on_main_thread(std::move(fn));
                    wxWakeUpIdle();

                    return ftr;
                }

                void clear_percent() override
                {
                    ctl.clear_percent();
                    wxWakeUpIdle();
                }

                void show_error_info(const std::string& msg, int code, const std::string& description, const std::string& extra) override
                {
                    ctl.show_error_info(msg, code, description, extra);
                    wxWakeUpIdle();
                }

            } wctl{c};

            // [INTENT] Shows a busy cursor while the job is running.
            CursorSetterRAII busycursor{wctl};

            using namespace std::chrono;
            steady_clock::time_point process_start = steady_clock::now();
            m_job->process(wctl);
            steady_clock::time_point process_end = steady_clock::now();
            m_process_duration                   = duration_cast<milliseconds>(process_end - process_start).count();
        }

        // [INTENT] This method executes the wrapped job's `finalize` method on the
        // main UI thread. It also logs the total execution time and handles exceptions.
        // [THREAD] This method runs on the main UI thread.
        void finalize(bool canceled, std::exception_ptr& eptr) override
        {
            using namespace std::chrono;
            steady_clock::time_point finalize_start = steady_clock::now();
            m_job->finalize(canceled, eptr);
            steady_clock::time_point finalize_end      = steady_clock::now();
            long long                finalize_duration = duration_cast<milliseconds>(finalize_end - finalize_start).count();

            // [INTENT] Log the total execution time of the job.
            BOOST_LOG_TRIVIAL(info) << std::fixed // do not use scientific notations
                                    << "Job '" << typeid(*m_job).name() << "' "
                                    << "spend " << m_process_duration + finalize_duration << "ms "
                                    << "(process " << m_process_duration << "ms + finalize " << finalize_duration << "ms)";

            // [INTENT] Centralized exception handling. If the job threw an
            // exception, it is re-thrown here and displayed to the user.
            if (eptr)
                try {
                    std::rethrow_exception(eptr);
                } catch (std::exception& e) {
                    show_error(m_plater, _L("An unexpected error occurred") + ": " + e.what());
                    eptr = nullptr;
                }
        }

        PlaterJob(wxWindow* p, std::shared_ptr<Job> j) : m_job{std::move(j)}, m_plater{p}
        {
            // TODO: decide if disabling slice button during UI job is what we
            // want.
            //        if (m_plater)
            //            m_plater->sidebar().enable_buttons(false);
        }

        ~PlaterJob() override
        {
            // TODO: decide if disabling slice button during UI job is what we want.

            // Reload scene ensures that the slice button gets properly
            // enabled or disabled after the job finishes, depending on the
            // state of slicing. This might be an overkill but works for now.
            //        if (m_plater)
            //            m_plater->canvas3D()->reload_scene(false);
        }
    };

    // [EVENT] Guards for the idle and paint events, ensuring that the event
    // handlers are automatically disconnected when the `PlaterWorker` is destroyed.
    EventGuard on_idle_evt;
    EventGuard on_paint_evt;

public:
    // [INTENT] Constructs the `PlaterWorker`.
    // [PARAM] plater: A pointer to the Plater window.
    // [PARAM] args: Arguments to be forwarded to the `WorkerSubclass` constructor.
    template<class... WorkerArgs>
    PlaterWorker(wxWindow* plater, WorkerArgs&&... args)
        : m_w{std::forward<WorkerArgs>(args)...}
        , m_plater{plater} // Ensure that messages from the worker thread to the UI thread are
                           // processed continuously.
        , on_idle_evt(plater, wxEVT_IDLE, [this](wxIdleEvent&) { process_events(); })
        , on_paint_evt(plater, wxEVT_PAINT, [this](wxPaintEvent&) { process_events(); })
    {}

    // [INTENT] Pushes a new job to the worker queue, wrapping it in a `PlaterJob`.
    bool push(std::shared_ptr<Job> job) override { return m_w.push(std::make_shared<PlaterJob>(m_plater, std::move(job))); }

    bool is_idle() const override { return m_w.is_idle(); }
    void cancel() override { m_w.cancel(); }
    void cancel_all() override { m_w.cancel_all(); }
    void process_events() override { m_w.process_events(); }
    bool wait_for_current_job(unsigned timeout_ms = 0) override { return m_w.wait_for_current_job(timeout_ms); }
    bool wait_for_idle(unsigned timeout_ms = 0) override { return m_w.wait_for_idle(timeout_ms); }
};

}} // namespace Slic3r::GUI

#endif // PLATERJOB_HPP
