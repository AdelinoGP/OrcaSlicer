// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements outer-wall isolation: outer walls are computed at a
//          fixed width (optimal_width_outer), leaving only the inner-wall
//          thickness to vary and be distributed by the parent strategy.
//
// [STATE]  All methods are const. Object is immutable after construction.
//
// [HAZARD] compute() directly computes inner_bead_count = bead_count - 2.
//          If bead_count < 2 this underflows to a large positive number
//          (coord_t is signed but negative inner_bead_count produces negative
//          inner_thickness). The early-return guard at bead_count == 0 catches
//          that, but bead_count == 1 is handled by the outer-wall insertion
//          below (actual_outer_thickness = thickness / 1 = thickness). This
//          is correct but subtle — the `inner_bead_count > 0` guard prevents
//          the parent call for bead_count == 1 or 2.
//
// [HAZARD] toolpath_locations are shifted by +optimal_width_outer for inner
//          beads (line 82) to account for the outer bead offset. This is done
//          after the parent computes from inner_thickness == 0-origin coords.
//          A refactor of the coordinate system must replicate this shift.
//
// [HAZARD] left_over is computed as `thickness - sum(bead_widths)`. With
//          integer arithmetic this is exact only if actual_outer_thickness
//          divides evenly. For bead_count == 1 left_over may be non-zero
//          due to the integer division `thickness / bead_count`.

#include "RedistributeBeadingStrategy.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {

RedistributeBeadingStrategy::RedistributeBeadingStrategy(const coord_t      optimal_width_outer,
                                                         const double       minimum_variable_line_ratio,
                                                         BeadingStrategyPtr parent)
    : BeadingStrategy(*parent)  // [INTENT] Copy base fields (optimal_width, thresholds) from parent
    , parent(std::move(parent)) // [MEMORY] Exclusive ownership transferred
    , optimal_width_outer(optimal_width_outer)
    , minimum_variable_line_ratio(minimum_variable_line_ratio)
{
    name = "RedistributeBeadingStrategy";
}

coord_t RedistributeBeadingStrategy::getOptimalThickness(coord_t bead_count) const
{
    // [INTENT] Total optimal thickness = outer pair (fixed) + inner (from parent).
    //          inner_bead_count = max(0, bead_count - 2) handles 0/1 wall cases.
    const coord_t inner_bead_count = std::max(static_cast<coord_t>(0), bead_count - 2);
    const coord_t outer_bead_count = bead_count - inner_bead_count;
    return parent->getOptimalThickness(inner_bead_count) + optimal_width_outer * outer_bead_count;
}

coord_t RedistributeBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    // [INTENT] Transition thresholds are defined directly in terms of
    //          optimal_width_outer for the first two counts (outer-wall only
    //          regime), then delegate to parent with a -2 offset for 3+.
    // [HAZARD] Case 1: threshold = (1 + split_threshold) * outer_width.
    //          This uses getSplitMiddleThreshold() from the BASE class, which
    //          returns wall_split_middle_threshold. For the outer-wall transition
    //          this is the correct threshold (odd count 1 → 2 is a "split").
    switch (lower_bead_count) {
    case 0: return minimum_variable_line_ratio * optimal_width_outer;
    case 1: return (1.0 + parent->getSplitMiddleThreshold()) * optimal_width_outer;
    default: return parent->getTransitionThickness(lower_bead_count - 2) + 2 * optimal_width_outer;
    }
}

coord_t RedistributeBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    // [INTENT] Below minimum_variable_line_ratio: no walls.
    //          Up to 2 * optimal_width_outer: 1 or 2 outer walls depending on
    //          split threshold. Above: parent count for inner walls + 2 for outer.
    if (thickness < minimum_variable_line_ratio * optimal_width_outer)
        return 0;
    if (thickness <= 2 * optimal_width_outer)
        return thickness > (1.0 + parent->getSplitMiddleThreshold()) * optimal_width_outer ? 2 : 1;
    return parent->getOptimalBeadCount(thickness - 2 * optimal_width_outer) + 2;
}

coord_t RedistributeBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float RedistributeBeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::string RedistributeBeadingStrategy::toString() const { return std::string("RedistributeBeadingStrategy+") + parent->toString(); }

BeadingStrategy::Beading RedistributeBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    Beading ret;

    // Take care of all situations in which no lines are actually produced:
    // [INTENT] Empty beading: thickness too thin for even one wall.
    if (bead_count == 0 || thickness < minimum_variable_line_ratio * optimal_width_outer) {
        ret.left_over       = thickness;
        ret.total_thickness = thickness;
        return ret;
    }

    // Compute the beadings of the inner walls, if any:
    // [INTENT] inner_bead_count = bead_count - 2 (the two outer walls are
    //          handled separately). inner_thickness = total - 2 * outer_width.
    //          Offset toolpath_locations by optimal_width_outer to convert
    //          from inner-relative to total-relative coordinates.
    const coord_t inner_bead_count = bead_count - 2;
    const coord_t inner_thickness  = thickness - 2 * optimal_width_outer;
    if (inner_bead_count > 0 && inner_thickness > 0) {
        ret = parent->compute(inner_thickness, inner_bead_count);
        for (auto& toolpath_location : ret.toolpath_locations)
            toolpath_location += optimal_width_outer;
    }

    // Insert the outer wall(s) around the previously computed inner wall(s), which may be empty:
    // [INTENT] actual_outer_thickness = min(thickness/2, optimal_width_outer)
    //          when bead_count > 2 — prevents outer walls from exceeding half
    //          of total thickness. For bead_count <= 2: thickness / bead_count
    //          (symmetric, divides evenly).
    const coord_t actual_outer_thickness = bead_count > 2 ? std::min(thickness / 2, optimal_width_outer) : thickness / bead_count;
    ret.bead_widths.insert(ret.bead_widths.begin(), actual_outer_thickness);
    ret.toolpath_locations.insert(ret.toolpath_locations.begin(), actual_outer_thickness / 2);
    if (bead_count > 1) {
        ret.bead_widths.push_back(actual_outer_thickness);
        ret.toolpath_locations.push_back(thickness - actual_outer_thickness / 2);
    }

    // Ensure correct total and left over thickness.
    ret.total_thickness = thickness;
    ret.left_over       = thickness - std::accumulate(ret.bead_widths.cbegin(), ret.bead_widths.cend(), static_cast<coord_t>(0));
    return ret;
}

} // namespace Slic3r::Arachne
