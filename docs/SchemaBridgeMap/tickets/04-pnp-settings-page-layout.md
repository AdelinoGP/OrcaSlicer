---
title: PNP settings page layout and control generation
status: closed
type: prototype
assignee: Adelino Penedo
blocked-by: [01, 02]
---

## Question

What does the generated PNP settings page look like, and what does it build for each schema
field type?

Use `/prototype` — a rough page against ticket 01's real "new to PNP" key list is worth more
than a description.

Decide:

- **Placement.** A new page inside Print Settings, a new top-level tab beside Print/Filament/
  Printer, or per-preset-type pages? Interacts with ticket 02's "which `Preset.cpp` list".
- **Grouping.** The schema carries both `module` (the owning module id) and `group` (the field's
  declared group). Group by module, by `group`, or by `group` with module as a subtitle? What
  happens to a field whose `group` matches an existing Orca group name.
- **Control per type.** `bool` -> checkbox, `enum`+`values` -> dropdown, `float`/`int` with
  `min`/`max`/`step` -> spin/slider, `string` -> text, list types -> ? Say what `validate`
  (a validation expression) and `max_length` do to the control, or that they are ignored.
- **Labels and help.** `display` -> label, `description` -> tooltip, `unit` -> suffix. Decide
  what happens when `display` is absent (the wire contract says the fields are always present
  but may be empty) and whether these strings are translatable (they come from a binary at
  runtime — Orca's `.po` catalogs cannot cover them).
- **Mode gating.** Schema `advanced` -> Orca's `comSimple`/`comAdvanced`/`comExpert`. Two states
  into three.
- **The read-only banner state.** The map's locked degradation: pnp_cli missing or schema major
  mismatched -> the page renders read-only with an error banner, showing whatever the
  project/preset carries. Prototype that state too; it is the one a user hits first on a broken
  install.
- **Empty state.** No pnp-only keys at all (possible if pnp keeps renaming toward Orca names) —
  does the page hide, or show an explanation?

Deliverable: a prototype linked as an asset, plus the decisions above written into the
resolution. Implementation tickets graduate from the map's fog afterwards.

## Resolution (2026-08-28)

Prototype: [PNP settings page prototype](../assets/04-pnp-page-prototype.md) — the whole page
rendered against ticket 01's real key list, plus the degraded state.

**Control generation is not a problem this ticket has to solve.** Ticket 02's registry already
fills `label`/`category`/`tooltip`/`sidetext`/`min`/`max`/enum domain/default on each
`ConfigOptionDef` (`PnpConfigKeyRegistry.cpp:61-82`), and Orca's
`page->new_optgroup(g)->append_single_option_line(key)` builds the widget entirely from the def.
The generation site is one loop over `pnp_registered_config_keys()` bucketed by `def->category`.
The ticket's per-type control table is therefore answered by construction, not by fork code.

**Placement: one page in Print Settings.** Every key that reaches the page is print-scoped, so
the per-tab and top-level-tab options both solved a problem that does not exist. Rejected:
distributing keys into existing Orca pages by matching `group` names — it reads better, but the
group-to-page match is a fork-side table, which is the artifact this map exists to delete, and it
would put the degraded-state banner on five pages instead of one.

**Grouping: by schema `group`, no merging into Orca pages**, ordered by descending key count with
alphabetical ties. A `group` colliding with an Orca page name (`Support`, `Quality`, `Speed`,
`Walls`) stays an optgroup on the PNP page — one place to look, no mapping table.

**Mode: `advanced=false` -> `comAdvanced`, `advanced=true` -> `comExpert`.** Nothing generated
reaches Simple mode. Softens ticket 02's blanket `comExpert` placeholder for 30 of 34 schema keys;
host keys have no `advanced` flag and take `comAdvanced`.

**Labels are untranslated.** `display` -> label (key name when empty), `description` -> tooltip,
`unit` -> sidetext, none passed through `_L()`: they come from a binary at runtime and no `.po`
catalog can carry them. The PNP page is English regardless of UI language. `validate` and
`max_length` are explicitly ignored — Orca has no expression validator and no length-capped field.

**Host-key metadata is declared in pnp's DSL, beside `@scope`.** `docs/config/host-keys.toml`
covers only 11 of the 20 candidates; the other 9 (`arachne_min_feature_size`,
`fill_authored_coloring`, `mmu_segmented_region_interlocking_beam`, `nonplanar_amplitude`,
`nonplanar_max_angle_deg`, `nonplanar_shell_count`, `smoothificator_adaptive`,
`smoothificator_target_height`, `solid_infill_speed`) are real `cli`/`cli_opt` rows in
`resolved_config.rs` that the TOML never mirrored — the doc-lock test pins agreement, not
completeness. Annotating the declaration site instead closes that gap by construction and reuses
ticket 02's own argument for `scope`: a routing/display declaration belongs in the declaration,
not in a mirrored side-file. Rejected: promoting the TOML onto the wire (the completeness gap
reopens with every new key) and a fork-side metadata table.

**Degraded state renders ticket 03's carrier.** The map's locked rule — pnp_cli missing or schema
major-mismatched -> read-only page "showing whatever the project/preset carries" — could not be
implemented as written: with no probe, no keys register, and ticket 03 deliberately keeps
unresolvable keys as opaque JSON outside `DynamicConfig::options`. Resolved by giving the page a
second rendering path that reads `Model::pnp_unknown_config` / `Preset::pnp_unknown_config`
directly as a flat read-only key/value list under an error banner, using neither
`ConfigOptionDef` nor optgroups. This also answers the map's fog item on whether preserved-but-
unresolvable keys get a surface: they do, read-only, in the degraded state only.

**Empty state: the page is not added** when the probe succeeds, registers zero page keys, and the
carrier is empty — that state means every pnp key is already an Orca key, which is the end state
the map is future-proofing toward.

### Inventory corrections

- **`machine_max_speed_x/y/z/e` are Orca keys**, loop-built at `PrintConfig.cpp:4905` alongside
  the `machine_max_jerk_*` set ticket 02 caught at `:4941`. Section F's 28 host candidates is
  **20**. The only concatenated `add()` sites in `PrintConfig.cpp` are the three machine-limit
  loops, so the scrape is now closed and no further loop-built key can be hiding.
- **Every page key is print-scoped.** `module_field_scope()` is unconditional (`manifest.rs:1573`)
  and all 12 `@printer`/`@filament` host keys (`resolved_config.rs:1267-1458`) are Orca identity
  rows. The map's fog item "which tab a filament- or printer-scoped pnp key is edited on" is moot
  for the page: no such key reaches it.
- Page totals: **54 registered, 53 rendered** (`slice_has_paint` excluded).

### Bugs found

- **`support_sharp_tails` is erased on every load.** `PrintConfigDef::handle_legacy` tests its
  obsolete-key `ignore` set at `PrintConfig.cpp:8408`, *before* the `print_config_def.has()` test
  at `:8414`, so ticket 02's registration cannot rescue it and ticket 03's carrier does not catch
  it either — the carrier only records keys *absent* from the def, and this one is present. The
  control renders, the value saves, and reload silently resets it. Fix: test the pnp registry
  before the `ignore` set, so a key pnp actively declares is never treated as obsolete. This
  generalises to any future collision. Cross-checked the whole `ignore` list against ticket 01's
  inventory: `support_sharp_tails` is the only collision, and pnp's `support_remove_small_overhang`
  is the singular, distinct from Orca's ignored plural.
- **`slice_has_paint` must not get a control** (host-injected, `classic-perimeters.toml:156`) and
  the wire cannot say so — no tag, no hidden flag, only the words "(host-injected)" inside its
  `display` string. **Decided: the fork carries a hardcoded skip list.** This is knowingly the
  curated fork-side table the map exists to delete, taken because it is one line against one key
  today; the pnp-side alternative (a per-field `internal = true`, following ticket 02's `scope`
  precedent) is recorded in the map's fog and should be taken if a second host-injected field
  appears.

### Rough edges recorded, not fixed here

Enum domains have no display labels (dropdowns show raw identifiers);
`wave_overhang_pattern` and `flat_bridge_closing_join` are declared `string` with their domains
written in prose rather than as `enum` + `values`; several `display` strings are sentence-length
documentation that will overflow the label column; `support_overhang_angle` is the deprecated
alias of `support_threshold_angle` and is the name the manifest declares (ticket 09).

### Not done here

No code. This ticket decided the page; building it — the generation loop, the `TabPrint` wiring,
the degraded-state renderer, the `handle_legacy` fix, the skip list, and the pnp-side DSL
annotation commit — graduates as implementation tickets.
