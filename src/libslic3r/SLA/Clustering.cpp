// [INTENT] Clustering.cpp — Density-connected component clustering implementation.
// Uses a mutable Boost R*-tree (Index3D) as both the spatial lookup structure and
// the "unvisited points" set. Points are removed from the tree as they are assigned
// to clusters, so the outer loop naturally terminates when all points are clustered.
//
// [ALGORITHM] For each unvisited point p:
//   1. Start a new cluster with p.
//   2. Recursively (via the `group` lambda) find all points in the tree that satisfy
//      the query predicate (distance-based or custom), set-difference against already-
//      clustered points, and add them to the current cluster.
//   3. Remove clustered points from the tree.
//   4. Repeat until tree is empty.
//
// [MEMORY] Index3D (Boost rstar<16,4>) is rebuilt from scratch per `cluster()` call.
//          O(N log N) insertion. Removal during clustering is O(log N) per element.
//          Total: O(N log N) amortized.
//
// [CONCURRENCY] Not thread-safe. Local state in `group` lambda captures `sindex` by ref.
//               Multiple concurrent calls to `cluster()` are safe if called with separate inputs.
//
// [HAZARD] H913 — `distance_queryfn` uses `it = tmp.erase(it)` to remove out-of-range points
//   while iterating. This is correct C++ (erase returns the next valid iterator), but is
//   brittle: any future refactoring that adds `++it` before erase would cause UB.
//   Also: the loop modifies `tmp` (a local sorted-by-distance vector from `bgi::nearest`),
//   erasing points beyond `dist`. If `max_points` is very large, this causes O(N) erase
//   operations on a vector — O(N²) in the worst case for the inner erase loop.

#include "Clustering.hpp"
#include "boost/geometry/index/rtree.hpp"

#include <libslic3r/SLA/SpatIndex.hpp>
#include <libslic3r/SLA/BoostAdapter.hpp>

namespace Slic3r { namespace sla {

namespace bgi = boost::geometry::index;
// [STATE] Index3D is the internal mutable R*-tree used during clustering.
// rstar<16,4>: max 16 elements per node. Same params as PointIndex/BoxIndex.
using Index3D = bgi::rtree<PointIndexEl, bgi::rstar<16, 4> /* ? */>;

namespace {

// [INTENT] Comparison for PointIndexEl by index (second field). Used for sorted set ops.
bool cmp_ptidx_elements(const PointIndexEl& e1, const PointIndexEl& e2) { return e1.second < e2.second; };

// [INTENT] Core clustering loop. Recursively expands clusters using the provided query fn.
// The `group` lambda is self-referential (captures itself) — a recursive std::function.
// [COUPLING] `sindex` is mutated (elements removed) as a side-effect of clustering.
ClusteredPoints cluster(Index3D&                                                                      sindex,
                        unsigned                                                                      max_points,
                        std::function<std::vector<PointIndexEl>(const Index3D&, const PointIndexEl&)> qfn)
{
    using Elems = std::vector<PointIndexEl>;

    // Recursive function for visiting all the points in a given distance to
    // each other
    std::function<void(Elems&, Elems&)> group = [&sindex, &group, max_points, qfn](Elems& pts, Elems& cluster) {
        for (auto& p : pts) {
            std::vector<PointIndexEl> tmp = qfn(sindex, p);

            std::sort(tmp.begin(), tmp.end(), cmp_ptidx_elements);

            Elems newpts;
            std::set_difference(tmp.begin(), tmp.end(), cluster.begin(), cluster.end(), std::back_inserter(newpts), cmp_ptidx_elements);

            int c = max_points && newpts.size() + cluster.size() > max_points ? int(max_points - cluster.size()) : int(newpts.size());

            cluster.insert(cluster.end(), newpts.begin(), newpts.begin() + c);
            std::sort(cluster.begin(), cluster.end(), cmp_ptidx_elements);

            if (!newpts.empty() && (!max_points || cluster.size() < max_points))
                group(newpts, cluster);
        }
    };

    std::vector<Elems> clusters;
    for (auto it = sindex.begin(); it != sindex.end();) {
        Elems cluster = {};
        Elems pts     = {*it};
        group(pts, cluster);

        for (auto& c : cluster)
            sindex.remove(c);
        it = sindex.begin();

        clusters.emplace_back(cluster);
    }

    ClusteredPoints result;
    for (auto& cluster : clusters) {
        result.emplace_back();
        for (auto c : cluster)
            result.back().emplace_back(c.second);
    }

    return result;
}

// [INTENT] Query function for distance-based clustering.
// Gets the nearest `max_points` neighbors from the tree, then filters out those
// beyond `dist`.
// [HAZARD] H913 — erase-while-iterate pattern: safe but brittle.
std::vector<PointIndexEl> distance_queryfn(const Index3D& sindex, const PointIndexEl& p, double dist, unsigned max_points)
{
    std::vector<PointIndexEl> tmp;
    tmp.reserve(max_points);
    sindex.query(bgi::nearest(p.first, max_points), std::back_inserter(tmp));

    for (auto it = tmp.begin(); it < tmp.end(); ++it)
        if ((p.first - it->first).norm() > dist)
            it = tmp.erase(it);

    return tmp;
}

} // namespace

// Clustering a set of points by the given criteria
ClusteredPoints cluster(const std::vector<unsigned>& indices, std::function<Vec3d(unsigned)> pointfn, double dist, unsigned max_points)
{
    // A spatial index for querying the nearest points
    Index3D sindex;

    // Build the index
    for (auto idx : indices)
        sindex.insert(std::make_pair(pointfn(idx), idx));

    return cluster(sindex, max_points,
                   [dist, max_points](const Index3D& sidx, const PointIndexEl& p) { return distance_queryfn(sidx, p, dist, max_points); });
}

// Clustering a set of points by the given criteria
ClusteredPoints cluster(const std::vector<unsigned>&                                  indices,
                        std::function<Vec3d(unsigned)>                                pointfn,
                        std::function<bool(const PointIndexEl&, const PointIndexEl&)> predicate,
                        unsigned                                                      max_points)
{
    // A spatial index for querying the nearest points
    Index3D sindex;

    // Build the index
    for (auto idx : indices)
        sindex.insert(std::make_pair(pointfn(idx), idx));

    return cluster(sindex, max_points, [max_points, predicate](const Index3D& sidx, const PointIndexEl& p) {
        std::vector<PointIndexEl> tmp;
        tmp.reserve(max_points);
        sidx.query(bgi::satisfies([p, predicate](const PointIndexEl& e) { return predicate(p, e); }), std::back_inserter(tmp));
        return tmp;
    });
}

ClusteredPoints cluster(const Eigen::MatrixXd& pts, double dist, unsigned max_points)
{
    // A spatial index for querying the nearest points
    Index3D sindex;

    // Build the index
    for (Eigen::Index i = 0; i < pts.rows(); i++)
        sindex.insert(std::make_pair(Vec3d(pts.row(i)), unsigned(i)));

    return cluster(sindex, max_points,
                   [dist, max_points](const Index3D& sidx, const PointIndexEl& p) { return distance_queryfn(sidx, p, dist, max_points); });
}

}} // namespace Slic3r::sla
