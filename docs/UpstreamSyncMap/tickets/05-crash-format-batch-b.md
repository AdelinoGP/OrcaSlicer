---
title: Crash and format fixes — mixed
status: closed
type: implementation
assignee: opencode-agent
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
`git range-diff` recorded. **One pick, `4c2ae6d202`, does touch `tests/`** (the other five do not; re-measured in the Answer). An earlier draft claimed
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

## Answer

Four of the six picks are on `pnp/main`. The other two were re-measured and **not landed**: the
defect they fix does not exist at this base. A fifth commit pins the behaviour one of them would
have protected.

| Commit | Pick | What landed |
|---|---|---|
| `3950da06c6` | `e410cf3656` | Preset-sync thread pushes notifications via `CallAfter`, drops them once closing or stopped, and returns early from the bundle loop (`GUI_App.cpp`). Plain `cherry-pick -x` |
| `6d3d692e1f` | `5bf1571bb8` | Move gizmo in Object coordinates with several instances (`Selection.cpp`). Plain `cherry-pick -x` |
| `4eb5b9f012` | `eda4062d68` | `load_files`: null-checked `filament_colour` / `sparse_infill_*` reads, one `skip_project_config` fallback for every skipped config, one 2.3.1-alpha migration lambda, extruder step moved after the checks. `cherry-pick(partial)`, hand-ported |
| `0f47117209` | `3c72223aaf` | Null-font guards in `PopNotification`/`HintNotification::init`; once-per-session make-current failure report; `load_gcode_preview` guard. `cherry-pick(partial)` |
| `63cf70cee6` | `4c2ae6d202` (not picked) | `test(3mf)`: instance-level pin that a build item's `auto_drop` survives load (true and false) |
| — | `c9d897b4d0` (not picked) | — |

### Not landed

- **`4c2ae6d202` (Auto Drop).** It repairs `a338f6ca7d`, which moved `auto_drop` from
  `ModelInstance` to `ModelObject` and then defaulted a missing object attribute to on. This base
  never made that move: `_handle_start_item` reads the item attribute straight into
  `instance->auto_drop`, and `ModelObject` has no such field. The conflict is therefore structural
  (the `ObjectMetadata::auto_drop` the hunks edit does not exist), and a port means taking
  `a338f6ca7d` (7 files). Features enter through tickets, so it is out of scope. The ticket's
  claim that no pick touches `tests/` was **wrong for this pick**: it adds a case to
  `test_3mf.cpp` whose fixture sets `ModelObject::auto_drop`; dropped with the pick.
- **`c9d897b4d0` (Send dialog).** The whole function it edits, `update_material_item_pos`, is absent
  (it arrives with in-range `5cf7e27c23`). Here the dialog picks its panel from the project's
  `nozzle_diameter` count, the same predicate that fills `m_filaments_map`
  (`reset_and_sync_ams_list`), so the upstream empty-map read is not reachable by that route.

### Divergence notes

- `eda4062d68`: the conflict was context drift. A sorted +/- line comparison of the fork diff
  against upstream's matches except the fork's `int current_height` (upstream's `double` is an
  unpicked commit) and one comment upstream removed that was kept. Fork-owned content intact: no
  published-3MF block, the fork's `file_percent` line, and `En3mfType`'s four values are all still
  covered, so dropping the two unreachable chain tails is safe. New string
  `msg_no_project_settings` is not in any catalog (ticket 07).
- `3c72223aaf`: no `_render_frame` here (`render()` carries the split); no
  `_set_shown_canvas_current()`, so `load_gcode_preview` gets only the `init_opengl()` guard;
  `SCENE_CANVAS_NAME` not added. `git range-diff` for each pick shows only message and these
  context changes.
- **Shared notification plumbing.** The make-current failure is a one-shot `show_error` dialog
  gated by `m_make_current_failure_reported` (set before anything else in
  `report_make_current_failure`), so it cannot repeat per frame. It is not a `NotificationManager`
  entry. The `NotificationManager` hunks only return early while `ImGui::GetFont()` is null;
  `render()` still calls `init()` for `Unknown` notifications.

### Verification

- `xmake -j2 libslic3r_tests`: ok, 412.9 s. `xmake -j2 OrcaSlicer`: ok, 1,673.7 s (no errors).
  Built once after all commits, not per commit, so the intermediate commits are not individually
  compile-verified.
- `xmake test`: **7/7 passed** (libslic3r 73.3 s, filament_group 53.4 s). The new pin test: 42
  assertions in 1 case (both `GENERATE` values). Running `[3mf]` directly from the repo root shows one
  failure writing `/tmp/orca.ascii` (an environment path, not touched by this batch); under
  `xmake test` it passes.
- GUI, `orca-slicer.exe` from the build tree, driven with cua-driver; fixtures are scratch files
  made from a GUI-saved project (not committed):
  - **(4) Project with a stripped or empty settings block — observed.** `project_settings.config`
    removed, and replaced by `{}`: both open to Prepare with the default printer/filament/process
    and the geometry, no crash ([no settings](../assets/05/02-no-project-settings.png),
    [empty](../assets/05/03-empty-project-settings.png)). No before-fix run, so this shows the fixed
    build opens them, not that the old one crashed.
  - **(3) 2.4.x project with Auto Drop off — observed, but not a test of the pick.** Item
    `auto_drop="0"`, `OrcaSlicer` tag `2.4.2`, object floated at Z 60: loads with Z still 60.00
    ([move gizmo](../assets/05/05-autodrop-off-move-gizmo-z.png)) and the Auto Drop menu item
    unchecked ([context menu](../assets/05/06-autodrop-off-context-menu.png)). This base already
    behaved this way, which is why the pick was not landed.
  - **(2) Move gizmo, Object coordinates, one of several instances — observed.** Added a second
    instance, selected it, switched the gizmo to Object, dragged its X arrow ~200 px: the drag
    completed, the instance moved, the app stayed up
    ([screenshot](../assets/05/07-move-gizmo-object-coords-instance-drag.png)). No before-fix run.
  - **Notification plumbing — observed.** A PNP preset warning (amber) and a regular
    notification render together after the `3c72223aaf` change
    ([screenshot](../assets/05/01-notification-banners-render.png)).

### Gaps

- **(1) Quit during a preset sync — not exercised.** `start_sync_user_preset` runs only for a
  signed-in OrcaCloud account with subscribed bundles; the profile used was "Not signed in". Plain
  quits of the build (Alt+F4, several launches) exited with no process left, which says nothing
  about the in-flight race.
- **(5) One-nozzle plate on a two-extruder printer, Send dialog — not exercised.** The pick was not
  landed, and the dialog needs a Bambu printer in the device list.
- **(6) Forced GL context failure — not exercised.** No driver toggle or acceleration-less VM was
  available, and nothing short of one makes `wglMakeCurrent` fail. The new code is therefore
  unexercised: the dialog text, the `GetLastError()` log, and the one-shot guard are reviewed, not
  observed. The skipped-measure notification path is likewise unobserved.
- The `skip_project_config` info log line was not seen: the session log was near-empty at the log
  level in use.
- No test exists for `e410cf3656`, `5bf1571bb8`, `eda4062d68`, `3c72223aaf` upstream; none was
  invented.
- Observation, not fixed: `SelectMachineDialog::auto_supply_with_ext` indexes `m_filaments_map`
  without a bounds check.

## Done when

Six commits (two clean, four partial), build and tests green, screenshots including the
hard-to-reach GL and shutdown cases, ledger evidence filled.
