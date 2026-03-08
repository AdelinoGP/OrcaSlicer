// [INTENT] Declares FillHoneycomb: a 2D regular hexagonal honeycomb infill pattern.
// Each layer rotates by 60° from the previous (cycling through 0°, 60°, 120°) to produce
// a 3D-stable interlocking structure. The hexagon geometry is computed from the line
// spacing and density, cached per (density, spacing) pair to avoid recomputation.
//
// The hexagon pattern is generated as a set of zig-zag polylines that trace the edges
// of the hexagonal grid. Each polyline covers the full bounding box height, snaking up
// and down between alternating hex vertices.
//
// Key geometry: for a regular hexagon with inner radius = distance/2:
//   hex_side   = distance / (sqrt(3)/2)  [side length]
//   hex_width  = distance * 2             [full width = hex_side * sqrt(3)]
//   y_short    = distance * sqrt(3)/3    [half the hex corner-to-corner height]
//   x_offset   = spacing/2               [half-width alignment offset]
//   y_offset   = x_offset * sqrt(3)/3    [corresponding Y offset for 60° angle]
//
// [COUPLING] Depends on FillBase (multiline_fill, chain_or_connect_infill),
//            ClipperUtils (intersection_pl), ShortestPath.
// [STATE] Inherits Fill::spacing, Fill::angle. Maintains a `cache` map keyed by
//         (density, spacing) to avoid redundant geometry recomputation.
// [CONCURRENCY] The `cache` is an instance member with no locking. NOT thread-safe
//               for concurrent calls on the same FillHoneycomb instance.

#ifndef slic3r_FillHoneycomb_hpp_
#define slic3r_FillHoneycomb_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// [INTENT] FillHoneycomb generates regular hexagonal honeycomb infill by computing
// per-hex geometry once and caching it. Each call to _fill_surface_single() uses
// the cached parameters to generate zig-zag polylines, then clips to the ExPolygon.
//
// [HAZARD H339] `cache` is an `std::map` instance member. If FillHoneycomb objects are
// shared across threads (unlikely but possible via Fill factory), concurrent cache
// insertion is a data race. No mutex protects the cache.
//
// [HAZARD H340] `_layer_angle()` returns 60° × (idx % 3). If `idx` overflows `size_t`
// (unlikely for normal print jobs) the modulo wraps to 0. For very long prints
// (> SIZE_MAX layers), the angle resets to 0° instead of continuing the 60° cycle.
class FillHoneycomb : public Fill
{
public:
    ~FillHoneycomb() override {}
    // [INTENT] Returns false — hexagonal grid lines within a layer do not self-intersect.
    bool is_self_crossing() override { return false; }

protected:
    // [INTENT] Default copy constructor for Fill factory mechanism.
    Fill* clone() const override { return new FillHoneycomb(*this); };

    // [INTENT] Main fill entry point. Generates hexagonal zig-zag polylines for the
    // given ExPolygon, clips to shape, and connects for G-code travel efficiency.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    // [INTENT] Cache key: (density, spacing) pair uniquely identifies a hex geometry.
    // `density` is float (not double) to match FillParams::density type.
    // [HAZARD H341] Floating-point equality comparisons in operator== and operator< are
    // exact. Two identical densities from different floating-point paths (e.g., 0.2f
    // computed via different expressions) may not compare equal, causing duplicate cache
    // entries with identical geometry. Not a correctness issue, but wastes memory.
    struct CacheID
    {
        CacheID(float adensity, coordf_t aspacing) : density(adensity), spacing(aspacing) {}
        float    density;
        coordf_t spacing;
        bool     operator<(const CacheID& other) const
        {
            return (density < other.density) || (density == other.density && spacing < other.spacing);
        }
        bool operator==(const CacheID& other) const { return density == other.density && spacing == other.spacing; }
    };

    // [INTENT] Cached hexagon geometry for a given (density, spacing) pair.
    // All values are in scaled Slic3r coordinates (coord_t = int32, 1 unit = 1 nm).
    // [STATE] Populated lazily on first use; never invalidated.
    struct CacheData
    {
        coord_t distance;       // Half-width between parallel hex edges (scaled)
        coord_t hex_side;       // Length of one hex side (scaled)
        coord_t hex_width;      // Full hex width = distance*2 (scaled)
        coord_t pattern_height; // Y-period of the hex pattern = hex_height + hex_side
        coord_t y_short;        // Short Y segment: distance*sqrt(3)/3 (scaled)
        coord_t x_offset;       // Half-spacing offset for line alignment
        coord_t y_offset;       // Corresponding Y offset for 60° vertex alignment
        Point   hex_center;     // Center of reference hex for rotation pivot
    };
    typedef std::map<CacheID, CacheData> Cache;
    // [HAZARD H339] Cache member — not thread-safe for concurrent calls on same instance.
    Cache cache;

    // [INTENT] Returns the infill rotation angle for a given layer index.
    // FillHoneycomb rotates by 60° per layer, cycling through 3 orientations (0°, 60°, 120°).
    float _layer_angle(size_t idx) const override { return float(M_PI / 3.) * (idx % 3); }
};

} // namespace Slic3r

#endif // slic3r_FillHoneycomb_hpp_
