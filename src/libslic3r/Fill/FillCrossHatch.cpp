// [INTENT] Implements FillCrossHatch: a Bambu Lab original multi-layer alternating-direction
// infill pattern for FDM 3D printing.
//
// Pattern structure:
//   - "Repeat layers": straight horizontal OR vertical lines (like FillLine but axis-locked).
//   - "Transform layers": zig-zag polylines that smoothly transition line direction by 90°.
//   - Direction (H vs V) and phase are computed purely from Fill::z using modular arithmetic.
//   - No cross-layer state is stored: each call is self-contained.
//
// Layer phase computation:
//   period   = trans_layer_size + repeat_layer_size
//   z_height = Fill::z (offset for initial-layer strength)
//   trans_z  = z position within the transform sub-period
//   If trans_z < 0  → repeat layer (straight lines)
//   Else            → transform layer (zig-zag transition)
//
// Key functions:
//   generate_one_cycle()         — 4 points forming one zig-zag unit cell.
//   generate_transform_pattern() — tiles one_cycle across the bounding box for a transition layer.
//   generate_repeat_pattern()    — straight horizontal/vertical lines for a repeat layer.
//   generate_infill_layers()     — selects which type to generate based on z, computes direction.
//
// [COUPLING] FillBase (Fill, align_to_grid, chain_or_connect_infill, multiline_fill),
//            ClipperUtils (intersection_pl, to_polygons).
// [STATE] Reads Fill::z, Fill::angle, Fill::spacing. No member state is written.
// [CONCURRENCY] No shared mutable state. Thread-safe per call.

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"
#include <cmath>
#include "FillBase.hpp"
#include "FillCrossHatch.hpp"

namespace Slic3r {

// CrossHatch Infill: Enhances 3D Printing Speed & Reduces Noise
// CrossHatch, as its name hints, alternates line direction by 90 degrees every few layers to improve adhesion.
// It introduces transform layers between direction shifts for better line cohesion, which fixes the weakness of line infill.
// The transform technique is inspired by David Eccles, improved 3D honeycomb but we made a more flexible implementation.
// This method notably increases printing speed, meeting the demands of modern high-speed 3D printers, and reduces noise for most layers.
// By Bambu Lab

// graph credits: David Eccles (gringer).
// But we made a different definition for points.
/*    o---o
 *   /     \
 *  /       \
 *           \       /
 *            \     /
 *             o---o
 *   p1   p2  p3   p4
 */

// [INTENT] generate_one_cycle: produce one zig-zag cell (4 points) for a transition layer.
// The cell spans one `period` in X and the y-displacement is proportional to `progress`.
//
// At progress=0: all 4 points are on y=0 (straight horizontal line, no transition).
// At progress=1: the zig-zag reaches maximum amplitude = 1/8 × period.
//
// Points layout (in local coords):
//   p1 = (0.25×period - offset,  offset)   ← left peak (above axis)
//   p2 = (0.25×period + offset,  offset)   ← left peak end
//   p3 = (0.75×period - offset, -offset)   ← right valley (below axis)
//   p4 = (0.75×period + offset, -offset)   ← right valley end
//
// [HAZARD H365] The offset = progress × period/8. At progress=1, offset = period/8.
// The points are NOT evenly distributed in X — p1..p2 are clustered at 0.25×period
// and p3..p4 at 0.75×period. When replicated across the bounding box, this produces
// non-uniform segment lengths. Longer diagonal segments print at lower effective density.
static Pointfs generate_one_cycle(double progress, coordf_t period)
{
    Pointfs out;
    double  offset = progress * 1. / 8. * period;
    out.reserve(4);
    out.push_back(Vec2d(0.25 * period - offset, offset));
    out.push_back(Vec2d(0.25 * period + offset, offset));
    out.push_back(Vec2d(0.75 * period - offset, -offset));
    out.push_back(Vec2d(0.75 * period + offset, -offset));
    return out;
}

// [INTENT] generate_transform_pattern: tile one_cycle across the bounding box to produce
// a full transition-layer infill. Generates two interleaved rows (odd + even) offset by
// half a period, so all of the bounding box width is covered.
//
// Parameters:
//   inprogress  — 0.0..1.0: transition completeness (0 = straight lines, 1 = full zig-zag).
//   direction   — positive = horizontal lines; negative = vertical (X/Y are swapped).
//   ingrid_size — base grid unit (= line_spacing). grid_size = ingrid_size × 2.
//   inwidth     — bounding box width.
//   inheight    — bounding box height.
//
// [HAZARD H366] `grid_size = ingrid_size * 2` doubles the grid unit internally.
// num_of_cycle = width / grid_size + 2. The +2 ensures coverage at both edges.
// However, for the odd/even interleave, even rows use `odd_poly` (the same pre-built row)
// but `i` is used as the Y-row index for the even_polylines loop, while `num_of_lines`
// was computed for odd rows. This causes the even rows to have the same count as odd rows,
// which may leave a gap at the top boundary for odd total row counts.
static Polylines generate_transform_pattern(double inprogress, int direction, coordf_t ingrid_size, coordf_t inwidth, coordf_t inheight)
{
    coordf_t width  = inwidth;
    coordf_t height = inheight;
    // [INTENT] Work in a doubled grid (handles odd/even rows separately at the base cycle level).
    coordf_t  grid_size = ingrid_size * 2; // we due with odd and even saparately.
    double    progress  = inprogress;
    Polylines out_polylines;

    // generate template patterns;
    Pointfs one_cycle_points = generate_one_cycle(progress, grid_size);

    Polyline one_cycle;
    one_cycle.points.reserve(one_cycle_points.size());
    // [INTENT] Convert Vec2d float points to coord_t integer scaled points.
    for (size_t i = 0; i < one_cycle_points.size(); i++)
        one_cycle.points.push_back(Point(one_cycle_points[i]));

    // swap if vertical
    // [INTENT] For vertical lines (direction < 0), generate in "horizontal" coords first,
    // then swap X and Y at the end. This reuses the same generation logic for both axes.
    if (direction < 0) {
        width  = height;
        height = inwidth;
    }

    // replicate polylines;
    Polylines odd_polylines;
    Polyline  odd_poly;
    int       num_of_cycle = width / grid_size + 2;
    odd_poly.points.reserve(num_of_cycle * one_cycle.size());

    // replicate to odd line
    // [INTENT] Build one full-width row by tiling `one_cycle` horizontally.
    Point translate = Point(0, 0);
    for (size_t i = 0; i < num_of_cycle; i++) {
        Polyline odd_points;
        odd_points = Polyline(one_cycle);
        odd_points.translate(Point(i * grid_size, 0.0));
        odd_poly.points.insert(odd_poly.points.end(), odd_points.begin(), odd_points.end());
    }

    // fill the height
    // [INTENT] Tile the row vertically to cover the full bounding box height.
    int num_of_lines = height / grid_size + 2;
    odd_polylines.reserve(num_of_lines * odd_poly.size());
    for (size_t i = 0; i < num_of_lines; i++) {
        Polyline poly = odd_poly;
        poly.translate(Point(0.0, grid_size * i));
        odd_polylines.push_back(poly);
    }
    // save to output
    out_polylines.insert(out_polylines.end(), odd_polylines.begin(), odd_polylines.end());

    // replicate to even lines
    // [INTENT] Even rows are offset by (-0.5×grid_size, (i+0.5)×grid_size) to interleave
    // between odd rows, creating the interlocking zig-zag pattern.
    //
    // [HAZARD H366 continued] The even_polylines loop iterates `i` from 0..odd_polylines.size()-1
    // but uses `odd_poly` (constant, pre-built row). The Y-offsets for even rows thus exactly
    // match the spacing between odd rows. If height is not an even multiple of grid_size,
    // the last even row may be above the bounding box — clipped by intersection_pl later.
    Polylines even_polylines;
    even_polylines.reserve(odd_polylines.size());
    for (size_t i = 0; i < odd_polylines.size(); i++) {
        Polyline even = odd_poly;
        even.translate(Point(-0.5 * grid_size, (i + 0.5) * grid_size));
        even_polylines.push_back(even);
    }

    // save for output
    out_polylines.insert(out_polylines.end(), even_polylines.begin(), even_polylines.end());

    // change to vertical if need
    // [INTENT] For vertical direction: swap X and Y on all points to rotate the entire
    // pattern 90°. A port must preserve this X↔Y swap for vertical phases.
    if (direction < 0) {
        // swap xy, see if we need better performance method
        for (Polyline& poly : out_polylines) {
            for (Point& p : poly) {
                std::swap(p.x(), p.y());
            }
        }
    }

    return out_polylines;
}

// [INTENT] generate_repeat_pattern: generate straight horizontal (or vertical) lines
// covering the bounding box. Each line spans the full width at a Y step of grid_size.
//
// [HAZARD H367] num_of_lines = height / grid_size + 1. For height that is exactly an
// integer multiple of grid_size, the last line lands exactly on height — one line beyond
// the bounding box edge. For height < grid_size (very thin fill regions), num_of_lines=1,
// producing a single line that may be outside the expolygon and clipped away entirely.
static Polylines generate_repeat_pattern(int direction, coordf_t grid_size, coordf_t inwidth, coordf_t inheight)
{
    coordf_t  width  = inwidth;
    coordf_t  height = inheight;
    Polylines out_polylines;

    // swap if vertical
    if (direction < 0) {
        width  = height;
        height = inwidth;
    }

    int num_of_lines = height / grid_size + 1;
    out_polylines.reserve(num_of_lines);

    // [INTENT] Each line is a simple 2-point horizontal segment.
    for (int i = 0; i < num_of_lines; i++) {
        Polyline poly;
        poly.points.reserve(2);
        poly.append(Point(coordf_t(0), coordf_t(grid_size * i)));
        poly.append(Point(width, coordf_t(grid_size * i)));
        out_polylines.push_back(poly);
    }

    // change to vertical if needed
    if (direction < 0) {
        // swap xy
        for (Polyline& poly : out_polylines) {
            for (Point& p : poly) {
                std::swap(p.x(), p.y());
            }
        }
    }

    return out_polylines;
}

// it makes the real patterns that overlap the bounding box
// repeat_ratio define the ratio between the height of repeat pattern and grid
// [INTENT] generate_infill_layers: the top-level layer-type selector.
// Computes phase and direction from z_height, then dispatches to either
// generate_repeat_pattern or generate_transform_pattern.
//
// Layer periods:
//   trans_layer_size  = grid_size × 0.4   (40% of grid is transition)
//   repeat_layer_size = grid_size × repeat_ratio   (rest is straight lines)
//   period = trans_layer_size + repeat_layer_size
//   Full cycle (both directions) = 2 × period
//
// Within each period:
//   [0, repeat_layer_size) → repeat layer (straight, one direction)
//   [repeat_layer_size, period) → transform layer (transition)
//
// Direction is determined by which half of the 2-period cycle we're in.
//
// [HAZARD H368] `int phase = fmod(z_height, period * 2) - (period - 1)`.
// The `fmod` result is a float. The `- (period - 1)` adjusts the threshold.
// The `+epsilon` comment (" add epsilon") suggests this is off-by-epsilon-prone.
// Near the exact boundary between directions, floating-point rounding can flip
// direction for a single layer, producing a visually inconsistent direction change.
// There is no layer-to-layer hysteresis guard.
//
// [HAZARD H369] z_height is offset by `+ repeat_layer_size/2 + trans_layer_size`
// before the modular arithmetic. This offset ensures the first few layers have
// straight (repeat) patterns for better bed adhesion and warp resistance, per comment.
// A port must preserve this offset exactly or the initial-layer pattern changes.
static Polylines generate_infill_layers(coordf_t z_height, double repeat_ratio, coordf_t grid_size, coordf_t width, coordf_t height)
{
    Polylines result;
    coordf_t  trans_layer_size  = grid_size * 0.4;          // upper.
    coordf_t  repeat_layer_size = grid_size * repeat_ratio; // lower.
    // [INTENT] Offset z so the sequence starts with repeat layers for better first-layer strength.
    z_height += repeat_layer_size / 2 + trans_layer_size; // offset to improve first few layer strength and reduce the risk of warpping.
    coordf_t period = trans_layer_size + repeat_layer_size;
    // [INTENT] `remains` is the z position within the current period (0..period).
    coordf_t remains = z_height - std::floor(z_height / period) * period;
    // [INTENT] trans_z: position within the transition sub-window.
    //          < 0 → we're in the repeat layer phase.
    //          ≥ 0 → we're in the transform layer phase.
    coordf_t trans_z  = remains - repeat_layer_size; // put repeat layer first.
    coordf_t repeat_z = remains;

    // [INTENT] Determine print direction (horizontal +1 vs vertical -1) from the 2-period phase.
    int phase     = fmod(z_height, period * 2) - (period - 1); // add epsilon
    int direction = phase <= 0 ? -1 : 1;

    // this is a repeat layer
    if (trans_z < 0) {
        result = generate_repeat_pattern(direction, grid_size, width, height);
    }
    // this is a transform layer
    else {
        // [INTENT] progress = 0..1, how far through the transition window.
        double progress = fmod(trans_z, trans_layer_size) / trans_layer_size;

        // split the progress to forward and backward, with a opposite direction.
        // [INTENT] First half of transition (progress < 0.5): transition forward in `direction`.
        //          Second half (progress ≥ 0.5): transition backward in `-direction`.
        // The +0.1 and (1.1 - progress) increase overlap at the halfway point for smoother seam.
        //
        // [HAZARD H370] At progress exactly 0.5, `(progress + 0.1) * 2 = 1.2` exceeds 1.0.
        // `generate_transform_pattern` receives inprogress=1.2. offset = 1.2 × period/8.
        // This makes the zig-zag points exceed the one-cycle boundary by 20%.
        // The extra points fall outside the bounding box and are clipped by intersection_pl,
        // but the intent is a brief over-shoot for stronger overlap at the seam.
        if (progress < 0.5)
            result = generate_transform_pattern((progress + 0.1) * 2, direction, grid_size, width, height); // increase overlapping.
        else
            result = generate_transform_pattern((1.1 - progress) * 2, -1 * direction, grid_size, width, height);
    }

    return result;
}

// [INTENT] FillCrossHatch::_fill_surface_single: the main fill driver.
// Steps:
//   1. Rotate expolygon by -infill_angle.
//   2. Compute line_spacing from spacing and density.
//   3. Align bounding box to a 4×line_spacing grid for inter-layer registration.
//   4. Compute repeat_ratio (density-dependent: low density → shorter repeat layers).
//   5. Generate the infill polylines via generate_infill_layers.
//   6. Translate to bounding box origin.
//   7. Apply multiline fill expansion.
//   8. Clip to expolygon.
//   9. Remove short segments (< 0.8 × spacing).
//  10. Chain/connect and rotate back.
void FillCrossHatch ::_fill_surface_single(const FillParams&              params,
                                           unsigned int                   thickness_layers,
                                           const std::pair<float, Point>& direction,
                                           ExPolygon                      expolygon,
                                           Polylines&                     polylines_out)
{
    // rotate angle
    // [INTENT] CrossHatch ignores `direction.first` (the FillBase angle) and uses `this->angle`
    // directly. This diverges from most other infill types that use `direction.first`.
    // [HAZARD H371] CrossHatch uses `this->angle` not `direction.first`. Other infill types
    // rotate using `direction.first` which may incorporate both the base angle and a per-layer
    // variation. CrossHatch skips any per-layer angle variation from Fill::_layer_angle().
    auto infill_angle = float(this->angle);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    // get the rotated bounding box
    BoundingBox bb = expolygon.contour.bounding_box();

    // linespace modifier
    // [INTENT] density_adjusted accounts for multiline: if 3 parallel lines are printed per pass,
    // effective density per pass is density/3.
    double  density_adjusted = params.density / params.multiline;
    coord_t line_spacing     = coord_t(scale_(this->spacing) / density_adjusted);

    // reduce density
    // [INTENT] For non-solid fills, enlarge spacing by 8% to prevent over-dense infill.
    // This compensates for the larger effective area covered per line in low-density mode.
    // [HAZARD H372] The 1.08 factor is a magic constant with no documented derivation.
    // It is unconditionally applied for all densities < 99.9%. A port must preserve this
    // exact value or the infill density will be visually wrong at all sparse densities.
    if (params.density < 0.999)
        line_spacing *= 1.08;

    // [INTENT] Align the bounding box to a 4×line_spacing grid so all layers share the same
    // grid origin — necessary for consistent cross-layer line registration.
    bb.merge(align_to_grid(bb.min, Point(line_spacing * 4, line_spacing * 4)));

    // generate pattern
    // Orca: optimize the cross-hatch infill pattern to improve strength when low infill density is used.
    // [INTENT] For low density (< 30%), reduce repeat_ratio via an exponential approach curve:
    //   repeat_ratio = clamp(1 - exp(-5 × density), 0.2, 1.0)
    // At density=0.05: repeat_ratio ≈ 0.22. At density=0.3: repeat_ratio ≈ 0.78.
    // Shorter repeat layers → more transition layers → better adhesion at low density.
    // [HAZARD H373] The exponential formula is an empirical heuristic. The constants
    // (5, 0.2, 1.0) are undocumented design choices. A port must preserve these exact values.
    double repeat_ratio = 1.0;
    if (params.density < 0.3)
        repeat_ratio = std::clamp(1.0 - std::exp(-5 * params.density), 0.2, 1.0);

    // [INTENT] Generate the raw (unclipped) polylines covering the full bounding box.
    // Fill::z is passed as z_height — the generator uses absolute Z to compute phase.
    Polylines polylines = generate_infill_layers(scale_(this->z), repeat_ratio, line_spacing, bb.size()(0), bb.size()(1));

    // shift the pattern to the actual space
    // [INTENT] All generated polylines are in [0..width]×[0..height] local coords.
    // Translate by bb.min to place them correctly in the rotated coordinate frame.
    for (Polyline& pl : polylines) {
        pl.translate(bb.min);
    }

    // Apply multiline offset if needed
    multiline_fill(polylines, params, spacing);

    // [INTENT] Clip to the expolygon boundary. to_polygons() flattens the expolygon
    // (contour + holes) into a Polygons collection for intersection_pl.
    polylines = intersection_pl(std::move(polylines), to_polygons(expolygon));

    // --- remove small remains from gyroid infill
    // [INTENT] The comment says "gyroid infill" but this is CrossHatch — copy-paste label error.
    // Remove polyline segments shorter than 0.8 × spacing to avoid micro-extrusions.
    // [HAZARD H374] 0.8 × spacing uses unscaled `this->spacing` (mm), but polyline
    // lengths from Polyline::length() are in scaled coord_t units. scale_(0.8 * spacing)
    // IS correctly applied here via `scale_()`. This is consistent and correct.
    if (!polylines.empty()) {
        // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
        polylines.erase(std::remove_if(polylines.begin(), polylines.end(),
                                       [minlength](const Polyline& pl) { return pl.length() < minlength; }),
                        polylines.end());
    }

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
