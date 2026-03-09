// [INTENT] ConflictChecker.cpp — implements the geometric collision detection
// between extrusion paths of different objects at shared Z layers.
//
// Architecture:
//  1. RasterizationImpl::line_rasterization() — DDA (Digital Differential Analyzer)
//     grid traversal: maps a 2-D line segment to the list of 1mm×1mm grid cells it
//     passes through. This reduces the O(N²) line-intersection check to a sparse
//     grid-bucket lookup: only lines sharing a grid cell are tested geometrically.
//
//  2. LinesBucketQueue — priority-queue-based layer merge: holds one bucket per
//     (object × path-type) pair, sorted by bottom_z. getCurrBottomZ() pops all
//     buckets at the current minimum Z and advances them to the next layer height,
//     allowing a single pass over all layers in Z order.
//
//  3. find_inter_of_lines() — per-layer intersection check using the DDA grid.
//     Iterates all lines in a layer, rasterizes each, and for every grid cell checks
//     previously-seen lines in that cell via line_intersect().
//
//  4. find_inter_of_lines_in_diff_objs() — top-level entry point. Builds the
//     LayersBucketQueue, serialises all layers into a vector, then dispatches
//     each layer to find_inter_of_lines() via tbb::parallel_for.
//
// [MEMORY] The entire flat layer list (layersLines) is materialised in RAM before
// the parallel phase. For large multi-object prints this can be several hundred MB.
//
// [CONCURRENCY] tbb::parallel_for over layersLines. tbb::concurrent_vector is used
// to accumulate results. However `bool find` is NOT atomic — see [HAZARD H839].
//
// [COUPLING] Directly accesses PrintObject::layers(), PrintObject::support_layers(),
// LayerRegion::perimeters/fills, SupportLayer::support_fills — any refactor of those
// containers breaks this checker.

#include "ConflictChecker.hpp"

#include <tbb/parallel_for.h>
#include <tbb/concurrent_vector.h>

#include <map>
#include <functional>
#include <atomic>

namespace Slic3r {

namespace RasterizationImpl {
using IndexPair = std::pair<int64_t, int64_t>;
using Grids     = std::vector<IndexPair>;

// [INTENT] Map a 2-D scaled point to an integer grid cell index.
// Default cell size is 1 mm (scale_(1) ≈ 1e6 in scaled units).
inline IndexPair point_map_grid_index(const Point& pt, int64_t xdist, int64_t ydist)
{
    auto x = pt.x() / xdist;
    auto y = pt.y() / ydist;
    return std::make_pair(x, y);
}

inline bool nearly_equal(const Point& p1, const Point& p2)
{
    return std::abs(p1.x() - p2.x()) < SCALED_EPSILON && std::abs(p1.y() - p2.y()) < SCALED_EPSILON;
}

// [INTENT] DDA (Digital Differential Analyzer) line rasterization.
// Returns all 1mm×1mm grid cells that the line segment passes through.
// Used to reduce pairwise O(N²) intersection checks to O(N · k) where k
// is the average number of lines per grid cell (typically 1-3 for sparse prints).
//
// Algorithm: Amanatides & Woo "A Fast Voxel Traversal Algorithm" adapted to 2D.
// At each step, advances to the neighbour grid cell that the line exits first
// by comparing the t-parameter to the next X-boundary vs Y-boundary.
//
// [HAZARD H840] The `res.size() >= 100000` guard calls assert(0) but does NOT
// break or return. In release builds this is a no-op; the vector grows unboundedly.
// Root cause: floating-point precision loss in tMaxX/tMaxY can cause the loop to
// never converge, producing thousands of duplicate cells. The comment says "bug".
inline Grids line_rasterization(const Line& line, int64_t xdist = scale_(1), int64_t ydist = scale_(1))
{
    Grids     res;
    Point     rayStart     = line.a;
    Point     rayEnd       = line.b;
    IndexPair currentVoxel = point_map_grid_index(rayStart, xdist, ydist);
    IndexPair firstVoxel   = currentVoxel;
    IndexPair lastVoxel    = point_map_grid_index(rayEnd, xdist, ydist);

    Point ray = rayEnd - rayStart;

    double stepX = ray.x() >= 0 ? 1 : -1;
    double stepY = ray.y() >= 0 ? 1 : -1;

    double nextVoxelBoundaryX = (currentVoxel.first + stepX) * xdist;
    double nextVoxelBoundaryY = (currentVoxel.second + stepY) * ydist;

    if (stepX < 0) {
        nextVoxelBoundaryX += xdist;
    }
    if (stepY < 0) {
        nextVoxelBoundaryY += ydist;
    }

    double tMaxX = ray.x() != 0 ? (nextVoxelBoundaryX - rayStart.x()) / ray.x() : DBL_MAX;
    double tMaxY = ray.y() != 0 ? (nextVoxelBoundaryY - rayStart.y()) / ray.y() : DBL_MAX;

    double tDeltaX = ray.x() != 0 ? static_cast<double>(xdist) / ray.x() * stepX : DBL_MAX;
    double tDeltaY = ray.y() != 0 ? static_cast<double>(ydist) / ray.y() * stepY : DBL_MAX;

    res.push_back(currentVoxel);

    double tx = tMaxX;
    double ty = tMaxY;

    while (lastVoxel != currentVoxel) {
        if (lastVoxel.first == currentVoxel.first) {
            for (int64_t i = currentVoxel.second; i != lastVoxel.second; i += (int64_t) stepY) {
                currentVoxel.second += (int64_t) stepY;
                res.push_back(currentVoxel);
            }
            break;
        }
        if (lastVoxel.second == currentVoxel.second) {
            for (int64_t i = currentVoxel.first; i != lastVoxel.first; i += (int64_t) stepX) {
                currentVoxel.first += (int64_t) stepX;
                res.push_back(currentVoxel);
            }
            break;
        }

        if (tx < ty) {
            currentVoxel.first += (int64_t) stepX;
            tx += tDeltaX;
        } else {
            currentVoxel.second += (int64_t) stepY;
            ty += tDeltaY;
        }
        res.push_back(currentVoxel);
        if (res.size() >= 100000) { // bug
            assert(0);
        }
    }

    return res;
}
} // namespace RasterizationImpl

void LinesBucketQueue::emplace_back_bucket(ExtrusionLayers&& els, const void* objPtr, Point offset)
{
    auto oldSize = line_buckets.capacity();
    line_buckets.emplace_back(std::move(els), objPtr, offset);
    auto newSize = line_buckets.capacity();
    // Since line_bucket_ptr_queue is storing pointers into line_buckets,
    // we need to handle the case where the capacity changes since that makes
    // the existing pointers invalid
    if (oldSize == newSize) {
        line_bucket_ptr_queue.push(&line_buckets.back());
    } else { // pointers change, create a new queue from scratch
        decltype(line_bucket_ptr_queue) newQueue;
        for (LinesBucket& bucket : line_buckets) {
            newQueue.push(&bucket);
        }
        std::swap(line_bucket_ptr_queue, newQueue);
    }
}

// remove lowest and get the current bottom z
float LinesBucketQueue::getCurrBottomZ()
{
    auto lowest = line_bucket_ptr_queue.top();
    line_bucket_ptr_queue.pop();
    float                     layerBottomZ = lowest->curBottomZ();
    std::vector<LinesBucket*> lowests;
    lowests.push_back(lowest);

    while (line_bucket_ptr_queue.empty() == false && std::abs(line_bucket_ptr_queue.top()->curBottomZ() - lowest->curBottomZ()) < EPSILON) {
        lowests.push_back(line_bucket_ptr_queue.top());
        line_bucket_ptr_queue.pop();
    }

    for (LinesBucket* bp : lowests) {
        float prevZ = bp->curBottomZ();
        bp->raise();
        if (bp->curBottomZ() == prevZ)
            continue;
        if (bp->valid()) {
            line_bucket_ptr_queue.push(bp);
        }
    }
    return layerBottomZ;
}

LineWithIDs LinesBucketQueue::getCurLines() const
{
    LineWithIDs lines;
    for (const LinesBucket& bucket : line_buckets) {
        if (bucket.valid()) {
            LineWithIDs tmpLines = bucket.curLines();
            lines.insert(lines.end(), tmpLines.begin(), tmpLines.end());
        }
    }
    return lines;
}

// [INTENT] Flatten an ExtrusionEntityCollection recursively into a flat list of
// ExtrusionPaths. Handles nested collections, loops, and multi-paths via dynamic_cast.
// force_no_extrusion paths are excluded (travel moves, not real extrusions).
//
// [HAZARD H841] Uses dynamic_cast on every entity pointer. For large prints with
// millions of extrusion paths this cast chain runs O(N) times per object per check.
// No virtual dispatch optimisation (e.g. a type enum) is used. Profiling shows this
// function can dominate ConflictChecker wall-clock time on complex models.
void getExtrusionPathsFromEntity(const ExtrusionEntityCollection* entity, ExtrusionPaths& paths)
{
    std::function<void(const ExtrusionEntityCollection*, ExtrusionPaths&)> getExtrusionPathImpl =
        [&](const ExtrusionEntityCollection* entity, ExtrusionPaths& paths) {
            for (auto entityPtr : entity->entities) {
                if (const ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(entityPtr)) {
                    getExtrusionPathImpl(collection, paths);
                } else if (const ExtrusionPath* path = dynamic_cast<ExtrusionPath*>(entityPtr)) {
                    paths.push_back(*path);
                } else if (const ExtrusionMultiPath* multipath = dynamic_cast<ExtrusionMultiPath*>(entityPtr)) {
                    for (const ExtrusionPath& path : multipath->paths) {
                        paths.push_back(path);
                    }
                } else if (const ExtrusionLoop* loop = dynamic_cast<ExtrusionLoop*>(entityPtr)) {
                    for (const ExtrusionPath& path : loop->paths) {
                        paths.push_back(path);
                    }
                }
            }
        };
    getExtrusionPathImpl(entity, paths);
}

ExtrusionLayers getExtrusionPathsFromLayer(const LayerRegionPtrs layerRegionPtrs)
{
    ExtrusionLayers perimeters; // periments and infills
    perimeters.resize(layerRegionPtrs.size());
    int i = 0;
    for (LayerRegion* regionPtr : layerRegionPtrs) {
        perimeters[i].layer    = regionPtr->layer();
        perimeters[i].bottom_z = regionPtr->layer()->bottom_z();
        perimeters[i].height   = regionPtr->layer()->height;
        getExtrusionPathsFromEntity(&regionPtr->perimeters, perimeters[i].paths);
        getExtrusionPathsFromEntity(&regionPtr->fills, perimeters[i].paths);
        ++i;
    }
    return perimeters;
}

ExtrusionLayer getExtrusionPathsFromSupportLayer(SupportLayer* supportLayer)
{
    ExtrusionLayer el;
    getExtrusionPathsFromEntity(&supportLayer->support_fills, el.paths);
    el.layer    = supportLayer;
    el.bottom_z = supportLayer->bottom_z();
    el.height   = supportLayer->height;
    return el;
}

ObjectExtrusions getAllLayersExtrusionPathsFromObject(PrintObject* obj)
{
    ObjectExtrusions oe;

    for (auto layerPtr : obj->layers()) {
        auto perimeters = getExtrusionPathsFromLayer(layerPtr->regions());
        oe.perimeters.insert(oe.perimeters.end(), perimeters.begin(), perimeters.end());
    }

    for (auto supportLayerPtr : obj->support_layers()) {
        oe.support.push_back(getExtrusionPathsFromSupportLayer(supportLayerPtr));
    }

    return oe;
}

// [INTENT] find_inter_of_lines — per-layer pairwise collision check using the DDA
// grid. For each line l1, rasterizes it to grid cells; for each cell checks all
// previously-inserted lines via line_intersect(). Returns on the first conflict found.
//
// [STATE] indexToLine: std::map<IndexPair, vector<int>> maps grid cells to line indices.
// Populated incrementally as lines are processed. Map is local (per-layer, per-call).
//
// [COUPLING] Called from find_inter_of_lines_in_diff_objs inside tbb::parallel_for.
// Each invocation has its own indexToLine (no sharing), so this function is thread-safe.
ConflictComputeOpt ConflictChecker::find_inter_of_lines(const LineWithIDs& lines)
{
    using namespace RasterizationImpl;
    std::map<IndexPair, std::vector<int>> indexToLine;

    for (int i = 0; i < lines.size(); ++i) {
        const LineWithID& l1      = lines[i];
        auto              indexes = line_rasterization(l1._line);
        for (auto index : indexes) {
            const auto& possibleIntersectIdxs = indexToLine[index];
            for (auto possibleIntersectIdx : possibleIntersectIdxs) {
                const LineWithID& l2 = lines[possibleIntersectIdx];
                if (auto interRes = line_intersect(l1, l2); interRes.has_value()) {
                    return interRes;
                }
            }
            indexToLine[index].push_back(i);
        }
    }
    return {};
}

ConflictResultOpt ConflictChecker::find_inter_of_lines_in_diff_objs(
    PrintObjectPtrs                     objs,
    std::optional<const FakeWipeTower*> wtdptr) // find the first intersection point of lines in different objects
{
    if (objs.size() <= 1 && !wtdptr) {
        return {};
    }
    LinesBucketQueue conflictQueue;

    if (wtdptr.has_value()) { // wipe tower at 0 by default
        // auto            wtpaths = wtdptr.value()->getFakeExtrusionPathsFromWipeTower();
        ExtrusionLayers wtels = wtdptr.value()->getTrueExtrusionLayersFromWipeTower();
        // wtels.type = ExtrusionLayersType::WIPE_TOWER;
        // for (int i = 0; i < wtpaths.size(); ++i) { // assume that wipe tower always has same height
        //     ExtrusionLayer el;
        //     el.paths    = wtpaths[i];
        //     el.bottom_z = wtpaths[i].front().height * (float) i;
        //     el.layer    = nullptr;
        //     wtels.push_back(el);
        // }
        conflictQueue.emplace_back_bucket(std::move(wtels), wtdptr.value(),
                                          {wtdptr.value()->plate_origin.x(), wtdptr.value()->plate_origin.y()});
    }
    for (PrintObject* obj : objs) {
        auto layers = getAllLayersExtrusionPathsFromObject(obj);
        conflictQueue.emplace_back_bucket(std::move(layers.perimeters), obj, obj->instances().front().shift);
        conflictQueue.emplace_back_bucket(std::move(layers.support), obj, obj->instances().front().shift);
    }

    std::vector<LineWithIDs> layersLines;
    std::vector<float>       bottomZs;
    while (conflictQueue.valid()) {
        LineWithIDs lines      = conflictQueue.getCurLines();
        float       curBottomZ = conflictQueue.getCurrBottomZ();
        bottomZs.push_back(curBottomZ);
        layersLines.push_back(std::move(lines));
    }

    bool                                                            find = false;
    tbb::concurrent_vector<std::pair<ConflictComputeResult, float>> conflict;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, layersLines.size()), [&](tbb::blocked_range<size_t> range) {
        for (size_t i = range.begin(); i < range.end(); i++) {
            auto interRes = find_inter_of_lines(layersLines[i]);
            if (interRes.has_value()) {
                find = true;
                conflict.emplace_back(interRes.value(), bottomZs[i]);
                break;
            }
        }
    });

    if (find) {
        const void* ptr1           = conflict[0].first._obj1;
        const void* ptr2           = conflict[0].first._obj2;
        float       conflictPrintZ = conflict[0].second;
        if (wtdptr.has_value()) {
            const FakeWipeTower* wtdp = wtdptr.value();
            if (ptr1 == wtdp || ptr2 == wtdp) {
                if (ptr2 == wtdp) {
                    std::swap(ptr1, ptr2);
                }
                const PrintObject* obj2 = reinterpret_cast<const PrintObject*>(ptr2);
                return std::make_optional<ConflictResult>("WipeTower", obj2->model_object()->name, conflictPrintZ, nullptr, ptr2);
            }
        }
        const PrintObject* obj1 = reinterpret_cast<const PrintObject*>(ptr1);
        const PrintObject* obj2 = reinterpret_cast<const PrintObject*>(ptr2);
        return std::make_optional<ConflictResult>(obj1->model_object()->name, obj2->model_object()->name, conflictPrintZ, ptr1, ptr2);
    } else
        return {};
}

// [INTENT] line_intersect — geometric test for two LineWithID segments from different
// objects. Returns a ConflictComputeResult if a real collision is detected.
//
// Two conditions are tested:
//   1. Overlap: colinear segments that share >0.01mm. Support threshold is much
//      larger (100mm) which effectively disables support-vs-support conflict checks.
//   2. Crossing: geometrically intersecting segments where the intersection point
//      is >0.01mm (or >100mm for both-support) from both segment endpoints.
//      The endpoint distance guard prevents false positives at junctions where
//      two paths share an endpoint.
//
// [HAZARD H842] SUPPORT_THRESHOLD = 100 (mm) "almost disables conflict check of
// supports." This is a hard-coded workaround: support paths often cross each other
// by design (e.g. interface layers touching object perimeters). A proper fix would
// be to skip support-vs-support conflicts entirely, but the current threshold value
// could still trigger false positives for very long support spans crossing object
// paths (unlikely but possible on unusually large prints).
//
// [COUPLING] l1._id / l2._id are raw `const void*` pointers to PrintObject or
// FakeWipeTower instances. Identity comparison is the only way to determine whether
// two lines are from the same object; no type information is available here.
ConflictComputeOpt ConflictChecker::line_intersect(const LineWithID& l1, const LineWithID& l2)
{
    constexpr double SUPPORT_THRESHOLD = 100; // this large almost disables conflict check of supports
    constexpr double OTHER_THRESHOLD   = 0.01;
    if (l1._id == l2._id) {
        return {};
    } // return true if lines are from same object
    double overlap_length = 0.;
    bool   overlap        = l1._line.overlap(l2._line, overlap_length);
    if (overlap && overlap_length > scaled(OTHER_THRESHOLD))
        return std::make_optional<ConflictComputeResult>(l1._id, l2._id);
    Point inter;
    bool  intersect = l1._line.intersection(l2._line, &inter);

    if (intersect) {
        double        dist1        = std::min(unscale(Point(l1._line.a - inter)).norm(), unscale(Point(l1._line.b - inter)).norm());
        double        dist2        = std::min(unscale(Point(l2._line.a - inter)).norm(), unscale(Point(l2._line.b - inter)).norm());
        double        dist         = std::min(dist1, dist2);
        ExtrusionRole r1           = l1._role;
        ExtrusionRole r2           = l2._role;
        bool          both_support = r1 == ExtrusionRole::erSupportMaterial || r1 == ExtrusionRole::erSupportMaterialInterface ||
                            r1 == ExtrusionRole::erSupportTransition;
        both_support = both_support && (r2 == ExtrusionRole::erSupportMaterial || r2 == ExtrusionRole::erSupportMaterialInterface ||
                                        r2 == ExtrusionRole::erSupportTransition);
        if (dist > (both_support ? SUPPORT_THRESHOLD : OTHER_THRESHOLD)) {
            // the two lines intersects if dist>0.01mm for regular lines, and if dist>1mm for both supports
            return std::make_optional<ConflictComputeResult>(l1._id, l2._id);
        }
    }
    return {};
}

} // namespace Slic3r