---
title: Crash and format fixes — clean picks
status: open
type: implementation
assignee: ""
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

## Done when

Six commits, test hunks dropped or ported, build and tests green, screenshots attached, ledger
evidence filled.
