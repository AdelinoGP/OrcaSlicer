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

PnpTranslationResult translate(const DynamicPrintConfig& full_config);

} // namespace PnpConfigTranslator

}} // namespace Slic3r::GUI
