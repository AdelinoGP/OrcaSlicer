// Polygon offsetting using Voronoi diagram prodiced by boost::polygon.

// [INTENT] Public API for Voronoi-based polygon offsetting.
// Given a set of input Lines (edges of closed polygons), this module:
//   1. Annotates every VD vertex/edge/cell as Inside/Outside/OnContour/Boundary
//      using the signed half-plane of adjacent segment sites.
//   2. Computes signed distances from each VD vertex to the nearest input site.
//   3. Finds where an offset iso-curve (at distance |offset_distance|) crosses
//      each VD edge (linear lerp on segment–segment bisectors, closed-form
//      point–point / point–segment intersections elsewhere).
//   4. Traces those intersection points around VD cells into closed Polygons,
//      discretizing circular arcs that arise around point sites.
// [COUPLING] Tightly coupled to VoronoiDiagram (Voronoi.hpp): all accessors
//   read/write the boost::polygon color() fields on VD elements.
// [MEMORY] No persistent state; all functions are stateless or take const &vd.
//   Exception: annotate_inside_outside() and offset(VD&,...) mutate the VD
//   color fields in-place.

#ifndef slic3r_VoronoiOffset_hpp_
#define slic3r_VoronoiOffset_hpp_

#include <boost/polygon/polygon.hpp>
#include <cmath>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "Voronoi.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r { namespace Voronoi {

// [INTENT] Alias for the Slic3r VoronoiDiagram type.
using VD = Slic3r::Geometry::VoronoiDiagram;

// [INTENT] Return the input contour point associated with a VD point cell
//   (which was created from one endpoint of an input segment).
//   source_category() == SEGMENT_START_POINT -> line.a, else line.b.
// [COUPLING] Boost VD source_category API.
inline const Point& contour_point(const VD::cell_type& cell, const Line& line)
{
    return ((cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line.a : line.b);
}
inline Point& contour_point(const VD::cell_type& cell, Line& line)
{
    return ((cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line.a : line.b);
}

inline const Point& contour_point(const VD::cell_type& cell, const Lines& lines) { return contour_point(cell, lines[cell.source_index()]); }
inline Point&       contour_point(const VD::cell_type& cell, Lines& lines) { return contour_point(cell, lines[cell.source_index()]); }

// [INTENT] Convert a VD vertex (double) to Eigen Vec2d for arithmetic.
inline Vec2d vertex_point(const VD::vertex_type& v) { return Vec2d(v.x(), v.y()); }
inline Vec2d vertex_point(const VD::vertex_type* v) { return Vec2d(v->x(), v->y()); }

// [INTENT] Topological classification of a VD vertex relative to the input contour.
//   Stored in the boost::polygon VD vertex color() field (unsigned char).
// [HAZARD] color() field is shared with any other user of the VD — callers must
//   reset_inside_outside_annotations() before reuse, or colors will be stale.
// "Color" stored inside the boost::polygon Voronoi vertex.
enum class VertexCategory : unsigned char {
    // Voronoi vertex is on the input contour.
    // VD::vertex_type stores coordinates in double, though the coordinates shall match exactly
    // with the coordinates of the input contour when converted to int32_t.
    OnContour,
    // Vertex is inside the CCW input contour, holes are respected.
    Inside,
    // Vertex is outside the CCW input contour, holes are respected.
    Outside,
    // Not known yet.
    Unknown,
};

// [INTENT] Topological classification of a VD half-edge (direction to its target vertex).
//   Stored in the boost::polygon VD edge color() field.
// [CONCURRENCY] Not thread-safe: annotation modifies color() fields globally on the VD.
// "Color" stored inside the boost::polygon Voronoi edge.
// The Voronoi edge as represented by boost::polygon Voronoi module is really a half-edge,
// the half-edges are classified based on the target vertex (VD::vertex_type::vertex1())
enum class EdgeCategory : unsigned char {
    // This half-edge points onto the contour, this VD::edge_type::vertex1().color() is OnContour.
    PointsToContour,
    // This half-edge points inside, this VD::edge_type::vertex1().color() is Inside.
    PointsInside,
    // This half-edge points outside, this VD::edge_type::vertex1().color() is Outside.
    PointsOutside,
    // Not known yet.
    Unknown
};

// [INTENT] Topological classification of a VD cell (Voronoi region).
//   Boundary = the cell straddles the contour (segment cell with vertices on both sides).
// "Color" stored inside the boost::polygon Voronoi cell.
enum class CellCategory : unsigned char {
    // This Voronoi cell is split by an input segment to two halves, one is inside, the other is outside.
    Boundary,
    // This Voronoi cell is completely inside.
    Inside,
    // This Voronoi cell is completely outside.
    Outside,
    // Not known yet.
    Unknown
};

// [INTENT] Typed accessors/setters for the color() fields — avoid raw casts at call sites.
// [COUPLING] Directly reads/writes boost::polygon color_type; must stay in sync with enum layout.
inline VertexCategory vertex_category(const VD::vertex_type& v) { return static_cast<VertexCategory>(v.color()); }
inline VertexCategory vertex_category(const VD::vertex_type* v) { return static_cast<VertexCategory>(v->color()); }
inline void           set_vertex_category(VD::vertex_type& v, VertexCategory c) { v.color(static_cast<VD::vertex_type::color_type>(c)); }
inline void           set_vertex_category(VD::vertex_type* v, VertexCategory c) { v->color(static_cast<VD::vertex_type::color_type>(c)); }

inline EdgeCategory edge_category(const VD::edge_type& e) { return static_cast<EdgeCategory>(e.color()); }
inline EdgeCategory edge_category(const VD::edge_type* e) { return static_cast<EdgeCategory>(e->color()); }
inline void         set_edge_category(VD::edge_type& e, EdgeCategory c) { e.color(static_cast<VD::edge_type::color_type>(c)); }
inline void         set_edge_category(VD::edge_type* e, EdgeCategory c) { e->color(static_cast<VD::edge_type::color_type>(c)); }

inline CellCategory cell_category(const VD::cell_type& v) { return static_cast<CellCategory>(v.color()); }
inline CellCategory cell_category(const VD::cell_type* v) { return static_cast<CellCategory>(v->color()); }
inline void         set_cell_category(const VD::cell_type& v, CellCategory c) { v.color(static_cast<VD::cell_type::color_type>(c)); }
inline void         set_cell_category(const VD::cell_type* v, CellCategory c) { v->color(static_cast<VD::cell_type::color_type>(c)); }

// [INTENT] Reset all VD color fields to Unknown. Must be called before re-annotating
//   a previously used VD, or before handing the VD off to VoronoiUtilsCgal planarity
//   checks (which also use the color field as a "visited" flag).
// Mark the "Color" of VD vertices, edges and cells as Unknown.
void reset_inside_outside_annotations(VD& vd);

// [INTENT] Annotate every VD vertex/edge/cell as Inside/Outside/OnContour/Boundary.
//   Algorithm:
//     1. Mark vertices exactly on a site (secondary edges, on_site() check).
//     2. For finite edges with a segment site, classify v1 by half-plane of the segment.
//     3. Propagate through point–point edges (seed fill from boundary cells).
//   Prerequisite: vd must have been built from lines (segment input only).
// [STATE] Mutates vd.color() fields for all vertices, edges, and cells.
// [CONCURRENCY] Not thread-safe.
// Assign "Color" to VD vertices, edges and cells signifying whether the entity is inside or outside
// the input polygons defined by Lines.
void annotate_inside_outside(VD& vd, const Lines& lines);

// [INTENT] Compute signed Euclidean distance from each VD vertex to the nearest input site.
//   Negative = inside, positive = outside, zero = OnContour.
//   Distance is computed geometrically by finding the closest point site or
//   projecting onto the segment site (via rot_next() walk to locate a point cell).
// [STATE] Read-only; requires VD to already be annotated (annotate_inside_outside called).
// [MEMORY] Allocates one double per VD vertex.
// Returns a signed distance to Voronoi vertices from the input polygons.
// (negative distances inside, positive distances outside).
std::vector<double> signed_vertex_distances(const VD& vd, const Lines& lines);

// [INTENT] Sentinel helpers: intersection points are stored as Vec2d(nan, 0) when absent,
//   Vec2d(nan, anything != 0) when "visited but no intersection", Vec2d(x, y) when present.
static inline bool edge_offset_no_intersection(const Vec2d& intersection_point) { return std::isnan(intersection_point.x()); }
static inline bool edge_offset_has_intersection(const Vec2d& intersection_point)
{
    return !edge_offset_no_intersection(intersection_point);
}

// [INTENT] For each VD half-edge, compute the point where the iso-curve at
//   |offset_distance| crosses the edge (if at all).
//   Returns one Vec2d per edge; nan.x means no intersection on that edge.
// [HAZARD] For Point-Point and Point-Segment edges the distance along the edge
//   is non-monotone; the code tries to find 0, 1, or 2 intersections using
//   closed-form bisector geometry (detail::point_point_equal_distance_points,
//   detail::line_point_equal_distance_points). Numerical precision is limited
//   to SCALED_EPSILON; callers must tolerate small errors in intersection coords.
// [COUPLING] Requires distances from signed_vertex_distances() and annotated VD.
std::vector<Vec2d> edge_offset_contour_intersections(const VD&                  vd,
                                                     const Lines&               lines,
                                                     const std::vector<double>& distances,
                                                     double                     offset_distance);

// [INTENT] For each VD half-edge that is an internal skeleton edge (PointsInside,
//   finite, non-secondary), compute the start point of the skeleton segment at
//   that half-edge, applying a dr/dl threshold to filter shallow skeleton branches.
//   Returns nan for edges not part of the skeleton above the threshold.
// [STATE] Requires annotated VD. Read-only.
std::vector<Vec2d> skeleton_edges_rough(const VD& vd, const Lines& lines, const double threshold_alpha);

// [INTENT] Primary offset API (pre-computed distances variant).
//   Traces all offset iso-curves from intersection points, discretizing
//   circular arcs at point sites to achieve <= discretization_error chord error.
// [HAZARD] If edge_offset_contour_intersections() misses an intersection (numerical
//   failure), next_offset_edge() returns nullptr and an open loop is discarded
//   silently (with a VORONOI_DEBUG_OUT dump in debug builds).
Polygons offset(const Geometry::VoronoiDiagram& vd,
                const Lines&                    lines,
                const std::vector<double>&      signed_vertex_distances,
                double                          offset_distance,
                double                          discretization_error);

// [INTENT] Convenience offset overload: calls annotate_inside_outside +
//   signed_vertex_distances internally then delegates to the primary overload.
// [STATE] Mutates vd color fields (calls annotate_inside_outside on const_cast'd vd).
// [HAZARD] The const_cast is safe only if no other thread is reading vd concurrently.
// Offset a polygon or a set of polygons possibly with holes by traversing a Voronoi diagram.
// The input polygons are stored in lines and lines are referenced by vd.
// Outer curve will be extracted for a positive offset_distance,
// inner curve will be extracted for a negative offset_distance.
// Circular arches will be discretized to achieve discretization_error.
Polygons offset(const VD& vd, const Lines& lines, double offset_distance, double discretization_error);

}} // namespace Slic3r::Voronoi

#endif // slic3r_VoronoiOffset_hpp_
