---
title: Native-slicing rip-out scope
status: closed
type: grilling
assignee: claude
blocked-by: [003]
---

## Question

Exactly what gets removed: FFF `Print::process` slicing paths, all SLA (presets, `SLAPrint`, SLA UI), calibration flows that slice. What must stay because it uses libslic3r geometry without slicing: arrange, orient, mesh repair, cut, supports painting UI (?). Long-lived fork: removal must be clean and maintainable, not `#ifdef`-hidden.

## Resolution

Scope settled across five decisions. Two surfaces neither this ticket nor ticket 003 anticipated were found and ruled on (headless CLI, C++ test suite).

### `Print::process()` has four production callers, not one

Ticket 003 designed the `BackgroundSlicingProcess` seam; it is **not** the only native-slicing entry point. Verified by grep across `src/` and `tests/`:

| Caller | Disposition |
|---|---|
| `src/slic3r/GUI/BackgroundSlicingProcess.cpp` (×3) | Replaced by `PnpSlicingProcess` (ticket 003) |
| `src/OrcaSlicer.cpp` | **Delete** — headless CLI slicing (see below) |
| `src/slic3r/Utils/CalibUtils.cpp` | **Delete** — calibration (see below) |
| `src/dev-utils/OrcaSlicer_profile_validator.cpp` | **Delete** — dev tool that validates profiles by slicing; meaningless without a native pipeline |
| `tests/fff_print/*`, `tests/libslic3r/*` (~25 sites) | **Delete** the slicing cases (see below) |

### 1. Headless CLI — deleted; `pnp_cli` is the CLI

`src/OrcaSlicer.cpp` holds a complete second slicer: `--slice` with plate loops, cached slice-data reuse (`print->load_cached_data()` / `print->process(nullptr, true)`), and status callbacks. **Rip out the `--slice`/`--export` slicing path**; keep non-slicing CLI verbs (3mf conversion, `--info`) where cheap to retain. Rationale: the fork's thesis is that `pnp_cli` slices — a second, broken CLI is pure liability, and anyone wanting headless slicing invokes `pnp_cli` directly. Explicitly **not** reimplemented as a shell-out: it would add a second consumer of the seam for a use case PNP already serves natively.

### 2. Calibration — UI survives as a mock, implementation deleted

Calibration is not a *consumer* of native slicing; it is implemented **inside** it. Each `Plater::calib_*` (`calib_pa`, `calib_flowrate`, `calib_temp`, `calib_retraction`, `calib_VFA`, `calib_input_shaping_freq/damp`, `calib_max_flowrate`) builds a test model then pushes params via `background_process.fff_print()->set_calib_params(params)`; `Print.cpp` and `GCode.cpp` read those params *during generation*. Shell-out cannot carry this — `pnp_cli` takes a model and a config, not a calib-params object. `CalibUtils.cpp` (BBL cloud-calibration wizard) additionally builds its own headless `Print` and calls `fff_print->process()` + `export_gcode()` directly.

**Decision — keep the UI as a placeholder/mock; delete the implementation.** Invoking any calibration shows a "not implemented" message.

- **Keep:** the *type* block in `libslic3r/calib.hpp` (`Calib_Params`, `CalibMode`) — every calib dialog embeds `Calib_Params m_params` as a member, so the dialogs don't compile without it. Keep `calib_dlg.*`, `Calibration*.cpp`, `CalibrationWizard*`, `ExtrusionCalibration.*`, and their menu entries.
- **Delete:** `libslic3r/calib.cpp` generators (`CalibPressureAdvancePattern` incl. `generate_custom_gcodes`, tower/pattern geometry), `Model::calib_pa_pattern` (`Model.hpp`/`Model.cpp` incl. the `reset()` calls), `Print::set_calib_params` + the calib params member and every read of it in `Print.cpp`/`GCode.cpp`, `Plater::_calib_pa_pattern`/`_calib_pa_tower`/`_calib_pa_pattern_gen_gcode`/`_calib_pa_select_added_objects`, and `CalibUtils`' slice-and-upload path.
- **Stub:** each `Plater::calib_*` collapses to a body that posts the not-implemented notification. Same for the `CalibUtils` entry points still referenced by `CalibrationWizard`/`AMSMaterialsSetting`.

Rationale: the UI is retained deliberately as a mock so a future PNP-side calibration design has its surface already in place; the dead generation code is not.

### 3. C++ test suite — slicing tests deleted, geometry tests kept

**Delete** test files/cases exercising `Print::process()`: `tests/fff_print/test_print.cpp`, `test_skirt_brim.cpp`, `test_gcodewriter.cpp`, `test_model.cpp`'s process case, the process cases in `test_helpers.cpp` and `test_trianglemesh.cpp`, `tests/libslic3r/test_toolordering_nozzle_group.cpp`. **Keep** tests for surviving code: geometry, `TriangleMesh`, `Model`, 3mf I/O, config. Rationale: you cannot regression-test deleted code; PNP owns slicing correctness and carries its own suite. Explicitly **not** `#if 0`-tagged — that contradicts this ticket's clean-not-hidden rule.

### 4. Support-painting preview — dropped; pnp handoff item for later

**The only GUI feature depending on native slicing *output*.** `GLGizmoFdmSupports` runs a worker thread calling `PrintObject::generate_support_preview()`, whose body is literally `this->slice(); this->generate_support_material();` — real `posSlice` + `posSupportMaterial`, pulling in all of `Support/*`, purely to render the overlay while painting.

**Decision — delete `generate_support_preview()` and the gizmo's preview thread; the gizmo stays paint-only.** Painting still writes `support_facets` onto `ModelVolume` and still reaches PNP via the per-object config handoff (ticket 004); the user simply sees no generated supports until a real slice. This is what lets `Support/*` be deleted outright. Recorded as **pnp handoff item 13** (a support-preview/geometry query so the overlay can be restored later via the CLI). Accepted v1 UX regression vs. stock Orca.

Note the other painting gizmos are clean: seam, fuzzy-skin, and MMU-segmentation gizmos only write `TriangleSelector` facet data to `ModelVolume` — no slicing calls. `GLGizmoFdmSupports::init_print_instance()`'s reads of `PrintObject::config()`/`instances()` need only the skeleton built by `Print::apply()`, which is retained.

### 5. SLA — full cut, config defs included, with explicit project detection

**Orca already did a partial SLA rip-out**, which makes this far cheaper than the file count suggests: `GLGizmoSlaSupports.*` and `GLGizmoHollow.*` are commented out of `src/slic3r/CMakeLists.txt` (never compiled), `TabSLAPrint`/`TabSLAMaterial` compile but are never instantiated (their `add_created_tab` calls in `MainFrame.cpp` are commented out), `Preset.cpp` filters SLA vendor printers out of the catalog, `PresetBundle.cpp`'s `"- default SLA -"` registration is commented out, and `Plater::import_sl1_archive()` has no caller. Much of the GUI cut is deleting already-inert code.

**Delete:**
- `src/libslic3r/SLAPrint.{cpp,hpp}`, `SLAPrintSteps.{cpp,hpp}`, `Format/SL1.{cpp,hpp}`.
- `src/libslic3r/SLA/` — **except `IndexedMesh.{cpp,hpp}` and `Rotfinder.{cpp,hpp}`**, which are *not* SLA-only: `IndexedMesh` backs the general `AABBMesh` used by `PrintObject`, `Layer.hpp`, `ContourZ`, `FaceDetector`/`GLGizmoFaceDetector`; `Rotfinder` backs `RotoptimizeJob` (the FFF-usable orientation optimizer). These two graduate out of the `SLA/` directory rather than dying with it. (`SLA/SupportTreeIGL.cpp` is already not compiled.)
- The dead GUI files: `Gizmos/GLGizmoSlaSupports.*`, `Gizmos/GLGizmoHollow.*`, `Jobs/SLAImportDialog.hpp`, `Jobs/SLAImportJob.*`, `Plater::import_sl1_archive()`.
- `TabSLAPrint`/`TabSLAMaterial` from `Tab.cpp`/`Tab.hpp`.
- **`ModelObject`'s SLA fields** — `sla_support_points`, `sla_points_status`, `sla_drain_holes`, and the three `friend class SLAPrint;` declarations in `Model.hpp`, plus their copy/clear handling in `ModelObject::assign`, move-assign, `split`, and clone paths.
- **`sla_shift_z`** — `get_sla_shift_z()`/`set_sla_shift_z()` and the SLA-elevation Z-offset concept, across the 9 files that reference it: `3DScene.{cpp,hpp}`, `GLCanvas3D.cpp`, `Selection.cpp`, `SurfaceDrag.cpp`, `Gizmos/GLGizmoCut.cpp`, `GLGizmoFlatten.cpp`, `GLGizmoMeasure.cpp`, `GLGizmosCommon.cpp`. Each touch point is shallow (a conditional extra Z offset) but must all go together.
- `PresetBundle`'s parallel `sla_prints` / `sla_materials` collections and their `switch` wiring.
- `Plater::priv`'s `SLAPrint sla_print` member and `m_sla_import_dlg`, `sla_object_menu()`, `reslice_SLA_until_step`, and the ~116 `ptSLA` branch sites (heaviest: `GLCanvas3D.cpp`, `Plater.cpp`, `ConfigWizard.cpp`, `Tab.cpp`).
- `PrintBase.hpp`'s `RELOAD_SLA_SUPPORT_POINTS` / `RELOAD_SLA_PREVIEW` status flags (cosmetic).
- `bbs_3mf.cpp`'s SLA support-point/drain-hole **write** side (the read side is already disabled — it currently writes metadata nothing reads).
- **`PrintConfigDef::init_sla_params()`** and the SLA config classes (`SLAPrinterConfig`, `SLAPrintConfig`, `SLAPrintObjectConfig`, `SLAMaterialConfig`, `SLAFullPrintConfig`), their `STATIC_PRINT_CONFIG_CACHE` entries, the SLA enum tables (`SLADisplayOrientation`, `SLAPillarConnectionMode`, `SLAMaterialSpeed`), and the `--export-sla`/`--help-sla` CLI flags.

**Kept clean by construction:** the `PrintBase`/`Print`/`SLAPrint` hierarchy needs no rework — `Print` and `SLAPrint` are *siblings* deriving from the template `PrintBaseWithState<StepType, COUNT>`, not parent/child, so deleting `SLAPrint` doesn't disturb `PrintBase`'s abstraction. `PrintConfig.cpp`'s `init_sla_params()` is already a contiguous function, so the config cut is mechanical.

**Back-compat — verified hazard, and the mitigation.** Dropping the SLA config defs is **not** free. Unknown keys throw `UnknownOptionException` from `ConfigBase::set_deserialize_raw`. The `load_from_ini` / `load_string_map` / `load_from_gcode_file` paths catch it **per key** and ignore. But `.3mf` project settings load via `_extract_project_config_from_archive` → **`ConfigBase::load_from_json`, which does not** — its per-key `set_deserialize` throw escapes to a function-level `catch(std::exception&)` that logs and `return -1`, failing the **entire project config load**, not just the offending key.

**Decision — drop the defs AND add explicit SLA-project detection to the 3MF importer.** Before config parsing, detect `printer_technology = SLA` in `Metadata/project_settings.config` and refuse the load with a clear "SLA projects are not supported by this fork" message. Implementation note: the check must be a **pre-pass** over the JSON, not a branch inside `load_from_json`'s key loop — JSON iteration order is not guaranteed, so an SLA key can be reached before `printer_technology` and throw first. Blast radius is narrow (only `.3mf` saved under an SLA printer profile carries SLA keys, and such projects are unopenable in a PNP fork regardless), but the alternative is an opaque whole-project load failure, which would be a silent `.3mf` back-compat break against AGENTS.md's mandate.

(`handle_legacy()`'s obsolete-key clearing — which records into `unrecogized_keys` and returns without throwing — was the considered alternative; explicit detection was chosen for the clearer user-facing message. It remains available as a fallback if a real SLA-key-bearing FFF project is ever found.)

### What stays (verified independent of `Print::process()`)

- **`GCodeProcessor` — confirmed separable.** `GCodeProcessor.cpp` includes only config/geometry headers; its sole use of `Print.hpp` is the static utility `Print::get_hrc_by_nozzle_type()`. It needs only `PrintConfig` + a G-code file. None of the other `GCode/` helpers (`ToolOrdering`, `WipeTower*`, `SeamPlacer`, `CoolingBuffer`, …) are pulled in by it. This is the preview path (ticket 007).
- **Variable/adaptive layer height — confirmed independent.** `GLCanvas3D::LayersEditing` uses only `PrintObject::update_layer_height_profile()` and `PrintObject::slicing_parameters()` (both **static**, pure functions of `ModelObject` + config + mesh bounds) and the free functions `adjust_layer_height_profile()` / `generate_object_layers()` in `Slicing.cpp`. No `PrintObjectStep` execution required; the editor works fully without native slicing.
- **`Print::apply()`** (in its own file `PrintApply.cpp`) and **`Print::validate()`** — retained per ticket 003. `validate()` is pure config/geometry sanity checking and needs no step to have run.
- Arrange (`ArrangeJob`, `FillBedJob`, `PartPlate`), Orient (`OrientJob`), MeshBoolean, Cut (`CutUtils`/`CutSurface`), Simplify (`QuadricEdgeCollapse`), fix-through-netfabb, `TriangleMesh`, `Geometry::`, `Model`/`ModelObject`/`ModelVolume`, 3MF/STL/STEP I/O — all mesh/geometry only, no slicing dependency.
- `Layer` / `LayerRegion` / `Surface*` stay as **data containers** (preview and `SlicingParameters` read them) even though the steps that populate them die.
- `PrintBase.cpp`'s step-state machine (`set_started`/`set_done`) — `Print::apply()`/`validate()` and gizmo code rely on it.

### Deletable in bulk vs. surgical

**Droppable from the build wholesale** (no symbol outside `Print.cpp`/`PrintObject.cpp` references them — verified: no `src/slic3r/GUI` include of `Fill/*.hpp`, `Support/*.hpp`, `Arachne/*.hpp`, `PerimeterGenerator.hpp`): `PrintObjectSlice.cpp`, `MultiMaterialSegmentation.cpp`, `PerimeterGenerator.*`, `Arachne/*`, `Fill/*`, `Support/*`, `Brim.cpp`, `GCode/ToolOrdering.*`, `GCode/WipeTower*.cpp`, `GCode.cpp` (the generator) and its exclusive helpers (`CoolingBuffer`, `SeamPlacer`, `SpiralVase`, `AvoidCrossingPerimeters`, `PressureEqualizer`, `FanMover`, `ExtrusionProcessor`, `AdaptivePAProcessor/Interpolator`, `SmallAreaInfillFlowCompensator`, `RetractWhenCrossingPerimeters`), `GCode/ConflictChecker.*`.

**Surgical — cannot delete the file:** `Print.cpp` and `PrintObject.cpp` interleave pipeline-step methods (`make_perimeters`, `infill`, `generate_support_material`, `contour_z`, the `Print::process()` body) with structural accessors the GUI needs (`config()`, `instances()`, `model_object()`, `skirt()`, `print_statistics()`, `slicing_parameters()`, `update_layer_height_profile()`). Delete the step methods, keep the accessors.

**`Print::export_gcode()` must be removed, not stubbed-and-kept:** it calls into the deleted `GCode.cpp` generator, and its only callers are `BackgroundSlicingProcess` (being replaced) and `CalibUtils` (being stubbed) — so with both gone it has no callers and can go. If any caller were retained it would have to be reimplemented against the CLI rather than left declared, or the link fails.

**Known consequence:** `Print::print_statistics()` is populated only during the now-deleted `psGCodeExport`. Its GUI readers (`GCodeViewer.cpp`, and the sidebar via the slicing process) must source stats from the PNP path instead — ticket 007 established that Orca's `GCodeProcessor` recomputes time/filament from the parsed moves itself, so this is largely covered; grams/cost depend on pnp handoff item 10's `filament_density`/`filament_cost` passthrough. Flagged for ticket 011's consolidation.
