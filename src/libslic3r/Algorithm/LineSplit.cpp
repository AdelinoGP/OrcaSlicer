// [INTENT] LineSplit.cpp — implementation of the path-clipping split algorithm.
// A subject polyline (open path) is clipped against a set of ExPolygons using
// ClipperLib_Z (the Z-coordinate variant of Clipper), which tags each intersection
// point with Z metadata so that the clipped segments can be reordered and reconnected
// back onto the original path index sequence.
//
// [MEMORY] Uses ClipperLib_Z::ZPath (Eigen-based 3D integer vector) throughout; these
// carry (x, y, z) where z encodes source-path index or clip-origin flags.
// All intermediate ZPath/ZPaths are heap-allocated; the final SplittedLine result
// is a flat std::vector<SplitLineJunction> returned by value.
//
// [CONCURRENCY] No shared mutable state in this translation unit (g_dbg_id is only
// compiled in DEBUG_SPLIT_LINE builds). Thread-safe as a pure function.

#include "LineSplit.hpp"

#include "AABBTreeLines.hpp"
#include "SVG.hpp"
#include "Utils.hpp"

// #define DEBUG_SPLIT_LINE

namespace Slic3r { namespace Algorithm {

#ifdef DEBUG_SPLIT_LINE
// [STATE] g_dbg_id: atomic counter for debug SVG filenames. Not present in release builds.
static std::atomic<std::uint32_t> g_dbg_id = 0;
#endif

// [INTENT] CLIP_IDX: sentinel Z value assigned to all points that originate from the
// clip polygon (ExPolygon contours/holes). Distinguishes clip-origin points from
// source-path points (Z >= 0, sequential index) and intersection points (Z < 0).
// Uses the maximum value of ClipperLib_Z::cInt so it cannot collide with any
// negative intersection encoding or valid non-negative source index.
// [HAZARD H658] CLIP_IDX == numeric_limits<cInt>::max(). If a source path ever has
// exactly max(cInt) points, the last point's Z index would equal CLIP_IDX — a
// collision. In practice source paths are never that large, but the implicit contract
// is fragile and undocumented in the header.
static constexpr auto CLIP_IDX = std::numeric_limits<ClipperLib_Z::cInt>::max();

// [INTENT] cb_split_line: Clipper Z-fill callback invoked for every intersection event.
// Given the four endpoint Z values of the two intersecting segments (e1bot/e1top,
// e2bot/e2top), it encodes the intersection point's Z as -(min_Z + 1).
// The minimum Z across the four endpoints is the earliest source-path index involved
// in the intersection — the negative encoding marks it as a "new" (intersection) point,
// and the embedded value (-z-1) recovers the source edge start index.
// [COUPLING] This encoding is the contract between cb_split_line and is_new()/to_src_idx().
// The three predicates is_src, is_clip, is_new and the extractor to_src_idx must all
// agree on the Z encoding; changing the formula here breaks the reconstruction loop.
static void cb_split_line(const ClipperZUtils::ZPoint& e1bot,
                          const ClipperZUtils::ZPoint& e1top,
                          const ClipperZUtils::ZPoint& e2bot,
                          const ClipperZUtils::ZPoint& e2top,
                          ClipperZUtils::ZPoint&       pt)
{
    coord_t zs[4]{e1bot.z(), e1top.z(), e2bot.z(), e2top.z()};
    std::sort(zs, zs + 4);
    // [INTENT] Encode as negative: -(minimum_z + 1). Recoverable via to_src_idx().
    pt.z() = -(zs[0] + 1);
}

// [INTENT] Z-tag predicates for classifying ZPoints after clipping:
//   is_src  : originated from the subject path (Z ∈ [0, CLIP_IDX-1])
//   is_clip : originated from a clip polygon contour/hole (Z == CLIP_IDX)
//   is_new  : created at an intersection event (Z < 0)
// [COUPLING] These predicates and to_src_idx() are the decoding side of cb_split_line.
// Must remain consistent with the cb_split_line encoding.
static bool is_src(const ClipperZUtils::ZPoint& p) { return p.z() >= 0 && p.z() != CLIP_IDX; }
static bool is_clip(const ClipperZUtils::ZPoint& p) { return p.z() == CLIP_IDX; }
static bool is_new(const ClipperZUtils::ZPoint& p) { return p.z() < 0; }

// [INTENT] Recover the source path index from a ZPoint regardless of whether it is
// a src point (direct Z value) or a new intersection point (recover via -z-1).
// [HAZARD H659] Called for both is_src and is_new points; asserts that is_clip points
// are never passed in. If called on a clip point (z == CLIP_IDX) after the assert
// is stripped in release, it returns CLIP_IDX — a large positive index — which would
// silently corrupt the split_chain lookup.
static size_t to_src_idx(const ClipperZUtils::ZPoint& p)
{
    assert(!is_clip(p));
    if (is_src(p)) {
        return p.z();
    } else {
        // [INTENT] Intersection point: recover the source edge start index.
        return -p.z() - 1;
    }
}

// [INTENT] Project a ZPoint to a plain 2D Point (discard the Z tag).
static Point to_point(const ClipperZUtils::ZPoint& p) { return {p.x(), p.y()}; }

// [INTENT] SplitNode: for each source path vertex, the list of clipped segments
// (ZPath pointers) whose first point lies on or near that vertex's outgoing edge.
// The outer vector is indexed by source path vertex index; each entry is ordered by
// increasing distance from the source vertex along its outgoing edge.
using SplitNode = std::vector<ClipperZUtils::ZPath*>;

// [INTENT] point_on_line: strict interior collinearity test (endpoints excluded).
// First checks cross-product == 0 (collinear), then checks strict between-ness along
// whichever axis has larger extent.
// [HAZARD H660] The cross-product `d1.x() * d2.y() - d1.y() * d2.x()` is computed in
// coord_t arithmetic (int64_t). For coordinates near ±2^31 (common in scaled geometry),
// the intermediate products can overflow int64_t, producing a false "non-collinear"
// result and causing a valid intersection point to be misclassified.
// [HAZARD H661] The between-ness test `(p.x() > l.a.x()) == (p.x() < l.b.x())` is
// correct only when l.a.x() < p.x() < l.b.x() or l.b.x() < p.x() < l.a.x(). It
// returns false for the endpoint-coincident case (p == l.a or p == l.b), consistent
// with the comment "p cannot be one of the line end". If callers ever pass an endpoint,
// the result is silently wrong.
// Note: p cannot be one of the line end
static bool point_on_line(const Point& p, const Line& l)
{
    // Check collinear
    const Vec2crd d1 = l.b - l.a;
    const Vec2crd d2 = p - l.a;
    if (d1.x() * d2.y() != d1.y() * d2.x()) {
        return false;
    }

    // Make sure p is in between line.a and line.b
    if (l.a.x() != l.b.x())
        return (p.x() > l.a.x()) == (p.x() < l.b.x());
    else
        return (p.y() > l.a.y()) == (p.y() < l.b.y());
}

// [INTENT] do_split_line: core implementation — clips a ZPath subject against ExPolygons,
// then reconstructs an ordered SplittedLine (flat junction list) from the Clipper output.
//
// Algorithm in four phases:
//   1. Clipper intersection: subject ZPath vs clip contours, using cb_split_line as Z-fill.
//      Output: a set of clipped ZPath segments, each a contiguous run of the subject
//      that lies inside the clip region.
//   2. AABB-tree resolution: clip-origin points (Z == CLIP_IDX) are re-tagged by finding
//      their source edge via AABBTreeLines nearest-line search, then re-encoding using
//      the same negative convention as cb_split_line.
//   3. Sorting: within each clipped segment, points are sorted by increasing source index
//      (and by distance from the source vertex for same-index intersection points).
//   4. Chain reconstruction: split_chain[i] holds the ordered list of clipped segments
//      starting from or near source vertex i. The final SplittedLine is built by
//      iterating source path vertices in order and emitting points from each segment.
//
// [STATE] `split_chain`: vector of SplitNode (indexed by source path vertex).
//         `aabb_tree`: lazily constructed on first clip-origin point encountered.
//         `result`: the returned SplittedLine, built incrementally.
//
// [MEMORY] ZPath segments are stored in `intersections` (heap). split_chain holds raw
// pointers into those segments — do NOT move or reallocate `intersections` after
// split_chain is populated.
//
// [HAZARD H662] If `clip` contains ExPolygons whose holes are wound CCW (should be CW),
// Clipper's nonzero fill rule will treat them as additional filled regions rather than
// holes. The intersection result will include segments inside holes. The algorithm
// performs no winding validation before passing clip paths to Clipper.
//
// [HAZARD H663] `zclipper.PreserveCollinear(true)` is set to prevent Clipper from
// removing collinear points. Without this, source vertices that lie exactly on a clip
// boundary edge could be removed, breaking the Z index chain. However, PreserveCollinear
// may produce duplicate consecutive points in the output — the reconstruction loop does
// not explicitly deduplicate, so the SplittedLine result can have zero-length segments.
//
// [HAZARD H664] `ClipperLib_Z::PolyTreeToPaths` flattens the poly tree into a flat list
// of ZPaths. Open paths in a PolyTree are stored as direct children of the root; closed
// paths appear nested. Since the subject is open (ptSubject with `closed=false`),
// all output paths should be open and at root level. If a future change accidentally
// adds a closed subject, some segments could be lost (nested in the tree).
SplittedLine do_split_line(const ClipperZUtils::ZPath& path, const ExPolygons& clip, bool closed)
{
    assert(path.size() > 1);
#ifdef DEBUG_SPLIT_LINE
    const auto  dbg_path_points = ClipperZUtils::from_zpath<false>(path);
    BoundingBox dbg_bbox        = get_extents(clip);
    dbg_bbox.merge(get_extents(dbg_path_points));
    dbg_bbox.offset(scale_(1.));
    const std::uint32_t dbg_id = g_dbg_id++;
    {
        ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_input.svg", dbg_id).c_str(), dbg_bbox);
        svg.draw(clip, "red", 0.5);
        svg.draw_outline(clip, "red");
        svg.draw(Polyline{dbg_path_points});
        svg.draw(dbg_path_points);
        svg.Close();
    }
#endif

    // [INTENT] Phase 1: Clipper Z intersection.
    // Subject = the input ZPath (open). Clip = all ExPolygon contours and holes
    // converted to closed ZPaths with Z == CLIP_IDX.
    ClipperZUtils::ZPaths intersections;
    // Perform an intersection
    {
        // Convert clip polygon to closed contours
        // [INTENT] Both contours and holes are added as clip paths. Clipper nonzero
        // fill determines which regions are "inside". Holes (CW wound) are correctly
        // treated as subtractive under nonzero fill only if wound consistently.
        ClipperZUtils::ZPaths clip_path;
        for (const auto& exp : clip) {
            clip_path.emplace_back(ClipperZUtils::to_zpath<false>(exp.contour.points, CLIP_IDX));
            for (const Polygon& hole : exp.holes)
                clip_path.emplace_back(ClipperZUtils::to_zpath<false>(hole.points, CLIP_IDX));
        }

        ClipperLib_Z::Clipper zclipper;
        zclipper.PreserveCollinear(true); // [SEE H663] prevents removal of on-boundary vertices
        zclipper.ZFillFunction(cb_split_line);
        zclipper.AddPaths(clip_path, ClipperLib_Z::ptClip, true);
        zclipper.AddPath(path, ClipperLib_Z::ptSubject, false); // open subject
        ClipperLib_Z::PolyTree polytree;
        zclipper.Execute(ClipperLib_Z::ctIntersection, polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
        ClipperLib_Z::PolyTreeToPaths(std::move(polytree), intersections); // [SEE H664]
    }
    // [INTENT] Early exit: if no intersection found, return empty SplittedLine.
    // [SEE H656 in LineSplit.hpp] callers must handle empty result.
    if (intersections.empty()) {
        return {};
    }

#ifdef DEBUG_SPLIT_LINE
    {
        int i = 0;
        for (const auto& segment : intersections) {
            ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_seg_%d.svg", dbg_id, i).c_str(), dbg_bbox);
            svg.draw(clip, "red", 0.5);
            svg.draw_outline(clip, "red");
            const auto segment_points = ClipperZUtils::from_zpath<false>(segment);
            svg.draw(Polyline{segment_points});
            for (const ClipperZUtils::ZPoint& p : segment) {
                const auto z = p.z();
                if (is_new(p)) {
                    svg.draw(to_point(p), "yellow");
                } else if (is_clip(p)) {
                    svg.draw(to_point(p), "red");
                } else {
                    svg.draw(to_point(p), "black");
                }
            }
            svg.Close();
            i++;
        }
    }
#endif

    // [INTENT] Phase 2 & 3: Re-tag clip-origin points, sort each segment by source index.
    // Phase 4: populate split_chain[source_vertex_index] → list of segment pointers.
    std::vector<SplitNode> split_chain;
    {
        // [INTENT] AABBTree over source path edges — lazily constructed only when needed.
        // A clip-origin point (Z == CLIP_IDX) needs to be assigned to the source edge
        // it lies on; the AABB tree provides candidate edges within SCALED_EPSILON distance.
        // [HAZARD H665] SCALED_EPSILON (1e6 * 1e-4 = 100 nm) is used as the search radius.
        // For short edges (< 200 nm), an intersection point exactly at the midpoint lies
        // within SCALED_EPSILON of BOTH endpoints. The AABB search returns multiple candidates
        // and the code picks the first matching one — correct in practice but order-dependent.
        AABBTreeLines::LinesDistancer<Line> aabb_tree;
        const auto                          resolve_clip_point = [&path, &aabb_tree](ClipperZUtils::ZPoint& zp) {
            if (!is_clip(zp)) {
                return;
            }

            // [INTENT] Lazily build AABB tree of source path lines on first clip point.
            if (aabb_tree.get_lines().empty()) {
                Lines lines;
                lines.reserve(path.size() - 1);
                for (auto it = path.begin() + 1; it != path.end(); ++it) {
                    lines.emplace_back(to_point(it[-1]), to_point(*it));
                }
                aabb_tree = AABBTreeLines::LinesDistancer(lines);
            }

            const Point p              = to_point(zp);
            const auto  possible_edges = aabb_tree.all_lines_in_radius(p, SCALED_EPSILON);
            assert(!possible_edges.empty());
            for (const size_t l : possible_edges) {
                // Check if the point is on the line
                const Line line(to_point(path[l]), to_point(path[l + 1]));
                if (p == line.a) {
                    zp.z() = path[l].z();
                    break;
                }
                if (p == line.b) {
                    zp.z() = path[l + 1].z();
                    break;
                }
                if (point_on_line(p, line)) {
                    // [INTENT] Interior of edge l: encode as new-intersection-style Z.
                    // -(path[l].z() + 1) = -(source_index + 1) — same convention as cb_split_line.
                    zp.z() = -(path[l].z() + 1);
                    break;
                }
            }
            if (is_clip(zp)) {
                // [INTENT] Fallback: couldn't resolve to a source edge; use the closest
                // candidate and hope the result is geometrically close enough.
                // [HAZARD H666] This fallback is a silent approximation — the clip-origin
                // point is tagged with the first candidate edge's start index without
                // verifying it is actually on that edge. This can misorder the point in
                // the sort step (Phase 3), leading to an incorrectly sequenced SplittedLine.
                zp.z() = -(path[possible_edges[0]].z() + 1);
            }
        };

        // [INTENT] Initialize split_chain: one empty SplitNode per source path vertex.
        split_chain.assign(path.size(), {});
        for (ClipperZUtils::ZPath& segment : intersections) {
            assert(segment.size() >= 2);
            // [INTENT] Phase 2: resolve all clip-origin points in this segment.
            std::for_each(segment.begin(), segment.end(), resolve_clip_point);

            // [INTENT] Phase 3: sort segment points by ascending source index.
            // Tie-break: if two "new" points share the same source edge index, sort
            // by squared distance from the source vertex (nearer first).
            // [HAZARD H667] The sort comparator has a subtle tie-break path:
            // two is_src points at the same src index (can occur if Clipper emits a
            // degenerate zero-length segment) would compare as (is_src(a) → true vs
            // is_src(b) → true) → neither preferred, violating strict weak ordering.
            // std::sort with a non-strict-weak comparator is UB in C++.
            std::sort(segment.begin(), segment.end(), [&path](const ClipperZUtils::ZPoint& a, const ClipperZUtils::ZPoint& b) -> bool {
                if (is_new(a) && is_new(b) && a.z() == b.z()) {
                    // Make sure a point is closer to the src point than b
                    const auto src = to_point(path[-a.z() - 1]);
                    return (to_point(a) - src).squaredNorm() < (to_point(b) - src).squaredNorm();
                }
                const auto a_idx = to_src_idx(a);
                const auto b_idx = to_src_idx(b);
                if (a_idx == b_idx) {
                    // On same line, prefer the src point first
                    return is_src(a);
                } else {
                    return a_idx < b_idx;
                }
            });

            // [INTENT] Phase 4: insert the segment pointer into split_chain at the
            // correct source vertex index. The segment is kept in order of distance
            // along the outgoing edge so that multiple segments on the same edge are
            // emitted in the correct spatial order.
            ClipperZUtils::ZPoint&       front              = segment.front();
            const ClipperZUtils::ZPoint* previous_src_point = nullptr;
            if (is_src(front)) {
                // [INTENT] Segment starts at a source path vertex → insert at the
                // front of split_chain[front.z()]. Multiple segments starting at the
                // same source vertex are prepended in reverse; the overall order is
                // determined by the sort above.
                auto& node = split_chain[front.z()];
                node.insert(node.begin(), &segment);

                previous_src_point = &front;
            } else if (is_new(front)) {
                // [INTENT] Segment starts at an interior intersection point on edge [id, id+1].
                // Insert into split_chain[id] in order of increasing distance from path[id].
                const auto                   id    = -front.z() - 1;                // Get the src path index
                const ClipperZUtils::ZPoint& src_p = path[id];                      // Get the corresponding src point
                const auto dist2 = (front - src_p).block<2, 1>(0, 0).squaredNorm(); // Distance between the src point and current point
                // Find the place on the src line that current point should lie on
                auto& node = split_chain[id];
                auto  it   = std::find_if(node.begin(), node.end(), [dist2, &src_p](const ClipperZUtils::ZPath* p) {
                    const ClipperZUtils::ZPoint& p_front = p->front();
                    if (is_src(p_front)) {
                        return false;
                    }

                    const auto dist2_2 = (p_front - src_p).block<2, 1>(0, 0).squaredNorm();
                    return dist2_2 > dist2;
                });
                // Insert this split
                node.insert(it, &segment);

                previous_src_point = &src_p;
            } else {
                assert(false);
            }

            // [INTENT] Once the start point is classified, re-encode any remaining
            // clip-origin interior points using the previous_src_point convention.
            // Each clip-origin point in the segment body is tagged with -(previous_src_point->z()+1).
            for (ClipperZUtils::ZPoint& p : segment) {
                assert(!is_new(p) || p == front || p == segment.back()); // Only the first and last point can be a new intersection
                if (is_src(p)) {
                    previous_src_point = &p;
                } else if (is_clip(p)) {
                    // Treat point from clip polygon as new point
                    p.z() = -(previous_src_point->z() + 1);
                }
            }
        }
    }

    // [INTENT] Final reconstruction: walk split_chain in source-path-index order.
    // For each source vertex:
    //   - If split_chain[idx] is empty → emit the source vertex and advance.
    //   - Otherwise → emit the source vertex (if needed), then emit all points of all
    //     segments in split_chain[idx], then advance to the next source vertex determined
    //     by the last segment's tail.
    // [STATE] `result`: flat output list of SplitLineJunctions.
    // [HAZARD H668] The `back` index after segments are consumed drives the next `idx`.
    // If back < 0, the next idx is (-back - 1) or (-back - 1 + 1). If split_chain at
    // next_idx is empty, next_idx++ again. This two-level skip assumes at most one empty
    // node is skipped. If two consecutive source vertices have no segments AND the
    // segment tail lands exactly at one of them, a vertex can be silently omitted from
    // the result (gap in the SplittedLine output).
    SplittedLine result;
    size_t       idx = 0;
    while (idx < split_chain.size()) {
        const ClipperZUtils::ZPoint& p    = path[idx];
        const auto&                  node = split_chain[idx];
        if (node.empty()) {
            result.emplace_back(to_point(p), false, idx);
            idx++;
        } else {
            if (!is_src(node.front()->front())) {
                if (result.empty() || result.back().get_src_index() != to_src_idx(p)) {
                    // const auto& last = result.back();
                    // if (result.empty() || last.get_src_index() != to_src_idx(p)) {
                    result.emplace_back(to_point(p), false, idx);
                }
            }
            for (const auto segment : node) {
                for (const ClipperZUtils::ZPoint& sp : *segment) {
                    assert(!is_clip(sp));
                    // [INTENT] clipped=true marks that this point is inside the clip region.
                    result.emplace_back(to_point(sp), true, sp.z());
                }
                // [INTENT] The last point of each segment marks the end of a clipped run.
                result.back().clipped = false; // Mark the end of the clipped line
            }

            // Determine the next start point
            // [INTENT] The tail of the last emitted segment determines which source vertex
            // to continue from. Negative src_idx → intersection point on edge [next_idx, next_idx+1].
            const auto back = result.back().src_idx;
            if (back < 0) {
                auto next_idx = -back - 1;
                if (next_idx == idx) {
                    next_idx++;
                } else if (split_chain[next_idx].empty()) {
                    next_idx++;
                }
                idx = next_idx;
            } else {
                // [INTENT] Tail is a src vertex — pop it (it will be re-emitted as the
                // loop's own source vertex at the next iteration) and advance to it.
                result.pop_back();
                idx = back;
            }
        }
    }

#ifdef DEBUG_SPLIT_LINE
    {
        ::Slic3r::SVG svg(debug_out_path("do_split_line_%d_result.svg", dbg_id).c_str(), dbg_bbox);
        svg.draw(clip, "red", 0.5);
        svg.draw_outline(clip, "red");
        for (auto it = result.begin() + 1; it != result.end(); ++it) {
            const auto& a       = *(it - 1);
            const auto& b       = *it;
            const bool  clipped = a.clipped;
            const Line  l(a.p, b.p);
            svg.draw(l, clipped ? "yellow" : "black");
        }
        svg.Close();
    }
#endif

    if (closed) {
        // [INTENT] For closed paths, the first point was duplicated at the end in
        // split_line<PathType>() before calling do_split_line(). Remove the duplicate
        // closing point to restore the original cardinality.
        result.pop_back();
    }

    return result;
}

}} // namespace Slic3r::Algorithm
