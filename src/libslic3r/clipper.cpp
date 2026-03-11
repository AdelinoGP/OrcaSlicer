// Hackish wrapper around the ClipperLib library to compile the Clipper library using Slic3r::Point.
// [INTENT] Build Clipper implementation once with libslic3r point ABI so all polygon ops share one coordinate type.

#include "clipper.hpp"

// Don't include <clipper/clipper.hpp> for the second time.
#define clipper_hpp

// Override ClipperLib namespace to Slic3r::ClipperLib
#define CLIPPERLIB_NAMESPACE_PREFIX Slic3r
// Override Slic3r::ClipperLib::IntPoint to Slic3r::Point
#define CLIPPERLIB_INTPOINT_TYPE Slic3r::Point
// [COUPLING] Macro injection ties this TU to upstream Clipper preprocessor extension points.
// [HAZARD] Any upstream rename/removal of these macros breaks build-time adaptation silently.

#include <clipper/clipper.cpp>
