// [INTENT] Fill3DHoneycomb.cpp — implementation of 3D Honeycomb infill.
// Generates cross-sections of a space-filling truncated regular octahedron (bitruncated
// cubic honeycomb) at each layer height Z. The octahedrons have square faces horizontal.
//
// Algorithm overview:
//   triWave()         — triangular wave: period=2×gridSize, amplitude=gridSize/2.
//   troctWave()       — truncated-octahedron wave: clips triWave perpendicular offset by Z-phase.
//   getCriticalPoints()— finds the 4 inflection x-positions per period.
//   colinearPoints()  — generates along-axis coordinates (rows or columns).
//   perpendPoints()   — generates perpendicular-axis offsets using troctWave.
//   zip()             — combines x[] and y[] arrays into Vec2d points.
//   makeActualGrid()  — assembles one layer's full set of polylines.
//   makeGrid()        — wraps makeActualGrid() into Slic3r Polylines.
//   _fill_surface_single() — main entry: rotate, compute grid size, generate, clip, connect.
//
// Credits: Based on original 3D Honeycomb implementation by David Eccles (gringer).
//
// [HAZARD H332] `triWave()` uses `float t` for intermediate computation while input is
// `coordf_t` (double). The downcast to float loses ~7 decimal digits of precision.
// For large coordinate values (e.g., gridSize > 10^6 scaled units), the floating-point
// quantisation error in `t - (int)t` can be significant.
//
// [HAZARD H333] `gridSize` calculation uses an empirical path-length correction factor
// `(sqrt(2) + 1) / 2 ≈ 1.207`. The code comment notes "I think" — the formula is
// approximate. For densities > 42%, a different formula (`1.1 × spacing`) is used with
// the comment "scale of 1.1 guessed based on modeling". Both correction factors are
// empirical and layer-height-dependent; a port must preserve them exactly.
//
// [HAZARD H334] Orca hardcodes `layerHeight = scale_(1.0)` instead of using the actual
// `thickness_layers` parameter. This avoids variable-layer-height artifacts but means
// the pattern is incorrect for layer heights other than 1.0 mm. The original code
// (commented out above) used `scale_(thickness_layers)`.
//
// [CONCURRENCY] All functions are pure/static with no shared mutable state. Thread-safe.
// [COUPLING] Uses scale_(), EPSILON from libslic3r.h; ClipperUtils::intersection_pl(),
//            chain_or_connect_infill() from ShortestPath/FillBase.

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"
#include "FillBase.hpp"
#include "Fill3DHoneycomb.hpp"

namespace Slic3r {

// [INTENT] Generic sign function: returns -1, 0, or +1 for negative, zero, positive T.
// [STATE] Pure template function; no side effects.
// sign function
template<typename T> int sgn(T val) { return (T(0) < val) - (val < T(0)); }

/*
Creates a contiguous sequence of points at a specified height that make
up a horizontal slice of the edges of a space filling truncated
octahedron tesselation. The octahedrons are oriented so that the
square faces are in the horizontal plane with edges parallel to the X
and Y axes.

Credits: David Eccles (gringer).
*/

// [INTENT] Triangular wave function with period (gridSize×2) and amplitude (gridSize/2).
// Maps position `pos` to a y-offset in the range [0, gridSize/2], peaking at gridSize/4.
// Used as the base waveform for both the along-axis coordinate series and the Z-phase offset.
//
// [HAZARD H332] `float t = (pos / (gridSize * 2.)) + 0.25` truncates coordf_t (double) to
// float. For large scaled values (pos ~ 10^8 scaled units = 100 mm), float precision is
// ~4 units (~4 nm). Minimal user impact, but the downcast is unguarded and silently lossy.
// triangular wave function
// this has period (gridSize * 2), and amplitude (gridSize / 2),
// with triWave(pos = 0) = 0
static coordf_t triWave(coordf_t pos, coordf_t gridSize)
{
    float t = (pos / (gridSize * 2.)) + 0.25; // convert relative to grid size
    t       = t - (int) t;                    // extract fractional part
    return ((1. - abs(t * 8. - 4.)) * (gridSize / 4.) + (gridSize / 4.));
}

// [INTENT] Truncated octahedron waveform. At a given Z position (Zpos), the horizontal
// cross-section of the octahedron edge transitions between a purely diagonal "slanted"
// path and a capped "flat top" path. The flat-top region width is determined by the
// Z-phase offset `perpOffset = triWave(Zpos, gridSize) / 2`.
//
// When |y| > |perpOffset|: the waveform is clamped to the Z-offset level (flat section).
// When |y| ≤ |perpOffset|: the waveform follows the base triangular wave (slanted section).
// This creates the characteristic truncated-octahedron cross-section shape.
// truncated octagonal waveform, with period and offset
// as per the triangular wave function. The Z position adjusts
// the maximum offset [between -(gridSize / 4) and (gridSize / 4)], with a
// period of (gridSize * 2) and troctWave(Zpos = 0) = 0
static coordf_t troctWave(coordf_t pos, coordf_t gridSize, coordf_t Zpos)
{
    coordf_t Zcycle     = triWave(Zpos, gridSize);
    coordf_t perpOffset = Zcycle / 2;
    coordf_t y          = triWave(pos, gridSize);
    return ((abs(y) > abs(perpOffset)) ? (sgn(y) * perpOffset) : (y * sgn(perpOffset)));
}

// [INTENT] Computes the 4 x-positions (as fractions of period 2π) where the truncated
// octahedron wave transitions between slanted and flat sections. Returns a sorted vector
// starting at 0.0 (start of period). If normalisedOffset==0 (z at a phase node), returns
// only {0.0} — a straight line with no transitions.
//
// The 4 critical points are: [normOffset, 1-normOffset, 1+normOffset, 2-normOffset]
// scaled by gridSize. These correspond to the 4 corners of the truncated flat section.
// Identify the important points of curve change within a truncated
// octahedron wave (as waveform fraction t):
// 1. Start of wave (always 0.0)
// 2. Transition to upper "horizontal" part
// 3. Transition from upper "horizontal" part
// 4. Transition to lower "horizontal" part
// 5. Transition from lower "horizontal" part
/*    o---o
 *   /     \
 * o/       \
 *           \       /
 *            \     /
 *             o---o
 */
static std::vector<coordf_t> getCriticalPoints(coordf_t Zpos, coordf_t gridSize)
{
    std::vector<coordf_t> res        = {0.};
    coordf_t              perpOffset = abs(triWave(Zpos, gridSize) / 2.);

    coordf_t normalisedOffset = perpOffset / gridSize;
    // note: 0 == straight line
    if (normalisedOffset > 0) {
        res.push_back(gridSize * (0. + normalisedOffset));
        res.push_back(gridSize * (1. - normalisedOffset));
        res.push_back(gridSize * (1. + normalisedOffset));
        res.push_back(gridSize * (2. - normalisedOffset));
    }
    return (res);
}

// [INTENT] Generates the list of coordinates along the "colinear" axis (the axis along
// which the polyline travels). For a vertical polyline (column), these are the Y coords;
// for a horizontal polyline (row), these are the X coords.
// The critical points from getCriticalPoints() are tiled across [0, gridLength] in steps
// of 2×gridSize to produce the full-range coordinate series.
//
// [HAZARD H335] `baseLocation` is added twice in the inner loop:
//   `points.push_back(baseLocation + cLoc + critPoints[pi])`
// where `cLoc` already starts at `baseLocation`. This produces an offset double-count
// for cLoc == baseLocation. The value baseLocation is always 0 in all call sites, so
// the bug has no observable effect — but a port that passes non-zero baseLocation
// will produce an incorrect offset.
// Generate an array of points that are in the same direction as the
// basic printing line (i.e. Y points for columns, X points for rows)
// Note: a negative offset only causes a change in the perpendicular
// direction
static std::vector<coordf_t> colinearPoints(
    const coordf_t Zpos, coordf_t gridSize, std::vector<coordf_t> critPoints, const size_t baseLocation, size_t gridLength)
{
    std::vector<coordf_t> points;
    points.push_back(baseLocation);
    for (coordf_t cLoc = baseLocation; cLoc < gridLength; cLoc += (gridSize * 2)) {
        for (size_t pi = 0; pi < critPoints.size(); pi++) {
            points.push_back(baseLocation + cLoc + critPoints[pi]);
        }
    }
    points.push_back(gridLength);
    return points;
}

// [INTENT] Generates the list of coordinates along the "perpendicular" axis — the offsets
// transverse to the main travel direction. For a vertical polyline, these are the X coords;
// for a horizontal polyline, these are the Y coords.
// At each critical point, the perpendicular offset is `troctWave(critPoint, gridSize, Zpos)`.
// `perpDir` (±1) reverses the wave direction for alternating polylines (snaking pattern).
// `offsetBase` is the base position of this polyline on the perpendicular axis.
// Generate an array of points for the dimension that is perpendicular to
// the basic printing line (i.e. X points for columns, Y points for rows)
static std::vector<coordf_t> perpendPoints(const coordf_t        Zpos,
                                           coordf_t              gridSize,
                                           std::vector<coordf_t> critPoints,
                                           size_t                baseLocation,
                                           size_t                gridLength,
                                           size_t                offsetBase,
                                           coordf_t              perpDir)
{
    std::vector<coordf_t> points;
    points.push_back(offsetBase);
    for (coordf_t cLoc = baseLocation; cLoc < gridLength; cLoc += gridSize * 2) {
        for (size_t pi = 0; pi < critPoints.size(); pi++) {
            coordf_t offset = troctWave(critPoints[pi], gridSize, Zpos);
            points.push_back(offsetBase + (offset * perpDir));
        }
    }
    points.push_back(offsetBase);
    return points;
}

// [INTENT] Zips two equal-length coordinate arrays into a vector of 2D points (Vec2d).
// [HAZARD H336] `assert(x.size() == y.size())` is debug-only. In release, mismatched sizes
// cause `out[i]` to be initialized with out-of-bounds values from whichever array is
// shorter. Both arrays are always generated by the same loop in practice (safe), but
// a future refactor that independently sizes the arrays would produce silent UB.
static inline Pointfs zip(const std::vector<coordf_t>& x, const std::vector<coordf_t>& y)
{
    assert(x.size() == y.size());
    Pointfs out;
    out.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        out.push_back(Vec2d(x[i], y[i]));
    return out;
}

// [INTENT] Generates the full set of wave polylines for a given Z layer position.
// Determines orientation (vertical vs. horizontal) based on the Z-cycle phase:
//   zCycle = fmod(Zpos + gridSize/2, 2×gridSize) / (2×gridSize) ∈ [0, 1)
//   zCycle < 0.5 → vertical polylines (columns), perpDir alternates left/right.
//   zCycle ≥ 0.5 → horizontal polylines (rows), perpDir alternates up/down.
// Each polyline is reversed if perpDir=+1 (vertical) or perpDir=-1 (horizontal) to
// maintain a consistent snake-pattern travel direction across alternating polylines.
//
// [HAZARD H337] The `perpDir` variable is declared as `int` but alternates between
// ±1 by `perpDir *= -1`. On each loop iteration, the integer multiplication is correct,
// but `perpDir` is passed as `coordf_t` to `perpendPoints()`. The implicit conversion
// is correct, but relying on implicit int→double conversion for a direction parameter
// could be confusing in a port with strict typing.
// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron.
static std::vector<Pointfs> makeActualGrid(coordf_t Zpos, coordf_t gridSize, size_t boundsX, size_t boundsY)
{
    std::vector<Pointfs>  points;
    std::vector<coordf_t> critPoints = getCriticalPoints(Zpos, gridSize);
    // [INTENT] Determine phase: zCycle < 0.5 → print vertical stripes; else horizontal.
    coordf_t zCycle    = fmod(Zpos + gridSize / 2, gridSize * 2.) / (gridSize * 2.);
    bool     printVert = zCycle < 0.5;
    if (printVert) {
        int perpDir = -1;
        for (coordf_t x = 0; x <= (boundsX); x += gridSize, perpDir *= -1) {
            points.push_back(Pointfs());
            Pointfs& newPoints = points.back();
            newPoints          = zip(perpendPoints(Zpos, gridSize, critPoints, 0, boundsY, x, perpDir),
                                     colinearPoints(Zpos, gridSize, critPoints, 0, boundsY));
            if (perpDir == 1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    } else {
        int perpDir = 1;
        for (coordf_t y = gridSize; y <= (boundsY); y += gridSize, perpDir *= -1) {
            points.push_back(Pointfs());
            Pointfs& newPoints = points.back();
            newPoints          = zip(colinearPoints(Zpos, gridSize, critPoints, 0, boundsX),
                                     perpendPoints(Zpos, gridSize, critPoints, 0, boundsX, y, perpDir));
            if (perpDir == -1)
                std::reverse(newPoints.begin(), newPoints.end());
        }
    }
    return points;
}

// [INTENT] Converts the raw `Pointfs` (Vec2d in mm-scale) output of `makeActualGrid()`
// into Slic3r `Polylines` (coord_t integer-scaled points). No simplification is applied
// here; simplification happens in `_fill_surface_single()` after translation.
// Generate a set of curves (array of array of 2d points) that describe a
// horizontal slice of a truncated regular octahedron with a specified
// grid square size.
// gridWidth and gridHeight define the width and height of the bounding box respectively
static Polylines makeGrid(coordf_t z, coordf_t gridSize, coordf_t boundWidth, coordf_t boundHeight, bool fillEvenly)
{
    std::vector<Pointfs> polylines = makeActualGrid(z, gridSize, boundWidth, boundHeight);
    Polylines            result;
    result.reserve(polylines.size());
    for (std::vector<Pointfs>::const_iterator it_polylines = polylines.begin(); it_polylines != polylines.end(); ++it_polylines) {
        result.push_back(Polyline());
        Polyline& polyline = result.back();
        for (Pointfs::const_iterator it = it_polylines->begin(); it != it_polylines->end(); ++it)
            polyline.points.push_back(Point(coord_t((*it)(0)), coord_t((*it)(1))));
    }
    return result;
}

// [INTENT] Main Fill3DHoneycomb entry point. Orchestrates:
//   1. Rotate ExPolygon by -angle (canonical axis alignment).
//   2. Compute gridSize from spacing, density, and zScale correction factor.
//   3. Handle >42% density special case with different correction formula.
//   4. Align bounding box to 4×gridSize grid module for seamless tiling.
//   5. makeGrid(): generate truncated-octahedron cross-sections.
//   6. Translate polylines to bounding box origin; simplify to 5× line width.
//   7. multiline_fill(): Orca multi-extrusion extension.
//   8. intersection_pl(): clip to ExPolygon.
//   9. Remove short fragments (< 0.8 × spacing).
//   10. chain_or_connect_infill(): G-code travel ordering.
//   11. Rotate all new polylines back by +angle.
//
// FillParams used: density, dont_adjust, multiline.
// FillParams NOT used: anchor_length, anchor_length_max (anchor logic is in chain_or_connect_infill).
//
// [STATE] Reads: this->angle, this->z, this->spacing.
//         Writes: polylines_out (appended, never cleared).
//
// [HAZARD H333] (repeat) gridSize formula uses empirical correction factors that are
// approximate. For densities > 42% a different formula is used (also approximate).
// [HAZARD H334] (repeat) Orca hardcodes layerHeight=scale_(1.0) instead of actual
// layer height, making 3D Honeycomb incorrect for non-1.0mm layer heights.
// [HAZARD H338] `polyline.simplify(5 * spacing)` with unscaled `spacing` applied to
// scaled polylines. `pl.simplify()` expects a tolerance in scaled units, but `spacing`
// is in mm (unscaled). At spacing=0.4mm, `5 * 0.4 = 2.0` — the tolerance is 2.0 scaled
// units (~0.002 μm), effectively a no-op. The intent is "5× line width tolerance" but
// the actual call does almost nothing due to the unscaled input.

// FillParams has the following useful information:
// density <0 .. 1>  [proportion of space to fill]
// anchor_length     [???]
// anchor_length_max [???]
// dont_connect()    [avoid connect lines]
// dont_adjust       [avoid filling space evenly]
// monotonic         [fill strictly left to right]
// complete          [complete each loop]

void Fill3DHoneycomb::_fill_surface_single(const FillParams&              params,
                                           unsigned int                   thickness_layers,
                                           const std::pair<float, Point>& direction,
                                           ExPolygon                      expolygon,
                                           Polylines&                     polylines_out)
{
    // [INTENT] Rotate the ExPolygon to canonical axis. No explicit CorrectionAngle —
    // the 3D Honeycomb uses the raw user angle directly.
    // no rotation is supported for this infill pattern
    // Support infill angle
    auto infill_angle = float(this->angle);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);
    BoundingBox bb = expolygon.contour.bounding_box();

    // [INTENT] Expand bounding box by 5× spacing to prevent edge aliasing.
    // Expand the bounding box to avoid artifacts at the edges
    coord_t expand = 5 * (scale_(this->spacing));
    bb.offset(expand);

    // [INTENT] Z scale factor: sqrt(2) stretches the vertical axis so the octahedron
    // faces are equilateral in 3D. Without this, the octahedron would be vertically
    // compressed for typical layer heights much smaller than XY spacing.
    // Note: with equally-scaled X/Y/Z, the pattern will create a vertically-stretched
    // truncated octahedron; so Z is pre-adjusted first by scaling by sqrt(2)
    coordf_t zScale = sqrt(2);

    // [INTENT] Compute initial gridSize using empirical path-length correction.
    // The (zScale+1)/2 factor accounts for the extra travel distance in the slanted
    // segments vs. a straight horizontal line.
    // [HAZARD H333] "I think" comment acknowledges this is approximate.
    // adjustment to account for the additional distance of octagram curves
    // note: this only strictly applies for a rectangular area where the total
    //       Z travel distance is a multiple of the spacing... but it should
    //       be at least better than the prevous estimate which assumed straight
    //       lines
    // = 4 * integrate(func=4*x(sqrt(2) - 1) + 1, from=0, to=0.25)
    // = (sqrt(2) + 1) / 2 [... I think]
    // make a first guess at the preferred grid Size
    coordf_t gridSize = (scale_(this->spacing) * ((zScale + 1.) / 2.) * params.multiline / params.density);

    // [HAZARD H334] Orca uses fixed 1.0mm layer height instead of actual thickness_layers
    // to avoid inconsistent bridge/variable-layer-height artifacts in the 3D pattern.
    // This means the 3D Honeycomb pattern is only geometrically correct for 1.0mm layers.
    // Orca: uses a fixed layer height to avoid inconsistent bridges and variable layer height artifacts.
    // coordf_t layerHeight = scale_(thickness_layers);
    coordf_t layerHeight = scale_(1.0);
    // [INTENT] Compute number of layers per Z-module (gridSize*2 / (zScale*layerHeight)).
    // "Nudge" (+0.05) prevents floating-point boundary cases from rounding down.
    // ceiling to an integer value of layers per Z
    // (with a little nudge in case it's close to perfect)
    coordf_t layersPerModule = floor((gridSize * 2) / (zScale * layerHeight) + 0.05);
    if (params.density > 0.42) { // exact layer pattern for >42% density
        layersPerModule = 2;
        // [HAZARD H333] "scale of 1.1 guessed based on modeling" — empirical, not derived.
        // re-adjust the grid size for a partial octahedral path
        // (scale of 1.1 guessed based on modeling)
        gridSize = (scale_(this->spacing) * 1.1 * params.multiline / params.density);
        // re-adjust zScale to make layering consistent
        zScale = (gridSize * 2) / (layersPerModule * layerHeight);
    } else {
        if (layersPerModule < 2) {
            layersPerModule = 2;
        }
        // re-adjust zScale to make layering consistent
        zScale = (gridSize * 2) / (layersPerModule * layerHeight);
        // re-adjust the grid size to account for the new zScale
        gridSize = (scale_(this->spacing) * ((zScale + 1.) / 2.) * params.multiline / params.density);
        // re-calculate layersPerModule and zScale
        layersPerModule = floor((gridSize * 2) / (zScale * layerHeight) + 0.05);
        if (layersPerModule < 2) {
            layersPerModule = 2;
        }
        zScale = (gridSize * 2) / (layersPerModule * layerHeight);
    }

    // [INTENT] Align bounding box min to a 4×gridSize grid — 4× because one full
    // horizontal period is 4×gridSize (2 half-periods of gridSize×2 each).
    // align bounding box to a multiple of our honeycomb grid module
    // (a module is 2*$gridSize since one $gridSize half-module is
    // growing while the other $gridSize half-module is shrinking)
    bb.merge(align_to_grid(bb.min, Point(gridSize * 4, gridSize * 4)));

    // [INTENT] Generate truncated-octahedron cross-sections at scaled Z.
    // zScale adjusts the Z coordinate so the 3D pattern stays layer-consistent.
    // generate pattern
    Polylines polylines = makeGrid(scale_(this->z) * zScale, gridSize, bb.size()(0), bb.size()(1), !params.dont_adjust);

    // [INTENT] Translate waves from origin to actual bounding-box position.
    // Simplify to 5× line width — [HAZARD H338] tolerance is likely wrong (unscaled).
    // move pattern in place
    for (Polyline& pl : polylines) {
        pl.translate(bb.min);
        pl.simplify(5 * spacing); // simplify to 5x line width
    }

    // Apply multiline offset if needed
    multiline_fill(polylines, params, spacing);

    // [INTENT] Clip waves to the ExPolygon boundary.
    // clip pattern to boundaries, chain the clipped polylines
    polylines = intersection_pl(std::move(polylines), to_polygons(expolygon));

    if (!polylines.empty()) {
        // [INTENT] Remove short fragments to avoid blob defects.
        // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

    // [INTENT] Connect and order polylines for G-code travel efficiency, then rotate back.
    // copy from fliplines
    if (!polylines.empty()) {
        int infill_start_idx = polylines_out.size(); // only rotate what belongs to us.
        // connect lines
        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

        // rotate back
        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + infill_start_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r
