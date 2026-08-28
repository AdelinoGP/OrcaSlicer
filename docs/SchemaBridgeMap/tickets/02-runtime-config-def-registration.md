---
title: Runtime registration of pnp module keys into print_config_def
status: open
type: grilling
assignee:
blocked-by: [01]
---

## Question

How does the fork inject a `ConfigOptionDef` per pnp module field into Orca's config core at
startup, given that `print_config_def` is an `extern const PrintConfigDef`
(`src/libslic3r/PrintConfig.hpp:702`) built as a global singleton?

Decisions this ticket must settle:

- **Mutation mechanism.** De-const the global, add a `PrintConfigDef::add_pnp_keys()` seam, or
  hold pnp defs in a subclass/second `ConfigDef` that `DynamicPrintConfig::def()` consults?
  Whichever is chosen must not fork every `def()` override (`PrintConfig.hpp:747`, `842`, `965`).
- **Ordering.** The schema probe lives in `GUI_App.cpp:928`. Registration must happen after the
  probe and before presets load — establish exactly where in startup that window is, and what
  happens on the paths that touch config *before* the GUI exists (CLI entry, 3mf load).
- **Serialization ordinals.** `by_serialization_key_ordinal` (`PrintConfig.hpp:2190`) assumes a
  fixed key set. Do dynamic keys get ordinals, or are they excluded from that path — and what
  breaks if excluded?
- **Preset key lists.** `Preset.cpp`'s static print/filament/printer option lists (the
  `pnp_bridge_fill_holder` precedent is `Preset.cpp:1058`) must gain the dynamic keys. Which
  list — i.e. which preset type does a generated pnp key belong to? (May defer the *routing* of
  that to ticket 04 but the *mechanism* is this ticket's.)
- **Type mapping.** pnp schema `type` (`bool`/`int`/`float`/`string`/`enum`, plus list forms
  implied by `min_list_length`/`max_list_length`) -> Orca `ConfigOptionType`. Where the mapping
  is not total, say so and decide the fallback.
- **Blast radius.** Diff, undo/redo, `ConfigManipulation`, and the modified-state colouring all
  read the def. Confirm each tolerates a key set that varies between runs.

Build the answer, per this map's execution discipline: one commit, compiles, plus a
`pnp_config_translator`/`pnp_runtime` test pinning that a synthetic schema doc produces
registered keys with the right types and defaults.

## Amended by ticket 01

The inventory found that `module config-schema` reports only **171 of pnp's 236 config keys**.
The other 65 come from three channels the probe never mentions: 62 host keys (`ResolvedConfig`'s
`cli`/`cli_opt` DSL rows, `FeedrateConfig`'s `read_speed` call sites, `[host_runtime]`), 2
extensions-map keys read by name (`support_type`, `support_family`), and 1 undeclared module read
(`infill_shift_step`). See [`assets/01-schema-key-inventory.md`](../assets/01-schema-key-inventory.md)
section E.

So this ticket must additionally settle **where the key universe comes from**. Registering only
schema keys leaves 62 host keys with no `ConfigOptionDef`, no preset persistence and no control.
The host keys also carry no type tag, display name, group or range — `docs/config/host-keys.toml`
documents a subset in prose only — so a `ConfigOptionDef` cannot be synthesised from them today.

Options, at least: hardcode a fork-side table for the host keys (a curated table by another name,
which the map's end state rejects); hand off to pnp to emit host keys from `config-schema` too;
or scope the generated page to schema keys and leave host keys unbound, stating that as a limit.
Pick one explicitly — the map's "zero fork edits" promise depends on the answer.
