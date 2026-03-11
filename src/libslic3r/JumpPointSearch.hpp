#pragma once
#ifndef SRC_LIBSLIC3R_JUMPPOINTSEARCH_HPP_
#define SRC_LIBSLIC3R_JUMPPOINTSEARCH_HPP_

#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include <unordered_map>
#include <unordered_set>

namespace Slic3r {

class JPSPathFinder
{
    using Pixel = Point;
    // [STATE] Mutable occupancy set updated per layer/pathfinding request by clear() and add_obstacles().
    std::unordered_set<Pixel, PointHash> inpassable;
    coordf_t                             print_z;
    BoundingBox                          max_search_box;
    Lines                                bed_shape;

    // [HAZARD] Fixed quantization can miss narrow channels; path quality depends on this compile-time constant.
    const coord_t resolution = scaled(1.5);
    Pixel         pixelize(const Point &p) { return p / resolution; }
    Point         unpixelize(const Pixel &p) { return p * resolution; }

public:
    JPSPathFinder() = default;
    // [COUPLING] Consumes bed geometry derived from print config and caches it as segment lines.
    void     init_bed_shape(const Points &bed_shape) { this->bed_shape = (to_lines(Polygon{bed_shape})); };
    void     clear();
    void     add_obstacles(const Lines &obstacles);
    // [INTENT] Jump Point Search prunes symmetric A* expansions for fast collision-avoiding travel planning.
    // [CONCURRENCY] Object is not synchronized; each worker thread should own its own finder instance.
    Polyline find_path(const Point &start, const Point &end);
};

} // namespace Slic3r

#endif /* SRC_LIBSLIC3R_JUMPPOINTSEARCH_HPP_ */
