// Copyright (c) 2018 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

// [INTENT] PathsPointIndex is Arachne's lightweight handle for walking polygon vertices without
// copying the polygons themselves. Spatial indices and stitching code pass these tiny handles around
// so they can recover either a point or an adjacent segment lazily from the original container.
// [STATE] The handle is just three fields: a non-owning pointer to the source path container plus
// polygon and point indices. Increment/decrement mutate only the indices and wrap inside one path.
// [MEMORY] No ownership is transferred anywhere in this file. Every index becomes dangling as soon as
// the referenced `Paths` container is reallocated, destroyed, or reordered.
// [COUPLING] Shared by `WallToolPaths`, `PolylineStitcher`, and sparse-grid helpers. The generic
// template works for `Polygons` and Arachne's `VariableWidthLines` because both expose `operator[]`
// and nested point-like entries.
// [HAZARD] Comparison and hashing collapse indices down to coordinates (`p()`). Distinct vertices at
// the same XY location compare equal under ordering/hash use-cases, which is fine for proximity grids
// but unsafe if a port assumes these handles encode unique topological identity.

#ifndef UTILS_POLYGONS_POINT_INDEX_H
#define UTILS_POLYGONS_POINT_INDEX_H

#include <vector>

#include "../../Point.hpp"
#include "../../Polygon.hpp"

namespace Slic3r::Arachne {

// Identity function, used to be able to make templated algorithms where the input is sometimes points, sometimes things that contain or can
// be converted to points. [INTENT] Adapter hook for the generic index template: plain polygons store Points directly, while other path
// types can overload `make_point()` to expose their embedded coordinate field.
inline const Point& make_point(const Point& p) { return p; }

/*!
 * A class for iterating over the points in one of the polygons in a \ref Polygons object
 */
template<typename Paths> class PathsPointIndex
{
public:
    /*!
     * The polygons into which this index is indexing.
     */
    // [STATE] Non-owning source container for this iterator-like handle.
    const Paths* polygons; // (pointer to const polygons)

    // [STATE] Chooses which sub-path inside `polygons` this handle addresses.
    unsigned int poly_idx; //!< The index of the polygon in \ref PolygonsPointIndex::polygons

    // [STATE] Vertex offset within the selected path. ++/-- wrap modulo the path length.
    unsigned int point_idx; //!< The index of the point in the polygon in \ref PolygonsPointIndex::polygons

    /*!
     * Constructs an empty point index to no polygon.
     *
     * This is used as a placeholder for when there is a zero-construction
     * needed. Since the `polygons` field is const you can't ever make this
     * initialisation useful.
     */
    // [HAZARD] Default construction yields an inert sentinel, not a valid vertex reference.
    PathsPointIndex() : polygons(nullptr), poly_idx(0), point_idx(0) {}

    /*!
     * Constructs a new point index to a vertex of a polygon.
     * \param polygons The Polygons instance to which this index points.
     * \param poly_idx The index of the sub-polygon to point to.
     * \param point_idx The index of the vertex in the sub-polygon.
     */
    PathsPointIndex(const Paths* polygons, unsigned int poly_idx, unsigned int point_idx)
        : polygons(polygons), poly_idx(poly_idx), point_idx(point_idx)
    {}

    /*!
     * Copy constructor to copy these indices.
     */
    PathsPointIndex(const PathsPointIndex& original) = default;

    Point p() const
    {
        // [INTENT] Recover the current vertex on demand. Returning by value keeps the interface uniform
        // across point-like payload types and avoids exposing references into heterogenous path objects.
        if (!polygons)
            return {0, 0};

        return make_point((*polygons)[poly_idx][point_idx]);
    }

    /*!
     * \brief Returns whether this point is initialised.
     */
    // [STATE] Callers use this as the only validity test before dereferencing the handle.
    bool initialized() const { return polygons; }

    /*!
     * Get the polygon to which this PolygonsPointIndex refers
     */
    // [COUPLING] Only valid when `Paths` is actually `Polygons`; this helper bakes in the common
    // straight-skeleton use-case even though the surrounding template is otherwise generic.
    const Polygon& getPolygon() const { return (*polygons)[poly_idx]; }

    /*!
     * Test whether two iterators refer to the same polygon in the same polygon list.
     *
     * \param other The PolygonsPointIndex to test for equality
     * \return Wether the right argument refers to the same polygon in the same ListPolygon as the left argument.
     */
    bool operator==(const PathsPointIndex& other) const
    {
        return polygons == other.polygons && poly_idx == other.poly_idx && point_idx == other.point_idx;
    }
    bool operator!=(const PathsPointIndex& other) const { return !(*this == other); }
    bool operator<(const PathsPointIndex& other) const
    {
        // [HAZARD] Orders by coordinate only. Two different polygons sharing one coordinate become
        // equivalent under strict-weak-order checks, which is acceptable for nearest-point buckets but
        // loses topological uniqueness.
        return this->p() < other.p();
    }
    PathsPointIndex& operator=(const PathsPointIndex& other)
    {
        // [STATE] Assignment copies the non-owning reference and both indices verbatim.
        polygons  = other.polygons;
        poly_idx  = other.poly_idx;
        point_idx = other.point_idx;
        return *this;
    }
    //! move the iterator forward (and wrap around at the end)
    PathsPointIndex& operator++()
    {
        // [INTENT] Polygon traversal in Arachne is circular: the successor of the last vertex is the
        // first vertex again because segments wrap around closed contours.
        point_idx = (point_idx + 1) % (*polygons)[poly_idx].size();
        return *this;
    }
    //! move the iterator backward (and wrap around at the beginning)
    PathsPointIndex& operator--()
    {
        // [INTENT] Mirror image of operator++ for predecessor traversal around the same closed path.
        if (point_idx == 0)
            point_idx = (*polygons)[poly_idx].size();
        point_idx--;
        return *this;
    }
    //! move the iterator forward (and wrap around at the end)
    PathsPointIndex next() const
    {
        PathsPointIndex ret(*this);
        ++ret;
        return ret;
    }
    //! move the iterator backward (and wrap around at the beginning)
    PathsPointIndex prev() const
    {
        PathsPointIndex ret(*this);
        --ret;
        return ret;
    }
};

using PolygonsPointIndex = PathsPointIndex<Polygons>;

/*!
 * Locator to extract a line segment out of a \ref PolygonsPointIndex
 */
struct PolygonsPointIndexSegmentLocator
{
    std::pair<Point, Point> operator()(const PolygonsPointIndex& val) const
    {
        // [INTENT] Convert a vertex handle into the outgoing polygon edge that starts at that vertex.
        // SparseLineGrid uses this to index polygon segments without materializing a second segment list.
        const Polygon& poly           = (*val.polygons)[val.poly_idx];
        Point          start          = poly[val.point_idx];
        unsigned int   next_point_idx = (val.point_idx + 1) % poly.size();
        Point          end            = poly[next_point_idx];
        return std::pair<Point, Point>(start, end);
    }
};

/*!
 * Locator of a \ref PolygonsPointIndex
 */
template<typename Paths> struct PathsPointIndexLocator
{
    Point operator()(const PathsPointIndex<Paths>& val) const
    {
        // [INTENT] Generic location accessor for sparse point grids and nearest-neighbor searches.
        return make_point(val.p());
    }
};

} // namespace Slic3r::Arachne

namespace std {
/*!
 * Hash function for \ref PolygonsPointIndex
 */
template<> struct hash<Slic3r::Arachne::PolygonsPointIndex>
{
    size_t operator()(const Slic3r::Arachne::PolygonsPointIndex& lpi) const
    {
        // [HAZARD] Hashing by point only mirrors operator< semantics: coincident vertices from distinct
        // polygons intentionally collide because the surrounding spatial indices care about geometry,
        // not unique vertex identity.
        return Slic3r::PointHash{}(lpi.p());
    }
};
} // namespace std

#endif // UTILS_POLYGONS_POINT_INDEX_H
