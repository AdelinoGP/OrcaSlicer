#ifndef slic3r_ArcFitter_hpp_
#define slic3r_ArcFitter_hpp_

// [INTENT] ArcFitter converts dense polyline point sequences into compact arc
// (G2/G3) + line (G1) segment descriptions. The output is consumed by GCodeWriter
// to emit arc move commands instead of thousands of tiny linear moves, reducing
// file size and smoothing motion at high speeds.
// [COUPLING] Depends on Circle.hpp for ArcSegment / ArcDirection primitives, and
// on Point.hpp (coord_t scaled integers). Output PathFittingData is consumed by
// GCode.cpp / GCodeWriter.cpp for arc emission.

#include "Circle.hpp"

namespace Slic3r {

// [INTENT] Discriminated-union tag for path segment type, maps directly to
// G-code move types: G0/G1 = linear, G2 = CW arc, G3 = CCW arc.
// [MEMORY] Stored as unsigned char to keep PathFittingData compact.
// BBS: linear move(G0 and G1) or arc move(G2 and G3).
enum class EMovePathType : unsigned char { Noop_move, Linear_move, Arc_move_cw, Arc_move_ccw, Count };

// [INTENT] Describes one fitted segment span: a contiguous range of the original
// point array [start_point_index, end_point_index] plus the type and, for arcs,
// the geometric arc parameters (center, radius, direction, angles).
// [STATE] arc_data is only valid when path_type == Arc_move_cw/ccw; undefined
// for Linear_move and Noop_move segments — callers MUST check path_type first.
// [MEMORY] Embedded by value inside std::vector<PathFittingData>; ArcSegment
// carries two Points and four doubles, so each entry is ~80 bytes.
// BBS
struct PathFittingData
{
    size_t        start_point_index;
    size_t        end_point_index;
    EMovePathType path_type;
    // BBS: only valid when path_type is arc move
    // Used to store detail information of arc segment
    ArcSegment arc_data;

    bool is_linear_move() { return (path_type == EMovePathType::Linear_move); }
    bool is_arc_move() { return (path_type == EMovePathType::Arc_move_ccw || path_type == EMovePathType::Arc_move_cw); }
    // [INTENT] Reverses the arc direction in-place so the caller can traverse the
    // segment backwards (used when reversing travel order for optimisation).
    // [HAZARD] Mutates arc_data.direction; caller must also reverse the index range
    // separately — this function only flips geometry, not the index span.
    bool reverse_arc_path()
    {
        if (!is_arc_move() || !arc_data.reverse())
            return false;
        path_type = (arc_data.direction == ArcDirection::Arc_Dir_CCW) ? EMovePathType::Arc_move_ccw : EMovePathType::Arc_move_cw;
        return true;
    }
};

// [INTENT] Stateless utility class (all static methods). Provides two entry
// points for fitting a Points array: pure fitting and fitting-with-simplification.
// [CONCURRENCY] All methods are pure functions with no shared mutable state;
// safe to call from multiple TBB tasks concurrently as long as the caller
// provides separate output vectors.
class ArcFitter
{
public:
    // BBS: this function is used to check the point list and return which part can fit as arc, which part should be line
    //  [INTENT] Greedy left-to-right scan: expand a window until ArcSegment::try_create_arc
    //  fails, then commit the longest fitting arc and restart. Remaining straights are
    //  emitted as Linear_move segments. Result indices refer into the original `points` array.
    //  [STATE] result is fully cleared and rebuilt; caller should not pre-populate it.
    //  [COUPLING] Calls ArcSegment::try_create_arc (Circle.hpp) with DEFAULT_SCALED_MAX_RADIUS
    //  and DEFAULT_ARC_LENGTH_PERCENT_TOLERANCE constants.
    static void do_arc_fitting(const Points& points, std::vector<PathFittingData>& result, double tolerance);

    // BBS: this function is used to check the point list and return which part can fit as arc, which part should be line.
    // By the way, it also use DP simplify to reduce point of straight part and only keep the start and end point of arc.
    //  [INTENT] Two-pass pipeline: (1) arc-fit via do_arc_fitting, then (2) Douglas-Peucker
    //  simplification on each sub-segment independently. The `points` array is modified
    //  in-place to the simplified set, and `result` indices are remapped accordingly.
    //  [HAZARD] H761 — `points` is mutated (passed by non-const reference). After the call
    //  the original point array is gone; callers that hold iterators or indices into the
    //  original array will have dangling references. Must deep-copy before calling if the
    //  original is needed.
    //  [HAZARD] H762 — Index remapping (reduce_count prefix-sum) assumes the sub-segments
    //  returned by do_arc_fitting are non-overlapping and in ascending index order. If that
    //  invariant breaks (e.g. from a future change to do_arc_fitting), the remapping silently
    //  produces wrong indices with no assertion to catch it.
    static void do_arc_fitting_and_simplify(Points& points, std::vector<PathFittingData>& result, double tolerance);
};

} // namespace Slic3r

#endif
