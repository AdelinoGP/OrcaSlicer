---
title: Startup drift reconciliation against the live schema
status: open
type: task
assignee:
blocked-by: [05]
---

## Question

Make the fork notice when pnp moves out from under the curated table.

On each successful schema probe, diff every curated-table **target** (the pnp-side key each
rename/remap row writes) against the keys the live schema declares. A target the schema no longer
declares is a dead row: the Orca setting silently stops reaching pnp. This bump already produced
one — `support_density`, retired for `support_base_pattern_spacing`.

Work:

- Compute the dead-target set at probe time and record it to the existing
  `<data_dir>/pnp-config-warnings.jsonl` sink (its own record shape — this is a fork-health
  event, not a per-slice config warning) plus one line in Orca's log.
- Raise **one** notification naming the dead targets. This is a developer-facing signal, not an
  end-user one; decide the level and wording accordingly.
- Decide the inverse direction: schema keys that are neither identity matches nor table targets
  are, by the map's routing rule, PNP-page keys — so they are *expected*, not drift. Confirm the
  reconciliation does not report them.
- Decide whether this also runs as a **build-time** check against the pinned submodule. The
  grilling chose runtime reconciliation; a test would additionally make drift uncommittable. If
  taken, it belongs with the submodule's staged `pnp_cli`, and it must not make the build depend
  on a cargo step that may not have run.

One commit, compiles, test covering a fixture schema missing a known table target.

## Amended by ticket 01

Two corrections from the inventory
([`assets/01-schema-key-inventory.md`](../assets/01-schema-key-inventory.md)):

- **Diff against the union, not the schema.** A schema-only dead-target check reports 22 dead
  rows at `dbf3449c`; only 6 are real. 14 resolve through host keys the probe never reports, and
  2 (`support_type`, `infill_shift_step`) are consumed through the extensions map and an
  undeclared `config.get()` respectively. Both classes must be exempt or the notification is
  noise from day one.
- **`schema_version` is not a drift signal.** It is `1.0.0` at both `1238ef02` and `dbf3449c`,
  across a module split, 39 added manifest keys and 6 removed ones — five of which broke live
  translator rows. The per-key diff is the only thing that catches this, which strengthens the
  case for the build-time check this ticket already lists as optional.

The six real dead rows are ticket 09's to repair; this ticket is the mechanism that would have
caught them.
