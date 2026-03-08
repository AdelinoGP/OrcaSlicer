// [INTENT] BridgeDetector.hpp — header for the bridge orientation optimizer.
//
// A "bridge" is a span of extrusion material printed over empty space, anchored
// only at its endpoints. The bridge orientation matters: parallel lines across the
// gap minimize unsupported length. This class finds the optimal bridging angle.
//
// Two algorithms are exposed:
//   1. BridgeDetector class — brute-force sweep over candidate angles, selects
//      the angle maximizing total "anchored" line coverage. Used by FillBridge
//      (via PerimeterGenerator) and old-style support material.
//   2. detect_bridging_direction() inline functions — PCA-based or floating-edge-
//      cost minimisation; used by Arachne variable-width perimeter bridges.
//
// [COUPLING] Depends on ClipperUtils (polygon intersection), Geometry
//            (directions_parallel, rad2deg), PrincipalComponents2D (PCA).
// [MEMORY]   BridgeDetector holds a const ExPolygons& reference AND may own a
//            copy in expolygons_owned. The reference path is chosen by the second
//            constructor and the owned-copy path by the first constructor.
//            The invariant is: expolygons always aliases either the external object
//            or expolygons_owned. Callers must not destroy the external ExPolygons
//            while BridgeDetector is alive (no lifetime enforcement).
// [HAZARD H331] angle field is output state written in detect_angle(); its initial
//               value -1.0 means "unknown/not-yet-detected". Callers that use angle
//               before calling detect_angle() get -1.0 silently, causing coverage()
//               and unsupported_edges() to return empty results. No assert guards
//               against this misuse pattern.

#ifndef slic3r_BridgeDetector_hpp_
#define slic3r_BridgeDetector_hpp_

#include "ClipperUtils.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "PrincipalComponents2D.hpp"
#include "libslic3r.h"
#include "ExPolygon.hpp"
#include <string>

namespace Slic3r {

// The bridge detector optimizes a direction of bridges over a region or a set of regions.
// A bridge direction is considered optimal, if the length of the lines strang over the region is maximal.
// This is optimal if the bridge is supported in a single direction only, but
// it may not likely be optimal, if the bridge region is supported from all sides. Then an optimal
// solution would find a direction with shortest bridges.
// The bridge orientation is measured CCW from the X axis.
class BridgeDetector
{
public:
    // [STATE] expolygons — input bridge region polygons (const reference).
    //   If constructed from ExPolygon value, expolygons_owned holds the copy and expolygons
    //   is bound to expolygons_owned. Either way, expolygons is always valid and const.
    // The non-grown holes.
    const ExPolygons& expolygons;
    // In case the caller gaves us the input polygons by a value, make a copy.
    ExPolygons expolygons_owned;
    // [STATE] lower_slices — read-only reference to the layer below. Used to detect anchors.
    // Lower slices, all regions.
    const ExPolygons& lower_slices;
    // [STATE] spacing — scaled extrusion width; controls anchor outset margin and line density.
    // Scaled extrusion width of the infill.
    coord_t spacing;
    // [STATE] resolution — angular step for brute-force sweep (default PI/36 = 5 degrees).
    // Angle resolution for the brute force search of the best bridging angle.
    double resolution;
    // [STATE] angle — output field; written by detect_angle(). -1.0 before detection.
    // The final optimal angle.
    double angle;

    // [INTENT] Constructor variants: first takes ExPolygon by value (stores in expolygons_owned),
    //   second takes const ExPolygons& (callers must ensure it outlives this object).
    BridgeDetector(ExPolygon _expolygon, const ExPolygons& _lower_slices, coord_t _extrusion_width);
    BridgeDetector(const ExPolygons& _expolygons, const ExPolygons& _lower_slices, coord_t _extrusion_width);
    // If bridge_direction_override != 0, then the angle is used instead of auto-detect.
    bool      detect_angle(double bridge_direction_override = 0.);
    Polygons  coverage(double angle = -1, bool precise = true) const;
    void      unsupported_edges(double angle, Polylines* unsupported) const;
    Polylines unsupported_edges(double angle = -1) const;

private:
    // Suppress warning "assignment operator could not be generated"
    BridgeDetector& operator=(const BridgeDetector&);

    void initialize();

    // [INTENT] BridgeDirection — value type used by detect_angle() sweep.
    //   Accumulates coverage (total bridged line length) and max_length per angle.
    //   operator< sorts by coverage descending so the best direction is first after std::sort.
    // [HAZARD H332] archored_percent field name has a typo ("archored" vs "anchored").
    //   Must be reproduced exactly in port for ABI compatibility with serialised data.
    struct BridgeDirection
    {
        BridgeDirection(double a = -1.) : angle(a), coverage(0.), max_length(0.), archored_percent(0.) {}
        // the best direction is the one causing most lines to be bridged (thus most coverage)
        bool operator<(const BridgeDirection& other) const
        {
            // Initial sort by coverage only - comparator must obey strict weak ordering
            return this->coverage > other.coverage; // this->archored_percent > other.archored_percent;
        };
        double angle;
        double coverage;
        double max_length;
        double archored_percent;
    };

    // Get possible briging direction candidates.
    std::vector<double> bridge_direction_candidates() const;

    // [STATE] _edges — open polylines that lie on the lower slice contours (supporting edges).
    //   Used only to derive candidate angles in bridge_direction_candidates().
    // Open lines representing the supporting edges.
    Polylines _edges;
    // [STATE] _anchor_regions — closed polygons where the bridge region overlaps with lower slices.
    //   Lines bridged between two distinct anchor regions are considered "anchored".
    // Closed polygons representing the supporting areas.
    ExPolygons _anchor_regions;
};

// [INTENT] detect_bridging_direction(floating_edges, overhang_area) — faster alternative to
//   BridgeDetector for Arachne bridge detection. Two code paths:
//   1. If floating_edges is empty: the overhang is fully surrounded by anchors.
//      Use PCA (principal components) on the overhang polygon to find the axis of
//      minimum span — this typically yields the shortest possible bridge lines.
//   2. If floating_edges is non-empty: find the bridging direction that minimises
//      the dot product of each floating edge with the bridge direction normal.
//      This minimizes the number of bridge endpoints/U-turns that land in unsupported air.
//
// [COUPLING] PrincipalComponents2D::compute_principal_components().
// [HAZARD H333] direction_costs is built from quantized angles (ceil * 1000), so each
//   distinct edge direction adds exactly one entry. For degenerate geometry (e.g. all
//   edges parallel), only one candidate exists and the loop body runs once — correct
//   but could be made more explicit.
// [HAZARD H334] The return value "min_cost" is not a length but a sum of dot products;
//   callers (Arachne) use it to decide if bridge detection succeeded. Semantics change
//   if the unit or normalization of floating_edges changes.

// return ideal bridge direction and unsupported bridge endpoints distance.
inline std::tuple<Vec2d, double> detect_bridging_direction(const Lines& floating_edges, const Polygons& overhang_area)
{
    if (floating_edges.empty()) {
        // consider this area anchored from all sides, pick bridging direction that will likely yield shortest bridges
        auto [pc1, pc2] = compute_principal_components(overhang_area);
        if (pc2 == Vec2f::Zero()) { // overhang may be smaller than resolution. In this case, any direction is ok
            return {Vec2d{1.0, 0.0}, 0.0};
        } else {
            return {pc2.normalized().cast<double>(), 0.0};
        }
    }

    // Overhang is not fully surrounded by anchors, in that case, find such direction that will minimize the number of bridge ends/180turns
    // in the air
    std::unordered_map<double, Vec2d> directions{};
    for (const Line& l : floating_edges) {
        Vec2d  normal          = l.normal().cast<double>().normalized();
        double quantized_angle = std::ceil(std::atan2(normal.y(), normal.x()) * 1000.0);
        directions.emplace(quantized_angle, normal);
    }
    std::vector<std::pair<Vec2d, double>> direction_costs{};
    // it is acutally cost of a perpendicular bridge direction - we find the minimal cost and then return the perpendicular dir
    for (const auto& d : directions) {
        direction_costs.emplace_back(d.second, 0.0);
    }

    for (const Line& l : floating_edges) {
        Vec2d line = (l.b - l.a).cast<double>();
        for (auto& dir_cost : direction_costs) {
            // the dot product already contains the length of the line. dir_cost.first is normalized.
            dir_cost.second += std::abs(line.dot(dir_cost.first));
        }
    }

    Vec2d  result_dir = Vec2d::Ones();
    double min_cost   = std::numeric_limits<double>::max();
    for (const auto& cost : direction_costs) {
        if (cost.second < min_cost) {
            // now flip the orientation back and return the direction of the bridge extrusions
            result_dir = Vec2d{cost.first.y(), -cost.first.x()};
            min_cost   = cost.second;
        }
    }

    return {result_dir, min_cost};
};

// [INTENT] Overload that accepts two Polygons directly. Computes floating_edges as
//   the contour segments of to_cover minus the anchors_area. The expand(anchors_area,
//   SCALED_EPSILON) prevents false "unsupported" classification on numerically touching edges.
// return ideal bridge direction and unsupported bridge endpoints distance.
inline std::tuple<Vec2d, double> detect_bridging_direction(const Polygons& to_cover, const Polygons& anchors_area)
{
    Polygons overhang_area  = diff(to_cover, anchors_area);
    Lines    floating_edges = to_lines(diff_pl(to_polylines(overhang_area), expand(anchors_area, float(SCALED_EPSILON))));
    return detect_bridging_direction(floating_edges, overhang_area);
}

} // namespace Slic3r

#endif
