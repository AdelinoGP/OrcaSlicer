// [INTENT] Implements the non-trivial methods of the Line, Linef3, and related line types.
// All geometry is performed in double precision to avoid coord_t integer overflow, then
// results are cast back to coord_t where needed.
// [COUPLING] Depends on Geometry.hpp (directions_parallel, directions_perpendicular, liang_barsky)
//            and BoundingBox for clip_with_bbox.

#include "Geometry.hpp"
#include "Line.hpp"
#include "Polyline.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Slic3r {

// [INTENT] Applies a 4×4 homogeneous Transform3d to a Linef3 (3D floating-point line).
// The two endpoints are packed as columns of a 3×2 matrix, then left-multiplied by t
// after adding the homogeneous row. Result is a new Linef3 in the transformed coordinate frame.
// [COUPLING] Used by the GUI camera/ray intersection code and SLA tilt simulation.
Linef3 transform(const Linef3& line, const Transform3d& t)
{
    typedef Eigen::Matrix<double, 3, 2> LineInMatrixForm;

    LineInMatrixForm world_line;
    ::memcpy((void*) world_line.col(0).data(), (const void*) line.a.data(), 3 * sizeof(double));
    ::memcpy((void*) world_line.col(1).data(), (const void*) line.b.data(), 3 * sizeof(double));

    // [INTENT] colwise().homogeneous() promotes each 3D column to a 4D homogeneous vector;
    // left-multiplying by t (4×4) applies the full affine transform.
    // Result is 3×2: first two rows are X/Y, third row is Z.
    LineInMatrixForm local_line = t * world_line.colwise().homogeneous();
    return Linef3(Vec3d(local_line(0, 0), local_line(1, 0), local_line(2, 0)), Vec3d(local_line(0, 1), local_line(1, 1), local_line(2, 1)));
}

// [INTENT] Intersects two infinite lines (not segments) in 2D.
// Uses the parametric form: intersection = this->a + t1 * (this->b - this->a).
// [HAZARD] H641: Result point is cast from double to coord_t. For nearly-parallel lines
// (denom near zero), t1 can be very large, causing the intersection point to exceed
// coord_t limits. The overflow guard checks against numeric_limits<coord_t> and returns
// false in that case, but "near-overflow" values just below the limit are still accepted
// with truncated precision.
bool Line::intersection_infinite(const Line& other, Point* point) const
{
    Vec2d  a1    = this->a.cast<double>();
    Vec2d  v12   = (other.a - this->a).cast<double>();
    Vec2d  v1    = (this->b - this->a).cast<double>();
    Vec2d  v2    = (other.b - other.a).cast<double>();
    double denom = cross2(v1, v2);
    if (std::fabs(denom) < EPSILON)
        return false;
    double t1     = cross2(v12, v2) / denom;
    Vec2d  result = (a1 + t1 * v1);
    if (result.x() > std::numeric_limits<coord_t>::max() || result.x() < std::numeric_limits<coord_t>::lowest() ||
        result.y() > std::numeric_limits<coord_t>::max() || result.y() < std::numeric_limits<coord_t>::lowest()) {
        // Intersection has at least one of the coordinates much bigger (or smaller) than coord_t maximum value (or minimum).
        // So it can not be stored into the Point without integer overflows. That could mean that input lines are parallel or near parallel.
        return false;
    }
    *point = (result).cast<coord_t>();
    return true;
}

// [INTENT] Perpendicular (unsigned) distance from `point` to the infinite line through a,b.
// Uses the cross product formula: |cross(v, va)| / |v|.
// [HAZARD] H642: If `a == b` (zero-length line), the method returns the Euclidean distance
// from `point` to `a`, which is reasonable but differs from the "line distance" concept.
// The division by `v.norm()` is guarded by the `a == b` check, so no division by zero occurs.
double Line::perp_distance_to(const Point& point) const
{
    const Line& line = *this;
    const Vec2d v    = (line.b - line.a).cast<double>();
    const Vec2d va   = (point - line.a).cast<double>();
    if (line.a == line.b)
        return va.norm();
    return std::abs(cross2(v, va)) / v.norm();
}

// [INTENT] Returns the directed angle in [0, 2π) from the positive X axis to the direction a→b.
// Built from atan2_() which returns [-π, π]; negative angles are remapped by adding 2π.
double Line::orientation() const
{
    double angle = this->atan2_();
    if (angle < 0)
        angle = 2 * PI + angle;
    return angle;
}

// [INTENT] Returns the undirected angle in [0, π). A line and its reverse share the same direction.
// Special case: angle == π is remapped to 0 to ensure range is strictly [0, π).
double Line::direction() const
{
    double atan2 = this->atan2_();
    return (fabs(atan2 - PI) < EPSILON) ? 0 : (atan2 < 0) ? (atan2 + PI) : atan2;
}

bool Line::parallel_to(double angle) const { return Slic3r::Geometry::directions_parallel(this->direction(), angle); }

// [INTENT] Two lines are parallel if the cross product of their direction vectors is near zero.
// Uses the normalized criterion: cross^2 < EPSILON^2 * |v1|^2 * |v2|^2 to be scale-invariant.
bool Line::parallel_to(const Line& line) const
{
    const Vec2d v1 = (this->b - this->a).cast<double>();
    const Vec2d v2 = (line.b - line.a).cast<double>();
    return sqr(cross2(v1, v2)) < sqr(EPSILON) * v1.squaredNorm() * v2.squaredNorm();
}

// [INTENT] Compute the overlap length between two collinear (parallel and coincident) lines.
// Returns false if lines are not parallel, or not collinear (bridging test uses `parallel_to(line_)`).
// The overlap is measured along the X axis projection; for near-vertical lines this can produce
// very small or zero overlap even when there is significant physical overlap.
// [HAZARD] H643: Overlap computation uses only the X projection (a_min, a_max, b_min, b_max).
// For lines that are nearly vertical (|dx| ~ 0), the X projection is nearly zero and
// the computed overlap_length can be wildly inaccurate. Use only for non-vertical lines.
// [HAZARD] H644: `overlap_length` is scaled by `this->length() / (a_max - a_min)` to convert
// from X-projection units back to line length units. If `a_max == a_min` (vertical line),
// division by zero occurs. Only triggered if lines are exactly vertical and pass all prior checks.
bool Line::overlap(const Line& line, double& overlap_length) const
{
    if (!this->parallel_to(line))
        return false;
    Line line_(this->a, line.a);
    if (line_.length() > scaled(EPSILON) && !this->parallel_to(line_))
        return false;
    coord_t a_min = std::min(this->a.x(), this->b.x());
    coord_t a_max = std::max(this->a.x(), this->b.x());
    coord_t b_min = std::min(line.a.x(), line.b.x());
    coord_t b_max = std::max(line.a.x(), line.b.x());
    if (a_min > b_max || a_max < b_min)
        return false;
    overlap_length = std::max((coord_t) 0, std::min(a_max, b_max) - std::max(a_min, b_min));
    // [INTENT] Convert from X-projection units back to arc-length units.
    overlap_length /= ((double) a_max - a_min) / this->length();
    return true;
}

bool Line::perpendicular_to(double angle) const { return Slic3r::Geometry::directions_perpendicular(this->direction(), angle); }

// [INTENT] Two lines are perpendicular if their dot product is near zero.
// Uses the normalized criterion: dot^2 < EPSILON^2 * |v1|^2 * |v2|^2.
bool Line::perpendicular_to(const Line& line) const
{
    const Vec2d v1 = (this->b - this->a).cast<double>();
    const Vec2d v2 = (line.b - line.a).cast<double>();
    return sqr(v1.dot(v2)) < sqr(EPSILON) * v1.squaredNorm() * v2.squaredNorm();
}

bool Line::intersection(const Line& l2, Point* intersection) const { return line_alg::intersection(*this, l2, intersection); }

// [INTENT] Clips this line segment to the given bounding box using the Liang-Barsky algorithm.
// Converts to double for the clipping, then converts results back to coord_t.
// Returns false (and leaves a/b unchanged) if the segment is fully outside the bbox.
bool Line::clip_with_bbox(const BoundingBox& bbox)
{
    Vec2d x0clip, x1clip;
    bool  result = Geometry::liang_barsky_line_clipping<double>(this->a.cast<double>(), this->b.cast<double>(),
                                                                BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>()), x0clip,
                                                                x1clip);
    if (result) {
        this->a = x0clip.cast<coord_t>();
        this->b = x1clip.cast<coord_t>();
    }
    return result;
}

// [INTENT] Extends both endpoints of the segment outward by `offset` mm (in scaled coord_t units).
// The extension direction for `a` is b→a; for `b` it is a→b. The vector is cast to coord_t
// after scaling by the normalized direction — sub-pixel rounding is introduced at this step.
// [HAZARD] H645: For zero-length lines (a == b), vector().cast<double>().normalized() produces
// NaN. The offset_vector will be NaN cast to coord_t (implementation-defined behavior).
// No guard for zero-length lines in extend().
void Line::extend(double offset)
{
    Vector offset_vector = (offset * this->vector().cast<double>().normalized()).cast<coord_t>();
    this->a -= offset_vector;
    this->b += offset_vector;
}

// [INTENT] Intersects the infinite line with a horizontal plane at z.
// Returns the 3D point where the line crosses z.
// [HAZARD] H639: If v(2) == 0 (horizontal line), division by zero → NaN. No guard.
Vec3d Linef3::intersect_plane(double z) const
{
    Vec3d  v = (this->b - this->a).cast<double>();
    double t = (z - this->a(2)) / v(2);
    return Vec3d(this->a(0) + v(0) * t, this->a(1) + v(1) * t, z);
}

// [INTENT] Accumulates bounding box over a flat vector of Lines. Returns an undefined BoundingBox
// if `lines` is empty (no merges performed; bbox.defined remains false).
BoundingBox get_extents(const Lines& lines)
{
    BoundingBox bbox;
    for (const Line& line : lines) {
        bbox.merge(line.a);
        bbox.merge(line.b);
    }
    return bbox;
}

} // namespace Slic3r
