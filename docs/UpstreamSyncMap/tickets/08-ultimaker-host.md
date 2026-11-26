---
title: UltiMaker host support
status: open
type: implementation
assignee: ""
blocked-by: []
---

# UltiMaker host support

## Question

Port upstream's UltiMaker S5 local-API print host: `6e4be42a04` (2026-09-20, 19 files,
+1,184/−6; code is `UltiMaker.cpp` 666 lines plus `UltiMaker.hpp` 72) **plus five in-range
follow-ups** that touch the same two files (`4895bc03b4`, `84657ff11e`, `23546e71ce`,
`e821665e4b`, and one further commit). The first draft said "1 commit"; it is six.

This is the cheapest genuinely useful feature in the range, which is why it is the first feature
ticket — but the cost is not zero, and the first draft overstated how clean it is:

- `UltiMaker.hpp` derives from `PrintHost` and includes only `PrintConfig.hpp`, `Http` and wx —
  no `Print`, no `GCode`, nothing removed here. That part holds.
- `PhysicalPrinterDialog.cpp` (65-line hunk) and `PrintHost.cpp` (2 lines) have **zero** fork
  divergence since the base.
- `PrintConfig.cpp` and `PrintConfig.hpp` do **not**: they diverge by +65/−681 and +110/−226
  respectively, because the fork removed large parts of the option set. The three config lines
  and one enum line are small, but they land in heavily rewritten files — expect a hand-port, not
  a clean apply.
- `resources/profiles/UltiMaker.json` already exists at base, so the profile half is additive.
- The commit also touches `src/slic3r/CMakeLists.txt` (absent here — CMake removed),
  `OrcaSlicer.pot`, and `list.txt`. The CMake hunk is dropped; the `.pot` half belongs to
  ticket 07's snapshot.

Value is niche — UltiMaker S5 owners — but the debt is almost nil, which is the opposite of the
belt-printers trade.

## Scope

1. `src/slic3r/Utils/UltiMaker.cpp|hpp` — the host implementation.
2. `PrintHost.cpp` + `PrintConfig.cpp/.hpp` — register the host type.
3. `PhysicalPrinterDialog.cpp` — the 65-line hunk that exposes the host.
4. The profile update in the commit, **unless** ticket 03 already brought `UltiMaker.json` from
   head, in which case skip the profile half entirely and say so.

Interaction with ticket 02: that ticket deliberately omits the `UltiMaker.cpp|hpp` hunks from
`e40030cf81` (network hardening). Once this ticket lands, re-run `git apply --check` for those
hunks and take them, so the host ships hardened rather than being opened up to malformed
responses.

## PNP prerequisite

None. The host uploads files to a printer; it never calls the slicer. This is the one ported
feature that is complete on its own.

## Verification

1. Build and `xmake test` green.
2. Walkthrough, screenshots:
   - Physical Printer dialog lists the UltiMaker host.
   - Configure a host pointing at a local stub endpoint; confirm the dialog accepts it and the
     stored config round-trips through a preset save/load.
   - Attempt an upload against the stub and confirm the request shape (method, path, auth header)
     matches the documented local API — a stub that returns 200 is enough to prove the plumbing,
     since no S5 is available.
   - Malformed-response case from `e40030cf81`'s hunks → no crash.
3. Note in the PR body that no physical S5 was used and what the stub covered.

## Risks

- The host is unverifiable end-to-end without hardware. The stub proves plumbing, not
  compatibility; say so plainly rather than implying the printer path was tested.
- The 65-line `PhysicalPrinterDialog` hunk is the only fork-owned diff this ticket creates.

## Done when

Host ported, config registered, dialog exposes it, stub-based walkthrough with screenshots, any
hardening hunks from ticket 02 re-applied, ledger row updated.
