// Copyright (c) 2022 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] These specializations bind the generic stitcher to the two concrete path domains used
// in OrcaSlicer: plain polygons and Arachne variable-width walls.
// [COUPLING] The `VariableWidthLines` specialization depends on `ExtrusionLine::is_odd`, so any
// port that changes Arachne's wall metadata also has to revisit stitchability/orientation rules.

#include "PolylineStitcher.hpp"

#include "ExtrusionLine.hpp"
#include "libslic3r/Arachne/utils/PolygonsPointIndex.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r { namespace Arachne {
struct ExtrusionJunction;
}} // namespace Slic3r::Arachne

namespace Slic3r::Arachne {

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::canReverse(const PathsPointIndex<VariableWidthLines>& ppi)
{
    // [INTENT] Only odd centerlines may flip direction freely. Even wall bands encode sidedness
    // relative to their neighboring wall and must keep that orientation stable.
    if ((*ppi.polygons)[ppi.poly_idx].is_odd)
        return true;
    else
        return false;
}

template<> bool PolylineStitcher<Polygons, Polygon, Point>::canReverse(const PathsPointIndex<Polygons>&) { return true; }

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::canConnect(const ExtrusionLine& a, const ExtrusionLine& b)
{
    // [INTENT] Prevent stitching an odd center path into an even paired wall path; doing so would
    // break the beading assumptions used when later converting lines into extrusion entities.
    return a.is_odd == b.is_odd;
}

template<> bool PolylineStitcher<Polygons, Polygon, Point>::canConnect(const Polygon&, const Polygon&) { return true; }

template<> bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::isOdd(const ExtrusionLine& line)
{
    return line.is_odd;
}

template<> bool PolylineStitcher<Polygons, Polygon, Point>::isOdd(const Polygon&) { return false; }

} // namespace Slic3r::Arachne
