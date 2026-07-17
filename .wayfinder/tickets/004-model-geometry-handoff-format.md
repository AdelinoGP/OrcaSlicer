---
title: Model/geometry handoff format
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: [001]
---

## Question

What file does the GUI hand `pnp_cli` per plate — 3MF or per-plate STL (or something else)? How do object transforms, modifiers, and per-object settings survive the handoff, or which are explicitly dropped for v1 (each drop recorded as a pnp-side handoff item where PNP support is the fix)?

## Resolution (2026-07-16, grilling)

**Format: per-plate 3MF.** The slice worker writes a temp 3MF for the target plate via the existing `Plater::export_3mf(path, strategy, plate_idx)` → `store_bbs_3mf` path (Silence, no G-code, no thumbnails), into the per-slice temp dir from the slicing-seam design (ticket 003). This preserves instance transforms, negative volumes, modifier meshes, and per-object/per-volume config via the `model_settings.config` sidecar — the sidecar PNP's loader already parses. No new serialization code on the GUI side.

**Coordinates: plate-local origin.** Plate contents are translated so the plate origin is (0,0) in the exported 3MF; PNP sees a normal single-bed scene. PNP's single-`<build>` limitation is a non-issue under the per-plate loop — handoff item 7 (multi-plate 3MF) is confirmed won't-fix.

**Non-uniform scale: PNP-side handoff item.** The GUI does NOT bake scale. Until PNP's `slicer-model-io` lifts `NonUniformScaleUnsupported` (handoff item 6, now confirmed PNP-side), the GUI blocks slicing with a clear error when any instance on the plate carries non-uniform scale.

**Per-object/per-volume config deltas: passed through raw, Orca key names.** The GUI writes the sidecar untouched; the handoff item is for PNP to accept Orca per-object key names (parser-side parity/translation). Until it lands, PNP silently ignores unknown per-object keys — accepted v1 behavior (no GUI-side stripping, no per-object warning UX).

**Multi-material/extruder assignments: written as-is, "let PNP try".** Per-volume extruder/filament assignments are exported in the sidecar; behavior rides PNP's existing (unproven) multi-material plumbing, whose E2E proof remains handoff item 4. No GUI-side single-extruder restriction imposed by this ticket (ticket 012 may still restrict UI).
