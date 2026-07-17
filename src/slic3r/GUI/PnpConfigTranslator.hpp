#pragma once

// PNP fork (wayfinder ticket F01): pure translator from an Orca
// DynamicPrintConfig (preset_bundle->full_config(), ~925 keys) to the flat-key
// PNP JSON that `pnp_cli slice --config` consumes, plus a classified warning
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

} // namespace PnpConfigTranslator

}} // namespace Slic3r::GUI
