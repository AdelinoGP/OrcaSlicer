#include <exception>

#include "BoostThreadWorker.hpp"

namespace Slic3r { namespace GUI {

void BoostThreadWorker::WorkerMessage::deliver(BoostThreadWorker& runner)
{
    // [EVENT][THREAD] Deliver encapsulated worker messages while the UI thread drains `m_output_queue`, keeping finalization serialized
    // with the main event pump.
    switch (MsgType(get_type())) {
    case Empty: break;
    case Status: {
        auto info = boost::get<StatusInfo>(m_data);
        if (runner.get_pri()) {
            runner.get_pri()->set_progress(info.status);
            runner.get_pri()->set_status_text(info.msg.c_str());
        }
        break;
    }
    case Finalize: {
        auto& entry = boost::get<JobEntry>(m_data);
        entry.job->finalize(entry.canceled, entry.eptr);

        // [THREAD] after finalization the exception flows back to the worker thread so any upstream scheduler can log or crash explicitly.
        if (entry.eptr)
            std::rethrow_exception(entry.eptr);

        break;
    }
    case MainThreadCall: {
        auto& calldata = boost::get<MainThreadCallData>(m_data);
        calldata.fn();
        calldata.promise.set_value();

        break;
    }
    }
}

void BoostThreadWorker::run()
{
    // [INTENT][THREAD] Persistent worker loop consumes job entries until it receives the sentinel, keeping processing isolated from the UI thread.
    bool stop = false;
    while (!stop) {
        m_input_queue.consume_one(BlockingWait{0, &m_running}, [this, &stop](JobEntry& e) {
            if (!e.job)
                stop = true;
            else {
                m_canceled.store(false);

                try {
                    e.job->process(*this);
                } catch (...) {
                    e.eptr = std::current_exception();
                }

                e.canceled = m_canceled.load();
                // [EVENT][STATE] enqueue the job entry for the UI thread to finalize and mirror progress into the renderer.
                m_output_queue.push(std::move(e)); // finalization message
            }
            m_running.store(false);
        });
    };
}

void BoostThreadWorker::update_status(int st, const std::string& msg)
{
    // [EVENT] worker progress/status updates ride the output queue so the UI thread can surface them through the progress indicator.
    m_output_queue.push(st, msg);
}

std::future<void> BoostThreadWorker::call_on_main_thread(std::function<void()> fn)
{
    // [THREAD][UNITY] Enqueue a callback for the main thread and expose a future; Unity ports should similarly queue work through a
    // `UnityMainThreadDispatcher` plus `TaskCompletionSource`.
    MainThreadCallData cbdata{std::move(fn), {}};
    std::future<void>  future = cbdata.promise.get_future();

    m_output_queue.push(std::move(cbdata));

    return future;
}

BoostThreadWorker::BoostThreadWorker(std::shared_ptr<ProgressIndicator> pri, boost::thread::attributes& attribs, const char* name)
    : m_progress(std::move(pri)), m_name{name}
{
    // [STATE][EVENT] tie the progress indicator so cancel buttons trigger `cancel_all` and the worker can show status updates.
    if (m_progress)
        m_progress->set_cancel_callback([this]() { cancel(); });

    m_thread = create_thread(attribs, [this] { this->run(); });

    std::string nm{name};
    if (!nm.empty())
        set_thread_name(m_thread, name);
}

constexpr int ABORT_WAIT_MAX_MS = 10000;

BoostThreadWorker::~BoostThreadWorker()
{
    // [THREAD][PORTING_HAZARD:P3] teardown grabs the input queue and waits for join so background threads don't leak on fast shutdown.
    bool joined = false;
    try {
        cancel_all();
        wait_for_idle(ABORT_WAIT_MAX_MS);
        m_input_queue.push(JobEntry{nullptr});
        joined = join(ABORT_WAIT_MAX_MS);
    } catch (...) {}

    if (!joined)
        BOOST_LOG_TRIVIAL(error) << "Could not join worker thread '" << m_name << "'";
}

bool BoostThreadWorker::join(int timeout_ms)
{
    // [THREAD] guard the blocking join call so shutdown observers can bail out if the worker fails to exit.
    if (!m_thread.joinable())
        return true;

    if (timeout_ms <= 0) {
        m_thread.join();
    } else if (m_thread.try_join_for(boost::chrono::milliseconds(timeout_ms))) {
        return true;
    } else
        return false;

    return true;
}

void BoostThreadWorker::process_events()
{
    // [EVENT][THREAD][UNITY] Drain queued messages on the main thread so the worker's finalization and callbacks fire before continuing
    // (mirror with UnityMainThreadDispatcher polling).
    while (m_output_queue.consume_one([this](WorkerMessage& msg) { msg.deliver(*this); }))
        ;
}

bool BoostThreadWorker::wait_for_current_job(unsigned timeout_ms)
{
    // [STATE][THREAD] Wait until the running job posts Finalize so callers know when the worker finished, similar to Unity
    // `TaskCompletionSource` wait handles.
    bool ret = true;

    if (!is_idle()) {
        bool was_finish      = false;
        bool timeout_reached = false;
        while (!timeout_reached && !was_finish) {
            timeout_reached = !m_output_queue.consume_one(BlockingWait{timeout_ms}, [this, &was_finish](WorkerMessage& msg) {
                msg.deliver(*this);
                if (msg.get_type() == WorkerMessage::Finalize)
                    was_finish = true;
            });
        }

        ret = !timeout_reached;
    }

    return ret;
}

bool BoostThreadWorker::wait_for_idle(unsigned timeout_ms)
{
    // [STATE][THREAD][UNITY] Spin until the worker reports idle so callers know background work drained, akin to Unity's `Task.WhenAll` polls.
    bool timeout_reached = false;
    while (!timeout_reached && !is_idle()) {
        timeout_reached = !m_output_queue.consume_one(BlockingWait{timeout_ms}, [this](WorkerMessage& msg) { msg.deliver(*this); });
    }

    return !timeout_reached;
}

bool BoostThreadWorker::push(std::shared_ptr<Job> job)
{
    // [INTENT][STATE] queue background jobs on the input queue; null submissions are rejected so callers notice mistakes.
    if (!job)
        return false;

    m_input_queue.push(JobEntry{std::move(job)});
    return true;
}

}} // namespace Slic3r::GUI
