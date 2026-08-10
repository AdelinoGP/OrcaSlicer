---
title: Progress, cancel, and error UX
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: [003]
---

## Question

How do PNP's `--instrument-stderr` JSONL progress events map onto `SlicingStatusEvent` and the existing progress UI? How is the subprocess cancelled cleanly (user hits stop, closes app, changes model mid-slice)? How are slicing errors from pnp_cli surfaced? How is progress aggregated across the per-plate loop in "Slice all"?

## Resolution

Grilled 2026-07-17. All progress/cancel/error UX decisions for the `PnpSlicingProcess` seam (ticket 003):

**Progress percent — GUI-estimated layer total, phase-weighted.** pnp_cli's JSONL stream carries `layer_index` but no total layer count (the `slice_stats` event with `layer_count` is reserved schema 1.2.0, unshipped). The GUI estimates total layers from plate model height ÷ layer height (it holds the config) and maps phases onto the existing 0–100 percent: validation+prepass → 0–10%, `per_layer` → 10–90% scaled by `layer_index / estimate` (clamped), postpass → 90–100%. Events feed `SlicingStatusEvent` → `NotificationManager::set_slicing_progress_percentage` and `PartPlate::update_slicing_percent`, unchanged. **New pnp handoff item:** add `layer_count` to `phase_start(per_layer)` (or ship `slice_stats`) so the estimate becomes exact.

**Degraded slices — one aggregated warning on completion.** Non-fatal `module_error` events are collected during the slice; on `slice_complete(degraded=true)` push a single `WarningNotificationLevel` notification ("Slice completed with N warnings" + leading message(s)); per-event detail goes to the log. This satisfies PNP's error-visibility contract without Orca's ObjectID/warning_step plumbing, which doesn't apply to subprocess errors.

**Fatal errors — structured message + suggestion.** The `SlicingError` (ticket 003 mechanism) surfaces `error.message`, `error.suggestion`, and the failing module/stage name in the error notification; the raw stderr tail goes only to the Orca log file, and the notification mentions the log path.

**"Slice all" — per-plate 0–100% + plate label.** Each plate's run shows its own 0–100% with text "Slicing plate N/M…", matching Orca's existing per-plate `slicing_percent` mechanism and the unchanged Plater loop. No new aggregation.

**Orphan guard — Windows Job Object.** At spawn (boost::process extension hook) the child is assigned to a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, so pnp_cli dies with the GUI even on crash; Plater's shutdown path additionally calls `stop()` (terminate). POSIX: terminate-on-stop suffices for v1.

**Hangs — no watchdog in v1.** Cancel (kill child) is always available; pnp_cli is stateless/local, so a stale-event timeout adds tuning risk (slow layers on large plates) for little gain. Revisit if hangs appear.

**Stream robustness — tolerate, judge by exit code.** Malformed/unknown JSONL lines are logged and skipped (progress may degrade to coarse); slice success/failure is decided solely by exit code + output-file existence, never parser fidelity. Major schema-version mismatch logs a one-time warning; version handshake proper is ticket 009.

Cancel semantics themselves (kill child + delete partial output; graceful cancel as pnp handoff) were fixed in ticket 003 and stand unchanged; model-change-mid-slice keeps Orca's stop-and-restart behavior via the preserved `apply()`/`stop()` surface.
