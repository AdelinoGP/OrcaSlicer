// PNP fork: regression tests for the 3MF project_settings.config sidecar
// merge (the slice path's only config channel — no separate --config file).
//
// The merge must overlay the translated (typed) PNP config over the raw Orca
// config store_bbs_3mf wrote, preserving the raw keys pnp consumes directly
// (gcode_flavor, filament_colour) and keeping the translated values typed
// (numbers stay numbers, arrays stay arrays) so pnp's sidecar parser maps them
// to typed ConfigValues.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include <miniz.h>

#include <nlohmann/json.hpp>

#include "slic3r/GUI/PnpModelSidecar.hpp"

using json = nlohmann::json;
using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

// Write a minimal 3MF: one dummy model entry plus a raw Orca-style
// project_settings.config sidecar (all values as strings, like Orca stores
// them). Returns the temp path.
boost::filesystem::path make_minimal_3mf(const std::string& sidecar_json)
{
    const boost::filesystem::path path =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("pnp-sidecar-test-%%%%%%%%-%%%%%%%%.3mf");

    mz_zip_archive writer;
    memset(&writer, 0, sizeof(writer));
    REQUIRE(mz_zip_writer_init_file(&writer, path.string().c_str(), 0));
    const char* model_xml = "<model unit=\"mm\" xml:lang=\"en-US\"/>";
    REQUIRE(mz_zip_writer_add_mem(&writer, "3D/3dmodel.model", model_xml, strlen(model_xml), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_add_mem(&writer, "Metadata/project_settings.config", sidecar_json.data(), sidecar_json.size(),
                                  MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&writer));
    mz_zip_writer_end(&writer);
    return path;
}

// Read the sidecar back out of a 3MF as parsed JSON.
json read_sidecar(const boost::filesystem::path& path)
{
    mz_zip_archive reader;
    memset(&reader, 0, sizeof(reader));
    REQUIRE(mz_zip_reader_init_file(&reader, path.string().c_str(), 0));
    const mz_uint num = mz_zip_reader_get_num_files(&reader);
    for (mz_uint i = 0; i < num; ++i) {
        mz_zip_archive_file_stat stat;
        REQUIRE(mz_zip_reader_file_stat(&reader, i, &stat));
        if (stat.m_filename != nullptr && boost::iequals(stat.m_filename, "Metadata/project_settings.config")) {
            std::string text(stat.m_uncomp_size, '\0');
            REQUIRE(mz_zip_reader_extract_to_mem(&reader, i, text.data(), text.size(), 0));
            mz_zip_reader_end(&reader);
            return json::parse(text);
        }
    }
    mz_zip_reader_end(&reader);
    FAIL("project_settings.config not found in the rewritten 3MF");
    return json();
}

} // namespace

TEST_CASE("3MF sidecar merge overlays the translated config", "[pnp][sidecar]")
{
    const boost::filesystem::path path = make_minimal_3mf(R"({
        "version": "1.0",
        "name": "project_settings",
        "sparse_infill_density": "25",
        "line_width": "105%",
        "gcode_flavor": "klipper",
        "filament_colour": ["#FF0000"]
    })");

    const json translated = json::parse(R"({
        "infill_density": 0.25,
        "sparse_infill_density": 25,
        "line_width": 0.525,
        "min_feature_size": 0.075,
        "wall_transition_filter_deviation": 1250,
        "support_enabled": true,
        "bed_shape": [0.0, 0.0, 220.0, 0.0, 220.0, 200.0, 0.0, 200.0],
        "skirt_loops": 0
    })");

    REQUIRE(merge_translated_config_into_3mf(path, translated));

    const json sidecar = read_sidecar(path);

    SECTION("raw keys pnp consumes directly are preserved")
    {
        REQUIRE(sidecar.at("gcode_flavor").get<std::string>() == "klipper");
        REQUIRE(sidecar.at("filament_colour").is_array());
        REQUIRE(sidecar.at("filament_colour")[0].get<std::string>() == "#FF0000");
    }

    SECTION("translated keys win over the raw string values")
    {
        REQUIRE(sidecar.at("line_width").get<double>() == Catch::Approx(0.525));
        REQUIRE(sidecar.at("sparse_infill_density").get<double>() == Catch::Approx(25.0));
    }

    SECTION("translated values keep their JSON types")
    {
        REQUIRE(sidecar.at("infill_density").is_number());
        REQUIRE(sidecar.at("infill_density").get<double>() == Catch::Approx(0.25));
        REQUIRE(sidecar.at("min_feature_size").is_number());
        REQUIRE(sidecar.at("wall_transition_filter_deviation").is_number());
        REQUIRE(sidecar.at("support_enabled").is_boolean());
        REQUIRE(sidecar.at("support_enabled").get<bool>());
        REQUIRE(sidecar.at("bed_shape").is_array());
        REQUIRE(sidecar.at("bed_shape").size() == 8);
        REQUIRE(sidecar.at("bed_shape")[2].get<double>() == Catch::Approx(220.0));
        // "0" must stay a number, not coerce to a boolean on the pnp side.
        REQUIRE(sidecar.at("skirt_loops").is_number());
        REQUIRE(sidecar.at("skirt_loops").get<int>() == 0);
    }

    SECTION("non-sidecar entries survive the rewrite")
    {
        mz_zip_archive reader;
        memset(&reader, 0, sizeof(reader));
        REQUIRE(mz_zip_reader_init_file(&reader, path.string().c_str(), 0));
        bool found_model = false;
        const mz_uint num = mz_zip_reader_get_num_files(&reader);
        for (mz_uint i = 0; i < num; ++i) {
            mz_zip_archive_file_stat stat;
            REQUIRE(mz_zip_reader_file_stat(&reader, i, &stat));
            if (stat.m_filename != nullptr && boost::iequals(stat.m_filename, "3D/3dmodel.model"))
                found_model = true;
        }
        mz_zip_reader_end(&reader);
        REQUIRE(found_model);
    }

    boost::filesystem::remove(path);
}

TEST_CASE("3MF sidecar merge fails closed on a missing sidecar", "[pnp][sidecar]")
{
    const boost::filesystem::path path =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("pnp-sidecar-test-%%%%%%%%-%%%%%%%%.3mf");
    mz_zip_archive writer;
    memset(&writer, 0, sizeof(writer));
    REQUIRE(mz_zip_writer_init_file(&writer, path.string().c_str(), 0));
    const char* model_xml = "<model unit=\"mm\" xml:lang=\"en-US\"/>";
    REQUIRE(mz_zip_writer_add_mem(&writer, "3D/3dmodel.model", model_xml, strlen(model_xml), MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&writer));
    mz_zip_writer_end(&writer);

    REQUIRE_FALSE(merge_translated_config_into_3mf(path, json::object()));
    boost::filesystem::remove(path);
}
