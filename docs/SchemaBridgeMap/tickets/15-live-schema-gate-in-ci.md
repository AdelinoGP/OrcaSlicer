---
title: Wire the live-schema gate into CI
status: open
type: task
assignee:
blocked-by: [08]
---

## Question

The hidden `[.][live-schema]` Catch2 case (added by ticket 06, in
`tests/pnp/test_pnp_config_translator.cpp`) is the one check that the fork's
curated table matches the backend the build actually ships — but it never runs
automatically, and ticket 06's run of it was a one-off local measurement.

Ticket 08 gave CI the missing pieces: a Rust toolchain, a "Stage PNP backend"
step running `cargo xtask dist`, and the staged `target/dist/developer/`
layout. Wire the gate to follow it:

- After staging, CI dumps `pnp_cli module config-schema` to a file, exports
  `PNP_LIVE_SCHEMA=<that file>`, and runs
  `pnp_config_translator_tests "[live-schema]"` explicitly (the leading-dot
  tag keeps it invisible to `xmake test`'s default selection).
- ~~Today the case **fails by design**: it reports exactly ticket 09's six dead
  curated rows. Decide the sequencing — land it as a hard gate in the same
  change as ticket 09's row repairs, so it starts green and holds the line
  (ticket 08's session recommendation), or run it reporting-only until 09
  closes.~~ *(Closed by ticket 09, 2026-08-28: the dead rows are deleted and
  the case passes against a real wire-1.1.0 document, so there is no
  sequencing question left — land the gate as a hard, already-green check;
  it breaks only on genuinely new drift, which is its job.)*
- The non-Windows legs are `continue-on-error` already; the only per-platform
  wrinkle is the `pnp_cli` binary name (no `.exe` outside Windows).
- Decide whether a runtime smoke (launching the GUI against the staged dist)
  is in scope for CI or stays manual; the Catch2 suites already cover the
  pure seam.

Deliverable: the gate green (or explicitly reporting-only) on the Windows leg,
proving CI's staged `pnp_cli` produces a schema document the fork's drift
reconciliation accepts.