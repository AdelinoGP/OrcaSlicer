// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements Layer: per-slice Lightning Infill tree management.
// Responsibilities:
//   1. fillLocator()             — build SparseNodeGrid from current tree_roots
//   2. generateNewTrees()        — main growth loop: DistanceField → attach new branches
//   3. polygonCollidesWithLineSegment() — EdgeGrid visitor for line-outline intersection
//   4. getBestGroundingLocation() — choose tree-node or boundary attachment (TBB parallel)
//   5. attach()                  — actually add node(s) to tree_roots / child lists
//   6. reconnectRoots()          — reattach orphaned roots after layer-down propagation
//   7. convertToLines()          — tree → Polylines via Node::convertToPolylines + Clipper clip
// [COUPLING] Depends on DistanceField, Node (TreeNode.hpp), EdgeGrid, ClipperUtils, Geometry, Utils.
// [CONCURRENCY] getBestGroundingLocation() uses tbb::parallel_for + std::mutex; all other methods
//               are single-threaded.

#include "Layer.hpp" //The class we're implementing.

#include "DistanceField.hpp"
#include "TreeNode.hpp"

#include "../../ClipperUtils.hpp"
#include "../../Geometry.hpp"
#include "Utils.hpp"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range2d.h>
#include <mutex>

namespace Slic3r::FillLightning {

// [INTENT] Compute Euclidean distance between two points as coord_t (integer microns).
// Used as the boundary-grounding weight in getBestGroundingLocation().
// [HAZARD H294] double→coord_t truncation; overflows for distances > ~2.1 km. Safe for printers.
coord_t Layer::getWeightedDistance(const Point& boundary_loc, const Point& unsupported_location)
{
    return coord_t((boundary_loc - unsupported_location).cast<double>().norm());
}

// [INTENT] Return the physical coordinate of the grounding point.
// If tree_node is set, returns the node's location; otherwise returns the boundary_location.
// [HAZARD H287] assert() is debug-only; release builds with both null/empty → UB on deref.
Point GroundingLocation::p() const
{
    assert(tree_node || boundary_location);
    return tree_node ? tree_node->getLocation() : *boundary_location;
}

// [INTENT] Convert a world-space point to a grid-cell address for the SparseNodeGrid.
// Uses locator_cell_size() (defined in TreeNode.hpp as scaled<coord_t>(4.0) = 4 mm in integer units).
// [COUPLING] locator_cell_size() must match the cell size used when the grid was built.
inline static Point to_grid_point(const Point& point, const BoundingBox& bbox) { return (point - bbox.min) / locator_cell_size(); }

// [INTENT] Populate tree_node_locator with all nodes currently in tree_roots.
// Each tree is traversed depth-first via Node::visitNodes(); each node is inserted as
// (grid_cell_addr, weak_ptr<Node>).
// [STATE] Writes to tree_node_locator. Does NOT clear it first — caller is responsible for
//         passing a fresh (empty) SparseNodeGrid or an intentionally pre-populated one.
// [MEMORY] Values are weak_ptr — the strong references live in tree_roots. Stale entries remain
//          until the locator is discarded (it is rebuilt fresh each call to generateNewTrees /
//          reconnectRoots, so staleness is not a persistent problem).
void Layer::fillLocator(SparseNodeGrid& tree_node_locator, const BoundingBox& current_outlines_bbox)
{
    std::function<void(NodeSPtr)> add_node_to_locator_func = [&tree_node_locator, &current_outlines_bbox](const NodeSPtr& node) {
        tree_node_locator.insert(std::make_pair(to_grid_point(node->getLocation(), current_outlines_bbox), node));
    };
    for (auto& tree : tree_roots)
        tree->visitNodes(add_node_to_locator_func);
}

// [INTENT] Main per-layer tree-growth algorithm (bottom-up iteration).
// Steps:
//   1. Build a DistanceField from current_overhang (samples overhang area at cell_size spacing,
//      sorted by distance-to-boundary descending so interior points are served first).
//   2. Build a SparseNodeGrid locator from existing tree_roots (propagated from layer above).
//   3. Loop: ask DistanceField for the next unsupported cell (tryGetNextPoint) → find best
//      grounding location → attach new branch → update distance field.
// [STATE] Mutates tree_roots (via attach()) and tree_node_locator (inline insertions).
// [CONCURRENCY] Not thread-safe on 'this'; called single-threaded from Generator::generateTrees().
//               getBestGroundingLocation() (called inside loop) spawns TBB tasks internally.
// [HAZARD H295] throw_on_cancel_callback() is called only once before the loop starts and once
//               per loop iteration. If a single iteration is very slow (large outline, many nodes),
//               cancellation latency can be several seconds.
void Layer::generateNewTrees(const Polygons&              current_overhang,
                             const Polygons&              current_outlines,
                             const BoundingBox&           current_outlines_bbox,
                             const EdgeGrid::Grid&        outlines_locator,
                             const coord_t                supporting_radius,
                             const coord_t                wall_supporting_radius,
                             const std::function<void()>& throw_on_cancel_callback)
{
    // [INTENT] Construct the distance field; this samples the overhang polygon at m_cell_size
    // intervals and computes dist_to_boundary for each sample. O(N_samples * N_contour_vertices).
    DistanceField distance_field(supporting_radius, current_outlines, current_outlines_bbox, current_overhang);
    throw_on_cancel_callback();

    // [INTENT] Build SparseNodeGrid from already-propagated tree_roots (from layer above).
    // Allows new branches to attach to existing trees rather than always rooting to the boundary.
    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator, current_outlines_bbox);

    // Until no more points need to be added to support all:
    // Determine next point from tree/outline areas via distance-field
    // [INTENT] unsupported_cell_idx tracks the scan position into DistanceField::m_unsupported_points
    // so that tryGetNextPoint() can resume scanning from the last visited position rather than
    // restarting from index 0, giving O(N) amortised total scan cost across all iterations.
    size_t unsupported_cell_idx = 0;
    Point  unsupported_location;
    while (distance_field.tryGetNextPoint(&unsupported_location, &unsupported_cell_idx, unsupported_cell_idx)) {
        throw_on_cancel_callback();
        GroundingLocation grounding_loc = getBestGroundingLocation(unsupported_location, current_outlines, current_outlines_bbox,
                                                                   outlines_locator, supporting_radius, wall_supporting_radius,
                                                                   tree_node_locator);

        NodeSPtr new_parent;
        NodeSPtr new_child;
        this->attach(unsupported_location, grounding_loc, new_child, new_parent);
        // [STATE] Insert the newly created child (and root if boundary-grounded) into the locator.
        tree_node_locator.insert(std::make_pair(to_grid_point(new_child->getLocation(), current_outlines_bbox), new_child));
        if (new_parent)
            tree_node_locator.insert(std::make_pair(to_grid_point(new_parent->getLocation(), current_outlines_bbox), new_parent));
        // update distance field — mark cells within the influence zone of the new branch as supported
        distance_field.update(grounding_loc.p(), unsupported_location);
    }

#ifdef LIGHTNING_TREE_NODE_DEBUG_OUTPUT
    // [INTENT] Debug SVG export; disabled by default. Enabled via #define at top of TreeNode.hpp.
    {
        static int iRun = 0;
        export_to_svg(debug_out_path("FillLightning-TreeNodes-%d.svg", iRun++), current_outlines, this->tree_roots);
    }
#endif /* LIGHTNING_TREE_NODE_DEBUG_OUTPUT */
}

// [INTENT] Local helper: test if the line segment [from, to] intersects any polygon edge stored
// in the EdgeGrid. Uses a Bresenham-style grid traversal to visit only relevant cells — O(1)
// average per test after the grid is built.
// [COUPLING] Depends on EdgeGrid::Grid::visit_cells_intersecting_line() and
//            Geometry::segments_intersect().
// [HAZARD H296] If 'from' == 'to' (zero-length segment), visit_cells_intersecting_line may
//               enter a degenerate loop. Callers should guard against zero-length inputs.
static bool polygonCollidesWithLineSegment(const Point& from, const Point& to, const EdgeGrid::Grid& loc_to_line)
{
    struct Visitor
    {
        explicit Visitor(const EdgeGrid::Grid& grid, const Line& line) : grid(grid), line(line) {}

        bool operator()(coord_t iy, coord_t ix)
        {
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                 ++it_contour_and_segment) {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, line.a, line.b)) {
                    this->intersect = true;
                    // [INTENT] Returning false stops traversal — short-circuit on first intersection.
                    return false;
                }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid& grid;
        Line                  line;
        bool                  intersect = false;
    } visitor(loc_to_line, {from, to});

    loc_to_line.visit_cells_intersecting_line(from, to, visitor);
    return visitor.intersect;
}

// [INTENT] Find the best GroundingLocation for 'unsupported_location'.
// Strategy:
//   1. Linear scan over all polygon vertices to find closest boundary point (O(N_vertices)).
//   2. If distance to boundary >= wall_supporting_radius, search tree nodes via TBB parallel_for
//      over a bounding-box of grid cells. Each candidate tree node is tested for:
//        - not being in the excluded tree (to avoid cycles)
//        - no polygon collision on the connecting segment
//        - getWeightedDistance() < current best (Euclidean dist minus valence boost)
//   3. Return tree-node grounding if a better one was found; else return boundary grounding.
// [CONCURRENCY] tbb::parallel_for over a 2D blocked_range2d of grid cells.
//               current_dist and sub_tree are shared and protected by current_dist_mutex.
//               Tie-breaking on grid address preserves determinism vs sequential execution.
// [HAZARD H289] Step 1 is O(N_vertices) with no spatial acceleration. Hot path for complex outlines.
// [HAZARD H297] The lambda captures 'current_dist' by reference AND as 'current_dist_copy' (value).
//               'current_dist_copy' is the baseline before any TBB task runs; tasks only update
//               shared 'current_dist' via the mutex. This is correct but subtle — a port must
//               replicate the double-capture pattern exactly to avoid race conditions.
GroundingLocation Layer::getBestGroundingLocation(const Point&          unsupported_location,
                                                  const Polygons&       current_outlines,
                                                  const BoundingBox&    current_outlines_bbox,
                                                  const EdgeGrid::Grid& outline_locator,
                                                  const coord_t         supporting_radius,
                                                  const coord_t         wall_supporting_radius,
                                                  const SparseNodeGrid& tree_node_locator,
                                                  const NodeSPtr&       exclude_tree)
{
    // Closest point on current_outlines to unsupported_location:
    // [HAZARD H289] O(N_vertices) scan — no spatial index.
    Point node_location;
    {
        double d2 = std::numeric_limits<double>::max();
        for (const Polygon& contour : current_outlines)
            if (contour.size() > 2) {
                Point prev = contour.points.back();
                for (const Point& p2 : contour.points) {
                    Point closest_point;
                    if (double d = line_alg::distance_to_squared(Line{prev, p2}, unsupported_location, &closest_point); d < d2) {
                        d2            = d;
                        node_location = closest_point;
                    }
                    prev = p2;
                }
            }
    }

    // [INTENT] within_dist: Euclidean distance from unsupported_location to closest boundary point.
    const auto within_dist = coord_t((node_location - unsupported_location).cast<double>().norm());

    NodeSPtr sub_tree{nullptr};
    coord_t  current_dist = getWeightedDistance(node_location, unsupported_location);
    // [INTENT] Only search tree nodes if the point is not already "close enough" to the wall.
    // wall_supporting_radius acts as a minimum distance: points within this radius are always
    // grounded directly on the boundary (no tree attachment).
    if (current_dist >= wall_supporting_radius) { // Only reconnect tree roots to other trees if they are not already close to the outlines.
        const coord_t search_radius = std::min(current_dist, within_dist);
        // [INTENT] Convert the world-space search box to grid-cell coordinates for the locator.
        BoundingBox region(unsupported_location - Point(search_radius, search_radius),
                           unsupported_location + Point(search_radius + locator_cell_size(), search_radius + locator_cell_size()));
        region.min = to_grid_point(region.min, current_outlines_bbox);
        region.max = to_grid_point(region.max, current_outlines_bbox);

        // [INTENT] current_dist_grid_addr tracks the grid-cell origin of the current best candidate
        // to ensure deterministic tie-breaking: when two nodes have equal weighted distance, the
        // one from the lexicographically smallest (row, col) cell wins, matching sequential order.
        Point      current_dist_grid_addr{std::numeric_limits<coord_t>::lowest(), std::numeric_limits<coord_t>::lowest()};
        std::mutex current_dist_mutex;
        // [CONCURRENCY] tbb::parallel_for over 2D grid cell range. Each task reads tree_node_locator
        // (const, safe), runs polygonCollidesWithLineSegment (stateless, safe), and conditionally
        // updates current_dist / sub_tree under current_dist_mutex.
        // [HAZARD H297] current_dist captured by ref + current_dist_copy captured by value — see above.
        tbb::parallel_for(tbb::blocked_range2d<coord_t>(region.min.y(), region.max.y(), region.min.x(), region.max.x()),
                          [&current_dist, current_dist_copy = current_dist, &current_dist_mutex, &sub_tree, &current_dist_grid_addr,
                           &exclude_tree = std::as_const(exclude_tree), &outline_locator = std::as_const(outline_locator),
                           &supporting_radius = std::as_const(supporting_radius), &tree_node_locator = std::as_const(tree_node_locator),
                           &unsupported_location = std::as_const(unsupported_location)](const tbb::blocked_range2d<coord_t>& range) -> void {
                              for (coord_t grid_addr_y = range.rows().begin(); grid_addr_y < range.rows().end(); ++grid_addr_y)
                                  for (coord_t grid_addr_x = range.cols().begin(); grid_addr_x < range.cols().end(); ++grid_addr_x) {
                                      const Point local_grid_addr{grid_addr_x, grid_addr_y};
                                      NodeSPtr    local_sub_tree{nullptr};
                                      // [INTENT] Use current_dist_copy (pre-parallel snapshot) as baseline so each task
                                      // independently competes against the initial best, not a partially-updated shared best.
                                      coord_t    local_current_dist = current_dist_copy;
                                      const auto it_range           = tree_node_locator.equal_range(local_grid_addr);
                                      for (auto it = it_range.first; it != it_range.second; ++it) {
                                          const NodeSPtr candidate_sub_tree = it->second.lock();
                                          if ((candidate_sub_tree && candidate_sub_tree != exclude_tree) &&
                                              !(exclude_tree && exclude_tree->hasOffspring(candidate_sub_tree)) &&
                                              !polygonCollidesWithLineSegment(unsupported_location, candidate_sub_tree->getLocation(),
                                                                              outline_locator)) {
                                              if (const coord_t candidate_dist =
                                                      candidate_sub_tree->getWeightedDistance(unsupported_location, supporting_radius);
                                                  candidate_dist < local_current_dist) {
                                                  local_current_dist = candidate_dist;
                                                  local_sub_tree     = candidate_sub_tree;
                                              }
                                          }
                                      }
                                      // To always get the same result in a parallel version as in a non-parallel version,
                                      // we need to preserve that for the same current_dist, we select the same sub_tree
                                      // as in the non-parallel version. For this purpose, inside the variable
                                      // current_dist_grid_addr is stored from with 2D grid position assigned sub_tree comes.
                                      // And when there are two sub_tree with the same current_dist, one which will be found
                                      // the first in the non-parallel version is selected.
                                      {
                                          std::lock_guard<std::mutex> lock(current_dist_mutex);
                                          if (local_current_dist < current_dist ||
                                              (local_current_dist == current_dist &&
                                               (grid_addr_y < current_dist_grid_addr.y() ||
                                                (grid_addr_y == current_dist_grid_addr.y() && grid_addr_x < current_dist_grid_addr.x())))) {
                                              current_dist           = local_current_dist;
                                              sub_tree               = local_sub_tree;
                                              current_dist_grid_addr = local_grid_addr;
                                          }
                                      }
                                  }
                          }); // end of parallel_for
    }

    // [INTENT] Return tree-node grounding if a better (lower-weight) candidate was found;
    // otherwise fall back to the boundary point computed in step 1.
    return !sub_tree ? GroundingLocation{nullptr, node_location} : GroundingLocation{sub_tree, std::optional<Point>()};
}

// [INTENT] Attach unsupported_location to grounding_loc, inserting into tree_roots if needed.
// Two cases:
//   a) boundary_location set: create a new root at boundary + a child at unsupported_location.
//      Push root to tree_roots. Returns true.
//   b) tree_node set: add unsupported_location as a child of the tree node. Returns false.
// [STATE] Modifies tree_roots (boundary case) and the chosen Node's m_children.
// [MEMORY] Nodes created via Node::create() (make_shared). new_child and new_root are out-params;
//          caller is responsible for inserting them into the SparseNodeGrid locator immediately.
// [HAZARD H290] tree_roots.push_back() may reallocate. If caller holds iterators or pointers into
//               tree_roots (it doesn't in the current code), they would be invalidated.
bool Layer::attach(const Point& unsupported_location, const GroundingLocation& grounding_loc, NodeSPtr& new_child, NodeSPtr& new_root)
{
    // Update trees & distance fields.
    if (grounding_loc.boundary_location) {
        // [INTENT] Root is pinned to the outline boundary; child hangs inward to unsupported_location.
        new_root  = Node::create(grounding_loc.p(), std::make_optional(grounding_loc.p()));
        new_child = new_root->addChild(unsupported_location);
        tree_roots.push_back(new_root);
        return true;
    } else {
        // [INTENT] Graft the unsupported point as a child of an existing tree node.
        new_child = grounding_loc.tree_node->addChild(unsupported_location);
        return false;
    }
}

// [INTENT] After tree propagation from the layer above, some roots may lie outside the new
// outline (the outline shrinks / differs per layer). This function reattaches them:
//   For each orphaned root:
//     1. If it has a last_grounding_location, try a fast path: snap root to the outline
//        intersection of [root→last_ground] within outline_locator.resolution()*2 of ground_loc.
//     2. Otherwise call getBestGroundingLocation() with a slightly reduced wall_supporting_radius
//        (tree_connecting_ignore_offset = 100 nm subtracted) to prefer tree attachment.
//     3a. If boundary grounding: create new root on boundary, reroot existing tree onto it.
//     3b. If tree grounding: reroot existing tree onto the target tree node.
//        In this case the old root entry is replaced by the back element (pop-back swap).
// [STATE] Mutates tree_roots: replaces entries in-place or pop-back swaps.
// [CONCURRENCY] Single-threaded except getBestGroundingLocation() which uses TBB internally.
// [HAZARD H291] If to_be_reconnected_tree_roots contains entries not present in tree_roots,
//               std::find returns end() and *old_root_it is UB. Caller (Generator) must guarantee
//               all entries are still valid. Currently satisfied because reconnectRoots() is called
//               immediately after propagateToNextLayer() in the same layer iteration.
// [HAZARD H298] The pop-back swap in the tree-grounding branch (line ~300) changes iteration order
//               of tree_roots for subsequent operations. Not a bug, but must be preserved in ports.
void Layer::reconnectRoots(std::vector<NodeSPtr>& to_be_reconnected_tree_roots,
                           const Polygons&        current_outlines,
                           const BoundingBox&     current_outlines_bbox,
                           const EdgeGrid::Grid&  outline_locator,
                           const coord_t          supporting_radius,
                           const coord_t          wall_supporting_radius)
{
    // [INTENT] 100 nm offset shrinks the effective wall_supporting_radius slightly so that roots
    // which are already on or very near the boundary are still allowed to attach to trees.
    // This prevents perpetual re-rooting of boundary-touching roots.
    constexpr coord_t tree_connecting_ignore_offset = 100;

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator, current_outlines_bbox);

    // [INTENT] within_max_dist: maximum allowed distance from the target boundary point
    // for the intersection snap in the fast path. Equals 2 × EdgeGrid resolution ≈ 2 × cell_size.
    const coord_t within_max_dist = outline_locator.resolution() * 2;
    for (const auto& root_ptr : to_be_reconnected_tree_roots) {
        auto old_root_it = std::find(tree_roots.begin(), tree_roots.end(), root_ptr);

        if (root_ptr->getLastGroundingLocation()) {
            const Point& ground_loc = *root_ptr->getLastGroundingLocation();
            if (ground_loc != root_ptr->getLocation()) {
                Point new_root_pt;
                // [INTENT] Fast path: find where the line from the current root position to its
                // last grounding location crosses the new outline. Create a new boundary root there
                // and make the existing root a child.
                // [COUPLING] lineSegmentPolygonsIntersection() defined in TreeNode.cpp.
                if (lineSegmentPolygonsIntersection(root_ptr->getLocation(), ground_loc, outline_locator, new_root_pt, within_max_dist)) {
                    auto new_root = Node::create(new_root_pt, new_root_pt);
                    root_ptr->addChild(new_root);
                    new_root->reroot();

                    tree_node_locator.insert(std::make_pair(to_grid_point(new_root->getLocation(), current_outlines_bbox), new_root));

                    *old_root_it = std::move(new_root); // replace old root with new root
                    continue;
                }
            }
        }

        // [INTENT] General path: use getBestGroundingLocation() with a slightly reduced
        // wall_supporting_radius to find the optimal attachment (tree or boundary).
        const coord_t tree_connecting_ignore_width =
            wall_supporting_radius -
            tree_connecting_ignore_offset; // Ideally, the boundary size in which the valence rule is ignored would be configurable.
        GroundingLocation ground = getBestGroundingLocation(root_ptr->getLocation(), current_outlines, current_outlines_bbox,
                                                            outline_locator, supporting_radius, tree_connecting_ignore_width,
                                                            tree_node_locator,
                                                            root_ptr // exclude self-tree to avoid cycles
        );
        if (ground.boundary_location) {
            if (*ground.boundary_location == root_ptr->getLocation())
                continue; // Already on the boundary.

            // [INTENT] Create a new boundary root, find the closest existing node, reroot it,
            // and attach it as a child of the new boundary root. Replace old root entry.
            auto new_root   = Node::create(ground.p(), ground.p());
            auto attach_ptr = root_ptr->closestNode(new_root->getLocation());
            attach_ptr->reroot();

            new_root->addChild(attach_ptr);
            tree_node_locator.insert(std::make_pair(to_grid_point(new_root->getLocation(), current_outlines_bbox), new_root));

            *old_root_it = std::move(new_root); // replace old root with new root
        } else {
            // [INTENT] Attach to another tree: reroot the existing tree from its closest node,
            // then graft it onto the target tree node. The old root entry is removed via
            // pop-back swap (destroys ordering).
            assert(ground.tree_node);
            assert(ground.tree_node != root_ptr);
            assert(!root_ptr->hasOffspring(ground.tree_node));
            assert(!ground.tree_node->hasOffspring(root_ptr));

            auto attach_ptr = root_ptr->closestNode(ground.tree_node->getLocation());
            attach_ptr->reroot();

            ground.tree_node->addChild(attach_ptr);

            // remove old root
            // [HAZARD H298] pop-back swap changes tree_roots iteration order.
            *old_root_it = std::move(tree_roots.back());
            tree_roots.pop_back();
        }
    }
}

// [INTENT] Dead code block — moveInside() from CuraEngine; not used in OrcaSlicer port.
// Computes the closest point on a polygon to a query point 'from', then moves 'from' inward or
// outward by 'distance'. Disabled with #if 0 because lineSegmentPolygonsIntersection() is used
// instead for boundary snapping.
// [HAZARD H299] This dead block contains a subtle bug: Vec2d perp() is used for outward
//               direction but the comment says "inward or outward depending on the sign of
//               [distance]" — the sign handling may be inconsistent. Not relevant since unused.
#if 0
/*!
    * Moves the point \p from onto the nearest polygon or leaves the point as-is, when the comb boundary is not within the root of \p max_dist2 distance.
    * Given a \p distance more than zero, the point will end up inside, and conversely outside.
    * When the point is already in/outside by more than \p distance, \p from is unaltered, but the polygon is returned.
    * When the point is in/outside by less than \p distance, \p from is moved to the correct place.
    * Implementation assumes moving inside, but moving outside should just as well be possible.
    *
    * \param polygons The polygons onto which to move the point
    * \param from[in,out] The point to move.
    * \param distance The distance by which to move the point.
    * \param max_dist2 The squared maximal allowed distance from the point to the nearest polygon.
    * \return The index to the polygon onto which we have moved the point.
 */
static unsigned int moveInside(const Polygons& polygons, Point& from, int distance, int64_t maxDist2)
{
    Point   ret                                    = from;
    int64_t bestDist2                              = std::numeric_limits<int64_t>::max();
    auto    bestPoly                               = static_cast<unsigned int>(-1);
    bool    is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        const Polygon &poly = polygons[poly_idx];
        if (poly.size() < 2)
            continue;
        Point p0 = poly[poly.size() - 2];
        Point p1 = poly.back();
        // because we compare with vSize2 here (no division by zero), we also need to compare by vSize2 inside the loop
        // to avoid integer rounding edge cases
        bool projected_p_beyond_prev_segment = (p1 - p0).cast<int64_t>().dot((from - p0).cast<int64_t>()) >= (p1 - p0).cast<int64_t>().squaredNorm();
        for (const Point& p2 : poly)
        {
            // X = A + Normal(B-A) * (((B-A) dot (P-A)) / VSize(B-A));
            //   = A +       (B-A) *  ((B-A) dot (P-A)) / VSize2(B-A);
            // X = P projected on AB
            const Point& a = p1;
            const Point& b = p2;
            const Point& p = from;
            Point ab = b - a;
            Point ap = p - a;
            int64_t ab_length2 = ab.cast<int64_t>().squaredNorm();
            if (ab_length2 <= 0) //A = B, i.e. the input polygon had two adjacent points on top of each other.
            {
                p1 = p2; //Skip only one of the points.
                continue;
            }
            int64_t dot_prod = ab.cast<int64_t>().dot(ap.cast<int64_t>());
            if (dot_prod <= 0) // x is projected to before ab
            {
                if (projected_p_beyond_prev_segment)
                { //  case which looks like:   > .
                    projected_p_beyond_prev_segment = false;
                    Point& x = p1;

                    int64_t dist2 = (x - p).cast<int64_t>().squaredNorm();
                    if (dist2 < bestDist2)
                    {
                        bestDist2 = dist2;
                        bestPoly = poly_idx;
                        if (distance == 0) { 
                            ret = x;
                        } else {
                            // inward direction irrespective of sign of [distance]
                            Point inward_dir = perp((ab.cast<double>().normalized() * scaled<double>(10.0) + (p1 - p0).cast<double>().normalized() * scaled<double>(10.0)).eval()).cast<coord_t>();
                            // MM2INT(10.0) to retain precision for the eventual normalization
                            ret = x + (inward_dir.cast<double>().normalized() * distance).cast<coord_t>();
                            is_already_on_correct_side_of_boundary = inward_dir.cast<int64_t>().dot((p - x).cast<int64_t>()) * distance >= 0;
                        }
                    }
                }
                else
                {
                    projected_p_beyond_prev_segment = false;
                    p0 = p1;
                    p1 = p2;
                    continue;
                }
            }
            else if (dot_prod >= ab_length2) // x is projected to beyond ab
            {
                projected_p_beyond_prev_segment = true;
                p0 = p1;
                p1 = p2;
                continue;
            }
            else
            { // x is projected to a point properly on the line segment (not onto a vertex). The case which looks like | .
                projected_p_beyond_prev_segment = false;
                Point x = (a.cast<int64_t>() + ab.cast<int64_t>() * dot_prod / ab_length2).cast<coord_t>();

                int64_t dist2 = (p - x).cast<int64_t>().squaredNorm();
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestPoly = poly_idx;
                    if (distance == 0) { ret = x; }
                    else
                    {
                        // inward or outward depending on the sign of [distance]
                        Vec2d inward_dir = perp((ab.cast<double>().normalized() * distance).eval());
                        ret = x + inward_dir.cast<coord_t>();
                        is_already_on_correct_side_of_boundary = inward_dir.dot((p - x).cast<double>()) >= 0;
                    }
                }
            }
            p0 = p1;
            p1 = p2;
        }
    }
    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        else
        {
            //            from = from; // original point stays unaltered. It is already inside by enough distance
        }
        return bestPoly;
    }
    else if (bestDist2 < maxDist2)
    {
        from = ret;
        return bestPoly;
    }
    return static_cast<unsigned int>(-1);
}
#endif

// [INTENT] Convert all trees in this layer to printable Polylines:
//   1. Call Node::convertToPolylines() on each root (DFS traversal, rand() to pick long-line child).
//   2. Clip all result lines to limit_to_outline via Clipper intersection_pl().
// [STATE] Read-only on tree_roots; creates and returns new Polylines.
// [HAZARD H292] Clipper intersection_pl() call may be costly for many trees + complex outline.
// [HAZARD H293] Non-deterministic order: rand() inside convertToPolylines selects which child
//               extends the "long line". Each run may produce differently ordered polylines.
//               This is acceptable for infill (no functional difference) but must be noted for ports.
Polylines Layer::convertToLines(const Polygons& limit_to_outline, const coord_t line_overlap) const
{
    if (tree_roots.empty())
        return {};

    Polylines result_lines;
    for (const auto& tree : tree_roots)
        tree->convertToPolylines(result_lines, line_overlap);

    return intersection_pl(result_lines, limit_to_outline);
}

} // namespace Slic3r::FillLightning
