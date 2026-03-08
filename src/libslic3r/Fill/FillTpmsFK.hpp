// [INTENT] Declares FillTpmsFK: an infill pattern based on the Fischer-Koch S (FK)
// triply periodic minimal surface (TPMS). The surface equation is:
//   cos(2x)·sin(y)·cos(z) + cos(2y)·sin(z)·cos(x) + cos(2z)·sin(x)·cos(y) = 0
//
// Unlike FillTpmsD (which solves the surface analytically as a wave function),
// FillTpmsFK uses a **MarchingSquares** algorithm over a sampled scalar field.
// The field is sampled on a grid of cell size gsizef=0.40mm with raster pixel
// accuracy rsizef=0.004mm.
//
// Key design points:
// - No DensityAdjust constant (unlike FillTpmsD); density is controlled via
//   vari_T = 4.18 × spacing × multiline / density_factor, where
//   density_factor = min(0.9, params.density) caps effective density at 90%.
// - Uses TBB parallel execution (`ex_tbb`) for MarchingSquares.
// - Calls `connect_infill` directly (not `chain_or_connect_infill`) because
//   the FK pattern produces internal "islands" that chain_infill cannot handle.
// - CorrectionAngle = -45°, same as FillTpmsD.
//
// [COUPLING] FillBase (Fill, connect_infill, multiline_fill), ClipperUtils (intersection_pl),
//            MarchingSquares (execute_with_policy), FillTpmsFK.cpp (ScalarField, get_polylines).
// [STATE] Uses Fill::z, Fill::angle, Fill::spacing. No persistent member state.
// [CONCURRENCY] MarchingSquares uses TBB (thread-safe internally). No shared mutable state.
//
// [HAZARD H380] `use_bridge_flow()` returns false, but the comment says "require bridge
//   flow since most of this pattern hangs in air." Same dead-comment contradiction as
//   FillTpmsD (H375) and FillGyroid (H319). FK does NOT use bridge flow.
// [HAZARD H381] No `DensityAdjust` constant in FillTpmsFK (unlike FillTpmsD's 2.1×).
//   Instead, the density–period relation is encoded as `vari_T = 4.18 × spacing × multiline
//   / density_factor` in the .cpp. The 4.18 factor is empirical with no documented derivation.
//   A port must preserve this constant or infill density will deviate from the requested value.
// [HAZARD H382] `density_factor = min(0.9, params.density)` hard-clamps density at 90%.
//   Requesting 100% FK infill silently produces ~90% effective density. No warning is emitted.
//   This is intentional (the FK field becomes degenerate near density=1.0) but must be
//   documented for a port that exposes density as a user-visible parameter.

#ifndef slic3r_FillTpmsFK_hpp_
#define slic3r_FillTpmsFK_hpp_

#include <utility>

#include "libslic3r/libslic3r.h"
#include "FillBase.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {
class Point;

// [INTENT] FillTpmsFK: Fischer-Koch S TPMS infill.
// Generates iso-contours of the FK S scalar field at each layer Z using MarchingSquares.
// Unlike the analytical wave approach of FillTpmsD, the MarchingSquares approach
// handles the FK surface's more complex topology (internal "islands") correctly.
class FillTpmsFK : public Fill
{
public:
    FillTpmsFK() {}
    Fill* clone() const override { return new FillTpmsFK(*this); }

    // require bridge flow since most of this pattern hangs in air
    // [HAZARD H380] Comment contradicts implementation — returns false, not true.
    bool use_bridge_flow() const override { return false; }

    // Correction applied to regular infill angle to maximize printing
    // speed in default configuration (degrees)
    // [INTENT] -45° rotation aligns the FK pattern diagonal with the printer's fast axis,
    // same rationale as FillTpmsD::CorrectionAngle.
    static constexpr float CorrectionAngle = -45.;

    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    bool is_self_crossing() override { return false; }
};

} // namespace Slic3r

#endif // slic3r_FillTpmsFK_hpp_
