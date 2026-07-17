---
title: Preview ingestion of PNP G-code
status: done
batch: B3
blocked-by: [F04]
files: [src/slic3r/GUI/PnpSlicingProcess.cpp, src/libslic3r/GCode/GCodeProcessor.cpp]
---

## Goal

Fill the plate's `GCodeProcessorResult` from the pnp-emitted G-code file so `GUI_Preview` /
`GLCanvas3D` render it, per [ticket 007](../../tickets/007-preview-ingestion-of-pnp-gcode.md)
and its [matrix asset](../007-preview-ingestion-of-pnp-gcode.md).

## Decisions (do not re-open)

- Path: `GCodeProcessor::process_file(<tmp gcode path>)` on the worker thread, result moved into
  the plate's `GCodeProcessorResult` before posting completion (007 verified roles, layers, and
  the CONFIG_BLOCK all parse).
- **MUST force `GCodeProcessor::s_IsBBLPrinter = false` on this ingestion path** — it defaults
  to `true` and PNP emits no `printer_model`; the BBL tag table would collapse the preview.
  (pnp handoff item 10 later makes drag-in safe too; the fork-side force stays regardless.)
- Keep `finalize(false)` — the fork never runs Orca post-processing / M73 injection
  (pnp emits M73 itself, handoff item 15).
- Time/filament stats come from Orca's own estimator over the parsed moves; accuracy rides pnp
  item 10's `machine_max_*` passthrough — no fork-side compensation.
- `process_file`'s `EProducer::OrcaSlicer` pre-pass **discards pre-applied config** (ticket 011
  discovery) — do not attempt to hand the processor the in-memory preset; everything arrives via
  the file's CONFIG_BLOCK.

## Steps

1. Ingestion step in F04's worker after child exit 0: construct processor, force the BBL flag
   off, `process_file`, move result, post completion.
2. Confirm the preview's legend role list matches a native-Orca slice of the same model
   (007's annotation matrix is the reference).

## Done / verify

- `libslic3r_gui` + `libslic3r` build clean.
- Batch smoke: slice via PNP → preview shows toolpaths, per-role colors, layer slider, and a
  nonzero time estimate; no BBL-specific legend artifacts.
