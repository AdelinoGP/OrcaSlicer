#ifndef slic3r_VariableWidth_hpp_
#define slic3r_VariableWidth_hpp_

#include "Polygon.hpp"
#include "ExtrusionEntity.hpp"
#include "Flow.hpp"

// [INTENT] Variable-width extrusion path generation from thick polylines.
// Converts geometric ThickPolyline representations into actual ExtrusionEntity paths
// that the G-code generator can emit. Bridge between geometry and extrusion system.
// [COUPLING] Depends on Polygon (ThickPolyline), ExtrusionEntity, Flow - core printing types.

namespace Slic3r {
// [INTENT] Convert single thick polyline to multi-path extrusion (multiple loops possible).
// [PARAM] tolerance: maximum deviation for simplifying the path
// [PARAM] merge_tolerance: threshold for merging adjacent extrusions
ExtrusionMultiPath thick_polyline_to_multi_path(
    const ThickPolyline& thick_polyline, ExtrusionRole role, const Flow& flow, const float tolerance, const float merge_tolerance);

// [INTENT] Process multiple thick polylines and generate extrusion entities.
void variable_width(const ThickPolylines& polylines, ExtrusionRole role, const Flow& flow, std::vector<ExtrusionEntity*>& out);
} // namespace Slic3r

#endif
