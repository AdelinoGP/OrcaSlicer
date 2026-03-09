#ifndef EXECUTION_HPP
#define EXECUTION_HPP

// [INTENT] Policy-based execution abstraction layer. Defines a uniform
// interface (for_each, reduce, accumulate) that dispatches to either a
// sequential implementation (ExecutionSeq) or a TBB parallel implementation
// (ExecutionTBB) depending on the execution policy type passed as a template
// parameter. This is the façade header — it contains no implementation, only
// the contract.
//
// [COUPLING] Consumed throughout libslic3r/SLA/ wherever parallel iteration
// is needed. All callers must include either ExecutionSeq.hpp or
// ExecutionTBB.hpp (not just this file) to get a concrete Traits
// specialization; including only this header produces linker/template
// instantiation errors.
//
// [CONCURRENCY] The execution policy is passed by value/const-ref at every
// call site — the policy objects themselves carry no mutable state, so they
// are safe to share across threads. Mutex types are aliased from the policy
// (SpinningMutex, BlockingMutex) and must satisfy BasicLockable.

#include <type_traits>
#include <utility>
#include <cstddef>
#include <iterator>

#include "libslic3r/libslic3r.h"

namespace Slic3r {

// [INTENT] Opt-in registration: a policy type becomes valid only when
// IsExecutionPolicy_<EP> is specialized to inherit std::true_type.
// Prevents accidental instantiation with arbitrary types.
template<class EP> struct IsExecutionPolicy_ : public std::false_type
{};

template<class EP> constexpr bool IsExecutionPolicy = IsExecutionPolicy_<remove_cvref_t<EP>>::value;

// [INTENT] SFINAE gate: function templates are enabled only when EP is a
// registered execution policy.
template<class EP, class T = void> using ExecutionPolicyOnly = std::enable_if_t<IsExecutionPolicy<EP>, T>;

namespace execution {

// [INTENT] Primary template for policy traits — intentionally left empty.
// Must be fully specialized for each execution policy. The specialization
// must provide:
//   - SpinningMutex type (BasicLockable, low-contention)
//   - BlockingMutex  type (BasicLockable, high-contention / OS-backed)
//   - static for_each(ep, from, to, fn, granularity)
//   - static reduce(ep, from, to, init, mergefn, accessfn, granularity)
//   - static max_concurrency(ep) -> size_t
// [HAZARD] H947 — If a port introduces a new execution policy without
// specializing Traits<EP>, the code will compile (the primary template
// matches) but calls to for_each/reduce will silently do nothing. There is
// no static_assert in the primary template to catch this omission.
// Severity: P2/Medium
template<class EP, class En = void> struct Traits
{};

template<class EP> using AsTraits = Traits<remove_cvref_t<EP>>;

// [INTENT] Mutex type aliases — allow callers to declare lock variables
// that match the concurrency model of the chosen policy without knowing
// whether TBB or sequential execution is in use.
template<class EP> using SpinningMutex = typename Traits<EP>::SpinningMutex;
template<class EP> using BlockingMutex = typename Traits<EP>::BlockingMutex;

// [INTENT] Query the number of hardware/arena threads available.
// Returns 1 for sequential policy; TBB arena size for TBB policy.
template<class EP, class = ExecutionPolicyOnly<EP>> size_t max_concurrency(const EP& ep) { return AsTraits<EP>::max_concurrency(ep); }

// [INTENT] Parallel/sequential for-each over [from, to). 'granularity'
// controls the TBB grain size (minimum chunk per task). Ignored in
// sequential policy.
// [HAZARD] H948 — granularity=1 (the default) passed to TBB results in
// very fine-grained tasks with high scheduler overhead for large ranges.
// Callers that do not tune granularity may see severe performance
// regression on large inputs. Severity: P3/Low
template<class EP, class It, class Fn, class = ExecutionPolicyOnly<EP>>
void for_each(const EP& ep, It from, It to, Fn&& fn, size_t granularity = 1)
{
    AsTraits<EP>::for_each(ep, from, to, std::forward<Fn>(fn), granularity);
}

// [INTENT] Parallel reduce. 'mergefn' combines two partial results of
// type T; 'accessfn' maps each element (or integer index) to T.
// Mirrors std::transform_reduce semantics.
// [HAZARD] H949 — 'mergefn' MUST be commutative and associative for
// correct parallel results. No static check enforces this. A port using
// a non-associative merge (e.g. floating-point subtraction) will produce
// non-deterministic results with the TBB policy. Severity: P1/High
template<class EP, class I, class MergeFn, class T, class AccessFn, class = ExecutionPolicyOnly<EP>>
T reduce(const EP& ep, I from, I to, const T& init, MergeFn&& mergefn, AccessFn&& accessfn, size_t granularity = 1)
{
    return AsTraits<EP>::reduce(ep, from, to, init, std::forward<MergeFn>(mergefn), std::forward<AccessFn>(accessfn), granularity);
}

// [INTENT] Overload of reduce for direct-iterator ranges where the
// element itself is the value (identity access functor).
template<class EP, class I, class MergeFn, class T, class = ExecutionPolicyOnly<EP>>
T reduce(const EP& ep, I from, I to, const T& init, MergeFn&& mergefn, size_t granularity = 1)
{
    return reduce(ep, from, to, init, std::forward<MergeFn>(mergefn), [](const auto& i) { return i; }, granularity);
}

// [INTENT] Parallel sum: specialisation of reduce with std::plus<T> as
// the merge function. Equivalent to std::transform_reduce with plus.
template<class EP, class I, class T, class AccessFn, class = ExecutionPolicyOnly<EP>>
T accumulate(const EP& ep, I from, I to, const T& init, AccessFn&& accessfn, size_t granularity = 1)
{
    return reduce(ep, from, to, init, std::plus<T>{}, std::forward<AccessFn>(accessfn), granularity);
}

// [INTENT] Identity-access overload of accumulate (element == value).
template<class EP, class I, class T, class = ExecutionPolicyOnly<EP>>
T accumulate(const EP& ep, I from, I to, const T& init, size_t granularity = 1)
{
    return reduce(ep, from, to, init, std::plus<T>{}, [](const auto& i) { return i; }, granularity);
}

} // namespace execution
} // namespace Slic3r

#endif // EXECUTION_HPP
