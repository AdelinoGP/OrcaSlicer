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
};

namespace PnpConfigTranslator {

// Schema guard (F01 follow-up): validate a translated config against the
// parsed `pnp_cli module config-schema` document and drop every key whose
// value pnp's config resolution would reject — wrong JSON type, out of
// [min, max] range, or unknown enum value — appending one lossy-fallback
// warning per dropped key. pnp then falls back to its own default, so a
// config mismatch degrades to a logged warning instead of a failed slice.
// Keys the schema does not mention are left untouched (pnp ignores them).
void apply_schema_guard(nlohmann::json& config, const nlohmann::json& schema_doc,
                        std::vector<PnpConfigWarning>& warnings);

PnpTranslationResult translate(const DynamicPrintConfig& full_config);

// True when `orca_key` has no PNP equivalent: it is consumed by no Tier-A/B
// row, so translate() emits a Tier-D warning for it (ticket 013). Drives the
// yellow label tint on unimplemented options in the settings tabs.
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
