---
title: v1 UI surface restrictions for PNP gaps
status: closed
type: grilling
assignee: claude
blocked-by: []
---

## Question

Ticket 001 found PNP gaps the Orca UI currently exposes controls for: standalone raft, multi-material/multi-extruder (plumbing unproven end-to-end), non-Marlin G-code flavors, non-uniform object scaling (loader rejects it). For each: hide/disable the UI in v1, warn at slice time, or block on the pnp-side handoff item? Decide the per-gap policy and how it's implemented consistently (single capability gate vs. ad-hoc).

## Resolution (2026-07-17, grilling)

**Answer: no UI-surface restrictions in v1. Zero UI diff. No capability gate.** All four gaps resolve to "leave Orca's UI alone"; the ticket's second half (single gate vs. ad-hoc) is therefore moot — there is nothing to gate.

### The reframe that drove it (standing fact, now on the map)

PNP's missing features are being implemented **in parallel with this fork's roadmap**, scheduled so each lands before the fork needs it. Gaps are therefore **scheduled dependencies, not permanent constraints**. The fork does not build defensive UI (hiding, gating, or blocking) against a gap that will be closed before the feature is reachable here. This inverts the ticket's premise, which assumed the gaps were fixed and the GUI had to absorb them.

### Per-gap policy

| Gap | Policy | Rationale |
|---|---|---|
| **Standalone raft** | Leave UI alone | Already specced pnp-side: ADR-0009 (`raft-as-layer-infill-role`, status *Proposed*) + `docs/specs/raft-default-module.md` (design sketch; central output-carrier question still open; depends on `support-modules-orca-port.md` §C6 landing `SupportPlanIR.raft_plan`). Interim already covered by ticket 005's mapping row: `raft_layers` → `support_raft_layers` (copy), warn when >0 with supports off. **Corrects ticket 001**, which recorded "❌ no raft" from the live config-schema probe and never checked the spec backlog. |
| **Non-Marlin G-code flavor** | Leave UI alone; ticket 005's warning already covers it | `gcode_flavor` is a Tier-D unresolved key already classified `unsupported-feature` — the warning costs no new code. Restricting the dropdown or hiding presets would delete ~60% of the bundled printer library (of profiles that declare a flavor: klipper 233, marlin 92, marlin2 59, reprapfirmware 31, repetier 1; BBL's 73 machines are all marlin). Klipper — the largest group — accepts Marlin G-code, and `machine_start_gcode`/`machine_end_gcode` pass through to PNP unchanged (both are Tier-A identical keys), so each profile's printer-specific setup survives. Flavor selection is pnp handoff item 5. |
| **Multi-material / multi-extruder** | Leave UI alone, **no warning** | Consistent with ticket 004's "let PNP try". The gap is *unproven*, not absent, and the E2E proof (handoff item 4 — TASK-210/211/212) is scheduled. Note the lever would have been `Sidebar::show_SEMM_buttons()` (hides "+ add filament"), **not** extruder count — BBL/AMS printers are 1 nozzle + N filaments, so `nozzle_diameter.size()` is the wrong gate. |
| **Non-uniform scale** | Leave UI alone, **and do not implement the slice-time block** | **Supersedes ticket 004.** PNP will lift `NonUniformScaleUnsupported` (handoff item 6) before the fork is user-visible, so the fork implements no check at all — not in the UI, not at slice time. |

### Superseded

Ticket 004's "the GUI blocks slicing with a clear error when any instance on the plate carries non-uniform scale" is **withdrawn**. Handoff item 6 is now a scheduled prerequisite rather than a gap the GUI defends against. Ticket 004's other decisions stand unchanged.

### Consequences

- **No diff** in `ConfigManipulation.cpp`, `Tab.cpp`, `Plater.cpp`, `GizmoObjectManipulation.cpp`, `GLGizmoScale3D.cpp`, or `Print.cpp` for gap-gating purposes — all high-churn upstream files, so this directly serves ticket 002's thin-fork/cherry-pick merge-cost goal.
- The fork's **only** gap-communication channel is ticket 005's slice-time warning vector. Ticket 013 (config-warning UX) therefore carries the entire load; its "interacts with 012's per-gap policy" dependency resolves to "no gap warns by UI policy — 005's classification is the whole input".
- Handoff items 4, 5, 6 (and 3/raft) are promoted from "either/or" framing ("implement, or the fork hides the control") to **scheduled prerequisites**, since the fork now exposes all four surfaces unguarded.

### Facts established (for the ticket 010 handoff doc)

- **Orca has no capability system.** Grepping `capabilit` across `src/slic3r` yields only unrelated hits. Gating is ad-hoc booleans over config values, recomputed per change. Had a gate been needed, the choke point was `Tab::toggle_option` / `Tab::toggle_line` — *not* `ConfigManipulation`, which `TabPrinter::toggle_options` and the `TabFilament` blocks bypass by calling `Tab::` directly.
- `Field::toggle` = grey out (still visible); `toggle_line` sets `Line::toggle_visible` = hide the row, ANDed with the mode filter, effective only on the next `Page::update_visibility`. Both early-return unless the page is active.
- **Raft's UI is only two lines** (`raft_layers`, `raft_contact_distance`, in a dedicated "Raft" optgroup in `TabPrint::build`). `raft_first_layer_density`/`raft_first_layer_expansion` sit in the *Support* optgroup because Orca reuses them for support first layers — they are not raft-only. `raft_expansion` has no UI line at all. Raft is **not** in the live per-object settings menu (`OBJECT_CATEGORY_SETTINGS`); its only appearance in `GUI_Factories.cpp` is inside a commented-out block, though `PartPlate.cpp` still reads per-object `raft_layers` defensively for imported 3MFs.
- `gcode_flavor` exposes 5 of 13 enum values (marlin, klipper, reprapfirmware, repetier, marlin2); the rest are commented out. It is never toggled and is explicitly `readonly = false`. Value-restriction precedent exists: `TabPrint::toggle_options` `const_cast`s `support_style`'s `ConfigOptionDef` and repopulates its `enum_values`.
- `Preset::is_visible` + `PresetBundle::load_installed_printers` / `set_visible_from_appconfig` is the preset-hiding mechanism, but several sites force `is_visible = true` and `PresetComboBox::update`'s `m_show_all` bypasses it — a preset-level gate would have been leaky.
- `Geometry::Transformation::is_scaling_uniform()` already exists and is used **only** as a perf short-circuit in `Geometry.cpp`, never as validation. `GizmoObjectManipulation::m_uniform_scale` defaults `true` and is a live checkbox; a commented-out BBS `disabled_begin()/disabled_end()` block in `do_render_scale_input_window` is the force-uniform seam. All left untouched by this decision.
- `Print::validate()` returns `StringObjectException`; non-empty `.string` hard-blocks slicing and routes to `push_validate_error_notification`, with `object` making it click-to-select and `opt_key` click-to-jump. Unused by this ticket, but it is the known error path for any future slice-time block.
