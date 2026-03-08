// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] SkeletalTrapezoidation is the core of the Arachne variable-width perimeter algorithm.
// It implements the "skeletal trapezoidation" method from the paper:
//   "A framework for adaptive width control of dense contour-parallel toolpaths in
//    fused deposition modeling" — Kuipers et al. (2020)
//
// Algorithm overview:
// 1. VORONOI CONSTRUCTION: Build a Voronoi Diagram (VD) from the input polygon segments.
//    The VD partitions the polygon into cells equidistant from the nearest polygon edges.
//    Each cell edge (Voronoi edge) traces a locus of points equidistant from two polygon features.
//
// 2. HALF-EDGE GRAPH TRANSFER: Transfer VD edges into a half-edge (HE) data structure
//    (SkeletalTrapezoidationGraph). Parabolic VD edges (formed near polygon vertex-line pairs)
//    are discretized into line segments during transfer. Each HE edge stores its R value:
//    R = distance to nearest polygon boundary.
//
// 3. CENTRALITY DETERMINATION: Mark edges as "central" or "non-central" based on
//    transitioning_angle. Central edges represent the medial axis (skeleton) of the polygon.
//    Non-central edges radiate from the skeleton outward to the polygon boundary.
//
// 4. BEAD COUNT ASSIGNMENT: For each central edge, compute the optimal bead count via
//    BeadingStrategy::optimal_bead_count(R). This answers: "how many parallel walls fit at
//    this width?". Bead counts are then filtered and propagated to smooth sudden transitions.
//
// 5. TRANSITION GENERATION: Where adjacent edges have different bead counts, generate
//    transition zones. Transitions are represented as TransitionMiddle (midpoint) and
//    TransitionEnd (endpoint) markers on edges.
//
// 6. TOOLPATH GENERATION: Propagate bead information from high-R nodes (skeleton interior)
//    toward low-R nodes (polygon boundary). Generate ExtrusisonJunctions at the intersections
//    of beads and ribs. Connect junctions to form ExtrusisonLines. The output is a
//    vector<VariableWidthLines> sorted by inset_idx (outer to inner).
//
// [MEMORY] The algorithm uses:
//   - graph_t (SkeletalTrapezoidationGraph): stores all half-edges and nodes. Large structure.
//   - vd_edge_to_he_edge / vd_node_to_he_node: ankerl::unordered_dense maps for VD→HE mapping.
//     These are populated during construction and used only in constructFromPolygons().
//   - ptr_vector_t<BeadingPropagation>: vector of shared_ptr, one per skeletal node.
//   - ptr_vector_t<LineJunctions>: vector of shared_ptr to junction lists per edge.
// Peak memory is proportional to the number of Voronoi vertices (O(N) for N polygon segments).
//
// [CONCURRENCY] Not thread-safe. One SkeletalTrapezoidation instance is created per polygon
// region per layer, used exclusively by a single layer-processing thread.
//
// [COUPLING] Depends on:
//   - boost::polygon::voronoi (Voronoi diagram computation) — vendored, boost/polygon/voronoi.hpp
//   - SkeletalTrapezoidationGraph (half-edge data structure)
//   - BeadingStrategy (bead width distribution strategy — injected via const reference)
//   - SkeletalTrapezoidationEdge/Joint (half-edge annotations)
//   - ankerl::unordered_dense (fast hash maps for VD→HE mapping)
//
// [HAZARD] The Voronoi diagram from boost::polygon uses floating-point arithmetic internally
// but produces integer-coordinate vertices via rounding. Zero-length HE edges can result from
// this rounding. The algorithm handles this by collapsing degenerate edges/cells in
// constructFromPolygons(), but pathological inputs (nearly-collinear vertices at integer
// coordinates) may produce uncollapsed zero-length edges that cause division-by-zero later.
//
// [HAZARD] Many methods in this class use recursive graph traversal (e.g., filterCentral(),
// filterNoncentralRegions(), isGoingDown()). For complex polygons with very long medial axes
// (e.g., a long thin rectangle), the recursion depth can be proportional to the number of
// Voronoi edges, potentially causing stack overflow. No depth limit or iterative fallback
// is implemented.

#ifndef SKELETAL_TRAPEZOIDATION_H
#define SKELETAL_TRAPEZOIDATION_H

#include <boost/polygon/voronoi.hpp>
#include <ankerl/unordered_dense.h>
#include <memory>  // smart pointers
#include <utility> // pair
#include <list>
#include <vector>

#include "utils/HalfEdgeGraph.hpp"
#include "utils/PolygonsSegmentIndex.hpp"
#include "utils/ExtrusionJunction.hpp"
#include "utils/ExtrusionLine.hpp"
#include "SkeletalTrapezoidationEdge.hpp"
#include "SkeletalTrapezoidationJoint.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"
#include "SkeletalTrapezoidationGraph.hpp"
#include "../Geometry/Voronoi.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

// #define ARACHNE_DEBUG
// #define ARACHNE_DEBUG_VORONOI

namespace Slic3r::Arachne {

// [INTENT] VD is an alias for the Voronoi Diagram type from OrcaSlicer's Geometry namespace.
// Slic3r::Geometry::VoronoiDiagram wraps boost::polygon::voronoi_diagram with integer-coordinate
// input adapters appropriate for OrcaSlicer's coord_t (nanometer) coordinate system.
using VD = Slic3r::Geometry::VoronoiDiagram;

/*!
 * Main class of the dynamic beading strategies.
 *
 * The input polygon region is decomposed into trapezoids and represented as a half-edge data-structure.
 *
 * We determine which edges are 'central' accordinding to the transitioning_angle of the beading strategy,
 * and determine the bead count for these central regions and apply them outward when generating toolpaths. [oversimplified]
 *
 * The method can be visually explained as generating the 3D union of cones surface on the outline polygons,
 * and changing the heights along central regions of that surface so that they are flat.
 * For more info, please consult the paper "A framework for adaptive width control of dense contour-parallel toolpaths in fused
deposition modeling" by Kuipers et al.
 * This visual explanation aid explains the use of "upward", "lower" etc,
 * i.e. the radial distance and/or the bead count are used as heights of this visualization, there is no coordinate called 'Z'.
 *
 * TODO: split this class into two:
 * 1. Class for generating the decomposition and aux functions for performing updates
 * 2. Class for editing the structure for our purposes.
 */
class SkeletalTrapezoidation
{
    // [INTENT] Type aliases for the half-edge data structures. The "graph_t" is the whole graph
    // (nodes + edges), "edge_t" is a directed half-edge, "node_t" is a graph vertex.
    // Beading = the data structure storing how bead widths are distributed at a given radius.
    // BeadingPropagation = Beading + distance metadata for upward/downward propagation.
    // TransitionMiddle / TransitionEnd = markers stored on edges to represent bead-count transitions.
    using graph_t            = SkeletalTrapezoidationGraph;
    using edge_t             = STHalfEdge;
    using node_t             = STHalfEdgeNode;
    using Beading            = BeadingStrategy::Beading;
    using BeadingPropagation = SkeletalTrapezoidationJoint::BeadingPropagation;
    using TransitionMiddle   = SkeletalTrapezoidationEdge::TransitionMiddle;
    using TransitionEnd      = SkeletalTrapezoidationEdge::TransitionEnd;

    // [INTENT] ptr_vector_t<T> = vector of shared_ptr<T>. Used for BeadingPropagation and
    // LineJunctions collections that are passed between methods. shared_ptr is used because
    // the same beading may be referenced from multiple nodes during propagation.
    // [HAZARD] shared_ptr introduces reference-count overhead for every bead access. For
    // polygons with thousands of skeletal nodes, this overhead may become noticeable.
    // A refactored implementation might use std::unique_ptr with explicit ownership transfer,
    // or arena allocation.
    template<typename T> using ptr_vector_t = std::vector<std::shared_ptr<T>>;

    // [STATE] Core algorithm parameters, set in constructor:
    double  transitioning_angle;      //!< How pointy a region should be before we apply the method. Equals 180* - limit_bisector_angle
    coord_t discretization_step_size; //!< approximate size of segments when parabolic VD edges get discretized (and vertex-vertex edges)
    coord_t transition_filter_dist;   //!< Filter transition mids (i.e. anchors) closer together than this
    coord_t allowed_filter_deviation; //!< The allowed line width deviation induced by filtering
    // [STATE] beading_propagation_transition_dist: distance over which beadings from below and above
    // are merged when they encounter each other. This is the parameter `wall_transition_length`
    // from WallToolPaths, passed through as the constructor's last argument.
    coord_t beading_propagation_transition_dist; //!< When there are different beadings propagated from below and from above, use this
                                                 //!< transitioning distance

    // [INTENT] central_filter_dist = 0.02mm = 20 microns. Any central edge shorter than this is
    // un-marked as central (it's likely a Voronoi rounding artifact, not a real medial axis segment).
    // snap_dist = 0.02mm = 20 microns. Used to determine whether a transition really needs
    // a new edge inserted (if the transition is within snap_dist of an existing node, don't insert).
    //!< Filter areas marked as 'central' smaller than this
    inline coord_t central_filter_dist() { return scaled<coord_t>(0.02); }
    //!< Generic arithmatic inaccuracy. Only used to determine whether a transition really needs to insert an extra edge.
    inline coord_t snap_dist() { return scaled<coord_t>(0.02); }

    /*!
     * The strategy to use to fill a certain shape with lines.
     *
     * Various BeadingStrategies are available that differ in which lines get to
     * print at their optimal width, where the play is being compensated, and
     * how the joints are handled where we transition to different numbers of
     * lines.
     */
    // [STATE] Injected const reference to the beading strategy. Must outlive this object.
    // [HAZARD] Stored as a const reference — the BeadingStrategy object must remain valid
    // for the entire lifetime of SkeletalTrapezoidation. In generate() (WallToolPaths.cpp),
    // the beading_strat unique_ptr is created locally, and SkeletalTrapezoidation is also
    // a local object — both go out of scope at the same time, so this is safe currently.
    // But if SkeletalTrapezoidation were stored beyond the generate() scope (e.g., cached
    // for debugging), the reference would dangle.
    const BeadingStrategy& beading_strategy;

public:
    // [INTENT] Type aliases for public API callers:
    // Segment = PolygonsSegmentIndex, identifies a segment within a Polygons structure.
    // NodeSet = unordered set of node_t* pointers (used for tracking visited nodes).
    using Segment = PolygonsSegmentIndex;
    using NodeSet = ankerl::unordered_dense::set<node_t*>;

    /*!
     * Construct a new trapezoidation problem to solve.
     * \param polys The shapes to fill with walls.
     * \param beading_strategy The strategy to use to fill these shapes.
     * \param transitioning_angle Where we transition to a different number of
     * walls, how steep should this transition be? A lower angle means that the
     * transition will be longer.
     * \param discretization_step_size Since g-code can't represent smooth
     * transitions in line width, the line width must change with discretized
     * steps. This indicates how long the line segments between those steps will
     * be.
     * \param transition_filter_dist The minimum length of transitions.
     * Transitions shorter than this will be considered for dissolution.
     * \param beading_propagation_transition_dist When there are different
     * beadings propagated from below and from above, use this transitioning
     * distance.
     */
    // [INTENT] The constructor:
    //   1. Stores parameters.
    //   2. Calls constructFromPolygons() to build the Voronoi diagram and transfer to half-edge graph.
    // After construction, the graph is fully built and indexed. generateToolpaths() can then
    // be called to run the remaining algorithm phases.
    // [COUPLING] polys must be pre-processed (no self-intersections, etc.) — WallToolPaths
    // is responsible for this pre-processing before calling this constructor.
    SkeletalTrapezoidation(const Polygons&        polys,
                           const BeadingStrategy& beading_strategy,
                           double                 transitioning_angle,
                           coord_t                discretization_step_size,
                           coord_t                transition_filter_dist,
                           coord_t                allowed_filter_deviation,
                           coord_t                beading_propagation_transition_dist);

    /*!
     * A skeletal graph through the polygons that we need to fill with beads.
     *
     * The skeletal graph represents the medial axes through each part of the
     * polygons, and the lines from these medial axes towards each vertex of the
     * polygons. The graph can be used to see what the width is of a polygon in
     * each place and where the width transitions.
     */
    // [STATE] The half-edge graph representing the Voronoi skeleton + rib edges.
    // This is the primary data structure that the entire algorithm operates on.
    // It is public to allow external inspection (e.g., for debug visualization).
    // [MEMORY] graph_t owns all nodes and edges. Edges are stored in std::list<edge_t>
    // and std::list<node_t> within graph_t, so iterators are stable under insertion.
    graph_t graph;

    /*!
     * Generate the paths that the printer must extrude, to print the outlines
     * in the input polygons.
     * \param filter_outermost_central_edges Some edges are "central" but still
     * touch the outside of the polygon. If enabled, don't treat these as
     * "central" but as if it's a obtuse corner. As a result, sharp corners will
     * no longer end in a single line but will just loop.
     */
    // [INTENT] Main entry point after construction. Runs all phases of the algorithm:
    //   Phase 1: updateIsCentral() — mark central vs non-central edges
    //   Phase 2: filterCentral() — remove tiny central artifacts
    //   Phase 3: (optional) filterOuterCentral() — remove outermost central edges
    //   Phase 4: updateBeadCount() — assign bead counts to central edges
    //   Phase 5: filterNoncentralRegions() — fill bead counts in non-central regions
    //   Phase 6: generateTransitionMids() — compute transition midpoints
    //   Phase 7: filterTransitionMids() — merge close transitions
    //   Phase 8: generateTransitioningRibs() — create rib edges at transitions
    //   Phase 9: generateExtraRibs() — create ribs for bottleneck parabolas
    //   Phase 10: generateSegments() — propagate beadings, generate junctions + toolpath segments
    // Output stored in generated_toolpaths (passed by reference).
    // [HAZARD] filter_outermost_central_edges defaults to false. The docstring says enabling it
    // changes how sharp corners are handled. The code currently uses it as a conditional in
    // filterOuterCentral(). The effect on print quality is documented as "emulating related
    // literature" — it may not be correct for all geometries.
    void generateToolpaths(std::vector<VariableWidthLines>& generated_toolpaths, bool filter_outermost_central_edges = false);

#ifdef ARACHNE_DEBUG
    // [INTENT] Debug-only: stores a copy of the input outline polygon for SVG visualization.
    // Only available when compiled with ARACHNE_DEBUG defined.
    Polygons outline;
#endif

protected:
    /*!
     * Auxiliary for referencing one transition along an edge which may contain multiple transitions
     */
    // [INTENT] TransitionMidRef is a lightweight handle pointing to a specific transition
    // on a specific edge. Used in dissolveNearbyTransitions() to return a list of transitions
    // that should be removed. The edge pointer + list iterator pair uniquely identifies
    // one TransitionMiddle entry within the edge's transition list.
    // [HAZARD] TransitionMidRef stores a raw edge_t* and a std::list iterator. If the edge
    // is removed from the graph or the transition list is modified between storing and using
    // a TransitionMidRef, the iterator becomes invalid. The code must be careful to only
    // use TransitionMidRef within the same algorithm pass.
    struct TransitionMidRef
    {
        edge_t*                               edge;
        std::list<TransitionMiddle>::iterator transition_it;
        TransitionMidRef(edge_t* edge, std::list<TransitionMiddle>::iterator transition_it) : edge(edge), transition_it(transition_it) {}
    };

    /*!
     * Compute the skeletal trapezoidation decomposition of the input shape.
     *
     * Compute the Voronoi Diagram (VD) and transfer all inside edges into our half-edge (HE) datastructure.
     *
     * The algorithm is currently a bit overcomplicated, because the discretization of parabolic edges is performed at the same time as all
     * edges are being transfered, which means that there is no one-to-one mapping from VD edges to HE edges. Instead we map from a VD edge
     * to the last HE edge. This could be cimplified by recording the edges which should be discretized and discretizing the mafterwards.
     *
     * Another complication arises because the VD uses floating logic, which can result in zero-length segments after rounding to integers.
     * We therefore collapse edges and their whole cells afterwards.
     */
    // [INTENT] Called from the constructor. Performs:
    //   1. Build VD from polygon segments using boost::polygon::construct_voronoi()
    //   2. For each VD edge inside the polygon: create a HE edge in graph_t
    //   3. Discretize parabolic VD edges into multiple linear HE edges during transfer
    //   4. Handle degenerate (zero-length) VD cells by collapsing them
    //   5. Build the vd_edge_to_he_edge and vd_node_to_he_node lookup maps
    //   6. Call separatePointyQuadEndNodes() to fix vertex-cell incidence
    // [HAZARD] The "VD edge to last HE edge" mapping is noted as "overcomplicated" in the
    // docstring. It means that for a parabolic edge discretized into N segments, only the
    // LAST segment is recorded in vd_edge_to_he_edge. Any code that needs to find the first
    // HE segment of a VD edge must traverse the HE graph.
    // [HAZARD] boost::polygon::voronoi uses exact integer arithmetic for input but floating-point
    // for intermediate computations. The resulting Voronoi vertex coordinates are then
    // rounded to integer coord_t values, which can create zero-length or near-zero-length edges.
    // The code handles this by collapsing cells, but corner cases remain (noted: TODO in code).
    void constructFromPolygons(const Polygons& polys);

    /*!
     * mapping each voronoi VD edge to the corresponding halfedge HE edge
     * In case the result segment is discretized, we map the VD edge to the *last* HE edge
     */
    // [STATE] Two lookup maps used during constructFromPolygons() to connect the VD topology
    // to the HE graph topology. Both are ankerl::unordered_dense (O(1) amortized lookup).
    // These maps are ONLY needed during construction — they are not used after constructFromPolygons()
    // returns. A refactored implementation could scope them locally within constructFromPolygons().
    // [MEMORY] These maps store raw pointers (VD edge/vertex pointers) as keys. The VD object
    // is a local variable in constructFromPolygons() — the maps become stale after it returns.
    // This is safe because they are only accessed within constructFromPolygons(), but a
    // refactored implementation should make the lifetime relationship explicit.
    ankerl::unordered_dense::map<const VD::edge_type*, edge_t*>   vd_edge_to_he_edge;
    ankerl::unordered_dense::map<const VD::vertex_type*, node_t*> vd_node_to_he_node;
    node_t&                                                       makeNode(const VD::vertex_type& vd_node,
                                                                           Point p); //!< Get the node which the VD node maps to, or create a new mapping if there wasn't any yet.

    /*!
     * (Eventual) returned 'polylines per index' result (from generateToolpaths):
     */
    // [STATE] Pointer to the output toolpaths vector. Set in generateToolpaths() before
    // calling generateSegments(). Allows addToolpathSegment() to append segments without
    // passing the vector through every recursive call chain.
    // [HAZARD] Raw pointer with no ownership. If generateToolpaths() is called with a
    // vector that goes out of scope during generateSegments() (which shouldn't happen in
    // practice), this would be a dangling pointer.
    std::vector<VariableWidthLines>* p_generated_toolpaths;

    /*!
     * Transfer an edge from the VD to the HE and perform discretization of parabolic edges (and vertex-vertex edges)
     * \p prev_edge serves as input and output. May be null as input.
     */
    // [INTENT] Creates one or more HE edges for a single VD edge. For vertex-vertex edges
    // (straight medial axis segments), creates a single HE edge. For vertex-line edges
    // (parabolic medial axis segments), calls discretize() to get intermediate points,
    // then creates a chain of HE edges.
    // prev_edge is used to link the new edge(s) into the existing HE chain.
    // [HAZARD] The `prev_edge` parameter is modified by the function (output parameter).
    // The caller must preserve and pass it correctly for each consecutive call within
    // the same cell traversal. If a cell is processed non-sequentially (unlikely but possible),
    // the half-edge chain will be incorrectly linked.
    void transferEdge(const Point&                from,
                      const Point&                to,
                      const VD::edge_type&        vd_edge,
                      edge_t*&                    prev_edge,
                      const Point&                start_source_point,
                      const Point&                end_source_point,
                      const std::vector<Segment>& segments);

    /*!
     * Discretize a Voronoi edge that represents the medial axis of a vertex-
     * line region or vertex-vertex region into small segments that can be
     * considered to have a straight medial axis and a linear line width
     * transition.
     *
     * The medial axis between a point and a line is a parabola. The rest of the
     * algorithm doesn't want to have to deal with parabola, so this discretises
     * the parabola into straight line segments. This is necessary if there is a
     * sharp inner corner (acts as a point) that comes close to a straight edge.
     *
     * The medial axis between a point and a point is a straight line segment.
     * However the distance from the medial axis to either of those points draws
     * a parabola as you go along the medial axis. That means that the resulting
     * line width along the medial axis would not be linearly increasing or
     * linearly decreasing, but needs to take the shape of a parabola. Instead,
     * we'll break this edge up into tiny line segments that can approximate the
     * parabola with tiny linear increases or decreases in line width.
     * \param segment The variable-width Voronoi edge to discretize.
     * \param points All vertices of the original Polygons to fill with beads.
     * \param segments All line segments of the original Polygons to fill with
     * beads.
     * \return A number of coordinates along the edge where the edge is broken
     * up into discrete pieces.
     */
    // [INTENT] Samples the parametric parabola or vertex-vertex curve at intervals of
    // discretization_step_size, producing a list of intermediate Points. These points
    // are then used by transferEdge() to create a chain of short linear HE edges.
    // [HAZARD] The discretization step size is set to 0.8mm (from WallToolPaths::generate()).
    // For very tight arcs (small polygon corners), the parabolic edge may be much shorter
    // than 0.8mm, resulting in 0 intermediate points and the edge being treated as a
    // straight line segment. This introduces approximation error in the bead width
    // calculation near sharp corners.
    Points discretize(const VD::edge_type& segment, const std::vector<Segment>& segments);

    /*!
     * For VD cells associated with an input polygon vertex, we need to separate the node at the end and start of the cell into two
     * That way we can reach both the quad_start and the quad_end from the [incident_edge] of the two new nodes
     * Otherwise if node.incident_edge = quad_start you couldnt reach quad_end.twin by normal iteration (i.e. it = it.twin.next)
     */
    // [INTENT] Fixes a topological issue in Voronoi cells around polygon vertices (sharp corners).
    // When the VD has a vertex-vertex cell (the cell is the region equidistant from two polygon
    // vertices), the HE representation needs two distinct nodes at the cell boundaries — one
    // reachable from the "start" edge and one from the "end" edge. Without this fix, certain
    // cells would have only one reachable node, breaking the cell traversal in generateToolpaths().
    // [HAZARD] This function modifies the graph structure (creates new nodes, remaps incident edges).
    // It must be called exactly once, after constructFromPolygons() builds the initial graph but
    // before any algorithm phases that depend on graph topology. If called twice, it will create
    // duplicate nodes.
    void separatePointyQuadEndNodes();

    // ^ init | v transitioning

    // [INTENT] Updates the `is_central` flag on each HE edge based on whether the angle
    // subtended by the edge at the polygon boundary meets the transitioning_angle threshold.
    // Central edges form the medial axis (skeleton) of the polygon. Non-central edges are the
    // "rib" edges connecting the skeleton to the polygon boundary.
    // The transitioning_angle parameter (from WallToolPathsParams::wall_transition_angle) controls
    // how "pointy" a feature needs to be before we treat it as central. Lower angle = more edges
    // are marked central = more complex toolpaths.
    void updateIsCentral(); // Update the "is_central" flag for each edge based on the transitioning_angle

    /*!
     * Filter out small central areas.
     *
     * Only used to get rid of small edges which get marked as central because
     * of rounding errors because the region is so small.
     */
    // [INTENT] Removes central markings from edges that form tiny central regions
    // (shorter than central_filter_dist = 20 microns). These small central regions
    // typically arise from Voronoi rounding errors at near-collinear polygon vertices.
    // Without this filter, tiny central regions would generate spurious bead-count
    // transitions and produce extrusion artifacts.
    // [HAZARD] max_length is passed as central_filter_dist() = 0.02mm. This is a global
    // constant, not user-configurable. For very small parts (< 1mm), even legitimate
    // central regions may be shorter than 20 microns and get filtered out.
    void filterCentral(coord_t max_length);

    /*!
     * Filter central areas connected to starting_edge recursively.
     * \return Whether we should unmark this section marked as central, on the
     * way back out of the recursion.
     */
    // [INTENT] Recursive subroutine for filterCentral(coord_t). Traverses the graph
    // from starting_edge, accumulating traveled_dist. If the entire traversal stays
    // within max_length (the region is "tiny"), returns true to trigger unmarking.
    // [HAZARD] Recursive traversal with no depth limit. For a long thin central region,
    // the traversal terminates early (traveled_dist > max_length returns false), so
    // stack depth is bounded by max_length / min_edge_length. For max_length = 20 microns,
    // this is at most a few edges. Safe in practice.
    bool filterCentral(edge_t* starting_edge, coord_t traveled_dist, coord_t max_length);

    /*!
     * Unmark the outermost edges directly connected to the outline, as not
     * being central.
     *
     * Only used to emulate some related literature.
     *
     * The paper shows that this function is bad for the stability of the framework.
     */
    // [INTENT] Experimental function — the code comment explicitly notes that "the paper
    // shows that this function is bad for the stability of the framework." It is only
    // called when generateToolpaths(filter_outermost_central_edges=true).
    // [HAZARD] This function is documented as destabilizing. It should never be used in
    // production (the default is false). Its presence is historical — it was added to
    // replicate behavior from a related paper. A refactored implementation should remove
    // or clearly gate this function.
    void filterOuterCentral();

    /*!
     * Set bead count in central regions based on the optimal_bead_count of the
     * beading strategy.
     */
    // [INTENT] For each central edge, queries beading_strategy.optimal_bead_count(2*R)
    // (where R = edge's distance-to-boundary) to determine how many walls fit at that width.
    // Sets the bead count on the corresponding SkeletalTrapezoidationEdge.
    // Non-central edges have their bead count set to the count of the adjacent central edge.
    void updateBeadCount();

    /*!
     * Add central regions and set bead counts where there is an end of the
     * central area and when traveling upward we get to another region with the
     * same bead count.
     */
    // [INTENT] Handles a specific topology: when the skeleton has a "local minimum" region —
    // where a non-central branch ends at the same bead count as the central region it connects to.
    // In this case, the non-central branch should actually be treated as central (same bead count
    // = same visual width behavior). This function promotes such regions to central status.
    // [HAZARD] Delegates to recursive filterNoncentralRegions(edge*, ...). Recursive traversal
    // with traveled_dist accumulation limited by max_dist. For typical inputs, recursion depth
    // is bounded, but complex skeletal graphs may have long non-central branches.
    void filterNoncentralRegions();

    /*!
     * Add central regions and set bead counts for a particular edge and all of
     * its adjacent edges.
     *
     * Recursive subroutine for \ref filterNoncentralRegions().
     * \return Whether to set the bead count on the way back
     */
    bool filterNoncentralRegions(edge_t* to_edge, coord_t bead_count, coord_t traveled_dist, coord_t max_dist);

    /*!
     * Generate middle points of all transitions on edges.
     *
     * The transition middle points are saved in the graph itself. They are also
     * returned via the output parameter.
     * \param[out] edge_transitions A list of transitions that were generated.
     */
    // [INTENT] Iterates all central edges where adjacent edges have different bead counts.
    // At each such boundary, computes the "transition midpoint" — the R value along the edge
    // where the bead count changes (i.e., where the wall appears or disappears).
    // The midpoint is computed analytically from the bead strategy's bead-count boundaries.
    // Results are stored both on the edge (edge->data.transitions) and returned via out-param.
    // [HAZARD] edge_transitions is a vector of shared_ptr<list<TransitionMiddle>>, one per edge.
    // The list pointers are shared with the edge's own transition list. Modifying the edge's
    // list without going through the shared_ptr (e.g., via the graph directly) would create
    // inconsistency between the two references.
    void generateTransitionMids(ptr_vector_t<std::list<TransitionMiddle>>& edge_transitions);

    /*!
     * Removes some transition middle points.
     *
     * Transitions can be removed if there are multiple intersecting transitions
     * that are too close together. If transitions have opposite effects, both
     * are removed.
     */
    // [INTENT] Filters out transition midpoints that are closer than transition_filter_dist.
    // Two transitions within transition_filter_dist of each other are candidates for merging
    // or cancellation. If one adds a bead and the other removes the same bead (opposite effect),
    // both are removed (they cancel). If they have the same direction, they are merged (one removed).
    // [HAZARD] transition_filter_dist comes from WallToolPaths::generate() where it is hardcoded
    // to 100mm. This is extremely large — for typical geometry (< 200mm), most transitions will
    // be within the filter range and thus filtered. This may explain why OrcaSlicer's Arachne
    // produces "smoother" toolpaths than CuraEngine but with potentially less accurate width control.
    void filterTransitionMids();

    /*!
     * Merge transitions that are too close together.
     * \param edge_to_start Edge pointing to the node from which to start
     * traveling in all directions except along \p edge_to_start .
     * \param origin_transition The transition for which we are checking nearby
     * transitions.
     * \param traveled_dist The distance traveled before we came to
     * \p edge_to_start.to .
     * \param going_up Whether we are traveling in the upward direction as seen
     * from the \p origin_transition. If this doesn't align with the direction
     * according to the R diff on a consecutive edge we know there was a local
     * optimum.
     * \return Whether the origin transition should be dissolved.
     */
    // [INTENT] Recursive function that walks the graph from edge_to_start searching
    // for other transitions within max_dist. Returns a list of transitions (as TransitionMidRef)
    // that should be dissolved. Called by filterTransitionMids() for each transition.
    // going_up indicates whether we're traversing toward higher R values (away from polygon boundary).
    // [HAZARD] Recursive traversal. traveled_dist bounds recursion (stops at max_dist).
    // max_dist = transition_filter_dist = 100mm, which is large. For a densely-transitioned
    // polygon (many bead count changes in a small area), this function may traverse thousands
    // of edges before stopping, and then traverse them again for each new origin transition.
    // Overall complexity: O(E × T) where E = edges, T = transitions per edge.
    std::list<TransitionMidRef> dissolveNearbyTransitions(
        edge_t* edge_to_start, TransitionMiddle& origin_transition, coord_t traveled_dist, coord_t max_dist, bool going_up);

    /*!
     * Spread a certain bead count over a region in the graph.
     * \param edge_to_start One edge of the region to spread the bead count in.
     * \param from_bead_count All edges with this bead count will be changed.
     * \param to_bead_count The new bead count for those edges.
     */
    // [INTENT] BFS/DFS flood-fill of a bead count change through the graph. Starts at
    // edge_to_start and propagates to all connected edges that have `from_bead_count`,
    // changing them to `to_bead_count`. Used during bead count dissolution after filtering.
    void dissolveBeadCountRegion(edge_t* edge_to_start, coord_t from_bead_count, coord_t to_bead_count);

    /*!
     * Change the bead count if the given edge is at the end of a central
     * region.
     *
     * This is necessary to provide a transitioning bead count to the edges of a
     * central region to transition more smoothly from a high bead count in the
     * central region to a lower bead count at the edge.
     * \param edge_to_start One edge from a zone that needs to be filtered.
     * \param traveled_dist The distance along the edges we've traveled so far.
     * \param max_distance Don't filter beyond this range.
     * \param replacing_bead_count The new bead count for this region.
     * \return ``true`` if the bead count of this edge was changed.
     */
    bool filterEndOfCentralTransition(edge_t* edge_to_start, coord_t traveled_dist, coord_t max_dist, coord_t replacing_bead_count);

    /*!
     * Generate the endpoints of all transitions for all edges in the graph.
     * \param[out] edge_transition_ends The resulting transition endpoints.
     */
    // [INTENT] For each TransitionMiddle already placed on edges, computes the two
    // TransitionEnd points (one on each side of the middle). A TransitionEnd marks
    // the "end of the transition zone" — where the extra/removed bead starts/stops.
    // Results are stored on each edge and returned via out-param.
    void generateAllTransitionEnds(ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);

    /*!
     * Also set the rest values at nodes in between the transition ends
     */
    // [INTENT] After transition ends are generated, sets the `rest` value (fractional
    // bead width) at intermediate nodes between the transition start and end points.
    // These rest values are later used by generateJunctions() to compute the actual
    // bead widths at the ExtrusisonJunctions.
    void applyTransitions(ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);

    /*!
     * Create extra edges along all edges, where it needs to transition from one
     * bead count to another.
     *
     * For example, if an edge of the graph goes from a bead count of 6 to a
     * bead count of 1, it needs to generate 5 places where the beads around
     * this line transition to a lower bead count. These are the "ribs". They
     * reach from the edge to the border of the polygon. Where the beads hit
     * those ribs the beads know to make a transition.
     */
    // [INTENT] "Transitioning ribs" are extra HE edges inserted perpendicular to the
    // medial axis at each TransitionEnd point. They represent the physical location where
    // one bead's extrusion path terminates (at the rib junction). Without ribs, the
    // algorithm would have no way to generate a correct endpoint for a disappearing bead.
    // This is one of the most complex steps — it modifies the graph topology by inserting
    // new edges and nodes.
    // [HAZARD] Graph modification during traversal. generateTransitioningRibs() creates new
    // nodes and edges while iterating over the existing graph. The code relies on std::list
    // stability (iterators into list<edge_t> remain valid under insertion). If edge_t storage
    // were changed to std::vector, this would cause iterator invalidation.
    void generateTransitioningRibs();

    /*!
     * Generate the endpoints of a specific transition midpoint.
     */
    void generateTransitionEnds(edge_t&                                 edge,
                                coord_t                                 mid_R,
                                coord_t                                 transition_lower_bead_count,
                                ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);

    /*!
     * Compute a single endpoint of a transition.
     */
    // [INTENT] Computes one endpoint of a transition zone. Returns whether the edge is
    // going "downward" (toward thinner polygon = lower R). Used to correctly orient the
    // endpoint relative to the midpoint.
    bool generateTransitionEnd(edge_t&                                 edge,
                               coord_t                                 start_pos,
                               coord_t                                 end_pos,
                               coord_t                                 transition_half_length,
                               double                                  start_rest,
                               double                                  end_rest,
                               coord_t                                 transition_lower_bead_count,
                               ptr_vector_t<std::list<TransitionEnd>>& edge_transition_ends);

    /*!
     * Determines whether an edge is going downwards or upwards in the graph.
     */
    // [INTENT] Checks whether `outgoing` leads toward smaller R (downward = toward boundary).
    // Recursive: if the immediate edge doesn't clearly go up or down (ambiguous R ordering),
    // recurse into adjacent edges.
    // [HAZARD] Recursive with traveled_dist bound. Similar to dissolveNearbyTransitions,
    // recursion depth is bounded by transition_half_length / min_edge_length. For typical
    // values (transition_length ≈ 0.5–2mm, min edge ≈ 0.02mm), depth up to ~100 is possible.
    bool isGoingDown(edge_t* outgoing, coord_t traveled_dist, coord_t transition_half_length, coord_t lower_bead_count) const;

    /*!
     * Determines whether this edge marks the end of the central region.
     */
    // [INTENT] Returns true if `edge` goes from a central region (is_central=true) to a
    // non-central region (is_central=false). Used to identify "exit points" of the skeleton
    // where beads transition from the medial axis back out to the polygon boundary.
    bool isEndOfCentral(const edge_t& edge) const;

    /*!
     * Create extra ribs in the graph where the graph contains a parabolic arc
     * or a straight between two inner corners.
     */
    // [INTENT] At each local maximum of R on the skeleton (a "peak" where the polygon is
    // locally widest), if the beading strategy has multiple beads at that width but the
    // skeleton is a single node, we need extra rib edges to correctly represent all bead
    // endpoints. generateExtraRibs() inserts these extra edges.
    // This handles the case of, e.g., a circle-shaped polygon where the center is equidistant
    // from all boundary points — a single Voronoi vertex with high R that needs many beads.
    void generateExtraRibs();

    // ^ transitioning ^

    // v toolpath generation v

    /*!
     * \param[out] segments the generated segments
     */
    // [INTENT] Main toolpath generation function. Runs all sub-phases:
    //   1. Collect all "inner" edges (medial axis edges not directly on the polygon boundary)
    //   2. Sort by R value (highest R first = outermost skeleton first)
    //   3. propagateBeadingsUpward() — from low-R nodes toward high-R nodes
    //   4. propagateBeadingsDownward() — from high-R nodes toward low-R nodes
    //   5. generateJunctions() — place ExtrusisonJunction points on each edge
    //   6. connectJunctions() — connect junctions to form ExtrusisonLines
    //   7. generateLocalMaximaSingleBeads() — special handling for local maximum single-bead nodes
    // Output via p_generated_toolpaths (set by generateToolpaths() before calling this).
    void generateSegments();

    /*!
     * From a quad (a group of linked edges in one cell of the Voronoi), find
     * the edge pointing to the node that is furthest away from the border of the polygon.
     */
    // [INTENT] A "quad" in Arachne terminology is the HE subgraph corresponding to one
    // Voronoi cell. getQuadMaxRedgeTo() finds the HE edge within this quad whose destination
    // node has the maximum R value (furthest from the polygon boundary = on the skeleton).
    // Used during upward/downward propagation to find the "top" of each quad.
    edge_t* getQuadMaxRedgeTo(edge_t* quad_start_edge);

    /*!
     * Propagate beading information from nodes that are closer to the edge
     * (low radius R) to nodes that are farther from the edge (high R).
     */
    // [INTENT] Phase 1 of bead propagation. Traverses edges sorted by R from low to high.
    // For each edge without a beading, copies the beading from the lower-R end toward the
    // higher-R end. This effectively "seeds" beadings at local minima (polygon corners),
    // allowing them to propagate inward toward the skeleton.
    // Results stored in node_beadings: a shared_ptr<BeadingPropagation> per node.
    // [HAZARD] upward_quad_mids is expected to be pre-sorted by R (highest first). If the
    // sorting contract is violated, propagation will produce incorrect bead assignments.
    void propagateBeadingsUpward(std::vector<edge_t*>& upward_quad_mids, ptr_vector_t<BeadingPropagation>& node_beadings);

    /*!
     * propagate beading info from higher R nodes to lower R nodes
     */
    // [INTENT] Phase 2 of bead propagation. Traverses edges sorted by R from high to low.
    // Propagates beadings from skeleton nodes outward toward the polygon boundary.
    // If both an upward-propagated and a downward-propagated beading reach the same node,
    // they are merged via interpolate() using distance-weighted blending.
    // [HAZARD] The merge of upward and downward beadings at a node involves an interpolation
    // whose ratio depends on beading_propagation_transition_dist (= wall_transition_length).
    // If this distance is 0 or very small, one beading completely overwrites the other.
    // If very large, the two beadings are merged over a long distance, creating a long
    // gradual width transition which may look unnatural.
    void propagateBeadingsDownward(std::vector<edge_t*>& upward_quad_mids, ptr_vector_t<BeadingPropagation>& node_beadings);

    /*!
     * Subroutine of propagateBeadingsDownward
     */
    void propagateBeadingsDownward(edge_t* edge_to_peak, ptr_vector_t<BeadingPropagation>& node_beadings);

    /*!
     * Find a beading in between two other beadings.
     */
    // [INTENT] Creates an interpolated Beading at position `ratio_left_to_whole` between
    // `left` and `right` beadings. If the two beadings have different bead counts, uses
    // `switching_radius` to determine where to switch from the left bead count to the right.
    // This is the key function for smooth width transitions between bead-count regions.
    // [HAZARD] The interpolation constructs a new Beading by value. For beadings with many
    // beads (large wall count), each interpolate() call allocates a new vector of bead widths
    // and toolpath widths. In a dense bead mesh (many nodes), this can produce many allocations.
    Beading interpolate(const Beading& left, double ratio_left_to_whole, const Beading& right, coord_t switching_radius) const;

    /*!
     * Subroutine of interpolate(const Beading&, Ratio, const Beading&, coord_t)
     * Assumes same number of beads.
     */
    Beading interpolate(const Beading& left, double ratio_left_to_whole, const Beading& right) const;

    /*!
     * Get the beading at a certain node of the skeletal graph, or create one if
     * it doesn't have one yet.
     */
    // [INTENT] Lazy accessor for node beadings. If the node doesn't have a beading yet,
    // creates one by querying beading_strategy.compute(2*R, bead_count) and stores it
    // in node_beadings. The created beading is also linked from node->data.beading.
    // [HAZARD] node_beadings uses shared_ptr for ownership, but node->data.beading is a
    // raw pointer (or weak reference). If node_beadings is cleared before the node's
    // raw beading pointer is used, this creates a use-after-free. The code avoids this
    // by ensuring node_beadings outlives all uses of beading pointers within generateSegments().
    std::shared_ptr<BeadingPropagation> getOrCreateBeading(node_t* node, ptr_vector_t<BeadingPropagation>& node_beadings);

    /*!
     * In case we cannot find the beading of a node, get a beading from the nearest node.
     */
    // [INTENT] Fallback for orphaned nodes (nodes that haven't received a beading via
    // propagation). Searches outward from `node` up to `max_dist`, returning the beading
    // of the nearest node that has one. Returns nullptr if no beading is found within range.
    // [HAZARD] If nullptr is returned and the caller doesn't handle it, this can cause
    // a null pointer dereference in generateJunctions(). The code checks for nullptr after
    // each getNearestBeading() call.
    std::shared_ptr<BeadingPropagation> getNearestBeading(node_t* node, coord_t max_dist);

    /*!
     * generate junctions for each bone
     * \param edge_to_junctions junctions ordered high R to low R
     */
    // [INTENT] For each HE edge, computes the ExtrusisonJunction points where the beads
    // intersect the edge. For an edge connecting R=3mm to R=1mm with bead widths 0.5mm,
    // this places junctions at R=2.75mm, R=2.25mm, R=1.75mm, R=1.25mm along the edge
    // (one per bead, positioned at their center-line radius).
    // Results stored in edge_junctions: one LineJunctions (vector<ExtrusionJunction>) per edge.
    void generateJunctions(ptr_vector_t<BeadingPropagation>& node_beadings, ptr_vector_t<LineJunctions>& edge_junctions);

    /*!
     * Add a new toolpath segment, defined between two extrusion-juntions.
     */
    // [INTENT] Appends a new ExtrusionLine segment to p_generated_toolpaths.
    // If force_new_path=false and there's an existing ExtrusisonLine ending at `from`,
    // the new segment is appended to that line (avoiding orphan single-segment lines).
    // from_is_3way / to_is_3way flags indicate whether the junction is a branching point
    // (3 lines meeting), which requires force_new_path=true to avoid incorrect path merging.
    // [HAZARD] Path reuse logic (appending to an existing line) searches backward through
    // p_generated_toolpaths for a matching line endpoint. This search is O(N_segments) per
    // new segment in the worst case. For complex multi-wall parts, this becomes O(N²) overall.
    void addToolpathSegment(
        const ExtrusionJunction& from, const ExtrusionJunction& to, bool is_odd, bool force_new_path, bool from_is_3way, bool to_is_3way);

    /*!
     * connect junctions in each quad
     */
    // [INTENT] For each Voronoi cell (quad), iterates the cell's HE edges in order and
    // connects the corresponding ExtrusisonJunctions into ExtrusisonLine segments.
    // Uses addToolpathSegment() for each pair of adjacent junctions along an edge.
    // Handles "is_odd" detection (whether this is a gap-filling transition line) and
    // "is_3way" detection (whether junctions branch into three directions).
    void connectJunctions(ptr_vector_t<LineJunctions>& edge_junctions);

    /*!
     * Genrate small segments for local maxima where the beading would only result in a single bead
     */
    // [INTENT] Special case for polygon local maxima (widest points). If a skeleton node
    // is a local maximum and the beading strategy produces only 1 bead at that radius,
    // the node needs a tiny closed loop segment rather than a connection to other junctions.
    // Without this, single-bead local maxima would produce orphaned junction points with
    // no connecting extrusion line.
    void generateLocalMaximaSingleBeads();
};

} // namespace Slic3r::Arachne
#endif // VORONOI_QUADRILATERALIZATION_H
