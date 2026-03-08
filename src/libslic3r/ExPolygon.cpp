// [INTENT] ExPolygon.cpp — implementation of ExPolygon methods.
// ExPolygon = (CCW outer contour Polygon) + (zero or more CW hole Polygons).
// This file implements: geometric transforms, area, validity, containment,
// boundary tests, point projection, simplification, medial axis extraction,
// and collection-level helpers (overlaps, extents, duplicate point detection,
// neighbor removal, small-polygon filtering).
//
// [COUPLING] Depends on ClipperUtils for diff_pl, intersection_pl, union_ex,
// simplify_polygons; Geometry::MedialAxis for skeleton extraction; BoundingBox
// for spatial acceleration; SVG for disabled debug rendering paths.
//
// [STATE] All operations are applied in-place or return new values.
// No persistent internal state beyond the ExPolygon's own contour/holes.

#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Exception.hpp"
#include "Geometry/MedialAxis.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "ClipperUtils.hpp"
#include "SVG.hpp"
#include <algorithm>
#include <cassert>
#include <list>

namespace Slic3r {

// [INTENT] scale(double): uniform in-place scale of all points in contour and
// holes. Delegates to Polygon::scale(double). Applied independently to each
// sub-polygon — no cross-contour rounding interactions.
// [HAZARD H611] Each coord_t is multiplied by a double factor and rounded.
// Repeated scaling accumulates rounding error per-vertex.
void ExPolygon::scale(double factor)
{
    contour.scale(factor);
    for (Polygon& hole : holes)
        hole.scale(factor);
}

// [INTENT] scale(double, double): non-uniform scale — separate X and Y factors.
// Destroys the isotropy of the shape. Delegates to Polygon::scale(fx, fy).
void ExPolygon::scale(double factor_x, double factor_y)
{
    contour.scale(factor_x, factor_y);
    for (Polygon& hole : holes)
        hole.scale(factor_x, factor_y);
}

// [INTENT] translate(Point): shift all points by integer vector p (scaled
// coords). Delegates to Polygon::translate(Point) for contour and each hole.
void ExPolygon::translate(const Point& p)
{
    contour.translate(p);
    for (Polygon& hole : holes)
        hole.translate(p);
}

// [INTENT] rotate(double): in-place rotation about the origin by angle radians.
// Delegates to Polygon::rotate(angle) for contour and each hole.
// [HAZARD H608] Polygon::rotate uses cos/sin snapping (round()); repeated
// rotations accumulate rounding error.
void ExPolygon::rotate(double angle)
{
    contour.rotate(angle);
    for (Polygon& hole : holes)
        hole.rotate(angle);
}

// [INTENT] rotate(double, Point): rotation about an arbitrary center point.
// Delegates to Polygon::rotate(angle, center) for each sub-polygon.
void ExPolygon::rotate(double angle, const Point& center)
{
    contour.rotate(angle, center);
    for (Polygon& hole : holes)
        hole.rotate(angle, center);
}

// [INTENT] area(): signed area of the ExPolygon in (scaled integer units)^2.
// Contour contributes positive area (CCW → shoelace positive).
// Each hole contributes negative area (CW → shoelace negative).
// Net area = contour.area() - |sum of hole areas|.
//
// [HAZARD H602] The double-negation idiom:
//   a -= -hole.area()
// is equivalent to:
//   a += |hole.area()|
// because hole.area() returns a negative value for a CW polygon.
// The code is correct but unintuitive. A port should rewrite as:
//   a += std::abs(hole.area())   // or a -= hole.area() if holes are CW
// Note: hole.area() < 0 for CW holes, so "a -= hole.area()" would add the
// absolute area back in — but "-hole.area()" = "+|hole.area()|", and then
// "a -= +|hole.area()|" SUBTRACTS it. The negation is intentional.
double ExPolygon::area() const
{
    double a = this->contour.area();
    for (const Polygon& hole : holes)
        a -= -hole.area(); // holes have negative area
    return a;
}

// [INTENT] is_valid(): checks both geometric validity (>= 3 points per polygon)
// AND winding convention (contour CCW, each hole CW).
// Uses Polygon::is_counter_clockwise() which delegates to ClipperLib::Orientation.
// [HAZARD H610] is_valid() is the only enforcement of winding convention.
// Construction does NOT call is_valid(). Algorithms that assume valid winding
// (area(), Clipper ops) will silently produce wrong results for invalid shapes.
bool ExPolygon::is_valid() const
{
    if (!this->contour.is_valid() || !this->contour.is_counter_clockwise())
        return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (!(*it).is_valid() || (*it).is_counter_clockwise())
            return false;
    }
    return true;
}

// [INTENT] douglas_peucker(double): simplify all rings (contour + holes)
// independently with the D-P algorithm using the given tolerance (scaled units).
// Delegates to Polygon::douglas_peucker().
// [HAZARD H613] Independent simplification of contour and holes may violate
// topological consistency — a simplified hole edge could become coincident
// with or cross the contour boundary. Use simplify() (D-P + union_ex) to
// preserve topology at the cost of potential shape merging.
void ExPolygon::douglas_peucker(double tolerance)
{
    this->contour.douglas_peucker(tolerance);
    for (Polygon& poly : this->holes)
        poly.douglas_peucker(tolerance);
}

// [INTENT] contains(Line): wraps a Line as a 2-point Polyline and delegates
// to contains(Polyline). The ExPolygon contains the Line iff the line segment
// (not just the endpoints) is entirely inside the ExPolygon.
// [HAZARD H614] This allocates a temporary Polyline. Hot-path callers should
// use the Polyline overload directly if they already have a Polyline.
bool ExPolygon::contains(const Line& line) const { return this->contains(Polyline(line.a, line.b)); }

// [INTENT] contains(Polyline): true iff the polyline is completely inside this
// ExPolygon. Uses a bbox quick-reject first, then Clipper diff_pl to check
// if any portion of the polyline lies outside.
// A polyline is "contained" iff diff_pl(polyline, *this) is empty.
// [HAZARD H614] bbox2.inflated(1) on a local copy — the original bbox2 is not
// modified. This is correct (inflation is only needed for the overlap test).
// [HAZARD H629] diff_pl is Clipper-based and subject to Clipper's open-boundary
// conventions. Polyline segments exactly on the contour edge may or may not
// appear in the diff depending on orientation.
bool ExPolygon::contains(const Polyline& polyline) const
{
    BoundingBox bbox1 = get_extents(*this);
    BoundingBox bbox2 = get_extents(polyline);
    bbox2.inflated(1);
    if (!bbox1.overlap(bbox2))
        return false;

    return diff_pl(polyline, *this).empty();
}

// [INTENT] contains(Polylines): same as contains(Polyline) but for a collection.
// True iff ALL polylines in the collection are entirely inside the ExPolygon.
// Uses diff_pl(Polylines, ExPolygon) — Clipper computes the union diff in one
// pass.
// [COUPLING] Disabled debug SVG rendering code is bracketed with #if 0.
bool ExPolygon::contains(const Polylines& polylines) const
{
#if 0
    BoundingBox bbox = get_extents(polylines);
    bbox.merge(get_extents(*this));
    SVG svg(debug_out_path("ExPolygon_contains.svg"), bbox);
    svg.draw(*this);
    svg.draw_outline(*this);
    svg.draw(polylines, "blue");
#endif
    Polylines pl_out = diff_pl(polylines, *this);
#if 0
    svg.draw(pl_out, "red");
#endif
    return pl_out.empty();
}

// [INTENT] contains(Point, border_result): point-in-ExPolygon test.
// A point is "contained" iff:
//   1. It is inside (or on the boundary of) the contour.
//   2. It is NOT inside (or on the boundary of) any hole.
// border_result controls boundary membership. For the hole test, the logic
// INVERTS border_result: if border_result=true (boundary IS inside), then a
// point on a hole boundary is treated as NOT inside the hole. This preserves
// the intuition that a point on the inner boundary of an ExPolygon is still
// "contained" in the ExPolygon.
// [HAZARD H615] The inversion of border_result for holes is correct but subtle.
// A point exactly on a hole boundary with border_result=true:
//   - Slic3r::contains(hole, point, !border_result=false) → returns false
//     (boundary NOT inside), so the hole check fails, point is still "in" ExPolygon.
bool ExPolygon::contains(const Point& point, bool border_result /* = true */) const
{
    if (!Slic3r::contains(contour, point, border_result))
        // Outside the outer contour, not on the contour boundary.
        return false;
    for (const Polygon& hole : this->holes)
        if (Slic3r::contains(hole, point, !border_result))
            // Inside a hole, not on the hole boundary.
            return false;
    return true;
}

// [INTENT] on_boundary(Point, eps): returns true if the point is within eps
// (scaled coord_t units) of any edge of any ring (contour or holes).
// Linear scan — O(total_points). eps is a distance threshold in scaled integers.
bool ExPolygon::on_boundary(const Point& point, double eps) const
{
    if (this->contour.on_boundary(point, eps))
        return true;
    for (const Polygon& hole : this->holes)
        if (hole.on_boundary(point, eps))
            return true;
    return false;
}

// [INTENT] point_projection(Point): find the closest point on any boundary
// of this ExPolygon to the query point. When there are no holes, delegates
// directly to contour.point_projection() to avoid allocation overhead.
// With holes, iterates all rings via contour_or_hole() and picks the minimum
// squared distance. Returns the closest projected point.
// [COUPLING] Uses contour_or_hole() for unified indexing (0 = contour,
// 1..n = holes). Squared distance avoids sqrt for comparison.
// Projection of a point onto the polygon.
Point ExPolygon::point_projection(const Point& point) const
{
    if (this->holes.empty()) {
        return this->contour.point_projection(point);
    } else {
        double dist_min2 = std::numeric_limits<double>::max();
        Point  closest_pt_min;
        for (size_t i = 0; i < this->num_contours(); ++i) {
            Point  closest_pt = this->contour_or_hole(i).point_projection(point);
            double d2         = (closest_pt - point).cast<double>().squaredNorm();
            if (d2 < dist_min2) {
                dist_min2      = d2;
                closest_pt_min = closest_pt;
            }
        }
        return closest_pt_min;
    }
}

// [INTENT] symmetric_y(coord_t y_axis): reflect all points through the
// horizontal line y = y_axis. Delegates to Polygon::symmetric_y for each ring.
// BBS-added utility for plate symmetry operations.
void ExPolygon::symmetric_y(const coord_t& y_axis)
{
    this->contour.symmetric_y(y_axis);
    for (Polygon& hole : holes)
        hole.symmetric_y(y_axis);
}

// [INTENT] overlaps(ExPolygon other): true iff the two ExPolygons geometrically
// overlap (share interior area). Two cases:
//   1. They intersect: intersection_pl(to_polylines(other), *this) is non-empty.
//   2. *this is fully inside other: pl_out is empty but
//      other.contains(this->contour.points.front()) is true.
// [HAZARD H616] The test is NOT commutative. From the source comment:
//   - Touching at a vertical boundary → considered overlapping.
//   - Touching at a horizontal boundary → NOT considered overlapping.
// This asymmetry is Clipper's behavior for open polyline / horizontal edge
// intersections. The unit test SCENARIO("Clipper diff with polyline") documents
// this. Callers should check both directions if symmetry is required.
// [HAZARD H630] If *this has zero points (empty), the function returns false
// immediately via the early-out on empty(). But if *this is non-empty and
// other is a proper subset of *this, only the front point is tested — if that
// one point happens to be in a hole of *this, the test could give a wrong result.
// In practice, the front point is always a valid interior point for non-degenerate
// polygons.
bool ExPolygon::overlaps(const ExPolygon& other) const
{
    if (this->empty() || other.empty())
        return false;

#if 0
    BoundingBox bbox = get_extents(other);
    bbox.merge(get_extents(*this));
    static int iRun = 0;
    SVG svg(debug_out_path("ExPolygon_overlaps-%d.svg", iRun ++), bbox);
    svg.draw(*this);
    svg.draw_outline(*this);
    svg.draw_outline(other, "blue");
#endif

    Polylines pl_out = intersection_pl(to_polylines(other), *this);

#if 0
    svg.draw(pl_out, "red");
#endif

    // See unit test SCENARIO("Clipper diff with polyline", "[Clipper]")
    // for in which case the intersection_pl produces any intersection.
    return !pl_out.empty() ||
           // If *this is completely inside other, then pl_out is empty, but the expolygons overlap. Test for that situation.
           other.contains(this->contour.points.front());
}

// [INTENT] overlaps(ExPolygons, ExPolygons): O(n*m) pairwise overlap test.
// Short-circuits on first overlap found. No spatial index — for large n,m
// a spatial index (e.g. AABBTree) would be needed for performance.
// [HAZARD H616] Per-pair asymmetry applies (see ExPolygon::overlaps).
bool overlaps(const ExPolygons& expolys1, const ExPolygons& expolys2)
{
    for (const ExPolygon& expoly1 : expolys1) {
        for (const ExPolygon& expoly2 : expolys2) {
            if (expoly1.overlaps(expoly2))
                return true;
        }
    }
    return false;
}

// [INTENT] overlaps(ExPolygons, ExPolygon): same as above but for a single
// query ExPolygon against a collection. Linear scan.
bool overlaps(const ExPolygons& expolys, const ExPolygon& expoly)
{
    for (const ExPolygon& el : expolys) {
        if (el.overlaps(expoly))
            return true;
    }
    return false;
}

// [INTENT] projection_onto(ExPolygons, Point): find the closest point on any
// boundary (contour or hole) of any ExPolygon in the collection to the query
// point "from". Uses squared distance to avoid sqrt during comparison.
// Returns the globally closest projected point.
// [HAZARD H631] Uses int-typed loop index "i" for num_contours() (size_t).
// Signed/unsigned comparison on some platforms. Not a correctness issue but
// a clean-code concern.
Point projection_onto(const ExPolygons& polygons, const Point& from)
{
    Point  projected_pt;
    double min_dist = std::numeric_limits<double>::max();

    for (const auto& poly : polygons) {
        for (int i = 0; i < poly.num_contours(); i++) {
            Point  p    = from.projection_onto(poly.contour_or_hole(i));
            double dist = (from - p).cast<double>().squaredNorm();
            if (dist < min_dist) {
                projected_pt = p;
                min_dist     = dist;
            }
        }
    }

    return projected_pt;
}

// [INTENT] simplify_p(tolerance, Polygons*): Douglas-Peucker simplification
// then Clipper simplify_polygons(), appending results to an existing Polygons
// vector. Delegates to the returning overload then inserts.
void ExPolygon::simplify_p(double tolerance, Polygons* polygons) const
{
    Polygons pp = this->simplify_p(tolerance);
    polygons->insert(polygons->end(), pp.begin(), pp.end());
}

// [INTENT] simplify_p(tolerance) -> Polygons: apply Douglas-Peucker to each
// ring independently. Before D-P, temporarily adds a closing duplicate point
// (push_back(front)), then removes it after D-P. This is needed because
// MultiPoint::_douglas_peucker works on open polylines.
// Result goes through simplify_polygons() (Clipper) which resolves any
// self-intersections introduced by D-P.
// [HAZARD H617] If D-P reduces a ring to < 3 unique points, simplify_polygons
// may return an empty result. Callers must handle an empty Polygons return.
// [HAZARD H613] Contour and holes are simplified independently — topological
// consistency across rings is not guaranteed by this function alone. Use
// simplify() (which follows with union_ex) for topology-safe output.
Polygons ExPolygon::simplify_p(double tolerance) const
{
    Polygons pp;
    pp.reserve(this->holes.size() + 1);
    // contour
    {
        Polygon p = this->contour;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }
    // holes
    for (Polygon p : this->holes) {
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }
    return simplify_polygons(pp);
}

// [INTENT] simplify(tolerance) -> ExPolygons: full topology-safe simplification.
// Calls simplify_p() (D-P + Clipper simplify), then union_ex() to reconstruct
// hole topology. union_ex may merge adjacent regions that were separated only
// by detail that D-P simplified away.
// [COUPLING] union_ex is in ClipperUtils — heavy operation (full Clipper union).
ExPolygons ExPolygon::simplify(double tolerance) const { return union_ex(this->simplify_p(tolerance)); }

// [INTENT] simplify(tolerance, ExPolygons*): appends simplified results to
// an existing ExPolygons vector.
void ExPolygon::simplify(double tolerance, ExPolygons* expolygons) const { append(*expolygons, this->simplify(tolerance)); }

// [INTENT] medial_axis(min_width, max_width, ThickPolylines*): extract the
// medial axis (skeleton) of this ExPolygon as ThickPolylines (polylines with
// per-segment width derived from the Voronoi diagram).
//
// ALGORITHM (4 phases):
//   Phase 1 — Build: delegate to Geometry::MedialAxis::build() which constructs
//             a Voronoi diagram and traces edges into ThickPolylines.
//
//   Phase 2 — Endpoint extension: for each polyline with open endpoints
//             (endpoints.first / .second flags), extend the endpoint outward
//             along the segment direction by max_width, then intersect with
//             this->contour to snap the endpoint to the boundary.
//             Special case: for a 2-point polyline, split the segment at its
//             midpoint before extending, to prevent the extension from passing
//             through the other end.
//
//   Phase 3 — Short polyline removal: remove any polyline that has at least
//             one open endpoint AND is shorter than max_w*2 (where max_w is
//             the maximum width found across all output polylines).
//
//   Phase 4 — Greedy reconnection (only if removals occurred): iterate all
//             remaining polylines; for each pair sharing an endpoint, append
//             the second to the first (reversing as needed) and erase the
//             second. This enables loop detection downstream.
//
// [STATE] pp is a local ThickPolylines vector built and mutated in-place.
// [HAZARD H618] Endpoint extension only intersects with this->contour, not
// holes. If the ExPolygon has internal holes, the extension line may cross a
// hole boundary — the intersection with the contour will still snap correctly
// (the contour is the outer boundary), but the extension path may visually
// cross a hole region. MedialAxis is not used for holed ExPolygons in practice.
// [HAZARD H619] Greedy reconnection in Phase 4 connects random pairs when more
// than two polylines share a point. Per code comment: "This has no drawbacks
// since we optimize later using nearest-neighbor." Assumes nearest-neighbor
// post-processing. If a different optimizer is used, this may need to be
// reconsidered.
// [HAZARD H632] The width invariant "polyline.width.size() == polyline.points.size()*2 - 2"
// is asserted after reconnection (line 360). The reconnection appends
// other.width (all entries) to polyline.width and other.points[1..end] to
// polyline.points. This preserves the invariant:
//   new_points_size = polyline.points.size() + other.points.size() - 1
//   new_width_size  = polyline.width.size() + other.width.size()
//   since other.width.size() == other.points.size()*2 - 2 (one entry per
//   segment endpoint pair). The assert verifies this. Any port must maintain
//   this invariant.
void ExPolygon::medial_axis(double min_width, double max_width, ThickPolylines* polylines) const
{
    // init helper object
    Slic3r::Geometry::MedialAxis ma(min_width, max_width, *this);

    // compute the Voronoi diagram and extract medial axis polylines
    ThickPolylines pp;
    ma.build(&pp);

    /*
    SVG svg("medial_axis.svg");
    svg.draw(*this);
    svg.draw(pp);
    svg.Close();
    */

    /* Find the maximum width returned; we're going to use this for validating and
       filtering the output segments. */
    double max_w = 0;
    for (ThickPolylines::const_iterator it = pp.begin(); it != pp.end(); ++it)
        max_w = fmaxf(max_w, *std::max_element(it->width.begin(), it->width.end()));

    /* Loop through all returned polylines in order to extend their endpoints to the
       expolygon boundaries */
    bool removed = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];

        // extend initial and final segments of each polyline if they're actual endpoints
        /* We assign new endpoints to temporary variables because in case of a single-line
           polyline, after we extend the start point it will be caught by the intersection()
           call, so we keep the inner point until we perform the second intersection() as well */
        Point new_front = polyline.points.front();
        Point new_back  = polyline.points.back();
        if (polyline.endpoints.first && !this->on_boundary(new_front, SCALED_EPSILON)) {
            Vec2d p1 = polyline.points.front().cast<double>();
            Vec2d p2 = polyline.points[1].cast<double>();
            // prevent the line from touching on the other side, otherwise intersection() might return that solution
            if (polyline.points.size() == 2)
                p2 = (p1 + p2) * 0.5;
            // Extend the start of the segment.
            p1 -= (p2 - p1).normalized() * max_width;
            this->contour.intersection(Line(p1.cast<coord_t>(), p2.cast<coord_t>()), &new_front);
        }
        if (polyline.endpoints.second && !this->on_boundary(new_back, SCALED_EPSILON)) {
            Vec2d p1 = (polyline.points.end() - 2)->cast<double>();
            Vec2d p2 = polyline.points.back().cast<double>();
            // prevent the line from touching on the other side, otherwise intersection() might return that solution
            if (polyline.points.size() == 2)
                p1 = (p1 + p2) * 0.5;
            // Extend the start of the segment.
            p2 += (p2 - p1).normalized() * max_width;
            this->contour.intersection(Line(p1.cast<coord_t>(), p2.cast<coord_t>()), &new_back);
        }
        polyline.points.front() = new_front;
        polyline.points.back()  = new_back;

        /*  remove too short polylines
            (we can't do this check before endpoints extension and clipping because we don't
            know how long will the endpoints be extended since it depends on polygon thickness
            which is variable - extension will be <= max_width/2 on each side)  */
        if ((polyline.endpoints.first || polyline.endpoints.second) && polyline.length() < max_w * 2) {
            pp.erase(pp.begin() + i);
            --i;
            removed = true;
            continue;
        }
    }

    /*  If we removed any short polylines we now try to connect consecutive polylines
        in order to allow loop detection. Note that this algorithm is greedier than
        MedialAxis::process_edge_neighbors() as it will connect random pairs of
        polylines even when more than two start from the same point. This has no
        drawbacks since we optimize later using nearest-neighbor which would do the
        same, but should we use a more sophisticated optimization algorithm we should
        not connect polylines when more than two meet.  */
    if (removed) {
        for (size_t i = 0; i < pp.size(); ++i) {
            ThickPolyline& polyline = pp[i];
            if (polyline.endpoints.first && polyline.endpoints.second)
                continue; // optimization

            // find another polyline starting here
            for (size_t j = i + 1; j < pp.size(); ++j) {
                ThickPolyline& other = pp[j];
                if (polyline.last_point() == other.last_point()) {
                    other.reverse();
                } else if (polyline.first_point() == other.last_point()) {
                    polyline.reverse();
                    other.reverse();
                } else if (polyline.first_point() == other.first_point()) {
                    polyline.reverse();
                } else if (polyline.last_point() != other.first_point()) {
                    continue;
                }

                // [HAZARD H632] Width invariant: append other.width (all entries)
                // and other.points[1..end]. See function header for correctness proof.
                polyline.points.insert(polyline.points.end(), other.points.begin() + 1, other.points.end());
                polyline.width.insert(polyline.width.end(), other.width.begin(), other.width.end());
                polyline.endpoints.second = other.endpoints.second;
                assert(polyline.width.size() == polyline.points.size() * 2 - 2);

                pp.erase(pp.begin() + j);
                j = i; // restart search from i+1
            }
        }
    }

    polylines->insert(polylines->end(), pp.begin(), pp.end());
}

// [INTENT] medial_axis(min_width, max_width, Polylines*): width-discarding
// variant. Builds ThickPolylines internally then copies only the points vector
// into plain Polylines. Arc fitting metadata (fitting_result) is NOT copied
// (ThickPolyline has none from the Voronoi build path). Points-only copy.
void ExPolygon::medial_axis(double min_width, double max_width, Polylines* polylines) const
{
    ThickPolylines tp;
    this->medial_axis(min_width, max_width, &tp);
    polylines->reserve(polylines->size() + tp.size());
    for (auto& pl : tp)
        polylines->emplace_back(pl.points);
}

// [INTENT] lines(): collect all edges from all rings (contour + holes) into
// a flat Lines vector. Contour edges first, then each hole's edges.
// Delegates to Polygon::lines() which returns n edges for an n-point polygon
// (including the closing edge from last point to first point).
Lines ExPolygon::lines() const
{
    Lines lines = this->contour.lines();
    for (Polygons::const_iterator h = this->holes.begin(); h != this->holes.end(); ++h) {
        Lines hole_lines = h->lines();
        lines.insert(lines.end(), hole_lines.begin(), hole_lines.end());
    }
    return lines;
}

// [INTENT] expolygons_match(l, r): topology-level equality. Two ExPolygons match
// if they have the same number of holes AND their contours and each hole match
// under polygons_match() (rotation-invariant vertex equality).
// [HAZARD H633] Holes must be in the SAME ORDER in both ExPolygons. Topologically
// identical ExPolygons with holes in different order will NOT match. No sorting
// is performed. Callers that need order-invariant matching must sort holes first.
// Do expolygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool expolygons_match(const ExPolygon& l, const ExPolygon& r)
{
    if (l.holes.size() != r.holes.size() || !polygons_match(l.contour, r.contour))
        return false;
    for (size_t hole_idx = 0; hole_idx < l.holes.size(); ++hole_idx)
        if (!polygons_match(l.holes[hole_idx], r.holes[hole_idx]))
            return false;
    return true;
}

// [INTENT] get_extents(ExPolygon): bounding box of the CONTOUR only.
// This is correct because holes are geometrically inside the contour,
// so contour bbox == ExPolygon bbox.
// [HAZARD H625] If contour is empty but holes are non-empty (malformed input),
// the returned BoundingBox is undefined (BoundingBox::defined == false).
BoundingBox get_extents(const ExPolygon& expolygon) { return get_extents(expolygon.contour); }

// [INTENT] get_extents(ExPolygons): union bounding box of all non-empty contours.
// Skips ExPolygons with empty contours to avoid merging an undefined BoundingBox.
BoundingBox get_extents(const ExPolygons& expolygons)
{
    BoundingBox bbox;
    if (!expolygons.empty()) {
        for (size_t i = 0; i < expolygons.size(); ++i)
            if (!expolygons[i].contour.points.empty())
                bbox.merge(get_extents(expolygons[i]));
    }
    return bbox;
}

// [INTENT] get_extents_rotated(ExPolygon, angle): bounding box of the contour
// after rotation by angle radians. Only uses the contour — holes are inside.
// Delegates to get_extents_rotated(Polygon, angle) which rotates the polygon
// copy and takes its bbox.
BoundingBox get_extents_rotated(const ExPolygon& expolygon, double angle) { return get_extents_rotated(expolygon.contour, angle); }

// [INTENT] get_extents_rotated(ExPolygons, angle): union of rotated contour
// bboxes for all ExPolygons. Initializes with the first non-empty contour,
// then merges. If expolygons is empty, returns a default (undefined) BoundingBox.
BoundingBox get_extents_rotated(const ExPolygons& expolygons, double angle)
{
    BoundingBox bbox;
    if (!expolygons.empty()) {
        bbox = get_extents_rotated(expolygons.front().contour, angle);
        for (size_t i = 1; i < expolygons.size(); ++i)
            bbox.merge(get_extents_rotated(expolygons[i].contour, angle));
    }
    return bbox;
}

// [INTENT] get_extents_vector(ExPolygons): returns one BoundingBox per
// ExPolygon (contour only). Used for spatial binning. The `extern` linkage
// specifier here is redundant (function definitions in .cpp are already
// external), but harmless.
extern std::vector<BoundingBox> get_extents_vector(const ExPolygons& polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (ExPolygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it)
        out.push_back(get_extents(*it));
    return out;
}

// [INTENT] has_duplicate_points(ExPolygon): check for duplicate vertices.
// Active path (#if 1): global check — flattens ALL points (contour + holes)
// into one vector, sorts, and checks for adjacent duplicates. A point that
// appears in both the contour and a hole boundary will be reported as duplicate.
// Inactive path (#else): per-contour check — would only catch duplicates
// within a single ring, not across rings.
// [HAZARD H626] Global check reports false positives if a contour and hole
// share a vertex (geometrically degenerate but valid in some edge cases).
// The per-contour path (inactive) is more lenient. The choice to use global
// checking is a conservative stance — any ambiguity is treated as an error.
bool has_duplicate_points(const ExPolygon& expoly)
{
#if 1
    // Check globally.
    size_t cnt = expoly.contour.points.size();
    for (const Polygon& hole : expoly.holes)
        cnt += hole.points.size();
    Points allpts;
    allpts.reserve(cnt);
    allpts.insert(allpts.begin(), expoly.contour.points.begin(), expoly.contour.points.end());
    for (const Polygon& hole : expoly.holes)
        allpts.insert(allpts.end(), hole.points.begin(), hole.points.end());
    return has_duplicate_points(std::move(allpts));
#else
    // Check per contour.
    if (has_duplicate_points(expoly.contour))
        return true;
    for (const Polygon& hole : expoly.holes)
        if (has_duplicate_points(hole))
            return true;
    return false;
#endif
}

// [INTENT] has_duplicate_points(ExPolygons): same global check for a collection.
// Flattens all points from all ExPolygons into one vector — a point that appears
// in any two ExPolygons (even separate ones) would be reported as duplicate.
// [HAZARD H626 continued] This is even more conservative than the single-ExPolygon
// version — points shared across different ExPolygons (which is geometrically
// valid for adjacent islands) would be flagged. The intent appears to be
// integrity checking of freshly-produced geometry, where such sharing is unexpected.
bool has_duplicate_points(const ExPolygons& expolys)
{
#if 1
    // Check globally.
    Points allpts;
    allpts.reserve(count_points(expolys));
    for (const ExPolygon& expoly : expolys) {
        allpts.insert(allpts.begin(), expoly.contour.points.begin(), expoly.contour.points.end());
        for (const Polygon& hole : expoly.holes)
            allpts.insert(allpts.end(), hole.points.begin(), hole.points.end());
    }
    return has_duplicate_points(std::move(allpts));
#else
    // Check per contour.
    for (const ExPolygon& expoly : expolys)
        if (has_duplicate_points(expoly))
            return true;
    return false;
#endif
}

// [INTENT] remove_same_neighbor(ExPolygons): remove consecutive duplicate
// points from all rings of all ExPolygons. After removal, erases any ExPolygon
// whose contour has <= 2 remaining points (degenerate — not a valid polygon).
// Returns true if any removal was made.
// [COUPLING] Delegates to remove_same_neighbor(Polygon) and
// remove_same_neighbor(Polygons) (defined in Polygon.cpp).
// [HAZARD H634] Only contour degeneracy triggers ExPolygon erasure. If a hole
// is reduced to <= 2 points by this function, the hole is silently left in
// place (remove_same_neighbor(holes) removes points from the Polygons vector
// but does not check for degenerate holes). Downstream code may receive
// 2-point holes.
bool remove_same_neighbor(ExPolygons& expolygons)
{
    if (expolygons.empty())
        return false;
    bool remove_from_holes   = false;
    bool remove_from_contour = false;
    for (ExPolygon& expoly : expolygons) {
        remove_from_contour |= remove_same_neighbor(expoly.contour);
        remove_from_holes |= remove_same_neighbor(expoly.holes);
    }
    // Removing of expolygons without contour
    if (remove_from_contour)
        expolygons.erase(std::remove_if(expolygons.begin(), expolygons.end(),
                                        [](const ExPolygon& p) { return p.contour.points.size() <= 2; }),
                         expolygons.end());
    return remove_from_holes || remove_from_contour;
}

// [INTENT] remove_sticks(ExPolygon): remove "stick" edges (degenerate spikes
// or collinear runs that don't enclose area) from contour and all holes.
// Returns true if any stick was removed. Delegates to remove_sticks(Polygon)
// and remove_sticks(Polygons).
bool remove_sticks(ExPolygon& poly) { return remove_sticks(poly.contour) || remove_sticks(poly.holes); }

// [INTENT] remove_small_and_small_holes(ExPolygons, min_area): two-stage filter:
//   Stage 1: Retain only ExPolygons whose abs(area) >= min_area.
//   Stage 2: For each retained ExPolygon, remove holes whose abs(area) < min_area
//            via remove_small(holes, min_area).
// Uses a swap-based in-place compaction (free_idx pattern) to avoid excessive
// vector reallocations.
// Returns true if any modification was made (either ExPolygons or holes removed).
// [HAZARD H628] area() uses shoelace formula; result may be slightly negative
// for properly wound CCW contours due to floating-point accumulation, hence
// std::abs(). Holes (CW) have negative area — std::abs() handles them too.
bool remove_small_and_small_holes(ExPolygons& expolygons, double min_area)
{
    bool   modified = false;
    size_t free_idx = 0;
    for (size_t expoly_idx = 0; expoly_idx < expolygons.size(); ++expoly_idx) {
        if (std::abs(expolygons[expoly_idx].area()) >= min_area) {
            // Expolygon is big enough, so also check all its holes
            modified |= remove_small(expolygons[expoly_idx].holes, min_area);
            if (free_idx < expoly_idx) {
                std::swap(expolygons[expoly_idx].contour, expolygons[free_idx].contour);
                std::swap(expolygons[expoly_idx].holes, expolygons[free_idx].holes);
            }
            ++free_idx;
        } else
            modified = true;
    }
    if (free_idx < expolygons.size())
        expolygons.erase(expolygons.begin() + free_idx, expolygons.end());
    return modified;
}

// [INTENT] keep_largest_contour_only(ExPolygons): retains only the ExPolygon
// with the largest contour area (measured by Polygon::area() — shoelace,
// positive for CCW). Clears the vector and replaces it with a single-element
// vector.
// [HAZARD H627] Comparison uses Polygon::area() directly (not abs()). A CW
// contour returns negative area and will never be selected as the maximum
// (assuming there is at least one CCW contour). If all contours are CW (malformed
// input), max_area stays 0 and max_area_polygon remains nullptr — the assert
// will fire in debug builds; release builds would crash on nullptr dereference.
void keep_largest_contour_only(ExPolygons& polygons)
{
    if (polygons.size() > 1) {
        double     max_area         = 0.;
        ExPolygon* max_area_polygon = nullptr;
        for (ExPolygon& p : polygons) {
            double a = p.contour.area();
            if (a > max_area) {
                max_area         = a;
                max_area_polygon = &p;
            }
        }
        assert(max_area_polygon != nullptr);
        ExPolygon p(std::move(*max_area_polygon));
        polygons.clear();
        polygons.emplace_back(std::move(p));
    }
}

} // namespace Slic3r
