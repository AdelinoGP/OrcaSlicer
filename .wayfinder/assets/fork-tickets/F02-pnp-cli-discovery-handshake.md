---
title: pnp_cli discovery, Preferences override, schema handshake
status: open
batch: B1
blocked-by: []
files: [src/slic3r/GUI/PnpBackend.hpp, src/slic3r/GUI/PnpBackend.cpp, src/slic3r/GUI/Preferences.cpp, src/slic3r/GUI/GUI_App.cpp, src/slic3r/GUI/CMakeLists.txt]
---

## Goal

Locate and validate the PNP backend at startup, per
[ticket 009](../../tickets/009-pnp-cli-distribution-and-discovery.md).

## Decisions (do not re-open)

- Default location: `pnp_cli.exe` + `modules/` **flat next to orca-slicer.exe**
  (drop of `cargo xtask dist` output). Module dir is always `<pnp_cli dir>/modules`.
- Optional Preferences field "PNP CLI directory"; the override **wins when valid**, otherwise
  fall back to exe-dir.
- Handshake: startup `pnp_cli module config-schema` probe + per-slice JSONL `schema_version`
  check, gating on **semver major only** (pnp wire-contract rule).
- Probe/handshake failure → slicing disabled + one persistent notification
  (`NotificationManager`); GUI otherwise fully usable.

## Steps

1. `PnpBackend` singleton (or Plater-owned) holding resolved cli path, module dir, probed schema
   version, and an `available()` flag consulted before any slice starts.
2. Startup probe on `GUI_App` init (spawn `module config-schema`, parse version; boost::process,
   no window flags on Windows).
3. Preferences page entry (directory picker) persisted in `AppConfig`; re-probe on change.
4. Persistent notification on failure with the failing path in the message.

## Done / verify

- `libslic3r_gui` target builds clean.
- Manual: app launched with no pnp_cli present shows the persistent notification and disables
  slicing; with a staged `cargo xtask dist` drop next to the exe, `available()` is true.
