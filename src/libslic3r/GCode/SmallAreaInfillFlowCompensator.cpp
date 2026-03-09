// Modify the flow of extrusion lines inversely proportional to the length of
// the extrusion line. When infill lines get shorter the flow rate will auto-
// matically be reduced to mitigate the effect of small infill areas being
// over-extruded.
//
// Based on original work by Alexander Þór licensed under the GPLv3:
// https://github.com/Alexander-T-Moss/Small-Area-Flow-Comp
//
// [INTENT] This file implements the constructor (config parsing + validation) and the
//          two public methods of SmallAreaInfillFlowCompensator.
//
// [STATE] The compensation model is stateless after construction: eLengths, flowComps,
//         and flowModel are populated once from config and never mutated.
//
// [COUPLING] Uses:
//   - std::regex for whitespace trimming — O(N) regex JIT per parsed line (performance note)
//   - std::stod for CSV value parsing — locale-dependent decimal separator
//   - PchipInterpolatorHelper for monotone cubic interpolation
//   - boost::log::trivial for error logging before re-throwing

#include <math.h>
#include <cstring>
#include <cfloat>
#include <regex>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include "SmallAreaInfillFlowCompensator.hpp"
#include <boost/log/trivial.hpp>

namespace Slic3r {

// [INTENT] Floating-point equality helper: returns true if a and b are adjacent
//          representable doubles (within 1 ULP).  Used to check if a value is "exactly"
//          0.0 or 1.0 without exact equality comparison.
// [HAZARD H857] `nearly_equal` is defined at file scope (not in an anonymous namespace
//               or as a static function), so it pollutes the Slic3r namespace with a
//               generic name that could silently shadow or conflict with other
//               `Slic3r::nearly_equal` overloads added in future TUs.
bool nearly_equal(double a, double b)
{
    return std::nextafter(a, std::numeric_limits<double>::lowest()) <= b && std::nextafter(a, std::numeric_limits<double>::max()) >= b;
}

// [INTENT] Parses the user-configured CSV model from GCodeConfig and builds the PCHIP spline.
// [STATE] Populates eLengths, flowComps, then validates them, then constructs flowModel.
// [HAZARD H858] CSV parsing uses std::regex for whitespace trimming and std::stod for number
//               conversion.  std::regex is compiled at runtime per call — for N data points,
//               N regex compilations occur.  std::stod is locale-dependent: on a system with
//               a comma-decimal locale, "3.14" will fail to parse.  No locale guard is set.
// [HAZARD H859] The inner try/catch block catches ALL exceptions (...) from std::stod and
//               rethrows as Slic3r::InvalidArgument.  This suppresses std::bad_alloc and
//               other non-parsing exceptions, masking OOM conditions as user-input errors.
// [HAZARD H860] If the config model has zero valid data points (all lines are empty or
//               malformed), both eLengths and flowComps remain empty.  The validation loop
//               for eLengths is skipped (size == 0), and the `!flowComps.empty()` guard
//               before the final-factor check is also false.  flowModel is then constructed
//               from two empty vectors.  Depending on PchipInterpolatorHelper's behaviour
//               with zero knots, this may produce NaN/Inf from interpolate() calls.
SmallAreaInfillFlowCompensator::SmallAreaInfillFlowCompensator(const Slic3r::GCodeConfig& config)
{
    try {
        // Parse each "length,factor" CSV line from the config model.
        for (auto& line : config.small_area_infill_flow_compensation_model.values) {
            std::istringstream iss(line);
            std::string        value_str;
            double             eLength = 0.0;

            if (std::getline(iss, value_str, ',')) {
                try {
                    // Trim leading and trailing whitespace
                    // [HAZARD H858] Regex compiled on every iteration.
                    value_str = std::regex_replace(value_str, std::regex("^\\s+|\\s+$"), "");
                    if (value_str.empty()) {
                        continue;
                    }
                    eLength = std::stod(value_str); // [HAZARD H858] locale-dependent
                    if (std::getline(iss, value_str, ',')) {
                        eLengths.push_back(eLength);
                        flowComps.push_back(std::stod(value_str)); // [HAZARD H858] locale-dependent
                    }
                } catch (...) {
                    // [HAZARD H859] Catches std::bad_alloc and other non-parse exceptions too.
                    std::stringstream ss;
                    ss << "Small Area Flow Compensation: Error parsing data point in small area infill compensation model:" << line
                       << std::endl;

                    throw Slic3r::InvalidArgument(ss.str());
                }
            }
        }

        // Validate that the first extrusion length is exactly 0.
        for (size_t i = 0; i < eLengths.size(); i++) {
            if (i == 0) {
                if (!nearly_equal(eLengths[i], 0.0)) {
                    throw Slic3r::InvalidArgument(
                        "Small Area Flow Compensation: First extrusion length for small area infill compensation model must be 0");
                }
            } else {
                if (nearly_equal(eLengths[i], 0.0)) {
                    throw Slic3r::InvalidArgument(
                        "Small Area Flow Compensation: Only the first extrusion length for small area infill compensation model can be 0");
                }
                if (eLengths[i] <= eLengths[i - 1]) {
                    throw Slic3r::InvalidArgument(
                        "Small Area Flow Compensation: Extrusion lengths for subsequent points must be increasing");
                }
            }
        }

        // Validate that flow compensation factors are strictly increasing with length.
        for (size_t i = 1; i < flowComps.size(); ++i) {
            if (flowComps[i] <= flowComps[i - 1]) {
                throw Slic3r::InvalidArgument(
                    "Small Area Flow Compensation: Flow compensation factors must strictly increase with extrusion length");
            }
        }

        // Validate that the final compensation factor is 1.0 (no compensation at full length).
        // [HAZARD H860] Skipped if flowComps is empty — no error for zero-knot model.
        if (!flowComps.empty() && !nearly_equal(flowComps.back(), 1.0)) {
            throw Slic3r::InvalidArgument(
                "Small Area Flow Compensation: Final compensation factor for small area infill flow compensation model must be 1.0");
        }

        // Build the PCHIP spline over the validated knot points.
        flowModel = std::make_unique<PchipInterpolatorHelper>(eLengths, flowComps);

    } catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error parsing small area infill compensation model: " << e.what();
        throw;
    }
}

// [INTENT] Defaulted destructor required here (not in the header) to allow the header to
//          forward-declare PchipInterpolatorHelper; the destructor of unique_ptr<PchipInterpolatorHelper>
//          requires PchipInterpolatorHelper to be a complete type at the point of instantiation.
SmallAreaInfillFlowCompensator::~SmallAreaInfillFlowCompensator() = default;

// [INTENT] Returns the interpolated flow compensation factor for a given line length.
//          Returns 1.0 (no modification) for:
//            - null flowModel
//            - line_length == 0 (no extrusion)
//            - line_length > max_modified_length() (outside the model range — use full flow)
// [HAZARD H856] max_modified_length() calls eLengths.back() — UB if eLengths is empty.
//               The `flowModel == nullptr` guard above does NOT protect against an
//               empty-but-non-null flowModel spline.
double SmallAreaInfillFlowCompensator::flow_comp_model(const double line_length)
{
    if (flowModel == nullptr)
        return 1.0;

    if (line_length == 0 || line_length > max_modified_length()) {
        return 1.0;
    }

    return flowModel->interpolate(line_length);
}

// [INTENT] Public entry point called once per extrusion segment during G-code generation.
//          Returns dE * flow_comp_model(line_length) for the qualifying infill roles,
//          otherwise returns dE unchanged.
// [COUPLING] Only modifies: erSolidInfill, erTopSolidInfill, erBottomSurface.
//            Perimeter, support, wipe, travel and all other roles are NOT modified.
// [HAZARD H861] The role filter does not include erInternalInfill (sparse infill) or
//               erBridgeInfill (bridge infill).  Short sparse-infill lines and bridge
//               lines are NOT compensated, even though they may also suffer from
//               over-extrusion in small areas.  This is a design choice but is not
//               documented in the config or UI.
double SmallAreaInfillFlowCompensator::modify_flow(const double line_length, const double dE, const ExtrusionRole role)
{
    if (flowModel &&
        (role == ExtrusionRole::erSolidInfill || role == ExtrusionRole::erTopSolidInfill || role == ExtrusionRole::erBottomSurface)) {
        return dE * flow_comp_model(line_length);
    }

    return dE;
}

} // namespace Slic3r
