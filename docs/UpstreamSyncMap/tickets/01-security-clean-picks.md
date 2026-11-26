---
title: Security hardening — clean picks
status: closed
type: implementation
assignee: opencode-agent
blocked-by: []
---

# Security hardening — clean picks

## Question

Adopt the four upstream hardening commits whose **source hunks** apply at this base without a
hand-port, so dragging a hostile file in, or pointing the app at a hostile device, stops being
able to smash the stack, exhaust memory, or write outside the download directory.

These are the cheapest wins in the range: four patches in files that still exist here, measured
`CLEAN` with `git apply --3way --check -v --exclude='tests/*'`.

## Picks

| SHA | File | Threat |
|---|---|---|
| `d112a0af29` | `deps_src/admesh/stlinit.cpp` (2+/2−) | Unbounded `%[^\n]` and `%s` on a hostile STL solid name → stack smash |
| `d35ea27ea5` | `Format/3mf.cpp`, `Format/bbs_3mf.cpp` | Zip-bomb / oversized entry read before any XML validation |
| `8ffd3e514e` | `Format/OBJ.cpp`, `objparser`, `DRC.cpp` | Malformed OBJ/DRC → out-of-bounds reads |
| `41eeaf3883` | `libslic3r/utils.cpp`, `Utils.hpp`, `DownloaderFileGet.cpp`, `Plater.cpp` | Path traversal from print-host/cloud-supplied download names |

## Test hunks (all four)

Each of these adds or edits files under `tests/`, and none of those paths exist here —
`8ffd3e514e` fails outright on `tests/libslic3r/CMakeLists.txt`. Rule 6 applies: the source half
lands here, and the test half is **dropped unless ported**:

- `d35ea27ea5` → 3MF zip-entry tests.
- `8ffd3e514e` → OBJ/DRC parser tests.
- `41eeaf3883` → download-name tests.
- `d112a0af29` adds none.

If a test is portable, register it in `xmake.lua`'s test target rather than leaving an orphan file;
if it is not, say so in the commit body so the ledger's "test hunks" note stays true.

## Method

`git cherry-pick -x <sha>`, then drop the `tests/**` hunks from the commit. The result is a
partially applied pick, which rule 4 permits, and the body must name what was dropped.

`41eeaf3883` introduces `download_marker_path` / `find_unused_filename`; confirm the base's
`DownloaderFileGet` flow before picking, since the marker plumbing is part of the patch.

## Verification

1. `xmake -j2` and `xmake test` — seven suites, no new failures.
2. Hostile-input walkthrough, screenshots per case:
   - STL whose solid name is very long → opens or refuses cleanly, no crash.
   - 3MF with an oversized zip entry → error dialog, no OOM.
   - OBJ with truncated geometry, and a malformed DRC → error dialog, no crash.
   - Print host returning `..\..\evil.gcode` → the file lands inside the download directory.
3. For `41eeaf3883`, also confirm a **normal** download still works: the sanitizer must not
   rename every file.

## Risks

- `41eeaf3883` touches `Plater.cpp`, the fork's most divergent GUI file (+221/−1172). It is
  measured clean, but that measurement excludes tests; re-check after the cherry-pick.
- The web-asset half of `203bc63f35` is **not** in this ticket (it conflicts; see ticket 02), so
  the Project-page injection fix is not closed here.

## Done when

Four commits, test hunks explicitly dropped or ported, build and tests green, four screenshots,
ledger rows updated with the test-hunk disposition.

## Answer

All four picks are on `pnp/main` as one commit each, with the upstream author preserved and
`(cherry picked from …)` recorded:

| Upstream | Fork commit | Source change |
|---|---|---|
| `d112a0af29` | `3c18052abf` | `deps_src/admesh/stlinit.cpp` (2+/2−): bounded solid-name scan and bounded MW metadata conversions |
| `d35ea27ea5` | `f8afe3a5aa` | `Format/3mf.cpp`, `Format/bbs_3mf.cpp`: reject a zip entry whose declared uncompressed size exceeds `INT_MAX` before expat allocates |
| `8ffd3e514e` | `0d87b1d20f` | `Format/OBJ.cpp`, `Format/objparser.cpp/.hpp`, `Format/DRC.cpp`: UV index fallback, `OBJ_TEXCOORD_LENGTH`, POSITION/face-index validation |
| `41eeaf3883` | `03463e1592` | `Utils.hpp`, `utils.cpp`, `DownloaderFileGet.cpp`, `Plater.cpp`: `sanitize_file_basename`, shared `find_unused_filename`, marker reservation |

### Test hunks

All three test-bearing picks are ported; only CMake registration was dropped.

- `d112a0af29` adds no tests.
- `d35ea27ea5`: upstream appends a case to `tests/libslic3r/test_3mf.cpp`; appended here with the
  same helper, plus `test_utils.hpp` and `miniz_extension.hpp` includes. The `CMakeLists.txt`
  hunk is dropped because `xmake.lua` globs `tests/libslic3r/*.cpp`.
- `8ffd3e514e`: `test_drc.cpp` and `test_obj.cpp` are new files and are taken whole; the
  `CMakeLists.txt` hunk is dropped for the same reason.
- `41eeaf3883`: the new `test_utils.cpp` cases are appended by hand. The hunk needs
  `ScopedTemporaryDir`, which this fork's shared `tests/test_utils.hpp` did not have; the helper
  is added there (17 lines, upstream's implementation) and the file now includes `<fstream>`.
- The ledger's "Test-hunk reality" note records the change from the triage measurement.

### Verification

Build (Windows x64, release):

```
xmake build libslic3r
xmake build -j6 OrcaSlicer
xmake build -j6 OrcaSlicer_app_gui
xmake build -j6 libslic3r_tests
```

All linked; `OrcaSlicer.dll`, `orca-slicer.exe`, `libslic3r_tests.exe` rebuilt at the ticket HEAD.
The `xmake` in this shell rejects trailing `-j` on the task form; options belong after the task
name, as above.

Tests:

```
xmake test
→ 100% tests passed, 0 failed out of 7, 50.047s
```

Targeted cases in the rebuilt binary:

| Filter | Result |
|---|---|
| `"3MF XML entries declaring more than an int can hold fail to load"` | 14 assertions, 1 case, passed |
| `[DRC]` | 7 assertions, 3 cases, passed (logs `no POSITION attribute`, `invalid vertex index`) |
| `[OBJ]` | 40 assertions, 5 cases, passed |
| `[Utils]` | 53 assertions, 14 cases, passed |

A direct `[3mf]` filter also executes the **hidden** `[3mf][.]` "2D convex hull of sinking object"
case, which fails 2 of its own inverted `CHECK`s; `xmake test` excludes hidden tests, and the
failure is pre-existing and unrelated to this ticket. The new 3MF case passes when run by name.

GUI walkthrough (app built from the ticket HEAD, screenshots in [assets/01](../assets/01/)):

- **Long solid name** — [long-solid-name.png](../assets/01/long-solid-name.png). The app stays up
  and refuses the file; log says `Something is syntactically very wrong with this ASCII STL!`.
  No crash. Refusal is log-only, not a dialog.
- **Oversized 3MF entry** — [oversized-3mf.png](../assets/01/oversized-3mf.png). The app stays up;
  log says `Found invalid size`; no OOM, no crash. Refusal is log-only here too.
- **Hostile OBJ** — [hostile-obj-dialog.png](../assets/01/hostile-obj-dialog.png). The
  UV-out-of-range/absent-vt tetrahedron parses and raises the normal OBJ color-import dialog; the
  app stays up and no crash follows.
- **Truncated DRC** — [truncated-drc-dialog.png](../assets/01/truncated-drc-dialog.png). A
  200-byte prefix of `resources/handy_models/3DBenchy.drc` produces the `Loading of a model file
  failed.` dialog; no crash. The precise no-POSITION and invalid-face-index paths are covered by
  `test_drc.cpp`; producing those fixtures needs a Draco encoder, so the GUI case uses truncation.
- **Server-supplied download name** — [download-sanitized.png](../assets/01/download-sanitized.png).
  A local HTTP server sent `Content-Disposition: attachment; filename="..\..\upstream-sync-01-escape.3mf"`.
  The file landed at `C:\Users\agpen\Downloads\upstream-sync-01-escape.3mf` and the window title
  shows the sanitized name. No file was written outside the download directory.
- **Normal download control** — [download-normal.png](../assets/01/download-normal.png). A URL with
  no `Content-Disposition` produced `C:\Users\agpen\Downloads\normal.3mf` unchanged; the sanitizer
  does not rename ordinary downloads.

### Gaps and notes

- The STL and 3MF refusal paths are log-only in this fork; the ticket's "error dialog" did not
  appear. That is existing `Plater::load_files` behaviour, not introduced by these picks.
- `203bc63f35`'s Project-page/web-asset half remains ticket 02's; this ticket closes only the four
  clean picks.
- No app version bump, no new dependency, no submodule move. Behaviour changes are limited to
  malformed inputs and server-supplied download names.
