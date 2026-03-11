// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] ExtrusionLine is the main Arachne output container: an ordered polyline of
// ExtrusionJunction samples plus metadata describing which perimeter band it belongs to and whether
// it is a closed loop or an odd centerline. WallToolPaths, PolylineStitcher, and the variable-width
// extrusion conversion code all exchange toolpaths in this representation before producing concrete
// ExtrusionPath/ExtrusionLoop objects.
// [MEMORY] Owns its junctions in a std::vector, so copies duplicate the full point+width sequence.
// This is simple and safe, but repeated simplify/stitch passes can move large vectors around.
// [COUPLING] Bridges Arachne and core libslic3r geometry: depends on Polygon/Polyline/BoundingBox,
// ThickPolyline conversion, and Flow-driven extrusion path generation.
// [HAZARD] The default constructor uses `inset_idx(-1)` as a sentinel on an unsigned size_t. Any
// code that forgets to treat that as "invalid" will see a huge positive inset index instead.

#ifndef UTILS_EXTRUSION_LINE_H
#define UTILS_EXTRUSION_LINE_H

#include <clipper/clipper_z.hpp>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <cstddef>

#include "ExtrusionJunction.hpp"
#include "../../Polyline.hpp"
#include "../../Polygon.hpp"
#include "../../BoundingBox.hpp"
#include "../../ExtrusionEntity.hpp"
#include "../../Flow.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
class ThickPolyline;
class Flow;
} // namespace Slic3r

namespace Slic3r::Arachne {

/*!
 * Represents a polyline (not just a line) that is to be extruded with variable
 * line width.
 *
 * This polyline is a sequence of \ref ExtrusionJunction, with a bit of metadata
 * about which inset it represents.
 */
struct ExtrusionLine
{
    // [STATE] Outer-to-inner wall band index for this whole path.
    /*!
     * Which inset this path represents, counted from the outside inwards.
     *
     * The outer wall has index 0.
     */
    size_t inset_idx;

    // [STATE] True for the unpaired middle line that appears when an odd number of walls must span a
    // thin region. Downstream cleanup uses this to decide which short lines may be deleted.
    /*!
     * If a thin piece needs to be printed with an odd number of walls (e.g. 5
     * walls) then there will be one wall in the middle that is not a loop. This
     * field indicates whether this path is such a line through the middle, that
     * has no companion line going back on the other side and is not a closed
     * loop.
     */
    bool is_odd;

    // [STATE] Distinguishes closed wall loops from open polylines.
    // [HAZARD] Closed lines are expected to duplicate the first point as the last junction; several
    // helpers rely on that convention instead of recomputing closure.
    /*!
     * Whether this is a closed polygonal path
     */
    bool is_closed;

    /*!
     * Gets the number of vertices in this polygon.
     * \return The number of vertices in this polygon.
     */
    size_t size() const { return junctions.size(); }

    /*!
     * Whether there are no junctions.
     */
    bool empty() const { return junctions.empty(); }

    /*!
     * The list of vertices along which this path runs.
     *
     * Each junction has a width, making this path a variable-width path.
     */
    std::vector<ExtrusionJunction> junctions;

    // [INTENT] Construct a valid toolpath shell for a known inset. Junctions are added later by
    // SkeletalTrapezoidation::generateJunctions()/generateSegments().
    ExtrusionLine(const size_t inset_idx, const bool is_odd);

    // [INTENT] Sentinel constructor used by container code that needs a default-initializable line.
    // [HAZARD] `inset_idx(-1)` becomes SIZE_MAX; treat this as invalid metadata, not a real wall.
    ExtrusionLine() : inset_idx(-1), is_odd(true), is_closed(false) {}

    // [MEMORY] Copy constructor duplicates the entire junction vector because later passes mutate
    // widths and topology in-place; sharing would be unsafe without copy-on-write.
    ExtrusionLine(const ExtrusionLine& other)
        : inset_idx(other.inset_idx), is_odd(other.is_odd), is_closed(other.is_closed), junctions(other.junctions)
    {}

    // [STATE] Move assignment transfers junction storage but copies scalar metadata verbatim.
    ExtrusionLine& operator=(ExtrusionLine&& other)
    {
        junctions = std::move(other.junctions);
        inset_idx = other.inset_idx;
        is_odd    = other.is_odd;
        is_closed = other.is_closed;
        return *this;
    }

    // [STATE] Copy assignment duplicates the full path state.
    ExtrusionLine& operator=(const ExtrusionLine& other)
    {
        junctions = other.junctions;
        inset_idx = other.inset_idx;
        is_odd    = other.is_odd;
        is_closed = other.is_closed;
        return *this;
    }

    std::vector<ExtrusionJunction>::const_iterator         begin() const { return junctions.begin(); }
    std::vector<ExtrusionJunction>::const_iterator         end() const { return junctions.end(); }
    std::vector<ExtrusionJunction>::const_reverse_iterator rbegin() const { return junctions.rbegin(); }
    std::vector<ExtrusionJunction>::const_reverse_iterator rend() const { return junctions.rend(); }
    std::vector<ExtrusionJunction>::const_reference        front() const { return junctions.front(); }
    std::vector<ExtrusionJunction>::const_reference        back() const { return junctions.back(); }
    const ExtrusionJunction&                               operator[](unsigned int index) const { return junctions[index]; }
    ExtrusionJunction&                                     operator[](unsigned int index) { return junctions[index]; }
    std::vector<ExtrusionJunction>::iterator               begin() { return junctions.begin(); }
    std::vector<ExtrusionJunction>::iterator               end() { return junctions.end(); }
    std::vector<ExtrusionJunction>::reference              front() { return junctions.front(); }
    std::vector<ExtrusionJunction>::reference              back() { return junctions.back(); }

    template<typename... Args> void emplace_back(Args&&... args) { junctions.emplace_back(args...); }
    void                            remove(unsigned int index) { junctions.erase(junctions.begin() + index); }
    void                            insert(size_t index, const ExtrusionJunction& p) { junctions.insert(junctions.begin() + index, p); }

    template<class iterator>
    std::vector<ExtrusionJunction>::iterator insert(std::vector<ExtrusionJunction>::const_iterator pos, iterator first, iterator last)
    {
        return junctions.insert(pos, first, last);
    }

    void clear() { junctions.clear(); }
    void reverse() { std::reverse(junctions.begin(), junctions.end()); }

    /*!
     * Sum the total length of this path.
     */
    int64_t getLength() const;
    int64_t polylineLength() const { return getLength(); }

    /*!
     * Put all junction locations into a polygon object.
     *
     * When this path is not closed the returned Polygon should be handled as a polyline, rather than a polygon.
     */
    Polygon toPolygon() const
    {
        Polygon ret;
        for (const ExtrusionJunction& j : junctions)
            ret.points.emplace_back(j.p);

        return ret;
    }

    /*!
     * Removes vertices of the ExtrusionLines to make sure that they are not too high
     * resolution.
     *
     * This removes junctions which are connected to line segments that are shorter
     * than the `smallest_line_segment`, unless that would introduce a deviation
     * in the contour of more than `allowed_error_distance`.
     *
     * Criteria:
     * 1. Never remove a junction if either of the connected segments is larger than \p smallest_line_segment
     * 2. Never remove a junction if the distance between that junction and the final resulting polygon would be higher
     *    than \p allowed_error_distance
     * 3. The direction of segments longer than \p smallest_line_segment always
     *    remains unaltered (but their end points may change if it is connected to
     *    a small segment)
     * 4. Never remove a junction if it has a distinctively different width than the next junction, as this can
     *    introduce unwanted irregularities on the wall widths.
     *
     * Simplify uses a heuristic and doesn't necessarily remove all removable
     * vertices under the above criteria, but simplify may never violate these
     * criteria. Unless the segments or the distance is smaller than the
     * rounding error of 5 micron.
     *
     * Vertices which introduce an error of less than 5 microns are removed
     * anyway, even if the segments are longer than the smallest line segment.
     * This makes sure that (practically) co-linear line segments are joined into
     * a single line segment.
     * \param smallest_line_segment Maximal length of removed line segments.
     * \param allowed_error_distance If removing a vertex introduces a deviation
     *         from the original path that is more than this distance, the vertex may
     *         not be removed.
     * \param maximum_extrusion_area_deviation The maximum extrusion area deviation allowed when removing intermediate
     *        junctions from a straight ExtrusionLine
     */
    // [INTENT] Remove junctions that do not materially change centerline geometry or deposited area.
    // This is the last geometric cleanup step before variable-width paths are emitted to G-code.
    void simplify(int64_t smallest_line_segment_squared, int64_t allowed_error_distance_squared, int64_t maximum_extrusion_area_deviation);

    /*!
     * Computes and returns the total area error (in μm²) of the AB and BC segments of an ABC straight ExtrusionLine
     * when the junction B with a width B.w is removed from the ExtrusionLine. The area changes due to the fact that the
     * new simplified line AC has a uniform width which equals to the weighted average of the width of the subsegments
     * (based on their length).
     *
     * \param A Start point of the 3-point-straight line
     * \param B Intermediate point of the 3-point-straight line
     * \param C End point of the 3-point-straight line
     * */
    // [INTENT] Estimate how much deposited cross-sectional area changes if middle junction B is
    // removed from a straight A-B-C segment run and replaced by one weighted-average width.
    static int64_t calculateExtrusionAreaDeviationError(ExtrusionJunction A, ExtrusionJunction B, ExtrusionJunction C);

    // [INTENT] Detect whether this closed line is an outer contour rather than a hole by checking
    // orientation on the duplicated-junction polygon convention.
    bool is_contour() const;

    // [INTENT] Signed polygon area of the centerline loop, used for orientation-sensitive logic.
    double area() const;
};

template<class PathType> static inline Slic3r::ThickPolyline to_thick_polyline(const PathType& path)
{
    // [INTENT] Convert variable-width junction samples into the ThickPolyline format expected by
    // the core extrusion-path generator. Widths are stored per segment endpoint pair rather than
    // once per vertex, hence the duplicated width push pattern.
    assert(path.size() >= 2);
    Slic3r::ThickPolyline out;
    out.points.emplace_back(path.front().x(), path.front().y());
    out.width.emplace_back(path.front().z());
    out.points.emplace_back(path[1].x(), path[1].y());
    out.width.emplace_back(path[1].z());

    auto it_prev = path.begin() + 1;
    for (auto it = path.begin() + 2; it != path.end(); ++it) {
        out.points.emplace_back(it->x(), it->y());
        out.width.emplace_back(it_prev->z());
        out.width.emplace_back(it->z());
        it_prev = it;
    }

    return out;
}

static inline Polygon to_polygon(const ExtrusionLine& line)
{
    // [INTENT] Reinterpret a closed ExtrusionLine as a Polygon by dropping the duplicated terminal
    // junction and copying only XY coordinates.
    // [HAZARD] Assumes the line is closed and the final junction exactly equals the first point.
    Polygon out;
    assert(line.junctions.size() >= 3);
    assert(line.junctions.front().p == line.junctions.back().p);
    out.points.reserve(line.junctions.size() - 1);
    for (auto it = line.junctions.begin(); it != line.junctions.end() - 1; ++it)
        out.points.emplace_back(it->p);
    return out;
}

static Points to_points(const ExtrusionLine& extrusion_line)
{
    // [INTENT] Strip width metadata and expose only centerline coordinates for geometry utilities.
    Points points;
    points.reserve(extrusion_line.junctions.size());
    for (const ExtrusionJunction& junction : extrusion_line.junctions)
        points.emplace_back(junction.p);
    return points;
}

#if 0
static BoundingBox get_extents(const ExtrusionLine &extrusion_line)
{
    // [INTENT] Bounding box over all centerline junction points, ignoring local width inflation.
    BoundingBox bbox;
    for (const ExtrusionJunction &junction : extrusion_line.junctions)
        bbox.merge(junction.p);
    return bbox;
}

static BoundingBox get_extents(const std::vector<ExtrusionLine> &extrusion_lines)
{
    BoundingBox bbox;
    for (const ExtrusionLine &extrusion_line : extrusion_lines)
        bbox.merge(get_extents(extrusion_line));
    return bbox;
}

static BoundingBox get_extents(const std::vector<const ExtrusionLine *> &extrusion_lines)
{
    // [INTENT] Aggregate extents across a non-owning list of path pointers.
    // [HAZARD] Callers must ensure every pointer is non-null and remains alive for the duration.
    BoundingBox bbox;
    for (const ExtrusionLine *extrusion_line : extrusion_lines) {
        assert(extrusion_line != nullptr);
        bbox.merge(get_extents(*extrusion_line));
    }
    return bbox;
}

static std::vector<Points> to_points(const std::vector<const ExtrusionLine *> &extrusion_lines)
{
    // [INTENT] Batch helper for APIs that operate on many centerline polylines at once.
    std::vector<Points> points;
    for (const ExtrusionLine *extrusion_line : extrusion_lines) {
        assert(extrusion_line != nullptr);
        points.emplace_back(to_points(*extrusion_line));
    }
    return points;
}
#endif

// [INTENT] One inset worth of Arachne output lines. WallToolPaths returns `vector<VariableWidthLines>`
// so the outer container groups by inset index and this alias groups by individual line.
using VariableWidthLines = std::vector<ExtrusionLine>; //<! The ExtrusionLines generated by libArachne

} // namespace Slic3r::Arachne

namespace Slic3r {

void extrusion_paths_append(ExtrusionPaths& dst, const ClipperLib_Z::Paths& extrusion_paths, const ExtrusionRole role, const Flow& flow);
void extrusion_paths_append(ExtrusionPaths& dst, const Arachne::ExtrusionLine& extrusion, const ExtrusionRole role, const Flow& flow);

} // namespace Slic3r

#endif // UTILS_EXTRUSION_LINE_H
