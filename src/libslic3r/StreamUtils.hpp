#ifndef slic3r_StreamUtils_hpp_
#define slic3r_StreamUtils_hpp_

#include "Point.hpp"
#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "ExPolygon.hpp"
#include <sstream>
#include <vector>

namespace Slic3r {

// [INTENT] Provide lightweight debug / diagnostics serialization for core
// geometry containers so algorithm traces can dump polygons and point sets
// without introducing a dedicated logging abstraction.
// [COUPLING] Overloads bind directly to core geometry types (Point, Polygon,
// ExPolygon), so callers across libslic3r can stream these types with the
// standard iostream API.
// [HAZARD] Several loops iterate by value (copying Point/Polygon instances),
// which is acceptable for debug output but can be expensive if used in hot
// paths or accidentally compiled into high-frequency logging.

inline std::ostream& operator<<(std::ostream& os, const Points& pts)
{
    // [INTENT] Prefix with container size to make downstream log parsing robust
    // when multiple point arrays are concatenated into one stream.
    os << "[" << pts.size() << "]:";
    for (Point p : pts)
        os << " (" << p << ")";
    os << "\n";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const MultiPoint& mpts)
{
    os << "Multipoint" << mpts.points;
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Polygon& poly)
{
    os << "Polygon" << poly.points;
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Polygons& polys)
{
    os << "Polygons[" << polys.size() << "]:" << "\n";
    for (Polygon p : polys)
        os << " " << p;
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const ExPolygon& epoly)
{
    // [INTENT] Emit contour and holes separately because many slicer bugs
    // involve incorrect contour-hole relationships rather than raw vertices.
    os << "ExPolygon:\n";
    os << "  contour: " << epoly.contour;
    os << "  holes: " << epoly.holes;
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const ExPolygons& epolys)
{
    os << "ExPolygons[" << epolys.size() << "]:" << "\n";
    for (ExPolygon p : epolys)
        os << " " << p;
    return os;
}

} // namespace Slic3r
#endif // slic3r_StreamUtils_hpp_
