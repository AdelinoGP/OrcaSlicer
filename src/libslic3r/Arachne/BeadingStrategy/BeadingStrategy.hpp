// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Abstract base class for the Arachne "beading strategy" — the policy
//          that determines how many walls (beads) should cover a given wall
//          thickness, and what width each bead should have.
//
// [STATE]  The strategy is parameterised at construction time and then used as
//          a const object (all query methods are const). Key parameters:
//          - optimal_width: target bead width under ideal conditions
//          - wall_split_middle_threshold / wall_add_middle_threshold: fraction
//            of optimal_width at which an odd/even-count wall should gain a
//            new central bead
//          - default_transition_length: ramp distance for bead-count transitions
//          - transitioning_angle: angle threshold that controls where transitions
//            are inserted along skeleton edges (180° minus the limit bisector
//            angle from Kuipers et al.)
//
// [COUPLING] Consumed by SkeletalTrapezoidation as a const reference (hazard:
//            dangling if strategy is destroyed before ST). Decorated by
//            LimitedBeadingStrategy, RedistributeBeadingStrategy,
//            WideningBeadingStrategy, OuterWallInsetBeadingStrategy — all
//            implemented as the Decorator pattern (ownership via unique_ptr).
//
// [MEMORY]  BeadingStrategyPtr = unique_ptr<BeadingStrategy>. Decorator
//           strategies take ownership of their parent via std::move.

#ifndef BEADING_STRATEGY_H
#define BEADING_STRATEGY_H

#include <math.h>
#include <memory>
#include <string>
#include <vector>
#include <cmath>

#include "libslic3r/libslic3r.h"

namespace Slic3r::Arachne {

template<typename T> constexpr T pi_div(const T div) { return static_cast<T>(M_PI) / div; }

/*!
 * Mostly virtual base class template.
 *
 * Strategy for covering a given (constant) horizontal model thickness with a number of beads.
 *
 * The beads may have different widths.
 *
 * TODO: extend with printing order?
 */
class BeadingStrategy
{
public:
    /*!
     * The beading for a given horizontal model thickness.
     */
    // [INTENT] Represents the bead layout for a given wall thickness.
    // [STATE]  total_thickness must equal sum(bead_widths) + left_over.
    //          toolpath_locations are measured from the polygon outline inward.
    //          A zero-width entry in bead_widths is a "signal" bead used by
    //          LimitedBeadingStrategy to mark the infill/wall boundary.
    // [HAZARD] toolpath_locations may be off by ±1 (scaled nm) due to integer
    //          rounding in DistributedBeadingStrategy::compute() — see comment
    //          in that file. GH issue #8472 patch adds +1 epsilon for this.
    struct Beading
    {
        coord_t              total_thickness;
        std::vector<coord_t> bead_widths;        //! The line width of each bead from the outer inset inward
        std::vector<coord_t> toolpath_locations; //! The distance of the toolpath location of each bead from the outline
        coord_t              left_over;          //! The distance not covered by any bead; gap area.
    };

    BeadingStrategy(coord_t optimal_width,
                    double  wall_split_middle_threshold,
                    double  wall_add_middle_threshold,
                    coord_t default_transition_length,
                    float   transitioning_angle = pi_div(3));

    BeadingStrategy(const BeadingStrategy& other);

    virtual ~BeadingStrategy() = default;

    /*!
     * Retrieve the bead widths with which to cover a given thickness.
     *
     * Requirement: Given a constant \p bead_count the output of each bead width must change gradually along with the \p thickness.
     *
     * \note The \p bead_count might be different from the \ref BeadingStrategy::optimal_bead_count
     */
    // [INTENT] Compute bead layout for a given wall thickness and target bead
    //          count. bead_count may differ from getOptimalBeadCount() — the
    //          strategy must handle any reasonable integer count gracefully.
    virtual Beading compute(coord_t thickness, coord_t bead_count) const = 0;

    /*!
     * The ideal thickness for a given \param bead_count
     */
    // [INTENT] Returns `optimal_width * bead_count` by default. Decorators
    //          (Redistribute, Limited) override to account for outer/inner
    //          width asymmetry and hard bead-count caps.
    virtual coord_t getOptimalThickness(coord_t bead_count) const;

    /*!
     * The model thickness at which \ref BeadingStrategy::optimal_bead_count transitions from \p lower_bead_count to \p lower_bead_count + 1
     */
    // [INTENT] Computes the wall-thickness at which the bead count should
    //          increase from lower_bead_count to lower_bead_count+1.
    //          Default: interpolates between lower/upper optimal thicknesses
    //          using wall_split_middle_threshold (odd count) or
    //          wall_add_middle_threshold (even count).
    // [HAZARD] The split/add threshold asymmetry means hysteresis is built in
    //          to prevent oscillating bead counts at borderline thicknesses.
    virtual coord_t getTransitionThickness(coord_t lower_bead_count) const;

    /*!
     * The number of beads should we ideally usefor a given model thickness
     */
    // [INTENT] Pure virtual — every concrete strategy must implement its own
    //          count heuristic. DistributedBeadingStrategy uses floor(t/w) +
    //          threshold check; decorators delegate after adjusting thickness.
    virtual coord_t getOptimalBeadCount(coord_t thickness) const = 0;

    /*!
     * The length of the transitioning region along the marked / significant regions of the skeleton.
     *
     * Transitions are used to smooth out the jumps in integer bead count; the jumps turn into ramps with some incline defined by their length.
     */
    // [INTENT] Controls the skeleton-edge length over which a bead-count
    //          transition ramp is spread. Default returns a small constant
    //          (0.01 mm scaled) for count==0, else default_transition_length.
    // [COUPLING] SkeletalTrapezoidation reads this to determine where
    //            transition mid-points sit on the Voronoi skeleton.
    virtual coord_t getTransitioningLength(coord_t lower_bead_count) const;

    /*!
     * The fraction of the transition length to put between the lower end of the transition and the point where the unsmoothed bead count jumps.
     *
     * Transitions are used to smooth out the jumps in integer bead count; the jumps turn into ramps which could be positioned relative to
     * the jump location.
     */
    // [INTENT] Returns a value in [0,1] indicating where in the transition
    //          length the actual count-step should occur. Default: computed
    //          from getTransitionThickness vs. getOptimalThickness so the
    //          anchor sits proportionally between lower and upper optima.
    virtual float getTransitionAnchorPos(coord_t lower_bead_count) const;

    /*!
     * Get the locations in a bead count region where \ref BeadingStrategy::compute exhibits a bend in the widths.
     * Ordered from lower thickness to higher.
     *
     * This is used to insert extra support bones into the skeleton, so that the resulting beads in long trapezoids don't linearly change
     * between the two ends.
     */
    // [INTENT] Returns thickness values where bead widths have a kink/bend
    //          within the lower_bead_count zone. Default returns empty vector.
    //          WideningBeadingStrategy overrides to prepend min_output_width,
    //          driving generateExtraRibs() in SkeletalTrapezoidation.
    // [COUPLING] SkeletalTrapezoidation::generateExtraRibs() iterates this
    //            list and inserts auxiliary skeleton nodes to avoid linear
    //            interpolation artifacts in long thin features.
    virtual std::vector<coord_t> getNonlinearThicknesses(coord_t lower_bead_count) const;

    virtual std::string toString() const;

    double getSplitMiddleThreshold() const;
    double getTransitioningAngle() const;

protected:
    // [STATE] Human-readable name used for debug logging and toString().
    //         Set by each concrete strategy ctor (e.g. "DistributedBeadingStrategy").
    //         Decorators prepend their own name (e.g. "Widening+DistributedBeadingStrategy").
    std::string name;

    // [STATE] Target bead width in scaled coordinates (nm). All width
    //         computations normalise against this value.
    coord_t optimal_width; //! Optimal bead width, nominal width off the walls in 'ideal' circumstances.

    // [STATE] Threshold fraction of optimal_width above which an odd-count
    //         wall gains a new central bead (splits the middle gap into two).
    //         Typically 0.5 (50%). Controls hysteresis with wall_add_middle_threshold.
    double wall_split_middle_threshold; //! Threshold when a middle wall should be split into two, as a ratio of the optimal wall width.

    // [STATE] Threshold fraction of optimal_width above which an even-count
    //         wall adds a new central bead. Typically 0.5 (50%).
    double wall_add_middle_threshold; //! Threshold when a new middle wall should be added between an even number of walls, as a ratio of
                                      //! the optimal wall width.

    // [STATE] Default ramp length for bead-count transitions along the
    //         skeleton, in scaled coordinates (nm). Passed through by most
    //         decorators unchanged.
    coord_t default_transition_length; //! The length of the region to smoothly transfer between bead counts

    /*!
     * The maximum angle between outline segments smaller than which we are going to add transitions
     * Equals 180 - the "limit bisector angle" from the paper
     */
    // [STATE] Bisector angle (radians) threshold below which transitions are
    //         suppressed. Equals π minus the Kuipers et al. "limit bisector
    //         angle". SkeletalTrapezoidation reads this via getTransitioningAngle().
    // [HAZARD] Stored as double; passed in as float from WallToolPaths.
    //          Precision loss is negligible but a refactor should unify types.
    double transitioning_angle;
};

using BeadingStrategyPtr = std::unique_ptr<BeadingStrategy>;

} // namespace Slic3r::Arachne
#endif // BEADING_STRATEGY_H
