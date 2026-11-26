---
title: Translation catalogs
status: open
type: implementation
assignee: ""
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
