---
title: Config-warning UX for unresolved preset keys
status: closed
type: grilling
assignee: claude
blocked-by: [005]
---

## Question

The preset→PNP translation (ticket 005) produces a per-slice warning vector classified `unsupported-feature` / `not-yet-mapped` / `no-op`. Where and how do these surface in the UI: a one-time dialog, a sidebar notification, the slicing-progress notification area, or a dedicated panel? How are they grouped (per feature vs. per key), deduplicated across re-slices, and suppressible ("don't show again")? Do `no-op` warnings surface at all? ~~Interacts with ticket 012's per-gap policy (some gaps warn at slice time by that decision).~~

**Resolved input from [ticket 012](012-v1-ui-surface-restrictions.md) (2026-07-17):** 012 imposes **no** UI restrictions and adds **no** gap-specific warnings of its own, so there is no per-gap policy to reconcile — ticket 005's classified warning vector is this ticket's *entire* input. This makes 013 load-bearing: it is now the fork's **only** channel for telling the user a PNP gap affected their slice. Two concrete cases 012 hands over: `gcode_flavor` (a Tier-D `unsupported-feature` key — the sole signal that a Klipper/RRF user's G-code is Marlin) and `raft_layers` >0 with supports off (per 005's mapping row). Multi-material is deliberately silent — do not add a warning for it.

## Resolution

Grilled 2026-07-17. **The premise inverts, the same way ticket 012's did: there is no warning UI in v1 — config warnings are a dev instrument, not a user surface.** The question this ticket was chartered to answer ("a one-time dialog, a sidebar notification, the progress area, or a dedicated panel?") has the answer "none of them", so grouping, dedup across re-slices, and "don't show again" all dissolve with it — a log needs none of the three.

**Audience — log only, no UI (decided).** The fork ships zero user-facing config warnings. Accepted consequence, put explicitly to the user and confirmed: a Klipper user selects a Klipper preset, slices, and receives Marlin G-code **with no on-screen indication**. This is consistent with the map's standing Notes and with 012 — PNP gaps are scheduled to close before the fork has users other than the developer, so the warnings are scaffolding for the person growing the mapping table, not safety rails for an end user who does not exist yet. 012's two handover cases (`gcode_flavor`, `raft_layers`) are therefore recorded in the log rather than surfaced. The consequence of 013's answer is that **the fork's v1 has no gap-communication channel to the user at all** — 012 deferred that signal here, and here it is declined.

**Noise control — filter Tier D against the defaults (decided).** Load-bearing discovery: `preset_bundle->full_config()` returns **all 925 keys with defaults filled in**, not just the ones a user or preset touched. A naive reading of 005 warns on all ~830 Tier-D keys on every slice, including `raft_layers` sitting untouched at 0. The translator therefore diffs against `FullPrintConfig::defaults()` and records a Tier-D key **only when its value differs**. Mechanism already exists and needs nothing new: `ConfigBase::diff(const ConfigBase&)` (`src/libslic3r/Config.cpp`) returns the changed-key set; `DynamicPrintConfig::new_from_defaults_keys` (`PrintConfig.cpp`) builds the comparand. Effect: ~830 records/slice → typically <20.

**Filter scope — Tier D only (decided).** The non-default filter exists solely to suppress the untouched defaults `full_config()` fabricates. It does **not** apply to Tier-B `lossy-fallback` rows, which are only ever emitted when the translator actually took a fallback branch — there is no noise there to suppress. This is not a cosmetic distinction: **Orca's default `seam_position` is `spAligned`** (verified: `set_default_value(new ConfigOptionEnum<SeamPosition>(spAligned))`, `PrintConfig.cpp`), and `aligned` is precisely the value 005 records as having no PNP equivalent. Under a uniform filter the single most common lossy fallback in the whole table would be invisible on every default slice. The other two known cases survive a uniform filter and did not force this: `raft_layers` defaults to 0 (`INITIAL_RAFT_LAYERS`, `PrintConfigConstants.hpp`) and `gcode_flavor` defaults to `gcfMarlinLegacy`, so a raft or a Klipper preset both read as non-default.

**Classes — four, classified opportunistically (decided).** 005's three-way classification gains a fourth:

| Class | Meaning |
|---|---|
| `unsupported-feature` | Key's feature is absent from PNP (raft, non-Marlin flavor, SLA, calibration) |
| `not-yet-mapped` | No table row yet — **the default for any key absent from the table** |
| `no-op` | Cannot affect PNP output (GUI-only, device/AMS keys) |
| `lossy-fallback` | **New.** Key *was* sent, but the value was substituted (Tier-B fidelity loss) |

`lossy-fallback` is distinct from `unsupported-feature` because sent-with-a-different-value and never-sent are different debugging stories, and lossy rows are individually fixable pnp-side. Known members: `seam_position: aligned` → `nearest`, and `support_base_pattern_spacing` → `support_density` should the inversion formula stay unverified at implementation time (005 leaves it open).

**No up-front classification pass.** Keys default to `not-yet-mapped`; a class is written onto a key only when there is a reason to (hit while dogfooding, or a known feature gap). An ~830-key hand-classification project whose only consumer is a log file is not worth it, and the non-default filter already holds the volume down. All four classes are logged, `no-op` included — the jsonl is a complete record of what the translator decided about every non-default key.

**Sink — append-only jsonl in `data_dir()` + a summary line in Orca's log (decided).** One accumulating `<data_dir>/pnp-config-warnings.jsonl`, one JSON object per slice (`ts`, `plate`, `preset`, `warnings[]`), plus one human-readable summary line into the same Orca log ticket 006 already sends stderr tails and fatal-error detail to. The data_dir location is the point: ticket 003 puts per-slice inputs in a **temp dir that gets cleaned**, so a sidecar written there could not answer "which keys do I keep hitting?" — which is the instrument's entire purpose. Rejected: per-slice file next to the temp config (dies with the temp dir); jsonl only (invisible unless you know to look).

**No rotation in v1 (decided).** <20 records/slice at ~1–2 KB means ~2 MB per 1000 slices. Knowingly unbounded; revisit if it ever matters.

**Translator stays pure (decided).** `PnpConfigTranslator::translate()` takes a `DynamicPrintConfig` and returns `{json, std::vector<PnpConfigWarning>}` — no I/O, no logging. `PnpSlicingProcess` (ticket 003) writes the jsonl. Keeps the translator unit-testable, and preserves 005's vector as a live seam: if a UI ever wants these warnings it reads the return value rather than re-parsing a log.

**Dedup / suppression / grouping — none.** All three were premised on a UI. It is a log; each slice is a record.

**New pnp handoff item 16** (`seam_position: aligned`): promoted from 005's asset-local handoff list into [the handoff asset](../assets/handoff-pnp-gap-implementation.md), and escalated — 005 filed it as a "candidate future seam mode", but because `aligned` is Orca's *default*, every default slice silently loses seam fidelity. The framing note at the head of that asset ("the fork's only gap-communication channel is a slice-time warning") is corrected by this ticket: there is no such channel.

## Reopened past v1 (2026-08-10)

**The "no UI" answer is reversed.** With the fork past v1, the user-facing warning is implemented: `PnpSlicingProcess::start()` filters the translator's warning vector through the shared `filter_pnp_config_warnings()` (F03) and pushes a `WarningNotificationLevel` CustomNotification listing the affected keys, grouped by class (`unsupported-feature` / `lossy-fallback` / `not-yet-mapped`), via `format_pnp_config_warning_message()`. The jsonl sink is unchanged and remains the full record; the notification is a summary (each list capped at 5 keys). `no-op` records stay log-only — a by-design no-op has no behavioral loss and is noise to a user. The Tier-D default filter applies to the UI exactly as to the log. The accepted-consequence paragraph above ("a Klipper user receives Marlin G-code with no on-screen indication") is superseded: that user now sees a warning naming `gcode_flavor`.

**Second surface (2026-08-10): the settings tabs tint unimplemented options.** `PnpConfigTranslator::pnp_key_is_unimplemented()` (the handled-key set extracted from `translate()` into `pnp_handled_keys()`, one source of truth) drives a yellow label tint in `Tab::update_label_colours()` / `Tab::decorate()`: any option with no PNP equivalent renders its label amber (`#E6A800` dark / `#B8860B` light), overriding the modified/system state colors (the undo button still shows modified state). `compatible_prints`/`compatible_printers` are excluded — preset bookkeeping, not features. The tint is key-based and static, so it shows regardless of the current value; the slice-time notification remains the value-aware signal.

**Third surface (2026-08-10): infill patterns go through pnp's claim system.** Grilled (user: "pnp does it through the claim system — the orca gui has to adjust"): instead of expecting pnp to consume Orca's pattern enums, the GUI adapts. pnp selects the infill module per fill-role claim via `ResolvedConfig.{top,bottom,bridge,sparse}_fill_holder` (CLI-bound, default `rectilinear-infill`; unknown holder strings make the module emit nothing for that role — silent loss, so the remap table is closed). Changes:
- **Translator rows (Tier B):** `sparse_infill_pattern`→`sparse_fill_holder`, `top_surface_pattern`→`top_fill_holder`, `bottom_surface_pattern`→`bottom_fill_holder`, value-remapped to the module holding the claim: sparse offers `rectilinear-infill`/`gyroid-infill`/`lightning-infill` (the only holders of `claim:sparse-fill`); top/bottom offer `rectilinear-infill` only (gyroid/lightning hold no top/bottom claim). All other values — incl. Orca's defaults `crosshatch` and `monotonic` — fall back to `rectilinear-infill` with a lossy-fallback warning (defaults warn; accepted, consistent with the `seam_position` precedent). `pnp_bridge_fill_holder` (new fork key, Quality page → Bridging) → `bridge_fill_holder`. `infill_shift_step` → Tier A identity (pnp's rectilinear-infill consumes it; was falsely tinted). `internal_solid_infill_pattern` stays **unmapped** — pnp has no internal-solid claim, so there is no honest holder target; revisit when the pnp region→claim mapping for internal solid is verified.
- **Value-aware dropdown tint:** the infill-pattern dropdowns (sparse/top/bottom/internal-solid) tint the *choices* with no pnp module amber (`DropDown::Item::text_color_override` + `ComboBox::SetItemTextColor`; set at field build from `pnp_pattern_value_supported()`). The label tint clears for the mapped keys; `internal_solid_infill_pattern` keeps the label tint (unmapped) and has all choices tinted.
- Tests: remap rows, fallback+warning semantics, `pnp_pattern_value_supported`, handled-set agreement, `pnp_bridge_fill_holder` rename.
