# PNP Feature-Coverage Inventory (vs. v1 fork scope: slice + preview)

Sources: live `pnp_cli module config-schema` run (release binary, 2026-07-16), pnp `docs/` (esp. `07_implementation_status.md`, `09_progress_events.md`), module manifests under `modules/core-modules/`, `slicer-gcode` serializer source. Ticket: [001](../tickets/001-pnp-feature-coverage-inventory.md).

## CLI surface (verified live)

```
pnp_cli slice --model <STL|OBJ|3MF> [--config <json>] [--output <gcode>]
    [--module-dir <PATH>]... [--no-default-module-paths]
    [--thumbnail <png>] [--report <html>] [--instrument-stderr]
pnp_cli module config-schema [--module-dir <PATH>]...
```

Also available: `pnp_cli mesh repair|decimate|import|convert` (STEP import, 3MF geometry-only writer), `pnp_cli dag stages|stage|depends|claims` (manifest introspection).

## Module inventory (20 core modules)

arachne-perimeters, classic-perimeters (mutually exclusive via claim; `wall_generator` selects, default `classic`), fuzzy-skin, gyroid/lightning/rectilinear-infill (coexist, resolved per-region), layer-planner-default, machine-gcode-emit, overhang-classifier-default, part-cooling, path-optimization-default, seam-placer, seam-planner-default, skirt-brim, support-planner, support-surface-ironing, top-surface-ironing, traditional-support, tree-support (mutually exclusive `support-generator` claim), wipe-tower.

Live combined schema: **159 fields across 20 modules** (manifests declare 179 `[config.schema]` entries; delta is duplicate/shared key names across modules). Major groups: Walls, Support (29 keys incl. `support_raft_layers`), Infill, Cooling, Speed (per-role feedrates, 26 keys), Wipe Tower, Skirt/Brim, Seam, Quality, Machine G-code.

## Feature matrix vs. Orca v1 UI surface

| Feature | Status |
|---|---|
| Supports | ✅ Normal/grid + tree (both modules; Orca key parity per TASK-163) |
| Multi-material / multi-extruder | ⚠️ Plumbing exists (wipe tower, `T<n>` tool changes, per-region extruder via 3MF modifier config, filament colour arrays) but **not proven end-to-end on real fixtures** (TASK-210/211/212 open) |
| Per-object / modifier-mesh settings | ✅ `ModifierVolume` + 3MF `model_settings.config` sidecar (negative parts, support enforcer/blocker, per-object config deltas) |
| Skirt / brim | ✅ (`skirt_loops`, `skirt_distance`, `brim_width`) |
| Raft | ⚠️ **Corrected by ticket 012 (2026-07-17)** — not "❌". No *implemented* standalone raft-under-part (only `support_raft_layers`, support-planner, default 0), but it is **specced**: ADR-0009 (*Proposed*) + `docs/specs/raft-default-module.md` (design sketch, output carrier still open), pending `support-modules-orca-port.md` §C6. This row was derived from the live config-schema probe, which sees implemented modules only and cannot see the spec backlog |
| Seam control | ✅ `seam_position` = nearest/rear/random; ⚠️ live-path gaps flagged (TASK-120c) |
| Input formats | STL, OBJ, 3MF. 3MF: transforms + nested components composed; sidecar per-part config read; **non-uniform scale rejected**; **multi-plate 3MF not supported** (single `<build>` only) |
| Thumbnail | ⚠️ External PNG in via `--thumbnail`, emitted as Orca-compatible THUMBNAIL_BLOCK. **PNP does not render thumbnails** — the GUI must produce the PNG |
| Fuzzy skin, ironing, bridges, cooling, retraction/z-hop, spiral vase, overhang speed control | ✅ all present |
| Infill types | Gyroid, lightning, rectilinear only |
| Post-processing scripts | ❌ removed (Python bridge scrapped) |

## G-code output (preview-relevant)

- Flavor: **Marlin only**, hardcoded; no flavor selection.
- Orca wire-compatible: HEADER_BLOCK (incl. producer string engineered so Orca's viewer parses it), extrusion-width comments, optional THUMBNAIL_BLOCK, CONFIG_BLOCK at EOF (padded to ≥96 keys to satisfy Orca's loader minimum).
- `;TYPE:` annotations in canonical Orca spellings (Outer wall, Inner wall, Top surface, Bottom surface, Internal solid infill, Sparse infill, Bridge, Support, Support interface, Skirt, Brim, Prime tower, Ironing, Gap infill, Custom).
- Layer markers: `;LAYER_CHANGE` / `;Z:<z>` / `;HEIGHT:<Δz>`.
- **Print-time estimate: hardcoded 0** (`estimated_print_time_s: 0`; no kinematics model). No weight/cost; only raw per-tool filament mm.

## Progress events

JSONL on stderr (`docs/09_progress_events.md`). 1.0.0: phase_start/phase_complete/layer_start/layer_complete/module_error/validation_error/slice_complete. 1.1.0: `output_path`, `error.reason`. 1.3.0 (behind `--instrument-stderr`): stage/module start/complete. `slice_stats` (time/weight/filament-length) is **reserved, not implemented** (1.2.0). No `--log-events <file>` flag — stderr only.

## PNP-side handoff items (for pinch_n_print spec-packet workflow)

1. **Print-time estimation** — `estimated_print_time_s` hardcoded 0; Orca preview shows time estimates per feature/layer. Needs a kinematics-based estimator and/or the reserved `slice_stats` event implemented.
2. **Raft** — no *implemented* standalone raft; Orca UI exposes it. ~~Either implement or the fork hides the raft controls.~~ **Amended by ticket 012 (2026-07-17):** already specced pnp-side (ADR-0009 *Proposed* + `raft-default-module.md` sketch); the fork does **not** hide the controls, so this must land. See the [handoff asset](handoff-pnp-gap-implementation.md) item 3.
3. **Multi-material E2E proof** — TASK-210 (support filament routing), TASK-211 (real-fixture T0/T1 E2E), TASK-212 (per-object printer-key allowlist) must close before the fork exposes multi-extruder UI.
4. **G-code flavor selection** — Marlin only; fork must restrict printer profiles or PNP grows flavor support.
5. **Multi-plate 3MF** — PNP reads a single `<build>`; fine if the GUI slices per plate (map already locks per-plate loop), but PNP must accept whatever per-plate file the GUI writes (see ticket 004).
6. **Non-uniform scale** — loader rejects it; Orca allows non-uniform scaling in the GUI. Either bake scale into mesh on export or PNP lifts the restriction.
7. **Seam live-path gaps** — TASK-120c (seam-placer reads `resolved_seam`, sibling-wall re-emission risk).
8. **`slice_stats` progress event** — implement 1.2.0 reservation so the GUI gets filament/time stats without parsing G-code.

## Verdict for the fork

PNP covers the core single-material FFF slice surface Orca's v1 scope needs (walls incl. Arachne, 3 infills, top/bottom shells, bridges, supports incl. tree, skirt/brim, seam, ironing, fuzzy skin, cooling, spiral vase) and its G-code is deliberately Orca-viewer-compatible (`;TYPE:`, LAYER_CHANGE, CONFIG_BLOCK, THUMBNAIL_BLOCK). The hard gaps for the fork UI are: no time estimates, no raft, Marlin-only, unproven multi-material, no slicer-side thumbnail rendering, no multi-plate 3MF ingestion.

**Amended by ticket 012 (2026-07-17):** none of these become UI restrictions. The fork ships zero UI-surface restrictions and no capability gate, because pnp gaps are scheduled to close in step with the fork's roadmap (map Notes). Read every "the fork may hide X" hedge in this asset as withdrawn — the gaps above are **scheduled prerequisites** on the pnp side, tracked in the [handoff asset](handoff-pnp-gap-implementation.md).
