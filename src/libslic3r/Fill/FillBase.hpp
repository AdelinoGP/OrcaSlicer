#ifndef slic3r_FillBase_hpp_
#define slic3r_FillBase_hpp_

// [INTENT] FillBase.hpp — declares the abstract Fill base class, FillParams parameter bundle,
//          LockRegionParam (Orca-specific locked-zag density map), and the InfillFailedException
//          sentinel. Every infill pattern (rectilinear, honeycomb, gyroid, lightning, adaptive, …)
//          derives from Fill and implements _fill_surface_single(). The class hierarchy is:
//            Fill (abstract)
//              ├─ FillRectilinear, FillMonotonous, FillLockedZag  (scan-line)
//              ├─ FillAdaptive, FillSupportCubic                  (octree-driven)
//              ├─ FillLightning                                    (graph-grow)
//              ├─ FillHoneycomb, FillConcentricWPaths, …          (pattern-specific)
//              └─ FillEnsuring                                     (Arachne-based, concentric)

#include <assert.h>
#include <memory.h>
#include <float.h>
#include <stdint.h>
#include <stdexcept>

#include <type_traits>

#include "../libslic3r.h"
#include "../BoundingBox.hpp"
#include "../Exception.hpp"
#include "../Utils.hpp"
#include "../ExPolygon.hpp"
// BBS: necessary header for new function
//  [COUPLING] FillBase.hpp includes PrintConfig.hpp — couples the core geometry fill logic directly
//             to the full printer configuration type. Any port must either replicate PrintConfig or
//             decouple by passing only the relevant fields through FillParams.
#include "../PrintConfig.hpp"
#include "../Flow.hpp"
#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../ShortestPath.hpp"

namespace Slic3r {

class Surface;
enum InfillPattern : int;

namespace FillAdaptive {
struct Octree;
};

// [INTENT] InfillFailedException — thrown by fill_surface() / fill_surface_arachne() when an
//          infill pattern cannot produce valid output for a given surface fragment.
// [HAZARD] Classified as RuntimeError (not SlicingError) so callers treat it as a non-fatal
//          warning. fill_surface_extrusion() catches it silently: if both the primary fill and
//          the Arachne fill throw, the output ExtrusionEntitiesPtr is unchanged (no extrusion
//          appended for that surface). Callers must handle silently-empty output correctly.
// Infill shall never fail, therefore the error is classified as RuntimeError, not SlicingError.
class InfillFailedException : public Slic3r::RuntimeError
{
public:
    InfillFailedException() : Slic3r::RuntimeError("Infill failed") {}
};

// [INTENT] LockRegionParam — Orca-specific parameter bundle for the FillLockedZag dual-density
//          infill pattern. Carries per-density and per-flow ExPolygon region maps that define
//          where to use the skeleton (sparse) vs. skin (dense) sub-fills.
// [STATE]  Populated by PrintObject::_make_fill() from per-region config. Passed by const-ref to
//          set_lock_region_param() on the Fill instance.
// [COUPLING] The keys of skin_density_params and skeleton_density_params are float density values.
//            The keys of skin_flow_params and skeleton_flow_params are Flow objects (value-compared
//            by width + height + nozzle diameter). Changing Flow's equality definition breaks the maps.
// [MEMORY] All ExPolygons are stored by value (copied from the caller). No ownership issues.
struct LockRegionParam
{
    LockRegionParam() {}
    std::map<float, ExPolygons> skin_density_params;
    std::map<float, ExPolygons> skeleton_density_params;
    std::map<Flow, ExPolygons>  skin_flow_params;
    std::map<Flow, ExPolygons>  skeleton_flow_params;
};

// [INTENT] FillParams — POD (trivially copyable) parameter bundle passed to every Fill method.
//          Centralises all per-surface fill configuration to avoid long argument lists.
// [STATE]  All fields have in-class default values. Callers selectively override only relevant
//          fields. The static_assert below enforces POD/trivial-copy property.
// [HAZARD] `density` is a float in [0, 1]. full_infill() compares against 0.9999f — a float
//          comparison with no epsilon buffer. Compiler floating-point modes that round 1.0f to
//          0.99999994f (FP32 representation limit) will cause full_infill() to return false for
//          what the user intended as 100% fill. Not currently observed but worth monitoring.
// [HAZARD] `anchor_length` defaults to 1000mm. The comment notes "1000mm is roughly the maximum
//          length that fits into a 32bit coord_t." At SCALING_FACTOR=1e6, 1000mm = 1e9 scaled
//          units which exceeds INT32_MAX (~2.1e9). For very long anchors this is safe, but
//          if SCALING_FACTOR is ever changed the 1000mm default must be recalculated.
// [COUPLING] `config` is a raw non-owning pointer to PrintRegionConfig. It is only valid during
//            slicing; caching FillParams across slicing runs may expose dangling pointer UB.
// [HAZARD] `extrusion_role` is stored as ExtrusionRole(0), not as a named zero constant. Any
//          new ExtrusionRole value inserted before the existing zero entry would silently change
//          the default role for all FillParams constructed without explicit role assignment.
struct FillParams
{
    bool full_infill() const { return density > 0.9999f; }
    // Don't connect the fill lines around the inner perimeter.
    bool dont_connect() const { return anchor_length_max < 0.05f; }

    // Fill density, fraction in <0, 1>
    float density{0.f};
    // [INTENT] multiline > 1 activates Orca's multi-line infill (multiple parallel lines per
    //          fill pass). The integer value is the number of parallel lines per bundle.
    // [HAZARD] multiline == 0 is not validated; division by multiline in FillBase.cpp would
    //          produce division-by-zero. Callers must guarantee multiline >= 1.
    int multiline{1};

    // Length of an infill anchor along the perimeter.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float anchor_length{1000.f};
    float anchor_length_max{1000.f};

    // G-code resolution.
    double resolution{0.0125};

    // Don't adjust spacing to fill the space evenly.
    bool dont_adjust{true};

    // Monotonic infill - strictly left to right for better surface quality of top infills.
    bool monotonic{false};

    // For Honeycomb.
    // we were requested to complete each loop;
    // in this case we don't try to make more continuous paths
    bool complete{false};

    // For Concentric infill, to switch between Classic and Arachne.
    bool use_arachne{false};
    // Layer height for Concentric infill with Arachne.
    coordf_t layer_height{0.f};

    // For Lateral lattice
    coordf_t lateral_lattice_angle_1{0.f};
    coordf_t lateral_lattice_angle_2{0.f};
    // [INTENT] pattern stores the active InfillPattern enum value. Used by fill_surface_extrusion()
    //          to select between classic and Arachne paths, and by FillLockedZag to detect its own type.
    InfillPattern pattern{ipRectilinear};

    // For Lateral Honeycomb
    float infill_overhang_angle{60};

    // BBS
    // [INTENT] flow carries the pre-computed Flow object for this surface (nozzle width, layer height,
    //          spacing). Used by fill_surface_extrusion() to set the extrusion width on output paths.
    // [COUPLING] Flow is a value type; equality is compared by width + height + nozzle_diameter.
    //            FillLockedZag uses Flow as map key in LockRegionParam — changing Flow equality breaks this.
    Flow          flow;
    ExtrusionRole extrusion_role{ExtrusionRole(0)};
    bool          using_internal_flow{false};
    // BBS: only used for new top surface pattern
    float no_extrusion_overlap{0.0};
    // [HAZARD] config is a raw non-owning pointer. Valid only within a slicing session; must
    //          not be stored beyond the call stack of fill_surface_extrusion(). nullptr is valid
    //          (only gap fill and Arachne concentric use it).
    const PrintRegionConfig* config{nullptr};
    bool                     dont_sort{false}; // do not sort the lines, just simply connect them
    bool                     can_reverse{true};

    // [INTENT] horiz_move shifts infill lines horizontally to create cross-zag patterns.
    //          Used by FillLockedZag skin sub-fill to offset the skin lines from the skeleton.
    float horiz_move{0.0}; // move infill to get cross zag pattern
    // [INTENT] symmetric_infill_y_axis + symmetric_y_axis: Orca extension to force infill
    //          symmetry across a Y axis (useful for symmetric parts to reduce artefacts).
    bool    symmetric_infill_y_axis{false};
    coord_t symmetric_y_axis{0};
    // [INTENT] locked_zag + infill_lock_depth + skin_infill_depth: FillLockedZag parameters.
    //          locked_zag=true activates dual-density skeleton+skin mode.
    //          infill_lock_depth controls how deep the skeleton extends.
    //          skin_infill_depth controls how deep the skin extends.
    bool  locked_zag{false};
    float infill_lock_depth{0.0};
    float skin_infill_depth{0.0};
};
// [INTENT] Enforces that FillParams remains trivially copyable so it can be passed by value
//          efficiently across the fill pipeline without copy-constructor overhead.
// [HAZARD] Adding a non-trivial member (e.g. std::string, std::vector) to FillParams will cause
//          a compile-time error here — the build will break, which is intentional.
static_assert(IsTriviallyCopyable<FillParams>::value, "FillParams class is not POD (and it should be - see constructor).");

// [INTENT] Fill — abstract base class for all infill pattern implementations.
//          Instance fields (layer_id, z, spacing, overlap, angle, …) are set by the caller
//          (PrintObject::_make_fill()) before calling fill_surface() or fill_surface_extrusion().
//          Subclasses implement _fill_surface_single() (and optionally fill_surface_arachne()).
// [STATE]  Fill instances are NOT thread-safe. A new Fill instance is created per print region
//          per layer. TBB may process layers in parallel, but each TBB task owns its own Fill
//          instance — no sharing.
// [MEMORY] fill_surface() returns Polylines by value (move-eligible in callers).
//          fill_surface_extrusion() appends to an ExtrusionEntitiesPtr (pointer-owning vector).
//          The caller (SurfaceFill pipeline) owns all ExtrusionEntity* appended.
// [COUPLING] adapt_fill_octree is a raw non-owning pointer to FillAdaptive::Octree, built once
//            per model object and shared across all layers. Must remain valid for the lifetime of
//            any Fill instance that references it. Lifetime managed by PrintObject.
// [COUPLING] print_config + print_object_config are raw non-owning pointers. Valid only within
//            the slicing session that created the Fill instance.
class Fill
{
public:
    // Index of the layer.
    size_t layer_id;
    // Z coordinate of the top print surface, in unscaled coordinates
    coordf_t z;
    // in unscaled coordinates
    coordf_t spacing;
    // infill / perimeter overlap, in unscaled coordinates
    coordf_t overlap;
    // in radians, ccw, 0 = East
    // [HAZARD] Initial angle is FLT_MAX (sentinel for "not set"). If _infill_direction() is
    //          called without a valid angle override and the pattern does not set angle in
    //          _layer_angle(), the output angle will be FLT_MAX + π/2 = FLT_MAX (wraps to π/2
    //          for float arithmetic). A printf warning fires in _infill_direction() to catch this.
    float angle;

    // Orca: Fill direction is fixed absolute angle if SurfaceFillParams.fixed_angle or config.ironing_angle_fixed
    // [INTENT] fixed_angle = true: angle is treated as an absolute angle in radians, not a
    //          per-layer rotation. Used for ironing and certain top-surface patterns.
    bool fixed_angle{false};
    // In scaled coordinates. Maximum lenght of a perimeter segment connecting two infill lines.
    // Used by the FillRectilinear2, FillGrid2, FillTriangles, FillStars and FillCubic.
    // If left to zero, the links will not be limited.
    coord_t link_max_length;
    // In scaled coordinates. Used by the concentric infill pattern to clip the loops to create extrusion paths.
    coord_t loop_clipping;
    // In scaled coordinates. Bounding box of the 2D projection of the object.
    // [COUPLING] bounding_box must be set via set_bounding_box() before calling fill_surface().
    //            Not validated at fill time — using an uninitialised (empty) bounding box causes
    //            connect_infill() to produce degenerate arc connections.
    BoundingBox bounding_box;

    // Octree builds on mesh for usage in the adaptive cubic infill
    // [COUPLING] Raw non-owning pointer. Owned by PrintObject::m_adaptive_fill_octrees.
    //            Must remain valid for the entire slicing session. nullptr for non-adaptive fills.
    FillAdaptive::Octree* adapt_fill_octree = nullptr;

    // PrintConfig and PrintObjectConfig are used by infills that use Arachne (Concentric and FillEnsuring).
    // Orca: also used by gap fill function.
    // [HAZARD] Both are raw non-owning pointers. PrintObject passes its own config pointers.
    //          Accessing these after the PrintObject or Print is destroyed produces use-after-free.
    const PrintConfig*       print_config        = nullptr;
    const PrintObjectConfig* print_object_config = nullptr;

    // BBS: all no overlap expolygons in same layer
    // [INTENT] no_overlap_expolygons carries the union of all already-extruded areas on this layer
    //          (perimeters, supports, etc.) that infill must not overlap. Used in gap fill to
    //          subtract already-covered area before running medial_axis.
    // [STATE] Set by the caller per-layer before fill_surface_extrusion(). Not modified by Fill.
    ExPolygons no_overlap_expolygons;

    // [HAZARD] infill_anchor and infill_anchor_max are static class members (shared across ALL
    //          Fill instances). Written by PrintObject::_make_fill() on each slicing run.
    //          If two Print instances with different printer profiles exist simultaneously
    //          (future multi-printer UI), the last write wins. See hazard H250.
    static float infill_anchor;
    static float infill_anchor_max;

public:
    virtual ~Fill() {}
    // [INTENT] clone() is the standard Prototype pattern used to duplicate a Fill instance
    //          for parallel processing without re-running new_from_type().
    // [MEMORY] Returns raw owning pointer — caller must delete or assign to a smart pointer.
    virtual Fill* clone() const = 0;

    // [INTENT] new_from_type() — factory method mapping InfillPattern enum → Fill subclass instance.
    // [MEMORY] Returns raw owning pointer (no unique_ptr). See hazard H248.
    // [HAZARD] The string overload parses the pattern name; unrecognised names call assert(false)
    //          in debug builds and return nullptr in release. Callers must guard for nullptr.
    static Fill* new_from_type(const InfillPattern type);
    static Fill* new_from_type(const std::string& type);
    // [INTENT] use_bridge_flow() — class-level query. Returns true if the given pattern uses
    //          bridge flow rate (wide, thin extrusions to bridge gaps). Results are cached in a
    //          static vector populated lazily at first call. See hazard H249.
    static bool use_bridge_flow(const InfillPattern type);

    void set_bounding_box(const Slic3r::BoundingBox& bbox) { bounding_box = bbox; }
    // [INTENT] extended_object_bounding_box() — returns bounding_box expanded by sqrt(2) to
    //          encompass diagonal fill lines at any angle. Used by connect_infill() to bound
    //          the EdgeGrid search space.
    // [HAZARD] Scaling by sqrt(2) with float→coord_t cast; near INT32_MAX overflow silent. H240.
    BoundingBox extended_object_bounding_box() const;
    // Use bridge flow for the fill?
    virtual bool use_bridge_flow() const { return false; }

    // Do not sort the fill lines to optimize the print head path?
    virtual bool no_sort() const { return false; }

    // [INTENT] is_self_crossing() — pure virtual. Returns true if the pattern can produce
    //          self-intersecting toolpaths (e.g., gyroid). Used by PrintObject to decide
    //          whether to apply overlap avoidance.
    virtual bool is_self_crossing() = 0;

    // Return true if infill has a consistent pattern between layers.
    // [INTENT] has_consistent_pattern() — returns true for patterns (adaptive cubic, support)
    //          whose inter-layer geometry is consistent enough to enable alignment optimisations.
    virtual bool has_consistent_pattern() const { return false; }

    // Perform the fill.
    // [INTENT] fill_surface() — primary fill entry point. Returns Polylines for the given Surface.
    //          Default implementation calls _fill_surface_single() for each offset fragment.
    // [HAZARD] If the offset produces zero fragments (very narrow surface), returns empty Polylines.
    //          Callers must handle empty output. See hazard from FillBase.cpp:141.
    virtual Polylines fill_surface(const Surface* surface, const FillParams& params);
    // [INTENT] fill_surface_arachne() — Arachne-based fill producing ThickPolylines (variable-width).
    //          Only implemented by FillConcentricWPaths and FillEnsuring. Other subclasses return
    //          ThickPolylines built from the regular fill output.
    virtual ThickPolylines fill_surface_arachne(const Surface* surface, const FillParams& params);
    // [INTENT] set_lock_region_param() — Orca-specific hook for FillLockedZag to receive per-region
    //          density and flow maps before fill_surface() is called.
    virtual void set_lock_region_param(const LockRegionParam& lock_param) {};
    // BBS: this method is used to fill the ExtrusionEntityCollection.
    // It call fill_surface by default
    // [INTENT] fill_surface_extrusion() — orchestrates the complete fill pipeline:
    //          1. calls fill_surface() or fill_surface_arachne() to get raw polylines,
    //          2. converts polylines to ExtrusionPath/ExtrusionLoop objects,
    //          3. runs gap fill via _create_gap_fill() if params.density >= 1 and not a bridge.
    // [HAZARD] If InfillFailedException is thrown from both fill_surface and fill_surface_arachne,
    //          out is unchanged (no extrusion appended). Caller must handle silently-empty output.
    virtual void fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out);

protected:
    // [INTENT] Fill() — protected constructor. Layer_id starts at (size_t)-1 (sentinel "not set").
    //          angle starts at FLT_MAX (sentinel "not set"). Subclasses must not rely on these
    //          sentinel values remaining stable across invocations.
    Fill()
        : layer_id(size_t(-1))
        , z(0.)
        , spacing(0.)
        ,
        // Infill / perimeter overlap.
        overlap(0.)
        ,
        // Initial angle is undefined.
        angle(FLT_MAX)
        , link_max_length(0)
        , loop_clipping(0)
        ,
        // The initial bounding box is empty, therefore undefined.
        bounding_box(Point(0, 0), Point(-1, -1))
    {}

    // The expolygon may be modified by the method to avoid a copy.
    // [INTENT] _fill_surface_single() — pure-virtual per-subclass fill implementation.
    //          Called once per surface fragment (after offset) from fill_surface().
    //          Appends raw polylines to polylines_out.
    // [HAZARD] The ExPolygon parameter is passed by value intentionally (the method may modify
    //          it to avoid a copy on the hot path). Callers must not rely on the expolygon being
    //          unchanged after the call.
    // [CONCURRENCY] Called from TBB tasks; each task has its own Fill instance so no shared state
    //               is accessed through this virtual dispatch. Subclass implementations must be
    //               independently re-entrant (they generally are, being stateless algorithms).
    virtual void _fill_surface_single(const FillParams& /* params */,
                                      unsigned int /* thickness_layers */,
                                      const std::pair<float, Point>& /* direction */,
                                      ExPolygon /* expolygon */,
                                      Polylines& /* polylines_out */)
    {}

    // Used for concentric infill to generate ThickPolylines using Arachne.
    // [INTENT] Arachne overload of _fill_surface_single(). Only FillConcentricWPaths and
    //          FillEnsuring provide non-trivial implementations. Other subclasses rely on
    //          the default empty body, meaning fill_surface_arachne() degrades gracefully for them.
    virtual void _fill_surface_single(const FillParams&              params,
                                      unsigned int                   thickness_layers,
                                      const std::pair<float, Point>& direction,
                                      ExPolygon                      expolygon,
                                      ThickPolylines&                thick_polylines_out)
    {}

    // [INTENT] _layer_angle() — returns the infill rotation angle for a given layer index.
    //          Default: 0° on even layers, 90° on odd layers. Overridden by patterns that use
    //          other rotation sequences (e.g., triangles use 60° increments).
    // [HAZARD] fixed_angle == true skips the alternating rotation — returns 0.f always. Callers
    //          that apply angle + _layer_angle() must first check fixed_angle to avoid double rotation.
    virtual float _layer_angle(size_t idx) const { return fixed_angle ? 0.f : (idx & 1) ? float(M_PI / 2.) : 0.f; }

    // [INTENT] _infill_direction() — computes the (angle, origin) pair for fill_surface().
    //          Applies _layer_angle() offset and rotates the bounding box centre to the fill angle.
    virtual std::pair<float, Point> _infill_direction(const Surface* surface) const;

    // Orca: Dedicated function to calculate gap fill lines for the provided surface, according to the print object parameters
    // and append them to the out ExtrusionEntityCollection.
    // [INTENT] _create_gap_fill() — runs medial_axis on the uncovered gap region, filters short
    //          spurs by filter_out_gap_fill config, and appends ExtrusionPath objects to out.
    //          Called from fill_surface_extrusion() after the primary fill is appended.
    // [COUPLING] Requires print_config and print_object_config to be non-null. Uses
    //            no_overlap_expolygons to compute the uncovered gap area.
    void _create_gap_fill(const Surface* surface, const FillParams& params, ExtrusionEntityCollection* out);

public:
    // [INTENT] connect_infill() — three overloads for ExPolygon, Polygons, and vector<Polygon*>
    //          boundary types. Takes infill_ordered (sorted polylines), walks the boundary graph
    //          to connect endpoints via perimeter arcs, and outputs connected polylines.
    // [COUPLING] All overloads call create_boundary_infill_graph() internally, which builds the
    //            BoundaryInfillGraph linked-list structure. The EdgeGrid is built from `boundary`.
    // [HAZARD] See H237: map_infill_end_point_to_boundary must not be resized after construction.
    // [HAZARD] See H238: silent boundary_idx_unconnected on snapping failure.
    static void connect_infill(
        Polylines&& infill_ordered, const ExPolygon& boundary, Polylines& polylines_out, const double spacing, const FillParams& params);
    static void connect_infill(Polylines&&        infill_ordered,
                               const Polygons&    boundary,
                               const BoundingBox& bbox,
                               Polylines&         polylines_out,
                               const double       spacing,
                               const FillParams&  params);
    static void connect_infill(Polylines&&                        infill_ordered,
                               const std::vector<const Polygon*>& boundary,
                               const BoundingBox&                 bbox,
                               Polylines&                         polylines_out,
                               double                             spacing,
                               const FillParams&                  params);

    // [INTENT] chain_or_connect_infill() — heuristically decides between nearest-neighbour
    //          chaining (ShortestPath) and full boundary-arc connection (connect_infill()).
    //          Used by patterns that don't benefit from arc walking (e.g., gyroid, honeycomb).
    static void chain_or_connect_infill(
        Polylines&& infill_ordered, const ExPolygon& boundary, Polylines& polylines_out, const double spacing, const FillParams& params);

    // [INTENT] connect_base_support() — variant of connect_infill() designed for base support infill.
    //          Instead of perimeter arcs, it evaluates structural support arch costs and emits
    //          loop bands (emit_loops_in_band) for spanning arches.
    // [HAZARD] See H243: emit_loops_in_band() has division-by-zero on exactly vertical contour segments.
    // [HAZARD] See H245: static cost constants in connect_base_support() are misleading.
    static void connect_base_support(Polylines&&                        infill_ordered,
                                     const std::vector<const Polygon*>& boundary_src,
                                     const BoundingBox&                 bbox,
                                     Polylines&                         polylines_out,
                                     const double                       spacing,
                                     const FillParams&                  params);
    static void connect_base_support(Polylines&&        infill_ordered,
                                     const Polygons&    boundary_src,
                                     const BoundingBox& bbox,
                                     Polylines&         polylines_out,
                                     const double       spacing,
                                     const FillParams&  params);

    // [INTENT] _adjust_solid_spacing() — adjusts line spacing so that an integer number of
    //          lines fills the given width. Clamps the spacing increase to ≤120%.
    // [HAZARD] If number_of_intervals == 0 (very narrow fill), returns `distance` unchanged,
    //          producing a single overfill line rather than an empty result.
    static coord_t _adjust_solid_spacing(const coord_t width, const coord_t distance);
};
// Fill  Multiline
//  [INTENT] multiline_fill() — Orca-specific free function that takes a set of parallel infill
//           polylines and generates additional offset copies (multiline bundles) using Clipper2's
//           ClipperOffset. The original lines plus all offsets are written back to `polylines`.
//  [HAZARD] See H247: Clipper2 Execute is called in a loop after a single AddPaths — correct
//           usage but non-obvious semantics that differ from naive re-instantiation.
//  [HAZARD] See H246: static_cast<int>(polylines.size()) overflows for > INT_MAX polylines.
void multiline_fill(Polylines& polylines, const FillParams& params, float spacing);
} // namespace Slic3r

#endif // slic3r_FillBase_hpp_
