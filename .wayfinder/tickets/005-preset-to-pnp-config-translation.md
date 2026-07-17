---
title: Preset→PNP config translation spec
status: open
type: research
assignee:
blocked-by: [001]
---

## Question

Concrete key mapping from OrcaSlicer `DynamicPrintConfig` (see `src/libslic3r/PrintConfig.cpp`) to PNP JSON config (via `pnp_cli module config-schema`). Decided strategy: PNP's config system is adopted; Orca presets load and pass through; keys PNP can't resolve are marked with warnings; only PNP-accepted keys do work. This ticket produces the mapping table, the pass-through-with-warnings mechanism design, and where warnings surface in the UI.
