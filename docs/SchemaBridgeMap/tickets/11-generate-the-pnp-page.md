---
title: Build the generated PNP settings page
status: in_progress
type: task
assignee: Adelino Penedo
blocked-by: [04, 10]
---

## Question

Build the page ticket 04 decided, per
[the prototype](../assets/04-pnp-page-prototype.md).

- One page in `TabPrint`, added after the existing pages via `add_options_page`.
- One loop over `pnp_registered_config_keys()`, bucketed by `def->category` (the schema `group`),
  optgroups ordered by descending key count with alphabetical ties, each key appended with
  `append_single_option_line`. No per-type control code: ticket 02's registry already fills the
  `ConfigOptionDef`.
- Mode: `advanced=false -> comAdvanced`, `advanced=true -> comExpert`. This replaces the blanket
  `comExpert` placeholder in `PnpConfigKeyRegistry.cpp:68`, so the registry must start carrying
  the schema's `advanced` flag through to `def->mode`.
- Skip list: `slice_has_paint` renders no control (host-injected). Knowingly a fork-side curated
  constant; see the map's fog entry on the pnp-side `internal` alternative.
- Labels/tooltips/sidetext are used verbatim, **not** passed through `_L()`.
- The page is not added at all when the probe succeeded, zero page keys registered, and the
  carrier is empty.

The degraded / read-only state is ticket 12, but decide here where the seam between the two
rendering paths sits, since both live on the same page.

Verification: the map's discipline — compile, targeted Catch2 in `pnp_config_translator` or
`pnp_runtime`, manual smoke of the page against a real `pnp_cli` probe.
