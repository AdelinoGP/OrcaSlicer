// [INTENT] Central type header for all 2D/3D coordinate primitives used throughout the slicer.
// Defines the dual coordinate system: scaled integer coords (coord_t, factor 1e6) and floating-point
// coords (float/double mm). All geometry algorithms operate in scaled integers; G-code output
// unscales back to mm. Mixing the two systems without explicit conversion is a pervasive hazard.
// [COUPLING] Included (directly or transitively) by virtually every source file. Changes here
//            cascade across the entire codebase.
// [MEMORY]   Points typedef uses tbb::scalable_allocator — not std::allocator. Any port must
//            account for different allocator semantics and ABI differences (H601).
// [HAZARD H601] tbb::scalable_allocator baked into Points type alias; breaks ABI on any port
//               that uses std::allocator instead.
#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include "libslic3r.h"
#include <cstddef>
#include <vector>
#include <cmath>
#include <string>
#include <sstream>
#include <unordered_map>

#include <oneapi/tbb/scalable_allocator.h>

#include <Eigen/Geometry>

#include "LocalesUtils.hpp"

namespace Slic3r {

class BoundingBox;
class BoundingBoxf;
class Line;
class MultiPoint;
class Point;
using Vector = Point;
// [INTENT] All vector/point types are Eigen matrices with DontAlign to avoid SIMD alignment
// requirements. Vec2crd/Vec3crd use scaled-integer coord_t; Vec2f/Vec3f use float;
// Vec2d/Vec3d/Vec4d use double. The type system does NOT prevent mixing scaled and unscaled coords.
// [HAZARD H605] Vec2crd and Vec2d are distinct types, but implicit casts exist in some paths;
//               converting without unscale()/scale() loses the factor-of-1e6 silently.
template<int N, int M, class T> using Mat = Eigen::Matrix<T, N, M, Eigen::DontAlign, N, M>;

template<int N, class T> using Vec = Mat<N, 1, T>;

// Eigen types, to replace the Slic3r's own types in the future.
// Vector types with a fixed point coordinate base type.
using Vec2crd = Eigen::Matrix<coord_t, 2, 1, Eigen::DontAlign>;
using Vec3crd = Eigen::Matrix<coord_t, 3, 1, Eigen::DontAlign>;
// using Vec2i   = Eigen::Matrix<int,      2, 1, Eigen::DontAlign>;
// using Vec3i   = Eigen::Matrix<int,      3, 1, Eigen::DontAlign>;
// using Vec4i   = Eigen::Matrix<int,      4, 1, Eigen::DontAlign>;
using Vec2i32 = Eigen::Matrix<int32_t, 2, 1, Eigen::DontAlign>;
using Vec2i64 = Eigen::Matrix<int64_t, 2, 1, Eigen::DontAlign>;
using Vec3i32 = Eigen::Matrix<int32_t, 3, 1, Eigen::DontAlign>;
using Vec3i64 = Eigen::Matrix<int64_t, 3, 1, Eigen::DontAlign>;
using Vec4i32 = Eigen::Matrix<int32_t, 4, 1, Eigen::DontAlign>;

// Vector types with a double coordinate base type.
using Vec2f = Eigen::Matrix<float, 2, 1, Eigen::DontAlign>;
using Vec3f = Eigen::Matrix<float, 3, 1, Eigen::DontAlign>;
using Vec4f = Eigen::Matrix<float, 4, 1, Eigen::DontAlign>;
using Vec2d = Eigen::Matrix<double, 2, 1, Eigen::DontAlign>;
using Vec3d = Eigen::Matrix<double, 3, 1, Eigen::DontAlign>;
using Vec4d = Eigen::Matrix<double, 4, 1, Eigen::DontAlign>;
// [INTENT] PointsAllocator uses TBB's scalable_allocator for thread-local heap pooling,
// improving allocation performance in parallelized loops. Points is the canonical container
// for 2D integer-coord point sequences used by Polygon, Polyline, etc.
// [MEMORY] tbb::scalable_allocator<Point> — NOT std::allocator. Returned vectors are NOT
//          compatible with std::vector<Point> in binary interfaces.
// [CONCURRENCY] TBB allocator is thread-safe; each thread pool maintains its own heap.
template<typename BaseType> using PointsAllocator = tbb::scalable_allocator<BaseType>;
using Points                                      = std::vector<Point, PointsAllocator<Point>>;
using PointPtrs                                   = std::vector<Point*>;
using PointConstPtrs                              = std::vector<const Point*>;
using Points3                                     = std::vector<Vec3crd>;
using Pointfs                                     = std::vector<Vec2d>;
using Vec2ds                                      = std::vector<Vec2d>;
using Pointf3s                                    = std::vector<Vec3d>;

using VecOfPoints = std::vector<Points, PointsAllocator<Points>>;

using Matrix2f = Eigen::Matrix<float, 2, 2, Eigen::DontAlign>;
using Matrix2d = Eigen::Matrix<double, 2, 2, Eigen::DontAlign>;
using Matrix3f = Eigen::Matrix<float, 3, 3, Eigen::DontAlign>;
using Matrix3d = Eigen::Matrix<double, 3, 3, Eigen::DontAlign>;
using Matrix4f = Eigen::Matrix<float, 4, 4, Eigen::DontAlign>;
using Matrix4d = Eigen::Matrix<double, 4, 4, Eigen::DontAlign>;

template<int N, class T> using Transform = Eigen::Transform<float, N, Eigen::Affine, Eigen::DontAlign>;

using Transform2f = Eigen::Transform<float, 2, Eigen::Affine, Eigen::DontAlign>;
using Transform2d = Eigen::Transform<double, 2, Eigen::Affine, Eigen::DontAlign>;
using Transform3f = Eigen::Transform<float, 3, Eigen::Affine, Eigen::DontAlign>;
using Transform3d = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>;

// using ColorRGBA      = std::array<float, 4>;
// I don't know why Eigen::Transform::Identity() return a const object...
template<int N, class T> Transform<N, T> identity() { return Transform<N, T>::Identity(); }
inline const auto&                       identity3f = identity<3, float>;
inline const auto&                       identity3d = identity<3, double>;

inline bool operator<(const Vec2d& lhs, const Vec2d& rhs) { return lhs.x() < rhs.x() || (lhs.x() == rhs.x() && lhs.y() < rhs.y()); }

template<int Options>
int32_t cross2(const Eigen::MatrixBase<Eigen::Matrix<int32_t, 2, 1, Options>>& v1,
               const Eigen::MatrixBase<Eigen::Matrix<int32_t, 2, 1, Options>>& v2) = delete;

template<typename T, int Options>
inline T cross2(const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>>& v1, const Eigen::MatrixBase<Eigen::Matrix<T, 2, 1, Options>>& v2)
{
    return v1.x() * v2.y() - v1.y() * v2.x();
}

template<typename Derived, typename Derived2>
inline typename Derived::Scalar cross2(const Eigen::MatrixBase<Derived>& v1, const Eigen::MatrixBase<Derived2>& v2)
{
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value,
                  "cross2(): Scalar types of 1st and 2nd operand must be equal.");
    return v1.x() * v2.y() - v1.y() * v2.x();
}

// 2D vector perpendicular to the argument.
template<typename Derived> inline Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> perp(const Eigen::MatrixBase<Derived>& v)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "perp(): parameter is not a 2D vector");
    return {-v.y(), v.x()};
}

// Angle from v1 to v2, returning double atan2(y, x) normalized to <-PI, PI>.
template<typename Derived, typename Derived2>
inline double angle(const Eigen::MatrixBase<Derived>& v1, const Eigen::MatrixBase<Derived2>& v2)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "angle(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "angle(): second parameter is not a 2D vector");
    auto v1d = v1.template cast<double>();
    auto v2d = v2.template cast<double>();
    return atan2(cross2(v1d, v2d), v1d.dot(v2d));
}

template<typename Derived> Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> to_2d(const Eigen::MatrixBase<Derived>& ptN)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) >= 3,
                  "to_2d(): first parameter is not a 3D or higher dimensional vector");
    return ptN.template head<2>();
}

template<typename Derived>
inline Eigen::Matrix<typename Derived::Scalar, 3, 1, Eigen::DontAlign> to_3d(const Eigen::MatrixBase<Derived>& pt,
                                                                             const typename Derived::Scalar    z)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "to_3d(): first parameter is not a 2D vector");
    return {pt.x(), pt.y(), z};
}

inline Vec2d unscale(coord_t x, coord_t y) { return Vec2d(unscale<double>(x), unscale<double>(y)); }
inline Vec2d unscale(const Vec2crd& pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec2d unscale(const Vec2d& pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec3d unscale(coord_t x, coord_t y, coord_t z) { return Vec3d(unscale<double>(x), unscale<double>(y), unscale<double>(z)); }
inline Vec3d unscale(const Vec3crd& pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }
inline Vec3d unscale(const Vec3d& pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }

inline std::string to_string(const Vec2crd& pt)
{
    return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]";
}
inline std::string to_string(const Vec2d& pt)
{
    return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]";
}
inline std::string to_string(const Vec3crd& pt)
{
    return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " +
           float_to_string_decimal_point(pt.z()) + "]";
}
inline std::string to_string(const Vec3d& pt)
{
    return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " +
           float_to_string_decimal_point(pt.z()) + "]";
}

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t);
Pointf3s           transform(const Pointf3s& points, const Transform3d& t);

/// <summary>
/// Check whether transformation matrix contains odd number of mirroring.
/// NOTE: In code is sometime function named is_left_handed
/// </summary>
/// <param name="transform">Transformation to check</param>
/// <returns>Is positive determinant</returns>
inline bool has_reflection(const Transform3d& transform) { return transform.matrix().determinant() < 0; }

/// <summary>
/// Getter on base of transformation matrix
/// </summary>
/// <param name="index">column index</param>
/// <param name="transform">source transformation</param>
/// <returns>Base of transformation matrix</returns>
inline const Vec3d get_base(unsigned index, const Transform3d& transform) { return transform.linear().col(index); }
inline const Vec3d get_x_base(const Transform3d& transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d& transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d& transform) { return get_base(2, transform); }
inline const Vec3d get_base(unsigned index, const Transform3d::LinearPart& transform) { return transform.col(index); }
inline const Vec3d get_x_base(const Transform3d::LinearPart& transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d::LinearPart& transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d::LinearPart& transform) { return get_base(2, transform); }

template<int N, class T> using Vec = Eigen::Matrix<T, N, 1, Eigen::DontAlign, N, 1>;

// [INTENT] Point is the fundamental 2D integer-coordinate type. Inherits from Vec2crd
// (Eigen Matrix<coord_t,2,1,DontAlign>) to get Eigen's operator overloads, norm(), cast<>() etc.
// All coordinates are in SCALED units (coord_t = int32_t or int64_t, multiplied by 1e6).
// [STATE] x() and y() accessors from Eigen; no additional state beyond the 2 coord_t values.
// [HAZARD H606] Double constructor rounds via std::round — loses sub-nanometer precision.
//               For most slicer uses this is correct, but callers must not expect exact results
//               when constructing from floating-point coordinates derived from model geometry.
// [COUPLING] used as base by Polygon::points, Polyline::points (via MultiPoint). Any change
//            to the memory layout of Point breaks all of those collections.
class Point : public Vec2crd
{
public:
    using coord_type = coord_t;

    Point() : Vec2crd(0, 0) {}
    Point(int32_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int64_t x, int32_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(int32_t x, int64_t y) : Vec2crd(coord_t(x), coord_t(y)) {}
    Point(double x, double y) : Vec2crd(coord_t(std::round(x)), coord_t(std::round(y))) {}
    Point(const Point& rhs) { *this = rhs; }
    explicit Point(const Vec2d& rhs) : Vec2crd(coord_t(std::round(rhs.x())), coord_t(std::round(rhs.y()))) {}
    // This constructor allows you to construct Point from Eigen expressions
    // This constructor has to be implicit (non-explicit) to allow implicit conversion from Eigen expressions.
    template<typename OtherDerived> Point(const Eigen::MatrixBase<OtherDerived>& other) : Vec2crd(other) {}
    // [INTENT] new_scale() is the canonical mm→scaled_integer converter.
    // [HAZARD H607] scale_() macro multiplies by SCALING_FACTOR (1e6); truncation to coord_t
    //               can silently overflow for coordinates > ~2147m if coord_t is int32_t.
    static Point                                 new_scale(coordf_t x, coordf_t y) { return Point(coord_t(scale_(x)), coord_t(scale_(y))); }
    template<typename OtherDerived> static Point new_scale(const Eigen::MatrixBase<OtherDerived>& v)
    {
        return Point(coord_t(scale_(v.x())), coord_t(scale_(v.y())));
    }

    // This method allows you to assign Eigen expressions to MyVectorType
    template<typename OtherDerived> Point& operator=(const Eigen::MatrixBase<OtherDerived>& other)
    {
        this->Vec2crd::operator=(other);
        return *this;
    }

    Point& operator+=(const Point& rhs)
    {
        this->x() += rhs.x();
        this->y() += rhs.y();
        return *this;
    }
    Point& operator-=(const Point& rhs)
    {
        this->x() -= rhs.x();
        this->y() -= rhs.y();
        return *this;
    }
    Point& operator*=(const double& rhs)
    {
        this->x() = coord_t(this->x() * rhs);
        this->y() = coord_t(this->y() * rhs);
        return *this;
    }
    Point operator*(const double& rhs) { return Point(this->x() * rhs, this->y() * rhs); }
    // [INTENT] both_comp/any_comp: component-wise comparison helpers.
    //          both_comp returns true iff the condition holds for BOTH x and y.
    //          any_comp  returns true iff the condition holds for EITHER x or y.
    // [UNCLEAR] Operator is passed as a runtime std::string rather than a template or enum.
    //           This incurs string comparison overhead and is a code-smell; likely added for
    //           scripting convenience. A port should replace with typed overloads or an enum.
    bool both_comp(const Point& rhs, const std::string& op)
    {
        if (op == ">")
            return this->x() > rhs.x() && this->y() > rhs.y();
        else if (op == "<")
            return this->x() < rhs.x() && this->y() < rhs.y();
        return false;
    }
    bool any_comp(const Point& rhs, const std::string& op)
    {
        if (op == ">")
            return this->x() > rhs.x() || this->y() > rhs.y();
        else if (op == "<")
            return this->x() < rhs.x() || this->y() < rhs.y();
        return false;
    }

    // [INTENT] rotate(cos_a, sin_a) avoids repeated trig when rotating many points by the same angle.
    // [HAZARD H608] Result snapped to coord_t via round(); repeated rotations may accumulate rounding error.
    void rotate(double angle) { this->rotate(std::cos(angle), std::sin(angle)); }
    void rotate(double cos_a, double sin_a)
    {
        double cur_x = (double) this->x();
        double cur_y = (double) this->y();
        this->x()    = (coord_t) round(cos_a * cur_x - sin_a * cur_y);
        this->y()    = (coord_t) round(cos_a * cur_y + sin_a * cur_x);
    }

    // [INTENT] rotate(angle, center) is in Point.cpp and uses fast_round_up for rounding.
    void  rotate(double angle, const Point& center);
    Point rotated(double angle) const
    {
        Point res(*this);
        res.rotate(angle);
        return res;
    }
    Point rotated(double cos_a, double sin_a) const
    {
        Point res(*this);
        res.rotate(cos_a, sin_a);
        return res;
    }
    Point rotated(double angle, const Point& center) const
    {
        Point res(*this);
        res.rotate(angle, center);
        return res;
    }
    Point  rotate_90_degree_ccw() const { return Point(-this->y(), this->x()); }
    int    nearest_point_index(const Points& points) const;
    int    nearest_point_index(const PointConstPtrs& points) const;
    int    nearest_point_index(const PointPtrs& points) const;
    bool   nearest_point(const Points& points, Point* point) const;
    double ccw(const Point& p1, const Point& p2) const;
    double ccw(const Line& line) const;
    double ccw_angle(const Point& p1, const Point& p2) const;
    Point  projection_onto(const MultiPoint& poly) const;
    Point  projection_onto(const Line& line) const;

    double distance_to(const Point& point) const { return (point - *this).cast<double>().norm(); }
};

inline bool operator<(const Point& l, const Point& r) { return l.x() < r.x() || (l.x() == r.x() && l.y() < r.y()); }

inline Point operator*(const Point& l, const double& r) { return {coord_t(l.x() * r), coord_t(l.y() * r)}; }

inline std::ostream& operator<<(std::ostream& os, const Point& pt)
{
    os << unscale_(pt.x()) << "," << unscale_(pt.y());
    return os;
}

inline bool is_approx(const Point& p1, const Point& p2, coord_t epsilon = coord_t(SCALED_EPSILON))
{
    Point d = (p2 - p1).cwiseAbs();
    return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2f& p1, const Vec2f& p2, float epsilon = float(EPSILON))
{
    Vec2f d = (p2 - p1).cwiseAbs();
    return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2d& p1, const Vec2d& p2, double epsilon = EPSILON)
{
    Vec2d d = (p2 - p1).cwiseAbs();
    return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec3f& p1, const Vec3f& p2, float epsilon = float(EPSILON))
{
    Vec3f d = (p2 - p1).cwiseAbs();
    return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline bool is_approx(const Vec3d& p1, const Vec3d& p2, double epsilon = EPSILON)
{
    Vec3d d = (p2 - p1).cwiseAbs();
    return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline Point lerp(const Point& a, const Point& b, double t)
{
    assert((t >= -EPSILON) && (t <= 1. + EPSILON));
    return ((1. - t) * a.cast<double>() + t * b.cast<double>()).cast<coord_t>();
}

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary = false> BoundingBox get_extents(const Points& pts);
extern template BoundingBox                        get_extents<false>(const Points& pts);
extern template BoundingBox                        get_extents<true>(const Points& pts);

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary = false> BoundingBox get_extents(const VecOfPoints& pts);
extern template BoundingBox                        get_extents<false>(const VecOfPoints& pts);
extern template BoundingBox                        get_extents<true>(const VecOfPoints& pts);

BoundingBoxf get_extents(const std::vector<Vec2d>& pts);

// Test for duplicate points in a vector of points.
// The points are copied, sorted and checked for duplicates globally.
bool        has_duplicate_points(Points&& pts);
inline bool has_duplicate_points(const Points& pts)
{
    Points cpy = pts;
    return has_duplicate_points(std::move(cpy));
}

// Test for duplicate points in a vector of points.
// Only successive points are checked for equality.
inline bool has_duplicate_successive_points(const Points& pts)
{
    for (size_t i = 1; i < pts.size(); ++i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}

// Test for duplicate points in a vector of points.
// Only successive points are checked for equality. Additionally, first and last points are compared for equality.
inline bool has_duplicate_successive_points_closed(const Points& pts)
{
    return has_duplicate_successive_points(pts) || (pts.size() >= 2 && pts.front() == pts.back());
}

// Collect adjecent(duplicit points)
Points collect_duplicates(Points pts /* Copy */);

inline bool shorter_then(const Point& p0, const coord_t len)
{
    if (p0.x() > len || p0.x() < -len)
        return false;
    if (p0.y() > len || p0.y() < -len)
        return false;
    return p0.cast<int64_t>().squaredNorm() <= Slic3r::sqr(int64_t(len));
}

// [INTENT] int128 namespace provides exact integer orientation predicates using 128-bit arithmetic.
// orient() and cross() avoid the floating-point precision loss that would occur if coord_t values
// (up to ~2e9 in scaled units) were multiplied: the intermediate product can reach ~4e18, which
// overflows int64_t but fits in int128. These are correctness-critical for winding-order tests.
// [HAZARD] 128-bit integer arithmetic is NOT available on all platforms/compilers.
//          GCC/Clang have __int128; MSVC does not. Any Windows port requires a software emulation
//          (e.g., __int64 pair). Current impl in Point.cpp uses __int128 directly via Int128 struct.
// [COUPLING] Called by Geometry, ClipperUtils, and winding-order checks throughout the slicer.
namespace int128 {
// Exact orientation predicate,
// returns +1: CCW, 0: collinear, -1: CW.
int orient(const Vec2crd& p1, const Vec2crd& p2, const Vec2crd& p3);
// Exact orientation predicate,
// returns +1: CCW, 0: collinear, -1: CW.
int cross(const Vec2crd& v1, const Vec2crd& v2);
} // namespace int128

// [INTENT] PointHash: FNV-inspired hash for Vec2crd, used as the key hasher in
//          ClosestPointInRadiusLookup's unordered_multimap and any other spatial hash maps.
//          The mixing constants (89, 31) are chosen to distribute coord_t values with high
//          spatial locality; collisions increase near grid boundaries but are bounded in practice.
// [HAZARD] Hash output is truncated to coord_t (32-bit if coord_t = int32_t), then implicitly
//          widened to size_t. On 64-bit systems the upper 32 bits are zero, increasing collision
//          probability. This is a pre-existing limitation.
// To be used by std::unordered_map, std::unordered_multimap and friends.
struct PointHash
{
    size_t operator()(const Vec2crd& pt) const noexcept { return coord_t((89 * 31 + int64_t(pt.x())) * 31 + pt.y()); }
};

// [INTENT] ClosestPointInRadiusLookup: 2D spatial hash map for efficient nearest-neighbor queries
//          within a fixed radius. Uses std::unordered_multimap keyed by grid-cell (Vec2crd).
//          Grid resolution is rounded UP to the next power-of-2 via bit-shifting so that
//          right-shift (>>) can serve as both key computation and query cell determination.
// [MEMORY] All values are stored by copy in m_map. Large ValueType objects will be expensive
//          to insert/query. Prefer pointer-like ValueType when values are large.
// [CONCURRENCY] NOT thread-safe. All inserts and queries must be serialized externally.
//               TBB parallel loops must NOT share a single instance across threads.
// [HAZARD H604] find() and find_all() only search 4 grid cells (2×2 neighborhood around the
//               rounded grid corner). Points near the boundary between grid cells may be missed
//               if the actual radius overlaps a cell that is not searched. This is an
//               approximate — not exhaustive — radius search.
// [COUPLING] PointAccessor is a caller-provided functor returning const Point* from ValueType.
//            If PointAccessor returns nullptr, that entry is silently skipped.
// A generic class to search for a closest Point in a given radius.
// It uses std::unordered_multimap to implement an efficient 2D spatial hashing.
// The PointAccessor has to return const Point*.
// If a nullptr is returned, it is ignored by the query.
template<typename ValueType, typename PointAccessor> class ClosestPointInRadiusLookup
{
public:
    // [INTENT] Constructor computes m_grid_log2 so that (coord >> m_grid_log2) maps a coordinate
    //          to its grid cell index. The resolution is rounded UP to the next power-of-2 so the
    //          shift is exact and the radius guarantee holds (a point within search_radius will
    //          always land in one of the 4 queried cells).
    // [STATE]  m_grid_log2 is the log2 of the actual grid cell size used for hashing.
    //          m_grid_resolution is the actual cell size (= 1 << m_grid_log2).
    ClosestPointInRadiusLookup(coord_t search_radius, PointAccessor point_accessor = PointAccessor())
        : m_search_radius(search_radius), m_point_accessor(point_accessor), m_grid_log2(0)
    {
        // Resolution of a grid, twice the search radius + some epsilon.
        coord_t gridres   = 2 * m_search_radius + 4;
        m_grid_resolution = gridres;
        assert(m_grid_resolution > 0);
        assert(m_grid_resolution < (coord_t(1) << 30));
        // Compute m_grid_log2 = log2(m_grid_resolution)
        if (m_grid_resolution > 32767) {
            m_grid_resolution >>= 16;
            m_grid_log2 += 16;
        }
        if (m_grid_resolution > 127) {
            m_grid_resolution >>= 8;
            m_grid_log2 += 8;
        }
        if (m_grid_resolution > 7) {
            m_grid_resolution >>= 4;
            m_grid_log2 += 4;
        }
        if (m_grid_resolution > 1) {
            m_grid_resolution >>= 2;
            m_grid_log2 += 2;
        }
        if (m_grid_resolution > 0)
            ++m_grid_log2;
        m_grid_resolution = 1 << m_grid_log2;
        assert(m_grid_resolution >= gridres);
        assert(gridres >= m_grid_resolution / 2);
    }

    void insert(const ValueType& value)
    {
        const Vec2crd* pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x() >> m_grid_log2, pt->y() >> m_grid_log2), value));
    }

    void insert(ValueType&& value)
    {
        const Vec2crd* pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x() >> m_grid_log2, pt->y() >> m_grid_log2), std::move(value)));
    }

    // Erase a data point equal to value. (ValueType has to declare the operator==).
    // Returns true if the data point equal to value was found and removed.
    bool erase(const ValueType& value)
    {
        const Point* pt = m_point_accessor(value);
        if (pt != nullptr) {
            // Range of fragment starts around grid_corner, close to pt.
            auto range = m_map.equal_range(Point((*pt).x() >> m_grid_log2, (*pt).y() >> m_grid_log2));
            // Remove the first item.
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == value) {
                    m_map.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    // Return a pair of <ValueType*, distance_squared>
    std::pair<const ValueType*, double> find(const Vec2crd& pt)
    {
        // Iterate over 4 closest grid cells around pt,
        // find the closest start point inside these cells to pt.
        const ValueType* value_min = nullptr;
        double           dist_min  = std::numeric_limits<double>::max();
        // Round pt to a closest grid_cell corner.
        Vec2crd grid_corner((pt.x() + (m_grid_resolution >> 1)) >> m_grid_log2, (pt.y() + (m_grid_resolution >> 1)) >> m_grid_log2);
        // For four neighbors of grid_corner:
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType& value = it->second;
                    const Vec2crd*   pt2   = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 < dist_min) {
                            dist_min  = d2;
                            value_min = &value;
                        }
                    }
                }
            }
        }
        return (value_min != nullptr && dist_min < coordf_t(m_search_radius) * coordf_t(m_search_radius)) ?
                   std::make_pair(value_min, dist_min) :
                   std::make_pair(nullptr, std::numeric_limits<double>::max());
    }

    // Returns all pairs of values and squared distances.
    std::vector<std::pair<const ValueType*, double>> find_all(const Vec2crd& pt)
    {
        // Iterate over 4 closest grid cells around pt,
        // Round pt to a closest grid_cell corner.
        Vec2crd grid_corner((pt.x() + (m_grid_resolution >> 1)) >> m_grid_log2, (pt.y() + (m_grid_resolution >> 1)) >> m_grid_log2);
        // For four neighbors of grid_corner:
        std::vector<std::pair<const ValueType*, double>> out;
        const double                                     r2 = double(m_search_radius) * m_search_radius;
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType& value = it->second;
                    const Vec2crd*   pt2   = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 <= r2)
                            out.emplace_back(&value, d2);
                    }
                }
            }
        }
        return out;
    }

private:
    using map_type = typename std::unordered_multimap<Vec2crd, ValueType, PointHash>;
    PointAccessor m_point_accessor;
    map_type      m_map;
    coord_t       m_search_radius;
    coord_t       m_grid_resolution;
    coord_t       m_grid_log2;
};

std::ostream& operator<<(std::ostream& stm, const Vec2d& pointf);

// [INTENT] Type-safe scaled/unscaled conversion helpers.
//          scaled<Tout>(v): converts floating-point mm value v into a scaled coordinate.
//            - If Tout is floating-point: returns v / SCALING_FACTOR (float or double scaled coord).
//            - If Tout is coord_t (integer): returns Tout(v / SCALING_FACTOR) — NOTE: no rounding,
//              straight truncation. Comment in source notes this is intentional but debatable.
//          unscaled<Tout>(v): converts an arithmetic (possibly scaled-integer) value back to mm.
//            - Returns Tout(v) * SCALING_FACTOR. Counter-intuitively multiplies (not divides) —
//              see SCALING_FACTOR definition: it equals 1/1e6, so multiply by SCALING_FACTOR = ÷1e6.
// [HAZARD] The unscaled() template multiplies by SCALING_FACTOR (= 1e-6) not divides. This is
//          correct only because SCALING_FACTOR is defined as the reciprocal of the scale factor.
//          A port that redefines SCALING_FACTOR as 1e6 (the multiplier) would silently invert all
//          coordinate conversions — a catastrophic error. Verify SCALING_FACTOR semantics carefully.
// [COUPLING] These templates are the canonical conversion path used throughout the codebase.
//            The inline unscale() overloads above (lines ~155-160) are older non-template wrappers
//            that predate these; both exist simultaneously. Prefer the typed templates in new code.

// /////////////////////////////////////////////////////////////////////////////
// Type safe conversions to and from scaled and unscaled coordinates
// /////////////////////////////////////////////////////////////////////////////

// Semantics are the following:
// Upscaling (scaled()): only from floating point types (or Vec) to either
//                       floating point or integer 'scaled coord' coordinates.
// Downscaling (unscaled()): from arithmetic (or Vec) to floating point only

// Conversion definition from unscaled to floating point scaled
template<class Tout, class Tin, class = FloatingOnly<Tin>> inline constexpr FloatingOnly<Tout> scaled(const Tin& v) noexcept
{
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion definition from unscaled to integer 'scaled coord'.
// TODO: is the rounding necessary? Here it is commented  out to show that
// it can be different for integers but it does not have to be. Using
// std::round means loosing noexcept and constexpr modifiers
template<class Tout = coord_t, class Tin, class = FloatingOnly<Tin>> inline constexpr ScaledCoordOnly<Tout> scaled(const Tin& v) noexcept
{
    // return static_cast<Tout>(std::round(v / SCALING_FACTOR));
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion for Eigen vectors (N dimensional points)
template<class Tout = coord_t, class Tin, int N, class = FloatingOnly<Tin>, int... EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<Tout>, N, EigenArgs...> scaled(const Eigen::Matrix<Tin, N, EigenArgs...>& v)
{
    return (v / SCALING_FACTOR).template cast<Tout>();
}

// Conversion from arithmetic scaled type to floating point unscaled
template<class Tout = double, class Tin, class = ArithmeticOnly<Tin>, class = FloatingOnly<Tout>>
inline constexpr Tout unscaled(const Tin& v) noexcept
{
    return Tout(v) * Tout(SCALING_FACTOR);
}

// Unscaling for Eigen vectors. Input base type can be arithmetic, output base
// type can only be floating point.
template<class Tout = double, class Tin, int N, class = ArithmeticOnly<Tin>, class = FloatingOnly<Tout>, int... EigenArgs>
inline constexpr Eigen::Matrix<Tout, N, EigenArgs...> unscaled(const Eigen::Matrix<Tin, N, EigenArgs...>& v) noexcept
{
    return v.template cast<Tout>() * Tout(SCALING_FACTOR);
}

// [INTENT] align_to_grid(): snaps a coordinate DOWN to the nearest grid boundary.
//          The negative-coordinate-safe variant explicitly handles the C++ standard's
//          truncation-toward-zero for integer division. Without the adjustment, negative
//          coordinates would round toward zero (upward), violating the "never bigger than
//          original" invariant. The three-argument form takes an explicit base offset,
//          allowing arbitrary grid origins (not just 0).
// [HAZARD] spacing=0 would cause division by zero. No assert guards this.
// Align a coordinate to a grid. The coordinate may be negative,
// the aligned value will never be bigger than the original one.
inline coord_t align_to_grid(const coord_t coord, const coord_t spacing)
{
    // Current C++ standard defines the result of integer division to be rounded to zero,
    // for both positive and negative numbers. Here we want to round down for negative
    // numbers as well.
    coord_t aligned = (coord < 0) ? ((coord - spacing + 1) / spacing) * spacing : (coord / spacing) * spacing;
    assert(aligned <= coord);
    return aligned;
}
inline Point align_to_grid(Point coord, Point spacing)
{
    return Point(align_to_grid(coord.x(), spacing.x()), align_to_grid(coord.y(), spacing.y()));
}
inline coord_t align_to_grid(coord_t coord, coord_t spacing, coord_t base) { return base + align_to_grid(coord - base, spacing); }
inline Point   align_to_grid(Point coord, Point spacing, Point base)
{
    return Point(align_to_grid(coord.x(), spacing.x(), base.x()), align_to_grid(coord.y(), spacing.y(), base.y()));
}

// [INTENT] MinMax<T>: a simple inclusive range container [min, max] for any ordered type T.
//          apply() clamps val into [limit.min, limit.max], returning true if clamping occurred.
//          The std::optional overload silently returns false (no-op) if val has no value.
//          Used for parameter range enforcement in print settings validation.
// [COUPLING] Widely used across print configuration code; any port must preserve the
//            return-value semantics (true = was clamped) for callers that check it.
// MinMaxLimits
template<typename T> struct MinMax
{
    T min;
    T max;
};
template<typename T> static bool apply(std::optional<T>& val, const MinMax<T>& limit)
{
    if (!val.has_value())
        return false;
    return apply<T>(*val, limit);
}
template<typename T> static bool apply(T& val, const MinMax<T>& limit)
{
    if (val > limit.max) {
        val = limit.max;
        return true;
    }
    if (val < limit.min) {
        val = limit.min;
        return true;
    }
    return false;
}

} // namespace Slic3r

// requseted by ConfigOptionPointsGroups
namespace std {
template<> struct hash<Slic3r::Vec2ds>
{
    size_t operator()(const Slic3r::Vec2ds& vec)
    {
        size_t seed = 0;
        for (const auto& element : vec) {
            seed ^= std::hash<double>()(element[0]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= std::hash<double>()(element[1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

template<> struct hash<std::vector<int>>
{
    size_t operator()(const std::vector<int>& vec)
    {
        size_t seed = 0;
        for (const auto& element : vec) {
            seed ^= std::hash<double>()(element) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};
} // namespace std

// [INTENT] Boost.Polygon concept/trait specializations for Slic3r::Point.
//          These tell Boost.Polygon that Point is a point type with coord_t coordinates,
//          enabling Point to be used directly in Boost.Polygon algorithms (e.g., polygon clipping,
//          Voronoi construction). The `set` mutable trait allows Boost.Polygon to construct
//          and mutate points in its internal algorithms.
// [COUPLING] This couples Point's memory layout to Boost.Polygon's trait system. If Point's
//            x()/y() accessors change semantics (e.g., if coord indexing changes), all
//            Boost.Polygon operations silently break.
// start Boost
#include <boost/version.hpp>
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
template<> struct geometry_concept<Slic3r::Point>
{
    using type = point_concept;
};

template<> struct point_traits<Slic3r::Point>
{
    using coordinate_type = coord_t;

    static inline coordinate_type get(const Slic3r::Point& point, orientation_2d orient)
    {
        return static_cast<coordinate_type>(point((orient == HORIZONTAL) ? 0 : 1));
    }
};

template<> struct point_mutable_traits<Slic3r::Point>
{
    using coordinate_type = coord_t;
    static inline void set(Slic3r::Point& point, orientation_2d orient, coord_t value) { point((orient == HORIZONTAL) ? 0 : 1) = value; }
    static inline Slic3r::Point construct(coord_t x_value, coord_t y_value) { return Slic3r::Point(x_value, y_value); }
};
}} // namespace boost::polygon
// end Boost

// [INTENT] Cereal serialization specializations for all Vec types.
//          These make Point/Vec types serializable via Cereal for project-file I/O (3MF, config).
//          Vec2crd and Vec3crd are commented out — they are NOT serialized directly; callers
//          use Vec2i32/Vec3i32 or Vec2i64/Vec3i64 with explicit type promotion/demotion.
// [COUPLING] Any change to Vec field ordering or the number of components would silently corrupt
//            saved project files, as Cereal uses positional (not named) binary serialization for
//            the Matrix2f and Transform3d specializations (raw memory copy via loadBinary/saveBinary).
// [HAZARD] Matrix2f and Transform3d use loadBinary/saveBinary — raw memory blobs with no versioning.
//          Endianness differences across architectures will corrupt serialized data.
// Serialization through the Cereal library
namespace cereal {
//	template<class Archive> void serialize(Archive& archive, Slic3r::Vec2crd &v) { archive(v.x(), v.y()); }
//	template<class Archive> void serialize(Archive& archive, Slic3r::Vec3crd &v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec2i32& v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec3i32& v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec2i64& v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec3i64& v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec2f& v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec3f& v) { archive(v.x(), v.y(), v.z()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec2d& v) { archive(v.x(), v.y()); }
template<class Archive> void serialize(Archive& archive, Slic3r::Vec3d& v) { archive(v.x(), v.y(), v.z()); }

template<class Archive> void load(Archive& archive, Slic3r::Matrix2f& m) { archive.loadBinary((char*) m.data(), sizeof(float) * 4); }
template<class Archive> void save(Archive& archive, Slic3r::Matrix2f& m) { archive.saveBinary((char*) m.data(), sizeof(float) * 4); }

template<class Archive> void load(Archive& archive, Slic3r::Transform3d& m) { archive.loadBinary((char*) m.data(), sizeof(double) * 16); }
template<class Archive> void save(Archive& archive, const Slic3r::Transform3d& m)
{
    archive.saveBinary((char*) m.data(), sizeof(double) * 16);
}
} // namespace cereal

// [INTENT] Eigen range-based for-loop adapters for Mat<N,M,T>.
//          Eigen matrices do not natively support range-for. These begin()/end() overloads
//          in namespace Eigen inject the needed iterators, exposing the raw data() pointer
//          as a flat iterator over all N*M elements in column-major order (Eigen default).
// [COUPLING] Enables ranged iteration of any Mat<> or Vec<> type throughout the codebase.
//            Iterates in COLUMN-MAJOR order, not row-major. Any port assuming row-major order
//            will silently process elements in wrong sequence for non-vector (M>1) matrices.
// To be able to use Vec<> and Mat<> in range based for loops:
namespace Eigen {
template<class T, int N, int M> T* begin(Slic3r::Mat<N, M, T>& mat) { return mat.data(); }

template<class T, int N, int M> T* end(Slic3r::Mat<N, M, T>& mat) { return mat.data() + N * M; }

template<class T, int N, int M> const T* begin(const Slic3r::Mat<N, M, T>& mat) { return mat.data(); }

template<class T, int N, int M> const T* end(const Slic3r::Mat<N, M, T>& mat) { return mat.data() + N * M; }
} // namespace Eigen

#endif
