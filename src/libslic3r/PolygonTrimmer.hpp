#ifndef slic3r_PolygonTrimmer_hpp_
#define slic3r_PolygonTrimmer_hpp_

#include "libslic3r.h"
#include <vector>
#include <string>
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"
#include "Polygon.hpp"

namespace Slic3r {

namespace EdgeGrid {
class Grid;
}

// [INTENT] Container for a potentially trimmed closed contour plus per-segment provenance.
struct TrimmedLoop
{
    Points points;
    // Number of points per segment. Empty if the loop is
    std::vector<unsigned int> segments;

    bool is_trimmed() const { return !segments.empty(); }
};

// [INTENT] Remove loop sections that collide with grid-registered obstacles and return a compact loop representation.
// [COUPLING] Requires the caller to use the same coordinate space/resolution as EdgeGrid::Grid construction.
TrimmedLoop trim_loop(const Polygon& loop, const EdgeGrid::Grid& grid);
// [INTENT] Batch loop trimming against a shared grid to reduce repeated index lookups.
std::vector<TrimmedLoop> trim_loops(const Polygons& loops, const EdgeGrid::Grid& grid);

} // namespace Slic3r

#endif /* slic3r_PolygonTrimmer_hpp_ */
