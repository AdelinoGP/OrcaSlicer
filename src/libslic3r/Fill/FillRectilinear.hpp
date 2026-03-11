#ifndef slic3r_FillRectilinear_hpp_
#define slic3r_FillRectilinear_hpp_

// [INTENT] Declares the rectilinear-family infill hierarchy. `FillRectilinear` supplies the
// scan-line core used by classic rectilinear and by most grid-derived patterns; subclasses vary
// only in sweep scheduling, per-layer angle policy, or post-ordering semantics.
//
// Family overview:
//   - FillRectilinear        — base scan-line engine with single- and multi-sweep helpers.
//   - FillAlignedRectilinear — same engine, but pins the layer angle to 0 for every layer.
//   - FillMonotonic*         — preserve ordered left-to-right deposition for top-surface quality.
//   - FillGrid/Triangles/Stars/Cubic/QuarterCubic/Lateral* — compose multiple sweeps into 2D/3D lattices.
//   - FillSupportBase        — rectilinear support-specific variant.
//   - FillZigZag/CrossZag/LockedZag — Orca/BBS derivatives with continuity or density-lock logic.
//
// [COUPLING] Implementations in `FillRectilinear.cpp` depend on Clipper polygon offsets,
//            ExPolygon scan conversion, and FillBase helpers such as `_infill_direction()`,
//            `connect_infill()`, and `multiline_fill()`. The declarations here are therefore the
//            public front door for the largest portion of the infill subsystem.
// [CONCURRENCY] Instances are per-surface/per-layer workers. The classes are not internally
//               synchronized and rely on the generic Fill pipeline to provide one configured
//               instance per TBB job.

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class PrintRegionConfig;
class Surface;

// [INTENT] FillRectilinear — base class for all scan-line infill patterns built from one or more
// directional sweeps through an ExPolygon. Concrete subclasses usually override `fill_surface()`
// to choose sweep angles/pattern shifts, but reuse the helper methods declared below.
// [STATE] Inherits mutable Fill state (`spacing`, `overlap`, `bounding_box`, `angle`, etc.) which
//         must be configured by the caller before `fill_surface()` is invoked.
// [HAZARD] `clone()` returns a raw owning pointer. Lifetime is still governed by the legacy Fill
//          factory protocol rather than smart pointers.
class FillRectilinear : public Fill
{
public:
    Fill* clone() const override { return new FillRectilinear(*this); }
    ~FillRectilinear() override = default;
    // [INTENT] Main rectilinear entry point. Chooses one or more sweep schedules and then delegates
    // to `fill_surface_by_lines()` / `fill_surface_by_multilines()` depending on density, pattern,
    // and the subclass's angle policy.
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      is_self_crossing() override { return false; }

protected:
    // Fill by single directional lines, interconnect the lines along perimeters.
    // [INTENT] Shared helper for simple one-angle scan-line patterns. The input polygon is rotated so
    // vertical scan lines can be emitted, clipped, and linked through perimeter walks.
    // [STATE] Appends to `polylines_out` and reads inherited `bounding_box` / `spacing` state.
    bool fill_surface_by_lines(
        const Surface* surface, const FillParams& params, float angleBase, float pattern_shift, Polylines& polylines_out);

    // Fill by multiple sweeps of differing directions.
    // [INTENT] `SweepParams` is the minimal description of one sweep in a compound lattice pattern:
    // angle_base picks the scan orientation and pattern_shift offsets the infinite line family so
    // successive sweeps interleave instead of collapsing onto identical coordinates.
    struct SweepParams
    {
        float angle_base;
        float pattern_shift;
    };
    // [INTENT] Shared implementation for grid/triangles/stars/cubic families that overlay several
    // rectilinear passes and then merge/reconnect the resulting polylines.
    bool fill_surface_by_multilines(const Surface*                            surface,
                                    FillParams                                params,
                                    const std::initializer_list<SweepParams>& sweep_params,
                                    Polylines&                                polylines_out);
    // [INTENT] Specialized helper for trapezoidal or alternating-width variants. `Pattern_type`
    // selects a mode inside `FillRectilinear.cpp`; the enum is encoded as an int here, so callers
    // must match the implementation's private switch table exactly.
    // [HAZARD] Unscoped `int Pattern_type` is a weak interface boundary: changing the implementation
    //          constants without updating call sites silently changes pattern behaviour.
    bool fill_surface_trapezoidal(const Surface*                            surface,
                                  FillParams                                params,
                                  const std::initializer_list<SweepParams>& sweep_params,
                                  Polylines&                                polylines_out,
                                  int                                       Pattern_type);

    // The extended bounding box of the whole object that covers any rotation of every layer.
    // [INTENT] Used by sample_grid_pattern() and multi-sweep patterns so scan lines can be generated
    // in a stable world-space frame even after arbitrary rotation.
    BoundingBox extended_object_bounding_box() const;
};

// [INTENT] Variant that disables the normal alternating-layer rotation; every layer uses the same
// rectilinear frame. Downstream subclasses such as FillLateralHoneycomb inherit this fixed-angle policy.
class FillAlignedRectilinear : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillAlignedRectilinear(*this); }
    ~FillAlignedRectilinear() override = default;

protected:
    // Always generate infill at the same angle.
    virtual float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillMonotonic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonic(*this); }
    ~FillMonotonic() override = default;
    // [INTENT] Overrides the base algorithm to preserve a monotonic deposition order, trading some
    // path optimality for better top-surface optical consistency.
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      no_sort() const override { return true; }
};

// [INTENT] Thin monotonic-line variant used where only a single monotonic sweep is desired.
class FillMonotonicLine : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonicLine(*this); }
    ~FillMonotonicLine() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      no_sort() const override { return true; }
};

class FillGrid : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillGrid(*this); }
    ~FillGrid() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      is_self_crossing() override { return true; }

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

// [INTENT] Lateral lattice reuses the rectilinear engine but chooses a distinct multi-sweep layout
// tailored for sparse structural lattices rather than generic infill.
class FillLateralLattice : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillLateralLattice(*this); }
    ~FillLateralLattice() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillTriangles : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillTriangles(*this); }
    ~FillTriangles() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      is_self_crossing() override { return true; }

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillStars : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillStars(*this); }
    ~FillStars() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      is_self_crossing() override { return true; }

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillCubic(*this); }
    ~FillCubic() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      is_self_crossing() override { return true; }

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

// [INTENT] QuarterCubic is Cura-derived. It reuses the cubic family machinery but with a phase-
// shifted subset of sweeps so adjacent layers build a quarter-period 3D lattice.
class FillQuarterCubic : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillQuarterCubic(*this); }
    ~FillQuarterCubic() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

class FillLateralHoneycomb : public FillAlignedRectilinear
{
public:
    Fill* clone() const override { return new FillLateralHoneycomb(*this); }
    ~FillLateralHoneycomb() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
};

// [INTENT] SupportBase is the rectilinear implementation used for support-interface style fills.
// It inherits the scan-line algorithm but fixes the layer angle so support strands stack predictably.
class FillSupportBase : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillSupportBase(*this); }
    ~FillSupportBase() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;

protected:
    // The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
};

// [INTENT] Orca imports PrusaSlicer's `FillMonotonicLines` and makes it the canonical monotonic-
// line implementation, replacing the older BBS `FillMonotonicLineWGapFill` path kept below only as
// commented historical context.
class FillMonotonicLines : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillMonotonicLines(*this); }
    ~FillMonotonicLines() override = default;
    Polylines fill_surface(const Surface* surface, const FillParams& params) override;
    bool      no_sort() const override { return true; }
};

// Orca: Replaced with FillMonotonicLines, inheriting from FillRectilinear
/*class FillMonotonicLineWGapFill : public Fill
{
public:
    ~FillMonotonicLineWGapFill() override = default;
    void fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out) override;
    bool is_self_crossing() override { return false; }

protected:
    Fill* clone() const override { return new FillMonotonicLineWGapFill(*this); };
    bool no_sort() const override { return true; }

private:
    void fill_surface_by_lines(const Surface* surface, const FillParams& params, Polylines& polylines_out);
};*/

class FillZigZag : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillZigZag(*this); }
    ~FillZigZag() override = default;

    bool has_consistent_pattern() const override { return true; }
};

class FillCrossZag : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillCrossZag(*this); }
    ~FillCrossZag() override = default;

    bool has_consistent_pattern() const override { return true; }
};

// [INTENT] LockedZag is Orca's dual-density rectilinear derivative. It emits extrusion entities
// directly so it can mix different widths/flows inside one logical fill based on `lock_param`.
// [STATE] `lock_param` is a per-instance cache injected through `set_lock_region_param()` before
//         fill generation.
class FillLockedZag : public FillRectilinear
{
public:
    Fill* clone() const override { return new FillLockedZag(*this); }
    ~FillLockedZag() override = default;
    LockRegionParam lock_param;

    void fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out) override;

    bool has_consistent_pattern() const override { return true; }
    void set_lock_region_param(const LockRegionParam& lock_param) override { this->lock_param = lock_param; };
    void fill_surface_locked_zag(const Surface*                           surface,
                                 const FillParams&                        params,
                                 std::vector<std::pair<Polylines, Flow>>& multi_width_polyline);
};

// [INTENT] Grid samplers shared by Lightning infill and other scan-line patterns. They produce a
// deterministic lattice of point samples clipped to the given polygon set and aligned to a global
// bounding box so neighbouring layers use compatible sample phases.
// [COUPLING] DistanceField.cpp relies on these helpers to convert unsupported overhang polygons into
// discrete cells. Changing the sampling contract changes Lightning tree density.
Points sample_grid_pattern(const ExPolygon& expolygon, coord_t spacing, const BoundingBox& global_bounding_box);
Points sample_grid_pattern(const ExPolygons& expolygons, coord_t spacing, const BoundingBox& global_bounding_box);
Points sample_grid_pattern(const Polygons& polygons, coord_t spacing, const BoundingBox& global_bounding_box);

} // namespace Slic3r

#endif // slic3r_FillRectilinear_hpp_
