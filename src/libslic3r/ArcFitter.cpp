// [INTENT] Arc-fitting algorithm: converts a dense polyline (Points array using
// scaled integer coordinates) into a mixed sequence of arc (G2/G3) and linear
// (G1) segments stored in PathFittingData. The greedy sliding-window approach
// used here is O(n) amortized: the window grows until try_create_arc fails, at
// which point the last successful arc is committed and the window resets.
// [COUPLING] Outputs PathFittingData consumed by GCode.cpp / GCodeWriter.cpp
// for arc-move emission. Uses ArcSegment::try_create_arc from Circle.hpp.
// [MEMORY] All coordinates are in Slic3r scaled integers (coord_t = int64).
// The tolerance parameter is also in scaled units.
#include "ArcFitter.hpp"
#include "Polyline.hpp"

#include <cmath>
#include <cassert>

namespace Slic3r {

// [INTENT] Classify a point sequence into arc and linear runs.
// Algorithm: greedy window expansion from front_index to back_index.
// At each step, try_create_arc tests whether all points in [front_index,i]
// lie within `tolerance` of a single circle. When it fails, the last
// successful arc (saved in last_arc) is committed. Runs of 2 points that
// could not form an arc become Linear_move entries; adjacent linear entries
// are merged by extending end_point_index rather than appending new entries.
// [STATE] result is cleared at entry; indices in result refer into `points`.
// [HAZARD] H763 — If points.size() < 3 the function emits a single Linear_move
// spanning [0, size-1] and returns early. Callers must handle the size==0 and
// size==1 edge cases before calling: points[size-1] is undefined for size==0.
void ArcFitter::do_arc_fitting(const Points& points, std::vector<PathFittingData>& result, double tolerance)
{
// [INTENT] Optional SVG debug dump: controlled at compile time by DEBUG_ARC_FITTING
// preprocessor flag. Disabled in all release builds.
#ifdef DEBUG_ARC_FITTING
    static int  irun = 0;
    BoundingBox bbox_svg;
    bbox_svg.merge(get_extents(points));
    Polyline temp = Polyline(points);
    {
        std::stringstream stri;
        stri << "debug_arc_fitting_" << irun << ".svg";
        SVG svg(stri.str(), bbox_svg);
        svg.draw(points, "blue", 50000);
        svg.draw(temp, "red", 1);
        svg.Close();
    }
    ++irun;
#endif

    // [STATE] result is always reset so callers get a clean output.
    result.clear();
    result.reserve(points.size() / 2); // worst case size
    if (points.size() < 3) {
        // [INTENT] Degenerate case: cannot form an arc, emit a single linear span.
        PathFittingData data;
        data.start_point_index = 0;
        data.end_point_index   = points.size() - 1;
        data.path_type         = EMovePathType::Linear_move;
        result.push_back(data);
        return;
    }

    // [STATE] front_index = start of current candidate window;
    // back_index = last point added to the window (== i during loop);
    // last_arc = most recently succeeded arc fit (committed when window can no longer expand);
    // can_fit = whether current window fits an arc.
    size_t     front_index = 0;
    size_t     back_index  = 0;
    ArcSegment last_arc;
    bool       can_fit = false;
    Points     current_segment;
    current_segment.reserve(points.size());
    ArcSegment target_arc;
    for (size_t i = 0; i < points.size(); i++) {
        // BBS: point in stack is not enough, build stack first
        back_index = i;
        current_segment.push_back(points[i]);
        // [INTENT] Need at least 3 points to attempt arc fitting.
        if (back_index - front_index < 2)
            continue;

        // [INTENT] Try to fit all points in [front_index, i] as a single arc.
        // Polyline(current_segment).length() provides the approximate arc length
        // used for the length-percent-tolerance check inside try_create_arc.
        can_fit = ArcSegment::try_create_arc(current_segment, target_arc, Polyline(current_segment).length(), DEFAULT_SCALED_MAX_RADIUS,
                                             tolerance, DEFAULT_ARC_LENGTH_PERCENT_TOLERANCE);
        if (can_fit) {
            // BBS: can be fit as arc, then save arc data temperarily
            last_arc = target_arc;
            if (back_index == points.size() - 1) {
                // [INTENT] Reached end of input: commit the last pending arc immediately.
                result.emplace_back(std::move(PathFittingData{front_index, back_index,
                                                              last_arc.direction == ArcDirection::Arc_Dir_CCW ? EMovePathType::Arc_move_ccw :
                                                                                                                EMovePathType::Arc_move_cw,
                                                              last_arc}));
                front_index = back_index;
            }
        } else {
            if (back_index - front_index > 2) {
                // BBS: althought current point_stack can't be fit as arc,
                // but previous must can be fit if removing the top in stack, so save last arc
                //  [INTENT] Window of size > 3 failed: commit the arc up to back_index-1
                //  (which was the last successful fit) and restart from back_index-1.
                result.emplace_back(std::move(PathFittingData{front_index, back_index - 1,
                                                              last_arc.direction == ArcDirection::Arc_Dir_CCW ? EMovePathType::Arc_move_ccw :
                                                                                                                EMovePathType::Arc_move_cw,
                                                              last_arc}));
            } else {
                // BBS: save the first segment as line move when 3 point-line can't be fit as arc move
                //  [INTENT] 3-point window that fails arc fit: emit/extend a linear segment.
                //  Merge with the previous entry if it is also a linear to avoid fragmentation.
                if (result.empty() || result.back().path_type != EMovePathType::Linear_move)
                    result.emplace_back(std::move(PathFittingData{front_index, front_index + 1, EMovePathType::Linear_move, ArcSegment()}));
                else if (result.back().path_type == EMovePathType::Linear_move)
                    result.back().end_point_index = front_index + 1;
            }
            // [STATE] Reset window: overlap by one point so the next arc candidate
            // starts from the point that broke the previous arc.
            front_index = back_index - 1;
            current_segment.clear();
            current_segment.push_back(points[front_index]);
            current_segment.push_back(points[front_index + 1]);
        }
    }
    // BBS: handle the remain data
    //  [INTENT] After the loop, any uncommitted tail (front_index != back_index)
    //  that could not form an arc is flushed as a linear segment (or merged
    //  with a preceding linear entry).
    if (front_index != back_index) {
        if (result.empty() || result.back().path_type != EMovePathType::Linear_move)
            result.emplace_back(std::move(PathFittingData{front_index, back_index, EMovePathType::Linear_move, ArcSegment()}));
        else if (result.back().path_type == EMovePathType::Linear_move)
            result.back().end_point_index = back_index;
    }
    result.shrink_to_fit();
}

// [INTENT] Two-pass pipeline combining arc fitting with Douglas-Peucker
// polyline simplification. Pass 1 classifies segments via do_arc_fitting.
// Pass 2 applies DP decimation independently to each sub-segment so that:
//   - straight segments lose redundant collinear points,
//   - arc segments also have their interior points thinned (wipe-compatible
//     subset retained, not just start/end).
// After simplification the `points` array is replaced in-place and
// PathFittingData indices are remapped via a prefix-sum of reduction counts.
// [HAZARD] H761 — `points` is modified in place; see header annotation.
// [HAZARD] H762 — Index remapping relies on non-overlapping ascending segments.
void ArcFitter::do_arc_fitting_and_simplify(Points& points, std::vector<PathFittingData>& result, double tolerance)
{
    // BBS: 1 do arc fit first
    //  [INTENT] If tolerance is effectively zero skip arc fitting and treat everything
    //  as one linear span, then fall through to DP simplification below.
    if (abs(tolerance) > SCALED_EPSILON)
        ArcFitter::do_arc_fitting(points, result, tolerance);
    else
        result.push_back(PathFittingData{0, points.size() - 1, EMovePathType::Linear_move, ArcSegment()});

    // BBS: 2 for straight part which can't fit arc, use DP simplify
    // for arc part, only need to keep start and end point
    if (result.size() == 1 && result[0].path_type == EMovePathType::Linear_move) {
        // BBS: all are straight segment, directly use DP simplify
        //  [INTENT] Fast path: entire polyline is linear, run global DP and update
        //  the single result entry's end index.
        points                    = MultiPoint::_douglas_peucker(points, tolerance);
        result[0].end_point_index = points.size() - 1;
        return;
    } else {
        // BBS: has both arc part and straight part, we should spilit the straight part out and do DP simplify
        //  [INTENT] Mixed path: DP-simplify each sub-segment independently, accumulate
        //  how many points were removed per segment, then rebuild the point array and
        //  adjust all indices via a prefix-sum pass.
        Points simplified_points;
        simplified_points.reserve(points.size());
        simplified_points.push_back(points[0]);
        // [STATE] reduce_count[i] = number of points removed from segment i during DP.
        // Converted to prefix-sum after the loop to compute cumulative index shifts.
        std::vector<size_t> reduce_count(result.size(), 0);
        for (size_t i = 0; i < result.size(); i++) {
            size_t start_index = result[i].start_point_index;
            size_t end_index   = result[i].end_point_index;
            // BBS: get the straight and arc part, and do simplifing independently.
            // Why: It's obvious that we need to use DP to simplify straight part to reduce point.
            // For arc part, theoretically, we only need to keep the start and end point, and
            // delete all other point. But when considering wipe operation, we must keep the original
            // point data and shouldn't reduce too much by only saving start and end point.
            //  [INTENT] DP is applied to arc segments too (not just straights) for wipe
            //  compatibility — keeping more intermediate points allows wipe moves to
            //  follow the original path more accurately. A refactor should preserve this.
            Points straight_or_arc_part;
            straight_or_arc_part.reserve(end_index - start_index + 1);
            for (size_t j = start_index; j <= end_index; j++)
                straight_or_arc_part.push_back(points[j]);
            straight_or_arc_part = MultiPoint::_douglas_peucker(straight_or_arc_part, tolerance);
            // BBS: how many point has been reduced
            reduce_count[i] = end_index - start_index + 1 - straight_or_arc_part.size();
            // BBS: save the simplified result
            //  [INTENT] Skip index 0 of each sub-segment because it was already appended
            //  as the last point of the previous segment (shared boundary point).
            for (size_t j = 1; j < straight_or_arc_part.size(); j++) {
                simplified_points.push_back(straight_or_arc_part[j]);
            }
        }
        // BBS: save and will return the simplified_points
        points = simplified_points;
        // BBS: modify the index in result because the point index must be changed to match the simplified points
        //  [INTENT] Convert reduce_count[] to prefix sums so that reduce_count[i] becomes
        //  the total number of points removed from all segments 0..i. This is then
        //  subtracted from each segment's end_point_index to get correct post-simplify indices.
        //  [HAZARD] H762 — Correctness depends on segments being ordered and non-overlapping.
        for (size_t j = 1; j < reduce_count.size(); j++)
            reduce_count[j] += reduce_count[j - 1];
        for (size_t j = 0; j < result.size(); j++) {
            result[j].end_point_index -= reduce_count[j];
            // [INTENT] Chain: next segment's start must equal this segment's end (shared point).
            if (j != result.size() - 1)
                result[j + 1].start_point_index = result[j].end_point_index;
        }
    }
}

} // namespace Slic3r
