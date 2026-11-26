---
title: DeviceCore / printer agent
status: open
type: implementation
assignee: ""
blocked-by: []
---

# DeviceCore / printer agent

## Question

Take upstream's 110 commits of printer-agent and `DeviceCore` work: 94 files, +6,931/−1,409,
17 of them new (13 `DeviceCore`, 4 `DeviceTab`).

The fork is unusually well placed for this port and should take it early among the features: it
already ships 56 `DeviceCore` files, and diverges from the fork base by only **three** files
(+2/−67). There is no removed-core dependency — `DevCalib.cpp:9` and `DevNozzleSystem.cpp:14`
include `libslic3r/PrintConfig.hpp` and nothing from `Print`/`GCode`/`SLAPrint`. The cost is not
portability; it is steady churn across `GUI_App.cpp` (30 commits), `SelectMachine.cpp` (27),
`Plater.cpp` (24), `Preset.cpp` (17), `Tab.cpp` (16).

## Scope

| Piece | Notes |
|---|---|
| 13 new `DeviceCore` files | Mostly additive — the cheapest half |
| 4 new `DeviceTab` files | Device tab surfaces the fork already ships |
| Per-driver deltas | `SelectMachine.cpp`, `Preset.cpp`, `PresetBundle.cpp` changes are hand-ported; they are preset-selection logic, not network code |
| `GUI_App.cpp` / `MainFrame.cpp` wiring | The permanent fork-side diff |

Interaction with ticket 02: `e40030cf81` (network JSON hardening) touches the same six drivers.
Land ticket 02 first, then this ticket, so the hardening is in place before the drivers change
shape. Re-run `git apply --check` per pick at that point rather than trusting the 2026-11-25
survey.

## Sequencing

1. Ticket 01 and 02 first (hardening), so driver code is hardened before it moves.
2. New files, then per-driver deltas, then `GUI_App`/`MainFrame` wiring.
3. Every `Plater.cpp`, `Tab.cpp`, `Preset.cpp` hunk is hand-ported and documented.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - Device tab lists a discovered LAN printer.
   - Printer switch does not misroute the device view.
   - Per-driver status surfaces (Duet, ESP3D, Flashforge, MKS, PrintHost) still parse their
     responses; use stubs for the ones no hardware is present for.
   - `SelectMachine` flows: selecting a machine preset updates device configuration without crash.
3. State in the PR body which drivers were checked against real hardware and which against stubs.
   Do not imply hardware coverage that did not happen.

## Risks

- 110 commits across the GUI's most-divergent files — the churn, not the port, is the cost. This
  is the ticket where "always hand-port" accumulates the most permanent surface, and the ledger
  must record it as such.
- New device-management code can reach the network on startup; a regression here is a hang or a
  firewall prompt, not a crash, so the walkthrough includes app startup on a clean profile.

## Done when

New files ported, driver deltas hand-ported and documented, wiring in place, build and tests
green, walkthrough screenshots including a clean-startup case, ledger row updated.
