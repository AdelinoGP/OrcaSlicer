# Ticket 04 — PNP settings page prototype

Rough page rendered against ticket 01 section D (34 schema keys) and section F as corrected
below (20 host keys). Not code: the point is to see the real key list in its real groups
before any of it is built.

## Corrections this prototype rests on

- **Section F is 20, not 28.** `machine_max_speed_x/y/z/e` are loop-built Orca keys
  (`PrintConfig.cpp:4905`), exactly like the `machine_max_jerk_*` set ticket 02 caught
  (`PrintConfig.cpp:4941`). All eight are identity-routed. The only concatenated `add()`
  sites in `PrintConfig.cpp` are those three machine-limit loops, so the scrape is closed.
- **Every page key is print-scoped.** `module_field_scope()` returns `"print"` unconditionally
  (`manifest.rs:1573`), and all 12 `@printer`/`@filament` host keys
  (`resolved_config.rs:1267-1458`) are Orca identity rows. No non-print key reaches the page.
- **`slice_has_paint` is excluded** (host-injected, `classic-perimeters.toml:156`).

Registered: 54. Rendered: **53**.

## The page

One page in Print Settings, optgroups from the schema `group`. `[H]` marks a host key —
metadata from the DSL annotation this ticket asks pnp to add; group assignments below are
this prototype's proposal, and are what that pnp commit should encode.

```
Print Settings  >  PNP Backend                                    [Expert]

  Support ......................................................... 9
    Support Overhang Angle              [ 30.0 ]  0-90        A
    Support Layer Height                [  0.0 ]  0-1         A
    Support Interface Flow              [ 100% ]  min 0       A
    Support Branch Merge Distance       [  0.8 ]  min 0       A
    Support Max Branches Per Layer      [ 1024 ]  1-10000     A
    Support Base Interface Layers       [    0 ]  0-10        A
    Base Raft Layers                    [    1 ]  0-20        A
    Interface Raft Layers               [    0 ]  0-20        A
    Support sharp tails             [H] [x]                   A

  Wave Overhangs .................................................. 10
    Wave Overhang Pattern               [ smart          v ]  A   (*)
    Wave Overhang Print Speed           [  2.0 ]  0.1-300     A
    Wave Overhang Flow (mm3/mm)         [ 0.15 ]  0.02-1.5    A
    Wave Overhang Line Spacing          [ 0.35 ]  0.01-5      A
    Wave Overhang Minimum Width         [  0.7 ]  0-10        A
    Wave Overhang Minimum Length        [  0.0 ]  0-100       A
    Wave Overhang Minimum New Area      [ 0.01 ]  0-10        A
    Wave Overhang Perimeter Overlap     [  0.1 ]  0-5         A
    Wave Overhang Anchor Depth (mm)     [  0.0 ]  0-20        A
    Wave Overhang Max Iterations        [    0 ]  0-500       A

  Speed ........................................................... 5
    Thin Wall Speed                     [ 30.0 ]              A
    Solid infill speed              [H] [ 50.0 ]  min 0       A
    Bottom surface speed            [H] [100.0 ]  min 0       A
    Prime tower speed               [H] [ 90.0 ]  min 0       A
    Wipe tower speed                [H] [ 90.0 ]  min 0       A

  Quality ......................................................... 7
    Run gap-fill/thin-wall medial axis on painted slices  [ ]  A
    Perimeter arc tolerance (mm)        [0.0125]  0-1         A
    G-code resolution               [H] [0.0125]  min 0       A
    Infill resolution               [H] [ 0.04 ]  min 0       A
    Support resolution              [H] [0.0375]  min 0       A
    Minimum segment length          [H] [ 0.05 ]  min 0       A
    Flat bridge closing join        [H] [ miter        v ]    A   (*)

  Walls ........................................................... 4
    Extra perimeters (per-region bonus wall count)  [ 0 ]  0-10   A
    Narrow-island width threshold (mm)   [ 0.8 ]  0-10        A
    Minimum longest-dimension for narrow-island classification (mm)
                                         [10.0 ]  0-1000      A   (**)
    Smaller perimeter line width (narrow-island override, mm)
                                         [0.25 ]  0.05-2      A   (**)

  Arachne ......................................................... 5
    Maximum bead count                  [    0 ]  min 0       E
    Minimum central distance            [    0 ]  min 0  units E
    Minimum width                       [ 4000 ]  min 0  units E
    Outer wall offset                   [    0 ]  min 0  units E
    Arachne min feature size        [H] [  --  ]              E

  Nonplanar ....................................................... 3
    Nonplanar max angle (deg)       [H] [  --  ]              A
    Nonplanar shell count           [H] [  --  ]              A
    Nonplanar amplitude             [H] [  --  ]              A

  Smoothificator .................................................. 2
    Smoothificator adaptive         [H] [ ]                   A
    Smoothificator target height    [H] [  --  ]              A

  Output .......................................................... 2
    G-code XY decimals              [H] [    3 ]  1-6         A
    Thumbnail path                  [H] [        ]            A

  Infill .......................................................... 2
    Infill Overlap (xspacing)           [ 0.45 ]  0-1         A
    Fill authored coloring          [H] [        ]            A   (***)

  Seam ............................................................ 1
    Seam candidate sharp-corner angle threshold (degrees)
                                        [ 30.0 ]  0-180       A

  Travel & Retraction ............................................. 1
    Retraction Mode                     [ gcode          v ]  A   (*)

  Path Optimization ............................................... 1
    Emit Layer Markers                  [x]                   A

  Multimaterial ................................................... 1
    MMU segmented region interlocking beam  [H] [ ]           A
```

`A` = comAdvanced, `E` = comExpert. `--` = default is `None` on the pnp side, so the control
opens at the type's zero value.

## Decisions the prototype encodes

**Control per type.** Nothing to build: `page->new_optgroup(group)->append_single_option_line(key)`
constructs the widget entirely from the `ConfigOptionDef`, and ticket 02's registry
(`PnpConfigKeyRegistry.cpp:61-82`) already fills `label`, `category`, `tooltip`, `sidetext`,
`min`, `max`, `enum_values`/`enum_labels`/`enum_keys_map` and the default. bool -> checkbox,
enum -> dropdown, numeric -> field with min/max clamping, string -> text field, list -> Orca's
list editor. The generation site is a loop over `pnp_registered_config_keys()` bucketed by
`def->category`.

**Grouping.** By the schema `group`, in descending key count, ties broken alphabetically —
a stable order that needs no fork-side list. A group whose name matches an existing Orca page
(`Support`, `Quality`, `Speed`, `Walls`) is **not** merged into that page: it stays an optgroup
on the PNP page. The PNP page is the one place backend-declared settings live, so a user always
knows where to look, and no fork-side group-to-page mapping table exists to drift.

**Mode gating.** `advanced=false` -> `comAdvanced`, `advanced=true` -> `comExpert`. Nothing
generated appears in Simple mode: the page is uncurated by construction, and Simple mode is
Orca's promise of a small reviewed set. This replaces ticket 02's blanket `comExpert` placeholder
for the 30 non-advanced schema keys. Host keys have no `advanced` flag and take `comAdvanced`.

**Labels and help.** `display` -> label, falling back to the key name when empty;
`description` -> tooltip; `unit` -> sidetext. **These strings are not translated.** They arrive
from a binary at runtime, so Orca's `.po` catalogs cannot carry them and they are not passed
through `_L()` — the PNP page renders in English whatever the UI language. Group names come
from the same source and are equally untranslated. Accepted: translating a surface whose
contents change with the installed backend is not something the catalog workflow can express.

**`validate` and `max_length` are ignored.** Orca's `OptionsGroup` has no expression validator
and no length-capped text field; `min`/`max` clamping covers the numeric cases, and no current
key sets `max_length`. Recorded rather than silently dropped.

**Empty state.** If the probe succeeds and registers zero page keys, and the carrier is empty,
the page is **not added** — an explanation page for a state that means "everything pnp needs is
already an Orca key" is noise, and that is the end state the map is future-proofing toward.

**Read-only / banner state.** pnp_cli missing or `schema_version` major-mismatched: no keys
register, so the page cannot render optgroups at all. It instead renders the ticket 03 carrier
(`Model::pnp_unknown_config` / `Preset::pnp_unknown_config`) as a flat read-only key/value list
under an error banner — a second rendering path that uses neither `ConfigOptionDef` nor
optgroups:

```
Print Settings  >  PNP Backend

  +----------------------------------------------------------+
  | (!) pnp_cli not found - settings shown read-only.         |
  |     These values are preserved and written back           |
  |     unchanged when the project is saved.                  |
  +----------------------------------------------------------+

    nonplanar_amplitude                              0.35
    wave_overhang_pattern                            smart
    support_layer_height_mm                          0.2
```

This is also the surface the map's fog asks for under "whether preserved-but-unresolvable keys
get a surface of their own" — they get one, read-only, in the degraded state only.

## Rough edges this prototype exposes

- `(*)` **Enum values have no display labels.** The wire sends `values: [gcode, firmware]` and
  the registry sets `enum_labels = enum_values`, so dropdowns show raw lowercase identifiers.
  A `value_labels` array parallel to `values` would fix it; not blocking.
- `(*)` **`wave_overhang_pattern` and `flat_bridge_closing_join` are declared `string`, not
  `enum`,** with their domains written into prose — `"Wave Overhang Pattern (smart, monotonic,
  zigzag)"` and the `flat_bridge_closing_join` note in `host-keys.toml`. They render as free-text
  fields that accept any string. Both should be `enum` with `values`; a pnp-side fix.
- `(**)` **Some `display` strings are sentence-length**, written as documentation rather than as
  control labels. They overflow Orca's label column. A pnp-side editorial pass, not a fork issue.
- `(***)` **`fill_authored_coloring` is a `Vec<String>`** rendered as Orca's string-list editor.
  Whether a colour list belongs on a settings page at all is unexamined.
- **`support_overhang_angle` is the legacy alias** of `support_threshold_angle`
  (`host-keys.toml` `[resolved_config]`), and it is the one the module manifest declares. The
  page would show the deprecated name. Ticket 09's territory.
