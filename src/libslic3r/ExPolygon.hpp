#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

// [INTENT] ExPolygon: a polygon-with-holes type representing a single connected
// planar region. The outer boundary (contour) is a CCW Polygon; each hole is a
// CW Polygon. This is the canonical "slice layer island" type used throughout
// the slicer — nearly every layer-processing algorithm returns or consumes
// ExPolygons.
//
// [STATE] Two public data members:
//   contour (Polygon, CCW)  — outer boundary
//   holes   (Polygons, CW)  — zero or more inner exclusion regions
// Winding convention is enforced by callers and by is_valid(); the class itself
// does NOT enforce winding on construction.
//
// [COUPLING] Pulls in Point.hpp, Polygon.hpp, Polyline.hpp — the full 2-D
// geometry primitive stack. Also registers Boost.Polygon traits for interop
// with Boost polygon operations (at the bottom of this file).
//
// [MEMORY] ExPolygons = std::vector<ExPolygon>; each ExPolygon owns its
// contour (Polygon, which uses tbb::scalable_allocator for its points vector)
// and its holes (Polygons, same allocator family). Deep copies are O(n points).
//
// [HAZARD H609] The Boost.Polygon polygon_traits specialization exposes only
// the contour points as the "polygon" view. The holes are exposed separately
// via polygon_with_holes_traits. Code using ExPolygon through the plain
// polygon_traits interface (not polygon_with_holes_concept) will silently
// ignore all holes.

#include "Point.hpp"
#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include <vector>

namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

class ExPolygon
{
public:
    // [INTENT] Default, copy, and move construction. No invariant is enforced
    // on construction — callers are responsible for maintaining CCW contour and
    // CW holes.
    ExPolygon()                       = default;
    ExPolygon(const ExPolygon& other) = default;
    ExPolygon(ExPolygon&& other)      = default;

    // [INTENT] Convenience constructors for building ExPolygons from pre-built
    // Polygon or raw Points objects. The single-contour forms leave holes empty.
    // The two-argument forms add exactly one hole.
    explicit ExPolygon(const Polygon& contour) : contour(contour) {}
    explicit ExPolygon(Polygon&& contour) : contour(std::move(contour)) {}
    explicit ExPolygon(const Points& contour) : contour(contour) {}
    explicit ExPolygon(Points&& contour) : contour(std::move(contour)) {}
    explicit ExPolygon(const Polygon& contour, const Polygon& hole) : contour(contour) { holes.emplace_back(hole); }
    explicit ExPolygon(Polygon&& contour, Polygon&& hole) : contour(std::move(contour)) { holes.emplace_back(std::move(hole)); }
    explicit ExPolygon(const Points& contour, const Points& hole) : contour(contour) { holes.emplace_back(hole); }
    explicit ExPolygon(Points&& contour, Polygon&& hole) : contour(std::move(contour)) { holes.emplace_back(std::move(hole)); }

    // [INTENT] Initializer-list constructors for test / debug use.
    ExPolygon(std::initializer_list<Point> contour) : contour(contour) {}
    ExPolygon(std::initializer_list<Point> contour, std::initializer_list<Point> hole) : contour(contour), holes({hole}) {}

    ExPolygon& operator=(const ExPolygon& other) = default;
    ExPolygon& operator=(ExPolygon&& other)      = default;

    // [STATE] Core geometry data.
    // contour: outer boundary, CCW winding (positive area from shoelace formula).
    // holes:   inner exclusion polygons, CW winding (negative area from shoelace).
    // [HAZARD H610] Winding is a convention only — nothing prevents constructing
    // an ExPolygon with CW contour or CCW holes. Only is_valid() checks this.
    // Downstream algorithms that assume winding (e.g. Clipper, area accumulation)
    // will silently produce wrong results if the convention is violated.
    Polygon  contour; // CCW
    Polygons holes;   // CW

    // [INTENT] clear() resets to the empty ExPolygon state — no points, no holes.
    void clear()
    {
        contour.points.clear();
        holes.clear();
    }

    // [INTENT] Uniform scale all coordinates. Calls Polygon::scale() on contour
    // and each hole. Operates in-place in scaled integer space; floating-point
    // scale factor is applied to each coord_t individually.
    // [HAZARD H611] scale(double) multiplies each coord_t by a double and rounds.
    // Repeated scaling accumulates rounding error. Holes and contour are scaled
    // independently — no single-precision accumulation across the full shape.
    void scale(double factor);

    // [INTENT] Non-uniform scale — separate factors for X and Y axes. Destroys
    // isotropy; circles become ellipses. Rarely used outside of test fixtures.
    void scale(double factor_x, double factor_y);

    // [INTENT] Translate by a floating-point offset. Converts to coord_t first
    // (truncating, not rounding — see Point(double,double) constructor semantics).
    // [HAZARD H612] The inline form uses coord_t(x) which TRUNCATES, not rounds.
    // This differs from the scaling path which rounds. Sub-unit placement error
    // is possible for non-integer mm values when SCALING_FACTOR = 1e6.
    void translate(double x, double y) { this->translate(Point(coord_t(x), coord_t(y))); }
    void translate(const Point& vector);

    // [INTENT] In-place rotation about the origin or about a center point.
    // Delegates to Polygon::rotate() which uses cos/sin snapping (see H608).
    void rotate(double angle);
    void rotate(double angle, const Point& center);

    // [INTENT] Signed area of the ExPolygon: contour.area() minus the (positive)
    // absolute areas of all holes. Returns a positive double for a valid
    // (non-degenerate, properly wound) ExPolygon.
    // [HAZARD H602] Implementation uses a -= -hole.area(). Hole areas are
    // negative (CW winding → shoelace gives negative value). Double negation
    // converts negative to positive, then subtracts — mathematically correct
    // but confusing. See ExPolygon.cpp::area().
    double area() const;

    // [INTENT] Returns true if contour is empty (no points). Does NOT check holes.
    bool empty() const { return contour.points.empty(); }

    // [INTENT] Validates winding convention: contour must be CCW and each hole
    // must be CW (via Polygon::is_counter_clockwise() which uses Clipper).
    // Also checks that each polygon has >= 3 points (via Polygon::is_valid()).
    bool is_valid() const;

    // [INTENT] In-place Douglas-Peucker simplification with given tolerance (mm,
    // unscaled). Delegates to Polygon::douglas_peucker on each sub-polygon.
    // [HAZARD H613] D-P on individual contour/holes independently — topological
    // consistency between contour and holes is NOT guaranteed post-simplification.
    // A hole edge could become coincident with the contour after simplification.
    void douglas_peucker(double tolerance);

    // [INTENT] Test whether this ExPolygon COMPLETELY contains a Line, Polyline,
    // or Polylines. "Contains" means no part of the argument lies outside.
    // Implementation uses diff_pl() (Clipper-based polyline difference) — the
    // argument is contained iff the Clipper difference is empty.
    // [HAZARD H614] diff_pl uses Clipper for containment; touches at boundary are
    // subject to Clipper's open-boundary conventions (see overlaps() comment).
    // contains(Line) wraps to contains(Polyline(a,b)) — one extra allocation.
    bool contains(const Line& line) const;
    bool contains(const Polyline& polyline) const;
    bool contains(const Polylines& polylines) const;

    // [INTENT] Point-in-ExPolygon test. Returns true if point is inside the
    // contour AND not inside any hole. border_result controls whether points
    // exactly on the boundary count as "inside":
    //   border_result=true  → boundary points ARE inside (default)
    //   border_result=false → boundary points are NOT inside
    // [HAZARD H615] The hole test inverts border_result: a point on a hole
    // boundary with border_result=true is treated as NOT inside the hole (so
    // the point is still considered "contained" in the ExPolygon). This is the
    // intended semantic but can surprise callers reasoning about boundary cases.
    bool contains(const Point& point, bool border_result = true) const;

    // [INTENT] Approximate boundary test: returns true if point lies within
    // eps (in scaled coord_t units) of any edge of any contour or hole.
    // Approximate on boundary test.
    bool on_boundary(const Point& point, double eps) const;

    // [INTENT] Returns the closest point on any boundary edge (contour or hole)
    // to the given query point. When there are no holes, delegates directly to
    // contour.point_projection(); with holes, iterates all contours and picks
    // the globally closest projected point.
    // [COUPLING] Uses contour_or_hole() unified iteration, squared distances.
    // Projection of a point onto the polygon.
    Point point_projection(const Point& point) const;

    // [INTENT] Reflect all points through the horizontal line y = y_axis.
    // Delegates to Polygon::symmetric_y on each sub-polygon. BBS addition.
    void symmetric_y(const coord_t& y_axis);

    // [INTENT] Test whether this ExPolygon overlaps another. Either the
    // ExPolygons intersect, or one is fully inside the other and it is not
    // inside a hole of the other expolygon.
    //
    // [HAZARD H616] The test may NOT be commutative in edge cases. From the
    // comment: expolygons touching at a vertical boundary ARE considered
    // overlapping, while those touching at a horizontal boundary are NOT.
    // This asymmetry comes from Clipper's open-boundary handling for horizontal
    // edges. See unit test SCENARIO("Clipper diff with polyline", "[Clipper]").
    //
    // [COUPLING] Uses intersection_pl(to_polylines(other), *this) — converts
    // other to polylines then runs a Clipper intersection. Then falls back to
    // other.contains(this->contour.points.front()) to detect the case where
    // *this is fully inside other.
    // Does this expolygon overlap another expolygon?
    // Either the ExPolygons intersect, or one is fully inside the other,
    // and it is not inside a hole of the other expolygon.
    // The test may not be commutative if the two expolygons touch by a boundary only,
    // see unit test SCENARIO("Clipper diff with polyline", "[Clipper]").
    // Namely expolygons touching at a vertical boundary are considered overlapping, while expolygons touching
    // at a horizontal boundary are NOT considered overlapping.
    bool overlaps(const ExPolygon& other) const;

    // [INTENT] Two-stage simplification pipeline:
    //   simplify_p(): Douglas-Peucker → Clipper simplify_polygons() → Polygons
    //   simplify():   simplify_p() → union_ex() → ExPolygons
    // The union_ex() step reestablishes hole topology after D-P may have created
    // self-intersections.
    // [HAZARD H617] simplify_p() temporarily adds a duplicate closing point
    // before calling D-P, then removes it. If D-P reduces a contour to < 3 points,
    // simplify_polygons() will filter it out. Empty result is valid but callers
    // must handle it.
    void       simplify_p(double tolerance, Polygons* polygons) const;
    Polygons   simplify_p(double tolerance) const;
    ExPolygons simplify(double tolerance) const;
    void       simplify(double tolerance, ExPolygons* expolygons) const;

    // [INTENT] Extract the medial axis (skeleton) of this ExPolygon as a set of
    // ThickPolylines (each segment carries a width derived from the Voronoi
    // diagram). Also available as plain Polylines (width discarded).
    //
    // [COUPLING] Delegates to Geometry::MedialAxis which builds a Voronoi diagram
    // using Boost.Polygon Voronoi, then filters and traces edges.
    //
    // [STATE] Post-processing in medial_axis(ThickPolylines*):
    //   1. Extend open endpoints to the contour boundary.
    //   2. Remove short polylines (< max_w*2) — removes slivers.
    //   3. If any were removed: greedily reconnect consecutive polylines sharing
    //      endpoints (enables loop detection downstream).
    //
    // [HAZARD H618] The endpoint extension intersects with this->contour only
    // (not holes). If the ExPolygon is concave, the extension may overshoot.
    // [HAZARD H619] The greedy reconnection in step 3 connects random pairs when
    // more than two polylines share a point. See code comment: "This has no
    // drawbacks since we optimize later using nearest-neighbor." If a more
    // sophisticated optimizer is used, this assumption may not hold.
    void      medial_axis(double min_width, double max_width, ThickPolylines* polylines) const;
    void      medial_axis(double min_width, double max_width, Polylines* polylines) const;
    Polylines medial_axis(double min_width, double max_width) const
    {
        Polylines out;
        this->medial_axis(min_width, max_width, &out);
        return out;
    }

    // [INTENT] Returns all edges of all contours (contour + holes) as a flat
    // vector of Lines. Order: contour edges first, then each hole in order.
    Lines lines() const;

    // [INTENT] Unified accessor for all sub-polygons by index:
    //   idx == 0         → contour
    //   idx >= 1         → holes[idx - 1]
    // Used by point_projection() and projection_onto() to iterate without
    // special-casing contour vs holes.
    // Number of contours (outer contour with holes).
    size_t         num_contours() const { return this->holes.size() + 1; }
    Polygon&       contour_or_hole(size_t idx) { return (idx == 0) ? this->contour : this->holes[idx - 1]; }
    const Polygon& contour_or_hole(size_t idx) const { return (idx == 0) ? this->contour : this->holes[idx - 1]; }
};

// [INTENT] Equality operators: deep compare contour and all holes (order-sensitive).
inline bool operator==(const ExPolygon& lhs, const ExPolygon& rhs) { return lhs.contour == rhs.contour && lhs.holes == rhs.holes; }
inline bool operator!=(const ExPolygon& lhs, const ExPolygon& rhs) { return lhs.contour != rhs.contour || lhs.holes != rhs.holes; }

// [INTENT] count_points(): total number of vertices in an ExPolygon or vector
// of ExPolygons. Used to reserve output containers before conversion loops.
// O(n expolygons) for the vector form.
inline size_t count_points(const ExPolygons& expolys)
{
    size_t n_points = 0;
    for (const auto& expoly : expolys) {
        n_points += expoly.contour.points.size();
        for (const auto& hole : expoly.holes)
            n_points += hole.points.size();
    }
    return n_points;
}

inline size_t count_points(const ExPolygon& expoly)
{
    size_t n_points = expoly.contour.points.size();
    for (const auto& hole : expoly.holes)
        n_points += hole.points.size();
    return n_points;
}

// [INTENT] number_polygons(): total number of Polygon objects (1 per contour +
// 1 per hole) across all ExPolygons. Used to pre-allocate flat Polygons vectors
// when flattening ExPolygons → Polygons.
// Count a nuber of polygons stored inside the vector of expolygons.
// Useful for allocating space for polygons when converting expolygons to polygons.
inline size_t number_polygons(const ExPolygons& expolys)
{
    size_t n_polygons = 0;
    for (const ExPolygon& ex : expolys)
        n_polygons += ex.holes.size() + 1;
    return n_polygons;
}

// [INTENT] to_lines(): convert an ExPolygon or ExPolygons to a flat vector of
// Line segments (one per edge, closing edge included). Order: contour edges
// first, then each hole's edges in order.
// [HAZARD H620] to_lines(ExPolygons): inner loop uses count_points(src) to
// reserve, which is O(n) — fine. But the loop body has a closing edge appended
// outside the inner iterator loop, which is correct. Callers must not assume
// any particular ordering of lines across multiple ExPolygons.
inline Lines to_lines(const ExPolygon& src)
{
    Lines lines;
    lines.reserve(count_points(src));
    for (size_t i = 0; i <= src.holes.size(); ++i) {
        const Polygon& poly = (i == 0) ? src.contour : src.holes[i - 1];
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end() - 1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
        lines.push_back(Line(poly.points.back(), poly.points.front()));
    }
    return lines;
}

inline Lines to_lines(const ExPolygons& src)
{
    Lines lines;
    lines.reserve(count_points(src));
    for (ExPolygons::const_iterator it_expoly = src.begin(); it_expoly != src.end(); ++it_expoly) {
        for (size_t i = 0; i <= it_expoly->holes.size(); ++i) {
            const Points& points = ((i == 0) ? it_expoly->contour : it_expoly->holes[i - 1]).points;
            for (Points::const_iterator it = points.begin(); it != points.end() - 1; ++it)
                lines.push_back(Line(*it, *(it + 1)));
            lines.push_back(Line(points.back(), points.front()));
        }
    }
    return lines;
}

// [INTENT] to_linesf(): same as to_lines() but produces Linesf (double-precision
// floating-point lines) in SCALED units (no conversion to mm). The count_lines
// parameter allows the caller to pass a pre-computed count to avoid a second
// traversal; passing 0 triggers an internal count_points() call.
// [HAZARD H621] to_linesf uses a shared prev_pd variable in the lambda across
// multiple polygons — the lambda must be called with pts of size >= 2 or it
// returns early, leaving prev_pd stale. The assertion pts.size() >= 3 is debug-
// only; release builds silently skip short polygons.
// Line is from point index(see to_points) to next point.
// Next point of last point in polygon is first polygon point.
inline Linesf to_linesf(const ExPolygons& src, uint32_t count_lines = 0)
{
    assert(count_lines == 0 || count_lines == count_points(src));
    if (count_lines == 0)
        count_lines = count_points(src);
    Linesf lines;
    lines.reserve(count_lines);
    Vec2d prev_pd;
    auto  to_lines = [&lines, &prev_pd](const Points& pts) {
        assert(pts.size() >= 3);
        if (pts.size() < 2)
            return;
        bool is_first = true;
        for (const Point& p : pts) {
            Vec2d pd = p.cast<double>();
            if (is_first)
                is_first = false;
            else
                lines.emplace_back(prev_pd, pd);
            prev_pd = pd;
        }
        lines.emplace_back(prev_pd, pts.front().cast<double>());
    };
    for (const ExPolygon& expoly : src) {
        to_lines(expoly.contour.points);
        for (const Polygon& hole : expoly.holes)
            to_lines(hole.points);
    }
    assert(lines.size() == count_lines);
    return lines;
}

// [INTENT] to_unscaled_linesf(): convert ExPolygons to double-precision lines
// in MILLIMETER space (unscaled). Applies unscaled() to each vertex.
// Uses separate unscaled_a / unscaled_b variables — no prev_pd shared state
// hazard (contrast with to_linesf above).
inline Linesf to_unscaled_linesf(const ExPolygons& src)
{
    Linesf lines;
    lines.reserve(count_points(src));
    for (ExPolygons::const_iterator it_expoly = src.begin(); it_expoly != src.end(); ++it_expoly) {
        for (size_t i = 0; i <= it_expoly->holes.size(); ++i) {
            const Points& points     = ((i == 0) ? it_expoly->contour : it_expoly->holes[i - 1]).points;
            Vec2d         unscaled_a = unscaled(points.front());
            Vec2d         unscaled_b = unscaled_a;
            for (Points::const_iterator it = points.begin() + 1; it != points.end(); ++it) {
                unscaled_b = unscaled(*(it));
                lines.push_back(Linef(unscaled_a, unscaled_b));
                unscaled_a = unscaled_b;
            }
            lines.push_back(Linef(unscaled_a, unscaled(points.front())));
        }
    }
    return lines;
}

// [INTENT] to_points(): flatten all vertices of ExPolygons into a single
// Points vector. Order: for each ExPolygon, contour points first, then each
// hole's points in order. No closing points added — each polygon is represented
// by its n distinct vertices.
inline Points to_points(const ExPolygons& src)
{
    Points points;
    size_t count = count_points(src);
    points.reserve(count);
    for (const ExPolygon& expolygon : src) {
        append(points, expolygon.contour.points);
        for (const Polygon& hole : expolygon.holes)
            append(points, hole.points);
    }
    return points;
}

// [INTENT] to_polylines(): convert each sub-polygon (contour or hole) of an
// ExPolygon or ExPolygons into an open Polyline. The closing point (= first
// point) is explicitly appended so that the Polyline represents a full loop
// as a chain. The result has (1 + holes.size()) polylines per ExPolygon.
// [HAZARD H622] After std::move of src.contour.points, a push_back of
// pl.points.front() is needed to close the loop. This works because
// pl.points.front() is a copy made BEFORE the move — the move version is
// careful to save the front before moving. Readers should verify this on any
// refactor.
inline Polylines to_polylines(const ExPolygon& src)
{
    Polylines polylines;
    polylines.assign(src.holes.size() + 1, Polyline());
    size_t    idx = 0;
    Polyline& pl  = polylines[idx++];
    pl.points     = src.contour.points;
    pl.points.push_back(pl.points.front());
    for (Polygons::const_iterator ith = src.holes.begin(); ith != src.holes.end(); ++ith) {
        Polyline& pl = polylines[idx++];
        pl.points    = ith->points;
        pl.points.push_back(ith->points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polylines to_polylines(const ExPolygons& src)
{
    Polylines polylines;
    polylines.assign(number_polygons(src), Polyline());
    size_t idx = 0;
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        Polyline& pl = polylines[idx++];
        pl.points    = it->contour.points;
        pl.points.push_back(pl.points.front());
        for (Polygons::const_iterator ith = it->holes.begin(); ith != it->holes.end(); ++ith) {
            Polyline& pl = polylines[idx++];
            pl.points    = ith->points;
            pl.points.push_back(ith->points.front());
        }
    }
    assert(idx == polylines.size());
    return polylines;
}

// [INTENT] Move variants of to_polylines() — steal points from source to avoid
// copying. Safe: pl.points.front() is captured by value before the move.
// [HAZARD H622 continued] In the rvalue overloads, pl.points.front() is read
// AFTER the move from src.contour.points (or ith->points). After std::move on
// a vector, the source is in a valid but unspecified state — front() would be
// undefined. However the pattern used is:
//   pl.points = std::move(src.contour.points);   // pl.points now has data
//   pl.points.push_back(pl.points.front());      // front() from pl, not src
// This is safe. Future maintainers must not reorder these lines.
inline Polylines to_polylines(ExPolygon&& src)
{
    Polylines polylines;
    polylines.assign(src.holes.size() + 1, Polyline());
    size_t    idx = 0;
    Polyline& pl  = polylines[idx++];
    pl.points     = std::move(src.contour.points);
    pl.points.push_back(pl.points.front());
    for (auto ith = src.holes.begin(); ith != src.holes.end(); ++ith) {
        Polyline& pl = polylines[idx++];
        pl.points    = std::move(ith->points);
        pl.points.push_back(pl.points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polylines to_polylines(ExPolygons&& src)
{
    Polylines polylines;
    polylines.assign(number_polygons(src), Polyline());
    size_t idx = 0;
    for (auto it = src.begin(); it != src.end(); ++it) {
        Polyline& pl = polylines[idx++];
        pl.points    = std::move(it->contour.points);
        pl.points.push_back(pl.points.front());
        for (auto ith = it->holes.begin(); ith != it->holes.end(); ++ith) {
            Polyline& pl = polylines[idx++];
            pl.points    = std::move(ith->points);
            pl.points.push_back(pl.points.front());
        }
    }
    assert(idx == polylines.size());
    return polylines;
}

// [INTENT] to_polygons(): flatten an ExPolygon or ExPolygons into a flat
// Polygons vector. Order: contour first, then holes, repeated for each
// ExPolygon. Winding is preserved (contour=CCW, holes=CW).
// Move variants steal ownership to avoid copying large point vectors.
// [COUPLING] Used extensively when passing ExPolygon regions to Clipper
// operations that accept plain Polygons (no hole distinction).
inline Polygons to_polygons(const ExPolygon& src)
{
    Polygons polygons;
    polygons.reserve(src.holes.size() + 1);
    polygons.push_back(src.contour);
    polygons.insert(polygons.end(), src.holes.begin(), src.holes.end());
    return polygons;
}

inline Polygons to_polygons(const ExPolygons& src)
{
    Polygons polygons;
    polygons.reserve(number_polygons(src));
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        polygons.push_back(it->contour);
        polygons.insert(polygons.end(), it->holes.begin(), it->holes.end());
    }
    return polygons;
}

// [INTENT] to_polygon_ptrs(): non-owning view of all sub-polygons in an
// ExPolygon or ExPolygons as ConstPolygonPtrs (vector of const Polygon*).
// Avoids copying — useful for read-only iteration over all rings.
// [HAZARD H623] Pointers are invalidated if the source ExPolygon is moved,
// resized, or destroyed. Only safe for short-lived iteration.
inline ConstPolygonPtrs to_polygon_ptrs(const ExPolygon& src)
{
    ConstPolygonPtrs polygons;
    polygons.reserve(src.holes.size() + 1);
    polygons.emplace_back(&src.contour);
    for (const Polygon& hole : src.holes)
        polygons.emplace_back(&hole);
    return polygons;
}

inline ConstPolygonPtrs to_polygon_ptrs(const ExPolygons& src)
{
    ConstPolygonPtrs polygons;
    polygons.reserve(number_polygons(src));
    for (const ExPolygon& expoly : src) {
        polygons.emplace_back(&expoly.contour);
        for (const Polygon& hole : expoly.holes)
            polygons.emplace_back(&hole);
    }
    return polygons;
}

inline Polygons to_polygons(ExPolygon&& src)
{
    Polygons polygons;
    polygons.reserve(src.holes.size() + 1);
    polygons.push_back(std::move(src.contour));
    polygons.insert(polygons.end(), std::make_move_iterator(src.holes.begin()), std::make_move_iterator(src.holes.end()));
    return polygons;
}

inline Polygons to_polygons(ExPolygons&& src)
{
    Polygons polygons;
    polygons.reserve(number_polygons(src));
    for (ExPolygon& expoly : src) {
        polygons.push_back(std::move(expoly.contour));
        polygons.insert(polygons.end(), std::make_move_iterator(expoly.holes.begin()), std::make_move_iterator(expoly.holes.end()));
    }
    return polygons;
}

// [INTENT] to_expolygons(): wrap each Polygon in its own ExPolygon with no
// holes. This is a one-way lossy operation: holes from the original context
// (if any) are not reconstructed. Used when Clipper returns a flat Polygons
// result and the caller wants an ExPolygons container.
// [HAZARD H624] to_expolygons(Polygons) does NOT run Clipper union to resolve
// hole-contour topology. The resulting ExPolygons may have overlapping contours.
// Callers requiring correct topology should use union_ex() instead.
inline ExPolygons to_expolygons(const Polygons& polys)
{
    ExPolygons ex_polys;
    ex_polys.assign(polys.size(), ExPolygon());
    for (size_t idx = 0; idx < polys.size(); ++idx)
        ex_polys[idx].contour = polys[idx];
    return ex_polys;
}

inline ExPolygons to_expolygons(Polygons&& polys)
{
    ExPolygons ex_polys;
    ex_polys.assign(polys.size(), ExPolygon());
    for (size_t idx = 0; idx < polys.size(); ++idx)
        ex_polys[idx].contour = std::move(polys[idx]);
    return ex_polys;
}

// [INTENT] to_points(ExPolygon): flatten all vertices of a single ExPolygon
// into a Points vector. Contour points first, then holes. No duplicates added.
inline Points to_points(const ExPolygon& expoly)
{
    Points out;
    out.reserve(count_points(expoly));
    append(out, expoly.contour.points);
    for (const Polygon& hole : expoly.holes)
        append(out, hole.points);
    return out;
}

// [INTENT] translate(): in-place translate a collection of ExPolygons.
// Thin wrapper around ExPolygon::translate(Point).
inline void translate(ExPolygons& expolys, const Point& p)
{
    for (ExPolygon& expoly : expolys)
        expoly.translate(p);
}

// [INTENT] polygons_append(): append all sub-polygons (contour + holes) of an
// ExPolygon or ExPolygons to an existing Polygons vector, without creating a
// temporary. All four overloads (const&, &&, const ExPolygons&, ExPolygons&&)
// allow efficient composition. Reserve is called once per call.
inline void polygons_append(Polygons& dst, const ExPolygon& src)
{
    dst.reserve(dst.size() + src.holes.size() + 1);
    dst.push_back(src.contour);
    dst.insert(dst.end(), src.holes.begin(), src.holes.end());
}

inline void polygons_append(Polygons& dst, const ExPolygons& src)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        dst.push_back(it->contour);
        dst.insert(dst.end(), it->holes.begin(), it->holes.end());
    }
}

inline void polygons_append(Polygons& dst, ExPolygon&& src)
{
    dst.reserve(dst.size() + src.holes.size() + 1);
    dst.push_back(std::move(src.contour));
    dst.insert(dst.end(), std::make_move_iterator(src.holes.begin()), std::make_move_iterator(src.holes.end()));
}

inline void polygons_append(Polygons& dst, ExPolygons&& src)
{
    dst.reserve(dst.size() + number_polygons(src));
    for (ExPolygon& expoly : src) {
        dst.push_back(std::move(expoly.contour));
        dst.insert(dst.end(), std::make_move_iterator(expoly.holes.begin()), std::make_move_iterator(expoly.holes.end()));
    }
}

// [INTENT] expolygons_append(): append ExPolygons to an existing ExPolygons
// vector. The move form optimizes the common case where dst is empty by simply
// reassigning instead of inserting, avoiding a reallocation.
inline void expolygons_append(ExPolygons& dst, const ExPolygons& src) { dst.insert(dst.end(), src.begin(), src.end()); }

inline void expolygons_append(ExPolygons& dst, ExPolygons&& src)
{
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        dst.insert(dst.end(), std::make_move_iterator(src.begin()), std::make_move_iterator(src.end()));
    }
}

// [INTENT] expolygons_rotate(): rotate all ExPolygons in-place by angle radians
// about the origin. Thin loop wrapper.
inline void expolygons_rotate(ExPolygons& expolys, double angle)
{
    for (ExPolygon& expoly : expolys)
        expoly.rotate(angle);
}

// [INTENT] expolygons_contain(): returns true if any ExPolygon in the collection
// contains the point. Linear scan — O(n * m) where m = avg polygon complexity.
// border_result forwarded to ExPolygon::contains().
inline bool expolygons_contain(ExPolygons& expolys, const Point& pt, bool border_result = true)
{
    for (const ExPolygon& expoly : expolys)
        if (expoly.contains(pt, border_result))
            return true;
    return false;
}

// [INTENT] expolygons_simplify(): apply ExPolygon::simplify(tolerance) to each
// element and accumulate results. Because simplify() may split one ExPolygon
// into multiple (via union_ex), the output may have more elements than the input.
inline ExPolygons expolygons_simplify(const ExPolygons& expolys, double tolerance)
{
    ExPolygons out;
    out.reserve(expolys.size());
    for (const ExPolygon& exp : expolys)
        exp.simplify(tolerance, &out);
    return out;
}

// [INTENT] expolygons_match(): topology-level equality check. Two ExPolygons
// match if their contours and holes (in order) are rotationally equivalent
// (see polygons_match()). Holes must appear in the same order. A permutation
// of holes would NOT match even if geometrically identical.
// [COUPLING] Delegates to polygons_match() (Polygon.hpp) for each sub-polygon.
// Do expolygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool expolygons_match(const ExPolygon& l, const ExPolygon& r);

// [INTENT] overlaps(ExPolygons, ExPolygons): O(n*m) pairwise overlap test
// across two ExPolygons collections. Short-circuits on first overlap found.
// [HAZARD H616 continued] Asymmetric boundary handling in ExPolygon::overlaps()
// applies here too.
bool overlaps(const ExPolygons& expolys1, const ExPolygons& expolys2);
bool overlaps(const ExPolygons& expolys, const ExPolygon& expoly);

// [INTENT] projection_onto(): find the closest point on any boundary of any
// ExPolygon in the collection to a query point. Returns a single Point.
// Linear scan over all contours and holes.
Point projection_onto(const ExPolygons& expolys, const Point& pt);

// [INTENT] get_extents(): bounding box of an ExPolygon or ExPolygons.
// For a single ExPolygon, only the contour bbox is computed (holes are
// entirely inside the contour, so contour bbox == ExPolygon bbox).
// For ExPolygons, unions bboxes of all non-empty contours.
// [HAZARD H625] get_extents(ExPolygon) ignores holes. Since holes are
// geometrically inside the contour this is always correct. But callers
// should not pass an ExPolygon whose contour is empty but has non-empty
// holes — the result would be an undefined BoundingBox.
BoundingBox get_extents(const ExPolygon& expolygon);
BoundingBox get_extents(const ExPolygons& expolygons);

// [INTENT] get_extents_rotated(): bounding box after rotating by `angle`.
// Only uses the contour (holes stay inside). Delegates to
// get_extents_rotated(Polygon, angle) which rotates the polygon first.
BoundingBox get_extents_rotated(const ExPolygon& poly, double angle);
BoundingBox get_extents_rotated(const ExPolygons& polygons, double angle);

// [INTENT] get_extents_vector(): one BoundingBox per ExPolygon (contour only).
// Used for spatial indexing / binning of ExPolygon collections.
std::vector<BoundingBox> get_extents_vector(const ExPolygons& polygons);

// [INTENT] has_duplicate_points(): sort-based global duplicate detection.
// The active (#if 1) path flattens ALL points (contour + all holes) into one
// vector and checks globally — a point shared between the contour and a hole
// boundary would be reported as a duplicate even if each polygon is locally
// valid.
// The inactive (#else) path checks per-contour.
// [HAZARD H626] The global check may produce false positives if a contour
// vertex coincidentally equals a hole vertex. This is geometrically degenerate
// but not impossible. The per-contour path (commented out) would NOT report
// this as a duplicate. Callers relying on "no global duplicates" may be more
// strict than necessary.
// Test for duplicate points. The points are copied, sorted and checked for duplicates globally.
bool has_duplicate_points(const ExPolygon& expoly);
bool has_duplicate_points(const ExPolygons& expolys);

// [INTENT] remove_same_neighbor(): remove consecutive duplicate points from
// all contours and holes of all ExPolygons. After removal, ExPolygons whose
// contour has <= 2 points are erased entirely. Returns true if any removal
// occurred.
// [COUPLING] Delegates to remove_same_neighbor(Polygon) and
// remove_same_neighbor(Polygons) from Polygon.hpp.
// Return True when erase some otherwise False.
bool remove_same_neighbor(ExPolygons& expolys);

// [INTENT] remove_sticks(): remove collinear/degenerate edges from an
// ExPolygon. Delegates to Polygon-level remove_sticks for contour and holes.
bool remove_sticks(ExPolygon& poly);

// [INTENT] keep_largest_contour_only(): retain only the ExPolygon with the
// largest contour area (by Polygon::area(), which uses shoelace). Clears and
// replaces the input vector. Used after complex boolean ops to discard slivers.
// [HAZARD H627] Comparison uses raw Polygon::area() (shoelace, positive for
// CCW). If any polygon has CW winding, its area() is negative and it will
// never be selected as the maximum — even if it's geometrically the largest.
void keep_largest_contour_only(ExPolygons& polygons);

// [INTENT] area() free functions: delegate to ExPolygon::area() for a single
// polygon, or accumulate the sum for a collection.
inline double area(const ExPolygon& poly) { return poly.area(); }
inline double area(const ExPolygons& polys)
{
    double s = 0.;
    for (auto& p : polys)
        s += p.area();
    return s;
}

// [INTENT] remove_small_and_small_holes(): two-pass filter:
//   1. Remove ExPolygons whose total area < min_area.
//   2. For remaining ExPolygons, remove holes whose area < min_area.
// Returns true if any modification was made.
// [HAZARD H628] Uses std::abs(area()) for ExPolygon comparison — handles
// negative areas (wrongly wound contours) gracefully. Hole removal delegates
// to remove_small(holes, min_area) which also uses abs.
// Removes all expolygons smaller than min_area and also removes all holes smaller than min_area
bool remove_small_and_small_holes(ExPolygons& expolygons, double min_area);

} // namespace Slic3r

// start Boost
// [INTENT] Boost.Polygon trait specializations for ExPolygon and ExPolygons.
// These allow ExPolygon objects to participate in Boost.Polygon boolean and
// spatial operations (e.g., polygon_set operations, offset).
//
// [COUPLING] Five specializations:
//   polygon_traits<ExPolygon>              — read-only contour point access
//   polygon_mutable_traits<ExPolygon>      — write contour points from iterator
//   geometry_concept<ExPolygon>            — marks as polygon_with_holes_concept
//   polygon_with_holes_traits<ExPolygon>   — read-only hole access
//   polygon_with_holes_mutable_traits<ExPolygon> — write holes from iterator
//   geometry_concept<ExPolygons>           — marks as polygon_set_concept
//   polygon_set_traits<ExPolygons>         — ExPolygons as a polygon set
//   polygon_set_mutable_traits<ExPolygons> — write polygon set from iterator
//
// [HAZARD H609 continued] polygon_traits exposes the contour as the "polygon"
// face; the winding direction is reported as unknown_winding — Boost will not
// enforce our CCW convention. Any Boost operation that creates ExPolygons via
// polygon_mutable_traits::set_points() will pop_back the duplicate closing
// point, preserving our open-polygon convention.
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
template<> struct polygon_traits<Slic3r::ExPolygon>
{
    typedef coord_t                        coordinate_type;
    typedef Slic3r::Points::const_iterator iterator_type;
    typedef Slic3r::Point                  point_type;

    // Get the begin iterator
    static inline iterator_type begin_points(const Slic3r::ExPolygon& t) { return t.contour.points.begin(); }

    // Get the end iterator
    static inline iterator_type end_points(const Slic3r::ExPolygon& t) { return t.contour.points.end(); }

    // Get the number of sides of the polygon
    static inline std::size_t size(const Slic3r::ExPolygon& t) { return t.contour.points.size(); }

    // Get the winding direction of the polygon
    // [HAZARD H609] Returns unknown_winding — Boost does not enforce CCW.
    static inline winding_direction winding(const Slic3r::ExPolygon& /* t */) { return unknown_winding; }
};

template<> struct polygon_mutable_traits<Slic3r::ExPolygon>
{
    // expects stl style iterators
    //  [INTENT] Boost passes n+1 points (closing point = first point).
    //  pop_back() removes the duplicate to maintain our open-polygon convention.
    template<typename iT> static inline Slic3r::ExPolygon& set_points(Slic3r::ExPolygon& expolygon, iT input_begin, iT input_end)
    {
        expolygon.contour.points.assign(input_begin, input_end);
        // skip last point since Boost will set last point = first point
        expolygon.contour.points.pop_back();
        return expolygon;
    }
};

// [INTENT] Mark ExPolygon as polygon_with_holes_concept so Boost uses the
// polygon_with_holes_traits for hole access.
template<> struct geometry_concept<Slic3r::ExPolygon>
{
    typedef polygon_with_holes_concept type;
};

template<> struct polygon_with_holes_traits<Slic3r::ExPolygon>
{
    typedef Slic3r::Polygons::const_iterator iterator_holes_type;
    typedef Slic3r::Polygon                  hole_type;
    static inline iterator_holes_type        begin_holes(const Slic3r::ExPolygon& t) { return t.holes.begin(); }
    static inline iterator_holes_type        end_holes(const Slic3r::ExPolygon& t) { return t.holes.end(); }
    static inline unsigned int               size_holes(const Slic3r::ExPolygon& t) { return (int) t.holes.size(); }
};

template<> struct polygon_with_holes_mutable_traits<Slic3r::ExPolygon>
{
    template<typename iT> static inline Slic3r::ExPolygon& set_holes(Slic3r::ExPolygon& t, iT inputBegin, iT inputEnd)
    {
        t.holes.assign(inputBegin, inputEnd);
        return t;
    }
};

// [INTENT] ExPolygons is registered as a Boost.Polygon polygon set — a
// collection of polygon_with_holes objects. This enables ExPolygons to be
// passed directly to Boost polygon set operations.
// first we register CPolygonSet as a polygon set
template<> struct geometry_concept<Slic3r::ExPolygons>
{
    typedef polygon_set_concept type;
};

// next we map to the concept through traits
template<> struct polygon_set_traits<Slic3r::ExPolygons>
{
    typedef coord_t                            coordinate_type;
    typedef Slic3r::ExPolygons::const_iterator iterator_type;
    typedef Slic3r::ExPolygons                 operator_arg_type;

    static inline iterator_type begin(const Slic3r::ExPolygons& polygon_set) { return polygon_set.begin(); }

    static inline iterator_type end(const Slic3r::ExPolygons& polygon_set) { return polygon_set.end(); }

    // don't worry about these, just return false from them
    static inline bool clean(const Slic3r::ExPolygons& /* polygon_set */) { return false; }
    static inline bool sorted(const Slic3r::ExPolygons& /* polygon_set */) { return false; }
};

template<> struct polygon_set_mutable_traits<Slic3r::ExPolygons>
{
    template<typename input_iterator_type>
    static inline void set(Slic3r::ExPolygons& expolygons, input_iterator_type input_begin, input_iterator_type input_end)
    {
        expolygons.assign(input_begin, input_end);
    }
};
}} // namespace boost::polygon
// end Boost

#endif
