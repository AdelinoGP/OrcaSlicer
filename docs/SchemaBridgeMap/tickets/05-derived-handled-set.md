---
title: Derive the handled-key set from the live schema
status: open
type: task
assignee:
blocked-by: [01, 02]
---

## Question

Replace the static handled-key set with one derived from the live `module config-schema` doc, so
the GUI's picture of what pnp binds updates with no fork edit.

Today `PnpConfigTranslator::pnp_handled_keys()` is built from the `TIER_A_KEYS` array plus the
hand-written rows, and `pnp_key_is_unimplemented()` drives the amber label tint in
`Tab::update_label_colours()`/`Tab::decorate()`. After this ticket, "handled" means: the Orca key
is an identity match for a key the live schema declares, **or** a curated table row routes it to
one. The curated table survives — it still owns renames and value remaps — but it stops being the
authority on *handled-ness*.

Work:

- Thread the cached schema doc (`PnpBackend`'s raw JSON) into the translator's handled-set
  computation, replacing the static set. Keep the translator **pure** — schema in as an argument,
  no I/O — per BootstrapMap ticket 013.
- Decide what `pnp_key_is_unimplemented()` returns when there is **no** schema (probe failed).
  The map's degradation decision covers the PNP page; the tint needs its own answer. Tinting
  every key amber on a broken install is one option; tinting none is another.
- Update the Tier-D warning classification to match: `not-yet-mapped` now means "the live schema
  declares no binding", which is a stronger and more honest statement than "absent from our
  table".
- Keep `pnp_pattern_value_supported()` / `pnp_pattern_key()` working — value-level support (which
  infill module holds a fill-role claim) is not expressible in the config schema, so decide
  explicitly whether it stays hand-written or needs a pnp-side wire addition (a handoff item).
- Existing agreement tests between the handled set and `translate()` must be re-pointed at a
  fixture schema doc rather than the static array.

One commit, compiles, `pnp_config_translator` suite green with the reworked agreement test.

## Amended by ticket 01

"Handled" cannot be defined as membership in the `config-schema` doc. **14 curated-table rows
route to host keys that the probe does not report** (`travel_speed`, `top_fill_holder`,
`use_relative_e_distances`, `wall_generator`, the initial-layer speeds, …). A schema-only handled
set would tint every one of them amber even though they work today. See
[`assets/01-schema-key-inventory.md`](../assets/01-schema-key-inventory.md) sections E and G.

Whatever ticket 02 settles as the key universe is the input here; this ticket must consume that,
not the raw probe. Baseline to measure against: **123 of 845 Orca keys are in the handled set
today, of which 116 actually reach pnp** — the seven-key gap being the dead rows ticket 09 fixes.
