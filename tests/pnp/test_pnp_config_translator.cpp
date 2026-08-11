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

TEST_CASE("pnp_key_is_unimplemented matches the Tier-D warning set", "[pnp][translator]")
{
    // Tier-A identity keys are implemented.
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("layer_height"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("wall_loops"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("nozzle_diameter"));
    // Tier-B transform rows are implemented (possibly lossy).
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("seam_position"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("sparse_infill_density"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("fuzzy_skin"));
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("raft_layers"));
    // Tier-D keys are not.
    REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("gcode_flavor"));
    REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("gcode_comments"));
    REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("independent_support_layer_height"));
    // Unknown keys are not implemented either.
    REQUIRE(PnpConfigTranslator::pnp_key_is_unimplemented("no_such_key"));
}

TEST_CASE("pnp_key_is_unimplemented agrees with translate()'s warnings", "[pnp][translator]")
{
    // Every key translate() warns about as Tier D (not-yet-mapped or
    // unsupported-feature) must read as unimplemented, and every key it does
    // not warn about that way must read as implemented. Lossy-fallback
    // warnings are excluded: those keys ARE sent (with a substituted value).
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
    REQUIRE_FALSE(PnpConfigTranslator::pnp_key_is_unimplemented("support_type"));
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
