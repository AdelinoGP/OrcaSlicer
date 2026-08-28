---
title: PNP settings page layout and control generation
status: open
type: prototype
assignee:
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
