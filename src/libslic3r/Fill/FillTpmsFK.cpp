// [INTENT] FillTpmsFK.cpp — Fischer-Koch S (FK) TPMS infill implementation.
//
// Uses a MarchingSquares approach to compute 2D iso-contours of the Fischer-Koch S
// scalar field at each layer Z. This is fundamentally different from FillTpmsD, which
// solves the surface analytically.
//
// Fischer-Koch S surface equation:
//   cos(2x)·sin(y)·cos(z) + cos(2y)·sin(z)·cos(x) + cos(2z)·sin(x)·cos(y) = 0
//
// The MarchingSquares algorithm samples this field on a grid and traces iso-contours
// at isoval=0. The resulting polylines (as closed rings) are then simplified,
// clipped to the expolygon, and connected.
//
// [COUPLING] FillBase (Fill, connect_infill, multiline_fill), ClipperUtils (intersection_pl),
//            MarchingSquares (execute_with_policy, Ring, Coord), TBB (ex_tbb).
// [STATE] `ScalarField` is a per-call value type (no persistent shared state).
//         `_fill_surface_single` reads Fill::z, Fill::angle, Fill::spacing.
// [CONCURRENCY] `execute_with_policy(ex_tbb, ...)` parallelises MarchingSquares internally.
//               No shared mutable state between calls.

#include "../ClipperUtils.hpp"
#include "../MarchingSquares.hpp"
#include "FillTpmsFK.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace marchsq {
using namespace Slic3r;

using coordr_t = long; // length type for (r, c) raster coordinates.
// Note that coordf_t, Pointfs, Point3f, etc all use double not float.
using Pointf = Vec2d; // (x, y) field point in coordf_t.

// [INTENT] `ScalarField` — encapsulates the Fischer-Koch S field geometry and sampling.
// Serves as a MarchingSquares RasterType (via _RasterTraits specialisation below).
//
// Two-level coordinate system:
//   - "raster" (coordr_t): pixel-level grid for MarchingSquares, rsizef=0.004mm/pixel.
//   - "coord" (coord_t): Slic3r scaled coordinates (1 unit = 10^-6 mm).
//   - "real" (coordf_t/double): unscaled mm.
//
// [MEMORY] `size` and `offs` are `Point` (int32 scaled coords). For large prints, scaled
//          coordinates near int32 limits could overflow, but Slic3r's global coord space
//          keeps values well within int32 range for typical print beds.
struct ScalarField
{
    // [INTENT] gsizef — grid cell size in mm. Each MarchingSquares cell is ~0.40mm wide.
    // Larger values → coarser marching grid → faster but less precise polylines.
    static constexpr float gsizef = 0.40; // grid cell size in mm (roughly line segment length).
    // [INTENT] rsizef — raster pixel size in mm. Each pixel represents 0.004mm of field space.
    // The ratio gsize/rsize = 0.40/0.004 = 100 pixels per grid cell.
    static constexpr float rsizef = 0.004;                       // raster pixel size in mm (roughly point accuracy).
    const coord_t          rsize  = scaled(rsizef);              // raster pixel size in coord_t.
    const coordr_t         gsize  = std::round(gsizef / rsizef); // grid cell size in coordr_t.
    Point                  size;                                 // field size in coord_t.
    Point                  offs;                                 // field offset in coord_t.
    coordf_t               z;                                    // z offset as a float.
    float                  freq;                                 // field frequency in cycles per mm.
    float                  isoval = 0.0;                         // iso value threshold to use.

    // [INTENT] Constructor derives `freq` from the field period (vari_T, in mm).
    // `freq = 2π / period` — higher frequency = smaller cells = denser infill.
    // [HAZARD H383] `period` is passed as `vari_T = 4.18 × spacing × multiline / density_factor`.
    //   If `density_factor` is very small (near 0), `vari_T` → ∞ → `freq` → 0 → flat field.
    //   `density_factor = min(0.9, params.density)` prevents density_factor=0 only if
    //   params.density > 0 is guaranteed. No explicit guard at the ScalarField constructor.
    explicit ScalarField(const BoundingBox bb, const coordf_t z = 0.0, const float period = 10.0)
        : size{bb.size()}, offs{bb.min}, z{z}, freq{float(2 * PI) / period}
    {}

    // Get the scalar field value at x,y,z in coordf_t coordinates.
    // [INTENT] Evaluates the Fischer-Koch S implicit function at world coordinates (x,y,z).
    // x, y, z are in unscaled mm; freq maps them to the 0..2π phase domain.
    // [HAZARD H384] Uses `float` arithmetic (cosf/sinf) despite `coordf_t` inputs being double.
    //   The cast `freq * x` silently narrows double to float. For large field coordinates
    //   (large objects), float precision (7 digits) may cause phase errors. A port using
    //   double-precision trigonometry would be more accurate for large prints.
    float get_scalar(coordf_t x, coordf_t y, coordf_t z) const
    {
        const float fx = freq * x;
        const float fy = freq * y;
        const float fz = freq * z;

        // Fischer - Koch S equation:
        // cos(2x)sin(y)cos(z) + cos(2y)sin(z)cos(x) + cos(2z)sin(x)cos(y) = 0
        return cosf(2 * fx) * sinf(fy) * cosf(fz) + cosf(2 * fy) * sinf(fz) * cosf(fx) + cosf(2 * fz) * sinf(fx) * cosf(fy);
    }

    // Get the scalar field value at a Coord for the current z value.
    // [INTENT] Converts from raster-space Coord (row, col in pixel units) to world mm,
    // then evaluates the FK scalar. The stored `z` is used as the layer height.
    float get_scalar(Coord p) const
    {
        Pointf pf = to_Pointf(p);
        return get_scalar(pf.x(), pf.y(), z);
    }

    // Convert between dimension scales.
    // [INTENT] to_coord / to_coordr — lossless conversion between raster-pixel units
    // and Slic3r scaled coord units. Safe because rsize is an integer multiple.
    inline coord_t  to_coord(const coordr_t& x) const { return x * rsize; }
    inline coordr_t to_coordr(const coord_t& x) const { return x / rsize; }

    // Convert between point/coordinate systems, including translation.
    // [INTENT] to_Point: raster Coord → scaled coord Point (adds field offset `offs`).
    //          to_Coord: scaled coord Point → raster Coord (subtracts offset first).
    //          to_Pointf: scaled coord → unscaled mm (uses `unscaled()`).
    // [HAZARD H385] `to_coordr(p.y() - offs.y())` truncates (integer division) rather than
    //   rounding. Coords just below a pixel boundary map to the pixel below. Sub-pixel
    //   offsets introduce a systematic ≤1 pixel (0.004mm) bias in all converted coordinates.
    //   Acceptable for infill (0.004mm ≪ spacing ~0.4mm), but a port should use rounding.
    inline Point  to_Point(const Coord& p) const { return Point(to_coord(p.c) + offs.x(), to_coord(p.r) + offs.y()); }
    inline Coord  to_Coord(const Point& p) const { return Coord(to_coordr(p.y() - offs.y()), to_coordr(p.x() - offs.x())); }
    inline Pointf to_Pointf(const Point& p) const { return Pointf(unscaled(p.x()), unscaled(p.y())); }
    inline Pointf to_Pointf(const Coord& p) const { return to_Pointf(to_Point(p)); }
};

// [INTENT] Register ScalarField as a RasterType for MarchingSquares via template specialisation.
// MarchingSquares uses _RasterTraits<T>::get(), rows(), cols() to sample the field.
// `rows()` and `cols()` report the total number of raster pixels in Y and X respectively.
// [HAZARD H386] `rows()` = `to_coordr(sf.size.y())` — integer truncation. If `sf.size.y()`
//   is not exactly divisible by `rsize`, the last partial row is silently excluded.
//   The `bbox.offset(...)` expansion in `_fill_surface_single` provides margin, but
//   the truncation means the effective field extent is slightly smaller than the bbox.
// Register ScalarField as a RasterType for MarchingSquares.
template<> struct _RasterTraits<ScalarField>
{
    // The type of pixel cell in the raster
    using ValueType = float;

    // Value at a given position
    static float get(const ScalarField& sf, size_t row, size_t col) { return sf.get_scalar(Coord(row, col)); }

    // Number of rows and cols of the raster
    static size_t rows(const ScalarField& sf) { return sf.to_coordr(sf.size.y()); }
    static size_t cols(const ScalarField& sf) { return sf.to_coordr(sf.size.x()); }
};

// [INTENT] `get_polylines` — traces iso-contours of the scalar field at `sf.isoval`
// using MarchingSquares with TBB parallelism. The resulting polylines are closed rings
// (each `Ring` is converted to a `Polyline` by appending the first point at the end).
// Points are simplified with `poly.simplify(tolerance)` using Douglas-Peucker.
//
// [MEMORY] Allocates one `Polyline` per `Ring` returned by MarchingSquares.
//          Ring count depends on field topology — FK can produce many internal "islands".
// [HAZARD H387] `pts.push_back(pts.front())` closes the ring into a polyline by appending
//   the first point. This assumes pts is non-empty. If MarchingSquares returns an empty
//   ring (degenerate), this produces a Polyline with a single repeated point, which
//   survives the subsequent `pl.length() < minlength` filter only if minlength > 0.
// [HAZARD H388] The tolerance parameter defaults to `SCALED_EPSILON` (≈ 1e-3 scaled units,
//   sub-micron). When called from `_fill_surface_single`, the caller uses
//   `SCALED_SPARSE_INFILL_RESOLUTION` (~0.1mm) which is intentionally coarser.
//   Using the wrong tolerance (e.g., SCALED_EPSILON in production) would preserve
//   O(10×) more points per polyline with no quality benefit.
// Get the polylines for the scalar field. The tolerance is used for
// simplifying the polylines to remove redundant points. The default will
// only remove points on (almost) perfectly straight lines. Set to -1 to turn
// off simplifying entirely. Note tolerance is the max line deviation from
// simplifying and should be scaled.
Polylines get_polylines(const ScalarField& sf, const double tolerance = SCALED_EPSILON)
{
    // [INTENT] `execute_with_policy(ex_tbb, sf, sf.isoval, {sf.gsize, sf.gsize})` runs
    // MarchingSquares in parallel using TBB. The grid step `{sf.gsize, sf.gsize}` sets
    // the marching cell size (~100 pixels = 0.40mm). Returns a vector of Ring objects,
    // each ring being a closed iso-contour polygon in raster Coord space.
    std::vector<Ring> rings = execute_with_policy(ex_tbb, sf, sf.isoval, {sf.gsize, sf.gsize});

    Polylines polys;
    polys.reserve(rings.size());
    // size_t old_pts = 0, new_pts = 0;

    for (const Ring& ring : rings) {
        Polyline poly;
        Points&  pts = poly.points;
        pts.reserve(ring.size() + 1);
        // [INTENT] Convert raster Coord points to scaled-coord Points (world space).
        for (const Coord& crd : ring)
            pts.emplace_back(sf.to_Point(crd));
        // MarchingSquare's rings are polygons, so add the first point to the end to make it a PolyLine.
        // [HAZARD H387] Empty ring → pts is empty → pts.front() is UB. Safe if MarchingSquares
        //   guarantees non-empty rings (it does in practice), but not asserted.
        pts.push_back(pts.front());
        // old_pts += poly.points.size();
        //  Simplify within specified tolerance to reduce points.
        if (tolerance >= 0.0)
            poly.simplify(tolerance);
        // new_pts += poly.points.size();
        polys.emplace_back(poly);
    }
    // std::cerr << "MarchingSquares: poly.simplify(" << tolerance << ") reduced points from" <<
    //     old_pts << " to " << new_pts << " (" << 100*new_pts/old_pts << "%)\n";
    return polys;
}

} // namespace marchsq

namespace Slic3r {

using namespace std;

// [INTENT] `_fill_surface_single` — main entry point called per ExPolygon region.
// Orchestrates: angle rotation → FK field setup → MarchingSquares → multiline expansion
// → clipping → small-segment pruning → connection.
//
// [STATE] Reads: this->angle, this->z, this->spacing (inherited from Fill).
//         Writes: appends to `polylines_out`.
// [COUPLING] multiline_fill (FillBase), intersection_pl (ClipperUtils), connect_infill (FillBase).
// [CONCURRENCY] No shared mutable state. MarchingSquares internally parallelised.
void FillTpmsFK::_fill_surface_single(const FillParams&              params,
                                      unsigned int                   thickness_layers,
                                      const std::pair<float, Point>& direction,
                                      ExPolygon                      expolygon,
                                      Polylines&                     polylines_out)
{
    // [INTENT] CorrectionAngle (-45°) aligns the FK pattern diagonal with the printer fast axis.
    // Negative rotation applied to expolygon; undone at output stage.
    auto infill_angle = float(this->angle + (CorrectionAngle * 2 * M_PI) / 360.);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    // [INTENT] density_factor clamps density to [0..0.9] to prevent field degeneracy
    // near density=1.0. The FK field iso-contour disappears or self-intersects near 100%.
    // [HAZARD H382] Any density request above 90% is silently reduced to 90%.
    float density_factor = std::min(0.9f, params.density);
    // Density (field period) adjusted to have a good %of weight.
    // [INTENT] vari_T — the field spatial period in mm.
    //   Higher density → smaller density_factor denominator → smaller vari_T → higher freq
    //   → smaller cells → denser infill.
    // [HAZARD H381] The 4.18 empirical constant has no documented derivation.
    //   It is calibrated to match observed print density to the requested params.density value.
    //   A port must preserve this constant to produce matching density.
    // [HAZARD H389] If `params.multiline == 0` (density_factor × spacing → 0), vari_T → 0
    //   → freq → ∞ → field samples become NaN/garbage. The FillParams invariant should
    //   guarantee multiline ≥ 1, but no assert exists at this call site.
    const float vari_T = 4.18f * spacing * params.multiline / density_factor;

    BoundingBox bbox = expolygon.contour.bounding_box();
    // [INTENT] Enlarge the bounding box before field generation.
    // The `(params.multiline + 1) × spacing` expansion ensures iso-contours near the boundary
    // are fully resolved before being clipped. Without this, MarchingSquares cells at the
    // edge of the exact bbox may be truncated, leaving partial-period features missing.
    // [HAZARD H390] For large multiline values and coarse spacing, the bbox expansion can
    //   be many mm, causing MarchingSquares to evaluate the field over a significantly
    //   larger area. This grows computation O(area) ∝ (multiline × spacing)².
    // Enlarge the bounding box by the multi-line width to avoid artifacts at the edges.
    bbox.offset(scale_((params.multiline + 1) * spacing));
    marchsq::ScalarField sf = marchsq::ScalarField(bbox, this->z, vari_T);
    // [INTENT] `SCALED_SPARSE_INFILL_RESOLUTION` (~0.1mm) is the Douglas-Peucker simplification
    // tolerance used for the FK iso-contour polylines. Coarser than SCALED_EPSILON but
    // appropriate for infill (reduces point count without visual quality loss).
    // Get simplified lines using coarse tolerance of 0.1mm (this is infill).
    Polylines polylines = marchsq::get_polylines(sf, SCALED_SPARSE_INFILL_RESOLUTION);

    // [INTENT] multiline_fill offsets each polyline by spacing × multiline index for multi-pass fills.
    // Apply multiline offset if needed
    multiline_fill(polylines, params, spacing);

    // [INTENT] Clip the MarchingSquares output to the actual expolygon (the field was
    // sampled over the larger expanded bbox). All iso-contour segments outside the polygon
    // are discarded.
    // Prune the lines within the expolygon.
    polylines = intersection_pl(std::move(polylines), expolygon);

    if (!polylines.empty()) {
        // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        // [INTENT] 0.8 × spacing threshold removes arc fragments shorter than 80% of one line width.
        const double minlength = scale_(0.8 * this->spacing);
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

    if (!polylines.empty()) {
        // connect lines
        size_t polylines_out_first_idx = polylines_out.size();

        // chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);
        // [INTENT] `chain_or_connect_infill` is explicitly NOT used here.
        // The FK pattern generates internal "islands" (closed loops inside the main contour)
        // that chain_infill cannot order correctly without crossing other loops.
        // `connect_infill` handles these islands by treating each polyline independently.
        // chain_infill not situable for this pattern due to internal "islands", this also affect performance a lot.
        connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        // new paths must be rotated back
        // [INTENT] Undo the initial expolygon rotation. Only new polylines (from
        // polylines_out_first_idx) are rotated; previously accumulated output is unchanged.
        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r
