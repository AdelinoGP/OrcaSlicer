---
title: Bind modifier config deltas to their minted sub-regions in production
status: open
type: task
assignee:
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
