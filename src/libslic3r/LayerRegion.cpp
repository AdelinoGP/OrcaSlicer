// [INTENT] LayerRegion.cpp — per-region slice processing for a single layer.
// Responsibilities:
//   1. Flow calculation for various extrusion roles (perimeter, bridge).
//   2. Perimeter generation dispatch (classic vs Arachne).
//   3. Surface type assignment: slices_to_fill_surfaces_clipped(), prepare_fill_surfaces().
//   4. External surface expansion: process_external_surfaces() — the core bridge/top/bottom
//      expansion pipeline using wave propagation (RegionExpansion algorithm).
//   5. Geometry utilities: infill_area_threshold(), trim_surfaces(), elephant_foot_compensation.
//   6. Path simplification: arc-fitting or Douglas-Peucker for final extrusion paths.
//   7. SVG debug export.
//
// [COUPLING] LayerRegion → PerimeterGenerator (make_perimeters dispatch),
//            LayerRegion → BridgeDetector (bridge angle detection, legacy path),
//            LayerRegion → Algorithm::RegionExpansion (wave propagation, new path),
//            LayerRegion → ClipperUtils (intersection_ex, union_ex, diff_ex, offset_ex),
//            LayerRegion → Layer (lower_layer, upper_layer lslices access).
//
// [MEMORY] No owned heap objects. All surfaces stored by value in SurfaceCollection.
//          fill_surfaces, slices, perimeters, fills, thin_fills are all value-owned.
//
// [CONCURRENCY] process_external_surfaces() is called from PrintObject's parallel prepare_infill.
//               Each LayerRegion is independent; no shared mutable state between regions.
//
// [STATE] Two separate surface systems:
//   slices      — raw slices, typed by detect_surfaces_type()
//   fill_surfaces — post-perimeter fill geometry, typed into top/bottom/bridge/internal
#include "Layer.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "PerimeterGenerator.hpp"
#include "Print.hpp"
#include "Surface.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"
#include "Algorithm/RegionExpansion.hpp"

#include <string>
#include <map>

#include <boost/log/trivial.hpp>
#include <boost/algorithm/clamp.hpp>

namespace Slic3r {

// [INTENT] Compute the extrusion flow for a given role at the current layer height.
// Delegates to PrintRegion::flow() which factors in nozzle diameter, layer height, spacing.
// [COUPLING] → PrintRegion::flow() → Flow calculation.
Flow LayerRegion::flow(FlowRole role) const { return this->flow(role, m_layer->height); }

// [INTENT] Overload for explicit layer_height override (e.g. first layer, variable height).
Flow LayerRegion::flow(FlowRole role, double layer_height) const
{
    return m_region->flow(*m_layer->object(), role, layer_height, m_layer->id() == 0);
}

// [INTENT] Compute bridging flow. Two modes:
//   - thick_bridge (legacy Slic3r): sqrt(bridge_flow)*nozzle_diameter for rounded cross-section.
//   - normal (other slicers): reuse regular perimeter flow with bridge_flow ratio applied.
// [HAZARD H363] region.extruder(role) - 1 can underflow to MAX_INT for role=frExternalPerimeter
//               if extruder is 0. get_at() falls back to element 0 (commented in source) —
//               this silent fallback may be intentional but is fragile in a refactor.
Flow LayerRegion::bridging_flow(FlowRole role, bool thick_bridge) const
{
    const PrintRegion&       region        = this->region();
    const PrintRegionConfig& region_config = region.config();
    const PrintObject&       print_object  = *this->layer()->object();
    Flow                     bridge_flow;
    auto                     nozzle_diameter = float(print_object.print()->config().nozzle_diameter.get_at(region.extruder(role) - 1));
    if (thick_bridge) {
        // The old Slic3r way (different from all other slicers): Use rounded extrusions.
        // Get the configured nozzle_diameter for the extruder associated to the flow role requested.
        // Here this->extruder(role) - 1 may underflow to MAX_INT, but then the get_at() will follback to zero'th element, so everything is
        // all right. Applies default bridge spacing.
        bridge_flow = Flow::bridging_flow(float(sqrt(region_config.bridge_flow)) * nozzle_diameter, nozzle_diameter);
    } else {
        // The same way as other slicers: Use normal extrusions. Apply bridge_flow while maintaining the original spacing.
        bridge_flow = this->flow(role).with_flow_ratio(region_config.bridge_flow);
    }
    return bridge_flow;
}

// [INTENT] Re-classify fill_surfaces by trimming the existing typed surfaces against
// fill_expolygons (the fill boundary computed from perimeter generation output).
// Groups surfaces by type, intersects each group against fill_expolygons, rebuilds
// fill_surfaces. Must be idempotent — called before and sometimes after infill combination.
// [STATE] Modifies fill_surfaces in-place. fill_expolygons is read-only here.
// Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
void LayerRegion::slices_to_fill_surfaces_clipped()
{
    // Note: this method should be idempotent, but fill_surfaces gets modified
    // in place. However we're now only using its boundaries (which are invariant)
    // so we're safe. This guarantees idempotence of prepare_infill() also in case
    // that combine_infill() turns some fill_surface into VOID surfaces.
    // Collect polygons per surface type.
    std::array<SurfacesPtr, size_t(stCount)> by_surface;
    for (Surface& surface : this->slices.surfaces)
        by_surface[size_t(surface.surface_type)].emplace_back(&surface);
    // Trim surfaces by the fill_boundaries.
    this->fill_surfaces.surfaces.clear();
    for (size_t surface_type = 0; surface_type < size_t(stCount); ++surface_type) {
        const SurfacesPtr& this_surfaces = by_surface[surface_type];
        if (!this_surfaces.empty())
            this->fill_surfaces.append(intersection_ex(this_surfaces, this->fill_expolygons), SurfaceType(surface_type));
    }
}

// [INTENT] Instantiate PerimeterGenerator with all required inputs and dispatch to either
// Arachne (variable-width) or classic (constant-width) perimeter generation.
// Sets up generator with lower/upper layer lslices for overhang detection, upper same-region
// slices for internal bridging, and all flow values.
// [COUPLING] → PerimeterGenerator (two separate algorithms: classic and Arachne).
//             lower_layer->lslices and upper_layer->lslices accessed via raw ptr.
// [HAZARD H364] spiral_mode check uses region's bottom_shell_layers and bottom_shell_thickness
//               to determine layer threshold. This assumes raft layers are NOT counted in layer->id(),
//               which is noted as a FIXME in the code ("account for raft layers").
// [HAZARD H365] upper_slices_same_region: calls get_region(region_id) — no bounds check
//               if the upper layer has fewer regions than this layer (e.g. at object top).
void LayerRegion::make_perimeters(const SurfaceCollection& slices,
                                  const LayerRegionPtrs&   compatible_regions,
                                  SurfaceCollection*       fill_surfaces,
                                  ExPolygons*              fill_no_overlap)
{
    this->perimeters.clear();
    this->thin_fills.clear();

    const PrintConfig&       print_config  = this->layer()->object()->print()->config();
    const PrintRegionConfig& region_config = this->region().config();
    const PrintObjectConfig& object_config = this->layer()->object()->config();
    // This needs to be in sync with PrintObject::_slice() slicing_mode_normal_below_layer!
    bool spiral_mode = print_config.spiral_mode &&
                       // FIXME account for raft layers.
                       (this->layer()->id() >= size_t(region_config.bottom_shell_layers.value) &&
                        this->layer()->print_z >= region_config.bottom_shell_thickness - EPSILON);

    PerimeterGenerator g(
        // input:
        &slices, &compatible_regions, this->layer()->height, this->layer()->slice_z, this->flow(frPerimeter), &region_config,
        &this->layer()->object()->config(), &print_config, spiral_mode,

        // output:
        &this->perimeters, &this->thin_fills, fill_surfaces,
        // BBS
        fill_no_overlap);

    if (this->layer()->lower_layer != nullptr)
        // Cummulative sum of polygons over all the regions.
        g.lower_slices = &this->layer()->lower_layer->lslices;
    if (this->layer()->upper_layer != NULL)
        g.upper_slices = &this->layer()->upper_layer->lslices;

    int region_id = this->region().print_object_region_id();
    if (this->layer()->upper_layer != NULL)
        g.upper_slices_same_region = &this->layer()->upper_layer->get_region(region_id)->slices;

    g.layer_id           = (int) this->layer()->id();
    g.ext_perimeter_flow = this->flow(frExternalPerimeter);
    g.overhang_flow      = this->bridging_flow(frPerimeter, object_config.thick_bridges);
    g.solid_infill_flow  = this->flow(frSolidInfill);

    if (this->layer()->object()->config().wall_generator.value == PerimeterGeneratorType::Arachne && !spiral_mode)
        g.process_arachne();
    else
        g.process_classic();
}

#if 1

// [INTENT] Extract ExPolygons from surfaces matching any of the given surface types.
// Also captures the thickness of the last matched surface (all surfaces of same type
// should have the same thickness, but this is not enforced).
// [HAZARD H366] thickness is overwritten for each matching surface — if surfaces have
//               different thicknesses (combine_infill case), the last one wins silently.
// Extract surfaces of given type from surfaces, extract fill (layer) thickness of one of the surfaces.
static ExPolygons fill_surfaces_extract_expolygons(Surfaces& surfaces, std::initializer_list<SurfaceType> surface_types, double& thickness)
{
    size_t cnt = 0;
    for (const Surface& surface : surfaces)
        if (std::find(surface_types.begin(), surface_types.end(), surface.surface_type) != surface_types.end()) {
            ++cnt;
            thickness = surface.thickness;
        }
    if (cnt == 0)
        return {};

    ExPolygons out;
    out.reserve(cnt);
    for (Surface& surface : surfaces)
        if (std::find(surface_types.begin(), surface_types.end(), surface.surface_type) != surface_types.end())
            out.emplace_back(std::move(surface.expolygon));
    return out;
}

struct ExpansionZone
{
    ExPolygons                           expolygons;
    Algorithm::RegionExpansionParameters parameters;
    bool                                 expanded_into = false;
};

// [INTENT] Bridge struct: holds one bridge surface (expolygon), its group id for union-find
// merging of adjacent bridges, its expansion iterator for later anchor merging,
// and the detected bridging angle.
// [STATE] angle is std::nullopt until detect_bridge_directions() fills it in.
//         group_id is a union-find root: bridges with same root are merged into one.
// Cache for detecting bridge orientation and merging regions with overlapping expansions.
struct Bridge
{
    ExPolygon                                                 expolygon;
    uint32_t                                                  group_id;
    std::vector<Algorithm::RegionExpansionEx>::const_iterator bridge_expansion_begin;
    std::optional<double>                                     angle{std::nullopt};
};

// [INTENT] Union-find root query with path compression.
// Returns the canonical group id for the bridge at src_id.
// [HAZARD H367] Path compression is one-level only (sets bridges[src_id].group_id = group_id).
//               Full path compression would require a separate loop. Current impl is O(depth)
//               per query which is fine for typical bridge counts (<100).
// Group the bridge surfaces by overlaps.
uint32_t group_id(std::vector<Bridge>& bridges, uint32_t src_id)
{
    uint32_t group_id = bridges[src_id].group_id;
    while (group_id != src_id) {
        src_id   = group_id;
        group_id = bridges[src_id].group_id;
    }
    bridges[src_id].group_id = group_id;
    return group_id;
};

// [INTENT] Build a list of Bridge structs from bridge expolygons, then union-find merge
// any bridges whose expanded anchor regions overlap inside the same shell boundary.
// Each bridge starts as its own group; pairs that share overlapping anchor area get merged.
// [STATE] Consumes bridge_expolygons by move. bridge_expansions is sorted by boundary_id.
// [HAZARD H374] bridge_expansion_begin is initialized to bridge_expansions.end() for all
//               bridges at construction, then overwritten in merge_bridges(). If a bridge
//               has no expansions at all, its iterator stays at end() — caller must guard.
// [HAZARD H375] O(n²) intersection test within each boundary region group. For models with
//               many small bridge surfaces sharing one big shell region, this can be slow.
std::vector<Bridge> get_grouped_bridges(ExPolygons&& bridge_expolygons, const std::vector<Algorithm::RegionExpansionEx>& bridge_expansions)
{
    using namespace Algorithm;

    std::vector<Bridge> result;
    {
        result.reserve(bridge_expansions.size());
        uint32_t group_id = 0;
        using std::move_iterator;
        for (ExPolygon& expolygon : bridge_expolygons)
            result.push_back({std::move(expolygon), group_id++, bridge_expansions.end()});
    }

    // Detect overlaps of bridge anchors inside their respective shell regions.
    // bridge_expansions are sorted by boundary id and source id.
    for (auto expansion_iterator = bridge_expansions.begin(); expansion_iterator != bridge_expansions.end();) {
        auto boundary_region_begin = expansion_iterator;
        auto boundary_region_end = std::find_if(next(expansion_iterator), bridge_expansions.end(), [&](const RegionExpansionEx& expansion) {
            return expansion.boundary_id != expansion_iterator->boundary_id;
        });

        // Cache of bboxes per expansion boundary.
        std::vector<BoundingBox> bounding_boxes;
        bounding_boxes.reserve(std::distance(boundary_region_begin, boundary_region_end));
        std::transform(boundary_region_begin, boundary_region_end, std::back_inserter(bounding_boxes),
                       [](const RegionExpansionEx& expansion) { return get_extents(expansion.expolygon.contour); });

        // For each bridge anchor of the current source:
        for (; expansion_iterator != boundary_region_end; ++expansion_iterator) {
            auto candidate_iterator = std::next(expansion_iterator);
            for (; candidate_iterator != boundary_region_end; ++candidate_iterator) {
                const BoundingBox& current_bounding_box{bounding_boxes[expansion_iterator - boundary_region_begin]};
                const BoundingBox& candidate_bounding_box{bounding_boxes[candidate_iterator - boundary_region_begin]};
                if (expansion_iterator->src_id != candidate_iterator->src_id &&
                    current_bounding_box.overlap(candidate_bounding_box)
                    // One may ignore holes, they are irrelevant for intersection test.
                    && !intersection(expansion_iterator->expolygon.contour, candidate_iterator->expolygon.contour).empty()) {
                    // The two bridge regions intersect. Give them the same (lower) group id.
                    uint32_t id  = group_id(result, expansion_iterator->src_id);
                    uint32_t id2 = group_id(result, candidate_iterator->src_id);
                    if (id < id2)
                        result[id2].group_id = id;
                    else
                        result[id].group_id = id2;
                }
            }
        }
    }
    return result;
}

// [INTENT] For each bridge, accumulate anchor polygon areas from expansion_zones matching
// the bridge's WaveSeed boundary ids, then call detect_bridging_direction() on the
// unsupported lines (bridge contour minus anchor areas). Sets bridge.angle in radians.
// [STATE] Mutates bridges[*].angle. Consumes bridge_anchors iterator sequentially —
//         relies on bridge_anchors being sorted by src_id ascending (ensured by caller sort).
// [COUPLING] → detect_bridging_direction() (Geometry.cpp) — PCA-based span direction.
// [HAZARD H376] anchor_areas lookup uses a linear scan over expansion_zones per anchor id.
//               For many zones or many anchors this is O(zones * anchors). A prefix-sum
//               array of zone sizes would make it O(1) per lookup.
// [HAZARD H377] bridge.angle is set even when anchor_areas is empty (no supported edges).
//               detect_bridging_direction of a polygon with no lines returns a zero vector,
//               yielding angle = PI + atan2(0,0) = PI. The resulting horizontal bridge
//               direction may be wrong for unsupported islands.
void detect_bridge_directions(const Algorithm::WaveSeeds&       bridge_anchors,
                              std::vector<Bridge>&              bridges,
                              const std::vector<ExpansionZone>& expansion_zones)
{
    if (expansion_zones.empty()) {
        throw std::runtime_error("At least one expansion zone must exist!");
    }
    auto it_bridge_anchor = bridge_anchors.begin();
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++bridge_id) {
        Bridge&  bridge = bridges[bridge_id];
        Polygons anchor_areas;
        int32_t  last_anchor_id = -1;
        for (; it_bridge_anchor != bridge_anchors.end() && it_bridge_anchor->src == bridge_id; ++it_bridge_anchor) {
            if (last_anchor_id != int(it_bridge_anchor->boundary)) {
                last_anchor_id = int(it_bridge_anchor->boundary);

                unsigned start_index{};
                unsigned end_index{};
                for (const ExpansionZone& expansion_zone : expansion_zones) {
                    end_index += expansion_zone.expolygons.size();
                    if (last_anchor_id < static_cast<int64_t>(end_index)) {
                        append(anchor_areas, to_polygons(expansion_zone.expolygons[last_anchor_id - start_index]));
                        break;
                    }
                    start_index += expansion_zone.expolygons.size();
                }
            }
        }
        Lines lines{to_lines(diff_pl(to_polylines(bridge.expolygon), expand(anchor_areas, float(SCALED_EPSILON))))};
        auto [bridging_dir, unsupported_dist] = detect_bridging_direction(lines, to_polygons(bridge.expolygon));
        bridge.angle                          = M_PI + std::atan2(bridging_dir.y(), bridging_dir.x());

        if constexpr (false) {
            coordf_t    stroke_width = scale_(0.06);
            BoundingBox bbox         = get_extents(anchor_areas);
            bbox.merge(get_extents(bridge.expolygon));
            bbox.offset(scale_(1.));
            ::Slic3r::SVG svg(debug_out_path(
                                  ("bridge" + std::to_string(*bridge.angle) + "_" /* + std::to_string(this->layer()->bottom_z())*/).c_str()),
                              bbox);
            svg.draw(bridge.expolygon, "cyan");
            svg.draw(lines, "green", stroke_width);
            svg.draw(anchor_areas, "red");
        }
    }
}

// [INTENT] Merge all bridges in the same union-find group into a single Surface.
// For each group root, collects all source expolygons and their expanded anchor polygons,
// applies closing_ex() to fill small regularization gaps, then emits one stBottomBridge
// Surface with the group root's angle.
// [STATE] Mutates bridges[*].bridge_expansion_begin — stores per-bridge expansion start iterator.
//         Consumes bridges[*].expolygon by move.
// [HAZARD H378] Only the root bridge's angle is used for the entire merged group
//               ("FIXME: try to be smart and pick the best bridging angle for all?").
//               Adjacent bridges spanning different directions will use a single averaged angle,
//               potentially printing at a wrong angle for part of the merged surface.
// [HAZARD H379] assertion `bridge.angle.has_value()` uses assert(false) in release builds
//               the check is inside `if (!bridges[bridge_id].angle)` — in release the assert
//               is compiled out and execution falls through to `*bridges[bridge_id].angle`
//               which would dereference a nullopt (UB). A proper guard is needed.
Surfaces merge_bridges(std::vector<Bridge>&                             bridges,
                       const std::vector<Algorithm::RegionExpansionEx>& bridge_expansions,
                       const float                                      closing_radius)
{
    for (auto it = bridge_expansions.begin(); it != bridge_expansions.end();) {
        bridges[it->src_id].bridge_expansion_begin = it;
        uint32_t src_id                            = it->src_id;
        for (++it; it != bridge_expansions.end() && it->src_id == src_id; ++it)
            ;
    }

    Surfaces result;
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++bridge_id) {
        if (group_id(bridges, bridge_id) == bridge_id) {
            // Head of the group.
            Polygons acc;
            for (uint32_t bridge_id2 = bridge_id; bridge_id2 < uint32_t(bridges.size()); ++bridge_id2)
                if (group_id(bridges, bridge_id2) == bridge_id) {
                    append(acc, to_polygons(std::move(bridges[bridge_id2].expolygon)));
                    auto it_bridge_expansion = bridges[bridge_id2].bridge_expansion_begin;
                    assert(it_bridge_expansion == bridge_expansions.end() || it_bridge_expansion->src_id == bridge_id2);
                    for (; it_bridge_expansion != bridge_expansions.end() && it_bridge_expansion->src_id == bridge_id2;
                         ++it_bridge_expansion)
                        append(acc, to_polygons(it_bridge_expansion->expolygon));
                }
            // FIXME try to be smart and pick the best bridging angle for all?
            if (!bridges[bridge_id].angle) {
                assert(false && "Bridge angle must be pre-calculated!");
            }
            Surface templ{stBottomBridge, {}};
            templ.bridge_angle = bridges[bridge_id].angle ? *bridges[bridge_id].angle : -1;
            // NOTE: The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
            //  without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
            //  look for narrow_ensure_vertical_wall_thickness_region_radius filter.
            ExPolygons final = closing_ex(acc, closing_radius);
            // without safety offset, artifacts are generated (GH #2494)
            // union_safety_offset_ex(acc)
            for (ExPolygon& ex : final)
                result.emplace_back(templ, std::move(ex));
        }
    }
    return result;
}

struct ExpansionResult
{
    Algorithm::WaveSeeds                      anchors;
    std::vector<Algorithm::RegionExpansionEx> expansions;
};

// [INTENT] Expand a set of expolygons into each ExpansionZone using wave propagation.
// Returns all anchor seeds (WaveSeeds) and the expanded regions (RegionExpansionEx) across
// all zones. Boundary ids are offset by processed_bridges_count to keep them globally unique
// across zones.
// [COUPLING] → Algorithm::wave_seeds(), propagate_waves_ex() — the core iterative dilation.
// [STATE] Mutates expansion_zone.expanded_into flag per zone.
// [HAZARD H380] processed_bridges_count increments by expansion_zone.expolygons.size() AFTER
//               the zone runs. If expansion modifies expolygons.size() during propagate_waves_ex,
//               the offset would be wrong. Currently expolygons are not modified here, but
//               any future in-place pruning would silently corrupt boundary ids.
ExpansionResult expand_expolygons(const ExPolygons& expolygons, std::vector<ExpansionZone>& expansion_zones)
{
    using namespace Algorithm;
    WaveSeeds                      bridge_anchors;
    std::vector<RegionExpansionEx> bridge_expansions;

    unsigned processed_bridges_count = 0;
    for (ExpansionZone& expansion_zone : expansion_zones) {
        WaveSeeds seeds{wave_seeds(expolygons, expansion_zone.expolygons, expansion_zone.parameters.tiny_expansion, true)};
        std::vector<RegionExpansionEx> expansions{propagate_waves_ex(seeds, expansion_zone.expolygons, expansion_zone.parameters)};

        for (WaveSeed& seed : seeds)
            seed.boundary += processed_bridges_count;
        for (RegionExpansionEx& expansion : expansions)
            expansion.boundary_id += processed_bridges_count;

        expansion_zone.expanded_into = !expansions.empty();

        append(bridge_anchors, std::move(seeds));
        append(bridge_expansions, std::move(expansions));

        processed_bridges_count += expansion_zone.expolygons.size();
    }
    return {bridge_anchors, bridge_expansions};
}

// [INTENT] Top-level bridge surface processing pipeline (auto-angle mode):
//   1. Extract bridge expolygons (stBottomBridge) from surfaces.
//   2. Expand into expansion_zones via expand_expolygons() (wave propagation).
//   3. Group overlapping bridge anchors with get_grouped_bridges().
//   4. Detect per-bridge span direction with detect_bridge_directions().
//   5. Merge groups into final Surfaces with merge_bridges().
//   6. Clip remaining expansion_zones by the produced bridge surfaces.
// Returns the merged bridge Surfaces, leaving expansion_zones trimmed.
// [COUPLING] → get_grouped_bridges, detect_bridge_directions, merge_bridges (all in this TU).
// [HAZARD H381] If bridge_expolygons is empty, returns {} early — expansion_zones are NOT
//               clipped in this path. This is correct (no bridges = no clipping needed).
// [HAZARD H382] expand_expolygons moves bridge_expolygons into bridges via get_grouped_bridges,
//               but the original bridge_expolygons variable is cleared after — double-move
//               risk if any future refactor tries to reuse it post-call.
// Extract bridging surfaces from "surfaces", expand them into "shells" using expansion_params,
// detect bridges.
// Trim "shells" by the expanded bridges.
Surfaces expand_bridges_detect_orientations(Surfaces& surfaces, std::vector<ExpansionZone>& expansion_zones, const float closing_radius)
{
    using namespace Slic3r::Algorithm;

    double     thickness;
    ExPolygons bridge_expolygons = fill_surfaces_extract_expolygons(surfaces, {stBottomBridge}, thickness);
    if (bridge_expolygons.empty())
        return {};

    // Calculate bridge anchors and their expansions in their respective shell region.
    ExpansionResult expansion_result{expand_expolygons(bridge_expolygons, expansion_zones)};

    std::vector<Bridge> bridges{get_grouped_bridges(std::move(bridge_expolygons), expansion_result.expansions)};
    bridge_expolygons.clear();

    std::sort(expansion_result.anchors.begin(), expansion_result.anchors.end(), Algorithm::lower_by_src_and_boundary);
    detect_bridge_directions(expansion_result.anchors, bridges, expansion_zones);

    // Merge the groups with the same group id, produce surfaces by merging source overhangs with their newly expanded anchors.
    std::sort(expansion_result.expansions.begin(), expansion_result.expansions.end(),
              [](auto& l, auto& r) { return l.src_id < r.src_id || (l.src_id == r.src_id && l.boundary_id < r.boundary_id); });
    Surfaces out{merge_bridges(bridges, expansion_result.expansions, closing_radius)};

    // Clip by the expanded bridges.
    for (ExpansionZone& expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, out);
    return out;
}

// [INTENT] Generic surface expansion helper for non-bridge surfaces (top, bottom, or
// bridges with a fixed custom angle). Extracts surfaces of surface_type from surfaces,
// propagates wave expansions into expansion_zones, merges into ExPolygons using
// merge_expansions_into_expolygons(), applies closing_ex() for gap filling, and trims
// expansion_zones by the result. Returns merged Surfaces with the given surface_type.
// [STATE] Mutates expansion_zone.expolygons and expansion_zone.expanded_into in-place.
// [COUPLING] → Algorithm::propagate_waves(), merge_expansions_into_expolygons(), closing_ex().
// [HAZARD H383] bridge_angle parameter defaults to -1 (meaning "no bridge angle").
//               The Surface template uses this directly: templ.bridge_angle = bridge_angle.
//               A value of -1 is not a valid angle but is used as a sentinel throughout the
//               codebase. Any refactor must preserve this sentinel convention.
// [HAZARD H384] closing_radius absorbs "small unassigned regions" from regularization.
//               Comment references "narrow_ensure_vertical_wall_thickness_region_radius".
//               If closing_radius > half of a narrow wall, that wall's fill region disappears.
Surfaces expand_merge_surfaces(Surfaces&                   surfaces,
                               SurfaceType                 surface_type,
                               std::vector<ExpansionZone>& expansion_zones,
                               const float                 closing_radius,
                               const double                bridge_angle = -1)
{
    using namespace Slic3r::Algorithm;

    double     thickness;
    ExPolygons src = fill_surfaces_extract_expolygons(surfaces, {surface_type}, thickness);
    if (src.empty())
        return {};

    unsigned                     processed_expolygons_count = 0;
    std::vector<RegionExpansion> expansions;
    for (ExpansionZone& expansion_zone : expansion_zones) {
        std::vector<RegionExpansion> zone_expansions = propagate_waves(src, expansion_zone.expolygons, expansion_zone.parameters);
        expansion_zone.expanded_into                 = !zone_expansions.empty();

        for (RegionExpansion& expansion : zone_expansions)
            expansion.boundary_id += processed_expolygons_count;

        processed_expolygons_count += expansion_zone.expolygons.size();
        append(expansions, std::move(zone_expansions));
    }

    std::vector<ExPolygon> expanded = merge_expansions_into_expolygons(std::move(src), std::move(expansions));
    // NOTE: The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
    //  without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
    //  look for narrow_ensure_vertical_wall_thickness_region_radius filter.
    expanded = closing_ex(expanded, closing_radius);
    // Trim the zones by the expanded expolygons.
    for (ExpansionZone& expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, expanded);

    Surface templ{surface_type, {}};
    templ.bridge_angle = bridge_angle;
    Surfaces out;
    out.reserve(expanded.size());
    for (auto& expoly : expanded)
        out.emplace_back(templ, std::move(expoly));
    return out;
}

// [INTENT] Main external surface pipeline (active #if 1 path):
// Expands bridge/top/bottom surfaces into adjacent shell regions using wave propagation.
// Algorithm:
//   1. Compute shell_width from perimeter flow and loop count.
//   2. Extract shells (stInternalSolid), sparse (stInternal), top expolygons from fill_surfaces.
//   3. Detect bridges (stBottomBridge) with auto or custom angle, expand into shells/sparse.
//   4. Expand top surfaces (stTop) into shells/sparse/top zones.
//   5. Pop top expansion zone, then expand bottom (stBottom) into shells/sparse.
//   6. Convert small sparse regions to solid if below minimum_sparse_infill_area.
//   7. Rebuild fill_surfaces from remaining zones + bridges + bottoms + tops.
//
// [COUPLING] → Algorithm::RegionExpansion (wave_seeds + propagate_waves_ex),
//             → Geometry::deg2rad (custom bridge angle),
//             → ClipperUtils (closing_ex, union_ex, diff_ex, intersection_ex).
//
// [HAZARD H368] expansion_zones are mutated in-place by expand_*() helpers — each
//               expansion pass clips the zones by the newly expanded surfaces. Order matters:
//               bridges first, then top, then bottom. Reordering changes output geometry.
// [HAZARD H369] closing_radius formula: 0.55 * 0.65 * 1.05 * solid_infill_spacing.
//               Magic constants absorbing regularization artifacts ("benchy holes").
//               Must be re-derived for any change to the perimeter regularization algorithm.
// [HAZARD H370] expansion_zones.pop_back() removes the top zone after expanding tops into it,
//               so subsequent bottom expansion does not pollute the top zone. This implicit
//               ordering is not documented and will break if expansion_zones order changes.
void LayerRegion::process_external_surfaces(const Layer* lower_layer, const Polygons* lower_layer_covered)
{
    using namespace Slic3r::Algorithm;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Width of the perimeters.
    float shell_width   = 0;
    float expansion_min = 0;
    if (int num_perimeters = this->region().config().wall_loops; num_perimeters > 0) {
        Flow external_perimeter_flow = this->flow(frExternalPerimeter);
        Flow perimeter_flow          = this->flow(frPerimeter);
        shell_width                  = 0.5f * external_perimeter_flow.scaled_width() + external_perimeter_flow.scaled_spacing();
        shell_width += perimeter_flow.scaled_spacing() * (num_perimeters - 1);
        expansion_min = perimeter_flow.scaled_spacing();
    } else {
        // TODO: Maybe there is better solution when printing with zero perimeters, but this works reasonably well, given the situation
        shell_width   = float(SCALED_EPSILON);
        expansion_min = float(SCALED_EPSILON);
        ;
    }

    // Scaled expansions of the respective external surfaces.
    float expansion_top           = shell_width * sqrt(2.);
    float expansion_bottom        = expansion_top;
    float expansion_bottom_bridge = expansion_top;
    // Expand by waves of expansion_step size (expansion_step is scaled), but with no more steps than max_nr_expansion_steps.
    const float expansion_step = scaled<float>(0.1);
    // Don't take more than max_nr_steps for small expansion_step.
    static constexpr const size_t max_nr_expansion_steps = 5;
    // Radius (with added epsilon) to absorb empty regions emering from regularization of ensuring, viz  const float
    // narrow_ensure_vertical_wall_thickness_region_radius = 0.5f * 0.65f * min_perimeter_infill_spacing;
    const float closing_radius = 0.55f * 0.65f * 1.05f * this->flow(frSolidInfill).scaled_spacing();

    // Expand the top / bottom / bridge surfaces into the shell thickness solid infills.
    double     layer_thickness;
    ExPolygons shells = union_ex(fill_surfaces_extract_expolygons(this->fill_surfaces.surfaces, {stInternalSolid}, layer_thickness));
    ExPolygons sparse = union_ex(fill_surfaces_extract_expolygons(this->fill_surfaces.surfaces, {stInternal}, layer_thickness));
    ExPolygons top_expolygons = union_ex(fill_surfaces_extract_expolygons(this->fill_surfaces.surfaces, {stTop}, layer_thickness));
    const auto expansion_params_into_sparse_infill = RegionExpansionParameters::build(expansion_min, expansion_step, max_nr_expansion_steps);
    const auto expansion_params_into_solid_infill = RegionExpansionParameters::build(expansion_bottom_bridge, expansion_step,
                                                                                     max_nr_expansion_steps);

    std::vector<ExpansionZone> expansion_zones{
        ExpansionZone{std::move(shells), expansion_params_into_solid_infill},
        ExpansionZone{std::move(sparse), expansion_params_into_sparse_infill},
        ExpansionZone{std::move(top_expolygons), expansion_params_into_solid_infill},
    };

    SurfaceCollection bridges;
    {
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z;
        const double custom_angle = this->region().config().bridge_angle.value;
        bridges.surfaces          = custom_angle > 0 ?
                                        expand_merge_surfaces(this->fill_surfaces.surfaces, stBottomBridge, expansion_zones, closing_radius,
                                                              Geometry::deg2rad(custom_angle)) :
                                        expand_bridges_detect_orientations(this->fill_surfaces.surfaces, expansion_zones, closing_radius);
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun++).c_str(), true);
        }
#endif
    }

    this->fill_surfaces.remove_types({stTop});
    {
        Surface top_templ(stTop, {});
        top_templ.thickness = layer_thickness;
        this->fill_surfaces.append(std::move(expansion_zones.back().expolygons), top_templ);
    }

    expansion_zones.pop_back();

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_bottom, expansion_step, max_nr_expansion_steps);
    Surfaces bottoms                 = expand_merge_surfaces(this->fill_surfaces.surfaces, stBottom, expansion_zones, closing_radius);

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_top, expansion_step, max_nr_expansion_steps);
    Surfaces tops                    = expand_merge_surfaces(this->fill_surfaces.surfaces, stTop, expansion_zones, closing_radius);

    // turn too small internal regions into solid regions according to the user setting
    if (!this->layer()->object()->print()->config().spiral_mode && this->region().config().sparse_infill_density.value > 0) {
        // scaling an area requires two calls!
        double     min_area = scale_(scale_(this->region().config().minimum_sparse_infill_area.value));
        ExPolygons small_regions{};
        expansion_zones[1].expolygons.erase(std::remove_if(expansion_zones[1].expolygons.begin(), expansion_zones[1].expolygons.end(),
                                                           [min_area, &small_regions](ExPolygon& ex_polygon) {
                                                               if (ex_polygon.area() <= min_area) {
                                                                   small_regions.push_back(ex_polygon);
                                                                   return true;
                                                               }
                                                               return false;
                                                           }),
                                            expansion_zones[1].expolygons.end());

        if (!small_regions.empty()) {
            expansion_zones[0].expolygons = union_ex(expansion_zones[0].expolygons, small_regions);
        }
    }

    //    this->fill_surfaces.remove_types({ stBottomBridge, stBottom, stTop, stInternal, stInternalSolid });
    this->fill_surfaces.clear();
    unsigned zones_expolygons_count = 0;
    for (const ExpansionZone& zone : expansion_zones)
        zones_expolygons_count += zone.expolygons.size();
    reserve_more(this->fill_surfaces.surfaces, zones_expolygons_count + bridges.size() + bottoms.size() + tops.size());
    {
        Surface solid_templ(stInternalSolid, {});
        solid_templ.thickness = layer_thickness;
        this->fill_surfaces.append(std::move(expansion_zones[0].expolygons), solid_templ);
    }
    {
        Surface sparse_templ(stInternal, {});
        sparse_templ.thickness = layer_thickness;
        this->fill_surfaces.append(std::move(expansion_zones[1].expolygons), sparse_templ);
    }
    this->fill_surfaces.append(std::move(bridges.surfaces));
    this->fill_surfaces.append(std::move(bottoms));
    this->fill_surfaces.append(std::move(tops));

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}
#else

// [INTENT] LEGACY (disabled) process_external_surfaces implementation — old BridgeDetector-based
// algorithm. Active when `#if 1` at top is changed to `#if 0`. Kept for reference and rollback.
// Key differences from the active algorithm:
//   - Uses BridgeDetector (classic angle sweep) rather than detect_bridging_direction() (PCA).
//   - Expands top/bottom with fixed margin offsets instead of wave propagation.
//   - Groups bridge regions using simple bounding-box + polygon intersection (O(n²)).
//   - Clips top surfaces against bottom_polygons to give bottom priority.
// [HAZARD H390] BBS added nozzle_diameter-scaled bridge_margin: the original PrusaSlicer code
//               used a fixed 3mm BRIDGE_INFILL_MARGIN. The BBS scaling ties margin to nozzle size
//               which requires nozzle_dmr_avg() — a cross-extruder average. Multi-nozzle setups
//               may produce incorrect margins if nozzles differ significantly.
// [HAZARD H391] fill_boundaries is computed from fill_expolygons at function start but then
//               destructively consumed into top/internal vectors via move semantics. Any second
//               call to process_external_surfaces (non-idempotent path) would use empty boundaries.
// #define EXTERNAL_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 3.
// #define EXTERNAL_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 1.5
#define EXTERNAL_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.

void LayerRegion::process_external_surfaces(const Layer* lower_layer, const Polygons* lower_layer_covered)
{
    const bool has_infill = this->region().config().sparse_infill_density.value > 0.;
    // BBS
    auto        nozzle_diameter = this->region().nozzle_dmr_avg(this->layer()->object()->print()->config());
    const float margin          = float(scale_(EXTERNAL_INFILL_MARGIN));
    const float bridge_margin = std::min(float(scale_(BRIDGE_INFILL_MARGIN)), float(scale_(nozzle_diameter * BRIDGE_INFILL_MARGIN / 0.4)));

    // BBS
    const PrintObjectConfig& object_config = this->layer()->object()->config();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("3_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // 1) Collect bottom and bridge surfaces, each of them grown by a fixed 3mm offset
    // for better anchoring.
    // Bottom surfaces, grown.
    Surfaces bottom;
    // Bridge surfaces, initialy not grown.
    Surfaces bridges;
    // Top surfaces, grown.
    Surfaces top;
    // Internal surfaces, not grown.
    Surfaces internal;
    // Areas, where an infill of various types (top, bottom, bottom bride, sparse, void) could be placed.
    Polygons fill_boundaries = to_polygons(this->fill_expolygons);
    Polygons lower_layer_covered_tmp;

    // Collect top surfaces and internal surfaces.
    // Collect fill_boundaries: If we're slicing with no infill, we can't extend external surfaces over non-existent infill.
    // This loop destroys the surfaces (aliasing this->fill_surfaces.surfaces) by moving into top/internal/fill_boundaries!

    {
        // Voids are sparse infills if infill rate is zero.
        Polygons voids;

        double max_grid_area = -1;
        if (this->layer()->lower_layer != nullptr)
            max_grid_area = this->layer()->lower_layer->get_sparse_infill_max_void_area();
        for (const Surface& surface : this->fill_surfaces.surfaces) {
            if (surface.is_top()) {
                // Collect the top surfaces, inflate them and trim them by the bottom surfaces.
                // This gives the priority to bottom surfaces.
                if (max_grid_area < 0 || surface.expolygon.area() < max_grid_area)
                    surfaces_append(top, offset_ex(surface.expolygon, margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS), surface);
                else
                    // BBS: Don't need to expand too much in this situation. Expand 3mm to eliminate hole and 1mm for contour
                    surfaces_append(top,
                                    intersection_ex(offset(surface.expolygon.contour, margin / 3.0, EXTERNAL_SURFACES_OFFSET_PARAMETERS),
                                                    offset_ex(surface.expolygon, margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS)),
                                    surface);
            } else if (surface.surface_type == stBottom || (surface.surface_type == stBottomBridge && lower_layer == nullptr)) {
                // Grown by 3mm.
                surfaces_append(bottom, offset_ex(surface.expolygon, margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS), surface);
            } else if (surface.surface_type == stBottomBridge) {
                if (!surface.empty())
                    bridges.emplace_back(surface);
            }
            if (surface.is_internal()) {
                assert(surface.surface_type == stInternal || surface.surface_type == stInternalSolid);
                if (!has_infill && lower_layer != nullptr)
                    polygons_append(voids, surface.expolygon);
                internal.emplace_back(std::move(surface));
            }
        }
        if (!has_infill && lower_layer != nullptr && !voids.empty()) {
            // Remove voids from fill_boundaries, that are not supported by the layer below.
            if (lower_layer_covered == nullptr) {
                lower_layer_covered     = &lower_layer_covered_tmp;
                lower_layer_covered_tmp = to_polygons(lower_layer->lslices);
            }
            if (!lower_layer_covered->empty())
                voids = diff(voids, *lower_layer_covered);
            fill_boundaries = diff(fill_boundaries, voids);
        }
    }

#if 0
    {
        static int iRun = 0;
        bridges.export_to_svg(debug_out_path("bridges-before-grouping-%d.svg", iRun ++), true);
    }
#endif

    if (bridges.empty()) {
        fill_boundaries = union_safety_offset(fill_boundaries);
    } else {
        // 1) Calculate the inflated bridge regions, each constrained to its island.
        ExPolygons               fill_boundaries_ex = union_safety_offset_ex(fill_boundaries);
        std::vector<Polygons>    bridges_grown;
        std::vector<BoundingBox> bridge_bboxes;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        {
            static int iRun = 0;
            SVG svg(debug_out_path("3_process_external_surfaces-fill_regions-%d.svg", iRun++).c_str(), get_extents(fill_boundaries_ex));
            svg.draw(fill_boundaries_ex);
            svg.draw_outline(fill_boundaries_ex, "black", "blue", scale_(0.05));
            svg.Close();
        }

//        export_region_fill_surfaces_to_svg_debug("3_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

        {
            // Bridge expolygons, grown, to be tested for intersection with other bridge regions.
            std::vector<BoundingBox> fill_boundaries_ex_bboxes = get_extents_vector(fill_boundaries_ex);
            bridges_grown.reserve(bridges.size());
            bridge_bboxes.reserve(bridges.size());
            for (size_t i = 0; i < bridges.size(); ++i) {
                // Find the island of this bridge.
                const Point pt         = bridges[i].expolygon.contour.points.front();
                int         idx_island = -1;
                for (int j = 0; j < int(fill_boundaries_ex.size()); ++j)
                    if (fill_boundaries_ex_bboxes[j].contains(pt) && fill_boundaries_ex[j].contains(pt)) {
                        idx_island = j;
                        break;
                    }
                // Grown by 3mm.
                // BBS: eliminate too narrow area to avoid generating bridge on top layer when wall loop is 1
                // Polygons polys = offset(bridges[i].expolygon, bridge_margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS);
                Polygons polys = offset2({bridges[i].expolygon}, -scale_(nozzle_diameter * 0.1), bridge_margin,
                                         EXTERNAL_SURFACES_OFFSET_PARAMETERS);
                if (idx_island == -1) {
                    BOOST_LOG_TRIVIAL(trace) << "Bridge did not fall into the source region!";
                } else {
                    // Found an island, to which this bridge region belongs. Trim it,
                    polys = intersection(polys, fill_boundaries_ex[idx_island]);
                }
                bridge_bboxes.push_back(get_extents(polys));
                bridges_grown.push_back(std::move(polys));
            }
        }

        // 2) Group the bridge surfaces by overlaps.
        std::vector<size_t> bridge_group(bridges.size(), (size_t) -1);
        size_t              n_groups = 0;
        for (size_t i = 0; i < bridges.size(); ++i) {
            // A grup id for this bridge.
            size_t group_id = (bridge_group[i] == size_t(-1)) ? (n_groups++) : bridge_group[i];
            bridge_group[i] = group_id;
            // For all possibly overlaping bridges:
            for (size_t j = i + 1; j < bridges.size(); ++j) {
                if (!bridge_bboxes[i].overlap(bridge_bboxes[j]))
                    continue;
                if (intersection(bridges_grown[i], bridges_grown[j]).empty())
                    continue;
                // The two bridge regions intersect. Give them the same group id.
                if (bridge_group[j] != size_t(-1)) {
                    // The j'th bridge has been merged with some other bridge before.
                    size_t group_id_new = bridge_group[j];
                    for (size_t k = 0; k < j; ++k)
                        if (bridge_group[k] == group_id)
                            bridge_group[k] = group_id_new;
                    group_id = group_id_new;
                }
                bridge_group[j] = group_id;
            }
        }

        // 3) Merge the groups with the same group id, detect bridges.
        {
            BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z
                                     << ", bridge groups: " << n_groups;
            for (size_t group_id = 0; group_id < n_groups; ++group_id) {
                size_t n_bridges_merged = 0;
                size_t idx_last         = (size_t) -1;
                for (size_t i = 0; i < bridges.size(); ++i) {
                    if (bridge_group[i] == group_id) {
                        ++n_bridges_merged;
                        idx_last = i;
                    }
                }
                if (n_bridges_merged == 0)
                    // This group has no regions assigned as these were moved into another group.
                    continue;
                // Collect the initial ungrown regions and the grown polygons.
                ExPolygons initial;
                Polygons   grown;
                for (size_t i = 0; i < bridges.size(); ++i) {
                    if (bridge_group[i] != group_id)
                        continue;
                    initial.push_back(std::move(bridges[i].expolygon));
                    polygons_append(grown, bridges_grown[i]);
                }
                // detect bridge direction before merging grown surfaces otherwise adjacent bridges
                // would get merged into a single one while they need different directions
                // also, supply the original expolygon instead of the grown one, because in case
                // of very thin (but still working) anchors, the grown expolygon would go beyond them
                double custom_angle = Geometry::deg2rad(this->region().config().bridge_angle.value);
                if (custom_angle > 0.0) {
                    bridges[idx_last].bridge_angle = custom_angle;
                } else {
                    auto [bridging_dir, unsupported_dist] = detect_bridging_direction(to_polygons(initial),
                                                                                      to_polygons(lower_layer->lslices));
                    bridges[idx_last].bridge_angle        = PI + std::atan2(bridging_dir.y(), bridging_dir.x());
                }

                /*
                BridgeDetector bd(initial, lower_layer->lslices, this->bridging_flow(frInfill,
object_config.thick_bridges).scaled_width()); #ifdef SLIC3R_DEBUG printf("Processing bridge at layer %zu:\n", this->layer()->id());
                #endif
                //BBS: use 0 as custom angle to enable auto detection all the time
                double custom_angle = Geometry::deg2rad(this->region().config().bridge_angle.value);
                if(custom_angle > 0)
                        bridges[idx_last].bridge_angle = custom_angle;
                else if (bd.detect_angle(custom_angle)) {
                    bridges[idx_last].bridge_angle = bd.angle;
                    if (this->layer()->object()->has_support()) {
//                        polygons_append(this->bridged, bd.coverage());
                        append(this->unsupported_bridge_edges, bd.unsupported_edges());
                    }
                } else if (custom_angle > 0) {
                    // Bridge was not detected (likely it is only supported at one side). Still it is a surface filled in
                    // using a bridging flow, therefore it makes sense to respect the custom bridging direction.
                    bridges[idx_last].bridge_angle = custom_angle;
                }
                */
                // without safety offset, artifacts are generated (GH #2494)
                surfaces_append(bottom, union_safety_offset_ex(grown), bridges[idx_last]);
            }

            fill_boundaries = to_polygons(fill_boundaries_ex);
            BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";
        }

#if 0
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun ++), true);
        }
#endif
    }

    Surfaces new_surfaces;
    {
        // Merge top and bottom in a single collection.
        surfaces_append(top, std::move(bottom));
        // Intersect the grown surfaces with the actual fill boundaries.
        Polygons bottom_polygons = to_polygons(bottom);
        for (size_t i = 0; i < top.size(); ++i) {
            Surface& s1 = top[i];
            if (s1.empty())
                continue;
            Polygons polys;
            polygons_append(polys, to_polygons(std::move(s1)));
            for (size_t j = i + 1; j < top.size(); ++j) {
                Surface& s2 = top[j];
                if (!s2.empty() && surfaces_could_merge(s1, s2)) {
                    polygons_append(polys, to_polygons(std::move(s2)));
                    s2.clear();
                }
            }
            if (s1.is_top())
                // Trim the top surfaces by the bottom surfaces. This gives the priority to the bottom surfaces.
                polys = diff(polys, bottom_polygons);
            surfaces_append(new_surfaces,
                            // Don't use a safety offset as fill_boundaries were already united using the safety offset.
                            intersection_ex(polys, fill_boundaries), s1);
        }
    }

    // Subtract the new top surfaces from the other non-top surfaces and re-add them.
    Polygons new_polygons = to_polygons(new_surfaces);
    for (size_t i = 0; i < internal.size(); ++i) {
        Surface& s1 = internal[i];
        if (s1.empty())
            continue;
        Polygons polys;
        polygons_append(polys, to_polygons(std::move(s1)));
        for (size_t j = i + 1; j < internal.size(); ++j) {
            Surface& s2 = internal[j];
            if (!s2.empty() && surfaces_could_merge(s1, s2)) {
                polygons_append(polys, to_polygons(std::move(s2)));
                s2.clear();
            }
        }
        ExPolygons new_expolys = diff_ex(polys, new_polygons);
        polygons_append(new_polygons, to_polygons(new_expolys));
        surfaces_append(new_surfaces, std::move(new_expolys), s1);
    }

    this->fill_surfaces.surfaces = std::move(new_surfaces);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("3_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}
#endif

// [INTENT] Retype fill_surfaces surface types based on configuration constraints.
// Converts top/bottom surfaces to internal if their shell layer counts are zero.
// Converts sparse infill to solid if density is 100%.
// [STATE] Mutates fill_surfaces.surface_type in-place; does NOT change geometry.
// [HAZARD H371] BBS changes: infill_only_where_needed is now a static PrintObject constant
//               rather than a per-object config option. Refactoring must keep this consistent.
// [HAZARD H372] psPrepareInfill idempotency relies on fill_surfaces BOUNDARIES never being
//               altered here — only types change. Any code that modifies geometry here breaks
//               the pipeline's ability to re-run prepare_infill without re-running perimeters.
void LayerRegion::prepare_fill_surfaces()
{
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-initial");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    /*  Note: in order to make the psPrepareInfill step idempotent, we should never
        alter fill_surfaces boundaries on which our idempotency relies since that's
        the only meaningful information returned by psPerimeters. */

    bool spiral_mode = this->layer()->object()->print()->config().spiral_mode;

    // if no solid layers are requested, turn top/bottom surfaces to internal
    if (!spiral_mode && this->region().config().top_shell_layers == 0) {
        for (Surface& surface : this->fill_surfaces.surfaces)
            if (surface.is_top())
                // BBS
                // surface.surface_type = this->layer()->object()->config().infill_only_where_needed ? stInternalVoid : stInternal;
                surface.surface_type = PrintObject::infill_only_where_needed ? stInternalVoid : stInternal;
    }
    if (this->region().config().bottom_shell_layers == 0) {
        for (Surface& surface : this->fill_surfaces.surfaces)
            if (surface.is_bottom()) // (surface.surface_type == stBottom)
                surface.surface_type = stInternal;
    }

    if (!spiral_mode && fabs(this->region().config().sparse_infill_density.value - 100.) < EPSILON) {
        // Turn all internal sparse infill into solid infill, if sparse_infill_density is 100%
        for (Surface& surface : this->fill_surfaces.surfaces)
            if (surface.surface_type == stInternal)
                surface.surface_type = stInternalSolid;
    }

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-final");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

// [INTENT] Minimum fill area threshold: (solid infill spacing)² — if a fill region is
// smaller than this, it is too narrow to fill reliably and should be discarded or treated
// as a thin fill.
double LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss * ss;
}

// [INTENT] Trim this layer region's slices to the given polygon set (used during elephant
// foot compensation multi-pass). Asserts all slices are stInternal before trimming.
// [STATE] Replaces slices in-place with intersection result, all typed stInternal.
void LayerRegion::trim_surfaces(const Polygons& trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface& surface : this->slices.surfaces)
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    this->slices.set(intersection_ex(this->slices.surfaces, trimming_polygons), stInternal);
}

// [INTENT] One step of elephant foot compensation: grows the slice by the complement of
// an opening, then unions with the intersection. This progressively reduces the base
// flare while preserving internal geometry.
// [HAZARD H373] opening() uses elephant_foot_compensation_perimeter_step as morphological
//               radius. If this is larger than the narrowest wall, the wall disappears.
//               No minimum width guard is enforced here.
void LayerRegion::elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step, const Polygons& trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface& surface : this->slices.surfaces)
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    Polygons tmp = intersection(this->slices.surfaces, trimming_polygons);
    append(tmp, diff(this->slices.surfaces, opening(this->slices.surfaces, elephant_foot_compensation_perimeter_step)));
    this->slices.set(union_ex(tmp), stInternal);
}

// [INTENT] Render slices and fill_surfaces to an SVG file for visual debugging.
// Draws slices as filled, semi-transparent polygons colored by surface type.
// Draws fill_surface outlines over them in the same type color.
// Adds a legend box below the drawing area.
// [CONCURRENCY] Uses a static iRun counter — NOT thread-safe if called from parallel regions.
void LayerRegion::export_region_slices_to_svg(const char* path) const
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG         svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        svg.draw(surface->expolygon.lines(), surface_type_to_color_name(surface->surface_type));
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// [INTENT] Convenience wrapper: exports to a sequentially numbered SVG file in out/.
// Uses a static map keyed by debug name to maintain per-name counters.
// [HAZARD H385] Static idx_map is NOT thread-safe. Multiple threads calling the same
//               debug name concurrently will race on the map and counter. Debug-only path,
//               but unsafe in any parallelized debug build.
// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_slices_to_svg_debug(const char* name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t&                              idx = idx_map[name];
    this->export_region_slices_to_svg(debug_out_path("LayerRegion-slices-%s-%d.svg", name, idx++).c_str());
}

// [INTENT] Same as export_region_slices_to_svg but for fill_surfaces.
// Draws filled + outlined polygons from fill_surfaces (not slices).
// [HAZARD H385] Same static map thread-safety issue as export_region_slices_to_svg_debug.
void LayerRegion::export_region_fill_surfaces_to_svg(const char* path) const
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG         svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface& surface : this->fill_surfaces.surfaces) {
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
        svg.draw_outline(surface.expolygon, "black", "blue", scale_(0.05));
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_fill_surfaces_to_svg_debug(const char* name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t&                              idx = idx_map[name];
    this->export_region_fill_surfaces_to_svg(debug_out_path("LayerRegion-fill_surfaces-%s-%d.svg", name, idx++).c_str());
}

// [INTENT] Recursive dispatcher: walk an ExtrusionEntityCollection tree and simplify each
// leaf entity (path, multipath, loop) according to its concrete type. Throws on unknown type.
// [COUPLING] → simplify_path, simplify_multi_path, simplify_loop (all in this TU).
// [HAZARD H386] Dynamic casts on every entity — O(n_entities) casts per collection level.
//               For deep nesting this recurses. No depth guard; deeply nested collections
//               (unusual but possible from Arachne output) could overflow the stack.
// [HAZARD H387] Unknown entity type throws Slic3r::InvalidArgument at runtime rather than
//               at compile time. A visitor pattern or variant would catch new entity types
//               at compile time during a refactor.
void LayerRegion::simplify_entity_collection(ExtrusionEntityCollection* entity_collection)
{
    for (size_t i = 0; i < entity_collection->entities.size(); i++) {
        if (ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(entity_collection->entities[i]))
            this->simplify_entity_collection(collection);
        else if (ExtrusionPath* path = dynamic_cast<ExtrusionPath*>(entity_collection->entities[i]))
            this->simplify_path(path);
        else if (ExtrusionMultiPath* multipath = dynamic_cast<ExtrusionMultiPath*>(entity_collection->entities[i]))
            this->simplify_multi_path(multipath);
        else if (ExtrusionLoop* loop = dynamic_cast<ExtrusionLoop*>(entity_collection->entities[i]))
            this->simplify_loop(loop);
        else
            throw Slic3r::InvalidArgument("Invalid extrusion entity supplied to simplify_entity_collection()");
    }
}

// [INTENT] Simplify a single ExtrusionPath using either arc-fitting (GCode arc moves)
// or Douglas-Peucker polyline reduction, chosen by print config flags.
// Sparse infill uses a larger resolution constant (SCALED_SPARSE_INFILL_RESOLUTION) to
// avoid excessive arc-fitting on hatching lines that are already straight.
// [COUPLING] → ExtrusionPath::simplify_by_fitting_arc(), ExtrusionPath::simplify().
// [HAZARD H388] print_config is fetched by value (copy) on every call. For large collections
//               this copies the entire PrintConfig struct per path. Should be cached at the
//               collection level and passed in, or stored in a local variable in the caller.
void LayerRegion::simplify_path(ExtrusionPath* path)
{
    const auto print_config       = this->layer()->object()->print()->config();
    const bool spiral_mode        = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution  = scaled<double>(print_config.resolution.value);

    if (enable_arc_fitting && !spiral_mode) {
        if (path->role() == erInternalInfill)
            path->simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
        else
            path->simplify_by_fitting_arc(scaled_resolution);
    } else {
        path->simplify(scaled_resolution);
    }
}

// [INTENT] Same logic as simplify_path but iterates over all paths inside a MultiPath
// (a sequence of connected segments forming a single extrusion move with possible gaps).
// [HAZARD H388] Same PrintConfig copy-per-call issue as simplify_path.
void LayerRegion::simplify_multi_path(ExtrusionMultiPath* multipath)
{
    const auto print_config       = this->layer()->object()->print()->config();
    const bool spiral_mode        = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution  = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < multipath->paths.size(); ++i) {
        if (enable_arc_fitting && !spiral_mode) {
            if (multipath->paths[i].role() == erInternalInfill)
                multipath->paths[i].simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
            else
                multipath->paths[i].simplify_by_fitting_arc(scaled_resolution);
        } else {
            multipath->paths[i].simplify(scaled_resolution);
        }
    }
}

// [INTENT] Same logic as simplify_path but for ExtrusionLoop (closed perimeter paths).
// Each loop segment (ExtrusionPath within the loop) is simplified individually.
// Note: arc-fitting on closed loops can introduce discontinuities at the start/end seam
// where the arc wraps around — this is handled downstream in GCodeWriter.
// [HAZARD H388] Same PrintConfig copy-per-call issue as simplify_path.
// [HAZARD H389] spiral_mode disables arc fitting for loops. If a loop is simplified with
//               Douglas-Peucker in spiral mode, the resolution still applies, potentially
//               removing detail from the outer wall in spiral prints.
void LayerRegion::simplify_loop(ExtrusionLoop* loop)
{
    const auto print_config       = this->layer()->object()->print()->config();
    const bool spiral_mode        = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution  = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < loop->paths.size(); ++i) {
        if (enable_arc_fitting && !spiral_mode) {
            if (loop->paths[i].role() == erInternalInfill)
                loop->paths[i].simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
            else
                loop->paths[i].simplify_by_fitting_arc(scaled_resolution);
        } else {
            loop->paths[i].simplify(scaled_resolution);
        }
    }
}

} // namespace Slic3r
