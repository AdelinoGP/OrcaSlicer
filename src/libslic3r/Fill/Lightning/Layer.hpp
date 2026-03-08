// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Declares the Layer class, which represents one horizontal slice of the Lightning
// Infill structure. Each Layer owns a forest of NodeSPtr trees (tree_roots). The layer is
// responsible for three phases of the per-layer infill pipeline:
//   1. generateNewTrees()   — grow new trees bottom-up using a DistanceField
//   2. reconnectRoots()     — reattach roots that fell outside the new layer outline
//   3. convertToLines()     — convert finished trees into printable Polylines
// [COUPLING] Depends on EdgeGrid::Grid for fast outline-intersection tests, Node/DistanceField
// for tree construction, and ClipperUtils for polygon operations.

#ifndef LIGHTNING_LAYER_H
#define LIGHTNING_LAYER_H

#include "../../EdgeGrid.hpp"
#include "../../Polygon.hpp"

#include <memory>
#include <vector>
#include <list>
#include <unordered_map>
#include <optional>

namespace Slic3r::FillLightning {

class Node;
using NodeSPtr = std::shared_ptr<Node>;

// [INTENT] SparseNodeGrid: a spatial hash-map from grid-cell address (Point) to weak_ptr<Node>.
// Used for O(1) neighbourhood lookup when searching for the best grounding location.
// Key is a grid-cell coordinate (not a raw world coordinate); computed via to_grid_point().
// [MEMORY] Values are weak_ptr — nodes are owned by tree_roots vectors, not by this map.
//          Stale weak_ptrs (lock() == nullptr) may appear if a node is pruned; callers must check.
// [HAZARD H286] PointHash is not declared here; it must come from Polygon.hpp / Point.hpp.
//               A missing or ODR-violating hash specialisation will silently pick a bad bucket function.
using SparseNodeGrid = std::unordered_multimap<Point, std::weak_ptr<Node>, PointHash>;

// [INTENT] GroundingLocation: describes the attachment point for a new unsupported branch.
// Exactly one of tree_node or boundary_location is populated:
//   - tree_node non-null  → attach to an existing tree node
//   - boundary_location   → create a new root on the outline boundary
// [HAZARD H287] The assertion in p() (tree_node || boundary_location) is debug-only.
//               In release builds, a default-constructed GroundingLocation (both null/empty)
//               will silently call getLocation() on a null shared_ptr — UB/crash.
struct GroundingLocation
{
    NodeSPtr             tree_node;         //!< not null if the gounding location is on a tree
    std::optional<Point> boundary_location; //!< in case the gounding location is on the boundary
    Point                p() const;
};

/*!
 * A layer of the lightning fill.
 *
 * Contains the trees to be printed and propagated to the next layer below.
 */
// [INTENT] Layer is a plain data/algorithm class (no virtual dispatch, no inheritance).
// Lifetime: created and owned by Generator::m_lightning_layers (a std::vector<Layer>).
// [MEMORY] tree_roots owns all NodeSPtr instances for this layer through shared_ptr reference
//          counting.  Nodes may be shared temporarily across layers during propagation, but
//          Generator transfers ownership by moving / replacing entries.
// [CONCURRENCY] Layer methods are called single-threaded from Generator::generateTrees(),
//               EXCEPT getBestGroundingLocation() which uses tbb::parallel_for internally.
class Layer
{
public:
    // [STATE] The primary mutable state: a forest of trees for this layer.
    // Grows during generateNewTrees() and shrinks/rearranges during reconnectRoots().
    // [HAZARD H288] tree_roots is public — any caller can accidentally modify the forest without
    //               updating the SparseNodeGrid locator, leaving the locator stale.
    std::vector<NodeSPtr> tree_roots;

    // [INTENT] Main entry point for building new trees on this layer.
    // Constructs a DistanceField from current_overhang and current_outlines, then iterates
    // through unsupported cells in distance-priority order, attaching each to the best
    // grounding location (tree node or boundary point).
    // [STATE] Mutates tree_roots and a local SparseNodeGrid locator built from tree_roots.
    // [CONCURRENCY] Not thread-safe on 'this'; must be called from a single thread per Layer.
    //               Internally calls getBestGroundingLocation() which spawns TBB tasks.
    // [COUPLING] Requires a fully built EdgeGrid::Grid (outline_locator) computed from
    //            current_outlines by the caller (Generator::generateTrees).
    void generateNewTrees(const Polygons&              current_overhang,
                          const Polygons&              current_outlines,
                          const BoundingBox&           current_outlines_bbox,
                          const EdgeGrid::Grid&        outline_locator,
                          coord_t                      supporting_radius,
                          coord_t                      wall_supporting_radius,
                          const std::function<void()>& throw_on_cancel_callback);

    /*! Determine & connect to connection point in tree/outline.
     * \param min_dist_from_boundary_for_tree If the unsupported point is closer to the boundary than this then don't consider connecting it
     * to a tree
     */
    // [INTENT] For a given unsupported_location, find the best GroundingLocation by:
    //   1. Computing the closest point on current_outlines (O(N) vertex scan — no edge-grid shortcut).
    //   2. If the closest boundary is farther than wall_supporting_radius, searching existing tree
    //      nodes in a bounding box via TBB parallel_for for a cheaper weighted-distance attachment.
    // [STATE] Read-only on tree structures; result is a GroundingLocation value.
    // [CONCURRENCY] Uses tbb::parallel_for + std::mutex for result aggregation.
    //               The tie-breaking logic (current_dist_grid_addr) preserves determinism between
    //               parallel and sequential execution by always picking the lexicographically
    //               smallest grid address for equal distances.
    // [HAZARD H289] The O(N_vertices) closest-boundary scan in step 1 has no spatial index.
    //               For large outlines with thousands of vertices this is a hot-path bottleneck.
    GroundingLocation getBestGroundingLocation(const Point&          unsupported_location,
                                               const Polygons&       current_outlines,
                                               const BoundingBox&    current_outlines_bbox,
                                               const EdgeGrid::Grid& outline_locator,
                                               coord_t               supporting_radius,
                                               coord_t               wall_supporting_radius,
                                               const SparseNodeGrid& tree_node_locator,
                                               const NodeSPtr&       exclude_tree = nullptr);

    /*!
     * \param[out] new_child The new child node introduced
     * \param[out] new_root The new root node if one had been made
     * \return Whether a new root was added
     */
    // [INTENT] Attach unsupported_location to the given grounding location:
    //   - If grounding is on boundary: create a new root node + child, push root to tree_roots.
    //   - If grounding is on tree node: add child directly to that node.
    // [STATE] May push to tree_roots (boundary case). Mutates the chosen Node's child list.
    // [MEMORY] Creates NodeSPtr instances via Node::create() (make_shared under the hood).
    //          new_root and new_child are out-parameters; caller stores them in the SparseNodeGrid.
    // [HAZARD H290] No bounds check on tree_roots capacity; push_back may reallocate.
    //               The SparseNodeGrid locator is updated by the caller AFTER attach() returns —
    //               there is a brief window where tree_roots contains a node not in the locator.
    bool attach(const Point& unsupported_location, const GroundingLocation& ground, NodeSPtr& new_child, NodeSPtr& new_root);

    // [INTENT] After trees are propagated from the layer above, some root nodes may land outside
    // the new layer's outlines. This function re-attaches those orphaned roots either to the
    // outline boundary or to another tree, merging trees when profitable.
    // [STATE] Mutates tree_roots (replaces entries, pops back). Builds a fresh SparseNodeGrid.
    // [COUPLING] Calls getBestGroundingLocation() (TBB parallel) and lineSegmentPolygonsIntersection()
    //            from TreeNode.hpp.
    // [HAZARD H291] to_be_reconnected_tree_roots contains raw NodeSPtrs that must still be present
    //               in this->tree_roots; if a root was already removed before calling this function,
    //               std::find() returns end() and *old_root_it becomes UB (erase via end iterator).
    void reconnectRoots(std::vector<NodeSPtr>& to_be_reconnected_tree_roots,
                        const Polygons&        current_outlines,
                        const BoundingBox&     current_outlines_bbox,
                        const EdgeGrid::Grid&  outline_locator,
                        coord_t                supporting_radius,
                        coord_t                wall_supporting_radius);

    // [INTENT] Convert the forest into a set of printable Polylines, clipped to limit_to_outline.
    // Delegates to Node::convertToPolylines() per tree, then calls Clipper intersection_pl().
    // [STATE] Read-only on this. Creates and returns a new Polylines collection.
    // [HAZARD H292] intersection_pl() re-clips already-clipped lines each call. On layers with
    //               many trees and a complex outline polygon, this Clipper call dominates runtime.
    // [HAZARD H293] Non-deterministic polyline order because Node::convertToPolylines() uses
    //               rand() to choose which child extends the "long line" — see TreeNode.cpp H???.
    Polylines convertToLines(const Polygons& limit_to_outline, coord_t line_overlap) const;

    // [INTENT] Euclidean distance between boundary_loc and unsupported_location, returned as
    // coord_t (integer microns). Used as the base weight before the valence boost in getBestGroundingLocation().
    // [HAZARD H294] Cast from double norm() to coord_t truncates. For distances > ~2.1 km in
    //               microns (~INT32_MAX) this overflows. Benign in practice for printer bed sizes.
    coord_t getWeightedDistance(const Point& boundary_loc, const Point& unsupported_location);

    // [INTENT] Populate a SparseNodeGrid from all nodes currently in tree_roots.
    // Visits every node via Node::visitNodes() and inserts (grid_addr, weak_ptr<Node>) pairs.
    // [STATE] Writes to tree_node_locator (passed by reference from caller).
    // [COUPLING] Depends on locator_cell_size() defined in TreeNode.hpp.
    void fillLocator(SparseNodeGrid& tree_node_locator, const BoundingBox& current_outlines_bbox);
};

} // namespace Slic3r::FillLightning

#endif // LIGHTNING_LAYER_H
