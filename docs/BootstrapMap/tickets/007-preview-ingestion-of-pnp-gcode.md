---
title: Preview ingestion of PNP G-code
status: closed
type: research
assignee: Analysis Agent
blocked-by: [003]
---

## Question

Verify PNP G-code parses through Orca's `GCodeProcessor` into a full `GCodeProcessorResult`: feature-role coloring (`;TYPE:` comments), layer detection, time estimates, filament stats. PNP is supposed to be Orca-compatible here — any annotation gap becomes a precise pnp-side handoff item, and this ticket defines the exact required annotation set.

## Answer

Verified by cross-checking `GCodeProcessor.cpp`/`ExtrusionEntity.cpp` against pnp's `slicer-gcode` serializer and a live `pnp_cli` slice (2026-07-16). Full analysis, compatibility matrix, and the normative required-annotation set: [preview-ingestion asset](../assets/007-preview-ingestion-of-pnp-gcode.md).

- **Compatible today:** producer detection (substring `OrcaSlicer` in PNP's header line), all 15 `;TYPE:` spellings (exact matches in `string_to_role`), `;LAYER_CHANGE` tag-driven layer counting (gap-free, 200/200 in the live sample), `;HEIGHT:`, ≥96-key CONFIG_BLOCK with `gcode_flavor`/`filament_diameter`, THUMBNAIL_BLOCK.
- **One fork-side requirement:** `GCodeProcessor::s_IsBBLPrinter` (static, defaults true, set from preset vendor) selects a *different* tag table when true; PNP emits no `printer_model` to flip it. The PNP ingestion path MUST set it `false` before `process_file`, else the preview collapses to one uncolored layer under a Bambu vendor preset.
- **Time/filament stats:** `GCodeProcessor` computes both itself from moves — `; estimated printing time` and `; filament used` comments are ignored on external load, M73 is not even a registered command. So preview time/volume are NOT blank despite PNP's `estimated_print_time_s = 0`; accuracy is default-machine-limits grade. Grams/cost need `filament_density`/`filament_cost` in the CONFIG_BLOCK (currently header-only → defaults).
- **New pnp handoff item 10:** viewer-config passthrough keys in CONFIG_BLOCK (`printer_model`, `filament_density`, `filament_cost`, `printable_area`, `nozzle_diameter`, `machine_max_*`) — low effort, serializer already passes raw_config through.
- **Ripple:** ticket 011's "absent time estimates" premise is softened — preview and sidebar times exist via Orca's own estimator; the gap is confined to G-code-embedded metadata (print host upload, M73 on-printer progress). Noted on ticket 011.
