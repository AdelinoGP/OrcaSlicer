// Copyright (c) 2020 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] PolygonsSegmentIndex is the segment-flavored companion to PolygonsPointIndex. It treats a
// vertex handle as "the edge that starts here and ends at the next wrapped vertex", which is exactly
// the abstraction boost::polygon's Voronoi builder expects from the Arachne input contour set.
// [STATE] Inherits all state from PolygonsPointIndex; no extra fields are stored.
// [COUPLING] This type is the bridge between libslic3r `Polygons` and `boost::polygon` segment traits.
// SkeletalTrapezoidation feeds collections of these handles into Voronoi construction instead of first
// allocating a standalone segment array.
// [HAZARD] `to()` relies on wrapped `next()` traversal, so it assumes every polygon path is closed and
// has at least one point. Degenerate or mutable-open paths would produce bogus segments or divide-by-zero
// behavior elsewhere in the Voronoi pipeline.

#ifndef UTILS_POLYGONS_SEGMENT_INDEX_H
#define UTILS_POLYGONS_SEGMENT_INDEX_H

#include <vector>

#include "PolygonsPointIndex.hpp"

namespace Slic3r::Arachne {

/*!
 * A class for iterating over the points in one of the polygons in a \ref Polygons object
 */
class PolygonsSegmentIndex : public PolygonsPointIndex
{
public:
    PolygonsSegmentIndex() : PolygonsPointIndex() {};
    PolygonsSegmentIndex(const Polygons* polygons, unsigned int poly_idx, unsigned int point_idx)
        : PolygonsPointIndex(polygons, poly_idx, point_idx) {};

    // [INTENT] Segment start is the current wrapped polygon vertex.
    Point from() const { return PolygonsPointIndex::p(); }

    // [INTENT] Segment end is the successor vertex on the same closed contour.
    Point to() const { return PolygonsSegmentIndex::next().p(); }
};

} // namespace Slic3r::Arachne

namespace boost::polygon {

template<> struct geometry_concept<Slic3r::Arachne::PolygonsSegmentIndex>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::Arachne::PolygonsSegmentIndex>
{
    typedef coord_t       coordinate_type;
    typedef Slic3r::Point point_type;

    static inline point_type get(const Slic3r::Arachne::PolygonsSegmentIndex& CSegment, direction_1d dir)
    {
        // [COUPLING] boost::polygon queries segment endpoints through this trait during Voronoi input
        // extraction. Any port replacing boost needs an equivalent adapter layer between contour storage
        // and the chosen medial-axis / Voronoi library.
        return dir.to_int() ? CSegment.to() : CSegment.from();
    }
};

} // namespace boost::polygon

#endif // UTILS_POLYGONS_SEGMENT_INDEX_H
