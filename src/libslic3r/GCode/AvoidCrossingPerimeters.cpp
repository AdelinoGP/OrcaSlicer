// [INTENT] AvoidCrossingPerimeters.cpp — Travel-path planner that routes nozzle moves along
// perimeter contours instead of cutting across them, minimising stringing and ooze artifacts.
//
// ARCHITECTURE OVERVIEW
// ─────────────────────
//  Public entry point: AvoidCrossingPerimeters::travel_to()  (called once per travel move from GCode.cpp)
//  Per-layer setup:    AvoidCrossingPerimeters::init_layer()  (called at the start of each new layer)
//
//  Two routing modes:
//    m_internal  — travel inside the current object's layer; boundaries are inward-offset perimeter
//                  contours of the *current* PrintObject (computed by get_boundary()).
//    m_external  — travel between objects; boundaries are expanded holes of *all* PrintObjects at
//                  this Z (computed by get_boundary_external()).  Activated when m_use_external_mp or
//                  m_use_external_mp_once is set.
//
// [STATE] The Boundary struct (see AvoidCrossingPerimeters.hpp) holds:
//    boundaries        — Polygons that define crossing-forbidden zones.
//    grid              — EdgeGrid::Grid for O(1) spatial queries against those polygons.
//    boundaries_params — Per-polygon cumulative arc-length lookup table (used by get_shortest_direction).
//    bbox              — Floating-point bounding box; used for fast out-of-bounds early-exit.
//
// ALGORITHM (avoid_perimeters_inner, ~line 549)
// ─────────────────────────────────────────────
//  1. Fire AllIntersectionsVisitor along the direct travel line → sorted Intersection list.
//  2. Extend intersections via extend_for_closest_lines() for cases where start/end are already
//     inside an offset boundary (no intersection found by direct ray cast).
//  3. For each entry/exit pair on the same boundary, call get_shortest_direction() and walk
//     along the polygon forward or backward, emitting offset waypoints (SCALED_EPSILON inward).
//  4. Simplify the resulting TravelPoint path with simplify_travel() — greedy skip using
//     FirstIntersectionVisitor to verify each shortcut doesn't re-introduce a crossing.
//
// DEAD CODE WARNING  [HAZARD-78]
//    Lines 1340–1729 are wrapped in `#if 0`.  This block contains an entire parallel
//    implementation: a second avoid_perimeters_inner, a simplify_travel_heuristics optimiser,
//    a second avoid_perimeters, and a second init_layer / travel_to with different boundary logic.
//    It diverged from the active code without coordinated maintenance.
//
// COORDINATE SYSTEM
//    All points are in scaled integer space (coord_t, 1 unit = 1e-6 mm).
//    travel_to() applies a scaled_origin offset to convert between object-space and world-space
//    when use_external is true.
//
// [COUPLING] Reads gcodegen.layer(), gcodegen.last_pos(), gcodegen.config(), gcodegen.writer()
//            and gcodegen.origin() — tightly coupled to GCode state machine.
//
// [CONCURRENCY] No TBB parallelism; called from single GCode generation thread.
//
// Key callers:
//    GCode::travel_to()  →  AvoidCrossingPerimeters::travel_to()
//    GCode::layer_change_oop()  →  AvoidCrossingPerimeters::init_layer()

#include "../Layer.hpp"
#include "../GCode.hpp"
#include "../EdgeGrid.hpp"
#include "../Print.hpp"
#include "../Polygon.hpp"
#include "../ExPolygon.hpp"
#include "../Geometry.hpp"
#include "../ClipperUtils.hpp"
#include "../SVG.hpp"
#include "AvoidCrossingPerimeters.hpp"

#include <numeric>
#include <unordered_set>
#include <boost/range/adaptor/reversed.hpp>

namespace Slic3r {

// [INTENT] A waypoint in the rerouted travel path.
// border_idx == -1 means the point is free-space (start/end of travel or interior detour).
// Points with border_idx >= 0 lie on, or just inward of, a boundary polygon.
// [STATE] do_not_remove prevents simplify_travel() from collapsing this waypoint — used for
//         synthetic points injected by extend_for_closest_lines().
struct TravelPoint
{
    Point point;
    // Index of the polygon containing this point. A negative value indicates that the point is not on any border.
    int   border_idx;
    // simplify_travel() doesn't remove this point.
    bool do_not_remove = false;
};

// [INTENT] One crossing of the travel line with a boundary polygon edge.
// distance is the cumulative arc-length from the first vertex of the boundary polygon to this
// intersection, enabling fast forward/backward comparison in get_shortest_direction().
// [STATE] Sorted along the travel direction by avoid_perimeters_inner() before processing.
struct Intersection
{
    // Index of the polygon containing this point of intersection.
    size_t border_idx;
    // Index of the line on the polygon containing this point of intersection.
    size_t line_idx;
    // Point of intersection.
    Point  point;
    // Distance from the first point in the corresponding boundary
    float  distance;
    // simplify_travel() doesn't remove this point.
    bool do_not_remove = false;
};

// [INTENT] The nearest point on a boundary edge to a query point.
// Used by MinDistanceVisitor and extend_for_closest_lines() to handle the case where the
// travel start or end is already inside an offset boundary (no ray intersection exists).
struct ClosestLine
{
    // Index of the polygon containing this line.
    size_t border_idx;
    // Index of this line on the polygon containing it.
    size_t line_idx;
    // Closest point on the line.
    Point  point;
};

// [INTENT] EdgeGrid visitor that collects ALL intersections between the travel line and every
// boundary edge.  The deduplication set (intersection_set) prevents the same edge from being
// reported twice when the travel line clips a grid cell corner.
// [COUPLING] Depends on EdgeGrid::Grid cell_data_range() / line() API.
// Finding all intersections of a set of contours with a line segment.
struct AllIntersectionsVisitor
{
    AllIntersectionsVisitor(const EdgeGrid::Grid &grid, std::vector<Intersection> &intersections) : grid(grid), intersections(intersections)
    {
        intersection_set.reserve(intersections.capacity());
    }

    AllIntersectionsVisitor(const EdgeGrid::Grid &grid, std::vector<Intersection> &intersections, const Line &travel_line)
        : grid(grid), intersections(intersections), travel_line(travel_line)
    {
        intersection_set.reserve(intersections.capacity());
    }

    void reset() {
        intersection_set.clear();
    }

    bool operator()(coord_t iy, coord_t ix)
    {
        // Called with a row and column of the grid cell, which is intersected by a line.
        auto cell_data_range = grid.cell_data_range(iy, ix);
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
            Point intersection_point;
            if (travel_line.intersection(grid.line(*it_contour_and_segment), &intersection_point) &&
                intersection_set.find(*it_contour_and_segment) == intersection_set.end()) {
                intersections.push_back({ it_contour_and_segment->first, it_contour_and_segment->second, intersection_point });
                intersection_set.insert(*it_contour_and_segment);
            }
        }
        // Continue traversing the grid along the edge.
        return true;
    }

    const EdgeGrid::Grid                                                                 &grid;
    std::vector<Intersection>                                                            &intersections;
    Line                                                                                  travel_line;
    std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> intersection_set;
};

// [INTENT] EdgeGrid visitor for a fast yes/no collision check.  Returns false (stops traversal)
// the moment any boundary edge intersects the test segment [pt_current, pt_next].
// Used by simplify_travel() to validate that a shortcut doesn't introduce a new perimeter crossing.
// Visitor to check for any collision of a line segment with any contour stored inside the edge_grid.
struct FirstIntersectionVisitor
{
    explicit FirstIntersectionVisitor(const EdgeGrid::Grid &grid) : grid(grid) {}

    bool operator()(coord_t iy, coord_t ix)
    {
        assert(pt_current != nullptr);
        assert(pt_next != nullptr);
        // Called with a row and column of the grid cell, which is intersected by a line.
        auto cell_data_range = grid.cell_data_range(iy, ix);
        this->intersect      = false;
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
            // End points of the line segment and their vector.
            auto segment = grid.segment(*it_contour_and_segment);
            if (Geometry::segments_intersect(segment.first, segment.second, *pt_current, *pt_next)) {
                this->intersect = true;
                return false;
            }
        }
        // Continue traversing the grid along the edge.
        return true;
    }

    const EdgeGrid::Grid &grid;
    const Slic3r::Point  *pt_current = nullptr;
    const Slic3r::Point  *pt_next    = nullptr;
    bool                  intersect  = false;
};

// [INTENT] EdgeGrid visitor that finds all boundary edges within a bounding-box radius of a
// query point (center).  Deduplication via closest_lines_set ensures each edge appears once.
// Results are sorted by distance from center in get_closest_lines_in_radius().
// Used by extend_for_closest_lines() to handle start/end points already inside offset boundaries.
// Visitor to create a list of closet lines to a defined point.
struct MinDistanceVisitor
{
    explicit MinDistanceVisitor(const EdgeGrid::Grid &grid, const Point &center, double max_distance_squared)
        : grid(grid), center(center), max_distance_squared(max_distance_squared)
    {}

    void init()
    {
        this->closest_lines.clear();
        this->closest_lines_set.clear();
    }

    bool operator()(coord_t iy, coord_t ix)
    {
        // Called with a row and column of the grid cell, which is inside a bounding box.
        auto cell_data_range = grid.cell_data_range(iy, ix);
        for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
            // End points of the line segment and their vector.
            auto  segment = grid.segment(*it_contour_and_segment);
            Point closest_point;
            if (closest_lines_set.find(*it_contour_and_segment) == closest_lines_set.end() &&
                line_alg::distance_to_squared(Line(segment.first, segment.second), center, &closest_point) <= this->max_distance_squared) {
                closest_lines.push_back({it_contour_and_segment->first, it_contour_and_segment->second, closest_point});
                closest_lines_set.insert(*it_contour_and_segment);
            }
        }
        // Continue traversing the grid along the edge.
        return true;
    }

    const EdgeGrid::Grid &                                                                grid;
    const Slic3r::Point                                                                   center;
    std::vector<ClosestLine>                                                              closest_lines;
    std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> closest_lines_set;
    double                                                                                max_distance_squared = std::numeric_limits<double>::max();
};

// [INTENT] Convenience wrapper: fires MinDistanceVisitor over a square bounding box of side
// 2*search_radius, then returns results sorted nearest-first.  O(k) where k = edges in radius.
// Returns sorted list of closest lines to a passed point within a passed radius
static std::vector<ClosestLine> get_closest_lines_in_radius(const EdgeGrid::Grid &grid, const Point &center, float search_radius)
{
    Point              radius_vector(search_radius, search_radius);
    MinDistanceVisitor visitor(grid, center, search_radius * search_radius);
    grid.visit_cells_intersecting_box(BoundingBox(center - radius_vector, center + radius_vector), visitor);
    std::sort(visitor.closest_lines.begin(), visitor.closest_lines.end(), [&center](const auto &l, const auto &r) {
        return (center - l.point).template cast<double>().squaredNorm() < (center - r.point).template cast<double>().squaredNorm();
    });

    return visitor.closest_lines;
}

// [INTENT] Fallback handler for the case where the direct travel line finds no boundary crossings
// because start or end (or both) are already fully inside an offset boundary zone.
// Strategy:
//   1. If both endpoints are near the *same* boundary, discard all existing intersections and
//      synthesise a clean {start_closest, end_closest} pair routed entirely on that boundary.
//   2. Otherwise, try to replace or prepend/append a closer ClosestLine for the start/end that
//      shares a border_idx with an existing Intersection, to reduce the total detour.
// [HAZARD] The O(N*M) nested search (endpoints_close_to_same_boundary) is bounded in practice
//          because search_radius = 2 * perimeter_spacing limits candidate counts.
// When the offset is too big, then original travel doesn't have to cross created boundaries.
// For these cases, this function adds another intersection with lines around the start and the end point of the original travel.
static std::vector<Intersection> extend_for_closest_lines(const std::vector<Intersection>         &intersections,
                                                          const AvoidCrossingPerimeters::Boundary &boundary,
                                                          const Point                             &start,
                                                          const Point                             &end,
                                                          const float                              search_radius)
{
    const std::vector<ClosestLine> start_lines = get_closest_lines_in_radius(boundary.grid, start, search_radius);
    const std::vector<ClosestLine> end_lines   = get_closest_lines_in_radius(boundary.grid, end, search_radius);

    // Compute distance to the closest point in the ClosestLine from begin of contour.
    auto compute_distance = [&boundary](const ClosestLine &closest_line) -> float {
        float dist_from_line_begin = (closest_line.point - boundary.boundaries[closest_line.border_idx][closest_line.line_idx]).cast<float>().norm();
        return boundary.boundaries_params[closest_line.border_idx][closest_line.line_idx] + dist_from_line_begin;
    };

    // It tries to find closest lines for both start point and end point of the travel which has the same border_idx
    auto endpoints_close_to_same_boundary = [&start_lines, &end_lines]() -> std::pair<size_t, size_t> {
        std::unordered_set<size_t> boundaries_from_start;
        for (const ClosestLine &cl_start : start_lines)
            boundaries_from_start.insert(cl_start.border_idx);
        for (const ClosestLine &cl_end : end_lines)
            if (boundaries_from_start.find(cl_end.border_idx) != boundaries_from_start.end())
                for (const ClosestLine &cl_start : start_lines)
                    if (cl_start.border_idx == cl_end.border_idx) {
                        size_t cl_start_idx = &cl_start - &start_lines.front();
                        size_t cl_end_idx   = &cl_end - &end_lines.front();
                        return std::make_pair(cl_start_idx, cl_end_idx);
                    }
        return std::make_pair(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());
    };

    // If the existing two lines within the search radius start and end point belong to the same boundary,
    // discard all intersection points because the whole detour could be on one boundary.
    if (!start_lines.empty() && !end_lines.empty()) {
        std::pair<size_t, size_t> cl_indices = endpoints_close_to_same_boundary();
        if (cl_indices.first != std::numeric_limits<size_t>::max()) {
            assert(cl_indices.second != std::numeric_limits<size_t>::max());
            const ClosestLine &cl_start = start_lines[cl_indices.first];
            const ClosestLine &cl_end   = end_lines[cl_indices.second];
            std::vector<Intersection> new_intersections;
            new_intersections.push_back({cl_start.border_idx, cl_start.line_idx, cl_start.point, compute_distance(cl_start), true});
            new_intersections.push_back({cl_end.border_idx, cl_end.line_idx, cl_end.point, compute_distance(cl_end), true});
            return new_intersections;
        }
    }

    // Returns ClosestLine which is closer to the point "close_to" then point inside passed Intersection.
    auto get_closer = [&search_radius](const std::vector<ClosestLine> &closest_lines, const Intersection &intersection,
                                       const Point &close_to) -> size_t {
        for (const ClosestLine &cl : closest_lines) {
            double old_dist = (close_to - intersection.point).cast<float>().squaredNorm();
            if (cl.border_idx == intersection.border_idx && old_dist <= (search_radius * search_radius) &&
                (close_to - cl.point).cast<float>().squaredNorm() < old_dist)
                return &cl - &closest_lines.front();
        }
        return std::numeric_limits<size_t>::max();
    };

    // Try to find ClosestLine with same boundary_idx as any existing Intersection
    auto find_closest_line_with_same_boundary_idx = [](const std::vector<ClosestLine> & closest_lines,
                                                       const std::vector<Intersection> &intersections, const bool reverse) -> size_t {
        std::unordered_set<size_t> boundaries_indices;
        for (const ClosestLine &closest_line : closest_lines)
            boundaries_indices.insert(closest_line.border_idx);

        // This function must be called only in the case that exists closest_line with boundary_idx equals to intersection.border_idx
        auto find_closest_line_index = [&closest_lines](const Intersection &intersection) -> size_t {
            for (const ClosestLine &closest_line : closest_lines)
                if (closest_line.border_idx == intersection.border_idx) return &closest_line - &closest_lines.front();
            // This is an invalid state.
            assert(false);
            return std::numeric_limits<size_t>::max();
        };

        if (reverse) {
            for (const Intersection &intersection : boost::adaptors::reverse(intersections))
                if (boundaries_indices.find(intersection.border_idx) != boundaries_indices.end())
                    return find_closest_line_index(intersection);
        } else {
            for (const Intersection &intersection : intersections)
                if (boundaries_indices.find(intersection.border_idx) != boundaries_indices.end())
                    return find_closest_line_index(intersection);
        }
        return std::numeric_limits<size_t>::max();
    };

    std::vector<Intersection> new_intersections = intersections;
    if (!new_intersections.empty() && !start_lines.empty()) {
        size_t cl_start_idx = get_closer(start_lines, new_intersections.front(), start);
        if (cl_start_idx != std::numeric_limits<size_t>::max()) {
            // If there is any ClosestLine around the start point closer to the Intersection, then replace this Intersection with ClosestLine.
            const ClosestLine &cl_start = start_lines[cl_start_idx];
            new_intersections.front()   = {cl_start.border_idx, cl_start.line_idx, cl_start.point, compute_distance(cl_start), true};
        } else {
            // Check if there is any ClosestLine with the same boundary_idx as any Intersection. If this ClosestLine exists, then add it to the
            // vector of intersections. This allows in some cases when it is more than one around ClosestLine start point chose that one which
            // minimizes the number of contours (also length of the detour) in result detour. If there doesn't exist any ClosestLine like this, then
            // use the first one, which is the closest one to the start point.
            size_t             start_closest_lines_idx = find_closest_line_with_same_boundary_idx(start_lines, new_intersections, true);
            const ClosestLine &cl_start                = (start_closest_lines_idx != std::numeric_limits<size_t>::max()) ? start_lines[start_closest_lines_idx] : start_lines.front();
            new_intersections.insert(new_intersections.begin(), {cl_start.border_idx, cl_start.line_idx, cl_start.point, compute_distance(cl_start), true});
        }
    }

    if (!new_intersections.empty() && !end_lines.empty()) {
        size_t cl_end_idx = get_closer(end_lines, new_intersections.back(), end);
        if (cl_end_idx != std::numeric_limits<size_t>::max()) {
            // If there is any ClosestLine around the end point closer to the Intersection, then replace this Intersection with ClosestLine.
            const ClosestLine &cl_end = end_lines[cl_end_idx];
            new_intersections.back()  = {cl_end.border_idx, cl_end.line_idx, cl_end.point, compute_distance(cl_end), true};
        } else {
            // Check if there is any ClosestLine with the same boundary_idx as any Intersection. If this ClosestLine exists, then add it to the
            // vector of intersections. This allows in some cases when it is more than one around ClosestLine end point chose that one which
            // minimizes the number of contours (also length of the detour) in result detour. If there doesn't exist any ClosestLine like this, then
            // use the first one, which is the closest one to the end point.
            size_t             end_closest_lines_idx = find_closest_line_with_same_boundary_idx(end_lines, new_intersections, false);
            const ClosestLine &cl_end                = (end_closest_lines_idx != std::numeric_limits<size_t>::max()) ? end_lines[end_closest_lines_idx] : end_lines.front();
            new_intersections.push_back({cl_end.border_idx, cl_end.line_idx, cl_end.point, compute_distance(cl_end), true});
        }
    }
    return new_intersections;
}

// point_idx is the index from which is different vertex is searched.
template<bool forward>
static Point find_first_different_vertex(const Polygon &polygon, const size_t point_idx, const Point &point)
{
    assert(point_idx < polygon.size());
    // Solve case when vertex on passed index point_idx is different that pass point. This helps the following code keep simple.
    if (point != polygon.points[point_idx])
        return polygon.points[point_idx];

    auto line_idx = (int(point_idx) + 1) % int(polygon.points.size());
    assert(line_idx != int(point_idx));
    if constexpr (forward)
        for (; point == polygon.points[line_idx] && line_idx != int(point_idx); line_idx = line_idx + 1 < int(polygon.points.size()) ? line_idx + 1 : 0);
    else
        for (; point == polygon.points[line_idx] && line_idx != int(point_idx); line_idx = line_idx - 1 >= 0 ? line_idx - 1 : int(polygon.points.size()) - 1);
    assert(point != polygon.points[line_idx]);
    return polygon.points[line_idx];
}

static Vec2d three_points_inward_normal(const Point &left, const Point &middle, const Point &right)
{
    assert(left != middle);
    assert(middle != right);
    return (perp(Point(middle - left)).cast<double>().normalized() + perp(Point(right - middle)).cast<double>().normalized()).normalized();
}

// Compute normal of the polygon's vertex in an inward direction
static Vec2d get_polygon_vertex_inward_normal(const Polygon &polygon, const size_t point_idx)
{
    const size_t left_idx  = prev_idx_modulo(point_idx, polygon.points);
    const size_t right_idx = next_idx_modulo(point_idx, polygon.points);
    const Point &middle    = polygon.points[point_idx];
    const Point &left      = find_first_different_vertex<false>(polygon, left_idx, middle);
    const Point &right     = find_first_different_vertex<true>(polygon, right_idx, middle);
    return three_points_inward_normal(left, middle, right);
}

// Compute offset of point_idx of the polygon in a direction of inward normal
static Point get_polygon_vertex_offset(const Polygon &polygon, const size_t point_idx, const int offset)
{
    return polygon.points[point_idx] + (get_polygon_vertex_inward_normal(polygon, point_idx) * double(offset)).cast<coord_t>();
}

// Compute offset (in the direction of inward normal) of the point(passed on "middle") based on the nearest points laying on the polygon (left_idx and right_idx).
static Point get_middle_point_offset(const Polygon &polygon, const size_t left_idx, const size_t right_idx, const Point &middle, const coord_t offset)
{
    const Point &left  = find_first_different_vertex<false>(polygon, left_idx, middle);
    const Point &right = find_first_different_vertex<true>(polygon, right_idx, middle);
    return middle + (three_points_inward_normal(left, middle, right) * double(offset)).cast<coord_t>();
}

static Polyline to_polyline(const std::vector<TravelPoint> &travel)
{
    Polyline result;
    result.points.reserve(travel.size());
    for (const TravelPoint &t_point : travel)
        result.append(t_point.point);
    return result;
}

// #define AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
static void export_travel_to_svg(const Polygons                  &boundary,
                                 const Line                      &original_travel,
                                 const Polyline                  &result_travel,
                                 const std::vector<Intersection> &intersections,
                                 const std::string               &path)
{
    BoundingBox   bbox = get_extents(boundary);
    ::Slic3r::SVG svg(path, bbox);
    svg.draw_outline(boundary, "green");
    svg.draw(original_travel, "blue");
    svg.draw(result_travel, "red");
    svg.draw(original_travel.a, "black");
    svg.draw(original_travel.b, "grey");

    for (const Intersection &intersection : intersections)
        svg.draw(intersection.point, "lightseagreen");
}

static void export_travel_to_svg(const Polygons                  &boundary,
                                 const Line                      &original_travel,
                                 const std::vector<TravelPoint>  &result_travel,
                                 const std::vector<Intersection> &intersections,
                                 const std::string               &path)
{
    export_travel_to_svg(boundary, original_travel, to_polyline(result_travel), intersections, path);
}
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

// [INTENT] Given two intersection points on the same boundary polygon, compute whether going
// forward (increasing arc-length) or backward (decreasing arc-length) around the polygon is the
// shorter route.  Subtracts the partial segments at each endpoint so only full-vertex-to-full-vertex
// arc length is compared — avoids choosing the wrong direction when intersections are near vertices.
// [STATE] Uses Boundary::boundaries_params (cumulative arc-length table) so no per-call polygon walk.
// Returns a direction of the shortest path along the polygon boundary
enum class Direction { Forward, Backward };
// Returns a direction of the shortest path along the polygon boundary
static Direction get_shortest_direction(const AvoidCrossingPerimeters::Boundary &boundary,
                                        const Intersection                      &intersection_first,
                                        const Intersection                      &intersection_second,
                                        float                                    contour_length)
{
    assert(intersection_first.border_idx == intersection_second.border_idx);
    const Polygon &poly        = boundary.boundaries[intersection_first.border_idx];
    float          dist_first  = intersection_first.distance;
    float          dist_second = intersection_second.distance;

    assert(dist_first  >= 0.f && dist_first  <= contour_length);
    assert(dist_second >= 0.f && dist_second <= contour_length);

    bool reversed = false;
    if (dist_first > dist_second) {
        std::swap(dist_first, dist_second);
        reversed = true;
    }
    float total_length_forward  = dist_second - dist_first;
    float total_length_backward = dist_first + contour_length - dist_second;
    if (reversed) std::swap(total_length_forward, total_length_backward);

    total_length_forward  -= (intersection_first.point - poly[intersection_first.line_idx]).cast<float>().norm();
    total_length_backward -= (poly[(intersection_first.line_idx + 1) % poly.size()] - intersection_first.point).cast<float>().norm();

    total_length_forward  -= (poly[(intersection_second.line_idx + 1) % poly.size()] - intersection_second.point).cast<float>().norm();
    total_length_backward -= (intersection_second.point - poly[intersection_second.line_idx]).cast<float>().norm();

    if (total_length_forward < total_length_backward) return Direction::Forward;
    return Direction::Backward;
}

Polyline ConvertBBoxToPolyline(const BoundingBoxf &bbox)
{
    Point left_bottom = bbox.min.cast<coord_t>();
    Point left_up(bbox.min.cast<coord_t>()[0], bbox.max.cast<coord_t>()[1]);
    Point right_up = bbox.max.cast<coord_t>();
    Point right_bottom(bbox.max.cast<coord_t>()[0], bbox.min.cast<coord_t>()[1]);

    return Polyline({left_bottom, right_bottom, right_up, left_up, left_bottom});
}


// [INTENT] Greedy waypoint-elimination pass on a TravelPoint path.
// Iterates forward: at each point, tries to jump as far ahead as possible while still being
// able to draw a straight line without re-crossing any boundary (validated via FirstIntersectionVisitor).
// do_not_remove points (synthetic intersections from extend_for_closest_lines) are preserved.
// [HAZARD] Two FIXME notes in the code suggest binary search and tangent-point optimizations
//          that have not been implemented — worst-case O(N²) waypoint checks.
// Straighten the travel path as long as it does not collide with the contours stored in edge_grid.
static std::vector<TravelPoint> simplify_travel(const AvoidCrossingPerimeters::Boundary &boundary, const std::vector<TravelPoint> &travel)
{
    FirstIntersectionVisitor visitor(boundary.grid);
    std::vector<TravelPoint> simplified_path;
    simplified_path.reserve(travel.size());
    simplified_path.emplace_back(travel.front());
    // Try to skip some points in the path.
    //FIXME maybe use a binary search to trim the line?
    //FIXME how about searching tangent point at long segments? 
    for (size_t point_idx = 1; point_idx < travel.size(); ++point_idx) {
        const Point &current_point = travel[point_idx - 1].point;
        TravelPoint  next          = travel[point_idx];

        visitor.pt_current = &current_point;

        if (!next.do_not_remove)
            for (size_t point_idx_2 = point_idx + 1; point_idx_2 < travel.size(); ++point_idx_2) {
            // Workaround for some issue in MSVC 19.29.30037 32-bit compiler.
#if defined(_WIN32) && !defined(_WIN64)
                if (bool volatile do_not_remove = travel[point_idx_2].do_not_remove; do_not_remove) break;
#else
                if (travel[point_idx_2].do_not_remove) break;
#endif
                if (travel[point_idx_2].point == current_point) {
                    next      = travel[point_idx_2];
                    point_idx = point_idx_2;
                    continue;
                }

                visitor.pt_next = &travel[point_idx_2].point;
                boundary.grid.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);
                // Check if deleting point causes crossing a boundary
                if (!visitor.intersect) {
                    next      = travel[point_idx_2];
                    point_idx = point_idx_2;
                }
            }

        simplified_path.emplace_back(next);
    }

    return simplified_path;
}

// [INTENT] Average scaled nozzle diameter across all extruders used by this PrintObject.
// Fallback spacing when no non-empty LayerRegion is found.
// called by get_perimeter_spacing() / get_perimeter_spacing_external()
static inline float get_default_perimeter_spacing(const PrintObject &print_object)
{
    std::vector<unsigned int> printing_extruders = print_object.object_extruders();
    assert(!printing_extruders.empty());
    float avg_extruder = 0;
    for(unsigned int extruder_id : printing_extruders)
        avg_extruder += float(scale_(print_object.print()->config().nozzle_diameter.get_at(extruder_id)));
    avg_extruder /= printing_extruders.size();
    return avg_extruder;
}

// [INTENT] Average scaled perimeter-flow spacing across all non-empty LayerRegions of *this* layer.
// Used to compute the inward-offset distance for m_internal boundary construction.
// called by get_boundary() / avoid_perimeters_inner()
static float get_perimeter_spacing(const Layer &layer)
{
    size_t regions_count     = 0;
    float  perimeter_spacing = 0.f;
    for (const LayerRegion *layer_region : layer.regions())
        if (layer_region != nullptr && !layer_region->slices.empty()) {
            perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
            ++regions_count;
        }

    assert(perimeter_spacing >= 0.f);
    if (regions_count != 0)
        perimeter_spacing /= float(regions_count);
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object());
    return perimeter_spacing;
}

// [INTENT] Same as get_perimeter_spacing() but averages over *all* PrintObjects present at this Z.
// Used for m_external boundary construction where multiple objects may be co-printed.
// [COUPLING] Iterates over the entire Print's object list, querying layer existence at print_z.
// called by get_boundary_external()
static float get_perimeter_spacing_external(const Layer &layer)
{
    size_t regions_count     = 0;
    float  perimeter_spacing = 0.f;
    for (const PrintObject *object : layer.object()->print()->objects())
        if (const Layer *l = object->get_layer_at_printz(layer.print_z, EPSILON); l)
            for (const LayerRegion *layer_region : l->regions())
                if (layer_region != nullptr && !layer_region->slices.empty()) {
                    perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
                    ++ regions_count;
                }

    assert(perimeter_spacing >= 0.f);
    if (regions_count != 0)
        perimeter_spacing /= float(regions_count);
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object());
    return perimeter_spacing;
}

static float get_external_perimeter_width(const Layer &layer)
{
    size_t regions_count   = 0;
    float  perimeter_width = 0.f;
    for (const LayerRegion *layer_region : layer.regions())
        if (layer_region != nullptr && !layer_region->slices.empty()) {
            perimeter_width += float(layer_region->flow(frExternalPerimeter).scaled_width());
            ++regions_count;
        }

    assert(perimeter_width >= 0.f);
    if (regions_count != 0)
        perimeter_width /= float(regions_count);
    else
        perimeter_width = get_default_perimeter_spacing(*layer.object());
    return perimeter_width;
}

// [INTENT] CORE ROUTING FUNCTION.  Computes a detour path from start_point to end_point that
// avoids crossing the boundary polygons stored in 'boundary'.
//
// Algorithm:
//  1. Fire AllIntersectionsVisitor on the direct line → sorted Intersection list.
//  2. If no intersections, retry with a 1.5× perimeter_spacing expanded line (handles the case
//     where start/end are inside an offset zone and the direct line never reaches the boundary).
//  3. Assign arc-length distances to each intersection.
//  4. Call extend_for_closest_lines() to fix edge cases where both endpoints are inside boundary.
//  5. For each (entry, farthest-exit) pair on the same boundary:
//       - Insert start-of-boundary point (offset SCALED_EPSILON inward).
//       - Walk polygon vertices forward or backward (get_shortest_direction), emitting offset waypoints.
//       - Insert end-of-boundary point.
//  6. Call simplify_travel() to straighten the resulting path.
//  Returns: number of intersections found (used by need_wipe() to decide on retract/wipe).
//
// [HAZARD] search_radius = 2 * perimeter_spacing is hardcoded; may be wrong for very wide or
//          very narrow extrusions (see Hazard 80 notes).
static size_t avoid_perimeters_inner(
    const AvoidCrossingPerimeters::Boundary &boundary, const Point &start_point, const Point &end_point, const Layer &layer, std::vector<TravelPoint> &result_out)
{
    const Polygons       &boundaries = boundary.boundaries;
    const EdgeGrid::Grid &edge_grid  = boundary.grid;
    Point                 start = start_point, end = end_point;
    // Find all intersections between boundaries and the line segment, sort them along the line segment.
    std::vector<Intersection> intersections;
    {
        intersections.reserve(boundaries.size());
        AllIntersectionsVisitor visitor(edge_grid, intersections, Line(start, end));
        edge_grid.visit_cells_intersecting_line(start, end, visitor);
        Vec2d dir = (end - start).cast<double>();
        // if do not intersect due to the boundaries inner-offset, try to find the closest point to do intersect again!
        if (intersections.empty()) {
            // try to find the closest point on boundaries to start/end with distance less than extend_distance, which is noted as new start_point/end_point
            auto                           search_radius         = 1.5 * get_perimeter_spacing(layer);
            const std::vector<ClosestLine> closest_line_to_start = get_closest_lines_in_radius(boundary.grid, start, search_radius);
            const std::vector<ClosestLine> closest_line_to_end   = get_closest_lines_in_radius(boundary.grid, end, search_radius);
            if (!(closest_line_to_start.empty() && closest_line_to_end.empty())) {
                auto new_start_point = closest_line_to_start.empty() ? start : closest_line_to_start.front().point;
                auto new_end_point   = closest_line_to_end.empty() ? end : closest_line_to_end.front().point;
                dir                  = (new_end_point - new_start_point).cast<double>();
                auto unit_direction  = dir.normalized();
                // out-offset new_start_point/new_end_point epsilon along the Line(new_start_point, new_end_point) for right intersection!
                new_start_point = new_start_point - (unit_direction * double(coord_t(SCALED_EPSILON))).cast<coord_t>();
                new_end_point   = new_end_point + (unit_direction * double(coord_t(SCALED_EPSILON))).cast<coord_t>();
                AllIntersectionsVisitor visitor(edge_grid, intersections, Line(new_start_point, new_end_point));
                edge_grid.visit_cells_intersecting_line(new_start_point, new_end_point, visitor);
                if (!intersections.empty()) {
                    start = new_start_point;
                    end   = new_end_point;
                }
            }
        }

        for (Intersection &intersection : intersections) {
            float dist_from_line_begin = (intersection.point - boundary.boundaries[intersection.border_idx][intersection.line_idx]).cast<float>().norm();
            intersection.distance      = boundary.boundaries_params[intersection.border_idx][intersection.line_idx] + dist_from_line_begin;
        }
        std::sort(intersections.begin(), intersections.end(), [dir](const auto &l, const auto &r) { return (r.point - l.point).template cast<double>().dot(dir) > 0.; });

        // Search radius should always be at least equals to the value of offset used for computing boundaries.
        const float search_radius = 2.f * get_perimeter_spacing(layer);
        // When the offset is too big, then original travel doesn't have to cross created boundaries.
        // These cases are fixed by calling extend_for_closest_lines.
        intersections = extend_for_closest_lines(intersections, boundary, start, end, search_radius);
    }

    std::vector<TravelPoint> result;
    result.push_back({start, -1});

#if 0
    auto crossing_boundary_from_inside = [&boundary](const Point &start, const Intersection &intersection) {
        const Polygon &poly             = boundary.boundaries[intersection.border_idx];
        Vec2d          poly_line        = Line(poly[intersection.line_idx], poly[(intersection.line_idx + 1) % poly.size()]).normal().cast<double>();
        Vec2d          intersection_vec = (intersection.point - start).cast<double>();
        return poly_line.normalized().dot(intersection_vec.normalized()) >= 0;
    };
#endif

    for (auto it_first = intersections.begin(); it_first != intersections.end(); ++it_first) {
        // The entry point to the boundary polygon
        const Intersection &intersection_first = *it_first;
        //        if(!crossing_boundary_from_inside(start, intersection_first))
        //            continue;
        // Skip the it_first from the search for the farthest exit point from the boundary polygon
        auto it_last_item = std::make_reverse_iterator(it_first) - 1;
        // Search for the farthest intersection different from it_first but with the same border_idx
        auto it_second_r = std::find_if(intersections.rbegin(), it_last_item,
                                        [&intersection_first](const Intersection &intersection) { return intersection_first.border_idx == intersection.border_idx; });

        // Append the first intersection into the path
        size_t left_idx  = intersection_first.line_idx;
        size_t right_idx = intersection_first.line_idx + 1 == boundaries[intersection_first.border_idx].points.size() ? 0 : intersection_first.line_idx + 1;
        // Offset of the polygon's point using get_middle_point_offset is used to simplify the calculation of intersection between the
        // boundary and the travel. The appended point is translated in the direction of inward normal. This translation ensures that the
        // appended point will be inside the polygon and not on the polygon border.
        result.push_back({get_middle_point_offset(boundaries[intersection_first.border_idx], left_idx, right_idx, intersection_first.point, coord_t(SCALED_EPSILON)),
                          int(intersection_first.border_idx), intersection_first.do_not_remove});

        // Check if intersection line also exit the boundary polygon
        if (it_second_r != it_last_item) {
            // Transform reverse iterator to forward
            auto it_second = it_second_r.base() - 1;
            // The exit point from the boundary polygon
            const Intersection &intersection_second = *it_second;
            Direction           shortest_direction  = get_shortest_direction(boundary, intersection_first, intersection_second,
                                                                             boundary.boundaries_params[intersection_first.border_idx].back());
            // Append the path around the border into the path
            if (shortest_direction == Direction::Forward)
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                     line_idx     = line_idx + 1 < int(boundaries[intersection_first.border_idx].size()) ? line_idx + 1 : 0)
                    result.push_back(
                        {get_polygon_vertex_offset(boundaries[intersection_first.border_idx],
                                                   (line_idx + 1 == int(boundaries[intersection_first.border_idx].points.size())) ? 0 : (line_idx + 1), coord_t(SCALED_EPSILON)),
                             int(intersection_first.border_idx)});
            else
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                     line_idx     = line_idx - 1 >= 0 ? line_idx - 1 : int(boundaries[intersection_first.border_idx].size()) - 1)
                    result.push_back(
                        {get_polygon_vertex_offset(boundaries[intersection_second.border_idx], line_idx + 0, coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});

            // Append the farthest intersection into the path
            left_idx  = intersection_second.line_idx;
            right_idx = (intersection_second.line_idx >= (boundaries[intersection_second.border_idx].points.size() - 1)) ? 0 : (intersection_second.line_idx + 1);
            result.push_back({get_middle_point_offset(boundaries[intersection_second.border_idx], left_idx, right_idx, intersection_second.point, coord_t(SCALED_EPSILON)),
                              int(intersection_second.border_idx), intersection_second.do_not_remove});
            // Skip intersections in between
            it_first = it_second;
        }
    }

    result.push_back({end, -1});

    auto result_polyline = to_polyline(result);
    (void) result_polyline;

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections, debug_out_path("AvoidCrossingPerimetersInner-initial-%d-%d.svg", layer.id(), iRun++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    if (!intersections.empty()) result = simplify_travel(boundary, result);

    auto simplified_result_polyline = to_polyline(result);
    (void) simplified_result_polyline;

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections, debug_out_path("AvoidCrossingPerimetersInner-final-%d-%d.svg", layer.id(), iRun++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    append(result_out, std::move(result));
    return intersections.size();
}

// [INTENT] Thin public-facing wrapper around avoid_perimeters_inner().
// Converts the TravelPoint vector to a Polyline and returns the intersection count.
// Called by AvoidCrossingPerimeters::travel_to()
static size_t avoid_perimeters(const AvoidCrossingPerimeters::Boundary &boundary,
                               const Point                             &start,
                               const Point                             &end,
                               const Layer                             &layer,
                               Polyline                                &result_out)
{
    // Travel line is completely or partially inside the bounding box.
    std::vector<TravelPoint> path;
    size_t num_intersections = avoid_perimeters_inner(boundary, start, end, layer, path);
    result_out = to_polyline(path);


#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundary.boundaries, Line(start, end), path, {}, debug_out_path("AvoidCrossingPerimeters-final-%d-%d.svg", layer.id(), iRun ++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    return num_intersections;
}

// Check if anyone of ExPolygons contains whole travel.
// called by need_wipe() and AvoidCrossingPerimeters::travel_to()
// FIXME Lukas H.: Maybe similar approach could also be used for ExPolygon::contains()
static bool any_expolygon_contains(const ExPolygons               &ex_polygons,
                                   const std::vector<BoundingBox> &ex_polygons_bboxes,
                                   const EdgeGrid::Grid            &grid_lslice,
                                   const Line                      &travel)
{
    assert(ex_polygons.size() == ex_polygons_bboxes.size());
    if(!grid_lslice.bbox().contains(travel.a) || !grid_lslice.bbox().contains(travel.b))
        return false;

    FirstIntersectionVisitor visitor(grid_lslice);
    visitor.pt_current = &travel.a;
    visitor.pt_next    = &travel.b;
    grid_lslice.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);
    if (!visitor.intersect) {
        for (const ExPolygon &ex_polygon : ex_polygons) {
            const BoundingBox &bbox = ex_polygons_bboxes[&ex_polygon - &ex_polygons.front()];
            if (bbox.contains(travel.a) && bbox.contains(travel.b) && ex_polygon.contains(travel.a))
                return true;
        }
    }
    return false;
}

// Check if anyone of ExPolygons contains whole travel.
// called by need_wipe()
static bool any_expolygon_contains(const ExPolygons &ex_polygons, const std::vector<BoundingBox> &ex_polygons_bboxes, const EdgeGrid::Grid &grid_lslice, const Polyline &travel)
{
    assert(ex_polygons.size() == ex_polygons_bboxes.size());
    if(std::any_of(travel.points.begin(), travel.points.end(), [&grid_lslice](const Point &point) { return !grid_lslice.bbox().contains(point); }))
        return false;

    FirstIntersectionVisitor visitor(grid_lslice);
    bool any_intersection = false;
    for (size_t line_idx = 1; line_idx < travel.size(); ++line_idx) {
        visitor.pt_current = &travel.points[line_idx - 1];
        visitor.pt_next    = &travel.points[line_idx];
        grid_lslice.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);
        any_intersection = visitor.intersect;
        if (any_intersection) break;
    }

    if (!any_intersection) {
        for (const ExPolygon &ex_polygon : ex_polygons) {
            const BoundingBox &bbox = ex_polygons_bboxes[&ex_polygon - &ex_polygons.front()];
            if (std::all_of(travel.points.begin(), travel.points.end(), [&bbox](const Point &point) { return bbox.contains(point); }) &&
                ex_polygon.contains(travel.points.front()))
                return true;
        }
    }
    return false;
}

// [INTENT] Determines if a wipe/retract is needed after the travel move.
// Logic:
//   - If 0 intersections: travel was entirely inside/outside object → no wipe needed.
//   - z_lift enabled: wipe only if the rerouted path goes above holes (not fully contained
//     within lslices_offset).  If the original direct travel was already outside → always wipe.
//   - z_lift disabled: wipe if the rerouted travel is NOT fully inside lslices_offset.
// [COUPLING] Reads gcodegen.config().z_hop and gcodegen.writer().filament().
// [STATE] lslices_offset / lslices_offset_bboxes / grid_lslices_offset are precomputed by init_layer().
static bool need_wipe(const GCode                    &gcodegen,
                      const ExPolygons               &lslices_offset,
                      const std::vector<BoundingBox> &lslices_offset_bboxes,
                      const EdgeGrid::Grid           &grid_lslices_offset,
                      const Line                     &original_travel,
                      const Polyline                 &result_travel,
                      const size_t                    intersection_count)
{
    bool z_lift_enabled = gcodegen.config().z_hop.get_at(gcodegen.writer().filament()->id()) > 0.;
    bool wipe_needed    = false;

    // If the original unmodified path doesn't have any intersection with boundary, then it is entirely inside the object otherwise is entirely
    // outside the object.
    if (intersection_count > 0) {
        // The original layer is intersected with defined boundaries. Then it is necessary to make a detailed test.
        // If the z-lift is enabled, then a wipe is needed when the original travel leads above the holes.
        if (z_lift_enabled) {
            if (any_expolygon_contains(lslices_offset, lslices_offset_bboxes, grid_lslices_offset, original_travel)) {
                // Check if original_travel and result_travel are not same.
                // If both are the same, then it is possible to skip testing of result_travel
                wipe_needed = !(result_travel.size() > 2 && result_travel.first_point() == original_travel.a && result_travel.last_point() == original_travel.b) &&
                              !any_expolygon_contains(lslices_offset, lslices_offset_bboxes, grid_lslices_offset, result_travel);
            } else {
                wipe_needed = true;
            }
        } else {
            wipe_needed = !any_expolygon_contains(lslices_offset, lslices_offset_bboxes, grid_lslices_offset, result_travel);
        }
    }

    return wipe_needed;
}

// Adds points around all vertices so that the offset affects only small sections around these vertices.
static void resample_polygon(Polygon &polygon, double dist_from_vertex, double max_allowed_distance)
{
    Points resampled_poly;
    resampled_poly.reserve(3 * polygon.size());
    for (size_t pt_idx = 0; pt_idx < polygon.size(); ++pt_idx) {
        resampled_poly.emplace_back(polygon[pt_idx]);

        const Point &p1          = polygon[pt_idx];
        const Point &p2          = polygon[next_idx_modulo(pt_idx, polygon.size())];
        const Vec2d  line_vec    = (p2 - p1).cast<double>();
        double       line_length = line_vec.norm();
        const Vector vertex_offset_vec = (line_vec.normalized() * dist_from_vertex).cast<coord_t>();
        if (line_length > 2 * dist_from_vertex && vertex_offset_vec != Vector(0, 0)) {
            resampled_poly.emplace_back(p1 + vertex_offset_vec);

            const Vec2d  new_vertex_vec        = (p2 - p1 - 2 * vertex_offset_vec).cast<double>();
            const double new_vertex_vec_length = new_vertex_vec.norm();
            if (new_vertex_vec_length > max_allowed_distance) {
                const Vec2d &prev_point  = resampled_poly.back().cast<double>();
                const size_t parts_count = size_t(ceil(new_vertex_vec_length / max_allowed_distance));
                for (size_t part_idx = 1; part_idx < parts_count; ++part_idx) {
                    const double part_param = double(part_idx) / double(parts_count);
                    const Vec2d  new_point  = prev_point + new_vertex_vec * part_param;
                    resampled_poly.emplace_back(new_point.cast<coord_t>());
                }
            }

            resampled_poly.emplace_back(p2 - vertex_offset_vec);
        }
    }
    polygon.points = std::move(resampled_poly);
}

static void resample_expolygon(ExPolygon &ex_polygon, double dist_from_vertex, double max_allowed_distance)
{
    resample_polygon(ex_polygon.contour, dist_from_vertex, max_allowed_distance);
    for (Polygon &polygon : ex_polygon.holes)
        resample_polygon(polygon, dist_from_vertex, max_allowed_distance);
}

static void resample_expolygons(ExPolygons &ex_polygons, double dist_from_vertex, double max_allowed_distance)
{
    for (ExPolygon &ex_poly : ex_polygons)
        resample_expolygon(ex_poly, dist_from_vertex, max_allowed_distance);
}

static void precompute_polygon_distances(const Polygon &polygon, std::vector<float> &polygon_distances_out)
{
    polygon_distances_out.assign(polygon.size() + 1, 0.f);
    for (size_t point_idx = 1; point_idx < polygon.size(); ++point_idx)
        polygon_distances_out[point_idx] = polygon_distances_out[point_idx - 1] + float((polygon[point_idx] - polygon[point_idx - 1]).cast<double>().norm());
    polygon_distances_out.back() = polygon_distances_out[polygon.size() - 1] + float((polygon.points.back() - polygon.points.front()).cast<double>().norm());
}

static void precompute_expolygon_distances(const ExPolygon &ex_polygon, std::vector<std::vector<float>> &expolygon_distances_out)
{
    expolygon_distances_out.assign(ex_polygon.holes.size() + 1, std::vector<float>());
    precompute_polygon_distances(ex_polygon.contour, expolygon_distances_out.front());
    for (size_t hole_idx = 0; hole_idx < ex_polygon.holes.size(); ++hole_idx)
        precompute_polygon_distances(ex_polygon.holes[hole_idx], expolygon_distances_out[hole_idx + 1]);
}

// It is highly based on the function contour_distance2 from the ElephantFootCompensation.cpp
static std::vector<float> contour_distance(const EdgeGrid::Grid     &grid,
                                    const std::vector<float> &poly_distances,
                                    const size_t              contour_idx,
                                    const Polygon            &polygon,
                                    double                    compensation,
                                    double                    search_radius)
{
    assert(! polygon.empty());
    assert(polygon.size() >= 2);

    std::vector<float> out;

    if (polygon.size() > 2)
    {
        struct Visitor {
            Visitor(const EdgeGrid::Grid &grid, const size_t contour_idx, const std::vector<float> &polygon_distances, double dist_same_contour_accept, double dist_same_contour_reject) :
                grid(grid), idx_contour(contour_idx), contour(grid.contours()[contour_idx]), boundary_parameters(polygon_distances), dist_same_contour_accept(dist_same_contour_accept), dist_same_contour_reject(dist_same_contour_reject) {}

            void init(const Points &contour, const Point &apoint)
            {
                this->idx_point  = &apoint - contour.data();
                this->point      = apoint;
                this->found      = false;
                this->dir_inside = this->dir_inside_at_point(contour, this->idx_point);
                this->distance   = std::numeric_limits<double>::max();
            }

            bool operator()(coord_t iy, coord_t ix)
            {
                // Called with a row and colum of the grid cell, which is intersected by a line.
                auto cell_data_range = this->grid.cell_data_range(iy, ix);
                for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                     ++it_contour_and_segment) {
                    // End points of the line segment and their vector.
                    std::pair<const Point &, const Point &> segment = this->grid.segment(*it_contour_and_segment);
                    const Vec2d  v        = (segment.second - segment.first).cast<double>();
                    const Vec2d  va       = (this->point - segment.first).cast<double>();
                    const double l2       = v.squaredNorm(); // avoid a sqrt
                    const double t        = (l2 == 0.0) ? 0. : std::clamp(va.dot(v) / l2, 0., 1.);
                    // Closest point from this->point to the segment.
                    const Vec2d  foot     = segment.first.cast<double>() + t * v;
                    const Vec2d  bisector = foot - this->point.cast<double>();
                    const double dist     = bisector.norm();

                    if ((!this->found || dist < this->distance) && this->dir_inside.dot(bisector) > 0) {
                        bool accept = true;
                        if (it_contour_and_segment->first == idx_contour) {
                            // Complex case: The closest segment originates from the same contour as the starting point.
                            // Reject the closest point if its distance along the contour is reasonable compared to the current contour bisector
                            // (this->pt, foot).
                            const EdgeGrid::Contour &contour   = grid.contours()[it_contour_and_segment->first];
                            double                   param_lo  = boundary_parameters[this->idx_point];
                            double                   param_hi  = t * sqrt(l2);
                            double                   param_end = boundary_parameters.back();
                            const size_t ipt = it_contour_and_segment->second;
                            if (contour.begin() + ipt + 1 < contour.end())
                                param_hi += boundary_parameters[ipt];
                            if (param_lo > param_hi)
                                std::swap(param_lo, param_hi);
                            assert(param_lo > -SCALED_EPSILON && param_lo <= param_end + SCALED_EPSILON);
                            assert(param_hi > -SCALED_EPSILON && param_hi <= param_end + SCALED_EPSILON);
                            double dist_along_contour = std::min(param_hi - param_lo, param_lo + param_end - param_hi);
                            if (dist_along_contour < dist_same_contour_accept)
                                accept = false;
                            else if (dist < dist_same_contour_reject + SCALED_EPSILON) {
                                // this->point is close to foot. This point will only be accepted if the path along the contour is significantly
                                // longer than the bisector. That is, the path shall not bulge away from the bisector too much.
                                // Bulge is estimated by 0.6 of the circle circumference drawn around the bisector.
                                // Test whether the contour is convex or concave.
                                bool inside = (t == 0.) ? this->inside_corner(contour, ipt, this->point) :
                                              (t == 1.) ? this->inside_corner(contour, contour.segment_idx_next(ipt), this->point) :
                                                          this->left_of_segment(contour, ipt, this->point);
                                accept      = inside && dist_along_contour > 0.6 * M_PI * dist;
                            }
                        }
                        if (accept && (!this->found || dist < this->distance)) {
                            // Simple case: Just measure the shortest distance.
                            this->distance = dist;
                            this->found    = true;
                        }
                    }
                }
                // Continue traversing the grid.
                return true;
            }

            const EdgeGrid::Grid 			   &grid;
            const size_t 		  				idx_contour;
            const EdgeGrid::Contour            &contour;

            const std::vector<float>           &boundary_parameters;
            const double                        dist_same_contour_accept;
            const double 						dist_same_contour_reject;

            size_t 								idx_point;
            Point			      				point;
            // Direction inside the contour from idx_point, not normalized.
            Vec2d								dir_inside;
            bool 								found;
            double 								distance;

        private:
            static Vec2d dir_inside_at_point(const Points &contour, size_t i)
            {
                size_t iprev = prev_idx_modulo(i, contour);
                size_t inext = next_idx_modulo(i, contour);
                Vec2d  v1    = (contour[i] - contour[iprev]).cast<double>();
                Vec2d  v2    = (contour[inext] - contour[i]).cast<double>();
                return Vec2d(-v1.y() - v2.y(), v1.x() + v2.x());
            }

            static bool inside_corner(const EdgeGrid::Contour &contour, size_t i, const Point &pt_oposite)
            {
                const Vec2d pt         = pt_oposite.cast<double>();
                const Point &pt_prev   = contour.segment_prev(i);
                const Point &pt_this   = contour.segment_start(i);
                const Point &pt_next   = contour.segment_end(i);
                Vec2d       v1         = (pt_this - pt_prev).cast<double>();
                Vec2d       v2         = (pt_next - pt_this).cast<double>();
                bool        left_of_v1 = cross2(v1, pt - pt_prev.cast<double>()) > 0.;
                bool        left_of_v2 = cross2(v2, pt - pt_this.cast<double>()) > 0.;
                return cross2(v1, v2) > 0 ? left_of_v1 && left_of_v2 : // convex corner
                                            left_of_v1 || left_of_v2;                   // concave corner
            }

            static bool left_of_segment(const EdgeGrid::Contour &contour, size_t i, const Point &pt_oposite)
            {
                const Vec2d  pt      = pt_oposite.cast<double>();
                const Point &pt_this = contour.segment_start(i);
                const Point &pt_next = contour.segment_end(i);
                Vec2d        v       = (pt_next - pt_this).cast<double>();
                return cross2(v, pt - pt_this.cast<double>()) > 0.;
            }
        } visitor(grid, contour_idx, poly_distances, 0.5 * compensation * M_PI, search_radius);

        out.reserve(polygon.size());
        Point radius_vector(search_radius, search_radius);
        for (const Point &pt : polygon.points) {
            visitor.init(polygon.points, pt);
            grid.visit_cells_intersecting_box(BoundingBox(pt - radius_vector, pt + radius_vector), visitor);
            out.emplace_back(float(visitor.found ? std::min(visitor.distance, search_radius) : search_radius));
        }
    }

    return out;
}

// [INTENT] Per-vertex variable inward offset that avoids over-shrinking at narrow constrictions.
// Tries up to 3 progressively looser min_contour_width values.  For each:
//   - Builds a local EdgeGrid, calls contour_distance() to measure wall thickness at every vertex.
//   - Maps distance → per-vertex offset amount (0 if wall too thin, full offset if wide enough).
//   - Calls variable_offset_inner_ex().  If result fragmented, takes largest fragment.
// [HAZARD] Operates on each ExPolygon independently (comment: "ExPolygons could intersect").
// [COUPLING] Depends on variable_offset_inner_ex() from ClipperUtils.
// Polygon offset which ensures that if a polygon breaks up into several separate parts, the original polygon will be used in these places.
// ExPolygons are handled one by one so returned ExPolygons could intersect.
static ExPolygons inner_offset(const ExPolygons &ex_polygons, double offset_dis)
{
    // try different offset_dis distances
    const std::vector<double> min_contour_width_values = {offset_dis / 2., offset_dis, 2. * offset_dis + SCALED_EPSILON};
    ExPolygons                ex_poly_result           = ex_polygons;
    // remove too small holes from the ex_poly
    for (ExPolygon &ex_poly : ex_poly_result) {
        for (auto iter = ex_poly.holes.begin(); iter != ex_poly.holes.end();) {
            auto out_offset_holes = offset(*iter, scale_(0.1f));
            if (out_offset_holes.empty()) {
                iter = ex_poly.holes.erase(iter);
            } else {
                ++iter;
            }
        }
    }
    resample_expolygons(ex_poly_result, offset_dis / 2, scaled<double>(0.5));

    for (ExPolygon &ex_poly : ex_poly_result) {
        BoundingBox bbox(get_extents(ex_poly));
        bbox.offset(SCALED_EPSILON);

        // Filter out expolygons smaller than 0.1mm^2
        if (Vec2d bbox_size = bbox.size().cast<double>(); bbox_size.x() * bbox_size.y() < Slic3r::sqr(scale_(0.1f))) continue;

        for (const double &min_contour_width : min_contour_width_values) {
            const size_t min_contour_width_idx = &min_contour_width - &min_contour_width_values.front();
            const double search_radius         = 2. * (offset_dis + min_contour_width);

            EdgeGrid::Grid grid;
            grid.set_bbox(bbox);
            grid.create(ex_poly, coord_t(0.7 * search_radius));

            std::vector<std::vector<float>> ex_poly_distances;
            precompute_expolygon_distances(ex_poly, ex_poly_distances);

            std::vector<std::vector<float>> offsets;
            offsets.reserve(ex_poly.holes.size() + 1);
            for (size_t idx_contour = 0; idx_contour <= ex_poly.holes.size(); ++idx_contour) {
                const Polygon &poly = (idx_contour == 0) ? ex_poly.contour : ex_poly.holes[idx_contour - 1];
                assert(poly.is_counter_clockwise() == (idx_contour == 0));
                std::vector<float> distances = contour_distance(grid, ex_poly_distances[idx_contour], idx_contour, poly, offset_dis, search_radius);
                for (float &distance : distances) {
                    if (distance < min_contour_width)
                        distance = 0.0f;
                    else if (distance > min_contour_width + 2. * offset_dis)
                        distance = -float(offset_dis);
                    else
                        distance = -(distance - float(min_contour_width)) / 2.f;
                }
                offsets.emplace_back(distances);
            }

            ExPolygons offset_ex_poly = variable_offset_inner_ex(ex_poly, offsets);
            // If variable_offset_inner_ex produces empty result, then original ex_polygon is used
            if (offset_ex_poly.size() == 1 && offset_ex_poly.front().holes.size() == ex_poly.holes.size()) {
                ex_poly = std::move(offset_ex_poly.front());
                break;
            } else if ((min_contour_width_idx + 1) < min_contour_width_values.size()) {
                continue; // Try the next round with bigger min_contour_width.
            } else if (offset_ex_poly.size() == 1) {
                ex_poly = std::move(offset_ex_poly.front());
                break;
            } else if (offset_ex_poly.size() > 1) {
                // fix_after_inner_offset called inside variable_offset_inner_ex sometimes produces
                // tiny artefacts polygons, so these artefacts are removed.
                double max_area     = offset_ex_poly.front().area();
                size_t max_area_idx = 0;
                for (size_t poly_idx = 1; poly_idx < offset_ex_poly.size(); ++poly_idx) {
                    double area = offset_ex_poly[poly_idx].area();
                    if (max_area < area) {
                        max_area     = area;
                        max_area_idx = poly_idx;
                    }
                }
                ex_poly = std::move(offset_ex_poly[max_area_idx]);
                break;
            }
        }
    }
    return ex_poly_result;
}

//#define INCLUDE_SUPPORTS_IN_BOUNDARY

// [INTENT] Builds the m_internal crossing-forbidden boundary for a single PrintObject layer.
// Steps:
//   1. inner_offset(layer.lslices, 1.5 * perimeter_spacing) → shrunk zones travel must stay within.
//   2. For SupportLayer, also adds the layer below (to guide support-travel around model perimeters).
//   3. Subtracts top-surface areas (offset inward by 1.2 * perimeter_offset) to allow free travel
//      over completed top skins without triggering a wipe.
// Returns ExPolygons (not yet Polygons) — caller (travel_to) calls to_polygons().
// [COUPLING] Dynamic-casts to SupportLayer — tight coupling to layer type hierarchy.
// called by AvoidCrossingPerimeters::travel_to()
static ExPolygons get_boundary(const Layer &layer, float perimeter_spacing)
{
    // const float perimeter_spacing = get_perimeter_spacing(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    auto const *support_layer     = dynamic_cast<const SupportLayer *>(&layer);
    ExPolygons  boundary          = union_ex(inner_offset(layer.lslices, 1.5 * perimeter_spacing));
    if(support_layer) {
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
        append(boundary, inner_offset(support_layer->support_islands, 1.5 * perimeter_spacing));
#endif
        auto *layer_below = layer.object()->get_first_layer_bellow_printz(layer.print_z, EPSILON);
        if (layer_below)
            append(boundary, inner_offset(layer_below->lslices, 1.5 * perimeter_spacing));
        // After calling inner_offset it is necessary to call union_ex because of the possibility of intersection ExPolygons
        boundary = union_ex(boundary);
    }
    // Collect all top layers that will not be crossed.
    size_t      polygons_count    = 0;
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->fill_surfaces.surfaces)
            if (surface.is_top()) ++polygons_count;

    if (polygons_count > 0) {
        ExPolygons top_layer_polygons;
        top_layer_polygons.reserve(polygons_count);
        for (const LayerRegion *layer_region : layer.regions())
            for (const Surface &surface : layer_region->fill_surfaces.surfaces)
                if (surface.is_top()) top_layer_polygons.emplace_back(surface.expolygon);

        top_layer_polygons = union_ex(top_layer_polygons);
        return diff_ex(boundary, offset_ex(top_layer_polygons, -1.2 * perimeter_offset));
    }

    return boundary;
}

// [INTENT] Builds the m_external crossing-forbidden boundary for multi-object printing.
// Iterates over ALL PrintObjects at this print_z, collecting their contour holes (expanded by
// perimeter_offset) plus the layer below for support layers.  Reversed so normals face outward.
// [COUPLING] Reads every PrintObject in the Print — O(N_objects × N_layers) during init.
// [HAZARD] FIXME comment inside: layers at different heights across objects may not align.
// called by AvoidCrossingPerimeters::travel_to()
static Polygons get_boundary_external(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing_external(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    auto const *support_layer     = dynamic_cast<const SupportLayer *>(&layer);
    Polygons    boundary;
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
    ExPolygons  supports_boundary;
#endif
    // Collect all holes for all printed objects and their instances, which will be printed at the same time as passed "layer".
    for (const PrintObject *object : layer.object()->print()->objects()) {
        Polygons   holes_per_obj;
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
        ExPolygons supports_per_obj;
#endif
        if (const Layer *l = object->get_layer_at_printz(layer.print_z, EPSILON); l)
            for (const ExPolygon &island : l->lslices)
                append(holes_per_obj, island.holes);
        if (support_layer) {
            auto *layer_below = object->get_first_layer_bellow_printz(layer.print_z, EPSILON);
            if (layer_below)
                for (const ExPolygon &island : layer_below->lslices)
                    append(holes_per_obj, island.holes);
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
            append(supports_per_obj, support_layer->support_islands);
#endif
        }

        // After 7ff76d07684858fd937ef2f5d863f105a10f798e, when expand is called on CW polygons (holes), they are shrunk
        // instead of expanded because union that makes CCW from CW isn't called anymore. So let's make it CCW.
        polygons_reverse(holes_per_obj);

        for (const PrintInstance &instance : object->instances()) {
            size_t boundary_idx = boundary.size();
            append(boundary, holes_per_obj);
            for (; boundary_idx < boundary.size(); ++boundary_idx)
                boundary[boundary_idx].translate(instance.shift);
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
            size_t support_idx = supports_boundary.size();
            append(supports_boundary, supports_per_obj);
            for (; support_idx < supports_boundary.size(); ++support_idx)
                supports_boundary[support_idx].translate(instance.shift);
#endif
        }
    }

    // Used offset_ex for cases when another object will be in the hole of another polygon
    boundary = expand(boundary, perimeter_offset);
    // Reverse all polygons for making normals point from the polygon out.
    for (Polygon &poly : boundary)
        poly.reverse();
#ifdef INCLUDE_SUPPORTS_IN_BOUNDARY
    append(boundary, to_polygons(inner_offset(supports_boundary, perimeter_offset)));
#endif
    return boundary;
}

// [INTENT] Populates Boundary::boundaries_params — cumulative arc-length per polygon vertex.
// Index [i] = arc-length from vertex 0 to vertex i; back() = full perimeter length.
// Used by get_shortest_direction() to compare forward vs backward routing distance.
static void init_boundary_distances(AvoidCrossingPerimeters::Boundary *boundary)
{
    boundary->boundaries_params.assign(boundary->boundaries.size(), std::vector<float>());
    for (size_t poly_idx = 0; poly_idx < boundary->boundaries.size(); ++poly_idx)
        precompute_polygon_distances(boundary->boundaries[poly_idx], boundary->boundaries_params[poly_idx]);
}

// [INTENT] Constructs a Boundary from a set of polygons: clear old state, store polygons,
// compute bounding box with SCALED_EPSILON padding, build EdgeGrid (1mm cells), precompute arc-lengths.
// Overload 1: bbox tightly wraps the boundary polygons.
// [HAZARD-79] Hardcoded 1mm grid — see FIXME comment.  May be too coarse for fine details.
void init_boundary(AvoidCrossingPerimeters::Boundary *boundary, Polygons &&boundary_polygons)
{
    boundary->clear();
    boundary->boundaries = std::move(boundary_polygons);

    BoundingBox bbox(get_extents(boundary->boundaries));
    bbox.offset(SCALED_EPSILON);
    boundary->bbox = BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>());
    boundary->grid.set_bbox(bbox);
    // FIXME 1mm grid?
    boundary->grid.create(boundary->boundaries, coord_t(scale_(1.)));
    init_boundary_distances(boundary);
}

// [INTENT] Overload 2 (used by travel_to): bbox is expanded to also contain start and end points
// of the travel move (merge_points), padded by bbox.radius().  This ensures that even when start
// or end lie outside the polygon extents, the grid and bbox cover the full travel arc.
// [HAZARD-79] Lazy re-init triggered when start/end fall outside current bbox — see travel_to().
static void init_boundary(AvoidCrossingPerimeters::Boundary *boundary, Polygons &&boundary_polygons, const std::vector<Point>& merge_poins)
{
    boundary->clear();
    boundary->boundaries = std::move(boundary_polygons);

    BoundingBox bbox(get_extents(boundary->boundaries));
    for (const auto& merge_point : merge_poins) {
        bbox.merge(merge_point);
    }
    bbox.offset(bbox.radius());
    boundary->bbox = BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>());
    boundary->grid.set_bbox(bbox);
    // FIXME 1mm grid?
    boundary->grid.create(boundary->boundaries, coord_t(scale_(1.)));
    init_boundary_distances(boundary);
}

// [INTENT] PUBLIC API: plan a single travel move from gcodegen.last_pos() to `point`.
// Steps:
//   1. Determine coordinate system (object-space vs world-space) based on use_external.
//   2. If travel is entirely within m_lslices_offset (no perimeter at risk) AND not a support
//      layer, return direct line — no detour needed.
//   3. Lazy-init m_internal or m_external (including bbox expansion for start/end).
//   4. Call avoid_perimeters() → rerouted Polyline.
//   5. Enforce max_travel_detour_distance: revert to direct line if detour is too long.
//   6. Set *could_be_wipe_disabled via need_wipe() for the caller to suppress wipe retract.
// [STATE] Modifies m_internal/m_external lazily; clears and rebuilds if start/end outside bbox.
// [HAZARD-79] Re-init triggers full get_boundary() + inner_offset() recomputation per miss.
// Plan travel, which avoids perimeter crossings by following the boundaries of the layer.
Polyline AvoidCrossingPerimeters::travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled)
{
    // If use_external, then perform the path planning in the world coordinate system (correcting for the gcodegen offset).
    // Otherwise perform the path planning in the coordinate system of the active object.
    bool        use_external  = m_use_external_mp || m_use_external_mp_once;
    Point       scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    const Point start         = gcodegen.last_pos() + scaled_origin;
    const Point end           = point + scaled_origin;
    const Line  travel(start, end);
    Polyline result_pl;
    size_t   travel_intersection_count = 0;
    Vec2d startf = start.cast<double>();
    Vec2d endf   = end  .cast<double>();

    const ExPolygons               &lslices          = gcodegen.layer()->lslices;
    const std::vector<BoundingBox> &lslices_bboxes   = gcodegen.layer()->lslices_bboxes;
    bool                            is_support_layer = (dynamic_cast<const SupportLayer *>(gcodegen.layer()) != nullptr);
    if (!use_external && (is_support_layer || (!m_lslices_offset.empty() && !any_expolygon_contains(m_lslices_offset, m_lslices_offset_bboxes, m_grid_lslice, travel)))) {
        // Initialize m_internal only when it is necessary.
        if (m_internal.boundaries.empty()) {
            init_boundary(&m_internal, to_polygons(get_boundary(*gcodegen.layer(), get_perimeter_spacing(*gcodegen.layer()))), {start, end});
        } else if (!(m_internal.bbox.contains(startf) && m_internal.bbox.contains(endf))) {
            // check if start and end are in bbox, if not, merge start and end points to bbox
            m_internal.clear();
            init_boundary(&m_internal, to_polygons(get_boundary(*gcodegen.layer(), get_perimeter_spacing(*gcodegen.layer()))), {start, end});
        }

        if (!m_internal.boundaries.empty()) {
            travel_intersection_count = avoid_perimeters(m_internal, start, end, *gcodegen.layer(), result_pl);
            result_pl.points.front()  = start;
            result_pl.points.back()   = end;
        }
    } else if (use_external) {
        // Initialize m_external only when exist any external travel for the current layer.
        if (m_external.boundaries.empty()) {
            init_boundary(&m_external, get_boundary_external(*gcodegen.layer()), {start, end});
        } else if (!(m_external.bbox.contains(startf) && m_external.bbox.contains(endf))) {
            // check if start and end are in bbox
            m_external.clear();
            init_boundary(&m_external, get_boundary_external(*gcodegen.layer()), {start, end});
        }
        
        // Trim the travel line by the bounding box.
        if (!m_external.boundaries.empty()) 
        {
            travel_intersection_count = avoid_perimeters(m_external, start, end, *gcodegen.layer(), result_pl);
            result_pl.points.front()  = start;
            result_pl.points.back()   = end;
            
        }
    }

    if(result_pl.empty()) {
        // Travel line is completely outside the bounding box.
        result_pl                 = {start, end};
        travel_intersection_count = 0;
    }

    const ConfigOptionFloatOrPercent &opt_max_detour             = gcodegen.config().max_travel_detour_distance;
    bool                              max_detour_length_exceeded = false;
    if (opt_max_detour.value > 0) {
        double direct_length     = travel.length();
        double detour            = result_pl.length() - direct_length;
        double max_detour_length = opt_max_detour.percent ?
            direct_length * 0.01 * opt_max_detour.value :
            scale_(opt_max_detour.value);
        if (detour > max_detour_length) {
            result_pl = {start, end};
            max_detour_length_exceeded = true;
        }
    }

    if (use_external) {
        result_pl.translate(-scaled_origin);
        *could_be_wipe_disabled = false;
    } else if (max_detour_length_exceeded) {
        *could_be_wipe_disabled = false;
    } else
        *could_be_wipe_disabled = !need_wipe(gcodegen, m_lslices_offset, m_lslices_offset_bboxes, m_grid_lslice, travel, result_pl, travel_intersection_count);

    return result_pl;
}

// ************************************* AvoidCrossingPerimeters::init_layer() *****************************************
// [INTENT] PUBLIC API: called once per layer at the start of G-code generation for that layer.
// Resets m_internal and m_external (so they are rebuilt lazily on the first travel that needs them).
// Always precomputes m_lslices_offset (inward-offset layer slices) and m_grid_lslice:
//   - m_lslices_offset: tries coefficients 0.6, 0.5, 0.45 × external_perimeter_width until non-empty.
//     Used by travel_to() to short-circuit routing when travel stays inside the offset region.
//   - m_grid_lslice: EdgeGrid over m_lslices_offset, used by any_expolygon_contains() and need_wipe().
// [HAZARD-80] m_grid_lslice uses hardcoded 1mm cell size (FIXME comment inside).
// [STATE] After this call, m_internal.boundaries and m_external.boundaries are both empty.
//         They are populated lazily on the first matching travel_to() call.

void AvoidCrossingPerimeters::init_layer(const Layer &layer)
{
    m_internal.clear();
    m_external.clear();

    m_lslices_offset.clear();
    m_lslices_offset_bboxes.clear();
    for (auto coeff : {0.6f, 0.5f, 0.45f}) {
        m_lslices_offset = offset_ex(layer.lslices, -get_external_perimeter_width(layer) * coeff);
        if (!m_lslices_offset.empty()) break;
    }    
    m_lslices_offset_bboxes.reserve(m_lslices_offset.size());
    for (const auto &ex_polygon : m_lslices_offset) m_lslices_offset_bboxes.emplace_back(get_extents(ex_polygon));

    BoundingBox bbox_slice(get_extents(layer.lslices));
    bbox_slice.offset(SCALED_EPSILON);

    m_grid_lslice.set_bbox(bbox_slice);
    //FIXME 1mm grid?
    m_grid_lslice.create(m_lslices_offset, coord_t(scale_(1.)));
}

// [HAZARD-78] DEAD CODE BLOCK — entire alternative implementation disabled with #if 0.
// Contains: travel_length(), a second avoid_perimeters_inner() (simpler — no extend_for_closest_lines
// fallback), simplify_travel_heuristics() (more aggressive multi-segment shortcut pass),
// a second avoid_perimeters() (calls simplify_travel_heuristics forward + backward), and a second
// init_layer() that eagerly builds both m_internal and m_external at layer start.
// This represents a prior design iteration with eager init vs the current lazy-init approach.
// [UNCLEAR → RESOLVED] The active path lazily initializes boundary data in `travel_to()`, while the disabled block is the older eager-initialization variant.
// Maintenance risk: if the active code is changed without auditing this block, the two diverge further.
#if 0
static double travel_length(const std::vector<TravelPoint> &travel) {
    double total_length = 0;
    for (size_t idx = 1; idx < travel.size(); ++idx)
        total_length += (travel[idx].point - travel[idx - 1].point).cast<double>().norm();

    return total_length;
}

// Called by avoid_perimeters() and by simplify_travel_heuristics().
static size_t avoid_perimeters_inner(const AvoidCrossingPerimeters::Boundary &boundary,
                                     const Point              &start,
                                     const Point              &end,
                                     std::vector<TravelPoint> &result_out)
{
    const Polygons           &boundaries = boundary.boundaries;
    const EdgeGrid::Grid     &edge_grid = boundary.grid;
    // Find all intersections between boundaries and the line segment, sort them along the line segment.
    std::vector<Intersection> intersections;
    {
        intersections.reserve(boundaries.size());
        AllIntersectionsVisitor visitor(edge_grid, intersections, Line(start, end));
        edge_grid.visit_cells_intersecting_line(start, end, visitor);
        Vec2d dir = (end - start).cast<double>();
        for (Intersection &intersection : intersections)
            intersection.distance = boundary.boundaries_params[intersection.border_idx][intersection.line_idx];
        std::sort(intersections.begin(), intersections.end(), [dir](const auto &l, const auto &r) { return (r.point - l.point).template cast<double>().dot(dir) > 0.; });
    }

    std::vector<TravelPoint> result;
    result.push_back({start, -1});
    for (auto it_first = intersections.begin(); it_first != intersections.end(); ++it_first) {
        // The entry point to the boundary polygon
        const Intersection &intersection_first = *it_first;
        // Skip the it_first from the search for the farthest exit point from the boundary polygon
        auto it_last_item = std::make_reverse_iterator(it_first) - 1;
        // Search for the farthest intersection different from it_first but with the same border_idx
        auto it_second_r  = std::find_if(intersections.rbegin(), it_last_item, [&intersection_first](const Intersection &intersection) {
            return intersection_first.border_idx == intersection.border_idx;
        });

        // Append the first intersection into the path
        size_t left_idx  = intersection_first.line_idx;
        size_t right_idx = intersection_first.line_idx + 1 == boundaries[intersection_first.border_idx].points.size() ? 0 : intersection_first.line_idx + 1;
        // Offset of the polygon's point using get_middle_point_offset is used to simplify the calculation of intersection between the
        // boundary and the travel. The appended point is translated in the direction of inward normal. This translation ensures that the
        // appended point will be inside the polygon and not on the polygon border.
        result.push_back({get_middle_point_offset(boundaries[intersection_first.border_idx], left_idx, right_idx, intersection_first.point, coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});

        // Check if intersection line also exit the boundary polygon
        if (it_second_r != it_last_item) {
            // Transform reverse iterator to forward
            auto it_second = it_second_r.base() - 1;
            // The exit point from the boundary polygon
            const Intersection &intersection_second = *it_second;
            Direction           shortest_direction  = get_shortest_direction(boundary, intersection_first, intersection_second,
                                                                  boundary.boundaries_params[intersection_first.border_idx].back());
            // Append the path around the border into the path
            if (shortest_direction == Direction::Forward)
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                    line_idx      = line_idx + 1 < int(boundaries[intersection_first.border_idx].size()) ? line_idx + 1 : 0)
                    result.push_back({get_polygon_vertex_offset(boundaries[intersection_first.border_idx],
                                                                (line_idx + 1 == int(boundaries[intersection_first.border_idx].points.size())) ? 0 : (line_idx + 1), coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});
            else
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                    line_idx      = line_idx - 1 >= 0 ? line_idx - 1 : int(boundaries[intersection_first.border_idx].size()) - 1)
                    result.push_back({get_polygon_vertex_offset(boundaries[intersection_second.border_idx], line_idx + 0, coord_t(SCALED_EPSILON)), int(intersection_first.border_idx)});

            // Append the farthest intersection into the path
            left_idx  = intersection_second.line_idx;
            right_idx = (intersection_second.line_idx >= (boundaries[intersection_second.border_idx].points.size() - 1)) ? 0 : (intersection_second.line_idx + 1);
            result.push_back({get_middle_point_offset(boundaries[intersection_second.border_idx], left_idx, right_idx, intersection_second.point, coord_t(SCALED_EPSILON)), int(intersection_second.border_idx)});
            // Skip intersections in between
            it_first = it_second;
        }
    }

    result.push_back({end, -1});

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections,
                             debug_out_path("AvoidCrossingPerimetersInner-initial-%d.svg", iRun++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    if (! intersections.empty())
        result = simplify_travel(boundary, result);

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), result, intersections,
                             debug_out_path("AvoidCrossingPerimetersInner-final-%d.svg", iRun++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    append(result_out, std::move(result));
    return intersections.size();
}

static std::vector<TravelPoint> simplify_travel_heuristics(const AvoidCrossingPerimeters::Boundary &boundary,
                                                           const std::vector<TravelPoint> &travel)
{
    std::vector<TravelPoint>  simplified_path;
    std::vector<Intersection> intersections;
    AllIntersectionsVisitor   visitor(boundary.grid, intersections);
    simplified_path.reserve(travel.size());
    simplified_path.emplace_back(travel.front());
    for (size_t point_idx = 1; point_idx < travel.size(); ++point_idx) {
        // Skip all indexes on the same polygon
        while (point_idx < travel.size() && travel[point_idx - 1].border_idx == travel[point_idx].border_idx) {
            simplified_path.emplace_back(travel[point_idx]);
            point_idx++;
        }

        if (point_idx < travel.size()) {
            const TravelPoint       &current                 = travel[point_idx - 1];
            const TravelPoint       &next                    = travel[point_idx];
            TravelPoint              new_next                = next;
            size_t                   new_point_idx           = point_idx;
            double                   path_length             = (next.point - current.point).cast<double>().norm();
            double                   new_path_shorter_by     = 0.;
            size_t                   border_idx_change_count = 0;
            std::vector<TravelPoint> shortcut;
            for (size_t point_idx_2 = point_idx + 1; point_idx_2 < travel.size(); ++point_idx_2) {
                const TravelPoint &possible_new_next = travel[point_idx_2];
                if (travel[point_idx_2 - 1].border_idx != travel[point_idx_2].border_idx)
                    border_idx_change_count++;

                if (border_idx_change_count >= 2)
                    break;

                path_length += (possible_new_next.point - travel[point_idx_2 - 1].point).cast<double>().norm();
                double shortcut_length = (possible_new_next.point - current.point).cast<double>().norm();
                if ((path_length - shortcut_length) <= scale_(10.0))
                    continue;

                intersections.clear();
                visitor.reset();
                visitor.travel_line.a       = current.point;
                visitor.travel_line.b       = possible_new_next.point;
                boundary.grid.visit_cells_intersecting_line(visitor.travel_line.a, visitor.travel_line.b, visitor);
                if (!intersections.empty()) {
                    Vec2d dir = (visitor.travel_line.b - visitor.travel_line.a).cast<double>();
                    std::sort(intersections.begin(), intersections.end(), [dir](const auto &l, const auto &r) { return (r.point - l.point).template cast<double>().dot(dir) > 0.; });
                    size_t last_border_idx_count = 0;
                    for (const Intersection &intersection : intersections)
                        if (int(intersection.border_idx) == possible_new_next.border_idx)
                            ++last_border_idx_count;

                    if (last_border_idx_count > 0)
                        continue;

                    std::vector<TravelPoint> possible_shortcut;
                    avoid_perimeters_inner(boundary, current.point, possible_new_next.point, possible_shortcut);
                    double shortcut_travel = travel_length(possible_shortcut);
                    if (path_length > shortcut_travel && path_length - shortcut_travel > new_path_shorter_by) {
                        new_path_shorter_by = path_length - shortcut_travel;
                        shortcut            = possible_shortcut;
                        new_next            = possible_new_next;
                        new_point_idx       = point_idx_2;
                    }
                }
            }

            if (!shortcut.empty()) {
                assert(shortcut.size() >= 2);
                simplified_path.insert(simplified_path.end(), shortcut.begin() + 1, shortcut.end() - 1);
                point_idx = new_point_idx;
            }

            simplified_path.emplace_back(new_next);
        }
    }

    return simplified_path;
}

// Called by AvoidCrossingPerimeters::travel_to()
static size_t avoid_perimeters(const AvoidCrossingPerimeters::Boundary &boundary,
                               const Point              &start,
                               const Point              &end,
                               Polyline                 &result_out)
{
    // Travel line is completely or partially inside the bounding box.
    std::vector<TravelPoint> path;
    size_t num_intersections = avoid_perimeters_inner(boundary, start, end, path);
    if (num_intersections) {
        path = simplify_travel_heuristics(boundary, path);
        std::reverse(path.begin(), path.end());
        path = simplify_travel_heuristics(boundary, path);
        std::reverse(path.begin(), path.end());
    }

    result_out = to_polyline(path);

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, Line(start, end), path, {}, debug_out_path("AvoidCrossingPerimeters-final-%d.svg", iRun ++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    return num_intersections;
}

// Plan travel, which avoids perimeter crossings by following the boundaries of the layer.
Polyline AvoidCrossingPerimeters::travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled)
{
    // If use_external, then perform the path planning in the world coordinate system (correcting for the gcodegen offset).
    // Otherwise perform the path planning in the coordinate system of the active object.
    bool     use_external  = m_use_external_mp || m_use_external_mp_once;
    Point    scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    Point    start         = gcodegen.last_pos() + scaled_origin;
    Point    end           = point + scaled_origin;
    Polyline result_pl;
    size_t   travel_intersection_count = 0;
    Vec2d startf = start.cast<double>();
    Vec2d endf   = end  .cast<double>();
    // Trim the travel line by the bounding box.
    if (Geometry::liang_barsky_line_clipping(startf, endf, (use_external ? m_external : m_internal).bbox)) {
        // Travel line is completely or partially inside the bounding box.
        //FIXME initialize m_boundaries / m_boundaries_external on demand?
        travel_intersection_count = avoid_perimeters((use_external ? m_external : m_internal), startf.cast<coord_t>(), endf.cast<coord_t>(),
                                                     result_pl);
        result_pl.points.front()  = start;
        result_pl.points.back()   = end;
    } else {
        // Travel line is completely outside the bounding box.
        result_pl                 = {start, end};
        travel_intersection_count = 0;
    }

    Line travel(start, end);
    double max_detour_length scale_(gcodegen.config().max_travel_detour_distance);
    if (max_detour_length > 0 && (result_pl.length() - travel.length()) > max_detour_length)
        result_pl = {start, end};

    if (use_external) {
        result_pl.translate(-scaled_origin);
        *could_be_wipe_disabled = false;
    } else
        *could_be_wipe_disabled = !need_wipe(gcodegen, m_grid_lslice, travel, result_pl, travel_intersection_count);

    return result_pl;
}

// called by AvoidCrossingPerimeters::init_layer()->get_boundary()/get_boundary_external()
static std::pair<Polygons, Polygons> split_expolygon(const ExPolygons &ex_polygons)
{
    Polygons contours, holes;
    contours.reserve(ex_polygons.size());
    holes.reserve(std::accumulate(ex_polygons.begin(), ex_polygons.end(), size_t(0),
                                  [](size_t sum, const ExPolygon &ex_poly) { return sum + ex_poly.holes.size(); }));
    for (const ExPolygon &ex_poly : ex_polygons) {
        contours.emplace_back(ex_poly.contour);
        append(holes, ex_poly.holes);
    }
    return std::make_pair(std::move(contours), std::move(holes));
}

// called by AvoidCrossingPerimeters::init_layer()
static ExPolygons get_boundary(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    size_t      polygons_count    = 0;
    for (const LayerRegion *layer_region : layer.regions())
        polygons_count += layer_region->slices.surfaces.size();

    ExPolygons boundary;
    boundary.reserve(polygons_count);
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->slices.surfaces)
            boundary.emplace_back(surface.expolygon);

    boundary                      = union_ex(boundary);
    ExPolygons perimeter_boundary = offset_ex(boundary, -perimeter_offset);
    ExPolygons result_boundary;
    if (perimeter_boundary.size() != boundary.size()) {
        //FIXME ???
        // If any part of the polygon is missing after shrinking, then for misisng parts are is used the boundary of the slice.
        ExPolygons missing_perimeter_boundary = offset_ex(diff_ex(boundary,
                                                                  offset_ex(perimeter_boundary, perimeter_offset + float(SCALED_EPSILON) / 2.f)),
                                                          perimeter_offset + float(SCALED_EPSILON));
        perimeter_boundary                    = offset_ex(perimeter_boundary, perimeter_offset);
        append(perimeter_boundary, std::move(missing_perimeter_boundary));
        // By calling intersection_ex some artifacts arose by previous operations are removed.
        result_boundary = intersection_ex(offset_ex(perimeter_boundary, -perimeter_offset), boundary);
    } else {
        result_boundary = std::move(perimeter_boundary);
    }

    auto [contours, holes] = split_expolygon(boundary);
    // Add an outer boundary to avoid crossing perimeters from supports
    ExPolygons outer_boundary = union_ex(
        diff(offset(Geometry::convex_hull(contours), 2.f * perimeter_spacing), offset(contours, perimeter_spacing + perimeter_offset)));
    result_boundary.insert(result_boundary.end(), outer_boundary.begin(), outer_boundary.end());
    ExPolygons holes_boundary = offset_ex(holes, -perimeter_spacing);
    result_boundary.insert(result_boundary.end(), holes_boundary.begin(), holes_boundary.end());
    result_boundary = union_ex(result_boundary);

    // Collect all top layers that will not be crossed.
    polygons_count = 0;
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->fill_surfaces.surfaces)
            if (surface.is_top()) ++polygons_count;

    if (polygons_count > 0) {
        ExPolygons top_layer_polygons;
        top_layer_polygons.reserve(polygons_count);
        for (const LayerRegion *layer_region : layer.regions())
            for (const Surface &surface : layer_region->fill_surfaces.surfaces)
                if (surface.is_top()) top_layer_polygons.emplace_back(surface.expolygon);

        top_layer_polygons = union_ex(top_layer_polygons);
        return diff_ex(result_boundary, offset_ex(top_layer_polygons, -perimeter_offset));
    }

    return result_boundary;
}

// called by AvoidCrossingPerimeters::init_layer()
static ExPolygons get_boundary_external(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing_external(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    ExPolygons  boundary;
    // Collect all polygons for all printed objects and their instances, which will be printed at the same time as passed "layer".
    for (const PrintObject *object : layer.object()->print()->objects()) {
        ExPolygons polygons_per_obj;
        //FIXME with different layering, layers on other objects will not be found at this object's print_z.
        // Search an overlap of layers?
        if (const Layer* l = object->get_layer_at_printz(layer.print_z, EPSILON); l)
            for (const LayerRegion *layer_region : l->regions())
                for (const Surface &surface : layer_region->slices.surfaces)
                    polygons_per_obj.emplace_back(surface.expolygon);

        for (const PrintInstance &instance : object->instances()) {
            size_t boundary_idx = boundary.size();
            boundary.insert(boundary.end(), polygons_per_obj.begin(), polygons_per_obj.end());
            for (; boundary_idx < boundary.size(); ++boundary_idx)
                boundary[boundary_idx].translate(instance.shift);
        }
    }
    boundary               = union_ex(boundary);
    auto [contours, holes] = split_expolygon(boundary);
    // Polygons in which is possible traveling without crossing perimeters of another object.
    // A convex hull allows removing unnecessary detour caused by following the boundary of the object.
    ExPolygons result_boundary =
        diff_ex(offset(Geometry::convex_hull(contours), 2.f * perimeter_spacing),offset(contours,  perimeter_spacing + perimeter_offset));
    // All holes are extended for forcing travel around the outer perimeter of a hole when a hole is crossed.
    append(result_boundary, diff_ex(offset(holes, perimeter_spacing), offset(holes, perimeter_offset)));
    return union_ex(result_boundary);
}

void AvoidCrossingPerimeters::init_layer(const Layer &layer)
{
    m_internal.boundaries.clear();
    m_external.boundaries.clear();

    m_internal.boundaries = to_polygons(get_boundary(layer));
    m_external.boundaries = to_polygons(get_boundary_external(layer));

    BoundingBox bbox(get_extents(m_internal.boundaries));
    bbox.offset(SCALED_EPSILON);
    BoundingBox bbox_external = get_extents(m_external.boundaries);
    bbox_external.offset(SCALED_EPSILON);
    BoundingBox bbox_slice(get_extents(layer.lslices));
    bbox_slice.offset(SCALED_EPSILON);

    m_internal.bbox = BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>());
    m_external.bbox = BoundingBoxf(bbox_external.min.cast<double>(), bbox_external.max.cast<double>());

    m_internal.grid.set_bbox(bbox);
    //FIX1ME 1mm grid?
    m_internal.grid.create(m_internal.boundaries, coord_t(scale_(1.)));
    m_external.grid.set_bbox(bbox_external);
    //FIX1ME 1mm grid?
    m_external.grid.create(m_external.boundaries, coord_t(scale_(1.)));
    m_grid_lslice.set_bbox(bbox_slice);
    //FIX1ME 1mm grid?
    m_grid_lslice.create(layer.lslices, coord_t(scale_(1.)));

    init_boundary_distances(&m_internal);
    init_boundary_distances(&m_external);
}
#endif

} // namespace Slic3r
