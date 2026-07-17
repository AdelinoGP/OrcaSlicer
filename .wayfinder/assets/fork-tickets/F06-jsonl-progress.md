---
title: JSONL progress parser + phase-weighted percent
status: open
batch: B2
blocked-by: []
files: [src/slic3r/GUI/PnpProgress.hpp, src/slic3r/GUI/PnpProgress.cpp, src/slic3r/GUI/CMakeLists.txt]
---

## Goal

Parse pnp_cli's stderr JSONL progress stream (pnp `docs/09_progress_events.md`, schema 1.x) into
percent + status text for `SlicingStatusEvent`, per
[ticket 006](../../tickets/006-progress-cancel-error-ux.md).

## Decisions (do not re-open)

- Percent = **phase-weighted 0–10–90–100**: 0–10 setup/load, 10–90 per-layer scaled by
  `layer_index` against a **GUI-estimated layer total** (model height ÷ layer height), 90–100
  emit/finish. When pnp ships `layer_count` in the stream (handoff item 12 / `slice_stats`),
  prefer it over the estimate — code the fallback chain now.
- **Stream parse errors are tolerated** (skip the line); the child's exit code decides success.
- Per-slice `schema_version` from the stream is checked against F02's gate (semver major).
- Slice-all: percent stays per-plate 0–100 with "plate N/M" prefixed in the status text
  (Plater loop is F09; this TU just accepts an optional plate-label).
- Degraded-slice events (module warnings) are **collected**, not surfaced per-event — F08
  aggregates them into one completion notification.

## Steps

1. Line-buffered parser fed by F04's async stderr pipe (interface: callback or queue consumed on
   the worker thread; wx events posted to the UI thread).
2. Event taxonomy: `phase_start`/`phase_end`/per-layer progress/warning/error/`slice_stats`
   (accept-and-store even while pnp doesn't emit it yet).
3. Weighting + layer-estimate fallback; clamp monotonic non-decreasing percent.

## Done / verify

- `libslic3r_gui` builds clean.
- Replaying a captured pnp_cli stderr log through the parser yields monotonic 0→100 and collects
  warnings; a garbage line in the middle is skipped without aborting.
