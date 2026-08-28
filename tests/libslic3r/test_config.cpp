#include <catch2/catch_all.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintConfigConstants.hpp"
#include "libslic3r/LocalesUtils.hpp"

#include <algorithm>
#include <memory>

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>

#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

SCENARIO("Generic config validation performs as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN( "outer_wall_line_width is set to 250%, a valid value") {
            config.set_deserialize_strict("outer_wall_line_width", "250%");
            THEN( "The config is read as valid.") {
                REQUIRE(config.validate().empty());
            }
        }
        WHEN( "outer_wall_line_width is set to -10, an invalid value") {
            config.set("outer_wall_line_width", -10);
            THEN( "Validate returns error") {
                REQUIRE_FALSE(config.validate().empty());
            }
        }

        WHEN( "wall_loops is set to -10, an invalid value") {
            config.set("wall_loops", -10);
            THEN( "Validate returns error") {
                REQUIRE_FALSE(config.validate().empty());
            }
        }
    }
}

SCENARIO("Config accessor functions perform as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN("A boolean option is set to a boolean value") {
            REQUIRE_NOTHROW(config.set("gcode_comments", true));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing a 0 or 1") {
            CHECK_NOTHROW(config.set_deserialize_strict("gcode_comments", "1"));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing something other than 0 or 1") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", "Z"), BadOptionTypeException);
            }
            AND_THEN("Value is unchanged.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == false);
            }
        }
        WHEN("A boolean option is set to an int value") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", 1), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set from serialized string") {
            config.set_deserialize_strict("raft_layers", "20");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionInt>("raft_layers")->getInt() == 20);
            }
        }
	WHEN("An integer-based option is set through the integer interface") {
	    config.set("raft_layers", 100);
	    THEN("The underlying value is set correctly.") {
		REQUIRE(config.opt<ConfigOptionInt>("raft_layers")->getInt() == 100);
	    }
        }
        WHEN("An floating-point option is set through the integer interface") {
            config.set("max_bridge_length", 10);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat() == 10.0);
            }
        }
        WHEN("A floating-point option is set through the double interface") {
            config.set("max_bridge_length", 5.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat() == 5.5);
            }
        }
        WHEN("An integer-based option is set through the double interface") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("top_shell_layers", 5.5), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set to a non-numeric value.") {
	    auto prev_value = config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat();
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set_deserialize_strict("max_bridge_length", "zzzz"), BadOptionValueException);
            }
            THEN("The value does not change.") {
                REQUIRE(config.opt<ConfigOptionFloat>("max_bridge_length")->getFloat() == prev_value);
            }
        }
        WHEN("A string option is set through the string interface") {
            config.set("machine_end_gcode", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the integer interface") {
            config.set("machine_end_gcode", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the double interface") {
            config.set("machine_end_gcode", 100.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == float_to_string_decimal_point(100.5));
            }
        }
        WHEN("A float or percent is set as a percent through the string interface.") {
            config.set_deserialize_strict("initial_layer_line_width", "100%");
            THEN("Value and percent flag are 100/true") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == true);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the string interface.") {
            config.set_deserialize_strict("initial_layer_line_width", "100");
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the int interface.") {
            config.set("initial_layer_line_width", 100);
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the double interface.") {
            config.set("initial_layer_line_width", 100.5);
            THEN("Value and percent flag are 100.5/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("initial_layer_line_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100.5);
            }
        }
        WHEN("A numeric vector is set from serialized string") {
	    config.set_deserialize_strict("temperature_vitrification", "10,20");
            THEN("The underlying value is set correctly.") {
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(0) == 10);
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(1) == 20);
            }
        }
	// FIXME: Design better accessors for vector elements
	// The following isn't supported and probably shouldn't be:
	// WHEN("An integer-based vector option is set through the integer interface") {
	//     config.set("temperature_vitrification", 100);
	//     THEN("The underlying value is set correctly.") {
	// 	REQUIRE(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(0) == 100);
	//     }
        // }
	WHEN("An integer-based vector option is set through the set_key_value interface") {
	    config.set_key_value("temperature_vitrification", new ConfigOptionInts{10,20});
	    THEN("The underlying value is set correctly.") {
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(0) == 10);
                CHECK(config.opt<ConfigOptionInts>("temperature_vitrification")->get_at(1) == 20);
	    }
        }
        WHEN("An invalid option is requested during set.") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1.0), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", "1"), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", true), UnknownOptionException);
            }
        }

        WHEN("An invalid option is requested during get.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }
        WHEN("An invalid option is requested during opt.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }

        WHEN("getX called on an unset option.") {
            THEN("The default is returned.") {
                REQUIRE(config.opt_float("layer_height") == INITIAL_LAYER_HEIGHT);
                REQUIRE(config.opt_int("raft_layers") == INITIAL_RAFT_LAYERS);
                REQUIRE(config.opt_bool("reduce_crossing_wall") == INITIAL_REDUCE_CROSSING_WALL);
            }
        }

        WHEN("opt_float called on an option that has been set.") {
            config.set("layer_height", INITIAL_LAYER_HEIGHT*2);
            THEN("The set value is returned.") {
                REQUIRE(config.opt_float("layer_height") == INITIAL_LAYER_HEIGHT*2);
            }
        }
    }
}

SCENARIO("Config ini load/save interface", "[Config]") {
    WHEN("new_from_ini is called") {
		Slic3r::DynamicPrintConfig config;
		std::string path = std::string(TEST_DATA_DIR) + "/test_config/new_from_ini.ini";
		config.load_from_ini(path, ForwardCompatibilitySubstitutionRule::Disable);
        THEN("Config object contains ini file options.") {
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.size() == 1);
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.front() == "#ABCD");
        }
    }
}

// TODO: https://github.com/SoftFever/OrcaSlicer/issues/11269 - Is this test still relevant? Delete if not.
// It was failing so at least "nozzle_type" and "extruder_printable_area" could not be serialized
// and an exception was thrown, but "nozzle_type" has been around for at least 3 months now.
// So maybe this test and the serialization logic in Config.?pp should be deleted if it doesn't get used.
SCENARIO("DynamicPrintConfig serialization", "[Config]") {
    WHEN("DynamicPrintConfig is serialized and deserialized") {
        FullPrintConfig full_print_config;
        DynamicPrintConfig cfg;
        cfg.apply(full_print_config, false);

        std::string serialized;
        // try {
            std::ostringstream ss;
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(cfg);
            serialized = ss.str();
        // } catch (const std::runtime_error & /* e */) {
        //     // e.what();
        // }
	CAPTURE(serialized.length());

        THEN("Config object contains ini file options.") {
            DynamicPrintConfig cfg2;
            // try {
                std::stringstream ss(serialized);
                cereal::BinaryInputArchive iarchive(ss);
                iarchive(cfg2);
            // } catch (const std::runtime_error & /* e */) {
            //     // e.what();
            // }
	    CAPTURE(cfg.diff_report(cfg2));
            REQUIRE(cfg == cfg2);
        }
    }
}

SCENARIO("update_non_diff_values_to_base_config preserves child vectors when child has more extruders than parent",
         "[Config][Variant]") {
    GIVEN("A 2-extruder child printer config inheriting from a 1-extruder parent") {
        Slic3r::DynamicPrintConfig child;
        Slic3r::DynamicPrintConfig parent;

        child.set_key_value("nozzle_diameter",           new Slic3r::ConfigOptionFloats({0.4, 0.4}));
        child.set_key_value("printer_extruder_id",       new Slic3r::ConfigOptionInts({1, 2}));
        child.set_key_value("printer_extruder_variant",  new Slic3r::ConfigOptionStrings({"Direct Drive Standard", "Direct Drive Standard"}));
        child.set_key_value("retraction_length",         new Slic3r::ConfigOptionFloats({1.5, 1.5}));

        parent.set_key_value("nozzle_diameter",          new Slic3r::ConfigOptionFloats({0.4}));
        parent.set_key_value("printer_extruder_id",      new Slic3r::ConfigOptionInts({1}));
        parent.set_key_value("printer_extruder_variant", new Slic3r::ConfigOptionStrings({"Direct Drive Standard"}));
        parent.set_key_value("retraction_length",        new Slic3r::ConfigOptionFloats({0.8}));

        const Slic3r::t_config_option_keys keys = {
            "retraction_length", "printer_extruder_id", "printer_extruder_variant"
        };
        const std::set<std::string> different_keys = {
            "retraction_length", "printer_extruder_id", "printer_extruder_variant"
        };

        WHEN("update_non_diff_values_to_base_config is called") {
            std::string id_name  = "printer_extruder_id";
            std::string var_name = "printer_extruder_variant";
            child.update_non_diff_values_to_base_config(
                parent, keys, different_keys, id_name, var_name,
                Slic3r::printer_options_with_variant_1,
                Slic3r::printer_options_with_variant_2);

            THEN("printer_extruder_id retains size 2") {
                REQUIRE(child.option<Slic3r::ConfigOptionInts>("printer_extruder_id")->values.size() == 2);
            }
            THEN("printer_extruder_variant retains size 2") {
                REQUIRE(child.option<Slic3r::ConfigOptionStrings>("printer_extruder_variant")->values.size() == 2);
            }
            THEN("retraction_length retains size 2") {
                REQUIRE(child.option<Slic3r::ConfigOptionFloats>("retraction_length")->values.size() == 2);
            }
            THEN("printer_extruder_id values are preserved for both extruders") {
                auto* pe_id = child.option<Slic3r::ConfigOptionInts>("printer_extruder_id");
                REQUIRE(pe_id->values.size() == 2);
                REQUIRE(pe_id->values[0] == 1);
                REQUIRE(pe_id->values[1] == 2);
            }
        }
    }
}

SCENARIO("update_diff_values_to_child_config tolerates legacy machine-limit vector sizes",
         "[Config][Variant]") {
    // Regression: loading a user printer preset that inherits a non-BBL multi-extruder base and
    // overrides stride-2 machine limits used to throw in ConfigOptionVector::set_only_diff
    // ("invalid diff_index size"). The base's machine-limit vectors get length-extended by the
    // nozzle count while it carries no printer_extruder_variant, so the base length (nozzles*2)
    // no longer matches variant_index.size()*2. The throw was caught upstream and DELETED the
    // user's preset file. The merge must instead degrade gracefully.
    GIVEN("A 4-nozzle parent with stride-2 limits extended to nozzles*2 but no printer_extruder_variant") {
        Slic3r::DynamicPrintConfig parent;
        Slic3r::DynamicPrintConfig child;

        parent.set_key_value("nozzle_diameter",
            new Slic3r::ConfigOptionFloats({0.4, 0.4, 0.4, 0.4}));
        parent.set_key_value("machine_max_acceleration_x",
            new Slic3r::ConfigOptionFloats({25000, 25000, 25000, 25000, 25000, 25000, 25000, 25000}));

        // Child user preset declares 4 extruder variants and overrides the machine limit.
        child.set_key_value("printer_extruder_id",
            new Slic3r::ConfigOptionInts({1, 2, 3, 4}));
        child.set_key_value("printer_extruder_variant",
            new Slic3r::ConfigOptionStrings({"Direct Drive Standard", "Direct Drive Standard",
                                             "Direct Drive Standard", "Direct Drive Standard"}));
        child.set_key_value("machine_max_acceleration_x",
            new Slic3r::ConfigOptionFloats({8000, 8000, 8000, 8000, 8000, 8000, 8000, 8000}));

        WHEN("update_diff_values_to_child_config merges the child overrides") {
            std::string id_name  = "printer_extruder_id";
            std::string var_name = "printer_extruder_variant";

            THEN("it does not throw on the legacy size mismatch") {
                REQUIRE_NOTHROW(parent.update_diff_values_to_child_config(
                    child, id_name, var_name,
                    Slic3r::printer_options_with_variant_1,
                    Slic3r::printer_options_with_variant_2));

                AND_THEN("the child's overridden machine limit is preserved") {
                    auto* mx = parent.option<Slic3r::ConfigOptionFloats>("machine_max_acceleration_x");
                    REQUIRE(mx != nullptr);
                    REQUIRE(mx->values.size() >= 2);
                    REQUIRE_THAT(mx->values[0], Catch::Matchers::WithinAbs(8000.0, 1e-6));
                    REQUIRE_THAT(mx->values[1], Catch::Matchers::WithinAbs(8000.0, 1e-6));
                }
            }
        }
    }
}

// SCENARIO("DynamicPrintConfig JSON serialization", "[Config]") {
//     WHEN("DynamicPrintConfig is serialized and deserialized") {
// 	auto now = std::chrono::high_resolution_clock::now();
// 	auto timestamp = now.time_since_epoch().count();
// 	std::stringstream ss;
// 	ss << "catch_test_serialization_" << timestamp << ".json";
// 	std::string filename = (fs::temp_directory_path() / ss.str()).string();

// TODO: Finish making a unit test for JSON serialization
//         FullPrintConfig full_print_config;
//         DynamicPrintConfig cfg;
//         cfg.apply(full_print_config, false);

//         std::string serialized;
//         try {
//             std::ostringstream ss;
//             cereal::BinaryOutputArchive oarchive(ss);
//             oarchive(cfg);
//             serialized = ss.str();
//         } catch (const std::runtime_error & /* e */) {
//             // e.what();
//         }
// 	CAPTURE(serialized.length());

//         THEN("Config object contains ini file options.") {
//             DynamicPrintConfig cfg2;
//             try {
//                 std::stringstream ss(serialized);
//                 cereal::BinaryInputArchive iarchive(ss);
//                 iarchive(cfg2);
//             } catch (const std::runtime_error & /* e */) {
//                 // e.what();
//             }
// 	    CAPTURE(cfg.diff_report(cfg2));
//             REQUIRE(cfg == cfg2);
//         }
//     }
// }

TEST_CASE("H2C/A2L-era multi-nozzle and pre-heat config keys exist", "[config]") {
    // Foundation keys backing H2C 6-nozzle cluster grouping, the pre-heat/pre-cool time
    // model, and wipe-tower nozzle-change handling. Defaults must keep existing
    // single-nozzle printers behaving identically.
    Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();

    // Printer / per-extruder options
    REQUIRE(config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count") != nullptr);
    REQUIRE(config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count")->values == std::vector<int>{1});
    REQUIRE(config.option<ConfigOptionBool>("enable_pre_heating") != nullptr);
    REQUIRE(config.option<ConfigOptionBool>("enable_pre_heating")->value == false);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("hotend_cooling_rate") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("hotend_heating_rate") != nullptr);
    REQUIRE(config.option<ConfigOptionFloat>("machine_hotend_change_time") != nullptr);
    REQUIRE(config.option<ConfigOptionFloat>("machine_prepare_compensation_time") != nullptr);

    // Filament pre-cooling / ramming / nozzle-change (nc) options
    REQUIRE(config.option<ConfigOptionIntsNullable>("filament_pre_cooling_temperature") != nullptr);
    REQUIRE(config.option<ConfigOptionIntsNullable>("filament_pre_cooling_temperature_nc") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_preheat_temperature_delta") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_retract_length_nc") != nullptr);
    REQUIRE(config.option<ConfigOptionFloats>("filament_change_length_nc") != nullptr);
    REQUIRE(config.option<ConfigOptionFloats>("filament_prime_volume_nc") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_ramming_travel_time") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_ramming_travel_time_nc") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_ramming_volumetric_speed") != nullptr);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_ramming_volumetric_speed_nc") != nullptr);

    // Spot-check defaults that must not alter existing behavior.
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_retract_length_nc")->values == std::vector<double>{10.});
    REQUIRE(config.option<ConfigOptionFloats>("filament_prime_volume_nc")->values == std::vector<double>{60.});
    REQUIRE(config.option<ConfigOptionIntsNullable>("filament_pre_cooling_temperature_nc")->values == std::vector<int>{0});
    REQUIRE(config.option<ConfigOptionFloatsNullable>("filament_ramming_volumetric_speed")->values == std::vector<double>{-1});
}

SCENARIO("ConfigOptionVector::set_to_index with stride=1 copies values correctly", "[Config][set_to_index]") {
    GIVEN("A destination vector and a source vector with 3 values") {
        Slic3r::ConfigOptionFloats dest({0.0});
        Slic3r::ConfigOptionFloats src({10.0, 20.0, 30.0});
        std::vector<int> variant_index = {0, 1, 2};
        int stride = 1;

        WHEN("set_to_index is called with stride=1") {
            dest.set_to_index(&src, variant_index, stride);

            THEN("The destination contains the source values") {
                REQUIRE(dest.values.size() == 3);
                REQUIRE(dest.values[0] == 10.0);
                REQUIRE(dest.values[1] == 20.0);
                REQUIRE(dest.values[2] == 30.0);
            }
        }
    }

    GIVEN("A destination vector and a source vector with subset mapping") {
        Slic3r::ConfigOptionFloats dest({0.0});
        Slic3r::ConfigOptionFloats src({100.0, 200.0, 300.0});
        std::vector<int> variant_index = {1, 2};
        int stride = 1;

        WHEN("set_to_index maps only indices 1 and 2") {
            dest.set_to_index(&src, variant_index, stride);

            THEN("Only the mapped values are copied, default fills the others") {
                REQUIRE(dest.values.size() == 2);
                REQUIRE(dest.values[0] == 200.0);
                REQUIRE(dest.values[1] == 300.0);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index with stride=2 copies grouped values correctly", "[Config][set_to_index]") {
    GIVEN("A destination vector and a source vector with stride=2 (e.g., nozzle groups)") {
        // Source has 4 groups of 2 values each: (10,11), (20,21), (30,31), (40,41)
        Slic3r::ConfigOptionFloats dest({0.0});
        Slic3r::ConfigOptionFloats src({10.0, 11.0, 20.0, 21.0, 30.0, 31.0, 40.0, 41.0});
        int stride = 2;

        WHEN("set_to_index maps groups 0, 1, 3") {
            std::vector<int> variant_index = {0, 1, 3};
            dest.set_to_index(&src, variant_index, stride);

            THEN("The destination has 3 groups (6 values) mapped correctly") {
                REQUIRE(dest.values.size() == 6);
                // Group 0: (10, 11)
                REQUIRE(dest.values[0] == 10.0);
                REQUIRE(dest.values[1] == 11.0);
                // Group 1: (20, 21)
                REQUIRE(dest.values[2] == 20.0);
                REQUIRE(dest.values[3] == 21.0);
                // Group 3: (40, 41)
                REQUIRE(dest.values[4] == 40.0);
                REQUIRE(dest.values[5] == 41.0);
            }
        }
    }

    GIVEN("A destination and a single-group source") {
        Slic3r::ConfigOptionFloats dest({0.0});
        // Source has 1 group of 2 values
        Slic3r::ConfigOptionFloats src({50.0, 60.0});
        int stride = 2;

        WHEN("set_to_index maps group 0 from a single-group source") {
            std::vector<int> variant_index = {0};
            dest.set_to_index(&src, variant_index, stride);

            THEN("The destination contains the single group correctly") {
                REQUIRE(dest.values.size() == 2);
                REQUIRE(dest.values[0] == 50.0);
                REQUIRE(dest.values[1] == 60.0);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index handles empty dest_index", "[Config][set_to_index]") {
    GIVEN("A destination and source with stride=2") {
        Slic3r::ConfigOptionFloats dest({0.0});
        Slic3r::ConfigOptionFloats src({10.0, 11.0, 20.0, 21.0});
        std::vector<int> variant_index = {};
        int stride = 2;

        WHEN("set_to_index is called with an empty index vector") {
            dest.set_to_index(&src, variant_index, stride);

            THEN("The destination is resized to 0") {
                REQUIRE(dest.values.size() == 0);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index handles nil values in source", "[Config][set_to_index]") {
    GIVEN("A source with a nil group (stride=2)") {
        Slic3r::ConfigOptionFloatsNullable dest({0.0});
        Slic3r::ConfigOptionFloatsNullable src({10.0, 11.0,
            Slic3r::ConfigOptionFloatsNullable::nil_value(), Slic3r::ConfigOptionFloatsNullable::nil_value(),
            30.0, 31.0});
        int stride = 2;

        WHEN("set_to_index maps all groups including the nil one") {
            std::vector<int> variant_index = {0, 1, 2};
            dest.set_to_index(&src, variant_index, stride);

            THEN("Non-nil groups are copied and the nil group keeps the default") {
                REQUIRE(dest.values.size() == 6);
                // Group 0: (10, 11) — copied
                REQUIRE(dest.values[0] == 10.0);
                REQUIRE(dest.values[1] == 11.0);
                // Group 1: nil — keeps default (the front value = 10.0)
                REQUIRE(dest.values[2] == 10.0);
                REQUIRE(dest.values[3] == 10.0);
                // Group 2: (30, 31) — copied
                REQUIRE(dest.values[4] == 30.0);
                REQUIRE(dest.values[5] == 31.0);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index handles out-of-bounds dest_index", "[Config][set_to_index]") {
    GIVEN("A source with only 2 groups (4 values) but dest_index references group 3") {
        Slic3r::ConfigOptionFloats dest({0.0});
        Slic3r::ConfigOptionFloats src({10.0, 11.0, 20.0, 21.0}); // 2 groups of stride 2
        int stride = 2;

        WHEN("set_to_index maps group 3 which is out of bounds") {
            std::vector<int> variant_index = {0, 3}; // group 3 is out of range
            dest.set_to_index(&src, variant_index, stride);

            THEN("Group 0 is copied, group 3 falls back to default without crashing") {
                REQUIRE(dest.values.size() == 4);
                // Group 0: (10, 11) — copied
                REQUIRE(dest.values[0] == 10.0);
                REQUIRE(dest.values[1] == 11.0);
                // Group 3: out of bounds — keeps default (10.0 = src.values.front())
                REQUIRE(dest.values[2] == 10.0);
                REQUIRE(dest.values[3] == 10.0);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index handles negative dest_index values", "[Config][set_to_index]") {
    GIVEN("A destination and source with a negative entry in dest_index") {
        // The dest is initially empty, so resize fills all slots with src.values.front().
        Slic3r::ConfigOptionFloats dest;
        Slic3r::ConfigOptionFloats src({100.0, 101.0, 200.0, 201.0});
        int stride = 2;

        WHEN("set_to_index maps group 0 and a negative index") {
            std::vector<int> variant_index = {-1, 0};
            dest.set_to_index(&src, variant_index, stride);

            THEN("The negative index is skipped, the valid group is copied") {
                REQUIRE(dest.values.size() == 4);
                // Position 0 (variant_index[0] = -1): skipped, keeps default fill
                // from resize (src.values.front() = 100.0, applied to all new elements)
                REQUIRE(dest.values[0] == 100.0);
                REQUIRE(dest.values[1] == 100.0);
                // Position 1 (variant_index[1] = 0): copied from group 0 of src
                REQUIRE(dest.values[2] == 100.0);
                REQUIRE(dest.values[3] == 101.0);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index handles single-element groups with stride=1", "[Config][set_to_index]") {
    GIVEN("A destination re-mapping one variant index with a stride=1 source") {
        // Simulates the PrintObject.cpp code path: stride=1, variant_index={1}
        Slic3r::ConfigOptionFloats dest({99.0, 99.0, 99.0, 99.0}); // pre-sized for 4 extruders
        Slic3r::ConfigOptionFloats src({0.5, 0.6, 0.7, 0.8});      // 4 extruder values
        std::vector<int> variant_index = {1}; // only extruder 1 is active
        int stride = 1;

        WHEN("set_to_index is called") {
            dest.set_to_index(&src, variant_index, stride);

            THEN("Only the mapped value is copied, rest are defaulted") {
                REQUIRE(dest.values.size() == 1);
                REQUIRE(dest.values[0] == 0.6);
            }
        }
    }
}

SCENARIO("ConfigOptionVector::set_to_index throws on incompatible type", "[Config][set_to_index]") {
    GIVEN("A Floats destination and an Ints source") {
        Slic3r::ConfigOptionFloats dest({0.0});
        Slic3r::ConfigOptionInts src({1, 2, 3});
        std::vector<int> variant_index = {0};
        int stride = 1;

        WHEN("set_to_index is called with mismatched types") {
            THEN("A ConfigurationError is thrown") {
                REQUIRE_THROWS_AS(dest.set_to_index(&src, variant_index, stride), Slic3r::ConfigurationError);
            }
        }
    }
}

// PNP fork (F13) regression. The Plater view / Canvas3D build their config via
// DynamicPrintConfig::new_from_defaults_keys(plater_view_default_config_keys). If any
// key is absent from the print config def, that call throws UnknownOptionException and
// the GUI dies at startup — exactly how the removed SLA option "material_colour"
// crashed the app after the SLA cut (F12). Guard that every key stays defined so a
// def-less key can never be (re)introduced into the list unnoticed.
SCENARIO("Plater default view config keys are all defined", "[Config]") {
    GIVEN("the plater_view_default_config_keys list") {
        REQUIRE_FALSE(plater_view_default_config_keys.empty());

        THEN("every key resolves in the print config def") {
            for (const std::string& opt_key : plater_view_default_config_keys) {
                INFO("undefined plater view config key: " << opt_key);
                REQUIRE(print_config_def.get(opt_key) != nullptr);
            }
        }

        THEN("new_from_defaults_keys does not throw (the material_colour crash path)") {
            std::unique_ptr<DynamicPrintConfig> cfg;
            REQUIRE_NOTHROW(cfg.reset(DynamicPrintConfig::new_from_defaults_keys(plater_view_default_config_keys)));
            REQUIRE(cfg != nullptr);
        }

        THEN("the removed SLA option material_colour is not in the list") {
            REQUIRE(std::find(plater_view_default_config_keys.begin(),
                              plater_view_default_config_keys.end(),
                              std::string("material_colour")) == plater_view_default_config_keys.end());
        }
    }
}

// PNP fork (B9): a CONFIG_BLOCK is a foreign producer's artifact, and one unreadable value in it
// must not cost us the whole config -- and with it the G-code preview.
//
// Found by a human during B9 acceptance. pnp echoes its resolved config into the CONFIG_BLOCK, and
// for keys absent from its typed schema it infers the type from the value, so Orca's string "0"/"1"
// comes back as `false`/`true`. On a real slice that mistyped 69 numeric keys; the first of them
// (adaptive_bed_mesh_margin, a coFloat handed "false") threw BadOptionValueException out of
// load_from_gcode_file and the preview never loaded. Substitution cannot rescue it: set_deserialize_raw
// only substitutes coBools and enum/bool pairs, so a coFloat has no recovery path.
//
// The contract pinned here is that a bad *value* is tolerated exactly like an unknown *key* already
// was: skipped, left at its default, with the rest of the block still applied.
SCENARIO("A CONFIG_BLOCK with an unreadable value still loads", "[Config]") {
    GIVEN("a G-code CONFIG_BLOCK whose coFloat key carries a boolean, as pnp emits it") {
        namespace fs = boost::filesystem;
        const fs::path path = fs::temp_directory_path() / fs::unique_path("orca_cfgblock_%%%%%%%%.gcode");

        {
            // load_from_gcode_file rejects a block yielding fewer than 80 applied pairs, so the
            // fixture has to be a realistic block rather than a handful of lines. (That floor is
            // also why the real case survives: only 69 of pnp's ~700 keys are mistyped.)
            const DynamicPrintConfig full = DynamicPrintConfig::full_print_config();
            std::vector<std::string> filler;
            for (const std::string& key : full.keys()) {
                if (key == "layer_height" || key == "wall_loops" || key == "adaptive_bed_mesh_margin")
                    continue;
                const ConfigOption* opt = full.option(key);
                if (opt == nullptr)
                    continue;
                const std::string serialized = opt->serialize();
                // Multi-line values (custom G-code) cannot ride in a one-line-per-key block.
                if (serialized.empty() || serialized.find('\n') != std::string::npos)
                    continue;
                filler.push_back("; " + key + " = " + serialized + "\n");
                if (filler.size() >= 120)
                    break;
            }
            REQUIRE(filler.size() >= 100); // comfortably clear of the 80-pair floor

            boost::nowide::ofstream out(path.string());
            // Verbatim pnp header. load_from_gcode_file refuses anything without a recognised
            // producer line, and this is the one that gets pnp's output past that gate: it matches
            // on the "; OrcaSlicer" prefix.
            out << "; OrcaSlicer-compatible output generated by Pinch'n'Print\n"
                   "G1 X0 Y0\n"
                   "; CONFIG_BLOCK_START\n"
                   "; layer_height = 0.25\n"
                   "; adaptive_bed_mesh_margin = false\n"    // coFloat handed a bool -> unreadable
                   "; a_key_this_build_never_heard_of = 7\n"; // unknown key -> already tolerated
            for (const std::string& line : filler)
                out << line;
            // Deliberately after the unreadable key: proves parsing continued past it.
            out << "; wall_loops = 4\n"
                   "; CONFIG_BLOCK_END\n";
        }

        WHEN("loaded through load_from_gcode_file") {
            // The load lives here, not in a THEN: each THEN is its own Catch2 section, so a load
            // performed inside one would leave the other asserting against an untouched config.
            DynamicPrintConfig config;
            REQUIRE_NOTHROW(config.load_from_gcode_file(
                path.string(), ForwardCompatibilitySubstitutionRule::EnableSilent));

            THEN("the keys on both sides of the bad one are still applied") {
                // Parsing did not stop at the unreadable value.
                REQUIRE(config.option<ConfigOptionFloat>("layer_height") != nullptr);
                REQUIRE_THAT(config.opt_float("layer_height"),
                             Catch::Matchers::WithinAbs(0.25, 1e-9));
                REQUIRE(config.option<ConfigOptionInt>("wall_loops") != nullptr);
                REQUIRE(config.opt_int("wall_loops") == 4);
            }

            THEN("the unreadable key keeps its default rather than a half-applied value") {
                // set_deserialize_raw materialises the option (with its default) before attempting
                // the parse, so the key is present -- what matters is that the failed parse left
                // that default intact. A wrong number here would be worse than the original crash,
                // because the preview's legend reads from this config and would quietly mislead.
                const ConfigOptionFloat* opt = config.option<ConfigOptionFloat>("adaptive_bed_mesh_margin");
                REQUIRE(opt != nullptr);

                const ConfigOptionDef* def = print_config_def.get("adaptive_bed_mesh_margin");
                REQUIRE(def != nullptr);
                const auto* expected = static_cast<const ConfigOptionFloat*>(def->default_value.get());
                REQUIRE(expected != nullptr);
                REQUIRE_THAT(opt->value, Catch::Matchers::WithinAbs(expected->value, 1e-9));
            }
        }

        boost::system::error_code ec;
        fs::remove(path, ec);
    }
}

// PNP fork (SchemaBridgeMap ticket 03). A key print_config_def does not have is dropped on
// load: PrintConfigDef::handle_legacy clears it, set_deserialize_nothrow records the name in
// ConfigSubstitutionContext::unrecogized_keys and returns success, and no value is stored --
// so saving the file writes it out no more. For a setting belonging to a pnp module the
// current install does not have, that is silent data loss on a file the user expects to
// round-trip. Preservation is opt-in, so the drop-and-record behaviour must survive untouched
// for the callers (vendor profiles, inherits sub-files) that still want it.
SCENARIO("load_from_json tolerates and preserves keys this build cannot resolve", "[Config][PNP]") {
    namespace fs = boost::filesystem;

    GIVEN("a config json mixing a known key with keys no build of this fork defines") {
        const std::string scalar_key = "pnp_ticket03_scalar";
        const std::string list_key   = "pnp_ticket03_list";
        REQUIRE(print_config_def.get(scalar_key) == nullptr);
        REQUIRE(print_config_def.get(list_key) == nullptr);

        const fs::path path = fs::temp_directory_path() / fs::unique_path("pnp_t03_%%%%%%%%.json");
        {
            boost::nowide::ofstream out(path.string());
            out << "{\n"
                << "  \"version\": \"2.3.0\",\n"
                << "  \"name\": \"unit test\",\n"
                << "  \"layer_height\": \"0.28\",\n"
                << "  \"" << scalar_key << "\": \"smart\",\n"
                << "  \"" << list_key << "\": [\"1\", \"2\"]\n"
                << "}\n";
        }

        WHEN("loaded with an unknown-key carrier") {
            DynamicPrintConfig config;
            std::map<std::string, std::string> key_values;
            std::string reason;
            ConfigBase::t_unknown_config_values unknown;
            config.load_from_json(path.string(), ForwardCompatibilitySubstitutionRule::Enable,
                                  key_values, reason, &unknown);

            THEN("the known key loads and the unresolvable ones are collected verbatim") {
                REQUIRE(reason.empty());
                REQUIRE(config.option<ConfigOptionFloat>("layer_height") != nullptr);
                REQUIRE_THAT(config.option<ConfigOptionFloat>("layer_height")->value,
                             Catch::Matchers::WithinAbs(0.28, 1e-9));

                REQUIRE(unknown.size() == 2);
                // Stored as the JSON fragment as read, so the value round-trips exactly --
                // including the distinction between a scalar and a list.
                REQUIRE(unknown.at(scalar_key) == "\"smart\"");
                REQUIRE(unknown.at(list_key) == "[\"1\",\"2\"]");
            }

            THEN("they are not in the config, so diff() and the dirty-state cannot see them") {
                REQUIRE(config.option(scalar_key) == nullptr);
                REQUIRE(config.option(list_key) == nullptr);
                const t_config_option_keys keys = config.keys();
                REQUIRE(std::find(keys.begin(), keys.end(), scalar_key) == keys.end());
                REQUIRE(std::find(keys.begin(), keys.end(), list_key) == keys.end());
            }

            THEN("save_to_json re-emits them beside the live keys") {
                const fs::path out_path = fs::temp_directory_path() / fs::unique_path("pnp_t03_out_%%%%%%%%.json");
                config.save_to_json(out_path.string(), "unit test", "project", "2.3.0", &unknown);

                DynamicPrintConfig reloaded;
                std::map<std::string, std::string> kv2;
                std::string reason2;
                ConfigBase::t_unknown_config_values unknown2;
                reloaded.load_from_json(out_path.string(), ForwardCompatibilitySubstitutionRule::Enable,
                                        kv2, reason2, &unknown2);

                REQUIRE(reason2.empty());
                REQUIRE(unknown2.at(scalar_key) == "\"smart\"");
                REQUIRE(unknown2.at(list_key) == "[\"1\",\"2\"]");
                REQUIRE(reloaded.option<ConfigOptionFloat>("layer_height") != nullptr);

                boost::system::error_code ec;
                fs::remove(out_path, ec);
            }
        }

        WHEN("loaded without a carrier, as the vendor-profile call sites still do") {
            DynamicPrintConfig config;
            std::map<std::string, std::string> key_values;
            std::string reason;
            ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Enable };
            const int ret = config.load_from_json(path.string(), ctxt, true, key_values, reason);

            THEN("behaviour is exactly as before this ticket: dropped, and named in the context") {
                REQUIRE(ret == 0);
                REQUIRE(reason.empty());
                REQUIRE(config.option<ConfigOptionFloat>("layer_height") != nullptr);
                REQUIRE(config.option(scalar_key) == nullptr);
                REQUIRE(config.option(list_key) == nullptr);
                // The pre-existing signal this ticket hooks: PrintConfigDef::handle_legacy
                // clears a key print_config_def does not have, and set_deserialize_nothrow
                // records the original name here. Without a carrier the *value* is still
                // lost, which is the data loss the opt-in fixes.
                REQUIRE(std::find(ctxt.unrecogized_keys.begin(), ctxt.unrecogized_keys.end(), scalar_key)
                        != ctxt.unrecogized_keys.end());
                REQUIRE(std::find(ctxt.unrecogized_keys.begin(), ctxt.unrecogized_keys.end(), list_key)
                        != ctxt.unrecogized_keys.end());
            }
        }

        boost::system::error_code ec;
        fs::remove(path, ec);
    }
}

// A key that is unresolvable at load time may be defined later -- the module declaring it gets
// installed, so ticket 02's startup registration puts it in print_config_def and it loads as a
// normal key. The stale carrier entry must not then overwrite the live value on save.
SCENARIO("save_to_json prefers the live value over a stale preserved entry", "[Config][PNP]") {
    namespace fs = boost::filesystem;

    GIVEN("a config holding layer_height and a carrier claiming a different layer_height") {
        DynamicPrintConfig config;
        config.set_key_value("layer_height", new ConfigOptionFloat(0.28));

        ConfigBase::t_unknown_config_values stale;
        stale["layer_height"] = "\"0.10\"";

        WHEN("saved") {
            const fs::path out_path = fs::temp_directory_path() / fs::unique_path("pnp_t03_stale_%%%%%%%%.json");
            config.save_to_json(out_path.string(), "unit test", "project", "2.3.0", &stale);

            DynamicPrintConfig reloaded;
            std::map<std::string, std::string> kv;
            std::string reason;
            reloaded.load_from_json(out_path.string(), ForwardCompatibilitySubstitutionRule::Enable, kv, reason);

            THEN("the live value wins and the stale entry is dropped") {
                REQUIRE(reason.empty());
                REQUIRE(reloaded.option<ConfigOptionFloat>("layer_height") != nullptr);
                REQUIRE_THAT(reloaded.option<ConfigOptionFloat>("layer_height")->value,
                             Catch::Matchers::WithinAbs(0.28, 1e-9));
            }

            boost::system::error_code ec;
            fs::remove(out_path, ec);
        }
    }
}
