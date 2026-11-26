#include <catch2/catch_all.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_helpers.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// Ported with d7534d7fb9 from tests/fff_print/test_wipe_tower.cpp, which this base does not have.
// The suite keeps the fixture local: only this file validates a tower against two nozzle areas.
static DynamicPrintConfig tower_estimate_config(const char *wall_type, unsigned int filaments = 2)
{
    // 100 mm3 per purge on a 50 mm wide tower: one purge is 100/(layer_height * 50) of depth.
    return multifilament_config(filaments, {
        { "outer_wall_filament_id",         filaments == 2 ? "2" : "1" },
        { "enable_prime_tower",             "1"       },
        { "wipe_tower_wall_type",           wall_type },
        { "prime_tower_width",              "50"      },
        { "prime_volume",                   "100"     },
        { "prime_tower_infill_gap",         "100%"    },
        { "prime_tower_brim_width",         "3"       },
        { "purge_in_prime_tower",           "0"       },
        { "single_extruder_multi_material", "0"       },
        { "timelapse_type",                 "0"       },
        { "layer_height",                   "0.2"     },
        { "enable_wrapping_detection",      "0"       },
        { "raft_layers",                    "0"       } });
}

// One flush block per nozzle, as the GUI sizes them when the nozzle count changes; a single block
// leaves each additional nozzle's slice short.
static void size_flush_to_nozzles(DynamicPrintConfig &config)
{
    const size_t nozzles = config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
    auto        &flush   = config.option<ConfigOptionFloats>("flush_volumes_matrix", true)->values;
    const std::vector<double> block = flush;
    for (size_t i = 1; i < nozzles; ++i)
        flush.insert(flush.end(), block.begin(), block.end());
    for (const char *key : { "flush_multiplier", "flush_multiplier_fast" })
        config.option<ConfigOptionFloats>(key, true)->resize(nozzles);
}

// Two nozzles over a 200 mm bed, nozzle 1 reaching x up to `left_max` and nozzle 2 x from `right_min`,
// with a 20 mm cube at the bed's left and, when `filaments` is 2, a prime tower at (30, 150).
static StringObjectException validate_on_two_nozzle_areas(double left_max, double right_min, unsigned int filaments)
{
    DynamicPrintConfig config = tower_estimate_config("rectangle", filaments);
    config.set_num_extruders(2);
    config.set_deserialize_strict({
        { "nozzle_diameter",           "0.4,0.4" },
        { "printer_extruder_id",       "1,2" },
        { "printer_extruder_variant",  "Direct Drive Standard,Direct Drive Standard" },
        { "printable_area",            "0x0,200x0,200x200,0x200" },
        { "extruder_printable_height", "0,0" },
        { "filament_map_mode",         "Manual" },
        { "filament_map",              filaments == 2 ? "1,2" : "1" },
        { "wipe_tower_x",              "30" },
        { "wipe_tower_y",              "150" },
        { "layer_change_gcode",        "G92 E0\n" } }); // validate() relative-E reset
    size_flush_to_nozzles(config);
    config.set_key_value("extruder_printable_area", new ConfigOptionPointsGroups{
        { { 0., 0. }, { left_max, 0. }, { left_max, 200. }, { 0., 200. } },
        { { right_min, 0. }, { 200., 0. }, { 200., 200. }, { right_min, 200. } } });

    TriangleMesh mesh = cube(20);
    mesh.translate(20.f, 20.f, 0.f);
    Print print;
    Model model;
    init_print({ std::move(mesh) }, print, model, config, nullptr, false);
    print.apply(model, config);
    REQUIRE(print.has_wipe_tower() == (filaments == 2));
    return print.validate();
}

} // namespace

TEST_CASE("A prime tower is rejected when the extruders share no printable area", "[Print]")
{
    SECTION("areas that overlap under the tower accept it") {
        CHECK_THAT(validate_on_two_nozzle_areas(150., 10., 2).string, Catch::Matchers::Equals(std::string()));
    }
    SECTION("a tower outside the shared area is told so") {
        CHECK_THAT(validate_on_two_nozzle_areas(150., 120., 2).string, Catch::Matchers::ContainsSubstring("partially outside"));
    }
    SECTION("areas that only touch leave the tower nowhere to go") {
        const std::string error = validate_on_two_nozzle_areas(100., 100., 2).string;
        CHECK_THAT(error, Catch::Matchers::ContainsSubstring("share no common area"));
        CHECK_THAT(error, !Catch::Matchers::ContainsSubstring("partially outside"));
    }
    SECTION("one filament inside its nozzle's area needs no tower and is accepted") {
        CHECK_THAT(validate_on_two_nozzle_areas(100., 100., 1).string, Catch::Matchers::Equals(std::string()));
    }
}
