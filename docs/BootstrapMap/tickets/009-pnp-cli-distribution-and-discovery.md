---
title: pnp_cli distribution & discovery
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: []
---

## Question

How does the pnp_cli binary ship with the fork — bundled in the installer/build tree vs. user-configured path? How does the GUI discover it, verify version compatibility (handshake), and locate the `modules/core-modules` WASM module directory? Windows-first.

## Resolution

### Shipping (bundled, flat in bin root)

- `pnp_cli.exe` ships **next to `orca-slicer.exe`** in the installer/build tree; the WASM module set ships as `<bin>/modules/<name>/{<name>.wasm, <name>.toml}`.
- Source artifact: pnp's `cargo xtask dist` stages exactly `pnp_cli.exe` + `modules/…` into `target/dist/` (see `xtask/src/dist.rs`); packaging copies those two entries into the bin dir.

### Discovery & override

- **Universal rule:** module dir = `<dir containing pnp_cli.exe>/modules`; the GUI always passes `--module-dir <that>` (plus `--no-default-module-paths` implied by explicit dirs).
- Default discovery: `pnp_cli.exe` in the GUI executable's own directory.
- **Preferences override:** optional "PNP CLI directory" field in Preferences. When set and valid, it wins over the bundled copy — points at any dir containing `pnp_cli.exe` (+ `modules/` beside it), e.g. a pnp checkout's `target/dist/`. Enables backend iteration without touching the install.

### Version handshake (existing pnp tooling — no new subcommand)

pnp's frontend-compat mechanism is **per-surface wire schema versions** (pnp `docs/11_operational_governance_and_acceptance_gate.md` §"CLI output wire contracts"): `CONFIG_SCHEMA_WIRE_VERSION` (JSON from `pnp_cli module config-schema`) and `PROGRESS_EVENT_SCHEMA_VERSION` (slice JSONL stream). Both semver; **consumers gate on the major component only**.

- **Startup probe:** at startup and whenever the override path changes, the GUI runs `pnp_cli module config-schema --module-dir <dir>` once. This proves the exe exists and runs, the modules load, and the top-level `schema_version` major matches the GUI's compiled-in supported major.
- **Per-slice check:** the first JSONL event of every slice carries `schema_version`; the GUI validates its major (same-major = accept, per pnp's compatibility rule).
- `pnp_cli --version` (clap) exists but is informational only (shown in diagnostics/dialogs), not the gate.

### Failure UX

Missing exe, probe failure, or major mismatch → **slicing disabled + persistent error notification** naming the found vs. supported version (or "pnp_cli not found at <path>") with a pointer to the Preferences field. The rest of the GUI (modeling, arranging, preview of old G-code) stays usable. No blocking startup modal.
