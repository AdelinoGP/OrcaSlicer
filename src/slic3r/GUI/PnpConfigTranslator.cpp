// PNP fork (wayfinder ticket F01): implementation of the preset -> PNP flat
// JSON config translator. See PnpConfigTranslator.hpp and
// .wayfinder/assets/005-preset-to-pnp-config-mapping.md for the authoritative
// mapping table this code transcribes.

#include "PnpConfigTranslator.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
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
    "initial_layer_line_width",
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

// Orca keys consumed by a Tier-A or Tier-B row (they get no Tier-D warning).
// Single source of truth for translate() and pnp_key_is_unimplemented().
const std::set<std::string>& pnp_handled_keys()
{
    static const std::set<std::string> handled = [] {
        std::set<std::string> s;
        for (const char* key : TIER_A_KEYS)
            s.insert(key);
        // Tier-B rows (renames / transforms) — keep in sync with translate().
        s.insert("printable_area");
        s.insert({"curr_bed_type",
                  "supertack_plate_temp_initial_layer", "cool_plate_temp_initial_layer",
                  "textured_cool_plate_temp_initial_layer", "eng_plate_temp_initial_layer",
                  "hot_plate_temp_initial_layer", "textured_plate_temp_initial_layer"});
        s.insert({"close_fan_the_first_x_layers", "enable_overhang_bridge_fan",
                  "fan_max_speed", "fan_min_speed", "initial_layer_print_height",
                  "infill_direction"});
        s.insert({"sparse_infill_density", "sparse_infill_speed", "ironing_type"});
        s.insert({"sparse_infill_line_width", "internal_solid_infill_line_width",
                  "top_surface_line_width", "bridge_line_width"});
        s.insert("seam_position");
        s.insert("brim_type"); // skirt_loops is Tier A above
        s.insert({"spiral_mode", "enable_support", "raft_layers",
                  "support_top_z_distance"});
        s.insert({"wall_generator", "wall_sequence", "support_base_pattern_spacing",
                  "support_interface_spacing"});
        s.insert({"fuzzy_skin", "fuzzy_skin_thickness", "fuzzy_skin_point_distance"});
        s.insert({"z_hop", "retraction_length", "retraction_speed", "wall_loops",
                  "enable_prime_tower", "prime_tower_width", "prime_volume"});
        return s;
    }();
    return handled;
}

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
    const std::set<std::string>& handled = pnp_handled_keys();

    const auto put = [&](const char* pnp_key, json value) {
        if (!value.is_null())
            out[pnp_key] = std::move(value);
    };
    const auto warn = [&](const std::string& key, PnpWarningClass cls,
                          std::string orca_value, std::string sent_value = std::string()) {
        result.warnings.push_back({key, cls, std::move(orca_value), std::move(sent_value)});
    };
    // Copy an orca option to a (possibly renamed) pnp key. The source key is
    // already in pnp_handled_keys().
    const auto copy_as = [&](const char* orca_key, const char* pnp_key) {
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

    // printable_area -> bed_shape: points list -> flat [x0, y0, x1, y1, ...]
    // in mm (pnp types it float-list; nested pairs fail config resolution).
    if (auto* pts = cfg.option<ConfigOptionPoints>("printable_area"); pts != nullptr) {
        json shape = json::array();
        for (const Vec2d& p : pts->values) {
            shape.push_back(p.x());
            shape.push_back(p.y());
        }
        out["bed_shape"] = std::move(shape);
    }

    // <active bed type>_plate_temp_initial_layer -> bed_temperature_initial_layer_single.
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
    // Unit split: pnp's infill_density is a fraction (0.0-1.0, default 0.2),
    // while Orca's sparse_infill_density is a percent (25 = 25%) and the
    // arachne/classic perimeter modules consume the sparse_infill_density
    // sink as a percent > 0 gate. Sending the raw percent to infill_density
    // made pnp slice at 2500% density (clamped to 100% infill).
    if (json v = option_to_json(cfg.option("sparse_infill_density")); v.is_number()) {
        out["infill_density"]        = v.get<double>() / 100.;
        out["sparse_infill_density"] = v;
    }
    // sparse_infill_speed: one source, two sinks (module key + host speed).
    if (json v = option_to_json(cfg.option("sparse_infill_speed")); !v.is_null()) {
        out["infill_speed"]        = v;
        out["sparse_infill_speed"] = v;
    }

    // ironing_type -> ironing_enabled (bool).
    if (cfg.option("ironing_type") != nullptr)
        out["ironing_enabled"] = serialize_or_empty(cfg, "ironing_type") != "no ironing";
    // ironing_flow / ironing_spacing additionally feed PNP variant keys
    // (their identity copies are Tier A above).
    put("ironing_flow_rate", option_to_json(cfg.option("ironing_flow")));
    put("ironing_spacing_mm", option_to_json(cfg.option("ironing_spacing")));
    // Unit fix over the Tier-A copy: Orca ironing_flow is a percent (e.g. 10),
    // pnp ironing_flow is a fraction in [0.01, 1] (ironing_flow_rate above is
    // the percent-style variant and passes through raw).
    if (const ConfigOption* opt = cfg.option("ironing_flow"); opt != nullptr)
        out["ironing_flow"] = std::clamp(opt->getFloat() / 100., 0.01, 1.0);

    // Unit fixes over Tier-A copies: pnp types these keys as plain floats (mm),
    // so Orca float-or-percent widths must be resolved against the nozzle
    // diameter here — "105%" strings fail pnp config resolution outright.
    const double nozzle_d = cfg.option("nozzle_diameter") != nullptr ? cfg.opt_float("nozzle_diameter", 0) : 0.;
    // Of Orca's 37 float-or-percent keys, exactly two are also declared float
    // in pnp's schema (`line_width`, `initial_layer_line_width`); the rest land
    // untyped in pnp's extensions map, where a "105%" string is harmless. Both
    // must be resolved here. The wall widths below are not pnp-declared, but
    // resolving them costs nothing and keeps the set of widths consistent.
    // The role-specific infill/top/bridge widths are declared float (mm) in the
    // infill modules' schemas and consumed via get_float, so they must also be
    // resolved to absolute mm here (they were Tier D before, silently falling
    // back to pnp's line_width / 1.125*nozzle defaults). Unlike the four Tier-A
    // widths above, these keys have no identity copy, so absolute values must
    // be emitted here too, not only percent fix-ups.
    for (const char* key :
         {"line_width", "initial_layer_line_width", "inner_wall_line_width", "outer_wall_line_width",
          "sparse_infill_line_width", "internal_solid_infill_line_width", "top_surface_line_width",
          "bridge_line_width"}) {
        auto* fop = dynamic_cast<const ConfigOptionFloatOrPercent*>(cfg.option(key));
        if (fop == nullptr)
            continue;
        if (fop->percent && nozzle_d > 0.)
            out[key] = fop->get_abs_value(nozzle_d);
        else if (!fop->percent)
            out[key] = fop->value;
    }
    // Orca coPercent keys that pnp's arachne module resolves via
    // get_abs_value against the nozzle: a plain number would be treated as an
    // absolute mm value (15 mm feature size, 100 mm transition length), so
    // resolve the percent to absolute mm here.
    for (const char* key : {"min_feature_size", "wall_transition_length"}) {
        const ConfigOption* opt = cfg.option(key);
        if (opt != nullptr && nozzle_d > 0.)
            out[key] = opt->getFloat() / 100. * nozzle_d;
    }
    // wall_transition_filter_deviation is consumed by arachne via units_to_mm
    // (1 unit = 100 nm), so the percent-of-nozzle value must arrive in units.
    if (const ConfigOption* opt = cfg.option("wall_transition_filter_deviation");
        opt != nullptr && nozzle_d > 0.)
        out["wall_transition_filter_deviation"] = std::round(opt->getFloat() / 100. * nozzle_d * 10000.);
    // overhang_1_4_speed percent resolves over outer_wall_speed (its Orca
    // ratio_over); pnp types it float.
    if (auto* v = cfg.option<ConfigOptionFloatsOrPercents>("overhang_1_4_speed");
        v != nullptr && !v->values.empty() && v->values.front().percent) {
        // The base key can be absent (partial configs); never deref a missing
        // option — drop the key instead so pnp's default applies.
        // outer_wall_speed is coFloats (vector) — getFloat() on it throws.
        if (cfg.option("outer_wall_speed") != nullptr)
            out["overhang_1_4_speed"] = v->values.front().value / 100. * cfg.opt_float("outer_wall_speed", 0);
        else
            out.erase("overhang_1_4_speed");
    }
    // Orca bead widths are percent-of-nozzle-diameter; pnp expects absolute
    // values in its internal units (1 unit = 100 nm, i.e. mm * 10000).
    for (const char* key : {"min_bead_width", "initial_layer_min_bead_width"}) {
        const ConfigOption* opt = cfg.option(key);
        if (opt != nullptr && nozzle_d > 0.)
            out[key] = std::round(opt->getFloat() / 100. * nozzle_d * 10000.);
    }

    // Orca tree_support_wall_count 0 means "auto"; pnp requires [1, 10] with no
    // auto mode. Drop the key so pnp's default applies, and log the fallback.
    if (const ConfigOption* opt = cfg.option("tree_support_wall_count");
        opt != nullptr && opt->getInt() < 1) {
        out.erase("tree_support_wall_count");
        warn("tree_support_wall_count", PnpWarningClass::LossyFallback,
             opt->serialize(), "(omitted; pnp default)");
    }

    // seam_position -> seam_mode. nearest/back/random map; Orca "aligned"
    // (and "aligned_back") have no PNP equivalent -> "nearest" + lossy warning.
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
    // (skirt_loops is Tier A above.)
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
    if (cfg.option("wall_generator") != nullptr)
        out["wall_generator"] = serialize_or_empty(cfg, "wall_generator");

    // wall_sequence: Orca serializes "inner wall/outer wall"-style; PNP expects
    // "InnerOuter"-style (mapping asset, Tier-A caveats).
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
    if (cfg.option("support_base_pattern_spacing") != nullptr)
        warn("support_base_pattern_spacing", PnpWarningClass::LossyFallback,
             serialize_or_empty(cfg, "support_base_pattern_spacing"), std::string());

    copy_as("support_interface_spacing", "tree_support_interface_spacing_mm");

    // fuzzy_skin group: enum gates whether the module keys are emitted at all.
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

bool pnp_key_is_unimplemented(const std::string& orca_key)
{
    return pnp_handled_keys().count(orca_key) == 0;
}

namespace {

// True when `v` is acceptable for a schema field of type tag `type`.
// Unknown/empty type tags validate everything (fail open — the guard's job is
// to stop known-bad values, not to second-guess new schema features).
bool value_matches_type(const json& v, const std::string& type)
{
    if (type == "float" || type == "int" || type == "units")
        return v.is_number();
    if (type == "bool")
        return v.is_boolean();
    if (type == "string" || type == "enum")
        return v.is_string();
    if (type == "percent" || type == "float_or_percent")
        return v.is_number() || (v.is_string() && !v.get<std::string>().empty() && v.get<std::string>().back() == '%');
    if (type == "float-list" || type == "int-list") {
        if (!v.is_array())
            return false;
        for (const json& e : v)
            if (!e.is_number())
                return false;
        return true;
    }
    if (type == "string-list") {
        if (!v.is_array())
            return false;
        for (const json& e : v)
            if (!e.is_string())
                return false;
        return true;
    }
    return true;
}

} // anonymous namespace

void apply_schema_guard(json& config, const json& schema_doc, std::vector<PnpConfigWarning>& warnings)
{
    // Index the schema: key -> field descriptor (first definition wins, like
    // pnp's own module dedup).
    std::map<std::string, const json*> fields;
    if (const auto it = schema_doc.find("schema"); it != schema_doc.end() && it->is_array())
        for (const json& mod : *it)
            if (const auto fit = mod.find("fields"); fit != mod.end() && fit->is_array())
                for (const json& field : *fit)
                    if (field.is_object() && field.contains("key") && field["key"].is_string())
                        fields.emplace(field["key"].get<std::string>(), &field);

    for (auto it = config.begin(); it != config.end(); /* increment in body */) {
        const auto found = fields.find(it.key());
        if (found == fields.end()) {
            ++it; // unknown to the schema: pnp ignores it, leave it alone
            continue;
        }
        const json&       field  = *found->second;
        const json&       value  = it.value();
        const std::string type   = field.contains("type") && field["type"].is_string() ? field["type"].get<std::string>() : std::string();
        std::string       reason;

        if (!value_matches_type(value, type)) {
            reason = "schema type '" + type + "' rejects this value";
        } else if (value.is_number()) {
            const double v = value.get<double>();
            if (field.contains("min") && field["min"].is_number() && v < field["min"].get<double>())
                reason = "below schema min " + field["min"].dump();
            else if (field.contains("max") && field["max"].is_number() && v > field["max"].get<double>())
                reason = "above schema max " + field["max"].dump();
        }
        if (reason.empty() && type == "enum" && value.is_string()
            && field.contains("values") && field["values"].is_array() && !field["values"].empty()) {
            bool all_strings = true, member = false;
            for (const json& allowed : field["values"]) {
                if (!allowed.is_string()) { all_strings = false; break; }
                if (allowed.get<std::string>() == value.get<std::string>()) member = true;
            }
            if (all_strings && !member)
                reason = "not one of the schema enum values";
        }

        if (reason.empty()) {
            ++it;
        } else {
            warnings.push_back({it.key(), PnpWarningClass::LossyFallback, value.dump(),
                                "(dropped by schema guard: " + reason + "; pnp default applies)"});
            it = config.erase(it);
        }
    }
}

}}} // namespace Slic3r::GUI::PnpConfigTranslator
