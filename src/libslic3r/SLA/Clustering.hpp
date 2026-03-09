// [INTENT] Clustering.hpp — Density-connected component clustering for SLA support points.
// Given a set of 3D points, groups them into clusters where every point in a cluster
// is reachable from every other via a chain of points each within `dist` of the next.
// This is essentially DBSCAN-style density reachability, but implemented as recursive BFS.
//
// Used by SupportPointGenerator to group candidate support points by proximity before
// selecting one representative per cluster, minimizing total support count.
//
// Three overloads:
//   1. index-based with distance threshold + max_points cap
//   2. index-based with custom predicate (arbitrary pair-acceptance function)
//   3. Eigen::MatrixXd rows (bulk import from numeric matrix)
//
// [STATE] Stateless free functions. All state is in local variables.
// [MEMORY] Builds an internal Index3D (Boost R*-tree rstar<16,4>) per call.
//          The tree is mutable — elements are removed as they are assigned to clusters.
//          Total O(N log N) build + O(N log N) query amortized.
// [CONCURRENCY] Not thread-safe. Each call creates its own local tree — safe to call
//               from parallel contexts IF called with disjoint input sets.
//
// [COUPLING] `cluster_centroid` is an inline template in this header — exposed to all callers.
//   It is O(n²) in cluster size (all pairs). The comment says "intended for small clusters
//   (cca 3 elements)" but this is not enforced. If called on large clusters it will be slow.
//
// [HAZARD] H913 — `distance_queryfn` (in .cpp) iterates from front and uses `it = tmp.erase(it)`
//   to remove points beyond `dist`. This modifies the vector while iterating with a pointer-based
//   iterator. `std::vector::erase` invalidates all iterators from the erased position onward.
//   The re-assignment `it = tmp.erase(it)` correctly captures the new valid iterator, so the
//   pattern is safe — but it is non-obvious and brittle; any refactor that adds a `++it` before
//   the erase call would produce a dangling iterator UB.

#ifndef SLA_CLUSTERING_HPP
#define SLA_CLUSTERING_HPP

#include <vector>

#include <libslic3r/Point.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>

namespace Slic3r { namespace sla {

using ClusterEl       = std::vector<unsigned>;
using ClusteredPoints = std::vector<ClusterEl>;

// [INTENT] Group points by spatial density (distance threshold). Returns a vector of
// clusters, each cluster being a vector of indices into the original point set.
ClusteredPoints cluster(const std::vector<unsigned>& indices, std::function<Vec3d(unsigned)> pointfn, double dist, unsigned max_points);

// [INTENT] Group Eigen matrix rows by spatial density.
ClusteredPoints cluster(const Eigen::MatrixXd& points, double dist, unsigned max_points);

// [INTENT] Group points using a custom pair-acceptance predicate.
ClusteredPoints cluster(const std::vector<unsigned>&                                  indices,
                        std::function<Vec3d(unsigned)>                                pointfn,
                        std::function<bool(const PointIndexEl&, const PointIndexEl&)> predicate,
                        unsigned                                                      max_points);

// This function returns the position of the centroid in the input 'clust'
// vector of point indices.
// [HAZARD] O(n^2) in cluster size. The comment says "cca 3 elements" — not enforced.
// Do not call on large clusters without profiling.
template<class DistFn, class PointFn> long cluster_centroid(const ClusterEl& clust, PointFn pointfn, DistFn df)
{
    switch (clust.size()) {
    case 0: /* empty cluster */ return -1;
    case 1: /* only one element */ return 0;
    case 2: /* if two elements, there is no center */ return 0;
    default:;
    }

    // The function works by calculating for each point the average distance
    // from all the other points in the cluster. We create a selector bitmask of
    // the same size as the cluster. The bitmask will have two true bits and
    // false bits for the rest of items and we will loop through all the
    // permutations of the bitmask (combinations of two points). Get the
    // distance for the two points and add the distance to the averages.
    // The point with the smallest average than wins.

    // The complexity should be O(n^2) but we will mostly apply this function
    // for small clusters only (cca 3 elements)

    std::vector<bool> sel(clust.size(), false);  // create full zero bitmask
    std::fill(sel.end() - 2, sel.end(), true);   // insert the two ones
    std::vector<double> avgs(clust.size(), 0.0); // store the average distances

    do {
        std::array<size_t, 2> idx;
        for (size_t i = 0, j = 0; i < clust.size(); i++)
            if (sel[i])
                idx[j++] = i;

        double d = df(pointfn(clust[idx[0]]), pointfn(clust[idx[1]]));

        // add the distance to the sums for both associated points
        for (auto i : idx)
            avgs[i] += d;

        // now continue with the next permutation of the bitmask with two 1s
    } while (std::next_permutation(sel.begin(), sel.end()));

    // Divide by point size in the cluster to get the average (may be redundant)
    for (auto& a : avgs)
        a /= clust.size();

    // get the lowest average distance and return the index
    auto minit = std::min_element(avgs.begin(), avgs.end());
    return long(minit - avgs.begin());
}

}} // namespace Slic3r::sla

#endif // CLUSTERING_HPP
