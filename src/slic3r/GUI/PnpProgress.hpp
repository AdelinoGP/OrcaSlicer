#pragma once

// PNP fork (wayfinder ticket F06): line-buffered parser for pnp_cli's stderr
// JSONL progress stream (pnp docs/09_progress_events.md, schema 1.x) into a
// phase-weighted percent + status text suitable for SlicingStatusEvent.
//
// Percent model (wayfinder ticket 006): 0-10 setup/load (validation+prepass),
// 10-90 per_layer scaled by layer_index against the best-known layer total
// (stream-provided layer_count when pnp ships it, else the GUI estimate passed
// to the constructor), 90-100 postpass/finish. Percent is clamped monotonic
// non-decreasing. Garbage lines are skipped; the child's exit code — not the
// stream — decides success. Degraded-slice warnings are collected, never
// surfaced per-event (F08 aggregates them at completion).
//
// Threading: feed()/feed_line() are called on the slicing worker thread. The
// parser itself posts no events; the caller (PnpSlicingProcess, F04) forwards
// updates to the UI thread.

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {

class PnpProgressParser
{
public:
    // One collected non-fatal (degraded) module warning from the stream.
    struct Warning
    {
        std::string module_id;
        std::string message;
        int         layer_index { -1 }; // -1 when not layer-scoped
    };

    // Called (on the feeding thread) whenever percent or status text advances.
    // percent is 0..100, monotonic non-decreasing per parser instance.
    using UpdateCallback = std::function<void(int percent, const std::string& text)>;

    // `estimated_layer_count` is the GUI-side fallback layer total (model
    // height / layer height, >= 1); used until/unless the stream provides a
    // real layer_count. `plate_label` (e.g. "Plate 2/3: ") is prefixed to
    // every status text when non-empty (slice-all, F09).
    explicit PnpProgressParser(int estimated_layer_count, std::string plate_label = std::string());

    void set_update_callback(UpdateCallback cb) { m_on_update = std::move(cb); }

    // Feed a raw chunk from the async stderr pipe; the parser splits it into
    // lines internally and processes every complete line.
    void feed(const char* data, size_t len);

    // Feed one complete line (no trailing newline required). Parse errors are
    // tolerated: an unparsable line is skipped and counted, never fatal.
    void feed_line(const std::string& line);

    // Flush any buffered final partial line (call once at EOF).
    void finish();

    // --- results, valid on the worker thread after the child exits ---

    int current_percent() const { return m_percent; }

    // schema_version from the first parsed event (empty until then); the
    // caller gates on semver major against PnpBackend::SUPPORTED_CONFIG_SCHEMA_MAJOR.
    const std::string& schema_version() const { return m_schema_version; }

    // Collected degraded-slice / non-fatal module warnings, in stream order.
    const std::vector<Warning>& warnings() const { return m_warnings; }

    // True when the stream reported a fatal error (module_error with
    // fatal=true, validation_error, or slice_complete with fatal errors).
    bool has_fatal_error() const { return m_fatal; }

    // First fatal error seen, formatted "message — suggestion (stage: X)" with
    // empty parts omitted (F08); empty when has_fatal_error() is false.
    const std::string& fatal_error_message() const { return m_fatal_message; }

    // Raw JSON text of the slice_stats event when pnp ships it (handoff item
    // 2); empty until then. Stored verbatim, consumed by F10.
    const std::string& slice_stats_json() const { return m_slice_stats_json; }

    // Number of lines that failed to parse and were skipped.
    int skipped_line_count() const { return m_skipped_lines; }

private:
    void emit_update(const std::string& text);

    UpdateCallback       m_on_update;
    std::string          m_line_buffer;
    std::string          m_plate_label;
    int                  m_estimated_layer_count { 1 };
    int                  m_stream_layer_count { 0 }; // 0 = not provided by stream
    int                  m_percent { 0 };
    std::string          m_schema_version;
    std::vector<Warning> m_warnings;
    bool                 m_fatal { false };
    std::string          m_fatal_message;
    std::string          m_slice_stats_json;
    int                  m_skipped_lines { 0 };
    // Last (percent, text) forwarded to m_on_update; updates fire only on change.
    std::pair<int, std::string> m_last_emitted { -1, std::string() };
};

} // namespace GUI
} // namespace Slic3r
