// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements the LimitedBeadingStrategy hard-cap logic and the
//          zero-width "sentinel" bead insertion that marks the inner wall
//          boundary for infill/skin alignment.
//
// [STATE]  Stateless beyond the inherited + constructor-set fields.
//          All methods are const.
//
// [HAZARD] compute() at bead_count == max_bead_count + 1:
//          - Uses parent->compute(optimal_thickness, max_bead_count), NOT
//            the requested thickness. The excess is stored in left_over.
//          - Symmetry enforcement loop at lines 75-76 assumes ret is already
//            symmetric after parent->compute(); this holds for Distributed but
//            may NOT hold for custom parent strategies.
//          - Two 0-width sentinels are inserted (one per side). Their positions
//            are computed from the innermost actual wall's location + half-width.
//            Integer division by 2 can be off by ±1 nm.
//
// [HAZARD] getTransitionThickness() for lower_bead_count == max_bead_count
//          returns `parent->getOptimalThickness(max_bead_count + 1) - 10 µm`.
//          This hard-codes a -10 µm offset as a sentinel to ensure the
//          transition fires slightly before the "unlimited" threshold.
//          Any refactor of the threshold system must replicate this offset.
//
// [HAZARD] getOptimalBeadCount() returns max_bead_count + 1 for counts beyond
//          the cap, rather than the uncapped count. Callers must handle the
//          +1 as a signal meaning "capped", not the literal desired count.

#include <boost/log/trivial.hpp>
#include <cassert>
#include <utility>
#include <cstddef>

#include "LimitedBeadingStrategy.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {

std::string LimitedBeadingStrategy::toString() const { return std::string("LimitedBeadingStrategy+") + parent->toString(); }

coord_t LimitedBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float LimitedBeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

LimitedBeadingStrategy::LimitedBeadingStrategy(const coord_t max_bead_count, BeadingStrategyPtr parent)
    : BeadingStrategy(*parent) // [INTENT] Copy base fields from parent for consistency
    , max_bead_count(max_bead_count)
    , parent(std::move(parent)) // [MEMORY] Takes exclusive ownership of the parent chain
{
    if (max_bead_count % 2 == 1) {
        // [HAZARD] Odd max_bead_count produces asymmetric sentinel insertion.
        //          The sentinel is always inserted at max_bead_count/2 (integer
        //          division), which is off-centre for odd counts.
        BOOST_LOG_TRIVIAL(warning) << "LimitedBeadingStrategy with odd bead count is odd indeed!";
    }
}

LimitedBeadingStrategy::Beading LimitedBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    if (bead_count <= max_bead_count) {
        Beading ret = parent->compute(thickness, bead_count);
        bead_count  = ret.toolpath_locations.size();

        if (bead_count % 2 == 0 && bead_count == max_bead_count) {
            // [INTENT] Insert one 0-width sentinel bead at the centre when
            //          exactly at the cap boundary with even bead count.
            //          This marks where infill/skin should align.
            const coord_t innermost_toolpath_location = ret.toolpath_locations[max_bead_count / 2 - 1];
            const coord_t innermost_toolpath_width    = ret.bead_widths[max_bead_count / 2 - 1];
            ret.toolpath_locations.insert(ret.toolpath_locations.begin() + max_bead_count / 2,
                                          innermost_toolpath_location + innermost_toolpath_width / 2);
            ret.bead_widths.insert(ret.bead_widths.begin() + max_bead_count / 2, 0);
        }
        return ret;
    }
    assert(bead_count == max_bead_count + 1);
    if (bead_count != max_bead_count + 1) {
        // [HAZARD] Overflow case: bead_count far exceeds cap. The assert above
        //          catches this in debug only. In release, falls through with
        //          potentially garbled geometry.
        BOOST_LOG_TRIVIAL(warning) << "Too many beads! " << bead_count << " != " << max_bead_count + 1;
    }

    // [INTENT] Bead count is capped. Compute parent layout at the capped
    //          optimal thickness, then store the thickness excess in left_over.
    coord_t optimal_thickness = parent->getOptimalThickness(max_bead_count);
    Beading ret               = parent->compute(optimal_thickness, max_bead_count);
    bead_count                = ret.toolpath_locations.size();
    ret.left_over += thickness - ret.total_thickness;
    ret.total_thickness = thickness;

    // Enforce symmetry
    // [INTENT] Mirror toolpath locations about the centre (thickness/2) to
    //          guarantee symmetric output even when parent produced slight
    //          asymmetry due to integer rounding.
    if (bead_count % 2 == 1) {
        ret.toolpath_locations[bead_count / 2] = thickness / 2;
        ret.bead_widths[bead_count / 2]        = thickness - optimal_thickness;
    }
    for (coord_t bead_idx = 0; bead_idx < (bead_count + 1) / 2; bead_idx++)
        ret.toolpath_locations[bead_count - 1 - bead_idx] = thickness - ret.toolpath_locations[bead_idx];

    // Create a "fake" inner wall with 0 width to indicate the edge of the walled area.
    // This wall can then be used by other structures to e.g. fill the infill area adjacent to the variable-width walls.
    //  [INTENT] Insert sentinel at inner edge of left half, then mirror to right.
    //           Two sentinels bracket the unprinted centre gap.
    //  [HAZARD] `+ innermost_toolpath_width / 2` is integer division — off by
    //           ±1 nm for odd widths. Same issue on the mirrored side.
    coord_t innermost_toolpath_location = ret.toolpath_locations[max_bead_count / 2 - 1];
    coord_t innermost_toolpath_width    = ret.bead_widths[max_bead_count / 2 - 1];
    ret.toolpath_locations.insert(ret.toolpath_locations.begin() + max_bead_count / 2,
                                  innermost_toolpath_location + innermost_toolpath_width / 2);
    ret.bead_widths.insert(ret.bead_widths.begin() + max_bead_count / 2, 0);

    // Symmetry on both sides. Symmetry is guaranteed since this code is stopped early if the bead_count <= max_bead_count, and never
    // reaches this point then.
    const size_t opposite_bead  = bead_count - (max_bead_count / 2 - 1);
    innermost_toolpath_location = ret.toolpath_locations[opposite_bead];
    innermost_toolpath_width    = ret.bead_widths[opposite_bead];
    ret.toolpath_locations.insert(ret.toolpath_locations.begin() + opposite_bead,
                                  innermost_toolpath_location - innermost_toolpath_width / 2);
    ret.bead_widths.insert(ret.bead_widths.begin() + opposite_bead, 0);

    return ret;
}

coord_t LimitedBeadingStrategy::getOptimalThickness(coord_t bead_count) const
{
    if (bead_count <= max_bead_count)
        return parent->getOptimalThickness(bead_count);
    assert(false);
    // [HAZARD] Should never be reached. Returns 1 m as a sentinel to make
    //          any geometry using this value visibly wrong in debug scenarios.
    return scaled<coord_t>(1000.); // 1 meter (Cura was returning 10 meter)
}

coord_t LimitedBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    if (lower_bead_count < max_bead_count)
        return parent->getTransitionThickness(lower_bead_count);

    if (lower_bead_count == max_bead_count)
        // [INTENT] Artificial transition just below the theoretical max+1
        //          optimal to ensure the transition fires at/near the cap.
        // [HAZARD] The -10 µm hardcoded offset is magic. Changing optimal_width
        //          scale doesn't automatically update this offset.
        return parent->getOptimalThickness(lower_bead_count + 1) - scaled<coord_t>(0.01);

    assert(false);
    return scaled<coord_t>(900.); // 0.9 meter;
}

coord_t LimitedBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    coord_t parent_bead_count = parent->getOptimalBeadCount(thickness);
    if (parent_bead_count <= max_bead_count) {
        return parent->getOptimalBeadCount(thickness);
    } else if (parent_bead_count == max_bead_count + 1) {
        // [INTENT] In the transitional zone between max and max+1, use
        //          the -10 µm threshold to decide which side we're on.
        if (thickness < parent->getOptimalThickness(max_bead_count + 1) - scaled<coord_t>(0.01))
            return max_bead_count;
        else
            return max_bead_count + 1;
    }
    // [INTENT] For parent counts well above max+1 (very thick walls), cap
    //          at max+1 as the "capped" signal value.
    else
        return max_bead_count + 1;
}

} // namespace Slic3r::Arachne
