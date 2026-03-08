// AdaptivePAInterpolator.hpp
// OrcaSlicer
//
// Header file for the AdaptivePAInterpolator class, responsible for interpolating pressure advance (PA) values based on flow rate and
// acceleration using PCHIP interpolation.
//
// [INTENT] Public interface for the 2D PA interpolation model. Exposes two operations:
//   1. parseAndSetData(csv_string) — parses calibration CSV, builds internal PCHIP models.
//   2. operator()(flow_rate, acceleration) — queries the model for a PA value.
// Consumers (AdaptivePAProcessor) call isInitialised() before calling operator().
//
// [COUPLING] Depends on PchipInterpolatorHelper for the underlying 1D PCHIP primitive.
// The class stores a std::map<double, PchipInterpolatorHelper> (one per acceleration level).
// This means PchipInterpolatorHelper must be copyable/assignable — stored by value in the map.
//
// [STATE] m_isInitialised is the only boolean gate protecting operator() from use before
// parseAndSetData() succeeds. It is set to false on construction and on parse error,
// and to true only after a complete successful parse.
//
// [HAZARD] The class has no mutex protection. Multi-threaded access to a single instance
// would require external synchronization. In practice, one instance is created per tool
// and never shared across threads (see AdaptivePAProcessor.cpp).
//
// [HAZARD] operator() returns -1.0 (a sentinel double) to indicate failure. Callers
// must explicitly test `result == -1` or `result < 0`. This is error-prone in a language
// where -1.0 is a valid (though physically nonsensical) PA value. A std::optional<double>
// return would be safer for a refactored implementation.

#ifndef ADAPTIVEPAINTERPOLATOR_HPP
#define ADAPTIVEPAINTERPOLATOR_HPP

#include <vector>
#include <string>
#include <map>
#include "PchipInterpolatorHelper.hpp"

/**
 * @class AdaptivePAInterpolator
 * @brief A class to interpolate pressure advance (PA) values based on flow rate and acceleration using Piecewise Cubic Hermite
 * Interpolating Polynomial (PCHIP) interpolation.
 */
class AdaptivePAInterpolator
{
public:
    /**
     * @brief Default constructor.
     */
    // [STATE] Initializes m_isInitialised to false. The object is unusable for interpolation
    // until parseAndSetData() is called and returns 0 (success).
    AdaptivePAInterpolator() : m_isInitialised(false) {}

    /**
     * @brief Parses the input data and sets up the interpolators.
     * @param data A string containing the data in CSV format (PA, flow rate, acceleration).
     * @return 0 on success, -1 on error.
     */
    // [INTENT] Entry point for loading calibration data. Clears existing state before parsing,
    // so repeated calls are safe (acts as a reset). See implementation for CSV format details.
    // [HAZARD] On parse failure, ALL previously-valid state is cleared — no partial model is retained.
    int parseAndSetData(const std::string& data);

    /**
     * @brief Interpolates the PA value for the given flow rate and acceleration.
     * @param flow_rate The flow rate at which to interpolate.
     * @param acceleration The acceleration at which to interpolate.
     * @return The interpolated PA value, or -1 if interpolation fails.
     */
    // [INTENT] 2D "slice-and-interpolate" evaluation. For each known acceleration,
    // interpolates PA at flow_rate, then interpolates across the resulting (accel, PA) array.
    // [HAZARD] Constructs a new PchipInterpolatorHelper on every call (for the accel axis).
    // For hot loops (many segments), this is repeated O(N_accel) allocations per call.
    // [HAZARD] Returns -1.0 sentinel on failure — callers must check explicitly.
    double operator()(double flow_rate, double acceleration);

    /**
     * @brief Returns the initialization status.
     * @return The value of m_isInitialised.
     */
    // [STATE] Must be checked before calling operator(). If false, operator() will
    // return -1 for all inputs (no data has been loaded successfully).
    bool isInitialised() const { return m_isInitialised; }

private:
    // [STATE] Central data structure: maps each calibration acceleration value to a
    // 1D PCHIP interpolator over (flow_rate → PA). Built by parseAndSetData().
    // [MEMORY] PchipInterpolatorHelper stored by value — copying this map copies all
    // internal PCHIP coefficient vectors. The class is not move-optimized.
    std::map<double, PchipInterpolatorHelper> flow_interpolators_; ///< Map each acceleration to a flow-rate-to-PA interpolator.

    // [STATE] Parallel array to flow_interpolators_.keys(), maintained separately for
    // efficient iteration during the acceleration-axis interpolation in operator().
    // Must be kept in sync with flow_interpolators_ — both are populated only in parseAndSetData().
    std::vector<double> accelerations_; ///< Store unique accelerations.

    // [STATE] Gate flag. false = not yet initialized or last parse failed.
    // true = parseAndSetData() succeeded and the model is ready for queries.
    bool m_isInitialised;
};

#endif // ADAPTIVEPAINTERPOLATOR_HPP
