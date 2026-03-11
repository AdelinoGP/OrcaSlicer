#ifndef slic3r_ExtrusionProcessor_hpp_
#define slic3r_ExtrusionProcessor_hpp_

// This algorithm is copied from PrusaSlicer, original author is Pavel Mikus(pavel.mikus.mail@seznam.cz)
//
// [INTENT] ExtrusionProcessor.hpp packages the BBL overhang-quality estimator used by
// GCode generation to turn a geometric extrusion path into per-point slowdown and overlap
// hints. The output is not G-code directly; it is an intermediate "processed polyline"
// consumed by GCode.cpp when emitting segmented perimeter moves.
//
// [STATE] Runtime state lives in ExtrusionQualityEstimator caches keyed by PrintObject*.
// Each layer handoff updates previous/next boundary and curled-line distance fields so the
// current perimeter can query support from already printed geometry.
//
// [MEMORY] The estimator owns only its AABBTreeLines caches. PrintObject* / Layer* inputs are
// borrowed references to longer-lived print graph objects; no transfer of ownership occurs.
//
// [CONCURRENCY] No internal locking. The estimator is mutated as GCode.cpp walks layers in print
// order, so ports should keep it thread-confined or add explicit synchronization.
//
// [COUPLING] This header sits at the seam between support analysis and G-code emission:
//   - SupportSpotsGenerator populates Layer::curled_lines.
//   - Layer owns lslices / curled_lines for each Z slice.
//   - GCode.cpp calls prepare_for_new_layer(), set_current_object(), then
//     estimate_extrusion_quality() while segmenting external perimeters.
//
// [HAZARD] The estimator assumes the caller obeys that ordering contract. current_object is
// not initialized here, and operator[] lookups on the boundary maps silently create empty
// distancers if prepare_for_new_layer() was skipped. A port should make this dependency
// explicit instead of relying on call-order discipline.

#include "../AABBTreeLines.hpp"
//#include "../SupportSpotsGenerator.hpp"
#include "../libslic3r.h"
#include "../ExtrusionEntity.hpp"
#include "../Layer.hpp"
#include "../Point.hpp"
#include "../SVG.hpp"
#include "../BoundingBox.hpp"
#include "../Polygon.hpp"
#include "../ClipperUtils.hpp"
#include "../Flow.hpp"
#include "../Config.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {

struct ExtendedPoint
{
    // [STATE] position is always unscaled millimeter-space even when the input polyline is in
    // scaled coordinates; estimate_points_properties() normalizes the representation so later
    // distance / curvature logic can work in physical units.
    Vec2d position;
    // [STATE] distance stores signed or unsigned overhang distance to the previous layer's
    // supporting boundary, optionally biased by half a flow width to treat line centerlines as
    // nozzle-width ribbons instead of mathematical curves.
    float distance;
    // [STATE] curvature stores the maximum angle-per-mm observed across several sliding windows.
    // GCode.cpp can then detect sharp unsupported turns without re-walking the polyline.
    float curvature;
};

template<bool SCALED_INPUT, bool ADD_INTERSECTIONS, bool PREV_LAYER_BOUNDARY_OFFSET, bool SIGNED_DISTANCE, typename POINTS, typename L>
std::vector<ExtendedPoint> estimate_points_properties(const POINTS                           &input_points,
                                                      const AABBTreeLines::LinesDistancer<L> &unscaled_prev_layer,
                                                      float                                   flow_width,
                                                      float                                   max_line_length = -1.0f,
                                                      float                                   min_distance = -1.0f)
{
    // [INTENT] Sample a polyline against a previous-layer distance field and enrich each point
    // with support distance / curvature. Optional intersection insertion and segment splitting
    // prevent long sparse segments from hiding a short unsupported span that should trigger a
    // slowdown.
    //
    // [COUPLING] unscaled_prev_layer is typically built from Layer::lslices or Layer::curled_lines
    // through AABBTreeLines::LinesDistancer. This makes the algorithm generic over both geometric
    // support boundaries and "curled danger" line segments.
    //
    // [HAZARD] Template booleans encode behavior at compile time instead of via a runtime config.
    // Translators need to preserve the exact mode combinations used at call sites, otherwise the
    // resulting distance semantics will drift subtly.
    bool   looped     = input_points.front() == input_points.back();
    std::function<size_t(size_t,size_t)> get_prev_index = [](size_t idx, size_t count) {
        if (idx > 0) {
            return idx - 1;
        } else
            return idx;
    };
    if (looped) {
        get_prev_index = [](size_t idx, size_t count) {
            if (idx == 0)
                idx = count;
            return --idx;
        };
    };
    std::function<size_t(size_t,size_t)> get_next_index = [](size_t idx, size_t size) {
        if (idx + 1 < size) {
            return idx + 1;
        } else
            return idx;
    };
    if (looped) {
        get_next_index = [](size_t idx, size_t count) {
            if (++idx == count)
                idx = 0;
            return idx;
        };
    };

    using P = typename POINTS::value_type;
    // [INTENT] Newly synthesized split/intersection points are filtered by a nozzle-relative
    // spacing threshold so speed changes remain physically meaningful instead of creating a
    // dense cloud of almost coincident G-code vertices.
    double min_spacing = flow_width*0.25;

    using AABBScalar = typename AABBTreeLines::LinesDistancer<L>::Scalar;
    if (input_points.empty())
        return {};
    float boundary_offset = PREV_LAYER_BOUNDARY_OFFSET ? 0.5 * flow_width : 0.0f;
    auto  maybe_unscale   = [](const P &p) { return SCALED_INPUT ? unscaled(p) : p.template cast<double>(); };

    std::vector<ExtendedPoint> points;
    points.reserve(input_points.size() * (ADD_INTERSECTIONS ? 1.5 : 1));

    {
        ExtendedPoint start_point{maybe_unscale(input_points.front())};
        auto [distance, nearest_line,
              x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(start_point.position.cast<AABBScalar>());
        start_point.distance = distance + boundary_offset;
        points.push_back(start_point);
    }
    for (size_t i = 1; i < input_points.size(); i++) {
        ExtendedPoint next_point{maybe_unscale(input_points[i])};
        auto [distance, nearest_line,
              x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(next_point.position.cast<AABBScalar>());
        next_point.distance = distance + boundary_offset;

        // Intersection handling
        if (ADD_INTERSECTIONS &&
            ((points.back().distance > boundary_offset + EPSILON) != (next_point.distance > boundary_offset + EPSILON))) {
            const ExtendedPoint &prev_point    = points.back();
            auto                 intersections = unscaled_prev_layer.template intersections_with_line<true>(
                L{prev_point.position.cast<AABBScalar>(), next_point.position.cast<AABBScalar>()});
            for (const auto &intersection : intersections) {
                ExtendedPoint p{};
                p.position = intersection.first.template cast<double>();
                p.distance = boundary_offset;
                // ORCA: Filter out points that are introduced at intersections if their distance from the previous or next point is not meaningful
                if ((p.position - prev_point.position).norm() > min_spacing &&
                    (next_point.position - p.position).norm() > min_spacing) {
                    points.push_back(p);
                }
            }
        }
        points.push_back(next_point);
    }

    // [INTENT] When slowdown is enabled, split segments near the support/no-support threshold so
    // the downstream speed interpolator can localize the slowdown instead of penalizing the whole
    // edge with one coarse sample.
    if (PREV_LAYER_BOUNDARY_OFFSET && ADD_INTERSECTIONS) {
        std::vector<ExtendedPoint> new_points;
        new_points.reserve(points.size() * 2);
        new_points.push_back(points.front());
        for (int point_idx = 0; point_idx < int(points.size()) - 1; ++point_idx) {
            const ExtendedPoint &curr = points[point_idx];
            const ExtendedPoint &next = points[point_idx + 1];

            if ((curr.distance > -boundary_offset && curr.distance < boundary_offset + 2.0f) ||
                (next.distance > -boundary_offset && next.distance < boundary_offset + 2.0f)) {
                double line_len = (next.position - curr.position).norm();
                
                // ORCA: Segment path to smaller lines by adding additional points only if the path has an overhang that
                // will trigger a slowdown and the path is also reasonably large, i.e. 2mm in length or more
                // If there is no overhang in the start/end point, dont segment it.
                // Ignore this check if the control of segmentation for overhangs is disabled (min_distance=-1)
                if ((min_distance > 0 && ((std::abs(curr.distance) > min_distance) || (std::abs(next.distance) > min_distance)) && line_len >= 2.f) ||
                    (min_distance <= 0 && line_len > 4.0f)) {
                    double a0 = std::clamp((curr.distance + 3 * boundary_offset) / line_len, 0.0, 1.0);
                    double a1 = std::clamp(1.0f - (next.distance + 3 * boundary_offset) / line_len, 0.0, 1.0);
                    double t0 = std::min(a0, a1);
                    double t1 = std::max(a0, a1);

                    if (t0 < 1.0) {
                        Vec2d p0     = curr.position + t0 * (next.position - curr.position);
                        auto [p0_dist, p0_near_l,
                              p0_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(p0.cast<AABBScalar>());
                        ExtendedPoint new_p{};
                        new_p.position = p0;
                        new_p.distance = float(p0_dist + boundary_offset);
                        // ORCA: only create a new point in the path if the new point overhang distance will be used to generate a speed change
                        // or if this option is disabled (min_distance<=0)
                        if( (std::abs(p0_dist) > min_distance) || (min_distance<=0)){
                            // ORCA: also filter out points that are introduced to the start of the path when their distance from the start point is
                            // not meaningful
                            if ((p0 - curr.position).norm() > min_spacing && (next.position - p0).norm() > min_spacing) {
                                new_points.push_back(new_p);
                            }
                        }
                    }
                    if (t1 > 0.0) {
                        Vec2d p1     = curr.position + t1 * (next.position - curr.position);
                        auto [p1_dist, p1_near_l,
                              p1_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(p1.cast<AABBScalar>());
                        ExtendedPoint new_p{};
                        new_p.position = p1;
                        new_p.distance = float(p1_dist + boundary_offset);
                        // ORCA: only create a new point in the path if the new point overhang distance will be used to generate a speed change
                        // or if this option is disabled (min_distance<=0)
                        if( (std::abs(p1_dist) > min_distance) || (min_distance<=0)){
                            // ORCA: filter out points that are introduced to the end of the path when their distance from the end point is
                            // not meaningful
                            if ((p1 - curr.position).norm() > min_spacing && (next.position - p1).norm() > min_spacing) {
                                new_points.push_back(new_p);
                            }
                        }
                    }
                }
            }
            new_points.push_back(next);
        }
        points = std::move(new_points);
    }

    // [INTENT] Optional max-length resampling caps the physical distance represented by any
    // single sample pair. This stabilizes curvature / distance interpolation on long straight
    // spans where only the endpoints would otherwise be measured.
    if (max_line_length > 0) {
        std::vector<ExtendedPoint> new_points;
        new_points.reserve(points.size() * 2);
        {
            for (size_t i = 0; i + 1 < points.size(); i++) {
                const ExtendedPoint &curr = points[i];
                const ExtendedPoint &next = points[i + 1];
                new_points.push_back(curr);
                double len             = (next.position - curr.position).squaredNorm();
                double t               = sqrt((max_line_length * max_line_length) / len);
                size_t new_point_count = 1.0 / t;
                for (size_t j = 1; j < new_point_count + 1; j++) {
                    Vec2d pos  = curr.position * (1.0 - j * t) + next.position * (j * t);
                    auto [p_dist, p_near_l,
                          p_x] = unscaled_prev_layer.template distance_from_lines_extra<SIGNED_DISTANCE>(pos.cast<AABBScalar>());
                    ExtendedPoint new_p{};
                    new_p.position = pos;
                    new_p.distance = float(p_dist + boundary_offset);
                    
                    // ORCA: Filter out points that are introduced if their distance from the previous or next point is not meaningful
                    if ((pos - curr.position).norm() > min_spacing && (next.position - pos).norm() > min_spacing) {
                        new_points.push_back(new_p);
                    }
                }
            }
            new_points.push_back(points.back());
        }
        points = std::move(new_points);
    }

    // [INTENT] Estimate curvature by comparing backward/forward chords across several windows
    // (3, 9, 16 mm). Taking the maximum normalized angle highlights both tight local corners and
    // broader unsupported bends that may curl even if any single segment looks shallow.
    float accumulated_distance = 0;
    std::vector<float> distances_for_curvature(points.size());
    for (size_t point_idx = 0; point_idx < points.size(); ++point_idx) {
        const ExtendedPoint &a = points[point_idx];
        const ExtendedPoint &b = points[get_prev_index(point_idx, points.size())];

        distances_for_curvature[point_idx] = (b.position - a.position).norm();
        accumulated_distance += distances_for_curvature[point_idx];
    }

    if (accumulated_distance > EPSILON)
        for (float window_size : {3.0f, 9.0f, 16.0f}) {
            for (int point_idx = 0; point_idx < int(points.size()); ++point_idx) {
                ExtendedPoint &current = points[point_idx];

                Vec2d back_position = current.position;
                {
                    size_t back_point_index = point_idx;
                    float  dist_backwards   = 0;
                    while (dist_backwards < window_size * 0.5 && back_point_index != get_prev_index(back_point_index, points.size())) {
                        float line_dist = distances_for_curvature[get_prev_index(back_point_index, points.size())];
                        if (dist_backwards + line_dist > window_size * 0.5) {
                            back_position = points[back_point_index].position +
                                            (window_size * 0.5 - dist_backwards) *
                                                (points[get_prev_index(back_point_index, points.size())].position -
                                                 points[back_point_index].position)
                                                    .normalized();
                            dist_backwards += window_size * 0.5 - dist_backwards + EPSILON;
                        } else {
                            dist_backwards += line_dist;
                            back_point_index = get_prev_index(back_point_index, points.size());
                        }
                    }
                }

                Vec2d front_position = current.position;
                {
                    size_t front_point_index = point_idx;
                    float  dist_forwards     = 0;
                    while (dist_forwards < window_size * 0.5 && front_point_index != get_next_index(front_point_index, points.size())) {
                        float line_dist = distances_for_curvature[front_point_index];
                        if (dist_forwards + line_dist > window_size * 0.5) {
                            front_position = points[front_point_index].position +
                                             (window_size * 0.5 - dist_forwards) *
                                                 (points[get_next_index(front_point_index, points.size())].position -
                                                  points[front_point_index].position)
                                                     .normalized();
                            dist_forwards += window_size * 0.5 - dist_forwards + EPSILON;
                        } else {
                            dist_forwards += line_dist;
                            front_point_index = get_next_index(front_point_index, points.size());
                        }
                    }
                }

                float new_curvature = angle(current.position - back_position, front_position - current.position) / window_size;
                if (abs(current.curvature) < abs(new_curvature)) {
                    current.curvature = new_curvature;
                }
            }
        }

    return points;
}

struct ProcessedPoint
{
    // [STATE] p is kept in scaled integer space because GCode.cpp emits motion commands using the
    // same coordinate representation as the rest of libslic3r extrusion paths.
    Point p;
    // [STATE] speed is an absolute mm/s target after overhang and curled-edge slowdown.
    float speed = 1.0f;
    // [STATE] overlap encodes how much of the nozzle width is considered supported at this point.
    // Downstream callers use it to adjust flow / visualization without recomputing distances.
    float overlap = 1.0f;
};

class ExtrusionQualityEstimator
{
    // [STATE] Per-object distance fields for the previous and next printed layer boundaries.
    // prepare_for_new_layer() shifts "next" into "prev" as the G-code pipeline advances through Z.
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<Linef>> prev_layer_boundaries;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<Linef>> next_layer_boundaries;
    // [STATE] Parallel cache built from Layer::curled_lines. This is the BBL extension that lets
    // normal overhang slowdown react to already-curled material, not just nominal layer overlap.
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<CurledLine>> prev_curled_extrusions;
    std::unordered_map<const PrintObject *, AABBTreeLines::LinesDistancer<CurledLine>> next_curled_extrusions;
    // [STATE] Active object selected by GCode.cpp before each object's toolpaths are emitted.
    // Non-owning raw pointer into Print / PrintObject lifetime.
    const PrintObject                                                            *current_object;

public:
    // [STATE] Setter-only API because the estimator is reused across many layers / objects during
    // a single export. The current object is external scheduler state, not intrinsic estimator data.
    void set_current_object(const PrintObject *object) { current_object = object; }

    void prepare_for_new_layer(const PrintObject * obj, const Layer *layer)
    {
        // [INTENT] Advance the per-object rolling window of support geometry so overhang distances
        // for layer N are measured against the already printed layer N-1 and its curled segments.
        // The next_* caches are populated first because GCode generation prefetches the upcoming
        // layer before emitting paths that depend on it.
        // [COUPLING] Reads Layer::lslices and Layer::curled_lines, which are produced earlier by
        // slicing and SupportSpotsGenerator. This is where the support-analysis pipeline hands off
        // its results to G-code motion planning.
        if (layer == nullptr) return;
        const PrintObject *object = obj;
        prev_layer_boundaries[object] = next_layer_boundaries[object];
        next_layer_boundaries[object] = AABBTreeLines::LinesDistancer<Linef>{to_unscaled_linesf(layer->lslices)};
        prev_curled_extrusions[object] = next_curled_extrusions[object];
        next_curled_extrusions[object] = AABBTreeLines::LinesDistancer<CurledLine>{layer->curled_lines};
    }

    std::vector<ProcessedPoint> estimate_extrusion_quality(const ExtrusionPath                &path,
                                                           const ConfigOptionPercents         &overlaps,
                                                           const ConfigOptionFloatsOrPercents &speeds,
                                                           float                               ext_perimeter_speed,
                                                           float                               original_speed,
                                                           bool								   slowdown_for_curled_edges)
    {
        // [INTENT] Convert user-configured overhang bands into a monotone distance->speed curve,
        // then sample the extrusion path against previous-layer support and optionally nearby
        // curled material. The minimum permitted speed of each segment endpoint becomes the emitted
        // per-point speed profile.
        //
        // [COUPLING] overlaps / speeds are the PrintConfig overhang tables already normalized by
        // GCode.cpp. This method assumes they describe the same number of bands and truncates to the
        // shorter one instead of validating config shape itself.
        size_t                               speed_sections_count = std::min(overlaps.values.size(), speeds.values.size());
        std::vector<std::pair<float, float>> speed_sections;
        
        
        
        for (size_t i = 0; i < speed_sections_count; i++) {
            float distance = path.width * (1.0 - (overlaps.get_at(i) / 100.0));
            float speed    = speeds.get_at(i).percent ? (ext_perimeter_speed * speeds.get_at(i).value / 100.0) : speeds.get_at(i).value;
            speed_sections.push_back({distance, speed});
        }
        std::sort(speed_sections.begin(), speed_sections.end(),
                  [](const std::pair<float, float> &a, const std::pair<float, float> &b) { 
                    if (a.first == b.first) {
                        return a.second > b.second;
                    }
                    return a.first < b.first; });

        std::pair<float, float> last_section{INFINITY, 0};
        for (auto &section : speed_sections) {
            if (section.first == last_section.first) {
                section.second = last_section.second;
            } else {
                last_section = section;
            }
        }
        
        // [INTENT] smallest_distance_with_lower_speed gates segment splitting. If the profile never
        // slows below the original move speed, there is no benefit to inserting extra breakpoints.
        float smallest_distance_with_lower_speed = std::numeric_limits<float>::infinity(); // Initialize to a large value
        bool found = false;
        for (const auto& section : speed_sections) {
            if (section.second <= original_speed) {
                if (section.first < smallest_distance_with_lower_speed) {
                    smallest_distance_with_lower_speed = section.first;
                    found = true;
                }
            }
        }

        // [HAZARD] A missing slowdown band is encoded as -1 instead of an optional value. Several
        // branches in estimate_points_properties() treat any negative threshold as "feature off".
        // Preserve that sentinel if this logic is ported verbatim.
        if (!found)
            smallest_distance_with_lower_speed=-1.f;

        // Orca: Pass to the point properties estimator the smallest ovehang distance that triggers a slowdown (smallest_distance_with_lower_speed)
        std::vector<ExtendedPoint> extended_points = estimate_points_properties<true, true, true, true>
                                                                (path.polyline.points,
                                                                 prev_layer_boundaries[current_object],
                                                                 path.width,
                                                                 -1,
                                                                 smallest_distance_with_lower_speed);
        const auto width_inv = 1.0f / path.width;
        std::vector<ProcessedPoint> processed_points;
        processed_points.reserve(extended_points.size());
        for (size_t i = 0; i < extended_points.size(); i++) {
            const ExtendedPoint &curr = extended_points[i];
            const ExtendedPoint &next = extended_points[i + 1 < extended_points.size() ? i + 1 : i];
            
            float artificial_distance_to_curled_lines = 0.0;
            if(slowdown_for_curled_edges) {
            	// [INTENT] Reuse the same overhang speed curve for curled-filament avoidance by
            	// synthesizing an extra "unsupported distance" contribution near previous curled
            	// segments. This keeps printer tuning in one table instead of adding a separate
            	// curled-speed profile.
            	const double dist_limit = 10.0 * path.width;
				{
				Vec2d middle = 0.5 * (curr.position + next.position);
				auto line_indices = prev_curled_extrusions[current_object].all_lines_in_radius(Point::new_scale(middle), scale_(dist_limit));
					if (!line_indices.empty()) {
						double len   = (next.position - curr.position).norm();
						// [INTENT] Long segments are only penalized when a substantial fraction of their
						// span overlaps the curled-line influence box; otherwise a tiny defect near the
						// midpoint would incorrectly slow the whole move.
						// For long lines, there is a problem with the additional slowdown. If by accident, there is small curled line near the middle of this long line
                    	//  The whole segment gets slower unnecesarily. For these long lines, we do additional check whether it is worth slowing down.
                    	// NOTE that this is still quite rough approximation, e.g. we are still checking lines only near the middle point
                    	// TODO maybe split the lines into smaller segments before running this alg? but can be demanding, and GCode will be huge
                    	// [HAZARD] The midpoint-only probe is an acknowledged approximation. Translators should not simplify it away assuming
                    	// exact geometry, but they may want a more principled segment subdivision strategy if output size permits.
                    	if (len > 2) {
                        	Vec2d dir   = Vec2d(next.position - curr.position) / len;
                        	Vec2d right = Vec2d(-dir.y(), dir.x());

                        	Polygon box_of_influence = {
                            	scaled(Vec2d(curr.position + right * dist_limit)),
                            	scaled(Vec2d(next.position + right * dist_limit)),
                            	scaled(Vec2d(next.position - right * dist_limit)),
                            	scaled(Vec2d(curr.position - right * dist_limit)),
                        	};

                        	double projected_lengths_sum = 0;
                        	for (size_t idx : line_indices) {
                            	const CurledLine &line   = prev_curled_extrusions[current_object].get_line(idx);
                            	Lines             inside = intersection_ln({{line.a, line.b}}, {box_of_influence});
                            	if (inside.empty())
                                	continue;
                            	double projected_length = abs(dir.dot(unscaled(Vec2d((inside.back().b - inside.back().a).cast<double>()))));
                            	projected_lengths_sum += projected_length;
                        	}
                        	if (projected_lengths_sum < 0.4 * len) {
                            	line_indices.clear();
                        	}
                    	}
                    
                    	for (size_t idx : line_indices) {
                        	const CurledLine &line                 = prev_curled_extrusions[current_object].get_line(idx);
                        	float             distance_from_curled = unscaled(line_alg::distance_to(line, Point::new_scale(middle)));
                        	float             dist                 = path.width * (1.0 - (distance_from_curled / dist_limit)) *
                                     (1.0 - (distance_from_curled / dist_limit)) *
                                     (line.curled_height / (path.height * 10.0f)); // max_curled_height_factor from SupportSpotGenerator
                        	artificial_distance_to_curled_lines = std::max(artificial_distance_to_curled_lines, dist);
                    	}
					}
				}
			}	

            auto calculate_speed = [&speed_sections, &original_speed](float distance) {
                // [INTENT] Piecewise-linear interpolation between configured overhang bands avoids
                // step changes in feedrate at band boundaries, reducing abrupt acceleration changes.
                float final_speed;
                if (distance <= speed_sections.front().first) {
                    final_speed = original_speed;
                } else if (distance >= speed_sections.back().first) {
                    final_speed = speed_sections.back().second;
                } else {
                    size_t section_idx = 0;
                    while (distance > speed_sections[section_idx + 1].first) {
                        section_idx++;
                    }
                    float t = (distance - speed_sections[section_idx].first) /
                              (speed_sections[section_idx + 1].first - speed_sections[section_idx].first);
                    t           = std::clamp(t, 0.0f, 1.0f);
                    final_speed = (1.0f - t) * speed_sections[section_idx].second + t * speed_sections[section_idx + 1].second;
                }
                return round(final_speed);
            };
            
            float extrusion_speed = std::min(calculate_speed(curr.distance), calculate_speed(next.distance));
            // ORCA: Clamp resulting speed to lowest of calculated speed based on the overhang values and the current speed
            // Fixes bug where resulting overhang speed is higher than the current speed due to (for example) volumetric flow limits.
            extrusion_speed = std::min(extrusion_speed, original_speed);
            
            if(slowdown_for_curled_edges) {
                float curled_speed = calculate_speed(artificial_distance_to_curled_lines);
            	extrusion_speed       = std::min(curled_speed, extrusion_speed); // adjust extrusion speed based on what is smallest - the calculated overhang speed or the artificial curled speed
            }
            
            float overlap = std::min(1 - (curr.distance+artificial_distance_to_curled_lines) * width_inv, 1 - (next.distance+artificial_distance_to_curled_lines) * width_inv);
			
            processed_points.push_back({ scaled(curr.position), extrusion_speed, overlap });
        }
        return processed_points;
    }
};

} // namespace Slic3r

#endif // slic3r_ExtrusionProcessor_hpp_
