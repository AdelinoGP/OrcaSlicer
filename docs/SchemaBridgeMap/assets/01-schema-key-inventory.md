# Ticket 01 — Schema / curated-table / `print_config_def` reconciliation inventory

Map: [Schema-driven config bridge and PNP settings page](../map.md) ·
Ticket: [Schema-to-table-to-print_config_def reconciliation inventory](../tickets/01-schema-table-inventory.md)

## Method and provenance

| input | provenance |
|---|---|
| pnp module schema | `pnp_cli module config-schema --module-dir target/dist/developer/modules`, run live against a `cargo xtask dist` build of submodule `dbf3449c` |
| pnp host keys | `crates/slicer-ir/src/resolved_config.rs` (`cli` / `cli_opt` DSL rows) + `crates/slicer-ir/src/feedrate.rs` (`read_speed(config, "…")` call sites) + `docs/config/host-keys.toml` `[host_runtime]`, all at `dbf3449c` |
| curated table | `src/slic3r/GUI/PnpConfigTranslator.cpp` — `TIER_A_KEYS` plus every `copy_as` / `out[…]` target in `translate()`, transcribed by hand |
| Orca keys | `def = this->add("<key>", co<Type>)` in `src/libslic3r/PrintConfig.cpp`, with `label`/`sidetext`/`ratio_over`/`min`/`max`/enum values scraped from the following `def->` lines |
| bump delta | `git show 1238ef02:…` vs `git show dbf3449c:…` over module manifests, `resolved_config.rs`, `feedrate.rs`, `host-keys.toml` |

The scripts are throwaway; every number below is reproducible from the commands above. Orca
key extraction is source-scraping, not a run of the real `PrintConfigDef` constructor — 845 is
the count of literal `this->add("…")` sites, and is the denominator used throughout. (The map's
"~925" is not reproduced by this method; treat 845 as the measured figure.)

## Blocking finding: the staged dist was stale, and its layout moved

`pinch_n_print_cli/target/dist/pnp_cli.exe` on this machine was built 2026-08-11 and its schema
is byte-identical to `1238ef02`'s manifests, not `dbf3449c`'s. The inventory could not be run as
the ticket described until the dist was rebuilt.

Rebuilding surfaced a second problem: at `dbf3449c` `cargo xtask dist` stages to
`target/dist/<edition>/` (here `target/dist/developer/`), **not** the flat `target/dist/`. The
fork's `--pnp_dist_dir=` default and the xmake bundling step both point at the flat path, which
now holds only the stale August artifacts. This is fork-side breakage introduced by the bump and
is not covered by any existing ticket.

Also note the superproject still records `1238ef02` for the submodule (`git ls-tree HEAD
pinch_n_print_cli`); `dbf3449c` is checked out but uncommitted.

## Headline findings

1. **`module config-schema` is not the authority the map assumes.** pnp resolves config keys
   through four channels, and the probe reports only the first:
   | channel | count | reported by `config-schema`? |
   |---|---|---|
   | module manifest `[config.schema.*]` | 171 | yes |
   | `ResolvedConfig` `cli`/`cli_opt` DSL rows + `FeedrateConfig` speeds + `[host_runtime]` | 62 (not also in a manifest) | **no** |
   | `resolved_config.extensions` read by name (`support_type`, `support_family`) | 2 | **no** |
   | module code reading `config.get()` for an undeclared key (`infill_shift_step`, rectilinear-infill) | 1 | **no** |

   Tickets 02, 05 and 06 are all written as if channel 1 were the whole picture. It covers
   171 of 236 keys. A schema-only "handled set" would wrongly mark 14 working curated rows
   unimplemented, and a schema-only drift check would wrongly report them dead.

2. **`support_enabled` is a dead target, and it is a live bug.** The translator does
   `copy_as("enable_support", "support_enabled")`, but pnp's config key *is* `enable_support`
   (`resolved_config.rs:1090`, `cli "enable_support" support_enabled: bool`) — `support_enabled`
   is only an internal `shared_settings` string inside `support_analysis_producer.rs`. Nothing
   reads the key the fork writes, so **turning supports on in the GUI does not turn them on in
   pnp**. This predates the bump: `enable_support` was already the CLI key at `1238ef02`.

3. **The part-cooling rename inverted four curated rows.** The bump renamed pnp's fan keys to
   Orca's names, so `disable_fan_first_layers`, `enable_overhang_fan`, `fan_speed_max` and
   `fan_speed_min` no longer exist. The four Tier-B rows writing them are now dead; their Orca
   sources (`close_fan_the_first_x_layers`, `enable_overhang_bridge_fan`, `fan_max_speed`,
   `fan_min_speed`) became identity rows. Same shape for `tree_support_interface_spacing_mm`,
   replaced by identity `support_interface_spacing`. **Five settings silently stopped reaching
   pnp at the bump.**

4. **`schema_version` did not move.** It is `1.0.0` at both `1238ef02` and `dbf3449c`, across a
   module split, 33 new manifest keys and five breaking renames. The map's degradation rule
   ("config-schema major mismatched → read-only + error banner") will never fire on drift of
   this kind; only the per-key diff of ticket 06 can catch it.

5. **Baseline for the "amber tint recedes" claim: 123 of 845 Orca keys (14.6%)** are in
   `pnp_handled_keys()` today. Seven of those are inert — the six dead-target rows in section G
   whose source therefore never reaches pnp, plus warn-only `support_base_pattern_spacing` — so
   **116 of 845 (13.7%) actually reach pnp**.

6. **62 PNP-page candidates**, not the handful the map implies: 34 module-schema keys with full
   metadata (section D) plus 28 host-only keys with none (section F).

7. **`support_type` and `infill_shift_step` are false positives for "dead".** Both are absent
   from every declaration channel but genuinely consumed — `support_type` from
   `resolved_config.extensions` (`execution_plan.rs:248`, the support-generator claim selector),
   `infill_shift_step` from `config.get()` in rectilinear-infill's `lib.rs:176`. Ticket 06's
   dead-target rule must not flag them.

### A. Universe sizes

| universe | count |
|---|---|
| pnp module-schema keys (`pnp_cli module config-schema`, wildcards excluded) | 171 |
| pnp host keys not in any module schema | 62 |
| Orca `print_config_def` keys (`this->add("k", coT)` in `PrintConfig.cpp`) | 845 |
| Orca keys in the fork's `pnp_handled_keys()` set | 123 (14.6%) |

### B. Identity rows — module-schema keys whose name already exists in `print_config_def` (113)

27 clean, **86 carry at least one mismatch**. A mismatch is not automatically a false positive — the curated table already fixes many of them (unit resolution, vector collapse) — but every row here needs an explicit decision before name-identity alone may route it.

| pnp key | pnp type | module(s) | mismatch vs Orca def |
|---|---|---|---|
| `bridge_density` | float_or_percent | rectilinear-infill, wave-overhangs | type: orca `coPercent` vs pnp `float_or_percent`; unit: orca `%` vs pnp `-`; max: orca 125 vs pnp 120.0 |
| `bridge_line_width` | float | arachne-perimeters, classic-perimeters, gyroid-infill, lightning-infill, rectilinear-infill, wave-overhangs | type: orca `coFloatOrPercent` vs pnp `float`; unit: orca `mm or %` vs pnp `mm`; max: orca 100 vs pnp 2.0; orca percent-of-`nozzle_diameter` vs pnp absolute |
| `bridge_speed` | float | overhang-classifier-default, rectilinear-infill, wave-overhangs | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `brim_width` | float | skirt-brim | unit: orca `mm` vs pnp `-`; max: orca 100 vs pnp 30.0 |
| `close_fan_the_first_x_layers` | int | part-cooling | unit: orca `layers` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `dont_filter_internal_bridges` | bool | rectilinear-infill | type: orca `coEnum` vs pnp `bool`; enum domain: orca ['disabled', 'limited', 'nofilter'] vs pnp [] |
| `enable_extra_bridge_layer` | bool | rectilinear-infill | type: orca `coEnum` vs pnp `bool`; enum domain: orca ['apply_to_all', 'disabled', 'external_bridge_only', 'internal_bridge_only'] vs pnp [] |
| `enable_overhang_bridge_fan` | bool | part-cooling | orca is a per-extruder vector (translator collapses to elem 0) |
| `enable_overhang_speed` | bool | overhang-classifier-default | orca is a per-extruder vector (translator collapses to elem 0) |
| `fan_max_speed` | int | part-cooling | type: orca `coFloats` vs pnp `int`; unit: orca `%` vs pnp `-`; max: orca 100 vs pnp 255.0; orca is a per-extruder vector (translator collapses to elem 0) |
| `fan_min_speed` | int | part-cooling | type: orca `coFloats` vs pnp `int`; unit: orca `%` vs pnp `-`; max: orca 100 vs pnp 255.0; orca is a per-extruder vector (translator collapses to elem 0) |
| `filament_change_extrusion_role_gcode` | string | machine-gcode-emit | orca is a per-extruder vector (translator collapses to elem 0) |
| `filament_end_gcode` | string | machine-gcode-emit | orca is a per-extruder vector (translator collapses to elem 0) |
| `filament_start_gcode` | string | machine-gcode-emit | orca is a per-extruder vector (translator collapses to elem 0) |
| `filter_out_gap_fill` | float | classic-perimeters | unit: orca `mm` vs pnp `-` |
| `gap_infill_speed` | float | classic-perimeters | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `infill_anchor` | float_or_percent | infill-linker | unit: orca `mm or %` vs pnp `mm` |
| `infill_anchor_max` | float_or_percent | infill-linker | unit: orca `-` vs pnp `mm` |
| `infill_wall_overlap` | percent | classic-perimeters | unit: orca `%` vs pnp `-` |
| `initial_layer_line_width` | float | arachne-perimeters, classic-perimeters, gyroid-infill, lightning-infill, rectilinear-infill | type: orca `coFloatOrPercent` vs pnp `float`; unit: orca `mm or %` vs pnp `mm`; max: orca 1000 vs pnp 2.0; orca percent-of-`nozzle_diameter` vs pnp absolute |
| `initial_layer_min_bead_width` | float | arachne-perimeters | type: orca `coPercent` vs pnp `float`; unit: orca `%` vs pnp `units`; orca percent-of-`?` vs pnp absolute |
| `inner_wall_line_width` | float_or_percent | arachne-perimeters, classic-perimeters | unit: orca `mm or %` vs pnp `-`; max: orca 1000 vs pnp 2.0 |
| `inner_wall_speed` | float | classic-perimeters, overhang-classifier-default | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `internal_bridge_density` | float_or_percent | rectilinear-infill | type: orca `coPercent` vs pnp `float_or_percent`; unit: orca `%` vs pnp `-` |
| `internal_bridge_speed` | float_or_percent | rectilinear-infill | unit: orca `mm/s or %` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `internal_solid_infill_line_width` | float | gyroid-infill, rectilinear-infill | type: orca `coFloatOrPercent` vs pnp `float`; unit: orca `mm or %` vs pnp `-`; max: orca 1000 vs pnp 2.0; orca percent-of-`nozzle_diameter` vs pnp absolute |
| `internal_solid_infill_speed` | float | rectilinear-infill | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `ironing_flow` | float | top-surface-ironing | type: orca `coPercent` vs pnp `float`; unit: orca `%` vs pnp `-`; min: orca 0 vs pnp 0.01; max: orca 100 vs pnp 1.0; orca percent-of-`layer_height` vs pnp absolute |
| `ironing_pattern` | enum | top-surface-ironing | enum domain: orca ['concentric', 'rectilinear'] vs pnp ['rectilinear'] |
| `ironing_spacing` | float | support-surface-ironing | unit: orca `mm` vs pnp `-`; min: orca 0 vs pnp 0.01 |
| `ironing_speed` | float | support-surface-ironing, top-surface-ironing | unit: orca `mm/s` vs pnp `-` |
| `layer_height` | float | arachne-perimeters, classic-perimeters, infill-linker, layer-planner-default, wave-overhangs | min: orca 0 vs pnp 0.01 |
| `line_width` | float | arachne-perimeters, classic-perimeters, gyroid-infill, infill-linker, lightning-infill, rectilinear-infill, skirt-brim, support-surface-ironing, traditional-support, traditional-support-planner, tree-support, tree-support-planner, wipe-tower | type: orca `coFloatOrPercent` vs pnp `float`; unit: orca `mm or %` vs pnp `mm`; max: orca 1000 vs pnp 2.0; orca percent-of-`nozzle_diameter` vs pnp absolute |
| `max_bridge_length` | float | tree-support-planner | unit: orca `mm` vs pnp `-` |
| `min_bead_width` | float | arachne-perimeters | type: orca `coPercent` vs pnp `float`; unit: orca `%` vs pnp `units`; orca percent-of-`?` vs pnp absolute |
| `min_length_factor` | float | arachne-perimeters | unit: orca `mm` vs pnp `ratio`; max: orca 25.0 vs pnp 2.0 |
| `min_width_top_surface` | float_or_percent | arachne-perimeters, classic-perimeters | unit: orca `mm or %` vs pnp `%` |
| `nozzle_diameter` | float | arachne-perimeters, classic-perimeters, machine-gcode-emit, tree-support-planner, wave-overhangs | orca is a per-extruder vector (translator collapses to elem 0) |
| `nozzle_temperature_initial_layer` | int | machine-gcode-emit | orca is a per-extruder vector (translator collapses to elem 0) |
| `outer_wall_line_width` | float_or_percent | arachne-perimeters, classic-perimeters | unit: orca `mm or %` vs pnp `-`; max: orca 1000 vs pnp 2.0 |
| `outer_wall_speed` | float | classic-perimeters, overhang-classifier-default | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `overhang_1_4_speed` | float | overhang-classifier-default | type: orca `coFloatsOrPercents` vs pnp `float`; unit: orca `mm/s or %` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0); orca percent-of-`outer_wall_speed` vs pnp absolute |
| `overhang_2_4_speed` | float | overhang-classifier-default | type: orca `coFloatsOrPercents` vs pnp `float`; unit: orca `mm/s or %` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0); orca percent-of-`outer_wall_speed` vs pnp absolute |
| `overhang_3_4_speed` | float | overhang-classifier-default | type: orca `coFloatsOrPercents` vs pnp `float`; unit: orca `mm/s or %` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0); orca percent-of-`outer_wall_speed` vs pnp absolute |
| `overhang_4_4_speed` | float | overhang-classifier-default | type: orca `coFloatsOrPercents` vs pnp `float`; unit: orca `mm/s or %` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0); orca percent-of-`outer_wall_speed` vs pnp absolute |
| `overhang_fan_speed` | int | part-cooling | unit: orca `%` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `overhang_reverse_threshold` | float_or_percent | arachne-perimeters | unit: orca `mm or %` vs pnp `mm` |
| `raft_first_layer_density` | float | tree-support-planner | type: orca `coPercent` vs pnp `float`; unit: orca `%` vs pnp `-`; min: orca 10 vs pnp 0.0; max: orca 100 vs pnp 1.0; orca percent-of-`?` vs pnp absolute |
| `skirt_distance` | float | skirt-brim | unit: orca `mm` vs pnp `-`; max: orca 60 vs pnp 20.0 |
| `skirt_height` | int | skirt-brim | unit: orca `layers` vs pnp `-`; max: orca 10000 vs pnp 10.0 |
| `skirt_loops` | int | skirt-brim | max: orca 10 vs pnp 20.0 |
| `slow_down_for_layer_cooling` | bool | part-cooling | orca is a per-extruder vector (translator collapses to elem 0) |
| `slow_down_layer_time` | float | part-cooling | unit: orca `s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `slow_down_min_speed` | float | part-cooling | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `slowdown_for_curled_perimeters` | bool | overhang-classifier-default | orca is a per-extruder vector (translator collapses to elem 0) |
| `sparse_infill_density` | float | arachne-perimeters, classic-perimeters | type: orca `coPercent` vs pnp `float`; orca percent-of-`?` vs pnp absolute |
| `sparse_infill_line_width` | float | gyroid-infill, lightning-infill, rectilinear-infill | type: orca `coFloatOrPercent` vs pnp `float`; unit: orca `mm or %` vs pnp `-`; max: orca 1000 vs pnp 2.0; orca percent-of-`nozzle_diameter` vs pnp absolute |
| `sparse_infill_speed` | float | rectilinear-infill | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `support_angle` | float | traditional-support | max: orca 359 vs pnp 90.0 |
| `support_base_pattern` | string | traditional-support-planner | type: orca `coEnum` vs pnp `string` |
| `support_base_pattern_spacing` | float | traditional-support, traditional-support-planner, tree-support | unit: orca `mm` vs pnp `-` |
| `support_bottom_interface_spacing` | float | traditional-support, tree-support | unit: orca `mm` vs pnp `-`; min: orca 0 vs pnp -1.0 |
| `support_interface_bottom_layers` | int | traditional-support-planner, tree-support-planner | unit: orca `layers` vs pnp `-` |
| `support_interface_spacing` | float | traditional-support, tree-support | unit: orca `mm` vs pnp `-` |
| `support_interface_top_layers` | int | traditional-support-planner, tree-support-planner | unit: orca `layers` vs pnp `-` |
| `support_line_width` | float_or_percent | tree-support-planner | unit: orca `mm or %` vs pnp `-`; max: orca 1000 vs pnp 2.0 |
| `support_object_xy_distance` | float | traditional-support-planner, tree-support-planner | unit: orca `mm` vs pnp `-` |
| `support_speed` | float | traditional-support, tree-support | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `support_style` | string | traditional-support, tree-support-planner | type: orca `coEnum` vs pnp `string`; enum domain: orca ['default', 'grid', 'organic', 'snug', 'tree_hybrid', 'tree_slim', 'tree_strong'] vs pnp [] |
| `support_threshold_angle` | float | traditional-support-planner | type: orca `coInt` vs pnp `float` |
| `top_bottom_infill_wall_overlap` | percent | classic-perimeters | unit: orca `%` vs pnp `-` |
| `top_surface_line_width` | float | gyroid-infill, rectilinear-infill | type: orca `coFloatOrPercent` vs pnp `float`; unit: orca `mm or %` vs pnp `-`; max: orca 1000 vs pnp 2.0; orca percent-of-`nozzle_diameter` vs pnp absolute |
| `top_surface_speed` | float | rectilinear-infill | unit: orca `mm/s` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `tree_support_branch_angle` | float | tree-support-planner | max: orca 60 vs pnp 75.0 |
| `tree_support_branch_diameter` | float | tree-support-planner | unit: orca `mm` vs pnp `-`; min: orca 1.0 vs pnp 0.5; max: orca 10 vs pnp 20.0 |
| `tree_support_branch_diameter_angle` | float | tree-support-planner | max: orca 15 vs pnp 90.0 |
| `tree_support_branch_distance` | float | tree-support-planner | unit: orca `mm` vs pnp `-`; min: orca 1.0 vs pnp 0.1 |
| `tree_support_wall_count` | int | tree-support, tree-support-planner | min: orca 0 vs pnp 1.0; max: orca 2 vs pnp 10.0 |
| `wall_direction` | string | arachne-perimeters | type: orca `coEnum` vs pnp `string`; enum domain: orca ['ccw', 'cw'] vs pnp [] |
| `wall_maximum_deviation` | float | arachne-perimeters | min: orca 0.005 vs pnp 0.0001; max: orca 0.05 vs pnp 1.0 |
| `wall_maximum_resolution` | float | arachne-perimeters | min: orca 0.005 vs pnp 0.001; max: orca 0.5 vs pnp 10.0 |
| `wall_sequence` | string | arachne-perimeters, classic-perimeters | type: orca `coEnum` vs pnp `string`; enum domain: orca ['inner wall/outer wall', 'inner-outer-inner wall', 'outer wall/inner wall'] vs pnp [] |
| `wall_transition_angle` | float | arachne-perimeters | unit: orca `-` vs pnp `deg`; min: orca 1. vs pnp 0.0; max: orca 59. vs pnp 180.0 |
| `wall_transition_filter_deviation` | float | arachne-perimeters | type: orca `coPercent` vs pnp `float`; unit: orca `%` vs pnp `units`; orca percent-of-`?` vs pnp absolute |
| `wipe_tower_x` | float | wipe-tower | unit: orca `mm` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |
| `wipe_tower_y` | float | wipe-tower | unit: orca `mm` vs pnp `-`; orca is a per-extruder vector (translator collapses to elem 0) |

<details><summary>Clean identity rows (27)</summary>

`alternate_extra_wall`, `before_layer_change_gcode`, `bridge_flow`, `change_extrusion_role_gcode`, `change_filament_gcode`, `detect_overhang_wall`, `detect_thin_wall`, `enable_support`, `extra_perimeters_on_overhangs`, `internal_bridge_angle`, `internal_bridge_flow`, `layer_change_gcode`, `machine_end_gcode`, `machine_start_gcode`, `min_feature_size`, `only_one_wall_first_layer`, `only_one_wall_top`, `overhang_reverse`, `overhang_reverse_internal_only`, `precise_outer_wall`, `process_change_extrusion_role_gcode`, `support_on_build_plate_only`, `thick_bridges`, `thick_internal_bridges`, `time_lapse_gcode`, `wall_distribution_count`, `wall_transition_length`

</details>

### C. Table-routed — module-schema keys reached only through a curated rename/remap (24)

| pnp key | pnp type | module(s) | Orca source(s) |
|---|---|---|---|
| `apply_to_all` | bool | fuzzy-skin | `fuzzy_skin` |
| `bed_shape` | float-list | wipe-tower | `printable_area` |
| `bed_temperature_initial_layer_single` | int | machine-gcode-emit | `curr_bed_type`, `supertack_plate_temp_initial_layer`, `cool_plate_temp_initial_layer`, `textured_cool_plate_temp_initial_layer`, `eng_plate_temp_initial_layer`, `hot_plate_temp_initial_layer`, `textured_plate_temp_initial_layer` |
| `first_layer_height` | float | layer-planner-default | `initial_layer_print_height` |
| `infill_angle` | float | gyroid-infill, rectilinear-infill | `infill_direction` |
| `infill_density` | float | gyroid-infill, lightning-infill, rectilinear-infill | `sparse_infill_density` |
| `infill_speed` | float | gyroid-infill, lightning-infill, rectilinear-infill | `sparse_infill_speed` |
| `ironing_enabled` | bool | support-surface-ironing, top-surface-ironing | `ironing_type` |
| `ironing_flow_rate` | float | support-surface-ironing | `ironing_flow` |
| `ironing_spacing_mm` | float | top-surface-ironing | `ironing_spacing` |
| `point_distance` | float | fuzzy-skin | `fuzzy_skin_point_distance` |
| `retract_length` | float | path-optimization-default, wipe-tower | `retraction_length` |
| `retract_speed` | float | path-optimization-default | `retraction_speed` |
| `seam_mode` | enum | seam-placer, seam-planner-default | `seam_position` |
| `skirt_brim_enabled` | bool | skirt-brim | `skirt_loops`, `brim_type` |
| `spiral_vase` | bool | arachne-perimeters, classic-perimeters | `spiral_mode` |
| `support_raft_layers` | int | arachne-perimeters, classic-perimeters, tree-support-planner | `raft_layers` |
| `support_top_z_distance_mm` | float | traditional-support-planner, tree-support-planner | `support_top_z_distance` |
| `thickness` | float | fuzzy-skin | `fuzzy_skin_thickness` |
| `travel_z_hop` | float | path-optimization-default | `z_hop` |
| `wall_count` | int | arachne-perimeters, classic-perimeters, wave-overhangs | `wall_loops` |
| `wipe_tower_enabled` | bool | wipe-tower | `enable_prime_tower` |
| `wipe_tower_purge_volume` | float | wipe-tower | `prime_volume` |
| `wipe_tower_width` | float | wipe-tower | `prime_tower_width` |

### D. New to PNP — PNP-page candidates (34)

Schema metadata verbatim, so ticket 04 knows which controls it must build.

| key | type | default | min | max | step | display | group | unit | advanced | values | module(s) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `base_raft_layers` | int | 1 | 0.0 | 20.0 |  | Base Raft Layers | Support |  | False |  | tree-support-planner |
| `extra_perimeters` | int | 0 | 0.0 | 10.0 |  | Extra perimeters (per-region bonus wall count) | Walls |  | False |  | classic-perimeters |
| `gap_fill_medial_axis_on_painted` | bool | false |  |  |  | Run gap-fill/thin-wall medial axis on painted slices | Quality |  | False |  | classic-perimeters |
| `infill_overlap` | float | 0.45 | 0.0 | 1.0 |  | Infill Overlap (Ã—spacing) | InfillLinker |  | False |  | infill-linker |
| `interface_raft_layers` | int | 0 | 0.0 | 20.0 |  | Interface Raft Layers | Support |  | False |  | tree-support-planner |
| `max_bead_count` | int | 0 | 0.0 |  |  | Maximum bead count | Arachne |  | True |  | arachne-perimeters |
| `min_central_distance` | float | 0 | 0.0 |  |  | Minimum central distance | Arachne | units | True |  | arachne-perimeters |
| `min_width` | float | 4000 | 0.0 |  |  | Minimum width | Arachne | units | True |  | arachne-perimeters |
| `narrow_loop_length_threshold_mm` | float | 10.0 | 0.0 | 1000.0 |  | Minimum longest-dimension for narrow-island classification (mm) | Walls |  | False |  | classic-perimeters |
| `num_top_base_interface_layers` | int | 0 | 0.0 | 10.0 |  | Support Base Interface Layers | Support |  | False |  | tree-support-planner |
| `outer_wall_offset` | float | 0 | 0.0 |  |  | Outer wall offset | Arachne | units | True |  | arachne-perimeters |
| `path_optimization_emit_layer_markers` | bool | true |  |  |  | Emit Layer Markers | Path Optimization |  | False |  | path-optimization-default |
| `perimeter_arc_tolerance` | float | 0.0125 | 0.0 | 1.0 |  | Perimeter arc tolerance (mm) | Quality |  | False |  | classic-perimeters |
| `retract_mode` | enum | "gcode" |  |  |  | Retraction Mode | Travel Retraction |  | False | gcode, firmware | path-optimization-default |
| `seam_candidate_angle_threshold_deg` | float | 30.0 | 0.0 | 180.0 |  | Seam candidate sharp-corner angle threshold (degrees) | Seam |  | False |  | arachne-perimeters, classic-perimeters |
| `slice_has_paint` | bool | false |  |  |  | Slice contains painted regions (host-injected) | Quality |  | False |  | classic-perimeters |
| `smaller_perimeter_line_width` | float | 0.25 | 0.05 | 2.0 |  | Smaller perimeter line width (narrow-island override, mm) | Walls |  | False |  | classic-perimeters |
| `smaller_perimeter_threshold_mm` | float | 0.8 | 0.0 | 10.0 |  | Narrow-island width threshold (mm) | Walls |  | False |  | classic-perimeters |
| `support_branch_merge_distance_mm` | float | 0.8 | 0.0 |  |  | Support Branch Merge Distance | Support |  | False |  | tree-support-planner |
| `support_interface_flow` | percent | "100%" | 0.0 |  |  | Support Interface Flow | Support |  | False |  | traditional-support, tree-support |
| `support_layer_height_mm` | float | 0.0 | 0.0 | 1.0 |  | Support Layer Height | Support |  | False |  | traditional-support-planner, tree-support-planner |
| `support_max_branches_per_layer` | int | 1024 | 1.0 | 10000.0 |  | Support Max Branches Per Layer | Support |  | False |  | tree-support-planner |
| `support_overhang_angle` | float | 30.0 | 0.0 | 90.0 |  | Support Overhang Angle | Support |  | False |  | traditional-support-planner |
| `thin_wall_speed` | float | 30.0 |  |  |  | Thin Wall Speed | Speed |  | False |  | overhang-classifier-default |
| `wave_overhang_anchor_depth_mm` | float | 0.0 | 0.0 | 20.0 |  | Wave Overhang Anchor Depth (mm) | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_flow_mm3_per_mm` | float | 0.15 | 0.02 | 1.5 |  | Wave Overhang Flow (mm3/mm) | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_line_spacing` | float | 0.35 | 0.01 | 5.0 |  | Wave Overhang Line Spacing | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_max_iterations` | int | 0 | 0.0 | 500.0 |  | Wave Overhang Max Iterations (0 = unbounded) | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_min_length` | float | 0.0 | 0.0 | 100.0 |  | Wave Overhang Minimum Length | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_min_new_area` | float | 0.01 | 0.0 | 10.0 |  | Wave Overhang Minimum New Area | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_minimum_width` | float | 0.7 | 0.0 | 10.0 |  | Wave Overhang Minimum Width | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_pattern` | string | "smart" |  |  |  | Wave Overhang Pattern (smart, monotonic, zigzag) | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_perimeter_overlap` | float | 0.1 | 0.0 | 5.0 |  | Wave Overhang Perimeter Overlap | Wave Overhangs |  | False |  | wave-overhangs |
| `wave_overhang_print_speed` | float | 2.0 | 0.1 | 300.0 |  | Wave Overhang Print Speed | Wave Overhangs |  | False |  | wave-overhangs |

Descriptions (ticket 04 tooltip source):

- `max_bead_count` — Cap threshold consumed by LimitedBeadingStrategy. Not a user-facing OrcaSlicer PrintConfig.cpp option â€” upstream computes it internally as 2 * inset_count in Arachne/WallToolPaths.cpp:525, which is ALWAYS EVEN (LimitedBeadingStrategy's ctor warns on odd counts, and its odd-center compute branch parks the whole surplus thickness in one wide centre bead â€” a physically impossible extrusion). Default 0 = auto-derive 2 * wall_count (even, tracks wall_count) rather than shadowing it with a fixed odd cap. A positive override is honoured verbatim (advanced/cap-testing only).
- `min_central_distance` — Depth floor (mm) for filter_central's stage 1: an edge whose deepest endpoint never reaches this distance from the boundary is never central. Maps to ArachneParams::min_central_distance. Not a user-facing OrcaSlicer PrintConfig.cpp option â€” an internal Arachne algorithm parameter threaded through the pipeline.
- `min_width` — Nominal width (mm) used by remove_small_lines' length threshold. Maps to ArachneParams::min_width. Default 4000 units = 0.4mm, matching this codebase's common 0.4mm line-width convention.
- `outer_wall_offset` — Inward offset applied to the outer wall's toolpath location by OuterWallInsetBeadingStrategy; 0 disables the decorator's offset. Not a user-facing OrcaSlicer PrintConfig.cpp option â€” an internal Arachne algorithm parameter (coord_t) threaded through BeadingStrategyFactory.

### E. Host keys — the universe `config-schema` does not report

62 keys are consumed by pnp host built-ins, not by any module manifest, so `module config-schema` never mentions them. Of those: 30 are Orca-name identity rows, 4 are curated-table targets, 28 are pnp-only.

| class | keys |
|---|---|
| identity with Orca | `bottom_shell_layers`, `bridge_no_support`, `disable_m73`, `enforce_support_layers`, `filament_density`, `filament_diameter`, `filament_ironing_speed`, `initial_layer_infill_speed`, `initial_layer_speed`, `initial_layer_travel_speed`, `machine_max_acceleration_extruding`, `machine_max_acceleration_travel`, `mmu_segmented_region_interlocking_depth`, `mmu_segmented_region_max_width`, `skirt_speed`, `slice_closing_radius`, `support_bottom_z_distance`, `support_critical_regions_only`, `support_expansion`, `support_interface_speed`, `support_object_first_layer_gap`, `support_remove_small_overhang`, `support_threshold_overlap`, `support_top_z_distance`, `top_shell_layers`, `travel_speed`, `travel_speed_z`, `use_relative_e_distances`, `wall_generator`, `wipe_speed` |
| curated-table target | `bottom_fill_holder`, `bridge_fill_holder`, `sparse_fill_holder`, `top_fill_holder` |
| pnp-only | `arachne_min_feature_size`, `bottom_surface_speed`, `fill_authored_coloring`, `flat_bridge_closing_join`, `gcode_resolution`, `gcode_xy_decimals`, `infill_resolution`, `machine_max_jerk_e`, `machine_max_jerk_x`, `machine_max_jerk_y`, `machine_max_jerk_z`, `machine_max_speed_e`, `machine_max_speed_x`, `machine_max_speed_y`, `machine_max_speed_z`, `min_segment_length`, `mmu_segmented_region_interlocking_beam`, `nonplanar_amplitude`, `nonplanar_max_angle_deg`, `nonplanar_shell_count`, `prime_tower_speed`, `smoothificator_adaptive`, `smoothificator_target_height`, `solid_infill_speed`, `support_resolution`, `support_sharp_tails`, `thumbnail_path`, `wipe_tower_speed` |

**14 curated-table targets resolve only through this universe** — a naive schema-only drift check (ticket 06) would call every one of them dead:

`bottom_fill_holder`, `bridge_fill_holder`, `initial_layer_infill_speed`, `initial_layer_speed`, `initial_layer_travel_speed`, `skirt_speed`, `sparse_fill_holder`, `support_interface_speed`, `top_fill_holder`, `travel_speed`, `travel_speed_z`, `use_relative_e_distances`, `wall_generator`, `wipe_speed`
### F. New to PNP via the host universe (28)

Also PNP-page candidates, but with no schema metadata — no type tag, display name, group, or range beyond what `docs/config/host-keys.toml` documents for a subset. Ticket 04 must decide whether these get controls at all, and if so where the metadata comes from.

`arachne_min_feature_size`, `bottom_surface_speed`, `fill_authored_coloring`, `flat_bridge_closing_join`, `gcode_resolution`, `gcode_xy_decimals`, `infill_resolution`, `machine_max_jerk_e`, `machine_max_jerk_x`, `machine_max_jerk_y`, `machine_max_jerk_z`, `machine_max_speed_e`, `machine_max_speed_x`, `machine_max_speed_y`, `machine_max_speed_z`, `min_segment_length`, `mmu_segmented_region_interlocking_beam`, `nonplanar_amplitude`, `nonplanar_max_angle_deg`, `nonplanar_shell_count`, `prime_tower_speed`, `smoothificator_adaptive`, `smoothificator_target_height`, `solid_infill_speed`, `support_resolution`, `support_sharp_tails`, `thumbnail_path`, `wipe_tower_speed`
### G. Dead curated-table targets

Eight targets of the curated table exist in no pnp declaration channel. Three classes:

| target | Orca source | verdict |
|---|---|---|
| `disable_fan_first_layers` | `close_fan_the_first_x_layers` | **dead at the bump** — pnp renamed it to the Orca name; use identity |
| `enable_overhang_fan` | `enable_overhang_bridge_fan` | **dead at the bump** — same |
| `fan_speed_max` | `fan_max_speed` | **dead at the bump** — same |
| `fan_speed_min` | `fan_min_speed` | **dead at the bump** — same |
| `tree_support_interface_spacing_mm` | `support_interface_spacing` | **dead at the bump** — replaced by identity `support_interface_spacing` |
| `support_enabled` | `enable_support` | **dead since written** — pnp's key is `enable_support`; see finding 2 |
| `support_type` | `support_type` (Tier A) | **alive** — read from `resolved_config.extensions`, declared nowhere |
| `infill_shift_step` | `infill_shift_step` (Tier A) | **alive** — read via `config.get()` in rectilinear-infill, declared nowhere |

One further row is warn-only rather than dead: `support_base_pattern_spacing` currently emits a
lossy-fallback warning and sends nothing, because its old target `support_density` was retired.
`support_base_pattern_spacing` is now declared directly by `traditional-support`, so the row can
become a plain identity send.

### H. Delta of the bump (`1238ef02` → `dbf3449c`)

Method: manifest and source diff, not two live probes — only one buildable dist exists at a time.

**Module set.** `com.core.support-planner` split into `com.core.traditional-support-planner` and
`com.core.tree-support-planner`; `com.core.wave-overhangs` added. 21 modules → 23.

**Module-manifest keys removed (6).** `disable_fan_first_layers`, `enable_overhang_fan`,
`fan_speed_max`, `fan_speed_min` (renamed to Orca names), `support_density` (retired for
`support_base_pattern_spacing`), `tree_support_interface_spacing_mm` (retired for
`support_interface_spacing`).

**Module-manifest keys added (39).**

- *part-cooling renames (4):* `close_fan_the_first_x_layers`, `enable_overhang_bridge_fan`,
  `fan_max_speed`, `fan_min_speed` — all identity with Orca.
- *bridge parity (12):* `bridge_density`, `dont_filter_internal_bridges`,
  `enable_extra_bridge_layer`, `internal_bridge_angle`, `internal_bridge_density`,
  `internal_bridge_flow`, `internal_bridge_speed`, `max_bridge_length`, `thick_internal_bridges`,
  plus `internal_solid_infill_speed`, `sparse_infill_speed`, `top_surface_speed` moving into
  rectilinear-infill's manifest.
- *support families (13):* `support_base_pattern`, `support_base_pattern_spacing`,
  `support_bottom_interface_spacing`, `support_branch_merge_distance_mm`,
  `support_interface_flow`, `support_interface_spacing`, `support_line_width`,
  `support_max_branches_per_layer`, `support_object_xy_distance`, `support_overhang_angle`,
  `support_style`, `support_threshold_angle`, `num_top_base_interface_layers`.
- *wave overhangs (10, all new to PNP):* the `wave_overhang_*` family.

**Host keys added (13).** `bridge_no_support`, `enforce_support_layers`,
`fill_authored_coloring`, `support_bottom_z_distance`, `support_critical_regions_only`,
`support_expansion`, `support_line_width`, `support_object_first_layer_gap`,
`support_remove_small_overhang`, `support_sharp_tails`, `support_threshold_angle`,
`support_threshold_overlap`, `support_top_z_distance`.
**Host keys removed (1).** `support_overhang_angle` — demoted from a `ResolvedConfig` `cli` row to
a `traditional-support-planner` manifest key, and `support_threshold_angle` promoted in its place
(the legacy alias still resolves).

**New pnp key the fork does not send:** `support_family`, the canonical per-region family
selector (`execution_plan.rs:250`). `support_type` remains a compatibility alias that overrides
it, so the fork's current Tier-A `support_type` copy still works — but the canonical key is
unbound. Relevant to ticket 07.

## Consequences for the rest of the map

- **Ticket 02** must register keys from the *union* of the four channels, not from
  `config-schema` alone, or 62 host keys get no `ConfigOptionDef` and no persistence.
- **Ticket 05** cannot define "handled" as schema membership. Doing so would mark 14 working
  curated rows (section E) unimplemented and tint them amber.
- **Ticket 06**'s dead-target diff must be against the union too, and must exempt the
  extensions-map and undeclared-read keys (`support_type`, `infill_shift_step`) or it reports two
  permanent false alarms. It should also not rely on `schema_version` (finding 4).
- **Ticket 04** is sized at 62 candidate controls, 28 of them with no metadata at all.
- **Ticket 07** gains a concrete question: whether the fork should send `support_family`.
