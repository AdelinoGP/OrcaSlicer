// [INTENT] Defines the process-global application version as a Semver object.
// SLIC3R_VERSION is injected by CMake at configure time. This object is used
// throughout the codebase for version checks, file format compatibility guards,
// and 3MF metadata. The single line `Semver SEMVER { SLIC3R_VERSION }` is the
// entire implementation; everything else lives in Semver.hpp.
// [STATE] SEMVER is a process-global const object after static-init; effectively read-only.
// [COUPLING] Semver.hpp, CMake SLIC3R_VERSION define, 3MF/AMF format writers.
#include "libslic3r.h"

namespace Slic3r {

Semver SEMVER{SLIC3R_VERSION};

}
