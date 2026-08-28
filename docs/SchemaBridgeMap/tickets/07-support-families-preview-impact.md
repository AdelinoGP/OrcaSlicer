---
title: Support-families impact on the support-preview and gizmo path
status: open
type: research
assignee:
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
