---
title: PnpConfigTranslator — preset → flat PNP JSON + warning vector
status: done
batch: B1
blocked-by: []
files: [src/slic3r/GUI/PnpConfigTranslator.hpp, src/slic3r/GUI/PnpConfigTranslator.cpp, src/slic3r/GUI/CMakeLists.txt]
---

## Goal

New GUI translation unit converting `preset_bundle->full_config(...)` (a `DynamicPrintConfig`,
925 keys) into the flat-key PNP JSON `pnp_cli slice --config` consumes, plus a classified warning
vector. **Pure function** — returns `{json, warnings}`; it never writes files or shows UI
(the sink is F03, per [ticket 013](../../tickets/013-config-warning-ux.md)).

## Decisions (do not re-open)

- Mapping = **one static table**, four tiers, from
  [ticket 005](../../tickets/005-preset-to-pnp-config-translation.md) and its
  [full mapping asset](../005-preset-to-pnp-config-mapping.md): 65 identical keys, ~30
  rename/transform rows (incl. `raft_layers`→`support_raft_layers`, `seam_position` enum map),
  ~830 unsent.
- Warning classes: `unsupported-feature`, `not-yet-mapped` (the default for unlisted keys),
  `no-op`, `lossy-fallback` (sent, value substituted — e.g. `seam_position: aligned`→`nearest`).
  No up-front classification pass.
- PNP key strings are snake_case (pnp repo convention).

## Steps

1. Implement the static table as data (rows: orca key, tier, pnp key, transform fn, warning class).
2. `PnpTranslationResult translate(const DynamicPrintConfig& full_config)` producing the JSON
   (boost::property_tree or nlohmann, whichever the tree already links — Orca vendors nlohmann)
   and `std::vector<PnpConfigWarning>` (key, class, orca value, substituted value if lossy).
3. Emit every Tier-B/C/D record unconditionally — **filtering against defaults is F03's job**, not
   the translator's; keep it pure and total so the table can be grown from logs.
4. Unit-style verification: a small Catch2 test in `tests/` exercising a default
   `FullPrintConfig` through the table (identical keys present, `seam_position` lossy-fallback
   recorded) if a GUI-free seam is feasible; otherwise a `--pnp-dump-config` style manual check
   documented in the commit message.

## Done / verify

- `cmake --build build --config RelWithDebInfo --target libslic3r_gui` clean.
- Translating a default config yields valid JSON containing the Tier-A keys and a
  `lossy-fallback` warning for `seam_position`.
