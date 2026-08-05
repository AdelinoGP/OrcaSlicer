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
