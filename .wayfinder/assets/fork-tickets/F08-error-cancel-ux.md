---
title: Error, degraded-slice, and cancel UX + Job Object orphan guard
status: open
batch: B3
blocked-by: [F04, F06]
files: [src/slic3r/GUI/PnpSlicingProcess.cpp, src/slic3r/GUI/PnpProgress.cpp, src/slic3r/GUI/NotificationManager.cpp]
---

## Goal

The UX half of [ticket 006](../../tickets/006-progress-cancel-error-ux.md) on top of F04's
mechanism and F06's collected events.

## Decisions (do not re-open)

- **Degraded slices** (pnp warning events, slice still succeeds) → **one aggregated warning
  notification on completion**, not per-event toasts.
- **Fatal errors** → dialog/notification with pnp's message + suggestion + stage; the stderr
  tail goes to Orca's log only.
- **Orphan guard: Windows Job Object with kill-on-close** — assign the child at spawn so a GUI
  crash cannot leak pnp_cli processes.
- **No hang watchdog in v1.** Cancel = kill (F04); no graceful path until pnp handoff item 11.
- Exit code decides success; stream parse errors never fail a slice.

## Steps

1. Job Object creation + `AssignProcessToJobObject` in F04's spawn path
   (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`); no-op on non-Windows for now.
2. Warning aggregation: on completion with exit 0 and collected warnings, one
   `NotificationManager` warning listing count + first few messages.
3. Fatal path: map the JSONL `error` event (message/suggestion/stage) into the `SlicingError`
   F04 throws; ensure the completion handler renders it like native slicing errors.

## Done / verify

- `libslic3r_gui` builds clean.
- Batch smoke: kill-tolerance (cancel mid-slice leaves no pnp_cli.exe in Task Manager; closing
  the app mid-slice also leaves none); a config forced to fail (bad module dir) shows the fatal
  message, app stays usable.
