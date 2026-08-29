# Ticket 07 — Support-families impact on the support-preview and gizmo path

**Question.** Does the fork's support-preview path still work against `dbf3449c`, and what
does it need to show the new support families?

**Answer. Yes — the path is unbroken, and the 2.2.0 bump never touches it.** The support
families are a *Tier 2 planning* concept; the fork's preview overlay consumes a *prepass*
geometry document that stops at Tier 1 and whose contract is byte-identical across the bump.
One correction to the ticket's premise, one live-config caveat inherited from ticket 09, and
one decision (send `support_family`) fall out.

Method: every fact below was read from the fork tree and the submodule working tree at
`a50bfc28` (one commit past the bump), and the three "against the actual pnp_cli" points were
measured against a freshly staged `target/dist/developer/pnp_cli.exe` (wire 1.1.0) with a real
end-to-end `support-preview` run over a generated overhang model.

## 1. No schema bump on this path — the premise was wrong

The ticket assumed the preview must parse "the 2.2.0 document". It does not see one.

- `SupportPlanIR` moved 2.0.0 → 2.2.0 (packet 238c: `SupportPlanRole::BaseInterface`,
  `ExtrusionRole::SupportBaseInterface` — see `CURRENT_SUPPORT_PLAN_IR_SCHEMA_VERSION` in
  `crates/slicer-ir/src/slice_ir.rs`). But the preview verb consumes a different IR slot.
- pnp's emitter is `build_preview_doc` in `crates/pnp-cli/src/support_preview.rs`: it reads the
  **`SupportGeometryIR`** blackboard slot, produced by the host builtin
  `PrePass::SupportGeometry` (`commit_support_geometry_builtin` in
  `crates/slicer-runtime/src/builtins/support_geometry_producer.rs`), from
  `prepare_prepass_context` (`crates/slicer-runtime/src/run.rs`). **`SupportGeometryIR` is
  still schema 1.0.0 and its shape (`SupportGeometryKey`, `Vec<ExPolygon>`) is unchanged in the
  bump diff** (`git diff 1238ef02 dbf3449c` touches this IR by 0 lines for the geometry types).
- The roles/families live in the *Tier 2 planning* documents (`SupportPlanIR`,
  `RegionSegmentationView`/`RegionSupportConfig` in `crates/slicer-sdk/src/prepass_types.rs`),
  which `support-preview` never runs — it executes the prepass prefix only, exactly like
  before: `git diff` across the bump touches `support_preview.rs` by one line (the new
  `no_integrated_modules` argument, passed `false`).

**Live document, measured:** over a base+overhang STL, the staged binary emitted
`schema_version 1.0.0`, `units mm`, `layer_count 75`, `skipped_intermediate_entries 1`, 75
layers whose objects carry exactly `layer_index`/`z_mm`/`support` and whose polygons carry
exactly `contour`/`holes` — no new fields. The fork therefore parses, meshes, and renders the
current document unchanged: its gate (`parse_support_preview` in
`src/slic3r/GUI/PnpSupportPreviewDoc.cpp`) accepts schema major 1 and `mm` units, both still
hold, and unknown fields would be tolerated anyway.

## 2. The CLI surface is unchanged

`pnp_cli support-preview --help` on the staged binary: `--input`, `--output`, `--config`,
`--module-dir` (repeatable), `--no-default-module-paths` — the fork's call in
`run_support_preview` (`src/slic3r/GUI/PnpSupportPreview.cpp`) sends
`support-preview --input <3mf> --module-dir <dist>/modules --output <tmp>/support-preview.json`
and that parses. No `--no-default-module-paths` passthrough needed: the fork wants the
integrated tier (it *is* the planner tier here), so the packet-203 "known gap" that
`support-preview` cannot disable the integrated tier is the fork's desired behaviour, not a gap
from its seat.

## 3. Config plumbing: two forks-side caveats

- **Silent-drop of unknown keys is by design here, and the guard keeps it safe.**
  `apply_schema_guard` (`PnpConfigTranslator.cpp`) leaves any key the schema does not declare
  untouched ("unknown to the schema: pnp ignores it"), so `support_type` passes through even
  though no manifest declares it.
- **`enable_support` caveat inherited from ticket 09 (still correct after 05).** Ticket 05's
  identity pass sends `enable_support` correctly *under a probed backend*, but the runner sets
  `translated.json["enable_support"] = true` unconditionally before merging — so smoke tests
  are not blocked by the historical row, and the preview sees supports even when the preset
  disabled them.

### The family-selection keys

- **`support_type` — identity-routed, value-correct.** The derived identity pass
  (ticket 05) copies the raw Orca enum string because pnp declares `support_type` through the
  extensions channel that ticket 05's `UNDECLARED_LIVE_KEYS` bridge folds into the fork's key
  universe. pnp's `select_support_family` (`crates/slicer-scheduler/src/execution_plan.rs`)
  maps it through `canonical_support_family` (`crates/slicer-ir/src/slice_ir.rs`):
  `tree*`/`hybrid*` → tree, everything else → traditional. Orca's four enum values
  (`stNormalAuto`/`stTreeAuto`/`stNormal`/`stTree`) therefore all land on the intended family.
  The bug is prior: no family selection at all. Therefore preview-family selection is
  **correct** at the current wiring.
- **`support_family` — the fork does not send it, and should keep not sending it** (decision
  below).
- **`support_density` (retired)** — the curated row routes to nothing and sends nothing; the
  derived identity pass now sends `support_base_pattern_spacing` under its own name since
  traditional-support declares it (ticket 05). The preview config therefore carries the
  replacement key correctly.
- **`support_threshold_angle` / legacy `support_overhang_angle`** — pnp declares both
  (traditional-support-planner manifest, per ticket 01's inventory asset), so both pass the
  identity pass; the `coInt` vs pnp-`float` type gap is part of ticket 01's §B mismatch audit.

## 4. Version gate — present, and it is enough

The analog of the config-schema major gate exists and fired correctly in testing history:
`parse_support_preview` rejects unreadable or non-1 majors and non-`mm` units
(`PnpSupportPreviewDoc.cpp`), the runner reports the failure through
`consume_support_preview` → one `WarningNotificationLevel` notification. Since the contract did
not move across the bump (nothing to *catch*), this is as much a ticket as config-schema's gate
would have been: fields did not silently drop (there are none to drop).

## 5. The fork should NOT send `support_family` — decision

Ticket 01 framed the question ("decide whether the fork should send it"). Decision: **no.**

- `support_type` *currently overrides it* (`select_support_family`:
  `support_type.or(support_family)`), so sending both would either be redundant (values agree)
  or dead (type says tree, family says traditional — type wins silently, dull surprise).
- Orca has no native `support_family` concept, so the fork would be minting a knob with no GUI
  exposure, violating the map's zero-fork-edit principle (use Type; the family axis is already
  the user-facing one).
- `support_family` remains the **GG (guest-module / per-region)** channel: pnp-side
  `RegionSupportConfig` rows carry both spellings and the per-region claim dispatch consumes
  them. Until the fork models per-region family selection (it does not), there is nothing on
  the fork side to send. Handoff line for pnp: none — nothing is broken.

## 6. What the preview would need to *show* tree-vs-traditional and interface bands

Not parseable today — and not a rebuild-away either. Layout of the gaps:

- **Coarse outlines are role-free.** The document (and its source IR) carries one anonymous
  `support` array per layer. Extending this would be a pnp schema change (a new
  `schema_version 1.1.0` of the *document*, additive per
  `docs/20_support_preview.md`'s own rule, with a role tag per polygon or per layer).
- **Interface geometry does not exist yet at this stage.** The coarse prepass polys are
  candidate outlines; interface vs. base vs. trunk are decisions of the *planner*
  (`SupportPlanIR` roles), which the verb never executes. Tree vs. traditional *is* resolvable
  here in one sense: the family dispatch is per-region and derivable from the config the fork
  already sends — the fork could colour the whole overlay by selected family without any pnp
  change.
- **Fork-side, no code path carries a role colour.** The overlay volume is one `GLVolume` with
  `force_native_color` (`consume_support_preview` in
  `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.cpp`); splitting by role would need the mesh
  builder to emit per-role sub-meshes keyed on the new document attachment point.

Cost/scope: each of these is a pnp-side enhancement (document role split) plus one fork-side
mapping to per-role `GLVolume`s. Both are additive to a contract that lives in a
**single place that states it is fork-facing** (`docs/20_support_preview.md`), so this is the
right channel for the ask. Recorded in the graduated ticket, not done here.