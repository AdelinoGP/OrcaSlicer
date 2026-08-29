---
title: Stop handle_legacy erasing registered pnp keys
status: closed
type: task
assignee: Adelino Penedo
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


## Resolution (2026-08-29)

Fixed, in the direction the ticket proposed: inside `handle_legacy`'s obsolete-key
branch, a key this fork registered is returned to the caller untouched, so the
existing `print_config_def.has(opt_key)` test -- which now passes -- lets the
value deserialize. The change is confined to the ignore branch of
`PrintConfigDef::handle_legacy` (src/libslic3r/PrintConfig.cpp); the
`print_config_def.has()` test below it is untouched.

**What was found beyond the ticket's premise:**

- The ticket proposed consulting "the pnp registry before the `ignore` set". The
  registry lives in `src/libslic3r/PnpConfigKeyRegistry.cpp` (ticket 02's sealed
  seam) and exposes exactly the two predicates needed: `pnp_config_keys_sealed()`
  and `pnp_registered_config_keys()`. The exemption runs only when the seal is
  set and only for names that registration actually added -- an identity-routed
  pnp key never reaches this branch, because a stock def already exists and the
  fact that `has()` wins today for those is exactly right.
- **A bare reorder of the two tests was rejected**, and the ticket's own
  verification now documents why: the ignore set is not disjoint from
  `print_config_def`. `silent_mode` (PrintConfig.cpp `add` at :4831, consumed by
  `GCodeProcessor` and `Tab`, listed in `Preset.cpp:1416`) and
  `tree_support_with_infill` (:7064) sit in the ignore set **and** have live
  stock defs -- an upstream OrcaSlicer quirk inherited with the fork. A reorder
  would have silently un-obsoleted both, changing stock behaviour for keys pnp
  never declares. The exemption is keyed on *this fork's registration*, not on
  def-presence, precisely so those keys keep dropping.
- Cross-checked every name in the ignore set against the backend at submodule
  `263b81d3`: `support_sharp_tails` is still the only collision (claimed above
  and re-verified). The registration path was also traced: the schema fixture's
  host half reaches `pnp_register_config_keys()` and registers the collision key
  (a pnp bool true -> Orca `ConfigOptionBool` default 1), so the collision is
  live in every probed GUI process, headless CLI excepted.
- The staged-dist caveat from ticket 06 applies here too: registration follows
  the probed wire, so the fix's benefit is active only when `pnp_cli` is staged
  and probes successfully. Without a probe, nothing is registered and stock
  Orca behaviour is byte-identical (verified by the negative test).

**Verification:** four new regression cases in
`tests/pnp/test_pnp_config_keys.cpp` (the registration-suite binary, since the
one-shot registration must precede them in-process):

1. seam: `set_deserialize("support_sharp_tails", "0")` stores `false` and leaves
   the key out of `unrecogized_keys`;
2. storage: the value round-trips through a real `Preset::save()` -> preset json
   -> `load_from_json` (the `PresetCollection::load_presets` path), landing back
   in `preset.config` with `pnp_unknown_config` empty;
3. storage: the value round-trips through `store_bbs_3mf()` -> project 3mf ->
   `load_bbs_3mf()` with the carrier empty and `layer_height` intact;
4. negative (guards the fix's shape): `support_remove_small_overhangs` -- in the
   ignore set, never declared by pnp -- still drops on both surfaces, so the
   fix cannot be read as licence to reorder the tests.

Red proof (regression bite): with the new exemption disabled
(`&& false` on the seal check) the three positive cases each fail with
`REQUIRE(opt != nullptr)` -> `nullptr` -- the silent drop made visible; the
negative case still passes, as it must. Re-enabling the exemption turns all
969 assertions of the suite green.
`libslic3r_tests` (48,771 assertions, includes ticket 03's drop-and-record
behaviour) and `pnp_runtime_tests` (282 assertions) pass unchanged on the same
tree.
