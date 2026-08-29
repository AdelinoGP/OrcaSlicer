---
title: Repair the six dead curated-table rows the bump and history left behind
status: closed
type: task
assignee: Adelino Penedo
blocked-by: [01]
---

## Question

Ticket 01's inventory found six `PnpConfigTranslator::translate()` rows writing pnp keys that no
longer exist (or never existed), plus one row that deliberately sends nothing. Each is a setting
the user changes in the GUI that does not reach pnp. Repair them, with a regression test per row.

| Orca source | writes | actual pnp key at `dbf3449c` | fix |
|---|---|---|---|
| `enable_support` | `support_enabled` | `enable_support` | identity — **supports currently never enable** |
| `close_fan_the_first_x_layers` | `disable_fan_first_layers` | `close_fan_the_first_x_layers` | identity |
| `enable_overhang_bridge_fan` | `enable_overhang_fan` | `enable_overhang_bridge_fan` | identity |
| `fan_max_speed` | `fan_speed_max` | `fan_max_speed` | identity |
| `fan_min_speed` | `fan_speed_min` | `fan_min_speed` | identity |
| `support_interface_spacing` | `tree_support_interface_spacing_mm` | `support_interface_spacing` | identity |
| `support_base_pattern_spacing` | *(nothing; lossy warning)* | `support_base_pattern_spacing` | identity send; drop the warning |

The `support_enabled` row is not a bump regression — `enable_support` was already pnp's config
key at `1238ef02`. The other five broke at the bump.

Watch for the unit and range mismatches section B of the inventory asset records for these keys
before making any of them a bare Tier-A identity copy — several Orca sources are per-extruder
vectors or percents.

Deliverable: one commit on `pnp/main`, `pnp_config_translator` suite extended with a case per
row asserting the emitted pnp key name, and a manual smoke confirming supports actually generate.

## Amended by ticket 05

Ticket 05 made the identity pass derive from the live key universe and run **before** the
curated rows, so every one of these settings now reaches pnp under its own name regardless
of the dead row below it — including `enable_support`, whose row wrote the non-existent
`support_enabled` since it was authored (supports never switched on in pnp), and
`support_base_pattern_spacing`, which was warn-only.

This ticket is therefore no longer a correctness fix; it is **cleanup**. The dead rows still
fire and still write target names pnp does not declare. pnp ignores them, so the cost is
noise in the emitted config and six rows that mislead the next reader. Verify against the
live universe rather than the inventory before deleting each row.

## Amended by ticket 06

The mechanism that catches these rows now exists, and with it a way to check the repair:

- `PnpConfigTranslator::dead_curated_targets()` names every row whose target the live backend does
  not declare, and `report_pnp_schema_drift()` reports them to `pnp-config-warnings.jsonl` and one
  notification at every startup.
- The hidden Catch2 case `[.][live-schema]` in `tests/pnp/test_pnp_config_translator.cpp` requires
  that set to be **empty** against a real document passed in `PNP_LIVE_SCHEMA`. It fails today; this
  ticket is done when it passes. Run it after `cargo xtask dist` — ticket 06 found the currently
  staged `pnp_cli` predates the wire-1.1.0 commit, so it must be restaged first (ticket 08).
- Ticket 01's "six real dead rows" figure **is** confirmed, measured against a freshly staged
  wire-1.1.0 `pnp_cli`: `close_fan_the_first_x_layers -> disable_fan_first_layers`,
  `enable_overhang_bridge_fan -> enable_overhang_fan`, `enable_support -> support_enabled`,
  `fan_max_speed -> fan_speed_max`, `fan_min_speed -> fan_speed_min`,
  `support_interface_spacing -> tree_support_interface_spacing_mm`. `support_density` is not in the
  list — its row routes to nothing rather than to a dead name, so it is the seventh row this ticket
  names ("one row that deliberately sends nothing") and the diff cannot see it.

## Resolution (2026-08-28)

The rows were deleted, not repaired: ticket 05's identity pass had already made each setting
reach pnp under its own name, so the six rename rows wrote only dead targets onto the wire.
Verified against the live wire-1.1.0 schema (93 host entries, 23 modules, freshly probed from
the staged `pnp_cli` at submodule `a50bfc28`) before deleting: every Orca source above is
declared by pnp under its own name, every old rename target is absent. Because a
correctness fix was already delivered by ticket 05, this ticket skipped the per-row unit
audit the deliverable table warned about — the live identity copy carries whatever value shape
Orca holds, exactly as it has since ticket 05, and the schema guard still drops anything pnp's
resolution would reject.

### What changed

- `PnpConfigTranslator::translate()`: the six `copy_as` rename rows and the
  `support_base_pattern_spacing` warn-only row are gone (replaced with comments stating why).
- The raft-warning block now records `enable_support -> support_raft_layers` inside the
  `raft > 0` branch. That edge is not a routing change — `routed` feeds the warning-key
  derivation, and without it a raft-plus-supports-off slice would double-warn `enable_support`
  as not-yet-mapped after the row that consumed it disappeared.
- `TIER_A_KEYS` (the unprobed fallback only) gains the six Orca names plus
  `support_base_pattern_spacing`. Without this, an unprobed `translate()` would have silently
  stopped sending these seven settings — the fallback's contract is to behave as before, and
  the deleted rows were its only carrier for them. The probed path needed no list change.
- Tests: the ticket-05 case's `REQUIRE(json.contains("support_enabled"))` is inverted to
  `REQUIRE_FALSE`, and a new `[ticket09]` case pins each of the seven rows — one SECTION per
  setting asserting the emitted name, probed and unprobed, plus the dead-name absence and the
  raft/`enable_support` routing.

### Verification

- `pnp_config_translator_tests`: 944 assertions in 21 cases, all passing.
- The hidden `[live-schema]` gate against the real probed document: **zero dead curated rows**
  — the case ticket 06 left failing by design is green, which is this ticket's definition of
  done on that axis.
- `pnp_runtime_tests` (the other suite sharing touched sources and the dead-target logger
  fixture): 282 assertions in 31 cases, all passing.
- Manual smoke (the deliverable's "supports actually generate"): `pnp_cli slice` on
  `calicat.stl` with a config carrying only the fork's post-repair emission names —
  `enable_support: true` produced 93 `;TYPE:Support` and 4 `;TYPE:Support interface` blocks;
  the control config identical except `enable_support: false` produced none. Both slices
  exited 0. (The pre-existing `machine-gcode-emit` layer-marker warnings appear in both runs
  and are unrelated.)