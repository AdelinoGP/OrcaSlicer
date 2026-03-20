#ifndef JOB_HPP
#define JOB_HPP

#include <atomic>
#include <exception>
#include <future>

#include <wx/window.h>

#include "libslic3r/libslic3r.h"
#include "ProgressIndicator.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] A base job encapsulating work that must run off the UI thread so
// the main application stays responsive while `Worker` orchestrates execution.
// [PORTING_HAZARD:P3] Unity will need to mirror this with async Tasks plus
// main-thread callbacks because there is no built-in wx-style Job framework.
class Job {
public:
    enum JobPrepareState {
        PREPARE_STATE_DEFAULT = 0,
        PREPARE_STATE_MENU    = 1,
    };

    // [STATE] Indicates whether the job is started from the regular pipeline or
    // triggered directly from a menu action so downstream callers can tweak
    // progress visuals or cancellation cues accordingly.

    // A controller interface that informs the job about cancellation and
    // makes it possible for the job to advertise its status.
    class Ctl {
    public:
        virtual ~Ctl() = default;

        // [INTENT] Keeps job authors informed of UI-visible progress and lets
        // them emit notifications from the worker thread.
        // [EVENT] Called repeatedly as long as the job publishes intermediate
        // state or log text while running.
        // status update, to be used from the work thread (process() method)
        virtual void update_status(int st, const std::string &msg = "") = 0;

        // Returns true if the job was asked to cancel itself.
        virtual bool was_canceled() const = 0;

        // Orca:
        virtual void clear_percent()                                                                                             = 0;
        virtual void show_error_info(const std::string &msg, int code, const std::string &description, const std::string &extra) = 0;

        // Execute a functor on the main thread. Note that the exact time of
        // execution is hard to determine. This can be used to make modifications
        // on the UI, like displaying some intermediate results or modify the
        // cursor.
        // This function returns a std::future<void> object which enables the
        // caller to optionally wait for the main thread to finish the function call.
        // [THREAD][UNITY] Unity would implement this as a `MainThreadDispatcher`
        // or `SynchronizationContext.Post` call that queues a lambda for execution
        // inside the MonoBehaviour update loop (e.g., `UnityMainThreadDispatcher`).
        // [PORTING_HAZARD:P2] Unity needs explicit marshaling so that the
        // worker task can observe UI state safely and avoid race conditions.
        virtual std::future<void> call_on_main_thread(std::function<void()> fn) = 0;
    };

    virtual ~Job() = default;

    // The method where the actual work of the job should be defined. This is
    // run on the worker thread.
    // [THREAD] Guaranteed to run off the UI thread; any heavy compute or I/O
    // must be confined here and communicate status back via `Ctl`.
    virtual void process(Ctl &ctl) = 0;

    // Launched when the job is finished on the UI thread.
    // If the job was cancelled, the first parameter will have a true value.
    // Exceptions occuring in process() are redirected from the worker thread
    // into the main (UI) thread. This method receives the exception and can
    // handle it properly. Assign nullptr to this second argument before
    // function return to prevent further action. Leaving it with a non-null
    // value will result in rethrowing by the worker.
    // [THREAD][INTENT] Post-processing on the UI thread; handles cancellation
    // cleanup, error reporting, and optionally rethrows via the provided
    // exception pointer so that dialogs can surface worker errors.
    // [PORTING_HAZARD:P3] Unity needs the same guarantee that only the main
    // thread touches UI state, so mirror this with `UnityMainThreadDispatcher`
    // callbacks to rethrow exceptions from worker Tasks.
    virtual void finalize(bool /*canceled*/, std::exception_ptr &) {}
};

}} // namespace Slic3r::GUI

#endif // JOB_HPP
