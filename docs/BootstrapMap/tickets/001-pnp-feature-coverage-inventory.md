---
title: PNP feature-coverage inventory
status: closed
type: research
assignee: Analysis Agent
blocked-by: []
---

## Question

What can the PNP backend actually slice today, measured against the OrcaSlicer UI surface the fork will expose? Inventory: supports, multi-material/multi-filament, per-object/modifier settings, brim/skirt/raft, seam control, input formats (STL/OBJ/3MF specifics), thumbnail generation, and G-code flavor/annotation output. Sources: pnp `docs/` index, `pnp_cli module config-schema`, module manifests under `modules/core-modules/`. Output: a markdown asset linked from this ticket; every gap vs. the v1 fork scope recorded as a precise pnp-side handoff item.

## Resolution (2026-07-16)

Full inventory: [assets/001-pnp-feature-coverage-inventory.md](../assets/001-pnp-feature-coverage-inventory.md).

**Answer in brief:** PNP covers the core single-material FFF surface the v1 fork needs — classic + Arachne walls, gyroid/lightning/rectilinear infill, top/bottom shells, bridges, normal + tree supports, skirt/brim, seam (nearest/rear/random), ironing, fuzzy skin, spiral vase, cooling — across 20 modules / 159 live config keys, and its G-code is deliberately Orca-viewer-compatible (canonical `;TYPE:` labels, `;LAYER_CHANGE`/`;Z:`/`;HEIGHT:`, HEADER_BLOCK with Orca producer string, CONFIG_BLOCK padded to Orca's ≥~80-key loader minimum, THUMBNAIL_BLOCK). Inputs: STL/OBJ/3MF with transforms, nested components, and per-part sidecar config.

**Gaps (each a recorded pnp-side handoff item in the asset):** print-time estimate hardcoded 0 (no `slice_stats` event either); no standalone raft (only `support_raft_layers`); Marlin-only G-code flavor; multi-material plumbing present but unproven end-to-end (TASK-210/211/212 open); thumbnails are external-PNG-in — the GUI must render them; multi-plate 3MF not read (single `<build>`); non-uniform scale rejected by the loader; seam live-path gaps (TASK-120c).
