// [INTENT] FillTpmsD.cpp — Schwartz Diamond (TPMS D) infill implementation.
// Generates 2D cross-sections of the Schwartz Diamond minimal surface at each
// layer height Z.  The implicit surface equation is:
//   sin(x)·sin(y)·sin(z) - cos(x)·cos(y)·cos(z) = 0
// which is rearranged analytically into a parameterised wave function
// (see `make_waves` below).  The result is a set of continuous polylines that
// are clipped to the expolygon and optionally multi-line expanded.
//
// [COUPLING] FillBase (align_to_grid, connect_infill, multiline_fill, chain_polylines),
//            ClipperUtils (intersection_pl), FillTpmsD.hpp (PatternTolerance, DensityAdjust).
// [STATE] Stateless static helpers (`scaled_floor`, `make_waves`).
//         `_fill_surface_single` reads Fill::z, Fill::angle, Fill::spacing from base.
// [CONCURRENCY] No shared mutable state; thread-safe per-call.

#include <cmath>
#include <algorithm>
#include <vector>
// #include <cstddef>

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"
#include "FillTpmsD.hpp"

namespace Slic3r {

// [INTENT] `scaled_floor(x, scale)` — snap `x` down to the nearest multiple of `scale`.
// Used to align the vShift loop start to an integer multiple of 2π, ensuring the
// wave tiles align consistently across layers regardless of bounding-box position.
// [MEMORY] Pure function, no heap allocation.
static double scaled_floor(double x, double scale) { return std::floor(x / scale) * scale; }

// [INTENT] `make_waves` — produce the complete set of TPMS D wave polylines for one layer.
//
// Mathematical derivation:
//   sin(x)·sin(y)·sin(z) - cos(x)·cos(y)·cos(z) = 0
//   ⟹ (sin(z)-cos(z))·cos(x-y) - (sin(z)+cos(z))·cos(x+y) = 0
//   Setting u = x-y, v = x+y, a = sin(z)-cos(z), b = sin(z)+cos(z):
//     a·cos(u) = b·cos(v)
//     v = acos(a/b · cos(u))   (valid when |b| ≥ |a|)
//   If |a| > |b|, swap u/v for numerical stability (avoids acos domain overflow).
//
// [INTENT] Parameters:
//   gridZ            — layer Z in unscaled mm (used to compute the z-dependent coefficients a, b).
//   density_adjusted — effective density after DensityAdjust + multiline correction.
//   line_spacing     — nominal spacing in mm.
//   width, height    — bounding-box dimensions in curve-space units (not mm).
//
// [MEMORY] Returns a `Polylines` vector; each polyline is one wave branch
//          (forward root +v or backward root -v) for one vShift period.
// [HAZARD H378] `wave.emplace(wave.begin()+current+1, middleU, middleV)` inserts into
//   a `std::vector` during an index-based traversal loop. This is index-safe
//   (uses `current` integer, not an iterator), but a refactor that switches to
//   range-for or iterator-based traversal would cause iterator invalidation UB
//   during the adaptive refinement loop.
static Polylines make_waves(double gridZ, double density_adjusted, double line_spacing, double width, double height)
{
    // [STATE] scaleFactor converts between curve-space and scaled-coord units.
    // Larger scaleFactor → smaller visual cells (higher density).
    const double scaleFactor = scale_(line_spacing) / density_adjusted;

    // [INTENT] tolerance — maximum chord deviation before inserting a midpoint
    // in the adaptive refinement pass. Clamped to line_spacing/2 so that even at
    // very coarse densities the chord error doesn't exceed half a line width.
    // Normalised by unscale(scaleFactor) to work in curve-space (dimensionless) units.
    // tolerance in scaled units. clamp the maximum tolerance as there's
    // no processing-speed benefit to do so beyond a certain point
    const double tolerance = std::min(line_spacing / 2, FillTpmsD::PatternTolerance) / unscale<double>(scaleFactor);

    // scale factor for 5% : 8 712 388
    //  1z = 10^-6 mm ?
    //  [INTENT] z — layer height mapped to curve-space (dividing by scaleFactor normalises
    //  the spatial frequency so the wave period matches the requested line spacing).
    const double z = gridZ / scaleFactor;
    Polylines    result;

    // sin(x)*sin(y)*sin(z)-cos(x)*cos(y)*cos(z)=0
    // 2*sin(x)*sin(y)*sin(z)-2*cos(x)*cos(y)*cos(z)=0
    //(cos(x-y)-cos(x+y))*sin(z)-(cos(x-y)+cos(x+y))*cos(z)=0
    //(sin(z)-cos(z))*cos(x-y)-(sin(z)+cos(z))*cos(x+y)=0
    double a = sin(z) - cos(z);
    double b = sin(z) + cos(z);
    // a*cos(x-y)-b*cos(x+y)=0
    // u=x-y, v=x+y

    // [INTENT] The bounding rectangle in (u, v) space is derived from the (x, y) bounding
    // box [0..width] × [0..height] by the linear transform u=x-y, v=x+y.
    // minU/maxU and minV/maxV define the parallelogram that covers the original bbox.
    double minU = 0 - height;
    double maxU = width - 0;
    double minV = 0 + 0;
    double maxV = width + height;
    // a*cos(u)-b*cos(v)=0
    // if abs(b)<abs(a), then u=acos(b/a*cos(v)) is a continuous line
    // otherwise we swap u and v

    // [INTENT] swapUV ensures the acos argument is always in [-1, 1].
    // When |a| > |b|, the equation a·cos(u) = b·cos(v) has |b/a| ≤ 1 so v = acos(b/a·cos(u))
    // is always defined. The swap makes b the "larger" coefficient.
    const bool swapUV = (std::abs(a) > std::abs(b));
    if (swapUV) {
        std::swap(a, b);
        std::swap(minU, minV);
        std::swap(maxU, maxV);
    }
    std::vector<std::pair<double, double>> wave;
    { // fill one wave
        // [INTENT] v(u) — the forward root of the wave equation for given u.
        // acos(a/b · cos(u)) is well-defined because |a/b| ≤ 1 (guaranteed by swapUV).
        const auto v = [&](double u) { return acos(a / b * cos(u)); };
        // [INTENT] Start with `initialSegments` uniformly-spaced samples over one 2π period
        // starting at minU. The adaptive refinement pass below inserts additional midpoints
        // wherever the chord deviation exceeds `tolerance`.
        const int initialSegments = 16;
        for (int c = 0; c <= initialSegments; ++c) {
            const double u = minU + 2 * M_PI * c / initialSegments;
            wave.emplace_back(u, v(u));
        }
        { // refine
            // [INTENT] Adaptive chord-error refinement. For each adjacent pair of points,
            // compute the true midpoint v((u1+u2)/2) and compare with the linear interpolant
            // (v1+v2)/2. If the difference exceeds `tolerance`, insert the true midpoint
            // and restart the comparison from the newly inserted point.
            // [HAZARD H378] `wave.emplace(wave.begin()+current+1, ...)` inserts into a
            //   vector during index-based iteration. Safe here because `current` is a plain
            //   integer. Converting to iterator-based or range-for traversal would cause UB.
            int current = 0;
            while (current + 1 < int(wave.size())) {
                const double u1      = wave[current].first;
                const double u2      = wave[current + 1].first;
                const double middleU = (u1 + u2) / 2;
                const double v1      = wave[current].second;
                const double v2      = wave[current + 1].second;
                const double middleV = v((u1 + u2) / 2);
                if (std::abs(middleV - (v1 + v2) / 2) > tolerance)
                    wave.emplace(wave.begin() + current + 1, middleU, middleV);
                else
                    ++current;
            }
        }
        // [INTENT] Tile the one-period wave across the full u-extent by shifting
        // each interior point by multiples of 2π. Start from index 1 (not 0) because
        // index 0 is already the duplicate of the last point of the previous period.
        for (int c = 1; c < int(wave.size()) && wave.back().first < maxU;
             ++c) // we start from 1 because the 0-th one is already duplicated as the last one in a period
            wave.emplace_back(wave[c].first + 2 * M_PI, wave[c].second);
    }
    // [INTENT] Outer loop: sweep vShift from the floor of minV (snapped to 2π multiples)
    // through maxV + one extra period. Each vShift produces two polylines: the
    // forward root (+v) and the backward root (-v), which together trace both
    // branches of the symmetric Schwartz D wave at this (u, vShift) phase.
    for (double vShift = scaled_floor(minV, 2 * M_PI); vShift < maxV + 2 * M_PI; vShift += 2 * M_PI) {
        for (bool forwardRoot : {false, true}) {
            result.emplace_back();
            for (const auto& pair : wave) {
                const double u = pair.first;
                double       v = pair.second;
                // [INTENT] forwardRoot selects +v or -v branch of the wave.
                // vShift tiles the wave vertically (in v-space) across the bbox.
                v = (forwardRoot ? v : -v) + vShift;
                // [INTENT] Back-transform from (u,v) to (x,y):
                //   x = (u + v) / 2
                //   y = (v - u) / 2 · sign(swapUV)
                // swapUV flips the y sign to undo the earlier u/v coordinate swap.
                const double x = (u + v) / 2;
                const double y = (v - u) / 2 * (swapUV ? -1 : 1);
                result.back().points.emplace_back(x * scaleFactor, y * scaleFactor);
            }
        }
    }
    // todo: select the step better
    //  [UNCLEAR] The comment above is from the original authors — the 16-segment initial
    //  discretization is a fixed heuristic. No analysis of optimal initial step count
    //  for different density/tolerance combinations has been documented.
    return result;
}

// [INTENT] Out-of-class constexpr definition required for C++14 ODR compliance.
// In C++17, this redundant definition may be removed safely.
// FIXME: needed to fix build on Mac on buildserver
constexpr double FillTpmsD::PatternTolerance;

// [INTENT] `_fill_surface_single` — main entry point called by the fill engine for
// each ExPolygon region. Orchestrates: angle rotation → wave generation → bbox alignment
// → multiline expansion → clipping → small-segment pruning → connection.
//
// [STATE] Reads: this->angle, this->z, this->spacing (inherited from Fill).
//         Writes: appends to `polylines_out`.
// [COUPLING] multiline_fill (FillBase), intersection_pl (ClipperUtils),
//            connect_infill / chain_polylines (FillBase / ShortestPath).
// [CONCURRENCY] No shared mutable state. Thread-safe per-call.
void FillTpmsD::_fill_surface_single(const FillParams&              params,
                                     unsigned int                   thickness_layers,
                                     const std::pair<float, Point>& direction,
                                     ExPolygon                      expolygon,
                                     Polylines&                     polylines_out)
{
    // [INTENT] CorrectionAngle (-45°) aligns the TPMS D diagonal waves with the printer's
    // fast axis, reducing deceleration events. The combined angle is applied as a negative
    // rotation to the expolygon (rotating geometry is equivalent to rotating the pattern).
    auto infill_angle = float(this->angle + (CorrectionAngle * 2 * M_PI) / 360.);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    BoundingBox bb = expolygon.contour.bounding_box();
    // Density adjusted to have a good %of weight.
    // [INTENT] density_adjusted < 1 → larger scaleFactor → smaller wave cells → denser fill.
    // multiline divides density so that multi-pass fills don't double-count line width.
    // [HAZARD H379] `params.multiline == 0` would cause division by zero. The FillParams
    //   invariant should guarantee multiline ≥ 1, but there is no assert here.
    double density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline);
    // Distance between the gyroid waves in scaled coordinates.
    coord_t distance = coord_t(scale_(this->spacing) / density_adjusted);

    // [INTENT] align_to_grid snaps bb.min to the nearest multiple of (2π × distance) in
    // each axis. This ensures that the wave tiling origin is consistent across layers
    // even when the expolygon bbox shifts, preventing inter-layer pattern drift.
    // align bounding box to a multiple of our grid module
    bb.merge(align_to_grid(bb.min, Point(2 * M_PI * distance, 2 * M_PI * distance)));

    // generate pattern
    // [INTENT] ceil(bb.size() / distance) + 1 computes the number of wave periods needed
    // to cover the full bounding-box dimension. The +1 ensures coverage at the edges
    // even when bb.size() is an exact multiple of distance.
    Polylines polylines = make_waves(scale_(this->z), density_adjusted, this->spacing, ceil(bb.size()(0) / distance) + 1.,
                                     ceil(bb.size()(1) / distance) + 1.);

    // shift the polyline to the grid origin
    // [INTENT] Translate all generated waves so their (0,0) origin aligns with bb.min.
    for (Polyline& pl : polylines)
        pl.translate(bb.min);

    // [INTENT] multiline_fill offsets each polyline laterally by spacing × multiline index,
    // creating parallel "fat" infill passes when params.multiline > 1.
    // Apply multiline offset if needed
    multiline_fill(polylines, params, spacing);

    // [INTENT] Clip the infinite-plane wave pattern to the actual expolygon boundary.
    // All wave segments outside the polygon are discarded.
    polylines = intersection_pl(std::move(polylines), expolygon);

    if (!polylines.empty()) {
        // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        // [INTENT] minlength = 0.8 × spacing ensures fragments shorter than 80% of one
        // line width are removed. These would produce negligible material deposition and
        // cause unnecessary travel moves.
        const double minlength = scale_(0.8 * this->spacing);
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

    if (!polylines.empty()) {
        // connect lines
        size_t polylines_out_first_idx = polylines_out.size();
        // [INTENT] `dont_connect()` is set for certain sparse-infill modes where travel
        // moves between segments are acceptable. Otherwise `connect_infill` attempts to
        // bridge adjacent segment endpoints without additional travel.
        if (params.dont_connect())
            append(polylines_out, chain_polylines(polylines));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        // new paths must be rotated back
        // [INTENT] Undo the initial rotation applied to expolygon so all output polylines
        // are in world coordinates. Only the new polylines (from polylines_out_first_idx
        // onward) are rotated; previously accumulated polylines are unaffected.
        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r
