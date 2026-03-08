// [INTENT] Implements FillPlanePath subclass logic: InfillPolylineClipper (bbox trimming),
// FillPlanePath::_fill_surface_single (shared alignment/clipping/chaining driver),
// and three concrete curve generators:
//   generate_archimedean_chords — Archimedean spiral (r = a + b·θ)
//   generate_hilbert_curve      — Hilbert space-filling curve (2^k × 2^k grid)
//   generate_octagram_spiral    — Expanding 8-pointed star spiral
//
// [COUPLING] ClipperUtils (intersection_pl), ShortestPath (chain_polylines),
//            FillBase (connect_infill, multiline_fill), Surface (ExPolygon).
// [MEMORY] All curves produce a single large Polyline; intersection_pl clips it into
//          multiple short segments. chain_polylines reorders for minimal travel.
// [CONCURRENCY] No shared mutable state. Each call is independent per fill region.

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "../Surface.hpp"

#include "FillPlanePath.hpp"

namespace Slic3r {

// [INTENT] InfillPolylineClipper: extends InfillPolylineOutput with a point-by-point
// bounding-box clip. Uses a simplified Cohen-Sutherland outcode scheme:
//   - Three consecutive points are examined at a time.
//   - If all three share the same outside half-plane (Left/Right/Top/Bottom),
//     the middle point is culled as provably invisible.
//   - If the middle point is inside, or if the edge might still cross the bbox
//     corner, the middle point is kept.
// This is a heuristic approximation — it does NOT perform exact Liang-Barsky or
// Cohen-Sutherland segment clipping. Actual clipping is done later by intersection_pl().
// The purpose here is solely to reduce the number of points before intersection_pl.
//
// [HAZARD H350] The clipper only culls the middle point of a three-point window.
// Points that are outside in the same half-plane but not three-consecutive may still
// pass through. This is safe (intersection_pl clips correctly later), but the
// "optimisation" does not guarantee a tight clip — it is a filter, not a clipper.
//
// [HAZARD H351] m_sides_prev and m_sides_this are uninitialized for the first two
// points (the `< 2` branch initialises them lazily). If add_point() is called with
// exactly 1 point total and then result() is called, m_sides_this is garbage.
// In practice, all generators emit ≥ 2 points, so this is benign but brittle.
class InfillPolylineClipper : public InfillPolylineOutput
{
public:
    // [INTENT] Construct with the bbox to filter against and the curve scale factor.
    InfillPolylineClipper(const BoundingBox bbox, const double scale_out) : InfillPolylineOutput(scale_out), m_bbox(bbox) {}

    void     add_point(const Vec2d& pt);
    Points&& result() { return std::move(m_out); }
    bool     clips() const override { return true; }

private:
    // [INTENT] Cohen-Sutherland outcode bits.
    // Multiple bits may be set simultaneously (e.g. Top|Left = upper-left corner).
    enum class Side { Left = 1, Right = 2, Top = 4, Bottom = 8 };

    // [INTENT] Compute the outcode bitmask for point p relative to m_bbox.
    // Returns 0 if inside or on the boundary.
    int sides(const Point& p) const
    {
        return int(p.x() < m_bbox.min.x()) * int(Side::Left) + int(p.x() > m_bbox.max.x()) * int(Side::Right) +
               int(p.y() < m_bbox.min.y()) * int(Side::Bottom) + int(p.y() > m_bbox.max.y()) * int(Side::Top);
    };

    // Bounding box to clip the polyline with.
    BoundingBox m_bbox;

    // Classification of the two last points processed.
    // [STATE] m_sides_prev: outcode of point before the most recently emitted point.
    //         m_sides_this: outcode of the most recently emitted point.
    //         Both are set lazily during the first two calls to add_point().
    int m_sides_prev;
    int m_sides_this;
};

// [INTENT] add_point: Accept one new curve point, apply the three-point filter,
// and append (or drop) the previous point accordingly.
//
// [STATE] m_out grows by 1 per accepted point. When a point is culled, m_out shrinks
// by 1 (pop_back of the previous point), then grows by 1 (the current point is still added).
// Net effect: the culled middle point disappears; the sliding window advances.
//
// [COUPLING] Calls this->scaled() (from InfillPolylineOutput) to convert Vec2d → Point.
void InfillPolylineClipper::add_point(const Vec2d& fpt)
{
    // [INTENT] Scale the incoming normalized curve point to coord_t.
    const Point pt{this->scaled(fpt)};

    if (m_out.size() < 2) {
        // Collect the two first points and their status.
        // [STATE] First call: initialise m_sides_prev and push. Second call: initialise m_sides_this and push.
        (m_out.empty() ? m_sides_prev : m_sides_this) = sides(pt);
        m_out.emplace_back(pt);
    } else {
        // Classify the last inserted point, possibly remove it.
        int sides_next = sides(pt);
        if ( // This point is inside. Take it.
            m_sides_this == 0 ||
            // Either this point is outside and previous or next is inside, or
            // the edge possibly cuts corner of the bounding box.
            // [INTENT] The AND of all three outcodes is zero only when there is no
            // single half-plane that all three points are outside of — meaning the
            // polyline segment might still clip through the bbox.
            (m_sides_prev & m_sides_this & sides_next) == 0) {
            // Keep the last point.
            m_sides_prev = m_sides_this;
        } else {
            // All the three points (this, prev, next) are outside at the same side.
            // Ignore the last point.
            // [INTENT] This is a conservative cull: if all three share an outcode bit,
            // the middle point is guaranteed invisible (same half-plane rejection).
            m_out.pop_back();
        }
        // And save the current point.
        m_out.emplace_back(pt);
        m_sides_this = sides_next;
    }
}

// [INTENT] _fill_surface_single: the common fill driver for all FillPlanePath subclasses.
// Steps:
//   1. Rotate the expolygon by -direction.first so the curve is axis-aligned.
//   2. Compute bounding box: aligned (object bbox) for sparse infill, snug for solid.
//   3. Translate to curve origin (center for centered(), min corner otherwise).
//   4. Normalise bounding box to curve units (divide by distance_between_lines).
//   5. Dispatch to the subclass generate() — either with or without bbox clipper.
//   6. Apply multiline fill expansion.
//   7. Clip result against expolygon with intersection_pl().
//   8. Chain or connect the clipped segments.
//   9. Translate and rotate back to world coordinates.
//
// [STATE] Modifies polylines_out (appends). Does not modify any class member state.
// [COUPLING] Uses Fill::bounding_box (for aligned infill), Fill::spacing, Fill::centered(),
//            Fill::print_object_config (for flow calibration special case).
void FillPlanePath::_fill_surface_single(const FillParams&              params,
                                         unsigned int                   thickness_layers,
                                         const std::pair<float, Point>& direction,
                                         ExPolygon                      expolygon,
                                         Polylines&                     polylines_out)
{
    // [INTENT] Rotate the region into the infill coordinate frame.
    // direction.first is the infill angle (radians). Rotating by -angle aligns the
    // pattern with the local X axis. Rotated back after chaining.
    expolygon.rotate(-direction.first);

    // FIXME Vojtech: We are not sure whether the user expects the fill patterns on visible surfaces to be aligned across all the islands of
    // a single layer.
    //  One may align for this->centered() to align the patterns for Archimedean Chords and Octagram Spiral patterns.
    //  [INTENT] Sparse infill (density < ~100%) must be layer-aligned so pattern lines
    //  from different islands line up. Solid infill can use a snug bbox for efficiency.
    const bool align = params.density < 0.995;

    // [INTENT] Snug bounding box: tight around the expolygon, inflated by SCALED_EPSILON
    // to avoid clipping artifacts at the exact boundary.
    BoundingBox snug_bounding_box = get_extents(expolygon).inflated(SCALED_EPSILON);

    // Expand the bounding box to avoid artifacts at the edges
    // [INTENT] For multiline fills, expand by multiline count × spacing so all
    // parallel offset lines have room to generate without edge truncation.
    snug_bounding_box.offset(scale_(this->spacing) * params.multiline);

    // Rotated bounding box of the area to fill in with the pattern.
    // [INTENT] aligned=true: use the object-level bounding box (rotated to match the
    // infill angle) so all layers share the same grid origin — necessary for sparse
    // infill to produce continuous vertical walls between layers.
    // aligned=false: use the per-island snug bbox for efficiency.
    BoundingBox bounding_box =
        align ?
            // Sparse infill needs to be aligned across layers. Align infill across layers using the object's bounding box.
            this->bounding_box.rotated(-direction.first) :
            // Solid infill does not need to be aligned across layers, generate the infill pattern
            // around the clipping expolygon only.
            snug_bounding_box;

    // [INTENT] For centered() patterns (Archimedean, Octagram), translate origin to
    // the bounding box center so the spiral emanates from the center.
    // For non-centered (Hilbert), translate to the min corner for grid alignment.
    Point shift = this->centered() ? bounding_box.center() : bounding_box.min;
    expolygon.translate(-shift.x(), -shift.y());
    bounding_box.translate(-shift.x(), -shift.y());

    Polyline polyline;
    {
        // [INTENT] Normalise spacing by multiline count and density to get the
        // actual distance between adjacent fill lines in scaled units.
        // This becomes the coordinate scale factor for the curve generators.
        auto distance_between_lines = scaled<double>(this->spacing) * params.multiline / params.density;

        // [INTENT] Convert bbox corners to curve integer units (divide by spacing).
        // Using ceil() for the max ensures we always fully cover the bbox.
        auto min_x = coord_t(ceil(coordf_t(bounding_box.min.x()) / distance_between_lines));
        auto min_y = coord_t(ceil(coordf_t(bounding_box.min.y()) / distance_between_lines));
        auto max_x = coord_t(ceil(coordf_t(bounding_box.max.x()) / distance_between_lines));
        auto max_y = coord_t(ceil(coordf_t(bounding_box.max.y()) / distance_between_lines));

        // [INTENT] Normalize params.resolution to curve units. Used by Archimedean to
        // limit chord approximation error. Hilbert and Octagram ignore it.
        auto resolution = scaled<double>(params.resolution) / distance_between_lines;

        if (align) {
            // Filling in a bounding box over the whole object, clip generated polyline against the snug bounding box.
            // [INTENT] For aligned (sparse) infill, the curve is generated over the full
            // object bbox and then trimmed to the island using InfillPolylineClipper.
            // The clipper provides an early coarse cull; intersection_pl() does the
            // precise clip.
            snug_bounding_box.translate(-shift.x(), -shift.y());
            InfillPolylineClipper output(snug_bounding_box, distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, output);
            polyline.points = std::move(output.result());
        } else {
            // Filling in a snug bounding box, no need to clip.
            // [INTENT] For dense/solid infill using the snug bbox, skip the intermediate
            // coarse clipper — generate directly into the base output.
            InfillPolylineOutput output(distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, output);
            polyline.points = std::move(output.result());
        }
    }

    // [INTENT] Wrap the single big polyline in a vector for multiline_fill and intersection_pl.
    Polylines polylines = {polyline};

    // Apply multiline offset if needed
    // [INTENT] For multiline fills, generate additional parallel offset polylines.
    // See FillBase::multiline_fill for details.
    multiline_fill(polylines, params, spacing);

    // [INTENT] Only proceed if the generated polyline has at least 2 points
    // (a degenerate single-point polyline cannot be clipped or chained meaningfully).
    if (polyline.size() >= 2) {
        // [INTENT] Clip the generated curve to the (un-shifted, still-rotated) expolygon.
        // This is the definitive clip — intersection_pl handles all concave boundaries
        // and holes, producing multiple short Polyline segments.
        polylines = intersection_pl(std::move(polylines), expolygon);
        if (!polylines.empty()) {
            Polylines chained;
            if (params.dont_connect() || params.density > 0.5) {
                // [INTENT] For high-density or no-connect infill, chain segments by
                // nearest-neighbor travel (no gap-bridging connections).
                //
                // [HAZARD H352] Special case: flow rate calibration (calib_flowrate_topinfill_special_order)
                // changes the chaining order for FillArchimedeanChords to put the center
                // spiral last. This uses a runtime dynamic_cast<FillArchimedeanChords*>
                // on every call — minor overhead, but a code smell: behavior should not
                // depend on the dynamic type of `this` inside a base class method.
                // A virtual method would be cleaner.

                // ORCA: special flag for flow rate calibration
                auto is_flow_calib = params.extrusion_role == erTopSolidInfill &&
                                     this->print_object_config->has("calib_flowrate_topinfill_special_order") &&
                                     this->print_object_config->option("calib_flowrate_topinfill_special_order")->getBool() &&
                                     dynamic_cast<FillArchimedeanChords*>(this);
                if (is_flow_calib) {
                    // We want the spiral part to be printed inside-out
                    // Find the center spiral line first, by looking for the longest one
                    // [INTENT] The longest post-clip segment is assumed to be the central
                    // spiral arm. It is reversed if needed (ensuring inside→outside print
                    // order) and moved to the end of the chain so it prints last.
                    //
                    // [HAZARD H353] This heuristic (longest polyline = center spiral) is
                    // fragile. For non-circular expolygons, the longest post-clip segment
                    // may not be the center; a corner rectangle could produce a longer
                    // outer arc than the center. The assumption is undocumented.
                    auto     it            = std::max_element(polylines.begin(), polylines.end(),
                                                              [](const Polyline& a, const Polyline& b) { return a.length() < b.length(); });
                    Polyline center_spiral = std::move(*it);

                    // Ensure the spiral is printed from inside to out
                    // [INTENT] The spiral should print from center outward. Compare the
                    // squared distance of first/last point from origin (the shifted center).
                    // [HAZARD H354] squaredNorm() compares distance from (0,0) in the
                    // shifted coordinate frame. After the shift-translate above, the spiral
                    // center IS at (0,0). But the expolygon has also been translated, so
                    // the check is valid — but only if the spiral center coincides with the
                    // expolygon centroid, which is true for Archimedean but may not hold if
                    // the bounding box center differs from the visual spiral center.
                    if (center_spiral.first_point().squaredNorm() > center_spiral.last_point().squaredNorm()) {
                        center_spiral.reverse();
                    }

                    // Chain the other polylines
                    polylines.erase(it);
                    chained = chain_polylines(std::move(polylines));

                    // Then add the center spiral back
                    chained.push_back(std::move(center_spiral));
                } else {
                    chained = chain_polylines(std::move(polylines));
                }
            } else
                // [INTENT] For low-density infill (≤ 50%), connect adjacent segments
                // with short bridging moves to minimize travel. connect_infill may add
                // thin connecting lines between segment endpoints.
                connect_infill(std::move(polylines), expolygon, chained, this->spacing, params);

            // paths must be repositioned and rotated back
            // [INTENT] All operations were done in the shifted+rotated frame.
            // Undo the shift and undo the rotation to return to world coordinates.
            for (Polyline& pl : chained) {
                pl.translate(shift.x(), shift.y());
                pl.rotate(direction.first);
            }
            append(polylines_out, std::move(chained));
        }
    }
}

// Follow an Archimedean spiral, in polar coordinates: r=a+b\theta
// [INTENT] generate_archimedean_chords: emits a discretised Archimedean spiral starting
// at the origin, spiraling outward until r >= rmax (the diagonal of the bbox + margin).
//
// The spiral equation is r = a + b·θ, with:
//   a = 1 (initial radius offset)
//   b = 1 / (2·π) (advances 1 unit per full revolution, so spacing = 1 unit)
//
// The discretization step per point adapts to the current radius:
//   dθ = 2·acos(1 - resolution/r)
// This ensures the chord error stays below `resolution` regardless of radius.
// At small r the steps are large (coarser approximation is safe); at large r steps shrink.
//
// [HAZARD H355] The spiral starts at (0,0) then (1,0), and `r` starts at 1, not at 0.
// The center of the spiral (r < 1) has a gap — the origin is not the true spiral
// center. The FIXME comment acknowledges this: for solid infill there is an uncovered
// center area of radius ~1 (in curve units). After scaling to mm this gap is ≈ spacing.
//
// [HAZARD H356] `resolution` is the normalised chord error. If called with resolution=0
// (e.g. if params.resolution rounds to zero after normalisation), dθ = 2·acos(1) = 0
// and the while loop is infinite. No guard exists for resolution ≤ 0.
// In practice FillParams::resolution defaults to a nonzero value, but this is fragile.
template<typename Output>
static void generate_archimedean_chords(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, Output& output)
{
    // Radius to achieve.
    // [INTENT] rmax is the diagonal of the max quadrant + 1.5 safety margin,
    // ensuring the spiral covers all four quadrants of the bbox.
    coordf_t rmax = std::sqrt(coordf_t(max_x) * coordf_t(max_x) + coordf_t(max_y) * coordf_t(max_y)) * std::sqrt(2.) + 1.5;
    // Now unwind the spiral.
    coordf_t a     = 1.;
    coordf_t b     = 1. / (2. * M_PI);
    coordf_t theta = 0.;
    coordf_t r     = 1;
    Pointfs  out;
    // FIXME Vojtech: If used as a solid infill, there is a gap left at the center.
    output.add_point({0, 0});
    output.add_point({1, 0});
    while (r < rmax) {
        // Discretization angle to achieve a discretization error lower than resolution.
        // [INTENT] dθ = 2·acos(1 - ε/r), derived from the chord-to-arc error formula:
        // chord_error = r · (1 - cos(dθ/2)) ≤ ε → dθ = 2·acos(1 - ε/r).
        theta += 2. * acos(1. - resolution / r);
        r = a + b * theta;
        output.add_point({r * cos(theta), r * sin(theta)});
    }
}

// [INTENT] FillArchimedeanChords::generate: dispatch to the template generator using
// the correct output type (clipper or plain). The template deduction handles both cases.
void FillArchimedeanChords::generate(
    coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput& output)
{
    if (output.clips())
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, static_cast<InfillPolylineClipper&>(output));
    else
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, output);
}

// Adapted from
// http://cpansearch.perl.org/src/KRYDE/Math-PlanePath-122/lib/Math/PlanePath/HilbertCurve.pm
//
// [INTENT] hilbert_n_to_xy: converts a Hilbert curve index n to (x, y) grid coordinates
// using the standard 2-bit digit state-machine algorithm.
//
// The Hilbert curve in a 2^k × 2^k grid is defined recursively:
//   - Divide the grid into four 2×2 quadrants.
//   - Each quadrant is one of four orientations (plain, transpose, rot180+transpose, etc.)
//   - The `state` variable encodes the current orientation.
//   - Each 2-bit digit of n selects a quadrant and transitions to the next state.
//
// State diagram (4 states × 4 digits = 16 entries in next_state[]):
//   state=0  (plain)               state=4  (transpose)
//   state=8  (rot180)              state=12 (rot180+transpose)
//
// state=0    3--2   plain
//               |
//            0--1
//
// state=4    1--2  transpose
//            |  |
//            0  3
//
// state=8
//
// state=12   3  0  rot180 + transpose
//            |  |
//            2--1
//
// [HAZARD H357] hilbert_n_to_xy uses coord_t (32-bit integer) for x and y. For very
// large grids (pw > 15, i.e. >32768 curve units on a side), x and y overflow coord_t.
// In practice Hilbert infill is limited by the object bounding box / spacing, so
// pw > 15 is unlikely at normal print scales, but possible for very large objects at
// very fine spacing.
static inline Point hilbert_n_to_xy(const size_t n)
{
    // [INTENT] Lookup tables for the Hilbert state machine.
    // next_state[state + digit] → next state after processing this digit.
    // digit_to_x/y[state + digit] → x/y bit contributed by this digit at current scale.
    static constexpr const int next_state[16]{4, 0, 0, 12, 0, 4, 4, 8, 12, 8, 8, 4, 8, 12, 12, 0};
    static constexpr const int digit_to_x[16]{0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0};
    static constexpr const int digit_to_y[16]{0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1};

    // [INTENT] Count the number of 2-bit digits in n (i.e. floor(log4(n)) + 1).
    // ndigits determines the grid level k (grid is 2^ndigits × 2^ndigits).
    // Number of 2 bit digits.
    size_t ndigits = 0;
    {
        size_t nc = n;
        while (nc > 0) {
            nc >>= 2;
            ++ndigits;
        }
    }
    // [INTENT] Initial state depends on parity of ndigits to ensure the curve ends
    // in the correct corner for recursive composition.
    int     state = (ndigits & 1) ? 4 : 0;
    coord_t x     = 0;
    coord_t y     = 0;
    for (int i = (int) ndigits - 1; i >= 0; --i) {
        int digit = (n >> (i * 2)) & 3;
        state += digit;
        // [INTENT] OR the x/y bit at position i into the result coordinate.
        // This builds up the binary representation of x and y simultaneously.
        x |= digit_to_x[state] << i;
        y |= digit_to_y[state] << i;
        state = next_state[state];
    }
    return Point(x, y);
}

// [INTENT] generate_hilbert_curve: emits Hilbert curve points for all cells in the
// smallest 2^k × 2^k grid that contains the given bounding box [min_x..max_x, min_y..max_y].
//
// Steps:
//   1. Find the smallest power-of-two sz ≥ max(width, height).
//   2. Iterate all sz² cell indices in Hilbert order (i = 0..sz²-1).
//   3. Convert each index to (x,y) via hilbert_n_to_xy, offset by (min_x, min_y).
//   4. Emit all points (InfillPolylineClipper culls those outside the bbox).
//
// [HAZARD H348 — see .hpp] Up to (sz² - actual_area) points are outside the bbox
// and are later clipped. For non-power-of-two dimensions, up to 75% of generated
// points may be discarded. The generation cost is always O(sz²) where sz is the
// rounded-up power of two.
//
// [MEMORY] reserve(sz2) pre-allocates the output vector. For a 1000×1000mm object
// at 0.4mm spacing, sz ≈ 2048, sz² ≈ 4M points × ~8 bytes = ~32MB. Large but
// bounded by object/spacing ratio.
template<typename Output> static void generate_hilbert_curve(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output& output)
{
    // Minimum power of two square to fit the domain.
    // [INTENT] sz starts at 2 (the minimum meaningful Hilbert grid).
    // Expand until sz ≥ (max - min + 1) in both dimensions.
    size_t sz = 2;
    size_t pw = 1;
    {
        size_t sz0 = std::max(max_x + 1 - min_x, max_y + 1 - min_y);
        while (sz < sz0) {
            sz = sz << 1;
            ++pw;
        }
    }

    size_t sz2 = sz * sz;
    output.reserve(sz2);
    for (size_t i = 0; i < sz2; ++i) {
        Point p = hilbert_n_to_xy(i);
        // [INTENT] Offset by (min_x, min_y) so the Hilbert curve is aligned to the
        // bounding box origin in curve integer units.
        output.add_point({p.x() + min_x, p.y() + min_y});
    }
}

// [INTENT] FillHilbertCurve::generate: dispatch to the template generator.
void FillHilbertCurve::generate(
    coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double /* resolution */, InfillPolylineOutput& output)
{
    if (output.clips())
        generate_hilbert_curve(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper&>(output));
    else
        generate_hilbert_curve(min_x, min_y, max_x, max_y, output);
}

// [INTENT] generate_octagram_spiral: emits a centered octagram (8-pointed star) spiral
// expanding outward. Each ring consists of 16 points forming a star shape, advancing
// by r_inc = sqrt(2) per ring.
//
// Ring geometry: for each ring at radius r, the 16 points are:
//   (r, 0), (r+rx, rx), (rx, rx), (rx, r+rx),   ← quadrant 1
//   (0, r), (-rx, r+rx), (-rx, rx), (-r-rx, rx), ← quadrant 2
//   (-r, 0), (-r-rx, -rx), (-rx, -rx), (-rx, -r-rx), ← quadrant 3
//   (0, -r), (rx, -r-rx), (rx, -rx), (r+r_inc, -rx)  ← close/transition
// where rx = r / sqrt(2).
//
// [HAZARD H349 — see .hpp] r_inc = sqrt(2) is hardcoded regardless of density or spacing.
// The ring spacing in mm is always sqrt(2) × distance_between_lines. For non-default
// densities the visual ring spacing does not match the requested line spacing.
//
// [HAZARD H358] The transition point at the end of each ring is (r + r_inc, -rx), not
// (r + r_inc, 0). This intentionally positions the next ring's start at a point slightly
// offset from the X axis. The asymmetry is correct for a continuous spiral but may
// produce a visible artifact at the ring transition when densely filled.
template<typename Output> static void generate_octagram_spiral(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output& output)
{
    // Radius to achieve.
    // [INTENT] Same rmax as Archimedean: diagonal of max quadrant + 1.5 margin.
    coordf_t rmax = std::sqrt(coordf_t(max_x) * coordf_t(max_x) + coordf_t(max_y) * coordf_t(max_y)) * std::sqrt(2.) + 1.5;
    // Now unwind the spiral.
    coordf_t r     = 0;
    coordf_t r_inc = sqrt(2.);
    output.add_point({0., 0.});
    while (r < rmax) {
        r += r_inc;
        // [INTENT] rx = r/√2 is the "diagonal" radius, positioning the 45° star points.
        coordf_t rx = r / sqrt(2.);
        // [INTENT] r2 = r + rx is the outer corner of the star arm at 45°.
        coordf_t r2 = r + rx;
        output.add_point({r, 0.});
        output.add_point({r2, rx});
        output.add_point({rx, rx});
        output.add_point({rx, r2});
        output.add_point({0., r});
        output.add_point({-rx, r2});
        output.add_point({-rx, rx});
        output.add_point({-r2, rx});
        output.add_point({-r, 0.});
        output.add_point({-r2, -rx});
        output.add_point({-rx, -rx});
        output.add_point({-rx, -r2});
        output.add_point({0., -r});
        output.add_point({rx, -r2});
        output.add_point({rx, -rx});
        // [INTENT] Final point of this ring jumps to the start of the next ring's
        // axis-aligned point, creating the outward spiral transition.
        output.add_point({r2 + r_inc, -rx});
    }
}

// [INTENT] FillOctagramSpiral::generate: dispatch to the template generator.
void FillOctagramSpiral::generate(
    coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double /* resolution */, InfillPolylineOutput& output)
{
    if (output.clips())
        generate_octagram_spiral(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper&>(output));
    else
        generate_octagram_spiral(min_x, min_y, max_x, max_y, output);
}

} // namespace Slic3r
