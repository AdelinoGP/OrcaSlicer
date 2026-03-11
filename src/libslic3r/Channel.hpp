#ifndef slic3r_Channel_hpp_
#define slic3r_Channel_hpp_

#include <memory>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <boost/optional.hpp>

// [INTENT] Channel<T> is a minimal MPMC-style in-process queue abstraction used to move
//   work items between producer/consumer threads without exposing condition_variable logic
//   at call sites. lock_read()/lock_rw() additionally offer explicit queue access windows.
// [STATE] Mutable shared state is m_queue guarded by m_mutex; push/pop mutate queue order.
//   size_hint() is intentionally unlocked and only a hint (racy by design).
// [MEMORY] Queue elements are owned by std::deque<T>. lock_read()/lock_rw() return unique_ptr
//   with custom deleter (Unlocker) that releases the held mutex at scope exit (RAII unlock).
// [CONCURRENCY] pop() blocks on m_condition until non-empty. push(..., silent=true) suppresses
//   notification for batched enqueue paths. try_pop() is non-blocking under lock.
// [COUPLING] Uses boost::optional instead of std::optional for historical compiler support,
//   making callers depend on Boost API conventions.
// [HAZARD] Unlocker has a copy constructor that moves lock ownership (MSVC workaround); copying
//   deleter state is non-standard and fragile if this pattern is translated naively.

namespace Slic3r {

template<class T> class Channel
{
public:
    using UniqueLock = std::unique_lock<std::mutex>;

    template<class Ptr> class Unlocker
    {
    public:
        Unlocker(UniqueLock lock) : m_lock(std::move(lock)) {}
        Unlocker(const Unlocker& other) noexcept
            : m_lock(std::move(other.m_lock)) {} // XXX: done beacuse of MSVC 2013 not supporting init of deleter by move
        Unlocker(Unlocker&& other) noexcept : m_lock(std::move(other.m_lock)) {}
        Unlocker& operator=(const Unlocker& other) = delete;
        Unlocker& operator=(Unlocker&& other) { m_lock = std::move(other.m_lock); }

        void operator()(Ptr*) { m_lock.unlock(); }

    private:
        mutable UniqueLock m_lock; // XXX: mutable: see above
    };

    using Queue          = std::deque<T>;
    using LockedConstPtr = std::unique_ptr<const Queue, Unlocker<const Queue>>;
    using LockedPtr      = std::unique_ptr<Queue, Unlocker<Queue>>;

    Channel() {}
    ~Channel() {}

    void push(const T& item, bool silent = false)
    {
        {
            UniqueLock lock(m_mutex);
            m_queue.push_back(item);
        }
        if (!silent) {
            m_condition.notify_one();
        }
    }

    void push(T&& item, bool silent = false)
    {
        {
            UniqueLock lock(m_mutex);
            m_queue.push_back(std::forward<T>(item));
        }
        if (!silent) {
            m_condition.notify_one();
        }
    }

    T pop()
    {
        UniqueLock lock(m_mutex);
        m_condition.wait(lock, [this]() { return !m_queue.empty(); });
        auto item = std::move(m_queue.front());
        m_queue.pop_front();
        return item;
    }

    boost::optional<T> try_pop()
    {
        UniqueLock lock(m_mutex);
        if (m_queue.empty()) {
            return boost::none;
        } else {
            auto item = std::move(m_queue.front());
            m_queue.pop();
            return item;
        }
    }

    // Unlocked observer/hint. Thread unsafe! Keep in mind you need to re-verify the result after locking.
    size_t size_hint() const noexcept { return m_queue.size(); }

    LockedConstPtr lock_read() const { return LockedConstPtr(&m_queue, Unlocker<const Queue>(UniqueLock(m_mutex))); }

    LockedPtr lock_rw() { return LockedPtr(&m_queue, Unlocker<Queue>(UniqueLock(m_mutex))); }

private:
    Queue                   m_queue;
    mutable std::mutex      m_mutex;
    std::condition_variable m_condition;
};

} // namespace Slic3r

#endif // slic3r_Channel_hpp_
