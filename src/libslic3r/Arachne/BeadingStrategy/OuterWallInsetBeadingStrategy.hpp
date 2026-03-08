// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Optional decorator that shifts the outermost wall's toolpath
//          inward by `outer_wall_offset` nm. Used to fine-tune where the
//          outer wall sits relative to the model outline, e.g. to compensate
//          for nozzle diameter or to achieve a tighter surface tolerance.
//          Orca Slicer extends this to support negative offsets (outward).
//
// [STATE]  parent: the decorated strategy (owned via unique_ptr).
//          outer_wall_offset: inward shift distance (positive = inward,
//            negative = outward per Orca extension).
//
// [MEMORY] Owns parent via unique_ptr. Takes ownership in ctor via std::move.
//
// [COUPLING] Applied between WideningBeadingStrategy (or RedistributeBeadingStrategy)
//            and LimitedBeadingStrategy in the factory stack. LimitedBeadingStrategy
//            is applied AFTER this, so zero-width sentinel beads are NOT yet
//            present in parent->compute() output — the sentinel-skip logic in
//            compute() guards against zero-width beads explicitly.
//
// [HAZARD] BUG: name field is "OuterWallOfsetBeadingStrategy" (missing 'f' in
//          "Offset"). Both ctor and toString() use the typo. Pre-existing upstream
//          bug — do NOT fix during annotation to avoid breaking any string-based
//          logic that may depend on this name for serialisation/logging.
//
// [HAZARD] compute() shifts only toolpath_locations[0] (the outer wall) by
//          outer_wall_offset. It clamps to thickness/2 (never past centre).
//          The INNER wall at [bead_count-1] is NOT mirrored — asymmetric inset.
//          This is intentional (only the outer wall needs to move), but means
//          the two outermost walls are no longer symmetric after this decorator.

#ifndef OUTER_WALL_INSET_BEADING_STRATEGY_H
#define OUTER_WALL_INSET_BEADING_STRATEGY_H

#include <string>

#include "BeadingStrategy.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {
/*
 * This is a meta strategy that allows for the outer wall to be inset towards the inside of the model.
 */
class OuterWallInsetBeadingStrategy : public BeadingStrategy
{
public:
    OuterWallInsetBeadingStrategy(coord_t outer_wall_offset, BeadingStrategyPtr parent);

    ~OuterWallInsetBeadingStrategy() override = default;

    // [INTENT] Delegates to parent, then shifts toolpath_locations[0]
    //          inward by outer_wall_offset (clamped to thickness/2).
    //          Zero-width beads (LimitedBeadingStrategy sentinels) are
    //          excluded from bead_count before the single-wall guard.
    Beading compute(coord_t thickness, coord_t bead_count) const override;

    // [INTENT] All pass-throughs to parent — the inset only affects
    //          computed toolpath positions, not the width/count logic.
    coord_t getOptimalThickness(coord_t bead_count) const override;
    coord_t getTransitionThickness(coord_t lower_bead_count) const override;
    coord_t getOptimalBeadCount(coord_t thickness) const override;
    coord_t getTransitioningLength(coord_t lower_bead_count) const override;

    // [HAZARD] Returns "OuterWallOfsetBeadingStrategy+..." — note the typo
    //          (missing 'f'). Pre-existing upstream bug. Do not rename.
    std::string toString() const override;

private:
    // [STATE] Owned parent strategy.
    BeadingStrategyPtr parent;
    // [STATE] Inset distance (positive = inward, negative = outward).
    //         Applied only to toolpath_locations[0] in compute().
    coord_t outer_wall_offset;
};
} // namespace Slic3r::Arachne
#endif // OUTER_WALL_INSET_BEADING_STRATEGY_H
