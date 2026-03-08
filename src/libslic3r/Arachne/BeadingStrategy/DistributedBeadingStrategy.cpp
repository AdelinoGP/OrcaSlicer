// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements the core width-distribution algorithm for Arachne walls.
//          Given a target bead count and total thickness, produces a Beading
//          where inner beads absorb more deviation from optimal width than
//          outer beads (Gaussian-like weight centred on the middle bead).
//
// [HAZARD] compute() for bead_count > 2:
//          - `to_be_divided` may be negative (thickness < count * optimal_width),
//            meaning beads are narrower than optimal. The weight/distribution
//            logic handles this symmetrically, so negative widths are possible
//            in theory; callers must clamp via LimitedBeadingStrategy first.
//          - The last bead width = thickness - accumulated_width (absorbs all
//            integer rounding). This means the last inner bead can differ from
//            its weight-computed value by ±1 nm. Documented in source comment.
//          - assert at line ~65 checks sum == thickness only in debug builds.
//
// [HAZARD] getOptimalBeadCount() uses integer division: naive_count = t/w.
//          Floating-point thickness values passed through coord_t may lose the
//          fractional part, slightly under-counting walls at precise thresholds.
//
// [CONCURRENCY] All methods are const; the object is immutable after construction.
//               Safe to share across threads.

#include <numeric>
#include <algorithm>
#include <vector>
#include <cassert>

#include "DistributedBeadingStrategy.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {

DistributedBeadingStrategy::DistributedBeadingStrategy(const coord_t optimal_width,
                                                       const coord_t default_transition_length,
                                                       const double  transitioning_angle,
                                                       const double  wall_split_middle_threshold,
                                                       const double  wall_add_middle_threshold,
                                                       const int     distribution_radius)
    : BeadingStrategy(optimal_width, wall_split_middle_threshold, wall_add_middle_threshold, default_transition_length, transitioning_angle)
{
    // [INTENT] Precompute 1/(r-1)² for the Gaussian weight function.
    //          When r < 2 (degenerate), fall back to 1/1² = uniform weight.
    // [HAZARD] distribution_radius == 1 gives (r-1) = 0, guarded by >= 2 branch.
    if (distribution_radius >= 2)
        one_over_distribution_radius_squared = 1.0f / (distribution_radius - 1) * 1.0f / (distribution_radius - 1);
    else
        one_over_distribution_radius_squared = 1.0f / 1 * 1.0f / 1;
    name = "DistributedBeadingStrategy";
}

DistributedBeadingStrategy::Beading DistributedBeadingStrategy::compute(const coord_t thickness, const coord_t bead_count) const
{
    Beading ret;

    ret.total_thickness = thickness;
    if (bead_count > 2) {
        // [INTENT] Compute how much thickness is left over after fitting
        //          bead_count beads at optimal_width each. Distribute this
        //          surplus (positive) or deficit (negative) weighted by
        //          distance from the middle bead index.
        const coord_t to_be_divided = thickness - bead_count * optimal_width;
        const float   middle        = static_cast<float>(bead_count - 1) / 2;

        // [INTENT] Weight function: Gaussian-like falloff from centre.
        //          w(i) = max(0, 1 - one_over_r² * (i - middle)²)
        //          Beads at the centre get weight ~1; beads at ± radius get 0.
        const auto getWeight = [middle, this](coord_t bead_idx) {
            const float dev_from_middle = bead_idx - middle;
            return std::max(0.0f, 1.0f - one_over_distribution_radius_squared * dev_from_middle * dev_from_middle);
        };

        std::vector<float> weights;
        weights.resize(bead_count);
        for (coord_t bead_idx = 0; bead_idx < bead_count; bead_idx++)
            weights[bead_idx] = getWeight(bead_idx);

        const float total_weight      = std::accumulate(weights.cbegin(), weights.cend(), 0.f);
        coord_t     accumulated_width = 0;
        for (coord_t bead_idx = 0; bead_idx < bead_count; bead_idx++) {
            const float   weight_fraction          = weights[bead_idx] / total_weight;
            const coord_t splitup_left_over_weight = to_be_divided * weight_fraction;
            // [HAZARD] Last bead absorbs all remaining thickness to avoid
            //          cumulative integer-rounding drift. Off by ±1 nm vs
            //          weight-computed value. toolpath_locations may also
            //          be off by ±1 due to width/2 integer division.
            const coord_t width = (bead_idx == bead_count - 1) ? thickness - accumulated_width : optimal_width + splitup_left_over_weight;

            // Be aware that toolpath_locations is computed by dividing the width by 2, so toolpath_locations
            // could be off by 1 because of rounding errors.
            if (bead_idx == 0)
                ret.toolpath_locations.emplace_back(width / 2);
            else
                ret.toolpath_locations.emplace_back(ret.toolpath_locations.back() + (ret.bead_widths.back() + width) / 2);
            ret.bead_widths.emplace_back(width);
            accumulated_width += width;
        }
        ret.left_over = 0;
        assert((accumulated_width + ret.left_over) == thickness);
    } else if (bead_count == 2) {
        // [INTENT] Two symmetric outer walls, each half the total thickness.
        //          No inner distribution needed.
        const coord_t outer_width = thickness / 2;
        ret.bead_widths.emplace_back(outer_width);
        ret.bead_widths.emplace_back(outer_width);
        ret.toolpath_locations.emplace_back(outer_width / 2);
        ret.toolpath_locations.emplace_back(thickness - outer_width / 2);
        ret.left_over = 0;
    } else if (bead_count == 1) {
        // [INTENT] Single wall that spans the full thickness.
        const coord_t outer_width = thickness;
        ret.bead_widths.emplace_back(outer_width);
        ret.toolpath_locations.emplace_back(outer_width / 2);
        ret.left_over = 0;
    } else {
        // [INTENT] bead_count == 0: entire thickness is unprinted gap.
        ret.left_over = thickness;
    }

    assert(([&ret = std::as_const(ret), thickness]() -> bool {
        coord_t total_bead_width = 0;
        for (const coord_t& bead_width : ret.bead_widths)
            total_bead_width += bead_width;
        return (total_bead_width + ret.left_over) == thickness;
    }()));

    return ret;
}

coord_t DistributedBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
{
    // [INTENT] Floor-divide thickness by optimal_width to get the safe count,
    //          then add 1 if the remainder exceeds the split/add threshold.
    //          Odd naive_count → split threshold; even → add threshold.
    // [HAZARD] Integer division truncates; at exactly N * optimal_width the
    //          remainder == 0 which is always < threshold, so count == N
    //          (correct). At values just below, same. Safe.
    const coord_t naive_count        = thickness / optimal_width;               // How many lines we can fit in for sure.
    const coord_t remainder          = thickness - naive_count * optimal_width; // Space left after fitting that many lines.
    const coord_t minimum_line_width = optimal_width * (naive_count % 2 == 1 ? wall_split_middle_threshold : wall_add_middle_threshold);
    return naive_count + (remainder >= minimum_line_width); // If there's enough space, fit an extra one.
}

} // namespace Slic3r::Arachne
