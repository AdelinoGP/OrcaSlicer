#pragma once

// CommonDefs.hpp
// ---------------
// This header provides common definitions and enumerations shared across multiple libraries.
// It is intended for use in projects that require consistent type definitions, such as nozzle types.
// The contents of this file are designed to be reusable and maintainable for cross-library integration.

namespace Slic3r {
// [INTENT] Small cross-module enum shared by preset/config/runtime code to keep nozzle
// material categories stable across serialization and printer capability checks.
// [COUPLING] Enum ordering is part of the implicit data contract with UI/config layers;
// inserting/reordering values can silently remap persisted presets.
// [HAZARD] ntUndefine / ntCount sentinels are relied on as numeric bounds in multiple
// call sites. Refactors should preserve sentinel semantics, not just names.
// BBS
enum NozzleType { ntUndefine = 0, ntHardenedSteel, ntStainlessSteel, ntTungstenCarbide, ntBrass, ntE3D, ntCount };
} // namespace Slic3r
