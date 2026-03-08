// [INTENT] Implementation of Point methods and free functions declared in Point.hpp.
//          Also provides batch-transform functions for Vec3f/Vec3d point clouds using Eigen
//          matrix operations (more efficient than per-point transforms for large meshes).
// [COUPLING] Includes Int128.hpp for exact 128-bit orientation predicates; includes
//            MultiPoint.hpp and Line.hpp for projection_onto() implementations.
#include "Point.hpp"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Int128.hpp"
#include "BoundingBox.hpp"
#include <algorithm>

namespace Slic3r {

// [INTENT] Batch-transform a vector of Vec3f points by a Transform3f using Eigen's matrix multiply.
//          Uses memcpy to/from a MatrixXf to avoid per-point overhead. The homogeneous() call
//          appends a 1.0 row to handle affine translation correctly.
// [HAZARD] memcpy assumes Vec3f is a contiguous 3-float struct with no padding — true for Eigen
//          DontAlign vectors, but could silently break if Vec3f's Eigen storage changes.
// [MEMORY] Allocates two temporary Eigen matrices (3×N) on the heap — may be expensive for very
//          large meshes. Consider in-place transform variants for memory-constrained paths.
std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t)
{
    unsigned int vertices_count = (unsigned int) points.size();
    if (vertices_count == 0)
        return std::vector<Vec3f>();

    unsigned int data_size = 3 * vertices_count * sizeof(float);

    Eigen::MatrixXf src(3, vertices_count);
    ::memcpy((void*) src.data(), (const void*) points.data(), data_size);

    Eigen::MatrixXf dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    std::vector<Vec3f> ret_points(vertices_count, Vec3f::Zero());
    ::memcpy((void*) ret_points.data(), (const void*) dst.data(), data_size);
    return ret_points;
}

// [INTENT] Double-precision variant of the batch transform above, for Pointf3s (Vec3d).
// [HAZARD] Same memcpy/padding assumption as the float variant (H hazard applies equally).
Pointf3s transform(const Pointf3s& points, const Transform3d& t)
{
    unsigned int vertices_count = (unsigned int) points.size();
    if (vertices_count == 0)
        return Pointf3s();

    unsigned int data_size = 3 * vertices_count * sizeof(double);

    Eigen::MatrixXd src(3, vertices_count);
    ::memcpy((void*) src.data(), (const void*) points.data(), data_size);

    Eigen::MatrixXd dst(3, vertices_count);
    dst = t * src.colwise().homogeneous();

    Pointf3s ret_points(vertices_count, Vec3d::Zero());
    ::memcpy((void*) ret_points.data(), (const void*) dst.data(), data_size);
    return ret_points;
}

// [INTENT] Rotate this point around an explicit center point by angle (radians).
//          Uses fast_round_up<coord_t> instead of std::round for slightly better performance.
// [HAZARD H608] fast_round_up truncates fractional bits; repeated rotations accumulate error
//               (same hazard as the inline rotate(cos,sin) in Point.hpp).
// [STATE] Modifies this->x() and this->y() in-place.
void Point::rotate(double angle, const Point& center)
{
    Vec2d  cur = this->cast<double>();
    double s   = ::sin(angle);
    double c   = ::cos(angle);
    Vec2d  d   = cur - center.cast<double>();
    this->x()  = fast_round_up<coord_t>(center.x() + c * d.x() - s * d.y());
    this->y()  = fast_round_up<coord_t>(center.y() + s * d.x() + c * d.y());
}

/* Three points are a counter-clockwise turn if ccw > 0, clockwise if
 * ccw < 0, and collinear if ccw = 0 because ccw is a determinant that
 * gives the signed area of the triangle formed by p1, p2 and this point.
 * In other words it is the 2D cross product of p1-p2 and p1-this, i.e.
 * z-component of their 3D cross product.
 * We return double because it must be big enough to hold 2*max(|coordinate|)^2
 */
// [INTENT] ccw(p1, p2): signed area of the triangle (p1, p2, this).
//          Positive = CCW turn, Negative = CW turn, Zero = collinear.
//          Uses cast<double>() to avoid integer overflow in the cross product.
// [UNCLEAR] A commented-out alternative using int64_t cast exists above — that would be
//           exact (no floating-point rounding) but risks overflow for large coord_t values.
//           The current float path introduces tiny rounding errors but is overflow-safe.
double Point::ccw(const Point& p1, const Point& p2) const
{
    // static_assert(sizeof(coord_t) == 4, "Point::ccw() requires a 32 bit coord_t");
    // return cross2((p2 - p1).cast<int64_t>(), (*this - p1).cast<int64_t>());
    return cross2((p2 - p1).cast<double>(), (*this - p1).cast<double>());
}

double Point::ccw(const Line& line) const { return this->ccw(line.a, line.b); }

// [INTENT] ccw_angle(p1, p2): returns the CCW angle from direction (this→p1) to (this→p2),
//          measured at 'this'. Always returns a value in (0, 2π].
// [HAZARD] Uses two atan2() calls — O(1) but relatively expensive due to transcendental math.
//          The FIXME comment in source acknowledges this; project-one-into-the-other would use
//          only one atan2 + dot/cross. Hot-path callers should be profiled.
// returns the CCW angle between this-p1 and this-p2
// i.e. this assumes a CCW rotation from p1 to p2 around this
double Point::ccw_angle(const Point& p1, const Point& p2) const
{
    // FIXME this calculates an atan2 twice! Project one vector into the other!
    double angle = atan2(p1.x() - (*this).x(), p1.y() - (*this).y()) - atan2(p2.x() - (*this).x(), p2.y() - (*this).y());
    // we only want to return only positive angles
    return angle <= 0 ? angle + 2 * PI : angle;
}

// [INTENT] projection_onto(MultiPoint): finds the closest orthogonal projection of this point
//          onto any segment of the given polyline/polygon. Linear scan O(N segments).
//          running_min tracks the shortest distance found so far.
// [HAZARD] Does NOT early-exit when distance becomes zero. For degenerate inputs (this point
//          is exactly on the polyline) it still scans all segments.
// [COUPLING] Calls poly.lines() which materialises all Line objects — allocation overhead.
Point Point::projection_onto(const MultiPoint& poly) const
{
    Point  running_projection = poly.first_point();
    double running_min        = (running_projection - *this).cast<double>().norm();

    Lines lines = poly.lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
        Point point_temp = this->projection_onto(*line);
        if ((point_temp - *this).cast<double>().norm() < running_min) {
            running_projection = point_temp;
            running_min        = (running_projection - *this).cast<double>().norm();
        }
    }
    return running_projection;
}

// [INTENT] projection_onto(Line): orthogonally projects this point onto the given line segment.
//          Uses the parametric line formula: theta is the parameter along the segment [0, 1].
//          If theta is outside [0,1], the closest endpoint is returned instead (clamped projection).
// [HAZARD] If line.a == line.b (degenerate segment), returns line.a immediately.
//          The divisor sqr(lx) + sqr(ly) could be zero for a truly degenerate line — avoided by
//          the early return, but callers must guarantee no degenerate lines reach this path.
// [COUPLING] Cast to coordf_t (double) for the intermediate blended result, then back to coord_t.
//            Precision lost on the final cast — sub-micron rounding is expected and acceptable.
Point Point::projection_onto(const Line& line) const
{
    if (line.a == line.b)
        return line.a;

    /*
        (Ported from VisiLibity by Karl J. Obermeyer)
        The projection of point_temp onto the line determined by
        line_segment_temp can be represented as an affine combination
        expressed in the form projection of
        Point = theta*line_segment_temp.first + (1.0-theta)*line_segment_temp.second.
        If theta is outside the interval [0,1], then one of the Line_Segment's endpoints
        must be closest to calling Point.
    */
    double lx    = (double) (line.b(0) - line.a(0));
    double ly    = (double) (line.b(1) - line.a(1));
    double theta = ((double) (line.b(0) - (*this)(0)) * lx + (double) (line.b(1) - (*this)(1)) * ly) / (sqr<double>(lx) + sqr<double>(ly));

    if (0.0 <= theta && theta <= 1.0)
        return (theta * line.a.cast<coordf_t>() + (1.0 - theta) * line.b.cast<coordf_t>()).cast<coord_t>();

    // Else pick closest endpoint.
    return ((line.a - *this).cast<double>().squaredNorm() < (line.b - *this).cast<double>().squaredNorm()) ? line.a : line.b;
}

// [INTENT] has_duplicate_points(): global duplicate check via sort + adjacent scan.
//          Takes a Points by value (move semantics) to avoid copying.
//          O(N log N) due to sort.
bool has_duplicate_points(Points&& pts)
{
    std::sort(pts.begin(), pts.end());
    for (size_t i = 1; i < pts.size(); ++i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}

// [INTENT] collect_duplicates(): returns a deduplicated list of points that appear more than once.
//          Sort-based O(N log N). The inner deduplication (`only unique duplicits`) ensures each
//          duplicated point appears exactly once in the output, regardless of how many copies exist.
// [HAZARD] Accesses pts.front() without checking if pts is empty — will crash on empty input.
Points collect_duplicates(Points pts /* Copy */)
{
    std::sort(pts.begin(), pts.end());
    Points       duplicits;
    const Point* prev = &pts.front();
    for (size_t i = 1; i < pts.size(); ++i) {
        const Point* act = &pts[i];
        if (*prev == *act) {
            // duplicit point
            if (!duplicits.empty() && duplicits.back() == *act)
                continue; // only unique duplicits
            duplicits.push_back(*act);
        }
        prev = act;
    }
    return duplicits;
}

// [INTENT] get_extents<IncludeBoundary>: computes BoundingBox from a Points vector.
//          Explicit template instantiations for both IncludeBoundary=true/false are provided
//          below so the definitions can live in this .cpp rather than the header.
template<bool IncludeBoundary> BoundingBox get_extents(const Points& pts)
{
    BoundingBox out;
    BoundingBox::construct<IncludeBoundary>(out, pts.begin(), pts.end());
    return out;
}
template BoundingBox get_extents<false>(const Points& pts);
template BoundingBox get_extents<true>(const Points& pts);

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary> BoundingBox get_extents(const VecOfPoints& pts)
{
    BoundingBox bbox;
    for (const Points& p : pts)
        bbox.merge(get_extents<IncludeBoundary>(p));
    return bbox;
}
template BoundingBox get_extents<false>(const VecOfPoints& pts);
template BoundingBox get_extents<true>(const VecOfPoints& pts);

BoundingBoxf get_extents(const std::vector<Vec2d>& pts)
{
    BoundingBoxf bbox;
    for (const Vec2d& p : pts)
        bbox.merge(p);
    return bbox;
}

// [INTENT] nearest_point_index(Points): wraps the PointConstPtrs overload for convenience.
//          Builds a temporary PointConstPtrs vector — O(N) overhead in addition to the O(N) scan.
int Point::nearest_point_index(const Points& points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(&*it);
    return this->nearest_point_index(p);
}

// [INTENT] nearest_point_index(PointConstPtrs): linear O(N) nearest-neighbor scan.
//          Uses an early-exit on X distance then squared distance to avoid sqrt until necessary.
//          Returns -1 if points is empty.
// [HAZARD] Uses `distance == -1` as a sentinel for "not yet initialized". This sentinel is
//          ambiguous if a valid squared distance happened to round to exactly -1 (not possible
//          for squared distances, but the idiom is fragile). A cleaner port should use
//          std::optional or std::numeric_limits<double>::max() as the initial value.
int Point::nearest_point_index(const PointConstPtrs& points) const
{
    int    idx      = -1;
    double distance = -1; // double because long is limited to 2147483647 on some platforms and it's not enough

    for (PointConstPtrs::const_iterator it = points.begin(); it != points.end(); ++it) {
        /* If the X distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        double d = sqr<double>((*this)(0) - (*it)->x());
        if (distance != -1 && d > distance)
            continue;

        /* If the Y distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        d += sqr<double>((*this)(1) - (*it)->y());
        if (distance != -1 && d > distance)
            continue;

        idx      = it - points.begin();
        distance = d;

        if (distance < EPSILON)
            break;
    }

    return idx;
}

// [INTENT] nearest_point_index(PointPtrs): adapts the mutable-pointer variant to the const-pointer
//          overload by building a temporary PointConstPtrs — same O(N) overhead as above.
int Point::nearest_point_index(const PointPtrs& points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (PointPtrs::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(*it);
    return this->nearest_point_index(p);
}

bool Point::nearest_point(const Points& points, Point* point) const
{
    int idx = this->nearest_point_index(points);
    if (idx == -1)
        return false;
    *point = points.at(idx);
    return true;
}

std::ostream& operator<<(std::ostream& stm, const Vec2d& pointf) { return stm << pointf(0) << "," << pointf(1); }

// [INTENT] int128::orient() and int128::cross(): exact orientation predicates.
//          Delegate to Int128::sign_determinant_2x2_filtered() which uses 128-bit integer
//          arithmetic to compute the sign of the 2×2 determinant exactly, avoiding floating-point
//          rounding errors that would corrupt winding-order tests at extreme coordinates.
// [HAZARD] Int128::sign_determinant_2x2_filtered() is implemented using __int128 or an
//          equivalent 128-bit integer type. This is GCC/Clang-specific; MSVC requires a
//          software-emulation fallback. See Int128.hpp for platform-specific details.
// [COUPLING] Called by winding-order tests in Geometry, ClipperUtils, and Voronoi construction.
namespace int128 {

int orient(const Vec2crd& p1, const Vec2crd& p2, const Vec2crd& p3)
{
    Slic3r::Vector v1(p2 - p1);
    Slic3r::Vector v2(p3 - p1);
    return Int128::sign_determinant_2x2_filtered(v1.x(), v1.y(), v2.x(), v2.y());
}

int cross(const Vec2crd& v1, const Vec2crd& v2) { return Int128::sign_determinant_2x2_filtered(v1.x(), v1.y(), v2.x(), v2.y()); }

} // namespace int128

} // namespace Slic3r
