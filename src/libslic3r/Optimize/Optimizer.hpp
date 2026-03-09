#ifndef OPTIMIZER_HPP
#define OPTIMIZER_HPP

// [INTENT] Abstract optimization framework using CRTP-style policy dispatch.
// Defines the common types (Result, Bound, Input, StopCriteria, ScoreGradient)
// and a primary Optimizer<Method> template that serves as both a fallback
// (static_assert on unimplemented methods) and a contract for specializations.
// Concrete implementations are provided by NLoptOptimizer.hpp (NLopt-based)
// and BruteforceOptimizer.hpp (grid search).
//
// [COUPLING] Used by SLA rotation finder (SLA/Rotfinder.cpp), support
// placement, and other geometry optimization passes. The optimizer is
// instantiated with a specific Method tag (e.g., AlgNLoptGenetic,
// AlgBruteForce).
//
// [MEMORY] All state is value-typed. StopCriteria holds a std::function
// for the stop condition — heap allocation on construction.

#include <utility>
#include <tuple>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <cassert>
#include <optional>

namespace Slic3r { namespace opt {

// [INTENT] Holds the complete result of an optimization run.
// 'resultcode' is method-dependent (e.g., NLopt result codes).
// 'optimum' is the input array at the best found point.
// 'score' is the objective function value at optimum.
template<size_t N> struct Result
{
    int                   resultcode; // Method dependent
    std::array<double, N> optimum;
    double                score;
};

// [INTENT] One-dimensional search interval [min, max].
// Default-constructed to [double::min, double::max] — effectively unbounded.
// [HAZARD] H952 — Default min is std::numeric_limits<double>::min() which is
// the smallest POSITIVE double (~2.2e-308), NOT the most negative value
// (which is lowest()). Callers expecting "no lower bound" must explicitly
// pass std::numeric_limits<double>::lowest(). This is a subtle semantic trap
// in the C++ numeric_limits API. Severity: P2/Medium
class Bound
{
    double m_min, m_max;

public:
    Bound(double min = std::numeric_limits<double>::min(), double max = std::numeric_limits<double>::max()) : m_min(min), m_max(max) {}

    double min() const noexcept { return m_min; }
    double max() const noexcept { return m_max; }
};

// [INTENT] Fixed-size array of doubles representing one evaluation point.
// [INTENT] Fixed-size array of Bounds, one per dimension.
template<size_t N> using Input  = std::array<double, N>;
template<size_t N> using Bounds = std::array<Bound, N>;

// [INTENT] Fluent-API stop criteria builder for all optimizer variants.
// All fields are default-initialized to NaN (for score thresholds) or 0
// (for max_iterations), which means "not set" for most backends.
//
// [HAZARD] H953 — m_max_iterations is 'unsigned' but the setter accepts
// 'double'. The assignment `m_max_iterations = val` on line 84 silently
// truncates a double to unsigned integer. If a caller passes a negative
// double (e.g., -1.0 meaning "unlimited"), the result is a very large
// unsigned value (wraps around), causing the optimizer to run for an
// enormous number of iterations. Severity: P1/High
//
// [HAZARD] H954 — max_iterations() getter returns 'double' but the field
// is 'unsigned'. The implicit widening conversion is harmless for values
// that fit in double's mantissa (<= 2^53), but the asymmetric types
// (unsigned storage, double interface) are confusing and likely to cause
// bugs in a port that preserves the field type. Severity: P2/Medium
class StopCriteria
{
    // If the absolute value difference between two scores.
    double m_abs_score_diff = std::nan("");

    // If the relative value difference between two scores.
    double m_rel_score_diff = std::nan("");

    // Stop if this value or better is found.
    double m_stop_score = std::nan("");

    // A predicate that if evaluates to true, the optimization should terminate
    // and the best result found prior to termination should be returned.
    // [MEMORY] std::function causes heap allocation here.
    std::function<bool()> m_stop_condition = [] { return false; };

    // [HAZARD] H953 — unsigned storage, see above.
    unsigned m_max_iterations = 0;

public:
    StopCriteria& abs_score_diff(double val)
    {
        m_abs_score_diff = val;
        return *this;
    }

    double abs_score_diff() const { return m_abs_score_diff; }

    StopCriteria& rel_score_diff(double val)
    {
        m_rel_score_diff = val;
        return *this;
    }

    double rel_score_diff() const { return m_rel_score_diff; }

    StopCriteria& stop_score(double val)
    {
        m_stop_score = val;
        return *this;
    }

    double stop_score() const { return m_stop_score; }

    // [HAZARD] H953 — double parameter silently truncated to unsigned on store.
    StopCriteria& max_iterations(double val)
    {
        m_max_iterations = val;
        return *this;
    }

    // [HAZARD] H954 — returns double but field is unsigned (implicit widening).
    double max_iterations() const { return m_max_iterations; }

    template<class Fn> StopCriteria& stop_condition(Fn&& cond)
    {
        m_stop_condition = cond;
        return *this;
    }

    bool stop_condition() { return m_stop_condition(); }
};

// [INTENT] Score + optional gradient for gradient-aware optimization methods.
// If gradient is std::nullopt, the method does not provide gradient info.
// If gradient is populated, it is an N-element array of partial derivatives.
//
// [HAZARD] H955 — In NLoptOptimizer.hpp line 95: `(*score.gradient)[i]` is
// called unconditionally when 'gradient' (the C-callback pointer) is non-null.
// However, if the user function returns a ScoreGradient with
// gradient == std::nullopt, dereferencing the optional is UB. The conditional
// `if constexpr (std::is_convertible_v<RetT, ScoreGradient<N>>)` branch
// does NOT check whether score.gradient has a value before indexing it.
// Severity: P0/Critical
template<size_t N> struct ScoreGradient
{
    double                               score;
    std::optional<std::array<double, N>> gradient;

    ScoreGradient(double s, const std::array<double, N>& grad) : score{s}, gradient{grad} {}
};

// [INTENT] always_false<T> enables deferred static_assert in templates.
// When the base Optimizer template is instantiated with an unrecognized
// Method, the static_assert fires with a meaningful error message instead
// of a cryptic "incomplete type" error.
template<class T> struct always_false
{
    enum { value = false };
};

// [INTENT] Primary (fallback) Optimizer template. All methods that would
// perform real work are empty no-ops. A partial specialization in
// NLoptOptimizer.hpp or BruteforceOptimizer.hpp must provide real
// implementations for each supported Method type.
template<class Method, class Enable = void> class Optimizer
{
public:
    // [HAZARD] If this constructor is instantiated (i.e., no specialization
    // matches), the static_assert fires at compile time. This is the intended
    // guard against unimplemented methods.
    Optimizer(const StopCriteria&) { static_assert(always_false<Method>::value, "Optimizer unimplemented for given method!"); }

    // Switch optimization towards function minimum
    Optimizer& to_min() { return *this; }

    // Switch optimization towards function maximum
    Optimizer& to_max() { return *this; }

    // Set criteria for successive optimizations
    Optimizer& set_criteria(const StopCriteria&) { return *this; }

    // Get current criteria
    StopCriteria get_criteria() const { return {}; };

    // [INTENT] Find function minimum or maximum. The objective function
    // signature is either:
    //   double(const Input<N>&)                  — score only
    //   ScoreGradient<N>(const Input<N>&)        — score + gradient
    // initvals must lie within bounds; behavior is undefined otherwise.
    template<class Func, size_t N> Result<N> optimize(Func&& /*func*/, const Input<N>& /*initvals*/, const Bounds<N>& /*bounds*/)
    {
        return {};
    }

    // optional for randomized methods:
    void seed(long /*s*/) {}
};

namespace detail {

// [INTENT] Copy C-array into std::array without branching. Relies on
// compiler optimization (copy elision / NRVO) to eliminate the temporary.
template<size_t N, class T> auto to_arr(const T* a)
{
    std::array<T, N> r;
    std::copy(a, a + N, std::begin(r));
    return r;
}

template<size_t N, class T> auto to_arr(const T (&a)[N]) { return to_arr<N>(static_cast<const T*>(a)); }

} // namespace detail

// [INTENT] Convenience helpers for constructing Bounds and Input arrays
// from C-style array literals, avoiding verbose initializer syntax.
template<size_t N> Bounds<N> bounds(const Bound (&b)[N]) { return detail::to_arr(b); }
template<size_t N> Input<N>  initvals(const double (&a)[N]) { return detail::to_arr(a); }
template<size_t N> auto      score_gradient(double s, const double (&grad)[N]) { return ScoreGradient<N>(s, detail::to_arr(grad)); }

}} // namespace Slic3r::opt

#endif // OPTIMIZER_HPP
