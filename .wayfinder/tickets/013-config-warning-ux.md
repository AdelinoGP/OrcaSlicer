---
title: Config-warning UX for unresolved preset keys
status: open
type: grilling
assignee:
blocked-by: [005]
---

## Question

The preset→PNP translation (ticket 005) produces a per-slice warning vector classified `unsupported-feature` / `not-yet-mapped` / `no-op`. Where and how do these surface in the UI: a one-time dialog, a sidebar notification, the slicing-progress notification area, or a dedicated panel? How are they grouped (per feature vs. per key), deduplicated across re-slices, and suppressible ("don't show again")? Do `no-op` warnings surface at all? Interacts with ticket 012's per-gap policy (some gaps warn at slice time by that decision).
