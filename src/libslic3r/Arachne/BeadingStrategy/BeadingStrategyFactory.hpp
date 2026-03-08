// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Single static factory method that assembles the full BeadingStrategy
//          decorator stack. The stack is always built in a fixed order:
//
//            DistributedBeadingStrategy          (innermost / base)
//            └── RedistributeBeadingStrategy     (outer-wall isolation)
//                └── [WideningBeadingStrategy]   (only if print_thin_walls)
//                    └── [OuterWallInsetBeadingStrategy] (only if offset != 0)
//                        └── LimitedBeadingStrategy (outermost / cap)
//
//          The LimitedBeadingStrategy is ALWAYS the final wrapper.
//          It inserts the 0-width sentinel beads, so it must be applied last.
//
// [COUPLING] Called from WallToolPaths::WallToolPaths() in WallToolPaths.cpp.
//            All parameters come from PrintObject settings. Changes to
//            parameter names here require matching changes in WallToolPaths.
//
// [HAZARD] SPECIAL CASE: when max_bead_count <= 2, the factory passes
//          `preferred_bead_width_outer` (not inner) to DistributedBeadingStrategy.
//          This avoids a large width mismatch when only 1 or 2 walls exist.
//          Any refactor must preserve this special case or single/double-wall
//          parts will exhibit extrusion-width discontinuities.
//
// [HAZARD] Orca extension: outer_wall_offset accepts NEGATIVE values (outward
//          inset). The guard `outer_wall_offset != 0` (not `> 0`) ensures
//          both positive and negative offsets activate OuterWallInsetBeadingStrategy.
//          Upstream CuraEngine only supports positive (inward) offsets.

#ifndef BEADING_STRATEGY_FACTORY_H
#define BEADING_STRATEGY_FACTORY_H

#include <math.h>
#include <cmath>

#include "BeadingStrategy.hpp"
#include "../../Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

class BeadingStrategyFactory
{
public:
    static BeadingStrategyPtr makeStrategy(coord_t preferred_bead_width_outer           = scaled<coord_t>(0.0005),
                                           coord_t preferred_bead_width_inner           = scaled<coord_t>(0.0005),
                                           coord_t preferred_transition_length          = scaled<coord_t>(0.0004),
                                           float   transitioning_angle                  = M_PI / 4.0,
                                           bool    print_thin_walls                     = false,
                                           coord_t min_bead_width                       = 0,
                                           coord_t min_feature_size                     = 0,
                                           double  wall_split_middle_threshold          = 0.5,
                                           double  wall_add_middle_threshold            = 0.5,
                                           coord_t max_bead_count                       = 0,
                                           coord_t outer_wall_offset                    = 0,
                                           int     inward_distributed_center_wall_count = 2,
                                           double  minimum_variable_line_width          = 0.5);
};

} // namespace Slic3r::Arachne
#endif // BEADING_STRATEGY_FACTORY_H
