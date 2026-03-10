// [INTENT] Defines the global SCALING_FACTOR variable (= 1e6) that converts
// millimetres to Clipper's integer coordinate space (coord_t / int64_t).
// [STATE] Global mutable double – technically writable at runtime but treated
// as a constant everywhere in the codebase.  Any multi-threaded read is safe
// because the value is set once before threading starts and never changes.
// [COUPLING] libslic3r.h declares SCALING_FACTOR as extern; this TU provides
// the single definition.  SCALING_FACTOR_INTERNAL is a compile-time constant
// in the header.
#include "libslic3r.h"

double SCALING_FACTOR = SCALING_FACTOR_INTERNAL;
