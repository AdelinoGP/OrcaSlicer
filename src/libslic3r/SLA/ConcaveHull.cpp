// [INTENT] ConcaveHull.cpp — Implementation of the SLA "fake concave hull."
// The algorithm:
//   1. Merge all input polygons with union (Clipper).
//   2. If only one island remains, done — no bridges needed.
//   3. Compute per-island centroids, then compute the centroid-of-centroids (cc).
//   4. For each island: find its nearest neighbor centroid, compute a thin
//      triangle bridge pointing from cc through the island centroid toward the
//      island, offset it by 1mm, and add to the polygon list.
//   5. Final union merges bridges with islands.
//
// [STATE] All state is in the `ConcaveHull` object's m_polys vector.
//         Construction is the only mutation; all subsequent access is read-only.
//
// [MEMORY] Polygon vectors are heap-allocated via std::vector.
//          `merge_polygons()` calls `union_ex(m_polys)` → Clipper → new allocation.
//          No raw pointers; all ownership via std::vector value semantics.
//
// [CONCURRENCY] Not thread-safe. Construction from multiple threads requires external sync.
//
// [HAZARD] H912 — `add_connector_rectangles` loop uses `return` (not `continue`) on the
//   `dist >= max_dist` check. If the FIRST island has no nearby neighbor, ALL subsequent
//   islands in the loop receive no bridge connector. This is almost certainly a bug —
//   the intent was to skip that one island, not all remaining islands.
//
// [COUPLING] `offset_waffle_style` uses `closing()` from ClipperUtils with ClipperLib::jtRound.
//   The arc_tolerance is hardcoded to scaled(0.01) = 0.01mm. If the calling context uses
//   a different arc resolution convention, this produces inconsistent results.

#include <libslic3r/SLA/ConcaveHull.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>

#include <libslic3r/MTUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace sla {

// [INTENT] Helpers to bridge between 2D coord_t points and 3D Vec3d for spatial index.
inline Vec3d   to_vec3(const Vec2crd& v2) { return {double(v2(X)), double(v2(Y)), 0.}; }
inline Vec3d   to_vec3(const Vec2d& v2) { return {v2(X), v2(Y), 0.}; }
inline Vec2crd to_vec2(const Vec3d& v3) { return {coord_t(v3(X)), coord_t(v3(Y))}; }

// [INTENT] Compute the bounding-box center of a point set.
// For 0-1 points: special cases. For 2+ points: AABB centroid (not true centroid).
// This is the "centroid" of each island polygon — used to route bridges.
Point ConcaveHull::centroid(const Points& pp)
{
    Point c;
    switch (pp.size()) {
    case 0: break;
    case 1: c = pp.front(); break;
    case 2: c = (pp[0] + pp[1]) / 2; break;
    default: {
        auto  MAX = std::numeric_limits<Point::coord_type>::max();
        auto  MIN = std::numeric_limits<Point::coord_type>::min();
        Point min = {MAX, MAX}, max = {MIN, MIN};

        for (auto& p : pp) {
            if (p(0) < min(0))
                min(0) = p(0);
            if (p(1) < min(1))
                min(1) = p(1);
            if (p(0) > max(0))
                max(0) = p(0);
            if (p(1) > max(1))
                max(1) = p(1);
        }
        c(0) = min(0) + (max(0) - min(0)) / 2;
        c(1) = min(1) + (max(1) - min(1)) / 2;
        break;
    }
    }

    return c;
}

// [INTENT] Compute per-island centroids using Polygon::centroid() (true area centroid).
Points ConcaveHull::calculate_centroids() const
{
    // We get the centroids of all the islands in the 2D slice
    Points centroids;
    centroids.reserve(m_polys.size());
    std::transform(m_polys.begin(), m_polys.end(), std::back_inserter(centroids), [](const Polygon& poly) { return centroid(poly); });

    return centroids;
}

// [INTENT] Replace m_polys with the outer contours of their union (removes holes/overlaps).
void ConcaveHull::merge_polygons() { m_polys = get_contours(union_ex(m_polys)); }

// [INTENT] Add thin triangle "bridge" rectangles from each island centroid toward the
// centroid-of-centroids (cc). The bridge is a 3-point triangle then offset by 1mm.
// Uses a PointIndex R*-tree to find each island's nearest neighbor centroid for distance check.
//
// [HAZARD] H912 — early `return` on dist >= max_dist exits the whole loop, not just this island.
void ConcaveHull::add_connector_rectangles(const Points& centroids, coord_t max_dist, ThrowOnCancel thr)
{
    // Centroid of the centroids of islands. This is where the additional
    // connector sticks are routed.
    Point cc = centroid(centroids);

    PointIndex ctrindex;
    unsigned   idx = 0;
    for (const Point& ct : centroids)
        ctrindex.insert(to_vec3(ct), idx++);

    m_polys.reserve(m_polys.size() + centroids.size());

    idx = 0;
    for (const Point& c : centroids) {
        thr();

        double dx = c.x() - cc.x(), dy = c.y() - cc.y();
        double l  = std::sqrt(dx * dx + dy * dy);
        double nx = dx / l, ny = dy / l;

        const Point& ct = centroids[idx];

        std::vector<PointIndexEl> result = ctrindex.nearest(to_vec3(ct), 2);

        double dist = max_dist;
        for (const PointIndexEl& el : result)
            if (el.second != idx) {
                dist = Line(to_vec2(el.first), ct).length();
                break;
            }

        idx++;

        // [HAZARD] H912 — `return` here exits the entire loop. If this island is far
        // from all neighbors, ALL subsequent islands also get no bridge connector.
        // The intended behavior is almost certainly `continue` (skip this island only).
        if (dist >= max_dist)
            return;

        Polygon r;
        r.points.reserve(3);
        r.points.emplace_back(cc);

        Point n(scaled(nx), scaled(ny));
        r.points.emplace_back(c + Point(n.y(), -n.x()));
        r.points.emplace_back(c + Point(-n.y(), n.x()));
        offset(r, scaled<float>(1.));

        m_polys.emplace_back(r);
    }
}

// [INTENT] Main constructor: merge inputs, early-exit if already a single island,
// compute centroids, add star bridges, merge again.
ConcaveHull::ConcaveHull(const Polygons& polys, double mergedist, ThrowOnCancel thr)
{
    if (polys.empty())
        return;

    m_polys = polys;
    merge_polygons();

    if (m_polys.size() == 1)
        return; // [STATE] No bridging needed for single island

    Points centroids = calculate_centroids();

    add_connector_rectangles(centroids, scaled(mergedist), thr);

    merge_polygons();
}

ExPolygons ConcaveHull::to_expolygons() const
{
    auto ret = reserve_vector<ExPolygon>(m_polys.size());
    for (const Polygon& p : m_polys)
        ret.emplace_back(ExPolygon(p));
    return ret;
}

// [INTENT] Expand hull outward using ClipperUtils closing (offset outward then inward by delta).
// arc_tolerance hardcoded to 0.01mm — controls rounded-corner arc approximation precision.
ExPolygons offset_waffle_style_ex(const ConcaveHull& hull, coord_t delta) { return to_expolygons(offset_waffle_style(hull, delta)); }

Polygons offset_waffle_style(const ConcaveHull& hull, coord_t delta)
{
    auto     arc_tolerance = scaled<double>(0.01);
    Polygons res           = closing(hull.polygons(), 2 * delta, delta, ClipperLib::jtRound, arc_tolerance);

    // [INTENT] Remove inner contours (clockwise = hole in Clipper convention).
    auto it = std::remove_if(res.begin(), res.end(), [](Polygon& p) { return p.is_clockwise(); });
    res.erase(it, res.end());

    return res;
}

}} // namespace Slic3r::sla
