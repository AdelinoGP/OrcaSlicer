---
title: Design-doc structure for subagent execution
status: closed
type: grilling
assignee: Analysis Agent
blocked-by: [003, 004, 005, 008, 009, 011, 012, 013]
---

## Question

Shape of the final handoff design doc: how implementation tickets are sized so each future session completes a batch via subagents, their ordering/dependencies, and per-ticket verification (build/run checks on Windows). This ticket consolidates all resolved decisions into the destination artifact.

**Added scope from [ticket 012](012-v1-ui-surface-restrictions.md) (2026-07-17) — cross-repo sequencing.** 012 established that pnp gaps are *scheduled dependencies*, closed in parallel with the fork's roadmap, and that the fork therefore ships **no defensive UI** against any of them. That makes the pnp side a hard input to the fork's ordering: the design doc must state which [handoff items](../assets/handoff-pnp-gap-implementation.md) gate which fork milestone, and which must land before the fork is user-visible at all (item 6 / non-uniform scale is the sharpest — the fork implements no check, so until it lands any non-uniformly-scaled object hits a raw pnp loader error). Ordering is no longer purely internal to this repo.

## Resolution (2026-07-17, grilling)

The destination artifact was written this session: [010-fork-implementation-plan.md](../assets/010-fork-implementation-plan.md) plus 15 per-ticket files in [assets/fork-tickets/](../assets/fork-tickets/). Structure decisions, all put to the user:

- **Home:** `.wayfinder/assets/` — design doc + per-ticket files (`F01`–`F15`), frontmatter `status/batch/blocked-by/files` authoritative.
- **Sizing:** one fresh subagent session per ticket, explicit file lists and done-criteria; a driving session claims one batch, dispatches its tickets in parallel (intra-batch file disjointness; no isolated/worktree agents per repo owner's rule), integrates, verifies, commits.
- **Ordering:** seam first, rip-out second — M1 seam (B1 foundations: translator/discovery/warnings-sink; B2 seam core: PnpSlicingProcess/3MF-export/progress; B3 GUI: preview/error-UX/Plater swap; B4 stats = M1 close), M2 rip-out (B5 CLI+calibration, B6 SLA, B7 FFF pipeline — sequential single-ticket batches, overlapping file sets), M3 polish (B8 thumbnails, B9 distribution+acceptance).
- **pnp gating:** table per milestone with a check ritual before each milestone's first batch (item 10+2 gate M1 close; item 6 hard-gates release; 14 gates B8; 16 gates print-quality sign-off). Fork tickets never block on pnp mid-batch.
- **Verification:** tiered — affected CMake target per ticket; full RelWithDebInfo build + scripted smoke ritual per batch; smoke + ctest + gating check per milestone.
- **Commits:** one per ticket on `pnp/main`, optional batch tag.

Remaining fog ("performance/latency expectations", "branding/naming") ruled out of scope: neither blocks implementation start, which is the destination's own bar.
