---
labels: [wayfinder:map]
title: Upstream OrcaSlicer sync (2.5.0-pnp → 2.6.0-dev)
---

# Upstream OrcaSlicer sync

## Destination

The 3,554 upstream commits between `pnp-fork-base` and upstream head are triaged once, and the
worthwhile ones land in this fork — hardening, crash and format fixes, profile data, translations,
and the feature subsystems that survive the fork's removals — each with a pnp-side prerequisite
named where the GUI cannot honour it alone.

Three things are true when this map is done:

1. Every candidate in the range has a recorded disposition in [the ledger](ledger.md)
   (adopted / deferred / rejected + reason). A future pass reads the ledger instead of
   re-surveying 3,554 commits.
2. The adopted picks are on `pnp/main` with build, test, and screenshot evidence per ticket.
3. Each feature ticket names what `pnp_cli` must implement before its UI is truthful, and that
   work is scheduled rather than hidden behind a disabled control.

## Notes

- **Range.** Base is tag `pnp-fork-base` = `33909a51cd` (2026-07-16, upstream `2.5.0-dev`,
  `SLIC3R_VERSION 02.06.00.51`). Head is `refs/remotes/main/main` = `808dd7dff83f`
  (2026-11-25, `2.6.0-dev` / `02.08.01.55`). Between them: 3,554 commits, 2,977 non-merge, with
  132 fork-local commits ahead. Note the base is **`2.5.0-dev`, not tag `v2.5.0`**: that tag is
  3,292 commits later and is not an ancestor of `main/main`.
- This map is a successor to [BootstrapMap ticket 002](../BootstrapMap/tickets/002-upstream-sync-strategy.md),
  which set the cherry-pick-only policy; it does not replace it.
- Domain: C++17/wxWidgets fork (this repo) + Rust backend (`pinch_n_print_cli/` submodule, binary
  `pnp_cli`). Windows x64 is the only verified target.
- Tracker: local markdown, same convention as the other maps. Tickets live in [tickets/](tickets/);
  frontmatter `status/type/assignee/blocked-by` is authoritative. Frontier = open, unassigned, all
  `blocked-by` closed.
- **Ledger is the artefact that matters.** [ledger.md](ledger.md) carries one row per candidate:
  upstream SHA, disposition, ticket id, evidence.

## Standing rules (decided 2026-11-25, grilling)

1. **Cherry-pick only, no merge.** Reaffirmed against a 3,554-commit gap: upstream still ships
   `CMakeLists.txt` and a live `Print::process`, so a merge would re-litigate the slicing rip-out
   across hundreds of files for no functional gain. ~1,000 in-range commits are permanently
   unreachable for this reason, not for lack of value.
2. **Relevance = fixes, security, profile data.** Upstream *features* enter only through a ticket,
   never as a drive-by pick. A ticket names a pnp prerequisite when the feature's effect is
   produced by `pnp_cli` (tickets 09, 10, 11) or when it does not (tickets 08, 13, 14); the
   prerequisite is the rule, not the ticket format.
3. **Removal boundary stands.** No pick may resurrect SLA, the calibration generators, native
   slicing (`Print::process` — an empty stub at `src/libslic3r/Print.cpp:2238`), or anything under
   the deleted `GCode.cpp` / `GCode/`.
4. **Always hand-port on conflict**, including *partial* picks with deliberately omitted hunks.
   Every fork-side divergence is recorded in the ledger, because it is permanent merge surface.
5. **Applicability is measured, never inferred.** A pick is `CLEAN` only when
   `git show <sha> | git apply --3way --check -v --exclude='tests/*'` produces no conflict markers.
   **Exit codes alone are unsound**: `--3way --check` returns 0 while leaving markers, which is
   how a first draft over-reported 19 clean picks instead of 12.
6. **Test hunks are a separate, per-pick decision.** Their paths differ here, so source verdicts
   exclude `tests/**`. Registration is **automatic** for `tests/libslic3r/*.cpp`
   (`xmake.lua:1247`); only `tests/pnp` lists files explicitly. Porting a test whose file is absent
   means taking the whole upstream file; porting one that conflicts means a hand-port in a file the
   fork owns. The ledger keeps the per-pick table.
7. **Translations: wholesale.** Safe because the fork has made **zero** `.po` edits since the fork
   base. Catalogs must match the
   [localization glossary](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/guides/localization_glossary.md)
   per `AGENTS.md`.
8. **Dependencies move only when forced** — a security advisory, or unblocking a pick.
   `conan/conan.lock` stays the authority; upstream CMake/vendored churn has no target here.
9. **Profiles ship with their keys, and with the ignore-set fix.** The refresh lands together with
   the missing config keys and with removal of `filament_prime_volume` from the obsolete-key
   ignore set — otherwise the keys are present and still cleared.
10. **No defensive UI gating.** [BootstrapMap ticket 012](../BootstrapMap/tickets/012-v1-ui-surface-restrictions.md)
    decided the fork builds no hiding, gating or blocking against a PNP gap. The warning channel is
    a separate axis — [ticket 013](../BootstrapMap/tickets/013-config-warning-ux.md) chose log-only
    and was later reopened, so gaps now surface as a `WarningNotificationLevel` notification; that
    is informational, not a gate. A feature whose backend half is missing **does not merge**
    rather than merging behind a dead control.
11. **Ticket size ≈ 6 picks**; tickets are the working unit, and each ticket ends with a GUI
    walkthrough rather than each pick.
12. **Verification per ticket:** `xmake -j2` build + `xmake test` (seven suites), then a real GUI
    walkthrough of the behaviour the ticket changes, with screenshots. Agent-run; the user
    spot-checks.

## Measured facts behind these rules

Verified in this repo against the fetched upstream, 2026-11-25:

| Fact | Measurement |
|---|---|
| Range | 3,554 commits / 2,977 non-merge; 1,497 touch `src/slic3r/GUI/**`, 699 are GUI-only |
| Out of reach by construction | 322 commits touch deleted `GCode.cpp`/`GCode/`; 265 on CAD paths; 168 plugin host; SLA/calibration series |
| Hardening wave | 8 picks — **4 CLEAN**, 4 CONFLICT (source hunks, tests excluded) |
| Crash/format shortlist | 16 adopted picks — **8 CLEAN**, 8 CONFLICT; plus `d7534d7fb9` (CONFLICT) = 17 |
| Fix picks overall | 24 picks: 12 CLEAN / 12 CONFLICT; with the added prerequisite, 25 rows and 13 conflicts |
| Config-key gap | upstream 1,016 `this->add()` keys vs fork 848 → **170 missing**; **19** referenced by shipped presets across **673 files** |
| Ignore-set trap | `filament_prime_volume` is in the fork's obsolete-key ignore set (`PrintConfig.cpp:8405`), so adding the key alone leaves 447 presets cleared |
| No profile-load test | `grep -rl resources/profiles tests/` is empty; no suite parses the shipped JSON |
| Profiles are pure upstream | `git diff 33909a51cd HEAD -- resources/profiles` is **empty** — no fork-local profile edits |
| Profile churn | 13,000 files changed (rename-detected), 145 vendor directories, **72** vendor index `version` bumps |
| Translations | 157 commits (125 non-merge, 103 `.po`-only), +683,058/−296,957; upstream 23 catalogs including `eu` |
| Feature portability | UltiMaker +1,184 (6 commits) / CAD +45,608 / DeviceCore +6,931 (56 files already here, 3 diverged) / IMEX +3,528 / plugins +23,083 / belt +2,625 |

## What existing users will notice

The docs are honest about behaviour changes, not just fixes:

- Downloads are renamed to a free filename (`41eeaf3883`) — a file with a clashing name no longer
  overwrites.
- Some accessory files stop opening (`203bc63f35`). An attachment outside the extracted auxiliary
  directory is refused, and a file type that can run as a program asks for confirmation first.
- Project names, authors and descriptions from a 3MF are no longer rendered as live HTML
  (`203bc63f35`): names are text, descriptions keep only plain formatting, http(s) links and https
  images.
- 3MFs with a cyclic component graph, a plate id below 1, or malformed paint data are now refused
  or repaired instead of crashing or exhausting memory (`a124b7d2df`, `490d134507`).
- A printer that answers with HTML or truncated JSON no longer takes the app (or the upload queue)
  down (`e40030cf81`).
- Malformed OBJ/DRC files that were tolerated are now refused (`8ffd3e514e`).
- A filament's compatible-printer list survives project load (`38f12b1357`).
- Auto Drop for 2.4.x projects keeps the project's value instead of re-enabling itself
  (`4c2ae6d202`).
- GPU failure now raises a notification instead of failing silently (`3c72223aaf`).
- Adding the missing keys makes those options *present but inert for slicing* until `pnp_cli`
  consumes them. Unmapped options are tinted amber per BootstrapMap 013's reopening; whether these
  keys render tinted cannot be verified until tickets 09/10 add their controls, and stock filament
  presets may begin warning once the keys are mapped.

## Build and packaging consequences

- **No app version bump.** `version.inc` stays `2.5.0-pnp` / `02.06.00.51`, and its mirror
  `version_inc` in `xmake.lua:46` stays in step, so `version_guard` (`xmake.lua:343`) keeps
  passing. The 72 vendor `version` fields ticket 03 bumps are **profile-data** versions, not app
  versions — but ticket 03 must still check how `PresetUpdater::sync_printer_config` behaves when
  a 2.8-era vendor profile lands under a 2.6 app number.
- **Submodule drift.** Tickets 09, 10 and 11 may edit `pinch_n_print_cli/` under the ticket 12
  waiver. That moves submodule HEAD, which the `.pnp-stamp` guard
  ([SchemaBridgeMap ticket 16](../SchemaBridgeMap/tickets/16-restage-on-bump-guard.md)) compares
  against the staged backend; every such commit must be followed by `xmake pnp`, and the build
  fails loudly until it is. That is by design.
- **No new shipped dependency except possibly three.** Tickets 01–10, 12 and 13 add none.
  Ticket 11 adds a Python/pybind11 runtime **only if** P3.1 keeps the Python plugin capabilities;
  if the Wasm ABI replaces them, it adds nothing. Ticket 14 adds SLVS (GPLv3) as a Conan recipe or
  vendored in `deps_src/`; its licence file must travel with it.

## Open assumptions

- **Six PRs, not one.** Every ticket is assigned:
  - **A — profiles:** ticket 03.
  - **B — fixes:** tickets 01, 02, 04, 05, 06, 07, 08, 13. One PR because each is a small,
    independently revertable commit; the translation snapshot rides along as its own commit.
  - **C — CAD:** ticket 14, because +45,608 lines needs its own review.
  - **D / E / F — IMEX, belt, plugins:** tickets 09, 10, 11. Each is a feature with a backend
    half and its own acceptance slice, so each gets its own PR, after ticket 12 closes.
  One PR carrying everything would serve none of these reviews.
- **Runner-ups deferred.** Twelve lower-priority crash fixes named in the ledger are not
  applicability-checked and have no ticket yet. "No ceiling" admits them; they queue behind the
  14 tickets here rather than being surveyed twice.

## Tickets

| # | Ticket | Blocked by | Picks |
|---|---|---|---|
| 01 | [Security hardening — clean picks](tickets/01-security-clean-picks.md) | — | 4 |
| 02 | [Security hardening — hand-ported](tickets/02-security-hand-ports.md) | — | 4 |
| 03 | [Profile sync, keys, and the ignore set](tickets/03-profile-sync-and-keys.md) | — | data + 19 keys |
| 04 | [Crash and format fixes — clean picks](tickets/04-crash-format-batch-a.md) | — | 6 |
| 05 | [Crash and format fixes — mixed](tickets/05-crash-format-batch-b.md) | — | 6 |
| 06 | [Crash fixes — hand-ported](tickets/06-crash-format-hand-ports.md) | 03 | 5 |
| 07 | [Translation catalogs](tickets/07-translations.md) | — | 23 catalogs |
| 08 | [UltiMaker host](tickets/08-ultimaker-host.md) | — | 6 commits |
| 09 | [IMEX / iXex multi-gantry](tickets/09-imex.md) | 03, 12 | 45 |
| 10 | [Belt printers](tickets/10-belt-printers.md) | 03, 12 | 54 |
| 11 | [Plugin host](tickets/11-plugin-host.md) | 12 | 168 |
| 12 | [PNP prerequisites for ported features](tickets/12-pnp-prerequisites.md) | — | — |
| 13 | [DeviceCore / printer agent](tickets/13-devicecore.md) | — | 110 |
| 14 | [Design / CAD](tickets/14-design-cad.md) | — | 265 |

Order is the decided one: security → profiles → crashes → features.

## Decisions so far

<!-- one line per closed ticket: [title](tickets/NN-slug.md) — gist -->

- [Security hardening — clean picks](tickets/01-security-clean-picks.md): four CLEAN picks landed
  as `3c18052abf`, `f8afe3a5aa`, `0d87b1d20f`, `03463e1592`; all three test halves ported (two by
  hand), 7/7 suites green, six walkthrough screenshots. STL/3MF refusals are log-only.
- [Security hardening — hand-ported](tickets/02-security-hand-ports.md): four CONFLICT picks landed
  as `77f2c943d6`, `f831718b08`, `7efd6d971d`, `69e6f9ef2d`. The cycle guard needed its in-range
  precondition `92ab583ecc`, so both are one commit; `TriangleSelector` is ported against the base's
  pre-`42f708bbb6` leaf encoding; `PrintHost` has no lifecycle events to pair, so the catch sits in
  `perform_job`. Tests: 149 + 54 + 24 assertions in the ported cases, 7/7 suites green. A harness
  fix (`14b761f8ca`) makes `slic3rutils_tests` actually run Catch2 instead of minilzo's self-test.
  Walkthrough found the Project page aborts on projects carrying `Auxiliaries/`, **on the pre-change
  package too** — pre-existing and ruled out of scope; the accessory-open guard is therefore
  unit-tested, not click-through.

Tickets 01 and 02 are closed. The map otherwise records the grilling of 2026-11-25 and the two
review rounds that followed: the applicability method (exit codes lie), the `filament_prime_volume`
ignore-set trap, the rejection of `32f6db87e8` as a defect this fork never had, the partial-port
plan for `d7534d7fb9`, and the fact that profiles are a pure upstream snapshot.

## Out of scope

- The Project page abort (`0xc0000409`, ucrtbase) on any project whose archive carries
  `Auxiliaries/` — reproduced on the portable package built before [ticket 02](tickets/02-security-hand-ports.md#gaps),
  so pre-existing and not an upstream pick. It blocks the accessory-open click-through but not the
  sync; a fork-local bug effort if wanted.
