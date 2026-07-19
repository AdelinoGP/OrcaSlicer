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
- [ ] B6: F12 — batch verified
- [ ] B7: F13 — **M2 smoke + ctest**
- [ ] B8: F14 — batch verified (gating: item 14)
- [ ] B9: F15 — **final acceptance; release gate (items 6, 16)**
