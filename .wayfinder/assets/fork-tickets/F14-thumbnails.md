---
title: Thumbnails — UI-thread render before spawn, multi-size PNG to pnp_cli
status: done
batch: B8
blocked-by: [F13]
files: [src/slic3r/GUI/PnpSlicingProcess.cpp, src/slic3r/GUI/Plater.cpp, src/slic3r/GUI/GLCanvas3D.cpp]
---

## ⚠ CONTRACT CORRECTION (driving session B8, 2026-07-19 — supersedes the repeatable-CLI-arg wording below)

pnp packet 173 (`pinch_n_print` commit `b348b179`) **deliberately rejected** the fork-011
"repeatable `(path, WxH, target-format)` CLI arg" shape and locked `D-173-THUMBNAIL-SINGLE-PNG`
instead. The landed, live-probed contract (verified end-to-end this session — a real `pnp_cli slice`
with `thumbnails="48x48/PNG,300x300/PNG"` + one 1569×1140 PNG emitted **two** parseable
`; thumbnail begin 48x48 …` / `; thumbnail begin 300x300 …` blocks, exit 0):

- The fork renders **ONE high-res top-down PNG** and passes it via the **existing single**
  `--thumbnail <path>` flag (pnp validates PNG magic, reads it into `thumbnail_path`).
- The requested **sizes/formats travel in the `thumbnails` config key** (the `"XxY/EXT,…"` coString),
  which the fork must place into the `--config` JSON pnp already receives. pnp's postpass
  (`run_postpass_with_thumbnail`, `crates/slicer-runtime/src/pipeline.rs`) reads
  `raw_config_source.get("thumbnails")`, parses it (`parse_thumbnails_key`), **resizes the single
  source PNG per spec** and **transcodes** each format, owning ALL wire framing (base64, 78-col,
  outer sentinels, inner begin/end, ColPic/BTT). Key absent ⇒ one PNG at source size.
- `thumbnails`/`thumbnail_path` are **passthrough raw keys, NOT in pnp's `module config-schema`**
  (probed: 61 KB schema, zero `thumbnail*` keys) and do **not** fatal config resolution. Therefore
  the fork must inject `thumbnails` into `config.json` **AFTER** `PnpConfigTranslator::apply_schema_guard`
  (the guard would otherwise strip an unknown key). The 3MF-config channel (`read_3mf_filament_colours`,
  `object_metadata_to_config_data`) does NOT carry `thumbnails` — the `--config` JSON is the only
  working channel today.

Read the "Steps"/"Decisions" below through this correction: **no repeated CLI args, no per-size
fork-side PNGs, no fork-side non-PNG encoding.** Everything else (UI-thread render before spawn,
delete BSP's `thumbnail_cb`/`m_thumbnail_cb`, PNG-only) still holds.

## Goal

Ship thumbnails in PNP G-code, per [ticket 011](../../tickets/011-thumbnails-and-gcode-metadata.md)
and pnp handoff item 14. **Gated on pnp**: `pnp_cli slice` must have grown the repeatable
`(path, WxH, target-format)` thumbnail argument — check before starting this batch.

## Decisions (do not re-open)

- The fork (only side with a GL context) renders each size the printer preset's `thumbnails`
  list requests, **on the UI thread, before spawning pnp_cli** — deleting BSP's old
  `thumbnail_cb`/`execute_ui_task` inversion, which F04 deliberately never reproduced.
- Fork ships **PNG only**; pnp decodes and re-encodes to the target format and owns all wire
  framing (base64, 78-col wrap, sentinels, inner begin/end, ColPic/BTT shapes). Do not encode
  other formats fork-side; do not flip rows (fork PNGs are already top-down).
- Parse the preset's `thumbnails` `coString` (`"XxY/EXT, ..."`, legacy `thumbnails_format`
  folded per `handle_legacy_composite`) to build the argument list.

## Steps

1. Reuse Orca's existing plate-thumbnail render path (the same one BSP's `thumbnail_cb` used —
   trace from the deleted code in git history if needed) to produce per-size PNGs into the
   per-slice temp dir.
2. Append repeated thumbnail args to F04's command line.
3. Verify emitted G-code's thumbnail block parses: `GUI_App::gcode_thumbnails_debug` or drag the
   file back into Orca and check the plate icon.

## Done / verify

- `ALL_BUILD` clean; smoke: slice with a profile requesting `48x48/PNG,300x300/PNG` → G-code
  contains two parseable `; thumbnail begin WxH len` blocks.

## Completion note (driving session B8, 2026-07-19)

**Implemented against the corrected `D-173-THUMBNAIL-SINGLE-PNG` contract** (see the correction note
at top). Edits, all in F14's frontmatter files:

- `PnpSlicingProcess.hpp`: dropped dead `set_thumbnail_cb` setter + `m_thumbnail_cb` member; added
  `boost::filesystem::path thumbnail_path` to `SliceJob`.
- `PnpSlicingProcess.cpp` `start()` (UI thread, inside `if (!job.reuse)`): `update_all_plate_thumbnails(true)`
  → encode `m_current_plate->thumbnail_data` via `GCodeThumbnails::compress_thumbnail(…, PNG)` → write
  `input_dir/thumbnail.png` → set `job.thumbnail_path` (all failures non-fatal). Then inject
  `translated.json["thumbnails"] = full_config.opt_string("thumbnails")` **after** `apply_schema_guard`,
  gated on a rendered PNG existing.
- `PnpSlicingProcess.cpp` `run_pnp_cli()`: append `--thumbnail <path>` to argv (+ log echo) when set.
- `Plater.cpp`: removed the lone live `background_process.set_thumbnail_cb(...)` caller (dead under
  shell-out). `GLCanvas3D.cpp` untouched — cached `thumbnail_data` reuse sufficed.

**Verified:** ALL_BUILD RelWithDebInfo 0 errors (OrcaSlicer.dll relinked); `ctest` 184/184 (incl. pnp
schema-guard #184 + translator-regression #183 — the post-guard injection doesn't disturb config
handling); GUI launch 22s+ to homepage, no crash/fatal. The pnp side (single PNG + `thumbnails` key →
two Orca-format blocks) was proven end-to-end against the live rebuilt `pnp_cli` before implementing.

**Note on orientation / camera:** the reused `update_all_plate_thumbnails` renders `thumbnail_data` at
Orca's default (Iso) plate-icon view, not a strict top-down camera. "Top-down" in this ticket/design
means PNG *row order* (not GL bottom-up), which `compress_thumbnail(PNG)` satisfies; camera angle is the
established plate-icon look. If a strict top-down camera is later required, substitute a fresh
`GLCanvas3D::render_thumbnail` call — noted, not done.

**HUMAN EYEBALL PENDING (GUI-only):** GUI-slice with `48x48/PNG,300x300/PNG` and confirm the emitted
`<plate>.gcode` carries two `; thumbnail begin WxH len` blocks (the GL plate render can't be script-driven).
