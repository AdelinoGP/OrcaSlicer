---
title: FFF pipeline removal, slicing-test deletion, support-preview drop
status: done
batch: B7
blocked-by: [F12]
files: [src/libslic3r/Print.cpp, src/libslic3r/PrintObject.cpp, src/libslic3r/GCode.cpp, src/libslic3r/Fill/, src/libslic3r/Support/, src/libslic3r/Arachne/, src/libslic3r/PerimeterGenerator.cpp, src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.cpp, src/slic3r/GUI/BackgroundSlicingProcess.cpp, tests/fff_print/, src/libslic3r/CMakeLists.txt]
---

## Goal

The final deletion, per [ticket 008](../../tickets/008-native-slicing-rip-out-scope.md) §3–4 and
its bulk-vs-surgical list — **the resolution is authoritative; follow it file by file.**

## Key points (from 008, do not re-open)

- **Bulk-droppable** (verified no outside references): `PrintObjectSlice.cpp`,
  `MultiMaterialSegmentation.cpp`, `PerimeterGenerator.*`, `Arachne/*`, `Fill/*`, `Support/*`,
  `Brim.cpp`, `GCode/ToolOrdering.*`, `GCode/WipeTower*`, `GCode.cpp` + its exclusive helpers
  (`CoolingBuffer`, `SeamPlacer`, `SpiralVase`, `AvoidCrossingPerimeters`, `PressureEqualizer`,
  `FanMover`, `ExtrusionProcessor`, `AdaptivePA*`, `SmallAreaInfillFlowCompensator`,
  `RetractWhenCrossingPerimeters`), `GCode/ConflictChecker.*`.
- **Surgical**: `Print.cpp`/`PrintObject.cpp` — delete step methods (`Print::process` body,
  `make_perimeters`, `infill`, `generate_support_material`, `contour_z`), keep structural
  accessors (`config()`, `instances()`, `model_object()`, `print_statistics()`,
  `slicing_parameters()`, `update_layer_height_profile()`). `Print::apply()`
  (`PrintApply.cpp`) and `validate()` stay. `Print::export_gcode()` is **removed** (callers gone).
- **Delete `BackgroundSlicingProcess.{hpp,cpp}`** — F09 left it unreferenced.
- Support-painting preview: delete `PrintObject::generate_support_preview()` and
  `GLGizmoFdmSupports`' preview thread; **gizmo stays paint-only** (facets still reach PNP via
  the sidecar). pnp handoff item 13 restores the overlay later.
- Tests: delete `tests/fff_print/test_print.cpp`, `test_skirt_brim.cpp`, `test_gcodewriter.cpp`,
  the process cases in `test_model.cpp`/`test_helpers.cpp`/`test_trianglemesh.cpp`,
  `tests/libslic3r/test_toolordering_nozzle_group.cpp`. Keep geometry/mesh/3mf/config tests.
  No `#if 0` tagging.
- Must survive, verified independent: `GCodeProcessor` (preview), variable-layer-height editor
  (static `PrintObject` fns + `Slicing.cpp` free fns), `Layer`/`LayerRegion` as data containers,
  `PrintBase` step-state machine, arrange/orient/cut/repair.

## Done / verify

- `ALL_BUILD` clean; **M2 close**: full smoke ritual + `ctest --output-on-failure` (remaining
  suites) + variable-layer-height editor opens and edits + support painting paints without crash.

## Completion note (driving session, B7)

Done 2026-07-19. `ALL_BUILD` RelWithDebInfo clean (0 errors), orca-slicer.exe + all test exes
relinked; regenerated vcxproj/CTest dropped every removed source. `ctest` all remaining suites
100%: libslic3r 134/134, fff_print 17/17, pnp 2/2, slic3rutils 5/5, libnest2d 21/21,
filament_group 3/3. Headless: `--info` on STL (Prusa.stl / 20mmbox-CRLF.stl) and on an FFF `.3mf`
exit 0 with real geometry; no orphan pnp_cli. (`--info 20mmbox-CR.stl` exits 3 — a CR-only-line-ending
malformed ASCII STL edge case, reproducible and unrelated to F13.)

**Scope decisions beyond the ticket's literal file list — the "bulk-droppable" premise was wrong
at the header level, so these are the minimal fixes preserving the always-compiling invariant:**

- **ToolOrdering / WipeTower / WipeTower2 RETAINED (types + `.cpp`), not deleted.** Their types
  saturate `Print.hpp`'s public `WipeTowerData` / `FakeWipeTower` / `tool_ordering()` surface, and
  live GUI/preview code consumes them at compile time: LibVGCode reads `WipeTower::ToolChangeResult`
  via `wipe_tower_data().priming/tool_changes/final_purge`; Plater calls `get_tool_ordering().first_extruder()`;
  GLCanvas3D reads `wipe_tower_data().wipe_tower_mesh_data`; PartPlate/GLCanvas3D/OrcaSlicer call the
  static geometry helpers `WipeTower::get_auto_brim_by_height` / `get_limit_depth_by_height` /
  `move_box_inside_box` / `WipeTower2::get_wipe_tower_cone_base` for plate layout. All three headers are
  self-contained (Point/Polygon/TriangleMesh/ExtrusionEntity/PrintConfig only). Only `WipeTower2.cpp`'s
  dead cone-base-infill block used `Fill::` — that block was neutralized (unreachable at runtime; the
  wipe tower is never generated in the fork).
- **`GCode/ConflictChecker.hpp` RETAINED (type header), `.cpp` deleted.** It defines `ExtrusionLayer`/
  `ExtrusionLayers`, used by the retained `FakeWipeTower::getTrueExtrusionLayersFromWipeTower`.
- **`MultiMaterialSegmentation.hpp` RETAINED (type header), `.cpp` deleted** — its `ColoredLine` +
  boost.polygon traits back the kept `Geometry/Voronoi` `construct_voronoi<ColoredLines>` instantiation.
- **`Arachne/` deleted EXCEPT `utils/PolygonsSegmentIndex.hpp` + `utils/PolygonsPointIndex.hpp`** —
  those two index headers back the kept `Geometry/Voronoi`/`MedialAxis` chain (reachable from `ExPolygon`).
- **`Feature/FuzzySkin/` and `Feature/Interlocking/` deleted** (perimeter/infill features consumed only
  by the deleted `PerimeterGenerator` / `PrintObjectSlice`); plus `GCode/PrintExtents`,
  `TimelapsePosPicker`, `PchipInterpolatorHelper` (generator-only orphans after their consumers went).
- **`BackgroundSlicingProcess.{hpp,cpp}` deleted; its two wx event classes (`SlicingStatusEvent`,
  `SlicingProcessCompletedEvent`) relocated to new `src/slic3r/GUI/SlicingProcessEvents.{hpp,cpp}`** —
  the live PNP path (PnpSlicingProcess/Plater/PartPlate/GLCanvas3D/PnpProgress) still posts them.
- **`Print::process` STUBBED (empty no-op), not deleted** — `PrintBase::process` is pure-virtual and
  `Print` is instantiated. `export_gcode` / `export_gcode_from_previous_file` deleted (no callers).
- **Surgical**: `LayerRegion::make_perimeters` + `Layer::make_perimeters` deleted; PrintObject step
  methods referencing Fill/Support/Arachne deleted; `PrintObject::{infill_only_where_needed,
  clip_multipart_objects}` static defs + `sort_object_instances_by_model_order` relocated out of the
  deleted `PrintObjectSlice.cpp` / `GCode.cpp` into their retained callers; stale `GCode.hpp` includes
  cleaned from Format/AMF|3mf|bbs_3mf.cpp, CustomGCode.cpp, IMSlider.cpp, OrcaSlicer.cpp;
  `AdaptivePAProcessor::validate_adaptive_pa_model` (pure config-string validator) relocated into its
  only caller `ConfigManipulation.cpp`.
- **Support-painting preview**: `PrintObject::generate_support_preview` deleted; `GLGizmoFdmSupports`
  is paint-only (worker thread removed). Accepted v1 UX regression = pnp handoff item 13.

**HUMAN EYEBALL PENDING (GUI-only, script can't drive):** PNP GUI slice progress/preview/tmp-gcode/
warnings-jsonl growth; variable-layer-height editor opens and edits; support painting paints without
crash (no generated overlay); plus the carried B4/B5/B6 set (legend weight+cost, cancel kill-tolerance,
calibration mock dialogs, SLA .3mf refusal dialog + FFF .3mf project opens).
