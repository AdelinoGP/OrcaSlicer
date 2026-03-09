// [INTENT] Public API for the FuzzySkin feature.
// FuzzySkin perturbs the outer perimeter of each layer with configurable noise
// (Perlin, Billow, RidgedMulti, Voronoi, or Uniform) to produce a textured
// surface finish.  Supports both classic Polygon perimeters and Arachne
// variable-width ExtrusionLine perimeters.
//
// [COUPLING] Depends on PerimeterGenerator (for FuzzySkinConfig, slice_z,
// layer_id, regions_by_fuzzify), ExtrusionJunction/ExtrusionLine (Arachne path),
// and Algorithm::LineSplit (per-region splitting).
//
// [STATE] Stateless free functions — all state lives in the PerimeterGenerator
// that is passed in.

#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"

namespace Slic3r::Feature::FuzzySkin {

// [INTENT] Apply fuzzy noise perturbation in-place to a classic polygon path.
// [STATE] Uses thread_local RNG for Uniform noise — thread-safe.
// [HAZARD H982 P2] `poly` is modified in-place; callers that cache the
// original polygon must copy before calling.  No copy is made internally.
void fuzzy_polyline(Points& poly, bool closed, coordf_t slice_z, const FuzzySkinConfig& cfg);

// [INTENT] Apply fuzzy noise perturbation in-place to an Arachne extrusion
// junction list.
// [HAZARD H983 P2] `ext_lines` is modified in-place (same as H982 for polygon).
// For FuzzySkinMode::Combined the new extrusion width depends on noise value
// plus min_extrusion_width (magic constant 0.01) — not tied to layer height
// parameter.  Comment in source calls this a "workaround".
void fuzzy_extrusion_line(Arachne::ExtrusionJunctions& ext_lines, coordf_t slice_z, const FuzzySkinConfig& cfg, bool closed = true);

// [INTENT] Pre-process all compatible regions in a PerimeterGenerator to group
// them by their FuzzySkinConfig.  Stores ExPolygon clip regions into
// g.regions_by_fuzzify for use in apply_fuzzy_skin().
// [STATE] Writes to g.regions_by_fuzzify, g.has_fuzzy_skin, g.has_fuzzy_hole.
// [COUPLING] Tightly coupled to PerimeterGenerator internal fields.
void group_region_by_fuzzify(PerimeterGenerator& g);

// [INTENT] Decide whether a given loop/contour at this layer and loop index
// should be fuzzified given the FuzzySkinConfig.
// [MEMORY] Pure predicate — no allocation.
bool should_fuzzify(const FuzzySkinConfig& config, int layer_id, size_t loop_idx, bool is_contour);

// [INTENT] Return a new fuzzified Polygon (classic perimeter path).
// Splits the polygon by active region boundaries and applies per-region noise.
// [MEMORY] Returns by value — one allocation per call.
// [HAZARD H984 P3] When all regions share the same config (optimization path),
// the full polygon is fuzzified regardless of region boundaries.  This is
// correct for single-region prints but silently skips region clipping.
Polygon apply_fuzzy_skin(const Polygon& polygon, const PerimeterGenerator& perimeter_generator, size_t loop_idx, bool is_contour);

// [INTENT] Apply fuzzy noise in-place to an Arachne ExtrusionLine.
// [HAZARD H985 P2] Modifies extrusion->junctions in-place.  If multi-region
// splitting is applied, the original junction list is copied to `current_ext`
// locally.  For the single-region optimization path no copy is made — the
// original is lost on modification.
void apply_fuzzy_skin(Arachne::ExtrusionLine* extrusion, const PerimeterGenerator& perimeter_generator, bool is_contour);

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_FuzzySkin_hpp_
