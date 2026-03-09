#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

// [INTENT] PerimeterGenerator.hpp: Declares the PerimeterGenerator class, which computes
//   perimeter extrusion paths (loops) for a single layer region.
//   Two execution paths:
//     - process_classic(): traditional Slic3r concentric-ring perimeter algorithm
//     - process_arachne(): Arachne/WallToolPaths variable-width perimeter algorithm
//   Caller (LayerRegion::make_perimeters) sets all public input pointer fields, then calls
//   one of the process_*() methods which populate the output pointer fields.
//
// [COUPLING] All input/output fields are raw non-owning pointers. Caller (LayerRegion) owns
//   all pointed-to objects and must ensure they outlive the PerimeterGenerator instance.
//   This design is caller-managed, not RAII — lifetimes are implicit and unchecked.
//
// [HAZARD] H1067 P2/Medium: Nine raw pointer inputs (slices, compatible_regions, upper_slices,
//   upper_slices_same_region, lower_slices, config, object_config, print_config) plus four raw
//   pointer outputs (loops, gap_fill, fill_surfaces, fill_no_overlap). None are guarded by
//   smart pointers or assertions in this header. A refactor should wrap inputs in const refs
//   and outputs in output-parameter wrappers.
//
// [HAZARD] H1068 P2/Medium: FuzzySkinConfig stored as key in
//   `std::unordered_map<FuzzySkinConfig, ExPolygons> regions_by_fuzzify`.
//   This requires the hash<FuzzySkinConfig> specialization in std:: namespace (defined below).
//   The hash uses boost::hash_combine which requires Boost headers. Any refactor to a
//   non-Boost environment must reimplement the combining step.
//   Note: `mode` field of FuzzySkinConfig is NOT included in the hash (only in operator==).
//   This is a latent hash collision hazard if two configs differ only in `mode`.

#include "libslic3r.h"
#include <vector>
#include "Layer.hpp"
#include "Flow.hpp"
#include "Polygon.hpp"
#include "PrintConfig.hpp"
#include "SurfaceCollection.hpp"

namespace Slic3r {
// [INTENT] FuzzySkinConfig: Parameters for fuzzy skin effect on this region.
//   Used as an unordered_map key — requires hash<FuzzySkinConfig> specialization (below).
//   Equality operator covers all fields; hash does NOT cover `mode` — see H1068.
struct FuzzySkinConfig
{
    FuzzySkinType type;
    coord_t       thickness;
    coord_t       point_distance;
    bool          fuzzy_first_layer;
    NoiseType     noise_type;
    double        noise_scale;
    int           noise_octaves;
    double        noise_persistence;
    FuzzySkinMode mode;

    bool operator==(const FuzzySkinConfig& r) const
    {
        return type == r.type && thickness == r.thickness && point_distance == r.point_distance &&
               fuzzy_first_layer == r.fuzzy_first_layer && noise_type == r.noise_type && noise_scale == r.noise_scale &&
               noise_octaves == r.noise_octaves && noise_persistence == r.noise_persistence && mode == r.mode;
    }

    bool operator!=(const FuzzySkinConfig& r) const { return !(*this == r); }
};
} // namespace Slic3r

namespace std {
template<> struct hash<Slic3r::FuzzySkinConfig>
{
    size_t operator()(const Slic3r::FuzzySkinConfig& c) const noexcept
    {
        std::size_t seed = std::hash<Slic3r::FuzzySkinType>{}(c.type);
        boost::hash_combine(seed, std::hash<coord_t>{}(c.thickness));
        boost::hash_combine(seed, std::hash<coord_t>{}(c.point_distance));
        boost::hash_combine(seed, std::hash<bool>{}(c.fuzzy_first_layer));
        boost::hash_combine(seed, std::hash<Slic3r::NoiseType>{}(c.noise_type));
        boost::hash_combine(seed, std::hash<double>{}(c.noise_scale));
        boost::hash_combine(seed, std::hash<int>{}(c.noise_octaves));
        boost::hash_combine(seed, std::hash<double>{}(c.noise_persistence));
        return seed;
    }
};
} // namespace std

namespace Slic3r {

// [INTENT] PerimeterGenerator: Stateful computation object for a single layer region's perimeters.
//   Caller fills public input fields, calls process_classic() or process_arachne(), then reads
//   output fields. Not reusable across multiple regions without re-instantiation.
//   [COUPLING] Tightly coupled to LayerRegion (caller), PrintRegionConfig, PrintObjectConfig,
//   PrintConfig, Flow, ExtrusionEntityCollection, SurfaceCollection. Any change to those types
//   cascades here.
class PerimeterGenerator
{
public:
    // [INTENT] Input fields: all non-owning raw pointers + value types set by caller before process_*().
    // [HAZARD] H1067 P2/Medium: Nine raw pointer inputs — no null assertions, no smart pointer safety.
    //   upper_slices and lower_slices are set to nullptr in constructor and must be set by caller.
    const SurfaceCollection* slices;                   // [STATE] Sliced surfaces of this region (owned by LayerRegion)
    const LayerRegionPtrs*   compatible_regions;       // [STATE] Sibling regions sharing perimeter config (owned by Layer)
    const ExPolygons*        upper_slices;             // [STATE] Polygons from layer above (nullptr if top layer)
    const SurfaceCollection* upper_slices_same_region; // [STATE] Surfaces from layer above, same region
    const ExPolygons*        lower_slices;             // [STATE] Polygons from layer below (nullptr if bottom layer)
    double                   layer_height;             // [STATE] Physical height of this layer in mm
    int                      layer_id;                 // [STATE] Zero-based layer index; -1 until set by caller
    coordf_t                 slice_z;                  // [STATE] Z coordinate of this slice in mm
    Flow                     perimeter_flow;           // [STATE] Flow for inner perimeters
    Flow                     ext_perimeter_flow;       // [STATE] Flow for outer/external perimeter
    Flow                     overhang_flow;            // [STATE] Flow for overhanging perimeters
    Flow                     solid_infill_flow;        // [STATE] Flow for solid infill (used for gap fill sizing)
    const PrintRegionConfig* config;                   // [STATE] Per-region config (perimeter count, speeds, etc.)
    const PrintObjectConfig* object_config;            // [STATE] Per-object config
    const PrintConfig*       print_config;             // [STATE] Global print config (resolution, etc.)

    // [INTENT] Output fields: populated by process_classic() or process_arachne().
    //   All are non-owning pointers to containers owned by LayerRegion.
    ExtrusionEntityCollection* loops;         // [STATE] Output: perimeter loop extrusions
    ExtrusionEntityCollection* gap_fill;      // [STATE] Output: gap fill extrusions
    SurfaceCollection*         fill_surfaces; // [STATE] Output: remaining infill surfaces after perimeters
    // BBS
    ExPolygons* fill_no_overlap; // [STATE] Output (BBS extension): infill area excluding overlap zones

    // BBS: Extended flow and overhang polygon series for BambuStudio-specific bridge/overhang detection
    Flow                  smaller_ext_perimeter_flow;               // [STATE] BBS: reduced-width outer perimeter flow
    std::vector<Polygons> m_lower_polygons_series;                  // [STATE] BBS: layered lower polygon offsets for overhang detection
    std::vector<Polygons> m_external_lower_polygons_series;         // [STATE] BBS: external variant of lower polygon series
    std::vector<Polygons> m_smaller_external_lower_polygons_series; // [STATE] BBS: smaller external variant

    // [INTENT] Fuzzy skin region map: groups ExPolygons by their FuzzySkin config for batch processing.
    // [HAZARD] H1068: hash<FuzzySkinConfig> does not include `mode` — two configs differing only in
    //   mode hash to the same bucket. regions_by_fuzzify lookup may return wrong entry in that case.
    bool                                            has_fuzzy_skin = false; // [STATE] any region has fuzzy skin enabled
    bool                                            has_fuzzy_hole = false; // [STATE] any hole has fuzzy skin enabled
    std::unordered_map<FuzzySkinConfig, ExPolygons> regions_by_fuzzify;     // [STATE] map from config to affected polygons

    // [INTENT] Constructor: sets all pointer inputs and initializes scalar state.
    //   upper_slices / lower_slices / upper_slices_same_region are intentionally null at construction;
    //   caller sets them before calling process_*().
    //   m_scaled_resolution clamps print_config->resolution below EPSILON to avoid zero-resolution.
    //   All mm3_per_mm metrics initialized to -1 as sentinel "not yet computed".
    PerimeterGenerator(
        // Input:
        const SurfaceCollection* slices,
        const LayerRegionPtrs*   compatible_regions,
        double                   layer_height,
        coordf_t                 slice_z,
        Flow                     flow,
        const PrintRegionConfig* config,
        const PrintObjectConfig* object_config,
        const PrintConfig*       print_config,
        const bool               spiral_mode,
        // Output:
        // Loops with the external thin walls
        ExtrusionEntityCollection* loops,
        // Gaps without the thin walls
        ExtrusionEntityCollection* gap_fill,
        // Infills without the gap fills
        SurfaceCollection* fill_surfaces,
        // BBS
        ExPolygons* fill_no_overlap)
        : slices(slices)
        , compatible_regions(compatible_regions)
        , upper_slices(nullptr) // [STATE] set by caller after construction
        , lower_slices(nullptr) // [STATE] set by caller after construction
        , layer_height(layer_height)
        , slice_z(slice_z)
        , layer_id(-1) // [STATE] -1 sentinel; set by caller before process_*()
        , perimeter_flow(flow)
        , ext_perimeter_flow(flow)
        , overhang_flow(flow)
        , solid_infill_flow(flow)
        , config(config)
        , object_config(object_config)
        , print_config(print_config)
        , m_spiral_vase(spiral_mode)
        , m_scaled_resolution(scaled<double>(print_config->resolution.value > EPSILON ? print_config->resolution.value : EPSILON))
        // [INTENT] resolution clamped to EPSILON to prevent zero-length resolution degenerate cases
        , loops(loops)
        , gap_fill(gap_fill)
        , fill_surfaces(fill_surfaces)
        , fill_no_overlap(fill_no_overlap)
        , m_ext_mm3_per_mm(-1) // [STATE] -1 = not yet computed
        , m_mm3_per_mm(-1)
        , m_mm3_per_mm_overhang(-1)
        , m_ext_mm3_per_mm_smaller_width(-1)
    {}

    // [INTENT] process_classic(): traditional concentric-ring perimeter algorithm (Slic3r heritage).
    //   Iterates inward from outer perimeter, eroding geometry by flow spacing each iteration.
    void process_classic();

    // [INTENT] process_arachne(): variable-width perimeter algorithm using Arachne WallToolPaths.
    //   Computes medial axis / skeletal trapezoidation to fill region with variable-width walls.
    //   Generally produces better thin-wall coverage than classic at the cost of complexity.
    void process_arachne();

    // [INTENT] add_infill_contour_for_arachne(): helper called from process_arachne() to compute
    //   and register infill contour polygons after perimeter widths are resolved.
    void add_infill_contour_for_arachne(ExPolygons infill_contour,
                                        int        loops,
                                        coord_t    ext_perimeter_spacing,
                                        coord_t    perimeter_spacing,
                                        coord_t    min_perimeter_infill_spacing,
                                        coord_t    spacing,
                                        bool       is_inner_part);

    // [INTENT] Volume-per-mm accessors: read-only access to computed flow metrics after process_*().
    //   Return -1 if process_*() has not been called yet (sentinel from constructor).
    double ext_mm3_per_mm() const { return m_ext_mm3_per_mm; }
    double mm3_per_mm() const { return m_mm3_per_mm; }
    double mm3_per_mm_overhang() const { return m_mm3_per_mm_overhang; }
    // BBS
    double   smaller_width_ext_mm3_per_mm() const { return m_ext_mm3_per_mm_smaller_width; }
    Polygons lower_slices_polygons() const { return m_lower_slices_polygons; }

private:
    // [INTENT] generate_lower_polygons_series(): precomputes a series of inward-offset lower-layer
    //   polygons at increasing offsets to support overhang angle classification.
    std::vector<Polygons> generate_lower_polygons_series(float width);

    // [INTENT] split_top_surfaces(): partitions infill polygons into top (fully exposed) and
    //   non-top, used to apply top-surface finish settings selectively.
    void split_top_surfaces(const ExPolygons& orig_polygons,
                            ExPolygons&       top_fills,
                            ExPolygons&       non_top_polygons,
                            ExPolygons&       fill_clip) const;

    // [INTENT] apply_extra_perimeters(): adds extra perimeter loops in areas where the infill
    //   would otherwise be unsupported (slope/overhang compensation).
    void apply_extra_perimeters(ExPolygons& infill_area);

    // [INTENT] process_no_bridge(): BBS extension — handles surfaces that must not be treated as
    //   bridges even if they overhang, applying special perimeter/infill rules.
    void process_no_bridge(Surfaces& all_surfaces, coord_t perimeter_spacing, coord_t ext_perimeter_width);

private:
    // [STATE] m_spiral_vase: if true, only one outer perimeter is generated, continuously rising Z
    //   (vase mode). process_classic() skips inner loops and gap fill in this mode.
    bool m_spiral_vase;

    // [STATE] m_scaled_resolution: polygon simplification threshold in scaled coords.
    //   Derived from print_config->resolution; clamped to EPSILON to avoid degenerate zero.
    double m_scaled_resolution;

    // [STATE] Computed volumetric flow rates (mm³/mm) for different perimeter types.
    //   All initialized to -1 (sentinel). Set during process_*() calls.
    double m_ext_mm3_per_mm;      // outer perimeter
    double m_mm3_per_mm;          // inner perimeter
    double m_mm3_per_mm_overhang; // overhang perimeter
    // BBS
    double   m_ext_mm3_per_mm_smaller_width; // BBS: reduced-width outer perimeter
    Polygons m_lower_slices_polygons;        // [STATE] BBS: cached union of lower_slices polygons for overhang queries
};

} // namespace Slic3r

#endif
