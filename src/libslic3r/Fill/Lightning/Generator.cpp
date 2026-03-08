// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] Implements the Generator class — the top-level orchestrator for Lightning Infill.
// Two construction paths exist:
//   1. Infill mode  (PrintObject const&)  — used by FillLightning::build_generator()
//   2. Support mode (PrintObject*, contours, overhangs)  — used by tree-support code
//
// Key algorithmic steps (both constructors):
//   a. Parse extrusion parameters → compute m_infill_extrusion_width, m_supporting_radius,
//      m_wall_supporting_radius, m_prune_length, m_straightening_max_distance
//   b. Populate m_overhang_per_layer (internal overhangs that need infill support)
//   c. generateTrees() / generateTreesforSupport() — build per-layer lightning trees
//
// [COUPLING]
//   - PrintObject, PrintConfig, PrintObjectConfig, PrintRegionConfig (region settings)
//   - Layer.hpp: Layer, NodeSPtr (tree nodes)
//   - EdgeGrid (outline locator for boundary proximity)
//   - ClipperUtils (diff, offset in generateInitialInternalOverhangs)
//   - Node::propagateToNextLayer (cross-layer tree propagation)
//   - SVG debug helpers (draw_two_overhangs_to_svg — debug-only, not compiled in release)
//
// [STATE] All state is instance-level (no static/global except the SVG debug helper).
// get_svg_filename() has a static bool rand_init — initialized lazily on first call.
// [HAZARD H273] get_svg_filename() uses srand(time(NULL)) with a static rand_init guard.
// If two Generator instances are constructed concurrently (not current usage but possible
// with multi-printer UI), both may call srand() simultaneously on the same global C RNG —
// a data race on a non-atomic global. Low risk (srand is typically idempotent), but UB by
// the C++ standard.
//
// [CONCURRENCY] Construction is single-threaded (called from PrintObject::prepare_infill).
// getBestGroundingLocation inside generateTrees uses tbb::parallel_for internally (see Layer.cpp).
// No data races in construction because each iteration works on a different Layer object.

#include "Generator.hpp"
#include "TreeNode.hpp"

#include "../../ClipperUtils.hpp"
#include "../../Layer.hpp"
#include "../../Print.hpp"

#include "ExPolygon.hpp"

/* Possible future tasks/optimizations,etc.:
 * - Improve connecting heuristic to favor connecting to shorter trees
 * - Change which node of a tree is the root when that would be better in reconnectRoots.
 * - (For implementation in Infill classes & elsewhere): Outline offset, infill-overlap & perimeter gaps.
 * - Allow for polylines, i.e. merge Tims PR about polyline fixes
 * - Unit Tests?
 * - Optimization: let the square grid store the closest point on boundary
 * - Optimization: only compute the closest dist to / point on boundary for the outer cells and flood-fill the rest
 * - Make a pass with Arachne over the output. Somehow.
 * - Generate all to-be-supported points at once instead of sequentially: See branch interlocking_gen PolygonUtils::spreadDots (Or work with
 * sparse grids.)
 * - Lots of magic values ... to many to parameterize. But are they the best?
 * - Move more complex computations from Generator constructor to elsewhere.
 */

namespace Slic3r {

// [INTENT] Debug helper — builds an SVG filename for layer debugging. Dead code in release.
// [STATE] static bool rand_init — lazy single-init guard for C RNG.
// [HAZARD H273] srand(time(NULL)) is called once. If two threads call get_svg_filename()
// before rand_init is set, both may call srand() — data race on global rand state.
// Not thread-safe. Safe in current single-threaded usage.
// [HAZARD H274] rand() % 1000000 but the result is currently commented out in the filename.
// The rand_num variable is computed but unused. Dead code.
static std::string get_svg_filename(std::string layer_nr_or_z, std::string tag = "bbl_ts")
{
    static bool rand_init = false;

    if (!rand_init) {
        srand(time(NULL));
        rand_init = true;
    }

    int rand_num = rand() % 1000000;
    // makedir("./SVG");
    std::string prefix = "./SVG/";
    std::string suffix = ".svg";
    // [HAZARD H274] rand_num is computed but commented out — dead code.
    return prefix + tag + "_" + layer_nr_or_z /*+ "_" + std::to_string(rand_num)*/ + suffix;
}

// [INTENT] Draws two overhang polygon sets to SVG for debugging. Compiled but never called
// in the current codebase (call site in the support constructor is commented out).
// [STATE] Returns an SVG object by value. Side effect: writes .svg file to ./SVG/ directory.
// [HAZARD H275] ./SVG/ directory is not guaranteed to exist. If missing, SVG constructor
// silently fails (file not created, no exception). Debug tool only.
Slic3r::SVG draw_two_overhangs_to_svg(size_t ts_layer, const ExPolygons& overhangs1, const ExPolygons& overhangs2)
{
    BoundingBox bbox1 = get_extents(overhangs1);
    BoundingBox bbox2 = get_extents(overhangs2);
    bbox1.merge(bbox2);

    Slic3r::SVG svg(get_svg_filename(std::to_string(ts_layer), "two_overhangs_generator"), bbox1);

    svg.draw(union_ex(overhangs1), "blue");
    svg.draw(union_ex(overhangs2), "red");

    return svg;
}
} // namespace Slic3r

namespace Slic3r::FillLightning {

// [INTENT] Primary constructor (infill use case). Reads all config from PrintObject.
// Computes infill extrusion width, supporting radius, overhang/prune/straighten radii,
// then invokes generateInitialInternalOverhangs + generateTrees.
//
// [STATE] After construction, m_overhang_per_layer and m_lightning_layers are fully populated.
// No member is modified after construction (read-only for callers).
//
// [COUPLING] Reads:
//   - PrintConfig::nozzle_diameter (for max nozzle dia)
//   - PrintRegionConfig::fill_multiline (Orca extension)
//   - PrintRegionConfig::sparse_infill_line_width
//   - PrintRegionConfig::sparse_infill_density
//   - PrintObjectConfig::layer_height
//   - PrintObjectConfig::line_width (fallback for line width)
//   - Flow::auto_extrusion_width (default line width computation)
//   - print_object.shared_regions()->all_regions.front() — assumes at least one region exists
//
// [HAZARD H276] `all_regions.front()` crashes with a dereference on empty all_regions.
// No guard. If a PrintObject has zero regions (degenerate mesh), this is UB.
//
// [HAZARD H277] m_supporting_radius formula:
//   = extrusion_width * 100 * n_multiline / sparse_infill_density
// Division by sparse_infill_density (a double). If density == 0, this is division by zero.
// Orca added a guard for extrusion_width == 0 but NOT for density == 0. A PrintRegion with
// infill density set to 0% would produce infinity/NaN supporting_radius → bogus tree.
// The calling code (FillLightning path) may guard against 0% density before building the
// generator, but this is not enforced inside the constructor.
//
// [HAZARD H278] layer_thickness = scaled<double>(object_config.layer_height.value).
// Uses the base layer height from object config, NOT the first-layer height. If first-layer
// height != layer_height, m_wall_supporting_radius / m_prune_length / m_straightening_max_distance
// are incorrect for layer 0. This matches Cura's "Note: There's not going to be a layer below
// the first one" comment — but it's wrong for tall first layers.
Generator::Generator(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback)
{
    const PrintConfig&       print_config  = print_object.print()->config();
    const PrintObjectConfig& object_config = print_object.config();
    // [HAZARD H276] all_regions.front() — requires at least one region to exist.
    const PrintRegionConfig&   region_config                  = print_object.shared_regions()->all_regions.front()->config();
    const std::vector<double>& nozzle_diameters               = print_config.nozzle_diameter.values;
    double                     max_nozzle_diameter            = *std::max_element(nozzle_diameters.begin(), nozzle_diameters.end());
    const int                  n_multiline                    = region_config.fill_multiline.value;
    const double               default_infill_extrusion_width = Flow::auto_extrusion_width(FlowRole::frInfill, float(max_nozzle_diameter));
    // Note: There's not going to be a layer below the first one, so the 'initial layer height' doesn't have to be taken into account.
    const double layer_thickness = scaled<double>(object_config.layer_height.value);

    // [INTENT] Compute infill line width in scaled coords.
    // Fallback chain: sparse_infill_line_width → line_width → auto_extrusion_width (nozzle dia).
    // [HAZARD H279] Orca guard: if extrusion width rounds to < EPSILON after scaling,
    // use line_width or default. This prevents the supporting_radius formula dividing by a
    // near-zero width. But EPSILON for a scaled float is ~1e-6 scaled units — this guard
    // fires for any width < ~0.001 µm, which is always 0 in practice when the setting is unset.
    m_infill_extrusion_width = scaled<float>(region_config.sparse_infill_line_width.get_abs_value(max_nozzle_diameter));
    // Orca: fix lightning infill divide by zero when infill line width is set to 0.
    // firstly attempt to set it to the default line width. If that is not provided either, set it to a sane default
    // based on the nozzle diameter.
    if (m_infill_extrusion_width < EPSILON)
        m_infill_extrusion_width = scaled<float>(object_config.line_width.get_abs_value(max_nozzle_diameter) < EPSILON ?
                                                     default_infill_extrusion_width :
                                                     object_config.line_width.get_abs_value(max_nozzle_diameter));

    // [INTENT] m_supporting_radius: the "circle of influence" of an infill line.
    // An infill line within m_supporting_radius of an unsupported overhang point
    // is considered to support it, removing that point from the distance field.
    // Formula: spacing_per_line = extrusion_width / density (one line per 1/density spacing).
    // Multiplied by 100 to work in scaled integer units (density is 0..1, not 0..100%).
    // n_multiline scales the effective coverage of each infill pass.
    // [HAZARD H277] Division by region_config.sparse_infill_density — no guard for 0.
    m_supporting_radius = coord_t(m_infill_extrusion_width) * 100 * n_multiline / region_config.sparse_infill_density;

    // [INTENT] All three angle-derived radii use M_PI/4 (45°) hardcoded.
    // These correspond to Cura's lightning_infill_overhang_angle,
    // lightning_infill_prune_angle, lightning_infill_straightening_angle settings.
    // [HAZARD H272] Hardcoded angles not user-configurable in OrcaSlicer.
    const double lightning_infill_overhang_angle      = M_PI / 4; // 45 degrees
    const double lightning_infill_prune_angle         = M_PI / 4; // 45 degrees
    const double lightning_infill_straightening_angle = M_PI / 4; // 45 degrees
    // [INTENT] tan(45°) == 1.0, so all three radii equal layer_thickness.
    m_wall_supporting_radius     = coord_t(layer_thickness * std::tan(lightning_infill_overhang_angle));
    m_prune_length               = coord_t(layer_thickness * std::tan(lightning_infill_prune_angle));
    m_straightening_max_distance = coord_t(layer_thickness * std::tan(lightning_infill_straightening_angle));

    generateInitialInternalOverhangs(print_object, throw_on_cancel_callback);
    generateTrees(print_object, throw_on_cancel_callback);
}

// [INTENT] Secondary constructor (tree-support use case). Same parameter initialization as
// the primary constructor, but:
//   - Takes pre-computed contours + overhangs vectors (not from PrintObject fill_surfaces)
//   - Uses a different m_supporting_radius formula (density clamped to 0.15)
//   - Calls generateTreesforSupport instead of generateInitialInternalOverhangs + generateTrees
//
// [STATE] After construction, m_overhang_per_layer = overhangs (copied from parameter).
// m_lightning_layers populated by generateTreesforSupport.
//
// [HAZARD H270] Different m_supporting_radius formula: extrusion_width / density.
// No multiply by 100 here (unlike infill constructor). This is NOT a bug — the support
// constructor receives density as a plain fraction 0..1, whereas the infill constructor
// divides by sparse_infill_density which is already in [0,1] range, but the * 100 accounts
// for Clipper integer coordinate scaling. The two paths produce numerically different radii
// for the same nominal density. A port MUST handle these as separate cases.
//
// [HAZARD H280] density parameter has default 0.15f. Caller may pass density = 0.0f and
// rely on the clamp: `density = std::max(0.15f, density)`. If caller passes 0.14f, the
// clamp silently overrides it. No warning.
Generator::Generator(PrintObject*                 m_object,
                     std::vector<Polygons>&       contours,
                     std::vector<Polygons>&       overhangs,
                     const std::function<void()>& throw_on_cancel_callback,
                     float                        density)
{
    const PrintConfig&         print_config                   = m_object->print()->config();
    const PrintObjectConfig&   object_config                  = m_object->config();
    const PrintRegionConfig&   region_config                  = m_object->shared_regions()->all_regions.front()->config();
    const std::vector<double>& nozzle_diameters               = print_config.nozzle_diameter.values;
    double                     max_nozzle_diameter            = *std::max_element(nozzle_diameters.begin(), nozzle_diameters.end());
    const double               default_infill_extrusion_width = Flow::auto_extrusion_width(FlowRole::frInfill, float(max_nozzle_diameter));
    // Note: There's not going to be a layer below the first one, so the 'initial layer height' doesn't have to be taken into account.
    const double layer_thickness = scaled<double>(object_config.layer_height.value);

    m_infill_extrusion_width = scaled<float>(region_config.sparse_infill_line_width.get_abs_value(max_nozzle_diameter));
    // Orca: fix lightning infill divide by zero when infill line width is set to 0.
    if (m_infill_extrusion_width < EPSILON)
        m_infill_extrusion_width = scaled<float>(object_config.line_width.get_abs_value(max_nozzle_diameter) < EPSILON ?
                                                     default_infill_extrusion_width :
                                                     object_config.line_width.get_abs_value(max_nozzle_diameter));

    // [INTENT] Clamp density to minimum 0.15 to prevent excessively large supporting_radius
    // (which would make the tree extremely sparse and potentially cover the entire layer with
    // one branch). 0.15 = 15% — empirically chosen minimum for usable support density.
    // TODO: decide whether enable density controller in advanced options or not
    density = std::max(0.15f, density);
    // [INTENT] Support mode formula: extrusion_width / density.
    // No *100 factor (unlike infill constructor). See [HAZARD H270].
    m_supporting_radius = coord_t(m_infill_extrusion_width) / density;

    const double lightning_infill_overhang_angle      = M_PI / 4; // 45 degrees
    const double lightning_infill_prune_angle         = M_PI / 4; // 45 degrees
    const double lightning_infill_straightening_angle = M_PI / 4; // 45 degrees
    // [HAZARD H281] These three assignments do NOT use coord_t() cast unlike the infill constructor.
    // They assign double directly to coord_t (implicit narrowing conversion). layer_thickness
    // is already in scaled integer units so the conversion is safe, but the inconsistency with
    // the primary constructor is a potential footgun when merging code.
    m_wall_supporting_radius     = layer_thickness * std::tan(lightning_infill_overhang_angle);
    m_prune_length               = layer_thickness * std::tan(lightning_infill_prune_angle);
    m_straightening_max_distance = layer_thickness * std::tan(lightning_infill_straightening_angle);

    // [STATE] Copy overhangs from caller. In infill mode, this is computed internally.
    m_overhang_per_layer = overhangs;

    generateTreesforSupport(contours, throw_on_cancel_callback);

    // [INTENT] Commented-out SVG debug code for visualizing overhangs per layer.
    // The draw_two_overhangs_to_svg function above exists only for this usage.
    // Not compiled in release (no #ifdef guard — it's a plain comment block).
}

// [INTENT] Computes per-layer internal overhang polygons: parts of infill area that
// need lightning support from the layer below. Algorithm (top-to-bottom):
//   1. Collect all stInternal + stInternalVoid fill surfaces for this layer (infill_area_here)
//   2. overhang = diff(offset(infill_area_here, -m_wall_supporting_radius), infill_area_above)
//      = parts of this layer NOT covered by the layer above, eroded by wall support radius
//   3. infill_area_above ← infill_area_here (for next iteration)
//
// [STATE] Populates m_overhang_per_layer[0..N-1]. Reads only fill_surfaces from print_object.
// infill_area_above is a local mutable temporary that carries data between loop iterations.
//
// [COUPLING] For each layer, iterates all LayerRegions and their fill_surfaces.surfaces.
// Only stInternal and stInternalVoid surfaces are considered (not top/bottom/bridge).
//
// [HAZARD H282] offset(infill_area_here, -m_wall_supporting_radius): if infill_area_here is
// very narrow (< m_wall_supporting_radius in width), the negative offset collapses it entirely.
// The result is an empty overhang for that layer — the lightning tree won't grow branches
// to support those areas. This means thin infill slivers may lack internal support.
// Expected behavior (walls support thin infill), but no warning is emitted.
void Generator::generateInitialInternalOverhangs(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback)
{
    m_overhang_per_layer.resize(print_object.layers().size());

    Polygons infill_area_above;
    // Iterate from top to bottom, to subtract the overhang areas above from the overhang areas on the layer below, to get only overhang in
    // the top layer where it is overhanging.
    for (int layer_nr = int(print_object.layers().size()) - 1; layer_nr >= 0; --layer_nr) {
        throw_on_cancel_callback();
        Polygons infill_area_here;
        for (const LayerRegion* layerm : print_object.get_layer(layer_nr)->regions())
            for (const Surface& surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    append(infill_area_here, to_polygons(surface.expolygon));

        // [INTENT] Remove the part of the infill area that is already supported by the walls.
        // offset(..., -m_wall_supporting_radius) erodes areas close to the walls.
        // diff(..., infill_area_above) removes areas directly under the layer above's infill.
        Polygons overhang = diff(offset(infill_area_here, -float(m_wall_supporting_radius)), infill_area_above);

        m_overhang_per_layer[layer_nr] = overhang;
        infill_area_above              = std::move(infill_area_here);
    }
}

// [INTENT] Returns a const reference to the lightning Layer for a given layer_id.
// Called from Filler::_fill_surface_single() once per infill ExPolygon.
// [HAZARD H268] Only asserts range in debug. UB in release if layer_id out-of-range.
const Layer& Generator::getTreesForLayer(const size_t& layer_id) const
{
    assert(layer_id < m_lightning_layers.size());
    return m_lightning_layers[layer_id];
}

// [INTENT] Builds lightning trees for all layers of a PrintObject (infill mode).
// Algorithm:
//   Phase 1: Collect infill_outlines[i] for all layers (infill + internal void surfaces).
//   Phase 2 (top-to-bottom): For each layer:
//     a. generateNewTrees()   — add new branches for unsupported overhang points.
//     b. reconnectRoots()     — reattach propagated tree roots to the new layer boundary.
//     c. propagateToNextLayer() — copy/prune/straighten trees for the layer below.
//   An EdgeGrid (outlines_locator) is maintained and updated per layer for fast boundary queries.
//
// [STATE] Populates m_lightning_layers[0..N-1] and bboxs[0..N-1].
// [MEMORY] infill_outlines: temporary N-element vector, freed on return.
//           outlines_locator: resized and rebuilt per layer.
//
// [CONCURRENCY] Outer loop is serial (top-to-bottom dependencies prevent parallelism).
// Inner parallel_for in getBestGroundingLocation (Layer.cpp) handles inner parallelism.
//
// [HAZARD H283] outlines_locator is initialized with infill_outlines[top_layer_id].
// If the top layer has empty infill (all-solid top), the locator has an empty bbox.
// get_extents(infill_outlines[top_layer_id]).inflated(SCALED_EPSILON) returns a degenerate box.
// The EdgeGrid::Grid constructor may assert or produce invalid results on a degenerate box.
// Low risk in practice (top-solid models skip lightning infill setup entirely), but worth noting.
//
// [HAZARD H284] `lower_trees` is a reference to m_lightning_layers[layer_id - 1].tree_roots.
// propagateToNextLayer() populates this vector. Since it's a reference into the m_lightning_layers
// vector and no reallocation occurs after resize(), this is safe. However, if m_lightning_layers
// is ever resized inside the loop (it is not currently), the reference would dangle.
void Generator::generateTrees(const PrintObject& print_object, const std::function<void()>& throw_on_cancel_callback)
{
    // [INTENT] locator_cell_size() = scaled<coord_t>(4mm). Grid cell size for EdgeGrid.
    // Controls spatial query resolution. Too small → too many grid cells (memory).
    // Too large → too many false positives in collision queries. 4mm is the Cura default.
    const auto _locator_cell_size = locator_cell_size();
    m_lightning_layers.resize(print_object.layers().size());
    bboxs.resize(print_object.layers().size());
    std::vector<Polygons> infill_outlines(print_object.layers().size(), Polygons());

    // For-each layer from top to bottom:
    for (int layer_id = int(print_object.layers().size()) - 1; layer_id >= 0; layer_id--) {
        throw_on_cancel_callback();
        for (const LayerRegion* layerm : print_object.get_layer(layer_id)->regions())
            for (const Surface& surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    append(infill_outlines[layer_id], to_polygons(surface.expolygon));
    }

    // [INTENT] Initialize EdgeGrid for the top layer. Updated per-layer below.
    // EdgeGrid provides O(1) approximate nearest-boundary-point and line-collision queries.
    const size_t   top_layer_id = print_object.layers().size() - 1;
    EdgeGrid::Grid outlines_locator(get_extents(infill_outlines[top_layer_id]).inflated(SCALED_EPSILON));
    outlines_locator.create(infill_outlines[top_layer_id], _locator_cell_size);

    // For-each layer from top to bottom:
    for (int layer_id = int(top_layer_id); layer_id >= 0; layer_id--) {
        throw_on_cancel_callback();
        Layer&             current_lightning_layer = m_lightning_layers[layer_id];
        const Polygons&    current_outlines        = infill_outlines[layer_id];
        const BoundingBox& current_outlines_bbox   = get_extents(current_outlines);

        bboxs[layer_id] = get_extents(current_outlines);

        // [INTENT] to_be_reconnected_tree_roots = roots propagated from layer above in the
        // previous iteration (via propagateToNextLayer). These are "orphaned" roots that need
        // to be attached to the current layer's outline or an existing tree.
        std::vector<NodeSPtr> to_be_reconnected_tree_roots = current_lightning_layer.tree_roots;

        // [INTENT] Add new branches for all currently-unsupported overhang points on this layer.
        current_lightning_layer.generateNewTrees(m_overhang_per_layer[layer_id], current_outlines, current_outlines_bbox, outlines_locator,
                                                 m_supporting_radius, m_wall_supporting_radius, throw_on_cancel_callback);
        // [INTENT] Reattach the roots that were propagated from above to the current boundary/tree.
        current_lightning_layer.reconnectRoots(to_be_reconnected_tree_roots, current_outlines, current_outlines_bbox, outlines_locator,
                                               m_supporting_radius, m_wall_supporting_radius);

        // [INTENT] Propagate trees to layer below. Not done for layer 0 (no layer below).
        if (layer_id == 0)
            return;

        // [INTENT] Update the outlines_locator for the layer below.
        // The BBox is expanded to cover both current and lower outlines + current tree extents,
        // ensuring the locator can answer queries from tree nodes that may be outside the lower outline.
        const Polygons& below_outlines      = infill_outlines[layer_id - 1];
        BoundingBox     below_outlines_bbox = get_extents(below_outlines).inflated(SCALED_EPSILON);
        if (const BoundingBox& outlines_locator_bbox = outlines_locator.bbox(); outlines_locator_bbox.defined)
            below_outlines_bbox.merge(outlines_locator_bbox);

        if (!current_lightning_layer.tree_roots.empty())
            below_outlines_bbox.merge(get_extents(current_lightning_layer.tree_roots).inflated(SCALED_EPSILON));

        outlines_locator.set_bbox(below_outlines_bbox);
        outlines_locator.create(below_outlines, _locator_cell_size);

        // [INTENT] Deep-copy + prune + straighten current trees into lower_trees.
        // prune_distance = m_prune_length (how far leaves retract),
        // smooth_magnitude = m_straightening_max_distance (how far nodes may be shifted).
        // max_remove_colinear_dist = locator_cell_size / 2 (minimum segment for collinearity removal).
        // [HAZARD H284] lower_trees is a reference; see hazard note above.
        std::vector<NodeSPtr>& lower_trees = m_lightning_layers[layer_id - 1].tree_roots;
        for (auto& tree : current_lightning_layer.tree_roots)
            tree->propagateToNextLayer(lower_trees, below_outlines, outlines_locator, m_prune_length, m_straightening_max_distance,
                                       _locator_cell_size / 2);
    }
}

// [INTENT] Identical algorithm to generateTrees() but operates on pre-built contours vector
// (tree-support use case) instead of extracting from PrintObject.
// [STATE] m_overhang_per_layer must be pre-populated before this call.
// [HAZARD H285] No throw_on_cancel_callback call in the first inner loop of generateTrees
// (contour collection). Here the single loop IS guarded by throw_on_cancel_callback.
// For very large objects (thousands of layers), missing cancel checks in generateTrees
// would cause a hang. Both paths have cancel checks in the tree-building loop.
void Generator::generateTreesforSupport(std::vector<Polygons>& contours, const std::function<void()>& throw_on_cancel_callback)
{
    if (contours.empty())
        return;

    m_lightning_layers.resize(contours.size());
    bboxs.resize(contours.size());

    const auto     _locator_cell_size = locator_cell_size();
    const size_t   top_layer_id       = contours.size() - 1;
    EdgeGrid::Grid outlines_locator(get_extents(contours[top_layer_id]).inflated(SCALED_EPSILON));
    outlines_locator.create(contours[top_layer_id], _locator_cell_size);

    // For-each layer from top to bottom:
    for (int layer_id = int(top_layer_id); layer_id >= 0; layer_id--) {
        throw_on_cancel_callback();
        Layer&             current_lightning_layer = m_lightning_layers[layer_id];
        const Polygons&    current_outlines        = contours[layer_id];
        const BoundingBox& current_outlines_bbox   = get_extents(current_outlines);

        bboxs[layer_id] = get_extents(current_outlines);

        std::vector<NodeSPtr> to_be_reconnected_tree_roots = current_lightning_layer.tree_roots;

        current_lightning_layer.generateNewTrees(m_overhang_per_layer[layer_id], current_outlines, current_outlines_bbox, outlines_locator,
                                                 m_supporting_radius, m_wall_supporting_radius, throw_on_cancel_callback);
        current_lightning_layer.reconnectRoots(to_be_reconnected_tree_roots, current_outlines, current_outlines_bbox, outlines_locator,
                                               m_supporting_radius, m_wall_supporting_radius);

        if (layer_id == 0)
            return;

        const Polygons& below_outlines      = contours[layer_id - 1];
        BoundingBox     below_outlines_bbox = get_extents(below_outlines).inflated(SCALED_EPSILON);
        if (const BoundingBox& outlines_locator_bbox = outlines_locator.bbox(); outlines_locator_bbox.defined)
            below_outlines_bbox.merge(outlines_locator_bbox);

        if (!current_lightning_layer.tree_roots.empty())
            below_outlines_bbox.merge(get_extents(current_lightning_layer.tree_roots).inflated(SCALED_EPSILON));

        outlines_locator.set_bbox(below_outlines_bbox);
        outlines_locator.create(below_outlines, _locator_cell_size);

        std::vector<NodeSPtr>& lower_trees = m_lightning_layers[layer_id - 1].tree_roots;
        for (auto& tree : current_lightning_layer.tree_roots)
            tree->propagateToNextLayer(lower_trees, below_outlines, outlines_locator, m_prune_length, m_straightening_max_distance,
                                       _locator_cell_size / 2);
    }
}

} // namespace Slic3r::FillLightning
