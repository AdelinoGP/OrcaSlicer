#ifndef libslic3r_Triangulation_hpp_
#define libslic3r_Triangulation_hpp_

#include <vector>
#include <set>
#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>

// [INTENT] Polygon/ExPolygon triangulation utilities.
// Converts 2D filled shapes into triangle indices for mesh rendering.
// Static utility class - all methods are static, no instance required.
// [COUPLING] Depends on Polygon, ExPolygon, Points - core 2D geometry types.

namespace Slic3r {

class Triangulation
{
public:
    Triangulation() = delete; // [INTENT] Prevent instantiation - pure static class.

    // [INTENT] Half-edge representation for triangulation constraints.
    // Used to specify edges that must appear in the final triangulation.
    using HalfEdge  = std::pair<uint32_t, uint32_t>;
    using HalfEdges = std::vector<HalfEdge>;
    using Indices   = std::vector<Vec3i32>;

    /// <summary>
    /// Connect points by triangulation to create filled surface by triangles
    /// Input points have to be unique
    /// Inspiration for make unique points is Emboss::dilate_to_unique_points
    /// </summary>
    // [INTENT] Triangulate arbitrary point cloud with optional edge constraints.
    // Half-edges are sorted lexicographically (from < to) for consistent ordering.
    // Uses ear clipping or Delaunay-based algorithm internally.
    /// <param name="points">Points to connect</param>
    /// <param name="edges">Constraint for edges, pair is from point(first) to
    /// point(second), sorted lexicographically</param>
    /// <returns>Triangles</returns>
    static Indices triangulate(const Points& points, const HalfEdges& half_edges);
    static Indices triangulate(const Polygon& polygon);
    static Indices triangulate(const Polygons& polygons);
    static Indices triangulate(const ExPolygon& expolygon);
    static Indices triangulate(const ExPolygons& expolygons);

    // Map for convert original index to set without duplication
    //              from_index<to_index>
    // [INTENT] Tracks index remapping when duplicate points are merged.
    using Changes = std::vector<uint32_t>;

    /// <summary>
    /// Create conversion map from original index into new
    /// with respect of duplicit point
    /// </summary>
    /// <param name="points">input set of points</param>
    /// <param name="duplicits">duplicit points collected from points</param>
    /// <returns>Conversion map for point index</returns>
    static Changes create_changes(const Points& points, const Points& duplicits);

    /// <summary>
    /// Triangulation for expolygons, speed up when points are already collected
    /// NOTE: Not working properly for ExPolygons with multiple point on same coordinate
    /// You should check it by "collect_changes"
    /// </summary>
    // [INTENT] Optimized triangulation when points are pre-processed.
    // [HAZARD] Does not handle coincident points correctly - caller must deduplicate.
    /// <param name="expolygons">Input shape to triangulation - define edges</param>
    /// <param name="points">Points from expolygons</param>
    /// <returns>Triangle indices</returns>
    static Indices triangulate(const ExPolygons& expolygons, const Points& points);

    /// <summary>
    /// Triangulation for expolygons containing multiple points with same coordinate
    /// </summary>
    // [INTENT] Full triangulation with explicit index remapping for coincident points.
    /// <param name="expolygons">Input shape to triangulation - define edge</param>
    /// <param name="points">Points from expolygons</param>
    /// <param name="changes">Changes swap for indicies into points</param>
    /// <returns>Triangle indices</returns>
    static Indices triangulate(const ExPolygons& expolygons, const Points& points, const Changes& changes);
};

} // namespace Slic3r
#endif // libslic3r_Triangulation_hpp_