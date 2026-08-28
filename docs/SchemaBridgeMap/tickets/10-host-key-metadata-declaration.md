---
title: Declare host-key display metadata in pnp's config DSL
status: open
type: task
assignee:
blocked-by: [04]
---

## Question

pnp-side commit. Host keys reach the wire as `{key, type, default, scope}` only, so 20 of the
PNP page's 53 controls have no label but their key name, no group, and no range.

Annotate display metadata directly on the `cli` / `cli_opt` declaration in
`crates/slicer-ir/src/resolved_config.rs`, beside the `@scope` form ticket 02 added, and emit it
in `build_host_key_entries` (`crates/slicer-scheduler/src/manifest.rs:1586`). Wire
`1.1.0 -> 1.2.0`, additive: `display`, `group`, `min`, `max`, `unit` on each host entry.

Chosen over promoting `docs/config/host-keys.toml` onto the wire because that file mirrors only
11 of the 20 — the doc-lock test pins agreement, not completeness, and the 9 absent keys
(`arachne_min_feature_size`, `fill_authored_coloring`, `mmu_segmented_region_interlocking_beam`,
`nonplanar_amplitude`, `nonplanar_max_angle_deg`, `nonplanar_shell_count`,
`smoothificator_adaptive`, `smoothificator_target_height`, `solid_infill_speed`) show exactly how
that gap reopens. Annotating the declaration site closes it by construction.

Decide:

- Whether every host key must carry `display`/`group` (a compile error when absent, as
  `HostWireField` does for types) or whether an un-annotated key falls back to key-name-as-label.
- The group vocabulary, and whether it must agree with the module manifests' `group` strings —
  the fork buckets both halves by the same field, so `Speed` and `speed` would render as two
  optgroups.
- Group assignments. The prototype proposes them for all 20; they are a proposal, not a decision.
- Whether the fork should also gain a lower bound for host keys whose `host-keys.toml` `range`
  is prose (`"> 0"`, `">= 0"`), or whether that stays documentation.

Also in scope, since it is the same file and the same wire bump — the manifest-quality items the
prototype turned up, if they are cheap here rather than as their own ticket:
`wave_overhang_pattern` and `flat_bridge_closing_join` are declared `string` with their domains
written into prose and should be `enum` + `values`; enum domains carry no display labels, so
dropdowns render raw identifiers.

Deliverable: submodule commit plus a bump of `pinch_n_print_cli` in this repo, per the map's
both-repos-in-play note.
