// Calculate extents of the extrusions assigned to Print / PrintObject.
// The extents are used for assessing collisions of the print with the priming towers,
// to decide whether to pause the print after the priming towers are extruded
// to let the operator remove them from the print bed.
//
// [INTENT] This file is a pure query layer: no state is mutated.  All four public
// functions walk already-computed extrusion data structures and accumulate a
// floating-point AABB (BoundingBoxf) for collision-detection purposes.
//
// [COUPLING] Depends on:
//   - BoundingBox / BoundingBoxf (geometric AABB in scaled int32 and float mm respectively)
//   - ExtrusionEntity hierarchy (ExtrusionPath, ExtrusionLoop, ExtrusionMultiPath, ExtrusionEntityCollection)
//   - Layer / LayerRegion / SupportLayer (slicing output)
//   - Print / PrintObject / WipeTower (top-level print state)
//   - Geometry::deg2rad, Eigen::Translation2d + Rotation2Dd (2D rigid transform for wipe tower)
//
// [STATE] No file-scope mutable state.  All helpers are file-static functions.

#include "../BoundingBox.hpp"
#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"

#include "PrintExtents.hpp"
#include "WipeTower.hpp"

namespace Slic3r {

// [INTENT] Expands a polyline into an axis-aligned bounding box by growing each point
//          outward by `radius` (half extrusion width) in both X and Y.
// [STATE]  The returned bbox is in scaled integer coordinates (coord_t = int32 × 10^6).
// [HAZARD] `bbox.merge(polyline.points.front())` is called unconditionally before the
//          loop; if the polyline has exactly one point, the loop also visits it, so the
//          first point is merged twice — harmless but redundant.  A zero-point polyline
//          is guarded by the `! polyline.points.empty()` check, returning an undefined bbox.
static inline BoundingBox extrusion_polyline_extents(const Polyline& polyline, const coord_t radius)
{
    BoundingBox bbox;
    if (!polyline.points.empty())
        bbox.merge(polyline.points.front());
    for (const Point& pt : polyline.points) {
        bbox.min(0) = std::min(bbox.min(0), pt(0) - radius);
        bbox.min(1) = std::min(bbox.min(1), pt(1) - radius);
        bbox.max(0) = std::max(bbox.max(0), pt(0) + radius);
        bbox.max(1) = std::max(bbox.max(1), pt(1) + radius);
    }
    return bbox;
}

// [INTENT] Convert a single ExtrusionPath to a float-mm BoundingBoxf by:
//          1. computing a scaled-int BoundingBox via extrusion_polyline_extents (half-width radius)
//          2. unscaling both corners to float mm
// [STATE]  Returns an undefined (empty) BoundingBoxf if the path polyline is empty.
static inline BoundingBoxf extrusionentity_extents(const ExtrusionPath& extrusion_path)
{
    BoundingBox  bbox = extrusion_polyline_extents(extrusion_path.polyline, coord_t(scale_(0.5 * extrusion_path.width)));
    BoundingBoxf bboxf;
    if (!empty(bbox)) {
        bboxf.min     = unscale(bbox.min);
        bboxf.max     = unscale(bbox.max);
        bboxf.defined = true;
    }
    return bboxf;
}

// [INTENT] Merge all path segments of an ExtrusionLoop into a single float-mm BoundingBoxf.
// [COUPLING] ExtrusionLoop::paths is a std::vector<ExtrusionPath>; each path may have a
//            different width, so the radius is re-computed per path.
static inline BoundingBoxf extrusionentity_extents(const ExtrusionLoop& extrusion_loop)
{
    BoundingBox bbox;
    for (const ExtrusionPath& extrusion_path : extrusion_loop.paths)
        bbox.merge(extrusion_polyline_extents(extrusion_path.polyline, coord_t(scale_(0.5 * extrusion_path.width))));
    BoundingBoxf bboxf;
    if (!empty(bbox)) {
        bboxf.min     = unscale(bbox.min);
        bboxf.max     = unscale(bbox.max);
        bboxf.defined = true;
    }
    return bboxf;
}

// [INTENT] Same as ExtrusionLoop overload but for ExtrusionMultiPath (multiple disconnected
//          polylines sharing a single entity).
static inline BoundingBoxf extrusionentity_extents(const ExtrusionMultiPath& extrusion_multi_path)
{
    BoundingBox bbox;
    for (const ExtrusionPath& extrusion_path : extrusion_multi_path.paths)
        bbox.merge(extrusion_polyline_extents(extrusion_path.polyline, coord_t(scale_(0.5 * extrusion_path.width))));
    BoundingBoxf bboxf;
    if (!empty(bbox)) {
        bboxf.min     = unscale(bbox.min);
        bboxf.max     = unscale(bbox.max);
        bboxf.defined = true;
    }
    return bboxf;
}

// Forward declaration to allow mutual recursion with extrusionentity_extents(const ExtrusionEntityCollection&).
// [INTENT] Dispatcher that handles any ExtrusionEntity* via dynamic_cast chain.
static BoundingBoxf extrusionentity_extents(const ExtrusionEntity* extrusion_entity);

// [INTENT] Recursively merge all entities in an ExtrusionEntityCollection.
// [HAZARD] Collections can be nested (entities inside entities); no depth limit.
//          A deeply nested structure (e.g., collection of collections of collections)
//          will recurse deeply, though in practice the nesting is bounded to ~3 levels.
static inline BoundingBoxf extrusionentity_extents(const ExtrusionEntityCollection& extrusion_entity_collection)
{
    BoundingBoxf bbox;
    for (const ExtrusionEntity* extrusion_entity : extrusion_entity_collection.entities)
        bbox.merge(extrusionentity_extents(extrusion_entity));
    return bbox;
}

// [INTENT] Central dispatcher: resolves an ExtrusionEntity* to one of the four concrete
//          types (Path, Loop, MultiPath, Collection) via dynamic_cast and delegates.
// [HAZARD] Four sequential dynamic_cast calls per entity — O(1) each but constant factor
//          is non-trivial on platforms where RTTI uses type_info string comparison.
//          For callers that iterate thousands of entities (e.g. get_print_object_extrusions_extents)
//          this is a visible performance cost.
// [HAZARD] The final `throw Slic3r::RuntimeError` is followed by `return BoundingBoxf()` —
//          the return is unreachable dead code.  A refactor port should omit it.
static BoundingBoxf extrusionentity_extents(const ExtrusionEntity* extrusion_entity)
{
    if (extrusion_entity == nullptr)
        return BoundingBoxf();
    auto* extrusion_path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);
    if (extrusion_path != nullptr)
        return extrusionentity_extents(*extrusion_path);
    auto* extrusion_loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);
    if (extrusion_loop != nullptr)
        return extrusionentity_extents(*extrusion_loop);
    auto* extrusion_multi_path = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);
    if (extrusion_multi_path != nullptr)
        return extrusionentity_extents(*extrusion_multi_path);
    auto* extrusion_entity_collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);
    if (extrusion_entity_collection != nullptr)
        return extrusionentity_extents(*extrusion_entity_collection);
    throw Slic3r::RuntimeError("Unexpected extrusion_entity type in extrusionentity_extents()");
    return BoundingBoxf(); // [HAZARD] unreachable dead code
}

// [INTENT] Returns the AABB of all skirt extrusions for collision/overlap testing.
// [STATE]  BBS note: brim extrusion bbox was removed from this function.
//          Only the skirt is returned; if no skirt is configured the result is an
//          undefined (empty/not-defined) BoundingBoxf.
// [COUPLING] Calls extrusionentity_extents(print.skirt()) which recurses over
//            the ExtrusionEntityCollection returned by Print::skirt().
BoundingBoxf get_print_extrusions_extents(const Print& print)
{
    // BBS: usage of m_brim are deleted, the bbx of skrit is always larger than that of brim
    BoundingBoxf bbox(extrusionentity_extents(print.skirt()));
    return bbox;
}

// [INTENT] Computes the world-space AABB of all extrusions (perimeters + fills + support)
//          for a single PrintObject across all layers with print_z <= max_print_z.
// [STATE]  Iterates print_object.layers() in ascending print_z order; breaks on first
//          layer exceeding max_print_z (assumes sorted order — see [HAZARD] below).
// [HAZARD] The break condition is `layer->print_z > max_print_z`.  If layers are not
//          strictly sorted (e.g. due to a future multi-material height mismatch), some
//          layers below max_print_z could be skipped.  No assertion guards sort order.
// [HAZARD] `dynamic_cast<const ExtrusionEntityCollection*>(ee)` inside the fill loop
//          is unchecked — if a fill entity is not an ExtrusionEntityCollection the cast
//          returns nullptr and extrusionentity_extents(*nullptr) would be a null dereference.
//          In practice, fills are always ExtrusionEntityCollections, but there is no assert.
// [COUPLING] Each layer's bbox_this is translated by instance.shift before merging into
//            the global bbox — correctly handles multi-instance PrintObjects.
BoundingBoxf get_print_object_extrusions_extents(const PrintObject& print_object, const coordf_t max_print_z)
{
    BoundingBoxf bbox;
    for (const Layer* layer : print_object.layers()) {
        if (layer->print_z > max_print_z)
            break;
        BoundingBoxf bbox_this;
        for (const LayerRegion* layerm : layer->regions()) {
            bbox_this.merge(extrusionentity_extents(layerm->perimeters));
            for (const ExtrusionEntity* ee : layerm->fills.entities)
                // fill represents infill extrusions of a single island.
                bbox_this.merge(extrusionentity_extents(*dynamic_cast<const ExtrusionEntityCollection*>(ee)));
        }
        const SupportLayer* support_layer = dynamic_cast<const SupportLayer*>(layer);
        if (support_layer)
            for (const ExtrusionEntity* extrusion_entity : support_layer->support_fills.entities)
                bbox_this.merge(extrusionentity_extents(extrusion_entity));
        for (const PrintInstance& instance : print_object.instances()) {
            BoundingBoxf bbox_translated(bbox_this);
            bbox_translated.translate(unscale(instance.shift));
            bbox.merge(bbox_translated);
        }
    }
    return bbox;
}

// Returns a bounding box of a projection of the wipe tower for the layers <= max_print_z.
// The projection does not contain the priming regions.
//
// [INTENT] Applies the wipe tower's 2D rigid transform (translation + rotation) to every
//          extrusion segment in the tower's tool-change results, then inflates each segment
//          by half its width in both axes to produce a conservative AABB.
// [STATE]  Wipe tower extrusions are stored in tower-local coordinates (origin = tower XY,
//          no rotation applied).  This function applies the full Transform2d so the returned
//          bbox is in world (print bed) coordinates.
// [COUPLING] Reads wipe_tower_x/y via `get_at(plate_idx)` + plate_origin offset — uses the
//            multi-plate config accessor convention (get_at(0) = default if index OOB).
// [HAZARD] The wipe tower inflation uses a square delta `Vec2d(e.width, e.width)` rather
//          than per-axis (width × cos(angle), width × sin(angle)).  After rotation the
//          inflated box is not a tight fit — it over-estimates the bounding box on diagonal
//          extrusions.  For collision detection this is conservative (safe) but imprecise.
// [HAZARD] `tool_changes.front().print_z > max_print_z` breaks on the FIRST element of
//          the outer vector being too high.  If tool_changes is not sorted by print_z, some
//          layers below the cutoff will not be included in the bbox.
BoundingBoxf get_wipe_tower_extrusions_extents(const Print& print, const coordf_t max_print_z)
{
    // Wipe tower extrusions are saved as if the tower was at the origin with no rotation
    // We need to get position and angle of the wipe tower to transform them to actual position.
    int         plate_idx    = print.get_plate_index();
    Vec3d       plate_origin = print.get_plate_origin();
    double      wipe_tower_x = print.config().wipe_tower_x.get_at(plate_idx) + plate_origin(0);
    double      wipe_tower_y = print.config().wipe_tower_y.get_at(plate_idx) + plate_origin(1);
    Transform2d trafo        = Eigen::Translation2d(wipe_tower_x, wipe_tower_y) *
                        Eigen::Rotation2Dd(Geometry::deg2rad(print.config().wipe_tower_rotation_angle.value));

    BoundingBoxf bbox;
    for (const std::vector<WipeTower::ToolChangeResult>& tool_changes : print.wipe_tower_data().tool_changes) {
        if (!tool_changes.empty() && tool_changes.front().print_z > max_print_z)
            break;
        for (const WipeTower::ToolChangeResult& tcr : tool_changes) {
            for (size_t i = 1; i < tcr.extrusions.size(); ++i) {
                const WipeTower::Extrusion& e = tcr.extrusions[i];
                if (e.width > 0) {
                    Vec2d delta = 0.5 * Vec2d(e.width, e.width);
                    Vec2d p1    = trafo * (&e - 1)->pos.cast<double>();
                    Vec2d p2    = trafo * e.pos.cast<double>();
                    bbox.merge(p1.cwiseMin(p2) - delta);
                    bbox.merge(p1.cwiseMax(p2) + delta);
                }
            }
        }
    }
    return bbox;
}

// Returns a bounding box of the wipe tower priming extrusions.
//
// [INTENT] Priming extrusions happen before the main print; their spatial extent matters
//          for collision checks to see if the priming strip collides with other objects.
// [HAZARD] Unlike get_wipe_tower_extrusions_extents, NO Transform2d is applied here.
//          Priming extrusion positions are used directly as-is from WipeTower data.
//          This means the returned bbox is in tower-local / unrotated coordinates.
//          A caller that needs to compare priming bbox with world-space object bboxes
//          must apply the same wipe tower transform manually — this asymmetry is a
//          latent integration hazard.
// [HAZARD] `(&e - 1)->pos` pointer arithmetic assumes the priming extrusions vector
//          stores contiguous WipeTower::Extrusion objects and that index i-1 is valid.
//          The loop starts at i=1 so the -1 step is safe, but relies on the vector
//          not being empty (guarded by the outer `priming != nullptr` check).
// [COUPLING] Reads print.wipe_tower_data().priming which is a
//            std::unique_ptr<std::vector<WipeTower::ToolChangeResult>>.
//            Null check: `priming != nullptr` guards the outer loop.
BoundingBoxf get_wipe_tower_priming_extrusions_extents(const Print& print)
{
    BoundingBoxf bbox;
    if (print.wipe_tower_data().priming != nullptr) {
        for (const WipeTower::ToolChangeResult& tcr : *print.wipe_tower_data().priming) {
            for (size_t i = 1; i < tcr.extrusions.size(); ++i) {
                const WipeTower::Extrusion& e = tcr.extrusions[i];
                if (e.width > 0) {
                    const Vec2d& p1 = (&e - 1)->pos.cast<double>();
                    const Vec2d& p2 = e.pos.cast<double>();
                    bbox.merge(p1);
                    coordf_t radius = 0.5 * e.width;
                    bbox.min(0)     = std::min(bbox.min(0), std::min(p1(0), p2(0)) - radius);
                    bbox.min(1)     = std::min(bbox.min(1), std::min(p1(1), p2(1)) - radius);
                    bbox.max(0)     = std::max(bbox.max(0), std::max(p1(0), p2(0)) + radius);
                    bbox.max(1)     = std::max(bbox.max(1), std::max(p1(1), p2(1)) + radius);
                }
            }
        }
    }
    return bbox;
}

} // namespace Slic3r
