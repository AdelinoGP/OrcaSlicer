- The `advanced`-mode decision and ticket 11's verbatim-label rule carried over unchanged: the
  degraded page's strings are fork-authored and *do* go through `_L()`/translatable strings
  (unlike the live page's schema-supplied ones) — banner copy is translatable, per the map's
  localization glossary rules.

## Addendum — the PNP Backend tab crash, found and fixed (2026-08-31, commit `e646f4f14b`)

Opening the PNP Backend tab crashed the app (`orca-slicer.exe.4676.dmp`, access violation in
`Slic3r::GUI::from_u8` at `OrcaSlicer.dll+0x3d8f46`, reading address `0x20` with `rdx=0x8`).
Symbolicated against a freshly linked `OrcaSlicer.pdb` (symbolicating the dump was only
possible after an unrelated build fix — see below), the true fault chain was:

> TabCtrl::buttonClicked → tree_sel_change_delayed → activate_selected_page → Page::activate →
> reload_config → get_config_value(coString) → `config.opt_string(key)` → from_u8(nullptr str)

The root cause sits in **ticket 11's page builder, not this ticket's degraded branch**:
`pnp_page_groups()` bucketed every registered key by schema `group` regardless of preset
scope. `thumbnail_path` is declared `coString` **scope=printer** on the wire, so its value
lives in the printer preset's config, but the Process tab's page added it to a field list
reloaded against the *print* preset's `DynamicPrintConfig`. `option<ConfigOptionString>()`
returned `nullptr` for the missing key; `opt_string()`'s `->value` on `nullptr` produced the
string reference at `nullptr+0x8`, and `from_u8`'s capacity test read `0x8+0x18 = 0x20` —
the exact fault address in the dump (measured, exception parameters `[read, 0x20]`).

Audit of the whole live wire: of 14 non-print-scoped keys, 12 are Orca identity rows present
in every print config; `filament_density`/`filament_diameter` are Orca rows too. `thumbnail_path`
was the single registered-key-without-print-preset-membership — the one crasher.

**Fix:** the key registry now keeps each key's scope (`pnp_registered_key_scope`), and
`pnp_page_groups()` buckets only `Print`-scoped keys into the generated page.
Printer/filament-scoped pnp keys stay registered — preset round-trip and the translator
still carry them — they render on no tab until a page exists on their own (fog).
Page-layout tests re-pinned: non-print keys assert **absent** from the Process page, the
key-accounting equation gains the non-print term, `pnp_registered_key_scope` covered.
`pnp_config_translator_tests` 1029/25 and `pnp_runtime_tests` 282/31 green; full app rebuilt.

**Incidental, load-bearing for this triage:** `add_ldflags` feeds the *binary* link's flag
table; the shared-library link reads `shflags`, so `/MANIFEST:NO` and `/DEBUG` had been
silently dropped from `OrcaSlicer.dll` ever since the xmake cutover — the DLL shipped with no
CODEVIEW debug entry and crash dumps could not be symbolicated. Fixed with `add_shflags` too
(commit above); the resulting `OrcaSlicer.pdb` is what made this diagnosis possible.

## Question

Implement the state a user on a broken install hits first.

When `pnp_cli` is missing or `schema_version` is major-mismatched, no keys register, so the page
has no `ConfigOptionDef`s and no optgroups to render. Ticket 04 resolved this by giving the page a
second rendering path that reads ticket 03's carrier — `Model::pnp_unknown_config` and
`Preset::pnp_unknown_config` — directly, as a flat read-only key/value list under an error banner
stating that the values are preserved and written back unchanged.

Decide and build:

- Which carrier wins when the project 3mf and the active preset both carry a fragment, and whether
  the list shows the union or just the effective one.
- Whether the banner distinguishes "pnp_cli not found" from "schema version mismatch" — ticket 01
  found `schema_version` static across a bump that added 39 and removed 6 keys, so the mismatch
  branch may be unreachable in practice and worth stating as such rather than implementing blind.
- Whether this path is reachable in any state other than degraded. Ticket 04 says no — preserved
  keys are otherwise log-only — but the map's fog asked whether they deserve a purge affordance,
  and this is the only surface that lists them.

Verification: a Catch2 case driving the renderer from a synthetic carrier, plus a manual smoke
with `pnp_cli` renamed out of the dist directory.

## Resolution (2026-08-31)

**Built. The page renders read-only from ticket 03's carriers, with a per-row purge, refreshable
without a restart.**

### The three decisions the ticket posed

**Carrier precedence: both, tagged — not a union.** The key insight that settles the question is
that the two carriers are *written back independently* by ticket 03's save paths: `Preset::save`
merges the preset carrier unconditionally, `_add_project_embedded_presets_to_archive`/
`save_to_json` merge the project carrier, and neither sees the other. A key can legitimately sit
in both stores with different values, and a purge affordance would have to say which copy it
removes — so a precedence-merged union (my first implementation) was wrong: removing "the" entry
would either leave the project's copy stranded behind a purged preset, or vice versa. The list is
therefore **one row per (key, store) pair**, every row tagged ("Preserved from the active print
preset / open project"), sorted by key with the preset copy first on ties. Effective-value display
was rejected with it: showing one row per key at "the effective value" would promise a priority
the two carriers do not actually have (they are independent write-backs, not a config hierarchy).

**Banner wording.** One message covering both failure shapes: "pnp_cli was not found or is
incompatible with this build" — the `schema_version`-major-mismatch branch is measured
(Ticket §Locked-by-grilling notes) near-unreachable, `schema_version` having stayed `1.0.0` across
a bump that added 39 and removed 6 keys, so branching the copy would implement a distinction the
backend does not currently make. The banner states the contract: values shown read-only, written
back unchanged on save.

**Reachable in non-degraded states — yes, and the ticket's premise needed correcting.**
`TabPrint::build()` runs at fork time, **before any project has loaded**: gating the page on
non-empty carriers (as ticket 04 sketched it) would leave the page missing exactly when a project
carrying preserved keys is opened, until restart. So the reachability answer is: the page exists
whenever the probe failed (an always-present degraded-state signal), and the *rows* are
(re)appended at activation from whatever the carriers hold then. This also answers the purge
fog question **yes, partly**: the purge affordance exists on the degraded page — but the map's
fog asked about a *reachable normal state with surviving orphans*, and the normal/live page does
not render the carrier (ticket 11's page has no preserved-key surface). Purging there stays
fog (see below), not silently dropped.

### What shipped

- **`PnpConfigKeys.hpp/.cpp` (GUI-free, unit-tested):** `PnpPreservedKey {key, value, source}`
  and `pnp_preserved_key_rows(preset_carrier, project_carrier)` — renders both carriers into
  display rows: one row per (key, store) pair (a key in both stores appears twice; the stores are
  written back independently, so a purge must remove exactly the store shown), preset store
  first on ties, fragments rendered as the user would have typed them (`"smart"` → `smart`,
  `[1,2]` → `1, 2`), unparsable fragments shown raw so nothing the file holds is invisible.
- **`Tab.cpp`/`Tab.hpp`:**
  - `TabPrint::build()` gains the degraded branch: probe failed (seal never set) ⇒ the **PNP
    Backend** page is added with one **Preserved settings** optgroup holding the banner. Same
    page title as the live path, so a flapping probe flips content, not structure (ticket 11's
    seam, held).
  - `Tab::activate_selected_page()` calls the new `TabPrint::refresh_pnp_preserved_page()` —
    the row list re-derives from the current carriers on every activation, so a key that
    resolves (module installed, re-probe from Preferences) drops off without a restart, and a
    newly loaded project's or preset's keys appear on the next tab visit.
  - Every row line is a **full-width widget line with no option** — load-bearing: both
    `OG_CustomCtrl::init_ctrl_lines()` and the non-BBS branch of `activate_line()` dereference
    `option_set.front()` for widget lines without `full_width`. `Line`'s default ctor is a
    *separator* (which `activate_line()` skips), so banner and rows use the two-argument ctor.
  - Per row: label = raw pnp key (unresolved keys are not namespaced, so the key is the only
    honest label), value rendered by the shared helper, source named in the tooltip, and a
    **Remove** button purging exactly that (key, store) pair: erase from
    `bundle->prints.get_edited_preset().pnp_unknown_config` (the working copy the next Save
    preset writes — `Preset::save` merges it unconditionally, so no file IO is needed) or from
    `plater->model().pnp_unknown_config`; then re-render via the same `clear_pages()` +
    `activate_selected_page()` sequence a page switch runs, and mark the right surface dirty
    (`Tab::update_dirty()` + `Plater::update_project_dirty_from_presets()` for the preset,
    `Plater::set_plater_dirty(true)` for the project).
- **Purge is a data change, not a file write.** Removing a project carrier entry only guarantees
  the key is gone from the next 3mf save (ticket 03 writes the carrier back unconditionally);
  nothing is written to disk at click time — the dirty flag carries the consequences, same as
  every other settings change in Orca.

### Corrected premise in the ticket body

The banner text lives on the page even with an empty carrier, and the page exists whenever the
probe failed — not "when the carrier is non-empty" as the ticket's rendering-path sketch had it.
The `schema_version`-mismatch branch is implemented only as text, per the second bullet's
suspicion: there is no measured state in which pnp reports a different config-schema major, so
"not found or incompatible" is the honest copy.

### Verification

- New GUI-free cases: `tests/pnp/test_pnp_preserved_rows.cpp` ([preserved] tag, in
  `pnp_config_translator_tests`) — every (key, store) pair exactly once, sorted, preset store
  first on ties, list/bool/number/string fragments rendered as typed, corrupt fragment shown raw,
  both-carriers-empty gives no rows.
- `pnp_config_translator_tests`: **1023 assertions / 25 cases, all passing**
  (was 1006/24; the new `[preserved]` case adds 17 assertions).
- `pnp_runtime_tests`: 282 assertions / 31 cases, all passing (unchanged).
- Full app + libslic3r_gui + orca-slicer launcher build clean (MSVC release, `xmake -j2`).
- **Not done (manual, deferred):** launching the app with `pnp_cli` renamed out of the dist and
  eyeballing the degraded page — needs an interactive desktop session. Same residual ticket 11
  recorded; the two together are the manual smoke. Everything derivable without a desktop is
  verified.

### What this ticket's answers graduate / sharpen in the map

- The fog entry "purge affordance for preserved keys" is **half-discharged**: the degraded page
  purges; the reachable-normal-state surface (successful probe + surviving orphans, ticket 11's
  seam note) is a question that now has a concrete owner but no ticket yet — it needs deciding
  whether orphans get a visible surface on the *live* page too or remain jsonl-only.
- The `advanced`-mode decision and ticket 11's verbatim-label rule carried over unchanged: the
  degraded page's strings are fork-authored and *do* go through `_L()`/translatable strings
  (unlike the live page's schema-supplied ones) — banner copy is translatable, per the map's
  localization glossary rules.