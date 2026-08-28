#pragma once

// PNP fork (wayfinder ticket F01): pure translator from an Orca
// DynamicPrintConfig (preset_bundle->full_config(), ~925 keys) to the flat-key
// PNP JSON that rides in the 3MF's project_settings.config sidecar (read by
// `pnp_cli slice` and `pnp_cli support-preview`), plus a classified warning
// vector. Mapping table and tier semantics: .wayfinder/assets/005-preset-to-pnp-config-mapping.md.
//
// This is a pure function: no file IO, no logging, no UI. Every Tier-B/C/D
// record is emitted unconditionally; filtering against defaults and writing
// the jsonl sink is the caller's job (F03, wayfinder ticket 013).

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "PnpConfigWarning.hpp"

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

struct PnpTranslationResult
{
    nlohmann::json                json;     // flat JSON object of PNP config keys
    std::vector<PnpConfigWarning> warnings; // one record per unmapped/lossy orca key
    // Provenance (ticket 05): every Orca key translate() consumed, mapped to
    // the pnp keys it wrote for it. An empty target list means the row read the
    // key but deliberately sent nothing. This is what makes the handled set
    // derivable from the code that does the routing, instead of a parallel
    // list that silently drifts out of date — the failure ticket 01 found,
    // where five settings stopped reaching pnp at a submodule bump and the
    // static handled set went on calling them implemented.
    std::map<std::string, std::vector<std::string>> routed;
};

namespace PnpConfigTranslator {

// The set of config keys the live pnp backend actually reads (SchemaBridgeMap
// ticket 05). Built from a `module config-schema` document; see
// pnp_key_universe_from_schema(). This replaces TIER_A_KEYS as the fork's
// picture of what pnp binds, so a pnp key added upstream reaches the GUI with
// no fork edit.
using PnpKeyUniverse = std::set<std::string>;


// Schema guard (F01 follow-up): validate a translated config against the
// parsed `pnp_cli module config-schema` document and drop every key whose
// value pnp's config resolution would reject — wrong JSON type, out of
// [min, max] range, or unknown enum value — appending one lossy-fallback
// warning per dropped key. pnp then falls back to its own default, so a
// config mismatch degrades to a logged warning instead of a failed slice.
// Keys the schema does not mention are left untouched (pnp ignores them).
void apply_schema_guard(nlohmann::json& config, const nlohmann::json& schema_doc,
                        std::vector<PnpConfigWarning>& warnings);

// Collect every key the backend declares out of a parsed `module config-schema`
// document: the per-module manifest fields plus the `host` array added by wire
// 1.1.0 (ticket 02). Pure. An older document with no `host` array yields the
// module half only, which is what those backends could describe.
PnpKeyUniverse pnp_key_universe_from_schema(const nlohmann::json& schema_doc);

// Install the universe for the process. Called once at startup after the probe,
// beside the ticket-02 key registration. Unlike that registry this is not
// sealed: it is read-only data with no ordinal or preset consequences, so
// re-installing it is harmless and lets tests drive a fixture universe.
void           set_pnp_key_universe(PnpKeyUniverse universe);
// False until set_pnp_key_universe() runs — the fork has not probed, so it has
// no evidence about what pnp binds.
bool           pnp_key_universe_known();
// Clears the installed universe; returns to the unprobed state. For tests.
void           reset_pnp_key_universe();

// `universe` nullptr means "not probed": the identity pass falls back to the
// compiled-in TIER_A_KEYS list so an unprobed translate() keeps behaving as it
// did before ticket 05. The one-argument form uses the installed universe.
PnpTranslationResult translate(const DynamicPrintConfig& full_config, const PnpKeyUniverse* universe);
PnpTranslationResult translate(const DynamicPrintConfig& full_config);

// The Orca keys that reach pnp, derived rather than listed (ticket 05):
//
//     handled(k) = k is itself a key the backend declares      (identity routing)
//               or translate() routed k to at least one such key (curated table)
//
// A curated row whose target no longer exists therefore stops making its source
// count as handled, which is exactly the drift the static set could not see.
std::set<std::string> pnp_handled_keys(const PnpKeyUniverse* universe);

// True when `orca_key` has no PNP equivalent, per pnp_handled_keys() against
// the installed universe. Drives the amber label tint on unimplemented options
// in the settings tabs.
//
// With no universe installed (pnp_cli missing, or the probe failed) every key
// reads unimplemented: without a backend nothing reaches pnp at all, so the
// tint is literally true and the broken install is visible on every tab rather
// than only on the PNP page's error banner.
bool pnp_key_is_unimplemented(const std::string& orca_key);

// True when `orca_value` of a pattern key maps to a real pnp infill module
// (i.e. the module holds the fill-role claim the key translates to). False
// for the fallback values and for keys with no pattern mapping at all.
// Drives the per-item yellow tint in the pattern dropdowns.
bool pnp_pattern_value_supported(const std::string& orca_key, const std::string& orca_value);

// True when `orca_key` is an infill-pattern enum key whose values are
// per-value checked against pnp support (sparse/top/bottom/internal solid).
// Gates the per-item dropdown tint: only these dropdowns get tinted items.
bool pnp_pattern_key(const std::string& orca_key);

} // namespace PnpConfigTranslator

}} // namespace Slic3r::GUI
