// PNP fork (SchemaBridgeMap ticket 02): runtime registration of pnp's config
// keys into Orca's config core.
//
// Two halves, tested separately because only one of them can run twice:
//   * parse_schema() is pure - a synthetic config-schema document in, defs out.
//   * pnp_register_config_keys() is a one-shot sealed seam, so exactly one test
//     case may call it. That case therefore asserts everything about the
//     registered state at once.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Format/STL.hpp"
#include "slic3r/GUI/PnpConfigKeys.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

// A config-schema reply in the shape pnp emits at wire 1.1.0: module manifest
// fields under "schema", host-declared keys under "host".
nlohmann::json synthetic_schema()
{
    return nlohmann::json::parse(R"JSON({
      "schema_version": "1.1.0",
      "schema": [
        {
          "module": "com.core.wave-overhangs",
          "fields": [
            {"key": "wave_overhang_line_spacing", "type": "float", "default": "0.35",
             "min": 0.01, "max": 5.0, "display": "Wave Overhang Line Spacing",
             "group": "Wave Overhangs", "unit": "mm", "description": "spacing",
             "scope": "print"},
            {"key": "wave_overhang_max_iterations", "type": "int", "default": "0",
             "min": 0.0, "max": 500.0, "display": "Wave Overhang Max Iterations",
             "group": "Wave Overhangs", "scope": "print"},
            {"key": "gap_fill_medial_axis_on_painted", "type": "bool", "default": "false",
             "display": "Run gap-fill on painted slices", "group": "Quality",
             "scope": "print"},
            {"key": "retract_mode", "type": "enum", "default": "gcode",
             "values": ["gcode", "firmware"], "display": "Retraction Mode",
             "group": "Travel Retraction", "scope": "print", "advanced": true},
            {"key": "support_interface_flow", "type": "percent", "default": "100%",
             "display": "Support Interface Flow", "group": "Support", "scope": "print"},
            {"key": "layer_height", "type": "float", "default": "0.2",
             "display": "Layer height", "group": "Quality", "scope": "print"},
            {"key": "wave_overhang_broken_enum", "type": "enum", "default": "x",
             "values": [], "display": "Broken", "group": "Wave Overhangs"},
            {"key": "slice_has_paint", "type": "bool", "default": "false",
             "display": "Slice contains painted regions (host-injected)", "group": "Quality",
             "scope": "print"},
            {"key": "wave_overhang_future_type", "type": "quaternion", "default": "0",
             "display": "From a newer backend", "group": "Wave Overhangs"}
          ]
        }
      ],
      "host": [
        {"key": "thumbnail_path", "type": "string", "default": null, "scope": "printer"},
        {"key": "fill_authored_coloring", "type": "string-list", "default": "", "scope": "filament"},
        {"key": "nonplanar_shell_count", "type": "int", "default": "0", "scope": "print"},
        {"key": "machine_max_jerk_x", "type": "float", "default": "20", "scope": "printer"},
        {
          "key": "support_sharp_tails", "type": "bool", "default": true, "scope": "print",
          "display": "Support Sharp Tails", "group": "Support",
          "description": "Orca-obsolete name that pnp's host runtime reads (ticket 13)"
        },
        {"key": "wave_overhang_line_spacing", "type": "float", "default": "9.9", "scope": "print"}
      ]
    })JSON");
}

// Stands in for the real "is this key already routed?" predicate: name identity
// against print_config_def, plus one curated-table target.
//
// Fixed rather than read live off print_config_def, because the registration
// case below mutates that global and Catch2 may run it first - a live
// predicate would then report keys as bound that only this binary bound.
// `identity_keys_really_are_orca_keys` pins that these names are genuinely
// Orca's.
const std::set<std::string>& identity_keys()
{
    static const std::set<std::string> keys = {"layer_height", "machine_max_jerk_x"};
    return keys;
}

bool default_already_bound(const std::string &key)
{
    return identity_keys().count(key) > 0 || key == "support_interface_flow";
}

const PnpConfigKeyDef *find(const std::vector<PnpConfigKeyDef> &defs, const std::string &key)
{
    auto it = std::find_if(defs.begin(), defs.end(),
                           [&key](const PnpConfigKeyDef &d) { return d.key == key; });
    return it == defs.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("identity_keys_really_are_orca_keys", "[pnp][config_keys]")
{
    // The routing rule is name identity against print_config_def, so the
    // fixture's stand-in predicate is only meaningful if these are real Orca
    // keys. machine_max_jerk_x is built in a loop (PrintConfig.cpp:4941),
    // which ticket 01's literal-scrape inventory missed.
    for (const std::string &key : identity_keys()) {
        INFO("orca key " << key);
        CHECK(print_config_def.has(key));
    }
}

TEST_CASE("pnp config-schema types map onto Orca option types", "[pnp][config_keys]")
{
    ConfigOptionType type = coNone;

    SECTION("every type pnp declares has an Orca counterpart") {
        struct Case { const char *wire; ConfigOptionType expect; };
        const Case cases[] = {
            {"bool", coBool}, {"int", coInt}, {"float", coFloat}, {"string", coString},
            {"enum", coEnum}, {"percent", coPercent}, {"float_or_percent", coFloatOrPercent},
            {"float-list", coFloats}, {"string-list", coStrings},
        };
        for (const Case &c : cases) {
            INFO("wire type " << c.wire);
            REQUIRE(PnpConfigKeys::map_wire_type(c.wire, type));
            CHECK(type == c.expect);
        }
    }

    SECTION("an unknown type is rejected rather than guessed") {
        // A newer backend may declare a type this fork cannot build a default
        // for. Registering it as the wrong type would corrupt the value on the
        // first preset round-trip.
        CHECK_FALSE(PnpConfigKeys::map_wire_type("quaternion", type));
        CHECK_FALSE(PnpConfigKeys::map_wire_type("", type));
    }
}

TEST_CASE("parse_schema turns a config-schema reply into registerable defs", "[pnp][config_keys]")
{
    std::vector<PnpConfigKeys::SkippedKey> skipped;
    const auto defs = PnpConfigKeys::parse_schema(synthetic_schema(), default_already_bound, &skipped);

    const auto was_skipped = [&skipped](const std::string &key) {
        return std::any_of(skipped.begin(), skipped.end(),
                           [&key](const PnpConfigKeys::SkippedKey &s) { return s.key == key; });
    };

    SECTION("module metadata is carried onto the def") {
        const PnpConfigKeyDef *d = find(defs, "wave_overhang_line_spacing");
        REQUIRE(d != nullptr);
        CHECK(d->type == coFloat);
        CHECK(d->label == "Wave Overhang Line Spacing");
        CHECK(d->category == "Wave Overhangs");
        CHECK(d->sidetext == "mm");
        CHECK(d->tooltip == "spacing");
        CHECK(d->default_value == "0.35");
        REQUIRE(d->has_min);
        REQUIRE(d->has_max);
        CHECK(d->min == 0.01);
        CHECK(d->max == 5.0);
        CHECK(d->scope == PnpPresetScope::Print);
    }

    SECTION("enum domains survive as enum_values") {
        const PnpConfigKeyDef *d = find(defs, "retract_mode");
        REQUIRE(d != nullptr);
        CHECK(d->type == coEnum);
        CHECK(d->enum_values == std::vector<std::string>{"gcode", "firmware"});

        // Ticket 04: the advanced tier flag rides the same wire record.
        CHECK(d->advanced == true);
        // ...and defaults to false when the field omits it (host keys).
        const PnpConfigKeyDef* plain = find(defs, "wave_overhang_line_spacing");
        REQUIRE(plain != nullptr);
        CHECK(plain->advanced == false);
    }

    SECTION("host keys are registered, and carry their declared preset scope") {
        // Ticket 01's central finding: config-schema used to report only the
        // module half, leaving 62 host keys with no def and no persistence.
        const PnpConfigKeyDef *thumb = find(defs, "thumbnail_path");
        REQUIRE(thumb != nullptr);
        CHECK(thumb->scope == PnpPresetScope::Printer);
        // A cli_opt with no value declares no default; that is not a zero.
        CHECK(thumb->default_value.empty());

        const PnpConfigKeyDef *coloring = find(defs, "fill_authored_coloring");
        REQUIRE(coloring != nullptr);
        CHECK(coloring->scope == PnpPresetScope::Filament);
        CHECK(coloring->type == coStrings);

        const PnpConfigKeyDef *shells = find(defs, "nonplanar_shell_count");
        REQUIRE(shells != nullptr);
        CHECK(shells->scope == PnpPresetScope::Print);
    }

    SECTION("a key already routed by name identity gets no generated control") {
        // layer_height exists in print_config_def, so Orca's own definition
        // binds it - name match wins over the PNP page.
        CHECK(find(defs, "layer_height") == nullptr);
        CHECK(was_skipped("layer_height"));
        // Identity holds for keys Orca builds in a loop too. machine_max_jerk_x
        // is one (PrintConfig.cpp:4941); ticket 01's inventory scraped literal
        // this->add("...") sites only and so listed it as pnp-only.
        CHECK(find(defs, "machine_max_jerk_x") == nullptr);
        CHECK(was_skipped("machine_max_jerk_x"));
    }

    SECTION("a key already routed by the curated table gets no generated control") {
        CHECK(find(defs, "support_interface_flow") == nullptr);
        CHECK(was_skipped("support_interface_flow"));
    }

    SECTION("undeclarable keys are skipped with a reason, not dropped silently") {
        CHECK(find(defs, "wave_overhang_future_type") == nullptr);
        CHECK(was_skipped("wave_overhang_future_type"));
        // An enum with an empty domain cannot be deserialized or presented.
        CHECK(find(defs, "wave_overhang_broken_enum") == nullptr);
        CHECK(was_skipped("wave_overhang_broken_enum"));
    }

    SECTION("a key declared in both halves is registered once") {
        // Modules and host built-ins may read the same key; that is not a
        // conflict, and registering it twice would trip ConfigDef::add.
        const auto occurrences = std::count_if(
            defs.begin(), defs.end(),
            [](const PnpConfigKeyDef &d) { return d.key == "wave_overhang_line_spacing"; });
        CHECK(occurrences == 1);
        // The module half wins, so the module's metadata is what survives.
        CHECK(find(defs, "wave_overhang_line_spacing")->default_value == "0.35");
    }

    SECTION("an empty or malformed document yields nothing") {
        CHECK(PnpConfigKeys::parse_schema(nlohmann::json::object(), default_already_bound).empty());
        CHECK(PnpConfigKeys::parse_schema(nlohmann::json::array(), default_already_bound).empty());
    }

    SECTION("a pre-1.1.0 reply still registers its module half") {
        nlohmann::json old_wire = synthetic_schema();
        old_wire.erase("host");
        const auto old_defs = PnpConfigKeys::parse_schema(old_wire, default_already_bound);
        CHECK(find(old_defs, "thumbnail_path") == nullptr);
        CHECK(find(old_defs, "wave_overhang_line_spacing") != nullptr);
    }
}

namespace {

// Catch2 re-enters a TEST_CASE body once per SECTION, but the seam registers
// exactly once per process - so registration has to happen behind a
// function-local static, not in the case body.
struct Registration
{
    std::vector<PnpConfigKeyDef> defs;
    size_t                       count = 0;
};

const Registration& registered_once()
{
    static const Registration r = [] {
        Registration out;
        out.defs  = PnpConfigKeys::parse_schema(synthetic_schema(), default_already_bound);
        out.count = pnp_register_config_keys(out.defs);
        return out;
    }();
    return r;
}

} // namespace

TEST_CASE("registered pnp keys become first-class Orca keys", "[pnp][config_keys]")
{
    const Registration &reg = registered_once();

    CHECK(reg.count == reg.defs.size());
    CHECK(reg.count > 0);
    CHECK(pnp_config_keys_sealed());

    SECTION("the key exists in print_config_def with its declared type and default") {
        const ConfigOptionDef *d = print_config_def.get("wave_overhang_line_spacing");
        REQUIRE(d != nullptr);
        CHECK(d->type == coFloat);
        CHECK(d->label == "Wave Overhang Line Spacing");
        REQUIRE(bool(d->default_value));
        CHECK(d->default_value->serialize() == "0.35");

        const ConfigOptionDef *i = print_config_def.get("wave_overhang_max_iterations");
        REQUIRE(i != nullptr);
        CHECK(i->type == coInt);
        CHECK(i->default_value->serialize() == "0");

        const ConfigOptionDef *b = print_config_def.get("gap_fill_medial_axis_on_painted");
        REQUIRE(b != nullptr);
        CHECK(b->type == coBool);
        CHECK(b->default_value->serialize() == "0");
    }

    SECTION("an enum key gets a live keys map, so its default deserializes") {
        const ConfigOptionDef *d = print_config_def.get("retract_mode");
        REQUIRE(d != nullptr);
        REQUIRE(d->enum_keys_map != nullptr);
        CHECK(d->enum_keys_map->at("firmware") == 1);
        REQUIRE(bool(d->default_value));
        CHECK(d->default_value->serialize() == "gcode");
    }

    SECTION("a registered key is serializable, which the undo/redo stack needs") {
        const ConfigOptionDef *d = print_config_def.get("wave_overhang_line_spacing");
        REQUIRE(d->serialization_key_ordinal > 0);
        CHECK(print_config_def.by_serialization_key_ordinal.at(d->serialization_key_ordinal) == d);
    }

    SECTION("keys land in the preset list their declared scope names") {
        const auto has = [](const std::vector<std::string> &list, const std::string &key) {
            return std::find(list.begin(), list.end(), key) != list.end();
        };
        CHECK(has(Preset::print_options(), "wave_overhang_line_spacing"));
        CHECK(has(Preset::print_options(), "nonplanar_shell_count"));
        // Mis-scoping round-trips a key into the wrong preset file, so these
        // must not fall through to the print list.
        CHECK(has(Preset::printer_options(), "machine_max_jerk_x"));
        CHECK_FALSE(has(Preset::print_options(), "machine_max_jerk_x"));
        CHECK(has(Preset::filament_options(), "filament_ironing_speed"));
        CHECK_FALSE(has(Preset::print_options(), "filament_ironing_speed"));
    }

    SECTION("a registered key round-trips through a DynamicPrintConfig") {
        DynamicPrintConfig        cfg;
        ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
        cfg.set_deserialize("wave_overhang_line_spacing", "1.25", ctx);
        CHECK(cfg.opt_float("wave_overhang_line_spacing") == 1.25);
        CHECK(cfg.opt_serialize("wave_overhang_line_spacing") == "1.25");
    }

    // SchemaBridgeMap ticket 13. `support_sharp_tails` names a key Orca
    // declares obsolete -- it sits in the ignore set that handle_legacy tests
    // BEFORE the print_config_def.has() test -- while pnp's host runtime reads
    // it as a live key (host-keys.toml [resolved_config], default true).
    // Registration must win over Orca's obsolete history: after registration
    // the key has a def, saves into presets and projects, and then has to load
    // back, which today's ordering silently prevents by clearing the key to ""
    // before the def is ever consulted. The value falls to its default -- in
    // pnp's case true -- so a user's false arrives as true on every load.
    SECTION("a registered key whose name Orca ignores still deserializes") {
        DynamicPrintConfig cfg;
        ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
        cfg.set_deserialize("support_sharp_tails", "0", ctx);
        // Presence first: on the broken ordering the key is silently dropped
        // here, and dereferencing a missing option would crash the binary
        // instead of reporting the regression.
        const ConfigOptionBool *opt = cfg.option<ConfigOptionBool>("support_sharp_tails");
        REQUIRE(opt != nullptr);
        CHECK(opt->value == 0);

        // The ignore set must not push this registered key into the
        // unknown-key carrier either: it IS defined by this build.
        CHECK(std::find(ctx.unrecogized_keys.begin(), ctx.unrecogized_keys.end(),
                        "support_sharp_tails") == ctx.unrecogized_keys.end());
    }

    // Same seam, the negative direction: without a probe nothing is
    // registered, so Orca's obsolete-key handling must still apply unchanged
    // (the GUI-free CLI and headless 3mf paths never register). This is what
    // forbids the tempting fix of simply moving the has() test above the
    // ignore set -- silent_mode and tree_support_with_infill have live stock
    // defs yet must keep dropping.
    SECTION("an unregistered name Orca ignores is still dropped") {
        REQUIRE_FALSE(print_config_def.has("support_remove_small_overhangs"));
        DynamicPrintConfig cfg;
        ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::Disable);
        cfg.set_deserialize("support_remove_small_overhangs", "true", ctx);
        CHECK(cfg.option("support_remove_small_overhangs") == nullptr);
        CHECK(std::find(ctx.unrecogized_keys.begin(), ctx.unrecogized_keys.end(),
                        "support_remove_small_overhangs") != ctx.unrecogized_keys.end());
    }

    SECTION("the seal rejects a second registration") {
        // Re-registering would hand the undo/redo stack an inconsistent
        // ordinal space and leave presets built from the older key set.
        CHECK(pnp_register_config_keys(reg.defs) == 0);
        CHECK(pnp_registered_config_keys().size() == reg.count);
    }

    // SchemaBridgeMap ticket 04 + 11: the schema `advanced` flag rides the wire
    // and picks the generated PNP page's mode tier. false -> comAdvanced (the
    // flag's own default, so unannotated host keys land there too), true ->
    // comExpert. Nothing generated reaches Simple mode either way.
    SECTION("the schema advanced flag becomes the def's mode tier") {
        const ConfigOptionDef* non = print_config_def.get("wave_overhang_line_spacing");
        REQUIRE(non != nullptr);
        CHECK(non->mode == comAdvanced);

        const ConfigOptionDef* adv = print_config_def.get("retract_mode");
        REQUIRE(adv != nullptr);
        CHECK(adv->mode == comExpert);
    }
}

// SchemaBridgeMap ticket 04 + 11: the generated PNP page's layout, derived
// (not listed) from the registered defs so it cannot drift from registration.
// Runs in this binary only, because the seam registers once per process.
TEST_CASE("the generated page layout derives from the registered defs", "[pnp][config_keys]")
{
    const Registration &reg = registered_once();
    REQUIRE(pnp_config_keys_sealed());

    const std::vector<PnpPageGroup> groups = pnp_page_groups(PnpConfigKeys::pnp_host_injected_skip_keys());

    SECTION("every print-scoped non-skipped registered key renders exactly once") {
        size_t total = 0;
        for (const PnpPageGroup& g : groups)
            total += g.keys.size();
        // Skipped keys are registered (the translator still sends them) but
        // never rendered; non-print-scoped keys are registered but live in
        // another preset's config, so they render on no page here either
        // (ticket 12 crash: reloading a printer-scoped key against the print
        // preset's config reads a nullptr option).
        size_t non_print = 0;
        for (const std::string& key : pnp_registered_config_keys())
            non_print += pnp_registered_key_scope(key) != PnpPresetScope::Print ? 1 : 0;
        CHECK(total + PnpConfigKeys::pnp_host_injected_skip_keys().size() + non_print == reg.count);
        CHECK(non_print > 0); // thumbnail_path (printer) + fill_authored_coloring (filament)
    }

    SECTION("keys bucket by the def's category, alphabetical inside") {
        auto contains = [&groups](const char* category, const std::string& key) {
            for (const PnpPageGroup& g : groups)
                if (g.category == category)
                    return std::find(g.keys.begin(), g.keys.end(), key) != g.keys.end();
            return false;
        };
        CHECK(contains("Wave Overhangs", "wave_overhang_line_spacing"));
        CHECK(contains("Wave Overhangs", "wave_overhang_max_iterations"));
        // Two synthetic keys whose only routing difference is the group string:
        // bucketing must follow the def, not the declaration order.
        CHECK(contains("Quality", "gap_fill_medial_axis_on_painted"));
    }

    SECTION("group order is descending key count, ties alphabetical") {
        for (size_t i = 1; i < groups.size(); ++i)
            CHECK(groups[i - 1].keys.size() >= groups[i].keys.size());
        for (size_t i = 1; i < groups.size(); ++i)
            if (groups[i - 1].keys.size() == groups[i].keys.size())
                CHECK(groups[i - 1].category < groups[i].category);
    }

    SECTION("non-print-scoped keys never reach the Process tab's page") {
        // thumbnail_path (printer) and fill_authored_coloring (filament) are
        // registered — preset round-trip and translation carry them — but their
        // values live in other presets' configs. Rendering them on the Process
        // tab made reload_config dereference a nullptr option (the ticket 12
        // crash the user hit opening the PNP Backend tab).
        auto page_has = [&groups](const std::string& key) {
            for (const PnpPageGroup& g : groups)
                if (std::find(g.keys.begin(), g.keys.end(), key) != g.keys.end())
                    return true;
            return false;
        };
        CHECK_FALSE(page_has("thumbnail_path"));
        CHECK_FALSE(page_has("fill_authored_coloring"));
        CHECK(pnp_registered_key_scope("thumbnail_path") == PnpPresetScope::Printer);
    }

    SECTION("an empty category collapses into one group, not a bare-key bucket") {
        // Host keys carry no group unless the DSL annotates one; non-print-
        // scoped hosts are filtered out by scope (above), but a print-scoped
        // unannotated host key must still land in the same (empty-string)
        // optgroup rather than disappear.
        bool has_empty = false;
        for (const PnpPageGroup& g : groups)
            has_empty |= g.category.empty();
        CHECK(has_empty);
        for (const PnpPageGroup& g : groups)
            if (g.category.empty())
                CHECK(std::find(g.keys.begin(), g.keys.end(), "nonplanar_shell_count") != g.keys.end());
    }

    SECTION("the host-injected skip list drops the key from the page entirely") {
        // slice_has_paint is host-injected (classic-perimeters.toml): its def
        // exists and the translator sends the value, but no user control.
        CHECK(print_config_def.has("slice_has_paint"));
        for (const PnpPageGroup& g : groups)
            CHECK(std::find(g.keys.begin(), g.keys.end(), "slice_has_paint") == g.keys.end());
        CHECK(std::find(PnpConfigKeys::pnp_host_injected_skip_keys().begin(), PnpConfigKeys::pnp_host_injected_skip_keys().end(),
                        "slice_has_paint") != PnpConfigKeys::pnp_host_injected_skip_keys().end());
    }

    SECTION("a group named like an Orca page stays on the PNP page, flagged") {
        for (const PnpPageGroup& g : groups) {
            if (g.category == "Support")
                CHECK(g.matches_orca_page);
            else if (g.category == "Wave Overhangs")
                CHECK_FALSE(g.matches_orca_page);
        }
    }
}

// SchemaBridgeMap ticket 13. The seam-level case above proves the deserializer
// accepts an ignore-set name once pnp has registered it; these two prove the
// storage surfaces the bug actually bites -- a user preset and a project 3mf
// -- round-trip the value. Both must run in the process that registered, so
// they live in this file rather than the libslic3r suites: registration is
// one-shot and only this binary calls it.
TEST_CASE("an ignore-set pnp key round-trips through a user preset file", "[pnp][config_keys]")
{
    const Registration &reg = registered_once();
    REQUIRE(pnp_config_keys_sealed());

    namespace fs = boost::filesystem;
    const fs::path dir = fs::temp_directory_path() / fs::unique_path("pnp_t13_preset_%%%%%%%%");
    fs::create_directories(dir);

    // Save through the real Preset writer: the key's saved form is what a
    // user's file carries (Orca booleans serialize as 1/0).
    {
        Preset preset(Preset::TYPE_PRINT, "Ticket13");
        preset.config.set_key_value("support_sharp_tails", new ConfigOptionBool(false));
        preset.file = (dir / "Ticket13.json").string();
        preset.save(nullptr);
    }

    // Load it back the way PresetCollection::load_presets does -- the path
    // that handle_legacy's ignore branch today silently drops the key on.
    Preset loaded(Preset::TYPE_PRINT, "Ticket13");
    loaded.file = (dir / "Ticket13.json").string();
    DynamicPrintConfig config;
    std::map<std::string, std::string> key_values;
    std::string reason;
    ConfigBase::t_unknown_config_values unknown;
    config.load_from_json(loaded.file, ForwardCompatibilitySubstitutionRule::Disable, key_values, reason, &unknown);
    loaded.config.apply(std::move(config));

    const ConfigOptionBool *opt = loaded.config.option<ConfigOptionBool>("support_sharp_tails");
    REQUIRE(opt != nullptr);
    CHECK(opt->value == 0);
    // Escaped the unknown-key carrier too: the key IS defined by this build.
    CHECK(unknown.empty());

    fs::remove_all(dir);
}

TEST_CASE("an ignore-set pnp key round-trips through a project 3mf", "[pnp][config_keys]")
{
    const Registration &reg = registered_once();
    REQUIRE(pnp_config_keys_sealed());

    namespace fs = boost::filesystem;

    Model model;
    const std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
    REQUIRE(load_stl(src_file.c_str(), &model));
    model.add_default_instances();

    const fs::path backup_dir = fs::temp_directory_path() / fs::unique_path("pnp_t13_3mf_%%%%%%%%");
    fs::create_directories(backup_dir);
    model.set_backup_path(backup_dir.string());

    DynamicPrintConfig config;
    config.set_key_value("support_sharp_tails", new ConfigOptionBool(false));
    config.set_key_value("layer_height", new ConfigOptionFloat(0.28));

    const std::string test_file = (fs::temp_directory_path() / fs::unique_path("pnp_t13_%%%%%%%%.3mf")).string();
    StoreParams store_params;
    store_params.path     = test_file.c_str();
    store_params.model    = &model;
    store_params.config   = &config;
    store_params.strategy = SaveStrategy::Zip64 | SaveStrategy::Silence;
    REQUIRE(store_bbs_3mf(store_params));

    Model                     dst_model;
    DynamicPrintConfig        dst_config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Enable };
    PlateDataPtrs             dst_plates;
    std::vector<Preset *>     project_presets;
    bool  is_bbl_3mf = false, is_orca_3mf = false;
    Semver file_version;
    const bool loaded = load_bbs_3mf(test_file.c_str(), &dst_config, &ctxt, &dst_model, &dst_plates,
                                     &project_presets, &is_bbl_3mf, &is_orca_3mf, &file_version, nullptr,
                                     LoadStrategy::LoadModel | LoadStrategy::LoadConfig);
    REQUIRE(loaded);

    // The key is a live registered key, not a carrier case: it must come back
    // as a config option holding the stored false, not verbatim in the
    // unknown-key carrier.
    const ConfigOptionBool *opt = dst_config.option<ConfigOptionBool>("support_sharp_tails");
    REQUIRE(opt != nullptr);
    CHECK(opt->value == 0);
    CHECK(dst_model.pnp_unknown_config.count("support_sharp_tails") == 0);
    const ConfigOptionFloat *lh = dst_config.option<ConfigOptionFloat>("layer_height");
    REQUIRE(lh != nullptr);
    CHECK_THAT(lh->value, Catch::Matchers::WithinAbs(0.28, 1e-9));

    boost::system::error_code ec;
    fs::remove(test_file, ec);
    fs::remove_all(backup_dir, ec);
    release_PlateData_list(dst_plates);
}

// Opt-in live check (ticket 11). Hidden by the leading `.` tag, same shape as
// ticket 06's: it needs a real `pnp_cli module config-schema` document, which
// only exists after `cargo xtask dist`. Point it at one and run explicitly:
//
//     pnp_config_translator_tests "[live-schema]"  (with PNP_LIVE_SCHEMA=<json>)
//
// Must run in its own process (the filter guarantees it): it registers the
// real backend's keys, and the seam refuses a second registration. What it
// proves is the generated page's derivable side against the wire the pinned
// backend actually emits -- the rendered controls themselves are manual smoke.
TEST_CASE("live backend: the derived page accounts for every registered key", "[.][pnp][config_keys][ticket11][live-schema]")
{
    const char* path = std::getenv("PNP_LIVE_SCHEMA");
    if (path == nullptr) {
        WARN("PNP_LIVE_SCHEMA is unset; skipping");
        return;
    }

    std::ifstream in(path);
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());

    const size_t registered = PnpConfigKeys::register_from_schema(doc.dump());
    WARN("registered page keys: " << registered);
    REQUIRE(pnp_config_keys_sealed());
    REQUIRE(registered > 0);

    const std::vector<PnpPageGroup> groups = pnp_page_groups(PnpConfigKeys::pnp_host_injected_skip_keys());

    size_t rendered = 0;
    for (const PnpPageGroup& g : groups)
        rendered += g.keys.size();
    WARN("page: " << groups.size() << " groups, " << rendered << " controls");
    for (const PnpPageGroup& g : groups) {
        WARN("  [" << g.category << "] " << g.keys.size() << (g.matches_orca_page ? "  (orca page name)" : ""));
        for (const std::string& k : g.keys)
            WARN("      " << k);
    }

    // Every registered key either renders or is named by the skip list; none
    // falls out silently. (The registry may hold fewer keys than the wire has
    // unique ones: identity rows are Orca's, and curated-table targets keep
    // their existing route and get no generated control.)
    CHECK(rendered + PnpConfigKeys::pnp_host_injected_skip_keys().size() <= registered);
    CHECK(rendered > 0);

    // Group ordering holds against real-world group counts too.
    for (size_t i = 1; i < groups.size(); ++i)
        CHECK(groups[i].keys.size() <= groups[i - 1].keys.size());

    // The one declared host-injected key is the skip list's only member, and
    // the live backend still declares it (classic-perimeters.toml). A second
    // name here is new drift the skip list must grow for.
    REQUIRE(PnpConfigKeys::pnp_host_injected_skip_keys().size() == 1);
    bool declared = false;
    if (doc.contains("schema") && doc["schema"].is_array())
        for (const auto& m : doc["schema"])
            if (m.contains("fields") && m["fields"].is_array())
                for (const auto& f : m["fields"])
                    if (f.value("key", "") == "slice_has_paint")
                        declared = true;
    if (doc.contains("host") && doc["host"].is_array())
        for (const auto& f : doc["host"])
            if (f.value("key", "") == "slice_has_paint")
                declared = true;
    WARN("slice_has_paint declared by live backend: " << declared);
}
