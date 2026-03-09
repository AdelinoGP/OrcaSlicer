// [INTENT] RetractWhenCrossingPerimeters answers a single boolean question per travel move:
//          "does this travel path stay entirely inside an internal (non-perimeter) region?"
//          If yes, retraction is suppressed for that move (no string is visible).
//
// [STATE]  The class caches the last visited Layer to avoid recomputing the AABB tree on
//          every travel query within the same layer.  Cache is invalidated by pointer
//          comparison: `m_layer != &layer`.
//
// [MEMORY] m_internal_islands stores raw const ExPolygon* pointers into
//          m_layer->regions()[*]->get_slices().surfaces[*].expolygon.
//          These pointers are valid only as long as m_layer's data is not rebuilt.
//          If the Layer object is destroyed or re-sliced while this object holds a
//          cache (e.g., live-recompute during multi-threaded slicing), all stored
//          pointers become dangling — no liveness check guards against this.
//
// [CONCURRENCY] Not thread-safe.  The mutable cache (m_layer, m_internal_islands,
//               m_aabbtree_internal_islands) must only be accessed from a single
//               thread.  GCode generation is single-threaded, so this is safe in
//               the current usage pattern.

#ifndef slic3r_RetractWhenCrossingPerimeters_hpp_
#define slic3r_RetractWhenCrossingPerimeters_hpp_

#include <vector>

#include "../AABBTreeIndirect.hpp"

namespace Slic3r {

// Forward declarations.
class ExPolygon;
class Layer;
class Polyline;

class RetractWhenCrossingPerimeters
{
public:
    // [INTENT] Returns true if the entire `travel` polyline lies inside at least one
    //          internal (non-bridge, non-perimeter) ExPolygon on the current layer.
    //          Used to suppress retraction for travel moves that never cross a perimeter.
    // [STATE]  Updates layer cache on first call for a new layer pointer.
    bool travel_inside_internal_regions(const Layer& layer, const Polyline& travel);

private:
    // Last object layer visited, for which a cache of internal islands was created.
    // [MEMORY] Raw pointer — validity depends on Layer lifetime outlasting this object's cache.
    const Layer* m_layer;
    // Internal islands only, referencing data owned by m_layer->regions()->surfaces().
    // [MEMORY] Raw const ExPolygon* pointers into Layer-owned storage — see file-level [MEMORY] note.
    std::vector<const ExPolygon*> m_internal_islands;
    // Search structure over internal islands (2D AABB tree, coord_t = int32 scaled coordinates).
    // [COUPLING] Rebuilt each time m_layer changes; tree nodes hold indices into m_internal_islands.
    using AABBTree = AABBTreeIndirect::Tree<2, coord_t>;
    AABBTree m_aabbtree_internal_islands;
};

} // namespace Slic3r

#endif // slic3r_RetractWhenCrossingPerimeters_hpp_
