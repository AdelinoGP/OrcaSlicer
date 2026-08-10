# PNP fork — known issues / fix backlog

Bugs found during the settings-translation fix (2026-08-10) and their
resolutions. Fixed items carry the fixing commit; open items stay at the top
of their section.

## Fixed

1. **Model backup temp folder landed at the executable root.** `temporary_dir()`
   returned an empty string until `set_temporary_dir()` ran (GUI startup only),
   so `Model::get_backup_path()` built `/orcaslicer_model/...` which Windows
   resolves to the root of the current drive (`F:\orcaslicer_model\`). Every
   libslic3r consumer without an explicit temp dir — the test binaries above
   all of them — polluted the drive root with `Fri_Aug_07/`-style `_temp_3.config`
   trees. Fixed in `temporary_dir()` (src/libslic3r/utils.cpp): never return
   empty, fall back to `boost::filesystem::temp_directory_path()`. The GUI's
   explicit `set_temporary_dir()` is unaffected.
   - Tests: `tests/pnp/test_pnp_backup_path.cpp` (new).
   - Verified: re-running `libslic3r_tests` (the previous writer of
     `F:\orcaslicer_model`) no longer creates a drive-root tree; backups land
     under `%TEMP%\orcaslicer_model`.
   - GUI commit: `this commit`; test binary evidence: pre-fix run recreated
     `F:\orcaslicer_model\Mon_Aug_10`, post-fix run created only
     `%TEMP%\orcaslicer_model\Mon_Aug_10\...`.

2. **Support-preview used a separate `--config` file.** The slice path carries
   the translated config in the 3MF sidecar, but `pnp_cli support-preview`
   only read `--config` and the gizmo wrote `config.json` alongside. Fixed on
   both sides: `run_support_preview` now seeds its config from
   `read_3mf_project_settings(input)` (explicit `--config` still wins, mirroring
   the slice path's `or_insert_with` seeding); `GLGizmoFdmSupports::request_support_preview`
   no longer writes `config.json`, and the runner drops the `--config` arg.
   The gizmo's `enable_support: true` override is preserved (merged into the
   sidecar at export).
   - Tests: `support_preview_tdd.rs` — sidecar enables/disables support without
     a config file; explicit `--config` wins over the sidecar (9 tests).
   - pnp commit: `a4cf82a2`; GUI commit: `this commit`.

3. **`FeedrateConfig` was never wired from config.** `DefaultGCodeEmitter::new()`
   hardcoded `FeedrateConfig::default()`, so every F value was a pnp default
   scaled by module speed factors. Fixed: `FeedrateConfig::from_raw_config`
   (slicer-ir) builds the table from the raw config source keyed by the
   `[speeds]` host names (Orca names, mm/s; absent keys keep the defaults, so
   `docs/config/host-keys.toml` stays the defaults' source of truth), and the
   production emitter in `run.rs` is constructed via `new_with_config`.
   - Tests: `feedrate_from_raw_config_tdd.rs` (new, 4 tests);
     `gcode_feedrate_emission_tdd.rs` — raw-config → F-token wiring test.
   - Verified on the real plate slice: sidecar `outer_wall_speed=120` → F7200
     (was F3600); `--config` override 60 → F3600.
   - pnp commit: `a4cf82a2`.

4. **First-layer E used a hardcoded 0.2 mm height.** `emit.rs` seeded
   `last_height_delta` with a literal 0.2, over-extruding the first layer ~2×
   whenever `first_layer_height` was 0.1 mm. Fixed: seed from
   `resolved_config.first_layer_height`.
   - Tests: `gcode_feedrate_emission_tdd.rs` — first-layer volumetric E equals
     width × 0.1 / filament_area; `;HEIGHT:0.1`.
   - Verified: first-layer outer-wall E/mm 0.02598 = 0.625 × 0.1 / 2.40528
     (old_flow.gcode pre-fix artifact: 0.04937 ≈ 2×).
   - pnp commit: `a4cf82a2`.

5. **`infill_density` module schema drift.** The infill manifests declared
   percent (default 20, max 100) while gyroid/lightning/rectilinear consume a
   fraction (default 0.2) — and the GUI now sends the fraction. Fixed the
   manifests to `default = 0.2, min = 0.0, max = 1.0` and regenerated
   `docs/15_config_keys_reference.md` (`cargo xtask gen-config-docs`). The GUI
   schema guard passes a fraction under the new max; `sparse_infill_density`
   stays percent (the perimeter modules' gate uses it as percent).
   - Verified: `cargo test -p gyroid-infill -p lightning-infill -p rectilinear-infill`,
     pnp-cli suite green.
   - pnp commit: `a4cf82a2`.

6. **Arachne wall widths — investigated, NOT a conversion bug.** The measured
   "~0.50 mm at 0.525 configured" outer-wall E was not unconverted beading
   spacing. The `flow_to_width` conversion at the ExtrusionLine → path boundary
   is exact on every path (plain, top-area, fallback, second-pass, first layer,
   bridges): a clean-square probe emits exactly 0.525 / 0.625 / 0.625(first
   layer). The sub-width population is bridge-flow vertices: 0.525 ×
   `bridge_flow`(0.95) = 0.49875, 0.625 × 0.95 = 0.59375 — re-slicing with
   `bridge_flow: 1.0` makes the 0.50 population disappear entirely. Feeding
   `preferred_bead_width_outer` the width instead of the spacing would diverge
   from canonical (Orca feeds `ext_perimeter_spacing` as `bead_width_0`).
   - Tests added anyway (regression lock): `wall_width_emission_tdd.rs` —
     emitted widths equal the configured values on a clean square, first layer
     included.
   - pnp commit: `a4cf82a2`.

## Open findings (new)

- **Flat-bridge detection flags bottom-layer walls.** On the real plate slice,
   the outer walls of a plain box carry `is_bridge` + `bridge_flow` (0.95) on
   3 of 4 sides from layer 1 upward — layer 1 cannot physically overhang.
   Suspect the mesh-level flat-bridge detection flags the object's bottom
   facets. Effect: ~5% under-extrusion on those wall segments. Needs its own
   ticket (pnp side).
- **Pre-existing libslic3r test failure (unrelated to PNP).** The hidden
   `[.]` "2D convex hull of sinking object" test in `tests/libslic3r/test_3mf.cpp`
   fails (hull differs from the reference); the test code itself documents the
   divergence. Excluded from default runs.
