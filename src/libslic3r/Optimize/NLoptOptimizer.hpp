#ifndef NLOPTOPTIMIZER_HPP
#define NLOPTOPTIMIZER_HPP

// [INTENT] NLopt-backed optimizer implementations. Provides specializations
// of Optimizer<M> for two families of NLopt algorithm descriptors:
//   - NLoptAlg<alg>         — single algorithm
//   - NLoptAlgComb<gl,lc>   — global algorithm augmented by a local refiner
// The C NLopt library is wrapped in a minimal RAII class (NLopt) and a
// C-callback bridge (optfunc<Fn,N>) that adapts C++ functors to NLopt's
// `nlopt_func` signature.
//
// [COUPLING] Depends on the NLopt C library (nlopt.h). The C callback
// receives a void* pointing to a stack-allocated tuple — callers must ensure
// the optimizer lifetime covers the entire optimize() call (it does, because
// optimize() is synchronous).
//
// [MEMORY] NLopt RAII wrapper owns the nlopt_opt handle; no copy/move.
// TOptData tuple holds raw pointers to the function object and NLoptOpt
// instance — valid only during the synchronous optimize() call.
//
// [CONCURRENCY] NLopt global state (nlopt_srand) is not thread-safe. The
// seed() method calls nlopt_srand which modifies global NLopt RNG state.
// Concurrent seeds from multiple threads will race. Severity: P2/Medium.

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#endif
#include <nlopt.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <utility>

#include <libslic3r/Optimize/Optimizer.hpp>

namespace Slic3r { namespace opt {

namespace detail {

// [INTENT] Tag type wrapping a single nlopt_algorithm enum value.
// Used as template parameter to select the NLopt algorithm at compile time.
template<nlopt_algorithm alg> struct NLoptAlg
{};

// [INTENT] Tag type for a combined global+local NLopt algorithm.
// The global algorithm (gl_alg) drives the search; when it finds a promising
// region it delegates refinement to the local algorithm (lc_alg, default
// Nelder-Mead simplex).
template<nlopt_algorithm gl_alg, nlopt_algorithm lc_alg = NLOPT_LN_NELDERMEAD> struct NLoptAlgComb
{};

// [INTENT] Type-trait to identify NLopt algorithm tag types.
template<class M> struct IsNLoptAlg
{
    static const constexpr bool value = false;
};

template<nlopt_algorithm a> struct IsNLoptAlg<NLoptAlg<a>>
{
    static const constexpr bool value = true;
};

template<nlopt_algorithm a1, nlopt_algorithm a2> struct IsNLoptAlg<NLoptAlgComb<a1, a2>>
{
    static const constexpr bool value = true;
};

// [INTENT] SFINAE gate for NLopt-only template specializations.
template<class M, class T = void> using NLoptOnly = std::enable_if_t<IsNLoptAlg<M>::value, T>;

enum class OptDir { MIN, MAX }; // Where to optimize

// [INTENT] Minimal RAII wrapper for nlopt_opt handle.
// Copy and move are disabled — NLopt handles are not reference-counted.
// Destruction calls nlopt_destroy to release NLopt internal memory.
struct NLopt
{ // Helper RAII class for nlopt_opt
    nlopt_opt ptr = nullptr;

    template<class... A> explicit NLopt(A&&... a) { ptr = nlopt_create(std::forward<A>(a)...); }

    NLopt(const NLopt&)            = delete;
    NLopt(NLopt&&)                 = delete;
    NLopt& operator=(const NLopt&) = delete;
    NLopt& operator=(NLopt&&)      = delete;

    ~NLopt() { nlopt_destroy(ptr); }
};

template<class Method> class NLoptOpt
{};

// [INTENT] Optimizer backend for a single NLopt algorithm.
// Template parameter 'alg' is the nlopt_algorithm enum value.
template<nlopt_algorithm alg> class NLoptOpt<NLoptAlg<alg>>
{
protected:
    StopCriteria m_stopcr;
    OptDir       m_dir;

    // [INTENT] TOptData packs the three things the C callback needs:
    // 1. pointer to the user functor, 2. pointer to this NLoptOpt,
    // 3. the nlopt_opt handle (for force-stop). All are raw pointers;
    // lifetime is guaranteed by the synchronous call stack.
    template<class Fn> using TOptData = std::tuple<std::remove_reference_t<Fn>*, NLoptOpt*, nlopt_opt>;

    // [INTENT] C-callback bridge. NLopt requires a C function pointer;
    // this static template function casts the void* back to TOptData,
    // checks the stop condition, and calls the user functor.
    //
    // [HAZARD] H955 — ScoreGradient null-optional UB: if the user function
    // returns a ScoreGradient<N> but score.gradient == std::nullopt, the
    // line `(*score.gradient)[i]` dereferences a null optional — undefined
    // behavior. The constexpr branch checks the RETURN TYPE but not whether
    // the optional is engaged at runtime. Severity: P0/Critical
    //
    // [HAZARD] H956 — 'gradient' pointer is assumed non-null when the
    // ScoreGradient branch executes. NLopt may pass gradient=nullptr for
    // gradient-free algorithms even if the function signature accepts it.
    // Writing to a null gradient pointer is UB. Severity: P1/High
    template<class Fn, size_t N> static double optfunc(unsigned n, const double* params, double* gradient, void* data)
    {
        assert(n >= N);

        auto tdata = static_cast<TOptData<Fn>*>(data);

        if (std::get<1>(*tdata)->m_stopcr.stop_condition())
            nlopt_force_stop(std::get<2>(*tdata));

        auto fnptr  = std::get<0>(*tdata);
        auto funval = to_arr<N>(params);

        double scoreval = 0.;
        using RetT      = decltype((*fnptr)(funval));
        if constexpr (std::is_convertible_v<RetT, ScoreGradient<N>>) {
            ScoreGradient<N> score = (*fnptr)(funval);
            // [HAZARD] H955: score.gradient may be std::nullopt here — UB.
            // [HAZARD] H956: 'gradient' pointer not checked for null.
            for (size_t i = 0; i < n; ++i)
                gradient[i] = (*score.gradient)[i];
            scoreval = score.score;
        } else {
            scoreval = (*fnptr)(funval);
        }

        return scoreval;
    }

    // [INTENT] Configure the nlopt_opt with bounds and stop criteria.
    template<size_t N> void set_up(NLopt& nl, const Bounds<N>& bounds)
    {
        std::array<double, N> lb, ub;

        for (size_t i = 0; i < N; ++i) {
            lb[i] = bounds[i].min();
            ub[i] = bounds[i].max();
        }

        nlopt_set_lower_bounds(nl.ptr, lb.data());
        nlopt_set_upper_bounds(nl.ptr, ub.data());

        double abs_diff = m_stopcr.abs_score_diff();
        double rel_diff = m_stopcr.rel_score_diff();
        double stopval  = m_stopcr.stop_score();
        if (!std::isnan(abs_diff))
            nlopt_set_ftol_abs(nl.ptr, abs_diff);
        if (!std::isnan(rel_diff))
            nlopt_set_ftol_rel(nl.ptr, rel_diff);
        if (!std::isnan(stopval))
            nlopt_set_stopval(nl.ptr, stopval);

        // [HAZARD] H953 — max_iterations() returns double (cast from unsigned);
        // nlopt_set_maxeval takes int. Large unsigned values overflow here.
        if (m_stopcr.max_iterations() > 0)
            nlopt_set_maxeval(nl.ptr, m_stopcr.max_iterations());
    }

    // [INTENT] Internal optimize: set objective, run nlopt_optimize.
    template<class Fn, size_t N> Result<N> optimize(NLopt& nl, Fn&& fn, const Input<N>& initvals)
    {
        Result<N> r;

        // Stack-allocated — valid for the duration of nlopt_optimize.
        TOptData<Fn> data = std::make_tuple(&fn, this, nl.ptr);

        switch (m_dir) {
        case OptDir::MIN: nlopt_set_min_objective(nl.ptr, optfunc<Fn, N>, &data); break;
        case OptDir::MAX: nlopt_set_max_objective(nl.ptr, optfunc<Fn, N>, &data); break;
        }

        r.optimum    = initvals;
        r.resultcode = nlopt_optimize(nl.ptr, r.optimum.data(), &r.score);

        return r;
    }

public:
    // [INTENT] Public optimize entry point for single-algorithm variant.
    template<class Func, size_t N> Result<N> optimize(Func&& func, const Input<N>& initvals, const Bounds<N>& bounds)
    {
        NLopt nl{alg, N};
        set_up(nl, bounds);

        return optimize(nl, std::forward<Func>(func), initvals);
    }

    explicit NLoptOpt(StopCriteria stopcr = {}) : m_stopcr(stopcr) {}

    void                set_criteria(const StopCriteria& cr) { m_stopcr = cr; }
    const StopCriteria& get_criteria() const noexcept { return m_stopcr; }
    void                set_dir(OptDir dir) noexcept { m_dir = dir; }

    // [CONCURRENCY] nlopt_srand modifies global NLopt state — not thread-safe.
    void seed(long s) { nlopt_srand(s); }
};

// [INTENT] Specialization for combined global+local algorithms.
// Inherits set_up/optfunc from the global NLoptOpt<NLoptAlg<glob>> base.
// Creates two nlopt_opt handles; attaches the local optimizer to the global
// via nlopt_set_local_optimizer.
template<nlopt_algorithm glob, nlopt_algorithm loc> class NLoptOpt<NLoptAlgComb<glob, loc>> : public NLoptOpt<NLoptAlg<glob>>
{
    using Base = NLoptOpt<NLoptAlg<glob>>;

public:
    template<class Fn, size_t N> Result<N> optimize(Fn&& f, const Input<N>& initvals, const Bounds<N>& bounds)
    {
        NLopt nl_glob{glob, N}, nl_loc{loc, N};

        Base::set_up(nl_glob, bounds);
        Base::set_up(nl_loc, bounds);
        nlopt_set_local_optimizer(nl_glob.ptr, nl_loc.ptr);

        return Base::optimize(nl_glob, std::forward<Fn>(f), initvals);
    }

    explicit NLoptOpt(StopCriteria stopcr = {}) : Base{stopcr} {}
};

} // namespace detail

// [INTENT] Public Optimizer<M> specialization for all NLopt algorithm tags.
// Thin wrapper delegating to the internal NLoptOpt<M> implementation.
template<class M> class Optimizer<M, detail::NLoptOnly<M>>
{
    detail::NLoptOpt<M> m_opt;

public:
    Optimizer& to_max()
    {
        m_opt.set_dir(detail::OptDir::MAX);
        return *this;
    }
    Optimizer& to_min()
    {
        m_opt.set_dir(detail::OptDir::MIN);
        return *this;
    }

    template<class Func, size_t N> Result<N> optimize(Func&& func, const Input<N>& initvals, const Bounds<N>& bounds)
    {
        return m_opt.optimize(std::forward<Func>(func), initvals, bounds);
    }

    explicit Optimizer(StopCriteria stopcr = {}) : m_opt(stopcr) {}

    Optimizer& set_criteria(const StopCriteria& cr)
    {
        m_opt.set_criteria(cr);
        return *this;
    }

    const StopCriteria& get_criteria() const { return m_opt.get_criteria(); }

    void seed(long s) { m_opt.seed(s); }
};

// [INTENT] Predefined algorithm aliases for common NLopt configurations.
// AlgNLoptGenetic — ESCH global evolutionary + Nelder-Mead local refiner
// AlgNLoptSubplex — Subplex (LN_SBPLX), a robust gradient-free local method
// AlgNLoptSimplex — Nelder-Mead simplex, classic gradient-free local method
// AlgNLoptDIRECT  — DIRECT global dividing rectangles
// AlgNLoptMLSL    — Multi-Level Single-Linkage (MLSL) global stochastic
using AlgNLoptGenetic = detail::NLoptAlgComb<NLOPT_GN_ESCH>;
using AlgNLoptSubplex = detail::NLoptAlg<NLOPT_LN_SBPLX>;
using AlgNLoptSimplex = detail::NLoptAlg<NLOPT_LN_NELDERMEAD>;
using AlgNLoptDIRECT  = detail::NLoptAlg<NLOPT_GN_DIRECT>;
using AlgNLoptMLSL    = detail::NLoptAlg<NLOPT_GN_MLSL>;

}} // namespace Slic3r::opt

#endif // NLOPTOPTIMIZER_HPP
