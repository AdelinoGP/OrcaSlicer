# Handoff: PNP-side gaps blocking the OrcaSlicer-frontend fork

Audience: a planning session in `F:\slicerProject\pinch_n_print` using the spec-packet workflow (`/spec-packet-generator`, then `/spec-review --preflight`, then `/swarm`). Source: [feature-coverage inventory](001-pnp-feature-coverage-inventory.md) (ticket 001 of the fork's wayfinder map), verified against the live release binary and pnp docs on 2026-07-16.

Context: the fork replaces OrcaSlicer's in-process slicing with `pnp_cli slice` shell-outs (v1 scope: slice + preview, single plate per invocation, Windows-first). Each item below is a PNP capability gap the fork's GUI currently has UI surface for. Priorities reflect fork impact; the fork side may independently decide to hide UI instead (its ticket 012), so confirm priorities with the human before packeting.

## Items

1. **Print-time estimation** — `estimated_print_time_s` is hardcoded 0 in `crates/slicer-gcode/src/emit.rs` (`PrintMetadata`); no kinematics/acceleration model exists. Orca's preview and sidebar show per-feature/per-layer time. Needs an estimator (even a simple feedrate-distance model beats 0) surfaced in G-code metadata. High priority — visible on every slice.
2. **`slice_stats` progress event** — reserved in `docs/09_progress_events.md` schema 1.2.0 (`gcode_prediction_seconds`, `gcode_weight_grams`, `gcode_filament_length_mm`, `layer_count`, `first_layer_height_mm`) but not emitted. Implementing it lets the GUI get stats without parsing G-code. Natural companion packet to item 1.
3. **Standalone raft** — no raft-under-part feature; only `support_planner`'s `support_raft_layers` (default 0) exists. Orca UI exposes raft controls. Medium priority; fork may hide the control in v1.
4. **Multi-material end-to-end proof** — plumbing exists (wipe-tower module, `T<n>` tool changes, per-region extruder via 3MF modifier config, filament colour arrays) but open backlog items block exposure: TASK-210 (support_filament / support_interface_filament routing), TASK-211 (real-fixture T0/T1 G-code E2E, currently synthetic-only), TASK-212 (object-scoped printer-key sidecar allowlist only partially populated). These are existing pnp backlog tasks — packet them rather than re-scoping.
5. **G-code flavor selection** — `gcode_flavor` is hardcoded `marlin` in `crates/slicer-gcode/src/serialize.rs` CONFIG_BLOCK; no Klipper/RepRap variants. Low priority if the fork restricts printer profiles to Marlin, but decide deliberately.
6. **Non-uniform scale support** — `slicer-model-io` loader rejects non-uniform-scale transforms (`NonUniformScaleUnsupported`). Orca's GUI freely applies non-uniform scaling. **Confirmed PNP-side by fork ticket 004 (2026-07-16):** the fork will NOT bake scale at export; its GUI blocks slicing with an error until PNP lifts the restriction (bake scale into vertices at load). Medium-high priority — real user-visible blocker.
7. **Multi-plate 3MF** — **Won't-fix, confirmed by fork ticket 004 (2026-07-16):** the fork hands PNP one plate per invocation as a plate-local-origin single-`<build>` 3MF. Close this item.
9. **Orca per-object config-key parity in the 3MF sidecar parser** — fork ticket 004 (2026-07-16): the fork writes `model_settings.config` per-object/per-volume config deltas untouched, using Orca key names, and also writes per-volume extruder/filament assignments as-is. PNP's sidecar parser should accept/translate Orca per-object key names (mirroring the global-key translation table the fork uses, its ticket 005); until then unknown per-object keys are silently ignored. Medium priority — per-object overrides silently no-op otherwise.
10. **Viewer-config passthrough keys in CONFIG_BLOCK** — fork ticket 007 (2026-07-16): emit `printer_model` (any non-"Bambu Lab" value; guards Orca's `s_IsBBLPrinter` tag-table derivation on drag-in), `filament_density`, `filament_cost`, `printable_area`, `nozzle_diameter`, and the `machine_max_acceleration_*`/`machine_max_speed_*`/`machine_max_jerk_*` family in the CONFIG_BLOCK (`crates/slicer-gcode/src/serialize.rs` padding or host raw_config passthrough). Orca's viewer computes time/mass/cost itself from moves + these keys; without them it falls back to defaults (approximate times, default-density grams). Low effort, low risk — the serializer already writes raw_config keys verbatim.
8. **Seam live-path gaps** — TASK-120c: seam-placer reads `resolved_seam` instead of selecting from `PerimeterIR.regions[*].seam_candidates`; rotated-wall replacement can erase sibling walls unless the full region wall set is re-emitted. Existing pnp backlog item; correctness risk on every slice with seam control.

## Not gaps (do not re-implement)

- Thumbnails: PNP's external-PNG-in `--thumbnail` design is fine; the fork's GUI renders the PNG (fork ticket 011).
- `;TYPE:` annotations, LAYER_CHANGE markers, HEADER/CONFIG/THUMBNAIL blocks: already Orca-viewer-compatible.

## Suggested packet grouping

- Packet A: items 1+2 (time estimation + slice_stats) — one workstream, shared metadata plumbing.
- Packet B: item 4 (close TASK-210/211/212) — existing backlog.
- Packet C: item 6 (non-uniform scale) — small, isolated in `slicer-model-io`.
- Packet D: item 8 (TASK-120c seam) — existing backlog.
- Packet E: item 9 (Orca per-object key parity) — sidecar parser work; can share the translation table shipped by the fork.
- Items 3, 5: hold until fork ticket 012 (UI restrictions) decides whether they're needed. Item 7: closed won't-fix (fork ticket 004).
