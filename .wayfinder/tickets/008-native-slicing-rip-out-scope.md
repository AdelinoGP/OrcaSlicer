---
title: Native-slicing rip-out scope
status: open
type: grilling
assignee:
blocked-by: [003]
---

## Question

Exactly what gets removed: FFF `Print::process` slicing paths, all SLA (presets, `SLAPrint`, SLA UI), calibration flows that slice. What must stay because it uses libslic3r geometry without slicing: arrange, orient, mesh repair, cut, supports painting UI (?). Long-lived fork: removal must be clean and maintainable, not `#ifdef`-hidden.
