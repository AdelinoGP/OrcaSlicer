---
title: Preserve unknown pnp keys through preset and 3mf load
status: open
type: grilling
assignee:
blocked-by: [02]
---

## Question

A preset or project 3mf carries pnp module keys for a module the current install does not have.
The map's locked decision is **preserve untouched, round-trip on save, and warn once on load**.
How?

The obstacle is known and load-bearing: `load_from_json` fails the **whole** project config on
an unknown key — unlike the ini/gcode paths, which ignore per-key. BootstrapMap ticket 008 hit
exactly this with SLA and worked around it with explicit up-front detection.

Settle:

- **Where the unknown keys are held** between load and save, given ticket 02's registry answer.
  A side map on the config? A tolerant-load mode that collects rather than throws?
- **Which load paths** need the behaviour: project 3mf, `.json` preset, `.ini` preset, G-code
  config block. They do not currently agree with each other on unknown-key handling.
- **What "unresolvable" means precisely** — a key absent from the live schema is not the same as
  a key whose *module* is absent; decide whether the warning can name modules at all (the
  3mf/preset stores keys, not module ids) or must name keys.
- **The notification.** One per load, using the fork's existing warning-notification surface
  (`PnpSlicingProcess`'s `WarningNotificationLevel` path, `format_pnp_config_warning_message()`).
  Decide whether it reuses that formatter or needs its own.
- **Save-side.** Confirm the preserved keys are written back byte-equivalent, and that they do
  not leak into `ConfigBase::diff()` results and mark a pristine preset as modified.

Test: a 3mf fixture carrying a key no schema declares opens, warns, and re-saves with the key
intact.
