---
title: Crash and format fixes — mixed
status: open
type: implementation
assignee: ""
blocked-by: []
---

# Crash and format fixes — mixed

## Question

The next six crashes, two of which apply cleanly and four of which need hand-ports. The batch is
heavier on GUI lifecycle than ticket 04: shutdown races, GPU-context failure, project load, and
gizmo manipulation, so its walkthrough is "make the app hit the bad state on purpose" rather than
"open a file".

## Picks

| SHA | Defect | Source verdict | Conflict site |
|---|---|---|---|
| `e410cf3656` | Crash at exit from the preset-sync thread (`GUI_App.cpp`) | CLEAN | — |
| `5bf1571bb8` | Move-gizmo crash with object coordinates and multiple instances (`Selection.cpp`) | CLEAN | — |
| `4c2ae6d202` | Auto Drop silently re-enabled for 2.4.x projects (`bbs_3mf.cpp`, 4 hunks) | CONFLICT | `bbs_3mf.cpp` |
| `eda4062d68` | Crash opening a project with missing or empty settings (`Plater.cpp`, 71+/109−) | CONFLICT | `Plater.cpp` |
| `c9d897b4d0` | Send-dialog crash: one-nozzle plate, two-extruder printer (`SelectMachine.cpp`) | CONFLICT | `SelectMachine.cpp` |
| `3c72223aaf` | OpenGL context activation failure crashes or says nothing (5 files) | CONFLICT | `GLCanvas3D.cpp`, `OpenGLManager.cpp` |

`eda4062d68` is the largest port and rewrites a chunk of `Plater.cpp`. Note that it is a **real**
conflict, not a measurement artefact: the 3-way merge leaves markers in the source file.

## Method

Two `cherry-pick -x`, four `cherry-pick(partial)` with the conflicting file named in the body and
`git range-diff` recorded. **No pick in this batch touches `tests/`** — an earlier draft claimed
`c9d897b4d0` and `3c72223aaf` add tests and that the latter is "the only automated proof of the
notification path"; both claims were wrong, so the GL-notification change ships with manual
evidence only.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - Quit while a preset sync is in flight → clean exit.
   - Move gizmo on one of several instances, in object coordinates → drag completes.
   - Project saved with Auto Drop off by 2.4.x → still off after load.
   - Project with a stripped/empty settings block → opens with defaults, no crash.
   - One-nozzle plate with a two-extruder printer → Send dialog opens.
   - Force OpenGL context failure (driver toggle or a VM without acceleration) → a message, not a
     crash.

## Risks

- `3c72223aaf` changes failure behaviour from silent to notified, which touches the notification
  plumbing shared with PNP warnings. Verify PNP warning banners still render, and that a GL
  failure does not spam the user with a notification per frame.
- `eda4062d68` is the highest divergence risk in the batch; its `Plater.cpp` port must be reviewed
  as fork-owned code, not as an upstream change.

## Done when

Six commits (two clean, four partial), build and tests green, screenshots including the
hard-to-reach GL and shutdown cases, ledger evidence filled.
