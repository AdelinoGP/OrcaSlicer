---
title: Derive the handled-key set from the live schema
status: closed
type: task
assignee: Adelino Penedo
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

---

## Resolution (2026-08-28)

### The rule

"Handled" is no longer a list. It is derived, per Orca key:

```
handled(k) = k is itself a key the backend declares         (identity routing)
          or translate() routed k to at least one such key  (curated table)
```

Both halves read the same input: the **key universe**, the set of every config key the
live backend reads. `pnp_key_universe_from_schema()` builds it from both halves of the
wire — the per-module manifest fields *and* the `host` array ticket 02 added at wire
1.1.0. Ticket 01's amendment to this ticket ("14 curated rows route to host keys the
probe does not report") is therefore **discharged, not worked around**: the probe now
reports them. Measured against a live `pnp_cli` probe: 171 module keys + 93 host keys,
31 shared, **233 declared**.

### Provenance, not a parallel list

The second half of the rule needs the source→target edges, which existed only as control
flow inside `translate()`. `PnpTranslationResult` gains a `routed` map — every Orca key
the translator consumed, against the pnp keys it wrote for it — and every routing site
now declares its sources. `pnp_handled_keys()` runs the translator and reads that back.

This is the point of the ticket. The old static set was a second copy of knowledge that
lived in `translate()`, and ticket 01 caught it exactly where a second copy fails: at the
submodule bump five settings stopped reaching pnp and the static set went on calling them
implemented. The two can no longer disagree, because there is now one source. The
agreement test survives as an assertion of that, not as a guard against drift.

### Identity is derived too

`handled` claiming a key by name identity is only honest if `translate()` actually sends
it. So the Tier-A pass no longer walks `TIER_A_KEYS`; it copies **every** Orca key the
universe declares. This is what makes the map's "zero fork edits" claim true: a key pnp
starts declaring is tinted *and sent* with no fork change.

It runs **first**, before the Tier-B rows, so the rows that fix a unit or respell an enum
(`ironing_flow`, the eight float-or-percent line widths, the bead widths, `wall_generator`,
`wall_sequence`) still win. Running it last would clobber them; there is a test for that.

`TIER_A_KEYS` survives only as the **unprobed fallback**, so a `translate()` with no
schema behaves exactly as it did before this ticket. It is explicitly no longer kept in
sync with the backend.

### Bugs this repairs for free

Because identity runs first and writes the live name, the dead curated rows below it no
longer matter to whether the setting arrives:

- The four part-cooling keys (`close_fan_the_first_x_layers`, `enable_overhang_bridge_fan`,
  `fan_max_speed`, `fan_min_speed`) and `support_interface_spacing` — ticket 01 finding 3's
  "five settings silently stopped reaching pnp at the bump".
- **`enable_support`** — ticket 01 finding 2. The curated row has written the non-existent
  `support_enabled` since it was authored, so turning supports on in the GUI never turned
  them on in pnp. It now arrives under the name pnp reads.
- `support_base_pattern_spacing`, which was warn-only because its old target
  `support_density` was retired; `traditional-support` declares it directly now.

The dead rows still fire and still write their dead target names — repairing them stays
**ticket 09**, which is now a cleanup rather than a correctness fix. pnp ignores keys it
does not declare.

### Measured effect

Against the live probe (`pnp_cli module config-schema`, submodule `a50bfc28`), over
`DynamicPrintConfig::full_print_config()` (665 keys — a smaller denominator than ticket
01's 845, which scraped `add()` sites in `PrintConfig.cpp`):

| | before | after |
|---|---|---|
| key universe | — | 236 |
| Orca keys handled | 117 | **176** |
| keys emitted by `translate()` | 115 | **180** |
| keys that stopped being handled | — | **0** |

Nothing regressed: no key that counted as handled before does not now.

### The two questions the ticket left open

**Tint with no schema → tint everything.** Without a backend nothing reaches pnp, so the
statement is literally true, and the broken install is visible on every settings tab
rather than only on the PNP page's error banner (ticket 12).

**`pnp_pattern_value_supported()` stays hand-written.** Checked against the live wire: a
`schema` entry carries only `module` and `fields`. Which module holds the `claim:sparse-fill`
role is not on the wire in any form, so this is not derivable today. Handoff item recorded
in the map's fog.

### Keys the wire cannot describe

`support_type`, `support_family` (read from `resolved_config.extensions`) and
`infill_shift_step` (a bare `config.get()` in rectilinear-infill) are genuinely read but
declared through no channel `config-schema` can express — ticket 01 finding 7. They are
carried in a three-entry `UNDECLARED_LIVE_KEYS` array, knowingly the curated-table pattern
this map exists to delete. Added only to a non-empty universe, so "no evidence" stays
distinguishable from "a backend declaring only these three". The pnp-side fix is a handoff
item.

### Risk, and what contains it

The identity pass widens what is sent by 65 keys, and ticket 01 §B found 86 of the 113
identity rows carry a type, unit or range mismatch against Orca's definition.
`apply_schema_guard()` is the net: it drops any value pnp's config resolution would reject
and logs a lossy-fallback warning, so a mismatch degrades to a warning rather than a bad
slice. It ran on the slice path only; this ticket also applies it to the **support-preview
/ gizmo** path (`GLGizmoFdmSupports.cpp`), which was emitting unguarded config.

The guard's coverage is not total — host keys reach the wire with no `min`/`max`, so range
errors on those 62 keys pass through. That is ticket 10's metadata work, and a systematic
audit of the 86 mismatched rows is recorded in the map's fog.

### Ordering

`register_from_schema()` (ticket 02) now calls `translate(defaults, nullptr)` explicitly:
it needs the *curated table's own* targets to decide what to register, and with a universe
installed the identity pass would return the whole universe, leaving nothing to register.
`GUI_App` installs the universe immediately after registration, both before the
`PresetBundle`.

### Verification

`pnp_config_translator` suite green — 898 assertions, 17 cases, including six new
`[ticket05]` cases: universe parsing from both wire halves, the old-wire and
empty-document cases, identity vs curated-target vs dead-target handling, the no-probe
tint, the identity repairs, and the Tier-B-wins ordering. The measured table above came
from a temporary instrumented case run against the live probe, since the suite cannot
depend on a built `pnp_cli`. **Not manually smoke-tested in the running GUI** — the amber
tint and the widened slice config have not been seen on screen.
