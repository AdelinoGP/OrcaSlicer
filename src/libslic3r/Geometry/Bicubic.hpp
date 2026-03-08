// [INTENT] Header-only bicubic interpolation library used for bed-leveling
// mesh interpolation and any 2D/1D field sampling over a regular grid.
// Provides three kernel families (linear, Catmull-Rom, B-spline) and two
// interpolation entry points:
//   • cubic_interpolate()   — 1D interpolation over an Eigen array.
//   • bicubic_interpolate() — 2D separable interpolation over an Eigen matrix.
//
// [COUPLING] Depends only on Eigen (Dense) and C++ standard library.
// No Slic3r-specific types are used; this module is self-contained.
//
// [CONCURRENCY] All functions are pure (no shared mutable state). Safe to call
// from multiple threads simultaneously.
#ifndef BICUBIC_HPP
#define BICUBIC_HPP

#include <algorithm>
#include <vector>
#include <cmath>

#include <Eigen/Dense>

namespace Slic3r { namespace Geometry {

namespace BicubicInternal {

// [INTENT] Linear (hat) kernel for testing purposes.  The 4×4 coefficient
// matrix degenerates so that only the a10=1 and a11=-1 / a20=0 / a21=1
// terms are non-zero, reproducing bilinear interpolation within each cell.
// Stored as 16 static methods (a00..a33) following the Catmull-Rom convention
// so that CubicKernelWrapper can use all kernels uniformly.
//
// [HAZARD H596] This clamp() function duplicates std::clamp (available in
// C++17, which this project targets).  It is kept for historical reasons but
// is functionally identical — prefer std::clamp in new code.
template<typename T> struct LinearKernel
{
    typedef T FloatType;

    static T a00() { return T(0.); }
    static T a01() { return T(0.); }
    static T a02() { return T(0.); }
    static T a03() { return T(0.); }
    static T a10() { return T(1.); }
    static T a11() { return T(-1.); }
    static T a12() { return T(0.); }
    static T a13() { return T(0.); }
    static T a20() { return T(0.); }
    static T a21() { return T(1.); }
    static T a22() { return T(0.); }
    static T a23() { return T(0.); }
    static T a30() { return T(0.); }
    static T a31() { return T(0.); }
    static T a32() { return T(0.); }
    static T a33() { return T(0.); }
};

// [INTENT] Catmull-Rom (Keys interpolation) kernel.  Passes through the data
// points exactly (interpolating) with C1 continuity.  The coefficient matrix
// is the standard Keys/Catmull-Rom parameterization with alpha=0.5.
// Used for smooth interpolation of bed-mesh height maps.
template<typename T> struct CubicCatmulRomKernel
{
    typedef T FloatType;

    static T a00() { return 0; }
    static T a01() { return T(-0.5); }
    static T a02() { return T(1.); }
    static T a03() { return T(-0.5); }
    static T a10() { return T(1.); }
    static T a11() { return 0; }
    static T a12() { return T(-5. / 2.); }
    static T a13() { return T(3. / 2.); }
    static T a20() { return 0; }
    static T a21() { return T(0.5); }
    static T a22() { return T(2.); }
    static T a23() { return T(-3. / 2.); }
    static T a30() { return 0; }
    static T a31() { return 0; }
    static T a32() { return T(-0.5); }
    static T a33() { return T(0.5); }
};

// [INTENT] Uniform cubic B-spline kernel.  Approximating (not interpolating):
// the curve passes near, but not through, the control points.  Provides C2
// continuity.  The coefficients are the standard 1/6 * [1,-3,3,-1; 4,0,-6,3;
// 1,3,3,-3; 0,0,0,1] matrix.
// Used when smoothness of the output surface is preferred over exact passage
// through sample points.
template<typename T> struct CubicBSplineKernel
{
    typedef T FloatType;

    static T a00() { return T(1. / 6.); }
    static T a01() { return T(-3. / 6.); }
    static T a02() { return T(3. / 6.); }
    static T a03() { return T(-1. / 6.); }
    static T a10() { return T(4. / 6.); }
    static T a11() { return 0; }
    static T a12() { return T(-6. / 6.); }
    static T a13() { return T(3. / 6.); }
    static T a20() { return T(1. / 6.); }
    static T a21() { return T(3. / 6.); }
    static T a22() { return T(3. / 6.); }
    static T a23() { return T(-3. / 6.); }
    static T a30() { return 0; }
    static T a31() { return 0; }
    static T a32() { return 0; }
    static T a33() { return T(1. / 6.); }
};

// [INTENT] Boundary-clamped index helper used by cubic_interpolate() and
// bicubic_interpolate() when the query point falls within two cells of an
// edge.  Returns `lower` if `a < lower`, `upper` if `a > upper`, else `a`.
// [HAZARD H596] Duplicates std::clamp; kept for pre-C++17 compatibility.
template<class T> inline T clamp(T a, T lower, T upper) { return (a < lower) ? lower : (a > upper) ? upper : a; }
} // namespace BicubicInternal

// [INTENT] Wrapper that adapts a raw kernel coefficient struct (LinearKernel,
// CubicCatmulRomKernel, CubicBSplineKernel) into a uniform evaluation API
// with two methods:
//   • kernel(x)  — evaluate the 1D kernel weight at fractional distance x.
//   • interpolate(f0,f1,f2,f3, x) — weighted combination of four neighboring
//     sample values using the kernel polynomial evaluated at fractional offset x.
//
// kernel_span = 4: every query requires four consecutive sample values.
//
// [INTENT] kernel() splits the evaluation into two polynomial pieces:
//   • |x| ≤ 1: inner piece using a10..a13 coefficients.
//   • 1 < |x| < 2: outer piece using a00..a03 coefficients.
//   • |x| ≥ 2: returns 0 (kernel support ends at ±2).
template<typename Kernel> struct CubicKernelWrapper
{
    typedef typename Kernel::FloatType FloatType;

    static constexpr size_t kernel_span = 4;

    // [INTENT] Evaluate the piecewise cubic kernel at fractional distance |x|.
    // Two-piece polynomial: inner region [0,1] uses a1x coefficients;
    // outer region (1,2) uses a0x coefficients (shifted by 1).
    static FloatType kernel(FloatType x)
    {
        x = fabs(x);
        if (x >= (FloatType) 2.)
            return 0.0f;
        if (x <= (FloatType) 1.) {
            FloatType x2 = x * x;
            FloatType x3 = x2 * x;
            return Kernel::a10() + Kernel::a11() * x + Kernel::a12() * x2 + Kernel::a13() * x3;
        }
        assert(x > (FloatType) 1. && x < (FloatType) 2.);
        x -= (FloatType) 1.;
        FloatType x2 = x * x;
        FloatType x3 = x2 * x;
        return Kernel::a00() + Kernel::a01() * x + Kernel::a02() * x2 + Kernel::a03() * x3;
    }

    // [INTENT] Compute the interpolated value from four sample values
    // f0..f3 at positions ix-1, ix, ix+1, ix+2 using fractional offset x ∈ [0,1).
    // Each fi is weighted by the corresponding kernel polynomial piece.
    static FloatType interpolate(FloatType f0, FloatType f1, FloatType f2, FloatType f3, FloatType x)
    {
        const FloatType x2 = x * x;
        const FloatType x3 = x * x * x;
        return f0 * (Kernel::a00() + Kernel::a01() * x + Kernel::a02() * x2 + Kernel::a03() * x3) +
               f1 * (Kernel::a10() + Kernel::a11() * x + Kernel::a12() * x2 + Kernel::a13() * x3) +
               f2 * (Kernel::a20() + Kernel::a21() * x + Kernel::a22() * x2 + Kernel::a23() * x3) +
               f3 * (Kernel::a30() + Kernel::a31() * x + Kernel::a32() * x2 + Kernel::a33() * x3);
    }
};

// [INTENT] Public kernel aliases exposing the three kernel families through
// CubicKernelWrapper.  These are the types passed as the Kernel template
// argument to cubic_interpolate() and bicubic_interpolate().

// Linear splines
template<typename NumberType> using LinearKernel = CubicKernelWrapper<BicubicInternal::LinearKernel<NumberType>>;

// Catmul-Rom splines
template<typename NumberType> using CubicCatmulRomKernel = CubicKernelWrapper<BicubicInternal::CubicCatmulRomKernel<NumberType>>;

// Cubic B-splines
template<typename NumberType> using CubicBSplineKernel = CubicKernelWrapper<BicubicInternal::CubicBSplineKernel<NumberType>>;

// [INTENT] 1D cubic interpolation over an Eigen array F of sample values at
// integer positions 0..F.size()-1.  Queries at fractional position pt.
//
// Two cases:
//   • Interior (ix>1 && ix+2<w): uses the four exact neighbors.
//   • Boundary: extends with a constant (clamped index) to avoid out-of-range
//     access; introduces a mild discontinuity at the array boundary.
//
// [HAZARD] The Eigen array type parameter is constrained only by the Eigen
// ArrayBase concept; passing a matrix with Dimension>1 will compile but give
// incorrect results because F[i] indexing is 1D.
template<typename KernelWrapper>
static typename KernelWrapper::FloatType cubic_interpolate(const Eigen::ArrayBase<typename KernelWrapper::FloatType>& F,
                                                           const typename KernelWrapper::FloatType                    pt)
{
    typedef typename KernelWrapper::FloatType T;
    const int                                 w  = int(F.size());
    const int                                 ix = (int) floor(pt);
    const T                                   s  = pt - T(ix);

    if (ix > 1 && ix + 2 < w) {
        // Inside the fully interpolated region.
        return KernelWrapper::interpolate(F[ix - 1], F[ix], F[ix + 1], F[ix + 2], s);
    }
    // Transition region. Extend with a constant function.
    auto f = [&F, w](T x) { return F[BicubicInternal::clamp(x, 0, w - 1)]; };
    return KernelWrapper::interpolate(f(ix - 1), f(ix), f(ix + 1), f(ix + 2), s);
}

// [INTENT] 2D bicubic interpolation over an Eigen matrix F (rows×cols) at a
// 2D fractional point pt = (px, py).  Implemented as two separable 1D passes:
//   1. For each of the four row-neighbors of py, interpolate horizontally
//      across the four column-neighbors of px.
//   2. Interpolate the four horizontal results vertically.
//
// [COUPLING] Kernel must be a CubicKernelWrapper specialization (provides
// FloatType and static interpolate()).
//
// [HAZARD] Returns float (not FloatType) regardless of the template parameter.
// If T is double, there is an implicit narrowing conversion on the return.
//
// Boundary handling: same clamped extension as cubic_interpolate().
template<typename Kernel, typename Derived>
static float bicubic_interpolate(const Eigen::MatrixBase<Derived>&                                        F,
                                 const Eigen::Matrix<typename Kernel::FloatType, 2, 1, Eigen::DontAlign>& pt)
{
    typedef typename Kernel::FloatType T;
    const int                          w  = F.cols();
    const int                          h  = F.rows();
    const int                          ix = (int) floor(pt[0]);
    const int                          iy = (int) floor(pt[1]);
    const T                            s  = pt[0] - T(ix);
    const T                            t  = pt[1] - T(iy);

    if (ix > 1 && ix + 2 < w && iy > 1 && iy + 2 < h) {
        // Inside the fully interpolated region.
        return Kernel::interpolate(Kernel::interpolate(F(ix - 1, iy - 1), F(ix, iy - 1), F(ix + 1, iy - 1), F(ix + 2, iy - 1), s),
                                   Kernel::interpolate(F(ix - 1, iy), F(ix, iy), F(ix + 1, iy), F(ix + 2, iy), s),
                                   Kernel::interpolate(F(ix - 1, iy + 1), F(ix, iy + 1), F(ix + 1, iy + 1), F(ix + 2, iy + 1), s),
                                   Kernel::interpolate(F(ix - 1, iy + 2), F(ix, iy + 2), F(ix + 1, iy + 2), F(ix + 2, iy + 2), s), t);
    }
    // Transition region. Extend with a constant function.
    auto f = [&F, w, h](int x, int y) { return F(BicubicInternal::clamp(x, 0, w - 1), BicubicInternal::clamp(y, 0, h - 1)); };
    return Kernel::interpolate(Kernel::interpolate(f(ix - 1, iy - 1), f(ix, iy - 1), f(ix + 1, iy - 1), f(ix + 2, iy - 1), s),
                               Kernel::interpolate(f(ix - 1, iy), f(ix, iy), f(ix + 1, iy), f(ix + 2, iy), s),
                               Kernel::interpolate(f(ix - 1, iy + 1), f(ix, iy + 1), f(ix + 1, iy + 1), f(ix + 2, iy + 1), s),
                               Kernel::interpolate(f(ix - 1, iy + 2), f(ix, iy + 2), f(ix + 1, iy + 2), f(ix + 2, iy + 2), s), t);
}

}} // namespace Slic3r::Geometry

#endif /* BICUBIC_HPP */
