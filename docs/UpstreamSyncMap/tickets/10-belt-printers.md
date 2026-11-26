---
title: Belt printers
status: open
type: implementation
assignee: ""
blocked-by: [03, 12]
---

# Belt printers

## Question

Port belt-printer support — 54 upstream commits, 17 files, +2,625 lines — knowing that the part
that makes it work is not in those commits at all.

This is the ticket most likely to be misread as cheap. Recorded here so nobody re-derives it:
20 of the 54 commits touch `Print.cpp`, 17 touch the deleted `GCode.hpp`, 16 touch the deleted
`PrintObjectSlice.cpp`, `BeltGCode.cpp` includes the deleted `GCodeWriter.hpp`, and
`BeltTransform.cpp` includes the deleted `GCode/MachineFrameTransform.hpp`. **None of the
mechanical half compiles here.** What ports is the configuration and the plate UI; the
kinematics belong to `pnp_cli` under ticket 12 (P2.1–P2.4).

## What ports

| Piece | Files | Notes |
|---|---|---|
| Config keys | `PrintConfig.cpp/.hpp` | `belt_printer`, `belt_printer_infinite_y`, `belt_slice_rotation`, `belt_slice_rotation_angle`, `build_plate_tilt_x` are the five preset-referenced keys; upstream defines **11** belt/tilt keys in total (the two `*_tilt_time` keys next to them are SLA and barred by rule 3). Upstream defs at `PrintConfig.cpp:7910-7970`. **Port the key definitions from head, not by replaying the 54-commit series** — replaying would also pull the retired keys upstream now ignores. Overlaps ticket 03 — sequence after 03 |
| Plate presentation | `PartPlate.cpp`, `Plater.cpp`, `Tab.cpp`, `GCodeViewer.cpp` | Infinite-Y plate, tilt, rotation controls |
| Purge tower dialog | `BeltPurgeTower.cpp` | GUI-only, but it exists to place belt purge towers — useless until P2.2 works. Port it last, or defer to ticket 12's follow-up |

## What does not port

`BeltGCode.cpp`, `BeltTransform.cpp`, and every `Print.cpp`/`GCode.hpp`/`PrintObjectSlice.cpp`
hunk. Reimplementing them is prerequisite P2.2 (infinite-Y slicing with back-transform), which
is backend work, not a cherry-pick.

## Sequencing

1. Ticket 12 closes with P2.1–P2.4 specified; this ticket implements them under the waiver and does
   not merge until P2.3's back-transform check passes.
2. Ticket 03's keys land; then add any belt keys it did not already bring.
3. Port plate presentation; hand-port the `Plater.cpp` and `PartPlate.cpp` hunks and document them.
4. `BeltPurgeTower.cpp` only once P2.2 produces belt G-code that a purge tower can sit on.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - An IdeaFormer/Custom belt machine preset loads and is presented as a belt printer (proves
     `belt_printer` is bound, not defaulted).
   - Infinite-Y mode shows an unbounded plate and the rotation controls.
   - Plate tilt renders and does not silently no-op (P2.4).
   - Slice a tall part through `pnp_cli` in belt mode, re-import the G-code, and confirm the part
     lands where the plate showed it — the back-transform check from P2.3.
3. If P2.3 is not met, the ticket **does not merge**: the GUI half stays on the branch and the
   ledger records `blocked-on-pnp`. A belt printer that slices Cartesian produces wrong parts while
   looking correct, which is worse than an absent feature, and rule 10 forbids shipping it behind a
   dead control.

## Risks

- This feature's failure mode is silent and physical: a user who trusts a belt-mode slice gets a
  part printed in the wrong frame. That is why the ticket may not close on GUI evidence alone.
- `build_plate_tilt_x` interacts with the plate and the viewer; if the backend ignores it, the
  viewer must not pretend otherwise, and P2.4 requires an explicit refusal rather than a no-op.

## Done when

Keys and plate UI ported, backend prerequisites demonstrated, back-transform verified by
re-imported G-code, screenshots attached, ledger row updated.
