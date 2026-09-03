---
title: Wire the live-schema gate into CI
status: closed
type: task
assignee: claude-code-agent
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
---

## Resolution (2026-09-03)

Landed as a **hard gate**, one step in `build_orca.yml` between `Test` and the
packaging steps:

```
- name: Live-schema gate (staged pnp_cli vs the fork's key handling)
```

It probes the dist ticket 08's staging step produced
(`pinch_n_print_cli/target/dist/developer/pnp_cli[.exe]`, `--module-dir
<dist>/modules`), writes the document to `pnp-live-schema.json`, finds the
already-built `pnp_config_translator_tests` binary under `build/`, and runs it
with `PNP_LIVE_SCHEMA` set and the `[live-schema]` filter. The document is
uploaded as an artifact (`if: always()`), so a failure is diagnosable without
re-running the leg.

**Both** hidden cases run in the one invocation — ticket 06's dead-curated-row
diff and ticket 11's page-accounting case. That is safe in a single process:
the registry seal is one-shot, and the filter admits exactly one case that
registers.

### Decisions

- **Hard gate, not reporting-only.** Ticket 09 already emptied the dead-row
  list, so the case starts green; the sequencing question the ticket carried
  was closed with it. Verified green in this session against the tree's
  bundled backend (wire `schema_version` 1.2.0, 23 modules / 260 module
  fields / 93 host entries): `All tests passed (32 assertions in 2 test
  cases)`.
- **The GUI is not launched in CI.** The runners are headless and the residual
  the map tracks is specifically the *rendered wxWidgets controls* (tickets
  11/12/14), which a smoke launch could not see either. Everything below the
  widget layer is what these two cases already cover. The manual-smoke debt
  stays manual; nothing new is owed.
- **Binary located by `find`, not `xmake run`.** `xmake run <target>` re-enters
  the build (measured here: it relinked the target and then tried to re-copy
  the Conan DLLs), so a gate step would silently become a second build. The
  direct path is deterministic and the `pnp.conan.dlls` rule has already put
  the runtime DLLs beside the binary.
- **`--module-dir` is parity, not the guard.** The map's ticket-11 operation
  note said a probe invoked without it loses its module manifests. Measured
  against this dist that is **no longer true** — the backend resolves
  `modules/` relative to its own binary, so the flag changed nothing from
  either the dist root or a foreign cwd (23 modules, 93 host entries both
  ways). The flag stays because it mirrors `PnpBackend`'s invocation, but the
  real defence is a shape assertion in the test, not the command line.

### Shape guard added (`tests/pnp/test_pnp_config_keys.cpp`)

The live page case now requires, before it registers anything, that the
document carries a non-empty `schema` array **and** at least one module field.
A manifest-less reply is still a *successful* probe carrying the host half
alone, and without this the gate would pass against a document the GUI never
sees (the page would have silently shrunk to host keys).

Both branches proven red by construction, against doctored copies of the live
document:

| document | result |
| --- | --- |
| real probe | 32 assertions, 2 cases, passed |
| `.schema = []` | `REQUIRE_FALSE(doc["schema"].empty())` failed |
| every `.fields = []` | `REQUIRE(module_fields > 0)` failed |

In both negative runs ticket 06's translator case failed too (the curated
targets go dead when the universe collapses), so the gate is loud from both
directions.

### Residual / caveats

- The `pnp.conan.dlls` rule is Windows-only, so on the Linux leg the test
  binary's shared-library resolution is untested — as is that whole leg
  (`continue-on-error` while unverified). Not investigated here.
- The gate ran against the tree's **bundled** dist (`build/windows/x64/release/`),
  not a freshly staged `target/dist/developer/` — no `cargo xtask dist` ran in
  this session. That is the same staleness class ticket 16 owns; the gate's
  mechanics and the CI path it uses are unaffected, but the "zero dead rows"
  measurement above is a statement about the bundled binary.
