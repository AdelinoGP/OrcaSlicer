---
title: Thumbnails & G-code metadata gaps in the GUI
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: [003]
---

## Question

Inventory (ticket 001) established: PNP embeds an externally-supplied PNG via `--thumbnail` (it renders nothing itself), and emits no print-time estimate (`estimated_print_time_s` = 0) or weight/cost stats. How does the fork's GUI render the per-plate thumbnail PNG to pass to `pnp_cli` (Orca already renders plate thumbnails — reuse path?), and how does the UI handle absent time/weight estimates in preview, sidebar stats, and print-host upload metadata — hide, show "n/a", or wait on the pnp-side `slice_stats`/time-estimator handoff items?

## Comments

Ticket 007 (2026-07-16) softens the premise: Orca's `GCodeProcessor` computes time estimates and filament volume itself from the parsed moves — it ignores `; estimated printing time`/`; filament used` comments entirely on external load. So preview and sidebar times/volumes are NOT blank; they're just default-machine-limits accuracy (improvable via pnp handoff item 10's `machine_max_*` passthrough). The genuinely absent pieces are: grams/cost (needs `filament_density`/`filament_cost` in CONFIG_BLOCK — also item 10), M73 on-printer progress, and G-code-embedded time metadata for print-host upload. Scope this ticket's "absent estimates" question to those.

**Correction (this ticket, 2026-07-17):** 007's comment above conflates two same-named structs. `GCodeProcessorResult::print_statistics` is a `PrintEstimatedStatistics` — it carries volumes and times but **no total weight and no total cost**. Those live on `Print::m_print_statistics` (`PrintStatistics`, `Print.hpp`) and are computed only by `DoExport::update_print_estimated_stats` in `GCode.cpp`, i.e. inside the export path ticket 008 deletes. So grams/cost are not merely "default-density approximate" on the fork — they are structurally zero until the fork fills them, and `GCodeViewer::render_legend` reads them with no zero-guard.

## Resolution

### Premises the ticket got wrong (verified)

- **There is no sidebar stats panel.** `Sidebar::update_sliced_info_sizer` has zero hits repo-wide. `SlicedInfo` (`Plater.cpp`) — with its `siFilament_g` / `siCost` / `siEstimatedTime` rows and the `"N/A"`-sentinel `SetTextAndShow` — is constructed by nobody; its only two references sit inside `#if 0` blocks (`Sidebar::sys_color_changed`, `Plater::priv::on_process_completed`). Orca removed PrusaSlicer's sidebar stats and orphaned the class. **Nothing to decide, nothing to do.**
- **Print-host upload carries no stats.** `PrintHostUpload` (`slic3r/Utils/PrintHost.hpp`) holds source/upload path, group, storage, post_action, `extended_info` — no time/weight/cost. `PrintHostDialogs.cpp` has zero hits for weight/estimated/print_statistics; the send dialogs show filename/group/storage only. The single stats touch is `PrintStatistics::finalize_output_path`, which expands placeholders into the output *filename* — fixed for free by filling `PrintStatistics` (below). **Nothing to do.**
- **Orca's real "sidebar stats" are the send dialogs**: `SelectMachineDialog::set_default_normal`, `SendToPrinterDialog::set_default`, `SendMultiMachinePage` — three near-identical copies reading `Print::print_statistics().total_weight` via `::sprintf("%.2f g", ...)` (unguarded) and `plate->get_slice_result()->print_statistics.modes[0].time`.

### Thumbnails — pnp owns embedding; the fork owns rendering

**Decided: extend `pnp_cli` to accept the thumbnail *list*** (rejected: fork-side post-slice injection reusing `export_thumbnails_to_file`; rejected: single-PNG `--thumbnail`). Rationale is ticket 002's cherry-pick-only sync — every fork-side edit to `Thumbnails.cpp` / `GCodeViewer.cpp` is permanent merge cost, so capability is pushed to pnp and the fork stays thin.

Why the current `--thumbnail` cannot work, verified:
- **Orca's contract is a list, not a file.** `thumbnails` is a `coString`, default `"48x48/PNG,300x300/PNG"`, format `"XxY/EXT, ..."`; `thumbnails_format` is legacy, folded into the per-entry `/EXT` suffix by `handle_legacy_composite`. `export_thumbnails_to_file` invokes the callback **once per requested size**. Across `resources/profiles/`, 190 machine profiles pin a format: 182 PNG, 3 QOI, 3 ColPic, 2 JPG.
- **Five formats, three wire shapes.** `compress_thumbnail` dispatches to PNG (tag `thumbnail`) / JPG (`thumbnail_JPG`) / QOI (`thumbnail_QOI`) / BTT_TFT (`thumbnail_BIQU`, `;WWWWHHHH\r\n` hex header) / ColPic (`thumbnail_QIDI`, `;gimage:` first then `;simage:`). PNG is the only encoder that does not row-flip the bottom-up GL buffer.
- **PNP's block is unparseable by anything but PNP.** `serialize_thumbnail_block` (`crates/slicer-gcode/src/thumbnail.rs`) emits `; THUMBNAIL_BLOCK_START`, bare base64, `; THUMBNAIL_BLOCK_END` — omitting the inner `; <tag> begin <W>x<H> <len>` / `; <tag> end` lines that Orca's `Thumbnails.hpp` nests inside those sentinels. The omission is deliberate (its comment: so the roundtrip test decodes the region cleanly). Every real consumer keys on `; thumbnail begin` — that is the `BEGIN_MASK` in Orca's own reader (`GUI_App::gcode_thumbnails_debug`) and the PrusaSlicer-lineage convention firmware and print hosts parse. Orca wraps at 78 cols, PNP at 76 (harmless).

**Fork side — render before spawn, on the UI thread.** `pnp_cli` takes thumbnails as *input*, which inverts today's control flow and deletes machinery rather than porting it:
- Rendering is **GL-only, main-thread-only**. `GLCanvas3D::render_thumbnail` → `render_thumbnail_internal` → `glReadPixels`; there is no software rasterizer. Today `GCode::_do_export` (worker) calls `thumbnail_cb` → `BackgroundSlicingProcess::render_thumbnails` → `execute_ui_task`, which `CallAfter`s to the UI thread and blocks the worker on a condition variable (cancellable via `cancel_ui_task`).
- Under shell-out the fork renders in `start()` on the UI thread *before* spawning the child, writes the encoded files into the per-slice temp dir (ticket 003), and passes their paths. **`PnpSlicingProcess` therefore drops `m_thumbnail_cb`, `render_thumbnails`, `execute_ui_task`, `cancel_ui_task`, and the `UITask` struct entirely** — they exist only to serve the inverted callback. Confirms ticket 003's "internals written fresh".
- Reuse: `PartPlate` already caches `thumbnail_data` (512x512) with `Plater::update_all_plate_thumbnails(force)` / `invalid_all_plate_thumbnails()` and reset-on-geometry-change hooks. Note the static `GLCanvas3D::render_thumbnail_framebuffer*` overloads take `PartPlateList&`/`GLVolumeCollection&`/shader explicitly and are callable without a canvas instance (precedent: `CalibUtils.cpp`, and headless `OrcaSlicer.cpp` which spins up its own glfw context). **`render_thumbnail` does not call `_set_current()`** — it assumes the wx GL context is already current from a prior render. That implicit invariant must hold at `start()` time; verify explicitly.
- Also verified: `GCode::_do_export` embeds thumbnails only for **non-BBL** printers (`else if` on the `is_bbl_printers` CONFIG_BLOCK branch). Moot once pnp embeds, but explains why no fork-side embed path is being preserved.

**Encode split — decided: the fork always emits PNG; pnp transcodes.** The fork renders each requested size and encodes it as PNG only; `pnp_cli` grows a **repeatable** thumbnail argument carrying `(path, WxH, target format)`, decodes the PNG, and re-encodes to the target format, then owns the wire framing — base64, 78-col wrap, outer sentinels, inner `begin`/`end` tag lines, and the ColPic/BTT special shapes. Rejected: fork encodes with Orca's existing `compress_thumbnail` and passes bytes+tag (cheaper — zero new code either side — but splits format knowledge across the two codebases); rejected: fork passes raw RGBA (large temp files, and pnp inherits GL's bottom-up row order).

Cost is real but **stages cleanly**: PNG is a passthrough (decode/re-encode is a no-op or can be skipped outright), and PNG covers **182 of the 190** profiles that pin a format. So pnp can ship PNG-only first — which is the entire fork-visible surface for all but 8 profiles — and add the QOI / ColPic / JPG transcodes as a follow-up. The BTT_TFT and ColPic shapes are the expensive tail: ColPic is a custom RGB565 palette/RLE codec (`ColPic_EncodeStr`, capped at 512px, aspect preserved) and BTT_TFT is RGB565 hex text with `\r\n` line endings; neither is a standard library format. Note also that PNG is the **only** Orca encoder that does not row-flip the bottom-up GL buffer — so a PNG arriving at pnp is already top-down, and the transcoders must not re-flip it.

### Time, weight, cost

- **Time is already handled and already guarded.** `GCodeViewer::render_legend` computes `show_estimated_time = total_estimated_time > 0.0f && (...)`, so a zero time **hides** the estimated-time section rather than showing `0s`; per-role times render `""` when `<= 0`. `GCodeViewer::load` falls back out of Stealth mode when its time is 0. `IMSlider::get_label` returns early when `m_layers_times` is empty. So preview degrades cleanly today and improves for free when pnp handoff item 1 lands. No fork work.
- **Weight — from pnp `slice_stats`** (handoff item 2). Decided over fork-side computation, same thin-fork rationale.
- **Cost — computed fork-side** from Orca's own preset. `slice_stats`' reserved schema has no cost field and cost is pure Orca preset math (`total_cost += weight * filament_cost * 0.001`, plus `time_cost * hours`, via `Extruder::filament_cost()`); teaching pnp Orca's pricing model to recompute a number the fork already has every input for was rejected.
- Either way `PnpSlicingProcess` fills `Print::m_print_statistics` post-ingestion, which is what `render_legend`'s Summary / FeatureType / ColorPrint blocks, `render_all_plates_stats`, the three send dialogs, `PartPlateList::store_to_3mf_structure` (the one caller with an explicit `if (ps.total_weight != 0.0)` guard), and `finalize_output_path` all read.
- **`slice_stats` as reserved is insufficient** — see handoff item 2 amendment below. `PrintStatistics` needs `total_used_filament` (mm), `total_extruded_volume` (mm³), per-extruder `filament_stats`, and `total_toolchanges`; the reserved five fields supply none of them.

### M73 — pnp emits it; the fork stays out of post-processing

**Decided: pnp emits M73 directly during G-code emit**, off its own estimator (handoff item 1). The fork's ingestion path is unchanged — `process_file` keeps ending in `finalize(false)`, and the fork never calls `run_post_process`. Consistent with the thin-fork rationale throughout this ticket, and it makes pnp's G-code correct standalone rather than correct-only-after-Orca-touches-it.

Mechanism verified (context for the rejected alternative): Orca does not emit M73 from the writer — `GCodeWriter::update_progress` early-returns unless the flavor is MakerWare/Sailfish, and honours `disable_m73`. Instead `GCode.cpp` plants `_GP_FIRST_LINE_M73_PLACEHOLDER` / `_GP_LAST_LINE_M73_PLACEHOLDER` comments, and `GCodeProcessor::run_post_process()` reopens the finished file, rewrites it via a `.postprocess` swap, injects M73 (`"M73 P%s R%s\n"` Normal / `"M73 Q%s S%s\n"` Stealth, masks set in `register_commands`), and regenerates the `; filament used [g]` block. It is reachable only from `finalize(true)`, called only by `GCode::_do_export`. There is no user-facing "add M73 to an arbitrary file" entry point (`export_remaining_time_to_file` does not exist in this checkout — Orca merged it into `run_post_process`).

**Rejected: pnp plants the two placeholder lines and the fork calls `set_print(m_print)` + `finalize(true)`.** Cheaper on paper (~2 lines each side, no UI-file edits, Orca's injector does the work) but it makes pnp's output depend on Orca to become printable, and puts the fork in the business of rewriting the sliced file post-hoc.

**Consequences for pnp:** M73 needs cumulative per-line remaining-time tracking, so it is strictly downstream of handoff item 1 — do not ship M73 before the estimator, since fabricated remaining-times are worse than none. pnp also owns the `; filament used [g]` comment block under this split (Orca's viewer ignores it on load — it recomputes from moves — but print hosts and firmware read it). `disable_m73` is an Orca printer-preset key (`coBool`, default false); if it is ever mapped it belongs in ticket 005's translation table.

**Zero fork-side work.** This axis authorizes nothing in the fork.

### Load-bearing constraint discovered — handoff item 10 is mandatory, not nice-to-have

`GCodeProcessor::process_file`'s pre-pass detects the producer, and for `EProducer::OrcaSlicer` does `config.apply(FullPrintConfig::defaults())` → `config.load_from_gcode_file(filename)` → `apply_config(config)`. It **starts from defaults and discards anything pre-applied**. PNP's header emits `; OrcaSlicer-compatible output generated by Pinch'n'Print`, which matches the `Producers` table entry `{ EProducer::OrcaSlicer, SLIC3R_APP_NAME }` — deliberately, per its own source comment. Consequence: **the fork cannot hand `GCodeProcessor` its in-memory preset**; everything the processor knows must arrive through the CONFIG_BLOCK. So item 10's `machine_max_*` (time accuracy) and `filament_density` (per-role grams in `used_filaments_per_role`, computed during parse) have no fork-side workaround short of patching `process_file`.

This is also exactly why the **cost** split above is sound: cost is computed *after* `process_file` returns, from Orca's preset, so it is immune to the reset.

Corollary reinforcing ticket 007's MUST: `s_IsBBLPrinter` defaults to **`true`** (`GCodeProcessor.cpp`), and `process_file` only overwrites it inside `if (printer_model_opt && !printer_model_opt->value.empty())`. With `printer_model` absent, whatever `Plater` last set survives — 007's forced `= false` is load-bearing, and item 10's `printer_model` passthrough is the belt-and-braces fix.

### Fork-side work this ticket authorizes

1. `PnpSlicingProcess::start()` renders each `thumbnails`-requested size on the UI thread (reusing `PartPlate` cache), encodes via `compress_thumbnail`, writes to the per-slice temp dir, passes paths to `pnp_cli`.
2. `PnpSlicingProcess` omits `m_thumbnail_cb` / `render_thumbnails` / `execute_ui_task` / `cancel_ui_task` / `UITask`.
3. Post-ingestion, fill `Print::m_print_statistics`: weight/length from `slice_stats`, cost computed from Orca's preset over `total_volumes_per_extruder`.
4. No edits to `GCodeViewer.cpp`, `Thumbnails.cpp`, or the send dialogs — all merge-cost surfaces stay untouched.

**Blocking:** thumbnails and weight now gate on pnp handoff items 1, 2, and the amended thumbnail item. v1 slice+preview is shippable without them (time hides cleanly, cost works, thumbnails simply absent); they gate *completeness*, not the seam.
