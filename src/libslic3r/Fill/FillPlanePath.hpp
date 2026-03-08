// [INTENT] Declares the FillPlanePath family of space-filling curve infill patterns.
// Three concrete implementations share a common framework:
//   FillArchimedeanChords  — Archimedean spiral (r = a + b·θ), centered, for spiral infill.
//   FillHilbertCurve       — Hilbert space-filling curve, corner-anchored, maximum coverage.
//   FillOctagramSpiral     — Octagram spiral (8-sided expanding star), centered.
//
// The shared base class FillPlanePath handles all clipping, alignment, and rotation
// concerns. Concrete subclasses only implement `generate()` which emits the curve
// points into an `InfillPolylineOutput` (either direct or bbox-clipped).
//
// Key design:
//   InfillPolylineOutput     — converts normalized curve points to scaled coord_t.
//   InfillPolylineClipper    — extends Output with bbox clipping (Cohen-Sutherland-like).
//   FillPlanePath::centered()— if true, origin at bbox center; else at bbox min.
//   FillPlanePath::generate()— pure virtual; emits the curve points.
//
// [COUPLING] Depends on FillBase (multiline_fill, connect_infill, chain_polylines),
//            ClipperUtils (intersection_pl), ShortestPath (chain_polylines).
// [STATE] Inherits Fill::spacing, Fill::angle, Fill::bounding_box, Fill::print_object_config.
// [CONCURRENCY] No shared mutable state. Thread-safe per-fill-region call.

#ifndef slic3r_FillPlanePath_hpp_
#define slic3r_FillPlanePath_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

// The original Perl code used path generators from Math::PlanePath library:
// http://user42.tuxfamily.org/math-planepath/
// http://user42.tuxfamily.org/math-planepath/gallery.html

// [INTENT] InfillPolylineOutput collects Vec2d curve points, converts them to
// scaled coord_t Points, and provides a result() move. Used as the base output
// for all PlanePath curve generators.
//
// [STATE] m_out: accumulated output point list.
//         m_scale_out: scaling factor from normalized curve units to coord_t.
// [HAZARD H345] `result()` moves m_out (leaving it empty). Calling result() twice
// returns an empty vector on the second call — no error is raised.
class InfillPolylineOutput
{
public:
    InfillPolylineOutput(const double scale_out) : m_scale_out(scale_out) {}

    void reserve(size_t n) { m_out.reserve(n); }
    // [INTENT] Convert and append one curve point (in normalized units) to output.
    void add_point(const Vec2d& pt) { m_out.emplace_back(this->scaled(pt)); }
    // [INTENT] Move-return the collected output points.
    Points&& result() { return std::move(m_out); }
    // [INTENT] Returns false for base class (no clipping). Overridden to true in Clipper.
    virtual bool clips() const { return false; }

protected:
    // [INTENT] Converts a Vec2d normalized point to a scaled coord_t Point.
    // Rounds to nearest integer via floor(...+0.5) to minimize quantization error.
    const Point scaled(const Vec2d& fpt) const
    {
        return {coord_t(floor(fpt.x() * m_scale_out + 0.5)), coord_t(floor(fpt.y() * m_scale_out + 0.5))};
    }

    // Output polyline.
    Points m_out;

private:
    // Scaling coefficient of the generated points before tested against m_bbox and clipped by bbox.
    double m_scale_out;
};

// [INTENT] Abstract base class for plane-path curve infill patterns. Handles:
//   1. Bounding box alignment (aligned vs. snug, centered vs. min).
//   2. Resolution parameter passing to generate().
//   3. Multiline fill expansion.
//   4. Clipping to ExPolygon, chaining/connecting, and rotation.
//
// Subclasses implement generate() to emit the specific curve pattern.
//
// [HAZARD H346] `_layer_angle()` always returns 0.f — plane-path fills do not rotate
// between layers. The infill direction is controlled by `direction.first` passed to
// `_fill_surface_single()`, which is typically derived from `_layer_angle()` at the
// call site. FillArchimedeanChords and FillOctagramSpiral use centered=true so they
// always generate from the object center, regardless of direction angle.
class FillPlanePath : public Fill
{
public:
    ~FillPlanePath() override = default;
    // [INTENT] Returns false — space-filling curves do not self-intersect by construction.
    bool is_self_crossing() override { return false; }

protected:
    // [INTENT] Main fill entry point. Generates curve, clips, connects, and rotates back.
    void _fill_surface_single(const FillParams&              params,
                              unsigned int                   thickness_layers,
                              const std::pair<float, Point>& direction,
                              ExPolygon                      expolygon,
                              Polylines&                     polylines_out) override;

    // [INTENT] No layer-to-layer rotation for plane-path fills.
    float _layer_angle(size_t idx) const override { return 0.f; }

    // [INTENT] If true, curve is generated centered on the bounding box center.
    //          If false, curve is generated from the bounding box min corner.
    virtual bool centered() const = 0;

    friend class InfillPolylineClipper;

    // [INTENT] Pure virtual: emits the curve points into output. The bounding box is
    // specified in normalized units (divided by distance_between_lines). resolution
    // is also normalized (params.resolution / distance_between_lines).
    virtual void generate(
        coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput& output) = 0;
};

// [INTENT] FillArchimedeanChords: Archimedean spiral r = a + b·θ, centered on the
// bounding box. Produces a single continuous spiral polyline from inside to outside.
// Used for solid surfaces and flow calibration (spiral inside-out printing).
//
// [HAZARD H347] The spiral generates until r >= rmax but does not stop at exactly rmax.
// The last point may be slightly outside the bounding box. Clipped by intersection_pl().
class FillArchimedeanChords : public FillPlanePath
{
public:
    Fill* clone() const override { return new FillArchimedeanChords(*this); };
    ~FillArchimedeanChords() override = default;

protected:
    // [INTENT] Spiral is centered on the bounding box center for axially symmetric coverage.
    bool centered() const override { return true; }
    void generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput& output) override;
};

// [INTENT] FillHilbertCurve: Hilbert space-filling curve. The curve visits every unit
// cell in a 2^k × 2^k grid exactly once in a locality-preserving order.
// Produces the most uniform coverage of the bounding box of all PlanePath fills.
//
// [HAZARD H348] Hilbert curve requires a power-of-two grid size. The actual bounding box
// is always rounded UP to the nearest power of 2. For a 3×5 region, a 8×8 grid is used —
// ~72% of points are outside the actual region and are clipped by intersection_pl().
// This is correct but wastes generation time for non-power-of-two geometries.
class FillHilbertCurve : public FillPlanePath
{
public:
    Fill* clone() const override { return new FillHilbertCurve(*this); };
    ~FillHilbertCurve() override = default;

protected:
    // [INTENT] Hilbert curve starts at the grid corner (min_x, min_y), not the center.
    bool centered() const override { return false; }
    void generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput& output) override;
};

// [INTENT] FillOctagramSpiral: expanding octagram (8-pointed star) spiral. Each ring
// adds 16 points (8 cardinal + 8 diagonal), expanding by sqrt(2) per ring.
// Produces a recognizably decorative pattern; less uniform than Hilbert but visually distinct.
//
// [HAZARD H349] Octagram spiral always advances by r_inc = sqrt(2) per outer ring,
// regardless of the requested line spacing / density. The ring spacing is always
// sqrt(2) × distance_between_lines. For non-default densities, the visual ring spacing
// deviates from the expected line spacing.
class FillOctagramSpiral : public FillPlanePath
{
public:
    Fill* clone() const override { return new FillOctagramSpiral(*this); };
    ~FillOctagramSpiral() override = default;

protected:
    // [INTENT] Octagram spiral is centered on the bounding box center.
    bool centered() const override { return true; }
    void generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput& output) override;
};

} // namespace Slic3r

#endif // slic3r_FillPlanePath_hpp_
