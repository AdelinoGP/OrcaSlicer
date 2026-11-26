---
title: Security hardening — hand-ported
status: closed
type: implementation
assignee: opencode-agent
blocked-by: []
---

# Security hardening — hand-ported

## Question

The four hardening commits that leave conflict markers at this base are the ones with the widest
blast radius: 3MF component/plate parsing, the `TriangleSelector` paint path, the Project page's
metadata rendering, and the LAN-printer JSON paths. Adopt them by hand-porting, as the standing
rule requires, and record exactly what the port changed.

Conflict is not a reason to skip a security fix. It is a reason to write down the diff.

## Picks

| SHA | Conflict site (measured) | Port shape |
|---|---|---|
| `a124b7d2df` | `Format/bbs_3mf.cpp` | Add the cyclic-component rejection to the fork's loader. Small, but it is the OOM vector: a crafted component graph otherwise recurses without bound |
| `490d134507` | **`src/libslic3r/TriangleSelector.cpp`**, plus `test_3mf.cpp`. `bbs_3mf.cpp` and `Model.cpp` apply cleanly | The conflict is inside the selector itself, not at "moved call sites" — port the validation into the fork's selector and keep its call sites. Test half dropped |
| `203bc63f35` | `Utils.hpp`, `utils.cpp`; `GUI/Project.cpp` and the web asset apply cleanly | Port the escaping helpers, then the Project-page use. Verify `resources/web/model/model.js` at head matches upstream before taking that file wholesale |
| `e40030cf81` | **`PrintHost.cpp` conflicts**; the other driver files apply, and `UltiMaker.*` do not exist here | **Omit `Utils/UltiMaker.cpp|hpp`** (new upstream; ticket 08 owns them later). 11 source files + 3 test/CMake files upstream; the test/CMake half is dropped |

## Method

1. `git show <sha> -- <paths>` into a patch, apply per file by hand where markers appear.
2. Commit as `cherry-pick(partial): <sha> <subject>`, body naming the omitted hunks and why.
3. `git range-diff` the result against upstream and record the divergence in the ledger.

## Verification

1. Build and `xmake test` green.
2. Hostile-input walkthrough, screenshots:
   - 3MF with a cyclic component graph → refused, no OOM.
   - 3MF with malformed plate IDs and malformed paint data → refused; painting on a normal model
     still works (this is the `TriangleSelector` regression risk).
   - Project whose metadata contains a script payload → rendered as text in the Project page, not
     executed; accessory-open path restricted.
   - A malformed response from each driver (Duet, ESP3D, Flashforge, MKS, PrintHost, UserManager),
     served by a local stub → no crash.

## Risks

- `490d134507` is the largest port (296+/63−) and touches the paint tool's geometry helper. A
  subtle porting error corrupts painting rather than crashing, so the walkthrough must exercise
  painting, not just loading.
- `e40030cf81`'s UltiMaker hunks will conflict again when ticket 08 lands. Take them from upstream
  there, not from this ticket.
- Four hand-ports is four permanent divergences; the ledger must name each conflicting file.

## Done when

Four hand-ported commits, each `range-diff`ed and documented, build and tests green, hostile-input
screenshots attached, ledger evidence filled including the omitted hunks and dropped tests.

## Answer

All four picks are on `pnp/main`, each `range-diff`ed against its upstream commit and recorded in
the ledger. One extra commit fixes a test-harness defect found while verifying the network pick.

| Upstream | Fork commit | What landed |
|---|---|---|
| `a124b7d2df` (+ its precondition `92ab583ecc`) | `77f2c943d6` | `bbs_3mf.cpp`: component expansion returns `bool`, both call sites propagate it, and the work list is capped at 100000 queued references before they are queued |
| `490d134507` | `f831718b08` | `bbs_3mf.cpp` plate id `< 1` rejection; `Model.cpp` drops rejected paint; `TriangleSelector.{hpp,cpp}` bounds-checked nibbles, split-code validation, corrupt-tree unwind, truncated `has_facets`, structural `update_used_states` |
| `203bc63f35` | `7efd6d971d` | `Utils.hpp`/`utils.cpp` containment + safe-type helpers; `GUI.cpp`/`GUI.hpp`/`Auxiliary.cpp`/`Project.cpp` guarded opening; `resources/web/model/model.js` taken whole |
| `e40030cf81` | `69e6f9ef2d` | `PrintHost` shared parser + per-job upload catch; Duet/MKS/Flashforge/ESP3D duplicates removed; `Flashforge::parse_material_slots`; `UserManager` parse inside `try` |
| — (harness) | `14b761f8ca` | `xmake.lua`: `slic3rutils_tests` links Catch2WithMain first, so the suite runs Catch2 instead of minilzo's `testmini.c` self-test |

### Divergences (full detail in the ledger)

- **`a124b7d2df` alone cannot apply.** Its parent contains `92ab583ecc`, which this base never
  had. Both are one commit: the bool return and first depth bound come from `92ab583ecc`, the
  queue budget from `a124b7d2df`. The budget is applied to the base's original
  `pair<Component, Transform3d>` work list.
- **`490d134507` is ported against an older paint encoding.** This base predates `42f708bbb6`, so
  there is no `0b1111` leaf escape; `update_used_states` keeps the `nibble + 3` encoding and the
  state mask stops at the 16 extruders the fork's serializer can emit. All other hunks are
  upstream's. `test_triangle_selector.cpp` does not exist here and is dropped.
- **`203bc63f35`'s helpers are narrower.** `is_path_within_root` / `is_symlink_target_within_root`
  are not exported by this base's utils (a local copy lives in `bbs_3mf.cpp`), so
  `is_path_within_root` is file-static in `utils.cpp` and only `is_absolute_path_within_root` +
  `is_safe_to_open_file_name` join the public header. `model.js` was byte-identical to the
  commit's parent, so it is taken whole.
- **`e40030cf81` has no lifecycle events to pair.** This base predates the plugin
  UploadStarted/UploadFinished events, so the exception catch sits in `perform_job` rather than a
  new `upload_job`, and no lifecycle event is fired. `UltiMaker.*` do not exist here and stay
  ticket 08's.

### Verification

Build at the ticket HEAD (Windows x64, release): `libslic3r`, `OrcaSlicer`,
`libslic3r_tests`, `slic3rutils_tests` all linked.

`xmake test`:

```
100% tests passed, 0 test(s) failed out of 7, spent 53.187s
```

Targeted cases in the rebuilt binaries:

| Filter | Result |
|---|---|
| `[3mf][Regression]` | 149 assertions, 4 cases, passed (logs `invalid plate index` ×2 and `invalid 3mf: cyclic or too many component references` ×3) |
| `is_absolute_path_within_root*,is_safe_to_open_file_name*` | 54 assertions, 3 cases, passed |
| `[PrintHost]` | 24 assertions, 3 cases, passed |
| full `slic3rutils_tests` | 47 assertions, 8 cases, passed |

GUI walkthrough (screenshots in [assets/02](../assets/02/)):

- **Cyclic component graph** — [cyclic-refused.png](../assets/02/cyclic-refused.png). The app
  stays up, shows `Loading of a model file failed.` plus the no-geometry warning, and the log
  records `invalid 3mf: cyclic or too many component references`. No OOM.
- **Plate id 0** — [plate0-refused.png](../assets/02/plate0-refused.png). Refused with
  `invalid plate index` in the log.
- **Malformed paint data** — [paint-malformed-loaded.png](../assets/02/paint-malformed-loaded.png).
  The project loads; the log records
  `dropping malformed paint data of triangle 0`. The ported `[3mf][Regression]` case proves the
  damaged Extruder2 facet is gone, the valid Extruder3 facet survives, and `deserialize` runs
  clean on the result.
- **Project metadata payload** — [project-metadata-escaped.png](../assets/02/project-metadata-escaped.png).
  A `<script>` title and `<img onerror>` author render as literal text; the description keeps
  bold and the https link, drops the script, the `javascript:` href and the plain-http image
  ([project-description-sanitized.png](../assets/02/project-description-sanitized.png)). No alert
  fired.

### Gaps

- **The Project page aborts on any project that carries `Auxiliaries/`** — `0xc0000409` in
  `ucrtbase.dll`, before the page navigates. Reproduced on the pre-change portable package in
  `build/package/OrcaSlicer_2.5.0-pnp_windows_x64/`, so it is **pre-existing**, not this port.
  It blocks the accessory-list half of the walkthrough: `desktop_open_project_attachment` is
  compiled and its helpers are unit-tested, but no click-through was possible. Recorded out of
  scope on the map.
- **Per-driver GUI stubs were not run.** The shared `get_err_code_from_body` and
  `parse_material_slots` are covered by the new unit tests, but a stub-server upload through each
  physical-printer dialog was not exercised. ESP3D's removed copy was unused.
- **Painting was verified at the data level, not by a GUI stroke.** With one filament configured
  the paint tool was not reachable through automation; the `[3mf][Regression]` case asserts a
  valid facet still decodes after a malformed one is dropped.
- **Harness fix is a check edit and is named here**: `slic3rutils_tests` had been running
  minilzo's self-test (exit 0, no assertions), so the suite was green without running. The link
  order commit makes the existing 5 cases and the 3 new ones actually execute.
