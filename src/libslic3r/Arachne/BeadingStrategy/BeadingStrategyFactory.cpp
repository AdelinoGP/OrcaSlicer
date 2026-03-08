// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Constructs the full BeadingStrategy decorator chain.
//          This is the single place where the strategy composition order is
//          defined. The chain is built innermost-first:
//
//   1. DistributedBeadingStrategy — core width distribution algorithm
//   2. RedistributeBeadingStrategy — outer/inner wall width isolation
//   3. WideningBeadingStrategy — thin-wall printing (optional)
//   4. OuterWallInsetBeadingStrategy — outer-wall offset (optional)
//   5. LimitedBeadingStrategy — hard bead-count cap + 0-width sentinel
//
//          Each decorator wraps the previous one via std::move.
//
// [COUPLING] Called only from WallToolPaths::WallToolPaths().
//            Parameter semantics are documented in BeadingStrategyFactory.hpp.
//
// [HAZARD] SPECIAL CASE at line ~35-38:
//          When max_bead_count <= 2, optimal_width = preferred_bead_width_OUTER
//          (not inner). This prevents DistributedBeadingStrategy from computing
//          inner-width-based distribution for single/double-wall parts where no
//          inner walls exist. The RedistributeBeadingStrategy will still run but
//          degenerates to symmetric outer-only behaviour for count <= 2.
//
// [HAZARD] LimitedBeadingStrategy logs a warning if max_bead_count is odd.
//          BeadingStrategyFactory does NOT guard against this — it is the
//          caller's responsibility to pass an even max_bead_count.
//
// [HAZARD] Orca extension: outer_wall_offset != 0 (not > 0) activates
//          OuterWallInsetBeadingStrategy. Upstream only supported positive.
//          The comment "// Orca: we allow negative outer_wall_offset here"
//          documents this intentional deviation from upstream CuraEngine.

#include "BeadingStrategyFactory.hpp"

#include <boost/log/trivial.hpp>
#include <memory>
#include <utility>

#include "LimitedBeadingStrategy.hpp"
#include "WideningBeadingStrategy.hpp"
#include "DistributedBeadingStrategy.hpp"
#include "RedistributeBeadingStrategy.hpp"
#include "OuterWallInsetBeadingStrategy.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {

BeadingStrategyPtr BeadingStrategyFactory::makeStrategy(const coord_t preferred_bead_width_outer,
                                                        const coord_t preferred_bead_width_inner,
                                                        const coord_t preferred_transition_length,
                                                        const float   transitioning_angle,
                                                        const bool    print_thin_walls,
                                                        const coord_t min_bead_width,
                                                        const coord_t min_feature_size,
                                                        const double  wall_split_middle_threshold,
                                                        const double  wall_add_middle_threshold,
                                                        const coord_t max_bead_count,
                                                        const coord_t outer_wall_offset,
                                                        const int     inward_distributed_center_wall_count,
                                                        const double  minimum_variable_line_ratio)
{
    // Handle a special case when there is just one external perimeter.
    // Because big differences in bead width for inner and other perimeters cause issues with current beading strategies.
    // [INTENT] For max_bead_count <= 2: use outer width as the Distributed
    //          strategy's optimal_width to avoid width mismatch when no inner
    //          walls exist. For count > 2: use inner width (usual case).
    const coord_t      optimal_width = max_bead_count <= 2 ? preferred_bead_width_outer : preferred_bead_width_inner;
    BeadingStrategyPtr ret = std::make_unique<DistributedBeadingStrategy>(optimal_width, preferred_transition_length, transitioning_angle,
                                                                          wall_split_middle_threshold, wall_add_middle_threshold,
                                                                          inward_distributed_center_wall_count);

    BOOST_LOG_TRIVIAL(trace) << "Applying the Redistribute meta-strategy with outer-wall width = " << preferred_bead_width_outer
                             << ", inner-wall width = " << preferred_bead_width_inner << ".";
    ret = std::make_unique<RedistributeBeadingStrategy>(preferred_bead_width_outer, minimum_variable_line_ratio, std::move(ret));

    if (print_thin_walls) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the Widening Beading meta-strategy with minimum input width " << min_feature_size
                                 << " and minimum output width " << min_bead_width << ".";
        ret = std::make_unique<WideningBeadingStrategy>(std::move(ret), min_feature_size, min_bead_width);
    }
    // Orca: we allow negative outer_wall_offset here
    // [INTENT] Apply outer-wall inset/outset if any offset is specified.
    //          Orca extension: negative offset = outward (upstream only had inward).
    if (outer_wall_offset != 0) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the OuterWallOffset meta-strategy with offset = " << outer_wall_offset << ".";
        ret = std::make_unique<OuterWallInsetBeadingStrategy>(outer_wall_offset, std::move(ret));
    }

    // Apply the LimitedBeadingStrategy last, since that adds a 0-width marker wall which other beading strategies shouldn't touch.
    // [INTENT] LimitedBeadingStrategy MUST be outermost so its 0-width
    //          sentinels are not modified by any other decorator.
    BOOST_LOG_TRIVIAL(trace) << "Applying the Limited Beading meta-strategy with maximum bead count = " << max_bead_count << ".";
    ret = std::make_unique<LimitedBeadingStrategy>(max_bead_count, std::move(ret));
    return ret;
}
} // namespace Slic3r::Arachne
