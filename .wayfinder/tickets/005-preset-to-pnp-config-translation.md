---
title: Preset→PNP config translation spec
status: closed
type: research
assignee: Analysis Agent
blocked-by: [001]
---

## Question

Concrete key mapping from OrcaSlicer `DynamicPrintConfig` (see `src/libslic3r/PrintConfig.cpp`) to PNP JSON config (via `pnp_cli module config-schema`). Decided strategy: PNP's config system is adopted; Orca presets load and pass through; keys PNP can't resolve are marked with warnings; only PNP-accepted keys do work. This ticket produces the mapping table, the pass-through-with-warnings mechanism design, and where warnings surface in the UI.

## Answer (2026-07-16)

Full mapping spec: [assets/005-preset-to-pnp-config-mapping.md](../assets/005-preset-to-pnp-config-mapping.md).

**In brief:** Orca has 925 config keys (`PrintConfig.cpp`); PNP consumes 115 unique module keys (from live `pnp_cli module config-schema`) plus ~43 host-registered keys (`docs/config/host-keys.toml`). PNP config JSON uses **flat keys** — 65 are already Orca-identical, ~30 need a rename/transform row (all Orca source names verified: `wall_loops`→`wall_count`, `initial_layer_print_height`→`first_layer_height`, `sparse_infill_density`→`infill_density`, `z_hop`→`travel_z_hop`, `enable_prime_tower`→`wipe_tower_enabled`, `fuzzy_skin*`→fuzzy-skin module keys, `wall_generator` identity, etc.), ~20 PNP keys are internal with no Orca source (defaults rule), and the remaining ~830 Orca keys are **unresolved**: not sent, each recorded as a warning classified `unsupported-feature` / `not-yet-mapped` / `no-op`.

**Mechanism:** one static data-driven table `{orca_key, pnp_key, transform, warning_class}` in a new `src/slic3r/GUI/PnpConfigTranslator.{hpp,cpp}`, consuming `preset_bundle->full_config()` at slice time, emitting the PNP JSON temp file + a warning vector. Adding PNP coverage later = adding a table row. Notable semantic traps: `seam_position: aligned` has no PNP mode (fallback `nearest` + warn); `raft_layers` maps but PNP has no standalone raft; enum spellings (`wall_sequence` etc.) need normalization; per-extruder vectors collapse to element 0 for v1.

**Where warnings surface in the UI** graduates to its own ticket (013) — the mechanism produces the warning list; presentation is a UX decision.

**Handoff items recorded in the asset:** machine-readable `orca_key` field in the config-schema JSON; explicit unknown-key contract for pnp_cli; `aligned` seam mode.
