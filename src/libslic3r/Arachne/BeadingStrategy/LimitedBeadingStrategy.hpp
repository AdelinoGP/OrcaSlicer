// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Outermost decorator in the BeadingStrategy stack. Caps the bead
//          count at max_bead_count and inserts a zero-width "signal" bead at
//          the innermost wall boundary when the cap is reached.
//
// [STATE]  max_bead_count: hard upper limit on printable wall count.
//          parent: the decorated strategy (owned via unique_ptr).
//          Inherits all threshold fields from BeadingStrategy via copy-ctor.
//
// [MEMORY] Owns parent exclusively. Constructed by BeadingStrategyFactory
//          as the final step, taking ownership of the prior stack via std::move.
//
// [HAZARD] The zero-width sentinel bead is inserted at position max_bead_count/2
//          (and its symmetric mirror for even counts). Any code that iterates
//          bead_widths and assumes all widths > 0 will break. The infill and
//          skin alignment code specifically watches for width == 0 to find this
//          boundary. Refactors MUST preserve the sentinel or use an alternative
//          boundary signal.
//
// [HAZARD] Constructor logs a warning if max_bead_count is odd — this strategy
//          was designed for even counts (symmetrical pairs of walls). Odd counts
//          produce an asymmetric sentinel insertion.
//
// [COUPLING] SkeletalTrapezoidation reads the 0-width bead position as the
//            infill/skin boundary. OuterWallInsetBeadingStrategy skips zero-
//            width beads when counting bead_count for inset application.

#ifndef LIMITED_BEADING_STRATEGY_H
#define LIMITED_BEADING_STRATEGY_H

#include <string>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

/*!
 * This is a meta-strategy that can be applied on top of any other beading
 * strategy, which limits the thickness of the walls to the thickness that the
 * lines can reasonably print.
 *
 * The width of the wall is limited to the maximum number of contours times the
 * maximum width of each of these contours.
 *
 * If the width of the wall gets limited, this strategy outputs one additional
 * bead with 0 width. This bead is used to denote the limits of the walled area.
 * Other structures can then use this border to align their structures to, such
 * as to create correctly overlapping infill or skin, or to align the infill
 * pattern to any extra infill walls.
 */
class LimitedBeadingStrategy : public BeadingStrategy
{
public:
    LimitedBeadingStrategy(coord_t max_bead_count, BeadingStrategyPtr parent);

    ~LimitedBeadingStrategy() override = default;

    // [INTENT] If bead_count <= max_bead_count, delegate to parent and insert
    //          the 0-width sentinel only at the cap boundary (even count == max).
    //          If bead_count == max_bead_count + 1, cap at max_bead_count using
    //          the parent's optimal thickness, insert TWO 0-width sentinels
    //          (one per side for symmetry), and store the excess as left_over.
    // [HAZARD] assert(bead_count == max_bead_count + 1) is only checked in
    //          debug builds. In release, the overflow path silently misbehaves
    //          for bead_count > max_bead_count + 1.
    Beading compute(coord_t thickness, coord_t bead_count) const override;

    // [INTENT] Delegates to parent for counts <= max_bead_count.
    //          Returns a sentinel 1000 m value for counts > max (should never
    //          be reached in practice; assert(false) in debug builds).
    coord_t getOptimalThickness(coord_t bead_count) const override;

    // [INTENT] For lower_bead_count < max_bead_count: delegates to parent.
    //          For lower_bead_count == max_bead_count: returns parent's
    //          optimal thickness for max+1 minus 10 µm, creating an artificial
    //          transition point just below the "unlimited" threshold.
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;

    // [INTENT] Clamps parent count to max_bead_count, returning max+1 only
    //          when thickness is at or beyond the parent's max+1 optimal.
    coord_t     getOptimalBeadCount(coord_t thickness) const override;
    std::string toString() const override;

    // [INTENT] Passes through to parent — transition lengths are not affected
    //          by the hard cap.
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;

    // [INTENT] Passes through to parent.
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;

protected:
    // [STATE] Hard upper limit on bead count. Typically set from
    //         WallToolPaths via PerimeterGenerator settings.
    const coord_t max_bead_count;
    // [STATE] Owned parent strategy. Lifetime tied to this decorator.
    const BeadingStrategyPtr parent;
};

} // namespace Slic3r::Arachne
#endif // LIMITED_DISTRIBUTED_BEADING_STRATEGY_H
