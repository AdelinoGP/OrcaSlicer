---
title: Repair the six dead curated-table rows the bump and history left behind
status: open
type: task
assignee:
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
