---
title: Headless --slice CLI deletion + calibration mock
status: done
batch: B5
blocked-by: [F10]
files: [src/OrcaSlicer.cpp, src/slic3r/Utils/CalibUtils.cpp, src/libslic3r/calib.cpp, src/libslic3r/calib.hpp, src/libslic3r/Model.hpp, src/libslic3r/Model.cpp, src/libslic3r/Print.cpp, src/libslic3r/GCode.cpp, src/slic3r/GUI/Plater.cpp, src/dev-utils/OrcaSlicer_profile_validator.cpp]
---

## Goal

First rip-out batch, per [ticket 008](../../tickets/008-native-slicing-rip-out-scope.md) §1–2
(**read the resolution in full** — it is the authoritative delete/keep list).

## Decisions (do not re-open)

- `src/OrcaSlicer.cpp`: delete the `--slice`/`--export` slicing path (plate loops,
  `load_cached_data`, status callbacks); keep cheap non-slicing verbs (3mf conversion, `--info`).
  Not reimplemented as a shell-out — `pnp_cli` is the CLI.
- Calibration: **UI survives as a mock, implementation deleted.** Keep `Calib_Params`/`CalibMode`
  types in `calib.hpp`, the dialogs (`calib_dlg.*`, `Calibration*.cpp`, `CalibrationWizard*`,
  `ExtrusionCalibration.*`) and menu entries. Delete `calib.cpp` generators,
  `Model::calib_pa_pattern`, `Print::set_calib_params` + every read in `Print.cpp`/`GCode.cpp`,
  `Plater::_calib_pa_*`, and `CalibUtils`' slice-and-upload path. Stub each `Plater::calib_*`
  and the still-referenced `CalibUtils` entry points to a "not implemented" notification.
- Delete the `dev-utils` profile validator (slices to validate; meaningless post-rip-out) and
  its CMake target.
- Clean deletion, **no `#ifdef` hiding**.

## Done / verify

- `ALL_BUILD` RelWithDebInfo clean.
- Smoke: PNP slice still works; every calibration menu entry opens its dialog and clicking OK
  posts the not-implemented notification (no crash); `orca-slicer.exe --info <stl>` still works,
  `--slice` is gone from `--help`.
