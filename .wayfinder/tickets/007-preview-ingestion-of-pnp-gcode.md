---
title: Preview ingestion of PNP G-code
status: open
type: research
assignee:
blocked-by: [003]
---

## Question

Verify PNP G-code parses through Orca's `GCodeProcessor` into a full `GCodeProcessorResult`: feature-role coloring (`;TYPE:` comments), layer detection, time estimates, filament stats. PNP is supposed to be Orca-compatible here — any annotation gap becomes a precise pnp-side handoff item, and this ticket defines the exact required annotation set.
