---
labels: [wayfinder:map]
title: Schema-driven config bridge and PNP settings page
---

# Schema-driven config bridge and PNP settings page

## Destination

The fork's config surface becomes **schema-driven**, and the features from submodule
bump `1238ef02 -> dbf3449c` become reachable — **built, not just planned**. Three things
are true when this map is done:

1. `pnp_cli module config-schema`, probed live at startup, is the authority on which Orca
   settings pnp actually binds. The amber unimplemented tint, the warning classes, and the
   handled-key set all derive from it, so a future pnp module key changes the GUI with
   **zero fork edits**.
2. Every pnp module key with **no** Orca counterpart has a real control on a generated PNP
   settings page, registered into `print_config_def` at startup, round-tripping through
   presets and project 3mf like any Orca key.
3. The new pnp features (support families, bridge parity, part-cooling renames, retired
   `support_density`) are correct in the settings tabs, the support-preview/gizmo path, and
   the warning surfaces.

## Notes

- Domain: C++17/wxWidgets (OrcaSlicer fork, this repo) + Rust CLI backend (`pinch_n_print_cli/`
  submodule, binary `pnp_cli`). Successor map to [BootstrapMap](../BootstrapMap/map.md), whose
  decisions (esp. tickets 005 and 013) are this map's starting state.
- **This map executes.** Overrides wayfinder's plan-only default: tickets decide *and* build.
  BootstrapMap discipline — one commit per ticket on `pnp/main`, compile verified per ticket,
  targeted Catch2 tests where behaviour changes (`pnp_config_translator`, `pnp_runtime`
  suites), manual smoke at batch boundaries.
- HITL tickets: use `/grilling` and `/domain-modeling`. Prototype tickets: `/prototype`.
- Tracker: local markdown. Tickets in `docs/SchemaBridgeMap/tickets/NN-<slug>.md`; frontmatter
  `status/type/assignee/blocked-by` is authoritative. Claim = set `assignee`. Frontier = open,
  unassigned, all `blocked-by` closed.
- **Both repos are in play.** A ticket may land a commit in `pinch_n_print_cli/` and bump the
  submodule (precedent: the `support_type` claim work, submodule `1238ef02`). Prefer fork-side
  answers; call out every pnp-side commit explicitly in the ticket.

### Corrected by ticket 01 (2026-08-28)

- **`config-schema` is not the whole authority.** pnp resolves config keys through four channels
  and the probe reports only module manifests (171 of 236). The other 65 are host keys
  (`ResolvedConfig` `cli`/`cli_opt` rows, `FeedrateConfig` speeds, `[host_runtime]`), two
  extensions-map keys (`support_type`, `support_family`), and one undeclared module read
  (`infill_shift_step`). Every "derive it from the live schema" decision below needs "the schema
  plus the host universe" substituted; ticket 02 owns choosing where that universe comes from.
- **`schema_version` is not a drift signal** — it stayed `1.0.0` across the bump's module split,
  39 added and 6 removed manifest keys. The degradation rule's "major mismatch" branch will not
  fire on drift of this kind; only ticket 06's per-key diff will.

### Locked by grilling (2026-08-28)

- **Routing rule.** A pnp module key is *Orca-based* if its key string already exists in
  `print_config_def` (identity row — pnp's recent "rename part-cooling keys to Orca names"
  move makes name-identity the primary integration mechanism), *or* if the curated table has a
  rename/remap row for it. Otherwise it is *new to PNP* and gets a control on the generated
  PNP page. Name match -> curated table -> PNP page, in that order.
- **Key registry.** pnp module keys are registered into `print_config_def` (de-const'd) at
  startup, after the schema probe and before presets load, plus the `Preset.cpp` key lists.
  They become first-class Orca keys: preset save/load, 3mf, diff, undo work for free. Accepted
  cost: a mutable global, and a key set that varies by installed modules.
- **Persistence.** Orca presets *and* project 3mf, like any key. A preset/3mf carrying keys for
  an absent module **preserves them untouched and round-trips them**, plus one notification on
  load naming the unresolvable modules.
- **Degradation.** pnp_cli missing or config-schema major mismatched -> the PNP page renders
  **read-only with an error banner**, showing whatever the project/preset carries. No cache.
  Nothing vanishes; nothing is editable.
- **Drift.** On each probe, every curated-table target is diffed against the live schema; dead
  targets (e.g. this bump retiring `support_density` for `support_base_pattern_spacing`) are
  logged to the existing `pnp-config-warnings.jsonl` sink and raise one notification.
- **End state being future-proofed toward.** Every Orca key eventually reaches pnp; the fork is
  the accurate scoreboard. As pnp modules declare more keys the amber tint recedes automatically
  with no fork edit, and the warning log names exactly what is still unbound. Not a curated
  "mappable set" with a permanent never-applicable class.
- **UI surfaces in scope:** settings tabs + generated PNP page, support preview / gizmo,
  notifications and warning surfaces. G-code preview (roles, legend, colours) is **not**.

### Standing facts

- `pnp_cli module config-schema` already emits a **GUI-facing** wire contract
  (`crates/slicer-scheduler/src/manifest.rs::build_config_schema_json`): per module, per field
  `key/type/default/min/max/step/display/description/group/unit/advanced/values/max_length/
  min_list_length/max_list_length/validate/tags`. Wildcard entries (`<prefix>:*`) are excluded.
  The fork currently reads only `schema_version` from it (`PnpBackend`), and uses the raw doc
  as a drop-bad-keys guard (`PnpConfigTranslator::apply_schema_guard`).
- **Superseded by ticket 05:** the handled-key set was the static `TIER_A_KEYS` array plus
  hand-written rows in `src/slic3r/GUI/PnpConfigTranslator.cpp`. It is now derived from the live
  key universe plus `translate()`'s own `routed` provenance map; `TIER_A_KEYS` survives only as
  the unprobed fallback. `pnp_key_is_unimplemented()` still drives the amber label tint in
  `Tab::update_label_colours()`/`Tab::decorate()`, and returns true for every key when no probe
  has run.
- `print_config_def` is `extern const PrintConfigDef` (`src/libslic3r/PrintConfig.hpp:702`);
  preset key lists are static (`src/libslic3r/Preset.cpp`); serialization uses
  `by_serialization_key_ordinal`.
- The fork-key precedent is `pnp_bridge_fill_holder`: `PrintConfig.cpp:2131` (def) +
  `Preset.cpp:1058` (key list) + `Tab.cpp:2765` (control) + translator row.
- **Corrected by ticket 03:** `load_from_json` does *not* fail the whole project config on an
  unknown key. `PrintConfigDef::handle_legacy` (`PrintConfig.cpp:8413`) clears any key absent
  from `print_config_def`, so `set_deserialize_nothrow` records the name in
  `unrecogized_keys` and returns success — the key is **silently dropped**, not fatal, and
  `UnknownOptionException` is unreachable on this path. The BootstrapMap ticket 008 comment at
  `bbs_3mf.cpp:2746` states the same wrong premise; its SLA pre-pass is still correct and
  still wanted, but for an unverified reason.
- Submodule bump `1238ef02 -> dbf3449c` brought: tree/traditional support **families**
  (`SupportPlanIR` 2.0.0 -> 2.2.0, `SupportPlanRole::BaseInterface`,
  `ExtrusionRole::SupportBaseInterface` -> `;TYPE:Support interface`), support pattern/threshold
  config keys (`support_threshold_angle` + legacy `support_overhang_angle` alias),
  `support_density` **retired** for `support_base_pattern_spacing`, bridge parity packets,
  and part-cooling keys renamed to Orca names.

## Decisions so far

<!-- one line per closed ticket: [title](tickets/NN-slug.md) — gist -->

- [Schema-to-table-to-print_config_def reconciliation inventory](tickets/01-schema-table-inventory.md)
  — `config-schema` reports 171 of pnp's 236 keys (four declaration channels, one probed), so
  it cannot be the sole authority; 113 identity / 24 table-routed / 34 new-to-PNP, 62 further
  host keys with no metadata; 6 dead curated rows including `enable_support` (supports never
  enable); `schema_version` static across the bump. Baseline 116/845 Orca keys reach pnp.

- [Runtime registration of pnp module keys into print_config_def](tickets/02-runtime-config-def-registration.md)
  — pnp now emits the whole key universe (`config-schema` wire 1.0.0 -> 1.1.0 adds a `host`
  array and per-field `scope`), so the fork holds no key table; `print_config_def` is de-const'd
  behind a one-shot sealed seam called before the PresetBundle, ordinals are safe because only
  the in-memory undo stack reads them, and the type mapping is total via
  `ConfigOptionEnumGeneric`'s runtime keys map. Curated-table targets are excluded by running
  the translator, not by restating the table. Also: `machine_max_jerk_*` are Orca keys, not
  pnp-only — ticket 01's scrape missed loop-built `add()` sites.

- [Preserve unknown pnp keys through preset and 3mf load](tickets/03-unknown-key-preservation.md)
  — the premise was wrong: `handle_legacy` clears any key absent from `print_config_def`, so an
  unresolvable key is **silently dropped**, not fatal. Fixed by a document-attached carrier
  (`Model::pnp_unknown_config`, `Preset::pnp_unknown_config`) holding the raw JSON fragment,
  filled by an opt-in out-param on `load_from_json` and re-emitted by `save_to_json`. Never
  enters `DynamicConfig::options`, so `diff()` and the dirty state cannot see it. Both JSON
  paths only; `.ini` and the G-code CONFIG_BLOCK still drop. New `UnresolvedPreserved` warning
  class, keys named but never modules — pnp keys are unnamespaced and the formats store no
  module ids.

- [PNP settings page layout and control generation](tickets/04-pnp-settings-page-layout.md)
  — one page in Print Settings, optgroups from the schema `group` with no merging into Orca's
  own pages; control generation is free because ticket 02's registry already fills the
  `ConfigOptionDef` and `append_single_option_line` reads it. `advanced` maps false ->
  `comAdvanced` / true -> `comExpert`, so nothing generated reaches Simple mode; runtime labels
  are untranslated by construction. Host-key metadata gets declared in pnp's DSL beside `@scope`
  (`host-keys.toml` mirrors only 11 of 20). The locked degraded state could not be implemented as
  written — with no probe there are no options to render — so the page gains a second path that
  reads ticket 03's carrier read-only under a banner. 54 registered / 53 rendered; section F is
  20, not 28, because `machine_max_speed_*` are loop-built Orca keys; every page key is
  print-scoped. Two bugs: `handle_legacy` erases `support_sharp_tails` before it ever checks the
  def, and `slice_has_paint` must be skipped via a fork-side list the wire cannot yet express.

- [Derive the handled-key set from the live schema](tickets/05-derived-handled-set.md)
  — "handled" is now derived, not listed: `k` is handled if the backend declares `k` itself
  (identity) or `translate()` routed `k` to a key it declares. `PnpTranslationResult` gains a
  `routed` provenance map so both halves and `translate()` read one source and cannot drift.
  The Tier-A pass is derived too — every declared key is copied under its own name, running
  before the Tier-B rows so unit fixes still win — which is what makes "zero fork edits" true
  and which repairs, for free, ticket 01's five bump-regressed keys **plus `enable_support`**,
  whose row wrote a non-existent key since it was authored, so supports never switched on in
  pnp. Ticket 01's "the probe does not report host keys" amendment is discharged: wire 1.1.0
  reports them. Measured 117 -> 176 handled of 665, 115 -> 180 keys sent, 0 lost. No probe ->
  tint everything. `TIER_A_KEYS` demoted to the unprobed fallback.

- [Startup drift reconciliation against the live schema](tickets/06-startup-drift-reconciliation.md)
  — the fork now diffs the curated table's rename/remap targets against the live universe after
  every probe (and after a Preferences re-probe), writing a `"event": "dead-curated-target"` record
  per dead row to `pnp-config-warnings.jsonl` plus one `ImportantNotificationLevel` notification
  naming the Orca setting. Identity edges, no-target rows and an empty universe are excluded; the
  inverse direction is silent by construction because only table targets are walked. Ticket 05's
  "ticket 01's 14-false-positive warning is discharged" holds only against a wire-1.1.0 backend, so
  the whole diff is gated on the document carrying a `host` array. The build-time gate was rejected
  as a build dependency (no real schema without `cargo xtask dist`) and taken as a hidden
  `[live-schema]` Catch2 case driven by `PNP_LIVE_SCHEMA`. Measured against a freshly staged
  `pnp_cli` (wire 1.1.0, 93 host entries): **exactly ticket 01's six dead rows, zero false
  positives** — the four part-cooling renames, `support_interface_spacing`, and `enable_support`;
  `support_density`'s row routes to nothing and so cannot be dead. Also found the tree's staged
  dist predated the wire-1.1.0 commit.

## Not yet specified

- **Migration of the curated table's existing rows.** Ticket 05 answered the tier question:
  Tier A is gone as a concept (derived from the universe), Tier D is derived, and Tier B
  survives as the only hand-written layer — the rows that fix a unit, respell an enum, or
  fan one Orca key out to several pnp keys. What is still unspecified is how far *that*
  layer can shrink, which is the same question as the 86 mismatched identity rows below.

- **A value-semantics audit of the identity rows.** Ticket 05 widened `translate()` to send
  every declared key by name — 65 more than before — while ticket 01 §B found 86 of 113
  identity rows carry a type, unit or range mismatch against Orca's definition.
  `apply_schema_guard()` contains this (a rejected value is dropped and logged rather than
  slicing wrong), but its coverage is not total: host keys reach the wire with no `min`/`max`,
  so range errors on those pass through until ticket 10 lands the metadata. Nobody has walked
  the 86 rows to say which are real losses.

- **pnp should declare the keys it reads outside the manifest.** `support_type`,
  `support_family` (read from `resolved_config.extensions`) and `infill_shift_step` (a bare
  `config.get()`) are read but declared through no channel the wire can express, so ticket 05
  carries them in a three-entry `UNDECLARED_LIVE_KEYS` array — knowingly the curated table this
  map exists to delete. Same shape as the `internal = true` handoff below; a pnp-side commit
  deletes the array.

- **pnp should declare module role claims on the wire.** Ticket 05 checked whether
  `pnp_pattern_value_supported()` could be derived and found it cannot: a `schema` entry
  carries only `module` and `fields`, and which module holds `claim:sparse-fill` appears
  nowhere. So the pattern → fill-role-holder tables stay hand-written, and the per-item
  dropdown tint stays a fork-side guess. Declaring claims per module would make the last
  value-level piece of the curated table derivable.
- **Per-object / modifier-volume overrides for pnp module keys.** Orca supports per-object
  config; whether a generated pnp key participates is unexamined. Ticket 02 registered keys into
  `print_config_def` and the preset lists but touched no per-object option list, so today they
  do not participate.
- **Whether pnp should declare its Orca correspondence in the manifest.** The rejected
  alternative to name-matching (an `orca_key` field on the wire). Ticket 02 established the
  pnp-side handoff channel, so this is now cheap if ticket 04 or 05 finds name-matching wanting
  — section B's 86 mismatched identity rows are the place that would show up.
- **What the bump's support-family work needs from the settings UI beyond key routing** — e.g.
  whether tree-support's own knobs deserve deliberate placement on Orca's Support page rather
  than falling to the PNP page. Depends on tickets 01 and 07.
- **Verification story for a schema-derived UI.** Ticket 02 established the unit-test shape —
  a synthetic schema document driving the pure parser, plus one case that registers and asserts
  the whole resulting state, since the seam is one-shot per process. What is still unspecified is
  how to test the *rendered* page, and whether anything checks the fork against a real `pnp_cli`
  probe rather than a synthetic document. Ticket 04 designed the page but wrote no code, so this
  stays open and now has a concrete subject: tickets 11 and 12 both end in "manual smoke", which
  is the gap. Ticket 06 sharpened the second half: it left a hidden `[live-schema]` Catch2 case that
  runs the drift diff against a real document from `PNP_LIVE_SCHEMA`, so the piece still missing is
  a **CI** job that stages `pnp_cli` and runs it — which cannot be phrased sharply until ticket 08
  settles where `cargo xtask dist` puts things and whether CI runs it at all. Ticket 06 also found
  the staged `target/dist` binaries are older than the submodule working tree, which is itself an
  argument for the fork checking the wire it actually got rather than the wire it expects.

- **Whether pnp should declare host-injected fields on the wire.** Ticket 04 needed to keep
  `slice_has_paint` off the page and found the wire cannot say a field is host-injected — no tag,
  no flag, only the words "(host-injected)" inside its `display` string. Resolved for now with a
  fork-side skip list, knowingly the curated table this map exists to delete. A per-field
  `internal = true`, following ticket 02's `scope` precedent, is the pnp-side answer; take it if a
  second host-injected field appears.

- **Whether the settings page needs a purge affordance for preserved keys.** Ticket 04 gives
  preserved-but-unresolvable keys a read-only surface, but only in the degraded state (ticket 12).
  In the normal state they remain invisible and unremovable, so a project still accumulates them
  with no way out but editing the file.

- **What the fork should do about a `pnp_cli` older than the wire it needs.** Ticket 06 found the
  staged dist answering `config-schema` at wire 1.0.0 while the submodule tree emits 1.1.0, and had
  to gate its diff on the `host` array to avoid 14 false dead rows. But the same old wire silently
  degrades more than drift reporting: 65 host keys drop out of the key universe (so they tint amber
  and never register), and the generated PNP page loses its host-key controls. Today that is
  indistinguishable from a backend that genuinely declares less. Whether the fork should detect the
  old wire and say so — and whether that is the degraded state ticket 12 renders or a third case —
  is unexamined; ticket 02's grilling may already have taken a position worth re-reading first.

## Out of scope

- **G-code preview roles, legend, and colours** — ruled out at charting. The bump's new
  extrusion role maps onto Orca's existing `;TYPE:Support interface`, so the preview needs no
  work; if that proves false it is a fresh effort.
- Native slicing, SLA, calibration — settled and removed by BootstrapMap.
- pnp_studio (Bevy frontend).
- Feature parity beyond slice + preview + support preview.
