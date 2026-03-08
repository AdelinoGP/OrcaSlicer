#ifndef slic3r_Geometry_ArcWelder_hpp_
#define slic3r_Geometry_ArcWelder_hpp_

// [INTENT] ArcWelder geometry utilities: two pure-math template functions used by the arc-welding
// pipeline (G0/G1 → G2/G3 arc compression). Originally ported from FormerLurker/ArcWelderLib.
// These helpers are coordinate-system-agnostic; callers must pass values in consistent units.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <Eigen/Geometry>
#include <type_traits>
#include <cassert>

#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace Geometry { namespace ArcWelder {

// [INTENT] Compute the center of the circle passing through start_pos and end_pos
// with the given signed radius.
// [MEMORY] Pure function — no heap allocation; all temporaries are stack Eigen expressions.
// [COUPLING] Consumed by GCode arc-welding path in GCodeWriter / GCodeProcessor; also
//   used indirectly by GCodeReader when re-discretizing arcs.
// positive radius: take shorter arc (|arc| <= PI)
// negative radius: take longer arc  (|arc| >  PI)
// radius must NOT be zero!
// [HAZARD] H573 (Low): When start_pos and end_pos are exactly antipodal (q2 == 4*r^2),
//   t2 == 0 and the center collapses to the chord midpoint, which is equidistant from both
//   input points but does NOT lie on any unique circle — any circle through antipodal points
//   is valid. Callers that rely on arc direction (is_ccw) will receive a degenerate result.
//   Port note: document this degenerate case explicitly.
template<typename Derived, typename Derived2, typename Float>
inline Eigen::Matrix<Float, 2, 1, Eigen::DontAlign> arc_center(const Eigen::MatrixBase<Derived>&  start_pos,
                                                               const Eigen::MatrixBase<Derived2>& end_pos,
                                                               const Float                        radius,
                                                               const bool                         is_ccw)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2,
                  "arc_center(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2,
                  "arc_center(): second parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value,
                  "arc_center(): Both vectors must be of the same type.");
    static_assert(std::is_same<typename Derived::Scalar, Float>::value, "arc_center(): Radius must be of the same type as the vectors.");
    assert(radius != 0);
    // [STATE] v = chord vector from start to end; q2 = squared chord length.
    using Vector = Eigen::Matrix<Float, 2, 1, Eigen::DontAlign>;
    auto  v      = end_pos - start_pos;
    Float q2     = v.squaredNorm();
    assert(q2 > 0);
    // [INTENT] t2 = (sagitta factor)^2; derived from: center is perpendicular to chord at midpoint,
    //   distance from midpoint to center satisfies Pythagoras: |center-mid|^2 = r^2 - (chord/2)^2.
    Float t2 = sqr(radius) / q2 - Float(.25f);
    // If the start_pos and end_pos are nearly antipodal, t2 may become slightly negative.
    // In that case return a centroid of start_point & end_point.
    Float t   = t2 > 0 ? sqrt(t2) : Float(0);
    auto  mid = Float(0.5) * (start_pos + end_pos);
    // [INTENT] vp is the perpendicular offset from chord midpoint to circle center.
    //   Direction chosen so that (radius>0)==is_ccw selects the correct half-plane.
    Vector vp{-v.y() * t, v.x() * t};
    return (radius > Float(0)) == is_ccw ? (mid + vp).eval() : (mid - vp).eval();
}

// [INTENT] Compute the minimum number of linear segments needed to approximate an arc
// (given radius and sweep angle) such that the maximum deviation from the true arc is
// within `deviation`. Used for arc re-discretization before output.
// [MEMORY] Pure function — no allocation; O(1) computation.
// [COUPLING] Called by GCode arc rendering and by any path that must convert arcs to
//   polylines for Clipper operations (which require linear segments only).
// Return number of linear segments necessary to interpolate arc of a given positive radius and positive angle to satisfy
// maximum deviation of an interpolating polyline from an analytic arc.
template<typename FloatType> size_t arc_discretization_steps(const FloatType radius, const FloatType angle, const FloatType deviation)
{
    assert(radius > 0);
    assert(angle > 0);
    assert(angle <= FloatType(2. * M_PI));
    assert(deviation > 0);

    // [INTENT] d = chord from center to chord (inset): if d < 0 the deviation exceeds the radius,
    //   meaning a single or two segments always suffice (degenerate tiny arc).
    FloatType d = radius - deviation;
    return d < EPSILON ?
               // Radius smaller than deviation.
               ( // Acute angle: a single segment interpolates the arc with sufficient accuracy.
                   angle < M_PI ||
                           // Obtuse angle: Test whether the furthest point (center) of an arc is closer than deviation to the center of a
                           // line segment.
                           radius * (FloatType(1.) + cos(M_PI - FloatType(.5) * angle)) < deviation ?
                       // Single segment is sufficient
                       1 :
                       // Two segments are necessary, the middle point is at the center of the arc.
                       2) :
               // [INTENT] General formula: step_angle = 2*acos(d/r); steps = ceil(angle/step_angle).
               //   Derived from the inscribed-angle condition: the midpoint of a chord is at distance
               //   r*cos(half_step) from center, which must be >= r - deviation.
               size_t(ceil(angle / (2. * acos(d / radius))));
}

}}} // namespace Slic3r::Geometry::ArcWelder

#endif // slic3r_Geometry_ArcWelder_hpp_
