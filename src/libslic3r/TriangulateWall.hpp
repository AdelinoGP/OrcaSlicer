#ifndef TRIANGULATEWALL_HPP
#define TRIANGULATEWALL_HPP

#include "libslic3r/Polygon.hpp"

// [INTENT] Wall triangulation - creates 3D mesh from two 2D polygon contours at different heights.
// Used for generating variable-layer-height meshes and walls between print layers.
// [COUPLING] Depends on Polygon from libslic3r, Eigen for Vec types.
// [ALGORITHM] Uses ring synchronization algorithm to match vertices between upper/lower contours.

namespace Slic3r {

namespace trianglulate_wall_detail {

// [INTENT] Ring iterator for traversing polygon contours during triangulation.
// Maintains current position and wraps around at ring boundaries.
class Ring
{
    size_t idx = 0, nextidx = 1, startidx = 0, begin = 0, end = 0;

public:
    explicit Ring(size_t from, size_t to) : begin(from), end(to) { init(begin); }

    size_t                    size() const { return end - begin; }
    std::pair<size_t, size_t> pos() const { return {idx, nextidx}; }
    bool                      is_lower() const { return idx < size(); }

    // [INTENT] Advance ring iterator to next position, wrapping at boundaries.
    void inc()
    {
        if (nextidx != startidx)
            nextidx++;
        if (nextidx == end)
            nextidx = begin;
        idx++;
        if (idx == end)
            idx = begin;
    }

    void init(size_t pos)
    {
        startidx = begin + (pos - begin) % size();
        idx      = startidx;
        nextidx  = begin + (idx + 1 - begin) % size();
    }

    bool is_finished() const { return nextidx == idx; }
};

// [INTENT] Squared distance helper - avoids sqrt for performance.
// Note: z component excluded (commented out) - 2.5D distance only.
template<class Sc> static Sc sq_dst(const Vec<3, Sc>& v1, const Vec<3, Sc>& v2)
{
    Vec<3, Sc> v = v1 - v2;
    return v.x() * v.x() + v.y() * v.y() /*+ v.z() * v.z()*/;
}

// [INTENT] Calculate matching score between two ring positions.
// Used to find optimal vertex correspondence between upper and lower contours.
template<class Sc> static Sc trscore(const Ring& onring, const Ring& offring, const std::vector<Vec<3, Sc>>& pts)
{
    Sc a = sq_dst(pts[onring.pos().first], pts[offring.pos().first]);
    Sc b = sq_dst(pts[onring.pos().second], pts[offring.pos().first]);
    return (std::abs(a) + std::abs(b)) / 2.;
}

// [INTENT] Triangulator class - synchronizes upper/lower rings and produces triangle indices.
// Implements greedy algorithm: at each step, choose the ring advance that minimizes
// the geometric distortion (matching score).
template<class Sc> class Triangulator
{
    const std::vector<Vec<3, Sc>>* pts;
    Ring *                         onring, *offring;

    double calc_score() const { return trscore(*onring, *offring, *pts); }

    // [INTENT] Synchronize rings by finding optimal starting position for offring.
    // Scans all possible starting positions and picks the one with minimum score.
    void synchronize_rings()
    {
        Ring   lring = *offring;
        auto   minsc = trscore(*onring, lring, *pts);
        size_t imin  = lring.pos().first;

        lring.inc();

        while (!lring.is_finished()) {
            double score = trscore(*onring, lring, *pts);
            if (score < minsc) {
                minsc = score;
                imin  = lring.pos().first;
            }
            lring.inc();
        }

        offring->init(imin);
    }

    // [INTENT] Emit one triangle using current ring positions.
    void emplace_indices(std::vector<Vec3i32>& indices)
    {
        Vec3i32 tr{int(onring->pos().first), int(onring->pos().second), int(offring->pos().first)};
        if (onring->is_lower())
            std::swap(tr(0), tr(1));
        indices.emplace_back(tr);
    }

public:
    // [INTENT] Main triangulation loop - greedy vertex matching.
    // [ALGORITHM] O(n+m) where n,m are ring sizes. At each step advances either
    // onring or offring based on which produces lower matching score.
    void run(std::vector<Vec3i32>& indices)
    {
        synchronize_rings();

        double score = 0, prev_score = 0;
        while (!onring->is_finished() || !offring->is_finished()) {
            prev_score = score;
            if (onring->is_finished() || (score = calc_score()) > prev_score) {
                std::swap(onring, offring);
            } else {
                emplace_indices(indices);
                onring->inc();
            }
        }
    }

    explicit Triangulator(const std::vector<Vec<3, Sc>>* points, Ring& lower, Ring& upper) : pts{points}, onring{&upper}, offring{&lower} {}
};

} // namespace trianglulate_wall_detail

// [INTENT] Main triangulation function - creates 3D wall mesh from two 2D polygons.
// [ALGORITHM] Uses greedy ring-synchronization: matches vertices between contours
// by minimizing geometric distortion at each step.
// [MEMORY] Pre-allocates vectors with reserve() for efficiency.
template<class Sc, class I>
void triangulate_wall(std::vector<Vec<3, Sc>>& pts,
                      std::vector<Vec<3, I>>&  ind,
                      const Polygon&           lower,
                      const Polygon&           upper,
                      double                   lower_z_mm,
                      double                   upper_z_mm)
{
    using namespace trianglulate_wall_detail;

    // [INTENT] Validate input - both polygons must have at least 3 vertices.
    if (upper.points.size() < 3 || lower.points.size() < 3)
        return;

    // [INTENT] Flatten 2D polygon points into 3D, assigning Z coordinates.
    pts.reserve(lower.points.size() + upper.points.size());
    for (auto& p : lower.points)
        pts.emplace_back(unscaled(p.x()), unscaled(p.y()), lower_z_mm);
    for (auto& p : upper.points)
        pts.emplace_back(unscaled(p.x()), unscaled(p.y()), upper_z_mm);

    // [INTENT] Pre-allocate index buffer - max triangles = 2 * (n + m).
    ind.reserve(2 * (lower.size() + upper.size()));

    // [INTENT] Create ring iterators and run triangulation algorithm.
    Ring         lring{0, lower.points.size()}, uring{lower.points.size(), pts.size()};
    Triangulator t{&pts, lring, uring};
    t.run(ind);
}

// using Wall = std::pair<std::vector<Vec3d>, std::vector<Vec3i32>>;

// Wall triangulate_wall(
//     const Polygon &       lower,
//     const Polygon &       upper,
//     double                lower_z_mm,
//     double                upper_z_mm);
// }

} // namespace Slic3r

#endif // TRIANGULATEWALL_HPP
