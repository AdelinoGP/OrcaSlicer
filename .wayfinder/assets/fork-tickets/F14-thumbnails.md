---
title: Thumbnails — UI-thread render before spawn, multi-size PNG to pnp_cli
status: open
batch: B8
blocked-by: [F13]
files: [src/slic3r/GUI/PnpSlicingProcess.cpp, src/slic3r/GUI/Plater.cpp, src/slic3r/GUI/GLCanvas3D.cpp]
---

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
