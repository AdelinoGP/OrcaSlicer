---
title: Config-warnings sink — Tier-D default filter + jsonl writer
status: done
batch: B1
blocked-by: []
files: [src/slic3r/GUI/PnpConfigWarningsLog.hpp, src/slic3r/GUI/PnpConfigWarningsLog.cpp, src/slic3r/GUI/CMakeLists.txt]
---

## Goal

The write side of [ticket 013](../../tickets/013-config-warning-ux.md): take F01's warning vector,
filter, and persist. **No UI, ever** — warnings are a dev instrument.

> **Reopened past v1 (2026-08-10):** the "no UI" half of this ticket is reversed.
> `filter_pnp_config_warnings()` (this TU) now also feeds a
> `WarningNotificationLevel` notification pushed by `PnpSlicingProcess` at slice
> start, listing the affected keys via `format_pnp_config_warning_message()`.
> The jsonl sink below is unchanged and remains the full record; the
> notification is a summary. `no-op` records remain log-only.

## Decisions (do not re-open)

- Filter: **Tier-D (`not-yet-mapped`) records are dropped when the key sits at its default** —
  computed via existing `ConfigBase::diff()` against `FullPrintConfig::defaults()` (brings
  ~830 records/slice to <20). The filter is **Tier-D-only**: `lossy-fallback` always records
  (Orca's default `seam_position=spAligned` is itself the loss), as do `unsupported-feature`
  and `no-op`.
- Sink: append-only `<data_dir>/pnp-config-warnings.jsonl` (Slic3r `data_dir()`, **not** the
  per-slice temp dir F04 deletes) + **one summary line** in Orca's boost log per slice.
  No rotation.
- Caller is `PnpSlicingProcess` (F04) at slice start; this TU exposes
  `log_pnp_config_warnings(const DynamicPrintConfig& full, std::vector<PnpConfigWarning>&&)`.
  Define the `PnpConfigWarning` struct here (or a small shared header) so F01 and F03 compile
  independently in the same batch — coordinate: F01 declares the struct, F03 includes it; if
  racing, duplicate a forward header and reconcile at batch integration.

## Steps

1. jsonl record shape: `{ts, plate, key, class, orca_value, sent_value?}` — one object per line.
2. Default-diff filter via `diff()` (keys differing from defaults) intersected with Tier-D records.
3. Boost-log one-liner: counts per class.

## Done / verify

- `libslic3r_gui` builds clean.
- Feeding a default-config warning vector writes <25 lines, including the `seam_position`
  lossy-fallback; a config with a modified unmapped key adds that key's record.
