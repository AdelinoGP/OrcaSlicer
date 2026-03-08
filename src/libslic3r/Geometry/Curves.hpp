// [INTENT] Header-only library for fitting 1D/nD curves to scattered
// observations via weighted least squares.  Two curve families:
//   1. PolynomialCurve — global polynomial of arbitrary degree, fitted by
//      Householder QR on a Vandermonde matrix.
//   2. PiecewiseFittedCurve — piecewise kernel curve (linear spline,
//      cubic B-spline, or Catmull-Rom spline), fitted by Householder QR on a
//      kernel weight matrix.
//
// Primary use in the codebase: pressure-advance calibration (AdaptivePAProcessor
// fits extruder speed profiles) and bed mesh compensation interpolation.
//
// [COUPLING] Depends on Bicubic.hpp (for kernel types), libslic3r/Point.hpp
// (Vec<Dimension, NumberType>), and Eigen (MatrixXf / householderQr /
// fullPivHouseholderQr).
//
// [CONCURRENCY] All fit_* functions are pure (no shared mutable state).
// fit_curve() uses Eigen's fullPivHouseholderQr which is NOT thread-safe
// for concurrent calls on the same decomposition object; each call creates its
// own local QR, so parallel calls on different data are safe.
//
// [MEMORY] Eigen matrices are heap-allocated via MatrixXf; sizes are
// proportional to observations×parameters.  For typical slicer usage (≤1000
// observations, ≤50 segments) this is negligible.
#ifndef SRC_LIBSLIC3R_GEOMETRY_CURVES_HPP_
#define SRC_LIBSLIC3R_GEOMETRY_CURVES_HPP_

#include "libslic3r/Point.hpp"
#include "Bicubic.hpp"

#include <iostream>

// #define LSQR_DEBUG

namespace Slic3r { namespace Geometry {

// [INTENT] Result type for fit_polynomial().  Stores a (Dimension × (order+1))
// coefficient matrix where each column is the coefficient vector for x^0, x^1,
// ..., x^order in the polynomial.
//
// [STATE] Pure data: no side effects, no mutation after construction.
template<int Dimension, typename NumberType> struct PolynomialCurve
{
    Eigen::MatrixXf coefficients;

    // [INTENT] Evaluate the polynomial at `value` using Horner-style accumulation
    // (implemented here as direct summation of powers for simplicity).
    // Returns a Dimension-vector result.
    Vec<Dimension, NumberType> get_fitted_value(const NumberType& value) const
    {
        Vec<Dimension, NumberType> result = Vec<Dimension, NumberType>::Zero();
        size_t                     order  = this->coefficients.rows() - 1;
        auto                       x      = NumberType(1.);
        for (size_t index = 0; index < order + 1; ++index, x *= value)
            result += x * this->coefficients.col(index);
        return result;
    }
};

// [INTENT] Fit a global polynomial of given `order` to `observations` at
// `observation_points` with per-point `weights`.
//
// Implementation: builds the weighted Vandermonde matrix T (size n × (order+1))
// and solves T * coefficients ≈ data_points per dimension using Householder QR.
// The weight is applied as sqrt(w) to both sides so the QR solves the weighted
// normal equations.
//
// Reference: https://towardsdatascience.com/least-square-polynomial-CURVES-using-c-eigen-package-c0673728bd01
//
// [HAZARD H597] Uses householderQr() (partial pivoting) which is O(n²m) and
// may be numerically unstable for near-degenerate Vandermonde matrices (high
// polynomial orders or clustered observation points). No size/condition guard.
//
// [HAZARD H598] No check that weights > 0; sqrt(w) for zero/negative weight
// produces 0 or NaN without a diagnostic.
// https://towardsdatascience.com/least-square-polynomial-CURVES-using-c-eigen-package-c0673728bd01
template<int Dimension, typename NumberType>
PolynomialCurve<Dimension, NumberType> fit_polynomial(const std::vector<Vec<Dimension, NumberType>>& observations,
                                                      const std::vector<NumberType>&                 observation_points,
                                                      const std::vector<NumberType>&                 weights,
                                                      size_t                                         order)
{
    // check to make sure inputs are correct
    size_t cols = order + 1;
    assert(observation_points.size() >= cols);
    assert(observation_points.size() == weights.size());
    assert(observations.size() == weights.size());

    Eigen::MatrixXf data_points(Dimension, observations.size());
    Eigen::MatrixXf T(observations.size(), cols);
    for (size_t i = 0; i < weights.size(); ++i) {
        auto squared_weight = sqrt(weights[i]);
        data_points.col(i)  = observations[i] * squared_weight;
        // Populate the matrix
        auto x = squared_weight;
        auto c = observation_points[i];
        for (size_t j = 0; j < cols; ++j, x *= c)
            T(i, j) = x;
    }

    const auto      QR = T.householderQr();
    Eigen::MatrixXf coefficients(Dimension, cols);
    // Solve for linear least square fit
    for (size_t dim = 0; dim < Dimension; ++dim) {
        coefficients.row(dim) = QR.solve(data_points.row(dim).transpose());
    }

    return {std::move(coefficients)};
}

// [INTENT] Result type for fit_curve().  Stores fitted kernel spline parameters:
//   coefficients: (Dimension × parameters_count) matrix — one column per control point.
//   start: x-value of the first observation point.
//   segment_size: width of each segment in observation-point units.
//   endpoints_level_of_freedom: extra control points added at each end for symmetry.
//
// [STATE] Pure data after construction; get_fitted_value() is const/pure.
template<size_t Dimension, typename NumberType, typename KernelType> struct PiecewiseFittedCurve
{
    using Kernel = KernelType;

    Eigen::MatrixXf coefficients;
    NumberType      start;
    NumberType      segment_size;
    size_t          endpoints_level_of_freedom;

    // [INTENT] Evaluate the piecewise curve at `observation_point` by finding
    // the kernel_span control points that influence this location and blending
    // them with their kernel weights.
    //
    // [COUPLING] Kernel::kernel_span determines the number of active control
    // points.  kernel(normalized_distance) returns the blending weight.
    //
    // [INTENT] std::clamp on parameter_index: out-of-range indices are clamped
    // to the nearest endpoint coefficient (same boundary extension used during fitting).
    Vec<Dimension, NumberType> get_fitted_value(const NumberType& observation_point) const
    {
        Vec<Dimension, NumberType> result = Vec<Dimension, NumberType>::Zero();

        // find corresponding segment index; expects kernels to be centered
        int middle_right_segment_index = floor((observation_point - start) / segment_size);
        // find index of first segment that is affected by the point i; this can be deduced from kernel_span
        int start_segment_idx = middle_right_segment_index - Kernel::kernel_span / 2 + 1;
        for (int segment_index = start_segment_idx; segment_index < int(start_segment_idx + Kernel::kernel_span); segment_index++) {
            NumberType segment_start               = start + segment_index * segment_size;
            NumberType normalized_segment_distance = (segment_start - observation_point) / segment_size;

            int parameter_index = segment_index + endpoints_level_of_freedom;
            parameter_index     = std::clamp(parameter_index, 0, int(coefficients.cols()) - 1);
            result += Kernel::kernel(normalized_segment_distance) * coefficients.col(parameter_index);
        }
        return result;
    }
};

// [INTENT] Fit a piecewise kernel curve to `observations` at `observation_points`
// with per-point `weights`, using `segments_count` uniform segments over the
// observation range and `endpoints_level_of_freedom` extra control points at
// each end.
//
// Algorithm:
//   1. Compute sqrt(weights) for weighted least squares.
//   2. Compute metadata: valid_length, segment_size, total parameter count.
//   3. Build the kernel weight matrix T (n_observations × parameters_count):
//      each row contains kernel(normalized_distance) * sqrt_weight for the
//      kernel_span neighboring control points.
//   4. Solve T * coefficients ≈ data_points per dimension via fullPivHouseholderQr.
//
// parameters_count = segments_count + 1 + 2*endpoints_level_of_freedom
//   (+1 makes the parametric space symmetric about the center)
//
// [HAZARD H597] fullPivHouseholderQr is O(n²m) (n=observations, m=parameters).
// No size guard; callers must ensure this is acceptable for their input sizes.
//
// [HAZARD H598] assert(weights[index] > 0) — fires only in debug builds;
// zero or negative weights produce sqrt(0) = 0 (degenerate row) or NaN without
// a graceful error path.

// observations: data to be fitted by the curve
// observation points: growing sequence of points where the observations were made.
//      In other words, for function f(x) = y, observations are y0...yn, and observation points are x0...xn
// weights: how important the observation is
// segments_count: number of segments inside the valid length of the curve
// endpoints_level_of_freedom: number of additional parameters at each end; reasonable values depend on the kernel span
template<typename Kernel, int Dimension, typename NumberType>
PiecewiseFittedCurve<Dimension, NumberType, Kernel> fit_curve(const std::vector<Vec<Dimension, NumberType>>& observations,
                                                              const std::vector<NumberType>&                 observation_points,
                                                              const std::vector<NumberType>&                 weights,
                                                              size_t                                         segments_count,
                                                              size_t                                         endpoints_level_of_freedom)
{
    // check to make sure inputs are correct
    assert(segments_count > 0);
    assert(observations.size() > 0);
    assert(observation_points.size() == observations.size());
    assert(observation_points.size() == weights.size());
    assert(segments_count <= observations.size());

    // prepare sqrt of weights, which will then be applied to both matrix T and observed data: https://en.wikipedia.org/wiki/Weighted_least_squares
    std::vector<NumberType> sqrt_weights(weights.size());
    for (size_t index = 0; index < weights.size(); ++index) {
        // [HAZARD H598] assert aborts in debug; no graceful error path for
        // zero or negative weights in release builds.
        assert(weights[index] > 0);
        sqrt_weights[index] = sqrt(weights[index]);
    }

    // prepare result and compute metadata
    PiecewiseFittedCurve<Dimension, NumberType, Kernel> result{};

    NumberType valid_length           = observation_points.back() - observation_points.front();
    NumberType segment_size           = valid_length / NumberType(segments_count);
    result.start                      = observation_points.front();
    result.segment_size               = segment_size;
    result.endpoints_level_of_freedom = endpoints_level_of_freedom;

    // prepare observed data
    // Eigen defaults to column major memory layout.
    Eigen::MatrixXf data_points(Dimension, observations.size());
    for (size_t index = 0; index < observations.size(); ++index) {
        data_points.col(index) = observations[index] * sqrt_weights[index];
    }
    // [INTENT] parameters_count = segments_count + 1 + 2*endpoints_level_of_freedom.
    // The +1 ensures symmetry: without it, the last segment boundary falls exactly
    // on the last observation, giving that boundary less DoF than the first.
    // parameters count is always increased by one to make the parametric space of the curve symmetric.
    // without this fix, the end of the curve is less flexible than the beginning
    size_t parameters_count = segments_count + 1 + 2 * endpoints_level_of_freedom;
    // Create weight matrix T for each point and each segment;
    Eigen::MatrixXf T(observation_points.size(), parameters_count);
    T.setZero();
    // Fill the weight matrix
    for (size_t i = 0; i < observation_points.size(); ++i) {
        NumberType observation_point = observation_points[i];
        // find corresponding segment index; expects kernels to be centered
        int middle_right_segment_index = floor((observation_point - result.start) / result.segment_size);
        // find index of first segment that is affected by the point i; this can be deduced from kernel_span
        int start_segment_idx = middle_right_segment_index - int(Kernel::kernel_span / 2) + 1;
        for (int segment_index = start_segment_idx; segment_index < int(start_segment_idx + Kernel::kernel_span); segment_index++) {
            NumberType segment_start               = result.start + segment_index * result.segment_size;
            NumberType normalized_segment_distance = (segment_start - observation_point) / result.segment_size;

            // [INTENT] Clamp out-of-range parameter indices to endpoints so that
            // observations outside the valid range still contribute (same boundary
            // extension used in get_fitted_value()).
            int parameter_index = segment_index + endpoints_level_of_freedom;
            parameter_index     = std::clamp(parameter_index, 0, int(parameters_count) - 1);
            T(i, parameter_index) += Kernel::kernel(normalized_segment_distance) * sqrt_weights[i];
        }
    }

#ifdef LSQR_DEBUG
    // [INTENT] Debug dump of the weight matrix to stdout when LSQR_DEBUG is defined.
    std::cout << "weight matrix: " << std::endl;
    for (int obs = 0; obs < observation_points.size(); ++obs) {
        std::cout << std::endl;
        for (int segment = 0; segment < parameters_count; ++segment) {
            std::cout << T(obs, segment) << "  ";
        }
    }
    std::cout << std::endl;
#endif

    // [INTENT] Solve the weighted least-squares system using full-pivot Householder
    // QR decomposition.  fullPivHouseholderQr() is used (rather than the cheaper
    // householderQr()) because the weight matrix T may have rank-deficient columns
    // when observations cluster in a small region of the parameter space.
    // [HAZARD H597] O(n²m) complexity; no guard against large inputs.
    // Solve for linear least square fit
    result.coefficients.resize(Dimension, parameters_count);
    const auto QR = T.fullPivHouseholderQr();
    for (size_t dim = 0; dim < Dimension; ++dim) {
        result.coefficients.row(dim) = QR.solve(data_points.row(dim).transpose());
    }

    return result;
}

// [INTENT] Convenience wrapper: fit a piecewise linear spline (LinearKernel)
// to multidimensional observations.  endpoints_level_of_freedom defaults to 0
// because the linear kernel has span 4 but each basis function overlaps only
// two segments, so no extra endpoint DoF is needed.
template<int Dimension, typename NumberType>
PiecewiseFittedCurve<Dimension, NumberType, LinearKernel<NumberType>> fit_linear_spline(
    const std::vector<Vec<Dimension, NumberType>>& observations,
    std::vector<NumberType>                        observation_points,
    std::vector<NumberType>                        weights,
    size_t                                         segments_count,
    size_t                                         endpoints_level_of_freedom = 0)
{
    return fit_curve<LinearKernel<NumberType>>(observations, observation_points, weights, segments_count, endpoints_level_of_freedom);
}

// [INTENT] Convenience wrapper: fit a piecewise cubic B-spline (CubicBSplineKernel).
// B-splines provide C2 continuity; the approximating property means the fitted
// curve does not pass through observations exactly, which can be desirable when
// observations are noisy.
template<int Dimension, typename NumberType>
PiecewiseFittedCurve<Dimension, NumberType, CubicBSplineKernel<NumberType>> fit_cubic_bspline(
    const std::vector<Vec<Dimension, NumberType>>& observations,
    std::vector<NumberType>                        observation_points,
    std::vector<NumberType>                        weights,
    size_t                                         segments_count,
    size_t                                         endpoints_level_of_freedom = 0)
{
    return fit_curve<CubicBSplineKernel<NumberType>>(observations, observation_points, weights, segments_count, endpoints_level_of_freedom);
}

// [INTENT] Convenience wrapper: fit a Catmull-Rom piecewise spline
// (CubicCatmulRomKernel).  Catmull-Rom is interpolating (passes through
// control points) with C1 continuity; preferred when exact passage through
// key observations is required.
template<int Dimension, typename NumberType>
PiecewiseFittedCurve<Dimension, NumberType, CubicCatmulRomKernel<NumberType>> fit_catmul_rom_spline(
    const std::vector<Vec<Dimension, NumberType>>& observations,
    std::vector<NumberType>                        observation_points,
    std::vector<NumberType>                        weights,
    size_t                                         segments_count,
    size_t                                         endpoints_level_of_freedom = 0)
{
    return fit_curve<CubicCatmulRomKernel<NumberType>>(observations, observation_points, weights, segments_count,
                                                       endpoints_level_of_freedom);
}

}} // namespace Slic3r::Geometry

#endif /* SRC_LIBSLIC3R_GEOMETRY_CURVES_HPP_ */
