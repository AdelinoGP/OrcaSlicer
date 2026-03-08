// [INTENT] Declares FillLine, an oscillating-line infill pattern.
// Each line alternates between a left-leaning and right-leaning orientation,
// producing a zig-zag effect. Adjacent lines can be connected (merged) into a
// continuous polyline when their endpoints are close enough.
//
// Key members:
//   _min_spacing       — base line spacing = scale_(spacing), density-independent.
//   _line_spacing      — actual spacing = _min_spacing / density.
//   _diagonal_distance — max Y-gap to consider two lines connectable (2 × _line_spacing).
//   _line_oscillation  — X-offset alternation amount = _line_spacing - _min_spacing.
//
// [COUPLING] Depends on FillBase (Fill base class, _adjust_solid_spacing, align_to_grid),
//            ClipperUtils (intersection_pl, offset, offset_ex), ShortestPath (chain_polylines).
// [STATE] _min_spacing, _line_spacing, _diagonal_distance, _line_oscillation are written
//         in _fill_surface_single on every call. Not thread-safe if two calls share instance.
// [CONCURRENCY] No mutex. Instance state is overwritten per call. Safe only if one call
//               at a time per instance (standard slicer usage pattern).

#ifndef slic3r_FillLine_hpp_
#define slic3r_FillLine_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Surface;

// [INTENT] FillLine: alternating-oscillation line infill.
// Generates vertical scan-lines with alternating X oscillation (odd lines shift left,
// even lines are straight). Consecutive lines that end close enough are merged into
// a single polyline to reduce travel moves.
class FillLine : public Fill
{
public:
    Fill* clone() const override { return new FillLine(*this); };
    ~FillLine() override = default;
    // [INTENT] Returns false — lines by construction do not cross each other.
    bool is_self_crossing() override { return false; }

protected:
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    // [STATE] Computed once per _fill_surface_single call.
    coord_t _min_spacing;  // Base scaled spacing (density=1.0 value).
    coord_t _line_spacing; // Actual spacing = _min_spacing / density.
    // distance threshold for allowing the horizontal infill lines to be connected into a continuous path
    coord_t _diagonal_distance; // Max Y gap for line connection = 2 × _line_spacing.
    // only for line infill
    coord_t _line_oscillation; // X oscillation = _line_spacing - _min_spacing. Non-zero when density < 1.

    // [INTENT] Build one scan-line at column x, from y_min to y_max.
    // Odd-index lines (i & 1) are shifted left by _line_oscillation at the start
    // and right by _line_oscillation at the end, creating the zig-zag.
    // Even-index lines are vertical (no oscillation).
    //
    // [HAZARD H359] The oscillation makes odd lines diagonal (not vertical). When
    // direction.first ≠ 0 they rotate correctly, but the FIXME in the .cpp notes
    // the overlap extension is only applied to "horizontal" (Y-axis) endpoints —
    // the X-extension for diagonal lines is not applied. For non-vertical infill
    // angles, line endpoints near the polygon boundary may be slightly short.
    Line _line(int i, coord_t x, coord_t y_min, coord_t y_max) const
    {
        coord_t osc = (i & 1) ? this->_line_oscillation : 0;
        return Line(Point(x - osc, y_min), Point(x + osc, y_max));
    }

    // [INTENT] Returns true if two polyline endpoints (separated by dist_X, dist_Y)
    // can be directly connected without a travel move.
    // Connection is allowed when:
    //   X gap ≈ _line_spacing ± _line_oscillation (within TOLERANCE)
    //   Y gap ≤ _diagonal_distance (= 2 × _line_spacing)
    //
    // [HAZARD H360] The Y gap test uses _diagonal_distance = 2 × _line_spacing.
    // This is a heuristic constant. For very high densities (small _line_spacing),
    // _diagonal_distance is small and connections are rare. For very low densities,
    // it is large and distant lines may be incorrectly connected across features.
    // No geometry check (e.g., does the connecting segment stay inside the polygon?)
    // is performed here — that check happens in the caller via expolygon_off.contains().
    bool _can_connect(coord_t dist_X, coord_t dist_Y)
    {
        const auto TOLERANCE = coord_t(10 * SCALED_EPSILON);
        return (dist_X >= (this->_line_spacing - this->_line_oscillation) - TOLERANCE) &&
               (dist_X <= (this->_line_spacing + this->_line_oscillation) + TOLERANCE) && (dist_Y <= this->_diagonal_distance);
    }
};

}; // namespace Slic3r

#endif // slic3r_FillLine_hpp_
