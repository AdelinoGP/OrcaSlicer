# PNP fork — known issues / fix backlog

Pre-existing bugs found during the settings-translation fix (2026-08-10).
None of these are fixed yet; they are recorded here so they are not lost.

## GUI side

1. **Model backup temp folder lands at the executable root.** The GUI writes
   its model backup tree (and the plate's temp G-code) to `F:\orcaslicer_model`
   — a folder at the root of wherever the executable is run — instead of
   `%TEMP%` or the app-data dir. Root cause: `Model::get_backup_path()` /
   `PartPlate::get_tmp_gcode_path()` resolve against the working directory.
   Symptom: stray `Fri_Aug_07/`, `Sat_Aug_08/` trees full of `_temp_3.config`
   files accumulate next to the executable.

2. **Support-preview still uses the separate `--config` file.** The slice path
   now carries the translated config in the 3MF sidecar, but
   `GLGizmoFdmSupports::request_support_preview` still writes `config.json` and
   passes `--config` because `pnp_cli support-preview` does not read the 3MF
   sidecar yet. Either extend `support_preview.rs` to read
   `read_3mf_project_settings` (pnp side) or keep the file (GUI side).

## PNP side (pinch_n_print_cli)

3. **`FeedrateConfig` is never wired from config in the production emitter.**
   `DefaultGCodeEmitter::new()` hardcodes `FeedrateConfig::default()`; the
   `[speeds]` host keys (`outer_wall_speed`, `infill_speed`, ...) reach
   `ResolvedConfig` but the emitter's `resolve_feedrate` reads the defaults.
   `new_with_config` exists but is only used by tests. All G-code F values are
   pnp defaults scaled by module speed factors.

4. **First-layer E uses the hardcoded 0.2 mm height fallback.**
   `emit.rs` `last_height_delta = 0.2` is used for the first layer's
   volumetric E even when `first_layer_height` is 0.1 mm — first-layer
   extrusion is ~2× the correct amount.

5. **Module schema drift on `infill_density`.** The infill modules' manifests
   declare `infill_density` as percent (default 20, min 0, max 100) while the
   modules consume it as a fraction (default 0.2). The GUI now sends the
   fraction; the schema's min/max/display are wrong and would mislead any
   schema-driven UI.

6. **Arachne wall widths emit spacing, not width, on some paths.** Measured
   outer-wall E implies ~0.50 mm for a 0.525 mm configured width (the
   `flow_to_width` conversion at `arachne-perimeters/src/lib.rs:827` does not
   fully undo the beading engine's spacing-domain widths). Pre-existing;
   unchanged by the settings fix.
