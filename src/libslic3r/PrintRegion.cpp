// [INTENT] PrintRegion.cpp — per-region utility methods for flow calculation, extruder
//          resolution, and extruder collection.  A PrintRegion is a thin configuration
//          wrapper around a PrintRegionConfig; the actual geometry lives in LayerRegion
//          objects that reference a PrintRegion by pointer.  This file contains only the
//          implementation; the class definition is in Print.hpp.
//
// [STATE]  PrintRegion owns a PrintRegionConfig (m_config) and reference-count fields
//          (m_ref_cnt, m_print_object_region_id).  These fields are manipulated by
//          PrintApply.cpp friend helpers (print_region_ref_inc/reset/cnt) under the
//          Print::state_mutex() lock; all access here is single-threaded.
//
// [COUPLING] Depends on PrintConfig (nozzle_diameter array), Flow::new_from_config_width,
//            and the FlowRole enum.  Any change to FlowRole values requires matching
//            updates in this file's switch chains.

#include "Exception.hpp"
#include "Print.hpp"

namespace Slic3r {

// 1-based extruder identifier for this region and role.
// [INTENT] Resolves PrintRegionConfig filament slot (1-based) for a given extrusion role.
// [HAZARD] frSupportMaterial, frSupportMaterialInterface, frTopSolidInfill (when treated
//          differently) all fall through to the throw — callers must never pass those roles.
unsigned int PrintRegion::extruder(FlowRole role) const
{
    // [INTENT] Map FlowRole → 1-based filament slot.  0 is intentionally returned for
    //          wall_filament/sparse/solid when their config value is 0 (meaning "inherit").
    size_t extruder = 0;
    if (role == frPerimeter || role == frExternalPerimeter)
        extruder = m_config.wall_filament;
    else if (role == frInfill)
        extruder = m_config.sparse_infill_filament;
    else if (role == frSolidInfill || role == frTopSolidInfill)
        extruder = m_config.solid_infill_filament;
    else
        throw Slic3r::InvalidArgument("Unknown role");
    return extruder;
}

// [INTENT] Compute the actual Flow (mm width + mm height + mm^2/mm area) for a given
//          extrusion role in this region.  First-layer override takes the highest priority;
//          zero config_width falls back to the object-level line_width setting.
// [COUPLING] Delegates to Flow::new_from_config_width, which may return a zero-area flow
//            if nozzle_diameter is 0 — a sign that the printer config is incomplete.
Flow PrintRegion::flow(const PrintObject& object, FlowRole role, double layer_height, bool first_layer) const
{
    const PrintConfig&         print_config = object.print()->config();
    ConfigOptionFloatOrPercent config_width;
    // Get extrusion width from configuration.
    // (might be an absolute value, or a percent value, or zero for auto)
    if (first_layer && print_config.initial_layer_line_width.value > 0) {
        config_width = print_config.initial_layer_line_width;
    } else if (role == frExternalPerimeter) {
        config_width = m_config.outer_wall_line_width;
    } else if (role == frPerimeter) {
        config_width = m_config.inner_wall_line_width;
    } else if (role == frInfill) {
        config_width = m_config.sparse_infill_line_width;
    } else if (role == frSolidInfill) {
        config_width = m_config.internal_solid_infill_line_width;
    } else if (role == frTopSolidInfill) {
        config_width = m_config.top_surface_line_width;
    } else {
        throw Slic3r::InvalidArgument("Unknown role");
    }

    if (config_width.value == 0)
        config_width = object.config().line_width;

    // Get the configured nozzle_diameter for the extruder associated to the flow role requested.
    // Here this->extruder(role) - 1 may underflow to MAX_INT, but then the get_at() will follback to zero'th element, so everything is all
    // right. [INTENT] Intentional unsigned underflow: extruder()==0 → slot -1 wraps to MAX_INT → get_at() clamps to [0].
    auto nozzle_diameter = float(print_config.nozzle_diameter.get_at(this->extruder(role) - 1));
    return Flow::new_from_config_width(role, config_width, nozzle_diameter, float(layer_height));
}

// [INTENT] Average nozzle diameter across the three filament slots used by this region.
//          Used by bridging height calculation.  When all three slots are the same nozzle,
//          the result equals that nozzle's diameter.
// [COUPLING] Reads nozzle_diameter vector; slot indices are 1-based so -1 is applied.
coordf_t PrintRegion::nozzle_dmr_avg(const PrintConfig& print_config) const
{
    return (print_config.nozzle_diameter.get_at(m_config.wall_filament.value - 1) +
            print_config.nozzle_diameter.get_at(m_config.sparse_infill_filament.value - 1) +
            print_config.nozzle_diameter.get_at(m_config.solid_infill_filament.value - 1)) /
           3.;
}

// [INTENT] Bridging height is the cross-sectional circle that just fits in the nozzle gap
//          when printing bridges.  h = nozzle_avg * sqrt(bridge_flow_ratio).
coordf_t PrintRegion::bridging_height_avg(const PrintConfig& print_config) const
{
    return this->nozzle_dmr_avg(print_config) * sqrt(m_config.bridge_flow.value);
}

// [INTENT] Static helper: given a PrintRegionConfig and PrintConfig, append all filament
//          slots that are actually used by this region to object_extruders (0-indexed).
//          Clamped to num_extruders-1 so out-of-range configs safely map to slot 0.
// [COUPLING] Called from both the instance method and from Print::collect_statistic
//            paths; must stay in sync with GUI extruder selector enable/disable logic.
void PrintRegion::collect_object_printing_extruders(const PrintConfig&         print_config,
                                                    const PrintRegionConfig&   region_config,
                                                    const bool                 has_brim,
                                                    std::vector<unsigned int>& object_extruders)
{
    // These checks reflect the same logic used in the GUI for enabling/disabling extruder selection fields.
    // BBS
    auto num_extruders = (int) print_config.filament_diameter.size();
    // [INTENT] emplace_extruder converts 1-based config slot to 0-based index, clamped.
    auto emplace_extruder = [num_extruders, &object_extruders](int extruder_id) {
        int i = std::max(0, extruder_id - 1);
        object_extruders.emplace_back((i >= num_extruders) ? 0 : i);
    };
    if (region_config.wall_loops.value > 0 || has_brim)
        emplace_extruder(region_config.wall_filament);
    if (region_config.sparse_infill_density.value > 0)
        emplace_extruder(region_config.sparse_infill_filament);
    if (region_config.top_shell_layers.value > 0 || region_config.bottom_shell_layers.value > 0)
        emplace_extruder(region_config.solid_infill_filament);
}

// [INTENT] Instance variant — validates in debug builds that all extruder slots are in
//          range (i.e. PrintApply produced a consistent region), then delegates.
void PrintRegion::collect_object_printing_extruders(const Print& print, std::vector<unsigned int>& object_extruders) const
{
    // PrintRegion, if used by some PrintObject, shall have all the extruders set to an existing printer extruder.
    // If not, then there must be something wrong with the Print::apply() function.
#ifndef NDEBUG
    // BBS
    auto num_extruders = int(print.config().filament_diameter.size());
    assert(this->config().wall_filament <= num_extruders);
    assert(this->config().sparse_infill_filament <= num_extruders);
    assert(this->config().solid_infill_filament <= num_extruders);
#endif
    collect_object_printing_extruders(print.config(), this->config(), print.has_brim(), object_extruders);
}

} // namespace Slic3r
