// [INTENT] Declares Fill3DHoneycomb: an infill pattern that generates cross-sections of a
// space-filling truncated regular octahedron (a.k.a. bitruncated cubic honeycomb).
// The octahedrons are oriented with square faces horizontal (edges parallel to X/Y axes).
//
// At each layer Z, the algorithm slices the 3D tesselation to produce a set of horizontal
// wave polylines. Alternate layers produce vertical vs. horizontal stripe orientation
// depending on the Z-cycle phase, creating a 3D interlocking structure.
//
// Key design:
//   - triWave(pos, gridSize): triangular wave, period=2×gridSize, amplitude=gridSize/2.
//   - troctWave(pos, gridSize, Zpos): truncated octahedron profile using triWave.
//   - getCriticalPoints(): identifies the 4 inflection points per period.
//   - makeActualGrid(): generates the full set of wave polylines for a given Z.
//   - makeGrid(): wraps makeActualGrid(), converting Pointfs to Polylines.
//
// [COUPLING] Depends on FillBase (chain_or_connect_infill, multiline_fill),
//            ClipperUtils (intersection_pl), ShortestPath (chain_or_connect_infill).
// [STATE] Inherits Fill::spacing, Fill::z, Fill::angle.
// [CONCURRENCY] No shared mutable state. Thread-safe per-fill-region call.

#ifndef slic3r_Fill3DHoneycomb_hpp_
#define slic3r_Fill3DHoneycomb_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// [INTENT] Fill3DHoneycomb generates polylines that are horizontal cross-sections of a
// space-filling truncated octahedron tesselation (the Goldberg-Coxeter 3D honeycomb).
// Each layer contributes to a different phase of the 3D structure.
// The pattern self-supports in 3D: each layer lands on the previous wave pattern.
//
// [HAZARD H331] `use_bridge_flow()` returns false. The comment "updated 3D Honeycomb
// doesn't need bridge flow because the pattern is placed on top of previous layers" is
// the intended design — not a contradiction like FillGyroid (H319). This is intentional.
class Fill3DHoneycomb : public Fill
{
public:
    // [INTENT] Default copy constructor for Fill factory mechanism.
    Fill* clone() const override { return new Fill3DHoneycomb(*this); };
    ~Fill3DHoneycomb() override {}

    // [INTENT] No bridge flow needed — pattern rests on previous layer waves.
    bool use_bridge_flow() const override { return false; }

    // [INTENT] Returns false — 3D Honeycomb waves do not self-intersect within a layer
    // (each Z cross-section is a set of non-crossing monotone curves).
    bool is_self_crossing() override { return false; }

protected:
    // [INTENT] Main fill entry point. Generates horizontal cross-sections of the
    // truncated octahedron at the current layer Z, clips to the ExPolygon, connects
    // for G-code travel efficiency, and rotates back to the original orientation.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_Fill3DHoneycomb_hpp_
