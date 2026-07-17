---
title: v1 UI surface restrictions for PNP gaps
status: open
type: grilling
assignee:
blocked-by: []
---

## Question

Ticket 001 found PNP gaps the Orca UI currently exposes controls for: standalone raft, multi-material/multi-extruder (plumbing unproven end-to-end), non-Marlin G-code flavors, non-uniform object scaling (loader rejects it). For each: hide/disable the UI in v1, warn at slice time, or block on the pnp-side handoff item? Decide the per-gap policy and how it's implemented consistently (single capability gate vs. ad-hoc).
