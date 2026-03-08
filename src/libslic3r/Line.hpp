#ifndef slic3r_Line_hpp_
#define slic3r_Line_hpp_

// [INTENT] Defines line segment primitives for 2D and 3D geometry in both integer-scaled
// (coord_t) and floating-point (double) coordinate systems. Provides the templated
// `line_alg` namespace for generic distance and intersection algorithms that work across
// all line types without code duplication.

#include "libslic3r.h"
#include "Point.hpp"

#include <type_traits>

namespace Slic3r {

class BoundingBox;
class Line;
class Line3;
class Linef3;
class Polyline;
class ThickLine;
typedef std::vector<Line>      Lines;
typedef std::vector<Line3>     Lines3;
typedef std::vector<ThickLine> ThickLines;

Linef3 transform(const Linef3& line, const Transform3d& t);

// [INTENT] Generic template algorithms for line segments. All algorithms are parameterized
// over L (line type), and resolve Dim and Scalar via the Traits specialization. This allows
// the same distance/intersection code to work on Line (coord_t, 2D), Line3 (coord_t, 3D),
// Linef (double, 2D), and Linef3 (double, 3D) without duplication.
// [COUPLING] Every line type that participates in line_alg must expose `a`, `b`, `Dim`, and `Scalar`
// members, either directly or via a Traits specialization.
namespace line_alg {

// [INTENT] Default Traits: reads `a` and `b` directly from the line struct.
// [COUPLING] Works for any type L that has public `.a` / `.b` members.
template<class L, class En = void> struct Traits
{
    static constexpr int Dim = L::Dim;
    using Scalar             = typename L::Scalar;

    static Vec<Dim, Scalar>&       get_a(L& l) { return l.a; }
    static Vec<Dim, Scalar>&       get_b(L& l) { return l.b; }
    static const Vec<Dim, Scalar>& get_a(const L& l) { return l.a; }
    static const Vec<Dim, Scalar>& get_b(const L& l) { return l.b; }
};

template<class L> const constexpr int Dim = Traits<remove_cvref_t<L>>::Dim;
template<class L> using Scalar            = typename Traits<remove_cvref_t<L>>::Scalar;

template<class L> auto get_a(L&& l) { return Traits<remove_cvref_t<L>>::get_a(l); }
template<class L> auto get_b(L&& l) { return Traits<remove_cvref_t<L>>::get_b(l); }

// [INTENT] Closest point on the finite segment [a,b] to `point`. Returns squared Euclidean
// distance. The internal computation is always in double to avoid overflow with large coord_t
// integers. The nearest_point output is cast back to Scalar<L> (potentially truncating).
// [HAZARD] H635: When Scalar<L> = coord_t (integer) and the true nearest point falls between two
// integers, the result is rounded by cast. The returned squared distance is computed before the
// cast, so it represents distance to the true projected point, NOT to the rounded nearest_point
// output. Callers that compare the squared distance against distance_to(*nearest_point, point)
// will see a discrepancy.
template<class L> double distance_to_squared(const L& line, const Vec<Dim<L>, Scalar<L>>& point, Vec<Dim<L>, Scalar<L>>* nearest_point)
{
    const Vec<Dim<L>, double> v  = (get_b(line) - get_a(line)).template cast<double>();
    const Vec<Dim<L>, double> va = (point - get_a(line)).template cast<double>();
    const double              l2 = v.squaredNorm(); // avoid a sqrt
    if (l2 == 0.0) {
        // a == b case
        *nearest_point = get_a(line);
        return va.squaredNorm();
    }
    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v) / l2;
    if (t <= 0.0) {
        // beyond the 'a' end of the segment
        *nearest_point = get_a(line);
        return va.squaredNorm();
    } else if (t >= 1.0) {
        // beyond the 'b' end of the segment
        *nearest_point = get_b(line);
        return (point - get_b(line)).template cast<double>().squaredNorm();
    }

    // [HAZARD] H635 (see above): nearest_point is rounded to Scalar<L> but returned distance
    // is computed from the unrounded projection.
    *nearest_point = (get_a(line).template cast<double>() + t * v).template cast<Scalar<L>>();
    return (t * v - va).squaredNorm();
}

// Distance to the closest point of line.
template<class L> double distance_to_squared(const L& line, const Vec<Dim<L>, Scalar<L>>& point)
{
    Vec<Dim<L>, Scalar<L>> nearest_point;
    return distance_to_squared<L>(line, point, &nearest_point);
}

template<class L> double distance_to(const L& line, const Vec<Dim<L>, Scalar<L>>& point)
{
    return std::sqrt(distance_to_squared(line, point));
}

// [INTENT] Distance to the closest point on the **infinite** extension of the line.
// Unlike distance_to_squared(), t is not clamped to [0,1] — the returned point may lie
// beyond the segment endpoints. Used for perpendicular-distance queries (e.g. seam placement).
// Returns a squared distance to the closest point on the infinite.
// Returned nearest_point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
template<class L>
double distance_to_infinite_squared(const L& line, const Vec<Dim<L>, Scalar<L>>& point, Vec<Dim<L>, Scalar<L>>* closest_point)
{
    const Vec<Dim<L>, double> v  = (get_b(line) - get_a(line)).template cast<double>();
    const Vec<Dim<L>, double> va = (point - get_a(line)).template cast<double>();
    const double              l2 = v.squaredNorm(); // avoid a sqrt
    if (l2 == 0.) {
        // a == b case
        *closest_point = get_a(line);
        return va.squaredNorm();
    }
    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v) / l2;
    *closest_point = (get_a(line).template cast<double>() + t * v).template cast<Scalar<L>>();
    return (t * v - va).squaredNorm();
}

// Returns a squared distance to the closest point on the infinite.
// Closest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
template<class L> double distance_to_infinite_squared(const L& line, const Vec<Dim<L>, Scalar<L>>& point)
{
    Vec<Dim<L>, Scalar<L>> nearest_point;
    return distance_to_infinite_squared<L>(line, point, &nearest_point);
}

// Returns a distance to the closest point on the infinite.
// Closest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
template<class L> double distance_to_infinite(const L& line, const Vec<Dim<L>, Scalar<L>>& point)
{
    return std::sqrt(distance_to_infinite_squared(line, point));
}

// [INTENT] Segment-segment intersection test and intersection point computation.
// Works for 2D only (uses cross2 which is a 2D operation).
// [HAZARD] H636: The collinear case (`fabs(denom) < EPSILON`) always returns false.
// The commented-out code would return true for overlapping collinear segments, but that
// branch is permanently disabled with `#if 0`. Callers expecting overlap detection
// for collinear segments must use a different method.
// [HAZARD] H637: Floating-point conditional `Floating` is `double` for integer line types.
// The result intersection_pt is cast back to Scalar<L>. For coord_t lines, sub-pixel
// intersection points are truncated (not rounded) by the cast.
template<class L> bool intersection(const L& l1, const L& l2, Vec<Dim<L>, Scalar<L>>* intersection_pt)
{
    using Floating      = typename std::conditional<std::is_floating_point<Scalar<L>>::value, Scalar<L>, double>::type;
    using VecType       = const Vec<Dim<L>, Floating>;
    const VecType v1    = (l1.b - l1.a).template cast<Floating>();
    const VecType v2    = (l2.b - l2.a).template cast<Floating>();
    Floating      denom = cross2(v1, v2);
    if (fabs(denom) < EPSILON)
#if 0
        // Lines are collinear. Return true if they are coincident (overlappign).
        return ! (fabs(nume_a) < EPSILON && fabs(nume_b) < EPSILON);
#else
        return false;
#endif
    const VecType v12    = (l1.a - l2.a).template cast<Floating>();
    Floating      nume_a = cross2(v2, v12);
    Floating      nume_b = cross2(v1, v12);
    Floating      t1     = nume_a / denom;
    Floating      t2     = nume_b / denom;
    if (t1 >= 0 && t1 <= 1.0f && t2 >= 0 && t2 <= 1.0f) {
        // Get the intersection point.
        (*intersection_pt) = (l1.a.template cast<Floating>() + t1 * v1).template cast<Scalar<L>>();
        return true;
    }
    return false; // not intersecting
}

} // namespace line_alg

// [INTENT] 2D integer-coordinate line segment using coord_t (scaled integers, 1 unit = 1e-6 mm).
// Participates in the `line_alg` generic algorithms via `Dim = 2` and `Scalar = coord_t`.
// [COUPLING] Used heavily throughout the slicing pipeline: ClipperUtils, BridgeDetector,
// ShortestPath, PerimeterGenerator, GCode path planning.
// [STATE] Mutable a/b endpoints; no internal state beyond them.
class Line
{
public:
    Line() {}
    Line(const Point& _a, const Point& _b) : a(_a), b(_b) {}
    explicit operator Lines() const
    {
        Lines lines;
        lines.emplace_back(*this);
        return lines;
    }
    void scale(double factor)
    {
        this->a *= factor;
        this->b *= factor;
    }
    void translate(const Point& v)
    {
        this->a += v;
        this->b += v;
    }
    void translate(double x, double y) { this->translate(Point(x, y)); }
    void rotate(double angle, const Point& center)
    {
        this->a.rotate(angle, center);
        this->b.rotate(angle, center);
    }
    void   reverse() { std::swap(this->a, this->b); }
    double length() const { return (b - a).cast<double>().norm(); }
    // [INTENT] Midpoint uses integer arithmetic: result is rounded toward zero for odd-length segments.
    Point  midpoint() const { return (this->a + this->b) / 2; }
    bool   intersection_infinite(const Line& other, Point* point) const;
    bool   operator==(const Line& rhs) const { return this->a == rhs.a && this->b == rhs.b; }
    double distance_to_squared(const Point& point) const { return distance_to_squared(point, this->a, this->b); }
    double distance_to_squared(const Point& point, Point* closest_point) const
    {
        return line_alg::distance_to_squared(*this, point, closest_point);
    }
    double distance_to(const Point& point) const { return distance_to(point, this->a, this->b); }
    double distance_to_infinite_squared(const Point& point, Point* closest_point) const
    {
        return line_alg::distance_to_infinite_squared(*this, point, closest_point);
    }
    // [INTENT] Perpendicular (signed) distance from point to the infinite line through a,b.
    // Always returns non-negative (uses std::abs). Equivalent to the height of the triangle
    // formed by a, b, point.
    double perp_distance_to(const Point& point) const;
    bool   parallel_to(double angle) const;
    bool   parallel_to(const Line& line) const;
    bool   perpendicular_to(double angle) const;
    bool   perpendicular_to(const Line& line) const;
    // [INTENT] Raw atan2 angle in [-π, π]. Distinct from `orientation()` (remapped to [0, 2π])
    // and `direction()` (remapped to [0, π] — undirected angle).
    double atan2_() const { return atan2(this->b(1) - this->a(1), this->b(0) - this->a(0)); }
    // [INTENT] orientation() returns angle in [0, 2π). Directed: a→b has a specific orientation.
    double orientation() const;
    // [INTENT] direction() returns undirected angle in [0, π). A line and its reverse have the same direction.
    double direction() const;
    Vector vector() const { return this->b - this->a; }
    // [INTENT] Outward normal: rotate vector 90° CCW (swaps x,y, negates new y).
    // Convention: normal points "left" of the directed segment a→b.
    Vector normal() const { return Vector((this->b(1) - this->a(1)), -(this->b(0) - this->a(0))); }
    bool   intersection(const Line& line, Point* intersection) const;
    // Clip a line with a bounding box. Returns false if the line is completely outside of the bounding box.
    // [INTENT] Uses Liang-Barsky algorithm; modifies a and b in-place on success.
    bool clip_with_bbox(const BoundingBox& bbox);
    // Extend the line from both sides by an offset.
    // [INTENT] Extends a and b by `offset` mm in the direction of a→b and b→a respectively.
    void                 extend(double offset);
    bool                 overlap(const Line& line, double& overlap_length) const;
    static inline double distance_to_squared(const Point& point, const Point& a, const Point& b)
    {
        return line_alg::distance_to_squared(Line{a, b}, Vec<2, coord_t>{point});
    }
    static double distance_to(const Point& point, const Point& a, const Point& b) { return sqrt(distance_to_squared(point, a, b)); }

    // Returns a distance to the closest point on the infinite.
    // Closest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
    static inline double distance_to_infinite_squared(const Point& point, const Point& a, const Point& b)
    {
        return line_alg::distance_to_infinite_squared(Line{a, b}, Vec<2, coord_t>{point});
    }
    static double distance_to_infinite(const Point& point, const Point& a, const Point& b)
    {
        return sqrt(distance_to_infinite_squared(point, a, b));
    }

    // [STATE] Endpoints in coord_t scaled integer space (1 unit = 1e-6 mm).
    Point a;
    Point b;

    static const constexpr int Dim = 2;
    using Scalar                   = Point::Scalar;
};

// [INTENT] A Line with per-endpoint widths used to represent variable-width extrusion paths.
// `a_width` and `b_width` are in scaled coord_t units (mm * 1e6).
// [COUPLING] Used by GCode extrusion output (ThickLines in GCodeWriter), Arachne WallToolPaths,
// and PerimeterGenerator variable-width output.
// [HAZARD] H638: There is no enforcement that a_width, b_width are non-negative. Negative widths
// passed through Clipper operations would cause incorrect offsets.
class ThickLine : public Line
{
public:
    ThickLine() : a_width(0), b_width(0) {}
    ThickLine(const Point& a, const Point& b) : Line(a, b), a_width(0), b_width(0) {}
    ThickLine(const Point& a, const Point& b, double wa, double wb) : Line(a, b), a_width(wa), b_width(wb) {}

    double a_width, b_width;
};

// [INTENT] A Line annotated with an estimated "curl height" (how much the extrudate lifts off
// the bed). Used by the overhang/bridge detection subsystem to tag segments where curling is
// predicted. The float is sufficient (sub-mm resolution), keeping the struct small.
// [COUPLING] Produced by GCode/CoolingBuffer and consumed by the thin-wall and anti-curling logic.
class CurledLine : public Line
{
public:
    CurledLine() : curled_height(0.0f) {}
    CurledLine(const Point& a, const Point& b) : Line(a, b), curled_height(0.0f) {}
    CurledLine(const Point& a, const Point& b, float curled_height) : Line(a, b), curled_height(curled_height) {}

    float curled_height;
};

using CurledLines = std::vector<CurledLine>;

// [INTENT] 3D integer-coordinate line segment (coord_t). Used in mesh-slicing intersection
// geometry (TriangleMeshSlicer) before converting to 2D slices.
// [COUPLING] Participates in line_alg via Dim=3, Scalar=coord_t.
class Line3
{
public:
    Line3() : a(Vec3crd::Zero()), b(Vec3crd::Zero()) {}
    Line3(const Vec3crd& _a, const Vec3crd& _b) : a(_a), b(_b) {}

    double  length() const { return (this->a - this->b).cast<double>().norm(); }
    Vec3crd vector() const { return this->b - this->a; }

    Vec3crd a;
    Vec3crd b;

    static const constexpr int Dim = 3;
    using Scalar                   = Vec3crd::Scalar;
};

// [INTENT] 2D floating-point line segment (double). Used in geometry algorithms that work
// in physical mm space (arc fitting, Voronoi, etc.) without the 1e6 scaling factor.
// [COUPLING] Participates in line_alg via Dim=2, Scalar=double.
class Linef
{
public:
    Linef() : a(Vec2d::Zero()), b(Vec2d::Zero()) {}
    Linef(const Vec2d& _a, const Vec2d& _b) : a(_a), b(_b) {}

    Vec2d a;
    Vec2d b;

    static const constexpr int Dim = 2;
    using Scalar                   = Vec2d::Scalar;
};
using Linesf = std::vector<Linef>;

// [INTENT] 3D floating-point line segment (double). Used for camera rays, transform operations,
// and plane intersection in the GUI and SLA/FDM toolpath logic.
// [COUPLING] `transform()` free function below applies a Transform3d to a Linef3.
class Linef3
{
public:
    Linef3() : a(Vec3d::Zero()), b(Vec3d::Zero()) {}
    Linef3(const Vec3d& _a, const Vec3d& _b) : a(_a), b(_b) {}

    // [INTENT] Intersect the infinite line with a horizontal plane at z.
    // Returns the 3D point where the line crosses the z plane.
    // [HAZARD] H639: If the line is horizontal (v(2) == 0.0), division by zero produces
    // NaN/inf. No guard. Callers must ensure the line is not parallel to the z=const plane.
    Vec3d intersect_plane(double z) const;
    void  scale(double factor)
    {
        this->a *= factor;
        this->b *= factor;
    }
    Vec3d vector() const { return this->b - this->a; }
    // [HAZARD] H639: unit_vector() returns Vec3d::Zero() for zero-length line, which is a
    // silent degenerate case — callers using the result as a direction may silently get a
    // zero vector instead of an error.
    Vec3d  unit_vector() const { return (length() == 0.0) ? Vec3d::Zero() : vector().normalized(); }
    double length() const { return vector().norm(); }

    Vec3d a;
    Vec3d b;

    static const constexpr int Dim = 3;
    using Scalar                   = Vec3d::Scalar;
};

BoundingBox get_extents(const Lines& lines);

} // namespace Slic3r

// [INTENT] Boost.Polygon segment_concept specialization for Slic3r::Line.
// Allows Line objects to be used directly in Boost.Polygon algorithms (e.g. Voronoi diagram
// builder via boost::polygon::construct_voronoi()).
// [COUPLING] The coordinate type is coord_t; point_type is Slic3r::Point.
// [HAZARD] H640: Boost.Polygon's Voronoi builder assumes segment endpoints are distinct and
// that coordinates fit in int32_t (despite coord_t being int64_t in some configurations).
// Segments with equal a/b endpoints, or coordinates that overflow int32_t, produce undefined
// behavior in the Voronoi algorithm.
// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
template<> struct geometry_concept<Slic3r::Line>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::Line>
{
    typedef coord_t       coordinate_type;
    typedef Slic3r::Point point_type;

    static inline point_type get(const Slic3r::Line& line, direction_1d dir) { return dir.to_int() ? line.b : line.a; }
};
}} // namespace boost::polygon
// end Boost

#endif // slic3r_Line_hpp_
