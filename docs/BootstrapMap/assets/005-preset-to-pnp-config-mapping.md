# Preset→PNP config translation spec (ticket 005)

Sources (all read 2026-07-16):
- PNP key universe: `pnp_cli module config-schema --module-dir modules/core-modules` (live binary, 20 modules, 159 schema entries = **115 unique module keys**) plus host-registered keys from pnp `docs/config/host-keys.toml` (`[speeds]` 26 keys, `[resolved_config]` 14 keys, `[host_runtime]` 3 keys).
- Orca key universe: `this->add("<key>")` registrations in `src/libslic3r/PrintConfig.cpp` — **925 keys**.
- pnp `docs/15_config_keys_reference.md` (canonical PNP catalog, generated) and pnp `docs/ORCA_CONFIG_REFERENCE.md` (upstream snapshot with per-key "In Codebase" ✅/❌ column — the long-form companion to this spec).

**PNP config JSON shape:** flat string keys (the `key` field of the schema; e.g. `point_distance`, not `fuzzy_skin.point_distance`). Modules claim keys via their manifests; the namespace is global and flat, so generic-sounding PNP names (`thickness`, `point_distance`, `apply_to_all` — all fuzzy-skin) are *reserved* by their owning module. Namespaced prefixes exist only for overrides: `object_config:`, `paint_config:` (pnp `docs/02_ir_schemas.md` IR 5).

## Translation strategy (decided)

One data-driven table in the GUI translation layer (not scattered ad-hoc code). Each Orca key in `DynamicPrintConfig` resolves to exactly one of four tiers:

| Tier | Meaning | Count | Action |
|---|---|---|---|
| A — identity | Same key string, compatible value | 65 | copy value (with unit/serialization normalization) |
| B — rename/transform | PNP key exists under a different name or derived value | ~30 | apply mapping row below |
| C — PNP-internal | PNP key with **no** Orca source | ~20 | never emitted by translation; PNP default applies |
| D — unresolved | Orca key with no PNP consumer | ~830 | **not sent**; recorded as a per-key warning |

Value normalization applies across tiers A and B: Orca `coFloatOrPercent` serializes as `"300%"` strings — PNP `float_or_percent` accepts the same form (see `min_width_top_surface` default `"300%"`), so percents pass through as strings; booleans `"1"/"0"` → JSON true/false; `coFloats` vectors (per-extruder) → first element for v1 (single-extruder scope).

## Tier A — identity keys (66)

`alternate_extra_wall`, `bridge_flow`, `brim_width`, `detect_overhang_wall`, `detect_thin_wall`, `extra_perimeters_on_overhangs`, `filter_out_gap_fill`, `gap_infill_speed`, `infill_shift_step`, `initial_layer_min_bead_width`, `inner_wall_line_width`, `inner_wall_speed`, `ironing_flow`, `ironing_pattern`, `ironing_spacing`, `ironing_speed`, `layer_height`, `line_width`, `machine_end_gcode`, `machine_start_gcode`, `min_bead_width`, `min_feature_size`, `min_length_factor`, `min_width_top_surface`, `nozzle_diameter`, `nozzle_temperature_initial_layer`, `only_one_wall_first_layer`, `only_one_wall_top`, `outer_wall_line_width`, `outer_wall_speed`, `overhang_1_4_speed`, `overhang_2_4_speed`, `overhang_3_4_speed`, `overhang_4_4_speed`, `overhang_fan_speed`, `overhang_reverse`, `overhang_reverse_internal_only`, `overhang_reverse_threshold`, `precise_outer_wall`, `skirt_distance`, `skirt_height`, `skirt_loops`, `slow_down_for_layer_cooling`, `slow_down_layer_time`, `slow_down_min_speed`, `sparse_infill_density`, `support_angle`, `support_interface_bottom_layers`, `support_interface_top_layers`, `support_speed`, `thick_bridges`, `tree_support_branch_angle`, `tree_support_branch_diameter`, `tree_support_branch_diameter_angle`, `tree_support_branch_distance`, `tree_support_wall_count`, `wall_direction`, `wall_distribution_count`, `wall_maximum_deviation`, `wall_maximum_resolution`, `wall_sequence`, `wall_transition_angle`, `wall_transition_filter_deviation`, `wall_transition_length`, `wipe_tower_x`, `wipe_tower_y`

> `infill_shift_step` added to Tier A 2026-08-10 (ticket 013, reopened past v1): consumed by pnp's rectilinear-infill (`config.get("infill_shift_step")`); it was falsely tinted as Tier D before.

Plus identity host-speed keys from `[speeds]` that Orca also defines (`top_surface_speed`, `sparse_infill_speed` → see Tier B note, `bridge_speed`, `internal_bridge_speed`, `support_interface_speed`, `travel_speed`, `travel_speed_z`, `initial_layer_speed`, `initial_layer_infill_speed`, `initial_layer_travel_speed`, `skirt_speed`, `wipe_speed`) — the implementer must diff `[speeds]` against `PrintConfig.cpp` the same way; most are identity.

Caveats inside Tier A:
- `wall_sequence`: Orca enum serializes as `inner wall/outer wall` style strings; PNP expects `"InnerOuter"`-style. Needs an enum-string transform row despite the identical key name.
- `wall_direction`, `ironing_pattern`: same enum-string caution — verify serialized spellings during implementation.

## Tier B — renames / transforms (Orca → PNP, all Orca names verified in PrintConfig.cpp)

| Orca key | PNP key | Transform |
|---|---|---|
| `printable_area` | `bed_shape` | points list → PNP bed-shape form |
| `hot_plate_temp_initial_layer` / `cool_plate_temp_initial_layer` / `eng_plate_temp_initial_layer` / `textured_plate_temp_initial_layer` (select by `curr_bed_type`) | `bed_temperature_initial_layer_single` | pick active bed type's value |
| `close_fan_the_first_x_layers` | `disable_fan_first_layers` | copy |
| `enable_overhang_bridge_fan` | `enable_overhang_fan` | copy |
| `fan_max_speed` / `fan_min_speed` | `fan_speed_max` / `fan_speed_min` | copy |
| `initial_layer_print_height` | `first_layer_height` | copy |
| `infill_direction` | `infill_angle` | copy (degrees) |
| `sparse_infill_density` | `infill_density` (infill modules) **and** `sparse_infill_density` (arachne-perimeters) | one source feeds both PNP keys; strip `%` |
| `sparse_infill_speed` | `infill_speed` (module key) and `sparse_infill_speed` (host speed) | one source, two sinks |
| `ironing_type` | `ironing_enabled` | `!= "no ironing"` → true |
| `ironing_flow` | `ironing_flow_rate` (support-surface variant) | copy |
| `ironing_spacing` | `ironing_spacing_mm` (top-surface variant) | copy |
| `seam_position` | `seam_mode` | `nearest/rear/random` map 1:1; Orca `aligned` has **no PNP equivalent** → fallback `nearest` + warning |
| `skirt_loops` > 0 OR `brim_type` != `no_brim` | `skirt_brim_enabled` | derived bool |
| `spiral_mode` | `spiral_vase` | copy |
| `enable_support` | `support_enabled` | copy |
| `raft_layers` | `support_raft_layers` | copy (note: PNP has no standalone raft — inventory gap; >0 with supports off must warn) |
| `support_top_z_distance` | `support_top_z_distance_mm` | copy |
| `wall_generator` | `wall_generator` (host_runtime) | Orca coEnum (classic/arachne) → PNP string; selects perimeter module at load time |
| `support_base_pattern_spacing` | `support_density` | spacing↔density inversion — **verify formula during implementation**; if unclear, leave PNP default + warn |
| `support_interface_spacing` | `tree_support_interface_spacing_mm` | copy |
| `fuzzy_skin` | `apply_to_all` | enum: `all`→true, `external`→false, `none`→omit fuzzy keys entirely |
| `fuzzy_skin_thickness` / `fuzzy_skin_point_distance` | `thickness` / `point_distance` | copy (flat PNP names owned by fuzzy-skin module) |
| `z_hop` | `travel_z_hop` | copy |
| `retraction_length` / `retraction_speed` | `retract_length` / `retract_speed` | per-extruder vector → element 0 |
| `wall_loops` | `wall_count` | copy |
| `enable_prime_tower` / `prime_tower_width` / `prime_volume` | `wipe_tower_enabled` / `wipe_tower_width` / `wipe_tower_purge_volume` | copy |
| `use_relative_e_distances` | `use_relative_e_distances` (host_runtime) | identity, listed here because host-side |
| `sparse_infill_pattern` | `sparse_fill_holder` | **value remap to the module holding `claim:sparse-fill`** (ticket 013, reopened past v1): `rectilinear`→`rectilinear-infill`, `gyroid`→`gyroid-infill`, `lightning`→`lightning-infill`; all other values (incl. Orca default `crosshatch`) → `rectilinear-infill` fallback + lossy warning. gyroid/lightning hold sparse-fill only, so top/bottom never map to them (an unknown holder makes the module emit nothing for the role — silent loss) |
| `top_surface_pattern` | `top_fill_holder` | value remap: `rectilinear`→`rectilinear-infill`; all other values (incl. Orca default `monotonic`) → `rectilinear-infill` fallback + lossy warning |
| `bottom_surface_pattern` | `bottom_fill_holder` | same table as `top_surface_pattern` |
| `pnp_bridge_fill_holder` | `bridge_fill_holder` | fork-specific key (Orca config side) carrying the pnp module id; only modules holding `claim:bridge-fill` are offered (today: `rectilinear-infill` only) |

`internal_solid_infill_pattern` deliberately has **no row**: pnp's region model has no internal-solid claim (only top/bottom/bridge/sparse), so there is no honest holder target — the key stays Tier D (tinted) until the pnp region→claim mapping for internal solid is verified.

## Tier C — PNP-internal keys (translation never writes; PNP defaults rule)

Arachne internals: `max_bead_count`, `min_central_distance`, `min_width`, `optimal_width`, `preferred_bead_width_outer`, `outer_wall_offset`, `visvalingam_area_threshold`.
Pipeline/behavior internals: `seam_candidate_angle_threshold_deg`, `perimeter_arc_tolerance`, `narrow_loop_length_threshold_mm`, `smaller_perimeter_line_width`, `smaller_perimeter_threshold_mm`, `thin_wall_speed`, `gap_fill_medial_axis_on_painted`, `slice_has_paint` (set by GUI when paint data present, not from presets), `path_optimization_emit_layer_markers`, `retract_mode`, `extra_perimeters` (Prusa-legacy name, not an Orca key), `support_layer_height_mm` (Orca's `independent_support_layer_height` is a coBool toggle, not a height value — no direct source; PNP default applies, warn when the Orca bool is set), `thumbnail_path` (driven by `--thumbnail` CLI flag, not config), all `[resolved_config]` tuning keys (`gcode_resolution` etc. — Orca has `resolution`-family keys; map only if semantics match, else internal).

## Tier D — unresolved Orca keys (~830)

Everything else in the 925-key universe. Policy (locked by the map's destination): presets load unchanged, unresolved keys are **not sent** to pnp_cli, and each is recorded as a warning. Warnings are classified by the translation table itself:

- `unsupported-feature` — key belongs to a feature PNP lacks (raft, non-Marlin flavor, SLA, calibration). Grouped per feature in the UI, not one line per key.
- `not-yet-mapped` — key plausibly maps but no row exists yet. This is the growth path: adding PNP coverage = adding a table row, no code.
- `no-op` — key can't affect PNP output and silence is correct (GUI-only keys, device/AMS keys).

Mechanism: the table is a single static array `{orca_key, pnp_key, transform_fn, warning_class}` in one new translation unit (proposed `src/slic3r/GUI/PnpConfigTranslator.{hpp,cpp}`), consumed at slice time from `preset_bundle->full_config()`; output is the PNP JSON written to the temp config file plus a `std::vector<PnpConfigWarning>`. Keys absent from the table default to `not-yet-mapped`. Where warnings *surface* in the UI is deliberately out of this ticket — it graduates to its own ticket (fog note on the map).

## Handoff items (pnp-side, for the pinch_n_print spec-packet workflow)

1. **Schema-driven mapping export**: `pnp_cli module config-schema` descriptions already name their Orca source key (e.g. "OrcaSlicer coBool key alternate_extra_wall") — formalize that as a machine-readable `orca_key` field in the schema JSON so the GUI table can be generated/verified instead of hand-maintained.
2. **Unknown-key behavior**: document/guarantee pnp_cli's behavior for config JSON keys no module claims (silently ignored vs. error). The GUI filters before sending, but the contract should be explicit.
3. **`seam_position: aligned`**: no PNP equivalent — candidate future seam mode.

## Open verification points for implementation (not blockers)

- Enum serialized spellings for `wall_sequence`, `wall_direction`, `ironing_pattern`, `seam_mode`.
- `support_base_pattern_spacing` → `support_density` conversion formula.
