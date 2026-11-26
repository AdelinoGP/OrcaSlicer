---
title: Profile sync, keys, and the ignore set
status: open
type: implementation
assignee: ""
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
