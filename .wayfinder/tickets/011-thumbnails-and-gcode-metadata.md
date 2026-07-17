---
title: Thumbnails & G-code metadata gaps in the GUI
status: open
type: grilling
assignee:
blocked-by: [003]
---

## Question

Inventory (ticket 001) established: PNP embeds an externally-supplied PNG via `--thumbnail` (it renders nothing itself), and emits no print-time estimate (`estimated_print_time_s` = 0) or weight/cost stats. How does the fork's GUI render the per-plate thumbnail PNG to pass to `pnp_cli` (Orca already renders plate thumbnails — reuse path?), and how does the UI handle absent time/weight estimates in preview, sidebar stats, and print-host upload metadata — hide, show "n/a", or wait on the pnp-side `slice_stats`/time-estimator handoff items?
