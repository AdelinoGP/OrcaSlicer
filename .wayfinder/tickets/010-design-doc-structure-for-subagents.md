---
title: Design-doc structure for subagent execution
status: open
type: grilling
assignee:
blocked-by: [003, 004, 005, 008, 009, 011, 012, 013]
---

## Question

Shape of the final handoff design doc: how implementation tickets are sized so each future session completes a batch via subagents, their ordering/dependencies, and per-ticket verification (build/run checks on Windows). This ticket consolidates all resolved decisions into the destination artifact.

**Added scope from [ticket 012](012-v1-ui-surface-restrictions.md) (2026-07-17) — cross-repo sequencing.** 012 established that pnp gaps are *scheduled dependencies*, closed in parallel with the fork's roadmap, and that the fork therefore ships **no defensive UI** against any of them. That makes the pnp side a hard input to the fork's ordering: the design doc must state which [handoff items](../assets/handoff-pnp-gap-implementation.md) gate which fork milestone, and which must land before the fork is user-visible at all (item 6 / non-uniform scale is the sharpest — the fork implements no check, so until it lands any non-uniformly-scaled object hits a raw pnp loader error). Ordering is no longer purely internal to this repo.
