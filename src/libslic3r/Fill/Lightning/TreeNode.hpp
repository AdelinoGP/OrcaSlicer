// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Declares the Node class: the fundamental vertex of a Lightning Infill tree.
// Each Node is a 2D point (m_p) connected to a parent (weak_ptr) and zero or more children
// (shared_ptr). Together the nodes form a tree rooted at the infill-outline boundary, with leaves
// pointing inward toward unsupported regions.
//
// Key design choices:
//   1. Shared ownership via shared_ptr (NodeSPtr) — trees can be deep-copied and nodes rerooted
//      without invalidating existing shared_ptr handles.
//   2. enable_shared_from_this — allows nodes to return shared_ptr<this> from member functions.
//   3. Protected constructor + static create() factory — required to use make_shared correctly
//      with enable_shared_from_this (the 'EnableMakeShared' struct workaround).
//   4. weak_ptr parent — breaks the reference cycle that would form if parent were a shared_ptr.
//
// [COUPLING] Depends on EdgeGrid::Grid (for realign's collision checks), Polygon.hpp (Point,
//            Polygons, Polylines), and SVG.hpp (debug output). Free functions inside() and
//            lineSegmentPolygonsIntersection() are also declared here and used by Layer.cpp.
// [MEMORY] The Node tree is fully RAII: when the last shared_ptr to a root is dropped, the entire
//          subtree is destroyed through the shared_ptr chain. Weak parent pointers do NOT prevent
//          destruction; a node's parent must always outlive it in practice.

#ifndef LIGHTNING_TREE_NODE_H
#define LIGHTNING_TREE_NODE_H

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "../../EdgeGrid.hpp"
#include "../../Polygon.hpp"
#include "SVG.hpp"

// #define LIGHTNING_TREE_NODE_DEBUG_OUTPUT

namespace Slic3r::FillLightning {

// [INTENT] Cell size for the SparseNodeGrid lookup grid used in getBestGroundingLocation().
// 4 mm in integer (scaled) units. Chosen so each grid cell is small enough for good spatial
// resolution but large enough to keep grid memory manageable.
// [HAZARD H300] This is a free function returning a constant, not a constexpr or static member.
//               Repeated calls add function-call overhead in the inner loop. Inlining is
//               expected by the compiler but not guaranteed on all platforms.
inline coord_t locator_cell_size() { return scaled<coord_t>(4.); }

class Node;

using NodeSPtr = std::shared_ptr<Node>;

// NOTE: As written, this struct will only be valid for a single layer, will have to be updated for the next.
// NOTE: Reasons for implementing this with some separate closures:
//       - keep clear deliniation during development
//       - possibility of multiple distance field strategies

/*!
 * A single vertex of a Lightning Tree, the structure that determines the paths
 * to be printed to form Lightning Infill.
 *
 * In essence these vertices are just a position linked to other positions in
 * 2D. The nodes have a hierarchical structure of parents and children, forming
 * a tree. The class also has some helper functions specific to Lightning Infill
 * e.g. to straighten the paths around this node.
 */
// [INTENT] Node uses enable_shared_from_this so member functions can safely produce a
// shared_ptr to themselves (e.g. addChild, visitNodes, hasOffspring).
// [MEMORY] All instances must be created via Node::create() — direct construction bypasses
//          the shared_ptr control block, making shared_from_this() invalid.
// [HAZARD H301] If any code constructs a Node on the stack or via raw 'new' without wrapping
//               in a shared_ptr, calling shared_from_this() produces UB (throws std::bad_weak_ptr
//               in C++17). The protected constructor makes this unlikely but not impossible via
//               the EnableMakeShared trick.
class Node : public std::enable_shared_from_this<Node>
{
public:
    // Workaround for private/protected constructors and 'make_shared': https://stackoverflow.com/a/27832765
    // [INTENT] The EnableMakeShared struct inherits Node and exposes its constructor publicly,
    // allowing make_shared<EnableMakeShared>() to call the protected Node constructor while still
    // placing the control block adjacent to the object for cache efficiency.
    // [HAZARD H301] Port note: this pattern is C++ specific. Other languages that support
    // factory-only construction (Rust: private new(), Java: private constructor + static factory)
    // should replicate the intent: no public direct construction.
    template<typename... Arg> NodeSPtr static create(Arg&&... arg)
    {
        struct EnableMakeShared : public Node
        {
            explicit EnableMakeShared(Arg&&... arg) : Node(std::forward<Arg>(arg)...) {}
        };
        return std::make_shared<EnableMakeShared>(std::forward<Arg>(arg)...);
    }

    /*!
     * Get the position on this layer that this node represents, a vertex of the
     * path to print.
     * \return The position that this node represents.
     */
    const Point& getLocation() const { return m_p; }

    /*!
     * Change the position on this layer that the node represents.
     * \param p The position that the node needs to represent.
     */
    void setLocation(const Point& p) { m_p = p; }

    /*!
     * Construct a new ``Node`` instance and add it as a child of
     * this node.
     * \param p The location of the new node.
     * \return A shared pointer to the new node.
     */
    // [STATE] Mutates m_children and sets new_child->m_parent = this.
    // [MEMORY] Creates a new Node via Node::create(); caller receives a shared_ptr.
    NodeSPtr addChild(const Point& p);

    /*!
     * Add an existing ``Node`` as a child of this node.
     * \param new_child The node that must be added as a child.
     * \return Always returns \p new_child.
     */
    // [STATE] Pushes new_child into m_children, sets new_child->m_parent and m_is_root = false.
    // [HAZARD H302] No cycle check: if new_child is an ancestor of this node, a cycle is formed.
    //               Callers (Layer::attach, reconnectRoots) assert no cycles via hasOffspring()
    //               but only in debug builds.
    NodeSPtr addChild(NodeSPtr& new_child);

    /*!
     * Propagate this node's sub-tree to the next layer.
     *
     * Creates a copy of this tree, realign it to the new layer boundaries
     * \p next_outlines and reduce (i.e. prune and straighten) it. A copy of
     * this node and all of its descendant nodes will be added to the
     * \p next_trees vector.
     * \param next_trees A collection of tree nodes to use for the next layer.
     * \param next_outlines The shape of the layer below, to make sure that the
     * tree stays within the bounds of the infill area.
     * \param prune_distance The maximum distance that a leaf node may be moved
     * such that it still supports the current node.
     * \param smooth_magnitude The maximum distance that a line may be shifted
     * to straighten the tree's paths, such that it still supports the current
     * paths.
     * \param max_remove_colinear_dist The maximum distance of a line-segment
     * from which straightening may remove a colinear point.
     */
    // [INTENT] Creates a deep copy of this tree, then applies prune → straighten → realign.
    // prune_distance = m_prune_length (45° angle-derived from supporting_radius).
    // smooth_magnitude = m_straightening_max_distance (also 45° angle-derived).
    // Modifies next_trees by pushing realigned subtrees; does NOT modify 'this'.
    // [STATE] Read-only on 'this'; mutates next_trees.
    // [MEMORY] deepCopy() allocates a new subtree of NodeSPtr instances. The copy is fully owned
    //          by next_trees after this call; this node's subtree is unchanged.
    void propagateToNextLayer(std::vector<NodeSPtr>& next_trees,
                              const Polygons&        next_outlines,
                              const EdgeGrid::Grid&  outline_locator,
                              coord_t                prune_distance,
                              coord_t                smooth_magnitude,
                              coord_t                max_remove_colinear_dist) const;

    /*!
     * Executes a given function for every line segment in this node's sub-tree.
     *
     * The function takes two `Point` arguments. These arguments will be filled
     * in with the higher-order node (closer to the root) first, and the
     * downtree node (closer to the leaves) as the second argument. The segment
     * from this node's parent to this node itself is not included.
     * The order in which the segments are visited is depth-first.
     * \param visitor A function to execute for every branch in the node's sub-
     * tree.
     */
    // [INTENT] DFS branch visitor. Skips the root→this segment; only visits this→child segments.
    // Used for debugging / SVG export.
    void visitBranches(const std::function<void(const Point&, const Point&)>& visitor) const;

    /*!
     * Execute a given function for every node in this node's sub-tree.
     *
     * The visitor function takes a node as input. This node is not const, so
     * this can be used to change the tree.
     * Nodes are visited in depth-first order. This node itself is visited as
     * well (pre-order).
     * \param visitor A function to execute for every node in this node's sub-
     * tree.
     */
    // [INTENT] DFS node visitor (pre-order, includes 'this'). Used by fillLocator() to populate
    // the SparseNodeGrid with all nodes in a tree.
    // [HAZARD H303] Recursive DFS. For very deep/linear trees (degenerate single-child chains),
    //               stack depth equals tree depth. In extreme cases this may overflow the call stack.
    //               Typical Lightning trees are shallow (< 100 levels) so this is benign in practice.
    void visitNodes(const std::function<void(NodeSPtr)>& visitor);

    /*!
     * Get a weighted distance from an unsupported point to this node (given the current supporting radius).
     *
     * When attaching a unsupported location to a node, not all nodes have the same priority.
     * (Eucludian) closer nodes are prioritised, but that's not the whole story.
     * For instance, we give some nodes a 'valence boost' depending on the nr. of branches.
     * \param unsupported_location The (unsuppported) location of which the weighted distance needs to be calculated.
     * \param supporting_radius The maximum distance which can be bridged without (infill) supporting it.
     * \return The weighted distance.
     */
    // [INTENT] Valence boost: nodes with 1–3 children get a negative boost (lower weight = higher
    // priority) of 4 × supporting_radius. This encourages "bushy" trees and discourages long single
    // chains. Valence = parent_count (0 or 1) + child_count.
    // [HAZARD H304] valence_boost_multiplier = 4 and max_valence_for_boost = 4 are hardcoded
    //               constants; not configurable by the user. A port must preserve these exact values.
    coord_t getWeightedDistance(const Point& unsupported_location, const coord_t& supporting_radius) const;

    /*!
     * Returns whether this node is the root of a lightning tree. It is the root
     * if it has no parents.
     * \return ``true`` if this node is the root (no parents) or ``false`` if it
     * is a child node of some other node.
     */
    bool isRoot() const { return m_is_root; }

    /*!
     * Reverse the parent-child relationship all the way to the root, from this node onward.
     * This has the effect of 're-rooting' the tree at the current node if no immediate parent is given as argument.
     * That is, the current node will become the root, it's (former) parent if any, will become one of it's children.
     * This is then recursively bubbled up until it reaches the (former) root, which then will become a leaf.
     * \param new_parent The (new) parent-node of the root, useful for recursing or immediately attaching the node to another tree.
     */
    // [INTENT] reroot() is the parent-child reversal operation used when grafting disconnected
    // roots onto a new boundary point or onto another tree. It walks up to the old root,
    // reversing parent→child relationships at each step.
    // [STATE] Mutates m_is_root, m_parent, m_children throughout the chain.
    // [HAZARD H305] reroot() is O(depth) recursive. If called on the middle of a deep tree,
    //               every ancestor is visited. Works correctly but deeply nested call stacks for
    //               very linear trees could overflow. Same concern as H303.
    void reroot(const NodeSPtr& new_parent = nullptr);

    /*!
     * Retrieves the closest node to the specified location.
     * \param loc The specified location.
     * \result The branch that starts at the position closest to the location within this tree.
     */
    // [INTENT] DFS search for the node whose m_p is closest to 'loc' (Euclidean, not weighted).
    // Returns shared_ptr to the closest node. Used in reconnectRoots().
    // [HAZARD H306] O(N_nodes) linear scan. For large trees this is a hotspot. No spatial index.
    NodeSPtr closestNode(const Point& loc);

    /*!
     * Returns whether the given tree node is a descendant of this node.
     *
     * If this node itself is given, it is also considered to be a descendant.
     * \param to_be_checked A node to find out whether it is a descendant of
     * this node.
     * \return ``true`` if the given node is a descendant or this node itself,
     * or ``false`` if it is not in the sub-tree.
     */
    // [INTENT] DFS membership test: returns true if to_be_checked is this or any descendant.
    // Used in getBestGroundingLocation() to prevent a tree from being grafted onto itself.
    // [HAZARD H303] Recursive DFS — same stack depth concern as visitNodes.
    bool hasOffspring(const NodeSPtr& to_be_checked) const;

    Node() = delete; // Don't allow empty contruction

protected:
    /*!
     * Construct a new node, either for insertion in a tree or as root.
     * \param p The physical location in the 2D layer that this node represents.
     * Connecting other nodes to this node indicates that a line segment should
     * be drawn between those two physical positions.
     */
    // [INTENT] protected constructor — all external callers must use Node::create().
    // last_grounding_location: only set on root nodes that were boundary-grounded; stored for
    // reconnectRoots() to use as the direction hint when snapping to the new layer outline.
    explicit Node(const Point& p, const std::optional<Point>& last_grounding_location = std::nullopt);

    /*!
     * Copy this node and its entire sub-tree.
     * \return The equivalent of this node in the copy (the root of the new sub-
     * tree).
     */
    // [INTENT] Deep recursive copy of the entire subtree rooted at 'this'. Parent pointers in the
    // copy are set to the new nodes (not the originals). Used by propagateToNextLayer().
    // [MEMORY] Allocates O(N_nodes) new NodeSPtr instances. All ownership flows from the returned root.
    // [HAZARD H307] Recursive — same stack depth concern for deep trees.
    NodeSPtr deepCopy() const;

    /*! Reconnect trees from the layer above to the new outlines of the lower layer.
     * \return Wether or not the root is kept (false is no, true is yes).
     */
    // [INTENT] After a deepCopy(), check if 'this' (the root) is inside the new layer outlines.
    //   - If inside: recursively realign children; any child that needs crossing the boundary is
    //     promoted to a new root in rerooted_parts (its m_last_grounding_location is set to the
    //     old parent's position for use in next reconnectRoots()).
    //   - If outside: promote all inside children to rerooted_parts and return false (discard this).
    // [STATE] May erase entries from m_children; appends to rerooted_parts.
    // [HAZARD H307] Recursive DFS — same stack concern.
    bool realign(const Polygons& outlines, const EdgeGrid::Grid& outline_locator, std::vector<NodeSPtr>& rerooted_parts);

    // [INTENT] RectilinearJunction is an intermediate return type for the straighten() recursion:
    //   total_recti_dist = rectilinear path length from the junction above to this junction below.
    //   junction_loc = the physical position of the next junction below.
    // Only used internally by the two overloads of straighten().
    struct RectilinearJunction
    {
        coord_t total_recti_dist; //!< rectilinear distance along the tree from the last junction above to the junction below
        Point   junction_loc;     //!< junction location below
    };

    /*!
     * Smoothen the tree to make it a bit more printable, while still supporting
     * the trees above.
     * \param magnitude The maximum allowed distance to move the node.
     * \param max_remove_colinear_dist Maximum distance of the (compound) line-segment from which a co-linear point may be removed.
     */
    // [INTENT] Public entry point for tree straightening. Delegates to the recursive overload.
    // Converts max_remove_colinear_dist from coord_t to int64_t squared for the recursion.
    void straighten(coord_t magnitude, coord_t max_remove_colinear_dist);

    /*! Recursive part of \ref straighten(.)
     * \param junction_above The last seen junction with multiple children above
     * \param accumulated_dist The distance along the tree from the last seen junction to this node
     * \param max_remove_colinear_dist2 Maximum distance _squared_ of the (compound) line-segment from which a co-linear point may be removed.
     * \return the total distance along the tree from the last junction above to the first next junction below and the location of the next
     * junction below
     */
    // [INTENT] Straighten single-child chains by interpolating nodes toward the ideal straight line
    // from junction_above to the next junction below. Also removes nearly-collinear nodes.
    // For multi-child nodes (junctions): nudge the junction toward the centroid of incoming directions.
    // [HAZARD H307] Recursive; same stack concern.
    // [HAZARD H308] Floating-point/integer mix: positions are adjusted via double normalisation then
    //               cast back to coord_t. Rounding errors accumulate over many straighten passes.
    RectilinearJunction straighten(coord_t      magnitude,
                                   const Point& junction_above,
                                   coord_t      accumulated_dist,
                                   int64_t      max_remove_colinear_dist2);

    /*! Prune the tree from the extremeties (leaf-nodes) until the pruning distance is reached.
     * \return The distance that has been pruned. If less than \p distance, then the whole tree was puned away.
     */
    // [INTENT] Remove leaf nodes and shorten branches until 'distance' microns have been removed
    // from each leaf path. Ensures the tree stays "short enough" to support the layer above.
    // Returns the actual pruned distance (may be < distance if the whole tree was pruned away).
    // [HAZARD H307] Recursive.
    coord_t prune(const coord_t& distance);

public:
    /*!
     * Convert the tree into polylines
     *
     * At each junction one line is chosen at random to continue
     *
     * The lines start at a leaf and end in a junction
     *
     * \param output all branches in this tree connected into polylines
     */
    // [INTENT] Top-level entry: calls the recursive convertToPolylines(long_line_idx, output) on
    // a freshly allocated Polylines container, then calls removeJunctionOverlap() to trim the start
    // of each polyline by line_overlap (half the line width) so that junction overlaps don't cause
    // over-extrusion.
    // [HAZARD H273] Uses rand() internally for non-deterministic child selection.
    void convertToPolylines(Polylines& output, coord_t line_overlap) const;

    /*! If this was ever a direct child of the root, it'll have a previous grounding location.
     *
     * This needs to be known when roots are reconnected, so that the last (higher) layer is supported by the next one.
     */
    const std::optional<Point>& getLastGroundingLocation() const { return m_last_grounding_location; }

    // [INTENT] Debug helper: draw all branches of this subtree to an SVG using yellow lines.
    // Non-recursive depth-first draw. Only used when LIGHTNING_TREE_NODE_DEBUG_OUTPUT is defined.
    void draw_tree(SVG& svg)
    {
        for (auto& child : m_children) {
            svg.draw(Line(m_p, child->getLocation()), "yellow");
            child->draw_tree(svg);
        }
    }

protected:
    /*!
     * Convert the tree into polylines
     *
     * At each junction one line is chosen at random to continue
     *
     * The lines start at a leaf and end in a junction
     *
     * \param long_line_idx a reference to a polyline in \p output which to continue building on in the recursion
     * \param output all branches in this tree connected into polylines
     */
    // [INTENT] Recursive polyline builder. For each node:
    //   - Pick one child at random (rand() % m_children.size()) to extend the current "long line".
    //   - All other children start new polylines.
    // Lines run from leaf → root (points appended in reverse DFS order), so each polyline ends at a
    // junction (or root) and begins at a leaf.
    // [HAZARD H273] rand() used here — non-deterministic. See Generator.cpp note.
    // [HAZARD H303] Recursive DFS.
    void convertToPolylines(size_t long_line_idx, Polylines& output) const;

    // [INTENT] Trim the junction end of each polyline by 'line_overlap' to prevent over-extrusion
    // where two branches meet. Works backward from the last point in each polyline.
    // Removes degenerate (single-point) polylines in-place via swap-and-pop.
    // [HAZARD H309] Modifies polylines in-place with swap-and-pop which destroys ordering.
    //               Any caller that needs stable polyline ordering must re-sort after this call.
    void removeJunctionOverlap(Polylines& polylines, coord_t line_overlap) const;

    // [STATE] m_is_root: true iff this node has no parent. Changes during reroot().
    bool m_is_root;
    // [STATE] m_p: the 2D position in scaled integer coordinates (microns). Mutable via setLocation()
    // and modified by straighten() and prune().
    Point m_p;
    // [MEMORY] m_parent: weak_ptr to avoid shared_ptr cycle. Always lock() before using.
    // [HAZARD H310] If a Node outlives its parent (only possible if shared_ptr cycles or if the root
    //               is destroyed while a child shared_ptr is still held externally), m_parent.lock()
    //               returns nullptr. Code that calls m_parent.lock() without checking may crash.
    std::weak_ptr<Node> m_parent;
    // [STATE] m_children: strong references; owns child subtree.
    std::vector<NodeSPtr> m_children;

    // [STATE] m_last_grounding_location: persists the last known boundary attachment point.
    // Only set on nodes that were direct children of a root (first-layer boundary nodes).
    // Used by reconnectRoots() to cheaply find the new boundary attachment direction.
    std::optional<Point> m_last_grounding_location; //<! The last known grounding location, see 'getLastGroundingLocation()'.

    friend BoundingBox get_extents(const NodeSPtr& root_node);
    friend BoundingBox get_extents(const std::vector<NodeSPtr>& tree_roots);

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
    friend void export_to_svg(const NodeSPtr& root_node, Slic3r::SVG& svg);
    friend void export_to_svg(const std::string& path, const Polygons& contour, const std::vector<NodeSPtr>& root_nodes);
#endif /* LIGHTNING_TREE_NODE_DEBUG_OUTPUT */
};

// [INTENT] Point-in-polygon test using Clipper's PointInPolygon (winding-number rule).
// Returns true if 'p' is strictly inside OR on the boundary of any polygon in 'polygons'.
// (PointInPolygon returns -1 for on-boundary, +1 for inside, 0 for outside.)
// [COUPLING] Uses ClipperLib::PointInPolygon directly from the Clipper library.
bool inside(const Polygons& polygons, const Point& p);

// [INTENT] Find the intersection point of segment [a,b] with any polygon edge in outline_locator
// that is within 'within_max_dist' of endpoint 'b'. Returns true and sets 'result' to the closest
// such intersection, or false if none found.
// Used by reconnectRoots() for fast-path boundary snapping.
// [COUPLING] Uses EdgeGrid::Grid for spatial traversal and Geometry::segment_segment_intersection.
bool lineSegmentPolygonsIntersection(
    const Point& a, const Point& b, const EdgeGrid::Grid& outline_locator, Point& result, coord_t within_max_dist);

// [INTENT] Compute the axis-aligned bounding box of a subtree rooted at root_node.
// Inline, recursive DFS over m_children. Friend declaration allows access to m_children.
// [HAZARD H307] Recursive — same stack concern for deep trees.
inline BoundingBox get_extents(const NodeSPtr& root_node)
{
    BoundingBox bbox;
    for (const NodeSPtr& children : root_node->m_children)
        bbox.merge(get_extents(children));
    bbox.merge(root_node->getLocation());
    return bbox;
}

// [INTENT] Compute the bounding box of a forest (vector of roots).
inline BoundingBox get_extents(const std::vector<NodeSPtr>& tree_roots)
{
    BoundingBox bbox;
    for (const NodeSPtr& root_node : tree_roots)
        bbox.merge(get_extents(root_node));
    return bbox;
}

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
// [INTENT] Debug SVG export functions. Controlled by #define LIGHTNING_TREE_NODE_DEBUG_OUTPUT
// (commented out at top of this file). Draws tree branches in red over a blue outline polygon.
void export_to_svg(const NodeSPtr& root_node, SVG& svg);
void export_to_svg(const std::string& path, const Polygons& contour, const std::vector<NodeSPtr>& root_nodes);
#endif /* LIGHTNING_TREE_NODE_DEBUG_OUTPUT */

} // namespace Slic3r::FillLightning

#endif // LIGHTNING_TREE_NODE_H
