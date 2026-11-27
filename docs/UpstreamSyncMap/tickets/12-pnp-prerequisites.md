---
title: PNP prerequisites for ported features
status: closed
type: grilling
assignee: opencode-agent
blocked-by: []
---

# PNP prerequisites for ported features

## Question

Three feature tickets (IMEX, belt printers, plugin host) port GUI and configuration whose effect
is produced by `pnp_cli`, not by this repo. What must the Rust side implement, in what order, and
what is the acceptance test that says a prerequisite is done — before the fork's UI can claim the
feature works?

This ticket exists because the fork's standing rule forbids defensive UI gating: no disabled
controls, no hiding, no blocking. The alternative to a gate is a **scheduled prerequisite with a
verifiable end state**, which is what this ticket defines. It is the successor, for features, of
the PNP handoff-item pattern in `BootstrapMap/assets/handoff-pnp-gap-implementation.md`.

## Waiver

The user grants, per ticket, that PNP code may be implemented in `pinch_n_print_cli/` under the
same ticket that ports the GUI half. That is a deliberate departure from the fork's usual
"no backend changes here" boundary, and it means these tickets can outgrow one repo:

- Rust work lives in the `pinch_n_print_cli/` submodule and is staged with `cargo xtask dist`
  (`xmake pnp`), per `AGENTS.md`;
- the submodule bump must be its own commit, so the fork change and the backend change can be
  reviewed and reverted independently.

## Prerequisites to specify

### IMEX / iXex (feeds ticket 09)

The fork can port config keys, zones, ghosts, arrangement and the mode picker. It cannot port
toolchange planning: upstream does that in the deleted `GCode.cpp` (`:493`, `:1835`,
`:3377-3404`, `:3857-3874`) and `Print.cpp:2198-2204`. So:

- **P1.1** `pnp_cli` **declares** the IMEX keys in `pnp_cli module config-schema` (`is_imex`,
  `imex_tool_layout`, `imex_gantry_count`, `imex_tools_per_gantry`, `imex_mode_gcodes`,
  `imex_mode_names`, `imex_mode_active_tools`, `imex_nozzle_clearance_x`), so the fork's schema
  guard (`apply_schema_guard`, hardened in `b6f57b68b8`) passes them through instead of dropping
  them as undeclared. Note what this is *not*: `pnp_cli` never "rejects unknown keys"; the GUI's
  guard is the filter.
- **P1.2** `pnp_cli` emits mode-correct toolchanges for a two-gantry machine: the mode's G-code
  selection is honoured, and inactive tools are parked per the mode.
- **P1.3** an acceptance slice reproduces the same tool sequence as a reference G-code file
  committed with the ticket.

### Belt printers (feeds ticket 10)

Upstream's belt support is kinematics: `BeltGCode.cpp` includes the deleted `GCodeWriter.hpp`,
`BeltTransform.cpp` includes the deleted `GCode/MachineFrameTransform.hpp`. None of that can be
ported; it must be implemented against pnp's model.

- **P2.1** `pnp_cli` declares `belt_printer`, `belt_printer_infinite_y`, `belt_slice_rotation`,
  `belt_slice_rotation_angle` and `build_plate_tilt_x` in `module config-schema`, so the GUI's
  schema guard passes them to the backend rather than clearing them.
- **P2.2** infinite-Y slicing: geometry is rotated into belt coordinates for slicing and
  back-transformed into object coordinates in the output.
- **P2.3** an acceptance slice whose G-code, re-imported into the viewer, lands the part where the
  plate showed it — the back-transform is the thing that breaks silently.
- **P2.4** plate tilt (`build_plate_tilt_x`) is applied or explicitly refused with a message; no
  silent ignore.

### Plugin host (feeds ticket 11)

Upstream's plugin host embeds Python (`PluginManager.cpp:13`, pybind11) and its flagship
`slicingPipeline` hooks into `Print::process` — an empty stub at `src/libslic3r/Print.cpp:2238`
here. The user's intent is that the fork's plugin story follows pnp's **Wasm** modularity rather
than re-embedding Python.

- **P3.1** decide whether the fork keeps any plugin capabilities at all without a Python
  runtime (pages, script, printerAgent), or whether the plugin surface is Wasm-only from day one.
- **P3.2** define a Wasm plugin ABI on the pnp side that can replace `slicingPipeline`: a step
  that receives geometry/config and returns geometry/config or G-code.
- **P3.3** an acceptance test where one Wasm step runs inside a `pnp_cli` slice and its effect is
  visible in the output.
- **P3.4** state plainly that `slicingPipeline` as upstream defines it is **not portable** and is
  being replaced, not ported.

## Sequencing

1. This ticket closes on the **specification**, not on Rust code: for each prerequisite, a numbered
   requirement, an acceptance test, and the fixture that proves it. It must not require the backend
   work to already exist — that is what unblocked the deadlock between this ticket and 09–11.
2. Tickets 09, 10 and 11 then open with `blocked-by` cleared. Each implements its GUI half **and**
   its backend prerequisite in the same ticket, because the waiver allows editing
   `pinch_n_print_cli/`.
3. If a prerequisite turns out impossible while implementing it, the feature ticket is **closed as
   not-portable** — not shipped with a dead control.
4. **Merge rule (one rule for all three):** a feature's GUI half does not merge to `pnp/main` until
   its acceptance slice passes. There is no "GUI now, backend later" state, because that is exactly
   the dead control rule 10 forbids.

## Verification

Not code. Evidence that each prerequisite is **specified well enough to be demonstrated**:

- For each, the `config-schema` key list it depends on, copied from the live
  `pnp_cli module config-schema` so the spec is not already stale.
- The acceptance slice fixture committed or referenced.
- A one-line "done when" a future reader can check without asking anyone.

## Done when

The prerequisite asset exists with per-feature acceptance tests, tickets 09–11 unblock, and every
requirement is written so it can be demonstrated — with the fixture and the `config-schema` list
that prove it.

## Answer

Closed 2026-11-27. The specification is
[assets/12/pnp-prerequisites.md](../assets/12/pnp-prerequisites.md). No backend or GUI code is part
of this ticket; it closes on the spec, as its sequencing requires.

**Decisions taken** (grilling, 2026-11-27):

- **P3.1** — Wasm-only, with HTML plugin pages. No Python/pybind11 runtime; upstream `pages` as
  Python, `script`, `printerAgent` and the Python `slicingPipeline` hook are dropped.
- **P3.2** — a plugin is an existing external pnp module. The v1 stage is
  `PostPass::GCodePostProcess`: G-code in, G-code out, multiple modules chained. Geometry-in
  plugins are explicitly out of v1.
- **P3.5/P3.6** — pages are manifest-declared local assets with a read-only JS bridge
  (`window.pnpPlugin.pageData`) backed by a new `pnp_cli plugin page-data` verb and a
  `slicer:plugin-pages@1.0.0` WIT export. Page-only plugins are deferred.
- **P3.7** — plugin bundles carry a SHA-256 audit; no signatures or permission prompts.
- **P1.0** — IMEX's active mode travels as upstream's per-plate `imex_parallel_mode` and
  `imex_head_filament_map`; v1 supports identity physical→logical mapping and refuses a
  non-identity map loudly. v1 roles are Primary/Copy/Mirror; `Span` (`S`) and the `mc-*` modes are
  deferred and refused by name.
- **P2.4** — `build_plate_tilt_x` is applied, not refused. All three shipped belt profiles set it
  to 45, so a refusal would fail every belt printer the fork ships.
- **Order** — belt (10), then IMEX (09), then plugin (11).
- **Fixtures** — live in `pinch_n_print_cli/.../tests/fixtures/`; the pnp suite owns the
  assertions, the fork walkthrough loads the same files through the submodule.

**Corrections to the ticket's premises**, recorded in the asset:

- `apply_schema_guard` **leaves unknown keys alone** (`PnpConfigTranslator.cpp:1091`). Declaring
  the keys is needed so pnp consumes and validates them, not so the GUI guard passes them; no guard
  change is part of the prerequisite.
- The 8 IMEX keys are insufficient: the active mode is **plate metadata**, and
  `ParsedSidecar.plate_metadata` is parsed but not stamped today (`docs/02_ir_schemas.md:446`).
- The 5 belt keys are insufficient: the shipped belt profiles also set `gcode_remap_x/y/z` (all
  three) and `printer_extruder_id` (two of three). The fork lacks `gcode_remap_*`; it defines
  `printer_extruder_id` (`PrintConfig.cpp:5945`) but pnp does not declare either family.
- `printable_height` is needed by the `rev_*` remap offset; the host schema lacks it.
- The live probe (`config-schema`, wire `1.3.0`) reports 107 host keys + 24 module schemas (131
  unique names); none of the new keys is among them.

**Unblocks:** tickets 09, 10 and 11 have `blocked-by` cleared. Each implements its GUI half and its
backend prerequisite in the same ticket under the waiver, and merges only when its acceptance slice
passes. A prerequisite that proves impossible closes its feature ticket as not portable rather than
shipping a dead control.
