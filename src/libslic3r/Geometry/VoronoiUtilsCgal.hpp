#ifndef slic3r_VoronoiUtilsCgal_hpp_
#define slic3r_VoronoiUtilsCgal_hpp_

// [INTENT] CGAL-based planarity validators for boost::polygon Voronoi diagrams.
//   Provides two independent planarity checks that can detect corrupt/degenerate VD output:
//
//   1. is_voronoi_diagram_planar_intersection() — CGAL sweep-line algorithm that finds any
//      pair of linear VD edges that geometrically intersect. O(n log n + k) where k = crossings.
//      [HAZARD] H594: parabolic (curved) edges are NOT included in this check (FIXME comment
//      in VoronoiUtilsCgal.cpp). The check is incomplete for diagrams with mixed curved edges.
//
//   2. is_voronoi_diagram_planar_angle<SegmentIterator>() — per-vertex CCW ordering check.
//      For each VD vertex, verifies that all incident edges are ordered counter-clockwise using
//      CGAL filtered predicates (Simple_cartesian<double> → Interval_nt_advanced → MP_Float).
//      Handles linear-linear, curved-curved, and linear-curved edge pairs.
//
// [COUPLING] Depends on CGAL: Simple_cartesian, Interval_nt_advanced, MP_Float kernels.
//   Including this header pulls in CGAL headers; compile time is significant.
// [CONCURRENCY] Both methods are stateless (read-only on VD); safe to call concurrently
//   on different VD instances.

#include <boost/polygon/polygon.hpp>
#include <iterator>

#include "Voronoi.hpp"
#include "../Arachne/utils/PolygonsSegmentIndex.hpp"

namespace Slic3r::Geometry {
class VoronoiDiagram;

class VoronoiUtilsCgal
{
public:
    // [INTENT] Detect any pair of linear VD edges that intersect using CGAL sweep-line.
    //   Returns true if the diagram is planar (no intersections found).
    // [HAZARD] H594: curved/parabolic VD edges are excluded from the check (FIXME in .cpp).
    // Check if the Voronoi diagram is planar using CGAL sweeping edge algorithm for enumerating all intersections between lines.
    static bool is_voronoi_diagram_planar_intersection(const VoronoiDiagram& voronoi_diagram);

    // [INTENT] Verify that all edges incident on each VD vertex are ordered CCW.
    //   Uses CGAL filtered-kernel orientation predicates for exact arithmetic.
    //   Returns true iff the diagram is planar (all vertices have CCW-ordered edges).
    // [COUPLING] SegmentIterator must provide source geometry for parabolic tangent computation.
    // Check if the Voronoi diagram is planar using verification that all neighboring edges are ordered CCW for each vertex.
    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        bool>::type
    is_voronoi_diagram_planar_angle(const VoronoiDiagram& voronoi_diagram, SegmentIterator segment_begin, SegmentIterator segment_end);
};
} // namespace Slic3r::Geometry

#endif // slic3r_VoronoiUtilsCgal_hpp_
