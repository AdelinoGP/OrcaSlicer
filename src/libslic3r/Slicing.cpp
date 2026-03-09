// [INTENT] Slicing.cpp — implementation of all layer height profile operations.
// Functions covered:
//   min/max_layer_height_from_nozzle (two overloads each: PrintConfig + DynamicPrintConfig)
//   SlicingParameters::create_from_config()
//   layer_height_profile_from_ranges()
//   layer_height_profile_adaptive()
//   smooth_height_profile()
//   adjust_layer_height_profile()
//   adjust_layer_series_to_align_object_height()   [BBS addition — Orca only]
//   generate_object_layers()
//   check_object_layers_fixed()
//   generate_layer_height_texture()
//
// [STATE] All functions are stateless free functions or static methods. No global
//   mutable state lives here. Thread-safety depends solely on callers not sharing
//   output vectors concurrently.
//
// [COUPLING] Depends on SlicingAdaptive (mesh curvature query), PrintConfig /
//   PrintObjectConfig (resolved print settings), Model / ModelObject
//   (layer_config_ranges, mesh), and the libslic3r.h primitives (coordf_t, lerp,
//   EPSILON, is_approx).

#include <limits>

#include "libslic3r.h"
#include "Slicing.hpp"
#include "SlicingAdaptive.hpp"
#include "PrintConfig.hpp"
#include "Model.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
#undef NDEBUG
#define DEBUG
#define _DEBUG
#include "SVG.hpp"
#undef assert
#include <cassert>
#endif

namespace Slic3r {

// [INTENT] Module-level constants for layer height clamping.
// MIN_LAYER_HEIGHT (0.01 mm) is the hard floor — prevents degenerate zero-height layers.
// MIN_LAYER_HEIGHT_DEFAULT (0.07 mm) is the fallback when the user has not configured
//   a minimum; 0.07 mm ≈ 1/4 of a 0.4 mm nozzle diameter (conservative).
// LAYER_HEIGHT_CHANGE_STEP (0.04 mm) is the BBS-added cap on how steeply the adaptive
//   layer height can change between adjacent layers (prevents abrupt stair-step artifacts).
static const coordf_t MIN_LAYER_HEIGHT         = 0.01;
static const coordf_t MIN_LAYER_HEIGHT_DEFAULT = 0.07;
static const double   LAYER_HEIGHT_CHANGE_STEP = 0.04;

// [INTENT] min_layer_height_from_nozzle (PrintConfig overload — file-local).
// Returns the configured per-nozzle minimum layer height, falling back to
// MIN_LAYER_HEIGHT_DEFAULT (0.07 mm) when the user has not set a value (== 0).
// Always floors at the hard MIN_LAYER_HEIGHT (0.01 mm) to prevent degenerate layers.
// [COUPLING] Used only by create_from_config() for the typed PrintConfig path.
// [HAZARD] idx_nozzle is 1-based; get_at() is called with idx_nozzle-1 (0-based).
//   Passing idx_nozzle == 0 would call get_at(-1) which wraps to size_t::max —
//   defensive only if callers guarantee 1-based input.
inline coordf_t min_layer_height_from_nozzle(const PrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = print_config.min_layer_height.get_at(idx_nozzle - 1);
    return (min_layer_height == 0.) ? MIN_LAYER_HEIGHT_DEFAULT : std::max(MIN_LAYER_HEIGHT, min_layer_height);
}

// [INTENT] max_layer_height_from_nozzle (PrintConfig overload — file-local).
// Returns the configured per-nozzle maximum layer height. When not configured (== 0),
// defaults to 0.75 * nozzle_diameter (empirical FFF upper bound for reliable extrusion).
// Always at least as large as the minimum layer height (floor via std::max).
// [COUPLING] Used only by create_from_config() for the typed PrintConfig path.
inline coordf_t max_layer_height_from_nozzle(const PrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = min_layer_height_from_nozzle(print_config, idx_nozzle);
    coordf_t max_layer_height = print_config.max_layer_height.get_at(idx_nozzle - 1);
    coordf_t nozzle_dmr       = print_config.nozzle_diameter.get_at(idx_nozzle - 1);
    return std::max(min_layer_height, (max_layer_height == 0.) ? (0.75 * nozzle_dmr) : max_layer_height);
}

// [INTENT] Slicing::min_layer_height_from_nozzle (DynamicPrintConfig overload — public API).
// Same logic as the file-local overload but accesses config values via string key lookup
// (DynamicPrintConfig uses a string-keyed map). Exposed for the UI layer-height editor.
// [COUPLING] Called by the UI adaptive layer height editor and by the public Slicing:: API.
// [HAZARD] opt_float("min_layer_height", idx_nozzle-1) — same 0-based indexing hazard as above.
coordf_t Slicing::min_layer_height_from_nozzle(const DynamicPrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = print_config.opt_float("min_layer_height", idx_nozzle - 1);
    return (min_layer_height == 0.) ? MIN_LAYER_HEIGHT_DEFAULT : std::max(MIN_LAYER_HEIGHT, min_layer_height);
}

// [INTENT] Slicing::max_layer_height_from_nozzle (DynamicPrintConfig overload — public API).
// Same logic as the file-local overload but via string key lookup.
// [COUPLING] Called by the UI adaptive layer height editor and by the public Slicing:: API.
coordf_t Slicing::max_layer_height_from_nozzle(const DynamicPrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = min_layer_height_from_nozzle(print_config, idx_nozzle);
    coordf_t max_layer_height = print_config.opt_float("max_layer_height", idx_nozzle - 1);
    coordf_t nozzle_dmr       = print_config.opt_float("nozzle_diameter", idx_nozzle - 1);
    return std::max(min_layer_height, (max_layer_height == 0.) ? (0.75 * nozzle_dmr) : max_layer_height);
}

// Maximum layer height for the variable layer height algorithm, 3/4 of a nozzle dimaeter by default,
// it should not be smaller than the minimum layer height.
inline coordf_t max_layer_height_from_nozzle(const PrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = min_layer_height_from_nozzle(print_config, idx_nozzle);
    coordf_t max_layer_height = print_config.max_layer_height.get_at(idx_nozzle - 1);
    coordf_t nozzle_dmr       = print_config.nozzle_diameter.get_at(idx_nozzle - 1);
    return std::max(min_layer_height, (max_layer_height == 0.) ? (0.75 * nozzle_dmr) : max_layer_height);
}

// Minimum layer height for the variable layer height algorithm.
coordf_t Slicing::min_layer_height_from_nozzle(const DynamicPrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = print_config.opt_float("min_layer_height", idx_nozzle - 1);
    return (min_layer_height == 0.) ? MIN_LAYER_HEIGHT_DEFAULT : std::max(MIN_LAYER_HEIGHT, min_layer_height);
}

// Maximum layer height for the variable layer height algorithm, 3/4 of a nozzle dimaeter by default,
// it should not be smaller than the minimum layer height.
coordf_t Slicing::max_layer_height_from_nozzle(const DynamicPrintConfig& print_config, int idx_nozzle)
{
    coordf_t min_layer_height = min_layer_height_from_nozzle(print_config, idx_nozzle);
    coordf_t max_layer_height = print_config.opt_float("max_layer_height", idx_nozzle - 1);
    coordf_t nozzle_dmr       = print_config.opt_float("nozzle_diameter", idx_nozzle - 1);
    return std::max(min_layer_height, (max_layer_height == 0.) ? (0.75 * nozzle_dmr) : max_layer_height);
}

// [INTENT] create_from_config — the single factory that converts raw PrintConfig +
// PrintObjectConfig into a fully resolved SlicingParameters value.
// Computes ALL layer height bounds, raft geometry, support gaps, and Z shrinkage
// compensation into absolute mm heights. The result is cached per PrintObject.
//
// Algorithm outline:
//   1. Resolve initial_layer_print_height (fall back to layer_height if 0).
//   2. Compute per-nozzle min/max layer height; take strictest across all extruders.
//   3. If raft_layers > 0: split into base + interface, compute cumulative Z tops.
//   4. Apply Z shrinkage compensation (Orca extension): scale object_print_z_max.
//   5. If raft: shift object_print_z_min/max up by raft thickness + gap.
//
// [HAZARD] support_filament == 0 → get_at(size_t(-1)) → wraps to nozzle[0].
//   Intentional: no tool change means use current nozzle, but all nozzles must match.
// [HAZARD] Gap rounding uses EPSILON to break ties; gap may exceed nominal by EPSILON.
// [COUPLING] Cached in PrintObject::m_slicing_params; invalidated on config change.
SlicingParameters SlicingParameters::create_from_config(const PrintConfig&               print_config,
                                                        const PrintObjectConfig&         object_config,
                                                        coordf_t                         object_height,
                                                        const std::vector<unsigned int>& object_extruders,
                                                        const Vec3d&                     object_shrinkage_compensation)
{
    // [INTENT] initial_layer_print_height == 0 is the sentinel for "use object layer height".
    coordf_t initial_layer_print_height = (print_config.initial_layer_print_height.value <= 0) ?
                                              object_config.layer_height.value :
                                              print_config.initial_layer_print_height.value;
    // If object_config.support_filament == 0 resp. object_config.support_interface_filament == 0,
    // print_config.nozzle_diameter.get_at(size_t(-1)) returns the 0th nozzle diameter,
    // which is consistent with the requirement that if support_filament == 0 resp. support_interface_filament == 0,
    // support will not trigger tool change, but it will use the current nozzle instead.
    // In that case all the nozzles have to be of the same diameter.
    coordf_t support_material_extruder_dmr           = print_config.nozzle_diameter.get_at(object_config.support_filament.value - 1);
    coordf_t support_material_interface_extruder_dmr = print_config.nozzle_diameter.get_at(object_config.support_interface_filament.value -
                                                                                           1);
    // [STATE] soluble_interface == true when support_top_z_distance == 0 (PVA-style fusing).
    bool soluble_interface = object_config.support_top_z_distance.value == 0.;

    SlicingParameters params;
    params.layer_height              = object_config.layer_height.value;
    params.first_print_layer_height  = initial_layer_print_height;
    params.first_object_layer_height = initial_layer_print_height;
    params.object_print_z_min        = 0.;
    // [INTENT] Orca XYZ filament compensation: scale object_print_z_max by shrinkage_compensation_z.
    // Emits layers slightly thicker than nominal, compensating for Z-axis shrinkage after cooling.
    // object_print_z_uncompensated_max stores the raw unscaled height for profile-space operations.
    params.object_print_z_max               = object_height * object_shrinkage_compensation.z();
    params.object_print_z_uncompensated_max = object_height;
    params.object_shrinkage_compensation_z  = object_shrinkage_compensation.z();
    params.base_raft_layers                 = object_config.raft_layers.value;
    params.soluble_interface                = soluble_interface;

    // [INTENT] Compute min/max layer height as the intersection of constraints from all extruders.
    // max-of-mins: no extruder asked to print thinner than it can handle.
    // min-of-maxes: no extruder asked to print thicker than it can handle.
    params.min_layer_height = MIN_LAYER_HEIGHT;
    params.max_layer_height = std::numeric_limits<double>::max();
    if (object_config.enable_support.value || params.base_raft_layers > 0 || object_config.enforce_support_layers > 0) {
        // Has some form of support. Add the support layers to the minimum / maximum layer height limits.
        params.min_layer_height        = std::max(min_layer_height_from_nozzle(print_config, object_config.support_filament),
                                                  min_layer_height_from_nozzle(print_config, object_config.support_interface_filament));
        params.max_layer_height        = std::min(max_layer_height_from_nozzle(print_config, object_config.support_filament),
                                                  max_layer_height_from_nozzle(print_config, object_config.support_interface_filament));
        params.max_suport_layer_height = params.max_layer_height;
    }
    if (object_extruders.empty()) {
        // [INTENT] No explicit extruder list — fall back to extruder 0 (idx 0 → get_at(0) for 1-based call with 0).
        params.min_layer_height = std::max(params.min_layer_height, min_layer_height_from_nozzle(print_config, 0));
        params.max_layer_height = std::min(params.max_layer_height, max_layer_height_from_nozzle(print_config, 0));
    } else {
        for (unsigned int extruder_id : object_extruders) {
            params.min_layer_height = std::max(params.min_layer_height, min_layer_height_from_nozzle(print_config, extruder_id));
            params.max_layer_height = std::min(params.max_layer_height, max_layer_height_from_nozzle(print_config, extruder_id));
        }
    }
    // [INTENT] Final clamp: nominal layer_height must be within [min, max].
    params.min_layer_height = std::min(params.min_layer_height, params.layer_height);
    params.max_layer_height = std::max(params.max_layer_height, params.layer_height);

    if (!soluble_interface) {
        // [INTENT] For non-soluble interfaces, read the explicit gap values from config.
        // When independent_support_layer_height is false (BBS default), round each gap to the
        // nearest integer multiple of layer_height so layers align cleanly.
        params.gap_raft_object = object_config.raft_contact_distance.value;
        // BBS
        params.gap_object_support = object_config.support_bottom_z_distance.value;
        params.gap_support_object = object_config.support_top_z_distance.value;

        if (!print_config.independent_support_layer_height) {
            params.gap_raft_object = std::round(params.gap_raft_object / object_config.layer_height + EPSILON) * object_config.layer_height;
            params.gap_object_support = std::round(params.gap_object_support / object_config.layer_height + EPSILON) *
                                        object_config.layer_height;
            params.gap_support_object = std::round(params.gap_support_object / object_config.layer_height + EPSILON) *
                                        object_config.layer_height;
        }
    }

    if (params.base_raft_layers > 0) {
        // [INTENT] Split raft layers into base + interface halves (ceiling division).
        // Example: raft_layers=3 → interface=2, base=1; raft_layers=4 → interface=2, base=2.
        // Note: base_raft_layers is decremented in place here.
        params.interface_raft_layers = (params.base_raft_layers + 1) / 2;
        params.base_raft_layers -= params.interface_raft_layers;
        // Use as large as possible layer height for the intermediate raft layers.
        params.base_raft_layer_height      = std::max(params.layer_height, 0.75 * support_material_extruder_dmr);
        params.interface_raft_layer_height = std::max(params.layer_height, 0.75 * support_material_interface_extruder_dmr);
        params.first_object_layer_bridging = false;
        params.contact_raft_layer_height   = std::max(params.layer_height, 0.75 * support_material_interface_extruder_dmr);
        params.first_object_layer_height   = params.layer_height;
    }

    if (params.has_raft()) {
        // Raise first object layer Z by the thickness of the raft itself plus the extra distance required by the support material logic.
        // FIXME The last raft layer is the contact layer, which shall be printed with a bridging flow for ease of separation. Currently it
        // is not the case.
        // [INTENT] Compute absolute Z tops for each raft section. Raft base starts at first_layer_height
        // and stacks base_raft_layers-1 additional layers. Interface sits on top, then contact layer.
        // object_print_z_min/max are shifted by print_z = raft_contact_top_z + gap_raft_object.
        if (params.raft_layers() == 1) {
            // There is only the contact layer.
            params.contact_raft_layer_height = initial_layer_print_height;
            params.raft_contact_top_z        = initial_layer_print_height;
        } else {
            assert(params.base_raft_layers > 0);
            assert(params.interface_raft_layers > 0);
            // Number of the base raft layers is decreased by the first layer.
            params.raft_base_top_z = initial_layer_print_height + coordf_t(params.base_raft_layers - 1) * params.base_raft_layer_height;
            // Number of the interface raft layers is decreased by the contact layer.
            params.raft_interface_top_z = params.raft_base_top_z +
                                          coordf_t(params.interface_raft_layers - 1) * params.interface_raft_layer_height;
            params.raft_contact_top_z = params.raft_interface_top_z + params.contact_raft_layer_height;
        }
        // [INTENT] Shift both object_print_z_max (compensated) and object_print_z_uncompensated_max
        // by the same print_z. The raft is printed at nominal height (no shrinkage compensation).
        coordf_t print_z          = params.raft_contact_top_z + params.gap_raft_object;
        params.object_print_z_min = print_z;
        params.object_print_z_max += print_z;
        params.object_print_z_uncompensated_max += print_z;
    }

    params.valid = true;
    return params;
}

// [INTENT] layer_height_profile_from_ranges — convert user-configured per-Z layer height ranges
// into the flat [z_i, h_i] profile vector.
//
// Both the input ranges and the output profile are referenced to z=0 (raft is NOT included).
// The raft lift is applied later at G-code generation time.
//
// Algorithm:
//   Pass 1: Sort and de-overlap the input ranges, inserting a fixed first-layer entry if needed.
//   Pass 2: Walk z=0..object_height, emitting [z, h] pairs; gaps between ranges are filled with
//           the nominal layer_height. Consecutive identical-height entries are compressed.
//
// [MEMORY] Output format: flat vector<coordf_t> with adjacent pairs [z_i, h_i].
//   The profile represents a piecewise-constant (staircase) function — not a piecewise-linear
//   interpolation. The z values mark transition points; height at z is the h of the preceding pair.
//
// [HAZARD] object_print_z_height() is used to clip range hi — but this is the COMPENSATED height.
//   The profile z values are in UNCOMPENSATED space. At the end, the fill-to-top uses
//   object_print_z_uncompensated_height(). This inconsistency means compensated and uncompensated
//   endpoints differ if shrinkage_compensation_z != 1.0.
std::vector<coordf_t> layer_height_profile_from_ranges(const SlicingParameters&     slicing_params,
                                                       const t_layer_config_ranges& layer_config_ranges)
{
    // 1) If there are any height ranges, trim one by the other to make them non-overlapping. Insert the 1st layer if fixed.
    std::vector<std::pair<t_layer_height_range, coordf_t>> ranges_non_overlapping;
    ranges_non_overlapping.reserve(layer_config_ranges.size() * 4);
    if (slicing_params.first_object_layer_height_fixed())
        ranges_non_overlapping.push_back(
            std::pair<t_layer_height_range, coordf_t>(t_layer_height_range(0., slicing_params.first_object_layer_height),
                                                      slicing_params.first_object_layer_height));
    // The height ranges are sorted lexicographically by low / high layer boundaries.
    for (t_layer_config_ranges::const_iterator it_range = layer_config_ranges.begin(); it_range != layer_config_ranges.end(); ++it_range) {
        coordf_t lo     = it_range->first.first;
        coordf_t hi     = std::min(it_range->first.second, slicing_params.object_print_z_height());
        coordf_t height = it_range->second.option("layer_height")->getFloat();
        if (!ranges_non_overlapping.empty())
            // Trim current low with the last high.
            lo = std::max(lo, ranges_non_overlapping.back().first.second);
        if (lo + EPSILON < hi)
            // Ignore too narrow ranges.
            ranges_non_overlapping.push_back(std::pair<t_layer_height_range, coordf_t>(t_layer_height_range(lo, hi), height));
    }

    // 2) Convert the trimmed ranges to a height profile, fill in the undefined intervals between z=0 and
    // z=slicing_params.object_print_z_max() with slicing_params.layer_height
    std::vector<coordf_t> layer_height_profile;
    // [INTENT] last_z — returns the last Z value already emitted into the profile.
    // Returns 0.0 if the profile is empty (i.e., before any entry is added).
    // Used to detect gaps between consecutive ranges and fill with nominal layer_height.
    auto last_z = [&layer_height_profile]() { return layer_height_profile.empty() ? 0. : *(layer_height_profile.end() - 2); };
    // [INTENT] lh_append — compressing append for [z, height] pairs.
    // Compression rules (to minimize output size):
    //   1. Exact duplicate [z, h] == last [z, h] → drop silently.
    //   2. Same h as last entry AND same h as the entry before the last (third repetition)
    //      → just update the z of the penultimate entry instead of pushing a new pair.
    //      This collapses runs of the same height into a single "step" of [start_z, h].
    //   3. Otherwise → push [z, layer_height] as a new pair.
    // [MEMORY] The output profile contains adjacent [z_i, h_i] pairs; each pair encodes
    //   "height is h_i from z_i until the next z entry". This is piecewise-constant, not linear.
    auto lh_append = [&layer_height_profile](coordf_t z, coordf_t layer_height) {
        if (!layer_height_profile.empty()) {
            bool last_z_matches = is_approx(*(layer_height_profile.end() - 2), z);
            bool last_h_matches = is_approx(layer_height_profile.back(), layer_height);
            if (last_h_matches) {
                if (last_z_matches) {
                    // Drop a duplicate.
                    return;
                }
                if (layer_height_profile.size() >= 4 && is_approx(*(layer_height_profile.end() - 3), layer_height)) {
                    // Third repetition of the same layer_height. Update z of the last entry.
                    *(layer_height_profile.end() - 2) = z;
                    return;
                }
            }
        }
        layer_height_profile.push_back(z);
        layer_height_profile.push_back(layer_height);
    };

    for (const std::pair<t_layer_height_range, coordf_t>& non_overlapping_range : ranges_non_overlapping) {
        coordf_t lo     = non_overlapping_range.first.first;
        coordf_t hi     = non_overlapping_range.first.second;
        coordf_t height = non_overlapping_range.second;
        if (coordf_t z = last_z(); lo > z + EPSILON) {
            // Insert a step of normal layer height.
            lh_append(z, slicing_params.layer_height);
            lh_append(lo, slicing_params.layer_height);
        }
        // Insert a step of the overriden layer height.
        lh_append(lo, height);
        lh_append(hi, height);
    }

    if (coordf_t z = last_z(); z < slicing_params.object_print_z_uncompensated_height()) {
        // Insert a step of normal layer height up to the object top.
        lh_append(z, slicing_params.layer_height);
        lh_append(slicing_params.object_print_z_uncompensated_height(), slicing_params.layer_height);
    }

    return layer_height_profile;
}

// [INTENT] layer_height_profile_adaptive — generate a layer height profile that ensures
// a prescribed maximum cusp height on the surface of the object.
// Based on the work of @platsch (original Slic3r contributor).
//
// The @platsch algorithm: for each candidate print_z, query SlicingAdaptive for the
// maximum layer height that keeps the cusp (vertical facet error) below quality_factor.
// The result is clamped to [min_layer_height, max_layer_height].
//
// [STATE] SlicingAdaptive is initialized fresh per call — no shared state.
//   current_facet is an optimization: facets are sorted by Z so the query can resume
//   from the last visited facet rather than scanning from the beginning each time.
//
// [HAZARD] BBS addition (LAYER_HEIGHT_CHANGE_STEP): limits how steeply the height can
//   change between adjacent layers. This overrides the purely cusp-optimal height if the
//   change would exceed 0.04 mm/layer. This can cause suboptimal cusp on steep transitions.
//
// [HAZARD] The #if 0 block (match_horizontal_surfaces) is permanently disabled. It would
//   shrink/expand layers to snap to horizontal features. Reactivating it requires
//   reimplementing `this->config` access which no longer exists in this context.
//
// [COUPLING] Calls SlicingAdaptive::next_layer_height() for each layer.
//   Also applies object.layer_config_ranges overrides (user per-range height settings
//   take priority over the adaptive algorithm within those ranges).
std::vector<double> layer_height_profile_adaptive(const SlicingParameters& slicing_params, const ModelObject& object, float quality_factor)
{
    // 1) Initialize the SlicingAdaptive class with the object meshes.
    SlicingAdaptive as;
    as.set_slicing_parameters(slicing_params);
    as.prepare(object);

    // 2) Generate layers using the algorithm of @platsch
    // [MEMORY] Profile starts with first layer pair [0, first_object_layer_height].
    // If first layer height is fixed (no raft or soluble raft), the pair is duplicated
    // to form a zero-width "step" that locks the first layer height.
    std::vector<double> layer_height_profile;
    layer_height_profile.push_back(0.0);
    layer_height_profile.push_back(slicing_params.first_object_layer_height);
    if (slicing_params.first_object_layer_height_fixed()) {
        layer_height_profile.push_back(slicing_params.first_object_layer_height);
        layer_height_profile.push_back(slicing_params.first_object_layer_height);
    }
    double print_z = slicing_params.first_object_layer_height;
    // last facet visited by the as.next_layer_height() function, where the facets are sorted by their increasing Z span.
    size_t current_facet = 0;
    // loop until we have at least one layer and the max slice_z reaches the object height
    while (print_z + EPSILON < slicing_params.object_print_z_uncompensated_height()) {
        float height = slicing_params.max_layer_height;
        // determine next layer height
        float cusp_height = as.next_layer_height(float(print_z), quality_factor, current_facet);

#if 0
        // [HAZARD] match_horizontal_surfaces: permanently disabled — would snap to horizontal features.
        if (this->config.match_horizontal_surfaces.value) {
            coordf_t horizontal_dist = as.horizontal_facet_distance(print_z + height, min_layer_height);
            if ((horizontal_dist < min_layer_height) && (horizontal_dist > 0)) {
#ifdef SLIC3R_DEBUG
                std::cout << "Horizontal feature ahead, distance: " << horizontal_dist << std::endl;
#endif
                if (height-(min_layer_height - horizontal_dist) > min_layer_height) {
                    height -= (min_layer_height - horizontal_dist);
#ifdef SLIC3R_DEBUG
                    std::cout << "Shrink layer height to " << height << std::endl;
#endif
                } else {
                    height += horizontal_dist;
#ifdef SLIC3R_DEBUG
                    std::cout << "Widen layer height to " << height << std::endl;
#endif
                }
            }
        }
#endif
        height = std::min(cusp_height, height);

        // [HAZARD] z-gradation and custom range Perl code are permanently commented out.
        // z-gradation would quantize height to multiples of a user step (never ported from Perl).
        // The custom range lookup below (object.layer_config_ranges) IS active and overrides
        // the adaptive algorithm within user-configured ranges.

        // apply z-gradation
        /*
        my $gradation = $self->config->get_value('adaptive_slicing_z_gradation');
        if($gradation > 0) {
            $height = $height - unscale((scale($height)) % (scale($gradation)));
        }
        */

        // look for an applicable custom range
        /*
        if (my $range = first { $_->[0] <= $print_z && $_->[1] > $print_z } @{$self->layer_height_ranges}) {
            $height = $range->[2];

            # if user set custom height to zero we should just skip the range and resume slicing over it
            if ($height == 0) {
                $print_z += $range->[1] - $range->[0];
                next;
            }
        }
        */
        // [INTENT] BBS: clamp height change to LAYER_HEIGHT_CHANGE_STEP (0.04 mm) per layer.
        // This prevents abrupt stair-step height transitions that could cause print quality issues.
        // The check uses layer_height_profile.back() which is the PREVIOUS layer's height (odd index).
        if (layer_height_profile.back() < height && height - layer_height_profile.back() > LAYER_HEIGHT_CHANGE_STEP)
            height = layer_height_profile.back() + LAYER_HEIGHT_CHANGE_STEP;
        else if (layer_height_profile.back() > height && layer_height_profile.back() - height > LAYER_HEIGHT_CHANGE_STEP)
            height = layer_height_profile.back() - LAYER_HEIGHT_CHANGE_STEP;

        // [INTENT] User-configured per-Z ranges override the adaptive height within those ranges.
        // The range check is inclusive on both ends — layer at exactly range.second gets overridden.
        for (auto const& [range, options] : object.layer_config_ranges) {
            if (print_z >= range.first && print_z <= range.second) {
                height = options.opt_float("layer_height");
                break;
            };
        };

        layer_height_profile.push_back(print_z);
        layer_height_profile.push_back(height);
        print_z += height;
    }

    // [INTENT] Handle the final gap: if the last accumulated print_z does not reach the object top,
    // append one more entry to close the profile. Height is clamped to [min, max].
    double z_gap = slicing_params.object_print_z_uncompensated_height() - *(layer_height_profile.end() - 2);
    if (z_gap > 0.0) {
        layer_height_profile.push_back(slicing_params.object_print_z_uncompensated_height());
        layer_height_profile.push_back(std::clamp(z_gap, slicing_params.min_layer_height, slicing_params.max_layer_height));
    }

    return layer_height_profile;
}

// [INTENT] smooth_height_profile — apply a biased Gaussian blur to a layer height profile.
// The bias moves the smoothed values toward min_layer_height (favors thinner layers for
// better surface quality after smoothing). Optionally enforces keep_min (no height increase).
//
// [HAZARD] The outer loop runs EXACTLY 6 passes. The has_steep_height_change adaptive
//   termination logic is permanently commented out (BBS). The 6-pass hardcode means:
//   - Under-smoothed profiles (steep changes remain) get the same treatment as flat ones.
//   - Over-smoothed profiles (already smooth after 2 passes) still run all 6.
//
// [STATE] Stateless: pure function. No shared state.
//
// Gaussian kernel: sigma = 0.3*(radius-1)+0.8 (OpenCV AKAZE convention).
//   The kernel is unnormalized in the weighted blur loop — weighting uses sqrt(dh/delta_h)
//   which biases toward sample points that are closer to max_layer_height. This is the
//   "bias toward min" effect: samples near max get more weight, pulling average DOWN.
//
// [COUPLING] Called by UI layer height editor and by layer_height_profile_adaptive post-processing.
std::vector<double> smooth_height_profile(const std::vector<double>&          profile,
                                          const SlicingParameters&            slicing_params,
                                          const HeightProfileSmoothingParams& smoothing_params)
{
    auto gauss_blur = [&slicing_params](const std::vector<double>&          profile,
                                        const HeightProfileSmoothingParams& smoothing_params) -> std::vector<double> {
        // [INTENT] Gaussian kernel construction (OpenCV AKAZE convention).
        // sigma = 0.3*(radius-1)+0.8; kernel size = 2*radius+1.
        // The kernel values are used as weights in a 1D convolution over the profile.
        auto gauss_kernel = [](unsigned int radius) -> std::vector<double> {
            unsigned int        size = 2 * radius + 1;
            std::vector<double> ret;
            ret.reserve(size);

            // Reworked from static inline int getGaussianKernelSize(float sigma) taken from
            // opencv-4.1.2\modules\features2d\src\kaze\AKAZEFeatures.cpp
            double sigma                    = 0.3 * (double) (radius - 1) + 0.8;
            double two_sq_sigma             = 2.0 * sigma * sigma;
            double inv_root_two_pi_sq_sigma = 1.0 / ::sqrt(M_PI * two_sq_sigma);

            for (unsigned int i = 0; i < size; ++i) {
                double x = (double) i - (double) radius;
                ret.push_back(inv_root_two_pi_sq_sigma * ::exp(-x * x / two_sq_sigma));
            }

            return ret;
        };

        // skip first layer ?
        size_t skip_count = slicing_params.first_object_layer_height_fixed() ? 4 : 0;

        // not enough data to smmoth
        if ((int) profile.size() - (int) skip_count < 6)
            return profile;

        unsigned int        radius     = std::max(smoothing_params.radius, (unsigned int) 1);
        std::vector<double> kernel     = gauss_kernel(radius);
        int                 two_radius = 2 * (int) radius;

        std::vector<double> ret;
        size_t              size = profile.size();
        ret.reserve(size);

        // leave first layer untouched
        for (size_t i = 0; i < skip_count; ++i) {
            ret.push_back(profile[i]);
        }

        // smooth the rest of the profile by biasing a gaussian blur
        // the bias moves the smoothed profile closer to the min_layer_height
        double delta_h     = slicing_params.max_layer_height - slicing_params.min_layer_height;
        double inv_delta_h = (delta_h != 0.0) ? 1.0 / delta_h : 1.0;

        double max_dz_band = (double) radius * slicing_params.layer_height;
        for (size_t i = skip_count; i < size; i += 2) {
            double zi = profile[i];
            double hi = profile[i + 1];
            ret.push_back(zi);
            ret.push_back(0.0);
            double& height       = ret.back();
            int     begin        = std::max((int) i - two_radius, (int) skip_count);
            int     end          = std::min((int) i + two_radius, (int) size - 2);
            double  weight_total = 0.0;
            for (int j = begin; j <= end; j += 2) {
                int    kernel_id = radius + (j - (int) i) / 2;
                double dz        = std::abs(zi - profile[j]);
                if (dz * slicing_params.layer_height <= max_dz_band) {
                    double dh     = std::abs(slicing_params.max_layer_height - profile[j + 1]);
                    double weight = kernel[kernel_id] * sqrt(dh * inv_delta_h);
                    height += weight * profile[j + 1];
                    weight_total += weight;
                }
            }

            height = std::clamp(weight_total == 0 ? hi : height / weight_total, slicing_params.min_layer_height,
                                slicing_params.max_layer_height);
            if (smoothing_params.keep_min)
                height = std::min(height, hi);
        }

        return ret;
    };

    // BBS: avoid the layer height change to be too steep
    // auto has_steep_height_change = [&slicing_params](const std::vector<double>& profile, const double height_step) {
    //     //BBS: skip first layer
    //     size_t skip_count = slicing_params.first_object_layer_height_fixed() ? 4 : 0;
    //     size_t size = profile.size();
    //     //BBS: not enough data to smmoth, return false directly
    //     if ((int)size - (int)skip_count < 6)
    //         return false;

    //    //BBS: Don't need to check the difference between top layer and the last 2th layer
    //    for (size_t i = skip_count; i < size - 6; i += 2) {
    //        if (abs(profile[i + 1] - profile[i + 3]) > height_step)
    //            return true;
    //    }
    //    return false;
    // [HAZARD] has_steep_height_change adaptive termination is permanently commented out.
    // The while loop runs EXACTLY 6 passes regardless of whether the profile is already smooth.
    // Reactivating adaptive termination would reduce over-smoothing for already-smooth profiles.
    int                 count = 0;
    std::vector<double> ret   = profile;
    // bool has_steep_change = has_steep_height_change(ret, LAYER_HEIGHT_CHANGE_STEP);
    while (/*has_steep_change &&*/ count < 6) {
        ret = gauss_blur(ret, smoothing_params);
        // has_steep_change = has_steep_height_change(ret, LAYER_HEIGHT_CHANGE_STEP);
        count++;
    }
    return ret;
    // return gauss_blur(profile, smoothing_params);
}

// [INTENT] adjust_layer_height_profile — interactive layer height editor action.
// Modifies the layer height profile in-place in response to a UI gesture:
//   INCREASE/DECREASE: add/subtract layer_thickness_delta * cosine_weight within band.
//   REDUCE: move height toward the nominal layer_height within band.
//   SMOOTH: resample within band then apply 6 passes of local weighted average.
//
// The modification is applied with a cosine falloff window: weight = 0.5*(1+cos(2π*(z-center)/band)).
// This gives a smooth tapering from full-weight at center to zero at band edges.
//
// [STATE] Modifies layer_height_profile in place. The profile must be valid (size >= 2,
//   last z entry == object_print_z_uncompensated_height). Asserts enforce this.
//
// [COUPLING] Called by the UI GLCanvas3D layer height editor on mouse drag events.
//   Also called from the Lua/Python scripting layer height API (if any).
//
// [HAZARD] The z_step = 0.1 mm resampling step in the band densification phase is
//   hardcoded. For very fine profiles (layer_height < 0.1 mm), this step may actually
//   be COARSER than the existing profile resolution — the resampling would REDUCE
//   precision rather than increase it within the band.
void adjust_layer_height_profile(const ModelObject&        model_object,
                                 const SlicingParameters&  slicing_params,
                                 std::vector<coordf_t>&    layer_height_profile,
                                 coordf_t                  z,
                                 coordf_t                  layer_thickness_delta,
                                 coordf_t                  band_width,
                                 LayerHeightEditActionType action)
{
    // [INTENT] z_span_variable defines the Z range open to modification.
    // If the first layer height is fixed, edits cannot touch the first layer.
    std::pair<coordf_t, coordf_t> z_span_variable = std::pair<coordf_t, coordf_t>(slicing_params.first_object_layer_height_fixed() ?
                                                                                      slicing_params.first_object_layer_height :
                                                                                      0.,
                                                                                  slicing_params.object_print_z_uncompensated_height());
    if (z < z_span_variable.first || z > z_span_variable.second)
        return;

    assert(layer_height_profile.size() >= 2);
    assert(std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_params.object_print_z_uncompensated_height()) < EPSILON);

    // 1) Get the current layer thickness at z.
    // [INTENT] Linearly interpolate within the profile to get height at cursor position z.
    coordf_t current_layer_height = slicing_params.layer_height;
    for (size_t i = 0; i < layer_height_profile.size(); i += 2) {
        if (i + 2 == layer_height_profile.size()) {
            current_layer_height = layer_height_profile[i + 1];
            break;
        } else if (layer_height_profile[i + 2] > z) {
            coordf_t z1          = layer_height_profile[i];
            coordf_t h1          = layer_height_profile[i + 1];
            coordf_t z2          = layer_height_profile[i + 2];
            coordf_t h2          = layer_height_profile[i + 3];
            current_layer_height = lerp(h1, h2, (z - z1) / (z2 - z1));
            break;
        }
    }

    // [INTENT] Do not allow editing within user-configured per-Z ranges. The range check
    // uses a ±current_layer_height tolerance to catch cursors near range boundaries.
    for (auto const& [range, options] : model_object.layer_config_ranges) {
        if (z >= range.first - current_layer_height && z <= range.second + current_layer_height)
            return;
    };

    // 2) Is it possible to apply the delta?
    // [INTENT] Pre-clip delta so the resulting height stays in [min_layer_height, max_layer_height].
    // For REDUCE/SMOOTH, convert to positive magnitude; delta is direction of approach to nominal.
    switch (action) {
    case LAYER_HEIGHT_EDIT_ACTION_DECREASE:
        layer_thickness_delta = -layer_thickness_delta;
        // fallthrough
    case LAYER_HEIGHT_EDIT_ACTION_INCREASE:
        if (layer_thickness_delta > 0) {
            if (current_layer_height >= slicing_params.max_layer_height - EPSILON)
                return;
            layer_thickness_delta = std::min(layer_thickness_delta, slicing_params.max_layer_height - current_layer_height);
        } else {
            if (current_layer_height <= slicing_params.min_layer_height + EPSILON)
                return;
            layer_thickness_delta = std::max(layer_thickness_delta, slicing_params.min_layer_height - current_layer_height);
        }
        break;
    case LAYER_HEIGHT_EDIT_ACTION_REDUCE:
    case LAYER_HEIGHT_EDIT_ACTION_SMOOTH:
        layer_thickness_delta = std::abs(layer_thickness_delta);
        layer_thickness_delta = std::min(layer_thickness_delta, std::abs(slicing_params.layer_height - current_layer_height));
        if (layer_thickness_delta < EPSILON)
            return;
        break;
    default: assert(false); break;
    }

    // 3) Densify the profile inside z +- band_width/2, remove duplicate Zs from the height profile inside the band.
    // [INTENT] The profile is resampled at z_step = 0.1 mm within [lo, hi] to allow smooth editing.
    // z_step is hardcoded — see HAZARD note in function header about coarse resampling.
    coordf_t lo = std::max(z_span_variable.first, z - 0.5 * band_width);
    // Do not limit the upper side of the band, so that the modifications to the top point of the profile will be allowed.
    coordf_t hi     = z + 0.5 * band_width;
    coordf_t z_step = 0.1;
    size_t   idx    = 0;
    while (idx < layer_height_profile.size() && layer_height_profile[idx] < lo)
        idx += 2;
    idx -= 2;

    std::vector<double> profile_new;
    profile_new.reserve(layer_height_profile.size());
    assert(idx >= 0 && idx + 1 < layer_height_profile.size());
    profile_new.insert(profile_new.end(), layer_height_profile.begin(), layer_height_profile.begin() + idx + 2);
    coordf_t zz                = lo;
    size_t   i_resampled_start = profile_new.size();
    while (zz < hi) {
        size_t   next   = idx + 2;
        coordf_t z1     = layer_height_profile[idx];
        coordf_t h1     = layer_height_profile[idx + 1];
        coordf_t height = h1;
        if (next < layer_height_profile.size()) {
            coordf_t z2 = layer_height_profile[next];
            coordf_t h2 = layer_height_profile[next + 1];
            height      = lerp(h1, h2, (zz - z1) / (z2 - z1));
        }
        // [INTENT] Apply the edit action with cosine-weighted taper from center.
        // weight = 0 at band edges, weight = 1 at z (center of brush).
        coordf_t weight = std::abs(zz - z) < 0.5 * band_width ? (0.5 + 0.5 * cos(2. * M_PI * (zz - z) / band_width)) : 0.;
        switch (action) {
        case LAYER_HEIGHT_EDIT_ACTION_INCREASE:
        case LAYER_HEIGHT_EDIT_ACTION_DECREASE: height += weight * layer_thickness_delta; break;
        case LAYER_HEIGHT_EDIT_ACTION_REDUCE: {
            coordf_t delta = height - slicing_params.layer_height;
            coordf_t step  = weight * layer_thickness_delta;
            step           = (std::abs(delta) > step) ? (delta > 0) ? -step : step : -delta;
            height += step;
            break;
        }
        case LAYER_HEIGHT_EDIT_ACTION_SMOOTH: {
            // Don't modify the profile during resampling process, do it at the next step.
            break;
        }
        default: assert(false); break;
        }
        height = std::clamp(height, slicing_params.min_layer_height, slicing_params.max_layer_height);
        if (zz == z_span_variable.second) {
            // This is the last point of the profile.
            if (profile_new[profile_new.size() - 2] + EPSILON > zz) {
                profile_new.pop_back();
                profile_new.pop_back();
            }
            profile_new.push_back(zz);
            profile_new.push_back(height);
            idx = layer_height_profile.size();
            break;
        }
        // Avoid entering a too short segment.
        if (profile_new[profile_new.size() - 2] + EPSILON < zz) {
            profile_new.push_back(zz);
            profile_new.push_back(height);
        }
        // Limit zz to the object height, so the next iteration the last profile point will be set.
        zz  = std::min(zz + z_step, z_span_variable.second);
        idx = next;
        while (idx < layer_height_profile.size() && layer_height_profile[idx] < zz)
            idx += 2;
        idx -= 2;
    }

    idx += 2;
    assert(idx > 0);
    size_t i_resampled_end = profile_new.size();
    if (idx < layer_height_profile.size()) {
        assert(zz >= layer_height_profile[idx - 2]);
        assert(zz <= layer_height_profile[idx]);
        profile_new.insert(profile_new.end(), layer_height_profile.begin() + idx, layer_height_profile.end());
    } else if (profile_new[profile_new.size() - 2] + 0.5 * EPSILON < z_span_variable.second) {
        profile_new.insert(profile_new.end(), layer_height_profile.end() - 2, layer_height_profile.end());
    }
    layer_height_profile = std::move(profile_new);

    if (action == LAYER_HEIGHT_EDIT_ACTION_SMOOTH) {
        // [INTENT] SMOOTH action — apply a local weighted average 6 times within the band.
        // i_resampled_start/end define the band in the freshly resampled profile_new.
        // The ±1 adjustments clamp to exclude profile boundary entries from averaging.
        // [STATE] profile_new is a snapshot of layer_height_profile at start of each pass.
        //   Each pass reads from profile_new and writes averaged values into layer_height_profile.
        //   6 passes are always executed (same hardcoded count as smooth_height_profile()).
        // [CONCURRENCY] Not called concurrently — UI gesture handler, single-threaded.
        if (i_resampled_start == 0)
            ++i_resampled_start;
        if (i_resampled_end == layer_height_profile.size())
            i_resampled_end -= 2;
        size_t n_rounds = 6;
        for (size_t i_round = 0; i_round < n_rounds; ++i_round) {
            profile_new = layer_height_profile;
            for (size_t i = i_resampled_start; i < i_resampled_end; i += 2) {
                coordf_t zz = profile_new[i];
                // [INTENT] Cosine taper: t ranges 0..0.25; weight is 0.25 at band center, 0 at edges.
                // The 0.25 scale ensures the average blends in nearest-neighbor heights at quarter weight.
                coordf_t t = std::abs(zz - z) < 0.5 * band_width ? (0.25 + 0.25 * cos(2. * M_PI * (zz - z) / band_width)) : 0.;
                assert(t >= 0. && t <= 0.5000001);
                // [INTENT] Three-case average:
                //   i == 0:            use only the right neighbor (no left neighbor available).
                //   i+1 == end:        use only the left neighbor (no right neighbor available).
                //   interior:          average of left and right neighbors, equally weighted.
                if (i == 0)
                    layer_height_profile[i + 1] = (1. - t) * profile_new[i + 1] + t * profile_new[i + 3];
                else if (i + 1 == profile_new.size())
                    layer_height_profile[i + 1] = (1. - t) * profile_new[i + 1] + t * profile_new[i - 1];
                else
                    layer_height_profile[i + 1] = (1. - t) * profile_new[i + 1] + 0.5 * t * (profile_new[i - 1] + profile_new[i + 3]);
            }
        }
    }

    // [INTENT] Final assertions — verify the profile is still well-formed after editing.
    // These are debug-only; release builds skip. Invariants:
    //   size > 2, even count, starts at z=0, ends at object_print_z_uncompensated_height.
    //   Z values are monotonically non-decreasing; all heights within [min, max].
    assert(layer_height_profile.size() > 2);
    assert(layer_height_profile.size() % 2 == 0);
    assert(layer_height_profile[0] == 0.);
    assert(std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_params.object_print_z_uncompensated_height()) < EPSILON);
#ifdef _DEBUG
    for (size_t i = 2; i < layer_height_profile.size(); i += 2)
        assert(layer_height_profile[i - 2] <= layer_height_profile[i]);
    for (size_t i = 1; i < layer_height_profile.size(); i += 2) {
        assert(layer_height_profile[i] > slicing_params.min_layer_height - EPSILON);
        assert(layer_height_profile[i] < slicing_params.max_layer_height + EPSILON);
    }
#endif /* _DEBUG */
}

// [INTENT] adjust_layer_series_to_align_object_height — BBS/Orca addition.
// After generate_object_layers(), the last layer boundary may not exactly equal
// object_print_z_height() due to floating-point accumulation. This function
// attempts to fix that by redistributing the error across the last 5 layers,
// adjusting each layer height within [min_layer_height, max_layer_height].
//
// [STATE] Modifies layer_series in place. layer_series is the flat [lo, hi] vector
//   from generate_object_layers() — pairs of coordf_t boundary values.
//
// [HAZARD] Requires at least 12 entries (6 layers including first layer) — returns false
//   if insufficient layers exist. The caller (generate_object_layers) ignores the
//   return value, so alignment failures are silently skipped.
//
// [HAZARD] The 5-layer correction window is hardcoded. For objects with exactly
//   5 variable layers at the top, this works correctly. For uniform-height objects,
//   the "last 5 layers" at identical height may all hit their bounds simultaneously
//   and still fail to close the gap (returns false).
//
// [HAZARD] Uses abs() instead of std::abs() for coordf_t — relies on implicit
//   double→int→double conversion in some compilers. Should be std::abs() or fabs().
bool adjust_layer_series_to_align_object_height(const SlicingParameters& slicing_params, std::vector<coordf_t>& layer_series)
{
    coordf_t object_height = slicing_params.object_print_z_height();
    if (is_approx(layer_series.back(), object_height))
        return true;

    // need at least 5 + 1(first_layer) layers to adjust the height
    size_t layer_size = layer_series.size();
    if (layer_size < 12)
        return false;

    std::vector<coordf_t> last_5_layers_heght;
    for (size_t i = 0; i < 5; ++i) {
        last_5_layers_heght.emplace_back(layer_series[layer_size - 10 + 2 * i + 1] - layer_series[layer_size - 10 + 2 * i]);
    }

    coordf_t          gap = abs(layer_series.back() - object_height);
    std::vector<bool> can_adjust(5, true); // to record whether every layer can adjust layer height
    bool              taller_than_object = layer_series.back() < object_height;

    auto get_valid_size = [&can_adjust]() -> int {
        int valid_size = 0;
        for (auto b_adjust : can_adjust) {
            valid_size += b_adjust ? 1 : 0;
        }
        return valid_size;
    };

    auto adjust_layer_height = [&slicing_params, &last_5_layers_heght, &can_adjust, &get_valid_size,
                                &taller_than_object](coordf_t gap) -> coordf_t {
        coordf_t delta_gap  = gap / get_valid_size();
        coordf_t remain_gap = 0;
        for (size_t i = 0; i < last_5_layers_heght.size(); ++i) {
            coordf_t& l_height = last_5_layers_heght[i];
            if (taller_than_object) {
                if (can_adjust[i] && is_approx(l_height, slicing_params.max_layer_height)) {
                    remain_gap += delta_gap;
                    can_adjust[i] = false;
                    continue;
                }

                if (can_adjust[i] && l_height + delta_gap > slicing_params.max_layer_height) {
                    remain_gap += l_height + delta_gap - slicing_params.max_layer_height;
                    l_height      = slicing_params.max_layer_height;
                    can_adjust[i] = false;
                } else {
                    l_height += delta_gap;
                }
            } else {
                if (can_adjust[i] && is_approx(l_height, slicing_params.min_layer_height)) {
                    remain_gap += delta_gap;
                    can_adjust[i] = false;
                    continue;
                }

                if (can_adjust[i] && l_height - delta_gap < slicing_params.min_layer_height) {
                    remain_gap += slicing_params.min_layer_height + delta_gap - l_height;
                    l_height      = slicing_params.min_layer_height;
                    can_adjust[i] = false;
                } else {
                    l_height -= delta_gap;
                }
            }
        }
        return remain_gap;
    };

    while (gap > 0) {
        int valid_size = get_valid_size();
        if (valid_size == 0) {
            // [INTENT] All 5 layers are already at their bounds (max or min) and still
            // cannot close the gap. Return false — alignment failed silently.
            return false;
        }

        gap = adjust_layer_height(gap);
        if (is_approx(gap, 0.0)) {
            // adjust succeed
            break;
        }
    }

    // [INTENT] Write the adjusted heights back into layer_series.
    // For each of the 5 layers (i=0..4), update [lo, hi] pairs in layer_series.
    // The lo of layer i > 0 is set equal to the hi of layer i-1 to guarantee continuity
    // (avoids gaps or overlaps from independent rounding).
    for (size_t i = 0; i < last_5_layers_heght.size(); ++i) {
        if (i > 0) {
            layer_series[layer_size - 10 + 2 * i] = layer_series[layer_size - 10 + 2 * i - 1];
        }
        layer_series[layer_size - 10 + 2 * i + 1] = layer_series[layer_size - 10 + 2 * i] + last_5_layers_heght[i];
    }

    return true;
}

// [INTENT] generate_object_layers — convert a [z_i, h_i] profile into a flat [lo, hi] boundary vector.
// Each pair (out[2i], out[2i+1]) = (lo, hi) defines one layer boundary in print-space Z (mm).
//
// Algorithm:
//   1. If first layer height is fixed, push [0, first_object_layer_height] directly.
//   2. Step print_z forward: at each iteration, compute slice_z = print_z + 0.5*min_layer_height
//      to probe the profile at the layer midpoint. Look up height h in the profile at slice_z
//      (applying shrinkage_compensation_z to map profile-space z into print-space z).
//   3. Push [print_z, print_z + h] and advance. Stop when slice_z >= object_print_z_height().
//   4. If is_precise_z_height, call adjust_layer_series_to_align_object_height() to close
//      any floating-point accumulation gap between the last layer and the object top.
//
// [MEMORY] Output is a flat vector<coordf_t> with 2N entries: [lo0, hi0, lo1, hi1, ...].
//   Consecutive pairs share boundaries: hi[i] == lo[i+1] (guaranteed by the step logic,
//   not by explicit copy — rounding errors are corrected by adjust_layer_series).
//
// [COUPLING] Called by PrintObject::_slice_region() and by the UI layer height preview.
//   The `layers` output is later used by generate_layer_height_texture() for visualization.
//
// [HAZARD] shrinkage_compensation_z is applied to the profile Z lookups but NOT to the
//   emitted [lo, hi] boundaries. The output is in print-space (compensated). The profile
//   is in object-space (uncompensated). This means the profile lookup uses:
//     slice_z (print-space) vs layer_height_profile[next] * shrinkage_compensation_z
//   which is correct, but it means downstream consumers of `out` see compensated Z values.
//
// [HAZARD] The loop terminates when slice_z = print_z + 0.5*min_layer_height >= object_print_z_height().
//   This means the last layer may not reach exactly object_print_z_height() — the caller of
//   adjust_layer_series_to_align_object_height() fixes this when is_precise_z_height is set.
//   When is_precise_z_height is false, the top may be slightly short.
std::vector<coordf_t> generate_object_layers(const SlicingParameters&     slicing_params,
                                             const std::vector<coordf_t>& layer_height_profile,
                                             bool                         is_precise_z_height)
{
    assert(!layer_height_profile.empty());

    coordf_t print_z = 0;
    coordf_t height  = 0;

    std::vector<coordf_t> out;

    if (slicing_params.first_object_layer_height_fixed()) {
        // [INTENT] Push the fixed first layer directly without consulting the profile.
        // The profile height lookup begins from first_object_layer_height onward.
        out.push_back(0);
        print_z = slicing_params.first_object_layer_height;
        out.push_back(print_z);
    }

    // Orca: XYZ shrinkage compensation
    // [INTENT] shrinkage_compensation_z scales profile Z lookups to account for Z-axis
    // material shrinkage. Profile Z values are in object-space; multiplying by this factor
    // converts them to print-space for comparison with slice_z.
    const coordf_t shrinkage_compensation_z = slicing_params.object_shrinkage_compensation_z;
    size_t         idx_layer_height_profile = 0;
    // loop until we have at least one layer and the max slice_z reaches the object height
    // [INTENT] slice_z is the midpoint probe: we check at print_z + 0.5*min_layer_height.
    // This ensures we stop generating layers before the midpoint would exceed the object top.
    coordf_t slice_z = print_z + 0.5 * slicing_params.min_layer_height;
    while (slice_z < slicing_params.object_print_z_height()) {
        height = slicing_params.min_layer_height;
        if (idx_layer_height_profile < layer_height_profile.size()) {
            // [INTENT] Advance idx_layer_height_profile to the last profile entry whose
            // compensated Z is <= slice_z. This is a forward scan that never rewinds —
            // correct only if slice_z is strictly increasing each iteration (which it is).
            size_t next = idx_layer_height_profile + 2;
            for (;;) {
                // Orca: XYZ shrinkage compensation
                if (next >= layer_height_profile.size() || slice_z < layer_height_profile[next] * shrinkage_compensation_z)
                    break;
                idx_layer_height_profile = next;
                next += 2;
            }
            // Orca: XYZ shrinkage compensation
            // [INTENT] Interpolate height between profile entries bracketing slice_z.
            // If at the last entry (no next), use h1 directly (no interpolation needed).
            const coordf_t z1 = layer_height_profile[idx_layer_height_profile] * shrinkage_compensation_z;
            const coordf_t h1 = layer_height_profile[idx_layer_height_profile + 1];
            height            = h1;
            if (next < layer_height_profile.size()) {
                // Orca: XYZ shrinkage compensation
                const coordf_t z2 = layer_height_profile[next] * shrinkage_compensation_z;
                const coordf_t h2 = layer_height_profile[next + 1];
                height            = lerp(h1, h2, (slice_z - z1) / (z2 - z1));
                assert(height >= slicing_params.min_layer_height - EPSILON && height <= slicing_params.max_layer_height + EPSILON);
            }
        }
        // [INTENT] Re-probe slice_z at print_z + 0.5*height (the actual midpoint) to
        // confirm the layer still fits. If the updated midpoint exceeds the object top, stop.
        slice_z = print_z + 0.5 * height;
        if (slice_z >= slicing_params.object_print_z_height())
            break;
        assert(height > slicing_params.min_layer_height - EPSILON);
        assert(height < slicing_params.max_layer_height + EPSILON);
        out.push_back(print_z);
        print_z += height;
        // [INTENT] Advance slice_z by min_layer_height for early termination check at loop top.
        slice_z = print_z + 0.5 * slicing_params.min_layer_height;
        out.push_back(print_z);
    }

    if (is_precise_z_height)
        // [INTENT] Attempt to close the gap between out.back() and object_print_z_height()
        // by redistributing error across the last 5 layers. Silently ignored on failure.
        adjust_layer_series_to_align_object_height(slicing_params, out);
    return out;
}

// [INTENT] check_object_layers_fixed — returns true if the layer height profile describes
// a uniform fixed-height profile (i.e., the user has not configured any variable heights).
//
// A "fixed" profile has exactly one step or two steps (the second to represent the first
// object layer being fixed), and all heights are equal to slicing_params.layer_height.
//
// Expected profile shapes for a "fixed" profile:
//   4-entry  [0, h, z_top, h]                           — uniform, no fixed first layer
//   8-entry  [0, h_first, z1, h_first, z1, h, z_top, h] — fixed first layer, then uniform
//
// [COUPLING] Called by PrintObject to determine whether variable layer height data needs
// to be applied or whether the default uniform layering applies. If this returns true,
// the expensive generate_object_layers() profiling path can be skipped.
//
// [HAZARD] The check is brittle: any profile that doesn't have exactly 4 or 8 entries
// (e.g. from a UI edit that left extra transition points) will return false even if the
// height is effectively uniform. This can cause unnecessary variable-height processing.
bool check_object_layers_fixed(const SlicingParameters& slicing_params, const std::vector<coordf_t>& layer_height_profile)
{
    assert(layer_height_profile.size() >= 4);
    assert(layer_height_profile.size() % 2 == 0);
    assert(layer_height_profile[0] == 0);

    // [INTENT] Only recognize exactly 4-entry (one-step) or 8-entry (two-step) profiles.
    if (layer_height_profile.size() != 4 && layer_height_profile.size() != 8)
        return false;

    // [INTENT] fixed_step1: check that the first height equals the second height (no change in step 1).
    // fixed_step2: for 8-entry profiles, check that the second step also has uniform height, AND
    //   the Z transition is continuous (layer_height_profile[2] == [4]).
    bool fixed_step1 = is_approx(layer_height_profile[1], layer_height_profile[3]);
    bool fixed_step2 = layer_height_profile.size() == 4 ||
                       (layer_height_profile[2] == layer_height_profile[4] && is_approx(layer_height_profile[5], layer_height_profile[7]));

    if (!fixed_step1 || !fixed_step2)
        return false;

    // [INTENT] Check that the first step height matches the configured first_object_layer_height.
    // layer_height_profile[2] must be at least 0.5 * first_object_layer_height to confirm
    // the profile Z boundary is positioned consistently with the fixed first layer.
    if (layer_height_profile[2] < 0.5 * slicing_params.first_object_layer_height + EPSILON ||
        !is_approx(layer_height_profile[3], slicing_params.first_object_layer_height))
        return false;

    // [INTENT] For a 4-entry profile: the transition Z (z_max) must be beyond the second-layer
    // midpoint. If it is, the profile is trivially uniform (only one layer step).
    double z_max = layer_height_profile[layer_height_profile.size() - 2];
    double z_2nd = slicing_params.first_object_layer_height + 0.5 * slicing_params.layer_height;
    if (z_2nd > z_max)
        return true;
    // [INTENT] For an 8-entry profile: also verify the last height entry equals the configured
    // uniform layer_height, and that the penultimate Z entry covers the second-layer midpoint.
    if (z_2nd < *(layer_height_profile.end() - 4) + EPSILON || !is_approx(layer_height_profile.back(), slicing_params.layer_height))
        return false;

    return true;
}

// [INTENT] generate_layer_height_texture — render a layer height profile as a 2D RGBA texture
// for the UI layer height editor overlay. The texture encodes layer height as a color using
// a diverging 8-color palette (green=thin, red=thick), modulated by a cosine intensity to
// give visible "bands" that indicate individual layer boundaries.
//
// Texture layout:
//   Primary texture (LOD 0): rows × cols pixels, RGBA8, stored at `data`.
//   Secondary texture (LOD 1, half resolution): rows × cols/2 pixels, RGBA8, at data + rows*cols*4.
//   The 2D texture encodes a 1D layer sequence using cell = row * (cols-1) + col.
//   Boundary cells (col == 0, row > 0) are duplicated as the last pixel of the preceding row,
//   preventing visible gaps at row wrap-around in the GPU sampler.
//
// [MEMORY] `data` is a raw void* buffer; caller must allocate rows * cols * (level_of_detail_2nd_level ? 5 : 4) bytes.
//   Note: memset at top is commented out — the buffer is NOT zeroed before writing.
//   Cells not covered by any layer remain uninitialized.
//
// [STATE] Pure function — no side effects beyond writing into `data`.
//
// [COUPLING] Called from GUI3DScene / GLCanvas3D layer height editor when the profile changes.
//   The `layers` input is the output of generate_object_layers().
//
// [HAZARD] ncells is capped at 16 * (object_height / min_layer_height) to prevent
//   extremely large texture allocations for fine profiles. For objects taller than
//   rows * (cols-1) / 16 * min_layer_height mm, the cap takes effect and some layers
//   will share a cell — losing per-layer resolution in the visualization.
//
// [HAZARD] The intensity function cos(π * 0.7 * (mid-z)/h) uses 0.7 instead of 1.0
//   to avoid full darkness at layer edges (avoids pure black bands). This is a visual
//   tuning parameter, not a physical property.
int generate_layer_height_texture(const SlicingParameters&     slicing_params,
                                  const std::vector<coordf_t>& layers,
                                  void*                        data,
                                  int                          rows,
                                  int                          cols,
                                  bool                         level_of_detail_2nd_level)
{
    // https://github.com/aschn/gnuplot-colorbrewer
    // [INTENT] 8-color ColorBrewer RdYlGn diverging palette from green (thin) to red (thick).
    // Indices 0-7: darkgreen → lightgreen → yellow → lightyellow → lightorange → orange → darkorange → darkred.
    std::vector<Vec3crd> palette_raw;
    palette_raw.push_back(Vec3crd(0x01A, 0x098, 0x050));
    palette_raw.push_back(Vec3crd(0x066, 0x0BD, 0x063));
    palette_raw.push_back(Vec3crd(0x0A6, 0x0D9, 0x06A));
    palette_raw.push_back(Vec3crd(0x0D9, 0x0F1, 0x0EB));
    palette_raw.push_back(Vec3crd(0x0FE, 0x0E6, 0x0EB));
    palette_raw.push_back(Vec3crd(0x0FD, 0x0AE, 0x061));
    palette_raw.push_back(Vec3crd(0x0F4, 0x06D, 0x043));
    palette_raw.push_back(Vec3crd(0x0D7, 0x030, 0x027));

    // Clear the main texture and the 2nd LOD level.
    //	memset(data, 0, rows * cols * (level_of_detail_2nd_level ? 5 : 4));
    // [INTENT] data1 points to the secondary LOD texture immediately after the primary.
    // LOD1 is rows/2 * cols/2 pixels at the same RGBA8 format.
    // 2nd LOD level data start
    unsigned char* data1 = reinterpret_cast<unsigned char*>(data) + rows * cols * 4;
    // [INTENT] ncells: number of 1D texture cells for the primary LOD.
    // Capped at 16 * (object_height / min_layer_height) to bound texture size.
    // z_to_cell / cell_to_z: linear scale factors between print-Z and cell index.
    // hscale: used to normalize height deviation for color mapping.
    //   If all layers are identical height, hscale = layer_height to avoid division by zero.
    int ncells  = std::min((cols - 1) * rows, int(ceil(16. * (slicing_params.object_print_z_height() / slicing_params.min_layer_height))));
    int ncells1 = ncells / 2;
    int cols1   = cols / 2;
    coordf_t z_to_cell  = coordf_t(ncells - 1) / slicing_params.object_print_z_height();
    coordf_t cell_to_z  = slicing_params.object_print_z_height() / coordf_t(ncells - 1);
    coordf_t z_to_cell1 = coordf_t(ncells1 - 1) / slicing_params.object_print_z_height();
    // for color scaling
    coordf_t hscale = 2.f * std::max(slicing_params.max_layer_height - slicing_params.layer_height,
                                     slicing_params.layer_height - slicing_params.min_layer_height);
    if (hscale == 0)
        // All layers have the same height. Provide some height scale to avoid division by zero.
        hscale = slicing_params.layer_height;
    for (size_t idx_layer = 0; idx_layer < layers.size(); idx_layer += 2) {
        coordf_t lo  = layers[idx_layer];
        coordf_t hi  = layers[idx_layer + 1];
        coordf_t mid = 0.5f * (lo + hi);
        assert(mid <= slicing_params.object_print_z_height());
        coordf_t h = hi - lo;
        hi         = std::min(hi, slicing_params.object_print_z_height());
        // [INTENT] Map layer [lo, hi] to a range of texture cells [cell_first, cell_last].
        // All cells in this range are filled with the same color (derived from height h).
        int cell_first = std::clamp(int(ceil(lo * z_to_cell)), 0, ncells - 1);
        int cell_last  = std::clamp(int(floor(hi * z_to_cell)), 0, ncells - 1);
        for (int cell = cell_first; cell <= cell_last; ++cell) {
            // [INTENT] idxf: fractional palette index. Centered at 0.5*hscale (nominal height).
            // idxf=0 → min_layer_height → darkgreen; idxf=7 → max_layer_height → darkred.
            coordf_t       idxf   = (0.5 * hscale + (h - slicing_params.layer_height)) * coordf_t(palette_raw.size() - 1) / hscale;
            int            idx1   = std::clamp(int(floor(idxf)), 0, int(palette_raw.size() - 1));
            int            idx2   = std::min(int(palette_raw.size() - 1), idx1 + 1);
            coordf_t       t      = idxf - coordf_t(idx1);
            const Vec3crd& color1 = palette_raw[idx1];
            const Vec3crd& color2 = palette_raw[idx2];
            coordf_t       z      = cell_to_z * coordf_t(cell);
            assert(lo - EPSILON <= z && z <= hi + EPSILON);
            // [INTENT] Cosine intensity modulation: peak at layer midpoint, partial darkening at edges.
            // The 0.7 factor prevents full-black at edges (avoids purely dark boundary bands).
            coordf_t intensity = cos(M_PI * 0.7 * (mid - z) / h);
            // Color mapping from layer height to RGB.
            Vec3d color(intensity * lerp(coordf_t(color1(0)), coordf_t(color2(0)), t),
                        intensity * lerp(coordf_t(color1(1)), coordf_t(color2(1)), t),
                        intensity * lerp(coordf_t(color1(2)), coordf_t(color2(2)), t));
            // [INTENT] 2D texture cell layout: cell = row * (cols-1) + col.
            // Note: cols-1 (not cols) is the stride — the last pixel of each row is reserved
            // for the boundary duplication below.
            int row = cell / (cols - 1);
            int col = cell - row * (cols - 1);
            assert(row >= 0 && row < rows);
            assert(col >= 0 && col < cols);
            unsigned char* ptr = (unsigned char*) data + (row * cols + col) * 4;
            ptr[0]             = (unsigned char) std::clamp(int(floor(color(0) + 0.5)), 0, 255);
            ptr[1]             = (unsigned char) std::clamp(int(floor(color(1) + 0.5)), 0, 255);
            ptr[2]             = (unsigned char) std::clamp(int(floor(color(2) + 0.5)), 0, 255);
            ptr[3]             = 255;
            if (col == 0 && row > 0) {
                // [INTENT] Boundary duplication: col=0 of row N is the same Z position as
                // col=cols-1 of row N-1. Copy pixel backward to fill the row-end gap.
                // Duplicate the first value in a row as a last value of the preceding row.
                ptr[-4] = ptr[0];
                ptr[-3] = ptr[1];
                ptr[-2] = ptr[2];
                ptr[-1] = ptr[3];
            }
        }
        if (level_of_detail_2nd_level) {
            // [INTENT] LOD1 (half resolution) uses the same color mapping but ncells1/cols1.
            // Written to data1 (the second half of the buffer).
            cell_first = std::clamp(int(ceil(lo * z_to_cell1)), 0, ncells1 - 1);
            cell_last  = std::clamp(int(floor(hi * z_to_cell1)), 0, ncells1 - 1);
            for (int cell = cell_first; cell <= cell_last; ++cell) {
                coordf_t       idxf   = (0.5 * hscale + (h - slicing_params.layer_height)) * coordf_t(palette_raw.size() - 1) / hscale;
                int            idx1   = std::clamp(int(floor(idxf)), 0, int(palette_raw.size() - 1));
                int            idx2   = std::min(int(palette_raw.size() - 1), idx1 + 1);
                coordf_t       t      = idxf - coordf_t(idx1);
                const Vec3crd& color1 = palette_raw[idx1];
                const Vec3crd& color2 = palette_raw[idx2];
                // Color mapping from layer height to RGB.
                // [INTENT] LOD1 omits the cosine intensity modulation — cells are flat-colored.
                Vec3d color(lerp(coordf_t(color1(0)), coordf_t(color2(0)), t), lerp(coordf_t(color1(1)), coordf_t(color2(1)), t),
                            lerp(coordf_t(color1(2)), coordf_t(color2(2)), t));
                int   row = cell / (cols1 - 1);
                int   col = cell - row * (cols1 - 1);
                assert(row >= 0 && row < rows / 2);
                assert(col >= 0 && col < cols / 2);
                unsigned char* ptr = data1 + (row * cols1 + col) * 4;
                ptr[0]             = (unsigned char) std::clamp(int(floor(color(0) + 0.5)), 0, 255);
                ptr[1]             = (unsigned char) std::clamp(int(floor(color(1) + 0.5)), 0, 255);
                ptr[2]             = (unsigned char) std::clamp(int(floor(color(2) + 0.5)), 0, 255);
                ptr[3]             = 255;
                if (col == 0 && row > 0) {
                    // Duplicate the first value in a row as a last value of the preceding row.
                    ptr[-4] = ptr[0];
                    ptr[-3] = ptr[1];
                    ptr[-2] = ptr[2];
                    ptr[-1] = ptr[3];
                }
            }
        }
    }

    // Returns number of cells of the 0th LOD level.
    return ncells;
}

}; // namespace Slic3r
