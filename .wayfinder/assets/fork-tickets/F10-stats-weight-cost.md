---
title: Stats — weight from slice_stats, fork-side cost, legend zero-guards
status: open
batch: B4
blocked-by: [F06, F07, F09]
files: [src/slic3r/GUI/PnpSlicingProcess.cpp, src/slic3r/GUI/GCodeViewer.cpp, src/libslic3r/Print.hpp]
---

## Goal

Make weight/cost correct in the preview legend and send/upload dialogs, per
[ticket 011](../../tickets/011-thumbnails-and-gcode-metadata.md). The export path that filled
`Print::m_print_statistics` dies in M2, and `render_legend` reads weight **unguarded** (`0.00 g`).

## Decisions (do not re-open)

- **Weight comes from pnp's `slice_stats` JSONL event** (handoff item 2, amended field list:
  weight, filament length, per-extruder volumes, toolchanges — no cost field). F06 already
  stores the event; this ticket routes it into `Print::PrintStatistics` (or the
  `GCodeProcessorResult` consumer, whichever `render_legend`/send dialogs read — trace first).
- **Cost is computed fork-side** from Orca's preset (`filament_cost`, `time_cost`) over pnp's
  volume — pnp does not import Orca pricing.
- Until item 2 lands: guard the weight/cost legend rows the way time already is
  (hidden at 0), so nothing renders `0.00 g`. The guard stays permanently (drag-in of foreign
  G-code hits it too).
- Times/volumes need no work — Orca's estimator computes them from moves (007), accuracy rides
  pnp item 10.

## Steps

1. Trace `render_legend`'s weight/cost source (`GCodeViewer.cpp`) and the send-dialog reads;
   confirm which struct they consume post-M2.
2. Zero-guards first (safe regardless of pnp timing); then the `slice_stats` → stats plumbing +
   cost computation.
3. B4 = **M1 close**: driving session runs the full smoke ritual + the M1 gating check
   (pnp items 10 and 2 landed).

## Done / verify

- `ALL_BUILD` clean; smoke: legend shows weight+cost when `slice_stats` present, hides both when
  absent — never `0.00 g`.
