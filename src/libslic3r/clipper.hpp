// Hackish wrapper around the ClipperLib library to compile the Clipper library using Slic3r's own Point type.
// [INTENT] Force all Clipper client code to consume a single adapted header so integer geometry stays ABI-compatible.

#ifndef slic3r_clipper_hpp

#ifdef clipper_hpp
#error "You should include the libslic3r/clipper.hpp before clipper/clipper.hpp"
#endif

#ifdef CLIPPERLIB_USE_XYZ
#error "Something went wrong. Using clipper.hpp with Slic3r Point type, but CLIPPERLIB_USE_XYZ is defined."
#endif

#define slic3r_clipper_hpp

#include "Point.hpp"

#define CLIPPERLIB_NAMESPACE_PREFIX Slic3r
#define CLIPPERLIB_INTPOINT_TYPE Slic3r::Point
// [HAZARD] This wrapper depends on pre-include macro contracts from third-party Clipper.
// [COUPLING] libslic3r Point layout is part of Clipper's effective API after macro substitution.

#include <clipper/clipper.hpp>

#undef clipper_hpp
#undef CLIPPERLIB_NAMESPACE_PREFIX
#undef CLIPPERLIB_INTPOINT_TYPE
// [STATE] Macros are explicitly undefined to prevent accidental leakage into unrelated translation units.

#endif // slic3r_clipper_hpp
