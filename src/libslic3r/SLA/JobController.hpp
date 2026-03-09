// [INTENT] JobController.hpp — callback bundle for cooperative cancellation and progress
// reporting in the SLA pipeline. Passed into long-running SLA algorithms so they can:
//   1. Report progress percentage to the front-end (statuscb)
//   2. Poll for cancellation (stopcondition)
//   3. Perform a hard abort by throwing (cancelfn, used by TriangleMeshSlicer)
//
// [DESIGN] "Stop condition" allows graceful termination (algorithm winds down naturally).
// "Cancel function" is a hard throw — used where the callee doesn't inspect stopcondition.
// This dual-mode design exists because some callers (TriangleMeshSlicer) can only be
// cancelled via exception, not by checking a flag.
//
// [COUPLING] All SLA pipeline entry points accept JobController by value. If any SLA step
// stores a raw reference to a temporary JobController, it will dangle. Callers must ensure
// the JobController outlives the algorithm call.
//
// [CONCURRENCY] std::function members are not thread-safe to copy/move concurrently.
// Safe to call concurrently IF the underlying function objects themselves are thread-safe.
// Default lambdas (no-op stopcondition, no-op cancelfn) ARE safe to call from parallel code.
//
// [HAZARD] H908 — stopcondition and cancelfn serve overlapping but non-identical roles.
// There is no documented contract about which to call in which context. New algorithm
// authors must read existing callers to understand the distinction, making incorrect usage
// likely when porting to a language without C++ function-object convention.

#ifndef SLA_JOBCONTROLLER_HPP
#define SLA_JOBCONTROLLER_HPP

#include <functional>
#include <string>

namespace Slic3r { namespace sla {

/// A Control structure for the support calculation. Consists of the status
/// indicator callback and the stop condition predicate.
struct JobController
{
    using StatusFn = std::function<void(unsigned, const std::string&)>;
    using StopCond = std::function<bool(void)>;
    using CancelFn = std::function<void(void)>;

    // This will signal the status of the calculation to the front-end
    // [STATE] Default is a no-op. Replace with a real callback to drive progress UI.
    StatusFn statuscb = [](unsigned, const std::string&) {};

    // Returns true if the calculation should be aborted.
    // [STATE] Default never cancels. Used by algorithms that check a flag between steps.
    StopCond stopcondition = []() { return false; };

    // Similar to cancel callback. This should check the stop condition and
    // if true, throw an appropriate exception. (TriangleMeshSlicer needs this)
    // consider it a hard abort. stopcondition is permits the algorithm to
    // terminate itself
    // [HAZARD] H908 — hard-abort path via exception; callers must be exception-safe.
    CancelFn cancelfn = []() {};
};

}} // namespace Slic3r::sla

#endif // JOBCONTROLLER_HPP
