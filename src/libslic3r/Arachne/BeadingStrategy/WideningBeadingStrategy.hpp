// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Optional decorator that enables printing of very thin features
//          (thinner than one optimal-width bead) by artificially widening them
//          to at least `min_output_width`. Without this, features below
//          `min_input_width` would produce zero beads and be skipped entirely.
//
// [STATE]  parent: the next strategy in the stack (owned via unique_ptr).
//          min_input_width: minimum thickness to even attempt printing.
//          min_output_width: minimum width of the single bead produced for
//            features in [min_input_width, optimal_width).
//
// [MEMORY] Owns parent via unique_ptr. Takes ownership in ctor via std::move.
//
// [COUPLING] Only applied when `print_thin_walls` is true in
//            BeadingStrategyFactory::makeStrategy(). Sits between
//            RedistributeBeadingStrategy (inner) and
//            OuterWallInsetBeadingStrategy/LimitedBeadingStrategy (outer).
//
// [HAZARD] compute() intercepts ONLY the case where thickness < optimal_width.
//          For thickness >= optimal_width it delegates to parent unchanged.
//          This means the "widening" logic fires at a different threshold
//          than getTransitionThickness(0) == min_input_width. If
//          optimal_width > min_input_width (typical), there is a gap between
//          min_input_width and optimal_width where the widening fires but
//          compute() produces a single bead at max(thickness, min_output_width).
//
// [HAZARD] getNonlinearThicknesses() ALWAYS prepends min_output_width to the
//          parent's list, regardless of whether print_thin_walls semantics
//          apply. This drives SkeletalTrapezoidation::generateExtraRibs() to
//          insert a support node at the min_output_width thickness. Any
//          refactor removing WideningBeadingStrategy must either also remove
//          the extra rib or find another way to insert that support node.

#ifndef WIDENING_BEADING_STRATEGY_H
#define WIDENING_BEADING_STRATEGY_H

#include <string>
#include <vector>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

/*!
 * This is a meta-strategy that can be applied on any other beading strategy. If
 * the part is thinner than a single line, this strategy adjusts the part so
 * that it becomes the minimum thickness of one line.
 *
 * This way, tiny pieces that are smaller than a single line will still be
 * printed.
 */
class WideningBeadingStrategy : public BeadingStrategy
{
public:
    /*!
     * Takes responsibility for deleting \param parent
     */
    WideningBeadingStrategy(BeadingStrategyPtr parent, coord_t min_input_width, coord_t min_output_width);

    ~WideningBeadingStrategy() override = default;

    // [INTENT] If thickness < optimal_width AND >= min_input_width: produce
    //          one bead of width max(thickness, min_output_width), centred.
    //          If thickness < min_input_width: return empty beading (skipped).
    //          Otherwise: delegate to parent.
    Beading compute(coord_t thickness, coord_t bead_count) const override;

    // [INTENT] Delegates to parent — widening doesn't change optimal thickness.
    coord_t getOptimalThickness(coord_t bead_count) const override;

    // [INTENT] For lower_bead_count == 0: returns min_input_width (the
    //          threshold where the single-bead widening regime begins).
    //          For higher counts: delegates to parent.
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;

    // [INTENT] If thickness >= min_input_width and parent returns 0: returns 1
    //          (forces at least one wall). Otherwise delegates to parent.
    coord_t getOptimalBeadCount(coord_t thickness) const override;

    // [INTENT] Delegates to parent.
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;

    // [INTENT] Delegates to parent.
    float getTransitionAnchorPos(coord_t lower_bead_count) const override;

    // [INTENT] ALWAYS prepends min_output_width to parent's list.
    //          Drives SkeletalTrapezoidation to insert an extra rib node at
    //          this thickness, preventing linear interpolation of bead widths
    //          in long thin features.
    std::vector<coord_t> getNonlinearThicknesses(coord_t lower_bead_count) const override;

    std::string toString() const override;

protected:
    // [STATE] Owned inner strategy.
    BeadingStrategyPtr parent;
    // [STATE] Minimum thickness to attempt printing (below this: zero beads).
    const coord_t min_input_width;
    // [STATE] Minimum width of the single bead produced for thin features.
    //         May be larger than actual thickness (over-extrusion for adhesion).
    const coord_t min_output_width;
};

} // namespace Slic3r::Arachne
#endif // WIDENING_BEADING_STRATEGY_H
