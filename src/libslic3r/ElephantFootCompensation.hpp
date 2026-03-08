#ifndef slic3r_ElephantFootCompensation_hpp_
#define slic3r_ElephantFootCompensation_hpp_

// [INTENT] Elephant Foot Compensation (EFC) corrects the dimensional expansion
// that occurs on the first printed layer due to the nozzle squishing molten
// filament outward against the bed. Without compensation the bottom outline is
// larger than all subsequent layers, creating a visible "elephant foot" flare.
// [COUPLING] Depends on ExPolygon (geometry), Flow (external perimeter width/
// spacing used to derive the minimum safe contour width). Called from
// PrintObject::process_layers() or equivalent before perimeter generation.
// All compensation values are in mm (unscaled); the implementation scales them
// internally.

#include "libslic3r.h"
#include "ExPolygon.hpp"
#include <vector>

namespace Slic3r {

class Flow;

// [INTENT] Overload set: compensate a single ExPolygon or a collection.
// The `min_contour_width` variants allow callers to supply an explicit minimum
// width; the Flow variants derive it from the external perimeter flow geometry.
// [STATE] All return new ExPolygon(s); the input is not modified.
// [HAZARD] H764 — The compensation value is expected to be positive (shrink
// outward). Passing a negative value will expand the contour rather than shrink
// it; there is no clamping guard in the public API.
ExPolygon  elephant_foot_compensation(const ExPolygon& input, double min_countour_width, const double compensation);
ExPolygons elephant_foot_compensation(const ExPolygons& input, double min_countour_width, const double compensation);
ExPolygon  elephant_foot_compensation(const ExPolygon& input, const Flow& external_perimeter_flow, const double compensation);
ExPolygons elephant_foot_compensation(const ExPolygons& input, const Flow& external_perimeter_flow, const double compensation);

} // namespace Slic3r

#endif /* slic3r_ElephantFootCompensation_hpp_ */
