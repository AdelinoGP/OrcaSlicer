// [INTENT] Declares FillGyroid: a 3D Gyroid surface infill pattern for FDM printing.
// The Gyroid is a triply periodic minimal surface (TPMS) defined implicitly by:
//   sin(x)cos(y) + sin(y)cos(z) + sin(z)cos(x) = 0
//
// In 2D cross-section at layer height z, this produces a wavy sinusoidal pattern.
// The algorithm slices the 3D surface at the current layer Z, generating a set of
// horizontal (or vertical, depending on z phase) wave polylines that fill the XY plane.
//
// Key design parameters:
//   CorrectionAngle = -45°: rotates the pattern 45° to maximize printing speed in default
//                           orientation (diagonals are faster on Cartesian machines).
//   DensityAdjust = 2.44: empirical factor scaling density-to-material-weight mapping.
//   PatternTolerance = 0.2 mm: controls adaptive sampling resolution of the wave curve.
//
// [COUPLING] Depends on FillBase (multiline_fill, chain_or_connect_infill), ClipperUtils
//            (intersection_pl), ShortestPath (chain_or_connect_infill).
// [STATE] Inherits Fill::spacing, Fill::z, Fill::angle, Fill::loop_clipping.
// [CONCURRENCY] No shared mutable state. Thread-safe per-fill-region call.

#ifndef slic3r_FillGyroid_hpp_
#define slic3r_FillGyroid_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// [INTENT] FillGyroid generates wavy sinusoidal infill lines that are cross-sections of
// the 3D Gyroid TPMS at the current layer Z. The waves interlock between layers to form
// a mechanically efficient 3D structure.
//
// [HAZARD H319] FillGyroid does not override use_bridge_flow() — the inherited default
// returns false. The header comment says "require bridge flow since most of this pattern
// hangs in air" but the implementation returns false (opposite). This is a dead comment
// that contradicts the code. In practice, Gyroid infill does NOT use bridge flow.
class FillGyroid : public Fill
{
public:
    FillGyroid() {}
    // [INTENT] Default copy constructor for the Fill factory mechanism.
    Fill* clone() const override { return new FillGyroid(*this); }

    // [INTENT] Returns false — Gyroid infill does NOT use bridge flow.
    // [HAZARD H319] This contradicts the "require bridge flow" class comment above.
    // The override is necessary to silence a possible parent class default that might
    // return true. See hazard note.
    bool use_bridge_flow() const override { return false; }

    // [INTENT] Returns false — Gyroid waves can self-intersect at certain densities and
    // z-values (two branches of the same TPMS surface crossing in projection). In practice
    // self-intersections are rare at default densities. Returning false allows the G-code
    // engine to apply travel-path optimization without special intersection handling.
    bool is_self_crossing() override { return false; }

    // [INTENT] -45° correction rotates the Gyroid pattern so that its main wave direction
    // aligns diagonally on a Cartesian printer, maximizing motion speed on machines with
    // diagonal-optimized kinematics.
    static constexpr float CorrectionAngle = -45.;

    // [INTENT] Empirical density correction factor. The Gyroid surface occupies ~41% of
    // space at 100% fill (not 100%), so density must be scaled by 2.44 to produce the
    // target weight-percentage. Formula: density_adjusted = density × DensityAdjust / multiline.
    static constexpr double DensityAdjust = 2.44;

    // [INTENT] Maximum chord-height tolerance for adaptive subdivision of the Gyroid wave
    // curve. Controls the sampling resolution: lower = more polyline points, smoother curve.
    // Clamped to min(spacing/2, PatternTolerance) to avoid over-sampling at large spacings.
    // [HAZARD H320] `constexpr double PatternTolerance` requires an out-of-class definition
    // in FillGyroid.cpp (`constexpr double FillGyroid::PatternTolerance;`) because C++ pre-17
    // ODR requires this for constexpr static members used in expressions. The .cpp definition
    // is marked "FIXME: needed to fix build on Mac on buildserver" — it's a pre-C++17 ODR fix.
    static constexpr double PatternTolerance = 0.2;

protected:
    // [INTENT] Main fill entry point. Generates Gyroid wave polylines for the given ExPolygon.
    // Algorithm:
    //   1. Rotate the ExPolygon by -(angle + CorrectionAngle) to align with the canonical axis.
    //   2. Compute density_adjusted = density × DensityAdjust / multiline.
    //   3. Compute scaleFactor = scaled(spacing) / density_adjusted for unit conversion.
    //   4. Determine z phase (z_sin, z_cos) and whether to use horizontal or vertical waves.
    //   5. make_gyroid_waves(): generate infinite-plane waves, clip to bounding box.
    //   6. multiline_fill(): Orca extension for multi-line offset.
    //   7. intersection_pl(): clip waves to ExPolygon boundary.
    //   8. Remove very short polylines (< 0.8 × spacing) that cause blobs.
    //   9. chain_or_connect_infill(): order/connect for G-code efficiency.
    //   10. Rotate all resulting polylines back by +infill_angle.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_FillGyroid_hpp_
