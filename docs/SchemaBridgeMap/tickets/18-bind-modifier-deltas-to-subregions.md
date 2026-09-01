---
title: Bind modifier config deltas to their minted sub-regions in production
status: resolved
type: task
assignee: opencode-agent
blocked-by: [17]
---

## Question

Ticket 17's fork half lets a modifier volume carry `support_type` (UI gate
lifted, 3MF export path verified), but pnp's **production** region mapping
stamps every modifier's `config_delta` object-wide, so the value would change
the whole object's family instead of the modifier's footprint. The per-region
machinery exists; the production call site does not. Wire it.

Measured (ticket 17, 2026-09-01, all code-traced):

- `slicer-runtime/src/region_partition.rs::split_modifier_footprints` mints
  modifier sub-regions (`base * MODIFIER_VARIANT_REGION_ID_STRIDE + hash`
  over object id + footprint polygon geometry) — production geometry path,
  called from the runtime partition.
- `slicer-runtime/src/layer_executor.rs::stage_modifier_footprints` stages
  `MODIFIER_FOOTPRINT_REGION_ID` footprints with `..Default::default()` —
  **no modifier identity, no resolved config** rides on the footprint, so
  downstream code cannot tell which modifier a sub-region came from.
- `slicer-core/src/algos/region_mapping.rs::execute_region_mapping_inner`
  (the production RegionMapIR builder, invoked by
  `commit_region_mapping_builtin`) stamps `stamp_modifier_config_deltas`
  (object-wide) for every region; the per-sub-region counterpart
  `stamp_modifier_sub_region_configs` (packet 132) has **zero production
  call sites** — only the contract/e2e tests call it with hand-built maps.
  Production always passes `host_config = Some(...)`, and the per-object
  host config wins over `region.resolved_config`, so a minted sub-region's
  (empty) module-emitted config never carries the modifier delta either.
  The `mixed_density_internal_bridge_rejection` e2e documented the practical
  symptom: the model-driven `cube_cilindrical_modifier.3mf` collapsed to a
  single object-wide region.

Resolve — wire the binding end to end:

- **Binding.** The minted sub-region id is a pure function of
  `(base_region_id, object_id, footprint polygons)` (FNV-1a in
  `modifier_sub_region_id`), and the footprint polygons are the modifier
  mesh's cross-section at the layer Z via `slicer_core::slice_mesh_ex` —
  the same inputs `stage_modifier_footprints` used. Either re-derive the
  owner in the region kernel (object's `modifier_volumes`, same slice +
  hash — deterministic, costs the already-paid slice per layer), or carry
  the binding forward from the staging site (a field on the footprint or a
  map threaded from `stage_modifier_footprints` to the kernel). Prefer the
  carried binding if it survives the arena round-trip; the re-derivation if
  not. Skip `support_enforcer`/`support_blocker` subtypes and empty
  `subtype` handling exactly as both stamping fns do today.
- **Kernel change.** In `execute_region_mapping_inner`, when the region is a
  modifier-namespace sub-region (empty `variant_chain`, id in the modifier
  namespace — predicate precedent `is_modifier_namespace_id` in
  `slicer-wasm-host/src/dispatch.rs`), stamp only the owning modifier's
  delta onto the base (use `stamp_modifier_sub_region_configs` and take the
  sub-region's entry); base regions keep the base config. Keep the
  object-wide arm for non-modifier regions or until the sub-region path is
  proven.
- **Tests.** (1) kernel-level unit case in the region-mapping tests: two
  modifiers with distinct `support_type` deltas on one object → base region
  keeps the global family, each minted sub-region carries its own. (2)
  extend or add a model-driven e2e (the `mixed_density` AC-N1 shape is the
  template) proving per-region family assignment reaches the support
  analysis (`support_analysis_producer::family_assignments`). (3)
  `pnp_cli slice` smoke is optional — the region-map assertions are the
  contract.
- Fork side needs no further work: `support_type` already rides in the part
  metadata and pnp's loader reads every part key into
  `ModifierVolume.config_delta`.

Deliverable: a modifier volume's `support_type` (and any other region-scoped
delta) applies inside its own footprint only — base region untouched —
verified at the region-map level and in the support analysis; sub-region
geometry semantics unchanged.

## Answer (2026-09-01)

**pnp half landed** (commit `11175ae2` on the submodule's `master`). The
binding is **re-derived, not carried** — the carried option cannot survive:
region mapping runs in prepass BEFORE `PrePass::Slice` and long before
`Layer::Perimeters` mints the sub-regions in the per-layer arena, so a field
on the staged footprint never reaches the kernel. The minted sub-region id
is a pure function of `(base_region_id, object_id, footprint polygons)`
(FNV-1a), and the footprint polygons are the modifier mesh's cross-section at
the layer Z via `slice_mesh_ex` — the exact inputs `stage_modifier_footprints`
used — so the kernel re-derives the Tier-2 ids byte-for-byte.

- **Shared namespace primitives.** `slicer-ir` now owns
  `MODIFIER_VARIANT_REGION_ID_STRIDE`, `modifier_sub_region_id` and
  `is_modifier_namespace_id`; `slicer-runtime::region_partition` and
  `slicer-wasm-host::dispatch` previously restated them (a drift hazard) and
  now read the shared copies.
- **Kernel change.** `execute_region_mapping_inner` mints one `RegionMapIR`
  entry per stampable modifier footprint per (layer, base region), stamped
  with only the OWNING modifier's delta via `stamp_modifier_sub_region_configs`
  (identical-footprint modifiers merge priority-ascending into one entry).
  The base region's own empty-chain entry keeps the **pure base config** —
  the object-wide `stamp_modifier_config_deltas` arm survives only for
  painted variant chains and for objects without modifier volumes (where it
  is a no-op). Orphan entries (footprint non-empty but disjoint from the
  base at that layer) are benign: no `SlicedRegion` materialises them.
- **Tests.** (1) Kernel unit case in `algo_region_mapping_tdd`: two modifiers
  with distinct `support_type` deltas on one object → base keeps the global
  family, each minted sub-region carries its own, ids equal the Tier-2 hash.
  (2) Model-driven e2e (`modifier_support_type_family_e2e_tdd`) through the
  PRODUCTION call sites — `commit_region_mapping_builtin` then
  `commit_support_analysis_builtin` — proving `family_assignments` gets
  base=`traditional` / sub-region=`tree`. (3) `pnp_cli slice` smoke: taken
  implicitly — the full e2e binary (137 tests, incl. the real `pnp_cli`
  slice pipelines) is green against a freshly rebuilt debug `pnp_cli`.
- **Fixture updates.** AC-Mod synthetic modifiers now carry a z-extent cube
  mesh (the flat z=0 triangle slices empty at the plan Z, so the modifier
  was invisible to the mint); AC-N2 updated to the per-region semantics
  (base pure, merged sub entry carries the higher-priority `extruder`).
- **Drive-by.** `runtime_wiring_tdd::config_schema_json_matches_documented_shape`
  pinned the wire version at `1.0.0` while `CONFIG_SCHEMA_WIRE_VERSION` is
  `1.2.0` since ticket 10 — proven red on the pre-change tree via stash;
  the literal now tracks the constant.
- **Verification.** slicer-core `host-algos` suite (87 binaries), slicer-runtime
  e2e (137) / unit (89) / integration (323) / contract (295) / executor (209),
  `cargo clippy --workspace --all-targets -- -D warnings`, `check-literals`,
  `build-guests --check` (guests rebuilt after the slicer-ir change).

**Residuals.** (1) The fork's bundled dist still runs the pre-ticket-18
kernel until `cargo xtask dist` + xmake bundle re-stage — mechanical, and
the ticket's contract is the region-map assertions, so it is not part of this
resolution. (2) The minted sub-regions still do not reach
`LayerPlanIR.active_regions` (module-emitted, `region_id "0"` only) or the
blackboard `SliceIR` (prepass slice mirrors active regions), so the support
PLANNERS do not yet route candidates inside the footprint: `family_assignments`
is correct, but no candidates exist for the sub-region id. That leg is
[ticket 19](19-surface-subregions-to-layer-plan-and-slice.md).
