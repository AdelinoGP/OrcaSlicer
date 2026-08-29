---
title: Declare host-key display metadata in pnp's config DSL
status: closed
type: task
assignee: Adelino Penedo
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

## Resolution (2026-08-29)

pnp-side commit `263b81d3` (submodule pointer bumped to it on `pnp/main`,
`15f671da57`). The wire gained optional display metadata — `display`, `group`,
`unit`, `description`, `min`, `max`, `values`, `advanced` on each `host` entry,
`null` where un-annotated — declared at each key's declaration site, wire
`CONFIG_SCHEMA_WIRE_VERSION` 1.1.0 → 1.2.0 (additive; a 1.1.0 consumer ignores
the new fields). **The fork needed no code change**: `PnpConfigKeys.cpp`
`field_to_def` already reads every one of these fields from any wire field and
`pnp_register_config_keys` already applies them, so the bump alone labels,
groups, ranges and enums the PNP page's host-key controls.

### The four open decisions

- **Compile error vs fallback** — fallback, decided by evidence. The
  `HostKeyMeta` annotation splices FRU over `HostKeyMeta::NONE`, so a
  *misspelled metadata field* is a compile error at the declaration site (the
  ticket's real safety concern), but an *un-annotated key* is not an error:
  the module manifests already treat `display`/`group` as optional
  (`get_string_opt` in `manifest.rs::parse_config_field_entry`), and forcing
  metadata onto every host key would author strings for the Orca-identity
  keys the fork never shows (see next bullet). Un-annotated → `null` → the
  fork renders the key name.
- **Group vocabulary** — host annotations reuse the module manifests' group
  strings verbatim. Measured from all 24 manifests: the prototype's `Travel &
  Retraction` does not exist in manifests (they say `Travel Retraction`);
  reusing the manifests' exact strings is why the fork buckets both halves of
  the reply into the same optgroups by construction. New buckets only where
  nothing fits: `Output`, `Nonplanar`, `Smoothificator`, `Multimaterial`
  (`prime_tower_speed` joins the existing `Multimaterial`, `wipe_tower_speed`
  the existing `Wipe Tower`).
- **Which keys get annotations** — measured, not copied from the prototype.
  Only keys the fork would otherwise render as a bare key name: no Orca
  `print_config_def` counterpart *and* no module-manifest declaration. The
  two-mechanism pass against `PrintConfig.hpp`'s macro table (the `add()`
  probe alone was wrong — Orca declares most options in the hpp table and
  loop-built `add()` sites) and all 24 module TOMLs left 16 DSL keys, 4
  speeds (`thin_wall_speed`, `bottom_surface_speed`, `prime_tower_speed`,
  `wipe_tower_speed`), and `thumbnail_path` — 21 annotated on the wire.
  `bed_shape`, `retract_length`, `wipe_tower_enabled`, the four
  `*_fill_holder` keys and `infill_speed`/`infill_density`/`infill_angle`/
  `wall_count` are curated-table targets, invisible on the fork's page, so
  they stay bare rather than carry dead strings.
  `support_layer_height_mm` / `perimeter_arc_tolerance` are declared by
  modules with full display metadata already — host annotations there would
  be a second source of truth, so none were added.
- **Prose ranges** — machine-readable ranges encoded (`gcode_xy_decimals`
  `[1, 6]`, support/precision keys `>= 0` → `min 0`); prose stays prose. A
  clamp is a different predicate than the written range: the GUI clamps at
  `min` rather than rejecting, so `"> 0"`→`min 0.0` on speeds is an honest
  inclusive floor, not a claim that 0 passes pnp's validation.

The schema-quality items the ticket scoped in: `flat_bridge_closing_join`
promoted `string` → `enum` (`values: [miter, square, round]`, `group:
Quality`) via a `wire_type` override in the meta — the DSL field stays
`String` in Rust, and `HostWireField` still reports it as the raw wire type
when unannotated; `wave_overhang_pattern`'s *module manifest* promoted
likewise and dropped the domain from its `display`. Enum display labels
(`value_labels`) did **not** happen: a new wire concept, noted on the map as
fog.

### Verification

- `cargo check/clippy -p slicer-ir -p slicer-scheduler --all-targets` clean;
  `cargo xtask check-literals` 0 violations (fixed a pre-existing master
  violation in `module_schema_fields_carry_a_preset_scope` en route).
- `cargo test -p slicer-scheduler --lib`: 44 passing, including the extended
  wire test pinning label/group/unit/`min` on `thin_wall_speed`, nulls on
  identity-routed `travel_speed`, and the enum promotion.
- `host_keys_doc_lock_tdd` (`slicer-runtime` `unit` binary): 3 passing.
- Live probe of the freshly built `pnp_cli` (wire 1.2.0, 93 host entries): 21
  keys carry metadata, grouped Quality 5 / Nonplanar 3 / Multimaterial 2 /
  Output 2 / Smoothificator 2 / Speed 2 / Support 2 / Arachne 1 / Infill 1 /
  Wipe Tower 1; every field `null` on un-annotated entries.
- Fork suites against the bumped submodule: `pnp_config_translator_tests`
  944 assertions / 21 cases, `pnp_runtime_tests` 282 assertions / 31 cases —
  all passing; the drifted-doc shape (`host` array) is unchanged, so
  ticket 06's `[live-schema]` gate still accepts a wire-1.1.0-shaped doc and
  passes against the real wire-1.2.0 document with **zero dead curated
  rows**. (First run failed on my own probe error — `--module-dir` must be
  the dist `modules/` subdirectory, not the edition root; the drift had
  nothing to do with this wire change. Probing correctly, the map's own
  correction stands.)
