---
labels: [wayfinder:map]
title: Fork OrcaSlicer frontend onto the PNP backend
---

# Fork OrcaSlicer frontend onto the PNP backend

## Destination

A handoff-ready design doc — optimized for subagent execution — for converting this OrcaSlicer checkout, in place, into a PNP-backed frontend: `pnp_cli` shell-out replaces `BackgroundSlicingProcess` slicing, native slicing (FFF `Print::process` and all SLA) removed cleanly, PNP's config system adopted with pass-through warnings for unresolved Orca preset keys, and PNP G-code rendered in Orca's preview. The map is done when every design decision needed to start implementation is resolved.

## Notes

- Domain: C++17/wxWidgets (OrcaSlicer, this repo) + Rust CLI backend (`F:\slicerProject\pinch_n_print`, binary `pnp_cli`).
- HITL tickets: use `/grilling` and `/domain-modeling`. Plan-only effort: tickets decide, they don't build.
- Tracker: local markdown. Tickets live in `.wayfinder/tickets/NNN-<slug>.md`; frontmatter `status/type/assignee/blocked-by` is authoritative. Claim = set `assignee`. Frontier = open, unassigned, all `blocked-by` closed.
- Standing facts:
  - GUI→slicing seam: `src/slic3r/GUI/BackgroundSlicingProcess.{hpp,cpp}` (state machine, worker thread, holds `Print*`/`SLAPrint*` + `GCodeProcessorResult*`), driven by `src/slic3r/GUI/Plater.cpp` via `background_process.apply(model, preset_bundle->full_config(...))` → `start()`. Results reach the GUI via wx events (`SlicingStatusEvent`, `SlicingProcessCompletedEvent`); preview consumes `GCodeProcessorResult` (`libslic3r/GCode/GCodeProcessor.hpp`) in `GUI_Preview`/`GLCanvas3D`. Multi-plate via `GUI::PartPlate`.
  - No existing subprocess/CLI slicing abstraction in OrcaSlicer — the seam must be newly built. `GCodeProcessor` can parse a G-code file: natural preview path.
  - PNP surface: `pnp_cli slice --model <stl/obj/3mf> --config <json> --output <gcode> --module-dir ... --thumbnail <png> --instrument-stderr` (JSONL progress, pnp `docs/09_progress_events.md`); `pnp_cli module config-schema` introspects config.
  - PNP-side gaps discovered here (e.g. missing Orca-compatible `;TYPE:` annotations) are recorded as precise **handoff items** for the pinch_n_print spec-packet workflow — not charted on this map.
  - Locked by grilling: shell-out integration (no FFI); v1 scope = slice + preview; PNP only (no native fallback); "Slice all" loops pnp_cli per plate sequentially; SLA removed entirely; Windows-first; long-lived product until pnp_studio (`F:\slicerProject\pinch_n_print_studio`, Bevy) matures — rip-outs must be clean and maintainable.

## Decisions so far

<!-- one line per closed ticket: [title](tickets/NNN-slug.md) — gist -->

- [PNP feature-coverage inventory](tickets/001-pnp-feature-coverage-inventory.md) — PNP covers the core single-material FFF slice surface with Orca-viewer-compatible G-code; hard gaps: no time estimates, no raft, Marlin-only flavor, unproven multi-material, GUI must render thumbnails, no multi-plate 3MF, non-uniform scale rejected. Full matrix + 8 pnp handoff items in [the inventory asset](assets/001-pnp-feature-coverage-inventory.md).
- [Upstream sync strategy](tickets/002-upstream-sync-strategy.md) — long-lived `pnp/main` branch off local `main`, fork point tagged `pnp-fork-base`; cherry-pick-only upstream sync (profiles/GUI/security fixes, no merges); this checkout loses canonical porting-source status — user keeps a separate pristine OrcaSlicer folder for pnp porting.
- [Preset→PNP config translation spec](tickets/005-preset-to-pnp-config-translation.md) — flat-key PNP JSON; of Orca's 925 keys, 65 map identically, ~30 via a verified rename/transform table, ~830 stay unsent with classified warnings; mechanism = one static table in a new `PnpConfigTranslator` GUI translation unit. Full four-tier table in [the mapping asset](assets/005-preset-to-pnp-config-mapping.md).
- [Slicing-seam replacement design](tickets/003-slicing-seam-replacement-design.md) — new `PnpSlicingProcess` replaces BSP wholesale but keeps its public surface (Plater ≈ type rename); `Print::apply()` retained for invalidation; one worker thread per slice; boost::process everywhere; v1 cancel = kill child (graceful cancel is a pnp handoff item); inputs in per-slice temp dir, output to `PartPlate::get_tmp_gcode_path()`; Plater's slice-all loop unchanged; reuse path keyed on plate validity + gcode file + `APPLY_STATUS_UNCHANGED`; failures → `SlicingError` via `exception_ptr`.

## Not yet specified

- Performance/latency expectations vs. in-process slicing; whether incremental re-slice matters.
- Branding/naming of the fork; relationship messaging vs. pnp_studio.

## Out of scope

- Feature parity beyond slice+preview (calibration suites, device-tab changes).
- FFI/in-process embedding of `slicer-runtime` — shell-out decided.
- pnp-side work (e.g. G-code annotations) — handed off to the pinch_n_print spec-packet workflow.
- pnp_studio (Bevy frontend) — separate long-term effort.
- Executing the fork — this map is plan-only; implementation follows the handoff design doc.
