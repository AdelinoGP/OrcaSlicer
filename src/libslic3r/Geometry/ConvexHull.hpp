#ifndef slic3r_Geometry_ConvexHull_hpp_
#define slic3r_Geometry_ConvexHull_hpp_

// [INTENT] ConvexHull: 2D and 3D convex hull computation, rotating calipers intersection test,
// and O(log n) point-in-convex-polygon via top/bottom trapezoidal decomposition.
// [COUPLING] Used by support material (bounding hulls), bed leveling, object placement,
//   and collision avoidance. convex_polygons_intersect() is the backbone of exclusion zone checks.
// [MEMORY] All functions take input by value or const-ref; output allocated on caller's heap.

#include <vector>

#include "../Polygon.hpp"

namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

namespace Geometry {

// [INTENT] Andrew's monotone chain 2D convex hull. O(n log n). Input: point sets / polygons.
// [HAZARD] H577 (Low): convex_hulll (note triple 'l') is a typo in the function name.
//   Port note: rename to convex_hull(const Polylines&) in any clean port.
Pointf3s convex_hull(Pointf3s points);
Polygon  convex_hull(Points points);
Polygon  convex_hull(const Polygons& polygons);
Polygon  convex_hull(const ExPolygons& expolygons);
Polygon  convex_hulll(const Polylines& polylines); // [HAZARD] H577: triple-l typo in function name

// [INTENT] Returns true if the intersection of the two convex polygons A and B
// is not an empty set. Uses Rotating Calipers algorithm with boost::multiprecision
// exact arithmetic to avoid integer overflow on scaled coordinates.
// [COMPLEXITY] O(|A| + |B|) after O(n) initial extrema search.
// [COUPLING] Called by wipe tower placement, exclusion zone checks.
// Returns true if the intersection of the two convex polygons A and B
// is not an empty set.
bool convex_polygons_intersect(const Polygon& A, const Polygon& B);

// [INTENT] Precompute a trapezoidal decomposition of a CCW convex polygon into top/bottom chains
// for O(log n) point-in-polygon queries.
// [STATE] Result pair: first=bottom chain (x-monotone, ascending), second=top chain (x-monotone,
//   ascending after std::reverse). Invalid if either chain has < 2 points.
// [COUPLING] Used together with inside_convex_polygon() for repeated point queries.
// Decompose source convex hull points into top / bottom chains with monotonically increasing x,
// creating an implicit trapezoidal decomposition of the source convex polygon.
// The source convex polygon has to be CCW oriented. O(n) time complexity.
std::pair<std::vector<Vec2d>, std::vector<Vec2d>> decompose_convex_polygon_top_bottom(const std::vector<Vec2d>& src);

// [INTENT] O(log n) point-in-convex-polygon test via binary search on top/bottom chains.
// [COUPLING] Must be called with the precomputed pair from decompose_convex_polygon_top_bottom().
// Convex polygon check using a top / bottom chain decomposition with O(log n) time complexity.
bool inside_convex_polygon(const std::pair<std::vector<Vec2d>, std::vector<Vec2d>>& top_bottom_decomposition, const Vec2d& pt);

} // namespace Geometry
} // namespace Slic3r

#endif
