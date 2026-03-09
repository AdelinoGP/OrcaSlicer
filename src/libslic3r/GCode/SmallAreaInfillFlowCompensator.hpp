// [INTENT] SmallAreaInfillFlowCompensator adjusts the extrusion amount (dE) for short
//          infill lines to compensate for over-extrusion in small/narrow areas.
//          The compensation is based on a user-configured piecewise cubic Hermite
//          interpolation (PCHIP) model: pairs of (extrusion_length, flow_factor) from
//          the config define the compensation curve.
//
// Based on original work by Alexander Þór licensed under the GPLv3:
// https://github.com/Alexander-T-Moss/Small-Area-Flow-Comp
//
// [COUPLING] Depends on:
//   - GCodeConfig::small_area_infill_flow_compensation_model — std::vector<std::string>
//     where each entry is a "length,factor" CSV pair.
//   - PchipInterpolatorHelper — wraps a PCHIP spline over (eLengths, flowComps) vectors.
//   - ExtrusionRole enum — only erSolidInfill, erTopSolidInfill, erBottomSurface are modified.
//
// [STATE] eLengths and flowComps are populated once during construction and are immutable
//         thereafter.  flowModel is a unique_ptr; the destructor is explicitly defaulted
//         to allow forward-declaration of PchipInterpolatorHelper in the header.

#ifndef slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_
#define slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"
#include "PchipInterpolatorHelper.hpp"
#include <memory>

namespace Slic3r {

// [INTENT] Applies per-line flow compensation to short infill extrusions.
// Constructed once per print from the GCodeConfig; immutable after construction.
//
// [COUPLING] Only modifies dE for ExtrusionRole::erSolidInfill, erTopSolidInfill,
//            erBottomSurface.  All other roles are passed through unchanged.
//
// [HAZARD H856] max_modified_length() calls eLengths.back() on a potentially empty vector.
//               If the config model has zero valid data points, eLengths is empty and
//               .back() is undefined behaviour.  The constructor throws if the model
//               is malformed, but if it is empty (zero entries) the throw is never reached
//               and flowModel will be constructed from empty vectors, which may cause
//               PchipInterpolatorHelper to behave incorrectly.  flow_comp_model guards
//               against nullptr flowModel but not against an empty-model spline returning
//               NaN/Inf on the first call.
class SmallAreaInfillFlowCompensator
{
public:
    SmallAreaInfillFlowCompensator() = delete;
    // [INTENT] Parses the compensation model from config.small_area_infill_flow_compensation_model.
    //          Validates: first length == 0, lengths strictly increasing, flow factors strictly
    //          increasing, final factor == 1.0.  Throws Slic3r::InvalidArgument on violation.
    explicit SmallAreaInfillFlowCompensator(const Slic3r::GCodeConfig& config);
    // [INTENT] Explicitly defaulted destructor to allow forward-declaration of
    //          PchipInterpolatorHelper (unique_ptr destructor requires complete type).
    ~SmallAreaInfillFlowCompensator();

    // [INTENT] Returns dE * flow_comp_model(line_length) for qualifying roles,
    //          or dE unchanged for all other roles or if flowModel is null.
    double modify_flow(const double line_length, const double dE, const ExtrusionRole role);

private:
    // Model knot points: extrusion lengths (mm) and corresponding flow compensation factors.
    // [STATE] Populated once in constructor, read-only thereafter.
    std::vector<double> eLengths;
    std::vector<double> flowComps;

    // PCHIP interpolator over (eLengths, flowComps).
    // [MEMORY] Owned via unique_ptr; null if construction fails (though constructor throws).
    std::unique_ptr<PchipInterpolatorHelper> flowModel;

    // [INTENT] Returns interpolated flow factor for the given line length.
    //          Returns 1.0 (no compensation) if flowModel is null or line_length == 0
    //          or line_length > max_modified_length().
    double flow_comp_model(const double line_length);

    // [HAZARD H856] Calls eLengths.back() — UB if eLengths is empty.
    double max_modified_length() { return eLengths.back(); }
};

} // namespace Slic3r

#endif /* slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_ */
