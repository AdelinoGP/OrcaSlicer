// [INTENT] FillGyroid.cpp — implementation of Gyroid TPMS infill pattern.
// The Gyroid is a triply periodic minimal surface (TPMS). At each layer Z, the 3D surface
// is sampled as a sinusoidal wave in XY, producing interlocking wave fills between layers.
//
// Key components:
//   f()              — evaluates the Gyroid cross-section curve y = f(x) at fixed Z.
//   make_one_period  — adaptively samples one 2π period of f() to within PatternTolerance.
//   make_wave        — replicates one_period across the full bounding-box width.
//   make_gyroid_waves— generates the full set of wave polylines for a given layer Z.
//   _fill_surface_single — main entry: rotate, generate waves, clip, connect, rotate back.
//
// [HAZARD H321] All math is done in normalized (unscaled) "gyroid units" and converted to
// scaled Slic3r coords only at the final step in make_wave(). Round-trip precision loss
// between double and coord_t (int32) is bounded by scaleFactor granularity.
//
// [HAZARD H322] make_one_period uses an insertion-sort refinement loop. Each iteration
// appends midpoints to a flat vector, then sorts the entire vector. O(n² log n) worst-case
// for very tight tolerances, but in practice n is small (< 100 pts/period).
//
// [HAZARD H323] make_gyroid_waves swaps width/height for vertical wave orientation. This
// swap is in-place and the function returns polylines in "logical" (unrotated) space, so
// callers must not assume axis orientation from the polyline coordinates.
//
// [CONCURRENCY] All functions are pure/static with no shared mutable state. Thread-safe.
// [COUPLING] Uses scale_(), unscale<double>(), EPSILON from libslic3r.h; sqr() from same.
//            ClipperUtils::intersection_pl(); ShortestPath::chain_or_connect_infill().

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include "FillBase.hpp"
#include "FillGyroid.hpp"

namespace Slic3r {

// [INTENT] Evaluates the implicit Gyroid curve y = f(x) at a fixed layer height Z.
// The Gyroid TPMS equation sin(x)cos(y)+sin(y)cos(z)+sin(z)cos(x)=0 is inverted to
// solve for y given x and z. Two orientations are supported:
//   vertical=false → horizontal waves: uses sin/cos swapped phase convention.
//   vertical=true  → vertical waves:   uses the complementary phase.
// [STATE] Pure function — no side effects.
// [MEMORY] Stack-only; constant memory regardless of input.
// [HAZARD H324] asin() is only defined for inputs in [-1, 1]. The expressions a/r and
// res/r are constructed so r = sqrt(a²+b²) ≥ |a| and r ≥ |res|, but floating-point
// rounding could push |a/r| slightly above 1.0, causing asin() to return NaN.
// There is NO clamp guard here — a NaN would silently propagate into polyline points.
static inline double f(double x, double z_sin, double z_cos, bool vertical, bool flip)
{
    if (vertical) {
        double phase_offset = (z_cos < 0 ? M_PI : 0) + M_PI;
        double a            = sin(x + phase_offset);
        double b            = -z_cos;
        double res          = z_sin * cos(x + phase_offset + (flip ? M_PI : 0.));
        double r            = sqrt(sqr(a) + sqr(b));
        return asin(a / r) + asin(res / r) + M_PI;
    } else {
        double phase_offset = z_sin < 0 ? M_PI : 0.;
        double a            = cos(x + phase_offset);
        double b            = -z_sin;
        double res          = z_cos * sin(x + phase_offset + (flip ? 0 : M_PI));
        double r            = sqrt(sqr(a) + sqr(b));
        return (asin(a / r) + asin(res / r) + 0.5 * M_PI);
    }
}

// [INTENT] Constructs a single wave polyline for one row of the Gyroid pattern.
// Takes a pre-computed one_period template and replicates it across the full 'width',
// appending a final point at exactly x=width. Then applies vertical axis swap if needed,
// translates by 'offset' in the transverse direction, clamps to [0, height], and converts
// from normalized units to scaled Slic3r coord_t by multiplying by scaleFactor.
//
// [STATE] 'one_period' is read-only input; 'points' is a local working copy.
// [MEMORY] Reserves approximate capacity upfront; actual size may be slightly larger due
//          to the final emplace_back at x=width.
// [HAZARD H325] The `do {...} while` loop copies points by index from the *same* vector
// being appended to. If reallocation occurs mid-loop (capacity exhausted), the index-based
// access `points[points.size()-n]` still works correctly (no iterator invalidation risk
// since we use indexing), but the reserve() is only approximate — a second reallocation
// is possible. This is a minor performance concern, not a correctness issue.
// [HAZARD H326] std::clamp(point.y(), 0., height) hard-clips waves at the bounding box
// edges, introducing a flat horizontal segment in the polyline. This is intentional
// (prevents overshoot), but creates non-smooth discontinuities at the clip boundary.
static inline Polyline make_wave(const std::vector<Vec2d>& one_period,
                                 double                    width,
                                 double                    height,
                                 double                    offset,
                                 double                    scaleFactor,
                                 double                    z_cos,
                                 double                    z_sin,
                                 bool                      vertical,
                                 bool                      flip)
{
    // [INTENT] Copy the template period; replicate until we span 'width'.
    std::vector<Vec2d> points = one_period;
    double             period = points.back()(0);
    if (width != period) // do not extend if already truncated
    {
        points.reserve(one_period.size() * size_t(floor(width / period)));
        // [INTENT] Remove the closing endpoint before tiling — it will be replaced
        // by offset copies, and a fresh endpoint is added at x=width afterwards.
        points.pop_back();

        size_t n = points.size();
        do {
            // [INTENT] Tile by copying x-shifted versions of the base period.
            points.emplace_back(points[points.size() - n].x() + period, points[points.size() - n].y());
        } while (points.back()(0) < width - EPSILON);

        // [INTENT] Add a terminating point exactly at x=width for clean boundary.
        points.emplace_back(Vec2d(width, f(width, z_sin, z_cos, vertical, flip)));
    }

    // [INTENT] Convert normalized wave points to scaled Slic3r coordinates:
    //   1. Shift transversally by 'offset' (row separation).
    //   2. Clamp to [0, height] to prevent out-of-bounds.
    //   3. If vertical orientation, swap X/Y axes.
    //   4. Scale to coord_t by multiplying by scaleFactor.
    Polyline polyline;
    polyline.points.reserve(points.size());
    for (auto& point : points) {
        point(1) += offset;
        point(1) = std::clamp(double(point.y()), 0., height);
        if (vertical)
            std::swap(point(0), point(1));
        polyline.points.emplace_back((point * scaleFactor).cast<coord_t>());
    }

    return polyline;
}

// [INTENT] Adaptively samples one complete period (x ∈ [0, 2π] or [0, width] if width < 2π)
// of the Gyroid curve f(x) at the current layer Z. Uses midpoint subdivision: coarse seed
// points at π/2 intervals, then iteratively inserts midpoints wherever the cross-product
// error exceeds tolerance², stopping when no more refinement is needed.
//
// Returns a sorted vector of 2D points in normalized (unscaled) space.
//
// [MEMORY] Initial capacity hinted as ceil(limit / tolerance / 3). At 0.2mm tolerance
// and 1.0mm spacing, that is ~10 points. Worst case is O(1/tolerance²) points.
// [HAZARD H322] (repeat) Insertion sort via full std::sort on every refinement pass is
// O(n² log n) overall. Acceptable for the small n encountered in practice.
// [HAZARD H327] Uses `cross2()` area test: |cross(ip-lp, ip-rp)| > tolerance². This is
// the triangle area heuristic — not the true chord-height error metric. For nearly-linear
// segments it underestimates curvature; for highly curved sections it may over-refine.
// The result is functionally correct but not geometrically optimal.
static std::vector<Vec2d> make_one_period(
    double width, double scaleFactor, double z_cos, double z_sin, bool vertical, bool flip, double tolerance)
{
    std::vector<Vec2d> points;
    double             dx    = M_PI_2; // exact coordinates on main inflexion lobes
    double             limit = std::min(2 * M_PI, width);
    // [INTENT] Preallocate for expected number of points before adaptive refinement.
    points.reserve(coord_t(ceil(limit / tolerance / 3)));

    // [INTENT] Seed the curve with coarse sample points at π/2 intervals.
    for (double x = 0.; x < limit - EPSILON; x += dx) {
        points.emplace_back(Vec2d(x, f(x, z_sin, z_cos, vertical, flip)));
    }
    points.emplace_back(Vec2d(limit, f(limit, z_sin, z_cos, vertical, flip)));

    // [INTENT] Piecewise adaptive refinement: repeatedly insert midpoints that exceed
    // the area-based tolerance, then sort to maintain x-order. Terminates when no new
    // points were added (convergence).
    for (;;) {
        size_t size = points.size();
        for (unsigned int i = 1; i < size; ++i) {
            auto&  lp = points[i - 1]; // left point
            auto&  rp = points[i];     // right point
            double x  = lp(0) + (rp(0) - lp(0)) / 2;
            double y  = f(x, z_sin, z_cos, vertical, flip);
            Vec2d  ip = {x, y};
            // [INTENT] Test: if midpoint deviates from the chord (lp–rp) by more than
            // tolerance (measured via triangle area), insert the midpoint.
            if (std::abs(cross2(Vec2d(ip - lp), Vec2d(ip - rp))) > sqr(tolerance)) {
                points.emplace_back(std::move(ip));
            }
        }

        if (size == points.size())
            break; // [INTENT] No new points added — curve is sampled within tolerance.
        else {
            // insert new points in order
            std::sort(points.begin(), points.end(), [](const Vec2d& lhs, const Vec2d& rhs) { return lhs(0) < rhs(0); });
        }
    }

    return points;
}

// [INTENT] Top-level wave generator. Produces a set of Polylines that tile the bounding
// box (width × height in normalized Gyroid units) with the Gyroid wave pattern for layer Z.
//
// Algorithm:
//   1. Compute scaleFactor = scaled(line_spacing) / density_adjusted.
//   2. Compute z (normalized) = gridZ / scaleFactor; extract z_sin, z_cos.
//   3. Choose orientation: |z_sin| ≤ |z_cos| → vertical waves; else horizontal.
//   4. Build two period templates: 'odd' and 'even' (flip=!flip shifts phase by π).
//   5. Iterate over y0 from lower_bound to upper_bound in steps of π, generating
//      alternating odd/even wave rows.
//
// [STATE] All locals; no shared mutable state. gridZ is the only layer-varying input.
// [HAZARD H323] (repeat) std::swap(width, height) mutates the local copies — caller
// passes dimensions by value. The swap is intentional for the vertical-wave branch.
// [HAZARD H328] The step `y0 += M_PI` inside the loop body (for even rows) also advances
// the for-loop variable y0, which is then incremented by M_PI again at the loop header.
// Net effect: y0 advances by 2π per full iteration (one odd + one even row). This is
// intentional but visually non-obvious and easy to misread as a double-increment bug.
static Polylines make_gyroid_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    // [INTENT] Convert line_spacing to Slic3r scaled coordinates, factoring in density.
    const double scaleFactor = scale_(line_spacing) / density_adjusted;

    // [INTENT] Clamp tolerance to avoid over-sampling at large spacings: the geometric
    // improvement from sub-PatternTolerance resolution is negligible.
    // tolerance in scaled units. clamp the maximum tolerance as there's
    // no processing-speed benefit to do so beyond a certain point
    const double tolerance = std::min(line_spacing / 2, FillGyroid::PatternTolerance) / unscale<double>(scaleFactor);

    // [INTENT] Convert gridZ from Slic3r scaled units to normalized Gyroid units.
    // scale factor for 5% : 8 712 388
    // [UNCLEAR] The comment "1z = 10^-6 mm ?" refers to Slic3r's coord_t scale where
    // 1 unit = 10^-6 m = 1 μm. This is a developer note, not a formula.
    const double z     = gridZ / scaleFactor;
    const double z_sin = sin(z);
    const double z_cos = cos(z);

    // [INTENT] At z-phases where |sin(z)| ≤ |cos(z)|, the wave is more "vertical" than
    // "horizontal" and orientation must be swapped for correct tiling geometry.
    bool   vertical    = (std::abs(z_sin) <= std::abs(z_cos));
    double lower_bound = 0.;
    double upper_bound = height;
    bool   flip        = true;
    if (vertical) {
        flip        = false;
        lower_bound = -M_PI;
        upper_bound = width - M_PI_2;
        std::swap(width, height); // [HAZARD H323] Swap normalizes axis for vertical orientation.
    }

    // [INTENT] Pre-compute two period templates: odd (flip=true) and even (flip=false).
    // The even template is phase-shifted by π relative to odd, creating the interleaved
    // wave pattern characteristic of the Gyroid infill.
    std::vector<Vec2d> one_period_odd =
        make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip,
                        tolerance);             // creates one period of the waves, so it doesn't have to be recalculated all the time
    flip                               = !flip; // even polylines are a bit shifted
    std::vector<Vec2d> one_period_even = make_one_period(width, scaleFactor, z_cos, z_sin, vertical, flip, tolerance);
    Polylines          result;

    // [INTENT] Generate wave rows at y-spacing of π (half-wavelength of the Gyroid).
    // [HAZARD H328] y0 is mutated inside the loop body for the even row — the loop
    // variable advances by 2π total per iteration (M_PI from body + M_PI from for-step).
    for (double y0 = lower_bound; y0 < upper_bound + EPSILON; y0 += M_PI) {
        // creates odd polylines
        result.emplace_back(make_wave(one_period_odd, width, height, y0, scaleFactor, z_cos, z_sin, vertical, flip));
        // creates even polylines
        y0 += M_PI;
        if (y0 < upper_bound + EPSILON) {
            result.emplace_back(make_wave(one_period_even, width, height, y0, scaleFactor, z_cos, z_sin, vertical, flip));
        }
    }

    return result;
}

// [INTENT] Out-of-class definition for constexpr static member. Required by C++ pre-17 ODR
// rules when the member is odr-used (e.g., passed by const-reference to std::min).
// C++17 makes static constexpr members implicitly inline, eliminating the need for this.
// [HAZARD H320] If the project is compiled with -std=c++17 or later, this definition is
// redundant but harmless. If compiled with -std=c++14, omitting it causes a linker error.
// FIXME: needed to fix build on Mac on buildserver
constexpr double FillGyroid::PatternTolerance;

// [INTENT] Main FillGyroid entry point. Orchestrates the full pipeline:
//   1. Rotate the ExPolygon to canonical axis alignment (angle + CorrectionAngle).
//   2. Compute density-adjusted spacing and bounding box in grid-aligned scaled coords.
//   3. Expand bounding box by 10× spacing to prevent edge artifacts.
//   4. Call make_gyroid_waves() to generate the full wave grid.
//   5. Translate the wave grid to the bounding box origin.
//   6. Apply multiline offset (Orca extension for multi-extrusion passes).
//   7. Clip waves to the actual ExPolygon shape (intersection_pl).
//   8. Remove tiny fragments (< 0.8 × spacing) that could cause blob defects.
//   9. chain_or_connect_infill() for G-code travel optimization.
//   10. Rotate all output polylines back by +infill_angle.
//
// [STATE] Reads: this->angle, this->z, this->spacing, this->loop_clipping.
//         Writes: polylines_out (appended, never cleared).
// [COUPLING] Calls: make_gyroid_waves(), multiline_fill() (FillBase), intersection_pl()
//            (ClipperUtils), chain_or_connect_infill() (ShortestPath).
// [HAZARD H329] The bounding box is expanded by 10 × spacing before wave generation.
// This is a heuristic guard against aliasing at polygon edges. If spacing is very large
// (low density), the expansion could be 10–50 mm — wave generation for a larger-than-
// necessary area wastes computation but is functionally correct (clipped at step 7).
// [HAZARD H330] density_adjusted = std::max(0., ...) — if params.multiline is 0 (division
// by zero attempted), this would be UB. However, multiline is always ≥ 1 by FillParams
// invariant. The std::max(0.) guard only catches negative density, not division-by-zero.
void FillGyroid::_fill_surface_single(const FillParams&              params,
                                      unsigned int                   thickness_layers,
                                      const std::pair<float, Point>& direction,
                                      ExPolygon                      expolygon,
                                      Polylines&                     polylines_out)
{
    // [INTENT] Apply CorrectionAngle (-45°) plus user-specified rotation.
    // Rotating the ExPolygon (not the fill) is equivalent to rotating the fill in the
    // opposite direction — the result is rotated back at the end of the function.
    auto infill_angle = float(this->angle + (CorrectionAngle * 2 * M_PI) / 360.);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    BoundingBox bb = expolygon.contour.bounding_box();
    // Density adjusted to have a good %of weight.
    double density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);
    // Distance between the gyroid waves in scaled coordinates.
    coord_t distance = coord_t(scale_(this->spacing) / density_adjusted);

    // [INTENT] Snap bounding box min to a grid multiple of 2π×distance to ensure the
    // pattern tiles correctly across layers (alignment to phase-coherent origin).
    // align bounding box to a multiple of our grid module
    bb.merge(align_to_grid(bb.min, Point(2 * M_PI * distance, 2 * M_PI * distance)));

    // [INTENT] Expand by 10× spacing to avoid pattern truncation at polygon edges.
    // Expand the bounding box to avoid artifacts at the edges
    coord_t expand = 10 * (scale_(this->spacing));
    bb.offset(expand);

    // [INTENT] Generate the wave polylines for this bounding box at the current layer Z.
    // Width and height are in Gyroid units (bounding-box size / wave-spacing distance).
    // generate pattern
    Polylines polylines = make_gyroid_waves(scale_(this->z), density_adjusted, this->spacing, ceil(bb.size()(0) / distance) + 1.,
                                            ceil(bb.size()(1) / distance) + 1.);

    // [INTENT] make_gyroid_waves() generates waves starting at (0,0). Translate to the
    // actual bounding box position in model coordinates.
    // shift the polyline to the grid origin
    for (Polyline& pl : polylines)
        pl.translate(bb.min);

    // Apply multiline offset if needed
    // [INTENT] Orca-specific extension: if params.multiline > 1, replicate each wave
    // with transverse offsets to produce multi-extrusion-pass infill at lower density.
    multiline_fill(polylines, params, spacing);

    // [INTENT] Clip waves to the actual polygon shape. Waves outside the ExPolygon are
    // discarded (they were generated for the expanded bounding box).
    polylines = intersection_pl(std::move(polylines), expolygon);

    if (!polylines.empty()) {
        // [INTENT] Remove degenerate short fragments that would cause blob defects.
        // The 0.8× threshold leaves a small margin below line_spacing to retain
        // wall-connecting segments while discarding true stub artifacts.
        // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

    if (!polylines.empty()) {
        // [INTENT] Re-order and connect polylines for G-code travel optimization.
        // chain_or_connect_infill() may merge polylines end-to-end where gap < threshold.
        // connect lines
        size_t polylines_out_first_idx = polylines_out.size();
        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        // [INTENT] Rotate all newly added polylines back to their original orientation.
        // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r
