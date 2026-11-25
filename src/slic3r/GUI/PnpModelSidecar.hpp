#pragma once

// PNP fork: merge the translated PNP config into a 3MF's
// Metadata/project_settings.config sidecar. GUI-free (miniz + boost::filesystem
// + nlohmann::json + the pure PnpConfigTranslator schema guard) so the pnp test
// targets can exercise it directly.

#include <string>

#include <boost/filesystem/path.hpp>

#include <nlohmann/json_fwd.hpp>

namespace Slic3r {
namespace GUI {

// Rewrite `path` (a 3MF written by store_bbs_3mf) so its
// Metadata/project_settings.config sidecar carries `translated` merged over the
// raw Orca config. The translated values are typed JSON (numbers, booleans,
// arrays), which pnp's sidecar parser maps to typed ConfigValues directly —
// the string round-trip through Orca's DynamicPrintConfig would corrupt them
// (e.g. bed_shape must stay a float list, and "0"/"1" strings would coerce to
// booleans for undeclared keys). Returns true on success.
//
// When `schema` is non-null it is the parsed `pnp_cli module config-schema`
// document, and the MERGED sidecar is run through
// PnpConfigTranslator::apply_schema_guard before it is written: the merge is
// the only channel pnp_cli reads, so this is the last place where a value
// whose wire shape pnp's config resolution would reject (a scalar against a
// declared list — the single-filament `filament_cost` failure) can be repaired
// or dropped instead of failing the slice with `config resolution failed`.
// Repairs are lossless and silent; drops are logged.
bool merge_translated_config_into_3mf(const boost::filesystem::path& path, const nlohmann::json& translated,
                                      const nlohmann::json* schema = nullptr);

} // namespace GUI
} // namespace Slic3r
