---
title: Support-families impact on the support-preview and gizmo path
status: closed
type: research
assignee: Adelino Penedo
blocked-by: []
---

## Question

Does the fork's support-preview path still work against `dbf3449c`, and what does it need to
show the new support families?

The bump moved `SupportPlanIR` 2.0.0 -> 2.2.0, added `SupportPlanRole::BaseInterface` and
`ExtrusionRole::SupportBaseInterface`, split support into tree and traditional **families**
selected per region, and retired `support_density`. The fork consumes support-plan output through
`src/slic3r/GUI/PnpSupportPreview.{cpp,hpp}` and `PnpSupportPreviewDoc.cpp`, gated on
`PnpBackend`'s schema and progress-event checks.

Answer, against the actual code and the actual `pnp_cli`:

- Does `PnpSupportPreviewDoc` still **parse** the 2.2.0 document, or does the schema bump break
  it? If it parses, does it silently drop the new role/family fields?
- Is there a version gate on the support-preview document (analogous to the config-schema major
  gate), and if not, should there be?
- What does the preview currently render, and what would tree-vs-traditional and the new
  base-interface band need in order to be visible or distinguishable?
- Does `pnp_cli support-preview` still take the same arguments and config keys the fork passes,
  now that `support_density` is retired and `support_threshold_angle` (with legacy
  `support_overhang_angle` alias) exists?
- Which of the above are fork-side fixes and which are pnp-side handoff items.

This ticket is unblocked and runs in parallel with ticket 01 — it touches a different surface.

Deliverable: `assets/07-support-preview-impact.md`, plus tickets graduated from the map's fog for
whatever repair or extension it finds. Fixes themselves are **not** in this ticket.

## Amended by ticket 01

Facts the inventory established, so this ticket need not re-derive them:

- The dist now stages to `target/dist/<edition>/` — use `target/dist/developer/`, not the stale
  flat `target/dist/` (ticket 08).
- `com.core.support-planner` **split** into `com.core.traditional-support-planner` and
  `com.core.tree-support-planner`; `com.core.wave-overhangs` is new. 21 modules → 23.
- `support_density` is gone; `support_base_pattern_spacing` is declared by `traditional-support`.
  `support_overhang_angle` moved from a host key to a `traditional-support-planner` manifest key,
  with `support_threshold_angle` promoted to the host key in its place.
- **`support_family` is the new canonical per-region family selector** (`execution_plan.rs:250`)
  and the fork does not send it. `support_type` still overrides it as a compatibility alias, so
  nothing is broken today — but decide whether the fork should send `support_family`.
- Supports do not currently enable at all: the translator writes `support_enabled` where pnp
  reads `enable_support` (ticket 09). Any preview smoke test must apply that fix first or work
  around it, or it will observe "no supports" for the wrong reason.

## Answer (2026-08-28)

**Yes, the path works — and the bump never touched it.** The question's premise dissolved on
inspection: the 2.2.0 bump lives entirely in Tier 2 (`SupportPlanIR`, roles, families), while
the fork's overlay consumes a prepass document with its own, unchanged contract.

### The four code answers

1. **Parsing.** `support-preview`'s document is built by `build_preview_doc`
   (`crates/pnp-cli/src/support_preview.rs`) from the **`SupportGeometryIR`** blackboard slot —
   produced by the host builtin `PrePass::SupportGeometry`
   (`commit_support_geometry_builtin`, `crates/slicer-runtime/src/builtins/support_geometry_producer.rs`)
   inside `prepare_prepass_context` (`crates/slicer-runtime/src/run.rs`) — not from
   `SupportPlanIR` (2.2.0) or the Tier 2 views. `SupportGeometryIR` is still schema 1.0.0; the
   bump diff (`1238ef02..dbf3449c`) touches `SupportGeometryKey`/`SupportGeometryIR` by zero
   lines and touches `support_preview.rs` by one (the new `no_integrated_modules` arg, `false`).
   Live-measured: a staged wire-1.1.0 `pnp_cli` run over a generated base+overhang STL emitted
   `schema_version 1.0.0`, `units mm`, 75/75 layers, polygon objects with
   exactly `contour`+`holes` — no new fields, nothing to drop. `parse_support_preview`
   (`PnpSupportPreviewDoc.cpp`) accepts it: major-1 gate and `mm` gate both still hold.
2. **Version gate.** Two gates exist and are sufficient for what moved (nothing): the parser's
   `schema_version` major gate (rejects ≥2) and the units gate. The document contract also
   lives in one fork-facing doc that states the additive-minor rule
   (`pinch_n_print_cli/docs/20_support_preview.md`), so a future role split arrives as its own
   minor bump rather than a silent drop. It fired in the right direction historically: the
   fork consumes the *fork-facing contract doc*, pnp added the sidecar-seeding comment and the
   empty-`layers: []` semantics without version movement.
3. **Arguments and config keys.** `--help` on the staged binary shows the same five flags; the
   fork's invocation parses. Config side: `enable_support` is force-set true by the runner
   before the export (so ticket 09's dead row cannot suppress supports in a preview), the
   `support_density` retarget is inert (its row routes to nothing; the derived identity pass
   now sends `support_base_pattern_spacing`, which the bump's `traditional-support` declares),
   `support_threshold_angle`/`support_overhang_angle` are declared by
   traditional-support-planner and ride the identity pass, and `support_type` is
   identity-routed via ticket 05's `UNDECLARED_LIVE_KEYS` bridge and maps through
   `canonical_support_family` (`crates/slicer-ir/src/slice_ir.rs`) to the intended family for
   all four Orca enum values.
4. **Fork-side vs pnp-side.** Fork-side repairs needed: **none.** pnp-side handoff: **none.**
   The only open question this ticket graduates is a *visibility* one — what the overlay could
   show if the two sides extended the contract (family tint, interface band) — ticketed as
   [14-support-preview-role-visibility](14-support-preview-role-visibility.md) rather than
   done here.

### The decision: do not send `support_family`

Ticket 01 framed "decide whether the fork should send `support_family`". Decided **no**:

- `select_support_family` (`crates/slicer-scheduler/src/execution_plan.rs`) resolves
  `support_type.or(support_family)`, so a fork-sent family is either redundant (values agree)
  or overridden (they disagree — and silently). Sending it buys nothing and adds a second
  source of truth.
- Orca has no family concept to expose; the fork would be minting a config knob with no UI,
  against the map's zero-UI-diff posture. The family is already user-addressable through
  Orca's `support_type`, whose four values all select correctly today.
- The per-region channel (`RegionSupportConfig` in
  `crates/slicer-sdk/src/prepass_types.rs`, marshalled in
  `crates/slicer-wasm-host/src/marshal/in_.rs` from region-resolved config) remains the pnp
  module's own affair. If the fork ever wants per-region family overrides, that is a per-object
  config question already tracked in the map's fog (per-object overrides), not a global key.

### What the preview would need to show the families

Nothing parseable changed, so the visible overlay is exactly what it was: anonymous role-free
outlines in a single green `GLVolume` (`force_native_color`,
`consume_support_preview` in `src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.cpp`). Three gaps to
visibility, all catalogued in
[`assets/07-support-preview-impact.md`](../assets/07-support-preview-impact.md) §6 and
graduated into ticket 14: the document carries no role tag (pnp-side schema bump), interface
geometry does not exist at prepass (planner-stage decisions), and the fork's mesh builder has
one mesh and one volume (no channel for per-role colour). Also noted there: the cheapest half
is fork-only — the selected family is derivable from the config the fork already sends, so a
family tint does not need pnp at all.

### Verification

- Static: both trees read at the cited symbols (submodule at `a50bfc28`, one past the bump);
  bump diff inspected for this path.
- Live, measured: `target/dist/developer/pnp_cli.exe` `support-preview --help` (five flags,
  unchanged) and a full run over `overhang.stl` (generated, 24 triangles) writing a 1.0.0
  document with `skipped_intermediate_entries 1`, through `json.load` field-set assertions.
- Not run: a full fork-build compile gate — this ticket writes no fork or pnp code beyond these
  map files; the existing parser/mesh floor (`tests/pnp/test_pnp_support_preview.cpp`, 13
  cases) already covers the unchanged contract, and one fixture pins `"1.0.0"` explicitly.
