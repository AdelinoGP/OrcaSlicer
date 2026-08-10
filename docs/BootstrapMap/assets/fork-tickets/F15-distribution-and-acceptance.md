---
title: Distribution layout + final acceptance pass
status: done
batch: B9
blocked-by: [F14]
files: [CMakeLists.txt, src/CMakeLists.txt]
---

## Goal

Package and sign off, per [ticket 009](../../tickets/009-pnp-cli-distribution-and-discovery.md)
and the map's release gate.

## Decisions (do not re-open)

- Distribution: `pnp_cli.exe` + `modules/` ship **flat next to orca-slicer.exe** — a straight
  drop of `cargo xtask dist` output (`target/dist/` in the pnp repo). Wire this into whatever
  packaging step the fork uses (install target / release zip script); at minimum, document the
  copy step in the repo README section for the fork.
- Final acceptance = the full smoke ritual **plus** the release gating check:
  pnp item 6 (non-uniform scale — hard blocker: scale an object non-uniformly and slice),
  item 16 (`aligned` seam default path), item 14 if thumbnails claimed, item 10/2 re-verified.
- Acceptance also sweeps the v1 loose ends: no `0.00 g` anywhere, no orphan processes after
  crash-close, SLA `.3mf` refusal message, calibration mocks, warnings jsonl growing,
  Preferences override honored.

## Done / verify

- A clean-machine-style test: fresh build dir output + `cargo xtask dist` drop, run the full
  smoke ritual from the packaged layout (not the dev tree).
- Record the acceptance run's results in this file under a `## Acceptance log` heading; the
  human signs off release readiness.

## Distribution wiring as implemented

Three symbols in the root `CMakeLists.txt` carry it:

- `PNP_BUNDLE_CLI` (option, default ON) — master switch.
- `PNP_DIST_DIR` (cache PATH, default `${CMAKE_CURRENT_SOURCE_DIR}/../target/dist`) — the pnp
  workspace's `cargo xtask dist` staging dir. This checkout is nested inside the pnp workspace, so
  the default resolves correctly with no configuration.
- `PNP_DIST_AVAILABLE` — internal flag set only when `${PNP_DIST_DIR}/pnp_cli[.exe]` exists. The
  build-tree staging and the install rules both key off this one flag so they cannot disagree.
  A missing dist tree is a `message(WARNING)` naming the remedy, never a configure error — a
  fork-only checkout with no pnp workspace beside it still builds.

`src/CMakeLists.txt` adds a `POST_BUILD` command on the `OrcaSlicer` target that copies
`pnp_cli[.exe]` (`copy_if_different`) and mirrors `modules/` (`copy_directory`) into
`$<TARGET_FILE_DIR:OrcaSlicer>` — the same directory `orca-slicer.exe` links into, which is what
`PnpBackend::resolve_paths` searches. The root file's install rules (`install(PROGRAMS ...)` +
`install(DIRECTORY "${PNP_DIST_DIR}/modules/" DESTINATION "./modules")`) put the same two entries
flat in the install tree. CPack packages whatever `install()` stages, so both the NSIS installer
and the portable zip — which *is* the install tree, per `.github/workflows/build_orca.yml`'s
7-Zip step — pick this up with no further wiring.

`README.md` gained a `# PNP backend (pnp_cli)` section documenting the layout, the
`cargo xtask dist` step, `-DPNP_DIST_DIR=` / `-DPNP_BUNDLE_CLI=OFF`, and the Preferences override.

## Acceptance log

Run 2026-07-24, driving session, commit-in-progress on `pnp/main`. Fixtures and logs under
`target/b9-probe/` and `target/b9-*.log` (untracked).

### Release gating probe (live pnp_cli, restaged from a fresh `cargo xtask dist`)

The pnp repo had moved since B8 and `target/dist/` was absent; rebuilt it there and restaged both
`pnp_cli.exe` and `modules/`. The staging was genuinely stale — **18 modules before, 21 after**
(`infill-linker`, `traditional-support`, `tree-support`, `wipe-tower` are new).

| Item | Gate | Result |
|---|---|---|
| 6 — non-uniform scale | **HARD BLOCKER** | **LANDED.** A 20 mm cube under a 3MF build transform of `diag(1.0, 2.5, 0.6)` slices with exit 0 and no loader rejection. The scale is *baked*, not merely tolerated: `layer_count=60` (= 0.6 × 20 mm ÷ 0.2 mm) and the emitted G-code's Y span is exactly 30.00 mm wider than its X span (35.60 / 65.60 incl. skirt), which is the 20→50 mm stretch. `ModelLoadError` no longer carries a non-uniform-scale variant. |
| 16 — `aligned` seam | print-quality | **LANDED.** `seam_mode = aligned` resolves and reaches the CONFIG_BLOCK; slice exits 0, `degraded=false`, 0 non-fatal errors. It is also the manifest default in `seam-placer.toml`. |
| 14 — thumbnails | B8 contract | **RE-VERIFIED** under `D-173-THUMBNAIL-SINGLE-PNG`: one 1024×768 PNG via `--thumbnail` + `thumbnails="48x48/PNG,300x300/PNG"` → exactly two `; thumbnail begin 48x48 1080` / `; thumbnail begin 300x300 6648` blocks. |
| 10 — CONFIG_BLOCK viewer keys | M1 | **RE-VERIFIED.** `printer_model`, `filament_density`, and every `machine_max_*` (accel/speed/jerk) round-trip into the CONFIG_BLOCK. |
| 2 — `slice_stats` | B4 | **RE-VERIFIED.** `gcode_weight_grams=5.32`, `extruded_volume_mm3={"0": 4290.75}`, plus prediction/length/layer_count/first_layer_height/toolchange_count. |
| 12 — `layer_count` in progress | non-blocking | Landed: `phase_start(per_layer)` carries `layer_count=60`. |
| 15 — M73 | non-blocking | Landed: 122 `M73` lines in the emitted G-code. |

**No release gating item is outstanding.** Note the progress-event schema moved **1.2.0 → 1.3.0**
since B8; same major, and every `slice_stats` field F10's `apply_slice_stats` parses is unchanged.
`module config-schema` still reports config schema `1.0.0`, matching
`PnpBackend::SUPPORTED_CONFIG_SCHEMA_MAJOR`.

### Build / test gate

- `ALL_BUILD` RelWithDebInfo (BUILD_TESTS=ON): **0 errors**, `OrcaSlicer.dll` relinked, every test
  exe rebuilt. The only CMake warnings are the pre-existing upstream `add_custom_command(TARGET)
  ... DEPENDS` CMP0175 dev-warnings from the gettext targets.
- Configure output confirms the new wiring fired:
  `-- PNP backend: bundling pnp_cli + modules/ from .../OrcaSlicerDocumented/../target/dist`,
  and the build log shows `Staging the PNP backend (pnp_cli + modules) into the build tree`.
- `ctest -C RelWithDebInfo`: **186/186 pass** (was 184 at B8; +1 from the `test_3mf` scenario that
  landed after B8, +1 from this batch's regression test).

### Clean-machine-style acceptance, from the packaged layout

`cmake --build . --target install --config RelWithDebInfo` into a previously non-existent
`build-dbginfo/OrcaSlicer/`. Resulting tree is flat and complete:
`orca-slicer.exe`, `OrcaSlicer.dll`, **`pnp_cli.exe`**, **`modules/` (21 dirs, `.wasm` + `.toml`)**,
`resources/`, `LICENSE.txt`, and 39 runtime DLLs. Everything below ran against that tree, not the
dev tree. `target/b9-probe/acceptance-headless.sh` re-runs it: **14/14 pass.**

- pnp_cli runs from the packaged layout; `module config-schema` (the GUI startup handshake) OK.
- Slice of the non-uniform 3MF from the packaged layout: exit 0, stream ends `slice_complete ok`.
- **No `0.00 g` anywhere** — weight 5.32 g, per-extruder volume map non-empty.
- `--info` on an STL: exit 0, real geometry.
- **FFF `.3mf` project loads** (exit 0, real geometry) — see the crash fix below.
- **SLA `.3mf` refused** with "SLA projects are not supported by this build." and a clean
  `flush_and_exit`, not a crash.
- **No orphan `pnp_cli.exe`** after the sweep, and none after force-killing the GUI.
- GUI, packaged tree: launches to homepage, stays alive 50 s+, opens a real FFF `.3mf` project
  (`resources/handy_models/OrcaBadge.3mf`), no new crash log, no fatal/`Uncaught`/ACCESS_VIOLATION
  in the debug log, clean kill.
- **pnp discovery proven by differential test:** renaming the packaged `pnp_cli.exe` aside makes
  the GUI log `PnpBackend: probe failed: pnp_cli not found at <packaged dir>\pnp_cli.exe` and the
  subsequent `restart_background_process` carries that reason; with it in place neither appears.
  So the probe *succeeds* against the packaged layout, and the single empty-reason
  "pnp backend unavailable" line at startup remains only the known pre-probe ordering artifact.

### Crash found and fixed during acceptance (driving session, beyond F15's frontmatter `files`)

`orca-slicer.exe` died with **ACCESS_VIOLATION (0xC0000005) on every Bambu/Orca-authored `.3mf`**,
including both samples the fork bundles in `resources/handy_models/`. Action-independent — it hit
before `--info`/`--export-settings`/`--uptodate` ran.

- **Trigger:** any `.3mf` whose model-level metadata carries a generator tag
  (`<metadata name="OrcaSlicer">` or `Application` = `OrcaSlicer-`/`BambuStudio-`) *and* whose
  version passes the CLI's version gate. That sets `is_bbl_3mf`, sending `CLI::run` down its
  project branch. Geometry, components, and plate metadata are all irrelevant — bisected down to a
  1 KB fixture that is a cube plus one metadata element.
- **Root cause:** that branch read `config.opt_float("printable_height")` unguarded.
  `ConfigBase::opt_float` is `option<ConfigOptionFloat>(key)->value`, so a *missing* key is a null
  dereference, not a default — the fault address is `0x8`, the `value` offset. A 3mf is classified
  as a project purely from its generator tag, so its embedded project config can be absent or
  empty; `OrcaBadge.3mf`'s `Metadata/project_settings.config` is literally `{}`.
- **Not fork-introduced.** `printable_height` is still defined in `PrintConfigDef`; the three
  `extruder_clearance_*` reads on the following lines were already written with the
  `if (config.option<ConfigOptionFloat>(...))` guard this one lacked. It is a latent upstream
  OrcaSlicer bug that the fork's bundled sample projects happen to trigger.
- **Fix:** guard the read with the same idiom as its neighbours (`src/OrcaSlicer.cpp`, `CLI::run`).
- **Not a GUI defect:** verified before fixing that the GUI opens the same files fine — it does not
  go through `CLI::run`. The blast radius was the vestigial CLI only.
- **Regression test:** `tests/libslic3r/test_3mf.cpp`, SCENARIO
  "A project .3mf may carry no printable_height" — stores a `.3mf` with an empty project config,
  reloads it, and asserts `is_bbl_3mf` is true while `printable_height` is absent. That is the
  reachable state which made the unguarded read fatal.

### Second fix: RelWithDebInfo install tree shipped no third-party DLLs

The first packaged launch failed with `OrcaSlicer.dll was not loaded, error=126`.
`src/CMakeLists.txt` called `orcaslicer_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_Release)`
while the install rule beside it reads `output_dlls_${build_type}` with `build_type` =
`"RelWithDebInfo"` — so `install(FILES ...)` expanded to nothing and freetype/GMP/MPFR/OCCT/WebView2
were all missing from the install tree. Also upstream, and Release-configured CI builds dodge it.
Corrected to return the list under the matching name; the install tree now carries all 39 DLLs and
the packaged app launches. This one is inside F15's granted `files`.

### Human eyeball required — GUI-only, not scriptable

Everything below needs a person driving the UI. Nothing here regressed as far as headless checks
can see; they are simply unverifiable from a script.

- **PNP GUI slice end-to-end:** progress bar advances, preview renders toolpaths with roles,
  `<plate>.gcode` written, `pnp-config-warnings.jsonl` grows, legend shows weight + cost.
  (Backend probe and a packaged-layout `pnp_cli slice` are both proven above, so the remaining
  risk is UI wiring, not the backend.)
- **Cancel kill-tolerance** — unexercised since B3.
- **Calibration menu:** every entry opens its mock dialog and OK posts the not-implemented
  notification without crashing.
- **SLA `.3mf` refusal dialog** (the CLI path is verified; the dialog is the same throw).
- **Variable-layer-height editor** opens and edits.
- **Support painting** paints without crash (paint-only, no overlay — pnp item 13 restores it).
- **Preset-diff dialog** and tab switching (B7 fixed one crash here; the surface is otherwise
  only launch-level verified).
- **Preferences → General → "PNP CLI directory"** override honored end-to-end. The underlying
  resolution order is proven by the differential test above; the UI round-trip is not.

### Known gap, not fixed here (out of F15's granted files — needs a decision)

`.github/workflows/build_orca.yml` checks this repo out standalone, so `PNP_DIST_DIR`'s default
(`../target/dist`) will not exist in CI. The configure step will warn and skip bundling, and the
resulting installer/portable zip will contain **no `pnp_cli.exe`** — a silently backend-less build.
The CMake side is correct and needs no change; the workflow needs a step that produces or fetches
the pnp dist tree and passes `-DPNP_DIST_DIR=<path>`. Flagged rather than fixed because F15's
frontmatter grants only `CMakeLists.txt` and `src/CMakeLists.txt`. The README says so explicitly.

Secondary, noted while diagnosing: **a CLI crash produces no crash log.** `CBaseException` only
opens one once `set_log_folder` is called, and only `GUI_App` calls it; `data_dir()` is also empty
unless `--datadir` is passed. The unhandled-exception filter installed by `SET_DEFULTER_HANDLER`
did not fire either — the GUI gets its stack traces from an explicit `__try/__except` around
`OnInit`, not from that filter. Diagnosing the crash above needed both to be added temporarily;
both were reverted, since neither was in scope.

### B9 follow-up: schema gate, remaining guards, first wire-contract coverage

Landed after the batch commit; `pnp-batch-B9` stays on the gated commit.

**The per-slice progress-schema gate from [ticket 009](../../tickets/009-pnp-cli-distribution-and-discovery.md)
was never implemented.** `PnpProgressParser` parsed `schema_version`, stored it, exposed it via
`schema_version()` — and nothing read it. The header comment even told the caller to gate against
`PnpBackend::SUPPORTED_CONFIG_SCHEMA_MAJOR`, which is the *config* line, and that conflation is the
bug: pnp exposes **two independent semver lines**, and they move separately. Config schema is at
`1.0.0` while the progress stream is already at `1.3.0`. The startup probe gates only the former, so
it offers no protection here at all — a pnp bumping the progress major to 2.0.0 while leaving config
schema at 1.x would pass the handshake and then drive the progress bar and the legend from events
this build cannot read. This session watched that line move 1.2.0 → 1.3.0 with nothing noticing.

Now implemented: `SUPPORTED_PROGRESS_SCHEMA_MAJOR` sits beside `SUPPORTED_CONFIG_SCHEMA_MAJOR` on
`PnpBackend`, so both wire gates are declared in one place. `PnpProgressParser::check_schema_version`
runs once, where the first `schema_version` is captured, and reports a mismatch through the parser's
existing fatal channel — `PnpSlicingProcess` already computes
`failed = exit_code != 0 || parser.has_fatal_error() || ...` and throws `SlicingError`, so the slice
fails through F08's normal path with no new plumbing. Behaviour by case:

| First `schema_version` seen | Result |
|---|---|
| Same major (`1.x.y`) | Accept, log at info |
| Different major (`2.0.0`) | Fatal — message names found-vs-supported |
| Present but not semver | Fatal — cannot be shown to be same-major, so no benign reading |
| Absent | Accept, log. Absence is not evidence of incompatibility, and refusing it would turn one dropped field upstream into a total slicing outage |

Ticket 009's resolution text is left as written: it was right, the implementation drifted from it.

**Remaining `opt_float` guards.** The two other unguarded `opt_float("printable_height")` reads in
`CLI::run` — the `--uptodate` branch and the `--downward-check` branch — now carry the same guard.
Both read machine-preset configs rather than project configs, so neither is known to be reachable;
the guard costs nothing and removes the class from the file. Every other candidate in `CLI::run`
(`max_layer_height`, `min_layer_height`, `default_print_profile`, `different_settings_to_system`,
all three `extruder_clearance_*`) was already guarded.

**First committed coverage of the pnp wire contract.** New `pnp_runtime_tests` target — kept
separate from `pnp_config_translator_tests` because the warnings-log cases redirect the global
`data_dir()`, which is not state to hand the translator cases as a side effect. 14 cases, 98
assertions:

- `PnpProgressParser`: phase→percent model, monotonicity under out-of-order events, stream
  `layer_count` overriding the GUI estimate, plate-label prefixing, `slice_stats` stored verbatim,
  fatal-vs-degraded separation, line reassembly across pipe reads, final-line flush, all five
  schema-gate cases, and **forward compatibility** — unknown event types and unknown fields ignored,
  garbage lines skipped and counted but never fatal. That tolerance is what let 1.3.0 land safely.
- `log_pnp_config_warnings`: the Tier-D filter (not-yet-mapped at default dropped, moved-off-default
  kept), the other three classes never filtered, record fields, `sent_value` only on lossy-fallback,
  and append-not-truncate across slices — the writer-level meaning of "warnings jsonl grows".

The schema-gate accept case is asserted against a line captured verbatim from a real `pnp_cli` run,
so it fails if the supported major and what pnp actually ships ever diverge.

**Verification, and its limits.** `ALL_BUILD` RelWithDebInfo clean (0 errors) and `ctest`
**200/200** (186 → 200; +14 new cases). By explicit decision the install target and the packaged
acceptance sweep were **not** re-run for this follow-up, so the gate's accept path is verified by
unit test against a captured real line — plus the measurement that 130 of 130 JSONL lines in a
packaged-run capture carry `schema_version: "1.3.0"` — rather than by a fresh live packaged slice.

### Sign-off

Release readiness: **awaiting the human's sign-off**, pending the GUI-only list above.

Note that the follow-up above does not shrink that list. The backlog is human-only by construction:
what it contains is precisely the set of behaviours that need eyes on a GL canvas. The new coverage
narrows one item — the progress/legend/warnings *logic* is now tested — while everything visual
about it, and items 2 through 8 entirely, still need a person driving the UI.
