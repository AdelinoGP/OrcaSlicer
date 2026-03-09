// [INTENT] ConcaveHull.hpp — "fake" concave hull for SLA pad generation.
// Rather than computing a true concave hull, this class connects disjoint
// polygon islands by adding thin bridge rectangles from each island's centroid
// to the centroid-of-centroids (a star topology). The result is a single merged
// polygon that can be offset to form the SLA print pad boundary.
//
// Design rationale: true concave hull algorithms are expensive and produce
// non-deterministic shapes. The star-bridge approximation is fast, deterministic,
// and "good enough" for pad geometry where exact concavity doesn't matter.
//
// [STATE] m_polys holds the merged polygon set after construction. Immutable after ctor.
// [MEMORY] Owns its Polygons vector. No external references after construction.
// [CONCURRENCY] Not thread-safe. Safe for concurrent read after construction.
//
// [COUPLING] Uses `ThrowOnCancel` (std::function<void()>) for cooperative
// cancellation inside `add_connector_rectangles`. Must be called with a
// valid throwable or the no-op lambda [] {}.
//
// [HAZARD] H912 — `add_connector_rectangles` early-returns (not continues) on
// `dist >= max_dist` for the FIRST island where this condition is true, silently
// skipping bridges for ALL subsequent islands in the loop. Islands processed
// after a far-apart pair receive no bridge connector.

#ifndef SLA_CONCAVEHULL_HPP
#define SLA_CONCAVEHULL_HPP

#include <libslic3r/ExPolygon.hpp>

namespace Slic3r { namespace sla {

// [INTENT] Extract outer contours from ExPolygons, discarding holes.
inline Polygons get_contours(const ExPolygons& poly)
{
    Polygons ret;
    ret.reserve(poly.size());
    for (const ExPolygon& p : poly)
        ret.emplace_back(p.contour);

    return ret;
}

using ThrowOnCancel = std::function<void()>;

/// A fake concave hull that is constructed by connecting separate shapes
/// with explicit bridges. Bridges are generated from each shape's centroid
/// to the center of the "scene" which is the centroid calculated from the shape
/// centroids (a star is created...)
// [COUPLING] Used by SLA pad generation (Pad.cpp) and SupportTree meshing.
class ConcaveHull
{
    Polygons m_polys;

    static Point centroid(const Points& pp);

    static inline Point centroid(const Polygon& poly) { return poly.centroid(); }

    Points calculate_centroids() const;

    void merge_polygons();

    void add_connector_rectangles(const Points& centroids, coord_t max_dist, ThrowOnCancel thr);

public:
    ConcaveHull(const ExPolygons& polys, double merge_dist, ThrowOnCancel thr) : ConcaveHull{to_polygons(polys), merge_dist, thr} {}

    ConcaveHull(const Polygons& polys, double mergedist, ThrowOnCancel thr);

    const Polygons& polygons() const { return m_polys; }

    ExPolygons to_expolygons() const;
};

// [INTENT] Expand the hull outward with a "waffle" closing to fill gaps and
// produce a smooth offset suitable for pad bottom geometry.
ExPolygons offset_waffle_style_ex(const ConcaveHull& ccvhull, coord_t delta);
Polygons   offset_waffle_style(const ConcaveHull& polys, coord_t delta);

}} // namespace Slic3r::sla
#endif // CONCAVEHULL_HPP
