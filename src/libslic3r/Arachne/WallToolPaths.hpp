// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] WallToolPaths is the public API entry point for the Arachne variable-width
// perimeter (wall) generation system. It takes a closed polygon outline, bead widths,
// wall count, and configuration parameters, then:
//   1. Pre-processes the outline (simplify, fix self-intersections, remove degenerate verts)
//   2. Runs SkeletalTrapezoidation (the core Arachne algorithm) to produce variable-width extrusion lines
//   3. Post-processes: stitch open polylines into closed polygons, remove short lines, simplify
//   4. Separates 0-width contour paths from printable extrusion paths
// The result is a sorted vector<VariableWidthLines> with toolpaths ordered from outer (inset_idx=0)
// to inner walls.
//
// [COUPLING] Depends on:
//   - SkeletalTrapezoidation — the core algorithm (Voronoi-based medial axis + bead distribution)
//   - BeadingStrategyFactory — creates the bead-width distribution strategy
//   - PolylineStitcher — joins open ExtrusisonLine polylines into closed polygons
//   - ClipperUtils — polygon boolean operations (offset, union)
//   - SparsePointGrid — spatial lookup for getRegionOrder()
//
// [MEMORY] WallToolPaths holds a reference to the input `outline` (caller-owned). The
// `toolpaths` and `inner_contour` vectors are grown by generate() and owned by this class.
// The `beading_strat` unique_ptr is created and destroyed within generate() — not stored.
//
// [CONCURRENCY] Not thread-safe. In OrcaSlicer, one WallToolPaths instance is created per
// PrintObject region per layer. Each is used in a per-layer worker thread, so different
// instances do not share state. The single instance must not be shared across threads.

#ifndef CURAENGINE_WALLTOOLPATHS_H
#define CURAENGINE_WALLTOOLPATHS_H

#include <memory>
#include <ankerl/unordered_dense.h>

#include "BeadingStrategy/BeadingStrategyFactory.hpp"
#include "utils/ExtrusionLine.hpp"
#include "../Polygon.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r::Arachne {

// [INTENT] Three inline functions encode the global mesh resolution/deviation constants used
// throughout the Arachne pipeline. These are not per-object configurable — they are
// hardcoded physical thresholds.
// meshfix_maximum_resolution: maximum segment length before simplification collapses it (0.5 mm)
// meshfix_maximum_deviation: maximum allowed deviation from original polygon when simplifying (0.025 mm)
// meshfix_maximum_extrusion_area_deviation: max area deviation in simplifyToolPaths (2 mm²)
// [HAZARD] These constants are inline functions rather than constexpr because scaled<coord_t>
// is not constexpr (it involves floating-point arithmetic at runtime). A refactored implementation
// might want these as user-configurable parameters rather than hardcoded constants.
constexpr bool fill_outline_gaps = true;
inline coord_t meshfix_maximum_resolution() { return scaled<coord_t>(0.5); }
inline coord_t meshfix_maximum_deviation() { return scaled<coord_t>(0.025); }
inline coord_t meshfix_maximum_extrusion_area_deviation() { return scaled<coord_t>(2.); }

// [INTENT] Parameter bundle for WallToolPaths construction. Separates the Arachne-specific
// tuning parameters (bead width, transition logic) from the geometry inputs (outline, bead widths).
// Created by make_paths_params() from PrintObjectConfig/PrintConfig.
// [HAZARD] All float members (min_bead_width, min_feature_size, etc.) are in millimeters,
// but WallToolPaths constructor scales them to coord_t (nanometers) via scaled<coord_t>().
// If a caller bypasses make_paths_params() and constructs WallToolPathsParams directly,
// they must ensure the values are in mm, not scaled units.
class WallToolPathsParams
{
public:
    float min_bead_width;                   ///< Minimum bead width as fraction of nozzle diameter (mm)
    float min_feature_size;                 ///< Minimum feature size to print (mm)
    float min_length_factor;                ///< Fraction of min_width; lines shorter than min_width*factor are removed
    float wall_transition_length;           ///< Length of wall transition zone (mm)
    float wall_transition_angle;            ///< Angle below which transitions between wall counts occur (degrees)
    float wall_transition_filter_deviation; ///< Allowed deviation when filtering transition lines (mm)
    int   wall_distribution_count;          ///< Number of walls over which bead width changes are distributed
    bool  is_top_or_bottom_layer;           ///< Controls short-line removal threshold in removeSmallLines
};

// [INTENT] Factory function that converts PrintObjectConfig/PrintConfig into WallToolPathsParams.
// Handles the layer_id==0 special case (uses initial_layer_min_bead_width instead of min_bead_width).
// All percentage-based config values are converted to absolute mm here.
// [COUPLING] Reads from PrintObjectConfig: min_feature_size, min_length_factor,
// initial_layer_min_bead_width, min_bead_width, wall_transition_filter_deviation,
// wall_transition_length, wall_transition_angle, wall_distribution_count.
// Reads from PrintConfig: nozzle_diameter (uses the minimum across all tools).
WallToolPathsParams make_paths_params(const int layer_id, const PrintObjectConfig& print_object_config, const PrintConfig& print_config);

class WallToolPaths
{
public:
    /*!
     * A class that creates the toolpaths given an outline, nominal bead width and maximum amount of walls
     * \param outline An outline of the area in which the ToolPaths are to be generated
     * \param bead_width_0 The bead width of the first wall used in the generation of the toolpaths
     * \param bead_width_x The bead width of the inner walls used in the generation of the toolpaths
     * \param inset_count The maximum number of parallel extrusion lines that make up the wall
     * \param wall_0_inset How far to inset the outer wall, to make it adhere better to other walls.
     */
    // [INTENT] Constructor: stores all parameters, scales mm-unit params to coord_t (nm), sets
    // toolpaths_generated = false. Does NOT call generate() — generation is lazy.
    // [HAZARD] `outline` is stored as a const reference (not by value). The caller must keep
    // the outline alive for the lifetime of this WallToolPaths object. If the outline polygon
    // is a temporary or is mutated by the caller after construction, this will cause UB.
    WallToolPaths(const Polygons&            outline,
                  coord_t                    bead_width_0,
                  coord_t                    bead_width_x,
                  size_t                     inset_count,
                  coord_t                    wall_0_inset,
                  coordf_t                   layer_height,
                  const WallToolPathsParams& params);

    /*!
     * Generates the Toolpaths
     * \return A reference to the newly create  ToolPaths
     */
    // [INTENT] Runs the full Arachne pipeline:
    //   1. Outline pre-processing (offset×3 to snap, simplify, fixSelfIntersections, ×2,
    //      removeDegenerateVerts, removeColinearEdges, removeSmallAreas, Clipper union)
    //   2. BeadingStrategy construction via BeadingStrategyFactory::makeStrategy()
    //   3. SkeletalTrapezoidation construction and generateToolpaths()
    //   4. stitchToolPaths() — joins open polylines into closed paths
    //   5. removeSmallLines() — prunes very short odd lines
    //   6. separateOutInnerContour() — splits 0-width contour lines from actual toolpaths
    //   7. simplifyToolPaths() — simplifies each ExtrusionLine by area deviation
    //   8. removeEmptyToolPaths() — final cleanup
    // Sets toolpaths_generated = true on completion.
    // [HAZARD] The outline pre-processing applies offset(-epsilon)*offset(+2*epsilon)*offset(-epsilon).
    // This Minkowski triple-offset is used to snap near-self-intersections closed, but it can
    // silently destroy very thin features (thinner than epsilon_offset ≈ allowed_distance/2 - 1).
    // Features smaller than ~12 microns will be eliminated before SkeletalTrapezoidation sees them.
    // [HAZARD] The `beading_strat` unique_ptr is created locally in generate() and destroyed at
    // function exit. SkeletalTrapezoidation holds a const reference to *beading_strat during its
    // constructor and generateToolpaths() call — safe here, but refactors moving strat to heap must
    // ensure lifetime.
    const std::vector<VariableWidthLines>& generate();

    /*!
     * Gets the toolpaths, if this called before \p generate() it will first generate the Toolpaths
     * \return a reference to the toolpaths
     */
    // [INTENT] Lazy accessor. Calls generate() if not yet generated, otherwise returns cached result.
    // [STATE] toolpaths_generated is the laziness gate. Once set to true by generate(), subsequent
    // getToolPaths() calls return the cached vector without re-running the pipeline.
    const std::vector<VariableWidthLines>& getToolPaths();

    /*!
     * Compute the inner contour of the walls. This contour indicates where the walled area ends and its infill begins.
     * The inside can then be filled, e.g. with skin/infill for the walls of a part, or with a pattern in the case of
     * infill with extra infill walls.
     */
    // [INTENT] Partitions toolpaths into printable (w > 0) and 0-width contour-only paths.
    // The 0-width paths define the boundary between wall and infill regions.
    // Populates `inner_contour` with these boundary polygons.
    // [HAZARD] `is_contour` determination (line 723-731) breaks out of the inner loop after checking
    // only the FIRST junction of the FIRST line. If an inset has mixed 0-width and non-zero-width
    // lines, only the first junction's width is checked. This could misclassify a mixed inset.
    void separateOutInnerContour();

    /*!
     * Gets the inner contour of the area which is inside of the generated tool
     * paths.
     *
     * If the walls haven't been generated yet, this will lazily call the
     * \p generate() function to generate the walls with variable width.
     * The resulting polygon will snugly match the inside of the variable-width
     * walls where the walls get limited by the LimitedBeadingStrategy to a
     * maximum wall count.
     * If there are no walls, the outline will be returned.
     * \return The inner contour of the generated walls.
     */
    // [INTENT] Returns the inner_contour polygon (boundary between walls and infill).
    // If inset_count == 0, returns the original outline unchanged (no walls = all infill).
    // [STATE] Lazy: calls generate() if toolpaths not yet generated. After generation,
    // inner_contour is set by separateOutInnerContour() (called within generate()).
    const Polygons& getInnerContour();

    /*!
     * Removes empty paths from the toolpaths
     * \param toolpaths the VariableWidthPaths generated with \p generate()
     * \return true if there are still paths left. If all toolpaths were removed it returns false
     */
    // [INTENT] Removes any VariableWidthLines entries that are empty (no extrusion lines).
    // Uses erase-remove idiom. Returns true if toolpaths is now EMPTY (all removed).
    // [HAZARD] The return value semantics are inverted from what the name suggests:
    // returns true when there are NO paths left (empty), false when paths remain.
    // Callers must read the return value carefully — it is NOT "successfully removed empties".
    static bool removeEmptyToolPaths(std::vector<VariableWidthLines>& toolpaths);

    // [INTENT] Type alias for the set of (ExtrusionLine*, ExtrusionLine*) ordering constraints.
    // Each pair (A, B) means: A must be printed before B.
    // Uses ankerl::unordered_dense::set for O(1) amortized insert/lookup.
    // boost::hash<std::pair<const ExtrusionLine*, const ExtrusionLine*>> provides the hash.
    // [COUPLING] Depends on ankerl::unordered_dense (vendored in deps/) and boost::hash.
    using ExtrusionLineSet = ankerl::unordered_dense::set<std::pair<const ExtrusionLine*, const ExtrusionLine*>,
                                                          boost::hash<std::pair<const ExtrusionLine*, const ExtrusionLine*>>>;

    /*!
     * Get the order constraints of the insets when printing walls per region / hole.
     * Each returned pair consists of adjacent wall lines where the left has an inset_idx one lower than the right.
     *
     * Odd walls should always go after their enclosing wall polygons.
     *
     * \param outer_to_inner Whether the wall polygons with a lower inset_idx should go before those with a higher one.
     */
    // [INTENT] Builds a spatial grid of all extrusion junction points, then finds neighboring
    // pairs across adjacent inset indices. For each neighboring pair, emits an ordering constraint
    // (first, second) meaning first must be printed before second.
    // `diagonal_extension = 1.9` gives a 1.9× line-width search radius to handle corner cases
    // where adjacent walls' vertices don't exactly align.
    // [HAZARD] Uses SparsePointGrid rather than SparseLineGrid for performance. In rare cases
    // (e.g., two large simplified insets where vertices don't happen to be near each other),
    // adjacency constraints can be missed. The PathOrderOptimizer may then violate the user's
    // requested wall print order for those specific segments.
    static ExtrusionLineSet getRegionOrder(const std::vector<ExtrusionLine*>& input, bool outer_to_inner);

protected:
    /*!
     * Stitch the polylines together and form closed polygons.
     *
     * Works on both toolpaths and inner contours simultaneously.
     */
    // [INTENT] Calls PolylineStitcher on each inset's ExtrusisonLines to join open polylines
    // into closed polygons. Maximum stitch distance = bead_width_x - 1 nm.
    // After stitching, reconnects closed polygons whose endpoints differ by < stitch_distance
    // (PolylineStitcher occasionally leaves a tiny gap).
    // [HAZARD] bead_width_x - 1 stitch distance means a 1nm gap is never stitched. In practice
    // this is negligible, but if bead_width_x is somehow 0 (degenerate case), the distance
    // becomes -1, and PolylineStitcher receives a negative distance — undefined behavior.
    static void stitchToolPaths(std::vector<VariableWidthLines>& toolpaths, coord_t bead_width_x);

    /*!
     * Remove polylines shorter than half the smallest line width along that polyline.
     */
    // [INTENT] Removes odd (transition) lines that are too short to print meaningfully.
    // "Too short" = shorter than (min_width/2) for top/bottom layers, or (min_width * min_length_factor)
    // for other layers. Uses the erase-move-back-decrement idiom for efficient removal.
    // [HAZARD] Only removes ODD (transition) lines — even (primary perimeter) lines are never removed
    // here regardless of length. A very short even line (e.g., caused by a tiny feature) will survive
    // this filter and potentially produce a single-point extrusion move in the G-code.
    void removeSmallLines(std::vector<VariableWidthLines>& toolpaths);

    /*!
     * Simplifies the variable-width toolpaths by calling the simplify on every line in the toolpath using the provided
     * settings.
     * \param settings The settings as provided by the user
     * \return
     */
    // [INTENT] Calls ExtrusionLine::simplify() on every line, using the three global mesh-fix thresholds.
    // Simplification removes extrusion junctions that contribute less than maximum_extrusion_area_deviation
    // to the cross-sectional area of the extrusion path.
    static void simplifyToolPaths(std::vector<VariableWidthLines>& toolpaths);

private:
    // [STATE] Reference to the caller-supplied outline polygon. NOT owned by this class.
    // Must remain valid for the entire lifetime of the WallToolPaths object.
    const Polygons& outline; //<! A reference to the outline polygon that is the designated area

    // [STATE] Bead widths in scaled coord_t (nanometers). bead_width_0 = outer wall,
    // bead_width_x = inner walls. These are not the extrusion widths — they are the
    // "nominal" widths used by SkeletalTrapezoidation to set up the beading strategy.
    coord_t bead_width_0; //<! The nominal or first extrusion line width with which libArachne generates its walls
    coord_t bead_width_x; //<! The subsequently extrusion line width with which libArachne generates its walls if WallToolPaths was called
                          //with the nominal_bead_width Constructor this is the same as bead_width_0

    // [STATE] Maximum wall count. Passed to BeadingStrategyFactory as max_bead_count = 2*inset_count.
    // [HAZARD] If inset_count >= INT_MAX/2, max_bead_count overflows. The generate() code guards
    // this with a conditional: `(inset_count < INT_MAX/2) ? 2*inset_count : INT_MAX`.
    size_t inset_count; //<! The maximum number of walls to generate

    // [STATE] Outer wall inset offset. Applied by BeadingStrategyFactory to position the outer wall
    // inward, improving adhesion to adjacent walls. 0 = no inset.
    coord_t wall_0_inset; //<! How far to inset the outer wall. Should only be applied when printing the actual walls, not extra
                          //infill/skin/support walls.
    coordf_t layer_height;

    // [STATE] print_thin_walls = fill_outline_gaps (currently hardcoded true).
    // Enables WidenedBeadingStrategy which widens beads narrower than min_feature_size.
    bool print_thin_walls; //<! Whether to enable the widening beading meta-strategy for thin features

    // [STATE] Scaled (nanometer) versions of mm parameters from WallToolPathsParams.
    // Scaling happens in the constructor via scaled<coord_t>().
    coord_t min_feature_size; //<! The minimum size of the features that can be widened by the widening beading meta-strategy. Features
                              //thinner than that will not be printed
    coord_t min_bead_width;   //<! The minimum bead size to use when widening thin model features with the widening beading meta-strategy

    // [STATE] small_area_length = bead_width_0 / 2.0 (in double, unscaled).
    // Used as the threshold for removeSmallAreas() in generate(): removes polygons with
    // area < small_area_length² (i.e., area < (bead_width_0/2)²).
    double small_area_length; //<! The length of the small features which are to be filtered out, this is squared into a surface

    coord_t wall_transition_filter_deviation; //!< The allowed line width deviation induced by filtering

    // [STATE] Gate flag: false until generate() completes successfully.
    // Prevents redundant re-generation in getToolPaths()/getInnerContour().
    bool toolpaths_generated; //<! Are the toolpaths generated

    // [STATE] Output: the generated variable-width extrusion lines, sorted by inset_idx.
    // Inset_idx 0 = outermost wall. Populated by generate(), consumed by callers.
    std::vector<VariableWidthLines> toolpaths; //<! The generated toolpaths

    // [STATE] Output: the inner contour polygon(s), representing the boundary between walls and infill.
    // Populated by separateOutInnerContour() (called within generate()).
    // Contains only 0-width extrusion paths (contour markers), not printable paths.
    Polygons inner_contour; //<! The inner contour of the generated toolpaths

    // [STATE] Copy of the WallToolPathsParams passed at construction. Used in generate()
    // for wall_transition_angle, wall_distribution_count, min_bead_width, min_length_factor,
    // is_top_or_bottom_layer.
    const WallToolPathsParams m_params;
};

} // namespace Slic3r::Arachne

#endif // CURAENGINE_WALLTOOLPATHS_H
