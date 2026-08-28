---
title: Preserve unknown pnp keys through preset and 3mf load
status: closed
type: grilling
assignee: Adelino Penedo
blocked-by: [02]
---

## Question

A preset or project 3mf carries pnp module keys for a module the current install does not have.
The map's locked decision is **preserve untouched, round-trip on save, and warn once on load**.
How?

The obstacle is known and load-bearing: `load_from_json` fails the **whole** project config on
an unknown key — unlike the ini/gcode paths, which ignore per-key. BootstrapMap ticket 008 hit
exactly this with SLA and worked around it with explicit up-front detection.

Settle:

- **Where the unknown keys are held** between load and save, given ticket 02's registry answer.
  A side map on the config? A tolerant-load mode that collects rather than throws?
- **Which load paths** need the behaviour: project 3mf, `.json` preset, `.ini` preset, G-code
  config block. They do not currently agree with each other on unknown-key handling.
- **What "unresolvable" means precisely** — a key absent from the live schema is not the same as
  a key whose *module* is absent; decide whether the warning can name modules at all (the
  3mf/preset stores keys, not module ids) or must name keys.
- **The notification.** One per load, using the fork's existing warning-notification surface
  (`PnpSlicingProcess`'s `WarningNotificationLevel` path, `format_pnp_config_warning_message()`).
  Decide whether it reuses that formatter or needs its own.
- **Save-side.** Confirm the preserved keys are written back byte-equivalent, and that they do
  not leak into `ConfigBase::diff()` results and mark a pristine preset as modified.

Test: a 3mf fixture carrying a key no schema declares opens, warns, and re-saves with the key
intact.

## Corrected premise (2026-08-28)

**The obstacle this ticket states is wrong, and so is the one the map states.**
`load_from_json` does *not* fail the whole project config on an unknown key.
`PrintConfigDef::handle_legacy` (`src/libslic3r/PrintConfig.cpp:8413`) ends with

```cpp
if (! print_config_def.has(opt_key)) { opt_key = ""; return; }
```

so any key the def does not have is *cleared*. `ConfigBase::set_deserialize_nothrow`
(`Config.cpp:580`) then records the original name in
`ConfigSubstitutionContext::unrecogized_keys` and returns success.
`UnknownOptionException` is thrown from `set_deserialize_raw:625` only for a key that
`handle_legacy` did **not** clear, which is unreachable for any config backed by
`PrintConfigDef`.

The actual behaviour is therefore **tolerate-and-drop, silently**: the key loads without
error and without a value, and the next save writes it out no more. The data loss this
ticket exists to fix is real; only the mechanism was misdescribed. The test found it,
failing with `unknown.size() == 0` because no exception was ever thrown.

Consequence for the design: the hook is the **cleared-key signal**, not an exception. Once
ticket 02 registers the keys of the live schema into `print_config_def`, a cleared key means
exactly that no module and no Orca definition claims it. The `UnknownOptionException`
catches are kept as the other route into the throw, for a `ConfigDef` whose `handle_legacy`
does not clear.

The BootstrapMap ticket 008 comment at `bbs_3mf.cpp:2746` asserts the same wrong premise for
its SLA pre-pass. Not touched here — the pre-pass is still correct and still wanted, since an
SLA project must be refused rather than silently stripped — but its stated reason is
unverified.

## Resolution (2026-08-28)

**Held on the document, never in the config.** `Model::pnp_unknown_config` for a project
3mf, `Preset::pnp_unknown_config` for a `.json` preset; both are
`ConfigBase::t_unknown_config_values` = `std::map<std::string, std::string>` mapping the key
to the **serialized JSON fragment** of its value (the nlohmann `dump()`), not to a parsed
`json`. That keeps `Config.hpp`, `Model.hpp` and `Preset.hpp` free of the nlohmann include,
which none of them had; `dump()` then `parse()` then `dump()` is stable, so the value
round-trips byte-equivalent. Rejected: a member on `DynamicPrintConfig` (every
apply/merge/copy site would have to maintain it, and the merge semantics of `full_config()`
are undefined), and in-band opaque `coString` options (violates the one-shot seal from
ticket 02, leaks into `ConfigBase::diff:518`, and flattens arrays to strings).

Because they never enter `DynamicConfig::options`, `ConfigBase::diff:518` and
`DynamicConfig::diff:1905` — which both walk `keys()` — cannot see them. A preserved key
therefore cannot mark a pristine preset modified, and cannot reach `PnpConfigTranslator`
either. That is structural, not an exclusion list.

**Read.** `load_from_json` gains a defaulted `t_unknown_config_values *unknown_out = nullptr`.
Non-null, a cleared key (and, on the unreachable path, a thrown `UnknownOptionException`) has
its raw JSON fragment collected and loading continues. Null, nothing changes at all.
Preservation is opt-in per call site; `unrecogized_keys` is still populated exactly as before.

**Write.** `save_to_json` gains a symmetric `const t_unknown_config_values *extra = nullptr`,
merged into the document after the live keys. A carrier entry whose key is defined now — the
module declaring it has since been installed, so ticket 02 registered it — is dropped in
favour of the live value.

**Call sites opted in:** `bbs_3mf.cpp:2781` (project config), `:2813` (project-embedded
presets), `Preset::reload`, `PresetCollection::load_presets`,
`PresetBundle::import_json_presets`. **Not opted in:** `PresetBundle.cpp:4936` (inherits
sub-file resolution), and the physical-printer loader at `Preset.cpp:4218` — a deviation from
the plan: `PhysicalPrinter` is not a `Preset`, has no carrier, and holds host and network
keys that no pnp module declares. Stated as a limit rather than given a fourth store.

`.ini` and the G-code `CONFIG_BLOCK` are untouched. Both already tolerate per key, `.ini` is
a legacy import path this fork never writes back, and the CONFIG_BLOCK is the echo pnp makes
of its own resolved config — preserving unknowns out of it would round-trip pnp output into
a preset.

**Save-side details.** `Preset::save` merges the carrier into all three branches
*unconditionally*, never through `config.diff(*parent_config)` (`Preset.cpp:693`): a derived
preset writes only its diff, and a preserved key is in neither config, so it would otherwise
be dropped on every save of an inheriting preset. The carrier is a plain `Preset` member, so
"Save preset as..." carries it to the copy. Project-embedded presets take
`_add_project_embedded_presets_to_archive` rather than `Preset::save` (which returns early
for them), so that exporter merges too. `Model::load_from` moves the carrier from the Model
of the importer onto the Model of the Plater, which is the one the exporter writes from.

Preserved keys are written back **always**, whatever presets the user has since selected. We
cannot attribute a key to a module, so a staleness heuristic would have nothing to work from.

**Warning.** New `PnpWarningClass::UnresolvedPreserved` ("unresolved-preserved"), appended to
the existing `pnp-config-warnings.jsonl` with a `source` field and no `orca_value` or
`sent_value`, since a load has no slice. It bypasses `filter_pnp_config_warnings`, which
needs a resolved full config. `format_pnp_config_warning_message` gained a matching fourth
section; `format_pnp_unresolved_keys_message` is the load-time message.

3mf loads notify inline from `Plater::priv::load_files`. Preset loads run in
`on_init_inner()` before the notification manager exists, so `GUI_App::post_init()` walks the
loaded bundle and reports once, deduplicated across presets — beside the deferred
`show_failure_notification()` from ticket 02, for the same reason. Walking the bundle after
the fact keeps the plumbing out of libslic3r entirely: the carrier is already on each
`Preset`.

**Keys only, never modules.** pnp module keys are not namespaced (`wave_overhang_pattern`,
`max_bead_count` — asset sections D and F), the 3mf and preset formats store keys rather than
module ids, and an unresolved key may equally be a retired Orca key or the artifact of
another producer. The message does not claim the keys belong to pnp.

### Verification

- `libslic3r_tests` — **142 cases / 48771 assertions, all passed** (the full suite, not just
  the new cases). New: `Unresolvable project config keys survive a .3mf round-trip` in
  `tests/libslic3r/test_3mf.cpp` (a real export, import, export, import cycle over a scalar
  and a list key, asserting the known key survives, the unresolvable ones come back
  byte-identical, and neither is in the config); `load_from_json tolerates and preserves keys
  this build cannot resolve` and `save_to_json prefers the live value over a stale preserved
  entry` in `tests/libslic3r/test_config.cpp`, including a case pinning the unchanged
  no-carrier behaviour.
- `pnp_runtime_tests` — 29 cases / 263 assertions, all passed; the `[warnings]` tag alone is
  14 cases / 66 assertions. Four new cases cover the sink record shape, the empty-list no-op,
  the message cap, and the fourth formatter section.
- `pnp_config_translator_tests` — 14 cases / 879 assertions, all passed (unchanged).
- `libslic3r` and `libslic3r_gui` both compile clean.

### Not done here

No UI lists or clears the preserved keys — the notification and the jsonl are the only
surfaces. A project can accumulate them indefinitely with no way to purge them short of
editing the file.
