---
title: SLA full cut incl. 3MF SLA-project detection
status: open
batch: B6
blocked-by: [F11]
files: [src/libslic3r/SLAPrint.cpp, src/libslic3r/SLA/, src/libslic3r/PrintConfig.cpp, src/libslic3r/Model.hpp, src/libslic3r/Format/bbs_3mf.cpp, src/slic3r/GUI/Plater.cpp, src/slic3r/GUI/GLCanvas3D.cpp, src/slic3r/GUI/Tab.cpp, src/slic3r/GUI/ConfigWizard.cpp, src/libslic3r/CMakeLists.txt]
---

## Goal

Remove SLA entirely, per [ticket 008](../../tickets/008-native-slicing-rip-out-scope.md) §5 —
**its resolution is the authoritative file-by-file list; follow it, not this summary.**

## Key points (from 008, do not re-open)

- Delete `SLAPrint.*`, `SLAPrintSteps.*`, `Format/SL1.*`, `src/libslic3r/SLA/` **except
  `IndexedMesh.*` and `Rotfinder.*`** — those graduate out of `SLA/` (they back `AABBMesh` and
  `RotoptimizeJob`).
- Delete `ModelObject`'s SLA fields (`sla_support_points`, `sla_points_status`,
  `sla_drain_holes`, `friend class SLAPrint`) incl. copy/assign/split/clone handling.
- Delete `sla_shift_z` across its 9 render files (`3DScene.*`, `GLCanvas3D.cpp`, `Selection.cpp`,
  `SurfaceDrag.cpp`, `GLGizmoCut/Flatten/Measure`, `GLGizmosCommon.cpp`).
- Delete `init_sla_params()`, the SLA config classes/enum tables, `--export-sla`/`--help-sla`,
  `PresetBundle` `sla_prints`/`sla_materials`, `Plater::priv`'s `sla_print`, the ~116 `ptSLA`
  branch sites, dead GUI files (already uncompiled gizmos, SLAImport, `TabSLA*`),
  `bbs_3mf.cpp`'s SLA write side, `RELOAD_SLA_*` flags.
- **MUST add explicit SLA-project detection to the 3MF importer**: a **pre-pass** over
  `Metadata/project_settings.config` JSON for `printer_technology = SLA`, refusing the load with
  a clear message — because `load_from_json` fails the *whole* project config on the first
  unknown key (unlike the per-key-tolerant ini/gcode paths), and JSON key order is not
  guaranteed. This is the `.3mf` back-compat mitigation; do not skip it.

## Done / verify

- `ALL_BUILD` clean; `ctest --test-dir ./tests/libslic3r` passes (geometry/model/config suites).
- Smoke: PNP slice unaffected; opening an SLA `.3mf` shows the "SLA projects are not supported"
  refusal (craft one fixture by saving a project under an SLA profile on stock Orca, or
  hand-edit `printer_technology`); an FFF `.3mf` project round-trips.
