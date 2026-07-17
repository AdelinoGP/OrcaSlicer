---
title: Progress, cancel, and error UX
status: open
type: grilling
assignee:
blocked-by: [003]
---

## Question

How do PNP's `--instrument-stderr` JSONL progress events map onto `SlicingStatusEvent` and the existing progress UI? How is the subprocess cancelled cleanly (user hits stop, closes app, changes model mid-slice)? How are slicing errors from pnp_cli surfaced? How is progress aggregated across the per-plate loop in "Slice all"?
