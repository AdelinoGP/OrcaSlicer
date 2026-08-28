---
title: Stop handle_legacy erasing registered pnp keys
status: open
type: task
assignee:
blocked-by: [02]
---

## Question

Bug found by ticket 04. `PrintConfigDef::handle_legacy` tests its obsolete-key `ignore` set at
`src/libslic3r/PrintConfig.cpp:8408`, **before** the `print_config_def.has(opt_key)` test at
`:8414`. `support_sharp_tails` is in that set, and is also a live pnp host key
(`docs/config/host-keys.toml` `[resolved_config]`, default `true`).

So the key registers (ticket 02), gets a control (ticket 04), saves to the preset and the 3mf —
and is silently cleared to its default on every load. Ticket 03's carrier does not catch it
either: the carrier records keys **absent** from `print_config_def`, and this one is present. It
falls through both nets.

Fix: consult the pnp registry before the `ignore` set — a key pnp actively declares is not
obsolete, whatever Orca's history says. This generalises to any future collision rather than
special-casing one name.

`support_sharp_tails` is the only collision today: the whole `ignore` list was cross-checked
against ticket 01's inventory, and pnp's `support_remove_small_overhang` is the singular form,
distinct from Orca's ignored `support_remove_small_overhangs`.

Verification: a regression test that round-trips `support_sharp_tails = false` through a preset
and a project 3mf and asserts it survives — it fails on today's ordering.
