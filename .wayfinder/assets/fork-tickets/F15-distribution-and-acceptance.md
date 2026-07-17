---
title: Distribution layout + final acceptance pass
status: open
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
