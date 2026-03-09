#ifndef BRUTEFORCEOPTIMIZER_HPP
#define BRUTEFORCEOPTIMIZER_HPP

// [INTENT] Exhaustive grid-search optimizer. Samples the N-dimensional
// search space on a uniform grid of size gridsz per dimension, evaluating
// the objective function at every grid point. Total evaluations = gridsz^N.
// This is O(gridsz^N) — exponential in dimensionality. Only suitable for
// small N (typically N=1 or N=2) or small gridsz.
//
// [COUPLING] Used by SLA Rotfinder for orientation search over a coarse
// angle grid, followed by NLopt local refinement.
//
// [MEMORY] No heap allocation beyond the Result struct. All recursion is
// compile-time (template parameter D decrements to -1).
//
// [HAZARD] H957 — AlgBurteForce (the internal struct) has a typo in its
// name ("Burte" instead of "Brute"). The public alias AlgBruteForce hides
// this, but any code that references the internal struct directly (e.g.,
// in a port or during template error messages) will encounter the typo.
// The typo is a documentation/readability hazard. Severity: P3/Low

#include <libslic3r/Optimize/Optimizer.hpp>

namespace Slic3r { namespace opt {

namespace detail {
// Implementing a bruteforce optimizer

// [INTENT] Compute the linear index of grid position 'idx' in a gridsz^N
// hypercube. Used to compare against max_iterations.
//
// [HAZARD] H958 — std::pow(gridsz, i) is floating-point exponentiation.
// For large gridsz or large i, the result may not be exactly representable
// as a long (precision loss for gridsz^i > 2^53). The result is cast to
// long implicitly. For practical grid sizes this is acceptable, but a port
// should replace with integer exponentiation (e.g., a simple loop multiply).
// Severity: P3/Low
template<size_t N> long num_iter(const std::array<size_t, N>& idx, size_t gridsz)
{
    long ret = 0;
    for (size_t i = 0; i < N; ++i)
        ret += idx[i] * std::pow(gridsz, i);
    return ret;
}

// [INTENT] Grid search implementation. Recursively steps through each
// dimension (template parameter D counts down from N-1 to -1). When D < 0,
// all dimensions have been assigned — evaluate fn(inp) and compare with
// the current best.
//
// [STATE] idx tracks the current N-dimensional grid position.
//        result holds the current best (score + optimum input).
//        bounds provides [min, max] per dimension.
//        cmp is std::less<double> for min, std::greater<double> for max.
//
// [HAZARD] H957 — struct name typo "AlgBurteForce" vs "AlgBruteForce".
struct AlgBurteForce
{
    bool         to_min;
    StopCriteria stc;
    size_t       gridsz;

    AlgBurteForce(const StopCriteria& cr, size_t gs) : stc{cr}, gridsz{gs} {}

    // [INTENT] Core recursive grid search. Returns false if iteration should
    // be terminated early (stop condition or max_iterations reached).
    template<int D, size_t N, class Fn, class Cmp>
    bool run(std::array<size_t, N>& idx, Result<N>& result, const Bounds<N>& bounds, Fn&& fn, Cmp&& cmp)
    {
        if (stc.stop_condition())
            return false;

        if constexpr (D < 0) { // Let's evaluate fn
            Input<N> inp;

            // [HAZARD] H953 — max_iterations() returns double (from unsigned
            // field). Comparison with num_iter() long is implicit conversion.
            auto max_iter = stc.max_iterations();
            if (max_iter && num_iter(idx, gridsz) >= max_iter)
                return false;

            for (size_t d = 0; d < N; ++d) {
                const Bound& b    = bounds[d];
                double       step = (b.max() - b.min()) / (gridsz - 1);
                inp[d]            = b.min() + idx[d] * step;
            }

            auto score = fn(inp);
            if (cmp(score, result.score)) { // Change current score to the new
                double absdiff = std::abs(score - result.score);

                result.score   = score;
                result.optimum = inp;

                // Check if the required precision is reached.
                if (absdiff < stc.abs_score_diff() || absdiff < stc.rel_score_diff() * std::abs(score))
                    return false;
            }

        } else {
            for (size_t i = 0; i < gridsz; ++i) {
                idx[D] = i; // Mark the current grid position and dig down
                if (!run<D - 1>(idx, result, bounds, std::forward<Fn>(fn), std::forward<Cmp>(cmp)))
                    return false;
            }
        }

        return true;
    }

    // [INTENT] Entry point for grid search. Initializes result to the
    // worst possible score and runs the recursive grid traversal.
    // 'initvals' is ignored — grid search is not warm-started.
    template<class Fn, size_t N> Result<N> optimize(Fn&& fn, const Input<N>& /*initvals*/, const Bounds<N>& bounds)
    {
        std::array<size_t, N> idx = {};
        Result<N>             result;

        if (to_min) {
            result.score = std::numeric_limits<double>::max();
            run<int(N) - 1>(idx, result, bounds, std::forward<Fn>(fn), std::less<double>{});
        } else {
            result.score = std::numeric_limits<double>::lowest();
            run<int(N) - 1>(idx, result, bounds, std::forward<Fn>(fn), std::greater<double>{});
        }

        return result;
    }
};

} // namespace detail

// [INTENT] Public alias — hides the internal typo. All external callers
// should use AlgBruteForce.
// [HAZARD] H957 — internal struct is still named AlgBurteForce (typo).
using AlgBruteForce = detail::AlgBurteForce;

// [INTENT] Full Optimizer<AlgBruteForce> specialization.
// 'gridsz' (default 100) controls the number of samples per dimension.
// For N=3, gridsz=100 means 10^6 evaluations — potentially expensive.
template<> class Optimizer<AlgBruteForce>
{
    AlgBruteForce m_alg;

public:
    Optimizer(const StopCriteria& cr = {}, size_t gridsz = 100) : m_alg{cr, gridsz} {}

    Optimizer& to_max()
    {
        m_alg.to_min = false;
        return *this;
    }
    Optimizer& to_min()
    {
        m_alg.to_min = true;
        return *this;
    }

    template<class Func, size_t N> Result<N> optimize(Func&& func, const Input<N>& initvals, const Bounds<N>& bounds)
    {
        return m_alg.optimize(std::forward<Func>(func), initvals, bounds);
    }

    Optimizer& set_criteria(const StopCriteria& cr)
    {
        m_alg.stc = cr;
        return *this;
    }

    const StopCriteria& get_criteria() const { return m_alg.stc; }
};

}} // namespace Slic3r::opt

#endif // BRUTEFORCEOPTIMIZER_HPP
