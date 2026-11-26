---
title: Crash fixes — hand-ported
status: open
type: implementation
assignee: ""
blocked-by: [03]
---

# Crash fixes — hand-ported

## Question

The last five crash/format picks, all of which leave conflict markers at this base. One of them
(`d7534d7fb9`) is a genuinely valuable crash fix whose port requires dropping a hunk that depends
on code the fork does not have.

This ticket previously carried a sixth pick, `32f6db87e8` ("legacy bed exclusion areas rejected at
load"). It is **rejected**: it patches `is_valid_bed_exclude_area`, a validator this fork has never
had, so the defect does not exist here. See the ledger's "defect absent" section. The two review
rounds disagreed about *why* the pick was wrong; the measurement settled it — neither the
validator nor its caller exists here.

## Picks, in dependency order

| # | SHA | Defect | Conflict site / port shape |
|---|---|---|---|
| 1 | `d7534d7fb9` | "Fix the Crash When the Per-Extruder Printable Areas Do Not Overlap" (#17439, 2026-11-11) — `result_polygon[0]` on an empty intersection | `Print.cpp`, `PartPlate.cpp/.hpp`, `Selection.cpp` conflict; `PrintConfig.cpp/.hpp` apply cleanly. **Omit the hunks that call `get_shared_printable_polygon`** (absent here; it arrives upstream in `7c73739e1a`). Keep `get_shared_poly` / `placement_bed_shape`, the `PartPlate::get_build_volume` hunk, and the `Print.cpp` validate message (`layered_print_cleareance_valid`) |
| 2 | `71c9f556f0` | Crash editing object speed without a matching variant (`Tab.cpp`) | `Tab.cpp` |
| 3 | `38f12b1357` | Project load wipes a filament's compatible-printer list (`Preset.cpp`) | `Preset.cpp` |
| 4 | `e56fd1ed44` | Crash deleting/merging a multi-part object, macOS (`ObjectDataViewModel.cpp`) | Model tree; test file not portable as-is |
| 5 | `27cafaa06c` | Decimal-comma locale misreads nozzle diameters from 3MF | `PartPlate.cpp` and `SelectMachine.cpp` conflict; the `GCode.cpp` path does not exist and that hunk is omitted |

Pick 1 is the reason this ticket exists: the crash is real here (empty intersection indexed at
`[0]`), and the naive cherry-pick fails in four files for three different reasons. Porting it
means keeping the polygon helper and the validation message while dropping the two hunks that need
`get_shared_printable_polygon`.

## Method

Five `cherry-pick(partial)` commits, each body naming the conflicting files, the omitted hunks, and
for pick 1 the omitted `get_shared_printable_polygon` call sites. `git range-diff` each against
upstream.

## Test hunks

- `d7534d7fb9`: its test file conflicts in an existing test; port if the fixture is self-contained,
  otherwise record the drop.
- `27cafaa06c`: `test_locales_utils.cpp` does not exist here. Porting means taking the whole
  upstream file, and it is the only automated proof of the decimal-comma fix — worth it, since the
  fix is invisible in English locales.
- `71c9f556f0`, `38f12b1357`, `e56fd1ed44`: no `tests/` changes except `e56fd1ed44`'s new test file.

## Verification

1. Build and `xmake test` green. **Build pick 1 alone first**, so a later failure is attributable.
2. Walkthrough, screenshots:
   - Printer with per-extruder printable areas that do not overlap → no crash on plate update.
   - Object speed edit without a matching variant → no crash.
   - Filament with a compatible-printer list → list survives project load.
   - Multi-part object: delete one part, then merge → no crash.
   - OS locale set to a decimal-comma language (e.g. German), open a 3MF whose machine uses a
     `0,4`-style nozzle diameter → plate shows the right diameter.

## Risks

- Pick 1 touches `PrintConfig.cpp`, which ticket 03 rewrites heavily; hence `blocked-by: [03]`.
  Re-check applicability after 03 lands rather than trusting the 2026-11-25 measurement.
- The locale case is the easiest to fake: an English-locale smoke test proves nothing.
- Five hand-ports is the highest divergence-per-pick ratio in the fix set; the ledger records it.

## Done when

Five commits in dependency order, pick 1 verified compiling alone, build and tests green,
screenshots including the German-locale case, ledger evidence filled with omitted hunks named.
