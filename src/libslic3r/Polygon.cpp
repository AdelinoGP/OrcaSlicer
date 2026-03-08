// [INTENT] Implements closed-polygon operations: perimeter length, line
//          extraction, splitting, area, winding, simplification, triangulation,
//          centroid, intersection/overlap tests, convex/concave vertex filtering,
//          densification, cleanup helpers (sticks, degenerate, collinear),
//          and circle generation.
// [COUPLING] Depends on ClipperLib for orientation, point-in-polygon, and
//            polygon simplification. Depends on MultiPoint::_douglas_peucker
//            for vertex reduction. Depends on BoundingBox for get_extents.
// [STATE] All methods operate on the inherited MultiPoint::points vector
//         (scaled integer coordinates, coord_t = int32_t).
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include <cmath>

namespace Slic3r {

// [INTENT] Compute the total perimeter length of this closed polygon.
//          Includes the closing edge from back to front.
// [HAZARD] H600-adjacent: coordinates are scaled integers; result is in scaled
//          units, not mm. Divide by SCALING_FACTOR for mm.
// [MEMORY] Returns double (stack). No allocation.
double Polygon::length() const
{
    double l = 0;
    if (this->points.size() > 1) {
        // [INTENT] Closing edge: from last point back to first point.
        l = (this->points.back() - this->points.front()).cast<double>().norm();
        for (size_t i = 1; i < this->points.size(); ++i)
            l += (this->points[i] - this->points[i - 1]).cast<double>().norm();
    }
    return l;
}

// [INTENT] Convert the closed polygon to a vector of Line segments, including
//          the implicit closing edge from back to front.
// [COUPLING] Delegates to free function to_lines(*this) defined in Polyline/MultiPoint.
Lines Polygon::lines() const { return to_lines(*this); }

// [INTENT] Split a closed polygon into an open Polyline starting at the given
//          Point, searching by value equality.
// [HAZARD] Throws Slic3r::InvalidArgument if the point is not found — caller
//          must ensure the point is an exact member of points[]. Even one
//          scaled-integer ULP difference causes a throw.
// [STATE] Returns a new Polyline with the split point duplicated at both ends
//         (split_at_index semantics).
Polyline Polygon::split_at_vertex(const Point& point) const
{
    // find index of point
    for (const Point& pt : this->points)
        if (pt == point)
            return this->split_at_index(int(&pt - &this->points.front()));
    throw Slic3r::InvalidArgument("Point not found");
    return Polyline();
}

// [INTENT] Split a closed polygon into an open Polyline at the given index,
//          wrapping around: output is points[index..end] + points[0..index],
//          with index duplicated at both ends (size = n+1).
// [MEMORY] Allocates a new Polyline of size n+1.
// Split a closed polygon into an open polyline, with the split point duplicated at both ends.
Polyline Polygon::split_at_index(int index) const
{
    Polyline polyline;
    polyline.points.reserve(this->points.size() + 1);
    for (Points::const_iterator it = this->points.begin() + index; it != this->points.end(); ++it)
        polyline.points.push_back(*it);
    for (Points::const_iterator it = this->points.begin(); it != this->points.begin() + index + 1; ++it)
        polyline.points.push_back(*it);
    return polyline;
}

// [INTENT] Static helper: compute the signed area of an arbitrary Points
//          sequence using the shoelace formula.
// [HAZARD] H600: Returns NEGATIVE for CW polygons. Callers needing unsigned
//          area must call std::abs(). Sign is used by make_counter_clockwise /
//          make_clockwise and triangulate_convex (area > 0 guard).
// [CONCURRENCY] Stateless pure function; thread-safe.
double Polygon::area(const Points& points)
{
    double a = 0.;
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point& p : points) {
            Vec2d p2 = p.cast<double>();
            // [INTENT] Shoelace: accumulate sum of cross products of consecutive vertices.
            a += cross2(p1, p2);
            p1 = p2;
        }
    }
    // [INTENT] Factor 0.5 from shoelace formula. Result is in scaled^2 units.
    return 0.5 * a;
}

// [INTENT] Instance wrapper: delegates to static Polygon::area(points).
double Polygon::area() const { return Polygon::area(points); }

// [INTENT] Return true if the polygon is wound counter-clockwise (positive area).
// [COUPLING] Delegates to ClipperLib::Orientation() — crosses the
//            coord_t↔Clipper integer boundary. Clipper interprets positive Y
//            as downward, so "CCW" here means Clipper-CCW (positive area in
//            screen coordinates). Ensure callers share that convention.
bool Polygon::is_counter_clockwise() const { return ClipperLib::Orientation(this->points); }

// [INTENT] Convenience negation of is_counter_clockwise().
bool Polygon::is_clockwise() const { return !this->is_counter_clockwise(); }

// [INTENT] Ensure the polygon is wound CCW; reverse in-place if not.
//          Returns true if a reversal was performed.
// [STATE] Mutates this->points in place via MultiPoint::reverse().
bool Polygon::make_counter_clockwise()
{
    if (!this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

// [INTENT] Ensure the polygon is wound CW; reverse in-place if not.
//          Returns true if a reversal was performed.
bool Polygon::make_clockwise()
{
    if (this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

// [INTENT] In-place Douglas-Peucker simplification of the polygon.
//          Temporarily appends the first point at the end to treat the closed
//          polygon as an open polyline for D-P, then removes it.
// [HAZARD] Modifies this->points in place. The temporary push_back is
//          undone by pop_back after D-P. If _douglas_peucker throws, the
//          polygon is left with the extra point — caller should not catch
//          and reuse.
// [COUPLING] Uses MultiPoint::_douglas_peucker (static method).
void Polygon::douglas_peucker(double tolerance)
{
    this->points.push_back(this->points.front());
    Points p = MultiPoint::_douglas_peucker(this->points, tolerance);
    p.pop_back();
    this->points = std::move(p);
}

// [INTENT] Simplify this CCW polygon using Douglas-Peucker followed by
//          ClipperLib::SimplifyPolygons to eliminate self-intersections.
// [HAZARD] assert(is_counter_clockwise()): caller MUST pass a CCW polygon.
//          A CW hole passed here will be silently reoriented by Clipper's
//          SimplifyPolygons to positive area, corrupting hole topology.
// [COUPLING] Calls simplify_polygons() (ClipperUtils.hpp). May return
//            multiple polygons if the simplified shape is self-intersecting.
// [MEMORY] Returns a new Polygons vector; does not modify *this.
Polygons Polygon::simplify(double tolerance) const
{
    // Works on CCW polygons only, CW contour will be reoriented to CCW by Clipper's simplify_polygons()!
    assert(this->is_counter_clockwise());

    // repeat first point at the end in order to apply Douglas-Peucker
    // on the whole polygon
    Points points = this->points;
    points.push_back(points.front());
    Polygon p(MultiPoint::_douglas_peucker(points, tolerance));
    p.points.pop_back();

    Polygons pp;
    pp.push_back(p);
    return simplify_polygons(pp);
}

// [INTENT] Fan-triangulate a CONVEX polygon from its first vertex into n-2
//          triangles. Appends triangles to the output vector; only keeps
//          triangles with positive (CCW) area.
// [HAZARD] NAME SAYS IT ALL: only valid on convex polygons. Concave polygons
//          produce overlapping or missing triangles. No convexity check is
//          performed at runtime.
// [COUPLING] Uses Polygon::area() (H600 — sign check "area > 0" is intentional
//            to skip degenerate/CW triangles from numerical noise).
// Only call this on convex polygons or it will return invalid results
void Polygon::triangulate_convex(Polygons* polygons) const
{
    for (Points::const_iterator it = this->points.begin() + 2; it != this->points.end(); ++it) {
        Polygon p;
        p.points.reserve(3);
        p.points.push_back(this->points.front());
        p.points.push_back(*(it - 1));
        p.points.push_back(*it);

        // this should be replaced with a more efficient call to a merge_collinear_segments() method
        // [INTENT] Skip degenerate triangles (area==0 from collinear/duplicate points).
        if (p.area() > 0)
            polygons->push_back(p);
    }
}

// [INTENT] Compute the centroid (center of mass) of a simple polygon using
//          the standard weighted-vertex formula from the shoelace expansion.
// [COUPLING] Uses cross2 helper; depends on points being scaled integers.
// [HAZARD] Result is a Point (rounded from Vec2d). If area_sum == 0 (degenerate
//          polygon), division by zero produces NaN/inf, then the coord_t
//          cast is undefined behavior.
// [STATE] Returns a new Point; does not modify *this.
// center of mass
// source: https://en.wikipedia.org/wiki/Centroid
Point Polygon::centroid() const
{
    double area_sum = 0.;
    Vec2d  c(0., 0.);
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point& p : points) {
            Vec2d  p2 = p.cast<double>();
            double a  = cross2(p1, p2);
            area_sum += a;
            // [INTENT] Each vertex pair contributes (p1+p2)*cross_area to the centroid sum.
            c += (p1 + p2) * a;
            p1 = p2;
        }
    }
    // [INTENT] 3*area_sum = 3*(2*area) from shoelace denominator.
    return Point(Vec2d(c / (3. * area_sum)));
}

// [INTENT] Return the first intersection of the given Line with any edge of
//          this polygon (including the closing edge). Output via pointer.
// [HAZARD] Returns the FIRST match found in iteration order, not the nearest.
//          Use first_intersection() if nearest-to-line-start is needed.
bool Polygon::intersection(const Line& line, Point* intersection) const
{
    if (this->points.size() < 2)
        return false;
    if (Line(this->points.front(), this->points.back()).intersection(line, intersection))
        return true;
    for (size_t i = 1; i < this->points.size(); ++i)
        if (Line(this->points[i - 1], this->points[i]).intersection(line, intersection))
            return true;
    return false;
}

// [INTENT] Return the intersection point on this polygon's boundary that is
//          closest to line.a (nearest to the line origin).
// [STATE] Iterates all polygon edges (including closing edge); returns nearest
//         via squared-distance comparison. *intersection is only valid if
//         return is true.
bool Polygon::first_intersection(const Line& line, Point* intersection) const
{
    if (this->points.size() < 2)
        return false;

    bool   found = false;
    double dmin  = 0.;
    Line   l(this->points.back(), this->points.front());
    for (size_t i = 0; i < this->points.size(); ++i) {
        l.b = this->points[i];
        Point ip;
        if (l.intersection(line, &ip)) {
            if (!found) {
                found         = true;
                dmin          = (line.a - ip).cast<double>().squaredNorm();
                *intersection = ip;
            } else {
                double d = (line.a - ip).cast<double>().squaredNorm();
                if (d < dmin) {
                    dmin          = d;
                    *intersection = ip;
                }
            }
        }
        l.a = l.b;
    }
    return found;
}

// [INTENT] Collect ALL intersection points of the given Line with the polygon
//          boundary (all edges, including the closing edge). Appends to the
//          provided vector; returns true if any new intersections were found.
// [STATE] intersections vector is appended to (not cleared). The size delta
//         is used to determine the bool return value.
bool Polygon::intersections(const Line& line, Points* intersections) const
{
    if (this->points.size() < 2)
        return false;

    size_t intersections_size = intersections->size();
    Line   l(this->points.back(), this->points.front());
    for (size_t i = 0; i < this->points.size(); ++i) {
        l.b = this->points[i];
        Point intersection;
        if (l.intersection(line, &intersection))
            intersections->emplace_back(std::move(intersection));
        l.a = l.b;
    }
    return intersections->size() > intersections_size;
}

// [INTENT] Test whether this polygon overlaps any polygon in 'other'.
// [COUPLING] Uses Clipper intersection_pl() — expensive Clipper boolean op
//            for the main test. Falls back to a point-in-polygon check for
//            the case where *this is entirely inside one of the 'other' polygons
//            (no boundary crossings, so intersection_pl returns empty).
// [HAZARD] Clipper-based; both *this and other must be in scaled integer coords.
//          Not suitable for hot-path use per H-note in Polygon.hpp.
bool Polygon::overlaps(const Polygons& other) const
{
    if (this->empty() || other.empty())
        return false;
    Polylines pl_out = intersection_pl(to_polylines(other), *this);

    // See unit test SCENARIO("Clipper diff with polyline", "[Clipper]")
    // for in which case the intersection_pl produces any intersection.
    return !pl_out.empty() ||
           // If *this is completely inside other, then pl_out is empty, but the expolygons overlap. Test for that situation.
           std::any_of(other.begin(), other.end(), [this](auto& poly) { return poly.contains(this->points.front()); });
}

// [INTENT] Template helper: iterate all polygon vertices, passing the incoming
//          vector (prev→this) and outgoing vector (this→next) to a filter
//          functor. Collects vertices where filter returns true.
// [STATE] Starts from the LAST point (polygon wrap-around) to capture the
//         incoming vector for the first vertex correctly.
// [COUPLING] Used internally by filter_convex_concave_points_by_angle_threshold.
// Filter points from poly to the output with the help of FilterFn.
// filter function receives two vectors:
// v1: this_point - previous_point
// v2: next_point - this_point
// and returns true if the point is to be copied to the output.
template<typename FilterFn> Points filter_points_by_vectors(const Points& poly, FilterFn filter)
{
    // Last point is the first point visited.
    Point p1 = poly.back();
    // Previous vector to p1.
    Vec2d v1 = (p1 - *(poly.end() - 2)).cast<double>();

    Points out;
    for (Point p2 : poly) {
        // p2 is next point to the currently visited point p1.
        Vec2d v2 = (p2 - p1).cast<double>();
        // std::cerr << ((void*) &poly) << ": p1=" << p1 << "\tp2=" << p2 << "\tv1="<<v1<<"\tv2="<<v2;
        if (filter(v1, v2))
            out.emplace_back(p1);
        // std::cerr << "\n";
        v1 = v2;
        p1 = p2;
    }

    return out;
}

// [INTENT] Filter polygon vertices by convex/concave criterion AND a minimum
//          angle threshold. The angle is the exterior turning angle at each
//          vertex. Vertices with angle > angle_threshold AND matching the
//          convex_concave_filter are returned.
// [HAZARD] angle_threshold uses nextafter(threshold, +INF) to avoid FP equality
//          ambiguity at right angles. Still not fully correct for all inputs —
//          see the inline comment below.
// [COUPLING] Delegates to filter_points_by_vectors<FilterFn>.
// [CONCURRENCY] Stateless; thread-safe if poly is not mutated concurrently.
/**
 * @brief Filters points in a polygon based on a minimum angle threshold and a convex/concave criterion.
 *
 * This function iterates through the vertices of the input polygon and selects
 * points where the internal angle meets or exceeds the specified \p angle_threshold
 * and the point satisfies the condition defined by the \p convex_concave_filter.
 *
 * @tparam ConvexConcaveFilterFn The type of the callable filter object (e.g., function pointer, lambda, functor)
 * that determines if a point is considered convex or concave.
 * First vector is incoming line segment (ending at point), second vector is leaving current point.
 * It should have the signature `bool(const Vec2d&, const Vec2d&)`
 * @param poly Vertices of the input polygon.
 * @param angle_threshold The **minimum** angle (in radians) that the internal angle at a vertex must meet or exceed. Must be less than Pi
 * (because angle is always positive) and greater than zero.
 * @param convex_concave_filter A callable object that returns `true` if the point should be included
 * (e.g., if it's convex), and `false` otherwise.
 * @return Points Point objects that meet both the angle threshold and the convex/concave filter criterion.
 */
template<typename ConvexConcaveFilterFn>
Points filter_convex_concave_points_by_angle_threshold(const Points&         poly,
                                                       double                angle_threshold,
                                                       ConvexConcaveFilterFn convex_concave_filter)
{
    // The filter function is typically cross2(v1, v2) {>,<} 0
    assert(angle_threshold >= 0.);
    assert(angle_threshold < M_PI);
    if (angle_threshold > EPSILON) {
        // The methods con{cave,vex}_points are documented as
        //   "with the angle at the vertex larger than a threshold."
        // Due to the imprecision of floating point, this is difficult to get exactly right.
        // So I'm adding just enough here that an input of (M_PI/2) does not match a right angle.
        // Which doesn't mean it'll be correct for all values.
        // And we might learn people actually want "at or larger than threshold" instead.
        double cos_threshold = cos(std::nextafter(angle_threshold, +INFINITY));
        return filter_points_by_vectors(poly, [convex_concave_filter, cos_threshold](const Vec2d& v1, const Vec2d& v2) {
            if (!convex_concave_filter(v1, v2)) { /*std::cerr << "FIL_FALS";*/
                return false;
            }
            // Math lesson: Dot product is the product of the magnitudes and the cos(angle) between them.
            // So if we normalize both, their magnitudes are 1 and, thus, the dot product is cos(angle)
            // So we want to ensure we only pick angles *bigger* than our angle_threshold
            // cos(θ) goes 1->-1 as θ=0->Pi , the opposite direction
            // So if we want angle_vectors > angle_threshold
            // we must check dot_product_of_vectors < cos(angle_threshold)
            auto vec_dot = v1.normalized().dot(v2.normalized());
            // std::cerr << "\tvec_dot="<<vec_dot<<"\tcos_threshold="<<cos_threshold;
            if (vec_dot < cos_threshold) {
                // std::cerr << "TRUE";
                return true;
            }
            // std::cerr <<"DOT_FALS";
            return false;
        });
    } else {
        return filter_points_by_vectors(poly, convex_concave_filter);
    }
}

// [INTENT] Return all convex vertices of this polygon, optionally filtered by
//          a minimum turning angle (angle_threshold in radians).
// [COUPLING] cross2(v1,v2) > 0 => CCW turn => convex vertex for a CCW polygon.
//            For CW polygons, this returns what would be "concave" vertices.
Points Polygon::convex_points(double angle_threshold) const
{
    return filter_convex_concave_points_by_angle_threshold(this->points, angle_threshold,
                                                           [](const Vec2d& v1, const Vec2d& v2) { return cross2(v1, v2) > 0.; });
}

// [INTENT] Return all concave vertices of this polygon, optionally filtered by
//          a minimum turning angle (angle_threshold in radians).
// [COUPLING] cross2(v1,v2) < 0 => CW turn => concave vertex for a CCW polygon.
Points Polygon::concave_points(double angle_threshold) const
{
    return filter_convex_concave_points_by_angle_threshold(this->points, angle_threshold,
                                                           [](const Vec2d& v1, const Vec2d& v2) { return cross2(v1, v2) < 0.; });
}

// [INTENT] Find the closest point on the polygon boundary to 'point'.
//          Tests both vertices and foot-of-perpendicular on each edge.
// [STATE] Returns the closest vertex or edge-foot; falls back to returning
//         'point' itself if polygon is empty (proj = point initially).
// [HAZARD] foot computation uses floor()+0.5 rounding — introduces up to 0.5
//          scaled unit error. For very dense polygons this is negligible.
// [CONCURRENCY] Const; thread-safe.
// Projection of a point onto the polygon.
Point Polygon::point_projection(const Point& point) const
{
    Point  proj = point;
    double dmin = std::numeric_limits<double>::max();
    if (!this->points.empty()) {
        for (size_t i = 0; i < this->points.size(); ++i) {
            const Point& pt0 = this->points[i];
            const Point& pt1 = this->points[(i + 1 == this->points.size()) ? 0 : i + 1];
            double       d   = (point - pt0).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt0;
            }
            d = (point - pt1).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt1;
            }
            Vec2d    v1(coordf_t(pt1(0) - pt0(0)), coordf_t(pt1(1) - pt0(1)));
            coordf_t div = v1.squaredNorm();
            if (div > 0.) {
                Vec2d    v2(coordf_t(point(0) - pt0(0)), coordf_t(point(1) - pt0(1)));
                coordf_t t = v1.dot(v2) / div;
                if (t > 0. && t < 1.) {
                    // [INTENT] Interpolate the foot point along edge [pt0,pt1] at parameter t.
                    Point foot(coord_t(floor(coordf_t(pt0(0)) + t * v1(0) + 0.5)), coord_t(floor(coordf_t(pt0(1)) + t * v1(1) + 0.5)));
                    d = (point - foot).cast<double>().norm();
                    if (d < dmin) {
                        dmin = d;
                        proj = foot;
                    }
                }
            }
        }
    }
    return proj;
}

// [INTENT] Build a monotone arc-length parameterization of the polygon as a
//          float array of size n+1 (lengths[0]=0, lengths[n]=total perimeter).
//          Useful for densify() and other parameter-space operations.
// [HAZARD] Uses float (not double) — accumulates rounding error for large
//          or very detailed polygons.
std::vector<float> Polygon::parameter_by_length() const
{
    // Parametrize the polygon by its length.
    std::vector<float> lengths(points.size() + 1, 0.);
    for (size_t i = 1; i < points.size(); ++i)
        lengths[i] = lengths[i - 1] + (points[i] - points[i - 1]).cast<float>().norm();
    lengths.back() = lengths[lengths.size() - 2] + (points.front() - points.back()).cast<float>().norm();
    return lengths;
}

// [INTENT] Densify the polygon by inserting intermediate vertices wherever
//          any edge exceeds min_length (in scaled units). Only ONE new vertex
//          is inserted per edge per pass; very long edges may still exceed
//          min_length after one call (the loop advances j, so subsequent
//          segments will be re-evaluated in the same pass as j<=points.size()).
// [STATE] Mutates this->points and the provided lengths vector in place.
//         If lengths_ptr is null, computes a local parameter_by_length().
// [HAZARD] Calling with lengths_ptr pointing to a stale lengths array (from
//          a previous polygon state) will insert points at wrong positions.
// [MEMORY] Inserts points and lengths at index j — O(n) per insertion if
//          using std::vector. For polygons with many long edges, this is O(n²).
void Polygon::densify(float min_length, std::vector<float>* lengths_ptr)
{
    std::vector<float>  lengths_local;
    std::vector<float>& lengths = lengths_ptr ? *lengths_ptr : lengths_local;

    if (!lengths_ptr) {
        // Length parametrization has not been provided. Calculate our own.
        lengths = this->parameter_by_length();
    }

    assert(points.size() == lengths.size() - 1);

    for (size_t j = 1; j <= points.size(); ++j) {
        bool last = j == points.size();
        // [INTENT] 'last' handles the closing edge (back-to-front).
        int i = last ? 0 : j;

        if (lengths[j] - lengths[j - 1] > min_length) {
            Point diff     = points[i] - points[j - 1];
            float diff_len = lengths[j] - lengths[j - 1];
            float r        = (min_length / diff_len);
            Point new_pt   = points[j - 1] + Point(r * diff[0], r * diff[1]);
            points.insert(points.begin() + j, new_pt);
            lengths.insert(lengths.begin() + j, lengths[j - 1] + min_length);
        }
    }
    assert(points.size() == lengths.size() - 1);
}

// [INTENT] Apply a 3D affine transform (Transform3d) to the polygon's 2D
//          vertices by lifting them to Z=0, transforming, then discarding Z.
// [HAZARD] Z component of the transformed result is silently discarded.
//          If the transform has non-trivial Z output (e.g., rotation about
//          X or Y axes), the projected result is geometrically incorrect.
// [COUPLING] Uses Eigen::MatrixXd batch multiply via homogeneous().
//            Result points are assigned from double — implicit coord_t
//            truncation/rounding (no explicit round()).
// [MEMORY] Allocates src/dst Eigen matrices of size 3×n. Returns new Polygon.
Polygon Polygon::transform(const Transform3d& trafo) const
{
    unsigned int vertices_count = (unsigned int) points.size();
    Polygon      dstpoly;
    dstpoly.points.resize(vertices_count);
    if (vertices_count == 0)
        return dstpoly;

    unsigned int data_size = 3 * vertices_count * sizeof(float);

    Eigen::MatrixXd src(3, vertices_count);
    for (size_t i = 0; i < vertices_count; i++) {
        // [INTENT] Lift 2D point to 3D by setting Z=0.
        src.col(i) = Vec3d{double(points[i].x()), double(points[i].y()), 0.};
    }

    Eigen::MatrixXd dst(3, vertices_count);
    // [INTENT] Apply the homogeneous 4×4 transform to all columns at once.
    dst = trafo * src.colwise().homogeneous();

    for (size_t i = 0; i < vertices_count; i++) {
        // [HAZARD] Z (dst(2,i)) is discarded. No rounding — truncation to coord_t.
        dstpoly.points[i] = {dst(0, i), dst(1, i)};
    }
    return dstpoly;
}

// [INTENT] Compute the axis-aligned bounding box of a single polygon.
BoundingBox get_extents(const Polygon& poly) { return poly.bounding_box(); }

// [INTENT] Compute the merged bounding box of a collection of polygons.
//          Returns an undefined BoundingBox if the collection is empty.
BoundingBox get_extents(const Polygons& polygons)
{
    BoundingBox bb;
    if (!polygons.empty()) {
        bb = get_extents(polygons.front());
        for (size_t i = 1; i < polygons.size(); ++i)
            bb.merge(get_extents(polygons[i]));
    }
    return bb;
}

// [INTENT] Bounding box of a polygon after rotation by 'angle' radians.
//          Delegates to the Points overload.
BoundingBox get_extents_rotated(const Polygon& poly, double angle) { return get_extents_rotated(poly.points, angle); }

// [INTENT] Merged bounding box of all polygons after rotation by 'angle'.
BoundingBox get_extents_rotated(const Polygons& polygons, double angle)
{
    BoundingBox bb;
    if (!polygons.empty()) {
        bb = get_extents_rotated(polygons.front().points, angle);
        for (size_t i = 1; i < polygons.size(); ++i)
            bb.merge(get_extents_rotated(polygons[i].points, angle));
    }
    return bb;
}

// [INTENT] Return a vector of per-polygon bounding boxes for all polygons.
// [MEMORY] Allocates a vector of BoundingBox of size polygons.size().
extern std::vector<BoundingBox> get_extents_vector(const Polygons& polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it)
        out.push_back(get_extents(*it));
    return out;
}

// [INTENT] Check if the polygon is strictly convex (all cross products of
//          consecutive edge vectors have the same sign ≥ 0).
// [COUPLING] Uses int64_t cross products to avoid overflow that would occur
//            with coord_t (int32_t) for coordinates near the ±2^31 boundary.
// [HAZARD] Requires polygon to be valid: no duplicate points, no collinear
//          points, at least 3 vertices. Returns false for <3 vertices.
//          Degenerate (collinear) edges with det==0 are treated as convex.
// Polygon must be valid (at least three points), collinear points and duplicate points removed.
bool polygon_is_convex(const Points& poly)
{
    if (poly.size() < 3)
        return false;

    Point p0 = poly[poly.size() - 2];
    Point p1 = poly[poly.size() - 1];
    for (size_t i = 0; i < poly.size(); ++i) {
        Point p2 = poly[i];
        // [INTENT] int64_t cast prevents coord_t overflow in edge cross product.
        auto det = cross2((p1 - p0).cast<int64_t>(), (p2 - p1).cast<int64_t>());
        if (det < 0)
            return false;
        p0 = p1;
        p1 = p2;
    }
    return true;
}

// [INTENT] Check if any two points across all polygons in the collection are
//          duplicates (same integer coordinate).
// [STATE] #if 1 branch: merges all points globally and checks globally — this
//         catches duplicates BETWEEN polygons (e.g., shared vertices).
//         The #else branch only checks within each polygon separately.
// [MEMORY] Allocates a single flat Points vector of all points combined.
bool has_duplicate_points(const Polygons& polys)
{
#if 1
    // Check globally.
    Points allpts;
    allpts.reserve(count_points(polys));
    for (const Polygon& poly : polys)
        allpts.insert(allpts.end(), poly.points.begin(), poly.points.end());
    return has_duplicate_points(std::move(allpts));
#else
    // Check per contour.
    for (const Polygon& poly : polys)
        if (has_duplicate_points(poly))
            return true;
    return false;
#endif
}

// [INTENT] Remove consecutive duplicate points from a single polygon.
//          Also removes the duplicate if the last and first points are equal
//          (wrap-around duplicate).
// [STATE] Mutates polygon.points in place via std::unique + erase.
//         Returns true if any duplicates were removed.
bool remove_same_neighbor(Polygon& polygon)
{
    Points& points = polygon.points;
    if (points.empty())
        return false;
    auto last = std::unique(points.begin(), points.end());

    // remove first and last neighbor duplication
    if (const Point& last_point = *(last - 1); last_point == points.front()) {
        --last;
    }

    // no duplicits
    if (last == points.end())
        return false;

    points.erase(last, points.end());
    return true;
}

// [INTENT] Remove consecutive duplicate points from all polygons in a
//          collection, then erase polygons that become degenerate (≤2 points).
// [STATE] Mutates polygons in place.
bool remove_same_neighbor(Polygons& polygons)
{
    if (polygons.empty())
        return false;
    bool exist = false;
    for (Polygon& polygon : polygons)
        exist |= remove_same_neighbor(polygon);
    // remove empty polygons
    polygons.erase(std::remove_if(polygons.begin(), polygons.end(), [](const Polygon& p) { return p.points.size() <= 2; }), polygons.end());
    return exist;
}

// [INTENT] Helper: detect whether p2 forms a "stick" (degenerate spike) with
//          its neighbors p1 and p3. A stick is a vertex that folds back toward
//          the previous vertex, within EPSILON distance.
// [STATE] Pure function. Uses int64_t dot product and double cross product to
//         avoid integer overflow.
// [COUPLING] Used by remove_sticks().
static inline bool is_stick(const Point& p1, const Point& p2, const Point& p3)
{
    Point   v1  = p2 - p1;
    Point   v2  = p3 - p2;
    int64_t dir = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
    if (dir > 0)
        // p3 does not turn back to p1. Do not remove p2.
        return false;
    double l2_1 = double(v1(0)) * double(v1(0)) + double(v1(1)) * double(v1(1));
    double l2_2 = double(v2(0)) * double(v2(0)) + double(v2(1)) * double(v2(1));
    if (dir == 0)
        // p1, p2, p3 may make a perpendicular corner, or there is a zero edge length.
        // Remove p2 if it is coincident with p1 or p2.
        return l2_1 == 0 || l2_2 == 0;
    // p3 turns back to p1 after p2. Are p1, p2, p3 collinear?
    // Calculate distance from p3 to a segment (p1, p2) or from p1 to a segment(p2, p3),
    // whichever segment is longer
    double cross = double(v1(0)) * double(v2(1)) - double(v2(0)) * double(v1(1));
    double dist2 = cross * cross / std::max(l2_1, l2_2);
    return dist2 < EPSILON * EPSILON;
}

// [INTENT] Remove all "stick" vertices from a polygon (vertices that form
//          thin spikes that fold back within EPSILON). Handles boundary
//          wrap-around: also checks the last-to-first and first-to-second
//          vertices after the main loop.
// [STATE] Mutates poly in place. Returns true if any vertices were removed.
bool remove_sticks(Polygon& poly)
{
    bool   modified = false;
    size_t j        = 1;
    for (size_t i = 1; i + 1 < poly.points.size(); ++i) {
        if (!is_stick(poly[j - 1], poly[i], poly[i + 1])) {
            // Keep the point.
            if (j < i)
                poly.points[j] = poly.points[i];
            ++j;
        }
    }
    if (++j < poly.points.size()) {
        poly.points[j - 1] = poly.points.back();
        poly.points.erase(poly.points.begin() + j, poly.points.end());
        modified = true;
    }
    // [INTENT] Handle wrap-around sticks at the end of the point list.
    while (poly.points.size() >= 3 && is_stick(poly.points[poly.points.size() - 2], poly.points.back(), poly.points.front())) {
        poly.points.pop_back();
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points.back(), poly.points.front(), poly.points[1]))
        poly.points.erase(poly.points.begin());
    return modified;
}

// [INTENT] Remove sticks from all polygons; also removes polygons that become
//          degenerate (fewer than 3 points) after stick removal.
// [STATE] Mutates polys in place. Returns true if any modification occurred.
bool remove_sticks(Polygons& polys)
{
    bool   modified = false;
    size_t j        = 0;
    for (size_t i = 0; i < polys.size(); ++i) {
        modified |= remove_sticks(polys[i]);
        if (polys[i].points.size() >= 3) {
            if (j < i)
                std::swap(polys[i].points, polys[j].points);
            ++j;
        }
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

// [INTENT] Remove all polygons with fewer than 3 points (degenerate).
//          Returns true if any polygons were removed.
// [STATE] Mutates polys in place using swap-and-truncate pattern.
bool remove_degenerate(Polygons& polys)
{
    bool   modified = false;
    size_t j        = 0;
    for (size_t i = 0; i < polys.size(); ++i) {
        if (polys[i].points.size() >= 3) {
            if (j < i)
                std::swap(polys[i].points, polys[j].points);
            ++j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

// [INTENT] Remove all polygons whose absolute area is below min_area.
//          Returns true if any polygons were removed.
// [HAZARD] Uses std::abs(poly.area()) — correct (handles both CW/CCW).
//          min_area is in scaled^2 units, not mm^2.
bool remove_small(Polygons& polys, double min_area)
{
    bool   modified = false;
    size_t j        = 0;
    for (size_t i = 0; i < polys.size(); ++i) {
        if (std::abs(polys[i].area()) >= min_area) {
            if (j < i)
                std::swap(polys[i].points, polys[j].points);
            ++j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

// [INTENT] Remove collinear vertices from a single polygon. Uses a sliding
//          window of (p1, p2, p3): if p2 lies on segment (p1,p3) within
//          SCALED_EPSILON, skip p2.
// [STATE] Works on a padded copy (prepends last point, appends first point)
//         so the wrap-around is handled correctly. Rebuilds poly.points.
// [HAZARD] Uses Line::distance_to() which is floating-point; may fail to
//          remove collinear points when the polygon is at extreme scale.
void remove_collinear(Polygon& poly)
{
    if (poly.points.size() > 2) {
        // copy points and append both 1 and last point in place to cover the boundaries
        Points pp;
        pp.reserve(poly.points.size() + 2);
        pp.push_back(poly.points.back());
        pp.insert(pp.begin() + 1, poly.points.begin(), poly.points.end());
        pp.push_back(poly.points.front());
        // delete old points vector. Will be re-filled in the loop
        poly.points.clear();

        size_t i = 0;
        size_t k = 0;
        while (i < pp.size() - 2) {
            k               = i + 1;
            const Point& p1 = pp[i];
            while (k < pp.size() - 1) {
                const Point& p2 = pp[k];
                const Point& p3 = pp[k + 1];
                Line         l(p1, p3);
                if (l.distance_to(p2) < SCALED_EPSILON) {
                    k++;
                } else {
                    if (i > 0)
                        poly.points.push_back(p1); // implicitly removes the first point we appended above
                    i = k;
                    break;
                }
            }
            if (k > pp.size() - 2)
                break; // all remaining points are collinear and can be skipped
        }
        poly.points.push_back(pp[i]);
    }
}

// [INTENT] Apply remove_collinear to every polygon in a collection.
void remove_collinear(Polygons& polys)
{
    for (Polygon& poly : polys)
        remove_collinear(poly);
}

// [INTENT] Simplify a collection of polygons: Douglas-Peucker decimation
//          followed by Clipper SimplifyPolygons (eliminates self-intersections,
//          produces strictly simple polygons when strictly_simple=true).
// [HAZARD] ClipperLib may reorient contours: if the original area was negative
//          (CW hole), Clipper SimplifyPolygons returns a positive-area CCW
//          contour. The code detects original winding and reverses if needed.
// [COUPLING] Uses ClipperUtils::SinglePathProvider for single-path ClipperLib
//            access. to_polyline() converts Polygon→Polyline (opens the ring).
// [MEMORY] Returns a new Polygons collection; source is not modified.
Polygons polygons_simplify(const Polygons& source_polygons, double tolerance, bool strictly_simple /* = true */)
{
    Polygons out;
    out.reserve(source_polygons.size());
    for (const Polygon& source_polygon : source_polygons) {
        // Run Douglas / Peucker simplification algorithm on an open polyline (by repeating the first point at the end of the polyline),
        Points simplified = MultiPoint::_douglas_peucker(to_polyline(source_polygon).points, tolerance);
        // then remove the last (repeated) point.
        simplified.pop_back();
        // Simplify the decimated contour by ClipperLib.
        bool ccw = ClipperLib::Area(simplified) > 0.;
        for (Points& path :
             ClipperLib::SimplifyPolygons(ClipperUtils::SinglePathProvider(simplified), ClipperLib::pftNonZero, strictly_simple)) {
            if (!ccw)
                // ClipperLib likely reoriented negative area contours to become positive. Reverse holes back to CW.
                std::reverse(path.begin(), path.end());
            out.emplace_back(std::move(path));
        }
    }
    return out;
}

// [INTENT] Check if two polygons are topologically equivalent: same vertex
//          count, same vertices in the same cyclic order (rotation allowed).
// [STATE] Finds the starting rotation by locating r.points.front() in l.
//         Then verifies the remaining vertices match circularly.
// [HAZARD] Only checks one rotation direction (no reverse check).
//          Mirrored polygons with the same vertex set will return false.
// Do polygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool polygons_match(const Polygon& l, const Polygon& r)
{
    if (l.size() != r.size())
        return false;
    auto it_l = std::find(l.points.begin(), l.points.end(), r.points.front());
    if (it_l == l.points.end())
        return false;
    auto it_r = r.points.begin();
    for (; it_l != l.points.end(); ++it_l, ++it_r)
        if (*it_l != *it_r)
            return false;
    it_l = l.points.begin();
    for (; it_r != r.points.end(); ++it_l, ++it_r)
        if (*it_l != *it_r)
            return false;
    return true;
}

// [INTENT] Test whether any polygon in polys1 overlaps any polygon in polys2.
//          Short-circuits on first overlap found.
// [COUPLING] Delegates to Polygon::overlaps(Polygons) — Clipper-based,
//            expensive. Not suitable for large collections in hot paths.
bool overlaps(const Polygons& polys1, const Polygons& polys2)
{
    for (const Polygon& poly1 : polys1) {
        if (poly1.overlaps(polys2))
            return true;
    }
    return false;
}

// [INTENT] Point-in-polygon test for a single polygon using ClipperLib.
//          Returns border_result if p lies exactly on the boundary
//          (ClipperLib returns -1 for on-boundary).
// [COUPLING] ClipperLib::PointInPolygon returns: 0=outside, 1=inside, -1=on boundary.
//            The modulo-2 test handles the winding number convention.
bool contains(const Polygon& polygon, const Point& p, bool border_result)
{
    if (const int poly_count_inside = ClipperLib::PointInPolygon(p, polygon.points); poly_count_inside == -1)
        return border_result;
    else
        return (poly_count_inside % 2) == 1;
}

// [INTENT] Point-in-polygon test across a collection of polygons. Accumulates
//          the winding count and uses modulo-2 to determine containment.
//          Returns border_result if p lies on any boundary.
// [HAZARD] Winding count accumulation across multiple polygons is only correct
//          if the collection forms a valid non-self-intersecting region. For
//          arbitrary overlapping polygons, the result is undefined.
bool contains(const Polygons& polygons, const Point& p, bool border_result)
{
    int poly_count_inside = 0;
    for (const Polygon& poly : polygons) {
        const int is_inside_this_poly = ClipperLib::PointInPolygon(p, poly.points);
        if (is_inside_this_poly == -1)
            return border_result;
        poly_count_inside += is_inside_this_poly;
    }
    return (poly_count_inside % 2) == 1;
}

// [INTENT] Generate a circle polygon approximation with the specified radius
//          and maximum radial error (chord-midpoint deviation ≤ error).
// [COUPLING] Computes the minimum number of segments needed from the error
//            tolerance, then delegates to make_circle_num_segments.
// [HAZARD] radius is in the CALLER's units (should be scaled integer units for
//          use with other Clipper geometry). If radius is in mm, the output
//          points will not align with scaled-int polygons.
Polygon make_circle(double radius, double error)
{
    // [INTENT] Compute half-angle from chord-height error: error = r*(1-cos(θ/2))
    //          => θ = 2*acos(1 - error/r). Number of segments = 2π/θ rounded up.
    double angle        = 2. * acos(1. - error / radius);
    size_t num_segments = size_t(ceil(2. * M_PI / angle));
    return make_circle_num_segments(radius, num_segments);
}

// [INTENT] Generate a regular polygon (circle approximation) with exactly
//          num_segments vertices, evenly spaced on a circle of given radius.
// [HAZARD] Output coord_t points are truncated from double — sub-unit rounding
//          error up to 0.5 scaled units per vertex. For very large radii this
//          may cause self-intersections.
Polygon make_circle_num_segments(double radius, size_t num_segments)
{
    Polygon out;
    out.points.reserve(num_segments);
    double angle_inc = 2.0 * M_PI / num_segments;
    for (size_t i = 0; i < num_segments; ++i) {
        const double angle = angle_inc * i;
        out.points.emplace_back(coord_t(cos(angle) * radius), coord_t(sin(angle) * radius));
    }
    return out;
}
} // namespace Slic3r
