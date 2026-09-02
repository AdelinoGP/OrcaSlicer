---
title: Surface minted modifier sub-regions to the layer plan and blackboard slice
status: closed
type: task
assignee: opencode-agent
blocked-by: [18]
---

## Question

Ticket 18 bound each modifier's config delta to its minted sub-region in the
`RegionMapIR` (per-modifier entries, base region pure), and the support
analysis records the per-region family in `family_assignments`. But the
minted sub-regions still never reach the two inputs the support PLANNERS
route on:

- `LayerPlanIR.active_regions` is module-emitted (`layer-planner-default`
  emits `region_id "0"` per object), so `module_receives_slice_region`
  (`slicer-wasm-host/src/dispatch.rs`) gates support-family modules on
  active regions that do not exist for the sub-region id — a tree-family
  sub-region is never offered to the tree planner.
- The blackboard `SliceIR` (committed at `PrePass::Slice`, which mirrors the
  active regions) has no sub-region geometry, so contact detection
  (`commit_support_analysis_builtin`) derives no candidates inside the
  footprint — `family_assignments` says "tree" but there is nothing to plan.

Measured (ticket 18, 2026-09-01, all code-traced): the sub-region geometry
exists only in the per-layer arena `SliceIR` minted at `Layer::Perimeters`
(`split_modifier_footprints`), which is after every prepass consumer; the
region-map kernel re-derives the same ids in prepass via
`slicer_ir::modifier_sub_region_id` + `slice_mesh_ex` (ticket 18), so the
ids are already reproducible at prepass time without touching Tier 2.

Resolve — surface the minted sub-regions to the prepass consumers:

- **Layer plan.** Add an `ActiveRegion` per minted sub-region (empty
  `variant_chain` for an unpainted parent; the painted parent's chain for a
  painted parent, with the parent ID encoded in the modifier namespace) to the
  committed `LayerPlanIR` at a host seam after `PrePass::RegionMapping` (the
  kernel already knows the ids; the seam must not fight the module-emitted
  plan or the WIT round-trip). `backfill_active_region_configs` (plan
  promotion) then copies the per-modifier config onto the sub-region's
  `resolved_config`, and `module_receives_slice_region` routes it to the right
  family planner.
- **Slice.** Mint the sub-region `SlicedRegion`s (id + footprint geometry,
  base polygons reduced by the footprint) into the blackboard `SliceIR` at
  `PrePass::Slice` — the same intersection semantics
  `split_modifier_footprints` uses, so the prepass slice and the Tier-2
  arena slice agree — so contact detection produces candidates inside the
  footprint.
- **Tests.** Extend the ticket-18 e2e (or add a sibling): with
  `support_enabled` and a modifier `support_type=tree(auto)`, the support
  plan actually contains entries inside the footprint (the tree planner
  routes the sub-region's candidates), and the base region's candidates stay
  with the traditional planner. Keep the region-map and `family_assignments`
  assertions from ticket 18 green.

Deliverable: a modifier volume's `support_type` changes the support actually
planned inside its footprint — not just the recorded family — with the
prepass slice and the Tier-2 arena slice agreeing on sub-region geometry.

## Answer (2026-09-02)

Implemented in the `pinch_n_print_cli` submodule.

- `commit_region_mapping_builtin` adds each modifier-namespace `RegionMapIR`
  entry to the committed `LayerPlanIR.active_regions`, preserving the base
  region's effective-layer metadata and assigning the sub-region's resolved
  config.
- `split_modifier_sub_regions_for_prepass`, called from the configured prepass
  after paint segmentation, materializes the same per-layer modifier geometry
  that Tier 2 uses. It partitions base fill roles by descending modifier
  priority with stable document-order ties, skips support enforcer/blocker
  volumes, keeps each painted parent's chain on its child, and leaves every
  parent pure outside its own modifier child.
- `execute_prepass_slice_single_layer_impl` skips already-surfaced modifier
  active regions so they are not re-sliced as full objects. The Tier-2
  `split_modifier_footprints` path remains a raw-footprint fallback, while
  `perimeter_source_regions` restores the unsplit base outline for perimeter
  modules and preserves wall sharing.
- `modifier_sub_region_id`, `is_modifier_namespace_id`, and
  `modifier_base_region_id` are shared by the kernel, runtime, and wasm host.
  Module-authored layer plans reject the host-reserved modifier namespace and
  raw footprint sentinel.

Coverage includes production support-family routing and structural support
planning in `modifier_support_type_family_e2e_tdd`, role preservation and
priority overlap regressions in `modifier_region_split_tdd`, and equal-priority
identical-footprint config ownership in `algo_region_mapping_tdd`.

Verification: `cargo check --workspace --all-targets`, workspace clippy with
`-D warnings`, `cargo xtask check-literals`, `cargo xtask build-guests --check`,
the targeted core/wasm-host/runtime suites, and `git diff --check` pass.
The repository-wide `cargo fmt --all -- --check` remains blocked by Windows
path-length error 206; all touched Rust files pass direct rustfmt checking.
