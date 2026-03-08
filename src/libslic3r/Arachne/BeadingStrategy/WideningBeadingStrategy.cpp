// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements the thin-wall widening logic. For features thinner than
//          one optimal-width bead, this strategy produces a single wall at
//          max(thickness, min_output_width) to ensure adhesion even on
//          features smaller than the nozzle diameter.
//
// [STATE]  All methods are const.
//
// [HAZARD] compute(): the bead width may be set to min_output_width even when
//          thickness < min_output_width. This intentionally over-extrudes to
//          fill thin features. Any downstream width-checking code that assumes
//          bead_width <= thickness will be wrong in this regime.
//
// [HAZARD] getNonlinearThicknesses(): always prepends min_output_width.
//          This is unconditional — it fires even when lower_bead_count > 0,
//          meaning every bead-count zone gets a nonlinear thickness added.
//          In practice only lower_bead_count == 0 is relevant (the widening
//          zone), but the implementation doesn't guard on count. A refactor
//          should consider limiting to lower_bead_count == 0.
//
// [HAZARD] getTransitionThickness(0) returns min_input_width, but compute()
//          intercepts at thickness < optimal_width. If min_input_width >
//          optimal_width, the transition signals a count increase before
//          the widening kicks in — a potential logic inconsistency. This
//          combination is not expected in practice (min_input_width is
//          always set < optimal_width by BeadingStrategyFactory).

#include "WideningBeadingStrategy.hpp"

#include <algorithm>
#include <utility>

#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {

WideningBeadingStrategy::WideningBeadingStrategy(BeadingStrategyPtr parent, const coord_t min_input_width, const coord_t min_output_width)
    : BeadingStrategy(*parent)  // [INTENT] Copy base fields from parent
    , parent(std::move(parent)) // [MEMORY] Exclusive ownership transferred
    , min_input_width(min_input_width)
    , min_output_width(min_output_width)
{}

std::string WideningBeadingStrategy::toString() const { return std::string("Widening+") + parent->toString(); }

WideningBeadingStrategy::Beading WideningBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
{
    if (thickness < optimal_width) {
        // [INTENT] Thin-wall regime: produce 0 or 1 beads depending on whether
        //          thickness clears min_input_width. Bead width = max(thickness,
        //          min_output_width) to guarantee minimum printable extrusion.
        Beading ret;
        ret.total_thickness = thickness;
        if (thickness >= min_input_width) {
            ret.bead_widths.emplace_back(std::max(thickness, min_output_width));
            ret.toolpath_locations.emplace_back(thickness / 2);
            ret.left_over = 0;
        } else
            // [INTENT] Below min_input_width: entire thickness is unprinted gap.
            ret.left_over = thickness;

        return ret;
    } else
        // [INTENT] Normal regime: delegate to parent unchanged.
        return parent->compute(thickness, bead_count);
}

coord_t WideningBeadingStrategy::getOptimalThickness(coord_t bead_count) const { return parent->getOptimalThickness(bead_count); }

coord_t WideningBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
{
    if (lower_bead_count == 0)
        // [INTENT] The 0→1 transition fires at min_input_width, not at the
        //          parent's computed threshold (which would be based on optimal_width).
        return min_input_width;
    else
        return parent->getTransitionThickness(lower_bead_count);
}

coord_t WideningBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    if (thickness < min_input_width)
        return 0;
    coord_t ret = parent->getOptimalBeadCount(thickness);
    // [INTENT] If parent says 0 but we're above min_input_width, force 1 bead.
    if (thickness >= min_input_width && ret < 1)
        return 1;
    return ret;
}

coord_t WideningBeadingStrategy::getTransitioningLength(coord_t lower_bead_count) const
{
    return parent->getTransitioningLength(lower_bead_count);
}

float WideningBeadingStrategy::getTransitionAnchorPos(coord_t lower_bead_count) const
{
    return parent->getTransitionAnchorPos(lower_bead_count);
}

std::vector<coord_t> WideningBeadingStrategy::getNonlinearThicknesses(coord_t lower_bead_count) const
{
    // [INTENT] Prepend min_output_width as a "kink" point in the width
    //          function. This tells SkeletalTrapezoidation::generateExtraRibs()
    //          to insert a support node at this thickness, preventing a purely
    //          linear width interpolation across the thin-wall transition.
    // [HAZARD] Unconditional prepend — applies to all lower_bead_count values,
    //          not just 0. See file-level [HAZARD] note.
    std::vector<coord_t> ret;
    ret.emplace_back(min_output_width);
    std::vector<coord_t> pret = parent->getNonlinearThicknesses(lower_bead_count);
    ret.insert(ret.end(), pret.begin(), pret.end());
    return ret;
}

} // namespace Slic3r::Arachne
