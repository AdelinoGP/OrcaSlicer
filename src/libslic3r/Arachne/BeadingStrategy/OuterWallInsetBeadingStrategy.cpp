// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements the single-axis outer-wall inset: delegates all bead
//          computation to parent, then moves toolpath_locations[0] inward.
//
// [STATE]  All methods are const. Object is immutable after construction.
//
// [HAZARD] BUG: name = "OuterWallOfsetBeadingStrategy" — missing 'f' in
//          "Offset". Upstream typo in ctor and toString(). Do not rename.
//
// [HAZARD] compute() counts non-zero-width beads as the effective bead_count.
//          This correctly skips LimitedBeadingStrategy's 0-width sentinels.
//          HOWEVER: this strategy sits BELOW LimitedBeadingStrategy in the
//          decorator stack (factory builds Limited last). So at the time
//          this compute() runs, sentinels are NOT yet present. The zero-width
//          filter is therefore defensive code that only matters if the stack
//          order is changed. Document this ordering dependency.
//
// [HAZARD] Only toolpath_locations[0] is moved (the single outer wall on one
//          side). The opposite outer wall (toolpath_locations[bead_count-1])
//          is NOT adjusted — creating an intentional asymmetry. This is
//          correct because the inset simulates a surface-quality adjustment
//          for the outermost printed wall only; the innermost printed wall
//          position follows from the beading strategy independently.
//
// [HAZARD] Clamp: `std::min(..., thickness / 2)`. Integer division truncates
//          for odd thicknesses — clamping may place the outer wall 1 nm
//          inside the true midpoint.

#include "OuterWallInsetBeadingStrategy.hpp"

#include <algorithm>
#include <utility>

#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {
OuterWallInsetBeadingStrategy::OuterWallInsetBeadingStrategy(coord_t outer_wall_offset, BeadingStrategyPtr parent)
    : BeadingStrategy(*parent)  // [INTENT] Copy base fields from parent
    , parent(std::move(parent)) // [MEMORY] Exclusive ownership transferred
    , outer_wall_offset(outer_wall_offset)
{
    // [HAZARD] Intentional typo: "OuterWallOfsetBeadingStrategy" (missing 'f').
    //          Pre-existing upstream bug — do not fix.
    name = "OuterWallOfsetBeadingStrategy";
}

coord_t OuterWallInsetBeadingStrategy::getOptimalThickness(coord_t bead_count) const { return parent->getOptimalThickness(bead_count); }

coord_t OuterWallInsetBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    return parent->getTransitionThickness(lower_bead_count);
}

coord_t OuterWallInsetBeadingStrategy::getOptimalBeadCount(coord_t thickness) const { return parent->getOptimalBeadCount(thickness); }

coord_t OuterWallInsetBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

std::string OuterWallInsetBeadingStrategy::toString() const
{
    // [HAZARD] Intentional typo carried from ctor.
    return std::string("OuterWallOfsetBeadingStrategy+") + parent->toString();
}

BeadingStrategy::Beading OuterWallInsetBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    Beading ret = parent->compute(thickness, bead_count);

    // Actual count and thickness as represented by extant walls. Don't count any potential zero-width 'signaling' walls.
    // [INTENT] Recount after parent compute to exclude any 0-width sentinel
    //          beads that may have been inserted by LimitedBeadingStrategy.
    //          In the current stack order this won't fire (Limited is outermost),
    //          but guards against future stack reordering.
    bead_count = std::count_if(ret.bead_widths.begin(), ret.bead_widths.end(), [](const coord_t width) { return width > 0; });

    // No need to apply any inset if there is just a single wall.
    if (bead_count < 2) {
        return ret;
    }

    // Actually move the outer wall inside. Ensure that the outer wall never goes beyond the middle line.
    // [INTENT] Shift toolpath_locations[0] inward by outer_wall_offset.
    //          Clamp to thickness/2 to prevent crossing the model centre.
    // [HAZARD] Only [0] is moved. The opposite wall [bead_count-1] is NOT
    //          mirrored — by design (see file-level [HAZARD]).
    ret.toolpath_locations[0] = std::min(ret.toolpath_locations[0] + outer_wall_offset, thickness / 2);
    return ret;
}

} // namespace Slic3r::Arachne
