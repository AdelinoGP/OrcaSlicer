---
labels: [wayfinder:asset]
title: PNP prerequisites — IMEX, belt printers, plugin host
---

# PNP prerequisites for IMEX, belt printers and the plugin host

Source: [ticket 12](../../tickets/12-pnp-prerequisites.md). Decided 2026-11-27 in a grilling session.
This file is the artefact ticket 12 closes on: it specifies the backend work, it does not perform
it. No `pinch_n_print_cli/` commit belongs to this ticket.

The rule this asset exists to enforce: **a feature's GUI half does not merge until its acceptance
slice passes.** There is no "GUI now, backend later" state, because that is the dead control
[map rule 10](../../map.md#standing-rules-decided-2026-11-25-grilling) forbids. A prerequisite that
turns out impossible closes its feature ticket as **not portable**; it is never shipped behind a
disabled control.

## Decisions

| Id | Decision | Consequence |
|---|---|---|
| P3.1 | **Wasm-only, with HTML plugin pages.** No Python runtime. Pages/script/printerAgent/slicingPipeline are not ported as Python capabilities. | Ticket 11 ports discovery/install/list/audit and the page host; no CPython/pybind11 dependency. P3.1 and P3.4 record the dropped capabilities. |
| P3.2 | **The plugin ABI is the existing external-module stage contract.** v1 plugin stage is `PostPass::GCodePostProcess`. | A plugin can stamp, filter or insert G-code; it cannot turn geometry into toolpaths. Geometry-in plugins are explicitly out of v1. |
| P3.7 | **Bundle + SHA-256 audit; no signatures, no permission prompts.** | The Wasm sandbox and manifest validation are the trust boundary. A modified bundle is visible and refused, not silently run. |
| P3.5 | **Plugin pages are manifest-declared local assets** with a read-only JS bridge. | `[[pages]]` entries, local HTML/CSS/JS, strict CSP, one bridge call. Page-only plugins are deferred. |
| P1.0 | **IMEX active mode travels as per-plate metadata**, upstream-compatible. v1 supports identity physical→logical mapping only. | `imex_parallel_mode` + `imex_head_filament_map` in `model_settings.config`; a non-identity head map is a loud error, never a silent wrong filament. |
| P2.4 | **Apply `build_plate_tilt_x`.** | In belt mode the tilt is synced to the belt angle; a mismatch is an error. Refusing would break all three shipped belt profiles. |
| — | **Build order: belt → IMEX → plugin.** | Belt (10), then IMEX (09), then plugin (11). Belt's transform is self-contained and establishes the key-declaration pattern IMEX reuses. |
| — | **Fixtures live in pnp**, under `pinch_n_print_cli/.../tests/fixtures/`. | One copy; the pnp suite owns the assertions, the fork walkthrough loads the same files through the submodule. |

## Live facts (probed 2026-11-27)

Reproduce with:

```bash
./pinch_n_print_cli/target/dist/developer/pnp_cli.exe module config-schema
```

- `schema_version` is `1.3.0`; the reply carries **107 host keys** and **24 module schemas**, 131
  unique key names. **None** of the 13 IMEX/belt keys is declared, nor are `gcode_remap_x/y/z` or
  `printer_extruder_id`.
- The fork already declares all 13 keys ticket 12 lists: `PrintConfig.cpp:6548-6609` (IMEX) and
  `:7088-7136` (belt), and lists them in `Preset::printer_options()` (`Preset.cpp:1422-1424`).
  `gcode_remap_x/y/z` are absent from the fork's `PrintConfig.cpp` (the belt profiles and upstream
  define them), so they drop on load today. `printer_extruder_id` **is** defined in the fork
  (`PrintConfig.cpp:5945`) and listed in `Preset::printer_options()`; only pnp's host schema lacks
  it.
- **The guard premise in the ticket is stale.** `apply_schema_guard` leaves unknown keys alone —
  `src/slic3r/GUI/PnpConfigTranslator.cpp:1091`: "unknown to the schema: pnp ignores it, leave it
  alone". Declaring the keys is still required, but so pnp **consumes and validates** them, not so
  the guard passes them through. No guard change is needed.
- **Plate metadata is parsed but not stamped.** `ParsedSidecar.plate_metadata` exists
  (`crates/slicer-model-io/src/sidecar.rs`), and pnp's own docs say it "is parsed into the
  host-local result but is not currently stamped into `MeshIR`" (`docs/02_ir_schemas.md:446`).
  IMEX mode selection cannot work until that wire is connected.
- **All three shipped belt family profiles** (`Custom`, `IdeaFormer`, `Printcepts`
  `fdm_belt_common.json`) set `belt_printer=1`, `belt_slice_rotation=x`,
  `belt_slice_rotation_angle=45`, `build_plate_tilt_x=45` and
  `gcode_remap_x=rev_x`, `gcode_remap_y=pos_z`, `gcode_remap_z=pos_y`; IdeaFormer and Printcepts
  also set `printer_extruder_id`.
- **Multiple modules already run per stage.** `crates/slicer-runtime/src/postpass.rs:492` iterates
  every module in `PostPass::GCodePostProcess` sequentially, so a plugin chains with the built-in
  emitter without a scheduler change.
- **Module discovery already exists.** `--module-dir`, `--no-default-module-paths` and
  `SLICER_MODULE_PATH` are implemented (TASK-190), so a plugin directory is a UI concern, not a
  new search path.

## P0 — shared prerequisites

### P0.1 One declaration channel

Declare every new printer key in the **host registry** (`ResolvedConfig::host_config_keys()` in
`crates/slicer-ir/src/resolved_config.rs`), not in a module manifest. These are global printer
settings; a module manifest would make one module own a key every stage reads, and the host channel
is what `module config-schema`'s `host[]` array already exposes. The two IMEX plate-level keys are
the exception: they travel in `model_settings.config` plate metadata (P1.0), not the host schema.
The `printer_extruder_id` key is shared by IMEX and belt and is declared once.

Wire type vocabulary is the existing one: `bool`, `int`, `float`, `int-list`, `float-list`,
`string`, `string-list`, `enum`, `float_or_percent`. `int-list` is already accepted by
`slicer-config`'s ingestion and by the GUI's `apply_schema_guard`; only the host-declaration DSL
lacks a form for it, so add that form here (it is what `printer_extruder_id` needs). Scope is
`printer` for every host key in this asset.

### P0.2 Schema regression

Add a test that runs the same code path as `module config-schema` and asserts the presence, type,
default and scope of every key in P1.1 and P2.1. Written against today's schema it fails; it passes
when the prerequisite is done. It must not read the fork's `PrintConfig.cpp`; the pnp schema is the
authority for what pnp consumes.

### P0.3 No guard change

Do not change `apply_schema_guard`. The correction in "Live facts" means the guard is already
permissive; adding a drop-on-unknown rule would break the `thumbnails` passthrough
(`PnpSlicingProcess.cpp:316-320` injects it after the guard because it is deliberately absent from
the schema).

## P1 — IMEX / iXex (feeds ticket 09)

### P1.0 Carry the active mode as plate metadata

Two plate-level keys are needed and neither exists in the fork today:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `imex_parallel_mode` | string | `"primary"` | Name of the active row in `imex_mode_names`; `primary` means no parallel mode. |
| `imex_head_filament_map` | string | `""` | Per-plate physical-head → filament-slot map, 1-based; empty means the printer's default routing. |

Requirements:

1. The fork's 3MF plate writer emits both keys in `<plate>` metadata when non-default, matching
   upstream (`Format/bbs_3mf.cpp:8581-8586`). Ticket 09 adds the fork-side keys and writer.
2. pnp stamps `ParsedSidecar.plate_metadata` into the resolved global config **before** config
   resolution, so the plate keys above (and the printer keys from P1.1) are readable by the stage
   that needs them. Today the map is dropped (`docs/02_ir_schemas.md:446`).
3. Mode resolution follows upstream `find_imex_mode()`: first row whose name matches wins; a
   sibling array too short yields an empty field and `ragged`, never an out-of-range read; an
   unresolvable non-primary name warns and falls back to `primary`.
4. **v1 mapping scope:** identity physical→logical mapping only. A non-empty
   `imex_head_filament_map` whose entries are not the identity permutation is refused with an
   error naming the key. This is a backend refusal, not a UI gate; it prevents a silent
   wrong-filament print on the multi-material IMEX machines the fork cannot yet model.

### P1.1 Declare the printer keys

| Key | Wire type | Default | Bounds / values | Fork definition |
|---|---|---|---|---|
| `is_imex` | bool | `false` | | `PrintConfig.cpp:6548` |
| `imex_gantry_count` | int | `1` | 1..2 | `:6554` |
| `imex_tools_per_gantry` | int | `2` | 1..4 | `:6562` |
| `imex_tool_layout` | string (enum) | `front-left` | `front-left`, `front-right`, `rear-left`, `rear-right` | `:6570` |
| `imex_nozzle_clearance_x` | float | `30.0` | min 0, mm | `:6589` |
| `imex_mode_names` | string-list | `[]` | position defines mode index | `:6597` |
| `imex_mode_active_tools` | string-list | `[]` | `idx:P,idx:C,idx:M,idx:S` per row | `:6603` |
| `imex_mode_gcodes` | string-list | `[]` | injected at print start | `:6609` |
| `printer_extruder_id` | int-list | `[1]` | physical tool ID per nozzle | `PrintConfig.cpp:5945` |

`imex_mode_*` rows are coupled by position only; lengths are not enforced. The pnp reader must pad
short sibling arrays, not index out of range.

### P1.2 Execute the mode

Requirement: a two-gantry machine emits mode-correct toolchanges.

1. Inject `imex_mode_gcodes[index]` once at print start, before the first `T<n>` and before
   `machine_start_gcode`, with `{imex_mode}`, `{imex_mode_index}` and `{imex_mode_gcode}`
   available as placeholders (upstream `GCode.cpp:4157-4253`).
2. Honour the mode's active-tool roles for copy and mirror: the primary path is emitted on the
   primary tool, copies on the copy tools with the gantry offset, mirrors with the mirrored
   geometry. Inactive tools are parked per the mode, not left where the previous move ended.
3. Physical tool IDs come from `printer_extruder_id`; identity mapping means tool `i` resolves to
   `printer_extruder_id[i]` (padded with `i + 1` when the array is short).
4. `primary` mode must not duplicate the path or command a non-primary tool; the only expected
   delta from today's single-tool output is the primary row's mode G-code. A non-primary mode must
   not command a heater or a `T<n>` for an inactive tool.
5. Reference implementation for semantics (not portable code): `IMEXHelpers.cpp` for the mode
   table and role parsing, and `GCode.cpp:3377-3404, 3857-3874` for parking and toolchange
   sequencing.
6. v1 roles are **Primary, Copy and Mirror**. `Span` (`S`, the multicolor partner on the primary's
   gantry) is deferred; a mode whose roster contains `S` is refused with an error naming the mode,
   never executed as Copy. The `mc-copy`/`mc-mirror` modes are therefore not claimed in v1.

### P1.3 Acceptance fixture and test

Fixture: `pinch_n_print_cli/crates/slicer-runtime/tests/fixtures/imex/imex_copy_2tool.3mf`,
plus `imex_copy_2tool.expected_tools`.

Contents: one 20×20×10 mm cube assigned to `extruder=1`; printer keys `is_imex=1`,
`imex_gantry_count=2`, `imex_tools_per_gantry=2`, `imex_tool_layout=rear-left`,
`imex_mode_names=["primary","copy"]`, `imex_mode_active_tools=["0:P","0:P,1:C"]`,
`imex_mode_gcodes=["SET_PRINT_MODE MODE=PRIMARY","SET_PRINT_MODE MODE=COPY01"]`; plate metadata
`imex_parallel_mode="copy"`.

Assertions:

- `SET_PRINT_MODE MODE=COPY01` appears exactly once, before the first `T`.
- The emitted tool sequence equals `expected_tools` (`T0`, then `T1`; no `T2`/`T3`).
- The copy path on `T1` is offset by the gantry pitch, not printed on top of `T0`.
- No heater command names an inactive tool.
- A second run with `imex_parallel_mode="primary"` emits `T0` only and no copy offset.
- A run with `imex_head_filament_map="2,1"` exits non-zero with an error naming the key.

**Done when:** the fixture slices, both tool sequences match, the non-identity map is refused, and
`module config-schema` lists all eight IMEX keys plus `printer_extruder_id` with the table's
types/defaults.

## P2 — Belt printers (feeds ticket 10)

### P2.1 Declare the printer keys

| Key | Wire type | Default | Bounds / values | Fork definition |
|---|---|---|---|---|
| `belt_printer` | bool | `false` | | `PrintConfig.cpp:7101` |
| `belt_printer_infinite_y` | bool | `true` | | `:7110` |
| `belt_slice_rotation` | string (enum) | `x` | `none`, `x`, `y`, `z` | `:7122` |
| `belt_slice_rotation_angle` | float | `45` | −180..180, deg | `:7136` |
| `build_plate_tilt_x` | float | `0` | −89..89, deg | `:7088` |
| `gcode_remap_x` | string (enum) | `pos_x` | `pos_x`, `pos_y`, `pos_z`, `neg_x`, `neg_y`, `neg_z`, `rev_x`, `rev_y`, `rev_z` | absent; upstream `:8020` |
| `gcode_remap_y` | string (enum) | `pos_y` | same | absent; upstream `:8023` |
| `gcode_remap_z` | string (enum) | `pos_z` | same | absent; upstream `:8026` |
| `printable_height` | float | `100.0` | min 0, mm | present in the fork; absent from pnp's host schema |
| `printer_extruder_id` | int-list | `[1]` | physical tool ID per nozzle | `PrintConfig.cpp:5945` |

`gcode_remap_*` are **not optional**: every shipped belt profile sets them, and a missing remap
puts the part in the wrong machine frame. `printable_height` is needed only by the `rev_*`
variants (the offset uses the build-volume max); declare it now so a profile that uses `rev_z` is
not silently wrong.

`belt_frame_tilt_decouple`, `belt_frame_tilt_angle`, `belt_support_floor_offset`,
`enable_belt_purge_tower`, `belt_purge_tower_width` and `belt_purge_tower_object` are out of v1.
They default to upstream's values; no shipped profile sets them. Declaring them later is additive.

### P2.2 Slice in belt coordinates

1. Apply the forward transform before slicing: rotate the model about
   `belt_slice_rotation` by `belt_slice_rotation_angle` (upstream
   `BeltTransform::build_forward_transform`). This is the only mesh-side belt transform.
2. When `belt_printer_infinite_y` is true, do not clamp the belt direction (Y after the rotation)
   to the bed's Y extent. The Z envelope still applies.
3. `belt_printer=0` must produce exactly the current Cartesian path; the feature is inert when
   disabled.

### P2.3 Back-transform and acceptance

The output transform is the inverse of P2.2, then the axis remap, then the machine-frame
shear/scale:

1. Inverse rotation (the forward transform's transpose) — `BeltBackTransform`.
2. `gcode_remap_*` axis remap, including the `rev_*` build-volume offset computed from
   `printable_area` (X/Y) or `printable_height` (Z).
3. Machine-frame shear `cot(tilt)` and scale `1/|sin(tilt)|` derived from the effective tilt —
   `MachineFrameTransform`.

Order matters and is the thing that breaks silently. Upstream's emit order is exactly
`BeltKinematics::to_machine`: back-transform, then remap, then machine frame
(`GCode/BeltKinematics.cpp`). The remap's `rev_*` offset is therefore computed in the bed frame,
**before** the machine-frame shear moves the point.

Fixture: `pinch_n_print_cli/crates/slicer-runtime/tests/fixtures/belt/belt_bar_200mm.3mf`, plus
`belt_bar_200mm.expected_bbox` and `belt_bar_200mm.expected_transform.json`.

Contents: a 20×200×20 mm bar, long axis along Y, `belt_printer=1`, `belt_printer_infinite_y=1`,
`belt_slice_rotation=x`, `belt_slice_rotation_angle=45`, `build_plate_tilt_x=45`,
`gcode_remap_x=rev_x`, `gcode_remap_y=pos_z`, `gcode_remap_z=pos_y`, a bed large enough in X/Z.

Assertions:

- The emitted G-code's Y extent exceeds the bed's Y extent (infinite Y).
- A known model vertex, pushed through the emitted G-code's coordinates and then through the
  inverse of the transform above, returns to its original model position within 0.05 mm.
- Re-importing the G-code into the viewer places the part's footprint where the plate preview
  showed it, within 0.5 mm (the GUI half of this requirement).
- `belt_printer=0` on the same model produces the pre-change Cartesian path.
- The tilt cases in P2.4.

### P2.4 Plate tilt

In belt mode `build_plate_tilt_x` is synced to the belt angle (all three shipped profiles set both
to 45). Requirements:

1. The effective tilt is `belt_slice_rotation_angle` when `belt_frame_tilt_decouple` is false;
   `belt_frame_tilt_angle` when true (v1: decouple is false).
2. A **non-zero** `build_plate_tilt_x` that differs from the effective tilt by more than 0.01° is
   an error naming both keys; do not silently pick one. Zero means "unset" and follows the belt
   angle, so a project that never set the key is not rejected by the default.
3. The same effective tilt drives the machine-frame transform. If support generation consumes a
   gravity vector, it uses the same tilt; the acceptance fixture asserts the transform's tilt,
   not a support-direction change.
4. `build_plate_tilt_x=44` with `belt_slice_rotation_angle=45` must fail with an error naming both
   keys; it must not slice.

**Done when:** the bar slices, the round-trip is within tolerance, infinite Y is honoured, the
tilt mismatch is refused, and `module config-schema` lists all ten keys with the table's
types/defaults.

## P3 — Plugin host (feeds ticket 11)

The ticket's P3.1–P3.4 keep their meaning here. P3.5–P3.7 are the page and packaging additions this
grilling made; P3.3's acceptance includes the page surface added by P3.5–P3.7.

### P3.1 Runtime and surviving capabilities

**No Python, no pybind11.** The fork keeps:

- discovery of installed plugins (manifest read, directory scan, module search paths);
- install/uninstall/list UI and a bundle audit;
- external Wasm modules that claim a stage;
- plugin pages (P3.5).

Dropped, and recorded as dropped in the map's ledger: upstream `pages` as Python, `script`,
`printerAgent`, and `slicingPipeline` as a Python hook. The first three are capabilities whose
implementation is the Python runtime; the fourth cannot fire because `Print::process` is a stub.

Verification: `conan/conanfile.py` and `xmake.lua` still contain no Python or pybind11 target; the
shipped tree grows no interpreter DLL.

### P3.2 The module ABI

A plugin **is** an external pnp module. No parallel plugin contract:

- manifest: the existing `<name>.toml` with `[module]`, `[stage]`, `[ir-access]`, `[claims]`,
  `[compatibility]`, optional `[config.schema]`, plus the P3.5 `[[pages]]` extension;
- code: a `.wasm` component built against the per-stage WIT package for its stage;
- v1 stage: `PostPass::GCodePostProcess` (package
  `slicer:postpass-gcode-postprocess@1.0.0`), G-code in, G-code out;
- chaining: the built-in emitter (`PostPass::GCodeEmit`) runs before the postprocess stage, and
  multiple `GCodePostProcess` modules then run sequentially (`postpass.rs:492`); a plugin is
  ordered within that stage by the manifest order.

A plugin at this stage can stamp, filter, insert or delete G-code commands. P3.4 records what that
does not replace.

### P3.3 Acceptance fixture and test

Fixture: `pinch_n_print_cli/crates/slicer-runtime/tests/fixtures/plugin/gcode_stamp/` — a test
module with `plugin.toml`, `src/lib.rs`, and a `pages/status.html` page.

The module claims `PostPass::GCodePostProcess`, inserts `; PLUGIN-STAMP` immediately after the
first `;LAYER_CHANGE`, and exports `page-data("status", _)` returning
`{"stamp_count": 1, "stage": "PostPass::GCodePostProcess"}`.

Assertions:

- `pnp_cli slice --module-dir <fixture> ...` on `perimeter_parity/multi_tool_triangle.3mf`
  produces G-code containing `; PLUGIN-STAMP` exactly once; stripping that line yields the
  no-plugin G-code byte-for-byte.
- The built-in emitter and the plugin both run; neither is silently displaced.
- `pnp_cli plugin page-data --module <id> --page status --request '{}'` returns
  `{"stamp_count":1,"stage":"PostPass::GCodePostProcess"}` on stdout and exits 0.
- `--page missing` exits 1 with a diagnostic naming the page; a module whose manifest duplicates
  a page name is refused.
- An installed bundle whose `plugin.wasm` hash does not match the recorded hash is refused with a
  **modified** diagnostic (a search-path module has no record and is allowed, per P3.7).
- A manifest with no `[stage]` is refused with a diagnostic naming the missing section.

**Done when:** the stamp test passes, the page-data verb returns the fixture JSON, the malformed
and modified cases are refused, and no Python/pybind11 target exists in the build.

### P3.4 What is not portable

`slicingPipeline` as upstream defines it is **not portable** and is being replaced, not ported:
its hook is `Print::process`, an empty stub here (`src/libslic3r/Print.cpp:2238`), and its host is
a Python runtime the fork is not adding. The replacement is the P3.2 post-process module plus the
P3.5 page surface. A plugin that needs to turn geometry into toolpaths has no v1 home; a future
geometry-in stage is a separate spec, not a hidden extension of this one.

### P3.5 Plugin pages

Pages are optional and manifest-declared:

```toml
[[pages]]
name  = "status"
title = "Stamp status"
entry = "pages/status.html"
```

Requirements:

1. `entry` is a path inside the bundle. `pages/*.html`, `pages/*.css` and `pages/*.js` are the
   allowed asset set; no symlinks, no parent traversal. Duplicate `name` values make the manifest
   invalid.
2. The host serves the assets from a local, per-plugin origin (a `file://` URL or a custom
   scheme) with this CSP: `default-src 'none'; script-src 'self'; style-src 'self'
   'unsafe-inline'; img-src 'self' data:; connect-src 'none'`. No network, no `eval`, no inline
   script.
3. The host injects one bridge object before the page's own scripts:
   `window.pnpPlugin.pageData(pageId, request) -> Promise<string>`. It accepts only a `pageId`
   declared in the manifest and forwards the call to P3.6. It exposes no other host function.
4. The page's response is JSON as a string. A malformed response is an error shown in the page,
   not a crash.
5. **Page-only plugins are deferred.** v1 requires a `[stage]`; a plugin with no stage is refused
   with a message naming the missing section. The module loader and DAG validation are not
   changed for pages.

### P3.6 The `page-data` ABI

Add a WIT package `slicer:plugin-pages@1.0.0`:

```wit
package slicer:plugin-pages@1.0.0;

interface pages {
  page-data: func(page-id: string, request: string) -> result<string, string>;
}

world plugin-pages {
  export pages;
}
```

And a CLI verb so the GUI never instantiates Wasm itself:

```text
pnp_cli plugin page-data --module-dir <DIR> --module <MODULE_ID> --page <PAGE_ID> [--request <JSON>]
```

Contract:

- stdout is exactly the module's JSON string; no wrapper, no log lines.
- exit 0 on success; exit 1 on any error, with one diagnostic line on stderr.
- `--page` is validated against the manifest's `[[pages]]` before the module is instantiated.
- v1 caps: request 64 KiB, response 1 MiB. Over-cap is an error, not a truncation.
- The verb loads the module from the same search paths as `slice`; it does not need a model.

### P3.7 Packaging and audit

A plugin is a directory bundle:

```text
<plugin-dir>/
  plugin.toml          # the module manifest; [module].id is the plugin identity
  plugin.wasm          # the component
  pages/               # optional P3.5 assets
  LICENSE
```

- Install target is a user plugin directory under the app data dir; the GUI also lists modules
  found through `--module-dir` / `SLICER_MODULE_PATH`.
- At install, record SHA-256 for `plugin.wasm` and every page asset. On load, recompute; a
  mismatch marks the plugin **modified** and refuses to run it until re-installed. This is an
  audit, not a permission prompt.
- A module discovered through a search path (`--module-dir`, `SLICER_MODULE_PATH`) has no install
  record; the list marks it **unverified** and it may run. Only installed bundles are hash-gated.
  This keeps the developer workflow (build a `.wasm` in place, point a module dir at it) usable.
- A malformed manifest, a missing `plugin.wasm`, an unknown stage, or a WIT major mismatch is
  refused with a visible error. No partial install.
- No signing, no network install, no automatic update in v1.

## Sequencing and merge gates

1. **Ticket 10 (belt)** after ticket 03. Implement P2.1–P2.4 in pnp and the fork's GUI half under
   the ticket 12 waiver. Merge only when the P2.3 acceptance test and the viewer walkthrough pass.
2. **Ticket 09 (IMEX)** after 10. Implement P1.0–P1.3 and the mode picker. Merge only when the
   P1.3 slice matches the expected tool sequence.
3. **Ticket 11 (plugin)** after 10 and 09. Implement P3.1–P3.7. Merge only when the stamp and
   page-data tests pass and the GUI lists/opens a page.

Every submodule bump is its own commit, followed by `xmake pnp` (the `.pnp-stamp` guard compares
the submodule HEAD against the staged backend). A feature ticket that cannot meet its acceptance
test is **closed as not portable**, not shipped with a dead control.

## Fixtures

| Fixture | Path (under `pinch_n_print_cli/`) | Owner | Proves |
|---|---|---|---|
| IMEX copy, 2 tools | `crates/slicer-runtime/tests/fixtures/imex/imex_copy_2tool.3mf` + `.expected_tools` | 09 | mode selection, mode G-code, copy offset, parking |
| Belt bar | `crates/slicer-runtime/tests/fixtures/belt/belt_bar_200mm.3mf` + `.expected_bbox`, `.expected_transform.json` | 10 | forward/back transform, infinite Y, tilt |
| G-code stamp plugin | `crates/slicer-runtime/tests/fixtures/plugin/gcode_stamp/` | 11 | postpass ABI, chaining, page-data |
| Malformed bundle | same directory, `broken_manifest/` | 11 | refusal path |

Fixtures are committed by the implementing ticket, not by ticket 12. Start the IMEX fixture from
`tests/fixtures/perimeter_parity/multi_tool_triangle/` (already a multi-tool 3MF) and add the
two-tool IMEX printer keys and plate metadata; do not hand-author a new mesh if that one can carry
the assignments.

## Verification commands

```bash
# pnp schema (the assertion P0.2 automates)
./pinch_n_print_cli/target/dist/developer/pnp_cli.exe module config-schema

# pnp acceptance (names are the implementing ticket's; the fixture paths are fixed above)
cargo test -p slicer-runtime imex
cargo test -p slicer-runtime belt
cargo test -p slicer-runtime plugin

# fork
xmake -j2
xmake test
xmake pnp          # after any submodule bump
```

The GUI walkthroughs are ticket 09/10/11's; screenshots land under `assets/09`, `assets/10`,
`assets/11` in this map.

## Deferred, explicitly

- Geometry-in plugin stages (the rest of `slicingPipeline`).
- Non-identity `imex_head_filament_map` and the MMU/AMS index machinery. The fork already declares
  `physical_extruder_map` and `printer_extruder_variant`; pnp does not consume them in v1. `Span`
  (`S`) roles and the `mc-copy`/`mc-mirror` modes.
- `imex_carriage_margin`, `imex_nozzle_clearance_y`, `imex_firmware_managed_zones`,
  `imex_viz_theme` (no shipped profile sets them).
- `belt_frame_tilt_decouple` / `belt_frame_tilt_angle`, belt support floor and belt purge keys.
- Plugin signing, permission grants, network install and auto-update.
- Page-only plugins (v1 requires a stage).
