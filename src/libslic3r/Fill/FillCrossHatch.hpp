// [INTENT] Declares FillCrossHatch: a Bambu Lab original infill pattern that alternates
// line direction between layers and uses "transform layers" (transition layers) to
// smoothly rotate lines by 90° between horizontal and vertical phases.
//
// Design goals (per source comments):
//   - Alternates line direction every N layers to improve inter-layer adhesion.
//   - "Transform layers" introduce gradual direction shifts for line cohesion.
//   - Inspired by David Eccles' "improved 3D honeycomb" but more flexible.
//   - Optimised for high-speed printing (low noise, high travel efficiency).
//
// [COUPLING] Depends on FillBase (Fill base class, align_to_grid, chain_or_connect_infill,
//            multiline_fill), ClipperUtils (intersection_pl), ShortestPath (chain_polylines).
// [STATE] Uses Fill::z (layer height), Fill::angle, Fill::spacing, Fill::bounding_box.
// [CONCURRENCY] No shared mutable state. Each call is independent per region.

#ifndef slic3r_FillCrossHatch_hpp_
#define slic3r_FillCrossHatch_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// [INTENT] FillCrossHatch: a multi-layer alternating direction infill.
// - "Repeat layers": straight horizontal or vertical lines (like standard Line infill).
// - "Transform layers": zig-zag patterns that transition between orientations.
// - The direction (horizontal / vertical) and phase are determined by Fill::z.
//
// [HAZARD H364] The direction and transition phase are computed from Fill::z using
// continuous floating-point modular arithmetic. If Fill::z is not monotonically
// increasing (e.g., re-slicing or non-standard layer ordering), the pattern phase
// is unpredictable. No cross-layer state is stored — phase is stateless per call.
class FillCrossHatch : public Fill
{
public:
    Fill* clone() const override { return new FillCrossHatch(*this); };
    ~FillCrossHatch() override {}
    // [INTENT] Lines within a phase do not self-intersect. Transform layers zig-zag
    // but adjacent segments are non-intersecting by construction.
    bool is_self_crossing() override { return false; }

protected:
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_FillCrossHatch_hpp_
