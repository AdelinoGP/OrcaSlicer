// PNP fork: tests for the config-warnings sink (F03, wayfinder ticket 013).
//
// The sink appends one JSON object per surviving warning to
// <data_dir>/pnp-config-warnings.jsonl. Its whole reason for existing is the
// Tier-D filter: full_config() hands the translator every key with defaults
// filled in, so without the filter roughly 830 not-yet-mapped records would be
// written on every single slice and the file would be useless as a dev
// instrument. That filter is what these tests mainly pin.
//
// log_pnp_config_warnings writes relative to the global data_dir(), so each case
// points it at a temp directory and restores it afterwards.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/filesystem/operations.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/PnpConfigWarning.hpp"
#include "slic3r/GUI/PnpConfigWarningsLog.hpp"

using json = nlohmann::json;
using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

// Redirects data_dir() at construction and restores it at destruction, so a
// failing assertion cannot leak the temp path into later test cases in this
// binary.
class ScopedDataDir
{
public:
    ScopedDataDir()
        : m_previous(data_dir())
        , m_path((boost::filesystem::temp_directory_path() /
                  boost::filesystem::unique_path("orca_pnp_warn_%%%%%%%%")).string())
    {
        boost::filesystem::create_directories(m_path);
        set_data_dir(m_path);
    }
    ~ScopedDataDir()
    {
        set_data_dir(m_previous);
        boost::system::error_code ec;
        boost::filesystem::remove_all(m_path, ec);
    }

    ScopedDataDir(const ScopedDataDir&)            = delete;
    ScopedDataDir& operator=(const ScopedDataDir&) = delete;

    std::string jsonl_path() const { return m_path + "/pnp-config-warnings.jsonl"; }

    // One parsed JSON object per non-empty line.
    std::vector<json> records() const
    {
        std::vector<json> out;
        std::ifstream in(jsonl_path());
        std::string   line;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;
            out.push_back(json::parse(line));
        }
        return out;
    }

private:
    std::string m_previous;
    std::string m_path;
};

PnpConfigWarning warning(const std::string& key, PnpWarningClass cls,
                         const std::string& orca_value = "1",
                         const std::string& sent_value = std::string())
{
    PnpConfigWarning w;
    w.key        = key;
    w.warn_class = cls;
    w.orca_value = orca_value;
    w.sent_value = sent_value;
    return w;
}

} // namespace

TEST_CASE("config-warnings sink drops not-yet-mapped keys sitting at their default",
          "[pnp][warnings]")
{
    ScopedDataDir dir;

    // A full config, with exactly one key moved off its Orca default.
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();
    full.set_key_value("layer_height", new ConfigOptionFloat(0.42));

    std::vector<PnpConfigWarning> warnings{
        warning("layer_height", PnpWarningClass::NotYetMapped, "0.42"), // changed -> kept
        warning("wall_loops", PnpWarningClass::NotYetMapped, "2"),      // at default -> dropped
    };

    log_pnp_config_warnings(full, std::move(warnings), /*plate=*/0);

    const std::vector<json> recs = dir.records();
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0]["key"] == "layer_height");
    REQUIRE(recs[0]["class"] == "not-yet-mapped");
}

TEST_CASE("config-warnings sink never filters the non-Tier-D classes", "[pnp][warnings]")
{
    ScopedDataDir dir;

    // Nothing is moved off its default, so the Tier-D filter would drop every
    // record if it applied to these classes. It must not.
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();

    std::vector<PnpConfigWarning> warnings{
        warning("spiral_mode", PnpWarningClass::UnsupportedFeature),
        warning("gcode_comments", PnpWarningClass::NoOp),
        warning("seam_position", PnpWarningClass::LossyFallback, "aligned", "nearest"),
        warning("wall_loops", PnpWarningClass::NotYetMapped, "2"),
    };

    log_pnp_config_warnings(full, std::move(warnings), /*plate=*/1);

    const std::vector<json> recs = dir.records();
    REQUIRE(recs.size() == 3);

    std::vector<std::string> classes;
    for (const json& r : recs)
        classes.push_back(r["class"].get<std::string>());
    REQUIRE(std::find(classes.begin(), classes.end(), "unsupported-feature") != classes.end());
    REQUIRE(std::find(classes.begin(), classes.end(), "no-op") != classes.end());
    REQUIRE(std::find(classes.begin(), classes.end(), "lossy-fallback") != classes.end());
    REQUIRE(std::find(classes.begin(), classes.end(), "not-yet-mapped") == classes.end());
}

TEST_CASE("config-warnings records carry the documented fields", "[pnp][warnings]")
{
    ScopedDataDir dir;
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();

    std::vector<PnpConfigWarning> warnings{
        warning("seam_position", PnpWarningClass::LossyFallback, "aligned", "nearest"),
        warning("spiral_mode", PnpWarningClass::UnsupportedFeature, "1"),
    };

    log_pnp_config_warnings(full, std::move(warnings), /*plate=*/2);

    const std::vector<json> recs = dir.records();
    REQUIRE(recs.size() == 2);

    for (const json& r : recs) {
        REQUIRE(r.contains("ts"));
        REQUIRE_FALSE(r["ts"].get<std::string>().empty());
        REQUIRE(r["plate"] == 2);
        REQUIRE(r.contains("key"));
        REQUIRE(r.contains("class"));
        REQUIRE(r.contains("orca_value"));
    }

    // sent_value is meaningful only for a substituted value, so it rides on
    // lossy-fallback records and nothing else.
    for (const json& r : recs) {
        if (r["class"] == "lossy-fallback") {
            REQUIRE(r["sent_value"] == "nearest");
            REQUIRE(r["orca_value"] == "aligned");
        } else {
            REQUIRE_FALSE(r.contains("sent_value"));
        }
    }
}

TEST_CASE("config-warnings sink appends across slices instead of truncating", "[pnp][warnings]")
{
    // "warnings jsonl grows" is the acceptance-level expectation; at this level
    // that means a second slice must not clobber the first slice's records.
    ScopedDataDir dir;
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();

    log_pnp_config_warnings(full, { warning("spiral_mode", PnpWarningClass::UnsupportedFeature) },
                            /*plate=*/0);
    const size_t after_first = dir.records().size();
    REQUIRE(after_first == 1);

    log_pnp_config_warnings(full, { warning("gcode_comments", PnpWarningClass::NoOp) },
                            /*plate=*/1);
    const std::vector<json> recs = dir.records();
    REQUIRE(recs.size() == after_first + 1);
    REQUIRE(recs[0]["plate"] == 0);
    REQUIRE(recs[1]["plate"] == 1);
}

TEST_CASE("config-warnings sink writes nothing when every record is filtered", "[pnp][warnings]")
{
    ScopedDataDir dir;
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();

    log_pnp_config_warnings(full, { warning("wall_loops", PnpWarningClass::NotYetMapped, "2") },
                            /*plate=*/0);

    REQUIRE(dir.records().empty());
}

TEST_CASE("config-warnings filter drops not-yet-mapped keys at their default", "[pnp][warnings]")
{
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();
    full.set_key_value("layer_height", new ConfigOptionFloat(0.42));

    std::vector<PnpConfigWarning> warnings{
        warning("layer_height", PnpWarningClass::NotYetMapped, "0.42"), // changed -> kept
        warning("wall_loops", PnpWarningClass::NotYetMapped, "2"),      // at default -> dropped
    };

    const auto kept = filter_pnp_config_warnings(full, warnings, /*include_no_op=*/true);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].key == "layer_height");
}

TEST_CASE("config-warnings filter never drops the non-Tier-D classes", "[pnp][warnings]")
{
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();
    full.set_key_value("wall_loops", new ConfigOptionInt(5));

    std::vector<PnpConfigWarning> warnings{
        warning("spiral_mode", PnpWarningClass::UnsupportedFeature),
        warning("seam_position", PnpWarningClass::LossyFallback, "aligned", "nearest"),
        warning("wall_loops", PnpWarningClass::NotYetMapped, "5"), // non-default -> kept
    };

    const auto kept = filter_pnp_config_warnings(full, warnings, /*include_no_op=*/true);
    REQUIRE(kept.size() == 3);
}

TEST_CASE("config-warnings filter drops no-op records for the UI", "[pnp][warnings]")
{
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();

    std::vector<PnpConfigWarning> warnings{
        warning("gcode_comments", PnpWarningClass::NoOp),
        warning("spiral_mode", PnpWarningClass::UnsupportedFeature),
    };

    const auto kept = filter_pnp_config_warnings(full, warnings, /*include_no_op=*/false);
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].key == "spiral_mode");
}

TEST_CASE("config-warning message groups by class and shows substituted values", "[pnp][warnings]")
{
    PnpConfigWarningLabels labels;
    labels.title       = "PNP config warnings:";
    labels.unsupported = "not supported by PNP";
    labels.lossy       = "sent with substituted value";
    labels.unmapped    = "not mapped to PNP";

    std::vector<PnpConfigWarning> warnings{
        warning("raft_layers", PnpWarningClass::UnsupportedFeature, "3"),
        warning("gcode_flavor", PnpWarningClass::UnsupportedFeature, "klipper"),
        warning("seam_position", PnpWarningClass::LossyFallback, "aligned", "nearest"),
        warning("wall_loops", PnpWarningClass::NotYetMapped, "2"),
    };

    const std::string msg = format_pnp_config_warning_message(warnings, labels);
    REQUIRE(msg.find("PNP config warnings:") != std::string::npos);
    REQUIRE(msg.find("not supported by PNP: raft_layers, gcode_flavor") != std::string::npos);
    REQUIRE(msg.find("seam_position (aligned → nearest)") != std::string::npos);
    REQUIRE(msg.find("not mapped to PNP: wall_loops") != std::string::npos);
}

TEST_CASE("config-warning message caps long key lists", "[pnp][warnings]")
{
    PnpConfigWarningLabels labels;
    labels.title       = "PNP config warnings:";
    labels.unsupported = "not supported by PNP";
    labels.lossy       = "sent with substituted value";
    labels.unmapped    = "not mapped to PNP";

    std::vector<PnpConfigWarning> warnings;
    for (int i = 0; i < 8; ++i)
        warnings.push_back(warning("key" + std::to_string(i), PnpWarningClass::UnsupportedFeature));

    const std::string msg = format_pnp_config_warning_message(warnings, labels);
    REQUIRE(msg.find("key0") != std::string::npos);
    REQUIRE(msg.find("key4") != std::string::npos);
    REQUIRE(msg.find("key5") == std::string::npos);
    REQUIRE(msg.find("3 more") != std::string::npos);
}
