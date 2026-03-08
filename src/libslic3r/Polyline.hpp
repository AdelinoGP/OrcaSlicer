// [INTENT] Declares the Polyline class (open path of 2D scaled-integer points),
//          ThickPolyline (Polyline with per-segment width and endpoint flags),
//          Polyline3 (3D polyline), and a variety of free-function helpers for
//          conversion, merging, filtering, and spatial queries.
// [STATE] Polyline inherits MultiPoint (points vector in scaled coord_t).
//         BBS addition: carries a parallel fitting_result vector recording
//         arc-fitting metadata for each span of the path. This vector MUST
//         stay synchronized with points across ALL mutations — H599.
// [COUPLING] Depends on ArcFitter.hpp (BBS arc fitting). Depends on
//            MultiPoint (inherited), Line, BoundingBox.
#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include "libslic3r.h"
#include "Line.hpp"
#include "MultiPoint.hpp"
#include <string>
#include <vector>
// BBS: new necessary header file
//  [COUPLING] ArcFitter provides PathFittingData struct and arc fitting logic.
#include "ArcFitter.hpp"

namespace Slic3r {

class Polyline;
class ThickPolyline;
// [INTENT] Standard collections; use std::allocator (not TBB).
typedef std::vector<Polyline>      Polylines;
typedef std::vector<ThickPolyline> ThickPolylines;

// [INTENT] An open path through a sequence of 2D scaled-integer points.
//          Unlike Polygon, the last point is NOT implicitly connected back
//          to the first point.
// [STATE] Two synchronized members:
//   - points (inherited from MultiPoint): the vertex array.
//   - fitting_result: arc-fit metadata, parallel to spans between points.
//     INVARIANT: if fitting_result is non-empty, it must describe every span
//     [i, i+1] up to points.size()-1. Any mutation to points MUST update
//     fitting_result accordingly (via append_fitting_result_* helpers).
// [HAZARD] H599: fitting_result must stay in sync with points across ALL
//          mutations. Direct manipulation of points without updating
//          fitting_result (e.g., MultiPoint::append bypassed wrappers) will
//          produce corrupt arc metadata that silently generates wrong G-code.
class Polyline : public MultiPoint
{
public:
    // [INTENT] Default constructor: empty polyline, empty fitting_result.
    Polyline() {};
    // [INTENT] Copy constructor: copies both points and fitting_result.
    Polyline(const Polyline& other) : MultiPoint(other.points), fitting_result(other.fitting_result) {}
    // [INTENT] Move constructor: moves both points and fitting_result.
    Polyline(Polyline&& other) : MultiPoint(std::move(other.points)), fitting_result(std::move(other.fitting_result)) {}
    // [INTENT] Initializer-list constructor: clears fitting_result (no arcs
    //          in a freshly constructed polyline from raw points).
    Polyline(std::initializer_list<Point> list) : MultiPoint(list) { fitting_result.clear(); }
    // [INTENT] Two-point constructor: builds a single-segment polyline.
    explicit Polyline(const Point& p1, const Point& p2)
    {
        points.reserve(2);
        points.emplace_back(p1);
        points.emplace_back(p2);
        fitting_result.clear();
    }
    explicit Polyline(const Points& points) : MultiPoint(points) { fitting_result.clear(); }
    explicit Polyline(Points&& points) : MultiPoint(std::move(points)) { fitting_result.clear(); }
    // [INTENT] Copy assignment: copies both points and fitting_result.
    Polyline& operator=(const Polyline& other)
    {
        points         = other.points;
        fitting_result = other.fitting_result;
        return *this;
    }
    // [INTENT] Move assignment: moves both points and fitting_result.
    Polyline& operator=(Polyline&& other)
    {
        points         = std::move(other.points);
        fitting_result = std::move(other.fitting_result);
        return *this;
    }
    // [INTENT] Construct a polyline from float mm coordinates, scaling to
    //          coord_t integers. fitting_result is cleared (no arc support
    //          for new_scale input).
    static Polyline new_scale(const std::vector<Vec2d>& points)
    {
        Polyline pl;
        pl.points.reserve(points.size());
        for (const Vec2d& pt : points)
            pl.points.emplace_back(Point::new_scale(pt(0), pt(1)));
        // BBS: new_scale doesn't support arc, so clean
        //  [HAZARD] H599: fitting_result explicitly cleared; any arc metadata
        //           in the input Vec2d source is discarded with no warning.
        pl.fitting_result.clear();
        return pl;
    }

    // [INTENT] Append a single point, skipping if it duplicates the last point.
    //          Calls append_fitting_result_after_append_points() to keep the
    //          fitting_result in sync with the new trailing point.
    void append(const Point& point)
    {
        // BBS: don't need to append same point
        if (!this->empty() && this->last_point() == point)
            return;
        MultiPoint::append(point);
        append_fitting_result_after_append_points();
    }

    // [INTENT] Prepend a point at the start of the polyline.
    //          Handles degenerate cases (size==1) by clearing fitting_result
    //          and using raw MultiPoint methods to avoid corrupting arc spans.
    //          For size>1: reverse, append, reverse (maintains fitting_result
    //          by going through the reverse-aware path).
    void append_before(const Point& point)
    {
        // BBS: don't need to append same point
        if (!this->empty() && this->first_point() == point)
            return;
        if (this->size() == 1) {
            // [HAZARD] H599: For single-point polylines, fitting_result is
            //          unconditionally cleared instead of updated.
            this->fitting_result.clear();
            MultiPoint::append(point);
            MultiPoint::reverse();
        } else {
            this->reverse();
            this->append(point);
            this->reverse();
        }
    }

    // [INTENT] Append a Points collection, skipping duplicated junction.
    //          Delegates to the iterator overload.
    void append(const Points& src)
    {
        // BBS: don't need to append same point
        if (!this->empty() && !src.empty() && this->last_point() == src[0])
            this->append(src.begin() + 1, src.end());
        else
            this->append(src.begin(), src.end());
    }
    // [INTENT] Append a range of points [begin, end). Skips if first point
    //          duplicates last_point(). Calls
    //          append_fitting_result_after_append_points() to sync metadata.
    void append(const Points::const_iterator& begin, const Points::const_iterator& end)
    {
        // BBS: don't need to append same point
        if (!this->empty() && begin != end && this->last_point() == *begin)
            MultiPoint::append(begin + 1, end);
        else
            MultiPoint::append(begin, end);
        append_fitting_result_after_append_points();
    }
    // [INTENT] Append Points by move. Calls
    //          append_fitting_result_after_append_points() to sync metadata.
    void append(Points&& src)
    {
        MultiPoint::append(std::move(src));
        append_fitting_result_after_append_points();
    }
    // [INTENT] Append another Polyline (const ref): merges both points and
    //          fitting_result, with index offset adjustment. Defined in .cpp.
    void append(const Polyline& src);
    // [INTENT] Append another Polyline (rvalue): moves points and fitting_result.
    void append(Polyline&& src);

    Point&       operator[](Points::size_type idx) { return this->points[idx]; }
    const Point& operator[](Points::size_type idx) const { return this->points[idx]; }

    const Point& last_point() const override { return this->points.back(); }
    const Point& leftmost_point() const;
    Lines        lines() const override;

    // [INTENT] Clear all points and fitting_result together — maintains H599 invariant.
    void clear()
    {
        MultiPoint::clear();
        this->fitting_result.clear();
    }
    // [INTENT] Reverse the polyline. Must also reverse fitting_result indices
    //          and arc directions. Defined in .cpp with arc-aware logic.
    void   reverse();
    void   clip_end(double distance);
    void   clip_start(double distance);
    void   extend_end(double distance);
    void   extend_start(double distance);
    Points equally_spaced_points(double distance) const;
    // [INTENT] Douglas-Peucker simplification. Clears fitting_result
    //          (arcs are lost after simplification — H599 compliance).
    void simplify(double tolerance);
    //    template <class T> void simplify_by_visibility(const T &area);
    void split_at(Point& point, Polyline* p1, Polyline* p2) const;
    bool split_at_index(const size_t index, Polyline* p1, Polyline* p2) const;
    bool split_at_length(const double length, Polyline* p1, Polyline* p2) const;

    bool is_straight() const;
    bool is_closed() const { return this->points.front() == this->points.back(); }

    // BBS: store arc fitting result
    //  [STATE] fitting_result: vector of PathFittingData. Each entry describes
    //          one span: [start_point_index, end_point_index, path_type, arc_data].
    //          Empty means all spans are linear moves (legacy/default).
    //          Non-empty means at least one arc span exists; MUST cover all spans.
    std::vector<PathFittingData> fitting_result;
    // BBS: simplify points by arc fitting
    //  [INTENT] Run arc fitting on this polyline's points, storing the result
    //           in fitting_result and simplifying straight segments via D-P.
    void simplify_by_fitting_arc(double tolerance);
    // BBS:
    //  [INTENT] Split the polyline into equal-length segments, returned as a
    //           collection of 2-point Polylines.
    Polylines equally_spaced_lines(double distance) const;

private:
    // [INTENT] After appending points to MultiPoint, update fitting_result:
    //          extend the last linear segment to cover the new trailing points,
    //          or append a new linear segment if last segment was an arc.
    void append_fitting_result_after_append_points();
    // [INTENT] After appending a Polyline's points, merge its fitting_result
    //          into this->fitting_result with proper index offset adjustment.
    void append_fitting_result_after_append_polyline(const Polyline& src);
    // [INTENT] Clear fitting_result and replace with a single linear segment
    //          covering all current points. Used as a fallback/reset.
    void reset_to_linear_move();
    // [INTENT] Split fitting_result at 'index', keeping segments that START
    //          before index. Clips any arc at the split boundary. Returns the
    //          new endpoint (may differ from points[index] if arc was clipped).
    bool split_fitting_result_before_index(const size_t index, Point& new_endpoint, std::vector<PathFittingData>& data) const;
    // [INTENT] Split fitting_result at 'index', keeping segments that END
    //          after index. Clips any arc at the split boundary. Returns the
    //          new startpoint. Re-indexes all kept segments by subtracting index.
    bool split_fitting_result_after_index(const size_t index, Point& new_startpoint, std::vector<PathFittingData>& data) const;
};

inline bool operator==(const Polyline& lhs, const Polyline& rhs) { return lhs.points == rhs.points; }
inline bool operator!=(const Polyline& lhs, const Polyline& rhs) { return lhs.points != rhs.points; }

// Don't use this class in production code, it is used exclusively by the Perl binding for unit tests!
#ifdef PERL_UCHAR_MIN
class PolylineCollection
{
public:
    Polylines polylines;
};
#endif /* PERL_UCHAR_MIN */

extern BoundingBox get_extents(const Polyline& polyline);
extern BoundingBox get_extents(const Polylines& polylines);

// Return True when erase some otherwise False.
bool remove_same_neighbor(Polyline& polyline);
bool remove_same_neighbor(Polylines& polylines);

// [INTENT] Sum of all polyline lengths (in scaled units).
inline double total_length(const Polylines& polylines)
{
    double total = 0;
    for (const Polyline& pl : polylines)
        total += pl.length();
    return total;
}

// [INTENT] Convert an open Polyline to a vector of Line segments.
//          Does NOT include a closing edge (unlike Polygon::lines()).
inline Lines to_lines(const Polyline& poly)
{
    Lines lines;
    if (poly.points.size() >= 2) {
        lines.reserve(poly.points.size() - 1);
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end() - 1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
    }
    return lines;
}

// [INTENT] Convert a collection of Polylines to a flat vector of Lines.
inline Lines to_lines(const Polylines& polys)
{
    size_t n_lines = 0;
    for (size_t i = 0; i < polys.size(); ++i)
        if (polys[i].points.size() > 1)
            n_lines += polys[i].points.size() - 1;
    Lines lines;
    lines.reserve(n_lines);
    for (size_t i = 0; i < polys.size(); ++i) {
        const Polyline& poly = polys[i];
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end() - 1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
    }
    return lines;
}

// [INTENT] Convert a vector of Points vectors to a Polylines collection.
//          fitting_result is cleared in each resulting Polyline (H599).
inline Polylines to_polylines(const std::vector<Points>& paths)
{
    Polylines out;
    out.reserve(paths.size());
    for (const Points& path : paths)
        out.emplace_back(path);
    return out;
}

// [INTENT] Move-convert: avoids copying each Points vector.
inline Polylines to_polylines(std::vector<Points>&& paths)
{
    Polylines out;
    out.reserve(paths.size());
    for (const Points& path : paths)
        out.emplace_back(std::move(path));
    return out;
}

// [INTENT] Append all polylines from src to dst (copy).
inline void polylines_append(Polylines& dst, const Polylines& src) { dst.insert(dst.end(), src.begin(), src.end()); }

// [INTENT] Append all polylines from src to dst (move), clearing src.
inline void polylines_append(Polylines& dst, Polylines&& src)
{
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        std::move(std::begin(src), std::end(src), std::back_inserter(dst));
        src.clear();
    }
}

// [INTENT] Merge two polylines (or PointsType collections) by reversing/swapping
//          to align endpoints, then concatenating. dst_first/src_first indicate
//          which end is the merge point.
// [HAZARD] H599: This template operates on raw PointsType (typically Points
//          or Polyline::points), NOT on Polyline objects with fitting_result.
//          Calling this on Polylines will LOSE arc metadata in the merged result.
// Merge polylines at their respective end points.
// dst_first: the merge point is at dst.begin() or dst.end()?
// src_first: the merge point is at src.begin() or src.end()?
// The orientation of the resulting polyline is unknown, the output polyline may start
// either with src piece or dst piece.
template<typename PointsType> inline void polylines_merge(PointsType& dst, bool dst_first, PointsType&& src, bool src_first)
{
    if (dst_first) {
        if (src_first)
            std::reverse(dst.begin(), dst.end());
        else
            std::swap(dst, src);
    } else if (!src_first)
        std::reverse(src.begin(), src.end());
    // Merge src into dst.
    append(dst, std::move(src));
}

// [INTENT] Find the leftmost point across all polylines.
//          Throws InvalidArgument if the collection is empty.
const Point& leftmost_point(const Polylines& polylines);

// [INTENT] Remove polylines with fewer than 2 points.
bool remove_degenerate(Polylines& polylines);

// [INTENT] Find the closest foot point on a polyline to pt.
//          Returns (segment_index, foot_point). segment_index is 0-based
//          index into the line segments (pairs of consecutive points).
// Returns index of a segment of a polyline and foot point of pt on polyline.
std::pair<int, Point> foot_pt(const Points& polyline, const Point& pt);

// [INTENT] A Polyline where each edge has an associated width (variable-width
//          extrusion path). Width is stored as a vector of (n-1)*2 values:
//          each segment has a width at start and at end.
// [STATE] width.size() == (points.size() - 1) * 2 when valid.
//         endpoints: pair of bools indicating whether each endpoint is a
//         true terminal (true) or a junction (false) of the medial axis.
// [HAZARD] H599: reverse() must also reverse width and swap endpoints —
//          this is implemented correctly in ThickPolyline::reverse().
class ThickPolyline : public Polyline
{
public:
    ThickPolyline() : endpoints(std::make_pair(false, false)) {}
    ThickLines thicklines() const;
    // [INTENT] Reverse both the inherited Polyline (and its fitting_result)
    //          and the width vector, and swap endpoint flags.
    void reverse()
    {
        Polyline::reverse();
        std::reverse(this->width.begin(), this->width.end());
        std::swap(this->endpoints.first, this->endpoints.second);
    }
    void clear()
    {
        Polyline::clear();
        width.clear();
    }

    // Make this closed ThickPolyline starting in the specified index.
    // Be aware that this method can be applicable just for closed ThickPolyline.
    // On open ThickPolyline make no effect.
    void start_at_index(int index);

    // [STATE] Width values: width[2*i] = start-width of segment i,
    //         width[2*i+1] = end-width of segment i.
    std::vector<coordf_t> width;
    // [STATE] endpoints.first: true if the start is a true open endpoint.
    //         endpoints.second: true if the end is a true open endpoint.
    std::pair<bool, bool> endpoints;
};

// [INTENT] Convert Polylines to ThickPolylines by assigning a uniform width.
//          Moves points, does not copy. fitting_result is left empty/default
//          (ThickPolyline inherits Polyline's default constructor behavior).
inline ThickPolylines to_thick_polylines(Polylines&& polylines, const coordf_t width)
{
    ThickPolylines out;
    out.reserve(polylines.size());
    for (Polyline& polyline : polylines) {
        out.emplace_back();
        // [INTENT] Each segment has a start and end width — all set to uniform 'width'.
        out.back().width.assign((polyline.points.size() - 1) * 2, width);
        out.back().points = std::move(polyline.points);
    }
    return out;
}

// [INTENT] 3D polyline using MultiPoint3 (Vec3crd = scaled 3D integer coords).
class Polyline3 : public MultiPoint3
{
public:
    virtual Lines3 lines() const;
};

typedef std::vector<Polyline3> Polylines3;

} // namespace Slic3r

#endif
