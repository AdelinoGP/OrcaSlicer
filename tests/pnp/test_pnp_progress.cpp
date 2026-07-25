// PNP fork: tests for PnpProgressParser (F06) — the parser for pnp_cli's stderr
// JSONL progress stream.
//
// This is the fork's wire contract with the pnp backend, and until B9 it had no
// committed coverage at all: the stream's schema moved 1.2.0 -> 1.3.0 between
// batches with nothing in the tree noticing. Two things are pinned here that
// matter more than the arithmetic:
//
//   * Forward compatibility. Unknown event types and unknown fields must be
//     ignored rather than treated as errors — that tolerance is exactly what let
//     1.3.0 land without a fork change.
//   * The per-slice schema-version gate (wayfinder ticket 009). The startup
//     probe gates the CONFIG schema; this stream carries the PROGRESS schema, an
//     independent semver line. A major mismatch has to condemn the slice, or a
//     stream this build cannot read would silently drive the progress bar and
//     the legend.
//
// Streams are hand-written literals so each case states its own intent. The one
// exception is the accept case, which uses a line captured verbatim from a real
// pnp_cli run so "the shipping version is accepted" is asserted against reality
// rather than against a guess at the format.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "slic3r/GUI/PnpBackend.hpp"
#include "slic3r/GUI/PnpProgress.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

// A real line from a `pnp_cli slice --instrument-stderr` run (B9 probe), used
// verbatim. Which event arrives first is not fixed — captures this session
// started with both phase_start and phase_complete — but every line of the
// stream carries schema_version (measured: 130 of 130 in one packaged run),
// which is why the parser reads it off whichever event it sees first.
const char* const REAL_FIRST_LINE =
    R"({"schema_version":"1.3.0","event":"phase_complete","timestamp_ms":1784939843333,)"
    R"("slice_id":"slice-1784939842733","phase":"validation","status":"ok","elapsed_ms":3})";

// Record of everything the parser pushed to its update callback.
struct Updates
{
    std::vector<int>         percents;
    std::vector<std::string> texts;

    void attach(PnpProgressParser& p)
    {
        p.set_update_callback([this](int percent, const std::string& text) {
            percents.push_back(percent);
            texts.push_back(text);
        });
    }

    bool monotonic() const
    {
        for (size_t i = 1; i < percents.size(); ++i)
            if (percents[i] < percents[i - 1])
                return false;
        return true;
    }
};

std::string event_line(const std::string& body, const char* version = "1.3.0")
{
    return std::string(R"({"schema_version":")") + version + R"(",)" + body + "}";
}

} // namespace

TEST_CASE("progress parser maps phases to a monotonic percent", "[pnp][progress]")
{
    PnpProgressParser parser(/*estimated_layer_count=*/10);
    Updates updates;
    updates.attach(parser);

    SECTION("phase completions walk the documented percent model")
    {
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"validation","status":"ok")"));
        REQUIRE(parser.current_percent() == 5);

        parser.feed_line(event_line(R"("event":"phase_complete","phase":"prepass","status":"ok")"));
        REQUIRE(parser.current_percent() == 10);

        parser.feed_line(event_line(R"("event":"phase_complete","phase":"per_layer","status":"ok")"));
        REQUIRE(parser.current_percent() == 90);

        parser.feed_line(event_line(R"("event":"phase_complete","phase":"postpass","status":"ok")"));
        REQUIRE(parser.current_percent() == 99);

        parser.feed_line(event_line(R"("event":"slice_complete","status":"ok")"));
        REQUIRE(parser.current_percent() == 100);

        REQUIRE(updates.monotonic());
    }

    SECTION("percent never goes backwards when events arrive out of order")
    {
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"per_layer","status":"ok")"));
        REQUIRE(parser.current_percent() == 90);
        // A late validation completion must not drag the bar back to 5.
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"validation","status":"ok")"));
        REQUIRE(parser.current_percent() == 90);
        REQUIRE(updates.monotonic());
    }

    SECTION("per-layer percent stays inside the 10-90 band")
    {
        for (int i = 0; i < 10; ++i) {
            parser.feed_line(event_line(R"("event":"layer_start","phase":"per_layer","layer_index":)" +
                                        std::to_string(i)));
            REQUIRE(parser.current_percent() >= 10);
            REQUIRE(parser.current_percent() <= 90);
        }
        REQUIRE(updates.monotonic());
    }
}

TEST_CASE("progress parser prefers the stream's layer_count over the estimate", "[pnp][progress]")
{
    // The GUI estimate is deliberately wrong (100 vs the stream's 10) so the two
    // cannot be confused: at layer 5 of 10 the bar is halfway through the band,
    // whereas 5 of 100 would barely have moved.
    PnpProgressParser parser(/*estimated_layer_count=*/100);
    parser.feed_line(event_line(R"("event":"phase_start","phase":"per_layer","layer_count":10)"));
    parser.feed_line(event_line(R"("event":"layer_complete","phase":"per_layer","layer_index":4)"));

    // 5 of 10 layers done => 10 + 80/2 = 50.
    REQUIRE(parser.current_percent() == 50);
}

TEST_CASE("progress parser carries the plate label into every status text", "[pnp][progress]")
{
    PnpProgressParser parser(/*estimated_layer_count=*/4, "Plate 2/3: ");
    Updates updates;
    updates.attach(parser);

    parser.feed_line(event_line(R"("event":"phase_complete","phase":"validation","status":"ok")"));

    REQUIRE_FALSE(updates.texts.empty());
    for (const std::string& t : updates.texts)
        REQUIRE(t.rfind("Plate 2/3: ", 0) == 0);
}

TEST_CASE("progress parser is forward compatible with additive schema changes", "[pnp][progress]")
{
    PnpProgressParser parser(/*estimated_layer_count=*/4);
    parser.feed_line(REAL_FIRST_LINE);

    SECTION("an unknown event type is ignored, not counted as a parse error")
    {
        parser.feed_line(event_line(R"("event":"module_timing","module_id":"com.core.x","elapsed_ms":3)"));
        REQUIRE(parser.skipped_line_count() == 0);
        REQUIRE_FALSE(parser.has_fatal_error());
    }

    SECTION("unknown fields on a known event are ignored")
    {
        parser.feed_line(event_line(
            R"("event":"phase_complete","phase":"prepass","status":"ok","some_future_field":{"a":1})"));
        REQUIRE(parser.current_percent() == 10);
        REQUIRE(parser.skipped_line_count() == 0);
    }

    SECTION("garbage lines are skipped and counted, never fatal")
    {
        parser.feed_line("this is not json");
        parser.feed_line("{\"unterminated\": ");
        parser.feed_line("");
        REQUIRE(parser.skipped_line_count() > 0);
        REQUIRE_FALSE(parser.has_fatal_error());
        // A later good event still parses.
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"prepass","status":"ok")"));
        REQUIRE(parser.current_percent() == 10);
    }
}

TEST_CASE("progress parser gates the progress-schema major", "[pnp][progress][schema]")
{
    SECTION("the shipping version is accepted")
    {
        // Asserted against a line captured from a real pnp_cli run, so this fails
        // if the supported major and the version pnp actually ships diverge.
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(REAL_FIRST_LINE);
        REQUIRE(parser.schema_version() == "1.3.0");
        REQUIRE_FALSE(parser.has_fatal_error());
        REQUIRE(PnpBackend::SUPPORTED_PROGRESS_SCHEMA_MAJOR == 1);
    }

    SECTION("a minor bump within the supported major is accepted")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"validation","status":"ok")", "1.9.7"));
        REQUIRE_FALSE(parser.has_fatal_error());
    }

    SECTION("a major mismatch condemns the slice with a message naming both versions")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"validation","status":"ok")", "2.0.0"));
        REQUIRE(parser.has_fatal_error());
        REQUIRE(parser.fatal_error_message().find("2.0.0") != std::string::npos);
        REQUIRE(parser.fatal_error_message().find("1") != std::string::npos);
    }

    SECTION("a present-but-unparseable version condemns the slice")
    {
        // Cannot be shown to be same-major, so there is no benign reading of it.
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"validation","status":"ok")", "not-a-version"));
        REQUIRE(parser.has_fatal_error());
    }

    SECTION("an absent version is accepted, so one dropped field is not an outage")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(R"({"event":"phase_complete","phase":"validation","status":"ok"})");
        REQUIRE(parser.schema_version().empty());
        REQUIRE_FALSE(parser.has_fatal_error());
        REQUIRE(parser.current_percent() == 5);
    }

    SECTION("the gate reads the first version seen and does not re-fire")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(REAL_FIRST_LINE);
        parser.feed_line(event_line(R"("event":"phase_complete","phase":"prepass","status":"ok")", "2.0.0"));
        // schema_version is captured once; a later line cannot retroactively
        // change which version the stream was judged on.
        REQUIRE(parser.schema_version() == "1.3.0");
        REQUIRE_FALSE(parser.has_fatal_error());
    }
}

TEST_CASE("progress parser separates fatal errors from degraded warnings", "[pnp][progress]")
{
    SECTION("a validation_error is fatal and keeps its message")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(
            R"("event":"validation_error","stage":"validation",)"
            R"("error":{"message":"model is below the bed","suggestion":"move it up"})"));
        REQUIRE(parser.has_fatal_error());
        REQUIRE(parser.fatal_error_message().find("model is below the bed") != std::string::npos);
        REQUIRE(parser.fatal_error_message().find("move it up") != std::string::npos);
    }

    SECTION("a fatal module_error is fatal")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(
            R"("event":"module_error","module_id":"com.core.infill",)"
            R"("error":{"message":"infill exploded","fatal":true})"));
        REQUIRE(parser.has_fatal_error());
        REQUIRE(parser.warnings().empty());
    }

    SECTION("a non-fatal module_error is collected as a warning, not a failure")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(
            R"("event":"module_error","module_id":"com.core.support","layer_index":7,)"
            R"("error":{"message":"support skipped","fatal":false})"));
        REQUIRE_FALSE(parser.has_fatal_error());
        REQUIRE(parser.warnings().size() == 1);
        REQUIRE(parser.warnings()[0].module_id == "com.core.support");
        REQUIRE(parser.warnings()[0].layer_index == 7);
    }

    SECTION("slice_complete reporting fatal errors is fatal even though it completed")
    {
        PnpProgressParser parser(/*estimated_layer_count=*/4);
        parser.feed_line(event_line(R"("event":"slice_complete","status":"error","fatal_error_count":2)"));
        REQUIRE(parser.has_fatal_error());
    }
}

TEST_CASE("progress parser stores slice_stats verbatim for the legend", "[pnp][progress]")
{
    // F10 re-parses this line for weight and per-extruder volumes, so the parser
    // must hand it back byte-for-byte rather than a re-serialized copy.
    const std::string stats = event_line(
        R"("event":"slice_stats","status":"ok","gcode_weight_grams":5.3205319428798035,)"
        R"("layer_count":60,"extruded_volume_mm3":{"0":4290.751566838551})");

    PnpProgressParser parser(/*estimated_layer_count=*/4);
    parser.feed_line(REAL_FIRST_LINE);
    parser.feed_line(stats);

    REQUIRE(parser.slice_stats_json() == stats);
    REQUIRE_FALSE(parser.has_fatal_error());
}

TEST_CASE("progress parser reassembles lines split across pipe reads", "[pnp][progress]")
{
    // feed() receives arbitrary chunks from the async stderr pipe; a split in the
    // middle of an event must not lose or corrupt it.
    const std::string line = event_line(R"("event":"phase_complete","phase":"prepass","status":"ok")");
    const std::string stream = line + "\n";

    PnpProgressParser parser(/*estimated_layer_count=*/4);
    const size_t split = stream.size() / 2;
    parser.feed(stream.data(), split);
    parser.feed(stream.data() + split, stream.size() - split);
    parser.finish();

    REQUIRE(parser.current_percent() == 10);
    REQUIRE(parser.skipped_line_count() == 0);
}

TEST_CASE("progress parser flushes a final line with no trailing newline", "[pnp][progress]")
{
    const std::string line = event_line(R"("event":"slice_complete","status":"ok")");

    PnpProgressParser parser(/*estimated_layer_count=*/4);
    parser.feed(line.data(), line.size());
    REQUIRE(parser.current_percent() != 100); // still buffered
    parser.finish();
    REQUIRE(parser.current_percent() == 100);
}
