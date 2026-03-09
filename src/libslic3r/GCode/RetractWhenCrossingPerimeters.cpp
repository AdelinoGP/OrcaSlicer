// [INTENT] Implements RetractWhenCrossingPerimeters::travel_inside_internal_regions.
//
// The algorithm:
//   1. On first call for a new layer (m_layer pointer changed): rebuild the cache of
//      internal surface ExPolygon* pointers and the AABB tree over their bounding boxes.
//   2. For the given travel Polyline: traverse the AABB tree to find candidate internal
//      islands whose bboxes intersect the travel bbox.
//   3. For each candidate: clip the island polygons to the travel bbox and check whether
//      diff_pl(travel, clipped_island) is empty (i.e. travel is entirely inside island).
//   4. Return true on first island found that contains the full travel path.
//
// [COUPLING] Depends on:
//   - ClipperUtils::clip_clipper_polygons_with_subject_bbox — Clipper1-based polygon clip
//   - diff_pl — Clipper-based polyline difference
//   - AABBTreeIndirect::traverse — generic tree traversal with inner/leaf predicates
//   - Surface::is_internal() — returns true for non-perimeter, non-bridge, non-top/bottom surfaces
//
// [CONCURRENCY] Not thread-safe (mutable cache shared across calls).
//               Safe in current usage because G-code generation is single-threaded.

#include "../ClipperUtils.hpp"
#include "../Layer.hpp"
#include "../Polyline.hpp"

#include "RetractWhenCrossingPerimeters.hpp"

namespace Slic3r {

// [INTENT] Returns true if the entire travel polyline lies within at least one internal
//          ExPolygon on the given layer.  "Internal" means the surface is not a top/bottom
//          solid, not a bridge, not a perimeter — it is a region safely enclosed by walls.
//          If true, retraction can be safely suppressed for this travel move.
//
// [STATE]  Cache invalidation: `m_layer != &layer` triggers a full rebuild of:
//            - m_internal_islands (ExPolygon* refs into layer surfaces)
//            - m_aabbtree_internal_islands (2D AABB tree of island bboxes)
//          The old cache is cleared first (m_internal_islands.clear(), m_aabbtree_internal_islands.clear()).
//
// [MEMORY] m_internal_islands stores raw const ExPolygon* into layer-owned storage.
//          Validity is guaranteed only while m_layer points to a live, unmodified Layer.
//          A layer rebuild between cache fill and tree traversal would cause dangling pointer use.
//
// [HAZARD H854] bbox_travel is offset by SCALED_EPSILON after being used to construct
//               bbox_travel_eigen for the AABB tree traversal.  The AABB traversal uses
//               the un-expanded bbox_travel_eigen, but the leaf predicate uses the expanded
//               bbox_travel (passed by reference into the lambda).  A travel move that is
//               exactly on an island boundary would fail the AABB traversal (no node bbox
//               intersects) but could pass the leaf clip test if the lambda were ever reached.
//               In practice this edge case is benign, but the asymmetry between the two
//               bbox variables is a maintenance trap: modifying one without the other changes
//               which islands are considered candidates.
//
// [HAZARD H855] diff_pl(travel, clipped) is called with the FULL travel polyline on every
//               candidate island, not just the subset of the polyline within the island bbox.
//               For a long travel path that crosses many island bboxes, each candidate island
//               runs a full Clipper polyline difference.  The test is O(N_candidates * |travel|)
//               in Clipper operations.  For dense prints with many internal islands this can
//               be a hot path.
//
// [COUPLING] AABBTreeIndirect::traverse callbacks:
//   - Inner node predicate: returns true if the travel bbox intersects the node's bbox
//     (continue descent).
//   - Leaf predicate: performs the actual poly-clip and diff test; returns false to STOP
//     traversal when a containing island is found (short-circuit optimization).
bool RetractWhenCrossingPerimeters::travel_inside_internal_regions(const Layer& layer, const Polyline& travel)
{
    if (m_layer != &layer) {
        // Update cache.
        m_layer = &layer;
        m_internal_islands.clear();
        m_aabbtree_internal_islands.clear();
        // Collect expolygons of internal slices.
        for (const LayerRegion* layerm : layer.regions())
            for (const Surface& surface : layerm->get_slices().surfaces)
                if (surface.is_internal())
                    m_internal_islands.emplace_back(&surface.expolygon);
        // Calculate bounding boxes of internal slices.
        std::vector<AABBTreeIndirect::BoundingBoxWrapper> bboxes;
        bboxes.reserve(m_internal_islands.size());
        for (size_t i = 0; i < m_internal_islands.size(); ++i)
            bboxes.emplace_back(i, get_extents(*m_internal_islands[i]));
        // Build AABB tree over bounding boxes of internal slices.
        m_aabbtree_internal_islands.build_modify_input(bboxes);
    }

    // [STATE] bbox_travel_eigen is built BEFORE bbox_travel.offset() is applied.
    //         The AABB tree traversal uses bbox_travel_eigen (un-expanded).
    //         The leaf predicate uses bbox_travel (after offset by SCALED_EPSILON).
    //         See [HAZARD H854] above.
    BoundingBox           bbox_travel = get_extents(travel);
    AABBTree::BoundingBox bbox_travel_eigen{bbox_travel.min, bbox_travel.max};
    int                   result = -1;
    bbox_travel.offset(SCALED_EPSILON);
    AABBTreeIndirect::traverse(
        m_aabbtree_internal_islands,
        // Inner node predicate: descend if travel bbox intersects node bbox.
        [&bbox_travel_eigen](const AABBTree::Node& node) { return bbox_travel_eigen.intersects(node.bbox); },
        // Leaf predicate: test if travel is fully inside this island.
        // Returns false to stop traversal once a containing island is found.
        [&travel, &bbox_travel, &result, &islands = m_internal_islands](const AABBTree::Node& node) {
            assert(node.is_leaf());
            assert(node.is_valid());
            Polygons clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*islands[node.idx], bbox_travel);
            if (diff_pl(travel, clipped).empty()) {
                // Travel path is completely inside an "internal" island. Don't retract.
                result = int(node.idx);
                // Stop traversal.
                return false;
            }
            // Continue traversal.
            return true;
        });
    return result != -1;
}

} // namespace Slic3r
