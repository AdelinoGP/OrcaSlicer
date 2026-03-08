// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Concrete beading strategy: distributes available wall thickness
//          across beads using a Gaussian-like weight centred at the middle
//          bead. Beads nearer the centre absorb more of the thickness surplus
//          or deficit than those near the outer walls.
//
// [STATE]  one_over_distribution_radius_squared = 1/(r-1)² where r is the
//          distribution_radius (in bead-count units). This is the only field
//          beyond those inherited from BeadingStrategy.
//
// [HAZARD] When distribution_radius == 1, the formula degenerates to 1/0²
//          but the ctor guards with `if (distribution_radius >= 2)` and falls
//          back to 1/1² = 1.0. Callers that pass r==1 get uniform distribution.
//
// [COUPLING] Used as the innermost strategy in the decorator stack built by
//            BeadingStrategyFactory::makeStrategy(). RedistributeBeadingStrategy
//            wraps this and delegates inner-wall computation to it.

#ifndef DISTRIBUTED_BEADING_STRATEGY_H
#define DISTRIBUTED_BEADING_STRATEGY_H

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

/*!
 * This beading strategy chooses a wall count that would make the line width
 * deviate the least from the optimal line width, and then distributes the lines
 * evenly among the thickness available.
 */
class DistributedBeadingStrategy : public BeadingStrategy
{
protected:
    // [STATE] Precomputed reciprocal squared radius used in the weight
    //         function: w(i) = max(0, 1 - one_over_distribution_radius_squared
    //         * (i - middle)²). Gaussian-like falloff, 0 at radius boundary.
    float one_over_distribution_radius_squared; // (1 / distribution_radius)^2

public:
    /*!
     * \param distribution_radius the radius (in number of beads) over which to distribute the discrepancy between the feature size and the
     * optimal thickness
     */
    DistributedBeadingStrategy(coord_t optimal_width,
                               coord_t default_transition_length,
                               double  transitioning_angle,
                               double  wall_split_middle_threshold,
                               double  wall_add_middle_threshold,
                               int     distribution_radius);

    ~DistributedBeadingStrategy() override = default;

    // [INTENT] Distribute surplus/deficit thickness among beads weighted by
    //          distance from the centre bead. The outermost bead absorbs any
    //          remaining integer rounding to guarantee width sum == thickness.
    Beading compute(coord_t thickness, coord_t bead_count) const override;

    // [INTENT] Naive floor division (thickness / optimal_width) plus one if
    //          the remainder exceeds the split/add threshold.
    coord_t getOptimalBeadCount(coord_t thickness) const override;
};

} // namespace Slic3r::Arachne
#endif // DISTRIBUTED_BEADING_STRATEGY_H
