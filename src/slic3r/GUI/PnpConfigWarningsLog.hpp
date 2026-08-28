#pragma once

// PNP fork: write side of the config-warnings channel (wayfinder ticket 013,
// fork ticket F03). Takes the warning vector produced by PnpConfigTranslator
// (F01), filters not-yet-mapped records whose key sits at its Orca default,
// and appends one JSON object per surviving record to
// <data_dir>/pnp-config-warnings.jsonl, plus one summary line in the boost
// log. The same filter feeds the GUI warning notification (PnpSlicingProcess):
// the dev-instrument log keeps no-op records, the UI drops them.

#include <string>
#include <vector>

#include "PnpConfigWarning.hpp"
#include "PnpConfigTranslator.hpp"

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

// Shared filter for the translator's warning vector. Drops not-yet-mapped
// records whose key sits at its Orca default — full_config() returns every
// key with defaults filled in, so without this filter every one of the ~830
// not-yet-mapped keys would be reported on every slice (ticket 013). The
// filter is Tier-D-only: lossy-fallback records are only emitted when the
// translator actually took a fallback branch (and Orca's default
// seam_position=spAligned is itself the loss), and unsupported-feature
// records always pass. `include_no_op` keeps no-op records for the
// dev-instrument log; the UI passes false, because a by-design no-op has no
// behavioral loss and is noise to a user.
std::vector<PnpConfigWarning> filter_pnp_config_warnings(const DynamicPrintConfig&      full,
                                                         const std::vector<PnpConfigWarning>& warnings,
                                                         bool include_no_op);

// Labels for format_pnp_config_warning_message, translated at the call site:
// the formatter itself is wx-free so the GUI-free test binary can exercise it.
struct PnpConfigWarningLabels
{
    std::string title;       // e.g. "PNP config warnings:"
    std::string unsupported; // e.g. "not supported by PNP"
    std::string lossy;       // e.g. "sent with substituted value"
    std::string unmapped;    // e.g. "not mapped to PNP"
    std::string unresolved;  // e.g. "kept but not understood by this build"
};

// One notification message for the surviving warnings, grouped by class:
//   PNP config warnings:
//   - not supported by PNP: raft_layers, gcode_flavor
//   - sent with substituted value: seam_position (aligned → nearest)
//   - not mapped to PNP: wall_loops
// Each list is capped at 5 keys ("and N more" beyond that); the full record
// stays in pnp-config-warnings.jsonl.
std::string format_pnp_config_warning_message(const std::vector<PnpConfigWarning>& warnings,
                                              const PnpConfigWarningLabels&        labels);

// `full` is the resolved full config for the slice (all keys present, defaults
// filled in — e.g. preset_bundle->full_config()); it is used only to compute
// the changed-from-default key set for the not-yet-mapped filter.
// `plate` is the 0-based plate index of the slice; pass -1 if not applicable.
void log_pnp_config_warnings(const DynamicPrintConfig&      full,
                             std::vector<PnpConfigWarning>&& warnings,
                             int                             plate = -1);

// SchemaBridgeMap ticket 03: load-time sink for keys this build could not resolve.
// Appends one unresolved-preserved record per key to the same jsonl, tagged with
// `source` (the file or preset the keys came from) so the log says where they were
// found. Separate entry point from log_pnp_config_warnings because a load has no
// slice and therefore no resolved full config to filter against.
void log_pnp_unresolved_config_keys(const std::vector<std::string>& keys,
                                    const std::string&              source);

// One notification message for `keys`, capped the same way the slice-time formatter
// caps its lists. Named keys only: pnp module keys are not namespaced, the 3mf and
// preset formats store keys rather than module ids, and an unresolved key may equally
// be a retired Orca key or another producer's artifact -- so the message cannot
// honestly name a module, or even claim the keys are pnp's.
std::string format_pnp_unresolved_keys_message(const std::vector<std::string>& keys,
                                               const std::string&              title);

// SchemaBridgeMap ticket 06: probe-time sink for curated-table rows whose pnp-side
// target the live backend no longer declares. Its own record shape -- this is a
// fork-health event ("this build's translation table is out of date"), not a
// per-slice config warning, so it carries no warning class and no value, and it
// is keyed on `event` rather than `class` so a reader can tell the two apart in
// the one jsonl. Also emits one boost-log line.
void log_pnp_dead_curated_targets(const std::vector<PnpDeadTarget>& dead);

// One notification message for `dead`, capped the same way the other formatters
// cap their lists. Names the Orca setting -- the thing a reader can find in the
// UI -- with the dead pnp target in parentheses.
std::string format_pnp_dead_targets_message(const std::vector<PnpDeadTarget>& dead,
                                            const std::string& title);

}} // namespace Slic3r::GUI
