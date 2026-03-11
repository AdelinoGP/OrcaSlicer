// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] ExtrusionJunction is the minimal Arachne output primitive: one sampled point along a
// variable-width wall centerline plus the local width that should be extruded there. Higher-level
// Arachne code assembles these junctions into ExtrusionLine polylines before converting them into
// OrcaSlicer ExtrusionEntity objects.
// [MEMORY] Plain value type. Junctions are copied by value into std::vector<ExtrusionJunction>
// containers, so there is no shared ownership or external lifetime tracking.
// [COUPLING] Couples Arachne's wall planner to the rest of libslic3r through Point/coord_t only.
// This keeps the Arachne output contract light enough for WallToolPaths and VariableWidth code to
// consume without depending on the skeletal graph internals.
// [HAZARD] `z()` returns extrusion width rather than a geometric Z coordinate. Generic code that
// assumes x/y/z are Cartesian position components can silently mis-handle this type.

#ifndef UTILS_EXTRUSION_JUNCTION_H
#define UTILS_EXTRUSION_JUNCTION_H

#include "../../Point.hpp"

namespace Slic3r::Arachne {

/*!
 * This struct represents one vertex in an extruded path.
 *
 * It contains information on how wide the extruded path must be at this point,
 * and which perimeter it represents.
 */
struct ExtrusionJunction
{
    // [STATE] Centerline position of the variable-width wall at this sample point.
    // Units are scaled coord_t in the XY print plane, not model-space millimeters.
    /*!
     * The position of the centreline of the path when it reaches this junction.
     * This is the position that should end up in the g-code eventually.
     */
    Point p;

    // [STATE] Local extrusion width at this junction in scaled coord_t. Adjacent junction widths are
    // interpolated by downstream conversion code when turning an ExtrusionLine into ThickPolyline data.
    /*!
     * The width of the extruded path at this junction.
     */
    coord_t w;

    // [STATE] Outer-to-inner wall index that this junction belongs to. The index is attached to every
    // point so stitched/simplified lines can still be traced back to the intended perimeter band.
    /*!
     * Which perimeter this junction is part of.
     *
     * Perimeters are counted from the outside inwards. The outer wall has index
     * 0.
     */
    size_t perimeter_index;

    // [INTENT] Constructor stores the centerline point, instantaneous width, and wall band index as
    // one immutable sample emitted by SkeletalTrapezoidation's junction-generation pass.
    ExtrusionJunction(const Point p, const coord_t w, const coord_t perimeter_index) : p(p), w(w), perimeter_index(perimeter_index) {}

    // [INTENT] Value equality is exact: same XY point, same local width, same perimeter band.
    // [HAZARD] Exact coord_t comparison makes this unsuitable for geometric fuzzy matching.
    bool operator==(const ExtrusionJunction& other) const
    {
        return p == other.p && w == other.w && perimeter_index == other.perimeter_index;
    }

    // [INTENT] x()/y() expose the centerline point for generic point-like algorithms.
    // [HAZARD] z() intentionally aliases the width so templated 3-component code can treat width as
    // the third axis. This is a semantic overload, not a true 3D coordinate.
    coord_t x() const { return p.x(); }
    coord_t y() const { return p.y(); }
    coord_t z() const { return w; }
};

// [INTENT] Difference between two junctions is defined purely in XY space; width/perimeter metadata
// is ignored because line simplification and length calculations operate on centerline geometry.
inline Point operator-(const ExtrusionJunction& a, const ExtrusionJunction& b) { return a.p - b.p; }

// [INTENT] Identity adapter so generic polygon/path utilities can treat ExtrusionJunction as a
// point-like type without knowing about its width/perimeter metadata.
inline const Point& make_point(const ExtrusionJunction& ej) { return ej.p; }

// [INTENT] Aliases document the two common aggregation levels: a bare ordered junction list and the
// same list when passed around under the more domain-specific `ExtrusionJunctions` name.
using LineJunctions = std::vector<
    ExtrusionJunction>; //<! The junctions along a line without further information. See \ref ExtrusionLine for a more extensive class.
using ExtrusionJunctions = std::vector<ExtrusionJunction>;

} // namespace Slic3r::Arachne
#endif // UTILS_EXTRUSION_JUNCTION_H
