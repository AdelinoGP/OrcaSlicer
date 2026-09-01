---
title: Support-preview role and family visibility
status: resolved
type: task
assignee: opencode-agent
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

## Answer (2026-09-01)

**Decision after grilling: both halves.** The user took the family tint *and* the interface
band. Landscape shift since the ticket was written: the document was already 1.1.0 (the
support-body fix, commit `0c4eb9bc67`) with `layers[].support_body` — the "where is support"
job was already done; only the "what kind" half remained.

**pnp side — one commit, submodule `588651d0`:** `layers[].support_interface`, schema 1.2.0,
additive per `docs/20_support_preview.md`'s own rule (`SupportPreviewLayer` gains the field;
`build_preview_doc` collects the `TopInterface`/`BaseInterface`/`BottomInterface` role
regions via a new `role_regions_by_layer` helper shared with the body pass; always emitted,
possibly empty). Shape question decided: **per-layer split**, not per-polygon role tags —
the fork renders one interface colour, so role tags would be dead data on the wire (the
map's "nothing dead" principle; a future per-role colour wants its own additive bump, noted
in the pnp doc). The traditional-support planner commits all three interface roles
(`traditional-support/src/lib.rs`), so the band has real content. pnp tests 12/12 including
a case pinning all three roles merging into `support_interface` and raft-prefix skipping;
`check-literals` green (the pre-existing `SupportPlanEntry` fixture violations in
`support_preview_tdd.rs` were waived, not restructured — the type has no `Default`).

**Fork side — one commit, `d09a6d4a51`:** `PnpSupportPreviewLayer` gains
`support_interface`; `parse_support_preview` buckets 1.2.0 `support_interface` polygons
additively (absent field = empty band); `build_support_preview_meshes` (renamed) returns
`PnpSupportPreviewMeshes { body, interface_mesh }` — one prism weld helper shared by both
buckets. The gizmo renders two `GLVolume`s: body coloured by family (tree green,
traditional blue) and a constant orange interface band; `m_preview_family` is captured from
`support_type` at request time (`opt->serialize()`, not `opt_string()` — it is an enum
option and `opt_string` throws) via `pnp_support_family_from_type()`, mirroring pnp's
`canonical_support_family` (`tree*`/`hybrid*` → tree, else traditional; Orca's four enum
values all land on the intended family). Regenerate/stale checks and consume's cleanup
cover both volumes. Named `interface_mesh`, **not** `interface`: MSVC reserves `interface`
as a COM extension keyword, active via the PCH's Windows headers (a real compile error,
`C2059` at `meshes.interface` — the GUI-free tests compiled it fine and masked it until
the app target compiled).

**Why the family tint is still nearly-free information and taken anyway:** Orca's
`support_type` is global, so pnp resolves the whole print to one family and the tint is a
constant colour — its value is a verification affordance (the overlay colour matches the
dropdown) and family awareness for a future per-region channel, at the cost of one enum
mapping function.

Fork tests 32/32 (316 assertions) — new cases: 1.2.0 parse buckets, 1.1.0 documents carry
an empty band, interface polygons build the interface mesh not the body, interface-only
documents leave the body empty, and the family mapping (tree/normal/unknown/hybrid).

Residuals:
- **Manual overlay smoke** (the deliverable's second half): deferred to an interactive
  desktop session — launch, select the support gizmo, paint, Preview supports, eyeball the
  two-colour overlay. Same residual as tickets 11/12; nothing in this session can click
  the app.
- **xmake operational finding (not a code issue):** after several build runs in this
  session, `xmake -j2`/`xmake -b` (implicit default-target selection) started reporting
  "build ok" without compiling anything — even for genuinely stale files — while
  `xmake -b <explicit target>` keeps working correctly (compiles stale files, relinks, and
  re-bundles the backend). All verification in this ticket used explicit targets. The
  trigger is unidentified; if the implicit path silently no-ops again, use explicit
  targets and consider reporting upstream.