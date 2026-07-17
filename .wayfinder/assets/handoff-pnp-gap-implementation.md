# Handoff: PNP-side gaps blocking the OrcaSlicer-frontend fork

Audience: a planning session in `F:\slicerProject\pinch_n_print` using the spec-packet workflow (`/spec-packet-generator`, then `/spec-review --preflight`, then `/swarm`). Source: [feature-coverage inventory](001-pnp-feature-coverage-inventory.md) (ticket 001 of the fork's wayfinder map), verified against the live release binary and pnp docs on 2026-07-16.

Context: the fork replaces OrcaSlicer's in-process slicing with `pnp_cli slice` shell-outs (v1 scope: slice + preview, single plate per invocation, Windows-first). Each item below is a PNP capability gap the fork's GUI currently has UI surface for. Priorities reflect fork impact; the fork side may independently decide to hide UI instead (its ticket 012), so confirm priorities with the human before packeting.

## Items

1. **Print-time estimation** — `estimated_print_time_s` is hardcoded 0 in `crates/slicer-gcode/src/emit.rs` (`PrintMetadata`); no kinematics/acceleration model exists. Orca's preview and sidebar show per-feature/per-layer time. Needs an estimator (even a simple feedrate-distance model beats 0) surfaced in G-code metadata. High priority — visible on every slice.
2. **`slice_stats` progress event** — reserved in `docs/09_progress_events.md` schema 1.2.0 (`gcode_prediction_seconds`, `gcode_weight_grams`, `gcode_filament_length_mm`, `layer_count`, `first_layer_height_mm`) but not emitted. Implementing it lets the GUI get stats without parsing G-code. Natural companion packet to item 1.
3. **Standalone raft** — no raft-under-part feature; only `support_planner`'s `support_raft_layers` (default 0) exists. Orca UI exposes raft controls. Medium priority; fork may hide the control in v1.
4. **Multi-material end-to-end proof** — plumbing exists (wipe-tower module, `T<n>` tool changes, per-region extruder via 3MF modifier config, filament colour arrays) but open backlog items block exposure: TASK-210 (support_filament / support_interface_filament routing), TASK-211 (real-fixture T0/T1 G-code E2E, currently synthetic-only), TASK-212 (object-scoped printer-key sidecar allowlist only partially populated). These are existing pnp backlog tasks — packet them rather than re-scoping.
5. **G-code flavor selection** — `gcode_flavor` is hardcoded `marlin` in `crates/slicer-gcode/src/serialize.rs` CONFIG_BLOCK; no Klipper/RepRap variants. Low priority if the fork restricts printer profiles to Marlin, but decide deliberately.
6. **Non-uniform scale support** — `slicer-model-io` loader rejects non-uniform-scale transforms (`NonUniformScaleUnsupported`). Orca's GUI freely applies non-uniform scaling. Either lift the restriction (bake scale into vertices at load) or the fork must bake it at export — cheapest fix is likely PNP-side.
7. **Multi-plate 3MF** — loader reads a single `<build>` item list; no plate concept. Likely NOT needed: the fork already locked a per-plate loop, handing PNP one plate per invocation. Confirm the per-plate handoff format (fork ticket 004) before doing anything here; may close as won't-fix.
8. **Seam live-path gaps** — TASK-120c: seam-placer reads `resolved_seam` instead of selecting from `PerimeterIR.regions[*].seam_candidates`; rotated-wall replacement can erase sibling walls unless the full region wall set is re-emitted. Existing pnp backlog item; correctness risk on every slice with seam control.

## Not gaps (do not re-implement)

- Thumbnails: PNP's external-PNG-in `--thumbnail` design is fine; the fork's GUI renders the PNG (fork ticket 011).
- `;TYPE:` annotations, LAYER_CHANGE markers, HEADER/CONFIG/THUMBNAIL blocks: already Orca-viewer-compatible.

## Suggested packet grouping

- Packet A: items 1+2 (time estimation + slice_stats) — one workstream, shared metadata plumbing.
- Packet B: item 4 (close TASK-210/211/212) — existing backlog.
- Packet C: item 6 (non-uniform scale) — small, isolated in `slicer-model-io`.
- Packet D: item 8 (TASK-120c seam) — existing backlog.
- Items 3, 5, 7: hold until fork tickets 012 (UI restrictions) and 004 (handoff format) decide whether they're needed.
