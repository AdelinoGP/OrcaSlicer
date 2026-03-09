// [INTENT] Concurrency.hpp — thin abstraction layer for switching the SLA pipeline
// between sequential and parallel execution. Wraps TBB and a sequential executor
// behind a uniform interface so algorithm code can be tested sequentially and
// deployed with TBB parallelism by flipping USE_FULL_CONCURRENCY.
//
// FIXME: Deprecated — marked for removal in-source. The Execution/ abstraction in
// libslic3r/Execution/ is the preferred replacement.
//
// [STATE] USE_FULL_CONCURRENCY is a compile-time constant (`true`). The `ccr` alias
// always resolves to `_ccr<true>` (TBB). The `ccr_seq` alias always resolves to
// `_ccr<false>` (sequential) regardless of USE_FULL_CONCURRENCY.
//
// [CONCURRENCY] `ccr::for_each` dispatches to `execution::for_each(ex_tbb, ...)`.
// TBB work-stealing scheduler; granularity parameter controls chunk size.
// `ccr::reduce` dispatches to `execution::reduce(ex_tbb, ...)` — associative reduction.
//
// [HAZARD] H909 — `max_concurreny()` (note typo: missing 'c') is a public API spelling
// error. Any port or refactor that introduces a correctly spelled `max_concurrency()`
// override must update all call sites, as the misspelled symbol is the actual entry point.
//
// [COUPLING] All SLA algorithms that use `ccr::for_each` implicitly assume TBB is
// available and initialized. Replacing TBB with another scheduler requires only
// changing the ExecutionTBB include and `ex_tbb` reference, but compile-time
// USE_FULL_CONCURRENCY is not a runtime switch — the selection is baked at compile time.

#ifndef SLA_CONCURRENCY_H
#define SLA_CONCURRENCY_H

// FIXME: Deprecated

#include <libslic3r/Execution/ExecutionSeq.hpp>
#include <libslic3r/Execution/ExecutionTBB.hpp>

namespace Slic3r { namespace sla {

// Set this to true to enable full parallelism in this module.
// Only the well tested parts will be concurrent if this is set to false.
// [STATE] Compile-time constant — always true in production. Not runtime-configurable.
const constexpr bool USE_FULL_CONCURRENCY = true;

// [INTENT] Template tag type: _ccr<true> = TBB parallel, _ccr<false> = sequential.
template<bool> struct _ccr
{};

// [INTENT] TBB parallel specialization.
template<> struct _ccr<true>
{
    using SpinningMutex = execution::SpinningMutex<ExecutionTBB>;
    using BlockingMutex = execution::BlockingMutex<ExecutionTBB>;

    template<class It, class Fn> static void for_each(It from, It to, Fn&& fn, size_t granularity = 1)
    {
        execution::for_each(ex_tbb, from, to, std::forward<Fn>(fn), granularity);
    }

    template<class... Args> static auto reduce(Args&&... args) { return execution::reduce(ex_tbb, std::forward<Args>(args)...); }

    // [HAZARD] H909 — typo: "max_concurreny" missing 'c'. Cannot be fixed without
    // breaking all existing call sites that use the misspelled name.
    static size_t max_concurreny() { return execution::max_concurrency(ex_tbb); }
};

// [INTENT] Sequential fallback specialization — used for testing or single-threaded contexts.
template<> struct _ccr<false>
{
    using SpinningMutex = execution::SpinningMutex<ExecutionSeq>;
    using BlockingMutex = execution::BlockingMutex<ExecutionSeq>;

    template<class It, class Fn> static void for_each(It from, It to, Fn&& fn, size_t granularity = 1)
    {
        execution::for_each(ex_seq, from, to, std::forward<Fn>(fn), granularity);
    }

    template<class... Args> static auto reduce(Args&&... args) { return execution::reduce(ex_seq, std::forward<Args>(args)...); }

    static size_t max_concurreny() { return execution::max_concurrency(ex_seq); }
};

// [STATE] ccr = compile-time selected executor (TBB in production).
// ccr_seq = always sequential (for tests).
// ccr_par = always parallel (explicit TBB).
using ccr     = _ccr<USE_FULL_CONCURRENCY>;
using ccr_seq = _ccr<false>;
using ccr_par = _ccr<true>;

}} // namespace Slic3r::sla

#endif // SLACONCURRENCY_H
