#ifndef slic3r_IntersectionPoints_hpp_
#define slic3r_IntersectionPoints_hpp_

#include "ExPolygon.hpp"

namespace Slic3r {

struct IntersectionLines {
    // [STATE] Keeps source segment indices so callers can mutate originating paths after intersection detection.
    uint32_t line_index1;
    uint32_t line_index2;
    Vec2d intersection;
};
using IntersectionsLines = std::vector<IntersectionLines>;

// collect all intersecting points
// [INTENT] Normalizes intersection discovery across line/polygon/expolygon containers.
// [COUPLING] API is anchored to ExPolygon topology types used throughout clipping/perimeter modules.
// [HAZARD] Degenerate overlaps and near-collinear crossings depend on epsilon policy in the .cpp implementation.
IntersectionsLines get_intersections(const Lines &lines);
IntersectionsLines get_intersections(const Polygon &polygon);
IntersectionsLines get_intersections(const Polygons &polygons);
IntersectionsLines get_intersections(const ExPolygon &expolygon);
IntersectionsLines get_intersections(const ExPolygons &expolygons);

} // namespace Slic3r
#endif // slic3r_IntersectionPoints_hpp_