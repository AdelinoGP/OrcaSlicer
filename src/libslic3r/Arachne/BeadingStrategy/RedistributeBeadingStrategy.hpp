// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Decorator that decouples outer wall width from inner wall width
//          changes. Outer walls maintain a constant `optimal_width_outer`
//          while inner walls are handled by the wrapped parent strategy.
//          This reduces surface-quality artifacts caused by inner-wall width
//          variation propagating to the outermost perimeter.
//
// [STATE]  parent: the inner-wall strategy (typically DistributedBeadingStrategy).
//          optimal_width_outer: fixed target width for the two outer walls.
//          minimum_variable_line_ratio: minimum fraction of optimal_width_outer
//            below which the model is too thin to print any wall at all.
//
// [MEMORY] Owns parent via unique_ptr. Takes ownership in ctor via std::move.
//
// [COUPLING] Sits between DistributedBeadingStrategy (inner) and
//            WideningBeadingStrategy/OuterWallInsetBeadingStrategy (outer) in
//            the decorator stack from BeadingStrategyFactory.
//
// [HAZARD] For bead_count <= 2 the strategy degenerates to purely symmetric
//          outer walls and ignores the parent strategy entirely. Inner walls
//          are only computed when bead_count > 2 AND inner_thickness > 0.
//          A refactor must replicate this degenerate case or the transition
//          from 1/2 walls to 3+ walls will produce a discontinuity.
//
// [HAZARD] actual_outer_thickness = min(thickness/2, optimal_width_outer) for
//          bead_count > 2. This means outer walls can shrink if the total
//          thickness is less than 2*optimal_width_outer, but cannot grow.
//          Asymmetry with the inner-wall behaviour (which can grow).
//
// [HAZARD] getTransitionThickness() has special cases for counts 0 and 1 that
//          use `optimal_width_outer` directly, bypassing the parent threshold
//          system entirely. This means the outer-wall transition behaviour is
//          not parametrised by the inner-wall split/add thresholds.

#ifndef REDISTRIBUTE_DISTRIBUTED_BEADING_STRATEGY_H
#define REDISTRIBUTE_DISTRIBUTED_BEADING_STRATEGY_H

#include <string>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {
/*!
 * A meta-beading-strategy that takes outer and inner wall widths into account.
 *
 * The outer wall will try to keep a constant width by only applying the beading strategy on the inner walls. This
 * ensures that this outer wall doesn't react to changes happening to inner walls. It will limit print artifacts on
 * the surface of the print. Although this strategy technically deviates from the original philosophy of the paper.
 * It will generally results in better prints because of a smoother motion and less variation in extrusion width in
 * the outer walls.
 *
 * If the thickness of the model is less then two times the optimal outer wall width and once the minimum inner wall
 * width it will keep the minimum inner wall at a minimum constant and vary the outer wall widths symmetrical. Until
 * The thickness of the model is that of at least twice the optimal outer wall width it will then use two
 * symmetrical outer walls only. Until it transitions into a single outer wall. These last scenario's are always
 * symmetrical in nature, disregarding the user specified strategy.
 */
class RedistributeBeadingStrategy : public BeadingStrategy
{
public:
    /*!
     * /param optimal_width_outer         Outer wall width, guaranteed to be the actual (save rounding errors) at a
     *                                    bead count if the parent strategies' optimum bead width is a weighted
     *                                    average of the outer and inner walls at that bead count.
     * /param minimum_variable_line_ratio Minimum factor that the variable line might deviate from the optimal width.
     */
    RedistributeBeadingStrategy(coord_t optimal_width_outer, double minimum_variable_line_ratio, BeadingStrategyPtr parent);

    ~RedistributeBeadingStrategy() override = default;

    // [INTENT] Compute outer walls first at fixed width, then delegate
    //          inner-wall computation to parent with the remaining thickness.
    //          Returns immediately (empty beading) if below min ratio.
    Beading compute(coord_t thickness, coord_t bead_count) const override;

    // [INTENT] Returns outer_bead_count * optimal_width_outer +
    //          parent->getOptimalThickness(inner_bead_count).
    //          inner_bead_count = max(0, bead_count - 2).
    coord_t getOptimalThickness(coord_t bead_count) const override;

    // [INTENT] Special-cases counts 0 and 1 against optimal_width_outer;
    //          for higher counts delegates to parent with offset -2.
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;

    // [INTENT] Returns 0 below min ratio, 1 or 2 for thin walls,
    //          or parent count + 2 for thicker walls.
    coord_t getOptimalBeadCount(coord_t thickness) const override;

    // [INTENT] Passes through to parent unchanged.
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;

    // [INTENT] Passes through to parent unchanged.
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;

    std::string toString() const override;

protected:
    // [STATE] Inner-wall strategy. Receives (thickness - 2*optimal_width_outer)
    //         as its input thickness when bead_count > 2.
    BeadingStrategyPtr parent;
    // [STATE] Fixed width for the two outermost walls.
    coord_t optimal_width_outer;
    // [STATE] Minimum ratio of optimal_width_outer; below this the model
    //         is too thin to print anything.
    double minimum_variable_line_ratio;
};

} // namespace Slic3r::Arachne
#endif // INWARD_DISTRIBUTED_BEADING_STRATEGY_H
