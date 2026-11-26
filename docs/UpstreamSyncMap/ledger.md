---
labels: [wayfinder:ledger]
title: Upstream sync ledger — 33909a51cd..808dd7dff8
---

# Upstream sync ledger

Range: `pnp-fork-base` (`33909a51cd`, 2026-07-16) → upstream `808dd7dff83f` (2026-11-25).
3,554 commits, 2,977 non-merge. A future pass starts here, not from `git log`.

## Method, and why it changed

Source applicability was measured on 2026-11-25 with:

```bash
git show <sha> --format= | git apply --3way --check -v --exclude='tests/*'
```

**CLEAN** = no conflict markers and no missing paths; **CONFLICT** = the 3-way merge leaves
markers, so the pick needs a hand-port; **ERROR** = a path in the patch does not exist.

Two corrections against the first draft, both material:

- `git apply --check --3way` **exits 0 even when the merge leaves conflict markers**. The first
  draft read exit codes and claimed 19 of 25 picks were clean. The true split across the 24 fix
  picks is **12 CLEAN / 12 CONFLICT**; with the added `d7534d7fb9` the table has 25 rows and 13
  conflicts. The tickets are partitioned around this.
- `tests/**` hunks are excluded from the source verdicts because their paths differ here. That is
  **not** the same as "tests cannot land": `xmake.lua:1247` globs `tests/libslic3r/*.cpp`, so a
  ported test registers automatically. Only `tests/pnp` lists files explicitly. Per-pick test
  reality is recorded below.

## Adopted — security hardening

| SHA | Date | What it fixes | Source | Conflict site / note | Ticket |
|---|---|---|---|---|---|
| `d112a0af29` | 09-09 | Two stack buffer overflows in ADMesh `stl_read` (unbounded `%[^\n]` solid name, MW metadata parse); `deps_src/admesh/stlinit.cpp` 2+/2− | CLEAN | — | 01 |
| `d35ea27ea5` | 09-29 | Zip entry sizes validated before parsing 3MF XML | CLEAN | Test half conflicts in an existing test file | 01 |
| `8ffd3e514e` | 09-29 | OBJ and DRC import hardened against malformed files | CLEAN | Tests apply too (new `test_drc.cpp`, `test_obj.cpp`); an earlier draft's `ERROR` was `tests/libslic3r/CMakeLists.txt` only | 01 |
| `41eeaf3883` | 09-29 | Server-supplied download filenames sanitized (`libslic3r/utils.cpp`, `Utils.hpp`, `DownloaderFileGet.cpp`, `Plater.cpp`) | CLEAN | Test half conflicts | 01 |
| `a124b7d2df` | 10-09 | Cyclic 3MF component graphs rejected before OOM | CONFLICT | `Format/bbs_3mf.cpp` | 02 |
| `490d134507` | 09-29 | 3MF plate-ID and paint-data hardening | CONFLICT | **Inside `TriangleSelector.cpp`**; `bbs_3mf.cpp` and `Model.cpp` apply cleanly; test half conflicts | 02 |
| `203bc63f35` | 09-30 | Project metadata escaped before rendering; accessory opening restricted | CONFLICT | `Utils.hpp`, `utils.cpp`; `GUI/Project.cpp` and the web asset clean | 02 |
| `e40030cf81` | 09-30 | Malformed network JSON no longer crashes (11 source files + 3 test/CMake) | CONFLICT | **`PrintHost.cpp` conflicts**; `Utils/UltiMaker.cpp|hpp` do not exist here and are omitted (ticket 08 owns them) | 02 |

## Adopted — crash and format fixes

| SHA | Date | What it fixes | Source | Conflict site / note | Ticket |
|---|---|---|---|---|---|
| `36228c4755` | 09-02 | Heap corruption repairing a model with auto-backup (`FixModelByCgal.cpp`, 4+/0−) | CLEAN | No tests | 04 |
| `8390d55127` | 07-21 | Crash opening Object Settings with mixed nullable values (`Field.cpp`) | CLEAN | No tests | 04 |
| `67a976e002` | 10-04 | Mirrored instances silently dropped on 3MF load | CLEAN | Test half conflicts | 04 |
| `9b2fcaa53a` | 10-16 | "bad allocation" opening a 2.4 project with a custom filament | CLEAN | Relies on `extend_default_config_length`, present here; test half conflicts | 04 |
| `4a20168742` | 10-09 | Multi-toolhead 3MF without `filament_self_index` fails to load | CLEAN | Test half conflicts | 04 |
| `70bc685101` | 11-03 | Crash syncing filaments after a plate is deleted (`SyncAmsInfoDialog.cpp`) | CLEAN | No tests | 04 |
| `e410cf3656` | 10-28 | Crash at exit from the preset-sync thread (`GUI_App.cpp`) | CLEAN | — | 05 |
| `5bf1571bb8` | 11-21 | Move-gizmo crash with object coordinates and multiple instances (`Selection.cpp`) | CLEAN | — | 05 |
| `4c2ae6d202` | 10-19 | Auto Drop silently re-enabled for 2.4.x projects (`bbs_3mf.cpp`, 4 hunks) | CONFLICT | `bbs_3mf.cpp` | 05 |
| `eda4062d68` | 10-30 | Crash opening a project with missing/empty settings (`Plater.cpp` 71+/109−) | CONFLICT | `Plater.cpp` | 05 |
| `c9d897b4d0` | 10-20 | Send-dialog crash, one-nozzle plate with two-extruder printer (`SelectMachine.cpp`) | CONFLICT | `SelectMachine.cpp`; no tests | 05 |
| `3c72223aaf` | 11-11 | OpenGL context failure crashes or stays silent (5 files) | CONFLICT | `GLCanvas3D.cpp`, `OpenGLManager.cpp`; no tests | 05 |
| `71c9f556f0` | 11-13 | Crash editing object speed without a matching variant (`Tab.cpp`) | CONFLICT | `Tab.cpp` | 06 |
| `38f12b1357` | 11-24 | Project load wipes a filament's compatible-printer list (`Preset.cpp`) | CONFLICT | `Preset.cpp` | 06 |
| `e56fd1ed44` | 11-25 | Crash deleting/merging a multi-part object, macOS (`ObjectDataViewModel.cpp`) | CONFLICT | Model tree; its test file is not portable as-is | 06 |
| `27cafaa06c` | 10-31 | Decimal-comma locale misreads nozzle diameters from 3MF | CONFLICT | **`PartPlate.cpp`, `SelectMachine.cpp`**; the `GCode.cpp` path does not exist and that hunk is omitted; `test_locales_utils.cpp` is absent here | 06 |
| `d7534d7fb9` | 11-11 | "Fix the Crash When the Per-Extruder Printable Areas Do Not Overlap" (#17439): `result_polygon[0]` on an empty intersection. Adds `placement_bed_shape` / `get_shared_poly` | CONFLICT | **`Print.cpp`, `PartPlate.cpp/.hpp`, `Selection.cpp`**; `PrintConfig.cpp/.hpp` apply cleanly. Its `Selection`/`PartPlate` hunks also call `get_shared_printable_polygon`, which arrives in `7c73739e1a` and is **absent here** — omit those hunks | 06 |

## Test-hunk reality per pick

Measured with only `CMakeLists.txt` excluded:

| Situation | Picks |
|---|---|
| Tests apply | `8ffd3e514e` (new `test_drc.cpp`, `test_obj.cpp`) |
| Conflict in an existing test file | `d35ea27ea5`, `41eeaf3883`, `67a976e002`, `9b2fcaa53a`, `4a20168742`, `490d134507`, `d7534d7fb9` |
| Target test file absent here | `test_triangle_selector`, `test_printhost`, `test_object_list`, `test_locales_utils`, `test_exclusion_volumes`, `fff_print/test_wipe_tower` |
| No tests at all | `d112a0af29`, `36228c4755`, `8390d55127`, `70bc685101`, `c9d897b4d0`, `3c72223aaf` |

Registration is automatic for `tests/libslic3r/*.cpp` via `xmake.lua:1247`. Porting a test whose
file is absent means taking the whole upstream file; porting one that conflicts means a hand-port
in a file the fork already owns.

## Adopted — profile data and config keys

| SHA / series | Disposition | Ticket |
|---|---|---|
| `15412cd812`, `91b712a237`, `ea35b9ce68`, `67645760f2`, `72f7b125c6` | New machine profiles: Bambu X2D/H2C/A2L, Snapmaker U1, Qidi X-Plus 5/X-Smart 5, Elegoo Centauri 2, Anycubic Kobra S1 Max | 03 |
| `8cb2e4e01e` | Deterministic `setting_id` — no code change needed; `generate_preset_setting_id` (`Preset.cpp:527`) is byte-identical to upstream | 03 |
| `43a83397d4`, `7d62ded630`, `8747605930` | Restore presets that were renamed; required for `renamed_from` coverage so user presets do not orphan | 03 |
| `3500a1e588` | **Not adopted**: upstream unified the id scripts and deleted `scripts/assign_vendor_setting_ids.py` / `orca_extra_profile_check.py`. The fork's copies agree with the same C++ rule | 03 |

Aggregate: 13,000 files changed (rename-detected), 13,658 paths with renames off (4,285 A /
7,697 M / 1,676 D), 145 vendor directories, 72 vendor index `version` bumps. **19** config keys
are referenced by shipped presets and absent here, across 673 files.

Corrections the reviews forced:

- `git diff 33909a51cd HEAD -- resources/profiles` is **empty**; profiles are a pure upstream
  snapshot and `BBL/cli_config.json` is not fork-specific.
- `filament_prime_volume` sits in the fork's **obsolete-key ignore set**
  (`src/libslic3r/PrintConfig.cpp:8405`; inherited from base, not fork-added), so adding the key
  alone leaves all 447 affected presets cleared. Ticket 03 removes it and reconciles the whole set:
  the fork's set equals upstream's **plus `filament_prime_volume`, minus twelve retired belt keys**
  (`belt_preslice_global`, `belt_slice_rotation_global`, `belt_support_floor_mode`,
  `belt_support_z_offset_mode`, `first_layer_plane`, `first_layer_plane_offset`,
  `first_layer_plane_thickness`, `gcode_back_transform`, `preslice_remap_global`,
  `preslice_remap_x`, `preslice_remap_y`, `preslice_remap_z`). Adding those twelve back is harmless
  (pnp-registered keys are exempt at `PrintConfig.cpp:8410-8428`) and is a ticket 03 decision, not
  a ticket 10 one.

## Adopted — translations

| Series | Disposition | Ticket |
|---|---|---|
| 157 commits touching `localization/**` (125 non-merge, 103 `.po`-only), +683,058/−296,957 | Wholesale replacement. Conflict-free: zero fork `.po` edits since the fork base | 07 |
| `2bd105b0ce` + `210bc927bc` | Thai added then reverted in-range — net zero; taken as part of the head snapshot | 07 |

Upstream ships **23** catalogs; the fork has 22. The extra is `eu` (Basque).

## Adopted — features (each carries a pnp prerequisite where its effect needs one)

| Series | Commits | Volume | Prerequisite | Ticket |
|---|---|---|---|---|
| UltiMaker host (`6e4be42a04` + 5 follow-ups) | 6 | +1,184 / 738 code lines, 19 files | none — `PrintHost` subclass | 08 |
| IMEX / iXex | 45 | +3,528, 12 paths | `pnp_cli` must execute modes and toolchanges; 8 keys are preset-referenced, upstream defines 14 `imex_*`/`is_imex` keys | 09 |
| Belt printers | 54 | +2,625, 17 files | `pnp_cli` belt kinematics and back-transform; 5 keys preset-referenced, upstream defines 11 belt/tilt keys (plus two SLA tilt times that rule 3 bars) | 10 |
| Plugin host | 168 | +23,083, 57 files | Python/pybind11 runtime if P3.1 keeps it; `slicingPipeline` cannot fire (`Print::process` stub) and is re-qualified as a Wasm ABI | 11 |
| DeviceCore / printer agent | 110 | +6,931/−1,409, 94 files | none. The fork already ships **56** `GUI/DeviceCore` files and diverges from base on only **3** | 13 |
| Design / CAD | 265 on CAD paths (~299 in the wider Design series) | +45,608, 35 files | none; needs SLVS built here (Conan recipe first, else `deps_src/`) | 14 |

## Deferred — named, not applicability-checked, no ticket

`176bb6062f` LAN-printer startup crash · `8d528b2a8f` Device-tab printer switch ·
`42009cf385` Flatpak thumbnail crash (`GLCanvas3D`) · `87a5d20d4c` GTK startup crash (`Tab.cpp`) ·
`91db25f2cf` Measure-tool crash (133+/140− rewrite) · `24cf1c6bcc` user-printer parent preset
crash · `16efd3458b` 3MF backup lock · `808dd7dff8` OBJ import with decimal comma (macOS) ·
`3f90d6ccf6` Export Preset Bundle list · `97e2d0a6e6` `Notebook.hpp` page-hide bug ·
`755af907c6` GLCanvas pan/rotate selection · `91d9cf01d0` fewer-filaments slice crash (its test
file is absent here).

## Rejected — defect absent from this fork

| SHA | Why rejected |
|---|---|
| `32f6db87e8` | "Fix validation of legacy bed exclusion areas" patches `is_valid_bed_exclude_area(...)` — a validator this fork has never had (`grep` = 0 in `PrintConfig.cpp/.hpp`). With no validator, no legacy area is rejected, so there is no defect to fix and the ticket's walkthrough would pass on today's build. Taking it would mean first porting the validator series (`c01ac8ff44`, `dbae415127`, `f832cae2eb`, `21760cc979`) — a feature, not a fix. **Correction:** an earlier draft made this pick the *reason* ticket 06 needed `d7534d7fb9`; the dependency does not exist. |

## Rejected — substrate removed

| SHA / series | Why rejected |
|---|---|
| 322 commits touching `GCode.cpp` / `GCode/` | Deleted; subject matter owned by `pnp_cli` |
| `258f927a0f` | Patches `GUI/BackgroundSlicingProcess.cpp`, deleted |
| `1f8111526c` | Contains a `SLAPrint.cpp` hunk; SLA removed (other hunks individually portable if ever needed) |
| `a816435087`, `6b51185fd6`, `0d6022477b`, `5941c5603d` | Calibration-wizard crashes; the calibration UI is a mock and its generators are gone |
| `185b7084d2`, `2cfdddf8b0`, `f540cf87a8`, `d9be71b1ca` | `Tab.cpp` IMEX code; `m_ixex_mode_combo` and friends do not exist at base (ticket 09 introduces them) |
| `9e5bfc272c` | Compare-Presets crash needs `GUI/ConfigValueFormatter.cpp`, absent |
| ~299 Design/CAD commits as upstream series | Superseded by ticket 14, which ports the feature |
| 168 plugin commits as upstream series | Superseded by ticket 11 with explicit re-qualification |

## Rejected — no value, or inverted by a later revert

| SHA | Why rejected |
|---|---|
| `87ca2bc42e` → reverted by `6ef02a67db` | Redone as `f05444dc94`; slicing-side |
| `d322b1a156` → reverted by `ba22973919` | Assembly parts / height ranges, net zero |
| `b46916a8be`/`edc2f8bf90`, `bf024ed34e`/`d51ad889fd` | Revert + reapply pairs, net zero |
| `d401d4f837`, `0028e65763`, `a860e533eb`, `bca3588ab0`, `dc0e269186`, `576cce2f72`, `d087941289`, `86153752e6` | OTA/CI scaffolding, marked temporary upstream |
| 165 docs/CI-only commits, 50 build-system-only commits | No target: xmake + Conan replaced CMake; CI is fork-specific |
| `826e36fb1e` (`enable_cooling_filter`) | Absent at upstream head; renamed to `support_cooling_filter` / `cooling_filter_enabled`, both already here (`PrintConfig.cpp:4180,4184`) |

## How to extend this ledger

Add a row before opening a ticket, with the source verdict recorded, and never record a pick as
clean from an exit code alone. Every CONFLICT row names the file that conflicted and what the
hand-port changed. Every row that ships without its tests says so.
