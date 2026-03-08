#ifndef slic3r_Geometry_Circle_hpp_
#define slic3r_Geometry_Circle_hpp_

// [INTENT] Circle geometry utilities: circumcenter, smallest-enclosing-circle (Welzl), Taubin-Newton
// algebraic circle fit, RANSAC circle fitting, and line-circle intersection helpers.
// [COUPLING] Used by MedialAxis (width measurement), Arachne (SkeletalTrapezoidation), AvoidCrossing,
//   GCode arc detection, and test harnesses. All functions are coordinate-agnostic (template on Vector).
// [MEMORY] No heap allocation in the header-only functions; Welzl is O(n³) worst-case on stack.

#include "../Point.hpp"

#include <Eigen/Geometry>

namespace Slic3r {
namespace Geometry {

// [INTENT] Circumcenter of triangle (a, b, c): the unique point equidistant from all three vertices.
// Falls back to midpoint of the longest side when the three points are collinear (det < epsilon).
// [HAZARD] H574 (Low): The collinear fallback returns a midpoint, not a true circumcenter. Callers
//   that subsequently compute a radius from this center may get a meaninglessly large or wrong value.
//   Port note: document the collinear contract explicitly.
// https://en.wikipedia.org/wiki/Circumscribed_circle
// Circumcenter coordinates, Cartesian coordinates
template<typename Vector> Vector circle_center(const Vector& a, const Vector& bsrc, const Vector& csrc, typename Vector::Scalar epsilon)
{
    using Scalar = typename Vector::Scalar;
    // [INTENT] Translate so that 'a' is origin; compute cross product to detect collinearity.
    Vector b  = bsrc - a;
    Vector c  = csrc - a;
    Scalar lb = b.squaredNorm();
    Scalar lc = c.squaredNorm();
    if (Scalar d = b.x() * c.y() - b.y() * c.x(); std::abs(d) < epsilon) {
        // The three points are collinear. Take the center of the two points
        // furthest away from each other.
        Scalar lbc = (csrc - bsrc).squaredNorm();
        return Scalar(0.5) * (lb > lc && lb > lbc ? a + bsrc : lc > lb && lc > lbc ? a + csrc : bsrc + csrc);
    } else {
        // [INTENT] Standard Cartesian circumcenter formula derived from equal-distance constraints.
        Vector v = lc * b - lb * c;
        return a + Vector(-v.y(), v.x()) / (2 * d);
    }
}
}

// [INTENT] CircleSq: 2D circle stored as center + squared radius. Avoids one sqrt() during
// containment tests; use when only inside/outside comparisons are needed.
// [STATE] radius2 < 0 encodes an "invalid" sentinel (see make_invalid()).
// [COUPLING] Welzl algorithm works exclusively with CircleSq to avoid sqrt overhead in inner loop.
// 2D circle defined by its center and squared radius
template<typename Vector> struct CircleSq
{
    using Scalar = typename Vector::Scalar;

    Vector center;
    Scalar radius2;

    CircleSq() {}
    CircleSq(const Vector& center, const Scalar radius2) : center(center), radius2(radius2) {}
    CircleSq(const Vector& a, const Vector& b) : center(Scalar(0.5) * (a + b)) { radius2 = (a - center).squaredNorm(); }
    CircleSq(const Vector& a, const Vector& b, const Vector& c, Scalar epsilon)
    {
        this->center  = circle_center(a, b, c, epsilon);
        this->radius2 = (a - this->center).squaredNorm();
    }

    bool invalid() const { return this->radius2 < 0; }
    bool valid() const { return !this->invalid(); }
    bool contains(const Vector& p) const { return (p - this->center).squaredNorm() < this->radius2; }
    bool contains(const Vector& p, const Scalar epsilon2) const { return (p - this->center).squaredNorm() < this->radius2 + epsilon2; }

    CircleSq inflated(Scalar epsilon) const
    {
        assert(this->radius2 >= 0);
        Scalar r = sqrt(this->radius2) + epsilon;
        return {this->center, r * r};
    }

    static CircleSq make_invalid() { return CircleSq{{0, 0}, -1}; }
};

// [INTENT] Circle: 2D circle stored as center + radius. Use when the radius value itself is needed
// (e.g., for inflation, output, or comparison against a non-squared threshold).
// [STATE] radius < 0 encodes an "invalid" sentinel (see make_invalid()).
// [COUPLING] Taubin-Newton and RANSAC return Circle<Vec2d>. Welzl returns CircleSq then converts.
// 2D circle defined by its center and radius
template<typename Vector> struct Circle
{
    using Scalar = typename Vector::Scalar;

    Vector center;
    Scalar radius;

    Circle() {}
    Circle(const Vector& center, const Scalar radius) : center(center), radius(radius) {}
    Circle(const Vector& a, const Vector& b) : center(Scalar(0.5) * (a + b)) { radius = (a - center).norm(); }
    Circle(const Vector& a, const Vector& b, const Vector& c, const Scalar epsilon) { *this = CircleSq(a, b, c, epsilon); }

    // Conversion from CircleSq
    template<typename Vector2>
    explicit Circle(const CircleSq<Vector2>& c) : center(c.center), radius(c.radius2 <= 0 ? c.radius2 : sqrt(c.radius2))
    {}
    template<typename Vector2> Circle operator=(const CircleSq<Vector2>& c)
    {
        this->center = c.center;
        this->radius = c.radius2 <= 0 ? c.radius2 : sqrt(c.radius2);
    }

    bool invalid() const { return this->radius < 0; }
    bool valid() const { return !this->invalid(); }
    bool contains(const Vector& p) const { return (p - this->center).squaredNorm() <= this->radius * this->radius; }
    bool contains(const Vector& p, const Scalar epsilon) const
    {
        Scalar re = this->radius + epsilon;
        return (p - this->center).squaredNorm() < re * re;
    }

    Circle inflated(Scalar epsilon) const
    {
        assert(this->radius >= 0);
        return {this->center, this->radius + epsilon};
    }

    static Circle make_invalid() { return Circle{{0, 0}, -1}; }
};

using Circlef   = Circle<Vec2f>;
using Circled   = Circle<Vec2d>;
using CircleSqf = CircleSq<Vec2f>;
using CircleSqd = CircleSq<Vec2d>;

// [INTENT] Taubin-Newton circle fitting: algebraic fit using Newton's method on the characteristic
// polynomial of the moments matrix (Chernov). More numerically stable than naive least-squares.
// Declared here; implemented in Circle.cpp.
/// Find the center of the circle corresponding to the vector of Points as an arc.
Point circle_center_taubin_newton(const Points::const_iterator& input_start, const Points::const_iterator& input_end, size_t cycles = 20);
inline Point circle_center_taubin_newton(const Points& input, size_t cycles = 20)
{
    return circle_center_taubin_newton(input.cbegin(), input.cend(), cycles);
}

/// Find the center of the circle corresponding to the vector of Pointfs as an arc.
Vec2d circle_center_taubin_newton(const Vec2ds::const_iterator& input_start, const Vec2ds::const_iterator& input_end, size_t cycles = 20);
inline Vec2d circle_center_taubin_newton(const Vec2ds& input, size_t cycles = 20)
{
    return circle_center_taubin_newton(input.cbegin(), input.cend(), cycles);
}
Circled circle_taubin_newton(const Vec2ds& input, size_t cycles = 20);

// [INTENT] RANSAC circle fit: randomly samples 3-point subsets, fits circles, keeps best by
// max-deviation score. More robust to outliers than Taubin-Newton but non-deterministic in principle.
// [HAZARD] H572 (Medium): std::mt19937 is default-constructed (seed=0) — same RNG sequence on every
//   call with the same input. The algorithm is documented as randomized but is fully deterministic.
//   Any caller expecting different results on repeated calls (e.g., with perturbed input) will not
//   get independent draws. Port note: seed from std::random_device or pass rng as parameter.
// Find circle using RANSAC randomized algorithm.
Circled circle_ransac(const Vec2ds& input, size_t iterations = 20, double* min_error = nullptr);

// [INTENT] Welzl SEC (squared-radius variant): O(n) expected, O(n³) worst-case.
// [COMPLEXITY] O(n) expected. Each inner loop body executes O(1) times in expectation.
// [HAZARD] H575 (Low): Worst-case O(n³) occurs with adversarial point ordering.
//   Pre-shuffle the input if performance is critical for large point clouds.
// Randomized algorithm by Emo Welzl, working with squared radii for efficiency. The returned circle radius is inflated by epsilon.
template<typename Vector, typename Points>
CircleSq<Vector> smallest_enclosing_circle2_welzl(const Points& points, const typename Vector::Scalar epsilon)
{
    using Scalar = typename Vector::Scalar;
    CircleSq<Vector> circle;

    if (!points.empty()) {
        const auto& p0 = points[0].template cast<Scalar>();
        if (points.size() == 1) {
            circle.center  = p0;
            circle.radius2 = epsilon * epsilon;
        } else {
            circle = CircleSq<Vector>(p0, points[1].template cast<Scalar>()).inflated(epsilon);
            for (size_t i = 2; i < points.size(); ++i)
                if (const Vector& p = points[i].template cast<Scalar>(); !circle.contains(p)) {
                    // p is the first point on the smallest circle enclosing points[0..i]
                    circle = CircleSq<Vector>(p0, p).inflated(epsilon);
                    for (size_t j = 1; j < i; ++j)
                        if (const Vector& q = points[j].template cast<Scalar>(); !circle.contains(q)) {
                            // q is the second point on the smallest circle enclosing points[0..i]
                            circle = CircleSq<Vector>(p, q).inflated(epsilon);
                            for (size_t k = 0; k < j; ++k)
                                if (const Vector& r = points[k].template cast<Scalar>(); !circle.contains(r))
                                    circle = CircleSq<Vector>(p, q, r, epsilon).inflated(epsilon);
                        }
                }
        }
    }

    return circle;
}

// Randomized algorithm by Emo Welzl. The returned circle radius is inflated by epsilon.
template<typename Vector, typename Points>
Circle<Vector> smallest_enclosing_circle_welzl(const Points& points, const typename Vector::Scalar epsilon)
{
    return Circle<Vector>(smallest_enclosing_circle2_welzl<Vector, Points>(points, epsilon));
}

// Randomized algorithm by Emo Welzl. The returned circle radius is inflated by SCALED_EPSILON.
inline Circled smallest_enclosing_circle_welzl(const Points& points)
{
    return smallest_enclosing_circle_welzl<Vec2d, Points>(points, SCALED_EPSILON);
}

// [INTENT] ray_circle_intersections_r2_lv2_c: solve line-circle intersection given pre-computed
// squared values (r², a²+b², c). Avoids redundant sqrt/mul by the caller.
// Parameters: r2=radius², a/b=line normal coefficients, lv2=a²+b², c=line constant (ax+by+c=0).
// Returns 0 (no intersection), 1 (tangent), or 2 (secant).
// Ugly named variant, that accepts the squared line
// Don't call me with a nearly zero length vector!
// sympy:
// factor(solve([a * x + b * y + c, x**2 + y**2 - r**2], [x, y])[0])
// factor(solve([a * x + b * y + c, x**2 + y**2 - r**2], [x, y])[1])
template<typename T>
int ray_circle_intersections_r2_lv2_c(
    T r2, T a, T b, T lv2, T c, std::pair<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>, Eigen::Matrix<T, 2, 1, Eigen::DontAlign>>& out)
{
    T x0 = -a * c;
    T y0 = -b * c;
    T d2 = r2 * lv2 - c * c;
    if (d2 < T(0))
        return 0;
    T d            = sqrt(d2);
    out.first.x()  = (x0 + b * d) / lv2;
    out.first.y()  = (y0 - a * d) / lv2;
    out.second.x() = (x0 - b * d) / lv2;
    out.second.y() = (y0 + a * d) / lv2;
    return d == T(0) ? 1 : 2;
}
// [INTENT] ray_circle_intersections: convenience wrapper that pre-computes squared values and
// delegates to ray_circle_intersections_r2_lv2_c.
// [HAZARD] H571 (High): This function calls `ray_circle_intersections_r2_lv2_c2` (note trailing '2')
//   at line 235, but the function defined above is named `ray_circle_intersections_r2_lv2_c` (no '2').
//   This is a linker/undefined-symbol error if this code path is ever instantiated.
//   The trailing '2' suffix appears to be a typo. No caller in the codebase currently triggers this
//   path (verified by grep), so it has not produced a build failure.
//   Port note: the call must be renamed to ray_circle_intersections_r2_lv2_c (drop the '2').
template<typename T>
int ray_circle_intersections(
    T r, T a, T b, T c, std::pair<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>, Eigen::Matrix<T, 2, 1, Eigen::DontAlign>>& out)
{
    T lv2 = a * a + b * b;
    if (lv2 < T(SCALED_EPSILON * SCALED_EPSILON)) {
        // FIXME what is the correct epsilon?
        //  What if the line touches the circle?
        return false;
    }
    return ray_circle_intersections_r2_lv2_c2(r * r, a, b, a * a + b * b, c, out);
}

}
} // namespace Slic3r::Geometry

#endif // slic3r_Geometry_Circle_hpp_
