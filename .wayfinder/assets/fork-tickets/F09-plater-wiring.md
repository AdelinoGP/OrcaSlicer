---
title: Plater wiring — BSP → PnpSlicingProcess swap, slice-all loop
status: open
batch: B3
blocked-by: [F04]
files: [src/slic3r/GUI/Plater.cpp, src/slic3r/GUI/Plater.hpp, src/slic3r/GUI/MainFrame.cpp, src/slic3r/GUI/GUI_Preview.cpp]
---

## Goal

Swap `Plater::priv`'s `BackgroundSlicingProcess background_process` for `PnpSlicingProcess` and
make the app slice via PNP end-to-end. Per
[ticket 003](../../tickets/003-slicing-seam-replacement-design.md), this is ≈ a type rename:
the new class preserves BSP's public surface.

## Decisions (do not re-open)

- wx event handlers (`on_slicing_update`, `on_slicing_completed`, `on_process_completed`,
  `on_export_began/finished`) survive unchanged.
- **"Slice all" loop unchanged**: existing `m_slice_all` / `on_process_completed` →
  `select_plate` → `start_next_slice()` re-enters the surface once per plate, sequential.
  Add the "plate N/M" label into F06's status text here.
- `update_background_process` / `is_slice_result_valid` / gcode-reset-on-invalidation behavior
  survive via retained `Print::apply()`.
- Gate slice start on F02's `PnpBackend::available()`.
- BSP itself is **not deleted here** — it merely loses its Plater usage (deletion is M2, F13's
  batch, once nothing references it). If it no longer compiles without its Plater friend
  wiring, prefer detaching it from the build over patching it.

## Steps

1. Type swap + compile-error-driven sweep of the ~50 call sites; SLA-only call sites
   (`reslice_SLA_until_step` etc.) get temporarily short-circuited (their removal is F12).
2. Thread the plate label into status events for slice-all.
3. Confirm export/upload (`finalize_gcode` `.pp` copy dance, removable-media checks) still key
   off `PartPlate::get_tmp_gcode_path()` untouched.

## Done / verify

- Full `ALL_BUILD` RelWithDebInfo clean (this ticket ends batch B3's integration).
- Batch smoke = the full M1 ritual: load model → Slice → progress advances → preview renders →
  export G-code to disk succeeds; Slice-All on a 2-plate project runs two pnp_cli invocations
  sequentially with per-plate percent.
