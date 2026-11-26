---
title: Crash fixes — hand-ported
status: closed
type: implementation
assignee: opencode-agent
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

## Answer

Four of the five picks are on `pnp/main`; the fifth was re-measured and **not landed** because the
defect does not exist at this base. The order is the ticket's: pick 1 first, built alone before any
other pick.

| Commit | Pick | What landed |
|---|---|---|
| `a816ed8d64` | `d7534d7fb9` | `get_shared_poly(areas, bed)` returns empty instead of indexing an empty intersection; a <3-point area no longer limits the shared area; `placement_bed_shape` and both `get_bed_shape` fall back to the whole bed; `PartPlate::get_build_volume(true)` falls back to the plate; `Print::validate` reports "share no common area" |
| `91fba6e8b3` | `71c9f556f0` | `switch_excluder` maps pages to the extruder's first variant when the process names none and logs it; `on_value_change` checks the map lookup and falls back to the field id; `find_variant_index` ported as the helper the fallback needs |
| `31e929bda0` | `38f12b1357` | a project filament no longer applies empty `compatible_printers` / `compatible_prints`; a later `Name(file.3mf)` copy inherits them from the preset the project names |
| `c30bb752f2` | `27cafaa06c` | six locale-safe nozzle-diameter reads (`bbs_3mf`, `DevMappingNozzle`, `PartPlate`, `Plater` ×2, `SelectMachine` ×2, `MultiNozzleSync`); `GCode.cpp` hunk omitted |
| — | `e56fd1ed44` (not landed) | — |

### Not landed

**`e56fd1ed44` (multi-part delete/merge).** Its crash is in an import-order branch of
`ObjectDataViewModel::GetChildren` that reads `m_model_object`; that branch arrives with
`fa5f7e3db4` (#17050), which is not picked here. This base's `GetChildren` only walks the row's
children and never reads the model object, so the freed-pointer read is unreachable. The pick's own
message says no release carries the crash. Its `tests/slic3rutils/test_object_list.cpp` is absent,
and porting `live_model_object` without the branch would add an unused helper. Recorded as
defect-absent in the ledger, like ticket 05's two such picks.

### Divergence notes

- **`d7534d7fb9`'s `Print.cpp` message needed its parent check.** This base predates
  `4303d723a1`'s wipe-tower-vs-printable-area validation, so the pick's hunk had no code to refine.
  The port adds the minimal equivalent check: the tower grown by its brim, compared against
  `Print::get_extruder_shared_printable_polygon()` (already here), with the pick's empty-area message
  when the shared area is empty and "partially outside" otherwise. The upstream estimate/footprint
  refinement is not ported.
- **`get_shared_printable_polygon` hunks are omitted.** `PartPlate::get_shared_printable_polygon`
  and its two callers (the wipe-tower default position and `Selection::translate`) arrive with
  `7c73739e1a`, which is not picked. This base clamps the tower through
  `get_build_volume(true)`'s bounding box, and the ported `PartPlate` hunk already falls back to the
  whole plate when the extruder areas share nothing. `PartPlate.hpp` keeps the `get_build_volume`
  comment and drops the comment for the absent function.
- **`find_variant_index` is a supporting port.** The pick falls back through it, but it is absent
  at this base (it arrives with `205de9ce63` / `a56d1bf33e`). The two-file helper is ported
  unchanged so the Tab code and the upstream test read the same function; `get_index_for_extruder`
  still returns -1 for its other callers.
- **`38f12b1357` moved placement only.** This base builds and applies the project config before the
  preset lookup. The two keys are therefore erased from the project's key list before the apply,
  and `found_by_name` is captured after the lookup; the applied semantics match the pick.
- **`27cafaa06c`'s test file needed two reductions.** `test_locales_utils.cpp` is absent, so it was
  taken whole, but `atof_decimal_point` was removed here with `GCode/` and this base's Windows
  `to_chars` buffer is 20 bytes, so its test case and the 1e21 float-to-string row are dropped with
  a comment. `CommaNumericLocale` moves into `tests/test_utils.hpp`. The `GCode.cpp` hunk is omitted
  because that file is gone.

### Verification

- `xmake -j2 libslic3r_tests` after pick 1: ok (build alone first, as the ticket asks).
- `xmake -j2 OrcaSlicer`: ok, 2,681.8 s.
- `xmake test`: **7/7 passed**, 0 failed. `libslic3r_tests` 69.5 s, `filament_group_tests` 54.8 s.
- New cases: `[BedShape]` 25 assertions in 5 cases, `[Arrange]` 52 in 11, `[Print]` 9 in 1
  (the ported wipe-tower case), `[LocalesUtils]` 24 in 5, the compatible-printer case 20 in 1, and
  the variant-fallback case 4 in 1.
- GUI, `orca-slicer.exe` from the build tree, driven with cua-driver:
  - **(1) Per-extruder areas that do not overlap — observed.** The active Anet AM8 preset was given
    two disjoint `extruder_printable_area` strips (x 0–100 and 150–240) and the app launched,
    switched to Prepare, and rendered the plate without crashing
    ([screenshot](../assets/06/01-disjoint-areas-plate.png)). The fixture was reverted after the
    run. There is **no before-fix run**, so this shows the fixed build works with the fixture, not
    that the old build crashed on it; the empty-intersection path is pinned by the `[BedShape]`
    case instead.

### Gaps

- **(2) Object speed edit without a matching variant — not exercised in the GUI.** The custom
  printer/filament combos could not be driven far enough to select the Bowden-printer /
  Direct-Drive-process fixture. The ported `[Config]` case covers the exact-lookup/fallback split,
  and the Tab path is compiled but not clicked through.
- **(3) Compatible printers surviving project load — automated only.** The ported case covers all
  four load shapes (first slot selected, first slot parent, later slot embedded, later slot copied);
  no project save/reopen was clicked through.
- **(5) Decimal-comma locale — automated only.** The ported `[LocalesUtils]` case forces a
  comma-decimal locale and proves both parsers; no 3MF carrying `slice_filaments_info` was opened
  with the UI in German.
- **(4) Multi-part delete/merge — not applicable**, see Not landed.
- The printer dropdown is a custom popup window (`wxWindowNR` with no UIA provider); pixel clicks
  open it inconsistently, which is why (1) used a preset file and (2)/(3)/(5) were not clicked
  through.
- The `fff_print` wipe-tower case went to a new `test_print.cpp` because `test_wipe_tower.cpp` is
  absent here and `Print::validate()` is that file's subsystem per `tests/fff_print/README.md`.

## Done when

Five commits in dependency order, pick 1 verified compiling alone, build and tests green,
screenshots including the German-locale case, ledger evidence filled with omitted hunks named.
