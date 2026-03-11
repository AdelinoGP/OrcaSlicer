#ifndef slic3r_Clipper2Utils_hpp_
#define slic3r_Clipper2Utils_hpp_

#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "clipper2/clipper.h"

namespace Slic3r {

// [INTENT] These adapters isolate Clipper2 integer polygon APIs from libslic3r geometry containers.
// [COUPLING] Polygon/infill code depends on this translation boundary to keep Clipper2-specific types local.
Clipper2Lib::Paths64 Slic3rPolylines_to_Paths64(const Slic3r::Polylines& in);
Slic3r::Polylines    Paths64_to_polylines(const Clipper2Lib::Paths64& in);
// [INTENT] Boolean wrappers mirror legacy Clipper1 utility naming so higher layers can swap backend versions.
Slic3r::Polylines intersection_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines diff_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
ExPolygons        union_ex_2(const Polygons& expolygons);
ExPolygons        union_ex_2(const ExPolygons& expolygons);
// [HAZARD] Offsets use floating delta on top of integer-coordinate clipping; unit conversion precision must stay consistent.
ExPolygons offset_ex_2(const ExPolygons& expolygons, double delta);
ExPolygons offset2_ex_2(const ExPolygons& expolygons, double delta1, double delta2);
} // namespace Slic3r

#endif
