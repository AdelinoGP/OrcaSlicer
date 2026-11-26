---
title: Profile sync, keys, and the ignore set
status: closed
type: implementation
assignee: opencode-agent
blocked-by: []
---

# Profile sync, keys, and the ignore set

## Question

Bring the vendor, machine, filament and process profile refresh from upstream head, together with
the config keys those presets reference **and** the ignore-set change that lets some of them take
effect, so the printer list matches upstream and no shipped preset silently loses meaning at load.

Three facts shape the ticket:

- Profiles **load** fine here. Unknown keys are cleared by `PrintConfigDef::handle_legacy`
  (`src/libslic3r/PrintConfig.cpp:8435`) because the vendor-load path passes `unknown_out ==
  nullptr`, so there is no load error to catch.
- Profiles therefore **misbehave silently**. 19 upstream keys referenced by shipped presets are
  absent, across 673 files.
- Adding a key is **not enough** when the key is also in the obsolete-key ignore set. The ignore
  test runs before `has()`, so a key added verbatim is still cleared. See Part A step 2.

## Part A — the 19 keys

Measured 2026-11-25 by parsing `this->add()` in both `PrintConfig.cpp` files and every JSON under
`resources/profiles/*/(machine|filament|process)`: upstream 1,016 keys, fork 848, 170 missing,
**19 used**, 673 files affected.

| Key | Files | Loaded here, then |
|---|---|---|
| `filament_prime_volume` | 447 | **Stays cleared unless removed from the ignore set** (Part A step 2). Even then, `pnp_cli` does not read it — see the consumption note |
| `top_color_penetration_layers` | 190 | Present, not consumed by `pnp_cli` |
| `bottom_color_penetration_layers` | 156 | Present, not consumed |
| `extruder_clearance_dist_to_rod` | 18 | Present, not consumed |
| `imex_mode_gcodes` | 9 | Present; ticket 09 consumes the rest of the family |
| `imex_mode_active_tools` | 9 | Present |
| `is_imex` | 9 | Present; this is what makes ticket 09's machines recognisable |
| `imex_mode_names` | 9 | Present |
| `imex_tool_layout` | 9 | Present |
| `imex_gantry_count` | 6 | Present |
| `imex_nozzle_clearance_x` | 6 | Present |
| `exclude_object_tolerance` | 4 | Present, not consumed |
| `imex_tools_per_gantry` | 3 | Present |
| `belt_slice_rotation_angle` | 3 | Present; ticket 10 consumes the family |
| `build_plate_tilt_x` | 3 | Present |
| `belt_slice_rotation` | 3 | Present |
| `belt_printer` | 3 | Present |
| `belt_printer_infinite_y` | 1 | Present |
| `wait_for_temp_on_wipe_tower` | 1 | Present, not consumed |

**Consumption note.** The fork slices through `pnp_cli`, and none of these keys appear in
`pinch_n_print_cli` sources today. Adding them makes values *present and preserved*, not
*sliced*. That is the honest description of this ticket's benefit: the data stops being lost, and
the features that consume it arrive in tickets 09, 10 and with pnp-side work.

**Visible side effect.** Per BootstrapMap 013 (reopened 2026-08-10), unmapped options gain an amber
label tint, and non-default Tier-D values can raise a `WarningNotificationLevel` notification.
Stock filament presets will therefore start warning on every slice until the keys are mapped. That
is a user-visible default change, not a silent one — record it in the PR body.

### Step 2 — the ignore set

`filament_prime_volume` sits in the fork's ignore list at `src/libslic3r/PrintConfig.cpp:8405`.
Upstream no longer ignores it (upstream's set keeps `overhang_speed_classic`, `anisotropic_surfaces`
and a list of retired belt keys). So:

1. Remove `filament_prime_volume` from the fork's ignore set.
2. Diff the fork's whole ignore set against upstream's and reconcile every entry. Measured: the
   fork's set equals upstream's **plus `filament_prime_volume`, minus twelve retired belt keys**
   (`belt_preslice_global`, `belt_slice_rotation_global`, `belt_support_floor_mode`,
   `belt_support_z_offset_mode`, `first_layer_plane`, `first_layer_plane_offset`,
   `first_layer_plane_thickness`, `gcode_back_transform`, `preslice_remap_global`,
   `preslice_remap_x`, `preslice_remap_y`, `preslice_remap_z`). The missing entry is **inherited
   from the fork base**, not fork-added.
   - Decide explicitly whether to add the twelve belt keys back. Doing so is harmless:
     pnp-registered keys are exempt (`PrintConfig.cpp:8410-8428`). Not doing so is also fine, as
     long as ticket 10 does not assume they are present.
   - Reconcile the third list too: `scripts/orca_extra_profile_check.py:8` keeps its own
     obsolete-key list.

### Step 3 — definitions

Add each key with upstream's definition, but check for `ConfigOptionEnum` values and struct
members in `PrintConfig.hpp`: "verbatim" is only safe for scalar options. Keep the key additions in
a commit separate from the profile data so any single key can be bisected.

## Part B — the profile data

Aggregate against the fork base: 13,000 files changed (rename-detected), 13,658 paths with renames
off (4,285 A / 7,697 M / 1,676 D), 145 vendor directories, **72** vendor index `version` bumps.

**There are no fork-local profile edits**: `git diff 33909a51cd HEAD -- resources/profiles` is
empty, and no fork commit has ever touched that tree. Profiles are a pure upstream snapshot, like
the translations. That means:

- take the vendor directories wholesale;
- take `resources/profiles/*.json` indexes as one atomic move (7 new, 66 modified, 72 version
  fields) — shipping new JSON under old version numbers would make the update path lie;
- do not go hunting for fork-specific files: `BBL/cli_config.json` is upstream's, and
  `blacklist.json`'s loss of five empty arrays upstream is harmless but should be diffed.

Order of operations:

1. **Keys and the ignore set first** (Part A), so the incoming data never lands in a tree that
   cannot describe it.
2. **Renames**: `43a83397d4`, `7d62ded630`, `8747605930` restore presets under new names. 1,626
   "deleted" filament files are renames; without `renamed_from` coverage in
   `update_map_system_profile_renamed` (`Preset.cpp` ~3063-3111) users' own filament presets orphan.
   Verify the existing `[Preset][Rename]` tests cover the new names and extend them if not.
3. **Vendor data** wholesale, indexes with it.
4. **Updater check (unverified risk).** Vendor versions jump past the app version — BBL goes
   `02.01.00.22` → `02.08.00.16` while the app stays `02.06.00.51`.
   `PresetUpdater::sync_printer_config` derives `using_version` from `SLIC3R_VERSION` and deletes
   caches on a major/minor mismatch, and `min_slic3r_version` gating was never checked. Test: fresh
   user data dir, launch, confirm no spurious profile-cache wipe or "profiles out of date" state,
   and record the observed behaviour.

`setting_id` needs no action: `generate_preset_setting_id` (`Preset.cpp:527`) is byte-identical to
upstream's, so ids re-derive correctly. Do **not** sync `scripts/`: upstream `3500a1e588` deleted
`assign_vendor_setting_ids.py` and `orca_extra_profile_check.py`, and the fork's copies still agree
with the same C++ rule.

## Verification

1. `xmake -j2`, `xmake test` green.
2. **New test (in scope):** a suite that walks `resources/profiles/` and asserts every top-level
   key in every preset is either declared in `print_config_def` or explicitly allowlisted. The
   allowlist must cover, at minimum:
   - preset metadata (`type`, `name`, `inherits`, `from`, `instantiation`, `setting_id`,
     `renamed_from`, `version`, `compatible_printers*`, `printer_model`, …);
   - keys in the obsolete-key ignore set (they are legal to ship, and cleared on purpose);
   - keys that only register at runtime from the pnp schema, which are absent from a static
     `print_config_def` scan. Use the existing API as the source:
     `pnp_registered_config_keys()` / `pnp_config_keys_sealed()`
     (`src/libslic3r/PrintConfig.hpp:770-777`, implemented in
     `src/libslic3r/PnpConfigKeyRegistry.cpp`), exercised by
     `tests/pnp/test_pnp_config_keys.cpp`. If the test cannot reach the registry, a checked-in
     schema snapshot is the fallback.

   Without those three exemptions the test fails on day one and gets deleted. Model it on
   `tests/libslic3r/test_preset_bundle_loading.cpp`, which builds synthetic bundles but never reads
   the shipped JSON.
3. `python scripts/orca_extra_profile_check.py --check-filaments --check-materials
   --check-obsolete-keys` clean.
4. Walkthrough, screenshots:
   - Vendor list shows the new machines (X2D/H2C/A2L, Snapmaker U1, Qidi X-Plus 5, Elegoo
     Centauri 2, Anycubic Kobra S1 Max).
   - **Presence is proved through saved data, not through the settings tabs.** None of these keys
     has a control in `Tab.cpp` today (`grep -c` returns 0 for `extruder_clearance_dist_to_rod`,
     `is_imex`, `belt_printer`, `filament_prime_volume`, `top_color_penetration_layers`), so this
     ticket adds definitions and data only. Prove the value survives by: load an affected preset,
     save a 3MF, and inspect `Metadata/project_settings.config` for
     `extruder_clearance_dist_to_rod` (BBL), `is_imex` (Vivedino), `belt_printer` (IdeaFormer) and
     `filament_prime_volume` (any filament). The `filament_prime_volume` case is the one that fails
     if the ignore set was not fixed.
   - A user filament preset created before the sync survives the rename: import a pre-sync bundle,
     relaunch, confirm the preset is still there.
   - Tab controls and the amber tint on unmapped keys are **not** verified here: those fields do
     not exist yet, and the tint claim comes from BootstrapMap 013's reopening rather than a
     measurement in this ticket. Tickets 09 and 10 own the visible controls and verify the tint.

## Risks

- Largest data change in the pass, least reviewable line by line. The new key test is the review
  substitute; without it the ticket is unverifiable.
- 1,626 deleted paths are renames; a missed `renamed_from` orphans user presets silently.
- `filament_prime_volume` affects ordinary single-material printing, not a niche feature.
- The updater interaction is unverified and could wipe caches on first launch.

## Done when

Keys committed with the ignore set reconciled, profile data synced with indexes consistent, the
new profile-key test passing in CI with an explicit allowlist, `orca_extra_profile_check.py`
clean, updater behaviour recorded, walkthrough screenshots attached.

## Answer

Ticket 03 is on `pnp/main` as five commits. The first four follow the ticket's order — keys and the
ignore set first, profile data second, the tests and script cleanup last; the fifth is the
option-list fix the GUI walkthrough exposed.

| Commit | What landed |
|---|---|
| `dc90dcf2d6` | The 21 config keys, their two enums and static enum maps; `filament_prime_volume` out of the obsolete-key set and the twelve retired belt keys in; `scripts/orca_extra_profile_check.py`'s list mirrors the set |
| `0cd2ff3bb9` | `resources/profiles/` replaced with upstream `808dd7dff8`'s tree, byte-identical (`git diff 808dd7dff83f HEAD -- resources/profiles` is empty) |
| `ba1f6242fa` | the shipped-profile key test and the `renamed_from` data invariant |
| `f2f3c5bc43` | the profile check script reads UTF-8 BOM profiles and skips non-preset support tables |
| `3212071996` | the 21 keys added to `Preset::printer_options()` / `print_options()` / `filament_options()`; the key test asserts that every declared preset key is in its collection's list |

### The third gate: the preset option lists

`print_config_def` is not the only place a key has to exist. `PresetBundle::load_vendor_configs_from_json`
calls `Preset::remove_invalid_keys(config, default_config)`, and `default_config` is the collection's
default preset, built from `Preset::printer_options()` / `print_options()` / `filament_options()`.
The first GUI load of a Vivedino preset logged

```
contains incorrect keys: imex_mode_active_tools, imex_mode_gcodes, imex_mode_names,
imex_nozzle_clearance_x, imex_tool_layout, is_imex, which were removed
```

even though all six are declared — the keys were silently erased at load. `3212071996` adds the 21
keys to the three lists. The test now walks the shipped presets and asserts every key that is both
in a preset and in `print_config_def` is also in that preset type's option list, so a future key
cannot land half-wired. Preset-name metadata (`filament_settings_id`, `print_settings_id`,
`printer_settings_id`) is exempt; the loader consumes it before the config.


### 21 keys, not 19

The ticket's 19 is a lower bound from a scan of literal `this->add("name", …)` calls. Two more keys
are generated from `filament_extruder_override_keys` and were missed by that scan:

- `filament_retract_length_toolchange` (97 preset files)
- `filament_retract_restart_extra_toolchange` (43 files)

Both are upstream definitions produced by the same generator loop as the other `filament_*`
overrides, so adding them is the same change. All 21 are defined with upstream's fields, the two
`coEnum` keys bring their enum (`ImexToolLayout`, `BeltRotationAxis`) and static map, and each key is
added to the static config class upstream puts it in.

### Divergences

- **The twelve retired belt keys were added back to the ignore set.** The fork's set had lost them
  relative to upstream (`belt_preslice_global`, `belt_slice_rotation_global`,
  `belt_support_floor_mode`, `belt_support_z_offset_mode`, `first_layer_plane`,
  `first_layer_plane_offset`, `first_layer_plane_thickness`, `gcode_back_transform`,
  `preslice_remap_global`, `preslice_remap_x`, `preslice_remap_y`, `preslice_remap_z`). The decision
  is explicit: the set is now upstream's 56 entries exactly, so ticket 10 inherits upstream's rule
  instead of a fork-local variant. The twelve are inert today — none is in `print_config_def` or
  pnp's registry — so behaviour is unchanged.
- **`gcode_remap_x/y/z` stay ticket 10's.** Upstream defines them through an `add_belt_remap`
  helper (which the literal-`this->add` measurement also missed) and the three belt presets
  reference them. The new test lists them as *deferred upstream* keys, separately from the
  vendor-only keys, rather than claiming nobody declares them.
- **`8747605930`'s source half is not taken.** The commit also made `check_preset_references()` run
  unconditionally in `has_errors()` — a CI-only validation change. Its profile-data half (fixing
  invalid `inherits` / `compatible_*` references) is in the snapshot.
- **`scripts/` is not synced.** Upstream deleted `orca_extra_profile_check.py` at `3500a1e588`; the
  fork keeps its copy. The copied `OBSOLETE_KEYS` list was stale in both directions, and the synced
  tree exposed two script bugs: BenFab profiles are UTF-8 with a BOM (the app tolerates it, the
  script did not) and BBL ships non-preset support tables under `filament/` (`filament_id_map.json`,
  `filament_name_map.json`, `support_recommended_params.json`) that have a `type` marker but no
  `name`. Both are fixed; the check is clean.
- **The test allowlists 234 vendor-only keys by name.** They are present in vendor profiles but
  declared by neither this fork nor upstream `808dd7dff8` (BambuStudio/Creality/Qidi import
  leftovers: `include`, `epoxy_resin_plate_temp`, `wall_infill_order`, …). The loader dropped them
  before the sync too. Listing them keeps the test meaningful: a *new* unknown key fails instead of
  hiding behind a wildcard.

### Renames

Of the 836 preset names present at the fork base and absent at upstream head, 761 are recorded in a
new preset's `renamed_from` and resolve through `update_map_system_profile_renamed()`. The other 75
are genuine removals — whole vendors or models upstream deleted (SeeMeCNC 21, Creality 19, Anker 13,
OrcaArena 9, …) — and upstream references none of them. The shipped `renamed_from` data has 1,858
entries across the three collections, no duplicate old name, no self-rename and no empty entry;
`[Preset][Rename]` now asserts that invariant, and the walkthrough proves a pre-sync user filament
resolves through it (see below).

### Updater behaviour (recorded, not changed)

Shipped vendor `version` values are metadata only: `PresetBundle::load_vendor_configs_from_json`
stores them on the presets and never compares them with `SLIC3R_VERSION`. No shipped index carries
`min_slic3r_version`. `PresetUpdater::sync_printer_config` compares `SLIC3R_VERSION` with the *OTA
printer-config cache* (`<data_dir>/printers/version.txt`, `<data_dir>/ota/printers/version.txt`),
not with the vendor profile version, and deletes only that cache. A fresh data dir has no cache, so
there is nothing to wipe; the network sync is attempted and, when it fails, is caught and logged.
Observed on the fresh data dir: the BBL OTA printer config was copied to `printers/` (its own
`version.txt` became `02.00.00.31`) and `ota/` stayed empty; no cached version existed to compare,
so nothing was deleted and no "profiles out of date" state appeared. The only updater prompt was the
unrelated application-update dialog for 2.5.2.

### Verification

Build (Windows x64, release, `GIT_COMMIT_HASH=f2f3c5bc43`): `xmake -j2 OrcaSlicer` succeeded; after
`3212071996` the DLL relinked against the changed `Preset.cpp`.

`xmake test`:

```
100% tests passed, 0 test(s) failed out of 7
```

The two ticket-specific suites:

| Filter | Result |
|---|---|
| `[Profile][Config]` | 14,818 assertions, 1 case, passed |
| `[Preset][Rename]` | 16,724 assertions, 8 cases, passed |

`python scripts/orca_extra_profile_check.py --check-filaments --check-materials
--check-obsolete-keys`: 71 vendors, 0 errors, 0 warnings.

**Updater.** Fresh data dir (`--datadir`, no `printers/` or `ota/printers/`): first launch installed
the two resource bundles (`Custom`, `OrcaFilamentLibrary`), copied the BBL OTA printer config to
`printers/` (`version.txt` = `02.00.00.31`; `ota/` stayed empty), and loaded every vendor including
BBL at `02.08.00.16`. There was no cached printer config to compare, so
`sync_printer_config` logged none of its mismatch lines and removed nothing; no "profiles out of
date" notification appeared. The only updater prompt was the unrelated application-update dialog
for 2.5.2, which was skipped.

**Walkthrough** (screenshots in [assets/03](../assets/03/)):

- The Setup Wizard's printer list shows the new machines: [Bambu Lab list with A2L/H2C/X2D](../assets/03/01-bambu-vendor-list.png),
  [Anycubic](../assets/03/03-anycubic-vendor-list.png), [Elegoo Centauri 2](../assets/03/04-elegoo-vendor-list.png),
  [Qidi X-Plus 5](../assets/03/05-qidi-vendor-list.png), [Snapmaker U1](../assets/03/06-snapmaker-vendor-list.png),
  [IdeaFormer IR3 V2](../assets/03/07-ideaformer-vendor-list.png).
- A new machine loads and its defaults resolve: [Bambu Lab H2C in Prepare](../assets/03/02-h2c-loaded.png)
  (printer panel, `0.20mm Standard @BBL H2C`, `PLA Basic`).
- 3MF data, via the app's own 3MF exporter (`--export-3mf`, the same `export_project` path the GUI
  Save Project uses) with the preset JSONs: `extruder_clearance_dist_to_rod = "33"` and
  `filament_prime_volume = ["30"]` (BBL X1C), `is_imex = "1"` and
  `imex_tool_layout = "front-left"` (Vivedino), `belt_printer = "1"`, `belt_slice_rotation = "x"`
  and `build_plate_tilt_x = "45"` (IdeaFormer). The inherited values are materialised into the
  preset copies because the CLI does not resolve `inherits`; the GUI loader resolves the chain (the
  log below confirms the Vivedino preset loads with its keys after `3212071996`).
- Pre-sync user preset: `Ticket03 Pre-Sync PLA` (a 2.5-era file inheriting `Anker Generic PLA`,
  which this sync renamed to `Generic PLA @Anker`) loads from the user preset dir and the X1C
  selection succeeds; the log shows `load preset: Ticket03 Pre-Sync PLA` and no preset errors.


### Gaps

- **The 3MF evidence was produced by the CLI exporter, not the GUI Save Project dialog.** The GUI
  was paused to free the desktop; `--export-3mf` is the same `export_project` writer, and the GUI
  preset load was checked from the log, but a click-through Save Project remains unrecorded. The
  inherited values were materialised into the preset copies for the CLI because it does not resolve
  `inherits`; the GUI loader does.
- **Vivedino is not in the Setup Wizard's printer list.** The wizard's cards need cover art and the
  Vivedino presets have none, so the vendor never appears there. Its `Xplorer IDEX` models were
  added to the app config's `models` list directly; the preset then loaded (log:
  `select Xplorer IDEX 0.4 nozzle, success`) with no `incorrect keys` after `3212071996`.
- `pnp_cli` reads none of the 21 keys yet. The values are present and preserved, not sliced;
  tickets 09 (IMEX) and 10 (belt) consume the families, and the rest are inert until pnp-side work.
- The amber tint / `WarningNotificationLevel` side effect is BootstrapMap 013's, not measured here:
  none of these keys has a control in `Tab.cpp` yet, so nothing renders tinted in this ticket.
- The 234 vendor-only keys and `gcode_remap_x/y/z` still drop on load, exactly as before the sync.
  The first group is upstream's dead data; the second is ticket 10's.
- The 75 removed preset names have no `renamed_from`. A user preset inheriting one of those would
  still orphan; that is upstream's removal, not a rename the sync dropped.
- The pre-sync user preset's `inherits` is normalized in memory; the file on disk keeps
  `Anker Generic PLA` until the preset is saved. Presence and loading are verified, not the rewrite.
- A future key needs **three** touches, not one: `print_config_def`, the collection's
  `Preset::*_options()` list, and (if it was ever in the ignore set) removal from that set. The
  shipped-profile test now fails on the first two; the ignore-set accessor covers the third.

