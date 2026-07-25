#include "PnpProgress.hpp"

#include "PnpBackend.hpp"

#include "libslic3r/Semver.hpp"
#include "libslic3r/format.hpp"

#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace Slic3r {
namespace GUI {

namespace {

// Phase-weighted percent model (wayfinder ticket 006):
//   0 -- 10   validation + prepass ("setup/load")
//  10 -- 90   per_layer, scaled by layer_index against the layer total
//  90 -- 100  postpass through slice_complete
constexpr int PERCENT_VALIDATION_DONE = 5;
constexpr int PERCENT_PREPASS_DONE    = 10;
constexpr int PERCENT_PER_LAYER_BASE  = 10;
constexpr int PERCENT_PER_LAYER_SPAN  = 80;
constexpr int PERCENT_PER_LAYER_DONE  = 90;
constexpr int PERCENT_POSTPASS_DONE   = 99;
constexpr int PERCENT_COMPLETE        = 100;

// Percent for having completed `completed` of `total` layers.
int per_layer_percent(int completed, int total)
{
    total = std::max(total, 1);
    completed = std::clamp(completed, 0, total);
    return PERCENT_PER_LAYER_BASE + PERCENT_PER_LAYER_SPAN * completed / total;
}

// Non-throwing accessors: the stream is untrusted, tolerate any shape.
std::string get_string(const nlohmann::json& j, const char* key)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

int get_int(const nlohmann::json& j, const char* key, int def = -1)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<int>() : def;
}

bool get_bool(const nlohmann::json& j, const char* key, bool def = false)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

// F08: "message — suggestion (stage: X)", omitting empty parts.
std::string format_fatal(std::string message, const std::string& suggestion, const std::string& stage)
{
    if (message.empty())
        message = "Slicing failed";
    if (!suggestion.empty())
        message += " \xE2\x80\x94 " + suggestion; // " — "
    if (!stage.empty())
        message += " (stage: " + stage + ")";
    return message;
}

} // anonymous namespace

PnpProgressParser::PnpProgressParser(int estimated_layer_count, std::string plate_label)
    : m_plate_label(std::move(plate_label))
    , m_estimated_layer_count(std::max(estimated_layer_count, 1))
{
}

void PnpProgressParser::feed(const char* data, size_t len)
{
    if (data == nullptr || len == 0)
        return;
    m_line_buffer.append(data, len);
    for (size_t pos = m_line_buffer.find('\n'); pos != std::string::npos; pos = m_line_buffer.find('\n')) {
        std::string line = m_line_buffer.substr(0, pos);
        m_line_buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        feed_line(line);
    }
}

void PnpProgressParser::finish()
{
    if (m_line_buffer.empty())
        return;
    std::string line;
    line.swap(m_line_buffer);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    feed_line(line);
}

void PnpProgressParser::feed_line(const std::string& line)
{
    // Blank lines are noise, not parse errors.
    if (line.find_first_not_of(" \t\r") == std::string::npos)
        return;

    nlohmann::json j = nlohmann::json::parse(line, /* callback */ nullptr, /* allow_exceptions */ false);
    if (j.is_discarded() || !j.is_object()) {
        ++m_skipped_lines;
        return;
    }
    const std::string event = get_string(j, "event");
    if (event.empty()) {
        ++m_skipped_lines;
        return;
    }

    if (m_schema_version.empty()) {
        m_schema_version = get_string(j, "schema_version");
        if (!m_schema_version.empty())
            check_schema_version();
    }

    // Best-known layer total: stream-provided layer_count wins over the GUI
    // estimate (pnp handoff item 12: layer_count on phase_start(per_layer) or
    // slice_stats — accept it wherever it shows up).
    if (int lc = get_int(j, "layer_count", 0); lc > 0)
        m_stream_layer_count = lc;
    const int layer_total = m_stream_layer_count > 0 ? m_stream_layer_count : m_estimated_layer_count;

    const std::string phase = get_string(j, "phase");

    if (event == "phase_start") {
        if (phase == "validation")
            emit_update("Validating…");
        else if (phase == "prepass")
            emit_update("Preparing…");
        else if (phase == "per_layer")
            emit_update("Slicing layers…");
        else if (phase == "postpass") {
            m_percent = std::max(m_percent, PERCENT_PER_LAYER_DONE);
            emit_update("Finalizing…");
        }
    } else if (event == "phase_complete") {
        if (phase == "validation") {
            m_percent = std::max(m_percent, PERCENT_VALIDATION_DONE);
            emit_update("Preparing…");
        } else if (phase == "prepass") {
            m_percent = std::max(m_percent, PERCENT_PREPASS_DONE);
            emit_update("Slicing layers…");
        } else if (phase == "per_layer") {
            m_percent = std::max(m_percent, PERCENT_PER_LAYER_DONE);
            emit_update("Finalizing…");
        } else if (phase == "postpass") {
            m_percent = std::max(m_percent, PERCENT_POSTPASS_DONE);
            emit_update("Finalizing…");
        }
    } else if (event == "layer_start") {
        const int idx = get_int(j, "layer_index", -1);
        if (idx >= 0) {
            m_percent = std::max(m_percent, per_layer_percent(idx, layer_total));
            emit_update("Slicing layer " + std::to_string(std::min(idx + 1, layer_total)) + "/" +
                        std::to_string(layer_total));
        }
    } else if (event == "layer_complete") {
        const int idx = get_int(j, "layer_index", -1);
        if (idx >= 0) {
            m_percent = std::max(m_percent, per_layer_percent(idx + 1, layer_total));
            emit_update("Slicing layer " + std::to_string(std::min(idx + 1, layer_total)) + "/" +
                        std::to_string(layer_total));
        }
        // degraded=true carries no message of its own; the matching
        // module_error event is what gets collected as a warning.
    } else if (event == "module_error") {
        const auto err_it = j.find("error");
        const bool has_error = err_it != j.end() && err_it->is_object();
        const bool fatal = has_error ? get_bool(*err_it, "fatal", false)
                                     : get_string(j, "status") == "fatal_error";
        std::string message = has_error ? get_string(*err_it, "message") : std::string();
        if (fatal) {
            m_fatal = true;
            if (m_fatal_message.empty())
                m_fatal_message = format_fatal(message.empty() ? std::string("Module error") : message,
                                               has_error ? get_string(*err_it, "suggestion") : std::string(),
                                               get_string(j, "stage"));
        } else {
            Warning w;
            w.module_id   = get_string(j, "module_id");
            w.message     = std::move(message);
            w.layer_index = get_int(j, "layer_index", -1);
            m_warnings.emplace_back(std::move(w));
        }
    } else if (event == "validation_error") {
        m_fatal = true;
        if (m_fatal_message.empty()) {
            const auto err_it = j.find("error");
            const bool  has_error = err_it != j.end() && err_it->is_object();
            std::string message   = has_error ? get_string(*err_it, "message") : std::string();
            m_fatal_message = format_fatal(message.empty() ? std::string("Validation error") : message,
                                           has_error ? get_string(*err_it, "suggestion") : std::string(),
                                           get_string(j, "stage"));
        }
    } else if (event == "slice_complete") {
        if (get_int(j, "fatal_error_count", 0) > 0) {
            m_fatal = true;
            if (m_fatal_message.empty())
                m_fatal_message = "Slicing failed";
        }
        m_percent = PERCENT_COMPLETE;
        emit_update("Slicing complete");
    } else if (event == "slice_stats") {
        // Reserved schema 1.2.0 (pnp handoff item 2): accept-and-store the raw
        // line verbatim for F10; layer_count (if present) was captured above.
        m_slice_stats_json = line;
    } else {
        // Unknown event types (e.g. 1.3.0 instrumented stream) are additive:
        // ignore, do not count as parse errors.
        BOOST_LOG_TRIVIAL(trace) << "pnp progress: ignoring event type " << event;
    }
}

void PnpProgressParser::check_schema_version()
{
    // Per-slice half of the version handshake (wayfinder ticket 009). The startup probe gates
    // the CONFIG schema; this stream carries the PROGRESS schema, an independent semver line
    // that moves on its own -- so a pnp bumping only the progress major would otherwise sail
    // through the probe and then drive the progress bar and the legend from events this build
    // cannot read. Same-major = accept, per pnp's compatibility rule.
    const boost::optional<Semver> ver = Semver::parse(m_schema_version);
    if (!ver) {
        // Present but not semver. That is a contract violation with no benign reading -- an
        // unparseable version cannot be shown to be same-major -- so refuse the stream.
        m_fatal = true;
        if (m_fatal_message.empty())
            m_fatal_message = Slic3r::format(
                "pnp_cli sent an unreadable progress-stream schema version (\"%1%\"); "
                "this build supports major version %2%.",
                m_schema_version, PnpBackend::SUPPORTED_PROGRESS_SCHEMA_MAJOR);
        BOOST_LOG_TRIVIAL(error) << "pnp progress: unparseable schema_version \"" << m_schema_version << "\"";
        return;
    }
    if (ver->maj() != PnpBackend::SUPPORTED_PROGRESS_SCHEMA_MAJOR) {
        m_fatal = true;
        if (m_fatal_message.empty())
            m_fatal_message = Slic3r::format(
                "pnp_cli sends progress-stream schema version %1%, but this build supports "
                "major version %2%.",
                m_schema_version, PnpBackend::SUPPORTED_PROGRESS_SCHEMA_MAJOR);
        BOOST_LOG_TRIVIAL(error) << "pnp progress: schema major mismatch, stream=" << m_schema_version
                                 << ", supported major=" << PnpBackend::SUPPORTED_PROGRESS_SCHEMA_MAJOR;
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "pnp progress: stream schema_version=" << m_schema_version << " accepted";
}

void PnpProgressParser::emit_update(const std::string& text)
{
    std::string full = m_plate_label.empty() ? text : m_plate_label + text;
    // Only notify when percent or text actually changed.
    if (m_percent == m_last_emitted.first && full == m_last_emitted.second)
        return;
    m_last_emitted = { m_percent, full };
    if (m_on_update)
        m_on_update(m_percent, full);
}

} // namespace GUI
} // namespace Slic3r
