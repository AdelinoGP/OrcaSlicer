#ifndef EXECUTIONSEQ_HPP
#define EXECUTIONSEQ_HPP

// [INTENT] Sequential (single-threaded) execution policy. Provides the same
// interface as ExecutionTBB but runs everything serially. Used in code paths
// that must not incur parallelism (e.g., unit tests, deterministic
// post-processing steps, or contexts where TBB is not available).
//
// [CONCURRENCY] All mutex types in this policy are no-ops: lock() and
// unlock() are empty inline functions. This is deliberate — there is no
// concurrent execution so locks are unnecessary. A port that replaces
// ExecutionSeq with a real policy must audit all SpinningMutex/BlockingMutex
// usages and insert real synchronization.
//
// [HAZARD] H950 — The no-op _Mtx is aliased as BOTH SpinningMutex AND
// BlockingMutex. If a caller guards state that is actually shared (e.g., a
// lambda that captures a reference used outside), a naive swap to the TBB
// policy without checking which mutex is appropriate will silently lose
// protection. The no-op makes the sequential path compile cleanly but masks
// concurrency assumptions. Severity: P2/Medium
//
// [COUPLING] Supports optional PRUSASLICER_USE_EXECUTION_STD to alias
// std::execution::sequenced_policy as a sequential execution policy. That
// path is gated behind the macro and is NOT active in OrcaSlicer builds.

#ifdef PRUSASLICER_USE_EXECUTION_STD // Conflicts with our version of TBB
#include <execution>
#endif

#include "Execution.hpp"

namespace Slic3r {

// [STATE] Tag type — zero-size, no member state. Safe to pass by value or
// as constexpr.
struct ExecutionSeq
{};

template<> struct IsExecutionPolicy_<ExecutionSeq> : public std::true_type
{};

// [STATE] Global constexpr instance — the canonical sequential policy object.
// All callers that need sequential execution use 'ex_seq'.
static constexpr ExecutionSeq ex_seq = {};

// [INTENT] Type traits to distinguish sequential policies from parallel ones.
// Used by callers that want to take different code paths depending on whether
// the policy is sequential (e.g., to skip locking entirely).
template<class EP> struct IsSequentialEP_
{
    static constexpr bool value = false;
};

template<> struct IsSequentialEP_<ExecutionSeq> : public std::true_type
{};
#ifdef PRUSASLICER_USE_EXECUTION_STD
// [COUPLING] When the STD execution path is enabled, std::execution::sequenced_policy
// is treated as a sequential policy too.
template<> struct IsExecutionPolicy_<std::execution::sequenced_policy> : public std::true_type
{};
template<> struct IsSequentialEP_<std::execution::sequenced_policy> : public std::true_type
{};
#endif

template<class EP> constexpr bool IsSequentialEP = IsSequentialEP_<remove_cvref_t<EP>>::value;

// [INTENT] SFINAE gate for sequential-only overloads.
template<class EP, class R = EP> using SequentialEPOnly = std::enable_if_t<IsSequentialEP<EP>, R>;

// [INTENT] Traits specialization for all sequential execution policies.
// A single specialization covers both ExecutionSeq and (if enabled) the
// std sequential policy via the SequentialEPOnly<EP> enable-if.
template<class EP> struct execution::Traits<EP, SequentialEPOnly<EP, void>>
{
private:
    // [INTENT] No-op mutex satisfying BasicLockable. Used for both
    // SpinningMutex and BlockingMutex in sequential contexts.
    // [HAZARD] H950 — see file header. No actual mutual exclusion.
    struct _Mtx
    {
        inline void lock() {}
        inline void unlock() {}
    };

    // [INTENT] Internal loop helper — dispatches on whether the range
    // uses iterators or integer indices.
    template<class Fn, class It> static IteratorOnly<It, void> loop_(It from, It to, Fn&& fn)
    {
        for (auto it = from; it != to; ++it)
            fn(*it);
    }

    template<class Fn, class I> static IntegerOnly<I, void> loop_(I from, I to, Fn&& fn)
    {
        for (I i = from; i < to; ++i)
            fn(i);
    }

public:
    using SpinningMutex = _Mtx; // [HAZARD] H950 — no-op, see above
    using BlockingMutex = _Mtx; // [HAZARD] H950 — no-op, see above

    // [INTENT] Serial for-each — granularity parameter is ignored.
    template<class It, class Fn> static void for_each(const EP&, It from, It to, Fn&& fn, size_t /* ignore granularity */ = 1)
    {
        loop_(from, to, std::forward<Fn>(fn));
    }

    // [INTENT] Serial reduce — left fold using mergefn(acc, access(element)).
    // Result is deterministic (sequential left-to-right order).
    template<class I, class MergeFn, class T, class AccessFn>
    static T reduce(const EP&, I from, I to, const T& init, MergeFn&& mergefn, AccessFn&& access, size_t /*granularity*/ = 1)
    {
        T acc = init;
        loop_(from, to, [&](auto& i) { acc = mergefn(acc, access(i)); });
        return acc;
    }

    // [INTENT] Always returns 1 — no parallelism in this policy.
    static size_t max_concurrency(const EP&) { return 1; }
};

} // namespace Slic3r

#endif // EXECUTIONSEQ_HPP
