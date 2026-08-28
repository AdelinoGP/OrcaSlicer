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
//
// SchemaBridgeMap ticket 05 demoted this list: it is no longer the authority on
// which keys route by name identity. When a universe is installed the identity
// pass asks the live backend instead, so a key pnp starts declaring is copied
// with no fork edit. This array survives only as the unprobed fallback, so that
// a translate() with no schema behaves exactly as it did before ticket 05.
// It is deliberately NOT kept in sync with the backend any more; two entries
// (`support_type`, `infill_shift_step`) are keys pnp reads through channels the
// wire cannot describe — see UNDECLARED_LIVE_KEYS.
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
    "infill_shift_step",
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
    "support_type",
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

// Keys the backend genuinely reads but declares through no channel the
// `module config-schema` wire can express (ticket 01, finding 7):
// `support_type` and `support_family` are read out of
// `resolved_config.extensions` by the support-generator claim selector, and
// `infill_shift_step` via a bare `config.get()` inside rectilinear-infill.
// Without this they would read as unimplemented and, worse, drift (ticket 06)
// would call them dead. The honest fix is pnp-side — declare them, as ticket 10
// does for host-key metadata — at which point this array goes away.
const char* const UNDECLARED_LIVE_KEYS[] = {
    "support_type",
    "support_family",
    "infill_shift_step",
};

// Derive the handled set from what translate() actually routed.
//
//     handled(k) = k is itself a key the backend declares       (identity)
//               or k was routed to at least one such key        (curated table)
//
// A curated row pointing at a target the backend no longer declares therefore
// stops counting: its source goes amber instead of silently claiming to work.
std::set<std::string> handled_from_routes(
    const std::map<std::string, std::vector<std::string>>& routed,
    const PnpKeyUniverse*                                  universe)
{
    std::set<std::string> handled;
    if (universe == nullptr) {
        // Unprobed: no evidence about targets, so every key the table consumed
        // counts — the pre-ticket-05 answer.
        for (const auto& entry : routed)
            handled.insert(entry.first);
        return handled;
    }
    for (const auto& entry : routed) {
        if (universe->count(entry.first) != 0) {
            handled.insert(entry.first);
            continue;
        }
        for (const std::string& target : entry.second)
            if (universe->count(target) != 0) {
                handled.insert(entry.first);
                break;
            }
    }
    return handled;
}

// Process-wide installed universe. Not sealed (unlike ticket 02's key
// registry): read-only data with no ordinal or preset consequences.
// `s_universe_generation` invalidates the derived-handled-set cache below.
bool          s_universe_known      = false;
PnpKeyUniverse s_universe;
unsigned      s_universe_generation = 0;

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

// Pattern → fill-role holder remap (ticket 013, reopened past v1). pnp
// selects the infill module per fill-role claim via the ResolvedConfig
// {top,bottom,bridge,sparse}_fill_holder keys; Orca's pattern enums have no
// pnp equivalent, so each value is remapped to the module id that holds the
// claim. Only modules that actually hold the claim are offered — gyroid and
// lightning hold claim:sparse-fill only, so top/bottom never map to them
// (an unknown holder makes the module emit nothing for that role, a silent
// loss). Everything else falls back to rectilinear-infill, the only module
// holding all four claims, with a lossy-fallback warning.
struct PatternValue
{
    const char* orca_value;
    const char* module;
};

struct PatternRow
{
    const char*        orca_key;
    const char*        pnp_key;   // holder key in the pnp config
    const char*        fallback;  // module id for values with no pnp module
    const PatternValue* supported;
    size_t             supported_count;
};

const PatternValue SPARSE_PATTERNS[] = {
    {"rectilinear", "rectilinear-infill"},
    {"gyroid",      "gyroid-infill"},
    {"lightning",   "lightning-infill"},
};
const PatternValue SOLID_PATTERNS[] = {
    {"rectilinear", "rectilinear-infill"},
};

const PatternRow PATTERN_ROWS[] = {
    {"sparse_infill_pattern",  "sparse_fill_holder",  "rectilinear-infill", SPARSE_PATTERNS, 3},
    {"top_surface_pattern",    "top_fill_holder",     "rectilinear-infill", SOLID_PATTERNS,  1},
    {"bottom_surface_pattern", "bottom_fill_holder",  "rectilinear-infill", SOLID_PATTERNS,  1},
};

const PatternRow* pattern_row_for(const std::string& orca_key)
{
    for (const PatternRow& row : PATTERN_ROWS)
        if (orca_key == row.orca_key)
            return &row;
    return nullptr;
}

// Module id for an orca pattern value, or nullptr when the value has no pnp
// module (the caller falls back and warns).
const char* pattern_module_for(const PatternRow& row, const std::string& value)
{
    for (size_t i = 0; i < row.supported_count; ++i)
        if (value == row.supported[i].orca_value)
            return row.supported[i].module;
    return nullptr;
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

PnpTranslationResult translate(const DynamicPrintConfig& cfg, const PnpKeyUniverse* universe)
{
    PnpTranslationResult result;
    result.json = json::object();
    json& out   = result.json;

    const auto put = [&](const std::string& pnp_key, json value) {
        if (!value.is_null())
            out[pnp_key] = std::move(value);
    };
    const auto warn = [&](const std::string& key, PnpWarningClass cls,
                          std::string orca_value, std::string sent_value = std::string()) {
        result.warnings.push_back({key, cls, std::move(orca_value), std::move(sent_value)});
    };
    // Record that `orca_key` was consumed, and (unless pnp_key is empty) which
    // pnp key it was written to. Called by every routing site so the handled
    // set can be derived from the routing itself — ticket 05.
    const auto route = [&](const std::string& orca_key, const std::string& pnp_key) {
        std::vector<std::string>& targets = result.routed[orca_key];
        if (!pnp_key.empty() &&
            std::find(targets.begin(), targets.end(), pnp_key) == targets.end())
            targets.push_back(pnp_key);
    };
    // Copy an orca option to a (possibly renamed) pnp key.
    const auto copy_as = [&](const std::string& orca_key, const std::string& pnp_key) {
        route(orca_key, pnp_key);
        put(pnp_key, option_to_json(cfg.option(orca_key)));
    };

    // -----------------------------------------------------------------------
    // Identity pass (ticket 05) — copy every Orca key the backend also
    // declares, under its own name. This is the map's primary integration
    // mechanism: pnp renames its keys to Orca's, so name identity is the
    // contract, and asking the live schema rather than a static list is what
    // lets a newly-declared pnp key reach the GUI with no fork edit.
    //
    // It runs FIRST so the Tier-B rows below can overwrite the keys whose
    // value still needs a unit fix or an enum respelling (ironing_flow, the
    // line widths, wall_generator, wall_sequence, the bead widths). Running it
    // last would clobber those fixes with the raw Orca value.
    //
    // Running it first also repairs, at no cost, the five settings ticket 01
    // found had silently stopped reaching pnp at the submodule bump: the four
    // part-cooling keys and support_interface_spacing, whose curated rows now
    // write dead target names, plus enable_support — whose row has written the
    // non-existent `support_enabled` since it was first authored, so turning
    // supports on in the GUI never turned them on in pnp. The dead rows below
    // still fire and still write their dead targets (repairing them is ticket
    // 09's job); pnp ignores keys it does not declare, and the live identity
    // copy is what it now reads.
    //
    // With no universe the fork has no evidence, so it falls back to the
    // compiled-in TIER_A_KEYS list and behaves exactly as it did before.
    // -----------------------------------------------------------------------
    if (universe != nullptr) {
        for (const std::string& key : cfg.keys())
            if (universe->count(key) != 0)
                copy_as(key, key);
    } else {
        for (const char* key : TIER_A_KEYS)
            copy_as(key, key);
    }

    // -----------------------------------------------------------------------
    // Tier B — renames / transforms (mapping asset, Tier-B table).
    // -----------------------------------------------------------------------

    // printable_area -> bed_shape: points list -> flat [x0, y0, x1, y1, ...]
    // in mm (pnp types it float-list; nested pairs fail config resolution).
    if (auto* pts = cfg.option<ConfigOptionPoints>("printable_area"); pts != nullptr) {
        route("printable_area", "bed_shape");
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
        route("curr_bed_type", "bed_temperature_initial_layer_single");
        if (!temp_key.empty()) {
            route(temp_key, "bed_temperature_initial_layer_single");
            put("bed_temperature_initial_layer_single", option_to_json(cfg.option(temp_key)));
        }
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
        route("sparse_infill_density", "infill_density");
        route("sparse_infill_density", "sparse_infill_density");
        out["infill_density"]        = v.get<double>() / 100.;
        out["sparse_infill_density"] = v;
    }
    // sparse_infill_speed: one source, two sinks (module key + host speed).
    if (json v = option_to_json(cfg.option("sparse_infill_speed")); !v.is_null()) {
        route("sparse_infill_speed", "infill_speed");
        route("sparse_infill_speed", "sparse_infill_speed");
        out["infill_speed"]        = v;
        out["sparse_infill_speed"] = v;
    }

    // Pattern → fill-role holder (ticket 013, reopened past v1): pnp selects
    // the infill module per claim via the *_fill_holder keys. Orca pattern
    // values are remapped to the module id holding the claim; values with no
    // pnp module fall back to rectilinear-infill (the only module holding
    // all four claims) with a lossy-fallback warning.
    for (const PatternRow& row : PATTERN_ROWS) {
        if (cfg.option(row.orca_key) == nullptr)
            continue;
        route(row.orca_key, row.pnp_key);
        const std::string value  = serialize_or_empty(cfg, row.orca_key);
        const char*       module = pattern_module_for(row, value);
        if (module == nullptr) {
            module = row.fallback;
            warn(row.orca_key, PnpWarningClass::LossyFallback, value, module);
        }
        out[row.pnp_key] = module;
    }

    // Fork-specific bridge module picker (ticket 013, reopened past v1):
    // pnp_bridge_fill_holder is an Orca-side key whose value is a pnp module
    // id; rename to pnp's bridge_fill_holder.
    copy_as("pnp_bridge_fill_holder", "bridge_fill_holder");

    // ironing_type -> ironing_enabled (bool).
    if (cfg.option("ironing_type") != nullptr) {
        route("ironing_type", "ironing_enabled");
        out["ironing_enabled"] = serialize_or_empty(cfg, "ironing_type") != "no ironing";
    }
    // ironing_flow / ironing_spacing additionally feed PNP variant keys
    // (their identity copies are Tier A above).
    route("ironing_flow", "ironing_flow_rate");
    route("ironing_spacing", "ironing_spacing_mm");
    put("ironing_flow_rate", option_to_json(cfg.option("ironing_flow")));
    put("ironing_spacing_mm", option_to_json(cfg.option("ironing_spacing")));
    // Unit fix over the Tier-A copy: Orca ironing_flow is a percent (e.g. 10),
    // pnp ironing_flow is a fraction in [0.01, 1] (ironing_flow_rate above is
    // the percent-style variant and passes through raw).
    if (const ConfigOption* opt = cfg.option("ironing_flow"); opt != nullptr) {
        route("ironing_flow", "ironing_flow");
        out["ironing_flow"] = std::clamp(opt->getFloat() / 100., 0.01, 1.0);
    }

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
        route(key, key);
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
        if (opt != nullptr && nozzle_d > 0.) {
            route(key, key);
            out[key] = opt->getFloat() / 100. * nozzle_d;
        }
    }
    // wall_transition_filter_deviation is consumed by arachne via units_to_mm
    // (1 unit = 100 nm), so the percent-of-nozzle value must arrive in units.
    if (const ConfigOption* opt = cfg.option("wall_transition_filter_deviation");
        opt != nullptr && nozzle_d > 0.) {
        route("wall_transition_filter_deviation", "wall_transition_filter_deviation");
        out["wall_transition_filter_deviation"] = std::round(opt->getFloat() / 100. * nozzle_d * 10000.);
    }
    // overhang_1_4_speed percent resolves over outer_wall_speed (its Orca
    // ratio_over); pnp types it float.
    if (auto* v = cfg.option<ConfigOptionFloatsOrPercents>("overhang_1_4_speed");
        v != nullptr && !v->values.empty() && v->values.front().percent) {
        route("overhang_1_4_speed", "overhang_1_4_speed");
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
        if (opt != nullptr && nozzle_d > 0.) {
            route(key, key);
            out[key] = std::round(opt->getFloat() / 100. * nozzle_d * 10000.);
        }
    }

    // Orca tree_support_wall_count 0 means "auto"; pnp requires [1, 10] with no
    // auto mode. Drop the key so pnp's default applies, and log the fallback.
    if (const ConfigOption* opt = cfg.option("tree_support_wall_count");
        opt != nullptr && opt->getInt() < 1) {
        route("tree_support_wall_count", "tree_support_wall_count");
        out.erase("tree_support_wall_count");
        warn("tree_support_wall_count", PnpWarningClass::LossyFallback,
             opt->serialize(), "(omitted; pnp default)");
    }

    // seam_position -> seam_mode. nearest/back/random map; Orca "aligned"
    // (and "aligned_back") have no PNP equivalent -> "nearest" + lossy warning.
    if (cfg.option("seam_position") != nullptr) {
        route("seam_position", "seam_mode");
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
        route("skirt_loops", "skirt_brim_enabled");
        route("brim_type", "skirt_brim_enabled");
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
    if (cfg.option("wall_generator") != nullptr) {
        route("wall_generator", "wall_generator");
        out["wall_generator"] = serialize_or_empty(cfg, "wall_generator");
    }

    // wall_sequence: Orca serializes "inner wall/outer wall"-style; PNP expects
    // "InnerOuter"-style (mapping asset, Tier-A caveats).
    if (cfg.option("wall_sequence") != nullptr) {
        route("wall_sequence", "wall_sequence");
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
    if (cfg.option("support_base_pattern_spacing") != nullptr) {
        // Sends nothing, so it is recorded as a consumed key with no target: it
        // counts as handled only when the identity pass has covered it, which
        // at dbf3449c it does (traditional-support now declares the key).
        route("support_base_pattern_spacing", std::string());
        warn("support_base_pattern_spacing", PnpWarningClass::LossyFallback,
             serialize_or_empty(cfg, "support_base_pattern_spacing"), std::string());
    }

    copy_as("support_interface_spacing", "tree_support_interface_spacing_mm");

    // fuzzy_skin group: enum gates whether the module keys are emitted at all.
    {
        route("fuzzy_skin", "apply_to_all");
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
        route("fuzzy_skin_thickness", "thickness");
        route("fuzzy_skin_point_distance", "point_distance");
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
    const std::set<std::string> handled = handled_from_routes(result.routed, universe);
    for (const std::string& key : cfg.keys())
        if (handled.count(key) == 0)
            warn(key, tier_d_class(key), cfg.opt_serialize(key));

    return result;
}

PnpKeyUniverse pnp_key_universe_from_schema(const json& schema_doc)
{
    PnpKeyUniverse universe;
    if (!schema_doc.is_object())
        return universe;

    const auto collect = [&universe](const json& field) {
        if (!field.is_object())
            return;
        auto key = field.find("key");
        if (key != field.end() && key->is_string())
            universe.insert(key->get<std::string>());
    };

    // Per-module manifest fields.
    if (auto schema = schema_doc.find("schema"); schema != schema_doc.end() && schema->is_array())
        for (const json& module : *schema)
            if (auto fields = module.find("fields"); fields != module.end() && fields->is_array())
                for (const json& field : *fields)
                    collect(field);

    // Host keys — the config pnp's own built-ins read, which no module manifest
    // declares. Added by wire 1.1.0 (ticket 02); absent from older backends, and
    // ticket 01 measured 62 keys that live only here, 14 of them curated-table
    // targets. Without this half a schema-derived handled set would call every
    // one of those rows dead.
    if (auto host = schema_doc.find("host"); host != schema_doc.end() && host->is_array())
        for (const json& field : *host)
            collect(field);

    // Only extend a universe the document actually described: an empty or
    // malformed reply must stay empty so callers can tell "no evidence" from
    // "a backend that declares only these".
    if (!universe.empty())
        for (const char* key : UNDECLARED_LIVE_KEYS)
            universe.insert(key);

    return universe;
}

void set_pnp_key_universe(PnpKeyUniverse universe)
{
    s_universe       = std::move(universe);
    s_universe_known = true;
    ++ s_universe_generation;
}

bool pnp_key_universe_known() { return s_universe_known; }

void reset_pnp_key_universe()
{
    s_universe.clear();
    s_universe_known = false;
    ++ s_universe_generation;
}

PnpTranslationResult translate(const DynamicPrintConfig& cfg)
{
    return translate(cfg, s_universe_known ? &s_universe : nullptr);
}

std::set<std::string> pnp_handled_keys(const PnpKeyUniverse* universe)
{
    // Derived by running the translator over the stock key set and reading its
    // provenance back, so the answer can never disagree with what translate()
    // does — the agreement the old static array had to be unit-tested for.
    const DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    return handled_from_routes(translate(cfg, universe).routed, universe);
}

bool pnp_key_is_unimplemented(const std::string& orca_key)
{
    // No probe, no backend, so nothing reaches pnp: every key is unimplemented
    // and the broken install shows on every settings tab, not only on the PNP
    // page's error banner.
    if (!s_universe_known)
        return true;

    // One translate() per installed universe; the tint is queried per label on
    // every tab rebuild, so this must not re-run the translator each time.
    static std::set<std::string> cached;
    static unsigned              cached_generation = 0;
    static bool                  cached_valid      = false;
    if (!cached_valid || cached_generation != s_universe_generation) {
        cached            = pnp_handled_keys(&s_universe);
        cached_generation = s_universe_generation;
        cached_valid      = true;
    }
    return cached.count(orca_key) == 0;
}

bool pnp_pattern_value_supported(const std::string& orca_key, const std::string& orca_value)
{
    const PatternRow* row = pattern_row_for(orca_key);
    return row != nullptr && pattern_module_for(*row, orca_value) != nullptr;
}

bool pnp_pattern_key(const std::string& orca_key)
{
    return orca_key == "sparse_infill_pattern" || orca_key == "top_surface_pattern"
        || orca_key == "bottom_surface_pattern" || orca_key == "internal_solid_infill_pattern";
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
