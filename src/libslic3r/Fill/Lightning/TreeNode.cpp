// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements all Node member functions for the Lightning Infill tree structure.
// Key algorithms:
//   getWeightedDistance()  — Euclidean dist minus valence boost (encourages bushy trees)
//   hasOffspring()         — DFS cycle-prevention check
//   addChild()             — shared_ptr child grafting
//   propagateToNextLayer() — deepCopy → prune → straighten → realign
//   visitBranches()/visitNodes() — DFS traversal callbacks
//   deepCopy()             — full subtree clone
//   reroot()               — parent-child chain reversal
//   closestNode()          — DFS nearest-node search
//   inside()               — point-in-polygon (Clipper winding-number)
//   lineSegmentPolygonsIntersection() — EdgeGrid-accelerated segment-polygon intersection
//   realign()              — snap tree to new outline; promote out-of-bounds subtrees
//   straighten()           — smooth single-child chains and nudge junctions
//   prune()                — remove leaf nodes up to pruning_distance
//   convertToPolylines()   — tree → Polylines with rand() junction choice
//   removeJunctionOverlap() — trim polylines at junctions to prevent over-extrusion
// [COUPLING] Depends on Geometry.hpp for segment_segment_intersection and Line::distance_to_squared.
//            Uses ClipperLib::PointInPolygon directly for inside().
// [CONCURRENCY] No threading in this file; all functions are single-threaded.
//               Layer.cpp's getBestGroundingLocation() calls getWeightedDistance() from TBB tasks —
//               that call is safe because getWeightedDistance() is const with no mutable state.

#include "TreeNode.hpp"

#include "../../Geometry.hpp"

namespace Slic3r::FillLightning {

// [INTENT] Weighted distance heuristic for choosing the best tree attachment point.
// Valence = number of existing connections (parent counts as 1, each child counts as 1).
// Boost is applied when 1 ≤ valence ≤ 3 (exclusive upper bound = 4), making partially-filled
// nodes appear "closer" than they are, biasing growth toward reusing existing branches.
// [STATE] Read-only; no mutations.
// [HAZARD H304] Hardcoded constants: min_valence_for_boost=0, max_valence_for_boost=4,
//               valence_boost_multiplier=4. Not user-configurable.
coord_t Node::getWeightedDistance(const Point& unsupported_location, const coord_t& supporting_radius) const
{
    constexpr coord_t min_valence_for_boost    = 0;
    constexpr coord_t max_valence_for_boost    = 4;
    constexpr coord_t valence_boost_multiplier = 4;

    // [INTENT] Valence = (1 if has parent) + child_count.
    // A root has no parent (m_is_root = true) so contributes 0; a non-root contributes 1.
    const size_t  valence       = (!m_is_root) + m_children.size();
    const coord_t valence_boost = (min_valence_for_boost < valence && valence < max_valence_for_boost) ?
                                      valence_boost_multiplier * supporting_radius :
                                      0;
    const auto    dist_here     = coord_t((getLocation() - unsupported_location).cast<double>().norm());
    return dist_here - valence_boost;
}

// [INTENT] Recursive DFS check: returns true if to_be_checked is this node or any descendant.
// Uses shared_from_this() for identity comparison (pointer equality of the control block).
// [HAZARD H303] Recursive DFS — can stack-overflow for very deep trees (unlikely in practice).
bool Node::hasOffspring(const NodeSPtr& to_be_checked) const
{
    if (to_be_checked == shared_from_this())
        return true;

    for (auto& child_ptr : m_children)
        if (child_ptr->hasOffspring(to_be_checked))
            return true;

    return false;
}

// [INTENT] Create a new Node at child_loc and add it as a child of 'this'.
// [STATE] Mutates m_children; sets child->m_parent = this.
// [MEMORY] new Node allocated via Node::create (make_shared). Returned shared_ptr is also stored
//          in m_children — reference count ≥ 2 after this call (one in m_children, one returned).
NodeSPtr Node::addChild(const Point& child_loc)
{
    assert(m_p != child_loc);
    NodeSPtr child = Node::create(child_loc);
    return addChild(child);
}

// [INTENT] Add an existing NodeSPtr as a child of 'this'.
// Sets child->m_parent and child->m_is_root to maintain tree invariants.
// [HAZARD H302] No cycle guard in release builds — relies on callers to assert hasOffspring().
NodeSPtr Node::addChild(NodeSPtr& new_child)
{
    assert(new_child != shared_from_this());
    // assert(p != new_child->p); // NOTE: No problem for now. Issue to solve later. Maybe even afetr final. Low prio.
    m_children.push_back(new_child);
    new_child->m_parent  = shared_from_this();
    new_child->m_is_root = false;
    return new_child;
}

// [INTENT] Propagate this tree to the next layer (layer below) by:
//   1. deepCopy()    — clone the full subtree (no aliasing with the original).
//   2. prune()       — remove leaf segments up to prune_distance from each leaf.
//   3. straighten()  — move nodes toward straighter paths (improves printability).
//   4. realign()     — snap nodes to new outline; promote disconnected subtrees to rerooted_parts.
//      If the root itself stays inside: push tree_below to next_trees.
//      If the root is outside: rerooted subtrees were already added inside realign().
// [STATE] Read-only on 'this'; mutates next_trees.
// [MEMORY] O(N) new NodeSPtr allocations for the copy. All owned by next_trees after return.
void Node::propagateToNextLayer(std::vector<NodeSPtr>& next_trees,
                                const Polygons&        next_outlines,
                                const EdgeGrid::Grid&  outline_locator,
                                const coord_t          prune_distance,
                                const coord_t          smooth_magnitude,
                                const coord_t          max_remove_colinear_dist) const
{
    auto tree_below = deepCopy();
    tree_below->prune(prune_distance);
    tree_below->straighten(smooth_magnitude, max_remove_colinear_dist);
    if (tree_below->realign(next_outlines, outline_locator, next_trees))
        next_trees.push_back(tree_below);
}

// NOTE: Depth-first, as currently implemented.
//       Skips the root (because that has no root itself), but all initial nodes will have the root point anyway.
// [INTENT] DFS branch (segment) visitor: calls visitor(parent_loc, child_loc) for each edge.
// 'this' node's segment to its own parent is NOT included (the comment above explains this).
// Used for debug exports; not part of the main infill pipeline.
void Node::visitBranches(const std::function<void(const Point&, const Point&)>& visitor) const
{
    for (const auto& node : m_children) {
        assert(node->m_parent.lock() == shared_from_this());
        visitor(m_p, node->m_p);
        node->visitBranches(visitor);
    }
}

// NOTE: Depth-first, as currently implemented.
// [INTENT] DFS node visitor (pre-order). Called by fillLocator() to build SparseNodeGrid.
// [HAZARD H303] Recursive; stack depth = tree depth.
void Node::visitNodes(const std::function<void(NodeSPtr)>& visitor)
{
    visitor(shared_from_this());
    for (const auto& node : m_children) {
        assert(node->m_parent.lock() == shared_from_this());
        node->visitNodes(visitor);
    }
}

// [INTENT] Construct a root node (m_is_root = true). Parent is null (weak_ptr default-constructed).
// last_grounding_location: the boundary point where this root is anchored. Stored for
// reconnectRoots() to reuse as an anchor hint when propagating to the next layer.
Node::Node(const Point& p, const std::optional<Point>& last_grounding_location /*= std::nullopt*/)
    : m_is_root(true), m_p(p), m_last_grounding_location(last_grounding_location)
{}

// [INTENT] Deep copy: clone 'this' and all descendants. Parent pointers in the copy point to
// the new nodes (not the originals). m_last_grounding_location is preserved on the root.
// [MEMORY] Allocates O(N_nodes) NodeSPtr instances. Caller owns the returned root.
// [HAZARD H307] Recursive — same stack concern for very deep trees.
NodeSPtr Node::deepCopy() const
{
    NodeSPtr local_root   = Node::create(m_p);
    local_root->m_is_root = m_is_root;
    if (m_is_root) {
        // [INTENT] Preserve or default the grounding location on root nodes only.
        // Non-root nodes never need this field.
        local_root->m_last_grounding_location = m_last_grounding_location.value_or(m_p);
    }
    local_root->m_children.reserve(m_children.size());
    for (const auto& node : m_children) {
        NodeSPtr child  = node->deepCopy();
        child->m_parent = local_root;
        local_root->m_children.push_back(child);
    }
    return local_root;
}

// [INTENT] Reverse the parent-child chain upward from 'this' to the old root, then optionally
// attach 'this' to new_parent. After reroot(nullptr), 'this' becomes the new root.
//
// Recursive algorithm:
//   - If not already root: recurse up (old_parent->reroot(this)) so the old root becomes the
//     penultimate node; then push old_parent as a child of 'this'.
//   - After recursion, set m_parent and m_is_root according to new_parent (nullptr → new root).
//
// [STATE] Modifies m_is_root, m_parent, m_children up the entire ancestor chain.
// [HAZARD H305] Recursive with O(depth) call depth. Risk of stack overflow for very deep chains.
// [HAZARD H311] After reroot(), the old root will have m_is_root = false and will be a leaf of
//               the new root. Any external code that cached the old root NodeSPtr as "the tree root"
//               must update its reference. Generator/Layer callers do this via the tree_roots vector.
void Node::reroot(const NodeSPtr& new_parent)
{
    if (!m_is_root) {
        auto old_parent = m_parent.lock();
        old_parent->reroot(shared_from_this());
        m_children.push_back(old_parent);
    }

    if (new_parent) {
        // [INTENT] Remove new_parent from our children list (it was just added by the recursion
        // above) since new_parent will be our new parent, not our child.
        m_children.erase(std::remove(m_children.begin(), m_children.end(), new_parent), m_children.end());
        m_is_root = false;
        m_parent  = new_parent;
    } else {
        m_is_root = true;
        m_parent.reset();
    }
}

// [INTENT] DFS search for the node closest (by Euclidean distance) to 'loc'.
// Initialises with 'this' as candidate, then recursively improves.
// [HAZARD H306] O(N_nodes) — no spatial index; hotspot in reconnectRoots() for large trees.
NodeSPtr Node::closestNode(const Point& loc)
{
    NodeSPtr result        = shared_from_this();
    auto     closest_dist2 = coord_t((m_p - loc).cast<double>().norm());

    for (const auto& child : m_children) {
        NodeSPtr   candidate_node = child->closestNode(loc);
        const auto child_dist2    = coord_t((candidate_node->m_p - loc).cast<double>().norm());
        if (child_dist2 < closest_dist2) {
            closest_dist2 = child_dist2;
            result        = candidate_node;
        }
    }

    return result;
}

// [INTENT] Point-in-polygon test using Clipper's PointInPolygon (winding-number method).
// Returns true if 'p' is on boundary (-1) or inside (positive odd winding count) of any polygon.
// [COUPLING] Uses ClipperLib directly; the Polygon::points is already in Clipper's integer format.
bool inside(const Polygons& polygons, const Point& p)
{
    int poly_count_inside = 0;
    for (const Polygon& poly : polygons) {
        const int is_inside_this_poly = ClipperLib::PointInPolygon(p, poly.points);
        if (is_inside_this_poly == -1)
            return true;
        poly_count_inside += is_inside_this_poly;
    }
    return (poly_count_inside % 2) == 1;
}

// [INTENT] Find the polygon-edge intersection of segment [a,b] nearest to endpoint 'b',
// within 'within_max_dist' of b. Uses EdgeGrid for fast spatial traversal.
// The visitor accumulates the closest intersection in 'intersection_pt' (minimises d2min from b).
// [STATE] Result written to 'result' by ref.
// [COUPLING] Geometry::segment_segment_intersection for exact intersection computation.
// [HAZARD H312] If the grid has not been built or if 'a'=='b', visit_cells_intersecting_line
//               behaviour is undefined. Callers should ensure a != b.
bool lineSegmentPolygonsIntersection(
    const Point& a, const Point& b, const EdgeGrid::Grid& outline_locator, Point& result, const coord_t within_max_dist)
{
    struct Visitor
    {
        bool operator()(coord_t iy, coord_t ix)
        {
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                 ++it_contour_and_segment) {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                // [INTENT] segment_segment_intersection returns true and fills 'ip' if segments intersect.
                if (Vec2d ip; Geometry::segment_segment_intersection(segment.first.cast<double>(), segment.second.cast<double>(),
                                                                     this->line_a, this->line_b, ip))
                    if (double d = (this->intersection_pt - this->line_b).squaredNorm(); d < d2min) {
                        this->d2min           = d;
                        this->intersection_pt = ip;
                    }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid& grid;
        Vec2d                 line_a;
        Vec2d                 line_b;
        // [INTENT] intersection_pt is initialised to line_b so that the distance comparison
        // (ip - line_b).squaredNorm() correctly selects the closest point to b.
        Vec2d  intersection_pt;
        double d2min{std::numeric_limits<double>::max()};
    } visitor{outline_locator, a.cast<double>(), b.cast<double>()};

    outline_locator.visit_cells_intersecting_line(a, b, visitor);
    if (visitor.d2min < double(within_max_dist) * double(within_max_dist)) {
        result = Point(visitor.intersection_pt);
        return true;
    }
    return false;
}

// [INTENT] After a deepCopy() and prune()/straighten(), snap this subtree to the new layer outline.
// Algorithm:
//   - If m_p is inside outlines: process children recursively. Any child whose connecting
//     segment [child→this] crosses the outline is detached (promoted to rerooted_parts) with
//     m_last_grounding_location set to m_p for later reconnectRoots() use.
//   - If m_p is outside: all inside children are promoted; return false (discard this node).
// [STATE] May erase entries from m_children; appends to rerooted_parts.
// [HAZARD H307] Recursive — same stack concern for deep trees.
// [HAZARD H313] lineSegmentPolygonsIntersection() uses outline_locator.resolution() * 2 as the
//               max snap distance. If the layer outline is very thin, this snap radius may cause
//               roots to be placed incorrectly on the wrong outline segment.
bool Node::realign(const Polygons& outlines, const EdgeGrid::Grid& outline_locator, std::vector<NodeSPtr>& rerooted_parts)
{
    if (outlines.empty())
        return false;

    if (inside(outlines, m_p)) {
        // Only keep children that have an unbroken connection to here, realign will put the rest in rerooted parts due to recursion:
        Point coll;
        bool  reground_me = false;
        m_children.erase(std::remove_if(m_children.begin(), m_children.end(),
                                        [&](const NodeSPtr& child) {
                                            bool connect_branch = child->realign(outlines, outline_locator, rerooted_parts);
                                            // [INTENT] Even if the child is inside, if the connecting segment [child→this] crosses
                                            // the outline boundary, the branch must be severed and re-grounded.
                                            // Find an intersection of the line segment from p to child->p, at maximum
                                            // outline_locator.resolution() * 2 distance from p.
                                            if (connect_branch && lineSegmentPolygonsIntersection(child->m_p, m_p, outline_locator, coll,
                                                                                                  outline_locator.resolution() * 2)) {
                                                child->m_last_grounding_location.reset();
                                                child->m_parent.reset();
                                                child->m_is_root = true;
                                                rerooted_parts.push_back(child);
                                                reground_me    = true;
                                                connect_branch = false;
                                            }
                                            return !connect_branch;
                                        }),
                         m_children.end());
        if (reground_me)
            m_last_grounding_location.reset();
        return true;
    }

    // 'Lift' any decendants out of this tree:
    // [INTENT] This node is outside the outline; promote any inside children as new orphan roots.
    for (auto& child : m_children)
        if (child->realign(outlines, outline_locator, rerooted_parts)) {
            // [INTENT] Record this (outside) node's position as the child's last grounding location
            // so reconnectRoots() can search for the boundary in that direction.
            child->m_last_grounding_location = m_p;
            child->m_parent.reset();
            child->m_is_root = true;
            rerooted_parts.push_back(child);
        }

    m_children.clear();
    return false;
}

// [INTENT] Entry point for tree straightening. Squares max_remove_colinear_dist to avoid
// sqrt in the inner loop, then delegates to the recursive overload.
void Node::straighten(const coord_t magnitude, const coord_t max_remove_colinear_dist)
{
    straighten(magnitude, m_p, 0, int64_t(max_remove_colinear_dist) * int64_t(max_remove_colinear_dist));
}

// [INTENT] Recursive tree straightening.
// For single-child chains:
//   - Interpolate this node's position along the straight line from junction_above to
//     junction_below by the fraction accumulated_dist / total_dist_to_junction_below.
//   - If this node is nearly collinear with parent and child (within close_enough = 10 nm),
//     remove it from the chain (replace sibling pointer with child, set child's parent).
// For multi-child nodes (junctions):
//   - Compute a junction_moving_dir as the normalised average of incoming + all outgoing directions.
//   - Nudge the junction in that direction by up to junction_magnitude (= 3/4 * magnitude).
//   - Do NOT move if any outgoing branch is shorter than magnitude (prevent flip-flopping).
// [STATE] Modifies m_p in-place on every node in the subtree. May remove collinear nodes
//         from m_children of the parent (via sibling-pointer swap).
// [HAZARD H308] Integer-to-double-to-integer rounding in position update. Accumulated over many
//               layers and straightening passes, nodes may drift slightly. The error is bounded by
//               ±1 nm per pass (fine for printing precision of ~10 µm).
// [HAZARD H307] Recursive.
Node::RectilinearJunction Node::straighten(const coord_t magnitude,
                                           const Point&  junction_above,
                                           const coord_t accumulated_dist,
                                           const int64_t max_remove_colinear_dist2)
{
    // [INTENT] junction_magnitude: slightly less than magnitude so junctions don't overshoot
    // as much as straight-line nodes (junctions are structural; straights can move more freely).
    constexpr coord_t junction_magnitude_factor_numerator   = 3;
    constexpr coord_t junction_magnitude_factor_denominator = 4;

    const coord_t junction_magnitude = magnitude * junction_magnitude_factor_numerator / junction_magnitude_factor_denominator;
    if (m_children.size() == 1) {
        auto                child_p                      = m_children.front();
        auto                child_dist                   = coord_t((m_p - child_p->m_p).cast<double>().norm());
        RectilinearJunction junction_below               = child_p->straighten(magnitude, junction_above, accumulated_dist + child_dist,
                                                                               max_remove_colinear_dist2);
        coord_t             total_dist_to_junction_below = junction_below.total_recti_dist;
        const Point&        a                            = junction_above;
        Point               b                            = junction_below.junction_loc;
        if (a != b) // should always be true!
        {
            Point ab = b - a;
            // [INTENT] Interpolate this node's ideal position along [junction_above, junction_below]
            // at the fractional distance accumulated_dist / total_dist_to_junction_below.
            // [HAZARD H308] Integer arithmetic: division may lose up to 1 unit of precision.
            Point destination = (a.cast<int64_t>() + ab.cast<int64_t>() * int64_t(accumulated_dist) /
                                                         std::max(int64_t(1), int64_t(total_dist_to_junction_below)))
                                    .cast<coord_t>();
            if ((destination - m_p).cast<int64_t>().squaredNorm() <= int64_t(magnitude) * int64_t(magnitude))
                m_p = destination;
            else
                m_p += ((destination - m_p).cast<double>().normalized() * magnitude).cast<coord_t>();
        }
        {                                        // remove nodes on linear segments
            constexpr coord_t close_enough = 10; // [INTENT] 10 nm threshold for collinearity removal.

            child_p                     = m_children.front(); // recursive call to straighten might have removed the child
            const NodeSPtr& parent_node = m_parent.lock();
            if (parent_node && (child_p->m_p - parent_node->m_p).cast<int64_t>().squaredNorm() < max_remove_colinear_dist2 &&
                Line::distance_to_squared(m_p, parent_node->m_p, child_p->m_p) < close_enough * close_enough) {
                // [INTENT] This node is approximately collinear with parent and child; remove it.
                // Reconnect child directly to parent by replacing our sibling slot in parent's m_children.
                child_p->m_parent = m_parent;
                for (auto& sibling : parent_node->m_children) { // find this node among siblings
                    if (sibling == shared_from_this()) {
                        sibling = child_p; // replace this node by child
                        break;
                    }
                }
            }
        }
        return junction_below;
    } else {
        // [INTENT] Multi-child (junction) node. Compute direction toward the weighted centroid
        // of all adjacent directions (parent + all children).
        constexpr coord_t weight = 1000; // [INTENT] weight for normalised direction vector — prevents precision loss in coord_t.
        Point             junction_moving_dir     = ((junction_above - m_p).cast<double>().normalized() * weight).cast<coord_t>();
        bool              prevent_junction_moving = false;
        for (auto& child_p : m_children) {
            const auto          child_dist = coord_t((m_p - child_p->m_p).cast<double>().norm());
            RectilinearJunction below      = child_p->straighten(magnitude, m_p, child_dist, max_remove_colinear_dist2);

            junction_moving_dir += ((below.junction_loc - m_p).cast<double>().normalized() * weight).cast<coord_t>();
            if (below.total_recti_dist < magnitude) // TODO: make configurable?
            {
                // [INTENT] Short branch below: prevent junction from moving to avoid oscillation
                // between junction shift and straightening in short branches.
                prevent_junction_moving = true; // prevent flipflopping in branches due to straightening and junctoin moving clashing
            }
        }
        if (junction_moving_dir != Point(0, 0) && !m_children.empty() && !m_is_root && !prevent_junction_moving) {
            auto junction_moving_dir_len = coord_t(junction_moving_dir.norm());
            if (junction_moving_dir_len > junction_magnitude) {
                // [INTENT] Clamp movement to junction_magnitude.
                junction_moving_dir = junction_moving_dir * junction_magnitude / junction_moving_dir_len;
            }
            m_p += junction_moving_dir;
        }
        return RectilinearJunction{accumulated_dist, m_p};
    }
}

// Prune the tree from the extremeties (leaf-nodes) until the pruning distance is reached.
// [INTENT] Remove branches from leaves inward until 'pruning_distance' microns have been pruned
// from each path. Returns the actual distance pruned (≥ pruning_distance if the whole subtree
// fits, else < pruning_distance and the subtree was consumed entirely).
//
// For each child:
//   - Recurse: if child's subtree returned dist_pruned_child >= pruning_distance, the child
//     survived (partial pruning already complete further down).
//   - If the child's subtree + the segment to this node is ≤ pruning_distance: remove the child.
//   - Else: the pruning boundary falls inside the segment; move the child's position to the
//     pruning cutpoint.
// [HAZARD H307] Recursive.
coord_t Node::prune(const coord_t& pruning_distance)
{
    if (pruning_distance <= 0)
        return 0;

    coord_t max_distance_pruned = 0;
    for (auto child_it = m_children.begin(); child_it != m_children.end();) {
        auto&   child             = *child_it;
        coord_t dist_pruned_child = child->prune(pruning_distance);
        if (dist_pruned_child >= pruning_distance) { // pruning is finished for child; dont modify further
            max_distance_pruned = std::max(max_distance_pruned, dist_pruned_child);
            ++child_it;
        } else {
            const Point a      = getLocation();
            const Point b      = child->getLocation();
            const Point ba     = a - b;
            const auto  ab_len = coord_t(ba.cast<double>().norm());
            if (dist_pruned_child + ab_len <= pruning_distance) {
                // we're still in the process of pruning
                assert(child->m_children.empty() && "when pruning away a node all it's children must already have been pruned away");
                max_distance_pruned = std::max(max_distance_pruned, dist_pruned_child + ab_len);
                // [INTENT] Remove the child entirely; iterator returned by erase() points to next.
                child_it = m_children.erase(child_it);
            } else {
                // pruning stops in between this node and the child
                // [INTENT] Move child to the pruning cutpoint along the segment from child toward this.
                const Point n = b + (ba.cast<double>().normalized() * (pruning_distance - dist_pruned_child)).cast<coord_t>();
                assert(std::abs((n - b).cast<double>().norm() + dist_pruned_child - pruning_distance) < 10 &&
                       "total pruned distance must be equal to the pruning_distance");
                max_distance_pruned = std::max(max_distance_pruned, pruning_distance);
                child->setLocation(n);
                ++child_it;
            }
        }
    }

    return max_distance_pruned;
}

// [INTENT] Top-level convertToPolylines entry point:
//   1. Allocate a Polylines with one empty polyline (the "long line" slot).
//   2. Fill via the recursive overload (DFS, rand() junction choice).
//   3. removeJunctionOverlap() to trim polyline ends at junctions by line_overlap.
//   4. Append to 'output'.
// [STATE] Read-only on this; appends to output.
// [HAZARD H273] rand() used inside step 2 — non-deterministic output order.
void Node::convertToPolylines(Polylines& output, const coord_t line_overlap) const
{
    Polylines result;
    result.emplace_back();
    convertToPolylines(0, result);
    removeJunctionOverlap(result, line_overlap);
    append(output, std::move(result));
}

// [INTENT] Recursive polyline builder. DFS traversal, reverse order (leaf→junction).
// At each node:
//   - Pick one child at random (rand() % m_children.size()) as the "long line" extension.
//   - Continue the current polyline through that child.
//   - All other children start new polylines.
//   - Append m_p to the current polyline after processing the long-line child.
//     (Results in polylines ordered from leaf to junction, reversed from tree direction.)
// [HAZARD H273] rand() — non-deterministic. Different runs produce different polyline orderings.
//               Acceptable for infill (no functional difference) but must be preserved in ports as
//               a known non-determinism or replaced with a seeded RNG for deterministic output.
// [HAZARD H303] Recursive — same stack concern.
void Node::convertToPolylines(size_t long_line_idx, Polylines& output) const
{
    if (m_children.empty()) {
        output[long_line_idx].points.push_back(m_p);
        return;
    }
    // [INTENT] Random child selection: ensures that no single child is always favoured for the
    // long line, distributing line overlap removal evenly. Non-deterministic side effect of this choice.
    size_t first_child_idx = rand() % m_children.size();
    m_children[first_child_idx]->convertToPolylines(long_line_idx, output);
    output[long_line_idx].points.push_back(m_p);

    for (size_t idx_offset = 1; idx_offset < m_children.size(); idx_offset++) {
        size_t      child_idx = (first_child_idx + idx_offset) % m_children.size();
        const Node& child     = *m_children[child_idx];
        output.emplace_back();
        size_t child_line_idx = output.size() - 1;
        child.convertToPolylines(child_line_idx, output);
        output[child_line_idx].points.emplace_back(m_p);
    }
}

// [INTENT] Trim 'reduction' microns from the junction end (last point) of each polyline.
// This prevents over-extrusion at junctions where two polylines share an endpoint.
// Works backward from the end of each polyline, removing points and shortening the last segment.
// Degenerate polylines (≤ 1 point after trimming) are removed via swap-and-pop.
// [STATE] Modifies result_lines in place. Ordering is not preserved (swap-and-pop).
// [HAZARD H309] Swap-and-pop destroys polyline ordering. Any downstream code assuming stable order
//               must re-sort or be order-insensitive. Layer::convertToLines() passes the result
//               directly to intersection_pl() which is order-insensitive.
void Node::removeJunctionOverlap(Polylines& result_lines, const coord_t line_overlap) const
{
    const coord_t reduction    = line_overlap;
    size_t        res_line_idx = 0;
    while (res_line_idx < result_lines.size()) {
        Polyline& polyline = result_lines[res_line_idx];
        if (polyline.size() <= 1) {
            // [INTENT] Remove degenerate polyline via swap-and-pop.
            polyline = std::move(result_lines.back());
            result_lines.pop_back();
            continue;
        }

        coord_t to_be_reduced = reduction;
        Point   a             = polyline.back();
        for (int point_idx = int(polyline.size()) - 2; point_idx >= 0; point_idx--) {
            const Point b      = polyline.points[point_idx];
            const Point ab     = b - a;
            const auto  ab_len = coord_t(ab.cast<double>().norm());
            if (ab_len >= to_be_reduced) {
                // [INTENT] Shorten last segment: move last point toward second-to-last by to_be_reduced.
                polyline.points.back() = a + (ab.cast<double>() * (double(to_be_reduced) / ab_len)).cast<coord_t>();
                break;
            } else {
                to_be_reduced -= ab_len;
                polyline.points.pop_back();
            }
            a = b;
        }

        if (polyline.size() <= 1) {
            polyline = std::move(result_lines.back());
            result_lines.pop_back();
        } else
            ++res_line_idx;
    }
}

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
// [INTENT] Debug SVG export: draw all branches in red (root → leaf direction).
// DFS recursion; disabled by default (#define at top of TreeNode.hpp).
void export_to_svg(const NodeSPtr& root_node, SVG& svg)
{
    for (const NodeSPtr& children : root_node->m_children) {
        svg.draw(Line(root_node->getLocation(), children->getLocation()), "red");
        export_to_svg(children, svg);
    }
}

void export_to_svg(const std::string& path, const Polygons& contour, const std::vector<NodeSPtr>& root_nodes)
{
    BoundingBox bbox = get_extents(contour);

    bbox.offset(SCALED_EPSILON);
    SVG svg(path, bbox);
    svg.draw_outline(contour, "blue");

    for (const NodeSPtr& root_node : root_nodes) {
        for (const NodeSPtr& children : root_node->m_children) {
            svg.draw(Line(root_node->getLocation(), children->getLocation()), "red");
            export_to_svg(children, svg);
        }
    }
}
#endif /* LIGHTNING_TREE_NODE_DEBUG_OUTPUT */

} // namespace Slic3r::FillLightning
