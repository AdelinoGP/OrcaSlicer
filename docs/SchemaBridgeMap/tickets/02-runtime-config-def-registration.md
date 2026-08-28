---
title: Runtime registration of pnp module keys into print_config_def
status: closed
type: grilling
assignee: Adelino Penedo
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

## Resolution (2026-08-28)

**The key universe comes from pnp.** `module config-schema` now reports the whole
universe, not just module manifests. Wire version `1.0.0 -> 1.1.0`, additive: a new
top-level `host` array of `{key, type, default, scope}`, plus a `scope` field on every
module field. The fork holds no host-key table — the map's "zero fork edits" promise
survives. Rejected: a fork-side host-key table (the curated table under a new name), and
scoping the page to schema keys only (leaves 28 pnp-only host keys permanently unreachable).

pnp-side commit (submodule, on top of `dbf3449c`):
- `crates/slicer-ir/src/resolved_config.rs` — `declare_resolved_config!` now also emits
  `ResolvedConfig::host_config_keys()`. Wire type comes from the declared Rust type via
  a `HostWireField` trait (an unmapped type is a compile error, not a silent omission);
  the default is read off the live `Default` impl, so the wire cannot disagree with the
  value the slicer uses. New `cli @<scope>` / `cli_opt @<scope>` DSL forms declare a
  non-print preset scope; 12 rows use them (10 printer, 2 filament).
- `crates/slicer-ir/src/feedrate.rs` — `FeedrateConfig::from_raw_config` is now driven by
  a `SPEED_KEYS` table instead of 26 hand-written `read_speed` calls. That table is both
  the reader and the schema source, so a speed the slicer reads and a speed the GUI can
  bind cannot diverge.
- `crates/slicer-scheduler/src/manifest.rs` — `build_config_schema_json` emits `host`
  (deduplicated, sorted) and per-field `scope`; `HOST_RUNTIME_KEYS` covers the three
  `[host_runtime]` keys.

**Module fields are print-scoped by declaration.** Scope initially rode on the free-form
`tags` vocabulary (`scope:printer`), which `docs/03_wit_and_manifest.md:1570` explicitly
forbids — tags must not be namespaced, and scope is a routing declaration, not UI taxonomy.
Corrected: `module_field_scope()` returns print unconditionally, which is true of all 171
manifest keys today. Scope only varies where it actually varies, in the host half. A module
that ever needs otherwise gets a real `scope` per-field manifest key, not a tag.

**Mutation mechanism: de-const the global, behind a one-shot sealed seam.**
`extern const PrintConfigDef print_config_def` is now non-const
(`src/libslic3r/PrintConfig.hpp:702`). Two facts made this the cheap option and made a
second `ConfigDef` unnecessary — `def()` overrides are untouched:
- `ConfigDef::options` is a `std::map` and `ConfigDef::add()` already assigns ordinals and
  maintains `by_serialization_key_ordinal`, so appending at runtime is pointer-safe and
  needs no new bookkeeping. `ConfigDef::add` is protected, so `PrintConfigDef::add_pnp_key`
  is the one sanctioned public seam.
- **Serialization ordinals are safe.** `serialization_key_ordinal` is consumed only by the
  in-memory undo/redo stack (`src/slic3r/Utils/UndoRedo.cpp:701`), never written to disk.
  Run-varying ordinals therefore cost nothing, provided the key set is frozen before the
  first snapshot — which the seal guarantees. Dynamic keys are *not* excluded from that path.

`pnp_register_config_keys()` (`src/libslic3r/PnpConfigKeyRegistry.cpp`) runs exactly once
and flips `pnp_config_keys_sealed()`; a second call logs an error and returns 0 rather than
aborting. Enum keys get a heap-owned `t_config_enum_values` that outlives the def, which is
what makes `ConfigOptionEnumGeneric`'s bare `keys_map` pointer legal.

**Ordering.** The probe moved from `GUI_App::post_init()` to `GUI_App::on_init_inner()`,
immediately before `preset_bundle = new PresetBundle()`. It ran *after* preset load before,
which the ticket's premise assumed was not the case. Only `show_failure_notification()`
stays in `post_init()`, since the notification manager does not exist that early.
CLI and headless paths do not probe and see the stock key set — including
`PrintAndCLIConfigDef`, which snapshots `print_config_def.options` at static-init time and
so never carries pnp keys.

**Preset key lists.** `Preset::append_pnp_options(scope, keys)` appends to
`s_Preset_print_options` / `_filament_` / `_printer_`. Ordering matters: `printer_options()`
memoises its combined list on first call, so registration must precede it — which it does.

**Type mapping is total.** bool/int/float/string/enum/percent/float_or_percent/float-list/
string-list map to coBool/coInt/coFloat/coString/coEnum/coPercent/coFloatOrPercent/coFloats/
coStrings. `ConfigOptionEnumGeneric` (`Config.hpp:2080`) takes a runtime `keys_map`, so a
schema-declared enum needs no compile-time C++ enum. Unmapped types and empty enum domains
are skipped with a recorded reason, never guessed at. pnp renders bools as `true`/`false`;
those are normalised to `1`/`0` for Orca's deserializer.

**Routing.** `register_from_schema()` derives the curated-table target set by running
`PnpConfigTranslator::translate()` over `DynamicPrintConfig::full_print_config()` and taking
the resulting JSON's keys, rather than restating the table. The exclusion set therefore
follows the table automatically as ticket 09 repairs rows. Order is name identity, then
curated table, then PNP page, as locked.

**Blast radius.** Diff, undo/redo, `ConfigManipulation` and modified-state colouring all read
the def and see one fixed key set per run, because the seal precedes every consumer.

### Inventory correction

`machine_max_jerk_x/y/z/e` are **Orca keys**, not pnp-only: `PrintConfig.cpp:4941` builds
them in a loop, which ticket 01's literal `this->add("...")` scrape could not see. They are
identity-routed, not PNP-page candidates. Section E's pnp-only count and the 28-candidate
figure in section F are correspondingly too high; the same caveat applies to any other
loop-built Orca key. `tests/pnp/test_pnp_config_keys.cpp::identity_keys_really_are_orca_keys`
pins this so it cannot regress.

### Verification

- `cargo test -p slicer-scheduler --lib` — 44 passed, including
  `config_schema_host_array_reports_every_host_declaration_channel` (one representative per
  declaration channel, type/default/scope, sorted and deduplicated) and
  `module_schema_fields_carry_a_preset_scope`.
- `cargo test -p slicer-ir`, `cargo test -p slicer-runtime` — pass (the feedrate refactor).
- `tests/pnp/test_pnp_config_keys.cpp`, new, in the `pnp_config_translator` suite: 14 cases /
  879 assertions, `--order rand`. Covers the type mapping, metadata carry-over, enum domains,
  host-key scope routing, both skip paths, cross-half dedup, a pre-1.1.0 reply, and — after
  registration — the def entry, the live enum map, the ordinal, preset-list placement per
  scope, a `DynamicPrintConfig` round-trip, and the seal.

### Not done here

Registered keys have `ConfigOptionDef`s and persist, but **no control renders yet** — no page
hosts them, and their mode is `comExpert` as a placeholder. That is ticket 04. Host keys carry
no display name, group or range, so ticket 04 still has to decide what a control for them
looks like; this ticket only guarantees they exist and round-trip.
