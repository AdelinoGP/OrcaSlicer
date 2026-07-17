---
title: Slicing-seam replacement design
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: [001]
---

## Question

What replaces `BackgroundSlicingProcess::process_fff`? Design the subprocess seam: pnp_cli process lifecycle, temp-file handoff (model + config in, gcode out), the per-plate loop for "Slice all", where in the existing state machine (`STATE_IDLE/STARTED/RUNNING/FINISHED`) the swap happens, and how much of `Plater`'s driving code stays unchanged vs. modified. A `/prototype` stub of the new process class may be warranted.

## Resolution

**Swap altitude — replace `BackgroundSlicingProcess` entirely with a new class (`PnpSlicingProcess`) that preserves BSP's public surface.** The new class reimplements BSP's public API (`apply/start/stop/reset`, `set_*_event` ids, `running()`, `schedule_export`, state queries) minus all SLA paths, so `Plater`'s ~50 call sites reduce to essentially a type rename and the existing wx-event handlers (`on_slicing_update`, `on_slicing_completed`, `on_process_completed`, `on_export_began/finished`) survive unchanged. Internals are written fresh — BSP's persistent worker thread + condition-variable state machine is **not** carried over.

**`Print::apply()` stays.** `Print` remains the change-detection/config-holder object: `PnpSlicingProcess::apply()` still calls `m_print->apply(model, config)` and uses `ApplyStatus` exactly as today, so `update_background_process`, per-plate `is_slice_result_valid`, and the gcode_result-reset-on-invalidation behavior all survive. `Print::process()` is simply never called (rip-out depth is ticket 008).

**Threading — one worker thread per slice.** `start()` spawns a thread that runs: export inputs → spawn pnp_cli → pump JSONL stderr progress → wait → ingest gcode → post `SlicingProcessCompletedEvent`. States map onto the existing vocabulary (IDLE → STARTED at `start()`, RUNNING once the child is spawned, FINISHED/CANCELED at thread end); no persistent thread, no `STATE_EXIT` teardown dance.

**Launcher — `boost::process` on all platforms** (already a dependency; POSIX precedent in `PostProcessor.cpp::run_post_process_scripts`). Async stderr pipe for JSONL progress events; `CREATE_NO_WINDOW`-equivalent flags on Windows.

**Cancel — kill now, graceful later.** v1: `stop()`/`stop_internal()` terminate the child (`child.terminate()`) and delete partial output — safe because pnp_cli is stateless. The internal-cancel-posts-no-event behavior is preserved. **Verified fact: pnp_cli has no cancellation support today** (no signal handling in `crates/pnp-cli`, nothing in `docs/09_progress_events.md`); graceful cancel (exit cleanly on stdin close or SIGINT/CTRL_BREAK, with kill-after-timeout fallback in the GUI) is recorded as a **pnp handoff item**.

**Temp-file handoff.** Inputs: per-slice subdir under the app temp dir holding the exported model file + `config.json`; deleted on success, retained on failure for diagnosis. Output: G-code written/copied to `PartPlate::get_tmp_gcode_path()` — the preview mmaps that path and the finalize/export/upload machinery keys off it, so downstream export (`finalize_gcode`'s `.pp` copy dance, removable-media checks) is untouched. (Model export format itself is ticket 004.)

**"Slice all" — Plater's loop unchanged.** The existing `m_slice_all` / `on_process_completed` → `select_plate` → `start_next_slice()` mechanism re-enters the preserved public surface once per plate: one pnp_cli run per plate, sequential, matching the locked scope decision. No new orchestration.

**Already-sliced reuse path.** `Print::finished()` can never be true anymore; reuse (skip subprocess, go straight to finalize/export) triggers when `PartPlate::is_slice_result_valid()` && the plate's tmp gcode file exists && `apply()` returned `APPLY_STATUS_UNCHANGED`.

**Error seam (mechanism only; UX is ticket 006).** Any pnp failure — nonzero exit, JSONL error event, missing output file — throws `SlicingError` (non-critical, plater stays valid) carrying pnp's error message + a stderr log tail; the worker catches it and posts the completion event with `exception_ptr`, identical to native error flow.

**No prototype stub** — plan-only map; this design plus the standing facts is the implementation handoff.

**GCodeProcessorResult boundary:** `PnpSlicingProcess` is responsible for filling the plate's `GCodeProcessorResult` before posting completion (the `Plater::load_gcode` → `GCodeProcessor::process_file` path is the proven ingredient); the ingestion details are ticket 007.
