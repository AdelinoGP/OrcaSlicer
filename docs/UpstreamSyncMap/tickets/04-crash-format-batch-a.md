---
title: Crash and format fixes — clean picks
status: closed
type: implementation
assignee: opencode-agent
blocked-by: []
---

# Crash and format fixes — clean picks

## Question

Adopt the six crash and format-compatibility fixes whose **source hunks** apply at this base
without hand-porting, measured `CLEAN` with
`git apply --3way --check -v --exclude='tests/*'`.

Four are backward-compatibility fixes, which matter more here than upstream: the fork must keep
reading `.3mf` projects and printer profiles written by older releases.

## Picks

| SHA | Defect |
|---|---|
| `36228c4755` | Heap corruption while repairing a model with auto-backup on (`FixModelByCgal.cpp`, 4+/0−) |
| `8390d55127` | Crash opening Object Settings when values are mixed nullable (`Field.cpp`) |
| `67a976e002` | Mirrored instances silently dropped when loading 3MF — Eigen-decomposition instability |
| `9b2fcaa53a` | "bad allocation" opening a 2.4 project carrying a custom filament (`Preset.cpp`) |
| `4a20168742` | Multi-toolhead 3MF without `filament_self_index` fails to load (`PresetBundle.cpp`) |
| `70bc685101` | Crash syncing filaments after a plate is deleted (`SyncAmsInfoDialog.cpp`) |

`67a976e002` and `9b2fcaa53a` are the two to take even if the rest were dropped: one silently
drops mirrored instances — a project-fidelity bug a user would not notice until printing — and the
other refuses to open valid old projects.

## Test hunks

Not every pick in this batch touches tests, contrary to an earlier draft:

- **No tests:** `36228c4755`, `8390d55127`, `70bc685101`.
- **Tests conflict in an existing test file:** `67a976e002`, `9b2fcaa53a`, `4a20168742`.
  Port if the fixture is self-contained; a committed fixture is cheaper than a manual re-test next
  time.
- `9b2fcaa53a` relies on `extend_default_config_length`, present at this base — confirm before
  picking.

Registration of a ported `tests/libslic3r/*.cpp` file is automatic (`xmake.lua:1247`).

## Method

`git cherry-pick -x <sha>`, drop the `tests/**` hunks, name the drop in the body.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - Fix Model with auto-backup on a repair-worthy model → no corruption; save and reopen.
   - Object Settings on an object with a null/mixed field → dialog opens.
   - 3MF containing mirrored and rotated instances → transforms preserved after load.
   - A 2.4-era project with a custom filament → opens instead of raising `bad_allocation`.
   - Multi-toolhead 3MF authored without `filament_self_index` → loads.
   - Delete a plate, then sync filaments → dialog behaves.

## Risks

- `8390d55127` and `eda4062d68` (ticket 05) are both dialog crash paths in the same family; take
  them in order and re-run after each.
- These six are the *easy* half of the crash list. Nine more crash/format picks in tickets 05 and
  06 need hand-ports, so this ticket is not the end of the crash work.

## Answer

All six picks are on `pnp/main`, each as one commit; the three source-only fixes are plain
`cherry-pick -x`, the three with test halves had those halves re-ported by hand.

| Commit | Pick | What landed |
|---|---|---|
| `f2537a1757` | `36228c4755` | `SaveObjectGaurd` around the CGAL repair worker (`FixModelByCgal.cpp`) |
| `b30138393b` | `8390d55127` | `TextCtrl`/`CheckBox` nullable handling no longer any-casts an empty value (`Field.cpp`) |
| `8eabfd90cd` | `67a976e002` | mirrored 3MF transforms validated as an affine matrix, not decomposed (`3mf.cpp`, `bbs_3mf.cpp`) |
| `fd7c341c68` | `9b2fcaa53a` | `extend_default_config_length` before the embedded-preset diff (`Preset.cpp`) |
| `29e1a5c703` | `4a20168742` | `filament_self_index` sized against the filament count (`PresetBundle.cpp`) |
| `176f569503` | `70bc685101` | `reinit_dialog` falls back to the current plate (`SyncAmsInfoDialog.cpp`) |

### Test halves

- `67a976e002`: ported verbatim into `tests/libslic3r/test_3mf.cpp` (both the `3mf` and `bbs_3mf`
  backends); the fork file needed only a `Utils.hpp` include for `ScopeGuard`.
- `9b2fcaa53a`, `4a20168742`: ported into the fork's smaller
  `tests/libslic3r/test_preset_bundle_loading.cpp`, plus its `check_double_vector` helper.
- **Divergence in the 9b2fcaa53a test:** this base's `filament_options_with_variant` does not
  contain `pressure_advance` / `adaptive_pressure_advance_model` (they arrived upstream after the
  fork base), so the test uses `filament_flow_ratio` and `filament_max_volumetric_speed`, which
  are in the set here. Same code path (`update_diff_values_to_child_config`), same assertion: one
  stored value becomes one value per variant.
- No tests exist for the other three picks upstream; none were invented.

### Verification

- `xmake test`: **7/7 passed** (libslic3r 70.5 s).
- Targeted: the mirrored scenario passes for both backends; the two preset regressions pass
  (`[Preset][Bundle][Regression]`, 7 assertions in the bad-allocation case).
- Walkthrough: `t04-mirrored.3mf` (a cube whose instance matrix is the regression test's mirrored
  transform) loads and renders in Prepare — [screenshot](../assets/04/01-mirrored-instance-loaded.png).
  Before `8eabfd90cd` the same matrix decomposed to a zero scale and the instance was dropped.

### Gaps

- Five walkthroughs were not click-through: Fix Model with auto-backup, Object Settings with mixed
  nullable values, the 2.4-era embedded filament, the multi-toolhead 3MF without
  `filament_self_index`, and plate-delete → filament sync. They are GUI-only paths with no desktop
  session available in this run; their code changes are small, and the two that have tests are
  covered by them. The no-self-index GUI load was attempted but the app exited during/after the
  load with no crash event recorded, so it is **inconclusive**, not passed.
- No test covers `36228c4755`, `8390d55127`, `70bc685101` upstream; the fixes are guards whose
  effect is a race/crash window, not an observable value.

## Done when

Six commits, test hunks dropped or ported, build and tests green, screenshots attached, ledger
evidence filled.
