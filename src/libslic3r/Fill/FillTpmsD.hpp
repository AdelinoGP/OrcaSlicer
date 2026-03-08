// [INTENT] Declares FillTpmsD: an infill pattern based on the Schwartz D (Diamond)
// triply periodic minimal surface (TPMS). The surface is defined by:
//   sin(x)·sin(y)·sin(z) - cos(x)·cos(y)·cos(z) = 0
// which can be factored as:
//   (sin(z) - cos(z))·cos(x-y) - (sin(z) + cos(z))·cos(x+y) = 0
//
// The 3D surface is sliced at layer z to produce a 2D wave pattern.
// The wave equation is solved analytically by sweeping parameter u and computing v.
//
// Key constants:
//   CorrectionAngle = -45°  — rotates the pattern 45° for better print speed.
//   DensityAdjust   = 2.1   — empirical scale factor for the density-to-spacing mapping.
//   PatternTolerance = 0.1 mm⁻² — adaptive refinement tolerance (chord deviation limit).
//
// [COUPLING] FillBase (Fill, align_to_grid, connect_infill, multiline_fill),
//            ClipperUtils (intersection_pl), ShortestPath (chain_polylines).
// [STATE] Uses Fill::z, Fill::angle, Fill::spacing. No persistent member state.
// [CONCURRENCY] No shared mutable state. Thread-safe per call.
//
// [HAZARD H375] `use_bridge_flow()` returns false but the comment says "require bridge
// flow since most of this pattern hangs in air." The comment contradicts the
// implementation — the same mismatch exists in FillGyroid (H319). TPMS D does NOT
// use bridge flow. A port that re-enables bridge flow based on the comment will produce
// incorrect pressure-advance for TPMS D layers.

#ifndef slic3r_FillTpmsD_hpp_
#define slic3r_FillTpmsD_hpp_

#include <utility>

#include "libslic3r/libslic3r.h"
#include "FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {
class Point;

// [INTENT] FillTpmsD: Schwartz Diamond TPMS infill.
// Produces a wave-based 2D cross-section of the Schwartz D surface at each layer Z.
// The wave function is computed analytically with adaptive chord-error refinement.
class FillTpmsD : public Fill
{
public:
    FillTpmsD() {}
    Fill* clone() const override { return new FillTpmsD(*this); }

    // require bridge flow since most of this pattern hangs in air
    // [HAZARD H375] Comment contradicts implementation — returns false, not true.
    bool use_bridge_flow() const override { return false; }

    // Correction applied to regular infill angle to maximize printing
    // speed in default configuration (degrees)
    // [INTENT] -45° rotation aligns the diagonal TPMS waves with the print head's
    // typical fast-axis direction, reducing deceleration/acceleration events.
    static constexpr float CorrectionAngle = -45.;

    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    bool is_self_crossing() override { return false; }

    // Density adjustment to have a good %of weight.
    // [INTENT] 2.1× empirical factor to correct for the TPMS wave's non-uniform
    // density distribution. Without this, the measured infill weight would be less
    // than the requested density percentage.
    // [HAZARD H376] DensityAdjust=2.1 is an empirical calibration constant with no
    // documented derivation. It applies only to the DensityAdjust pathway (density < ~95%),
    // not to solid fills. A port must preserve this constant or infill density will be wrong.
    static constexpr double DensityAdjust = 2.1;

    // Gyroid upper resolution tolerance (mm^-2)
    // [INTENT] Maximum chord deviation (in scaled units after normalisation) before
    // the adaptive refinement inserts a midpoint. Smaller = more points, better accuracy.
    // [HAZARD H377] Label says "Gyroid" — copy-paste from FillGyroid. This is the
    // Schwartz D pattern. The tolerance value (0.1) may not be optimal for TPMS D's
    // different curvature profile.
    static constexpr double PatternTolerance = 0.1;
};

} // namespace Slic3r

#endif // slic3r_FillTpmsD_hpp_
