#pragma once

// PNP fork (wayfinder ticket F05): per-plate temp 3MF export producing the
// model input `pnp_cli slice --model` consumes. Wraps Orca's existing
// plate-scoped 3MF export path (single <build>, plate-local origin). Per-object
// / per-volume config deltas stay untouched under their Orca key names in
// Metadata/model_settings.config; transforms (including non-uniform scale)
// pass straight through. One plate per pnp_cli invocation.

#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r {
namespace GUI {

// Export plate `plate_idx` (0-based index into the Plater's PartPlateList) as a
// 3MF at `path`. Only that plate's objects are included, at plate-local
// coordinates. Returns true on success; on failure returns false and, when
// `error` is non-null, stores a human-readable reason.
// Must be called on the UI thread (reads Plater model state).
bool export_plate_3mf_for_pnp(int plate_idx, const boost::filesystem::path& path, std::string* error = nullptr);

} // namespace GUI
} // namespace Slic3r
