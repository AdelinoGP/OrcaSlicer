#ifndef EXECUTIONTBB_HPP
#define EXECUTIONTBB_HPP

// [INTENT] TBB (Intel Threading Building Blocks) execution policy. Provides
// parallel implementations of for_each and reduce using tbb::parallel_for
// and tbb::parallel_reduce respectively. This is the primary parallel
// execution policy used throughout libslic3r and the SLA module.
//
// [CONCURRENCY] Parallel execution is controlled by the TBB task arena.
// The number of threads is determined by tbb::this_task_arena::max_concurrency().
// Lambdas passed to for_each/reduce MUST be thread-safe. Shared mutable
// state must be protected with SpinningMutex (tbb::spin_mutex, for short
// critical sections) or BlockingMutex (std::mutex, for longer sections).
//
// [COUPLING] Hardcodes use of ex_tbb as the policy instance in several
// CSGMesh/ and SLA/ callers. Any port must ensure the TBB arena is
// properly initialized before these calls or replace with a custom policy.
//
// [MEMORY] TBB parallel_reduce creates sub-range partial results on the
// stack of worker threads. The init value is copied per sub-range; T must
// be cheaply copyable for good performance.
//
// [HAZARD] H951 — tbb::spin_mutex is a busy-wait lock. If a critical
// section is longer than a few nanoseconds (e.g., involves heap allocation,
// logging, or I/O), using SpinningMutex (tbb::spin_mutex) instead of
// BlockingMutex (std::mutex) will waste CPU cycles. Callers must choose
// the mutex type based on critical-section length. Severity: P2/Medium

#include <mutex>

#include <tbb/spin_mutex.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#include "Execution.hpp"

namespace Slic3r {

// [STATE] Tag type — zero-size, no member state. Parallelism is
// configured via the TBB global/arena settings, not via this object.
struct ExecutionTBB
{};
template<> struct IsExecutionPolicy_<ExecutionTBB> : public std::true_type
{};

// [STATE] Global constexpr instance — the canonical TBB parallel policy.
// All callers that need parallel execution use 'ex_tbb'.
static constexpr ExecutionTBB ex_tbb = {};

// [INTENT] Full Traits specialization for the TBB execution policy.
template<> struct execution::Traits<ExecutionTBB>
{
private:
    // [INTENT] Inner loop helpers that run serially within a TBB grain.
    // Dispatched by iterator vs integer range type.
    template<class Fn, class It> static IteratorOnly<It, void> loop_(const tbb::blocked_range<It>& range, Fn&& fn)
    {
        for (auto& el : range)
            fn(el);
    }

    template<class Fn, class I> static IntegerOnly<I, void> loop_(const tbb::blocked_range<I>& range, Fn&& fn)
    {
        for (I i = range.begin(); i < range.end(); ++i)
            fn(i);
    }

public:
    // [CONCURRENCY] SpinningMutex = tbb::spin_mutex (busy-wait, low overhead
    // for very short critical sections). BlockingMutex = std::mutex (OS-backed,
    // suitable for longer critical sections that may block).
    // [HAZARD] H951 — see file header regarding mutex selection.
    using SpinningMutex = tbb::spin_mutex;
    using BlockingMutex = std::mutex;

    // [INTENT] Parallel for-each using tbb::parallel_for.
    // 'granularity' sets the minimum grain size (elements per task).
    // Caller is responsible for choosing an appropriate granularity;
    // the default of 1 in Execution.hpp means one element per task —
    // very high overhead for large ranges. [HAZARD] H948 applies here.
    template<class It, class Fn> static void for_each(const ExecutionTBB&, It from, It to, Fn&& fn, size_t granularity)
    {
        tbb::parallel_for(tbb::blocked_range{from, to, granularity}, [&fn](const auto& range) { loop_(range, std::forward<Fn>(fn)); });
    }

    // [INTENT] Parallel reduce using tbb::parallel_reduce.
    // TBB splits the range into sub-ranges, computes partial results
    // in parallel, then merges them using mergefn.
    // [HAZARD] H949 — mergefn MUST be associative and commutative.
    // [MEMORY] 'init' is copied once per sub-range; keep T cheap to copy.
    template<class I, class MergeFn, class T, class AccessFn>
    static T reduce(const ExecutionTBB&, I from, I to, const T& init, MergeFn&& mergefn, AccessFn&& access, size_t granularity = 1)
    {
        return tbb::parallel_reduce(
            tbb::blocked_range{from, to, granularity}, init,
            [&](const auto& range, T subinit) {
                T acc = subinit;
                loop_(range, [&](auto& i) { acc = mergefn(acc, access(i)); });
                return acc;
            },
            std::forward<MergeFn>(mergefn));
    }

    // [INTENT] Query available parallelism from the current TBB task arena.
    // Returns the number of worker threads the arena has allocated.
    static size_t max_concurrency(const ExecutionTBB&) { return tbb::this_task_arena::max_concurrency(); }
};

} // namespace Slic3r

#endif // EXECUTIONTBB_HPP
