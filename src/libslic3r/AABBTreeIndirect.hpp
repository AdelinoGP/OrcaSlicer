// AABB tree built upon external data set, referencing the external data by integer indices.
// The AABB tree balancing and traversal (ray casting, closest triangle of an indexed triangle mesh)
// were adapted from libigl AABB.{cpp,hpp} Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
// while the implicit balanced tree representation and memory optimizations are Vojtech's.

// [INTENT] Provides a static, balanced Axis-Aligned Bounding Box (AABB) tree for fast spatial
// queries (nearest-primitive, ray intersection, within-radius, point-inside-polygon tests) over
// any indexed set of geometric primitives (3D triangle meshes, 2D line segments, 2D points, etc.).
// The same Tree<NDims, CoordType> template covers all dimensionalities; specialisations for
// triangles (3D) and line segments (2D) are added via build_aabb_tree_over_indexed_triangle_set()
// and AABBTreeLines::build_aabb_tree_over_indexed_lines().
//
// [INTENT] The implicit heap layout stores children at 2*i+1 and 2*i+2, identical to a binary
// heap.  This avoids per-node pointer storage, improves cache locality on traversal, and lets the
// allocator touch only a single contiguous vector.
//
// [HAZARD] H708 — The tree is static: once built, adding or removing primitives requires a full
// rebuild.  There is no incremental update API.  Callers that mutate the underlying geometry after
// build will silently query a stale tree.
//
// [COUPLING] Depends on Eigen::AlignedBox for bounding-box type; Eigen types are the ABI boundary
// throughout (VectorType, BoundingBox).  A refactor to a non-Eigen host language must replicate
// the AlignedBox arithmetic (squaredExteriorDistance, contains, intersects).
//
// [CONCURRENCY] The tree itself is immutable after build().  build() / build_modify_input() are
// NOT thread-safe if multiple threads try to build the same Tree simultaneously.  Query functions
// (squared_distance_to_indexed_*, intersect_ray_*, traverse) are read-only and safe to call
// concurrently once the tree is built.

#ifndef slic3r_AABBTreeIndirect_hpp_
#define slic3r_AABBTreeIndirect_hpp_

#include <algorithm>
#include <limits>
#include <type_traits>
#include <vector>

#include <Eigen/Geometry>

#include "BoundingBox.hpp"
#include "Utils.hpp" // for next_highest_power_of_2()

// Definition of the ray intersection hit structure.
#include <igl/Hit.h>

namespace Slic3r { namespace AABBTreeIndirect {

// Static balanced AABB tree for raycasting and closest triangle search.
// The balanced tree is built over a single large std::vector of nodes, where the children of nodes
// are addressed implicitely using a power of two indexing rule.
// Memory for a full balanced tree is allocated, but not all nodes at the last level are used.
// This may seem like a waste of memory, but one saves memory for the node links and there is zero
// overhead of a memory allocator management (usually the memory allocator adds at least one pointer
// before the memory returned). However, allocating memory in a single vector is very fast even
// in multi-threaded environment and it is cache friendly.
//
// A balanced tree is built upon a vector of bounding boxes and their centroids, storing the reference
// to the source entity (a 3D triangle, a 2D segment etc, a 3D or 2D point etc).
// The source bounding boxes may have an epsilon applied to fight numeric rounding errors when
// traversing the AABB tree.
//
// [INTENT] Template parameters allow Tree<2,float>, Tree<3,float>, Tree<2,double>, Tree<3,double>
// etc.  Aliased as Tree2f, Tree3f, Tree2d, Tree3d at the bottom of this file.
// SourceNode is a duck-typed concept: must implement idx(), centroid(), bbox() — see build().
//
// [MEMORY] Single allocation: m_nodes (std::vector<Node>).  Size = next_highest_power_of_2(N)*2-1
// where N = number of primitives (worst-case ≈ 2× N nodes).  No dynamic per-node allocation.
// Entire tree destroyed when Tree goes out of scope.
//
// [HAZARD] H709 — build_modify_input() partitions the input vector in place via QuickSelect.
// build(std::move(input)) also clears the vector after use.  Callers must not assume the input
// is intact after build() or build_modify_input() returns.
template<int ANumDimensions, typename ACoordType> class Tree
{
public:
    static constexpr int NumDimensions = ANumDimensions;
    using CoordType                    = ACoordType;
    using VectorType                   = Eigen::Matrix<CoordType, NumDimensions, 1, Eigen::DontAlign>;
    using BoundingBox                  = Eigen::AlignedBox<CoordType, NumDimensions>;
    // Following could be static constexpr size_t, but that would not link in C++11
    enum : size_t {
        // Node is not used.
        npos = size_t(-1),
        // Inner node (not leaf).
        inner = size_t(-2)
    };

    // Single node of the implicit balanced AABB tree. There are no links to the children nodes,
    // as these links are calculated implicitely using a power of two rule.
    struct Node
    {
        // Index of the external source entity, for which this AABB tree was built, npos for internal nodes.
        size_t idx = npos;
        // Bounding box around this entity, possibly with epsilons applied to fight numeric rounding errors
        // when traversing the AABB tree.
        BoundingBox bbox;

        bool is_valid() const { return this->idx != npos; }
        bool is_inner() const { return this->idx == inner; }
        bool is_leaf() const { return !this->is_inner(); }

        template<typename SourceNode> void set(const SourceNode& rhs)
        {
            this->idx  = rhs.idx();
            this->bbox = rhs.bbox();
        }
    };

    void clear() { m_nodes.clear(); }

    // SourceNode shall implement
    // size_t SourceNode::idx() const
    // 		- Index to the outside entity (triangle, edge, point etc).
    // const VectorType& SourceNode::centroid() const
    // 		- Centroid of this node. The centroid is used for balancing the tree.
    // const BoundingBox& SourceNode::bbox() const
    // 		- Bounding box of this node, likely expanded with epsilon to account for numeric rounding during tree traversal.
    //        Union of bounding boxes at a single level of the AABB tree is used for deciding the longest axis aligned dimension
    //        to split around.
    template<typename SourceNode> void build(std::vector<SourceNode>&& input)
    {
        this->build_modify_input(input);
        input.clear();
    }

    template<typename SourceNode> void build_modify_input(std::vector<SourceNode>& input)
    {
        if (input.empty())
            clear();
        else {
            // Allocate enough memory for a full binary tree.
            m_nodes.assign(next_highest_power_of_2(input.size()) * 2 - 1, Node());
            build_recursive(input, 0, 0, input.size() - 1);
        }
    }

    const std::vector<Node>& nodes() const { return m_nodes; }
    const Node&              node(size_t idx) const { return m_nodes[idx]; }
    bool                     empty() const { return m_nodes.empty(); }

    // Addressing the child nodes using the power of two rule.
    static size_t left_child_idx(size_t idx) { return idx * 2 + 1; }
    static size_t right_child_idx(size_t idx) { return left_child_idx(idx) + 1; }
    const Node&   left_child(size_t idx) const { return m_nodes[left_child_idx(idx)]; }
    const Node&   right_child(size_t idx) const { return m_nodes[right_child_idx(idx)]; }

    template<typename SourceNode> void build(const std::vector<SourceNode>& input)
    {
        std::vector<SourceNode> copy(input);
        this->build(std::move(copy));
    }

private:
    // Build a balanced tree by splitting the input sequence by an axis aligned plane at a dimension.
    // [INTENT] Recursive top-down construction: at each level, compute the union bounding box of all
    // remaining primitives, split along the longest axis at the median position (via QuickSelect),
    // and recurse.  This produces a height-balanced tree with O(N log N) build time.
    // [HAZARD] H710 — Node index `node` must satisfy node < m_nodes.size(); if input.size() > the
    // power-of-two allocation, this assert fires.  next_highest_power_of_2() ensures correctness
    // but callers of build_recursive directly could violate this.
    template<typename SourceNode> void build_recursive(std::vector<SourceNode>& input, size_t node, const size_t left, const size_t right)
    {
        assert(node < m_nodes.size());
        assert(left <= right);

        if (left == right) {
            // Insert a node into the balanced tree.
            m_nodes[node].set(input[left]);
            return;
        }

        // Calculate bounding box of the input.
        BoundingBox bbox(input[left].bbox());
        for (size_t i = left + 1; i <= right; ++i)
            bbox.extend(input[i].bbox());
        int dimension = -1;
        bbox.diagonal().maxCoeff(&dimension);

        // Partition the input to left / right pieces of the same length to produce a balanced tree.
        size_t center = (left + right) / 2;
        partition_input(input, size_t(dimension), left, right, center);
        // Insert an inner node into the tree. Inner node does not reference any input entity (triangle, line segment etc).
        m_nodes[node].idx  = inner;
        m_nodes[node].bbox = bbox;
        build_recursive(input, node * 2 + 1, left, center);
        build_recursive(input, node * 2 + 2, center + 1, right);
    }

    // Partition the input m_nodes <left, right> at "k" and "dimension" using the QuickSelect method:
    // https://en.wikipedia.org/wiki/Quickselect
    // Items left of the k'th item are lower than the k'th item in the "dimension",
    // items right of the k'th item are higher than the k'th item in the "dimension",
    template<typename SourceNode>
    void partition_input(std::vector<SourceNode>& input, const size_t dimension, size_t left, size_t right, const size_t k) const
    {
        while (left < right) {
            size_t    center = (left + right) / 2;
            CoordType pivot;
            {
                // Bubble sort the input[left], input[center], input[right], so that a median of the three values
                // will end up in input[center].
                CoordType left_value   = input[left].centroid()(dimension);
                CoordType center_value = input[center].centroid()(dimension);
                CoordType right_value  = input[right].centroid()(dimension);
                if (left_value > center_value) {
                    std::swap(input[left], input[center]);
                    std::swap(left_value, center_value);
                }
                if (left_value > right_value) {
                    std::swap(input[left], input[right]);
                    right_value = left_value;
                }
                if (center_value > right_value) {
                    std::swap(input[center], input[right]);
                    center_value = right_value;
                }
                pivot = center_value;
            }
            if (right <= left + 2)
                // The <left, right> interval is already sorted.
                break;
            size_t i = left;
            size_t j = right - 1;
            std::swap(input[center], input[j]);
            // Partition the set based on the pivot.
            for (;;) {
                // Skip left points that are already at correct positions.
                // Search will certainly stop at position (right - 1), which stores the pivot.
                while (input[++i].centroid()(dimension) < pivot)
                    ;
                // Skip right points that are already at correct positions.
                while (input[--j].centroid()(dimension) > pivot && i < j)
                    ;
                if (i >= j)
                    break;
                std::swap(input[i], input[j]);
            }
            // Restore pivot to the center of the sequence.
            std::swap(input[i], input[right - 1]);
            // Which side the kth element is in?
            if (k < i)
                right = i - 1;
            else if (k == i)
                // Sequence is partitioned, kth element is at its place.
                break;
            else
                left = i + 1;
        }
    }

    // The balanced tree storage.
    std::vector<Node> m_nodes;
};

using Tree2f = Tree<2, float>;
using Tree3f = Tree<3, float>;
using Tree2d = Tree<2, double>;
using Tree3d = Tree<3, double>;

// Wrap a 2D Slic3r own BoundingBox to be passed to Tree::build() and similar
// to build an AABBTree over coord_t 2D bounding boxes.
// [INTENT] Adapts the Slic3r integer BoundingBox (coord_t) into the Eigen::AlignedBox<coord_t,2>
// expected by Tree<2, coord_t>.  The epsilon inflation (+/- SCALED_EPSILON on all four sides)
// prevents missed queries when points sit exactly on a box boundary due to integer rounding.
// [HAZARD] H711 — centroid() computes (min + max/2), which is (min + max)/2 only by coincidence
// (integer division rounds down).  The correct formula is (min + max) / 2; the current form
// can produce a centroid shifted toward min by 0–1 scaled units — benign in practice but
// mathematically incorrect and potentially de-balances the tree slightly.
class BoundingBoxWrapper
{
public:
    using BoundingBox = Eigen::AlignedBox<coord_t, 2>;
    BoundingBoxWrapper(const size_t idx, const Slic3r::BoundingBox& bbox)
        : m_idx(idx)
        ,
        // Inflate the bounding box a bit to account for numerical issues.
        m_bbox(bbox.min - Point(SCALED_EPSILON, SCALED_EPSILON), bbox.max + Point(SCALED_EPSILON, SCALED_EPSILON))
    {}
    size_t             idx() const { return m_idx; }
    const BoundingBox& bbox() const { return m_bbox; }
    Point              centroid() const { return (m_bbox.min() + m_bbox.max() / 2); }

private:
    size_t      m_idx;
    BoundingBox m_bbox;
};

namespace detail {
// [INTENT] RayIntersector bundles the query context (mesh data + tree reference + ray parameters)
// into a single struct so recursive traversal functions receive a single argument instead of a
// long parameter list.  invdir (component-wise reciprocal of dir) is precomputed once to convert
// division into multiplication inside the hot ray-box test loop.
// [COUPLING] Depends on igl::Hit for the hit record format (face_id, barycentric u/v, ray param t).
// A refactoring must either port igl::Hit or redefine the hit record type.
template<typename AVertexType, typename AIndexedFaceType, typename ATreeType, typename AVectorType> struct RayIntersector
{
    using VertexType      = AVertexType;
    using IndexedFaceType = AIndexedFaceType;
    using TreeType        = ATreeType;
    using VectorType      = AVectorType;

    const std::vector<VertexType>&      vertices;
    const std::vector<IndexedFaceType>& faces;
    const TreeType&                     tree;

    const VectorType origin;
    const VectorType dir;
    const VectorType invdir;

    // epsilon for ray-triangle intersection, see intersect_triangle1()
    const double eps;
};

template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
struct RayIntersectorHits : RayIntersector<VertexType, IndexedFaceType, TreeType, VectorType>
{
    std::vector<igl::Hit> hits;
};

// FIXME implement SSE for float AABB trees with float ray queries.
//  SSE/SSE2 is supported by any Intel/AMD x64 processor.
//  SSE support requires 16 byte alignment of the AABB nodes, representing the bounding boxes with 4+4 floats,
//  storing the node index as the 4th element of the bounding box min value etc.
//  https://www.flipcode.com/archives/SSE_RayBox_Intersection_Test.shtml
// [INTENT] Amy Williams et al. slab method: for each of 3 axis-aligned slab pairs, compute entry/exit
// t-intervals by multiplying (box_face - origin) by invdir (avoids division).  Swap slab faces
// based on sign of invdir to handle negative-direction rays without branches in the inner loop.
// Returns true iff the ray parameter interval [t0, t1) intersects the box.
// [HAZARD] H712 — Takes box by value and swaps its min/max faces, which mutates the local copy.
// This is intentional (the original box is unchanged) but can confuse readers expecting a const reference.
// [HAZARD] H713 — FIXME comment acknowledges SSE optimisation is not implemented; this is a known
// performance opportunity that translation agents should preserve as a comment/TODO.
template<typename Derivedsource, typename Deriveddir, typename Scalar>
inline bool ray_box_intersect_invdir(const Eigen::MatrixBase<Derivedsource>& origin,
                                     const Eigen::MatrixBase<Deriveddir>&    inv_dir,
                                     Eigen::AlignedBox<Scalar, 3>            box,
                                     const Scalar&                           t0,
                                     const Scalar&                           t1)
{
    // http://people.csail.mit.edu/amy/papers/box-jgt.pdf
    // "An Efficient and Robust Ray–Box Intersection Algorithm"
    if (inv_dir.x() < 0)
        std::swap(box.min().x(), box.max().x());
    if (inv_dir.y() < 0)
        std::swap(box.min().y(), box.max().y());
    Scalar tmin  = (box.min().x() - origin.x()) * inv_dir.x();
    Scalar tymax = (box.max().y() - origin.y()) * inv_dir.y();
    if (tmin > tymax)
        return false;
    Scalar tmax  = (box.max().x() - origin.x()) * inv_dir.x();
    Scalar tymin = (box.min().y() - origin.y()) * inv_dir.y();
    if (tymin > tmax)
        return false;
    if (tymin > tmin)
        tmin = tymin;
    if (tymax < tmax)
        tmax = tymax;
    if (inv_dir.z() < 0)
        std::swap(box.min().z(), box.max().z());
    Scalar tzmin = (box.min().z() - origin.z()) * inv_dir.z();
    if (tzmin > tmax)
        return false;
    Scalar tzmax = (box.max().z() - origin.z()) * inv_dir.z();
    if (tmin > tzmax)
        return false;
    if (tzmin > tmin)
        tmin = tzmin;
    if (tzmax < tmax)
        tmax = tzmax;
    return tmin < t1 && tmax > t0;
}

// The following intersect_triangle() is derived from raytri.c routine intersect_triangle1()
// Ray-Triangle Intersection Test Routines
// Different optimizations of my and Ben Trumbore's
// code from journals of graphics tools (JGT)
// http://www.acm.org/jgt/
// by Tomas Moller, May 2000
template<typename V, typename W>
std::enable_if_t<std::is_same<typename V::Scalar, double>::value && std::is_same<typename W::Scalar, double>::value, bool> intersect_triangle(
    const V& orig, const V& dir, const W& vert0, const W& vert1, const W& vert2, double& t, double& u, double& v, double eps)
{
    // find vectors for two edges sharing vert0
    const V edge1 = vert1 - vert0;
    const V edge2 = vert2 - vert0;
    // begin calculating determinant - also used to calculate U parameter
    const V pvec = dir.cross(edge2);
    // if determinant is near zero, ray lies in plane of triangle
    const double det = edge1.dot(pvec);
    V            qvec;

    if (det > eps) {
        // calculate distance from vert0 to ray origin
        V tvec = orig - vert0;
        // calculate U parameter and test bounds
        u = tvec.dot(pvec);
        if (u < 0.0 || u > det)
            return false;
        // prepare to test V parameter
        qvec = tvec.cross(edge1);
        // calculate V parameter and test bounds
        v = dir.dot(qvec);
        if (v < 0.0 || u + v > det)
            return false;
    } else if (det < -eps) {
        // calculate distance from vert0 to ray origin
        V tvec = orig - vert0;
        // calculate U parameter and test bounds
        u = tvec.dot(pvec);
        if (u > 0.0 || u < det)
            return false;
        // prepare to test V parameter
        qvec = tvec.cross(edge1);
        // calculate V parameter and test bounds
        v = dir.dot(qvec);
        if (v > 0.0 || u + v < det)
            return false;
    } else
        // ray is parallel to the plane of the triangle
        return false;

    double inv_det = 1.0 / det;
    // calculate t, ray intersects triangle
    t = edge2.dot(qvec) * inv_det;
    u *= inv_det;
    v *= inv_det;
    return true;
}

template<typename V, typename W>
std::enable_if_t<std::is_same<typename V::Scalar, double>::value && !std::is_same<typename W::Scalar, double>::value, bool>
intersect_triangle(const V& origin, const V& dir, const W& v0, const W& v1, const W& v2, double& t, double& u, double& v, double eps)
{
    return intersect_triangle(origin, dir, v0.template cast<double>(), v1.template cast<double>(), v2.template cast<double>(), t, u, v, eps);
}

template<typename V, typename W>
std::enable_if_t<!std::is_same<typename V::Scalar, double>::value && std::is_same<typename W::Scalar, double>::value, bool>
intersect_triangle(const V& origin, const V& dir, const W& v0, const W& v1, const W& v2, double& t, double& u, double& v, double eps)
{
    return intersect_triangle(origin.template cast<double>(), dir.template cast<double>(), v0, v1, v2, t, u, v, eps);
}

template<typename V, typename W>
std::enable_if_t<!std::is_same<typename V::Scalar, double>::value && !std::is_same<typename W::Scalar, double>::value, bool>
intersect_triangle(const V& origin, const V& dir, const W& v0, const W& v1, const W& v2, double& t, double& u, double& v, double eps)
{
    return intersect_triangle(origin.template cast<double>(), dir.template cast<double>(), v0.template cast<double>(),
                              v1.template cast<double>(), v2.template cast<double>(), t, u, v, eps);
}

template<typename Tree> double intersect_triangle_epsilon(const Tree& tree)
{
    double eps = 0.000001;
    if (!tree.empty()) {
        const typename Tree::BoundingBox& bbox = tree.nodes().front().bbox;
        double                            l    = (bbox.max() - bbox.min()).cwiseMax();
        if (l > 0)
            eps /= (l * l);
    }
    return eps;
}

// [INTENT] Depth-first branch-and-bound ray traversal: at each inner node, test both children
// against the ray-box test.  Recurse into the closer child first (implicit due to left-first order)
// to tighten min_t as quickly as possible, pruning the further child if its box exceeds the current
// best t.  On leaf, call intersect_triangle(); update hit only when t > 0 (forward hit only).
// Returns true iff any hit was found in the [0, min_t) interval.
// [HAZARD] H719 — Both left and right children are visited sequentially without ordering by
// box-entry distance.  The left child is always expanded first, even when the right child is
// geometrically closer.  This can cause O(log N) more triangle tests than an ordered traversal
// (e.g., front-to-back with priority queue).  For dense meshes with many triangles behind the
// first hit, this is a known performance hazard.
template<typename RayIntersectorType, typename Scalar>
static inline bool intersect_ray_recursive_first_hit(RayIntersectorType& ray_intersector, size_t node_idx, Scalar min_t, igl::Hit& hit)
{
    const auto& node = ray_intersector.tree.node(node_idx);
    assert(node.is_valid());

    if (!ray_box_intersect_invdir(ray_intersector.origin, ray_intersector.invdir, node.bbox.template cast<Scalar>(), Scalar(0), min_t))
        return false;

    if (node.is_leaf()) {
        // shoot ray, record hit
        auto   face = ray_intersector.faces[node.idx];
        double t, u, v;
        if (intersect_triangle(ray_intersector.origin, ray_intersector.dir, ray_intersector.vertices[face(0)],
                               ray_intersector.vertices[face(1)], ray_intersector.vertices[face(2)], t, u, v, ray_intersector.eps) &&
            t > 0.) {
            hit = igl::Hit{int(node.idx), -1, float(u), float(v), float(t)};
            return true;
        } else
            return false;
    } else {
        // Left / right child node index.
        size_t   left  = node_idx * 2 + 1;
        size_t   right = left + 1;
        igl::Hit left_hit;
        igl::Hit right_hit;
        bool     left_ret = intersect_ray_recursive_first_hit(ray_intersector, left, min_t, left_hit);
        if (left_ret && left_hit.t < min_t) {
            min_t = left_hit.t;
            hit   = left_hit;
        } else
            left_ret = false;
        bool right_ret = intersect_ray_recursive_first_hit(ray_intersector, right, min_t, right_hit);
        if (right_ret && right_hit.t < min_t)
            hit = right_hit;
        else
            right_ret = false;
        return left_ret || right_ret;
    }
}

// [INTENT] Exhaustive ray traversal: visits every node whose bounding box the ray hits,
// accumulating all leaf triangle intersections into ray_intersector.hits.  No early-exit.
// Caller sorts hits by t after this function returns (see intersect_ray_all_hits() below).
// [COUPLING] Writes directly into RayIntersectorHits::hits (inherited field); caller must
// pre-clear or pre-allocate the vector before calling.
template<typename RayIntersectorType>
static inline void intersect_ray_recursive_all_hits(RayIntersectorType& ray_intersector, size_t node_idx)
{
    using Scalar = typename RayIntersectorType::VectorType::Scalar;

    const auto& node = ray_intersector.tree.node(node_idx);
    assert(node.is_valid());

    if (!ray_box_intersect_invdir(ray_intersector.origin, ray_intersector.invdir, node.bbox.template cast<Scalar>(), Scalar(0),
                                  std::numeric_limits<Scalar>::infinity()))
        return;

    if (node.is_leaf()) {
        auto   face = ray_intersector.faces[node.idx];
        double t, u, v;
        if (intersect_triangle(ray_intersector.origin, ray_intersector.dir, ray_intersector.vertices[face(0)],
                               ray_intersector.vertices[face(1)], ray_intersector.vertices[face(2)], t, u, v, ray_intersector.eps) &&
            t > 0.) {
            ray_intersector.hits.emplace_back(igl::Hit{int(node.idx), -1, float(u), float(v), float(t)});
        }
    } else {
        // Left / right child node index.
        size_t left  = node_idx * 2 + 1;
        size_t right = left + 1;
        intersect_ray_recursive_all_hits(ray_intersector, left);
        intersect_ray_recursive_all_hits(ray_intersector, right);
    }
}

// Real-time collision detection, Ericson, Chapter 5
// [INTENT] Finds the closest point on triangle (a,b,c) to query point p using Voronoi region
// classification: test which of the 7 regions (3 vertex, 3 edge, 1 face) the projection of p
// belongs to, then compute the corresponding closest point.  All tests use dot products only —
// no square roots.  Works in any dimension N as long as the cross product is not needed.
// [HAZARD] H714 — The edge-AB guard `a != b` at the vc <= 0 branch prevents division by zero
// when triangle is degenerate (collapsed to a line along AB), but there is no guard for the
// remaining degenerate cases (AC or BC collapsed).  A fully degenerate triangle (all three
// vertices coincident) returns `a` silently without error.
template<typename Vector> static inline Vector closest_point_to_triangle(const Vector& p, const Vector& a, const Vector& b, const Vector& c)
{
    using Scalar = typename Vector::Scalar;
    // Check if P in vertex region outside A
    Vector ab = b - a;
    Vector ac = c - a;
    Vector ap = p - a;
    Scalar d1 = ab.dot(ap);
    Scalar d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    // Check if P in vertex region outside B
    Vector bp = p - b;
    Scalar d3 = ab.dot(bp);
    Scalar d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    // Check if P in edge region of AB, if so return projection of P onto AB
    Scalar vc = d1 * d4 - d3 * d2;
    if (a != b && vc <= 0 && d1 >= 0 && d3 <= 0) {
        Scalar v = d1 / (d1 - d3);
        return a + v * ab;
    }
    // Check if P in vertex region outside C
    Vector cp = p - c;
    Scalar d5 = ab.dot(cp);
    Scalar d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    // Check if P in edge region of AC, if so return projection of P onto AC
    Scalar vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        Scalar w = d2 / (d2 - d6);
        return a + w * ac;
    }
    // Check if P in edge region of BC, if so return projection of P onto BC
    Scalar va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        Scalar w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }
    // P inside face region. Compute Q through its barycentric coordinates (u,v,w)
    Scalar denom = Scalar(1.0) / (va + vb + vc);
    Scalar v     = vb * denom;
    Scalar w     = vc * denom;
    return a + ab * v + ac * w; // = u*a + v*b + w*c, u = va * denom = 1.0-v-w
};

// Nothing to do with COVID-19 social distancing.
template<typename AVertexType, typename AIndexedFaceType, typename ATreeType, typename AVectorType> struct IndexedTriangleSetDistancer
{
    using VertexType      = AVertexType;
    using IndexedFaceType = AIndexedFaceType;
    using TreeType        = ATreeType;
    using VectorType      = AVectorType;
    using ScalarType      = typename VectorType::Scalar;

    const std::vector<VertexType>&      vertices;
    const std::vector<IndexedFaceType>& faces;
    const TreeType&                     tree;

    const VectorType origin;

    inline VectorType closest_point_to_origin(size_t primitive_index, ScalarType& squared_distance) const
    {
        const auto& triangle      = this->faces[primitive_index];
        VectorType  closest_point = closest_point_to_triangle<VectorType>(origin, this->vertices[triangle(0)].template cast<ScalarType>(),
                                                                          this->vertices[triangle(1)].template cast<ScalarType>(),
                                                                          this->vertices[triangle(2)].template cast<ScalarType>());
        squared_distance          = (origin - closest_point).squaredNorm();
        return closest_point;
    }
};

// [INTENT] Branch-and-bound closest-primitive search on the implicit tree.
// up_sqr_d is the current best squared distance (upper bound); if the bounding box of a subtree
// is farther than up_sqr_d, the whole subtree is pruned.  The traversal order prefers the child
// whose bounding box is geometrically closer to the origin query point (lower squaredExteriorDistance),
// which reduces the number of primitives evaluated in practice.
// Output: returns updated up_sqr_d (smallest squared distance found); writes the primitive index
// to `i` and closest point to `c` as output-parameters (non-const ref).
// [STATE] `i` and `c` are output parameters (non-const references used as return values).
// [HAZARD] H715 — `low_sqr_d` is passed in but only used as an early-out guard for the initial
// bounding-box check.  If node_left.bbox.contains(origin), the call to look_left() happens before
// the squaredExteriorDistance guard, meaning even a very distant contained-node is always expanded.
// For meshes where the query point is inside many nested bounding boxes, this can degrade to O(N).
template<typename IndexedPrimitivesDistancerType, typename Scalar>
static inline Scalar squared_distance_to_indexed_primitives_recursive(
    IndexedPrimitivesDistancerType&                                              distancer,
    size_t                                                                       node_idx,
    Scalar                                                                       low_sqr_d,
    Scalar                                                                       up_sqr_d,
    size_t&                                                                      i,
    Eigen::PlainObjectBase<typename IndexedPrimitivesDistancerType::VectorType>& c)
{
    using Vector = typename IndexedPrimitivesDistancerType::VectorType;

    if (low_sqr_d > up_sqr_d)
        return low_sqr_d;

    // Save the best achieved hit.
    auto set_min = [&i, &c, &up_sqr_d](const Scalar sqr_d_candidate, const size_t i_candidate, const Vector& c_candidate) {
        if (sqr_d_candidate < up_sqr_d) {
            i        = i_candidate;
            c        = c_candidate;
            up_sqr_d = sqr_d_candidate;
        }
    };

    const auto& node = distancer.tree.node(node_idx);
    assert(node.is_valid());
    if (node.is_leaf()) {
        Scalar sqr_dist;
        Vector c_candidate = distancer.closest_point_to_origin(node.idx, sqr_dist);
        set_min(sqr_dist, node.idx, c_candidate);
    } else {
        size_t      left_node_idx  = node_idx * 2 + 1;
        size_t      right_node_idx = left_node_idx + 1;
        const auto& node_left      = distancer.tree.node(left_node_idx);
        const auto& node_right     = distancer.tree.node(right_node_idx);
        assert(node_left.is_valid());
        assert(node_right.is_valid());

        bool        looked_left  = false;
        bool        looked_right = false;
        const auto& look_left    = [&]() {
            size_t i_left;
            Vector c_left     = c;
            Scalar sqr_d_left = squared_distance_to_indexed_primitives_recursive(distancer, left_node_idx, low_sqr_d, up_sqr_d, i_left,
                                                                                    c_left);
            set_min(sqr_d_left, i_left, c_left);
            looked_left = true;
        };
        const auto& look_right = [&]() {
            size_t i_right;
            Vector c_right     = c;
            Scalar sqr_d_right = squared_distance_to_indexed_primitives_recursive(distancer, right_node_idx, low_sqr_d, up_sqr_d, i_right,
                                                                                  c_right);
            set_min(sqr_d_right, i_right, c_right);
            looked_right = true;
        };

        // must look left or right if in box
        using BBoxScalar = typename IndexedPrimitivesDistancerType::TreeType::BoundingBox::Scalar;
        if (node_left.bbox.contains(distancer.origin.template cast<BBoxScalar>()))
            look_left();
        if (node_right.bbox.contains(distancer.origin.template cast<BBoxScalar>()))
            look_right();
        // if haven't looked left and could be less than current min, then look
        Scalar left_up_sqr_d  = node_left.bbox.squaredExteriorDistance(distancer.origin);
        Scalar right_up_sqr_d = node_right.bbox.squaredExteriorDistance(distancer.origin);
        if (left_up_sqr_d < right_up_sqr_d) {
            if (!looked_left && left_up_sqr_d < up_sqr_d)
                look_left();
            if (!looked_right && right_up_sqr_d < up_sqr_d)
                look_right();
        } else {
            if (!looked_right && right_up_sqr_d < up_sqr_d)
                look_right();
            if (!looked_left && left_up_sqr_d < up_sqr_d)
                look_left();
        }
    }
    return up_sqr_d;
}

// [INTENT] All-within-radius primitive enumeration.  At each node, the bounding box is tested
// against the squared distance limit; if the box is farther away than the limit, the subtree is
// pruned entirely.  At a leaf, the actual closest-point distance is computed and the primitive
// is added to found_primitives_indices only if it passes the limit.
// [HAZARD] H720 — Typo in function name: "recurisve" should be "recursive".  This typo propagates
// to all call sites.  A refactoring must preserve the symbol name for link compatibility or update
// all callers simultaneously.
template<typename IndexedPrimitivesDistancerType, typename Scalar>
static inline void indexed_primitives_within_distance_squared_recurisve(const IndexedPrimitivesDistancerType& distancer,
                                                                        size_t                                node_idx,
                                                                        Scalar                                squared_distance_limit,
                                                                        std::vector<size_t>&                  found_primitives_indices)
{
    const auto& node = distancer.tree.node(node_idx);
    assert(node.is_valid());
    if (node.is_leaf()) {
        Scalar sqr_dist;
        distancer.closest_point_to_origin(node.idx, sqr_dist);
        if (sqr_dist < squared_distance_limit) {
            found_primitives_indices.push_back(node.idx);
        }
    } else {
        size_t      left_node_idx  = node_idx * 2 + 1;
        size_t      right_node_idx = left_node_idx + 1;
        const auto& node_left      = distancer.tree.node(left_node_idx);
        const auto& node_right     = distancer.tree.node(right_node_idx);
        assert(node_left.is_valid());
        assert(node_right.is_valid());

        if (node_left.bbox.squaredExteriorDistance(distancer.origin) < squared_distance_limit) {
            indexed_primitives_within_distance_squared_recurisve(distancer, left_node_idx, squared_distance_limit, found_primitives_indices);
        }
        if (node_right.bbox.squaredExteriorDistance(distancer.origin) < squared_distance_limit) {
            indexed_primitives_within_distance_squared_recurisve(distancer, right_node_idx, squared_distance_limit,
                                                                 found_primitives_indices);
        }
    }
}

} // namespace detail

// Build a balanced AABB Tree over an indexed triangles set, balancing the tree
// on centroids of the triangles.
// Epsilon is applied to the bounding boxes of the AABB Tree to cope with numeric inaccuracies
// during tree traversal.
// [INTENT] Constructs a 3D Tree<float> or Tree<double> from an indexed triangle set.  Each
// triangle's centroid is used for the QuickSelect median split; its axis-aligned bounding box
// (optionally expanded by eps on all 6 faces) is stored for fast box tests during traversal.
// [MEMORY] The InputType helper struct lives only in this function scope; the returned Tree<3,Scalar>
// owns the final m_nodes vector.  Caller owns the tree.
// [HAZARD] H716 — eps parameter defaults to 0 (no epsilon expansion).  For meshes with very thin
// triangles or near-zero-area faces, eps=0 can cause ray-miss artifacts during traversal.
// The FIXME comment in the source acknowledges this is unresolved.
template<typename VertexType, typename IndexedFaceType>
inline Tree<3, typename VertexType::Scalar> build_aabb_tree_over_indexed_triangle_set(
    // Indexed triangle set - 3D vertices.
    const std::vector<VertexType>& vertices,
    // Indexed triangle set - triangular faces, references to vertices.
    const std::vector<IndexedFaceType>& faces,
    // FIXME do we want to apply an epsilon?
    const typename VertexType::Scalar eps = 0)
{
    using TreeType = Tree<3, typename VertexType::Scalar>;
    //    using				 CoordType      = typename TreeType::CoordType;
    using VectorType  = typename TreeType::VectorType;
    using BoundingBox = typename TreeType::BoundingBox;

    struct InputType
    {
        size_t             idx() const { return m_idx; }
        const BoundingBox& bbox() const { return m_bbox; }
        const VectorType&  centroid() const { return m_centroid; }

        size_t      m_idx;
        BoundingBox m_bbox;
        VectorType  m_centroid;
    };

    std::vector<InputType> input;
    input.reserve(faces.size());
    const VectorType veps(eps, eps, eps);
    for (size_t i = 0; i < faces.size(); ++i) {
        const IndexedFaceType& face = faces[i];
        const VertexType&      v1   = vertices[face(0)];
        const VertexType&      v2   = vertices[face(1)];
        const VertexType&      v3   = vertices[face(2)];
        InputType              n;
        n.m_idx      = i;
        n.m_centroid = (1. / 3.) * (v1 + v2 + v3);
        n.m_bbox     = BoundingBox(v1, v1);
        n.m_bbox.extend(v2);
        n.m_bbox.extend(v3);
        n.m_bbox.min() -= veps;
        n.m_bbox.max() += veps;
        input.emplace_back(n);
    }

    TreeType out;
    out.build(std::move(input));
    return out;
}

// Find a first intersection of a ray with indexed triangle set.
// Intersection test is calculated with the accuracy of VectorType::Scalar
// even if the triangle mesh and the AABB Tree are built with floats.
// [INTENT] Public entry point for first-hit ray query.  Builds a RayIntersector on the stack
// (no heap allocation), precomputes invdir once, then calls the recursive traversal.
// Returns false immediately if tree is empty.
// [STATE] hit is an output parameter (igl::Hit struct: face_id, u, v, t).  The caller must
// provide storage; it is overwritten only if a hit is found.
template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool intersect_ray_first_hit(
    // Indexed triangle set - 3D vertices.
    const std::vector<VertexType>& vertices,
    // Indexed triangle set - triangular faces, references to vertices.
    const std::vector<IndexedFaceType>& faces,
    // AABBTreeIndirect::Tree over vertices & faces, bounding boxes built with the accuracy of vertices.
    const TreeType& tree,
    // Origin of the ray.
    const VectorType& origin,
    // Direction of the ray.
    const VectorType& dir,
    // First intersection of the ray with the indexed triangle set.
    igl::Hit& hit,
    // Epsilon for the ray-triangle intersection, it should be proportional to an average triangle edge length.
    const double eps = 0.000001)
{
    using Scalar         = typename VectorType::Scalar;
    auto ray_intersector = detail::RayIntersector<VertexType, IndexedFaceType, TreeType, VectorType>{vertices,
                                                                                                     faces,
                                                                                                     tree,
                                                                                                     origin,
                                                                                                     dir,
                                                                                                     VectorType(dir.cwiseInverse()),
                                                                                                     eps};
    return !tree.empty() &&
           detail::intersect_ray_recursive_first_hit(ray_intersector, size_t(0), std::numeric_limits<Scalar>::infinity(), hit);
}

// Find all intersections of a ray with indexed triangle set.
// Intersection test is calculated with the accuracy of VectorType::Scalar
// even if the triangle mesh and the AABB Tree are built with floats.
// The output hits are sorted by the ray parameter.
// [INTENT] Public entry point for all-hits ray query.  Reuses the caller-provided `hits` vector
// buffer (via std::move) to avoid repeated allocation across multiple calls.  Accumulates hits
// in arbitrary order, then sorts by t.
// [STATE] `hits` is an in/out parameter: its memory is reused but its contents are replaced.
// If no hits are found, `hits` is left empty.
// [COUPLING] Depends on std::sort with lambda comparator — standard library sort, no SIMD.
// If the ray intersects a shared edge of two triangles, hits for both triangles are returned.
template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool intersect_ray_all_hits(
    // Indexed triangle set - 3D vertices.
    const std::vector<VertexType>& vertices,
    // Indexed triangle set - triangular faces, references to vertices.
    const std::vector<IndexedFaceType>& faces,
    // AABBTreeIndirect::Tree over vertices & faces, bounding boxes built with the accuracy of vertices.
    const TreeType& tree,
    // Origin of the ray.
    const VectorType& origin,
    // Direction of the ray.
    const VectorType& dir,
    // All intersections of the ray with the indexed triangle set, sorted by parameter t.
    std::vector<igl::Hit>& hits,
    // Epsilon for the ray-triangle intersection, it should be proportional to an average triangle edge length.
    const double eps = 0.000001)
{
    auto ray_intersector = detail::RayIntersectorHits<VertexType, IndexedFaceType, TreeType, VectorType>{
        {vertices, faces, {tree}, origin, dir, VectorType(dir.cwiseInverse()), eps}};
    if (tree.empty()) {
        hits.clear();
    } else {
        // Reusing the output memory if there is some memory already pre-allocated.
        ray_intersector.hits = std::move(hits);
        ray_intersector.hits.clear();
        ray_intersector.hits.reserve(8);
        detail::intersect_ray_recursive_all_hits(ray_intersector, 0);
        hits = std::move(ray_intersector.hits);
        std::sort(hits.begin(), hits.end(), [](const auto& l, const auto& r) { return l.t < r.t; });
    }
    return !hits.empty();
}

// Finding a closest triangle, its closest point and squared distance to the closest point
// on a 3D indexed triangle set using a pre-built AABBTreeIndirect::Tree.
// Closest point to triangle test will be performed with the accuracy of VectorType::Scalar
// even if the triangle mesh and the AABB Tree are built with floats.
// Returns squared distance to the closest point or -1 if the input is empty.
template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline typename VectorType::Scalar squared_distance_to_indexed_triangle_set(
    // Indexed triangle set - 3D vertices.
    const std::vector<VertexType>& vertices,
    // Indexed triangle set - triangular faces, references to vertices.
    const std::vector<IndexedFaceType>& faces,
    // AABBTreeIndirect::Tree over vertices & faces, bounding boxes built with the accuracy of vertices.
    const TreeType& tree,
    // Point to which the closest point on the indexed triangle set is searched for.
    const VectorType& point,
    // Index of the closest triangle in faces.
    size_t& hit_idx_out,
    // Position of the closest point on the indexed triangle set.
    Eigen::PlainObjectBase<VectorType>& hit_point_out)
{
    using Scalar   = typename VectorType::Scalar;
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>{vertices, faces, tree, point};
    return tree.empty() ? Scalar(-1) :
                          detail::squared_distance_to_indexed_primitives_recursive(distancer, size_t(0), Scalar(0),
                                                                                   std::numeric_limits<Scalar>::infinity(), hit_idx_out,
                                                                                   hit_point_out);
}

// Decides if exists some triangle in defined radius on a 3D indexed triangle set using a pre-built AABBTreeIndirect::Tree.
// Closest point to triangle test will be performed with the accuracy of VectorType::Scalar
// even if the triangle mesh and the AABB Tree are built with floats.
// Returns true if exists some triangle in defined radius, false otherwise.
template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline bool is_any_triangle_in_radius(
    // Indexed triangle set - 3D vertices.
    const std::vector<VertexType>& vertices,
    // Indexed triangle set - triangular faces, references to vertices.
    const std::vector<IndexedFaceType>& faces,
    // AABBTreeIndirect::Tree over vertices & faces, bounding boxes built with the accuracy of vertices.
    const TreeType& tree,
    // Point to which the closest point on the indexed triangle set is searched for.
    const VectorType& point,
    // Square of maximum distance in which triangle is searched for
    typename VectorType::Scalar& max_distance_squared)
{
    using Scalar   = typename VectorType::Scalar;
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>{vertices, faces, tree, point};

    size_t     hit_idx;
    VectorType hit_point = VectorType::Ones() * (NaN<typename VectorType::Scalar>);

    if (tree.empty()) {
        return false;
    }

    detail::squared_distance_to_indexed_primitives_recursive(distancer, size_t(0), Scalar(0), max_distance_squared, hit_idx, hit_point);

    return hit_point.allFinite();
}

// Returns all triangles within the given radius limit
template<typename VertexType, typename IndexedFaceType, typename TreeType, typename VectorType>
inline std::vector<size_t> all_triangles_in_radius(
    // Indexed triangle set - 3D vertices.
    const std::vector<VertexType>& vertices,
    // Indexed triangle set - triangular faces, references to vertices.
    const std::vector<IndexedFaceType>& faces,
    // AABBTreeIndirect::Tree over vertices & faces, bounding boxes built with the accuracy of vertices.
    const TreeType& tree,
    // Point to which the distances on the indexed triangle set is searched for.
    const VectorType& point,
    // Square of maximum distance in which triangles are searched for
    typename VectorType::Scalar max_distance_squared)
{
    auto distancer = detail::IndexedTriangleSetDistancer<VertexType, IndexedFaceType, TreeType, VectorType>{vertices, faces, tree, point};

    if (tree.empty()) {
        return {};
    }

    std::vector<size_t> found_triangles{};
    detail::indexed_primitives_within_distance_squared_recurisve(distancer, size_t(0), max_distance_squared, found_triangles);
    return found_triangles;
}

// Traverse the tree and return the index of an entity whose bounding box
// contains a given point. Returns size_t(-1) when the point is outside.
// [INTENT] Descends the implicit tree collecting all leaf indices whose bounding boxes contain v.
// Unlike the ray/distance queries, this returns all overlapping boxes, not just the closest.
// This is typically used for fast candidate filtering before exact intersection tests.
// [COUPLING] `candidates` is an output-parameter vector passed by non-const reference — caller
// must pre-clear it or accept appending to an existing list.
template<typename TreeType, typename VectorType>
void get_candidate_idxs(const TreeType& tree, const VectorType& v, std::vector<size_t>& candidates, size_t node_idx = 0)
{
    if (tree.empty() || !tree.node(node_idx).bbox.contains(v))
        return;

    decltype(tree.node(node_idx)) node = tree.node(node_idx);
    static_assert(std::is_reference<decltype(node)>::value, "Nodes shall be addressed by reference.");
    assert(node.is_valid());
    assert(node.bbox.contains(v));

    if (!node.is_leaf()) {
        if (tree.left_child(node_idx).bbox.contains(v))
            get_candidate_idxs(tree, v, candidates, tree.left_child_idx(node_idx));
        if (tree.right_child(node_idx).bbox.contains(v))
            get_candidate_idxs(tree, v, candidates, tree.right_child_idx(node_idx));
    } else
        candidates.push_back(node.idx);

    return;
}

// Predicate: need to be specialized for intersections of different geomteries
// [INTENT] Policy-based predicate system for traverse().  Intersecting<G> prunes tree nodes
// whose bounding boxes do not intersect the query geometry G.  Within<G> prunes nodes not
// contained within G (with a relaxed intersects() test at inner nodes to avoid over-pruning).
// These are value types — cheap to copy, hold only an AlignedBox by value.
// [COUPLING] traverse() accepts any callable predicate; these structs are the canonical set.
// Custom predicates for e.g. sphere/ray can be added without modifying the traversal core.
template<class G> struct Intersecting
{};

// Intersection predicate specialization for box-box intersections
template<class CoordType, int NumD> struct Intersecting<Eigen::AlignedBox<CoordType, NumD>>
{
    Eigen::AlignedBox<CoordType, NumD> box;

    Intersecting(const Eigen::AlignedBox<CoordType, NumD>& bb) : box{bb} {}

    bool operator()(const typename Tree<NumD, CoordType>::Node& node) const { return box.intersects(node.bbox); }
};

template<class G> auto intersecting(const G& g) { return Intersecting<G>{g}; }

template<class G> struct Within
{};

// Intersection predicate specialization for box-box intersections
template<class CoordType, int NumD> struct Within<Eigen::AlignedBox<CoordType, NumD>>
{
    Eigen::AlignedBox<CoordType, NumD> box;

    Within(const Eigen::AlignedBox<CoordType, NumD>& bb) : box{bb} {}

    bool operator()(const typename Tree<NumD, CoordType>::Node& node) const
    {
        return node.is_leaf() ? box.contains(node.bbox) : box.intersects(node.bbox);
    }
};

template<class G> auto within(const G& g) { return Within<G>{g}; }

namespace detail {

// Returns true in case traversal should continue,
// returns false if traversal should stop (for example if the first hit was found).
template<int Dims, typename T, typename Pred, typename Fn>
bool traverse_recurse(const Tree<Dims, T>& tree, size_t idx, Pred&& pred, Fn&& callback)
{
    assert(tree.node(idx).is_valid());

    if (!pred(tree.node(idx)))
        // Continue traversal.
        return true;

    if (tree.node(idx).is_leaf()) {
        // Callback returns true to continue traversal, false to stop traversal.
        return callback(tree.node(idx));
    } else {
        // call this with left and right node idx:
        auto trv = [&](size_t idx) -> bool { return traverse_recurse(tree, idx, std::forward<Pred>(pred), std::forward<Fn>(callback)); };

        // Left / right child node index.
        // Returns true if both children allow the traversal to continue.
        return trv(Tree<Dims, T>::left_child_idx(idx)) && trv(Tree<Dims, T>::right_child_idx(idx));
    }
}

} // namespace detail

// Tree traversal with a predicate. Example usage:
// traverse(tree, intersecting(QueryBox), [](size_t face_idx) {
//      /* ... */
// });
// Callback shall return true to continue traversal, false if it wants to stop traversal, for example if it found the answer.
// [INTENT] General-purpose traversal that separates the pruning predicate from the per-leaf
// callback.  Predicate is tested on every node (inner and leaf); if it returns false, the entire
// subtree is skipped.  Callback is invoked only at leaves and returns true to continue, false to
// stop.  This allows implementing early-exit queries (first-hit) as well as exhaustive queries
// (all-hits) with the same traversal skeleton.
// [COUPLING] The Intersecting<Box> and Within<Box> predicate structs are defined in this file
// and are the canonical usage pattern.  Custom predicates must accept Tree::Node by const ref.
template<int Dims, typename T, typename Predicate, typename Fn> void traverse(const Tree<Dims, T>& tree, Predicate&& pred, Fn&& callback)
{
    if (tree.empty())
        return;

    detail::traverse_recurse(tree, size_t(0), std::forward<Predicate>(pred), std::forward<Fn>(callback));
}

}} // namespace Slic3r::AABBTreeIndirect

#endif /* slic3r_AABBTreeIndirect_hpp_ */
