// AdaptivePAInterpolator.cpp
// OrcaSlicer
//
// Implementation file for the AdaptivePAInterpolator class, providing methods to parse data and perform PA interpolation.
//
// [INTENT] The AdaptivePAInterpolator implements a 2D interpolation model:
//   PA_coefficient = f(flow_rate_mm3s, acceleration_mm_s2)
// using Piecewise Cubic Hermite Interpolating Polynomial (PCHIP) interpolation.
// The model structure is a "2D grid" of 1D PCHIP interpolators:
//   - Outer axis: acceleration values (from calibration CSV)
//   - Inner axis per acceleration: flow_rate → PA
//
// Evaluation for a (flow_rate, accel) query:
//   1. For each stored acceleration, interpolate PA at the query flow_rate
//      using that acceleration's PchipInterpolatorHelper.
//   2. Build a 1D array of (acceleration → interpolated_PA_at_flow).
//   3. Run a second PCHIP interpolation over this 1D array to get the final PA.
//
// This is a "slice-then-interpolate" strategy: first interpolate along flow_rate
// at each known acceleration, then interpolate the resulting values along acceleration.
// It avoids a full 2D surface fitting and works well for calibration grids.
//
// [MEMORY] This class has no dynamic allocation beyond STL containers.
// `flow_interpolators_` is a std::map<double, PchipInterpolatorHelper>.
// PchipInterpolatorHelper objects are stored by value (not pointer).
//
// [CONCURRENCY] Not thread-safe. AdaptivePAInterpolator instances are per-tool and
// are only accessed from the single-threaded process_layer() call in AdaptivePAProcessor.

#include "AdaptivePAInterpolator.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <sstream>

/**
 * @brief Parses the input data and sets up the interpolators.
 * @param data A string containing the data in CSV format (PA, flow rate, acceleration).
 * @return 0 on success, -1 on error.
 */
// [INTENT] Parses CSV data in the format: "PA,flowrate,acceleration\n..." (one sample per line).
// Groups samples by acceleration value. For each acceleration group, if there are at least 2
// data points, creates a PchipInterpolatorHelper(flowRates[], paValues[]).
// Groups with only 1 data point are silently discarded (insufficient for PCHIP).
//
// [HAZARD] The CSV parser uses std::stod() for all fields. If the CSV contains non-numeric
// data (e.g., header row, empty lines), stod() will throw std::invalid_argument, which is
// caught by the blanket catch(const std::exception&) block. The entire model is then set
// to m_isInitialised = false and returns -1, silently discarding ALL previously-parsed
// valid data. There is no partial-success recovery.
//
// [HAZARD] The CSV format is undocumented in this file. The expected column order is
// PA, flow_rate, acceleration (columns 1, 2, 3). If the user supplies the CSV with
// columns in a different order (e.g., flow_rate, PA, acceleration), the model is built
// incorrectly without any error. A header-parsing validation step is absent.
//
// [HAZARD] `paValue = flowRate = acceleration = 0.f` (line 30) initializes all as float
// but the variables are declared as double. The assignment from float literal 0.f to double
// is correct (implicit widening), but the initial value assignment pattern is misleading:
// if any of the three getline() calls fails (malformed CSV), the remaining values remain at 0.0
// rather than being skipped. A partially-parsed line with e.g. only PA parsed will contribute
// a (flowRate=0, acceleration=0) entry to the model.
//
// [STATE] Calls flow_interpolators_.clear() and accelerations_.clear() at entry — safe to
// call parseAndSetData() multiple times to reload the model.
int AdaptivePAInterpolator::parseAndSetData(const std::string& data)
{
    flow_interpolators_.clear();
    accelerations_.clear();

    try {
        std::istringstream ss(data);
        std::string        line;
        // [INTENT] Two-pass parsing: first collect all (flow_rate, PA) pairs per acceleration
        // into a map, then build PCHIP interpolators. The map keying by acceleration naturally
        // groups multi-acceleration calibration data.
        std::map<double, std::vector<std::pair<double, double>>> acc_to_flow_pa;

        while (std::getline(ss, line)) {
            std::istringstream lineStream(line);
            std::string        value;
            double             paValue, flowRate, acceleration;
            paValue = flowRate = acceleration = 0.f; // initialize all to zero.

            // Parse PA value
            if (std::getline(lineStream, value, ',')) {
                paValue = std::stod(value);
            }

            // Parse flow rate value
            if (std::getline(lineStream, value, ',')) {
                flowRate = std::stod(value);
            }

            // Parse acceleration value
            if (std::getline(lineStream, value, ',')) {
                acceleration = std::stod(value);
            }

            // Store the parsed values in a map with acceleration as the key
            acc_to_flow_pa[acceleration].emplace_back(flowRate, paValue);
        }

        // Iterate through the map to set up the interpolators
        for (const auto& kv : acc_to_flow_pa) {
            double      acceleration = kv.first;
            const auto& data         = kv.second;

            std::vector<double> flowRates;
            std::vector<double> paValues;

            for (const auto& pair : data) {
                flowRates.push_back(pair.first);
                paValues.push_back(pair.second);
            }

            // Only set up the interpolator if there are enough data points
            // [HAZARD] PchipInterpolatorHelper requires at least 2 points (minimum for PCHIP).
            // Acceleration groups with only 1 calibration point are silently discarded.
            // If all acceleration groups have only 1 point, the entire model will be empty
            // (accelerations_ remains empty) and operator() will return -1 for all queries.
            if (flowRates.size() > 1) {
                PchipInterpolatorHelper interpolator(flowRates, paValues);
                flow_interpolators_[acceleration] = interpolator;
                accelerations_.push_back(acceleration);
            }
        }
    } catch (const std::exception&) {
        m_isInitialised = false;
        return -1; // Error: Exception during parsing
    }
    m_isInitialised = true;
    return 0; // Success
}

/**
 * @brief Interpolates the PA value for the given flow rate and acceleration.
 * @param flow_rate The flow rate at which to interpolate.
 * @param acceleration The acceleration at which to interpolate.
 * @return The interpolated PA value, or -1 if interpolation fails.
 */
// [INTENT] 2D PA interpolation via "slice-and-interpolate":
// Step 1: For each stored acceleration value, evaluate the flow→PA PCHIP model at `flow_rate`.
//         Collect the resulting (acceleration, PA) pairs.
// Step 2: Build a new PchipInterpolatorHelper from (acceleration[], PA_at_flow[]).
// Step 3: Evaluate the acceleration→PA model at the query `acceleration`.
// Step 4: Round result to 3 decimal places (matches M900/SET_PRESSURE_ADVANCE precision).
//
// Return value: rounded PA ≥ 0 on success, or -1 on failure.
// Callers must check for -1 and apply a fallback (AdaptivePAProcessor uses static PA config).
//
// [HAZARD] If flow_interpolators_ is empty (parseAndSetData() not called, or all groups
// had < 2 points), this function returns -1. The caller handles -1 as a failure.
//
// [HAZARD] If PchipInterpolatorHelper::interpolate() returns -1 for a particular flow_rate
// (out-of-range or insufficient data), that (acceleration, PA) pair is excluded from the
// acceleration interpolation. If ALL per-acceleration interpolations fail, acc_values is empty
// and -1 is returned. Partial failures (some succeed, some don't) produce an acceleration
// interpolation using only the remaining valid points, potentially with reduced accuracy.
//
// [HAZARD] For the special case of a single calibration acceleration value,
// the flow-at-that-acceleration is returned directly (no acceleration interpolation).
// This is correct behavior but the rounding to 3 decimal places is applied here too,
// truncating any sub-0.001 PA precision that the PCHIP might produce.
//
// [HAZARD] std::round(x * 1000.0) / 1000.0 is subject to double-precision floating-point
// representation errors. For example, a PA of 0.025 might be represented as 0.0249999...
// and round to 0.025 correctly, but pathological cases near 0.0005 boundaries may round
// inconsistently. This is negligible for the application domain (PA values typically 0.01–0.2).
double AdaptivePAInterpolator::operator()(double flow_rate, double acceleration)
{
    std::vector<double> pa_values;
    std::vector<double> acc_values;

    // Estimate PA value for every flow to PA model for the given flow rate
    for (const auto& kv : flow_interpolators_) {
        double pa_value = kv.second.interpolate(flow_rate);

        // Check if the interpolated PA value is valid
        if (pa_value != -1) {
            pa_values.push_back(pa_value);
            acc_values.push_back(kv.first);
        }
    }

    // Check if there are enough acceleration values for interpolation
    if (acc_values.size() < 2) {
        // Special case: Only one acceleration value
        if (acc_values.size() == 1) {
            return std::round(pa_values[0] * 1000.0) / 1000.0; // Rounded to 3 decimal places
        }
        return -1; // Error: Not enough data points for interpolation
    }

    // Create a new PchipInterpolatorHelper for PA-acceleration interpolation
    // Use the estimated PA values from the for loop above and their corresponding accelerations to
    // generate the new PCHIP model. Then run this model to interpolate the PA value for the given acceleration value.
    // [HAZARD] A new PchipInterpolatorHelper is constructed on EVERY call to operator().
    // PCHIP initialization cost (computing derivatives for monotone interpolation) is O(N)
    // where N = number of calibration acceleration values. For typical calibration data (2–5
    // acceleration values) this is negligible, but it is wasted work that could be avoided
    // by pre-building a PchipInterpolatorHelper per flow_rate if the API were redesigned.
    PchipInterpolatorHelper pa_accel_interpolator(acc_values, pa_values);
    return std::round(pa_accel_interpolator.interpolate(acceleration) * 1000.0) / 1000.0; // Rounded to 3 decimal places
}
