---
title: Schema-to-table-to-print_config_def reconciliation inventory
status: closed
type: research
assignee: Adelino Penedo
blocked-by: []
---

## Question

What is the actual current correspondence between the three key universes this map bridges?

Produce an inventory asset by running the submodule's `pnp_cli module config-schema
--module-dir <dist modules>` at `dbf3449c` and diffing it against (a) the curated table in
`src/slic3r/GUI/PnpConfigTranslator.cpp` (`TIER_A_KEYS` + the hand-written rename/remap rows)
and (b) `print_config_def` (`src/libslic3r/PrintConfig.cpp`). Classify every pnp schema key
into the map's routing rule:

- **Identity** — the key string already exists in `print_config_def`, so it *is* the Orca
  setting (the case pnp's "rename part-cooling keys to Orca names" work created). Flag any
  identity match that is a **false positive**: same name, different meaning, unit, or enum
  domain. These are the dangerous rows.
- **Table-routed** — no name match, but the curated table has a rename or value-remap row.
- **New to PNP** — neither; a PNP-page candidate. Record its schema metadata
  (`type/default/min/max/step/display/description/group/unit/advanced/values/tags`) so
  ticket 04 knows what controls it must build.

Then the reverse direction:

- **Dead table targets** — curated-table rows pointing at pnp keys the live schema no longer
  declares (known: `support_density`, retired this bump for `support_base_pattern_spacing`).
- **Delta of the bump** — which of the above changed between `1238ef02` and `dbf3449c`, so the
  feature-parity work has a precise list rather than a reading of the commit log.

Also record how many of Orca's ~925 keys are currently bound by pnp at all — the baseline the
"amber tint recedes automatically" claim will be measured against.

Deliverable: `assets/01-schema-key-inventory.md` with the tables above. No code changes.

## Resolution

Asset: [`assets/01-schema-key-inventory.md`](../assets/01-schema-key-inventory.md).

**The inventory could not be run as written.** The staged dist was built 2026-08-11 and its
schema is byte-identical to `1238ef02`'s manifests. Rebuilding with `cargo xtask dist` revealed
that `dbf3449c` stages to `target/dist/<edition>/`, not the flat `target/dist/` the fork bundles
from — so the fork ships a stale backend. Ticket 08 filed.

Against a real `dbf3449c` probe (23 modules, `schema_version` 1.0.0):

- **171** module-schema keys — **113** identity with `print_config_def` (86 of them carrying at
  least one type/unit/range/enum mismatch that needs an explicit decision), **24** table-routed,
  **34** new to PNP.
- **62** further pnp keys exist that `config-schema` never reports. pnp resolves config through
  four channels; the probe covers one. **The map's premise that `config-schema` is the authority
  on what pnp binds is false as stated** — it covers 171 of 236 keys. Tickets 02, 05 and 06 were
  amended accordingly; each was written assuming the probe was complete.
- **6 genuinely dead curated rows**, each a setting that does not reach pnp. The worst:
  `enable_support` is written as `support_enabled`, so **supports never enable** — a bug that
  predates the bump. Four more broke when pnp renamed its part-cooling keys to Orca's names, and
  `tree_support_interface_spacing_mm` was replaced by identity `support_interface_spacing`.
  Ticket 09 filed to repair them.
- Two apparent dead rows are false alarms: `support_type` (read from `resolved_config.extensions`)
  and `infill_shift_step` (read via `config.get()` in rectilinear-infill) are consumed but
  declared nowhere.
- **`schema_version` did not move** across a module split, 39 added and 6 removed manifest keys.
  The map's "major mismatch → read-only" degradation rule cannot detect drift of this kind.
- **Baseline: 123 of 845 Orca keys are in `pnp_handled_keys()`; 116 actually reach pnp (13.7%).**
  (845 is the measured count of `this->add("…")` sites in `PrintConfig.cpp`; the map's "~925" is
  not reproduced by that method.)
- **62 PNP-page candidates** for ticket 04 — 34 schema keys with full metadata, 28 host keys with
  none.
- Bump delta recorded in full: module set 21 → 23 (`support-planner` split into tree/traditional
  planners, `wave-overhangs` added), 39 manifest keys added / 6 removed, 13 host keys added / 1
  moved. New unbound key: `support_family`, the canonical family selector.

No code changes, as specified.
