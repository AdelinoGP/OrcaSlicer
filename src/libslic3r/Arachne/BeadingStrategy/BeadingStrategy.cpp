// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Default implementations for BeadingStrategy virtual methods.
//          Concrete strategies (Distributed, Redistribute, etc.) override
//          only the methods they need to specialise; the rest fall through here.
//
// [STATE]  This translation unit is stateless — all state lives in the
//          BeadingStrategy base-class fields (optimal_width, thresholds, etc.).
//
// [HAZARD] getTransitioningLength() returns scaled<coord_t>(0.01) — i.e. 10 µm
//          — for lower_bead_count==0. This prevents a zero-length transition
//          (division-by-zero downstream in SkeletalTrapezoidation). Any
//          refactor must preserve this guard or replace with an explicit check.
//
// [HAZARD] getTransitionAnchorPos() computes a floating-point division using
//          integer coord_t arithmetic cast to float. If upper_optimum ==
//          lower_optimum the denominator is 0 (degenerate geometry). This
//          can only happen when optimal_width == 0, which is itself a bug.

#include "BeadingStrategy.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r::Arachne {

BeadingStrategy::BeadingStrategy(coord_t optimal_width,
                                 double  wall_split_middle_threshold,
                                 double  wall_add_middle_threshold,
                                 coord_t default_transition_length,
                                 float   transitioning_angle)
    : optimal_width(optimal_width)
    , wall_split_middle_threshold(wall_split_middle_threshold)
    , wall_add_middle_threshold(wall_add_middle_threshold)
    , default_transition_length(default_transition_length)
    , transitioning_angle(transitioning_angle)
{
    name = "Unknown";
}

BeadingStrategy::BeadingStrategy(const BeadingStrategy& other)
    : optimal_width(other.optimal_width)
    , wall_split_middle_threshold(other.wall_split_middle_threshold)
    , wall_add_middle_threshold(other.wall_add_middle_threshold)
    , default_transition_length(other.default_transition_length)
    , transitioning_angle(other.transitioning_angle)
    , name(other.name)
{}

coord_t BeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    // [INTENT] Return a very short length (10 µm) for the 0→1 transition to
    //          avoid division-by-zero in SkeletalTrapezoidation transition
    //          processing. All other counts use the configured default.
    if (lower_bead_count == 0)
        return scaled<coord_t>(0.01);
    return default_transition_length;
}

float BeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    // [INTENT] Place the transition anchor so it sits proportionally between
    //          the lower and upper optimal thicknesses, weighted by where the
    //          transition threshold falls. Result in [0,1]: 0 = lower end,
    //          1 = upper end of the transition ramp.
    // [HAZARD] If upper_optimum == lower_optimum (degenerate: optimal_width==0),
    //          the division produces NaN/inf. Guarded only implicitly by the
    //          assumption that optimal_width > 0.
    coord_t lower_optimum    = getOptimalThickness(lower_bead_count);
    coord_t transition_point = getTransitionThickness(lower_bead_count);
    coord_t upper_optimum    = getOptimalThickness(lower_bead_count + 1);
    return 1.0 - float(transition_point - lower_optimum) / float(upper_optimum - lower_optimum);
}

// [INTENT] Default: returns empty — no kink points in the base strategy.
//          WideningBeadingStrategy overrides to prepend min_output_width.
std::vector<coord_t> BeadingStrategy::getNonlinearThicknesses(coord_t lower_bead_count) const { return {}; }

// [INTENT] Returns the strategy name for debug logging.
//          Decorator strategies prepend their own name (see LimitedBeadingStrategy::toString).
std::string BeadingStrategy::toString() const { return name; }

double BeadingStrategy::getSplitMiddleThreshold() const { return wall_split_middle_threshold; }

double BeadingStrategy::getTransitioningAngle() const { return transitioning_angle; }

// [INTENT] Trivial default: all beads have optimal_width, total = count * width.
//          Decorators (Redistribute, Limited) override for outer/inner asymmetry.
coord_t BeadingStrategy::getOptimalThickness(coord_t bead_count) const { return optimal_width * bead_count; }

coord_t BeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    // [INTENT] Interpolate the transition thickness between lower and upper
    //          optimal thicknesses using the appropriate threshold fraction.
    //          - Odd lower_bead_count → wall_split_middle_threshold
    //          - Even lower_bead_count → wall_add_middle_threshold
    // [STATE]  Threshold in (0,1) creates hysteresis to prevent bead-count
    //          oscillation near boundary thicknesses.
    const coord_t lower_ideal_width  = getOptimalThickness(lower_bead_count);
    const coord_t higher_ideal_width = getOptimalThickness(lower_bead_count + 1);
    const double  threshold          = lower_bead_count % 2 == 1 ? wall_split_middle_threshold : wall_add_middle_threshold;
    return lower_ideal_width + threshold * (higher_ideal_width - lower_ideal_width);
}

} // namespace Slic3r::Arachne
