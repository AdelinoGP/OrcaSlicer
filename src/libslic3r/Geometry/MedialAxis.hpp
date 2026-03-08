#ifndef slic3r_Geometry_MedialAxis_hpp_
#define slic3r_Geometry_MedialAxis_hpp_

// [INTENT] MedialAxis: extracts the Medial Axis Transform (MAT) skeleton of an ExPolygon using
// a Voronoi diagram. The skeleton is then pruned to retain only edges within [min_width, max_width]
// (which represent material width as seen from the two boundary walls). Output is a set of
// ThickPolylines annotated with per-segment width values.
// [COUPLING] Used by PerimeterGenerator for thin-wall extrusion generation (single-line thin regions).
//   Depends on VoronoiDiagram (Voronoi.hpp) for construction and validity repair.
// [STATE] m_edge_data: flat vector sized edges.size()/2 — twins share one EdgeData entry.
//   edge_data() maps an edge to its EdgeData via index division; the bool flag indicates reversal.
// [CONCURRENCY] Not thread-safe — build() mutates m_vd and m_edge_data in place.
//   Callers must ensure single-threaded access per MedialAxis instance.

#include <stddef.h>
#include <utility>
#include <vector>
#include <cstddef>

#include "Voronoi.hpp"
#include "../ExPolygon.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r::Geometry {

class MedialAxis
{
public:
    // [INTENT] Construct from an ExPolygon with width filtering bounds (in scaled mm).
    MedialAxis(double min_width, double max_width, const ExPolygon& expolygon);
    // [INTENT] Build the medial axis skeleton and output as ThickPolylines (with width per segment).
    void build(ThickPolylines* polylines);
    // [INTENT] Simplified build: output as plain Polylines (width info discarded).
    void build(Polylines* polylines);

private:
    // Input
    const ExPolygon& m_expolygon;
    // [STATE] m_lines: boundary segments derived from expolygon.lines(). May be re-derived after
    //   morphological closing if the initial Voronoi diagram is invalid.
    Lines m_lines;
    // for filtering of the skeleton edges
    double m_min_width;
    double m_max_width;

    // Voronoi Diagram.
    using VD = VoronoiDiagram;
    // [STATE] m_vd: Voronoi diagram computed from m_lines. May be repaired in place.
    VD m_vd;

    // Annotations of the VD skeleton edges.
    struct EdgeData
    {
        bool   active{false}; // [STATE] true = edge has not yet been consumed by polyline building
        double width_start{0};
        double width_end{0};
    };
    // [INTENT] Returns a reference to EdgeData and a "reversed" boolean.
    // Twin edges (edge_id and edge_id^1) share one EdgeData entry at index edge_id/2.
    // The bool is true when the edge is the second of the pair (i.e. the twin), indicating
    // that width_start and width_end must be swapped when reading.
    // [COUPLING] edge_data() index arithmetic depends on VD twin edge layout: twin = edge XOR 1.
    //   Any VD implementation that breaks this guarantee will silently corrupt width annotations.
    std::pair<EdgeData&, bool> edge_data(const VD::edge_type& edge)
    {
        size_t edge_id = &edge - &m_vd.edges().front();
        return {m_edge_data[edge_id / 2], (edge_id & 1) != 0};
    }
    std::vector<EdgeData> m_edge_data;

    // [INTENT] Extend polyline by following active neighbors from edge's endpoint.
    void process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline);
    // [INTENT] Validate and populate EdgeData for an edge. Returns true if the edge should be kept.
    //   Checks: (1) width within [min_width, max_width], (2) narrow-angle orientation for two-segment cells.
    bool validate_edge(const VD::edge_type* edge);
};

} // namespace Slic3r::Geometry

#endif // slic3r_Geometry_MedialAxis_hpp_
