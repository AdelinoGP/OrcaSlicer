#ifndef BUSYCURSORJOB_HPP
#define BUSYCURSORJOB_HPP

#include "Job.hpp"

#include <wx/utils.h>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

// [INTENT] RAII helper that flips wxWidgets into busy-cursor mode for the duration of a background job.
// [THREAD] All cursor toggles happen through `Job::Ctl::call_on_main_thread` so we never call wxBegin/EndBusyCursor off the UI thread.
// [UNITY] Unity ports should mirror this with a main-thread dispatcher that wraps `Cursor.SetCursor` or a busy overlay around each
// `AsyncOperation`. [PORTING_HAZARD:P3] Without a synchronized dispatcher the cursor state can get stuck if the background work completes
// while Unity is still busy, so keep main-thread handoff precise.
struct CursorSetterRAII
{
    Job::Ctl &ctl;
    CursorSetterRAII(Job::Ctl &c) : ctl{c}
    {
        ctl.call_on_main_thread([] { wxBeginBusyCursor(); });
    }
    ~CursorSetterRAII()
    {
        try {
            ctl.call_on_main_thread([] { wxEndBusyCursor(); });
        } catch(...) {
            // [EVENT] best-effort cleanup run when the main-thread callback fails; we log to keep reviewers aware of stuck cursors.
            BOOST_LOG_TRIVIAL(error) << "Can't revert cursor from busy to normal";
        }
    }
};

template<class JobSubclass>
class BusyCursored: public Job {
    // [STATE] Decorated job instance keeps the substantive work and context alive for the wrapper.
    JobSubclass m_job;

public:
    template<class... Args>
    BusyCursored(Args &&...args) : m_job{std::forward<Args>(args)...}
    {
    }

    void process(Ctl &ctl) override
    {
        // [EVENT] `process` is invoked by the scheduler; the RAII cursor setter wraps the actual work so the user sees the busy cursor.
        // [THREAD] This code executes on a worker thread but switches back to the main thread briefly to toggle the cursor state.
        // [UNITY] In Unity this would be an `AsyncOperation` or `Job` whose `started`/`completed` callbacks update `Cursor.SetCursor`
        // through `UnityMainThreadDispatcher`.
        CursorSetterRAII cursor_setter{ctl};
        m_job.process(ctl);
    }

    void finalize(bool canceled, std::exception_ptr &eptr) override
    {
        // [EVENT] Finalize simply forwards so the decorated job can release resources or push notifications while the busy cursor already
        // wrapped the work.
        m_job.finalize(canceled, eptr);
    }
};

}} // namespace Slic3r::GUI

#endif // BUSYCURSORJOB_HPP
