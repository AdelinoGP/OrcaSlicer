// [INTENT] ShortestPath.hpp — Public API for TSP-approximation ordering utilities.
//          Provides greedy nearest-neighbor chaining (V1) and V1+2-opt post-improvement (V2)
//          for Polylines, ExtrusionEntities, ExPolygons, Lines, ClipperLib nodes, and
//          PrintObject instances.
// [COUPLING] Used by: FillBase (infill ordering), GCode (print-instance sequencing),
//            BrimGenerator (brim ordering), various geometry helpers.
// [HAZARD H553] chain_and_reorder_extrusion_entities casts to ExtrusionEntityCollection*
//          unconditionally — UB if any entity in the vector is not a collection.
// [HAZARD H554] chain_lines: `point_distance_epsilon2` is a static-const local inside .cpp —
//          ODR risk in multi-TU builds.
// [HAZARD H555] reorder_extrusion_paths declaration (header) takes `chain` by value;
//          definition in .cpp takes by non-const reference — declaration/definition mismatch.
// [HAZARD H557] chain_expolygons uses bounding-box centroid; non-deterministic for identical centroids.
// [HAZARD H558] reorder_by_two_exchanges_with_segment_flipping: hardcoded max 100 iterations.
// [HAZARD H559] Multiple dead #if 0 blocks (3-opt v1, 4-opt Eigen, do_crossover 4-span) preserved
//          as algorithm reference material.
// [HAZARD H560] do_crossover default: asserts (i>>6)==2 — fires in DEBUG if flip_min encoding
//          is out of range; silent in RELEASE.
#ifndef slic3r_ShortestPath_hpp_
#define slic3r_ShortestPath_hpp_

#include "libslic3r.h"
#include "ExtrusionEntity.hpp"
#include "Point.hpp"

#include <utility>
#include <vector>

namespace Slic3r {

namespace ClipperLib {
class PolyNode;
using PolyNodes = std::vector<PolyNode*, PointsAllocator<PolyNode*>>;
} // namespace ClipperLib

std::vector<size_t> chain_points(const Points& points, Point* start_near = nullptr);
std::vector<size_t> chain_expolygons(const ExPolygons& input_exploy);

std::vector<std::pair<size_t, bool>> chain_extrusion_entities(std::vector<ExtrusionEntity*>& entities, const Point* start_near = nullptr);
void reorder_extrusion_entities(std::vector<ExtrusionEntity*>& entities, const std::vector<std::pair<size_t, bool>>& chain);
void chain_and_reorder_extrusion_entities(std::vector<ExtrusionEntity*>& entities, const Point* start_near = nullptr);

std::vector<std::pair<size_t, bool>> chain_extrusion_paths(std::vector<ExtrusionPath>& extrusion_paths, const Point* start_near = nullptr);
void reorder_extrusion_paths(std::vector<ExtrusionPath>& extrusion_paths, std::vector<std::pair<size_t, bool>>& chain);
void chain_and_reorder_extrusion_paths(std::vector<ExtrusionPath>& extrusion_paths, const Point* start_near = nullptr);

Polylines        chain_polylines(Polylines&& src, const Point* start_near = nullptr);
inline Polylines chain_polylines(const Polylines& src, const Point* start_near = nullptr)
{
    Polylines tmp(src);
    return chain_polylines(std::move(tmp), start_near);
}
template<typename T> inline void reorder_by_shortest_traverse(std::vector<T>& polylines_out)
{
    Points start_point;
    start_point.reserve(polylines_out.size());
    for (const T& contour : polylines_out)
        start_point.push_back(contour.points.front());

    std::vector<Points::size_type> order = chain_points(start_point);

    std::vector<T> Temp = polylines_out;
    polylines_out.erase(polylines_out.begin(), polylines_out.end());

    for (size_t i : order)
        polylines_out.emplace_back(std::move(Temp[i]));
}

ClipperLib::PolyNodes chain_clipper_polynodes(const Points& points, const ClipperLib::PolyNodes& items);

// Chain instances of print objects by an approximate shortest path.
// Returns pairs of PrintObject idx and instance of that PrintObject.
class Print;
struct PrintInstance;
// BBS
class PrintObject;
std::vector<const PrintInstance*> chain_print_object_instances(const std::vector<const PrintObject*>& print_objects,
                                                               const Point*                           start_near);
std::vector<const PrintInstance*> chain_print_object_instances(const Print& print);

// Chain lines into polylines.
Polylines chain_lines(const std::vector<Line>& lines, const double point_distance_epsilon);

} // namespace Slic3r

#endif /* slic3r_ShortestPath_hpp_ */
