---
title: Render the degraded read-only PNP page from the unknown-key carrier
status: open
type: task
assignee:
blocked-by: [04, 11]
---

## Question

Implement the state a user on a broken install hits first.

When `pnp_cli` is missing or `schema_version` is major-mismatched, no keys register, so the page
has no `ConfigOptionDef`s and no optgroups to render. Ticket 04 resolved this by giving the page a
second rendering path that reads ticket 03's carrier — `Model::pnp_unknown_config` and
`Preset::pnp_unknown_config` — directly, as a flat read-only key/value list under an error banner
stating that the values are preserved and written back unchanged.

Decide and build:

- Which carrier wins when the project 3mf and the active preset both carry a fragment, and whether
  the list shows the union or just the effective one.
- Whether the banner distinguishes "pnp_cli not found" from "schema version mismatch" — ticket 01
  found `schema_version` static across a bump that added 39 and removed 6 keys, so the mismatch
  branch may be unreachable in practice and worth stating as such rather than implementing blind.
- Whether this path is reachable in any state other than degraded. Ticket 04 says no — preserved
  keys are otherwise log-only — but the map's fog asked whether they deserve a purge affordance,
  and this is the only surface that lists them.

Verification: a Catch2 case driving the renderer from a synthetic carrier, plus a manual smoke
with `pnp_cli` renamed out of the dist directory.
