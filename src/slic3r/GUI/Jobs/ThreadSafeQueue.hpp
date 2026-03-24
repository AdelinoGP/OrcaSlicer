#ifndef THREADSAFEQUEUE_HPP
#define THREADSAFEQUEUE_HPP

#include <type_traits>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace Slic3r { namespace GUI {

// Helper structure for overloads of ThreadSafeQueueSPSC::consume_one()
// to block if the queue is empty.
// [INTENT] Parameter structure for consumers that may need to block until an item arrives, with optional timeout and completion flag pushed
// before unlocking.
struct BlockingWait
{
    // Timeout to wait for the arrival of new element into the queue.
    unsigned timeout_ms = 0;

    // An optional atomic flag to set true if an incoming element gets
    // consumed. The flag will be atomically set to true when popping the
    // front of the queue.
    std::atomic<bool>* pop_flag = nullptr;
};

// [INTENT][THREAD] Single-producer/single-consumer queue for GUI jobs that serializes access via a mutex/condition-variable pair.
// [STATE] `m_queue` stores pending work while `m_mutex` and `m_cond_var` keep producer and consumer roles synchronized.
// [UNITY] Mirror this with a `Channel<T>` or `ConcurrentQueue<T>` plus `SemaphoreSlim`/`ManualResetEventSlim` to coordinate worker-to-main
// transitions.

// A thread safe queue for one producer and one consumer.
template<class T, template<class, class...> class Container = std::deque, class... ContainerArgs> class ThreadSafeQueueSPSC
{
    std::queue<T, Container<T, ContainerArgs...>> m_queue;
    mutable std::mutex                            m_mutex;
    std::condition_variable                       m_cond_var;

public:
    // Consume one element, block if the queue is empty.
    // [INTENT][THREAD] Blocks until data arrives or the optional timeout expires, then dispatches `fn` outside the lock.
    // [STATE] Updates `blkw.pop_flag` before releasing the mutex so callers can detect when work was consumed.
    // [PORTING_HAZARD:P3] Unity needs a non-blocking equivalent (e.g., `SemaphoreSlim.WaitAsync` or `ChannelReader.TryRead`) plus a
    // cancelable timeout instead of `condition_variable::wait_for`.
    template<class Fn> bool consume_one(const BlockingWait& blkw, Fn&& fn)
    {
        static_assert(!std::is_reference_v<T>, "");
        static_assert(std::is_default_constructible_v<T>, "");
        static_assert(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>, "");

        T el;
        {
            std::unique_lock lk{m_mutex};

            auto pred = [this] { return !m_queue.empty(); };
            if (blkw.timeout_ms > 0) {
                auto timeout = std::chrono::milliseconds(blkw.timeout_ms);
                if (!m_cond_var.wait_for(lk, timeout, pred))
                    return false;
            } else
                m_cond_var.wait(lk, pred);

            if constexpr (std::is_move_assignable_v<T>)
                el = std::move(m_queue.front());
            else
                el = m_queue.front();

            m_queue.pop();

            if (blkw.pop_flag)
                // The optional flag is set before the lock is unlocked so the caller sees the data departure.
                blkw.pop_flag->store(true);
        }

        fn(el);
        return true;
    }

    // Consume one element, return true if consumed, false if queue was empty.
    // [INTENT][THREAD] Non-blocking consumer path used by the UI thread or timeout polling when work should not stall.
    // [UNITY] `ChannelReader.TryRead` or `ConcurrentQueue.TryDequeue` replicates this immediate-return pattern when running on Unity's main loop.
    template<class Fn> bool consume_one(Fn&& fn)
    {
        T el;
        {
            std::unique_lock lk{m_mutex};
            if (!m_queue.empty()) {
                if constexpr (std::is_move_assignable_v<T>)
                    el = std::move(m_queue.front());
                else
                    el = m_queue.front();

                m_queue.pop();
            } else
                return false;
        }

        fn(el);

        return true;
    }

    // Push element into the queue.
    // [THREAD][STATE] Producer path that signals the consumer through the condition variable after enqueueing a new element.
    template<class... TArgs> void push(TArgs&&... el)
    {
        std::lock_guard lk{m_mutex};
        m_queue.emplace(std::forward<TArgs>(el)...);
        m_cond_var.notify_one();
    }

    // [STATE][THREAD] Snapshot helpers that hold the lock long enough to read `m_queue` safely.
    bool empty() const
    {
        std::lock_guard lk{m_mutex};
        return m_queue.empty();
    }

    size_t size() const
    {
        std::lock_guard lk{m_mutex};
        return m_queue.size();
    }

    // [INTENT][STATE] Used during shutdown to drain buffered items while holding the mutex; no signal needed because no consumer remains.
    void clear()
    {
        std::lock_guard lk{m_mutex};
        while (!m_queue.empty())
            m_queue.pop();
    }
};

}} // namespace Slic3r::GUI

#endif // THREADSAFEQUEUE_HPP
