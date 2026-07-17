---
title: Per-plate 3MF export + raw per-object sidecar
status: done
batch: B2
blocked-by: []
files: [src/slic3r/GUI/PnpModelExport.hpp, src/slic3r/GUI/PnpModelExport.cpp, src/slic3r/GUI/CMakeLists.txt]
---

## Goal

Produce the model input `pnp_cli slice --model` consumes, per
[ticket 004](../../tickets/004-model-geometry-handoff-format.md).

## Decisions (do not re-open)

- **Per-plate temp 3MF via the existing `export_3mf(plate_idx)` path** (plate-local origin,
  single `<build>`); one plate per pnp_cli invocation. Multi-plate 3MF is won't-fix.
- Per-object/per-volume config deltas written into `model_settings.config` **untouched, Orca key
  names**, plus per-volume extruder/filament assignments as-is (PNP-side translation is pnp
  handoff item 9 — do not translate fork-side).
- **No transform checks**: non-uniform scale passes straight through
  ([ticket 012](../../tickets/012-v1-ui-surface-restrictions.md) superseded 004's block; pnp
  handoff item 6 lifts the loader restriction before release). Do not bake scale into vertices.

## Steps

1. Thin wrapper `export_plate_3mf_for_pnp(plate_idx, path)` reusing Orca's existing plate-3MF
   export (find the `export_3mf`/`store_bbs_3mf` plate-scoped path Plater already uses); verify
   the emitted file is single-`<build>`, plate-local origin.
2. Confirm per-object settings and extruder assignments survive in `Metadata/model_settings.config`
   for an object with a modifier + per-object layer height (inspect the zip by hand once).
3. Called from F04's worker before spawn (interface: path in the per-slice temp dir).

## Done / verify

- `libslic3r_gui` builds clean.
- Manual: exported 3MF for a 2-plate project's plate 2 contains only plate-2 objects at
  plate-local coordinates; `pnp_cli slice --model <it>` loads it.
