---
title: Translation catalogs
status: closed
type: implementation
assignee: opencode-agent
blocked-by: []
---

# Translation catalogs

## Question

Replace the translation catalogs with upstream head in one commit.

Safe by measurement: the fork has made **zero** `.po` edits since `pnp-fork-base`
(`git diff 33909a51cd HEAD -- 'localization/**'` is empty), so there is no merge to do and no
fork-local translation to lose. The churn is 157 upstream commits touching `localization/**`
(125 non-merge, 103 touching `.po` only), +683,058/−296,957 lines; review value per line is nil,
which is why it is one commit.

The catalogs will contain upstream strings for UI this fork has deleted. That is acceptable:
unused msgids cost nothing at runtime, and pruning them would have to be redone after every sync.

## Method

1. Sync `localization/i18n/*/OrcaSlicer_*.po` from upstream head. Upstream ships **23** catalogs
   and the fork has 22; the extra is `eu` (Basque). Wholesale take therefore yields 23 — decide
   whether Basque ships (a catalog with translations is a free language; an empty one is dead
   weight, and the Preferences language list must match what actually shipped).
2. Do **not** take `2bd105b0ce` or `210bc927bc` individually: Thai was added and then reverted
   in-range, so the head snapshot already reflects the revert. Nothing to do, but do not
   cherry-pick either commit.
3. Regenerate `OrcaSlicer.pot` only if upstream moved it; do not hand-edit.

## Glossary check

`AGENTS.md` makes the
[localization glossary](https://github.com/OrcaSlicer/OrcaSlicer_WIKI/blob/main/guides/localization_glossary.md)
normative for recurring terms, and forbids translating product names, acronyms, file formats,
G-code, macros and variables. A wholesale upstream sync should already comply — upstream owns
the glossary — but the check is cheap and the ticket must state it was done:

- spot-check the recurring terms in the two largest catalogs (de, fr) against the glossary;
- confirm no placeholder, `msgctxt`, or encoding drift by loading the catalogs in the app
  (missing placeholders surface as `%s`-in-text or a wx assertion).

## Verification

1. `xmake -j2` green (the build compiles the catalogs; a malformed `.po` fails here).
2. Launch with `--lang de`, `--lang fr`, `--lang ja` → UI strings render, no `%!`/`%s` artefacts,
   no empty labels where English previously appeared.
3. Screenshot one dialog per language, plus the Preferences language dropdown listing every
   catalog that shipped.

## Risks

- A missing catalog is silent: the app falls back to English with no warning, so the walkthrough
  must confirm the language actually changed, not merely that the app started.
- Placeholder drift is the classic `.po` failure and is invisible until a dialog renders
  `%1%` literally. Check one formatted string per language.

## Done when

One commit, build green, three-language walkthrough with screenshots, glossary spot-check noted
in the PR body, ledger row updated.

## Answer

Two commits, because the Basque catalog needs a GUI half that is a code change:

| Commit | Upstream | What landed |
|---|---|---|
| `7d23195426` | the 157-commit `localization/**` series | 23 catalogs + `OrcaSlicer.pot`, every `.po` and the `.pot` byte-identical to `808dd7dff8`; +569,954/−135,000 |
| `575818d4e1` | `f894d08881` | `wxLANGUAGE_BASQUE` in the Preferences combobox filter, labelled "Euskara"; clean pick, upstream message plus a fork note |

### Basque ships

The catalog is not an empty shell: **9,918** entries, **447** untranslated (95.5% coverage),
**4,370** marked "AI Translated". It is listed by the Preferences combobox only if
`supported_languages[]` names it — that array, not the catalog set, decides what the UI offers — so
`f894d08881` (in range, 2026-10-30) travels with it. The pick's source hunks apply cleanly
(`git apply --3way --check -v` reports no conflict and no markers); its context differs from this
fork only where upstream had already renamed `wxLANGUAGE_CHINESE_TRADITIONAL` and appended
`wxLANGUAGE_ROMANIAN`, so the fork keeps `wxLANGUAGE_CHINESE` (same value) and adds no Romanian
entry for a catalog that does not ship.

### `list.txt` stays the fork's

Upstream's copy drops 9 files that exist here (`GLGizmoText`, `PublishDialog`, `ObjColorDialog`, …)
and adds 117 that do not. It is only the `--full` extraction input, so it is kept fork-correct
rather than byte-identical; the ticket scopes the sync to `.po` + `.pot`.

### The ticket's verification plan was wrong about the build

`xmake` does not compile catalogs. `scripts/run_gettext.bat` (Windows) / `run_gettext.sh`
(flatpak) runs `msgfmt --check-format` into `resources/i18n/<lang>/OrcaSlicer.mo`, `*.mo` is
gitignored, and the xmake resource step is one-shot (it returns early when the binary directory
already has `resources/`). Consequences, both acted on:

- **`msgfmt` is the catalog check.** Running the script compiled all 23 with `--check-format`,
  exit 0, so no placeholder/encoding/format error exists in the snapshot.
- **A build tree or package needs gettext run first.** The new `.mo` files were copied into the
  build tree's `resources/i18n` by hand for the walkthrough; `xmake package` on Windows still
  needs `run_gettext.bat` before it, or the package ships the previous catalogs. Only the flatpak
  manifest calls the script today.

Also, there is **no `--lang` option** in this fork or upstream: the language comes from
`OrcaSlicer.conf`'s `language` key or the Preferences dialog. The walkthrough set the key and
relaunched (and drove the dialog for the dropdown), rather than passing a flag.

### Placeholders

`msgfmt --check-format` covers C-format strings; a separate msgid/msgstr comparison over all
**215,821** translated entries (22 full catalogs plus the `en` stub) found **2** real drifts, both
`ru`, both present at `pnp-fork-base` and therefore neither introduced nor fixed here:

| msgid | ru msgstr | Renders |
|---|---|---|
| `Filament does not match the filament in AMS slot. …` | `Материал %s не соответствует материалу в слоте %s AMS. …` | two literal `%s` (the msgid has no placeholder) |
| `Select connected printers (0/6)` | `Выберите подключённые принтеры (%d/6)` | a literal `%d` |

They are recorded, not hand-edited: a hand-edit would reintroduce fork-local `.po` edits, which is
exactly what makes the next sync conflict-free. The remaining 14 apparent mismatches are literal
percent signs in prose (`%100`, `50%`) or the Catalan accelerator `Re%carregar`.

### Glossary spot-check (de, fr)

The glossary now lives at `developer_reference/localization_glossary.md` in the wiki (the path in
`AGENTS.md` moved); terms come from `developer_reference/glossary/{terms,de,fr}.tsv`. Of the 185
glossary terms that occur as msgids in both catalogs, **165/185 de** and **170/185 fr** match the
prescribed translation exactly. Every difference is one of:

- a context-specific entry the glossary explicitly exempts — `Top`/`Bottom` under `Alignment`,
  `Camera View`, `Layers`, `Mirror` under `IDEX/IQEX role`, `Pattern`/`Shell` under `Design`;
- a glossary cell that is empty (`Auto Refill`, `Emboss`, `Inscribed`, `Circumscribed`, …);
- **one wording regression upstream introduced**: de `Extrude` (no context) now reads
  "Extrusion" where the glossary prescribes "Extrudieren" (it read "Extrudieren" at the base).

None of it is fork-authored; the next sync replaces the catalog wholesale.

### Verification

- **Build.** `xmake build OrcaSlicer_app_gui` → `build ok`; `OrcaSlicer.dll` and
  `orca-slicer.exe` relinked, and `strings OrcaSlicer.dll | grep Euskara` = 1, so the Preferences
  change is in the binary. (A bare `xmake -j2` reported "build ok, spent 0.0xs" against the same
  build tree — the default target had nothing left to do.)
- **Tests.** `xmake test`: **7/7 passed, 0 failed**, 48.2 s. Assertion counts from the suite output:
  `libslic3r_tests` 81,329 in 185 cases, `fff_print_tests` 202/18,
  `libnest2d_tests` 638/21, `filament_group_tests` 353/3, `slic3rutils_tests` 47/8,
  `pnp_config_translator_tests` 1,112/31, `pnp_runtime_tests` 376/33.
- **GUI**, `orca-slicer.exe` from the build tree, driven with cua-driver; language set through
  `OrcaSlicer.conf` and the Preferences dialog:
  - **de** — main window ([01](../assets/07/01-main-de.png)), Preferences with *Sprache: Deutsch*
    ([02](../assets/07/02-preferences-de.png)), the language dropdown with **Euskara** at the top
    ([03](../assets/07/03-languages-de.png), [04](../assets/07/04-languages-de-tail.png)), and the
    restart prompt raised by changing the language
    ([05](../assets/07/05-language-selection-prompt.png)).
  - **fr** — main window, decimal comma throughout ([06](../assets/07/06-main-fr.png));
    Preferences with *Langue: Français* ([07](../assets/07/07-preferences-fr.png)).
  - **ja** — main window ([08](../assets/07/08-main-ja.png)); Preferences with *言語: 日本語*
    ([09](../assets/07/09-preferences-ja.png)).
  - **eu** — main window in Basque, tabs *Prestatu / Aurrebista / Gailua / Proiektua*
    ([10](../assets/07/10-main-eu.png)); Preferences with *Hizkuntza: Euskara*
    ([11](../assets/07/11-preferences-eu.png)). This run also proves the Preferences filter
    accepts the catalog end to end, not just that the file exists.
  - Formatting: every catalog translates `About %s` with its placeholder intact (`Über %s`,
    `À propos de %s`, `%s について`, `%s-i buruz`), and none of the captured panels shows a literal
    `%s`/`%1%`. The About dialog itself was **not** opened in these runs, so the formatted-string
    check is catalog-level plus the panel text above, not a clicked dialog.

### Gaps

- **No single screenshot of all 24 language entries.** The dropdown popup is 465 px tall and did
  not scroll under the driver's wheel, so (03) shows the head (Euskara → Deutsch) and (04) the
  middle; the count comes from the 23 `.mo` files plus English, and from the filter code.
- **The homepage card stays English** in `ja` ("New Project", "Recently opened") and in `eu`,
  while the same page is German in `de`. That is the WebView homepage asset (`?lang=ja_JP`,
  `?lang=en_US`), not the `.po` catalogs; it is the same at the fork base and out of this ticket's
  scope.
- The `xmake test` runner needed the GUI closed: while `orca-slicer.exe` was open it held
  `tbb12.dll` and the runner's dependency copy failed with "Not access because it is busy". The
  suites were run directly from the build tree first (all green, exit 0), then `xmake test` was
  re-run with the app closed.

### Ledger

`docs/UpstreamSyncMap/ledger.md` gained the `f894d08881` row, the two-commit record with the
coverage numbers, the gettext/`msgfmt` fact, the 23/9,906 counts, and the glossary/placeholder
defects upstream carries.

