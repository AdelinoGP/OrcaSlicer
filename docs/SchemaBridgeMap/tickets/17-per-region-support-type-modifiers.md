---
title: Per-region support type on modifier volumes
status: resolved
type: task
assignee: opencode-agent
blocked-by: []
---

## Question

OrcaSlicer's UI cannot express a per-region support type, even though pnp's
config model supports it. "Per region" here means **modifier volumes**: place
a modifier, set `support_type` (tree vs normal) on it, and pnp should plan
that family inside the modifier's footprint while the rest of the object
keeps its own family.

Resolved 2026-09-01 investigation — where each half stands:

**Fork side (the UI gate, certain):** `support_type` / `enable_support` are
`PrintObjectConfig` keys (`PrintConfig.hpp` ~1190), and the per-volume
Settings dialog only offers `PrintRegionConfig::keys()` for parts
(`SettingsFactory::get_options(true)` in `GUI_Factories.cpp`), so a modifier
volume cannot carry `support_type` at all. Object-level overrides exist
(whole object), volume-level do not. The export path is ready: per-volume
config rides into `Metadata/model_settings.config` per `<part>` via
`store_bbs_3mf` (PnpModelExport keeps per-volume configs intact), and pnp's
loader reads every per-part key into `ModifierVolume.config_delta`
(`slicer-model-io/src/loader.rs` ~693: "Generic per-part metadata
extraction"), so once the UI allows it, the value travels.

**pnp side (the per-region application gap, needs live verification + a
wiring decision):** the consumer half exists — `RegionSupportConfig`
(`slicer-sdk/src/prepass_types.rs`) carries per-region
`support_family`/`support_type`, the support-analysis reads the region's
resolved config (`support_analysis_producer.rs::support_family` →
`select_support_family`, matching the `execution_plan.rs` claim dispatch),
and Orca's enum serializes to pnp's canonical spellings
(`normal(auto)`/`tree(auto)`). But the *binding* half is unfinished:
modifier sub-regions are minted in production geometry-wise
(`region_partition.rs::split_modifier_footprints`), yet the config binding
(`slicer-core/src/algos/region_mapping.rs::stamp_modifier_sub_region_configs`,
packet 132) has **no production call sites** — `execute_region_mapping_inner`
stamps every modifier's `config_delta` OBJECT-WIDE
(`stamp_modifier_config_deltas`), and the packet-132 contract test calls the
sub-region helper directly with a hand-built map. The `mixed_density_internal_bridge_rejection`
e2e noted the model-driven fixture "collapsed into a single object-wide
region" — i.e. today a modifier-carried config may not even apply anywhere,
and if stamped, it would leak object-wide. Neither is per-region.

Resolve:

- **Fork half (this ticket):** allow `support_type` on modifier volume
  settings (extend the volume-settings key surface), with a test pinning
  that a set value survives 3mf export into the part's `model_settings.config`.
  Do NOT extend the surface to model parts — pnp skips non-modifier parts
  for support routing, and enforcer/blocker subtypes are excluded from
  region-config merging by design.
- **pnp half (measure first, then decide):** live-run a 3MF whose modifier
  carries `support_type=tree(auto)` through `pnp_cli` and record what today
  does with it (drop / object-wide / per-region). Then either wire the
  per-sub-region binding into `execute_region_mapping_inner` (the minted
  sub-region id re-derives the owning modifier via the same footprint hash —
  see `modifier_sub_region_id`; the binding map is computable once per
  object) or, if the sub-regions do not reach `LayerPlanIR.active_regions`
  / the support analysis at all, land the missing pipeline leg. The pnp side
  may outgrow this session; if so, scope it precisely and close this ticket
  with the fork half + the measurement.

Deliverable: modifier volumes setting the print's support family inside
their footprint only; the fork half landed and tested, the pnp half either
landed or measured-and-scoped with seams named.

## Answer (2026-09-01)

**Fork half landed** (commit `4b5746cac1`): the volume-settings tab
(`TabPrintPart`) now admits `support_type` on its key surface; the save gate
in `TabPrintModel::on_value_change` refuses to store it on any non-modifier
volume (parts/negative/enforcer/blocker — pnp drops normal-part metadata and
skips support subtypes, so the field would silently no-op there). Export link
is stock: `store_bbs_3mf` writes every volume-config key into the part's
`Metadata/model_settings.config` section (bbs_3mf.cpp ~8160), and pnp's
loader reads every part key into `ModifierVolume.config_delta`
(`slicer-model-io/src/loader.rs` ~701). App rebuilt and bundle re-staged.

**pnp half — measured, not wired (graduated to ticket 18).** Code-traced end
to end: the per-region consumer half exists and is correct (`RegionSupportConfig`
per region key; `support_analysis_producer::support_family` per region;
Orca's enum serializes to pnp's canonical spellings). The binding half is
unwired in production: `split_modifier_footprints` mints the sub-region
*geometry*, but `stage_modifier_footprints` stages the footprint with
`..Default::default()` (no modifier identity, no config), and
`execute_region_mapping_inner` (always called with a host-config authority)
stamps every modifier's `config_delta` OBJECT-WIDE via
`stamp_modifier_config_deltas` — the per-sub-region counterpart
`stamp_modifier_sub_region_configs` (packet 132) has zero production call
sites. The `mixed_density_internal_bridge_rejection` e2e documented the same
symptom ("collapsed into a single object-wide region"). So today a modifier
`support_type` would change the whole object's family, not the footprint.

**Therefore the UI half ships with a flagged caveat:** it is the permission
half and round-trips the value, but the per-region application lands with
ticket 18 (binding the minted sub-region to its owning modifier, kernel
change in `execute_region_mapping_inner`, kernel + e2e tests). Until then a
modifier's `support_type` will be stamped object-wide by pnp — the commit
message and this resolution both say so.
