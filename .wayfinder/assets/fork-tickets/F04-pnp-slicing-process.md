---
title: PnpSlicingProcess core — worker thread, boost::process, temp dirs, reuse
status: open
batch: B2
blocked-by: [F01, F02, F03]
files: [src/slic3r/GUI/PnpSlicingProcess.hpp, src/slic3r/GUI/PnpSlicingProcess.cpp, src/slic3r/GUI/CMakeLists.txt]
---

## Goal

The seam: a new class replacing `BackgroundSlicingProcess` wholesale while **preserving BSP's
public surface**, per [ticket 003](../../tickets/003-slicing-seam-replacement-design.md) (read its
resolution in full before starting). This ticket builds the class standalone; the Plater swap is
F09.

## Decisions (do not re-open)

- Public API mirrors BSP minus SLA: `apply/start/stop/reset`, `set_*_event` ids, `running()`,
  `schedule_export`, state queries. Existing wx events (`SlicingStatusEvent`,
  `SlicingProcessCompletedEvent`, export events) unchanged.
- `Print::apply()` retained as change detection: `apply()` calls `m_print->apply(model, config)`,
  `ApplyStatus` semantics as today. `Print::process()` never called.
- **One worker thread per slice** (no persistent thread / condition-variable machine):
  export inputs → spawn pnp_cli → pump JSONL stderr → wait → ingest → post completion.
  States: IDLE → STARTED at `start()`, RUNNING once spawned, FINISHED/CANCELED at thread end.
- Launcher: `boost::process`, async stderr pipe, `CREATE_NO_WINDOW` on Windows. cli path/module
  dir from F02's `PnpBackend`.
- Inputs: per-slice subdir under app temp dir (model file + `config.json` from F01 via F03's
  logging call); deleted on success, retained on failure. Output G-code to
  `PartPlate::get_tmp_gcode_path()`.
- Reuse: skip subprocess when `PartPlate::is_slice_result_valid()` && tmp gcode exists &&
  `APPLY_STATUS_UNCHANGED`.
- v1 cancel: `child.terminate()` + delete partial output; internal cancel posts no event.
  (Job Object orphan guard and error UX are F08; graceful cancel is pnp handoff item 11.)
- Failures throw `SlicingError` carrying pnp message + stderr tail, delivered via
  `exception_ptr` on the completion event — identical to native error flow.

## Steps

1. Class skeleton + state machine + thread lifecycle; compile against existing BSP call-site
   signatures (copy the header surface, drop SLA/`m_sla_print`).
2. Spawn/pump/wait path calling F06's parser for progress events (stub the parser interface if
   F06 lands in parallel — same batch; reconcile at integration).
3. Model export call delegates to F05's per-plate exporter (same stub rule).
4. Ingestion calls the F07 path (B3); until then, finishing a slice may post completion with the
   gcode path only — keep the seam compiling.

## Done / verify

- `libslic3r_gui` builds clean with the new TU; BSP still compiles untouched (swap is F09).
- Batch B2 smoke (driving session): headless-ish check — instantiate, `apply`, `start` against a
  loaded plate, confirm pnp_cli runs and gcode lands at `get_tmp_gcode_path()`.
