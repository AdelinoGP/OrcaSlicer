// PNP fork (wayfinder ticket F01): implementation of the preset -> PNP flat
// JSON config translator. See PnpConfigTranslator.hpp and
// .wayfinder/assets/005-preset-to-pnp-config-mapping.md for the authoritative
// mapping table this code transcribes.

#include "PnpConfigTranslator.hpp"

#include <cstdlib>
#include <set>
#include <string>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI { namespace PnpConfigTranslator {

namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------
// Tier A — identity keys: same key string, compatible value (mapping asset,
// "Tier A" section, plus the identity host-speed keys listed there).
// `sparse_infill_density` and `wall_sequence` from the asset's Tier-A list are
// handled as Tier-B transform rows below (fan-out / enum respelling), so they
// are intentionally absent here.
// ---------------------------------------------------------------------------
const char* const TIER_A_KEYS[] = {
    "alternate_extra_wall",
    "bridge_flow",
    "brim_width",
    "detect_overhang_wall",
    "detect_thin_wall",
    "extra_perimeters_on_overhangs",
    "filter_out_gap_fill",
    "gap_infill_speed",
    "initial_layer_min_bead_width",
    "inner_wall_line_width",
    "inner_wall_speed",
    "ironing_flow",
    "ironing_pattern",
    "ironing_spacing",
    "ironing_speed",
    "layer_height",
    "line_width",
    "machine_end_gcode",
    "machine_start_gcode",
    "min_bead_width",
    "min_feature_size",
    "min_length_factor",
    "min_width_top_surface",
    "nozzle_diameter",
    "nozzle_temperature_initial_layer",
    "only_one_wall_first_layer",
    "only_one_wall_top",
    "outer_wall_line_width",
    "outer_wall_speed",
    "overhang_1_4_speed",
    "overhang_2_4_speed",
    "overhang_3_4_speed",
    "overhang_4_4_speed",
    "overhang_fan_speed",
    "overhang_reverse",
    "overhang_reverse_internal_only",
    "overhang_reverse_threshold",
    "precise_outer_wall",
    "skirt_distance",
    "skirt_height",
    "skirt_loops",
    "slow_down_for_layer_cooling",
    "slow_down_layer_time",
    "slow_down_min_speed",
    "support_angle",
    "support_interface_bottom_layers",
    "support_interface_top_layers",
    "support_speed",
    "thick_bridges",
    "tree_support_branch_angle",
    "tree_support_branch_diameter",
    "tree_support_branch_diameter_angle",
    "tree_support_branch_distance",
    "tree_support_wall_count",
    "wall_direction",
    "wall_distribution_count",
    "wall_maximum_deviation",
    "wall_maximum_resolution",
    "wall_transition_angle",
    "wall_transition_filter_deviation",
    "wall_transition_length",
    "wipe_tower_x",
    "wipe_tower_y",
    // Identity host-speed keys ([speeds] in pnp docs/config/host-keys.toml
    // that Orca also defines; sparse_infill_speed is a Tier-B fan-out row).
    "top_surface_speed",
    "bridge_speed",
    "internal_bridge_speed",
    "support_interface_speed",
    "travel_speed",
    "travel_speed_z",
    "initial_layer_speed",
    "initial_layer_infill_speed",
    "initial_layer_travel_speed",
    "skirt_speed",
    "wipe_speed",
    // Identity host_runtime key (mapping asset Tier-B table, identity row).
    "use_relative_e_distances",
};

// Known Tier-D warning-class overrides. Per ticket 013 there is no up-front
// classification pass: keys default to NotYetMapped and a class is written
// here only when there is a concrete reason (dogfooding hit or known gap).
PnpWarningClass tier_d_class(const std::string& key)
{
    // Sole signal that a Klipper/RRF user's G-code is Marlin (ticket 013).
    if (key == "gcode_flavor")
        return PnpWarningClass::UnsupportedFeature;
    return PnpWarningClass::NotYetMapped;
}

// Convert element 0 of a vector option's vserialize() to a typed JSON value.
// Per the mapping spec, per-extruder vectors collapse to element 0 for v1.
json vector_elem0_to_json(ConfigOptionType type, const ConfigOptionVectorBase& opt)
{
    const std::vector<std::string> vals = opt.vserialize();
    if (vals.empty())
        return json(); // null -> caller skips the key
    const std::string& s = vals.front();
    if (s == "nil" || s.empty())
        return json();
    switch (int(type) & ~int(coVectorType)) {
    case coFloat:
        return std::atof(s.c_str());
    case coPercent: {
        // Percents vserialize with a trailing '%'; PNP wants the number.
        std::string t = s;
        if (!t.empty() && t.back() == '%')
            t.pop_back();
        return std::atof(t.c_str());
    }
    case coFloatOrPercent:
        // PNP float_or_percent accepts "300%" strings verbatim.
        return s.find('%') != std::string::npos ? json(s) : json(std::atof(s.c_str()));
    case coInt:
        return std::atoi(s.c_str());
    case coBool:
        return s == "1" || s == "true";
    case coString:
    default:
        return s;
    }
}

// Convert one Orca ConfigOption to a JSON value with PNP-compatible typing:
// booleans -> true/false, numbers -> numbers, percents of coFloatOrPercent
// pass through as "N%" strings, enums/strings -> serialized strings.
// Returns a null json when the value cannot/should not be emitted.
json option_to_json(const ConfigOption* opt)
{
    if (opt == nullptr || opt->is_nil())
        return json();
    switch (opt->type()) {
    case coBool:
        return opt->getBool();
    case coInt:
        return opt->getInt();
    case coFloat:
    case coPercent:
        return opt->getFloat();
    case coFloatOrPercent: {
        auto* fop = static_cast<const ConfigOptionFloatOrPercent*>(opt);
        return fop->percent ? json(fop->serialize()) : json(fop->value);
    }
    case coString: {
        // serialize() C-style-escapes strings (e.g. custom G-code); emit raw.
        auto* str = dynamic_cast<const ConfigOptionString*>(opt);
        return str != nullptr ? json(str->value) : json(opt->serialize());
    }
    case coEnum:
    case coPoint:
    case coPoint3:
        return opt->serialize();
    default:
        if (opt->is_vector())
            return vector_elem0_to_json(opt->type(), dynamic_cast<const ConfigOptionVectorBase&>(*opt));
        return opt->serialize();
    }
}

std::string serialize_or_empty(const DynamicPrintConfig& cfg, const std::string& key)
{
    const ConfigOption* opt = cfg.option(key);
    return opt != nullptr ? opt->serialize() : std::string();
}

} // anonymous namespace

PnpTranslationResult translate(const DynamicPrintConfig& cfg)
{
    PnpTranslationResult result;
    result.json = json::object();
    json& out   = result.json;

    // Orca keys consumed by a Tier-A or Tier-B row (they get no Tier-D warning).
    std::set<std::string> handled;

    const auto put = [&](const char* pnp_key, json value) {
        if (!value.is_null())
            out[pnp_key] = std::move(value);
    };
    const auto warn = [&](const std::string& key, PnpWarningClass cls,
                          std::string orca_value, std::string sent_value = std::string()) {
        result.warnings.push_back({key, cls, std::move(orca_value), std::move(sent_value)});
    };
    // Copy an orca option to a (possibly renamed) pnp key and claim the source.
    const auto copy_as = [&](const char* orca_key, const char* pnp_key) {
        handled.insert(orca_key);
        put(pnp_key, option_to_json(cfg.option(orca_key)));
    };

    // -----------------------------------------------------------------------
    // Tier A — identity copies.
    // -----------------------------------------------------------------------
    for (const char* key : TIER_A_KEYS)
        copy_as(key, key);

    // -----------------------------------------------------------------------
    // Tier B — renames / transforms (mapping asset, Tier-B table).
    // -----------------------------------------------------------------------

    // printable_area -> bed_shape: points list -> array of [x, y] pairs.
    handled.insert("printable_area");
    if (auto* pts = cfg.option<ConfigOptionPoints>("printable_area"); pts != nullptr) {
        json shape = json::array();
        for (const Vec2d& p : pts->values)
            shape.push_back(json::array({p.x(), p.y()}));
        out["bed_shape"] = std::move(shape);
    }

    // <active bed type>_plate_temp_initial_layer -> bed_temperature_initial_layer_single.
    handled.insert({"curr_bed_type",
                    "supertack_plate_temp_initial_layer", "cool_plate_temp_initial_layer",
                    "textured_cool_plate_temp_initial_layer", "eng_plate_temp_initial_layer",
                    "hot_plate_temp_initial_layer", "textured_plate_temp_initial_layer"});
    if (cfg.option("curr_bed_type") != nullptr) {
        const auto        bed_type = cfg.opt_enum<BedType>("curr_bed_type");
        const std::string temp_key = get_bed_temp_1st_layer_key(bed_type);
        if (!temp_key.empty())
            put("bed_temperature_initial_layer_single", option_to_json(cfg.option(temp_key)));
    }

    copy_as("close_fan_the_first_x_layers", "disable_fan_first_layers");
    copy_as("enable_overhang_bridge_fan", "enable_overhang_fan");
    copy_as("fan_max_speed", "fan_speed_max");
    copy_as("fan_min_speed", "fan_speed_min");
    copy_as("initial_layer_print_height", "first_layer_height");
    copy_as("infill_direction", "infill_angle");

    // sparse_infill_density feeds both PNP keys (infill modules + arachne).
    handled.insert("sparse_infill_density");
    if (json v = option_to_json(cfg.option("sparse_infill_density")); !v.is_null()) {
        out["infill_density"]        = v;
        out["sparse_infill_density"] = v;
    }
    // sparse_infill_speed: one source, two sinks (module key + host speed).
    handled.insert("sparse_infill_speed");
    if (json v = option_to_json(cfg.option("sparse_infill_speed")); !v.is_null()) {
        out["infill_speed"]        = v;
        out["sparse_infill_speed"] = v;
    }

    // ironing_type -> ironing_enabled (bool).
    handled.insert("ironing_type");
    if (cfg.option("ironing_type") != nullptr)
        out["ironing_enabled"] = serialize_or_empty(cfg, "ironing_type") != "no ironing";
    // ironing_flow / ironing_spacing additionally feed PNP variant keys
    // (their identity copies are Tier A above).
    put("ironing_flow_rate", option_to_json(cfg.option("ironing_flow")));
    put("ironing_spacing_mm", option_to_json(cfg.option("ironing_spacing")));

    // seam_position -> seam_mode. nearest/back/random map; Orca "aligned"
    // (and "aligned_back") have no PNP equivalent -> "nearest" + lossy warning.
    handled.insert("seam_position");
    if (cfg.option("seam_position") != nullptr) {
        const std::string seam = serialize_or_empty(cfg, "seam_position");
        if (seam == "nearest" || seam == "random") {
            out["seam_mode"] = seam;
        } else if (seam == "back") {
            out["seam_mode"] = "rear";
        } else { // "aligned", "aligned_back", or anything unknown
            out["seam_mode"] = "nearest";
            warn("seam_position", PnpWarningClass::LossyFallback, seam, "nearest");
        }
    }

    // skirt_loops > 0 OR brim_type != no_brim -> skirt_brim_enabled.
    handled.insert("brim_type"); // skirt_loops is Tier A above
    {
        const ConfigOption* loops = cfg.option("skirt_loops");
        const bool skirt_on = loops != nullptr && loops->getInt() > 0;
        const bool brim_on  = cfg.option("brim_type") != nullptr &&
                              serialize_or_empty(cfg, "brim_type") != "no_brim";
        out["skirt_brim_enabled"] = skirt_on || brim_on;
    }

    copy_as("spiral_mode", "spiral_vase");
    copy_as("enable_support", "support_enabled");

    // raft_layers -> support_raft_layers. PNP has no standalone raft: >0 with
    // supports off is an unsupported-feature warning (mapping asset + 013).
    copy_as("raft_layers", "support_raft_layers");
    {
        const ConfigOption* raft = cfg.option("raft_layers");
        const ConfigOption* sup  = cfg.option("enable_support");
        if (raft != nullptr && raft->getInt() > 0 && (sup == nullptr || !sup->getBool()))
            warn("raft_layers", PnpWarningClass::UnsupportedFeature,
                 raft->serialize(), raft->serialize());
    }

    copy_as("support_top_z_distance", "support_top_z_distance_mm");

    // wall_generator: identity name, but coEnum -> PNP string ("classic"/"arachne").
    handled.insert("wall_generator");
    if (cfg.option("wall_generator") != nullptr)
        out["wall_generator"] = serialize_or_empty(cfg, "wall_generator");

    // wall_sequence: Orca serializes "inner wall/outer wall"-style; PNP expects
    // "InnerOuter"-style (mapping asset, Tier-A caveats).
    handled.insert("wall_sequence");
    if (cfg.option("wall_sequence") != nullptr) {
        const std::string seq = serialize_or_empty(cfg, "wall_sequence");
        if (seq == "inner wall/outer wall")
            out["wall_sequence"] = "InnerOuter";
        else if (seq == "outer wall/inner wall")
            out["wall_sequence"] = "OuterInner";
        else if (seq == "inner-outer-inner wall")
            out["wall_sequence"] = "InnerOuterInner";
        else {
            out["wall_sequence"] = "InnerOuter";
            warn("wall_sequence", PnpWarningClass::LossyFallback, seq, "InnerOuter");
        }
    }

    // support_base_pattern_spacing -> support_density: spacing<->density
    // inversion formula unverified (mapping asset open point; ticket 013 lists
    // this as a lossy-fallback member). Not sent; PNP default rules.
    handled.insert("support_base_pattern_spacing");
    if (cfg.option("support_base_pattern_spacing") != nullptr)
        warn("support_base_pattern_spacing", PnpWarningClass::LossyFallback,
             serialize_or_empty(cfg, "support_base_pattern_spacing"), std::string());

    copy_as("support_interface_spacing", "tree_support_interface_spacing_mm");

    // fuzzy_skin group: enum gates whether the module keys are emitted at all.
    handled.insert({"fuzzy_skin", "fuzzy_skin_thickness", "fuzzy_skin_point_distance"});
    {
        const std::string fuzzy = serialize_or_empty(cfg, "fuzzy_skin");
        bool emit_fuzzy = true;
        if (fuzzy == "all") {
            out["apply_to_all"] = true;
        } else if (fuzzy == "external") {
            out["apply_to_all"] = false;
        } else if (fuzzy == "allwalls") {
            out["apply_to_all"] = true;
            warn("fuzzy_skin", PnpWarningClass::LossyFallback, fuzzy, "all");
        } else if (fuzzy == "hole") {
            out["apply_to_all"] = false;
            warn("fuzzy_skin", PnpWarningClass::LossyFallback, fuzzy, "external");
        } else { // "none", "disabled_fuzzy", missing -> omit fuzzy keys entirely
            emit_fuzzy = false;
        }
        if (emit_fuzzy) {
            put("thickness", option_to_json(cfg.option("fuzzy_skin_thickness")));
            put("point_distance", option_to_json(cfg.option("fuzzy_skin_point_distance")));
        }
    }

    copy_as("z_hop", "travel_z_hop");
    copy_as("retraction_length", "retract_length");
    copy_as("retraction_speed", "retract_speed");
    copy_as("wall_loops", "wall_count");
    copy_as("enable_prime_tower", "wipe_tower_enabled");
    copy_as("prime_tower_width", "wipe_tower_width");
    copy_as("prime_volume", "wipe_tower_purge_volume");

    // -----------------------------------------------------------------------
    // Tier C — PNP-internal keys have no Orca source: translation never writes
    // them, PNP defaults rule. Nothing to iterate here; Orca-side toggles that
    // *hint* at them (e.g. independent_support_layer_height) fall through to
    // Tier D and get their not-yet-mapped warning below.
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // Tier D — every remaining Orca key: not sent, one warning each. Emitted
    // unconditionally; diffing against defaults is the caller's job (F03).
    // -----------------------------------------------------------------------
    for (const std::string& key : cfg.keys())
        if (handled.count(key) == 0)
            warn(key, tier_d_class(key), cfg.opt_serialize(key));

    return result;
}

}}} // namespace Slic3r::GUI::PnpConfigTranslator
