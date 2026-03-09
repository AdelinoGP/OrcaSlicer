// [INTENT] SpatIndex.hpp — Pimpl-wrapped Boost.Geometry R*-tree spatial indexes for
// 3D points (PointIndex) and 2D bounding boxes (BoxIndex). Hides Boost headers behind
// a compilation firewall to keep the rest of the SLA pipeline buildable without
// including heavy Boost.Geometry headers in every TU.
//
// PointIndex: Stores (Vec3d, unsigned) pairs. Supports k-nearest and predicate queries.
// BoxIndex: Stores (BoundingBox, unsigned) pairs. Supports intersects/within queries.
//
// [MEMORY] Each index owns its Boost rtree via unique_ptr<Impl>. Copy ctor does a
// deep copy of the rtree. Move ctor transfers ownership. Both are O(N) cost.
//
// [CONCURRENCY] Not thread-safe. Multiple concurrent insert/remove/query calls on
// the same index constitute a data race. External locking required for concurrent use.
//
// [COUPLING] PointIndex uses Vec3d (double) coordinates regardless of whether the
// stored points represent 2D or 3D geometry. Callers that store 2D points must pad
// with z=0. Any distance queries use 3D Euclidean distance — using this for 2D
// proximity produces correct results only when z=0 for all entries.
//
// [HAZARD] H911 — BoxIndex::query() return vector is pre-reserved to
// `m_impl->m_store.size()` (all elements). For large indexes this reserves memory
// proportional to the total number of elements even when only a few results are
// expected. Memory usage is O(N) per query rather than O(results).

#ifndef SLA_SPATINDEX_HPP
#define SLA_SPATINDEX_HPP

#include <memory>
#include <utility>
#include <vector>

#include <Eigen/Geometry>

#include <libslic3r/BoundingBox.hpp>

namespace Slic3r { namespace sla {

// [STATE] Vec3d used for all 3D point indices. Z=0 convention for 2D points.
typedef Eigen::Matrix<double, 3, 1, Eigen::DontAlign> Vec3d;
using PointIndexEl = std::pair<Vec3d, unsigned>;

// [INTENT] R*-tree spatial index for 3D points.
// Pimpl hides Boost.Geometry rtree (rstar<16,4>) from callers.
// [MEMORY] Deep copy on copy construction; O(N). Move is O(1).
class PointIndex
{
    class Impl;

    // We use Pimpl because it takes a long time to compile boost headers which
    // is the engine of this class. We include it only in the cpp file.
    std::unique_ptr<Impl> m_impl;

public:
    PointIndex();
    ~PointIndex();

    PointIndex(const PointIndex&);
    PointIndex(PointIndex&&);
    PointIndex& operator=(const PointIndex&);
    PointIndex& operator=(PointIndex&&);

    void insert(const PointIndexEl&);
    bool remove(const PointIndexEl&);

    inline void insert(const Vec3d& v, unsigned idx) { insert(std::make_pair(v, unsigned(idx))); }

    // [CONCURRENCY] Not thread-safe — concurrent queries or mutations race on m_impl.
    std::vector<PointIndexEl> query(std::function<bool(const PointIndexEl&)>) const;
    std::vector<PointIndexEl> nearest(const Vec3d&, unsigned k) const;
    std::vector<PointIndexEl> query(const Vec3d& v, unsigned k) const // wrapper
    {
        return nearest(v, k);
    }

    // For testing
    size_t size() const;
    bool   empty() const { return size() == 0; }

    void foreach (std::function<void(const PointIndexEl& el)> fn);
    void foreach (std::function<void(const PointIndexEl& el)> fn) const;
};

// [STATE] Box index element: Slic3r BoundingBox (int32) paired with an unsigned ID.
using BoxIndexEl = std::pair<Slic3r::BoundingBox, unsigned>;

// [INTENT] R*-tree spatial index for 2D bounding boxes.
// Supports intersects and within queries by BoundingBox.
// [HAZARD] H911 — query() pre-reserves return vector to entire tree size.
class BoxIndex
{
    class Impl;

    // We use Pimpl because it takes a long time to compile boost headers which
    // is the engine of this class. We include it only in the cpp file.
    std::unique_ptr<Impl> m_impl;

public:
    BoxIndex();
    ~BoxIndex();

    BoxIndex(const BoxIndex&);
    BoxIndex(BoxIndex&&);
    BoxIndex& operator=(const BoxIndex&);
    BoxIndex& operator=(BoxIndex&&);

    void insert(const BoxIndexEl&);
    void insert(const BoundingBox& bb, unsigned idx) { insert(std::make_pair(bb, unsigned(idx))); }

    bool remove(const BoxIndexEl&);

    enum QueryType { qtIntersects, qtWithin };

    // [HAZARD] H911 — reserves ret to store.size() regardless of expected result count.
    std::vector<BoxIndexEl> query(const BoundingBox&, QueryType qt);

    // For testing
    size_t size() const;
    bool   empty() const { return size() == 0; }

    void foreach (std::function<void(const BoxIndexEl& el)> fn);
};

}} // namespace Slic3r::sla

#endif // SPATINDEX_HPP
