// PNP fork (UpstreamSyncMap ticket 03): the shipped vendor profiles must not
// reference config keys this build silently drops.
//
// Preset loading is deliberately forgiving: PrintConfigDef::handle_legacy()
// clears an unknown key and the vendor-load path passes `unknown_out == nullptr`,
// so a key that the fork never defined is not an error - the value just
// disappears. That is how 19 upstream keys used by 673 shipped presets went
// missing without a single log line (ticket 03).
//
// This suite reads the JSON under resources/profiles/ and fails on a top-level
// key that is neither:
//   * declared in print_config_def (the build's live key set),
//   * preset metadata the loader consumes before the config is applied,
//   * in the obsolete-key ignore set (legal to ship, cleared on purpose), or
//   * registered at runtime by the pnp backend (pnp_registered_config_keys()).
//
// The last exemption cannot fire in this process: the one-shot registration runs
// in the GUI startup path, not in a unit test. It is consulted anyway so a key
// that arrives through that seam is not reported as a fork omission.
//
// `undeclared_vendor_keys()` is the fourth, explicit list: keys that upstream
// 808dd7dff8 does not declare either. They are vendor leftovers (BambuStudio /
// Creality / Qidi import debris), dead in both builds before this sync; they are
// listed one by one rather than wildcarded so a *new* unknown key still fails
// this test instead of hiding in a prefix.

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>

#include <fstream>
#include <map>
#include <set>
#include <string>

#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

namespace {

namespace fs = boost::filesystem;

// Consumed by Preset::load() / PresetBundle before the remaining keys are handed
// to the config definition. Kept in sync with src/libslic3r/Preset.hpp's
// BBL_JSON_KEY_* / ORCA_JSON_KEY_* macros and Preset.cpp's loader.
const std::set<std::string>& preset_metadata_keys()
{
    static const std::set<std::string> keys = {
        "type", "name", "inherits", "from", "instantiation", "setting_id", "renamed_from", "version",
        "compatible_printers", "compatible_printers_condition", "compatible_prints", "compatible_prints_condition",
        "printer_model", "printer_variant", "default_print_profile", "default_filament_profile",
        "filament_id", "filament_vendor", "upward_compatible_machine", "is_external", "sync_info", "alias",
        "filament_settings_id", "print_settings_id", "printer_settings_id", "base_id", "user_id",
        "updated_time", "created_time", "description", "url", "force_update", "sub_path",
        "machine_model_list", "process_list", "filament_list", "machine_list", "machine_tech", "family",
        "bed_model", "bed_texture", "image_bed_type", "bottom_texture_end_name",
        "use_double_extruder_default_texture", "bottom_texture_rect", "is_custom_defined", "_comment",
    };
    return keys;
}

// Upstream keys deliberately not ported yet. gcode_remap_x/y/z are added by
// upstream's add_belt_remap() helper (so the literal `this->add` scan in ticket
// 03 missed them) and are referenced by the three belt machine presets. They
// belong to the belt family ticket 10 ports; until then the loader drops them
// exactly as it did before the sync. Listed separately from vendor leftovers so
// the "nobody declares this" comment on that list stays true.
const std::set<std::string>& deferred_upstream_keys()
{
    static const std::set<std::string> keys = {
        "gcode_remap_x", "gcode_remap_y", "gcode_remap_z",
    };
    return keys;
}

// Vendor keys that neither this fork nor upstream 808dd7dff8 declares in
// PrintConfigDef. The loader drops them today and dropped them before the sync;
// they are recorded here so this test can distinguish "a key the fork is
// supposed to know" from "a key nobody knows".
const std::set<std::string>& undeclared_vendor_keys()
{
    static const std::set<std::string> keys = {
    "accel_to_decel", "acceleration_limit_mess", "acceleration_limit_mess_enable",
    "activate_chamber_layer", "additional_cooling_fan_speed_unseal", "ai_infill",
    "ams_filament_load_time_ams", "ams_filament_load_time_ams_lite", "ams_filament_load_time_n3f_s",
    "ams_filament_unload_time_ams", "ams_filament_unload_time_ams_lite", "ams_filament_unload_time_n3f_s",
    "apply_top_surface_compensation", "auto_disable_filter_on_overheat", "auto_toolchange_command",
    "avoid_crossing_wall_includes_support", "bed_heat_soak_area", "bed_shape", "bed_texture_area",
    "bed_type", "box_id", "box_temperature", "box_temperature_range_high", "box_temperature_range_low",
    "bridge_infill_acceleration", "chamber_temperatures", "circle_compensation_manual_offset",
    "circle_compensation_speed", "color_bed_exclude_area", "cool_cds_fan_start_at_height",
    "cool_special_cds_fan_speed", "cooling_perimeter_transition_distance", "cooling_slowdown_logic",
    "counter_coef_1", "counter_coef_2", "counter_coef_3", "counter_limit_max", "counter_limit_min",
    "creality_flush_time", "customized_plate_temp", "customized_plate_temp_initial_layer",
    "default_ams_type", "default_materials", "default_wipe_tower_pos", "detect_floating_vertical_shell",
    "diameter_limit", "disable_fan_first_layers", "embedding_wall_into_infill",
    "enable_circle_compensation", "enable_filament_mapping", "enable_height_slowdown",
    "enable_special_area_additional_cooling_fan", "enable_support_ironing", "enable_volume_fan",
    "end_gcode", "epoxy_resin_plate_temp", "epoxy_resin_plate_temp_initial_layer",
    "external_perimeter_speed", "extruder_clearance_max_radius", "extruder_height_gap", "extruders_count",
    "fan_cooling_layer_time_BRASS", "fan_cooling_layer_time_HS", "fan_max_speed_BRASS", "fan_max_speed_HS",
    "fan_min_speed_BRASS", "fan_min_speed_HS", "fan_p2_after_x_layers", "fan_p2_before_x_layers",
    "fan_p2_speed_before_x_layers", "fan_speed_after_x_layers", "filament_bridge_speed",
    "filament_change_lift_type", "filament_color", "filament_contact_safe", "filament_emission_safe",
    "filament_enable_overhang_speed", "filament_flow_ratio_initial_layer", "filament_ingredients_safe",
    "filament_long_retractions_when_ec", "filament_metal_stickiness", "filament_overhang_1_4_speed",
    "filament_overhang_2_4_speed", "filament_overhang_3_4_speed", "filament_overhang_4_4_speed",
    "filament_overhang_totally_speed", "filament_retract_layer_change", "filament_retract_length",
    "filament_retract_speed", "filament_retraction_distances_when_ec", "filament_scarf_gap",
    "filament_scarf_height", "filament_scarf_length", "filament_scarf_seam_type", "filament_spool_weight",
    "filament_support_printable", "filament_swap_gcode", "filament_toolchange_time",
    "filament_velocity_adaptation_factor", "fins_extrude_safe_temp", "first_layer_jerk",
    "first_layer_temperature", "gap_fill_enabled",
    "hole_coef_1", "hole_coef_2", "hole_coef_3", "hole_limit_max", "hole_limit_min", "idle_temperture",
    "impact_strength_z", "include", "ineternal_bridge_speed", "infill_instead_top_bottom_surfaces",
    "infill_layer_acceleration", "infill_rotate_step", "inital_layer_height", "inital_travel_speed",
    "initial_layer_infill_line_width", "initial_travel_speed", "ironing_support_layer", "is_artillery",
    "is_support_3mf", "is_support_air_condition", "is_support_bed_leveling_force", "is_support_mqtt",
    "is_support_multi_box", "is_support_polar_cooler", "is_support_timelapse", "keep_fan_always_on",
    "lattice_angle_1", "lattice_angle_2", "layer_heigth", "layer_time_smoothing",
    "layer_time_smoothing_threshold", "locked_skeleton_infill_pattern", "locked_skin_infill_pattern",
    "machine_LED_light_exist", "machine_max_feedrate_e", "machine_max_feedrate_x",
    "machine_max_feedrate_y", "machine_max_feedrate_z", "machine_platform_motion_enable",
    "machine_ptc_exist", "machine_switch_extruder_time", "material_flow_dependent_temperature",
    "material_flow_temp_graph", "minimum_support_area", "model", "model_id", "monotonic_travel_into_wall",
    "multicolor_method", "no_slow_down_for_cooling_on_outwalls", "nozzle_count",
    "nozzle_temperature_BRASS", "nozzle_temperature_HS", "nozzle_temperature_initial_layer_BRASS",
    "nozzle_temperature_initial_layer_HS", "overhang_threshold_participating_cooling",
    "override_filament_scarf_seam_setting", "override_process_overhang_speed", "pause_gcode",
    "pre_start_fan_time", "prime_tower_enhance_type", "prime_tower_extra_rib_length",
    "prime_tower_fillet_wall", "prime_tower_lift_height", "prime_tower_lift_speed",
    "prime_tower_max_speed", "prime_tower_position_type", "prime_tower_rib_wall", "prime_tower_rib_width",
    "print_in_clockwise", "printer_flush_multiplier", "remaining_times", "retract_on_top_layer",
    "rotate_solid_infill_direction", "seam_placement_away_from_overhangs", "seam_slope_gap", "shape",
    "shrink_ratio", "skirt_per_object", "slow_down_curled_perimeters", "slow_down_layer_time_BRASS",
    "slow_down_layer_time_HS", "slow_layers_count", "slowdown_end_acc", "slowdown_end_height",
    "slowdown_end_speed", "slowdown_start_acc", "slowdown_start_height", "slowdown_start_speed",
    "smooth_speed_discontinuity_area", "solid_infill_filament", "sparse_infill_anchor",
    "sparse_infill_filament", "sparse_infill_lattice_angle_1", "sparse_infill_lattice_angle_2",
    "speed_limit_to_height", "speed_limit_to_height_enable", "start_gcode", "support_box_temp_control",
    "support_interface_bottom_spacing", "support_ironing_direction", "support_ironing_inset",
    "support_ironing_speed", "support_material_synchronize_layers", "support_multi_filament",
    "support_wall_loops", "support_wan_network", "support_xy_overrides_z", "temp_max", "temp_min",
    "temperture_vitrification", "thic_birdges", "thumbnail", "thumbnail_size", "thumbnails_internal",
    "thumbnails_internal_switch", "toolchange_gcode", "top_surface_flow_ratio",
    "travel_short_distance_acceleration", "tree_hybrid_cross_height", "tree_support_adaptive_layer_height",
    "tree_support_branch_diameter_double_wall", "unprintable_filament_types", "vertical_shell_speed",
    "wall_count", "wall_filament", "wall_infill_order", "wipe_tower_extruder",
    "z_direction_outwall_speed_continuous"
    };
    return keys;
}

bool is_shipped_preset(const nlohmann::json &doc)
{
    if (!doc.is_object())
        return false;
    const auto type = doc.find("type");
    if (type == doc.end() || !type->is_string())
        return false;
    const std::string &t = type->get_ref<const std::string &>();
    if (t != "machine" && t != "process" && t != "filament")
        return false;
    // Support files under a vendor dir (`filament_id_map.json`, the color-code
    // table, ...) carry a "type" marker for the loader but are not presets.
    const auto name = doc.find("name");
    return name != doc.end() && name->is_string();
}

} // namespace

TEST_CASE("Every shipped preset key is declared or explicitly allowlisted", "[Profile][Config]")
{
    const fs::path profiles_dir(PROFILES_DIR);
    REQUIRE(fs::is_directory(profiles_dir));

    const std::set<std::string> &metadata = preset_metadata_keys();
    const std::set<std::string> &vendor   = undeclared_vendor_keys();
    const std::set<std::string> &deferred = deferred_upstream_keys();
    const bool                   pnp_sealed = pnp_config_keys_sealed();
    const std::vector<std::string> &pnp_keys = pnp_registered_config_keys();
    const std::set<std::string>   pnp_set(pnp_keys.begin(), pnp_keys.end());

    size_t presets = 0;
    size_t keys    = 0;
    std::map<std::string, std::string> unknown; // key -> first file that uses it

    for (const fs::directory_entry &vendor_dir : fs::directory_iterator(profiles_dir)) {
        if (!fs::is_directory(vendor_dir.path()))
            continue; // vendor index JSON (Vendor.json, blacklist.json, ...)
        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(vendor_dir.path())) {
            if (!fs::is_regular_file(entry.path()) || entry.path().extension() != ".json")
                continue;

            std::ifstream stream(entry.path().string(), std::ios::binary);
            REQUIRE(stream.good());
            const nlohmann::json doc = nlohmann::json::parse(stream);
            if (!is_shipped_preset(doc))
                continue;

            ++presets;
            for (const auto &item : doc.items()) {
                ++keys;
                const std::string &key = item.key();
                if (print_config_def.has(key))
                    continue;
                if (metadata.find(key) != metadata.end())
                    continue;
                if (PrintConfigDef::is_ignored_legacy_key(key))
                    continue;
                if (pnp_set.find(key) != pnp_set.end())
                    continue;
                if (deferred.find(key) != deferred.end())
                    continue;
                if (vendor.find(key) != vendor.end())
                    continue;
                unknown.emplace(key, fs::relative(entry.path(), profiles_dir).generic_string());
            }
        }
    }

    // Guards against walking the wrong tree (or an empty PROFILES_DIR) and
    // passing vacuously.
    CHECK(presets > 14000);

    std::string report;
    for (const auto &[key, file] : unknown)
        report += "\n  " + key + "  (first seen in " + file + ")";
    INFO("pnp key registration sealed: " << pnp_sealed);
    INFO("undeclared keys:" << report);
    CHECK(unknown.empty());
    CHECK(keys > 0);
}

