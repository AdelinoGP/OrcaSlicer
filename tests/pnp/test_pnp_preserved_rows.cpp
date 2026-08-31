// PNP fork (SchemaBridgeMap ticket 12): the degraded PNP Backend page's
// preserved-key list.
//
// One half is GUI-free and tested here: pnp_preserved_key_rows(), which
// renders ticket 03's unknown-key carriers (key -> serialized JSON fragment)
// into the display rows the page shows. The other half -- the wx page itself
// (banner, row widgets, per-row Remove, activation-time refresh) -- is the
// manual smoke recorded in the ticket's resolution comment.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

#include "libslic3r/Config.hpp"
#include "slic3r/GUI/PnpConfigKeys.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;
using Slic3r::GUI::PnpConfigKeys::PnpPreservedKey;
using Slic3r::GUI::PnpConfigKeys::PnpPreservedSource;

namespace {

ConfigBase::t_unknown_config_values carrier(std::map<std::string, std::string> entries)
{
    return {entries.begin(), entries.end()};
}

} // namespace

TEST_CASE("preserved-key rows render ticket 03's carrier fragments", "[pnp][config_keys][preserved]")
{
    // The carrier stores the *serialized JSON fragment* (ticket 03), so a
    // string key arrives quoted and a list as a JSON array. The rows must be
    // what a user would have typed: "smart", 0.35, true, "a, b".
    GIVEN("one carrier per store, keys interleaved") {
        const ConfigBase::t_unknown_config_values preset = carrier({
            {"pattern_key", "\"smart\""},
            {"shared_key",  "0.35"},
        });
        const ConfigBase::t_unknown_config_values project = carrier({
            {"list_key",   "[1, 2]"},
            {"shared_key", "0.5"},
            {"bool_key",   "true"},
        });

        WHEN("the rows are derived") {
            const std::vector<PnpPreservedKey> rows = PnpConfigKeys::pnp_preserved_key_rows(preset, project);

            THEN("every (key, store) pair appears exactly once, sorted, preset first on ties") {
                REQUIRE(rows.size() == 5);
                REQUIRE(rows[0].key == "bool_key");    CHECK(rows[0].value == "true");
                REQUIRE(rows[1].key == "list_key");    CHECK(rows[1].value == "1, 2");
                REQUIRE(rows[2].key == "pattern_key"); CHECK(rows[2].value == "smart");
                REQUIRE(rows[3].key == "shared_key");  // preset store first
                REQUIRE(rows[4].key == "shared_key");  // then the project store
                CHECK(rows[3].value == "0.35");
                CHECK(rows[4].value == "0.5");
                CHECK(rows[3].source == PnpPreservedSource::PrintPreset);
                CHECK(rows[4].source == PnpPreservedSource::Project);
            }
        }
    }

    GIVEN("an unparsable fragment (corrupt store)") {
        const ConfigBase::t_unknown_config_values broken = carrier({{"odd_key", "not json"}});

        WHEN("the rows are derived") {
            const std::vector<PnpPreservedKey> rows = PnpConfigKeys::pnp_preserved_key_rows({}, broken);

            THEN("the fragment is shown raw -- nothing the file holds is invisible") {
                REQUIRE(rows.size() == 1);
                CHECK(rows[0].key == "odd_key");
                CHECK(rows[0].value == "not json");
            }
        }
    }

    GIVEN("both carriers empty -- the healthy case") {
        WHEN("the rows are derived") {
            const std::vector<PnpPreservedKey> rows = PnpConfigKeys::pnp_preserved_key_rows({}, {});

            THEN("no rows; the caller adds no page") {
                CHECK(rows.empty());
            }
        }
    }
}