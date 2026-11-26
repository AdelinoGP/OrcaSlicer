---
title: Security hardening — clean picks
status: open
type: implementation
assignee: ""
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
