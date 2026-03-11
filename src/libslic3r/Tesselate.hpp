#ifndef slic3r_Tesselate_hpp_
#define slic3r_Tesselate_hpp_

#include <vector>

#include "Point.hpp"

// [INTENT] Provides tessellation functions to convert 2D ExPolygon shapes into
// triangles for 3D mesh rendering. Essential for generating printable geometry
// from slice data - bridges 2D slice polygons back to 3D triangle meshes.
// [COUPLING] Depends on ExPolygon (from Polygon.hpp) - core polygon type used
// throughout slicing pipeline. Uses Vec3d/Vec2d from Point.hpp.

namespace Slic3r {

class ExPolygon;
typedef std::vector<ExPolygon> ExPolygons;

// [INTENT] Direction constants for normal vector orientation. NORMALS_UP produces
// triangles pointing upward (default for top surfaces), NORMALS_DOWN flips for
// bottom surfaces or holes. Critical for lighting and face culling.
// [HAZARD] constexpr bools declared at namespace scope - potential ODR issues if
// included in multiple translation units with different definitions.
const bool constexpr NORMALS_UP   = false;
const bool constexpr NORMALS_DOWN = true;

// [INTENT] 3D tessellation - converts 2D ExPolygon at given Z height into 3D triangle vertices.
// z parameter places polygons at specific layer height. flip parameter controls normal direction.
// Used for: generating mesh geometry from slice data, support structure meshes, visual preview.
extern std::vector<Vec3d> triangulate_expolygon_3d(const ExPolygon& poly, coordf_t z = 0, bool flip = NORMALS_UP);
extern std::vector<Vec3d> triangulate_expolygons_3d(const ExPolygons& polys, coordf_t z = 0, bool flip = NORMALS_UP);

// [INTENT] 2D tessellation - produces 2D triangle vertices for flat mesh generation.
// Useful for: flat label surfaces, decal projection, screen-space overlays.
// [MEMORY] Returns vector by value - copy elision (RVO) expected in C++17.
extern std::vector<Vec2d> triangulate_expolygon_2d(const ExPolygon& poly, bool flip = NORMALS_UP);
extern std::vector<Vec2d> triangulate_expolygons_2d(const ExPolygons& polys, bool flip = NORMALS_UP);
extern std::vector<Vec2f> triangulate_expolygon_2f(const ExPolygon& poly, bool flip = NORMALS_UP);
extern std::vector<Vec2f> triangulate_expolygons_2f(const ExPolygons& polys, bool flip = NORMALS_UP);

} // namespace Slic3r

#endif /* slic3r_Tesselate_hpp_ */
