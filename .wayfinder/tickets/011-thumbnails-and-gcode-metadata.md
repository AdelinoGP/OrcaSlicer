---
title: Thumbnails & G-code metadata gaps in the GUI
status: open
type: grilling
assignee:
blocked-by: [003]
---

## Question

Inventory (ticket 001) established: PNP embeds an externally-supplied PNG via `--thumbnail` (it renders nothing itself), and emits no print-time estimate (`estimated_print_time_s` = 0) or weight/cost stats. How does the fork's GUI render the per-plate thumbnail PNG to pass to `pnp_cli` (Orca already renders plate thumbnails — reuse path?), and how does the UI handle absent time/weight estimates in preview, sidebar stats, and print-host upload metadata — hide, show "n/a", or wait on the pnp-side `slice_stats`/time-estimator handoff items?

## Comments

Ticket 007 (2026-07-16) softens the premise: Orca's `GCodeProcessor` computes time estimates and filament volume itself from the parsed moves — it ignores `; estimated printing time`/`; filament used` comments entirely on external load. So preview and sidebar times/volumes are NOT blank; they're just default-machine-limits accuracy (improvable via pnp handoff item 10's `machine_max_*` passthrough). The genuinely absent pieces are: grams/cost (needs `filament_density`/`filament_cost` in CONFIG_BLOCK — also item 10), M73 on-printer progress, and G-code-embedded time metadata for print-host upload. Scope this ticket's "absent estimates" question to those.
