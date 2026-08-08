#pragma once

// PNP fork (wayfinder ticket F06): line-buffered parser for pnp_cli's stderr
// JSONL progress stream (pnp docs/09_progress_events.md, schema 1.x) into a
// phase-weighted percent + status text suitable for SlicingStatusEvent.
//
// Percent model (wayfinder ticket 006): 0-10 setup/load (validation+prepass),
// 10-90 per_layer scaled by layer_index against the best-known layer total
// (stream-provided layer_count when pnp ships it, else the GUI estimate passed
// to the constructor), 90-100 postpass/finish. Percent is clamped monotonic
// non-decreasing. Garbage lines are skipped. Success is decided by the child's
// exit code OR by has_fatal_error(): the stream can condemn a slice on its own
// (a fatal module_error, a validation_error, or an unusable schema_version),
// and PnpSlicingProcess treats either signal as failure. Degraded-slice
// warnings are collected, never surfaced per-event (F08 aggregates them at
// completion).
//
// Threading: feed()/feed_line() are called on the slicing worker thread. The
// parser itself posts no events; the caller (PnpSlicingProcess, F04) forwards
// updates to the UI thread.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {

// PNP fork (ADR-0002): per-global-plate-layer slice state, derived from the
// layer_start / layer_complete events. Values are stable wire-agnostic states;
// the UI maps them to colors (plus a UI-only "failed" state on slice failure).
enum class LayerStatus : uint8_t
{
    Pending    = 0,
    InProgress = 1,
    Complete   = 2,
    Degraded   = 3,
};

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
    // Called (on the feeding thread) whenever the per-layer status array
    // changes (resized or a layer changed state). The consumer decides how to
    // coalesce; the parser itself never posts events.
    using LayerStatusCallback = std::function<void()>;

    // `estimated_layer_count` is the GUI-side fallback layer total (model
    // height / layer height, >= 1); used until/unless the stream provides a
    // real layer_count. `plate_label` (e.g. "Plate 2/3: ") is prefixed to
    // every status text when non-empty (slice-all, F09).
    explicit PnpProgressParser(int estimated_layer_count, std::string plate_label = std::string());

    void set_update_callback(UpdateCallback cb) { m_on_update = std::move(cb); }
    void set_layer_status_callback(LayerStatusCallback cb) { m_on_layer_status = std::move(cb); }

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

    // schema_version from the first event that carries one (empty until then).
    // The parser itself gates its major against
    // PnpBackend::SUPPORTED_PROGRESS_SCHEMA_MAJOR -- note that is the PROGRESS
    // line, not the config line the startup probe checks; they are independent
    // and move separately. A mismatch, or a value that is present but not
    // semver, is reported as a fatal error (the slice then fails through F08's
    // normal path). An absent schema_version is accepted and logged: absence is
    // not evidence of incompatibility, and refusing it would turn one dropped
    // field upstream into a total slicing outage.
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

    // --- per-layer status (ADR-0002), valid on the feeding thread ---

    // Best-known total of global plate layers: the stream-provided layer_count
    // once phase_start(per_layer) delivers it, else the GUI estimate. 0 until
    // the per-layer phase begins.
    int layer_count() const { return m_layer_count; }
    // One LayerStatus per layer, sized to layer_count(). Empty until the
    // per-layer phase begins.
    const std::vector<LayerStatus>& layer_status() const { return m_layer_status; }

private:
    // Evaluates m_schema_version against the supported progress-schema major.
    // Called once, when the first schema_version in the stream is captured.
    void check_schema_version();
    void emit_update(const std::string& text);
    // Fire m_on_layer_status (if set).
    void notify_layer_status();
    // Grow m_layer_status to at least `count` entries (Pending), adopting
    // `count` as the best-known layer total. Fires notify_layer_status() when
    // the array actually changed.
    void ensure_layer_status(int count);

    UpdateCallback       m_on_update;
    LayerStatusCallback  m_on_layer_status;
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
    // ADR-0002 per-layer status; see accessors above.
    std::vector<LayerStatus> m_layer_status;
    int                      m_layer_count { 0 };
};

} // namespace GUI
} // namespace Slic3r
