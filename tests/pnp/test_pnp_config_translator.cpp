// PNP fork: regression tests for PnpConfigTranslator (F01).
//
// Every SECTION under "translator regressions" reproduces a real failure
// found during the first GUI slices (batch B3): values pnp's config
// resolution rejected outright, plus two crashes in the fixup code itself.
// The "schema guard" tests cover the generic guard that drops any key the
// pnp config-schema would reject, so config mismatches degrade to logged
// warnings instead of failed slices.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <set>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/PnpConfigTranslator.hpp"

using json = nlohmann::json;
using namespace Slic3r;
using namespace Slic3r::GUI;
using Catch::Approx;

namespace {

// Minimal config carrying only the keys a test cares about.
DynamicPrintConfig make_config(const std::initializer_list<std::pair<std::string, std::string>>& kv)
{
    DynamicPrintConfig cfg;
    for (const auto& [key, value] : kv)
        cfg.set_deserialize_strict(key, value);
    return cfg;
}

// SchemaBridgeMap ticket 05: the handled set is derived from an installed key
// universe, so these tests drive a fixture one instead of a static array. RAII
// so a failing REQUIRE cannot leak the universe into the next test.
PnpConfigTranslator::PnpKeyUniverse universe_of(std::initializer_list<const char*> keys)
{
    PnpConfigTranslator::PnpKeyUniverse u;
    for (const char* k : keys)
        u.insert(k);
    return u;
}

struct ScopedUniverse
{
    explicit ScopedUniverse(PnpConfigTranslator::PnpKeyUniverse u)
    {
        PnpConfigTranslator::set_pnp_key_universe(std::move(u));
    }
    ~ScopedUniverse() { PnpConfigTranslator::reset_pnp_key_universe(); }
};

bool has_warning_for(const std::vector<PnpConfigWarning>& warnings, const std::string& key)
{
    for (const PnpConfigWarning& w : warnings)
        if (w.key == key)
            return true;
    return false;
}

} // namespace

TEST_CASE("translator regressions from first GUI slices", "[pnp][translator]")
{
    SECTION("ironing_flow percent becomes a pnp fraction in [0.01, 1]")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"ironing_flow", "10"}}));
        REQUIRE(res.json.at("ironing_flow").get<double>() == Approx(0.1));
    }

    SECTION("line widths given as percent resolve against the nozzle diameter")
    {
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"line_width", "105%"},
            {"inner_wall_line_width", "125%"},
            {"outer_wall_line_width", "105%"},
        }));
        REQUIRE(res.json.at("line_width").get<double>() == Approx(0.525));
        REQUIRE(res.json.at("inner_wall_line_width").get<double>() == Approx(0.625));
        REQUIRE(res.json.at("outer_wall_line_width").get<double>() == Approx(0.525));
    }

    SECTION("absolute line widths pass through unchanged")
    {
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.4"},
            {"line_width", "0.42"},
        }));
        REQUIRE(res.json.at("line_width").get<double>() == Approx(0.42));
    }

    // Regression: a GUI slice died at pnp config resolution with
    //   "config key 'initial_layer_line_width': expected Float value, got String".
    // The key was absent from TIER_A_KEYS *and* from the percent-resolution
    // list, so nothing emitted it; pnp then fell back to the 3MF sidecar's
    // project_settings.config, where Orca had stored the raw "100%" string.
    // pnp declares this key float, so the string was a hard error. Both the
    // percent and the absolute form must be emitted here, as JSON numbers.
    SECTION("initial_layer_line_width reaches pnp as a number, never a string")
    {
        auto pct = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"initial_layer_line_width", "100%"},
        }));
        REQUIRE(pct.json.contains("initial_layer_line_width"));
        REQUIRE(pct.json.at("initial_layer_line_width").is_number());
        REQUIRE_THAT(pct.json.at("initial_layer_line_width").get<double>(),
                     Catch::Matchers::WithinAbs(0.5, 1e-9));

        auto abs = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.4"},
            {"initial_layer_line_width", "0.42"},
        }));
        REQUIRE(abs.json.contains("initial_layer_line_width"));
        REQUIRE(abs.json.at("initial_layer_line_width").is_number());
        REQUIRE_THAT(abs.json.at("initial_layer_line_width").get<double>(),
                     Catch::Matchers::WithinAbs(0.42, 1e-9));
    }

    SECTION("tree_support_wall_count 0 (Orca auto) is omitted with a warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"tree_support_wall_count", "0"}}));
        REQUIRE(!res.json.contains("tree_support_wall_count"));
        REQUIRE(has_warning_for(res.warnings, "tree_support_wall_count"));
    }

    SECTION("bead widths convert percent-of-nozzle to pnp units (1 unit = 100 nm)")
    {
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"min_bead_width", "50"},
            {"initial_layer_min_bead_width", "85"},
        }));
        REQUIRE(res.json.at("min_bead_width").get<double>() == Approx(2500));
        REQUIRE(res.json.at("initial_layer_min_bead_width").get<double>() == Approx(4250));
    }

    SECTION("bed_shape is a flat float list, not nested pairs")
    {
        auto res = PnpConfigTranslator::translate(make_config({
            {"printable_area", "0x0,220x0,220x200,0x200"},
        }));
        const json& shape = res.json.at("bed_shape");
        REQUIRE(shape.is_array());
        REQUIRE(shape.size() == 8);
        for (const json& v : shape)
            REQUIRE(v.is_number());
        REQUIRE(shape[2].get<double>() == Approx(220.0));
        REQUIRE(shape[5].get<double>() == Approx(200.0));
    }

    SECTION("percent overhang_1_4_speed without outer_wall_speed does not crash and is dropped")
    {
        // This exact shape null-dereffed (WER c0000005) before the guard.
        auto res = PnpConfigTranslator::translate(make_config({{"overhang_1_4_speed", "90%"}}));
        REQUIRE(!res.json.contains("overhang_1_4_speed"));
    }

    SECTION("percent overhang_1_4_speed resolves over outer_wall_speed (coFloats vector)")
    {
        // getFloat() on the coFloats vector threw before the indexed accessor.
        auto res = PnpConfigTranslator::translate(make_config({
            {"overhang_1_4_speed", "90%"},
            {"outer_wall_speed", "120"},
        }));
        REQUIRE(res.json.at("overhang_1_4_speed").get<double>() == Approx(108.0));
    }

    SECTION("full default config translates without throwing")
    {
        DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
        REQUIRE_NOTHROW(PnpConfigTranslator::translate(cfg));
    }
}

// PNP fork: unit-mismatch regressions found by visual-debugging real slices.
// pnp's ResolvedConfig/module consumers type these keys differently than
// Orca's presets, so the raw percent values produced 100% infill and
// mm-scale Arachne widths on the sliced G-code.
TEST_CASE("translator unit fixes for pnp consumers", "[pnp][translator][units]")
{
    SECTION("sparse_infill_density percent becomes a pnp fraction for infill_density")
    {
        // pnp's infill_density is a fraction (0.0-1.0, default 0.2); Orca's
        // sparse_infill_density is a percent (25 = 25%). Sending 25 raw made
        // pnp slice at 2500% density -> clamped to 100% infill.
        auto res = PnpConfigTranslator::translate(make_config({{"sparse_infill_density", "25"}}));
        REQUIRE(res.json.at("infill_density").get<double>() == Approx(0.25));
        // The sparse_infill_density sink stays percent: the perimeter modules
        // use it only as a > 0 gate (alternate_extra_wall).
        REQUIRE(res.json.at("sparse_infill_density").get<double>() == Approx(25.0));
    }

    SECTION("min_feature_size percent resolves against the nozzle diameter")
    {
        // pnp's arachne module resolves min_feature_size via get_abs_value
        // against the nozzle; a plain number is treated as absolute mm, so the
        // raw Orca percent (15 = 15% of 0.5 mm) became a 15 mm feature size.
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"min_feature_size", "15"},
        }));
        REQUIRE(res.json.at("min_feature_size").get<double>() == Approx(0.075));
    }

    SECTION("wall_transition_length percent resolves against the nozzle diameter")
    {
        // Same class: 100% of 0.5 mm must reach pnp as 0.5 mm, not 100 mm.
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"wall_transition_length", "100"},
        }));
        REQUIRE(res.json.at("wall_transition_length").get<double>() == Approx(0.5));
    }

    SECTION("wall_transition_filter_deviation percent converts to pnp units (1 unit = 100 nm)")
    {
        // pnp's arachne module consumes this key via units_to_mm; the raw
        // Orca percent (25 = 25% of 0.5 mm = 0.125 mm) must arrive as 1250
        // units, not 25 (which units_to_mm reads as 0.0025 mm).
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"wall_transition_filter_deviation", "25"},
        }));
        REQUIRE(res.json.at("wall_transition_filter_deviation").get<double>() == Approx(1250));
    }

    SECTION("role-specific infill/top/bridge line widths reach pnp as absolute mm")
    {
        // These four keys were Tier D (never sent), so pnp fell back to
        // line_width / 1.125*nozzle instead of the user's widths.
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.5"},
            {"sparse_infill_line_width", "100%"},
            {"internal_solid_infill_line_width", "100%"},
            {"top_surface_line_width", "100%"},
            {"bridge_line_width", "100%"},
        }));
        REQUIRE(res.json.at("sparse_infill_line_width").get<double>() == Approx(0.5));
        REQUIRE(res.json.at("internal_solid_infill_line_width").get<double>() == Approx(0.5));
        REQUIRE(res.json.at("top_surface_line_width").get<double>() == Approx(0.5));
        REQUIRE(res.json.at("bridge_line_width").get<double>() == Approx(0.5));
    }

    SECTION("absolute role-specific line widths pass through unchanged")
    {
        auto res = PnpConfigTranslator::translate(make_config({
            {"nozzle_diameter", "0.4"},
            {"sparse_infill_line_width", "0.45"},
            {"bridge_line_width", "0.4"},
        }));
        REQUIRE(res.json.at("sparse_infill_line_width").get<double>() == Approx(0.45));
        REQUIRE(res.json.at("bridge_line_width").get<double>() == Approx(0.4));
    }
}

TEST_CASE("schema guard drops keys the pnp schema would reject", "[pnp][translator][schema-guard]")
{
    // Synthetic schema mirroring the real `pnp_cli module config-schema` shape.
    const json schema = json::parse(R"({
        "schema_version": "1.1.0",
        "schema": [{"module": "test", "fields": [
            {"key": "float_key",   "type": "float", "min": 0.1, "max": 2.0},
            {"key": "int_key",     "type": "int",   "min": 1,   "max": 10},
            {"key": "bool_key",    "type": "bool"},
            {"key": "string_key",  "type": "string"},
            {"key": "percent_key", "type": "percent"},
            {"key": "fop_key",     "type": "float_or_percent"},
            {"key": "list_key",    "type": "float-list"},
            {"key": "enum_key",    "type": "enum", "values": ["a", "b"]}
        ]}]
    })");

    std::vector<PnpConfigWarning> warnings;

    SECTION("conforming values survive untouched")
    {
        json cfg = {{"float_key", 0.5}, {"int_key", 3}, {"bool_key", true},
                    {"string_key", "x"}, {"percent_key", "10%"}, {"fop_key", 1.5},
                    {"list_key", {1.0, 2.0}}, {"enum_key", "a"}};
        const json before = cfg;
        PnpConfigTranslator::apply_schema_guard(cfg, schema, warnings);
        REQUIRE(cfg == before);
        REQUIRE(warnings.empty());
    }

    SECTION("type mismatches are dropped with a warning")
    {
        json cfg = {{"float_key", "105%"}, {"bool_key", "yes"}, {"list_key", {{1.0, 2.0}}}};
        PnpConfigTranslator::apply_schema_guard(cfg, schema, warnings);
        REQUIRE(cfg.empty());
        REQUIRE(warnings.size() == 3);
    }

    SECTION("range violations are dropped with a warning")
    {
        json cfg = {{"float_key", 10.0}, {"int_key", 0}};
        PnpConfigTranslator::apply_schema_guard(cfg, schema, warnings);
        REQUIRE(!cfg.contains("float_key"));
        REQUIRE(!cfg.contains("int_key"));
        REQUIRE(warnings.size() == 2);
    }

    SECTION("unknown enum values are dropped, known ones kept")
    {
        json cfg = {{"enum_key", "c"}};
        PnpConfigTranslator::apply_schema_guard(cfg, schema, warnings);
        REQUIRE(!cfg.contains("enum_key"));
        REQUIRE(warnings.size() == 1);
    }

    SECTION("keys unknown to the schema pass through (pnp ignores them)")
    {
        json cfg = {{"no_such_key", "whatever"}};
        PnpConfigTranslator::apply_schema_guard(cfg, schema, warnings);
        REQUIRE(cfg.contains("no_such_key"));
        REQUIRE(warnings.empty());
    }

    SECTION("the real ironing_flow failure shape is caught by the guard alone")
    {
        const json real_schema = json::parse(R"({
            "schema": [{"fields": [{"key": "ironing_flow", "type": "float", "min": 0.01, "max": 1.0}]}]
        })");
        json cfg = {{"ironing_flow", 10.0}};
        PnpConfigTranslator::apply_schema_guard(cfg, real_schema, warnings);
        REQUIRE(!cfg.contains("ironing_flow"));
        REQUIRE(warnings.size() == 1);
    }
}

TEST_CASE("the key universe is read from both wire halves", "[pnp][translator][ticket05]")
{
    const json doc = json::parse(R"({
        "schema_version": "1.1.0",
        "schema": [
            {"module": "com.core.a", "fields": [{"key": "layer_height", "type": "float"},
                                                {"key": "wall_count",   "type": "int"}]},
            {"module": "com.core.b", "fields": [{"key": "layer_height", "type": "float"}]}
        ],
        "host": [{"key": "travel_speed", "type": "float", "scope": "print"}]
    })");

    const auto universe = PnpConfigTranslator::pnp_key_universe_from_schema(doc);
    REQUIRE(universe.count("layer_height") == 1); // declared twice, one key
    REQUIRE(universe.count("wall_count") == 1);
    // Host keys count: ticket 01 measured 62 keys that live only in this half,
    // 14 of them curated-table targets that a schema-only view calls dead.
    REQUIRE(universe.count("travel_speed") == 1);
    // Keys pnp reads through channels the wire cannot describe (ticket 01
    // finding 7) are added so they neither tint amber nor read as dead.
    REQUIRE(universe.count("support_type") == 1);
    REQUIRE(universe.count("infill_shift_step") == 1);

    SECTION("a wire with no host array yields the module half only")
    {
        const json old_wire = json::parse(R"({"schema_version": "1.0.0",
            "schema": [{"module": "m", "fields": [{"key": "layer_height", "type": "float"}]}]})");
        const auto u = PnpConfigTranslator::pnp_key_universe_from_schema(old_wire);
        REQUIRE(u.count("layer_height") == 1);
        REQUIRE(u.count("travel_speed") == 0);
    }

    SECTION("an empty or malformed document yields no universe, not a bare allowlist")
    {
        // Callers must be able to tell "no evidence" from "a backend that
        // declares only these three keys".
        REQUIRE(PnpConfigTranslator::pnp_key_universe_from_schema(json::object()).empty());
        REQUIRE(PnpConfigTranslator::pnp_key_universe_from_schema(json("nonsense")).empty());
    }
}

TEST_CASE("the handled set is derived from the live universe", "[pnp][translator][ticket05]")
{
    SECTION("a key the backend declares is handled by name identity")
    {
        // gcode_comments has no curated row at all; declaring it is enough.
        ScopedUniverse u{universe_of({"gcode_comments"})};
        REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("gcode_comments"));
        REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("gcode_flavor"));
    }

    SECTION("a curated row counts only while its target is live")
    {
        // seam_position reaches pnp solely through the rename to seam_mode.
        ScopedUniverse live{universe_of({"seam_mode"})};
        REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("seam_position"));
    }

    SECTION("a curated row whose target the backend dropped goes amber")
    {
        // This is the drift the static handled set could not see: the row still
        // fires, but nothing reads what it writes.
        ScopedUniverse dead{universe_of({"some_unrelated_key"})};
        REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("seam_position"));
    }

    SECTION("with no probe every key is unimplemented")
    {
        PnpConfigTranslator::reset_pnp_key_universe();
        REQUIRE(PnpConfigTranslator::pnp_key_universe_known() == false);
        // Nothing reaches pnp without a backend, so the tint says so on every
        // tab rather than only on the PNP page's banner.
        REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("layer_height"));
        REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("gcode_flavor"));
    }
}

TEST_CASE("the identity pass sends every declared key under its own name", "[pnp][translator][ticket05]")
{
    // Ticket 01 found five settings that silently stopped reaching pnp at the
    // submodule bump, because pnp renamed its keys to Orca's and the curated
    // rows kept writing the old names. Deriving the identity pass from the live
    // universe repairs them without touching the rows.
    DynamicPrintConfig cfg = make_config({{"fan_max_speed", "80"},
                                          {"enable_support", "1"},
                                          {"support_interface_spacing", "0.3"}});
    PnpConfigTranslator::PnpKeyUniverse universe = universe_of({"fan_max_speed", "enable_support",
                                                                "support_interface_spacing"});
    const auto res = PnpConfigTranslator::translate(cfg, &universe);

    REQUIRE(res.json.contains("fan_max_speed"));
    // enable_support is the one pnp actually reads; the curated row had written
    // the non-existent support_enabled since it was authored, so supports never
    // switched on (ticket 01, finding 2). Ticket 09 deleted that row and the
    // other five dead renames — the live identity pass is the only writer left,
    // so no dead name may appear in the emitted config (ticket 09).
    REQUIRE(res.json.contains("enable_support"));
    REQUIRE(res.json["enable_support"] == true);
    REQUIRE(res.json.contains("support_interface_spacing"));
    REQUIRE_FALSE(res.json.contains("support_enabled"));

    SECTION("a key the backend does not declare is not sent")
    {
        PnpConfigTranslator::PnpKeyUniverse empty;
        const auto none = PnpConfigTranslator::translate(cfg, &empty);
        REQUIRE_FALSE(none.json.contains("fan_max_speed"));
    }
}

// ---------------------------------------------------------------------------
// SchemaBridgeMap ticket 09 — the six dead curated rows are gone.
//
// Ticket 01 found six rename rows still writing pnp key names the backend does
// not declare (plus `support_density`'s warn-only row). Ticket 05's identity
// pass had made each setting reach pnp under its own name anyway, so the rows
// only added dead names to the emitted config; ticket 09 deleted them. Each
// SECTION pins one setting: sent under its own name, and the dead name it
// used to write is gone — probed (the live path) and unprobed (the
// TIER_A_KEYS fallback, which now carries the six names itself).
// ---------------------------------------------------------------------------

TEST_CASE("the dead curated rows are deleted, the settings still reach pnp", "[pnp][translator][ticket09]")
{
    // probed: every setting's target is its own name, from the universe.
    DynamicPrintConfig cfg                       = make_config({{"close_fan_the_first_x_layers", "3"},
                                                                {"enable_overhang_bridge_fan", "1"},
                                                                {"enable_support", "1"},
                                                                {"fan_max_speed", "80"},
                                                                {"fan_min_speed", "10"},
                                                                {"support_interface_spacing", "0.25"},
                                                                {"support_base_pattern_spacing", "2.5"},
                                                                {"raft_layers", "3"}});
    PnpConfigTranslator::PnpKeyUniverse universe = universe_of(
        {"close_fan_the_first_x_layers", "enable_overhang_bridge_fan", "enable_support", "fan_max_speed", "fan_min_speed",
         "support_interface_spacing", "support_base_pattern_spacing", "support_raft_layers"});

    const auto res = PnpConfigTranslator::translate(cfg, &universe);

    SECTION("close_fan_the_first_x_layers is identity-routed, not disable_fan_first_layers")
    {
        REQUIRE(res.json.at("close_fan_the_first_x_layers").get<int>() == 3);
        REQUIRE_FALSE(res.json.contains("disable_fan_first_layers"));
    }
    SECTION("enable_overhang_bridge_fan is identity-routed, not enable_overhang_fan")
    {
        REQUIRE(res.json.at("enable_overhang_bridge_fan") == true);
        REQUIRE_FALSE(res.json.contains("enable_overhang_fan"));
    }
    SECTION("enable_support is identity-routed, never support_enabled")
    {
        REQUIRE(res.json.at("enable_support") == true);
        REQUIRE_FALSE(res.json.contains("support_enabled"));
    }
    SECTION("fan_max_speed / fan_min_speed are identity-routed, not fan_speed_max/min")
    {
        REQUIRE(res.json.at("fan_max_speed").get<int>() == 80);
        REQUIRE(res.json.at("fan_min_speed").get<int>() == 10);
        REQUIRE_FALSE(res.json.contains("fan_speed_max"));
        REQUIRE_FALSE(res.json.contains("fan_speed_min"));
    }
    SECTION("support_interface_spacing is identity-routed, not tree_support_interface_spacing_mm")
    {
        REQUIRE(res.json.at("support_interface_spacing").get<double>() == Approx(0.25));
        REQUIRE_FALSE(res.json.contains("tree_support_interface_spacing_mm"));
    }
    SECTION("support_base_pattern_spacing is identity-routed and no longer warns")
    {
        REQUIRE(res.json.at("support_base_pattern_spacing").get<double>() == Approx(2.5));
        REQUIRE_FALSE(has_warning_for(res.warnings, "support_base_pattern_spacing"));
    }
    SECTION("raft_layers still warns when supports are off, routed via enable_support")
    {
        auto r = PnpConfigTranslator::translate(make_config({{"raft_layers", "2"}}), &universe);
        REQUIRE(r.json.contains("support_raft_layers"));
        REQUIRE(has_warning_for(r.warnings, "raft_layers"));
        // The enable_support -> support_raft_layers edge sits inside the
        // raft>0 branch, so with the raft off it is not recorded and the
        // warning set cannot consume enable_support.
        auto quiet = PnpConfigTranslator::translate(make_config({{"raft_layers", "0"}}), &universe);
        // The unconditional copy_as still sends the raft layer count (only the
        // warning is conditional) — its Tier-D record must name raft_layers,
        // not enable_support.
        REQUIRE(quiet.json.contains("support_raft_layers"));
        REQUIRE_FALSE(has_warning_for(quiet.warnings, "enable_support"));
    }
    SECTION("unprobed, the fallback list now carries the six Orca names")
    {
        const auto unprobed = PnpConfigTranslator::translate(cfg, nullptr);
        for (const char* key : {"close_fan_the_first_x_layers", "enable_overhang_bridge_fan", "enable_support", "fan_max_speed",
                                "fan_min_speed", "support_interface_spacing", "support_base_pattern_spacing"})
            REQUIRE(unprobed.json.contains(key));
        REQUIRE_FALSE(unprobed.json.contains("support_enabled"));
        REQUIRE_FALSE(unprobed.json.contains("tree_support_interface_spacing_mm"));
    }
}

TEST_CASE("the identity pass never clobbers a Tier-B unit fix", "[pnp][translator][ticket05]")
{
    // ironing_flow is declared by pnp AND needs a percent->fraction fix, so the
    // identity copy must run first and the curated row must win.
    DynamicPrintConfig cfg = make_config({{"ironing_flow", "10"}, {"nozzle_diameter", "0.4"}});
    PnpConfigTranslator::PnpKeyUniverse universe = universe_of({"ironing_flow", "line_width"});
    const auto res = PnpConfigTranslator::translate(cfg, &universe);
    REQUIRE(res.json["ironing_flow"].get<double>() == Approx(0.10));

    SECTION("float-or-percent widths still arrive resolved, never as \"105%\"")
    {
        DynamicPrintConfig wcfg = make_config({{"line_width", "105%"}, {"nozzle_diameter", "0.4"}});
        const auto wres = PnpConfigTranslator::translate(wcfg, &universe);
        REQUIRE(wres.json["line_width"].is_number());
        REQUIRE(wres.json["line_width"].get<double>() == Approx(0.42));
    }
}

TEST_CASE("pnp_key_is_unimplemented agrees with translate()'s warnings", "[pnp][translator]")
{
    // Every key translate() warns about as Tier D (not-yet-mapped or
    // unsupported-feature) must read as unimplemented, and every key it does
    // not warn about that way must read as implemented. Lossy-fallback
    // warnings are excluded: those keys ARE sent (with a substituted value).
    //
    // Ticket 05 made both sides derive from the same provenance, so this can no
    // longer drift; it stays as the assertion that they really are one source.
    ScopedUniverse u{universe_of({"layer_height", "seam_mode", "wall_count", "infill_density",
                                  "travel_speed", "gcode_comments"})};

    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    const auto         res = PnpConfigTranslator::translate(cfg);

    std::set<std::string> tier_d_warned;
    for (const PnpConfigWarning& w : res.warnings)
        if (w.warn_class == PnpWarningClass::NotYetMapped ||
            w.warn_class == PnpWarningClass::UnsupportedFeature)
            tier_d_warned.insert(w.key);

    for (const std::string& key : cfg.keys()) {
        const bool unimplemented = PnpConfigTranslator::pnp_key_is_unimplemented(key);
        REQUIRE(unimplemented == (tier_d_warned.count(key) != 0));
    }
}

TEST_CASE("infill pattern keys remap to pnp fill-role holders", "[pnp][translator]")
{
    SECTION("sparse gyroid maps to the gyroid module, no warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"sparse_infill_pattern", "gyroid"}}));
        REQUIRE(res.json.at("sparse_fill_holder").get<std::string>() == "gyroid-infill");
        REQUIRE_FALSE(has_warning_for(res.warnings, "sparse_infill_pattern"));
    }
    SECTION("sparse lightning maps to the lightning module")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"sparse_infill_pattern", "lightning"}}));
        REQUIRE(res.json.at("sparse_fill_holder").get<std::string>() == "lightning-infill");
        REQUIRE_FALSE(has_warning_for(res.warnings, "sparse_infill_pattern"));
    }
    SECTION("sparse rectilinear maps to the rectilinear module")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"sparse_infill_pattern", "rectilinear"}}));
        REQUIRE(res.json.at("sparse_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE_FALSE(has_warning_for(res.warnings, "sparse_infill_pattern"));
    }
    SECTION("sparse crosshatch (Orca default) falls back with a lossy warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"sparse_infill_pattern", "crosshatch"}}));
        REQUIRE(res.json.at("sparse_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE(has_warning_for(res.warnings, "sparse_infill_pattern"));
    }
    SECTION("sparse honeycomb falls back with a lossy warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"sparse_infill_pattern", "honeycomb"}}));
        REQUIRE(res.json.at("sparse_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE(has_warning_for(res.warnings, "sparse_infill_pattern"));
    }
    SECTION("top monotonic (Orca default) falls back with a lossy warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"top_surface_pattern", "monotonic"}}));
        REQUIRE(res.json.at("top_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE(has_warning_for(res.warnings, "top_surface_pattern"));
    }
    SECTION("top rectilinear maps without a warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"top_surface_pattern", "rectilinear"}}));
        REQUIRE(res.json.at("top_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE_FALSE(has_warning_for(res.warnings, "top_surface_pattern"));
    }
    SECTION("top gyroid is not offered (gyroid holds no top-fill claim) and falls back")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"top_surface_pattern", "gyroid"}}));
        REQUIRE(res.json.at("top_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE(has_warning_for(res.warnings, "top_surface_pattern"));
    }
    SECTION("bottom monotonic falls back with a lossy warning")
    {
        auto res = PnpConfigTranslator::translate(make_config({{"bottom_surface_pattern", "monotonic"}}));
        REQUIRE(res.json.at("bottom_fill_holder").get<std::string>() == "rectilinear-infill");
        REQUIRE(has_warning_for(res.warnings, "bottom_surface_pattern"));
    }
}

TEST_CASE("pnp_bridge_fill_holder renames to bridge_fill_holder", "[pnp][translator]")
{
    auto res = PnpConfigTranslator::translate(make_config({{"pnp_bridge_fill_holder", "rectilinear-infill"}}));
    REQUIRE(res.json.at("bridge_fill_holder").get<std::string>() == "rectilinear-infill");
    REQUIRE_FALSE(has_warning_for(res.warnings, "pnp_bridge_fill_holder"));
}

TEST_CASE("infill_shift_step is an identity Tier-A key", "[pnp][translator]")
{
    auto res = PnpConfigTranslator::translate(make_config({{"infill_shift_step", "0.4"}}));
    REQUIRE(res.json.at("infill_shift_step").get<double>() == Approx(0.4));
    REQUIRE_FALSE(has_warning_for(res.warnings, "infill_shift_step"));
}

TEST_CASE("support_type is an identity Tier-A key", "[pnp][translator]")
{
    // The pnp claim dedup resolves the support-generator claim holder from
    // the raw sidecar `support_type` value (like the raw `enable_support`
    // key), so this row is a plain identity copy: every Orca spelling must
    // pass through verbatim and the amber "not yet mapped" tint must clear.
    for (const char* value : {"normal(auto)", "tree(auto)", "normal(manual)", "tree(manual)"}) {
        auto res = PnpConfigTranslator::translate(make_config({{"support_type", value}}));
        REQUIRE(res.json.at("support_type").get<std::string>() == value);
        REQUIRE_FALSE(has_warning_for(res.warnings, "support_type"));
    }
    // support_type is declared by no channel the wire can describe, so it is
    // carried by UNDECLARED_LIVE_KEYS and only reads implemented once a real
    // universe has been built from a schema document (ticket 01, finding 7).
    {
        const json doc = json::parse(R"({"schema": [{"module": "m",
            "fields": [{"key": "layer_height", "type": "float"}]}]})");
        ScopedUniverse u{PnpConfigTranslator::pnp_key_universe_from_schema(doc)};
        REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("support_type"));
    }
}

TEST_CASE("pnp_pattern_value_supported reflects the module tables", "[pnp][translator]")
{
    REQUIRE(PnpConfigTranslator::pnp_pattern_value_supported("sparse_infill_pattern", "gyroid"));
    REQUIRE(PnpConfigTranslator::pnp_pattern_value_supported("sparse_infill_pattern", "lightning"));
    REQUIRE(PnpConfigTranslator::pnp_pattern_value_supported("sparse_infill_pattern", "rectilinear"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("sparse_infill_pattern", "crosshatch"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("sparse_infill_pattern", "honeycomb"));
    REQUIRE(PnpConfigTranslator::pnp_pattern_value_supported("top_surface_pattern", "rectilinear"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("top_surface_pattern", "monotonic"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("top_surface_pattern", "gyroid"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("bottom_surface_pattern", "monotonic"));
    // Keys with no pattern mapping at all are fully unsupported.
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("internal_solid_infill_pattern", "monotonic"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_pattern_value_supported("no_such_key", "whatever"));
}


// ---------------------------------------------------------------------------
// SchemaBridgeMap ticket 06 — drift reconciliation.
//
// A curated rename/remap row keeps running after pnp retires the key it writes.
// pnp ignores a key it does not declare, so the Orca setting silently stops
// arriving. The bump produced exactly that: support_density, retired for
// support_base_pattern_spacing.
// ---------------------------------------------------------------------------

TEST_CASE("host-key reporting gates the drift diff", "[pnp][translator][ticket06]")
{
    REQUIRE(PnpConfigTranslator::pnp_schema_reports_host_keys(
        json::parse(R"({"schema": [], "host": []})")));
    // Wire 1.0.0: the module half only. Ticket 01 measured 14 live rows that
    // resolve through host keys, so a diff here would report all 14 dead.
    REQUIRE_FALSE(PnpConfigTranslator::pnp_schema_reports_host_keys(
        json::parse(R"({"schema": []})")));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_schema_reports_host_keys(json("nonsense")));
}

TEST_CASE("dead curated targets are the rename rows the backend dropped", "[pnp][translator][ticket06]")
{
    const std::map<std::string, std::vector<std::string>> routed {
        {"identity_key",  {"identity_key"}},          // identity pass, not a curated row
        {"live_row",      {"live_target"}},           // rename row the backend still declares
        {"deliberate_noop", {}},                      // row that sends nothing by design
        {"dead_row",      {"retired_target"}},        // the drift this ticket exists to catch
        {"fan_out_row",   {"live_target", "also_gone"}},
    };
    const auto universe = universe_of({"identity_key", "live_target"});

    const auto dead = PnpConfigTranslator::dead_curated_targets(routed, universe);
    REQUIRE(dead.size() == 2);
    // Sorted by orca_key, then pnp_key.
    REQUIRE(dead[0].orca_key == "dead_row");
    REQUIRE(dead[0].pnp_key == "retired_target");
    REQUIRE(dead[1].orca_key == "fan_out_row");
    REQUIRE(dead[1].pnp_key == "also_gone");

    SECTION("an identity key the universe lacks is not reported as a dead row")
    {
        // The identity pass is derived from the universe when probed and is the
        // TIER_A_KEYS fallback when not, so it cannot drift against the universe
        // in a way this diff would explain -- and pnp_key_is_unimplemented
        // already tints such a key amber.
        const auto only_identity = PnpConfigTranslator::dead_curated_targets(
            {{"identity_key", {"identity_key"}}}, universe_of({"something_else"}));
        REQUIRE(only_identity.empty());
    }

    SECTION("no probe means no evidence, not universal drift")
    {
        REQUIRE(PnpConfigTranslator::dead_curated_targets(routed, {}).empty());
    }
}

TEST_CASE("the real curated table is diffed against the live universe", "[pnp][translator][ticket06]")
{
    // Harvest the table's own targets by running the unprobed translator, the
    // same derivation PnpConfigKeys::register_from_schema uses, then take one
    // known rename target away: wall_loops -> wall_count.
    const DynamicPrintConfig defaults = DynamicPrintConfig::full_print_config();
    const auto               routed   = PnpConfigTranslator::translate(defaults, nullptr).routed;

    PnpConfigTranslator::PnpKeyUniverse universe;
    for (const auto& [orca_key, targets] : routed)
        for (const std::string& target : targets)
            universe.insert(target);
    REQUIRE(universe.count("wall_count") == 1); // guards the fixture against a table edit
    universe.erase("wall_count");

    const auto dead = PnpConfigTranslator::dead_curated_targets(routed, universe);
    REQUIRE(dead.size() == 1);
    REQUIRE(dead[0].orca_key == "wall_loops");
    REQUIRE(dead[0].pnp_key == "wall_count");
}

// Opt-in live check (ticket 06). Hidden by the leading `.` tag so `xmake test`
// never runs it: it needs a real `pnp_cli module config-schema` document, which
// only exists after `cargo xtask dist`, and the fork's test suite must not
// depend on a cargo step that may not have run. Point it at one and run
// explicitly:
//
//     pnp_config_translator_tests "[live-schema]" \
//         --  (with PNP_LIVE_SCHEMA=<path to the config-schema JSON>)
//
// The expectation is the drift ticket 09 owns. Repairing those rows must empty
// this list; anything else appearing here is new drift a submodule bump
// introduced, which is the whole point of the reconciliation.
TEST_CASE("the pinned backend's own schema leaves only the known dead rows",
          "[.][pnp][translator][ticket06][live-schema]")
{
    const char* path = std::getenv("PNP_LIVE_SCHEMA");
    if (path == nullptr) {
        WARN("PNP_LIVE_SCHEMA is unset; skipping");
        return;
    }

    std::ifstream in(path);
    REQUIRE(in.good());
    const json doc = json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    REQUIRE(PnpConfigTranslator::pnp_schema_reports_host_keys(doc));

    const auto dead = PnpConfigTranslator::dead_curated_targets(
        PnpConfigTranslator::pnp_key_universe_from_schema(doc));
    for (const PnpDeadTarget& d : dead)
        WARN("dead curated row: " << d.orca_key << " -> " << d.pnp_key);
    REQUIRE(dead.size() == 0);
}
