# PNP Fork Implementation Plan — handoff design doc

**Destination artifact of the wayfinder map** [Fork OrcaSlicer frontend onto the PNP backend](../map.md).
Audience: future driving sessions that execute this plan via subagents. Ticket 010 resolution, 2026-07-17.

## What this converts

This OrcaSlicer checkout, in place on branch `pnp/main` (fork point tagged `pnp-fork-base`, per
[Upstream sync strategy](../tickets/002-upstream-sync-strategy.md)), into a PNP-backed frontend:

- `pnp_cli slice` shell-out replaces `BackgroundSlicingProcess` slicing (a new `PnpSlicingProcess`
  preserving BSP's public surface — [ticket 003](../tickets/003-slicing-seam-replacement-design.md)).
- Native slicing removed cleanly: FFF `Print::process` pipeline, all SLA, Orca's headless `--slice`
  CLI, calibration generators ([ticket 008](../tickets/008-native-slicing-rip-out-scope.md)).
- PNP config adopted via a static translation table with log-only warnings
  ([005](../tickets/005-preset-to-pnp-config-translation.md), [013](../tickets/013-config-warning-ux.md)).
- PNP G-code rendered in Orca's preview ([007](../tickets/007-preview-ingestion-of-pnp-gcode.md)).
- v1 scope: slice + preview, single plate per invocation, Windows-first, **zero gap-communication
  UI** ([012](../tickets/012-v1-ui-surface-restrictions.md), [013](../tickets/013-config-warning-ux.md)).

Every design decision is already resolved; each ticket below links the wayfinder tickets that hold
its detail. **Do not re-litigate closed decisions** — in particular: no defensive UI for pnp gaps,
no capability gate, warnings are a dev log only, shell-out (no FFI), SLA fully removed, kill-only
cancel in v1.

## Execution model

- Tickets live in [`fork-tickets/`](fork-tickets/), one file each, frontmatter
  `status / batch / blocked-by / files` authoritative. Sized for **one fresh subagent session
  (~100K context)** with explicit file lists and done-criteria.
- A **driving session** = claim one batch → dispatch its tickets to subagents **in parallel**
  (intra-batch tickets touch disjoint files) → integrate → run batch verification → commit doc
  status updates. **Note the repo owner's global rule: no isolated/worktree agents** — subagents
  work in the shared tree; intra-batch file disjointness is what makes that safe.
- **Commit discipline: one commit per ticket** (after its compile check passes), on `pnp/main`.
  Optional annotated tag per verified batch (`pnp-batch-B<N>`).
- Batches are strictly ordered B1→B9. Milestone boundaries (M1/M2/M3) are the smoke + gating
  checkpoints.

## Verification tiers

1. **Per ticket (subagent):** build the affected CMake target(s) — usually
   `cmake --build build --config RelWithDebInfo --target libslic3r_gui` or `libslic3r` — plus any
   ctest suite the ticket names. A ticket is not done until its target compiles clean.
2. **Per batch (driving session):** full Windows RelWithDebInfo build of `ALL_BUILD`, then the
   **smoke ritual**: launch orca-slicer.exe, load a canonical model (a bundled handy-model STL),
   slice via pnp_cli, confirm (a) progress bar advances, (b) preview renders toolpaths with roles,
   (c) `<plate tmp>.gcode` exists, (d) `<data_dir>/pnp-config-warnings.jsonl` gained records.
   GUI steps a script can't drive are eyeballed by the human.
3. **Per milestone:** batch verification **plus** the milestone's pnp gating check (below) and
   `ctest --output-on-failure` for the suites still in the tree.

Batches before B2 land can skip smoke steps that require the seam (nothing slices yet); B1's batch
check is full build + app launch only.

## Driving-session build & verify gotchas (Windows / MSBuild / GUI exe)

Environment-specific lessons proven across batches on this Windows checkout. Follow them — most
cost a wasted 10-minute build to rediscover.

- **Run `ALL_BUILD` in the FOREGROUND, not a background job.** Background bash commands are killed
  by a harness lifetime cap (~5–10 min) before a full build finishes (surfaces as status `killed`
  with `MSB4166` in the log). Use a foreground `cmake --build` with the max `600000`ms Bash
  timeout; builds are incremental, so just re-invoke if one pass hits the timeout. The full command:
  `cmake --build . --config RelWithDebInfo --target ALL_BUILD -- -m -nodeReuse:false` from
  `build-dbginfo`.
- **Never `taskkill` a live MSBuild/cmake.** Force-killing reused MSBuild nodes poisons the next
  build with `MSB4166 "child node exited prematurely"` — infrastructure noise, not a compile error.
  `-nodeReuse:false` prevents it. To stop a build, prefer letting it time out or Ctrl-C the
  foreground job.
- **Create `target/` before redirecting build/test logs.** A first-run `tee`/`>` to
  `target/<log>` fails silently (dir absent) and discards the whole build's output — you lose all
  error visibility. `mkdir -p target` first.
- **Header edits cascade.** Editing widely-included headers (`Model.hpp`, config headers) triggers
  a broad GUI recompile → expect a ~15-min first build, fast incrementals after. Not a hang.
- **CMake auto-reconfigures** on the next build after any `CMakeLists.txt` edit or source
  deletion. Confirm a removed target is actually gone by grepping the regenerated
  `build-dbginfo/OrcaSlicer.slnx` and the relevant `*.vcxproj` for 0 references. Stale `.exe` /
  `*.dir` left on disk are harmless cruft, not live targets. A deleted `.cpp` still referenced by a
  live target hard-errors the build, so a clean `ALL_BUILD` proves the reconfigure dropped it.
- **`orca-slicer.exe` is a Windows GUI-subsystem binary** — its `--help`/CLI text does **not** reach
  git-bash stdout. Verify CLI behavior via **exit codes** and by reading `--info` **output files**,
  never by grepping captured `--help`. `--info <stl>` prints geometry to a redirected file and
  exits 0; a gutted/removed CLI action should exit cleanly (bash reports the `flush_and_exit` error
  code, e.g. `127`) with its message in the output file — distinguish that from a real crash
  (`0xC000xxxx`, or bash `134`/`139`).
- **The runnable app is `build-dbginfo/src/RelWithDebInfo/orca-slicer.exe`** (freshly linked each
  build), with `pnp_cli.exe` + `modules/` staged beside it. The `build-dbginfo/OrcaSlicer/`
  `orca-slicer.exe` is a stale leftover — do not launch it for smoke.
- **Test-suite fallout from rip-outs.** With `BUILD_TESTS=ON`, `ALL_BUILD` includes every
  `tests/*` target, so deleting a class breaks any test that uses it even when that test isn't in
  the active ticket's `files`. When the broken test is owned by no other ticket's frontmatter
  (check the sibling tickets), the driving session deletes the test + its `tests/*/CMakeLists.txt`
  entry to preserve the always-compiling invariant (precedent: F11 removed
  `tests/libslic3r/test_calib.cpp`; F12 will need `tests/sla_print/`). If it *is* owned elsewhere,
  stop and flag rather than improvising.
- **Stale pnp staging.** A WASM `TypedInstantiation` error on a GUI slice means the staged
  `pnp_cli.exe`+`modules/` went stale against the parallel `pinch_n_print` workstream — restage
  **both** from `F:\slicerProject\pinch_n_print\target\dist\` (run `cargo xtask dist` there first if
  that repo's git log moved) before diagnosing anything else. The single `"pnp backend
  unavailable"` line at startup is a harmless pre-probe ordering artifact — don't chase it.

## Cross-repo pnp gating

pnp handoff items ([full list](handoff-pnp-gap-implementation.md)) are **scheduled prerequisites
implemented in parallel in `F:\slicerProject\pinch_n_print`** — the fork builds no defenses against
them. Ritual: **before starting a milestone's first batch, the driving session checks the gating
items for that milestone have landed in pinch_n_print** (probe the live `pnp_cli`/docs; if an item
hasn't landed, flag the human — do not build fork-side workarounds). Fork tickets never block on
pnp mid-batch.

| pnp item | Gates | Check |
|---|---|---|
| 10 — CONFIG_BLOCK viewer keys (**mandatory**) | M1 close / preview-accuracy sign-off | pnp G-code CONFIG_BLOCK carries `printer_model`, `filament_density`, `machine_max_*` |
| 2 — `slice_stats` event (amended fields) | B4 (weight display) | JSONL stream emits `slice_stats` with weight + per-extruder volumes |
| 12 — `layer_count` in progress stream | improves B2/F06 percent (not blocking) | `phase_start(per_layer)` carries `layer_count` |
| 6 — non-uniform scale (**hard blocker**) | any user-visible release | pnp loader accepts non-uniform scale |
| 16 — `aligned` seam mode | print-quality sign-off (default path) | `seam_mode` accepts `aligned` |
| 14 — thumbnail wire format + repeatable CLI arg | B8 (F14) | `pnp_cli slice` accepts repeated `(path, WxH, format)` thumbnail args |
| 15 — M73 (downstream of item 1) | on-printer progress sign-off | pnp G-code contains `M73 P… R…` lines |
| 1, 8, 9, 11, 13 | quality follow-ups, gate nothing fork-side | — |

## Milestones and batches

### M1 — Seam (native slicing untouched; app builds and slices via PNP at M1 close)

| Batch | Ticket | Title |
|---|---|---|
| B1 | [F01](fork-tickets/F01-pnp-config-translator.md) | `PnpConfigTranslator` (preset → flat PNP JSON + warning vector) |
| B1 | [F02](fork-tickets/F02-pnp-cli-discovery-handshake.md) | pnp_cli discovery, Preferences override, schema handshake |
| B1 | [F03](fork-tickets/F03-config-warnings-sink.md) | Config-warnings sink (Tier-D default filter + jsonl writer) |
| B2 | [F04](fork-tickets/F04-pnp-slicing-process.md) | `PnpSlicingProcess` core (worker thread, boost::process, temp dirs, reuse) |
| B2 | [F05](fork-tickets/F05-per-plate-3mf-export.md) | Per-plate 3MF export + raw per-object sidecar |
| B2 | [F06](fork-tickets/F06-jsonl-progress.md) | JSONL progress parser + phase-weighted percent |
| B3 | [F07](fork-tickets/F07-preview-ingestion.md) | Preview ingestion of PNP G-code |
| B3 | [F08](fork-tickets/F08-error-cancel-ux.md) | Error, degraded-slice, and cancel UX (Job Object orphan guard) |
| B3 | [F09](fork-tickets/F09-plater-wiring.md) | Plater wiring: BSP → PnpSlicingProcess swap, slice-all loop |
| B4 | [F10](fork-tickets/F10-stats-weight-cost.md) | Stats: weight from `slice_stats`, fork-side cost, legend zero-guards |

**M1 gating check before close:** pnp items 10 and 2.

### M2 — Rip-out (ticket 008 scope; always-compiling tree)

| Batch | Ticket | Title |
|---|---|---|
| B5 | [F11](fork-tickets/F11-cli-and-calibration-ripout.md) | Headless `--slice` CLI deletion + calibration mock |
| B6 | [F12](fork-tickets/F12-sla-full-cut.md) | SLA full cut incl. 3MF SLA-project detection |
| B7 | [F13](fork-tickets/F13-fff-pipeline-removal.md) | FFF pipeline removal, slicing-test deletion, support-preview drop |

B5–B7 are **sequential single-ticket batches** — each is one large coherent deletion whose file
set overlaps the others' (Plater.cpp, Print.cpp, CMakeLists); parallel subagents would collide.
A driving session may still split F12/F13's internal steps across sequential subagents.

### M3 — Polish

| Batch | Ticket | Title |
|---|---|---|
| B8 | [F14](fork-tickets/F14-thumbnails.md) | Thumbnails: UI-thread render before spawn, multi-size PNG to pnp_cli |
| B9 | [F15](fork-tickets/F15-distribution-and-acceptance.md) | Distribution layout + final acceptance pass |

**Release gating check (before any user-visible build):** pnp item 6 (hard), item 16
(print-quality), item 14 (if thumbnails are claimed as shipped).

## Status

Tick when a ticket's commit lands; batch line when smoke-verified.

- [x] B1: F01 · F02 · F03 — batch verified (ALL_BUILD RelWithDebInfo clean, app launch OK, 2026-07-17)
- [x] B2: F04 · F05 · F06 — batch verified (ALL_BUILD RelWithDebInfo clean, app launch OK, pnp_cli slice of regression_wedge.stl -> gcode + instrumented JSONL stream replayed through PnpProgressParser monotonic 0->99; GUI progress-bar/warnings-jsonl smoke steps need the F09 Plater swap and roll into B3's smoke, 2026-07-17. NOTE: pnp_cli emits JSONL events only with --instrument-stderr — F04 passes it)
- [x] B3: F07 · F08 · F09 — batch verified (ALL_BUILD RelWithDebInfo clean, app launch OK 25s+, pnp_cli staged + `module config-schema` handshake OK, no orphan pnp_cli, 2026-07-17. GUI slice human-confirmed 2026-07-17 after post-batch fixes: F01 translator unit fixes + schema guard + tests (32bf48aefa), F07 psGCodeExport preview gate (d570fb782f); cancel kill-tolerance still unexercised. NOTE: `restart_background_process` logs "pnp backend unavailable" once during startup — it runs before GUI_App::post_init's probe(); harmless ordering artifact, gate opens after probe)
- [x] B4: F10 — **M1 smoke + gating (items 10, 2)** — batch verified (ALL_BUILD RelWithDebInfo clean incl. test targets, ctest tests/pnp 2/2 pass, app launch 30s+ OK, restaged pnp_cli+modules from fresh `cargo xtask dist`, no orphan pnp_cli after kill, 2026-07-17). Gating: item 10 **landed** (probed live pnp_cli: CONFIG_BLOCK carries printer_model, filament_density, machine_max_* passthrough — required a dist rebuild; pnp commit 05eee46e). Item 2 `slice_stats` **landed 2026-07-18** (pnp packet 169 implemented; probed live pnp_cli: schema 1.2.0 `slice_stats` emits gcode_prediction_seconds, gcode_weight_grams, gcode_filament_length_mm, layer_count, first_layer_height_mm, extruded_volume_mm3 map, toolchange_count — exact field names F10's apply_slice_stats parses; dist restaged). **M1 gating: both items landed.** HUMAN EYEBALL PENDING: GUI slice smoke (progress/preview/tmp gcode/warnings-jsonl growth; legend should now SHOW weight+cost from slice_stats), and cancel kill-tolerance (still unexercised since B3 — GUI-only).
- [x] B5: F11 — **M2 opens** — batch verified (ALL_BUILD RelWithDebInfo clean incl. test targets, ~3871 lines deleted across the 10 granted files; ctest tests/pnp 2/2 pass; app launch 26s+ homepage-loaded OK, no orphan pnp_cli after kill, 2026-07-18). CLI checks: `orca-slicer.exe --info <stl>` works (rc 0, real geometry); `--slice` reaches the gutted branch and exits cleanly with "native --slice is not supported in this build; use pnp_cli to slice" (flush_and_exit CLI_UNSUPPORTED_OPERATION, no crash); `--help` slice-free (help stdout not capturable for a Windows GUI-subsystem exe — verified via the gutted action instead). Calib impl deleted, UI kept as mock: all `Plater::calib_*` + gutted `CalibUtils` entry points post a not-implemented notification (verified compiled/stubbed; clicking is GUI-only). **Scope-note (driving session, beyond F11's frontmatter `files`):** removed the `OrcaSlicer_profile_validator` target from `src/CMakeLists.txt` (its source is deleted; not one of the two reserved CMakeLists) and deleted `tests/libslic3r/test_calib.cpp` + its line in `tests/libslic3r/CMakeLists.txt` — that test subclasses the deleted `CalibPressureAdvancePattern`, is a pure calib-generator test owned by no other ticket (checked F12/F13 frontmatter), and would break the always-compiling invariant if deferred. CMake reconfigured and dropped both targets (0 refs in regenerated `.slnx`/vcxproj). Agent middle-grounds kept in scope (no Print.hpp/GCode.hpp edit): `Print::set_calib_params` gutted to no-op with member default-constructed → `calib_mode()` always `Calib_None`; `m_calib_config` left unwritten (harmless no-op apply). HUMAN EYEBALL PENDING (GUI-only, script can't drive): (a) GUI slice progress-bar/preview/tmp-gcode/warnings-jsonl growth (baseline 127058 bytes), (b) every calibration menu entry opens its dialog and clicking OK posts the not-implemented notification without crashing; plus carried from B4: legend weight+cost and cancel kill-tolerance.
- [x] B6: F12 — batch verified (ALL_BUILD RelWithDebInfo clean, 0 errors; orca-slicer.exe + all test exes linked; CMake reconfigure dropped every removed SLA target — only the 6 kept SLA files (IndexedMesh, Rotfinder, RasterBase, AGGRaster, Concurrency) remain in libslic3r.vcxproj, `sla_print` gone from CTest; ctest tests/libslic3r 156/156 + tests/pnp 2/2; commit 4e867d61b6, 2026-07-19). Deleted ~49 files (SLAPrint*/SLAPrintSteps*/SL1*, SLA support-gen half of SLA/, dead GUI gizmos/jobs, tests/sla_print/), ~48 edited (init_sla_params + SLA config classes/enums, sla_shift_z ×9, ModelObject SLA fields, PresetBundle sla_prints/materials, Plater/GLCanvas3D SLA members, ~116 ptSLA branches). Kept `ptSLA`/`TYPE_SLA_*` enumerators defined-but-unused; RasterBase/AGGRaster/Concurrency kept beyond IndexedMesh/Rotfinder (font-preview image jobs need them). **Driving-session fixes on top of the subagent deletion (would have failed the ticket otherwise):** (a) Rotfinder.cpp lost its Model.hpp include → added; (b) deleting `sla_print()` in GLCanvas3D also ate the next function header `WipeTowerInfo::apply_wipe_tower` → restored; (c) dead `write_thumbnail` (only caller was removed process_sla) → deleted; (d) **the 3MF SLA pre-pass used `add_error` (log-only — the extract loop never checks it), so the load would have proceeded with an empty config and crashed instead of refusing; changed to `throw version_error(...)`** — the importer's idiomatic reject-with-message signal. **Headless smoke:** orca-slicer.exe --info STL exit 0 (real geometry — the edited model path works); crafted SLA .3mf fixture (FFF project with printer_technology=SLA) is refused cleanly via CLI (exit 127 flush_and_exit + "SLA projects are not supported by this build.", NOT segfault) — same throw drives the GUI show_error dialog; FFF .3mf round-trips (exit 0). Fixture at scratchpad/f12-sla-project-FIXTURE.3mf. **HUMAN EYEBALL PENDING (GUI-only):** opening the SLA .3mf shows the refusal dialog; an FFF .3mf project opens; PNP GUI slice progress/preview/tmp-gcode/warnings-jsonl growth; plus carried from B4/B5: legend weight+cost, cancel kill-tolerance, calibration menu mock dialogs.
- [x] B7: F13 — **M2 CLOSE** — batch verified (ALL_BUILD RelWithDebInfo clean, 0 errors; orca-slicer.exe + all test exes relinked; regenerated vcxproj/CTest dropped every removed source; `ctest` remaining suites 100%: libslic3r 134/134, fff_print 17/17, pnp 2/2, slic3rutils 5/5, libnest2d 21/21, filament_group 3/3; commit on pnp/main, 2026-07-19). Native FFF `Print::process` pipeline removed: Fill/Support/Arachne/PerimeterGenerator/Brim/PrintObjectSlice/MultiMaterialSegmentation.cpp + the GCode.cpp generator and its exclusive helpers deleted; Print::process stubbed to a no-op (PrintBase pure-virtual); step methods cut from Print.cpp/PrintObject.cpp/Layer.cpp/LayerRegion.cpp, structural accessors kept. **Driving-session scope decisions (ticket's "bulk-droppable" premise was wrong at the header level — see F13 completion note):** ToolOrdering/WipeTower/WipeTower2 RETAINED (types + static plate-layout/preview geometry helpers consumed by LibVGCode/GLCanvas3D/PartPlate/Plater); ConflictChecker.hpp + MultiMaterialSegmentation.hpp + 2 Arachne util index headers kept as type-only headers (their .cpp deleted); Feature/FuzzySkin + Feature/Interlocking + GCode/PrintExtents/TimelapsePosPicker/PchipInterpolatorHelper deleted as generator-only fallout; BSP deleted with its two wx event classes relocated to new SlicingProcessEvents.{hpp,cpp}; slicing tests deleted (fff_print down to geometry/mesh/GCodeProcessor cases; test_arachne_walls + test_toolordering_nozzle_group removed). Support gizmo now paint-only (generate_support_preview + worker thread removed; pnp handoff item 13 restores the overlay). **M2 gating:** no pnp gating items for M2 (cleared when B5 opened it) — no cross-repo probe required. Headless smoke: `--info` on STL + FFF `.3mf` exit 0 with real geometry, no orphan pnp_cli. **After B7, M2 is complete and ticket 008 (native-slicing rip-out) is fully executed.** **GUI LAUNCH NOW VERIFIED** (human ran orca-slicer.exe): homepage loads (Document loaded / Navigation complete), no fatal in debug log, process stable — after fixing two B6/F12 SLA-removal-fallout startup crashes the human hit (follow-up commit 801e716641): (1) ACCESS_VIOLATION in `PresetCollection::get_selected_idx` via `DiffPresetDialog::create_presets_sizer` — combo loop still iterated `TYPE_SLA_*` whose `get_preset_collection` now returns nullptr; (2) `UnknownOptionException: material_colour` at OnInit — `Plater::priv::config = new_from_defaults_keys({... "material_colour" ...})` requested an SLA material option whose config def F12 dropped. Both were latent since B6 (whose GUI launch was never human-verified). HUMAN EYEBALL STILL PENDING (deeper GUI interactions): PNP GUI slice progress/preview/tmp-gcode/warnings-jsonl growth; variable-layer-height editor opens/edits; support painting paints without crash (no overlay); plus carried B4/B5/B6 set (legend weight+cost, cancel kill-tolerance, calibration mock dialogs, SLA .3mf refusal + FFF .3mf project opens).
- [x] B8: F14 — **M3 opens** — batch verified (ALL_BUILD RelWithDebInfo clean, 0 errors, OrcaSlicer.dll relinked; `ctest` 184/184 pass incl. pnp schema-guard #184 + translator-regression #183; app launch 22s+ homepage-loaded OK, no new crash log, clean kill, 2026-07-19). **Gating item 14 LANDED — but under a corrected contract (`D-173-THUMBNAIL-SINGLE-PNG`), NOT the repeatable-CLI-arg shape this plan's gating row described.** pnp packet 173 (`pinch_n_print` `b348b179`) deliberately rejected per-size CLI args (design.md "rejected by user decision"); the live contract, probed + proven end-to-end this session (real `pnp_cli slice` with `thumbnails="48x48/PNG,300x300/PNG"` + one 1569×1140 PNG → **two** parseable `; thumbnail begin 48x48 3764` / `; thumbnail begin 300x300 62224` Orca-format blocks, exit 0): the fork renders **one high-res top-down PNG** → existing single `--thumbnail`; the size/format list rides in the `thumbnails` **config key** (Orca coString) in the `--config` JSON; pnp resizes+transcodes+frames. `thumbnails`/`thumbnail_path` are passthrough raw keys **not in pnp's config-schema** (probed: 61 KB schema, zero `thumbnail*` keys) and do not fatal — so the fork injects `thumbnails` **after** `apply_schema_guard`. Correction recorded at top of F14 ticket + in F14 frontmatter. **F14 fork edits** (single commit): UI-thread plate-PNG render in `PnpSlicingProcess::start()` before worker spawn (reuse `Plater::update_all_plate_thumbnails` + `PartPlate::thumbnail_data` → `compress_thumbnail(PNG)` → per-slice temp `thumbnail.png`); `thumbnails`-key injection post-guard; `--thumbnail` appended to `run_pnp_cli` argv; dead BSP `set_thumbnail_cb`/`m_thumbnail_cb` + its lone live Plater caller removed. Restaged fresh pnp_cli+modules from `cargo xtask dist`. **HUMAN EYEBALL: thumbnail smoke CONFIRMED WORKING 2026-07-19** (human GUI-sliced with a `48x48/PNG,300x300/PNG` profile — emitted `<plate>.gcode` carries the two `; thumbnail begin WxH len` blocks / plate icon renders). Still carried forward is the B4–B7 backlog (PNP slice progress/preview/tmp-gcode/warnings-jsonl growth, legend weight+cost, cancel kill-tolerance, calibration mock dialogs, SLA .3mf refusal + FFF .3mf project opens, variable-layer-height editor, support painting).
- [x] B9: F15 — **M3 CLOSE / final acceptance; release gate** — batch verified (ALL_BUILD RelWithDebInfo clean, 0 errors, `OrcaSlicer.dll` relinked; `ctest` **186/186** — 184 at B8, +1 from the post-B8 `test_3mf` scenario, +1 from this batch's regression test; 2026-07-24). **Release gating: every item landed.** Restaged `pnp_cli.exe`+`modules/` from a fresh `cargo xtask dist` (the pnp repo had moved and `target/dist/` was gone; staging was genuinely stale — **18 modules → 21**). Item **6 (hard blocker) LANDED**: a 3MF build transform of `diag(1.0, 2.5, 0.6)` on a 20 mm cube slices exit 0 with the scale *baked* (`layer_count=60`; emitted Y span exactly 30.00 mm wider than X), and `ModelLoadError` no longer has a non-uniform-scale variant. Item **16 LANDED** (`seam_mode = aligned` resolves into the CONFIG_BLOCK, `degraded=false`; also the `seam-placer.toml` default). Items **14 / 10 / 2 re-verified** (two thumbnail blocks from one PNG; `printer_model`+`filament_density`+all `machine_max_*`; `slice_stats` weight 5.32 g + per-extruder volumes). Non-blocking 12 and 15 also landed (`layer_count` in `phase_start`, 122 `M73` lines). Progress-event schema moved **1.2.0 → 1.3.0** — same major, every field F10 parses unchanged; config schema still `1.0.0` = `SUPPORTED_CONFIG_SCHEMA_MAJOR`. **F15 distribution wiring:** `PNP_BUNDLE_CLI` / `PNP_DIST_DIR` / `PNP_DIST_AVAILABLE` in the root `CMakeLists.txt`, a `POST_BUILD` stage into `$<TARGET_FILE_DIR:OrcaSlicer>` in `src/CMakeLists.txt`, and `install(PROGRAMS/DIRECTORY)` rules that flow into CPack — so the NSIS installer and the portable zip (which *is* the install tree) carry the backend. A missing dist tree warns and skips; it never fails configure. README gained a `# PNP backend (pnp_cli)` section. **Clean-machine acceptance from the packaged install tree** (not the dev tree): flat layout with `pnp_cli.exe` + 21 `modules/` beside `orca-slicer.exe`, `target/b9-probe/acceptance-headless.sh` **14/14 pass** (no `0.00 g`, FFF `.3mf` loads, SLA `.3mf` refused not crashed, no orphan `pnp_cli`), GUI launches from the packaged tree and opens a real FFF `.3mf` with no crash log; pnp discovery proven by a differential test (hiding `pnp_cli.exe` produces `PnpBackend: probe failed: pnp_cli not found at <packaged dir>`, restoring it silences it — so the probe **succeeds** and the empty-reason startup line is only the known pre-probe artifact). **Two crashes fixed by the driving session:** (a) `orca-slicer.exe` ACCESS_VIOLATION on **every** Bambu/Orca-authored `.3mf` incl. both bundled `resources/handy_models` samples — `CLI::run` read `config.opt_float("printable_height")` unguarded, and `opt_float` is `option<ConfigOptionFloat>(key)->value`, so a missing key is a null deref (fault address `0x8`, the `value` offset); a 3mf is classified `is_bbl_3mf` from its generator tag alone, so its project config can be empty (`OrcaBadge.3mf`'s is literally `{}`). Latent **upstream** bug — the three `extruder_clearance_*` reads on the next lines already had the guard this one lacked; GUI unaffected (verified before fixing — it does not go through `CLI::run`). Fixed in `src/OrcaSlicer.cpp` + regression test `tests/libslic3r/test_3mf.cpp` SCENARIO "A project .3mf may carry no printable_height". (b) The RelWithDebInfo **install tree shipped zero third-party DLLs** (`OrcaSlicer.dll was not loaded, error=126`): `orcaslicer_copy_dlls(... "RelWithDebInfo" "" output_dlls_Release)` while the install rule reads `output_dlls_${build_type}` = `output_dlls_RelWithDebInfo`, so `install(FILES ...)` expanded to nothing; also upstream, dodged by Release-configured CI. **KNOWN GAP (needs a decision, out of F15's granted files):** `.github/workflows/build_orca.yml` checks the repo out standalone, so `PNP_DIST_DIR`'s default will not exist in CI and the produced installer would silently contain no `pnp_cli.exe`; the workflow needs to produce/fetch the dist tree and pass `-DPNP_DIST_DIR=`. **HUMAN EYEBALL PENDING (GUI-only) — the full carried backlog:** PNP GUI slice progress/preview/tmp-gcode/warnings-jsonl growth + legend weight+cost, cancel kill-tolerance, calibration mock dialogs, SLA `.3mf` refusal dialog, variable-layer-height editor, support painting, preset-diff dialog, Preferences "PNP CLI directory" override round-trip. **Release readiness awaits the human's sign-off in F15's `## Acceptance log`.** **B9 follow-up commit** (tag stays on the gated commit): implemented the **per-slice progress-schema major gate from ticket 009, which had never been built** — `PnpProgressParser` parsed `schema_version`, stored it, exposed it, and nothing read it; the header comment even pointed the caller at `SUPPORTED_CONFIG_SCHEMA_MAJOR`, and that conflation *is* the bug, because pnp's config-schema (`1.0.0`) and progress-stream (`1.3.0`) semver lines are independent and the startup probe gates only the former. A progress-major bump would have sailed through the handshake and then driven the progress bar and legend from unreadable events — this session watched 1.2.0 → 1.3.0 pass unnoticed. `SUPPORTED_PROGRESS_SCHEMA_MAJOR` now sits beside its sibling on `PnpBackend`; `PnpProgressParser::check_schema_version` reports mismatches through the parser's existing fatal channel, which `PnpSlicingProcess` already turns into a failed slice via `throw SlicingError` (no new plumbing). Same-major accepted; different major or present-but-unparseable is fatal; **absent is accepted and logged**, since refusing it would turn one dropped upstream field into a total slicing outage. Also guarded the two remaining unguarded `opt_float("printable_height")` reads (`--uptodate`, `--downward-check` branches), removing the null-deref class from `CLI::run`. New **`pnp_runtime_tests`** target gives the pnp wire contract its **first committed coverage** (14 cases / 98 assertions): `PnpProgressParser` percent model, monotonicity, stream `layer_count` precedence, `slice_stats` verbatim storage, fatal-vs-degraded, pipe-chunk reassembly, all five schema-gate cases, and forward-compat (unknown events/fields ignored, garbage skipped not fatal — the tolerance that let 1.3.0 land); plus `log_pnp_config_warnings` Tier-D filtering, record shape, and append-not-truncate. Kept a separate target because the warnings cases redirect the global `data_dir()`. Gate: ALL_BUILD clean, **ctest 200/200**. By explicit decision the install target and packaged sweep were **not** re-run — the accept path is verified by unit test against a real captured line (and 130/130 lines of a packaged-run capture carry `schema_version: "1.3.0"`), not by a fresh live packaged slice. **The follow-up does not shrink the human-eyeball backlog**, which is human-only by construction; it narrows item 1's logic only.
