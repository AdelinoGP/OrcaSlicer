---
title: Support-preview role and family visibility
status: open
type: task
assignee:
blocked-by: []
---

## Question

Should the support-preview overlay be able to tell tree from traditional supports and show the
new base-interface band — and if so, what changes on each side of the wire?

Ticket 07
([`assets/07-support-preview-impact.md`](../assets/07-support-preview-impact.md)) established
that the overlay's document (`pnp_cli support-preview`) carries per-layer coarse outlines with
**no role or family information** and is still document schema 1.0.0 — the 2.2.0
`SupportPlanIR` bump never touches it. The families and the `SupportPlanRole::BaseInterface`
band are Tier 2 planning concepts, and the verb executes the prepass prefix only. It also found
one cheap fork-only half: the selected family is derivable from the config the fork already
sends (`select_support_family` in `crates/slicer-scheduler/src/execution_plan.rs` maps the
`support_type` the fork identity-routes), so the whole overlay could be coloured by family
with no pnp change at all.

Resolve:

- **Is either half wanted?** The family tint (fork-only) and the interface band (needs a pnp
 -side document change) are independent; either, both, or neither can be taken. Ticket 07
  leaves the choice open — the overlay's job is "where is support", and both additions answer
  "what kind", which may be redundant with the G-code preview's own role legend.
- If the pnp half is taken: agree the document extension shape with the pnp side (a per-polygon
  `role` tag vs. a per-layer split), bump the *document* minor to 1.1.0 per
  `pinch_n_print_cli/docs/20_support_preview.md`'s own additive rule, and keep the fork's
  parser accepting 1.0.0 documents (the gate in `parse_support_preview` is major-only already).
  The document change is a pnp-side commit — the map allows those; call it out explicitly,
  precedent: ticket 02's wire 1.1.0.
- Fork-side consumption, either half: `build_support_preview_mesh` welds every expolygon of a
  layer into one mesh, and `consume_support_preview` renders a single `GLVolume` with
  `force_native_color`. Per-family or per-role rendering needs per-role meshes (or per-role
  colours) end to end.

Not a promise of the pnp-side work; if declined, this ticket resolves "no" and the overlay
stays as it is.

Deliverable: the decision, and if taken, the fork-side half with cases in
`tests/pnp/test_pnp_support_preview.cpp` for the extended parser plus a manually verified
overlay smoke.