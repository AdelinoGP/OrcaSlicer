---
title: Surface minted modifier sub-regions to the layer plan and blackboard slice
status: closed
type: task
assignee: claude-code-agent (reopen); opencode-agent (first close)
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

## Reopened 2026-09-02

The 2026-09-02 answer above was closed on the inverse direction only (base
traditional, thin tree modifier band). The user's `SupportTest.3mf` (global
`support_type=tree(auto)`, one modifier volume over the +y half with
`support_type=normal(auto)`) still sliced wrong in the GUI: tree everywhere, a
bad layer near the top, sparse infill reaching the top layer. A headless loop
separated three independent causes:

1. **Stale dist.** `pinch_n_print_cli/target/dist/developer/pnp_cli.exe` and the
   bundled `build/windows/x64/release/pnp_cli.exe` predated `04ec7c09` (no
   `split_modifier_sub_regions_for_prepass` symbol). The stale binary reproduces
   the user's screenshot exactly. Ticket 16 (restage guard) is still open, so
   nothing warned — the same bit that closed ticket 06/08.
2. **Cross-family annihilation.** With the fresh backend both planners emitted
   bodies under the same overhang (the tree's branches drift into the free air
   under the modifier half; the traditional column covers it) and the host's
   cross-family guard in `support_aggregation.rs` rejected BOTH sides on every
   overlapping layer: 2121 `cross-family positive-area overlap` rejections on
   the minimal repro, tree stopping at z = 4.0 (18.0 under the GUI config), no
   traditional support anywhere. Root cause: no notion of *territory* — the
   tree planner's only barred areas were the model's own slices, the
   traditional planner never intersected its column with its footprint, and
   the guard could only annihilate, never clip.
3. **Tier-2 dispatch gap.** Even with the plan correct, the traditional
   renderer never ran below the overhang: both Tier-2 gates (the per-layer
   scheduler and `module_receives_slice_region`) key on `active_regions`,
   which only lists regions with geometry, and the modifier sub-region has no
   cross-section under the overhang it modifies. Support lives in free air,
   so the family renderer must run wherever its plan holds bodies.

Two residual top-layer defects (modifier-independent, GUI config only) were
taken along: **R1** — the two shell layers under the top printed with no
inner walls (29.8 with no walls at all) and 29.6 as one whole-layer external
`Bridge`; **R2** — with the modifier, the wall seam ran along the modifier
edge on those layers.

## Answer (reopened, 2026-09-02)

Implemented in the `pinch_n_print_cli` submodule.

- **Support territory** (`SupportAnalysisIR` 1.2.0 → 1.3.0, additive
  `support_territory`, mirrored on the WIT `support-analysis-view`): the host
  publishes each minted sub-region's full modifier cross-section per layer,
  keyed by the same FNV id the region map carries, plus
  `shared_settings["support_territory_clearance_mm"]` (resolved support line
  width). One clip rule, shared through
  `SupportAnalysisView::territory_partition` / `region_territory`: a sub-region
  body keeps `roles ∩ own`, a base-region body keeps
  `roles − inflate(foreign, clearance)`. The tree planner folds foreign
  territory into its collision ladder (avoidance routes around it) and carves
  it from roles and skeleton points; the traditional planner clips its carry;
  host aggregation (`slicer_wasm_host::support_territory`) re-applies the
  rule and reports the trim as Info 1205 instead of rejecting both families.
  Orca has no per-region support family — filed as DEV-159.
- **Support carrier regions** (`dispatch::support_carrier_regions`): a
  family renderer receives an empty-geometry region for every
  `(object, region)` its plan claims on a layer that the slice does not carry,
  and the per-layer scheduler runs it there. Native and WASM paths.
- **R1**: the guest `SliceRegionView` never received `internal_solid_fill`
  (missing from the `slicer-macros` marshal), so `only_one_wall_top`'s second
  pass read the whole shell shadow as exposed top, walled it once, and — when
  the sliver remainder failed preprocessing — returned with no walls at all.
  The guest now uses `top_solid_fill − internal_solid_fill` as the top area
  and keeps the top wall on a second-pass failure. The infill module now
  emits the host-qualified internal-bridge sites as `InternalBridgeInfill`
  (bridge over sparse infill, canonical `stInternalBridge`) instead of
  external `Bridge` — that half-closes DEV-153, whose AC-6 pin in
  `calicat_internal_bridge_gating_e2e_tdd` recorded the missing label as
  measured truth and therefore went red on the fix. The pin is replaced by
  the canonical-correct expectation plus a conservation assertion: on
  `calicat.stl` the label flip moves 3 layers (z 4.45 / 18.45 / 29.45) from
  the `Bridge` bucket to `Internal Bridge` with combined bridge-labelled
  extrusion 25.39 mm -> 25.49 mm, so it is a relabel, not new geometry.
  DEV-153's other half — the distance from either number to canonical's
  ~950.56 mm, i.e. bridge geometry never classified as bridge at all —
  stays open and is untouched here.
- **R2**: `perimeter_source_regions` restores every fill mask from the
  modifier child into the base region, not only `polygons`.

Measured on `resources/support_test_modifier_normal_in_tree.3mf` (minimal
repro) after the fix: 0 cross-family rejections (was 2121), 0
`module_error` events, tree `Support interface` up to z = 24.8, traditional
support in the modifier half (x > 105.3, y > 98.6) on all 124 support
layers, control (modifier removed) unchanged. Under the GUI config
(`…_gui.3mf`): layers 29.6 / 29.8 carry the same wall loops as 25.4, 29.6 is
`Internal Bridge`, 29.8 `Internal solid infill`, 30.0 a single wall; wall
loops equal the modifier-free control on 29.6 / 29.8 / 30.0.

Coverage: `modifier_support_territory_e2e_tdd` (prepass level, G-code level
through the real `pnp_cli`, control, and the R1/R2 GUI-config regression),
`support_cross_family_scope_tdd` split into without/with territory,
`support_carrier_regions_tdd`, the producer territory unit tests, the
`perimeter_source_regions` mask test, and guest tests on both planners
(foreign territory barred from tree roles and skeleton; traditional column
inside own footprint / clear of foreign).

Verification: `cargo check --workspace --all-targets`, workspace clippy with
`-D warnings`, `cargo xtask check-literals`, `cargo xtask build-guests
--check` (exit 0), the narrow suites for every touched crate, and the
workspace `cargo xtask test --summary` at close. Fork side: dist restaged
(`cargo xtask dist --edition developer`) and bundled through xmake; the
bundled binary carries the territory symbols and reproduces the loop
numbers above. GUI slice of `SupportTest.3mf` is a manual smoke owed to an
interactive session.
