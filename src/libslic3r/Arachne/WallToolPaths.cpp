// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] WallToolPaths.cpp implements the Arachne variable-width perimeter pipeline.
// This file contains:
//   1. make_paths_params() — converts PrintConfig to WallToolPathsParams
//   2. A suite of static polygon pre-processing utilities (simplify, fixSelfIntersections,
//      removeDegenerateVerts, removeColinearEdges, removeSmallAreas)
//   3. WallToolPaths::generate() — the main pipeline entry point
//   4. Post-processing utilities (stitchToolPaths, removeSmallLines, simplifyToolPaths)
//   5. separateOutInnerContour() / getInnerContour() — inner boundary extraction
//   6. removeEmptyToolPaths() / getRegionOrder() — cleanup and ordering
//
// [MEMORY] All polygon pre-processing functions operate on Polygons by value or reference,
// creating temporary copies where needed (e.g., simplify() creates a new_path before assigning).
// The main pipeline in generate() creates `prepared_outline` as a local copy of `outline`,
// which is progressively modified through 7 pre-processing steps. Peak memory is approximately
// 2× the input outline size during this preprocessing phase.
//
// [CONCURRENCY] All functions are stateless static utilities except the WallToolPaths member
// functions which operate on `this`. Per the class contract, WallToolPaths instances are not
// shared across threads — each layer worker creates its own instance.
//
// [COUPLING] This file depends on:
//   - SkeletalTrapezoidation (core algorithm, instantiated in generate())
//   - BeadingStrategyFactory (creates bead distribution strategy)
//   - PolylineStitcher (joins open extrusion line polylines into closed polygons)
//   - ClipperUtils (offset, union polygon operations)
//   - SparseLineGrid/SparsePointGrid (spatial lookup for stitching and ordering)
//   - EdgeGrid (used implicitly via PolygonsPointIndex/PolygonsPointIndexSegmentLocator)

#include <algorithm> //For std::partition_copy and std::min_element.
#include <unordered_set>

#include "WallToolPaths.hpp"

#include "SkeletalTrapezoidation.hpp"
#include "../ClipperUtils.hpp"
#include "utils/linearAlg2D.hpp"
#include "EdgeGrid.hpp"
#include "utils/SparseLineGrid.hpp"
#include "Geometry.hpp"
#include "utils/PolylineStitcher.hpp"
#include "SVG.hpp"
#include "Utils.hpp"

#include <boost/log/trivial.hpp>

// #define ARACHNE_STITCH_PATCH_DEBUG

namespace Slic3r::Arachne {

// [INTENT] Converts PrintObjectConfig/PrintConfig into a WallToolPathsParams bundle.
// All percentage-based config values are converted to absolute mm by multiplying by
// min_nozzle_diameter (minimum of all tools). Layer 0 uses initial_layer_min_bead_width;
// all other layers use min_bead_width. Falls back to min_length_factor=0.5 if not configured.
// [HAZARD] is_top_or_bottom_layer is always set to false here. Callers that need true for
// top/bottom layers MUST set it manually after the call, or removeSmallLines() will use
// the wrong (too-aggressive) threshold for those layers.
// [HAZARD] Uses min_nozzle_diameter (minimum across all tools) for all percentage conversions.
// In multi-nozzle setups, a large secondary nozzle's Arachne parameters will be governed by
// the smallest nozzle, which may over-constrain the secondary's bead width choices.
WallToolPathsParams make_paths_params(const int layer_id, const PrintObjectConfig& print_object_config, const PrintConfig& print_config)
{
    WallToolPathsParams input_params;
    {
        const double min_nozzle_diameter = *std::min_element(print_config.nozzle_diameter.values.begin(),
                                                             print_config.nozzle_diameter.values.end());
        if (const auto& min_feature_size_opt = print_object_config.min_feature_size)
            input_params.min_feature_size = min_feature_size_opt.value * 0.01 * min_nozzle_diameter;

        if (const auto& min_wall_length_factor_opt = print_object_config.min_length_factor)
            input_params.min_length_factor = min_wall_length_factor_opt.value;
        else
            input_params.min_length_factor = 0.5f;

        if (layer_id == 0) {
            if (const auto& initial_layer_min_bead_width_opt = print_object_config.initial_layer_min_bead_width)
                input_params.min_bead_width = initial_layer_min_bead_width_opt.value * 0.01 * min_nozzle_diameter;
        } else {
            if (const auto& min_bead_width_opt = print_object_config.min_bead_width)
                input_params.min_bead_width = min_bead_width_opt.value * 0.01 * min_nozzle_diameter;
        }

        if (const auto& wall_transition_filter_deviation_opt = print_object_config.wall_transition_filter_deviation)
            input_params.wall_transition_filter_deviation = wall_transition_filter_deviation_opt.value * 0.01 * min_nozzle_diameter;

        if (const auto& wall_transition_length_opt = print_object_config.wall_transition_length)
            input_params.wall_transition_length = wall_transition_length_opt.value * 0.01 * min_nozzle_diameter;

        input_params.wall_transition_angle   = print_object_config.wall_transition_angle.value;
        input_params.wall_distribution_count = print_object_config.wall_distribution_count.value;

        input_params.is_top_or_bottom_layer = false; // Set to default value
    }

    return input_params;
}

// [INTENT] Constructor stores all inputs. Scales mm-unit params (min_feature_size, min_bead_width,
// wall_transition_filter_deviation) to coord_t (scaled nanometers) via scaled<coord_t>().
// small_area_length is computed as bead_width_0/2 in unscaled double for use in removeSmallAreas().
// print_thin_walls is hardcoded to fill_outline_gaps (true), enabling thin-wall widening.
// toolpaths_generated = false — generation is deferred until generate() is called explicitly.
// [HAZARD] The `outline` parameter is stored by const reference, not by value. The caller's
// polygon must outlive this WallToolPaths object. If the caller stores outline as a local
// variable and returns WallToolPaths, the reference will dangle.
// [HAZARD] params.min_feature_size and params.min_bead_width are float (mm). scaled<coord_t>()
// converts them to int32 nanometers. For very small values (< 0.001mm), the scaled value will
// be 0 (floors to integer). This silently disables thin-wall features that are too small to
// represent in coord_t precision. A refactored implementation should clamp to at least 1 nm.
WallToolPaths::WallToolPaths(const Polygons&            outline,
                             const coord_t              bead_width_0,
                             const coord_t              bead_width_x,
                             const size_t               inset_count,
                             const coord_t              wall_0_inset,
                             const coordf_t             layer_height,
                             const WallToolPathsParams& params)
    : outline(outline)
    , bead_width_0(bead_width_0)
    , bead_width_x(bead_width_x)
    , inset_count(inset_count)
    , wall_0_inset(wall_0_inset)
    , layer_height(layer_height)
    , print_thin_walls(Slic3r::Arachne::fill_outline_gaps)
    , min_feature_size(scaled<coord_t>(params.min_feature_size))
    , min_bead_width(scaled<coord_t>(params.min_bead_width))
    , small_area_length(static_cast<double>(bead_width_0) / 2.)
    , wall_transition_filter_deviation(scaled<coord_t>(params.wall_transition_filter_deviation))
    , toolpaths_generated(false)
    , m_params(params)
{}

// [INTENT] Polygon vertex simplification using the Ramer-Douglas-Peucker-style accumulated
// area deviation algorithm. Removes vertices that contribute less than allowed_error_distance
// of height deviation to the polygon's cross-section.
// Uses the Shoelace formula to accumulate area removed by each potential vertex deletion,
// avoiding recomputation for consecutive removals.
// Special case: if removing a vertex would leave a very long next segment, compute the
// intersection point of the two bounding lines and use that instead (avoids noticeable artifacts).
// [HAZARD] The algorithm uses `int64_t` for area and length squared, which can overflow for very
// large coordinates. At OrcaSlicer's maximum part size (~500mm = 500,000,000 nm), squared
// values can reach 2.5×10^17, safely within int64_t range (9.2×10^18). But chains of operations
// (e.g., area_removed_so_far accumulates across many removed vertices) could overflow for
// pathological polygon inputs.
// [HAZARD] Polygons with fewer than 3 points are cleared (made empty). Polygons with exactly
// 3 points are returned unchanged. The caller (simplify(Polygons&)) removes polygons that end
// up with fewer than 3 points after simplification, using erase+decrement.
void simplify(Polygon& thiss, const int64_t smallest_line_segment_squared, const int64_t allowed_error_distance_squared)
{
    if (thiss.size() < 3) {
        thiss.points.clear();
        return;
    }
    if (thiss.size() == 3)
        return;

    Polygon new_path;
    Point   previous          = thiss.points.back();
    Point   previous_previous = thiss.points.at(thiss.points.size() - 2);
    Point   current           = thiss.points.at(0);

    /* When removing a vertex, we check the height of the triangle of the area
     being removed from the original polygon by the simplification. However,
     when consecutively removing multiple vertices the height of the previously
     removed vertices w.r.t. the shortcut path changes.
     In order to not recompute the new height value of previously removed
     vertices we compute the height of a representative triangle, which covers
     the same amount of area as the area being cut off. We use the Shoelace
     formula to accumulate the area under the removed segments. This works by
     computing the area in a 'fan' where each of the blades of the fan go from
     the origin to one of the segments. While removing vertices the area in
     this fan accumulates. By subtracting the area of the blade connected to
     the short-cutting segment we obtain the total area of the cutoff region.
     From this area we compute the height of the representative triangle using
     the standard formula for a triangle area: A = .5*b*h
     */
    int64_t accumulated_area_removed = int64_t(previous.x()) * int64_t(current.y()) -
                                       int64_t(previous.y()) *
                                           int64_t(current.x()); // Twice the Shoelace formula for area of polygon per line segment.

    for (size_t point_idx = 0; point_idx < thiss.points.size(); point_idx++) {
        current = thiss.points.at(point_idx % thiss.points.size());

        // Check if the accumulated area doesn't exceed the maximum.
        Point next;
        if (point_idx + 1 < thiss.points.size()) {
            next = thiss.points.at(point_idx + 1);
        } else if (point_idx + 1 == thiss.points.size() &&
                   new_path.size() > 1) { // don't spill over if the [next] vertex will then be equal to [previous]
            next = new_path[0];           // Spill over to new polygon for checking removed area.
        } else {
            next = thiss.points.at((point_idx + 1) % thiss.points.size());
        }
        const int64_t removed_area_next = int64_t(current.x()) * int64_t(next.y()) -
                                          int64_t(current.y()) *
                                              int64_t(next.x()); // Twice the Shoelace formula for area of polygon per line segment.
        const int64_t negative_area_closing = int64_t(next.x()) * int64_t(previous.y()) -
                                              int64_t(next.y()) *
                                                  int64_t(previous.x()); // area between the origin and the short-cutting segment
        accumulated_area_removed += removed_area_next;

        const int64_t length2 = (current - previous).cast<int64_t>().squaredNorm();
        if (length2 < scaled<int64_t>(25.)) {
            // We're allowed to always delete segments of less than 5 micron.
            continue;
        }

        const int64_t area_removed_so_far = accumulated_area_removed + negative_area_closing; // close the shortcut area polygon
        const int64_t base_length_2       = (next - previous).cast<int64_t>().squaredNorm();

        if (base_length_2 == 0) // Two line segments form a line back and forth with no area.
            continue;           // Remove the vertex.
        // We want to check if the height of the triangle formed by previous, current and next vertices is less than
        // allowed_error_distance_squared. 1/2 L = A           [actual area is half of the computed shoelace value] // Shoelace formula is
        // .5*(...) , but we simplify the computation and take out the .5 A = 1/2 * b * h     [triangle area formula] L = b * h [apply above
        // two and take out the 1/2] h = L / b           [divide by b] h^2 = (L / b)^2     [square it] h^2 = L^2 / b^2     [factor the
        // divisor]
        const int64_t height_2 = double(area_removed_so_far) * double(area_removed_so_far) / double(base_length_2);
        if ((height_2 <= Slic3r::sqr(scaled<coord_t>(0.005)) // Almost exactly colinear (barring rounding errors).
             && Line::distance_to_infinite(current, previous, next) <=
                    scaled<double>(0.005))) // make sure that height_2 is not small because of cancellation of positive and negative areas
            continue;

        if (length2 < smallest_line_segment_squared &&
            height_2 <= allowed_error_distance_squared) // removing the vertex doesn't introduce too much error.)
        {
            const int64_t next_length2 = (current - next).cast<int64_t>().squaredNorm();
            if (next_length2 > 4 * smallest_line_segment_squared) {
                // Special case; The next line is long. If we were to remove this, it could happen that we get quite noticeable artifacts.
                // We should instead move this point to a location where both edges are kept and then remove the previous point that we
                // wanted to keep. By taking the intersection of these two lines, we get a point that preserves the direction (so it makes
                // the corner a bit more pointy). We just need to be sure that the intersection point does not introduce an artifact itself.
                Point intersection_point;
                bool  has_intersection = Line(previous_previous, previous).intersection_infinite(Line(current, next), &intersection_point);
                if (!has_intersection ||
                    Line::distance_to_infinite_squared(intersection_point, previous, current) > double(allowed_error_distance_squared) ||
                    (intersection_point - previous).cast<int64_t>().squaredNorm() >
                        smallest_line_segment_squared // The intersection point is way too far from the 'previous'
                    || (intersection_point - next).cast<int64_t>().squaredNorm() >
                           smallest_line_segment_squared) // and 'next' points, so it shouldn't replace 'current'
                {
                    // We can't find a better spot for it, but the size of the line is more than 5 micron.
                    // So the only thing we can do here is leave it in...
                } else {
                    // New point seems like a valid one.
                    current = intersection_point;
                    // If there was a previous point added, remove it.
                    if (!new_path.empty()) {
                        new_path.points.pop_back();
                        previous = previous_previous;
                    }
                }
            } else {
                continue; // Remove the vertex.
            }
        }
        // Don't remove the vertex.
        accumulated_area_removed =
            removed_area_next; // so that in the next iteration it's the area between the origin, [previous] and [current]
        previous_previous = previous;
        previous          = current; // Note that "previous" is only updated if we don't remove the vertex.
        new_path.points.push_back(current);
    }

    thiss = new_path;
}

/*!
 * Removes vertices of the polygons to make sure that they are not too high
 * resolution.
 *
 * This removes points which are connected to line segments that are shorter
 * than the `smallest_line_segment`, unless that would introduce a deviation
 * in the contour of more than `allowed_error_distance`.
 *
 * Criteria:
 * 1. Never remove a vertex if either of the connceted segments is larger than \p smallest_line_segment
 * 2. Never remove a vertex if the distance between that vertex and the final resulting polygon would be higher than \p allowed_error_distance
 * 3. The direction of segments longer than \p smallest_line_segment always
 * remains unaltered (but their end points may change if it is connected to
 * a small segment)
 *
 * Simplify uses a heuristic and doesn't neccesarily remove all removable
 * vertices under the above criteria, but simplify may never violate these
 * criteria. Unless the segments or the distance is smaller than the
 * rounding error of 5 micron.
 *
 * Vertices which introduce an error of less than 5 microns are removed
 * anyway, even if the segments are longer than the smallest line segment.
 * This makes sure that (practically) colinear line segments are joined into
 * a single line segment.
 * \param smallest_line_segment Maximal length of removed line segments.
 * \param allowed_error_distance If removing a vertex introduces a deviation
 * from the original path that is more than this distance, the vertex may
 * not be removed.
 */
void simplify(Polygons&     thiss,
              const int64_t smallest_line_segment  = scaled<coord_t>(0.01),
              const int64_t allowed_error_distance = scaled<coord_t>(0.005))
{
    const int64_t allowed_error_distance_squared = int64_t(allowed_error_distance) * int64_t(allowed_error_distance);
    const int64_t smallest_line_segment_squared  = int64_t(smallest_line_segment) * int64_t(smallest_line_segment);
    for (size_t p = 0; p < thiss.size(); p++) {
        simplify(thiss[p], smallest_line_segment_squared, allowed_error_distance_squared);
        if (thiss[p].size() < 3) {
            thiss.erase(thiss.begin() + p);
            p--;
        }
    }
}

typedef SparseLineGrid<PolygonsPointIndex, PolygonsPointIndexSegmentLocator> LocToLineGrid;
std::unique_ptr<LocToLineGrid>                                               createLocToLineGrid(const Polygons& polygons, int square_size)
{
    unsigned int n_points = 0;
    for (const auto& poly : polygons)
        n_points += poly.size();

    auto ret = std::make_unique<LocToLineGrid>(square_size, n_points);

    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
        for (unsigned int point_idx = 0; point_idx < polygons[poly_idx].size(); point_idx++)
            ret->insert(PolygonsPointIndex(&polygons, poly_idx, point_idx));
    return ret;
}

/* Note: Also tries to solve for near-self intersections, when epsilon >= 1
 */
void fixSelfIntersections(const coord_t epsilon, Polygons& thiss)
{
    if (epsilon < 1) {
        ClipperLib::SimplifyPolygons(ClipperUtils::PolygonsProvider(thiss), ClipperLib::pftEvenOdd);
        return;
    }

    const int64_t half_epsilon = (epsilon + 1) / 2;

    // Points too close to line segments should be moved a little away from those line segments, but less than epsilon,
    //   so at least half-epsilon distance between points can still be guaranteed.
    const coord_t grid_size  = scaled<coord_t>(2.);
    auto          query_grid = createLocToLineGrid(thiss, grid_size);

    const auto    move_dist         = std::max<int64_t>(2L, half_epsilon - 2);
    const int64_t half_epsilon_sqrd = half_epsilon * half_epsilon;

    const size_t n = thiss.size();
    for (size_t poly_idx = 0; poly_idx < n; poly_idx++) {
        const size_t pathlen = thiss[poly_idx].size();
        for (size_t point_idx = 0; point_idx < pathlen; ++point_idx) {
            Point& pt = thiss[poly_idx][point_idx];
            for (const auto& line : query_grid->getNearby(pt, epsilon)) {
                const size_t line_next_idx = (line.point_idx + 1) % thiss[line.poly_idx].size();
                if (poly_idx == line.poly_idx && (point_idx == line.point_idx || point_idx == line_next_idx))
                    continue;

                const Line segment(thiss[line.poly_idx][line.point_idx], thiss[line.poly_idx][line_next_idx]);
                Point      segment_closest_point;
                segment.distance_to_squared(pt, &segment_closest_point);

                if (half_epsilon_sqrd >= (pt - segment_closest_point).cast<int64_t>().squaredNorm()) {
                    const Point&  other = thiss[poly_idx][(point_idx + 1) % pathlen];
                    const Vec2i64 vec   = (LinearAlg2D::pointIsLeftOfLine(other, segment.a, segment.b) > 0 ? segment.b - segment.a :
                                                                                                             segment.a - segment.b)
                                            .cast<int64_t>();
                    assert(Slic3r::sqr(double(vec.x())) < double(std::numeric_limits<int64_t>::max()));
                    assert(Slic3r::sqr(double(vec.y())) < double(std::numeric_limits<int64_t>::max()));
                    const int64_t len = vec.norm();
                    pt.x() += (-vec.y() * move_dist) / len;
                    pt.y() += (vec.x() * move_dist) / len;
                }
            }
        }
    }

    ClipperLib::SimplifyPolygons(ClipperUtils::PolygonsProvider(thiss), ClipperLib::pftEvenOdd);
}

/*!
 * Removes overlapping consecutive line segments which don't delimit a positive area.
 */
void removeDegenerateVerts(Polygons& thiss)
{
    for (size_t poly_idx = 0; poly_idx < thiss.size(); poly_idx++) {
        Polygon& poly = thiss[poly_idx];
        Polygon  result;

        auto isDegenerate = [](const Point& last, const Point& now, const Point& next) {
            Vec2i64 last_line = (now - last).cast<int64_t>();
            Vec2i64 next_line = (next - now).cast<int64_t>();
            return last_line.dot(next_line) == -1 * last_line.norm() * next_line.norm();
        };
        bool isChanged = false;
        for (size_t idx = 0; idx < poly.size(); idx++) {
            const Point& last = (result.size() == 0) ? poly.back() : result.back();
            if (idx + 1 == poly.size() && result.size() == 0)
                break;

            const Point& next = (idx + 1 == poly.size()) ? result[0] : poly[idx + 1];
            if (isDegenerate(last, poly[idx], next)) { // lines are in the opposite direction
                // don't add vert to the result
                isChanged = true;
                while (result.size() > 1 && isDegenerate(result[result.size() - 2], result.back(), next))
                    result.points.pop_back();
            } else {
                result.points.emplace_back(poly[idx]);
            }
        }

        if (isChanged) {
            if (result.size() > 2) {
                poly = result;
            } else {
                thiss.erase(thiss.begin() + poly_idx);
                poly_idx--; // effectively the next iteration has the same poly_idx (referring to a new poly which is not yet processed)
            }
        }
    }
}

void removeSmallAreas(Polygons& thiss, const double min_area_size, const bool remove_holes)
{
    auto to_path = [](const Polygon& poly) -> ClipperLib::Path {
        ClipperLib::Path out;
        for (const Point& pt : poly.points)
            out.emplace_back(ClipperLib::cInt(pt.x()), ClipperLib::cInt(pt.y()));
        return out;
    };

    auto new_end = thiss.end();
    if (remove_holes) {
        for (auto it = thiss.begin(); it < new_end;) {
            // All polygons smaller than target are removed by replacing them with a polygon from the back of the vector.
            if (fabs(ClipperLib::Area(to_path(*it))) < min_area_size) {
                --new_end;
                *it = std::move(*new_end);
                continue; // Don't increment the iterator such that the polygon just swapped in is checked next.
            }
            ++it;
        }
    } else {
        // For each polygon, computes the signed area, move small outlines at the end of the vector and keep pointer on small holes
        Polygons small_holes;
        for (auto it = thiss.begin(); it < new_end;) {
            if (double area = ClipperLib::Area(to_path(*it)); fabs(area) < min_area_size) {
                if (area >= 0) {
                    --new_end;
                    if (it < new_end) {
                        std::swap(*new_end, *it);
                        continue;
                    } else { // Don't self-swap the last Path
                        break;
                    }
                } else {
                    small_holes.push_back(*it);
                }
            }
            ++it;
        }

        // Removes small holes that have their first point inside one of the removed outlines
        // Iterating in reverse ensures that unprocessed small holes won't be moved
        const auto removed_outlines_start = new_end;
        for (auto hole_it = small_holes.rbegin(); hole_it < small_holes.rend(); hole_it++)
            for (auto outline_it = removed_outlines_start; outline_it < thiss.end(); outline_it++)
                if (Polygon(*outline_it).contains(*hole_it->begin())) {
                    new_end--;
                    *hole_it = std::move(*new_end);
                    break;
                }
    }
    thiss.resize(new_end - thiss.begin());
}

void removeColinearEdges(Polygon& poly, const double max_deviation_angle)
{
    // TODO: Can be made more efficient (for example, use pointer-types for process-/skip-indices, so we can swap them without copy).
    size_t num_removed_in_iteration = 0;
    do {
        num_removed_in_iteration = 0;
        std::vector<bool> process_indices(poly.points.size(), true);

        bool go = true;
        while (go) {
            go = false;

            const auto&  rpath   = poly;
            const size_t pathlen = rpath.size();
            if (pathlen <= 3)
                return;

            std::vector<bool> skip_indices(poly.points.size(), false);

            Polygon new_path;
            for (size_t point_idx = 0; point_idx < pathlen; ++point_idx) {
                // Don't iterate directly over process-indices, but do it this way, because there are points _in_ process-indices that
                // should nonetheless be skipped:
                if (!process_indices[point_idx]) {
                    new_path.points.push_back(rpath[point_idx]);
                    continue;
                }

                // Should skip the last point for this iteration if the old first was removed (which can be seen from the fact that the new
                // first was skipped):
                if (point_idx == (pathlen - 1) && skip_indices[0]) {
                    skip_indices[new_path.size()] = true;
                    go                            = true;
                    new_path.points.push_back(rpath[point_idx]);
                    break;
                }

                const Point& prev = rpath[(point_idx - 1 + pathlen) % pathlen];
                const Point& pt   = rpath[point_idx];
                const Point& next = rpath[(point_idx + 1) % pathlen];

                float angle = LinearAlg2D::getAngleLeft(prev, pt, next); // [0 : 2 * pi]
                if (angle >= float(M_PI)) {
                    angle -= float(M_PI);
                } // map [pi : 2 * pi] to [0 : pi]

                // Check if the angle is within limits for the point to 'make sense', given the maximum deviation.
                // If the angle indicates near-parallel segments ignore the point 'pt'
                if (angle > max_deviation_angle && angle < M_PI - max_deviation_angle) {
                    new_path.points.push_back(pt);
                } else if (point_idx != (pathlen - 1)) {
                    // Skip the next point, since the current one was removed:
                    skip_indices[new_path.size()] = true;
                    go                            = true;
                    new_path.points.push_back(next);
                    ++point_idx;
                }
            }
            poly = new_path;
            num_removed_in_iteration += pathlen - poly.points.size();

            process_indices.clear();
            process_indices.insert(process_indices.end(), skip_indices.begin(), skip_indices.end());
        }
    } while (num_removed_in_iteration > 0);
}

void removeColinearEdges(Polygons& thiss, const double max_deviation_angle = 0.0005)
{
    for (int p = 0; p < int(thiss.size()); p++) {
        removeColinearEdges(thiss[p], max_deviation_angle);
        if (thiss[p].size() < 3) {
            thiss.erase(thiss.begin() + p);
            p--;
        }
    }
}

const std::vector<VariableWidthLines>& WallToolPaths::generate()
{
    // [INTENT] Early exit: if inset_count < 1, no walls are requested; return empty toolpaths.
    if (this->inset_count < 1)
        return toolpaths;

    // [INTENT] Pre-processing parameters:
    //   smallest_segment: max resolution threshold for polygon simplification (0.5mm)
    //   allowed_distance: max deviation when simplifying (0.025mm)
    //   epsilon_offset: snap threshold for the triple-offset self-intersection fix (≈ 11500 nm)
    //   transitioning_angle: convert degrees to radians for SkeletalTrapezoidation
    //   discretization_step_size: arc discretization for curved segments (0.8mm)
    const coord_t smallest_segment         = Slic3r::Arachne::meshfix_maximum_resolution();
    const coord_t allowed_distance         = Slic3r::Arachne::meshfix_maximum_deviation();
    const coord_t epsilon_offset           = (allowed_distance / 2) - 1;
    const double  transitioning_angle      = Geometry::deg2rad(m_params.wall_transition_angle);
    const coord_t discretization_step_size = scaled<coord_t>(0.8);

    // [INTENT] Outline pre-processing pipeline (7 steps):
    // Step 1: Triple offset (- epsilon, + 2*epsilon, - epsilon) snaps near-self-intersections.
    //         This shrinks and re-expands the polygon, merging tiny gaps/spikes.
    // [HAZARD] Features thinner than epsilon_offset ≈ 11500 nm will be permanently erased by
    //         this triple offset. The user's min_feature_size setting has NO effect here —
    //         features below this physical threshold are silently destroyed before SkeletalTrapezoidation
    //         ever sees them.
    // Step 2: simplify() — removes vertices contributing < allowed_distance of deviation.
    // Step 3: fixSelfIntersections() — nudges points near segments and Clipper-simplifies.
    // Step 4: removeDegenerateVerts() — removes vertices where consecutive segments reverse direction.
    // Step 5: removeColinearEdges() — removes nearly-colinear vertices (< 0.005 rad angle).
    //         [HAZARD] removeColinearEdges may INTRODUCE self-intersections (noted in code comment).
    // Step 6: fixSelfIntersections() again — repair any new self-intersections from step 5.
    // Step 7: removeDegenerateVerts() again.
    // Step 8: removeSmallAreas() — removes polygons with area < (bead_width_0/2)^2.
    // Step 9: union_() — final Clipper union to resolve any remaining intersections.
    //         [HAZARD] Each step can silently modify or discard part of the input outline.
    //         The cumulative effect on a complex input polygon with many thin features is
    //         difficult to predict or audit.
    Polygons prepared_outline = offset(offset(offset(outline, -epsilon_offset), epsilon_offset * 2), -epsilon_offset);
    simplify(prepared_outline, smallest_segment, allowed_distance);
    fixSelfIntersections(epsilon_offset, prepared_outline);
    removeDegenerateVerts(prepared_outline);
    removeColinearEdges(prepared_outline, 0.005);
    // Removing collinear edges may introduce self intersections, so we need to fix them again
    fixSelfIntersections(epsilon_offset, prepared_outline);
    removeDegenerateVerts(prepared_outline);
    removeSmallAreas(prepared_outline, small_area_length * small_area_length, false);

    // The functions above could produce intersecting polygons that could cause a crash inside Arachne.
    // Applying Clipper union should be enough to get rid of this issue.
    // Clipper union also fixed an issue in Arachne that in post-processing Voronoi diagram, some edges
    // didn't have twin edges. (a non-planar Voronoi diagram probably caused this).
    prepared_outline = union_(prepared_outline);

    // [INTENT] Final guard: if pre-processing consumed the entire outline (e.g., all features
    // were too thin), return empty toolpaths without running the expensive SkeletalTrapezoidation.
    if (area(prepared_outline) <= 0) {
        assert(toolpaths.empty());
        return toolpaths;
    }

    // [INTENT] Convert bead widths from coord_t (nm) to float mm using Flow::rounded_rectangle...
    // These float widths are used to compute the wall_split_middle_threshold and
    // wall_add_middle_threshold — the fractions of nozzle width at which to split/add a bead.
    // [HAZARD] The flow model (rounded rectangle cross-section) assumes a specific nozzle shape.
    // For non-standard nozzles (triangular tip, volcano, etc.), this width-to-spacing conversion
    // may be inaccurate, causing the bead count transition thresholds to fire at wrong widths.
    const float external_perimeter_extrusion_width = Flow::rounded_rectangle_extrusion_width_from_spacing(unscale<float>(bead_width_0),
                                                                                                          float(this->layer_height));
    const float perimeter_extrusion_width          = Flow::rounded_rectangle_extrusion_width_from_spacing(unscale<float>(bead_width_x),
                                                                                                          float(this->layer_height));

    const coord_t wall_transition_length = scaled<coord_t>(this->m_params.wall_transition_length);

    // [INTENT] Compute the two thresholds used by OuterWallInsetBeadingStrategy to decide when to
    // split the middle bead into two (wall_split_middle_threshold) or add a new middle bead
    // (wall_add_middle_threshold). Both are clamped to [0.01, 0.99] to avoid edge-case issues
    // at 0 or 1.
    // wall_split_middle_threshold: (2 * min_bead_width / outer_width) - 1
    //   Intuition: split the middle when each half would still be at least min_bead_width wide.
    // wall_add_middle_threshold: min_bead_width / inner_width
    //   Intuition: add a new middle bead when the gap is at least min_bead_width wide.
    const double wall_split_middle_threshold = std::clamp(2. * unscaled<double>(this->min_bead_width) / external_perimeter_extrusion_width -
                                                              1.,
                                                          0.01,
                                                          0.99); // For an uneven nr. of lines: When to split the middle wall into two.
    const double wall_add_middle_threshold =
        std::clamp(unscaled<double>(this->min_bead_width) / perimeter_extrusion_width, 0.01,
                   0.99); // For an even nr. of lines: When to add a new middle in between the innermost two walls.

    // [INTENT] max_bead_count = 2 * inset_count (capped at INT_MAX to avoid overflow).
    // Factor of 2: Arachne may internally split a nominal N-wall region into up to 2N beads
    // when using the DistributedBeadingStrategy (each wall can be split into two narrower beads).
    const int    wall_distribution_count = this->m_params.wall_distribution_count;
    const size_t max_bead_count          = (inset_count < std::numeric_limits<coord_t>::max() / 2) ? 2 * inset_count :
                                                                                                     std::numeric_limits<coord_t>::max();
    // [INTENT] BeadingStrategyFactory::makeStrategy() selects and chains the appropriate beading
    // strategies based on configuration. The resulting strategy encodes HOW bead widths are
    // distributed across the medial axis cross-section at each point. The beading_strat is a
    // unique_ptr that is alive for the duration of generate() and destroyed at function exit.
    // SkeletalTrapezoidation holds a const reference to *beading_strat — safe within this scope.
    // [COUPLING] The beading strategy chain typically includes:
    //   - LimitedBeadingStrategy (cap total beads at max_bead_count)
    //   - OuterWallInsetBeadingStrategy (apply wall_0_inset offset)
    //   - DistributedBeadingStrategy or RedistributeBeadingStrategy (distribute width changes)
    //   - WideningBeadingStrategy (widen thin features, if print_thin_walls)
    const auto beading_strat = BeadingStrategyFactory::makeStrategy(bead_width_0, bead_width_x, wall_transition_length, transitioning_angle,
                                                                    print_thin_walls, min_bead_width, min_feature_size,
                                                                    wall_split_middle_threshold, wall_add_middle_threshold, max_bead_count,
                                                                    wall_0_inset, wall_distribution_count);
    // [INTENT] transition_filter_dist = 100mm (hardcoded). This is the length along the Voronoi
    // skeleton over which transition filtering is applied. Transitions shorter than this are
    // filtered out to avoid very short sections with different wall counts.
    // [HAZARD] The transition_filter_dist is hardcoded to scaled<coord_t>(100.f) = 100mm.
    // This is unusually large (most parts are < 200mm). For small parts (< 100mm), nearly all
    // transitions may be within the filter range, potentially causing them all to be filtered.
    // The original CuraEngine value was lower; this may be an OrcaSlicer-specific change.
    const coord_t transition_filter_dist   = scaled<coord_t>(100.f);
    const coord_t allowed_filter_deviation = wall_transition_filter_deviation;
    // [INTENT] SkeletalTrapezoidation is the core Arachne algorithm:
    //   1. Computes the Voronoi diagram of the prepared_outline
    //   2. Extracts the medial axis (skeletal trapezoidation graph)
    //   3. Assigns bead counts to each edge via the beading strategy
    //   4. Generates variable-width ExtrusisonLine toolpaths
    // This is the most computationally expensive step — O(N log N) Voronoi + O(N) graph traversal.
    SkeletalTrapezoidation wall_maker(prepared_outline, *beading_strat, beading_strat->getTransitioningAngle(), discretization_step_size,
                                      transition_filter_dist, allowed_filter_deviation, wall_transition_length);
    wall_maker.generateToolpaths(toolpaths);

    stitchToolPaths(toolpaths, this->bead_width_x);

    removeSmallLines(toolpaths);

    separateOutInnerContour();

    simplifyToolPaths(toolpaths);

    removeEmptyToolPaths(toolpaths);
    // [INTENT] Debug assertion: verify that after all post-processing, toolpaths are still
    // sorted by inset_idx (outermost first). SkeletalTrapezoidation should produce them sorted,
    // and the post-processing steps should preserve this order. If sorting is violated, it
    // indicates a bug in one of the post-processing steps.
    assert(std::is_sorted(toolpaths.cbegin(), toolpaths.cend(),
                          [](const VariableWidthLines& l, const VariableWidthLines& r) {
                              return l.front().inset_idx < r.front().inset_idx;
                          }) &&
           "WallToolPaths should be sorted from the outer 0th to inner_walls");
    toolpaths_generated = true;
    return toolpaths;
}

// [INTENT] Joins open ExtrusisonLine polylines (produced by SkeletalTrapezoidation) into
// closed polygons using PolylineStitcher. Processes each inset index separately.
// stitch_distance = bead_width_x - 1 nm: maximum endpoint-to-endpoint distance to stitch.
// After PolylineStitcher, manually reconnects closed polygons whose endpoints differ by
// < stitch_distance (PolylineStitcher sometimes produces closed polygons with tiny endpoint gaps).
// [HAZARD] ARACHNE_STITCH_PATCH_DEBUG is currently commented out in the #define (line 49).
// The debug block (SVG output, BOOST_LOG_TRIVIAL error, assert) is only active when
// ARACHNE_STITCH_PATCH_DEBUG is defined. In release builds, even contour lines that fail to
// close are silently left as open polylines without any log output.
// [HAZARD] PolylineStitcher::stitch() is a greedy nearest-endpoint algorithm. For complex
// topologies (many closely-spaced polyline endpoints), it may produce suboptimal stitching
// that leaves some lines open or creates incorrect closures. The stitch_distance limit
// prevents over-stitching, but also means genuine gaps > bead_width_x will never be closed.
void WallToolPaths::stitchToolPaths(std::vector<VariableWidthLines>& toolpaths, const coord_t bead_width_x)
{
    const coord_t stitch_distance =
        bead_width_x - 1; // In 0-width contours, junctions can cause up to 1-line-width gaps. Don't stitch more than 1 line width.

    for (unsigned int wall_idx = 0; wall_idx < toolpaths.size(); wall_idx++) {
        VariableWidthLines& wall_lines = toolpaths[wall_idx];

        VariableWidthLines stitched_polylines;
        VariableWidthLines closed_polygons;
        PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::stitch(wall_lines, stitched_polylines, closed_polygons,
                                                                                       stitch_distance);
#ifdef ARACHNE_STITCH_PATCH_DEBUG
        for (const ExtrusionLine& line : stitched_polylines) {
            if (!line.is_odd && line.polylineLength() > 3 * stitch_distance && line.size() > 3) {
                BOOST_LOG_TRIVIAL(error) << "Some even contour lines could not be closed into polygons!";
                assert(false && "Some even contour lines could not be closed into polygons!");
                BoundingBox aabb;
                for (auto line2 : wall_lines)
                    for (auto j : line2)
                        aabb.merge(j.p);
                {
                    static int                 iRun = 0;
                    SVG                        svg(debug_out_path("contours_before.svg-%d.png", iRun), aabb);
                    std::array<const char*, 8> colors    = {"gray", "black", "blue", "green", "lime", "purple", "red", "yellow"};
                    size_t                     color_idx = 0;
                    for (auto& inset : toolpaths)
                        for (auto& line2 : inset) {
                            // svg.writePolyline(line2.toPolygon(), col);

                            Polygon poly = line2.toPolygon();
                            Point   last = poly.front();
                            for (size_t idx = 1; idx < poly.size(); idx++) {
                                Point here = poly[idx];
                                svg.draw(Line(last, here), colors[color_idx]);
                                //                                svg.draw_text((last + here) / 2,
                                //                                std::to_string(line2.junctions[idx].region_id).c_str(), "black");
                                last = here;
                            }
                            svg.draw(poly[0], colors[color_idx]);
                            // svg.nextLayer();
                            // svg.writePoints(poly, true, 0.1);
                            // svg.nextLayer();
                            color_idx = (color_idx + 1) % colors.size();
                        }
                }
                {
                    static int iRun = 0;
                    SVG        svg(debug_out_path("contours-%d.svg", iRun), aabb);
                    for (auto& inset : toolpaths)
                        for (auto& line2 : inset)
                            svg.draw_outline(line2.toPolygon(), "gray");
                    for (auto& line2 : stitched_polylines) {
                        const char* col = line2.is_odd ? "gray" : "red";
                        if (!line2.is_odd)
                            std::cerr << "Non-closed even wall of size: " << line2.size() << " at " << line2.front().p << "\n";
                        if (!line2.is_odd)
                            svg.draw(line2.front().p);
                        Polygon poly = line2.toPolygon();
                        Point   last = poly.front();
                        for (size_t idx = 1; idx < poly.size(); idx++) {
                            Point here = poly[idx];
                            svg.draw(Line(last, here), col);
                            last = here;
                        }
                    }
                    for (auto line2 : closed_polygons)
                        svg.draw(line2.toPolygon());
                }
            }
        }
#endif                                   // ARACHNE_STITCH_PATCH_DEBUG
        wall_lines = stitched_polylines; // replace input toolpaths with stitched polylines

        for (ExtrusionLine& wall_polygon : closed_polygons) {
            if (wall_polygon.junctions.empty()) {
                continue;
            }

            // PolylineStitcher, in some cases, produced closed extrusion (polygons),
            // but the endpoints differ by a small distance. So we reconnect them.
            // FIXME Lukas H.: Investigate more deeply why it is happening.
            if (wall_polygon.junctions.front().p != wall_polygon.junctions.back().p &&
                (wall_polygon.junctions.back().p - wall_polygon.junctions.front().p).cast<double>().norm() < stitch_distance) {
                wall_polygon.junctions.emplace_back(wall_polygon.junctions.front());
            }
            wall_polygon.is_closed = true;
            wall_lines.emplace_back(std::move(wall_polygon)); // add stitched polygons to result
        }
#ifdef DEBUG
        for (ExtrusionLine& line : wall_lines) {
            assert(line.inset_idx == wall_idx);
        }
#endif // DEBUG
    }
}

template<typename T> bool shorterThan(const T& shape, const coord_t check_length)
{
    const auto* p0     = &shape.back();
    int64_t     length = 0;
    for (const auto& p1 : shape) {
        length += (*p0 - p1).template cast<int64_t>().norm();
        if (length >= check_length)
            return false;
        p0 = &p1;
    }
    return true;
}

// [INTENT] Removes odd (transition/fill-gap) lines that are too short to meaningfully print.
// "Odd" lines are single-bead fill lines between two wall loops. Their minimum width is taken
// as the minimum junction width along the line. The removal threshold varies:
//   - top/bottom layers: lines shorter than min_width/2 are removed (aggressive — prevents surface gaps)
//   - other layers: lines shorter than min_width * min_length_factor are removed (configurable)
// Uses move-from-back + erase + decrement to avoid O(n²) removal in large insets.
// [HAZARD] Only odd, non-closed lines are filtered. Even (primary perimeter) lines and closed
// odd lines (tiny closed fill loops) are never removed here regardless of length.
// A very short primary perimeter will survive and produce a single-move extrusion in G-code.
// [HAZARD] min_length_factor default is 0.5f but can be configured via PrintObjectConfig.
// A factor of 0 would disable all filtering; a factor of 1 would remove lines shorter than
// their own width (overly aggressive for thin-walled sections).
void WallToolPaths::removeSmallLines(std::vector<VariableWidthLines>& toolpaths)
{
    for (VariableWidthLines& inset : toolpaths) {
        for (size_t line_idx = 0; line_idx < inset.size(); line_idx++) {
            ExtrusionLine& line      = inset[line_idx];
            coord_t        min_width = std::numeric_limits<coord_t>::max();
            for (const ExtrusionJunction& j : line)
                min_width = std::min(min_width, j.w);
            // Only use min_length_factor for non-topmost, to prevent top gaps. Otherwise use default value.
            if (line.is_odd && !line.is_closed &&
                shorterThan(line,
                            m_params.is_top_or_bottom_layer ? (min_width / 2) : (min_width * m_params.min_length_factor))) { // remove line
                line = std::move(inset.back());
                inset.erase(--inset.end());
                line_idx--; // reconsider the current position
            }
        }
    }
}

// [INTENT] Simplifies each ExtrusisonLine's junction list by removing junctions that
// contribute less than maximum_extrusion_area_deviation to the extrusion cross-section area.
// Uses squared thresholds internally for efficient distance comparison.
// [HAZARD] maximum_resolution and maximum_deviation are global constants (0.5mm, 0.025mm),
// not per-object configurable. For very small or very large objects, these fixed thresholds
// may over-simplify or under-simplify the toolpaths. A future refactor might expose these
// as configurable parameters derived from nozzle diameter or part size.
void WallToolPaths::simplifyToolPaths(std::vector<VariableWidthLines>& toolpaths)
{
    for (size_t toolpaths_idx = 0; toolpaths_idx < toolpaths.size(); ++toolpaths_idx) {
        const int64_t maximum_resolution               = Slic3r::Arachne::meshfix_maximum_resolution();
        const int64_t maximum_deviation                = Slic3r::Arachne::meshfix_maximum_deviation();
        const int64_t maximum_extrusion_area_deviation = Slic3r::Arachne::meshfix_maximum_extrusion_area_deviation(); // unit: μm²
        for (auto& line : toolpaths[toolpaths_idx]) {
            line.simplify(maximum_resolution * maximum_resolution, maximum_deviation * maximum_deviation, maximum_extrusion_area_deviation);
        }
    }
}

const std::vector<VariableWidthLines>& WallToolPaths::getToolPaths()
{
    if (!toolpaths_generated)
        return generate();
    return toolpaths;
}

// [INTENT] Partitions the toolpaths vector into:
//   - actual_toolpaths: insets with non-zero extrusion widths (will be printed)
//   - inner_contour: insets with all-zero widths (boundary markers between walls and infill)
// An "inset" is classified as a contour if the first junction of the first line has w==0.
// After partitioning, inner_contour is unioned with Clipper (pftEvenOdd) to normalize
// winding order (SkeletalTrapezoidation doesn't guarantee CW/CCW consistency).
// [HAZARD] The is_contour check (lines 878-884) only examines the FIRST junction of the FIRST
// ExtrusisonLine in the inset. It breaks out of both loops immediately. If an inset has
// mixed zero/non-zero width lines (which should not happen in practice), only the first
// junction's width determines classification. Any mixed inset would be entirely moved to
// either actual_toolpaths or inner_contour — never split.
// [HAZARD] contour_paths is allocated but never populated or used (only inner_contour is).
// This appears to be dead code left from a refactor. A refactored implementation can remove it.
void WallToolPaths::separateOutInnerContour()
{
    // We'll remove all 0-width paths from the original toolpaths and store them separately as polygons.
    std::vector<VariableWidthLines> actual_toolpaths;
    actual_toolpaths.reserve(toolpaths.size()); // A bit too much, but the correct order of magnitude.
    std::vector<VariableWidthLines> contour_paths;
    contour_paths.reserve(toolpaths.size() / inset_count);
    inner_contour.clear();
    for (const VariableWidthLines& inset : toolpaths) {
        if (inset.empty())
            continue;
        bool is_contour = false;
        for (const ExtrusionLine& line : inset) {
            for (const ExtrusionJunction& j : line) {
                if (j.w == 0)
                    is_contour = true;
                else
                    is_contour = false;
                break;
            }
        }

        if (is_contour) {
#ifdef DEBUG
            for (const ExtrusionLine& line : inset)
                for (const ExtrusionJunction& j : line)
                    assert(j.w == 0);
#endif // DEBUG
            for (const ExtrusionLine& line : inset) {
                if (line.is_odd)
                    continue;            // odd lines don't contribute to the contour
                else if (line.is_closed) // sometimes an very small even polygonal wall is not stitched into a polygon
                    inner_contour.emplace_back(line.toPolygon());
            }
        } else {
            actual_toolpaths.emplace_back(inset);
        }
    }
    if (!actual_toolpaths.empty())
        toolpaths = std::move(actual_toolpaths); // Filtered out the 0-width paths.
    else
        toolpaths.clear();

    // The output walls from the skeletal trapezoidation have no known winding order, especially if they are joined together from polylines.
    // They can be in any direction, clockwise or counter-clockwise, regardless of whether the shapes are positive or negative.
    // To get a correct shape, we need to make the outside contour positive and any holes inside negative.
    // This can be done by applying the even-odd rule to the shape. This rule is not sensitive to the winding order of the polygon.
    // The even-odd rule would be incorrect if the polygon self-intersects, but that should never be generated by the skeletal trapezoidation.
    inner_contour = union_(inner_contour, ClipperLib::PolyFillType::pftEvenOdd);
}

const Polygons& WallToolPaths::getInnerContour()
{
    if (!toolpaths_generated && inset_count > 0) {
        generate();
    } else if (inset_count == 0) {
        return outline;
    }
    return inner_contour;
}

bool WallToolPaths::removeEmptyToolPaths(std::vector<VariableWidthLines>& toolpaths)
{
    toolpaths.erase(std::remove_if(toolpaths.begin(), toolpaths.end(), [](const VariableWidthLines& lines) { return lines.empty(); }),
                    toolpaths.end());
    return toolpaths.empty();
}

/*!
 * Get the order constraints of the insets when printing walls per region / hole.
 * Each returned pair consists of adjacent wall lines where the left has an inset_idx one lower than the right.
 *
 * Odd walls should always go after their enclosing wall polygons.
 *
 * \param outer_to_inner Whether the wall polygons with a lower inset_idx should go before those with a higher one.
 */
WallToolPaths::ExtrusionLineSet WallToolPaths::getRegionOrder(const std::vector<ExtrusionLine*>& input, const bool outer_to_inner)
{
    ExtrusionLineSet order_requirements;
    // We build a grid where we map toolpath vertex locations to toolpaths,
    // so that we can easily find which two toolpaths are next to each other,
    // which is the requirement for there to be an order constraint.
    //
    // We use a PointGrid rather than a LineGrid to save on computation time.
    // In very rare cases two insets might lie next to each other without having neighboring vertices, e.g.
    //  \            .
    //   |  /        .
    //   | /         .
    //   ||          .
    //   | \         .
    //   |  \        .
    //  /            .
    // However, because of how Arachne works this will likely never be the case for two consecutive insets.
    // On the other hand one could imagine that two consecutive insets of a very large circle
    // could be simplify()ed such that the remaining vertices of the two insets don't align.
    // In those cases the order requirement is not captured,
    // which means that the PathOrderOptimizer *might* result in a violation of the user set path order.
    // This problem is expected to be not so severe and happen very sparsely.

    coord_t max_line_w = 0u;
    for (const ExtrusionLine* line : input) // compute max_line_w
        for (const ExtrusionJunction& junction : *line)
            max_line_w = std::max(max_line_w, junction.w);
    if (max_line_w == 0u)
        return order_requirements;

    struct LineLoc
    {
        ExtrusionJunction    j;
        const ExtrusionLine* line;
    };
    struct Locator
    {
        Point operator()(const LineLoc& elem) { return elem.j.p; }
    };

    // How much farther two verts may be apart due to corners.
    // This distance must be smaller than 2, because otherwise
    // we could create an order requirement between e.g.
    // wall 2 of one region and wall 3 of another region,
    // while another wall 3 of the first region would lie in between those two walls.
    // However, higher values are better against the limitations of using a PointGrid rather than a LineGrid.
    constexpr float diagonal_extension = 1.9f;
    const auto      searching_radius   = coord_t(max_line_w * diagonal_extension);
    using GridT                        = SparsePointGrid<LineLoc, Locator>;
    GridT grid(searching_radius);

    for (const ExtrusionLine* line : input)
        for (const ExtrusionJunction& junction : *line)
            grid.insert(LineLoc{junction, line});
    for (const std::pair<const SquareGrid::GridPoint, LineLoc>& pair : grid) {
        const LineLoc&       lineloc_here = pair.second;
        const ExtrusionLine* here         = lineloc_here.line;
        Point                loc_here     = pair.second.j.p;
        std::vector<LineLoc> nearby_verts = grid.getNearby(loc_here, searching_radius);
        for (const LineLoc& lineloc_nearby : nearby_verts) {
            const ExtrusionLine* nearby = lineloc_nearby.line;
            if (nearby == here)
                continue;
            if (nearby->inset_idx == here->inset_idx)
                continue;
            if (nearby->inset_idx > here->inset_idx + 1)
                continue; // not directly adjacent
            if (here->inset_idx > nearby->inset_idx + 1)
                continue; // not directly adjacent
            if (!shorter_then(loc_here - lineloc_nearby.j.p, (lineloc_here.j.w + lineloc_nearby.j.w) / 2 * diagonal_extension))
                continue; // points are too far away from each other
            if (here->is_odd || nearby->is_odd) {
                if (here->is_odd && !nearby->is_odd && nearby->inset_idx < here->inset_idx)
                    order_requirements.emplace(std::make_pair(nearby, here));
                if (nearby->is_odd && !here->is_odd && here->inset_idx < nearby->inset_idx)
                    order_requirements.emplace(std::make_pair(here, nearby));
            } else if ((nearby->inset_idx < here->inset_idx) == outer_to_inner) {
                order_requirements.emplace(std::make_pair(nearby, here));
            } else {
                assert((nearby->inset_idx > here->inset_idx) == outer_to_inner);
                order_requirements.emplace(std::make_pair(here, nearby));
            }
        }
    }
    return order_requirements;
}

} // namespace Slic3r::Arachne
