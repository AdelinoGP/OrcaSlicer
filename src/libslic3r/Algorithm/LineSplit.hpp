// [INTENT] Declares `split_line` — a utility to split a polyline (or polygon) into segments
// that are either inside or outside a set of clipping ExPolygons, preserving per-point
// source-index annotations. Used by overhang detection and curled-line filtering to partition
// perimeter paths by region membership.
//
// [COUPLING] Depends on ClipperZUtils (ClipperLib_Z with Z-coordinate tagging) and ExPolygons.
//            The template `split_line` wrapper converts any PathType to a ZPath before calling
//            the internal `do_split_line` implementation.
// [CONCURRENCY] Stateless free functions; safe to call from TBB tasks.

#ifndef SRC_LIBSLIC3R_ALGORITHM_LINE_SPLIT_HPP_
#define SRC_LIBSLIC3R_ALGORITHM_LINE_SPLIT_HPP_

#include "ClipperZUtils.hpp"

namespace Slic3r { namespace Algorithm {

// [INTENT] Annotates a point along a split line.
// - `p`       : 2D position in coord_t scaled integer space.
// - `clipped` : true if the segment *from this point to the next* is inside the clip polygon.
//               After the last point in a clipped run, clipped is set to false.
// - `src_idx` : tracks origin:
//     >= 0  : index of the original input point (from source path)
//     < 0   : -(1 + index_of_first_point_of_edge) — intersection point, not in original path
// [COUPLING] The negative encoding mirrors the Z-channel encoding used inside LineSplit.cpp.
// Any port must replicate the -(src_idx+1) / -(p.z()+1) encoding exactly.
struct SplitLineJunction
{
    Point p;

    // true if the line between this point and the next point is inside the clip polygon (or on the edge of the clip polygon)
    bool clipped;

    // Index from the original input.
    // - If this junction is presented in the source polygon/polyline, this is the index of the point with in the source;
    // - if this point in a new point that caused by the intersection, this will be -(1+index of the first point of the source line involved
    // in this intersection);
    // - if this junction came from the clip polygon, it will be treated as new point.
    int64_t src_idx;

    SplitLineJunction(const Point& p, bool clipped, int64_t src_idx) : p(p), clipped(clipped), src_idx(src_idx) {}

    // [INTENT] Returns true if this junction came directly from the source path (not an intersection).
    bool is_src() const { return src_idx >= 0; }
    // [INTENT] Returns the source index (zero-based) regardless of sign encoding.
    // For intersection points, -src_idx - 1 recovers the first-point-of-edge index.
    size_t get_src_index() const
    {
        if (is_src()) {
            return src_idx;
        } else {
            return -src_idx - 1;
        }
    }
};

// [INTENT] A split line result: ordered list of junctions (original + intersection points),
// annotated with inside/outside flags. The `clipped` field of junction i covers the edge
// from junction[i].p to junction[i+1].p.
using SplittedLine = std::vector<SplitLineJunction>;

// [INTENT] Internal implementation (non-template). Takes a ZPath (already tagged with
// per-point Z indices) and a set of ExPolygon clip regions. Returns the SplittedLine.
// [HAZARD] H656: If the intersections vector is empty (no intersection between path and clip),
// do_split_line returns an empty SplittedLine. Callers that iterate result expecting at least
// one junction will silently skip all processing for that path.
SplittedLine do_split_line(const ClipperZUtils::ZPath& path, const ExPolygons& clip, bool closed);

// [INTENT] Template wrapper: converts any PathType (Polyline, Points, etc.) to a ZPath by
// assigning sequential Z values (0, 1, 2, ...) to each point, then calls do_split_line.
// For closed paths, the first point is duplicated at the end to "open" the closed contour
// before clipping; the duplicate is removed after processing.
// [HAZARD] H657: The reserve size calculation `path.size() + closed ? 1 : 0` has operator
// precedence issues: `closed ? 1 : 0` is evaluated first, then added to `path.size()`.
// Due to C++ operator precedence (`+` before `?:`), this is actually
// `(path.size() + closed) ? 1 : 0`. For any non-empty path, `path.size() + closed` is
// always truthy, so the reserve always allocates 1, not `path.size() + 1` or `path.size()`.
// This means the ZPath is UNDER-reserved; push_back calls will reallocate. The result is
// still correct (reserve is advisory) but performance is degraded.
// Return the splitted line, or empty if no intersection found
template<class PathType> SplittedLine split_line(const PathType& path, const ExPolygons& clip, bool closed)
{
    if (path.empty()) {
        return {};
    }

    // Convert the input path into an open ZPath
    ClipperZUtils::ZPath p;
    p.reserve(path.size() + closed ? 1 : 0);
    ClipperLib_Z::cInt z = 0;
    for (const auto& point : path) {
        p.emplace_back(point.x(), point.y(), z);
        z++;
    }
    if (closed) {
        // duplicate the first point at the end to make a closed path open
        p.emplace_back(p.front());
        p.back().z() = z;
    }

    return do_split_line(p, clip, closed);
}

}} // namespace Slic3r::Algorithm

#endif /* SRC_LIBSLIC3R_ALGORITHM_LINE_SPLIT_HPP_ */
