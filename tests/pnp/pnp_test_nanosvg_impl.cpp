// PNP fork (SchemaBridgeMap ticket 13): nanosvg implementations for the pnp
// suites that link Model (and through it NSVGUtils) but pull no GUI TU, where
// the vendored header-only library would otherwise have no translation unit to
// live in. Same pattern as tests/libslic3r/libslic3r_tests.cpp.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"