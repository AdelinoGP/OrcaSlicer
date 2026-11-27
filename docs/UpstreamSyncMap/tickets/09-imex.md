---
title: IMEX / iXex multi-gantry support
status: open
type: implementation
assignee: ""
blocked-by: []
---

# IMEX / iXex multi-gantry support

## Question

Port the half of upstream's IMEX/iXex support that survives this fork's removals: the
preset-referenced config keys (eight of upstream's fourteen `imex_*`/`is_imex` keys appear in
shipped presets), the zones and ghosting model, the arrangement helpers, and the mode picker UI —
45 commits, 12 paths, +3,528 lines.

The other half — toolchange planning and mode execution — lives in the deleted `GCode.cpp` and
`Print.cpp`, so it becomes a `pnp_cli` prerequisite (ticket 12, P1.1–P1.3) rather than a
cherry-pick. Shipping the UI without that prerequisite reproduces the exact failure the profile
sync ticket warns about: nine Vivedino Xplorer presets load as ordinary single-tool printers.

## What ports

| Piece | Files | Notes |
|---|---|---|
| Config keys | `PrintConfig.cpp/.hpp` | Eight keys are preset-referenced; upstream defines fourteen. Upstream defs at `PrintConfig.cpp:7262-7384`. Overlaps ticket 03 — **sequence after 03** and re-check what is already present |
| Zones / ghosts | `IMEXZones.cpp` (414), `IMEXHelpers.cpp` (689) | Include only `Arrange.hpp`, `ClipperUtils`, `Config`, `PrintConfig` — no `Print`, no `GCode` |
| Arrangement | `IMEXArrange.cpp` (220) | Same include surface |
| Mode picker | `IMEXModesCtrl.cpp` (903), popover (147) | GUI only |
| Plate integration | `PartPlate.cpp`, `Plater.cpp`, `Tab.cpp` | The permanent fork-side surface; `Plater.cpp` is the highest-churn file in this repo |

## What does not port

`GCode.cpp:493,1835,3377-3404,3857-3874` and `Print.cpp:2198-2204` — mode G-code selection,
toolchange sequencing, parking. Deleted, and replaced by ticket 12's P1.1–P1.3.

Also excluded: `Tab.cpp` IMEX code from commits `185b7084d2`, `2cfdddf8b0`, `f540cf87a8`,
`d9be71b1ca`, which depend on `m_ixex_mode_combo` and friends that do not exist at this base. The
series in *this* ticket is what introduces them, so do not cherry-pick those four first.

## Sequencing

1. Ticket 03 (keys) lands.
2. Ticket 12 (prerequisites) closes with P1.1–P1.3 specified; this ticket implements them under the
   waiver and does not merge until P1.3's slice comparison passes.
3. Port config keys (skipping any already added by 03), then the non-GUI helpers, then the GUI.
4. `PartPlate`/`Plater`/`Tab` hunks are hand-ported per the standing rule; document each.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - A Vivedino Xplorer preset loads and the machine is presented as IMEX, with gantry/tool
     counts from its JSON (proves the keys are bound end-to-end).
   - The mode picker lists the machine's modes with their names.
   - Zones render on the plate; ghosts appear for the mirrored tool.
   - Arrange produces a layout that does not overlap gantries.
   - Slice through `pnp_cli` and confirm the tool sequence matches the reference file from
     prerequisite P1.3 — this is the case that proves the feature is real rather than drawn.
3. If P1.3 is not met, the ticket **does not merge**: the GUI half stays on the branch, and the
   ledger records `blocked-on-pnp`. It does not land behind a dead control, because rule 10 and
   BootstrapMap 012 forbid exactly that.

## Risks

- Three of the highest-churn files in the fork (`Plater.cpp`, `PartPlate.cpp`, `Tab.cpp`) are in
  scope. This is the largest permanent-divergence ticket among the fixes and must be reviewed as
  such.
- The GUI is convincing even when the backend does nothing. The P1.3 slice comparison is the only
  evidence that distinguishes the two.

## Done when

Keys, helpers, arrange and picker ported; plate integration hand-ported and documented; build and
tests green; walkthrough with screenshots including the reference-slice comparison; ledger row
updated.
