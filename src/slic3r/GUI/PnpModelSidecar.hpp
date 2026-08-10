#pragma once

// PNP fork: merge the translated PNP config into a 3MF's
// Metadata/project_settings.config sidecar. GUI-free (miniz + boost::filesystem
// + nlohmann::json only) so the pnp test targets can exercise it directly.

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
bool merge_translated_config_into_3mf(const boost::filesystem::path& path, const nlohmann::json& translated);

} // namespace GUI
} // namespace Slic3r
