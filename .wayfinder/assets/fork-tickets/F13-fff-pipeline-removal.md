---
title: FFF pipeline removal, slicing-test deletion, support-preview drop
status: open
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
