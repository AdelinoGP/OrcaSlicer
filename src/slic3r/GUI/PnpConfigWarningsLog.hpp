#pragma once

// PNP fork: write side of the config-warnings channel (wayfinder ticket 013,
// fork ticket F03). Takes the warning vector produced by PnpConfigTranslator
// (F01), filters not-yet-mapped records whose key sits at its Orca default,
// and appends one JSON object per surviving record to
// <data_dir>/pnp-config-warnings.jsonl, plus one summary line in the boost
// log. Warnings are a dev instrument only — no UI, ever.

#include <vector>

#include "PnpConfigWarning.hpp"

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

// `full` is the resolved full config for the slice (all keys present, defaults
// filled in — e.g. preset_bundle->full_config()); it is used only to compute
// the changed-from-default key set for the not-yet-mapped filter.
// `plate` is the 0-based plate index of the slice; pass -1 if not applicable.
void log_pnp_config_warnings(const DynamicPrintConfig&      full,
                             std::vector<PnpConfigWarning>&& warnings,
                             int                             plate = -1);

}} // namespace Slic3r::GUI
