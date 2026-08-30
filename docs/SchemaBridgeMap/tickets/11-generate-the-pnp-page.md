---
title: Build the generated PNP settings page
status: closed
type: task
assignee: Adelino Penedo
blocked-by: [04, 10]
---

## Question

Build the page ticket 04 decided, per
[the prototype](../assets/04-pnp-page-prototype.md).

- One page in `TabPrint`, added after the existing pages via `add_options_page`.
- One loop over `pnp_registered_config_keys()`, bucketed by `def->category` (the schema `group`),
  optgroups ordered by descending key count with alphabetical ties, each key appended with
  `append_single_option_line`. No per-type control code: ticket 02's registry already fills the
  `ConfigOptionDef`.
- Mode: `advanced=false -> comAdvanced`, `advanced=true -> comExpert`. This replaces the blanket
  `comExpert` placeholder in `PnpConfigKeyRegistry.cpp:68`, so the registry must start carrying
  the schema's `advanced` flag through to `def->mode`.
- Skip list: `slice_has_paint` renders no control (host-injected). Knowingly a fork-side curated
  constant; see the map's fog entry on the pnp-side `internal` alternative.
- Labels/tooltips/sidetext are used verbatim, **not** passed through `_L()`.
- The page is not added at all when the probe succeeded, zero page keys registered, and the
  carrier is empty.

The degraded / read-only state is ticket 12, but decide here where the seam between the two
rendering paths sits, since both live on the same page.

## Progress — implementation restored and verified (2026-08-30)

The session that started this ticket was lost to a drive move (its working tree never
committed). Its functional state was restored from the snapshot commit `7f599ddb9c`,
minus a whole-file reformat churn the editor's formatter pass had entangled — that churn
was reverted; every file now differs from its pristine state only by the functional
change (285 insertions, 8 deletions across 6 files, commit `fa28cee4c4`).

Implemented:
- `PnpConfigKeyDef::advanced` rides the wire (`PnpConfigKeys.cpp`), and
  `pnp_register_config_keys` derives `def->mode` from it (replaces the blanket
  `comExpert` of ticket 02).
- `pnp_page_groups()` in `PnpConfigKeyRegistry.cpp`: buckets registered keys by
  `def->category`, optgroups ordered by descending key count with alphabetical ties,
  keys alphabetical inside, host keys collapsing into one empty-category group;
  `matches_orca_page` flags groups named like Orca pages. GUI-free, unit-driven.
- `pnp_host_injected_skip_keys()` (`PnpConfigKeys.cpp`): `slice_has_paint` only.
- `TabPrint::build()` appends the **PNP Backend** page when sealed, non-empty, and
  not fully routed away; optgroups carry the schema group verbatim; labels/tooltips/
  sidetexts are used verbatim (never through `_L()`).
- Tests: the advanced flag (parse + mode tier), six page-layout sections, and a
  hidden `[live-schema]` opt-in case driven by `PNP_LIVE_SCHEMA`.
- pnp-side (submodule `263b81d3` → `ffa0302c`): `toml_default_to_wire` fixes string
  and array manifest defaults reaching the wire quoted/bracketed; regression test
  `module_defaults_reach_the_wire_unquoted`; 14 trailing NUL bytes dropped from
  wave-overhangs.toml. `cargo clippy -p slicer-scheduler --all-targets -D warnings`
  and `cargo xtask check-literals` pass; narrow test green.

## Resolution (2026-08-30)

**Everything shipped above, restated as the answer.** The page is built and verified.
Submodule now `263b81d3` → `ffa0302c`, fork `fa28cee4c4`.

**Where the ticket's spec landed, point by point:**

- *One page after the existing pages:* `TabPrint::build()` calls `add_options_page(L("PNP Backend"),
  "custom-gcode_other")` inside `pnp_config_keys_sealed() && !pnp_registered_config_keys().empty()`,
  after Orca's own pages (the last lines of the method).
- *Bucketing and order:* `pnp_page_groups()` implements exactly the prototype's rule — optgroups by
  descending key count with alphabetical ties, keys alphabetical inside each group. Host keys with
  no schema `group` collapse into one empty-category group rather than disappearing.
- *Mode tier:* the registry now derives `def->mode` from the schema `advanced` flag
  (`false -> comAdvanced`, `true -> comExpert`, absent -> false, matching pnp's declaration-site
  default). Nothing generated reaches Simple mode.
- *Skip list:* `pnp_host_injected_skip_keys()` = `slice_has_paint` only. The def still registers
  (the translator sends the value); it just renders no control. The pnp-side `internal` alternative
  stays in the map's fog, taken if a second host-injected field appears.
- *Verbatim labels:* the generation loop passes the schema strings without `_L()`.
- *The not-added-when-empty clause:* the condition above covers it — with the probe succeeded and
  zero page keys registered, no page exists. The degraded carrier path is ticket 12's subject.

**Seam decision for ticket 12.** The boundary between the two rendering paths is the *registry
seal*, i.e. exactly the data `TabPrint::build()` already branches on. Ticket 11 owns only the
page that exists when the seal is set and at least one generated control would render; ticket 12
owns the other branch of the same page — when the seal is absent (no probe, no `pnp_cli`), the
method gains a second path that adds the page read-only from ticket 03's preserved-key carrier
under an error banner. Two consequences worth recording:

1. The degraded path reuses the same `add_options_page` title and group titles, so a flapping
   probe flips content, not structure.
2. The normal path is *not* gated on the carrier being empty; a successful probe plus a carrier
   with unresolvable keys still renders the generated controls, and ticket 12's purge question
   (map fog) addresses the orphans separately.

**Verification run this session:**

- `xmake -j4` build ok on a fresh tree; the dependency graph re-derived against a warm conan
  cache, and guests + `pnp_cli` rebuilt by `xmake pnp` (44 guests + 23 modules staged to
  `target/dist/developer` — submodule `ffa0302c`).
- `pnp_config_translator_tests`: 1006 assertions in 24 test cases, all passing, including the six
  new page-layout sections and the advanced-flag cases.
- `pnp_runtime_tests`: 282 assertions in 31 cases. `libslic3r_tests`: 48 771 assertions in 142
  cases (the additive `PrintConfig.hpp` shape).
- Live probe gate: `PNP_LIVE_SCHEMA` pointed at a real `target/dist/developer/pnp_cli.exe module
  config-schema` document (wire 1.2.0, 23 modules + 93 host rows): **58 page keys registered,
  57 rendered + `slice_has_paint` skipped**, 18 optgroups — Wave Overhangs 10, Support 9 *(orca
  page name)*, Quality 7 *(orca page name)*, Arachne 5, Walls 4 *(orca page name)*, … empty-group 2
  hosts, Speed 2 *(orca page name)* — order rule holds, `slice_has_paint` declared and diverted.
  Ticket 04's smoke had measured 54/53 against the older wire; the live-1.2.0 delta is new
  declarations, not drift. (An earlier apparent mismatch showed up only when I probed *without*
  `--module-dir`, which drops module-holding manifests from the reply — the probe contract needs
  the flag pointed at the module root; noted for ticket 15's CI job.)
- Submodule gates: `cargo clippy -p slicer-scheduler --all-targets -- -D warnings` clean,
  `cargo xtask check-literals` 0 violations, regression test
  `module_defaults_reach_the_wire_unquoted` green, and the old behavior's failure mode proven
  (`toml::Value::String::to_string()` emits `"gcode"` with literal quotes).

**Manual smoke of the rendered page** (launching the app, eyeballing the PNP Backend tab against
the live probe output above) was *not* performed — it needs an interactive desktop session and is
the remaining gap before this ticket could be called user-verified. Everything derivable is
verified; ticket 12's rendering work and the smoke both touch the same page, so the natural point
to do the smoke is right after ticket 12 lands its degraded path. The ticket ships with that
residual, not hidden as done.

**Incidental fix riding along (load-bearing for everything above):** the enum/string/percent
controls on this page had *no possible correct default* before the submodule fix — every manifest
string default reached the fork quoted (`"gcode"`), failed Orca's deserializer, and fell to the
zero value. The one-line regression test pins it.

Verification: the map's discipline — compile, targeted Catch2 in `pnp_config_translator` or
`pnp_runtime`, manual smoke of the page against a real `pnp_cli` probe.
