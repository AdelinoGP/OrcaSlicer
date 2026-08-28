---
title: Startup drift reconciliation against the live schema
status: closed
type: task
assignee: Adelino Penedo
blocked-by: [05]
---

## Question

Make the fork notice when pnp moves out from under the curated table.

On each successful schema probe, diff every curated-table **target** (the pnp-side key each
rename/remap row writes) against the keys the live schema declares. A target the schema no longer
declares is a dead row: the Orca setting silently stops reaching pnp. This bump already produced
one — `support_density`, retired for `support_base_pattern_spacing`.

Work:

- Compute the dead-target set at probe time and record it to the existing
  `<data_dir>/pnp-config-warnings.jsonl` sink (its own record shape — this is a fork-health
  event, not a per-slice config warning) plus one line in Orca's log.
- Raise **one** notification naming the dead targets. This is a developer-facing signal, not an
  end-user one; decide the level and wording accordingly.
- Decide the inverse direction: schema keys that are neither identity matches nor table targets
  are, by the map's routing rule, PNP-page keys — so they are *expected*, not drift. Confirm the
  reconciliation does not report them.
- Decide whether this also runs as a **build-time** check against the pinned submodule. The
  grilling chose runtime reconciliation; a test would additionally make drift uncommittable. If
  taken, it belongs with the submodule's staged `pnp_cli`, and it must not make the build depend
  on a cargo step that may not have run.

One commit, compiles, test covering a fixture schema missing a known table target.

## Amended by ticket 01

Two corrections from the inventory
([`assets/01-schema-key-inventory.md`](../assets/01-schema-key-inventory.md)):

- **Diff against the union, not the schema.** A schema-only dead-target check reports 22 dead
  rows at `dbf3449c`; only 6 are real. 14 resolve through host keys the probe never reports, and
  2 (`support_type`, `infill_shift_step`) are consumed through the extensions map and an
  undeclared `config.get()` respectively. Both classes must be exempt or the notification is
  noise from day one.
- **`schema_version` is not a drift signal.** It is `1.0.0` at both `1238ef02` and `dbf3449c`,
  across a module split, 39 added manifest keys and 6 removed ones — five of which broke live
  translator rows. The per-key diff is the only thing that catches this, which strengthens the
  case for the build-time check this ticket already lists as optional.

The six real dead rows are ticket 09's to repair; this ticket is the mechanism that would have
caught them.

## Amended by ticket 05

The input this ticket needs now exists and is unblocked:

- `PnpConfigTranslator::pnp_key_universe_from_schema()` builds the live key universe from
  both halves of the wire, so a drift check no longer has to work around the probe's blind
  spot — ticket 01's warning that a schema-only check would call 14 working rows dead is
  discharged.
- `PnpTranslationResult::routed` gives every curated source→target edge as data, so "which
  targets does the table still write, and does the backend still declare them?" is a set
  difference rather than a re-reading of `translate()`.
- `UNDECLARED_LIVE_KEYS` (`support_type`, `support_family`, `infill_shift_step`) is already
  folded into the universe, so ticket 01's finding-7 false positives cannot be reported dead.

What is left for this ticket is the *reporting*: diffing on each probe, writing dead targets
to the `pnp-config-warnings.jsonl` sink, and raising the one notification.

## Answer (2026-08-28)

**Built.** The fork now diffs the curated table against the live schema after every
successful probe and reports what no longer lands.

### The diff

Two pure functions in `PnpConfigTranslator` (`src/slic3r/GUI/PnpConfigTranslator.cpp`):

- `dead_curated_targets(routed, universe)` — every edge in ticket 05's `PnpTranslationResult::routed`
  whose target the universe does not declare, as `PnpDeadTarget{orca_key, pnp_key}`, sorted.
- `dead_curated_targets(universe)` — the same against the table as it actually stands, harvesting
  `routed` by running the **unprobed** translator over `DynamicPrintConfig::full_print_config()`.
  Same derivation `PnpConfigKeys::register_from_schema` already uses: the rows are read out of the
  code that routes them, never restated.

Three exclusions, each one a decision this ticket owed:

1. **Identity edges (`target == orca_key`) are skipped.** The identity pass is derived from the
   universe when probed and is `TIER_A_KEYS` when not, so it cannot drift against the universe in
   a way this diff would explain — and `pnp_key_is_unimplemented()` already tints such a key amber.
   Only the rename/remap rows are diffed, which is what the ticket asked for.
2. **Rows that route to nothing are skipped** — no target, so nothing can be dead.
3. **An empty universe reports nothing.** No probe is no evidence, not universal drift.

The **inverse direction is deliberately silent**: the diff only walks table targets, so a schema key
that is neither an Orca key by name nor a table target never enters it. By the map's routing rule
that key is a PNP-page key — the expected case. Confirmed by construction rather than by a filter.

### The wire-version gate — the one thing the ticket did not anticipate

Ticket 01 warned that a schema-only diff calls 14 live rows dead because they resolve through host
keys the probe never reported. Ticket 05 called that discharged, since wire 1.1.0 reports host keys.
It is discharged only *for a 1.1.0 backend*. Against an older `pnp_cli` the universe is the module
half again and all 14 come back. So `pnp_schema_reports_host_keys(doc)` gates the whole
reconciliation on the presence of the `host` array, and a pre-1.1.0 backend gets one info log line
and no diff. Version-string parsing was rejected in favour of testing for the capability itself.

### Reporting

- `log_pnp_dead_curated_targets()` (`PnpConfigWarningsLog.cpp`) appends one record per dead row to
  `<data_dir>/pnp-config-warnings.jsonl`, keyed `{"ts", "event": "dead-curated-target", "orca_key",
  "pnp_key"}`. Deliberately `event` and not `class`: a reader of the one jsonl must be able to tell
  a fork-health event from a per-slice config warning, and this record has no warning class, no
  value and no plate. Plus one `BOOST_LOG_TRIVIAL(warning)` line naming every pair.
- One notification, `ImportantNotificationLevel`, wording *"This build's PNP translation table is
  out of date with pnp_cli: these settings no longer reach the backend"*, listing the **Orca** key
  with the dead pnp target in parentheses, capped at 5 like the other formatters. Level chosen
  between the two the ticket left open: the audience is a developer and the user cannot fix a stale
  table, so not `Warning` (which would imply the print is unsafe); but the consequence *is* the
  user's — their setting does not arrive — so not `Regular` either, which fades out.
- `report_pnp_schema_drift()` lives in `PnpBackend.cpp` beside `show_failure_notification()`, and is
  called from `GUI_App::post_init()` and from the **Preferences re-probe** — after
  `show_failure_notification()`, which closes the `CustomNotification` slot on a successful
  re-probe. It re-parses the raw document each time rather than caching at probe time, so pointing
  the fork at a different `pnp_cli` re-answers the question. Silent when nothing is dead.

### The build-time check: rejected as a build dependency, taken as an opt-in test

The ticket asked whether this should also gate the build. A `xmake test` case cannot honestly do it:
the only real schema document in the tree comes from `pnp_cli`, which exists only after
`cargo xtask dist` — a step `--pnp_bundle_cli=n` skips and CI may not have run — so the suite would
fail for reasons unrelated to the fork's code. A committed fixture schema would not make drift
uncommittable either: drift arrives with a submodule bump, and the fixture would have to be
regenerated by that same bump commit, which is precisely the manual step this ticket replaces.

Taken instead: a hidden Catch2 case, `[.][live-schema]`, excluded from `xmake test`, that reads a
real document from `PNP_LIVE_SCHEMA` and requires the dead set to be **empty**. It runs the whole
mechanism against a real backend on demand and will fail until ticket 09 repairs its rows — at
which point it becomes a standing gate anyone can run. Making it a *CI* gate needs the dist staged
in CI, which is ticket 08's ground; recorded on the map's verification-story fog patch rather than
ticketed, because it cannot be phrased sharply until ticket 08 settles.

### Verification

- `xmake -j2 pnp_config_translator_tests` / `pnp_runtime_tests` / `OrcaSlicer` — all build clean.
- `pnp_config_translator_tests`: 917 assertions, 20 cases, all pass (3 new `[ticket06]` cases —
  the pure diff with its identity/no-target/no-universe exclusions, the host-key gate, and the real
  table diffed against a universe with `wall_count` removed, which reports exactly
  `wall_loops -> wall_count`).
- `pnp_runtime_tests`: 282 assertions, 31 cases, all pass (2 new `[ticket06]` cases — the jsonl
  record shape, and the notification formatter including its 5-item cap).
- **Live check against a real backend, measured.** `cargo xtask dist` restaged `pnp_cli` (the tree
  had a stale one — see below), whose `config-schema` then answered `schema_version 1.1.0` with a
  93-entry `host` array. The `[live-schema]` case against that document reports **exactly 6 dead
  rows and no false positives**:

  | Orca key | dead pnp target |
  |---|---|
  | `close_fan_the_first_x_layers` | `disable_fan_first_layers` |
  | `enable_overhang_bridge_fan` | `enable_overhang_fan` |
  | `enable_support` | `support_enabled` |
  | `fan_max_speed` | `fan_speed_max` |
  | `fan_min_speed` | `fan_speed_min` |
  | `support_interface_spacing` | `tree_support_interface_spacing_mm` |

  That is ticket 01's six — the four part-cooling renames, `support_interface_spacing`, and
  `enable_support`, whose row never wrote a real key. None of the 14 host-key-resolved rows ticket
  01 warned about are reported, so the wire-1.1.0 `host` array plus the gate do contain that class
  exactly as ticket 05 predicted. `support_density` is not listed: its row routes to nothing rather
  than to a dead name, and a no-target row cannot be dead. The case fails today by design; it is
  ticket 09's definition of done.
- No manual GUI smoke: the notification and jsonl paths are covered by unit tests, but nobody has
  seen the notification on screen.

### Finding: the staged dist was older than the submodule working tree

Before this session both `pinch_n_print_cli/target/dist/pnp_cli.exe` and
`target/dist/developer/pnp_cli.exe` answered `config-schema` with `schema_version 1.0.0` and **no
`host` array**, while the submodule working tree is at `a50bfc28` ("config-schema: report the whole
key universe"), which emits it — the staged binaries predated ticket 02's pnp-side commit. Running
`cargo xtask dist` fixed it locally, which is how the measurement above was taken; the gate meant
the fork degraded quietly rather than reporting 14 false dead rows in the meantime.

The general problem is ticket 08's: a correct `--pnp_dist_dir` pointing at a stale binary silently
drops the host half of the key universe and disables this reconciliation, and nothing says so. The
map's new fog patch on old-wire detection is the follow-on question.
