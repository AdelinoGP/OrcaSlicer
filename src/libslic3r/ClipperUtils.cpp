// [INTENT] ClipperUtils.cpp — implementation of all polygon/polyline boolean operations,
// morphological offset (expand/shrink), variable-width miter offsets, and PolyTree conversions.
// Every public slicer operation that reshapes geometry flows through this file.
// [COUPLING] Depends on: ClipperUtils.hpp (providers/constants), Geometry.hpp (math helpers),
//   ShortestPath.hpp (chain_clipper_polynodes for spatial ordering).
// [STATE] All functions are stateless free functions; no mutable global state except
//   the static empty-sentinel Points in the .hpp (EmptyPathsProvider::s_empty_points,
//   SinglePathProvider::s_end defined below at file scope).
// [MEMORY] Clipper objects are stack-allocated and destroyed after each call.
//   Heavy use of move semantics to transfer Paths/PolyTree out of ClipperLib.
//   raw_offset() allocates one ClipperOffset per path — O(N) ClipperOffset instances
//   for N input paths. For large polyline sets this is N heap-allocated objects.
// [CONCURRENCY] No shared mutable state; all functions are thread-safe as long as
//   callers do not share the same output containers. Multi-threaded callers in
//   PrintObject/Fill pass disjoint output buffers.
// [HAZARD H440] The debug binary dump `export_clipper_input_polygons_bin` uses FILE*
//   with no error recovery on the `fwrite` calls; the `err:` label at line 51 is
//   only reachable if `fopen` fails, so any mid-write `fwrite` failure silently
//   produces a truncated/corrupt debug file.

#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "ShortestPath.hpp"

// #define CLIPPER_UTILS_DEBUG

#ifdef CLIPPER_UTILS_DEBUG
#include "SVG.hpp"
#endif /* CLIPPER_UTILS_DEBUG */

// Profiling support using the Shiny intrusive profiler
// #define CLIPPER_UTILS_PROFILE
#if defined(SLIC3R_PROFILE) && defined(CLIPPER_UTILS_PROFILE)
#include <Shiny/Shiny.h>
#define CLIPPERUTILS_PROFILE_FUNC() PROFILE_FUNC()
#define CLIPPERUTILS_PROFILE_BLOCK(name) PROFILE_BLOCK(name)
#else
#define CLIPPERUTILS_PROFILE_FUNC()
#define CLIPPERUTILS_PROFILE_BLOCK(name)
#endif

namespace Slic3r {

#ifdef CLIPPER_UTILS_DEBUG
// For debugging the Clipper library, for providing bug reports to the Clipper author.
bool export_clipper_input_polygons_bin(const char* path, const ClipperLib::Paths& input_subject, const ClipperLib::Paths& input_clip)
{
    FILE* pfile = fopen(path, "wb");
    if (pfile == NULL)
        return false;

    uint32_t sz = uint32_t(input_subject.size());
    fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_subject.size(); ++i) {
        const ClipperLib::Path& path = input_subject[i];
        sz                           = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(ClipperLib::IntPoint), sz, pfile);
    }
    sz = uint32_t(input_clip.size());
    ::fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_clip.size(); ++i) {
        const ClipperLib::Path& path = input_clip[i];
        sz                           = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(ClipperLib::IntPoint), sz, pfile);
    }
    ::fclose(pfile);
    return true;

err:
    ::fclose(pfile);
    return false;
}
#endif /* CLIPPER_UTILS_DEBUG */

namespace ClipperUtils {
// [STATE] File-scope static sentinels for range-based iteration over empty/single-path providers.
// EmptyPathsProvider::s_empty_points is shared across all calls — read-only, thread-safe.
// SinglePathProvider::s_end is a dummy Points used as an end sentinel — read-only, thread-safe.
Points EmptyPathsProvider::s_empty_points;
Points SinglePathProvider::s_end;

// [INTENT] clip_clipper_polygon_with_subject_bbox_templ — fast approximate vertex filter.
// Clips (prunes) vertices of `src` that are provably outside the bounding box `bbox`.
// This is NOT a true geometric clip; it uses the Cohen-Sutherland outcode heuristic:
//   a vertex is kept if it is inside, OR if its outcode AND with its neighbours' outcodes is 0
//   (meaning the edge between them potentially crosses into the bbox).
// Purpose: pre-filter clip polygons before expensive Clipper boolean ops to reduce vertex count.
// [HAZARD H441] This is a vertex-level approximation, NOT a true Sutherland-Hodgman clip.
//   The result may include vertices slightly outside bbox (corner-crossing edges are kept whole).
//   Callers must not assume the returned polygon is exactly clipped to bbox — it is a superset.
//   Using the result as a subject polygon in Clipper is safe because Clipper handles the extras,
//   but callers that check point-in-bbox on the output will be surprised.
// [HAZARD H442] get_entire_polygons=true bypasses filtering entirely (returns src as-is).
//   This flag exists for the case where the caller wants unmodified ExPolygon holes passed through.
//   The flag name is misleading — it means "don't filter, return the whole polygon unchanged".
template<typename PointsType>
inline void clip_clipper_polygon_with_subject_bbox_templ(const PointsType&  src,
                                                         const BoundingBox& bbox,
                                                         PointsType&        out,
                                                         const bool         get_entire_polygons = false)
{
    using PointType = typename PointsType::value_type;

    out.clear();
    const size_t cnt = src.size();
    if (cnt < 3)
        return;

    enum class Side { Left = 1, Right = 2, Top = 4, Bottom = 8 };

    auto sides = [bbox](const PointType& p) {
        return int(p.x() < bbox.min.x()) * int(Side::Left) + int(p.x() > bbox.max.x()) * int(Side::Right) +
               int(p.y() < bbox.min.y()) * int(Side::Bottom) + int(p.y() > bbox.max.y()) * int(Side::Top);
    };

    int          sides_prev = sides(src.back());
    int          sides_this = sides(src.front());
    const size_t last       = cnt - 1;
    for (size_t i = 0; i < last; ++i) {
        int sides_next = sides(src[i + 1]);
        if ( // This point is inside. Take it.
            sides_this == 0 ||
            // Either this point is outside and previous or next is inside, or
            // the edge possibly cuts corner of the bounding box.
            (sides_prev & sides_this & sides_next) == 0) {
            out.emplace_back(src[i]);
            sides_prev = sides_this;
        } else {
            // All the three points (this, prev, next) are outside at the same side.
            // Ignore this point.
        }
        sides_this = sides_next;
    }

    // Never produce just a single point output polygon.
    if (!out.empty()) {
        if (get_entire_polygons) {
            out = src;
        } else {
            if (int sides_next = sides(out.front());
                // The last point is inside. Take it.
                sides_this == 0 ||
                // Either this point is outside and previous or next is inside, or
                // the edge possibly cuts corner of the bounding box.
                (sides_prev & sides_this & sides_next) == 0)
                out.emplace_back(src.back());
        }
    }
}

void clip_clipper_polygon_with_subject_bbox(const Points& src, const BoundingBox& bbox, Points& out, const bool get_entire_polygons)
{
    clip_clipper_polygon_with_subject_bbox_templ(src, bbox, out, get_entire_polygons);
}
void clip_clipper_polygon_with_subject_bbox(const ZPoints& src, const BoundingBox& bbox, ZPoints& out)
{
    clip_clipper_polygon_with_subject_bbox_templ(src, bbox, out);
}

template<typename PointsType>
[[nodiscard]] PointsType clip_clipper_polygon_with_subject_bbox_templ(const PointsType& src, const BoundingBox& bbox)
{
    PointsType out;
    clip_clipper_polygon_with_subject_bbox(src, bbox, out);
    return out;
}

[[nodiscard]] Points clip_clipper_polygon_with_subject_bbox(const Points& src, const BoundingBox& bbox)
{
    return clip_clipper_polygon_with_subject_bbox_templ(src, bbox);
}
[[nodiscard]] ZPoints clip_clipper_polygon_with_subject_bbox(const ZPoints& src, const BoundingBox& bbox)
{
    return clip_clipper_polygon_with_subject_bbox_templ(src, bbox);
}

void clip_clipper_polygon_with_subject_bbox(const Polygon& src, const BoundingBox& bbox, Polygon& out)
{
    clip_clipper_polygon_with_subject_bbox(src.points, bbox, out.points);
}

[[nodiscard]] Polygon clip_clipper_polygon_with_subject_bbox(const Polygon& src, const BoundingBox& bbox, const bool get_entire_polygons)
{
    Polygon out;
    clip_clipper_polygon_with_subject_bbox(src.points, bbox, out.points, get_entire_polygons);
    return out;
}

[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const Polygons& src, const BoundingBox& bbox)
{
    Polygons out;
    out.reserve(src.size());
    for (const Polygon& p : src)
        out.emplace_back(clip_clipper_polygon_with_subject_bbox(p, bbox));
    out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon& polygon) { return polygon.empty(); }), out.end());
    return out;
}
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygon& src, const BoundingBox& bbox, const bool get_entire_polygons)
{
    Polygons out;
    out.reserve(src.num_contours());
    out.emplace_back(clip_clipper_polygon_with_subject_bbox(src.contour, bbox, get_entire_polygons));
    for (const Polygon& p : src.holes)
        out.emplace_back(clip_clipper_polygon_with_subject_bbox(p, bbox, get_entire_polygons));
    out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon& polygon) { return polygon.empty(); }), out.end());
    return out;
}
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygons&  src,
                                                               const BoundingBox& bbox,
                                                               const bool         get_entire_polygons)
{
    Polygons out;
    out.reserve(number_polygons(src));
    for (const ExPolygon& p : src) {
        Polygons temp = clip_clipper_polygons_with_subject_bbox(p, bbox, get_entire_polygons);
        out.insert(out.end(), temp.begin(), temp.end());
    }

    out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon& polygon) { return polygon.empty(); }), out.end());
    return out;
}
} // namespace ClipperUtils

// [INTENT] PolyTreeToExPolygons — converts ClipperLib's hierarchical PolyTree result into
// a flat vector of ExPolygons (contour + holes). This is how all *_ex() / *_pt() results
// are returned from Clipper operations.
// [STATE] PolyTree ownership is transferred (move) so the tree is consumed after this call.
// [MEMORY] Two-pass: first counts total ExPolygons (PolyTreeCountExPolygons), then reserves
//   and fills. This avoids reallocation but requires traversing the tree twice.
// [HAZARD H443] PolyTree depth semantics: PolyNode at even depth = outer contour,
//   odd depth = hole. Children of holes at even depth are again outer contours (islands
//   within holes). The recursive descent must strictly alternate contour/hole. If Clipper
//   ever produces a malformed tree (bug in JoinCommonEdges, etc.), the resulting ExPolygons
//   will silently have misassigned holes as contours or vice versa.
// [COUPLING] Exclusively called by clipper_do_polytree(), expolygons_offset_pt(),
//   and directly by union_ex/union_pt overloads. Must be kept in sync with
//   ClipperLib PolyNode::IsHole() semantics.
static ExPolygons PolyTreeToExPolygons(ClipperLib::PolyTree&& polytree)
{
    struct Inner
    {
        static void PolyTreeToExPolygonsRecursive(ClipperLib::PolyNode&& polynode, ExPolygons* expolygons)
        {
            size_t cnt = expolygons->size();
            expolygons->resize(cnt + 1);
            (*expolygons)[cnt].contour.points = std::move(polynode.Contour);
            (*expolygons)[cnt].holes.resize(polynode.ChildCount());
            for (int i = 0; i < polynode.ChildCount(); ++i) {
                (*expolygons)[cnt].holes[i].points = std::move(polynode.Childs[i]->Contour);
                // Add outer polygons contained by (nested within) holes.
                for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++j)
                    PolyTreeToExPolygonsRecursive(std::move(*polynode.Childs[i]->Childs[j]), expolygons);
            }
        }

        static size_t PolyTreeCountExPolygons(const ClipperLib::PolyNode& polynode)
        {
            size_t cnt = 1;
            for (int i = 0; i < polynode.ChildCount(); ++i) {
                for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++j)
                    cnt += PolyTreeCountExPolygons(*polynode.Childs[i]->Childs[j]);
            }
            return cnt;
        }
    };

    ExPolygons retval;
    size_t     cnt = 0;
    for (int i = 0; i < polytree.ChildCount(); ++i)
        cnt += Inner::PolyTreeCountExPolygons(*polytree.Childs[i]);
    retval.reserve(cnt);
    for (int i = 0; i < polytree.ChildCount(); ++i)
        Inner::PolyTreeToExPolygonsRecursive(std::move(*polytree.Childs[i]), &retval);
    return retval;
}

Polylines PolyTreeToPolylines(ClipperLib::PolyTree&& polytree)
{
    struct Inner
    {
        static void AddPolyNodeToPaths(ClipperLib::PolyNode& polynode, Polylines& out)
        {
            if (!polynode.Contour.empty())
                out.emplace_back(std::move(polynode.Contour));
            for (ClipperLib::PolyNode* child : polynode.Childs)
                AddPolyNodeToPaths(*child, out);
        }
    };

    Polylines out;
    out.reserve(polytree.Total());
    Inner::AddPolyNodeToPaths(polytree, out);
    return out;
}

#if 0
// Global test.
bool has_duplicate_points(const ClipperLib::PolyTree &polytree)
{
    struct Helper {
        static void collect_points_recursive(const ClipperLib::PolyNode &polynode, ClipperLib::Path &out) {
            // For each hole of the current expolygon:
            out.insert(out.end(), polynode.Contour.begin(), polynode.Contour.end());
            for (int i = 0; i < polynode.ChildCount(); ++ i)
                collect_points_recursive(*polynode.Childs[i], out);
        }
    };
    ClipperLib::Path pts;
    for (int i = 0; i < polytree.ChildCount(); ++ i)
        Helper::collect_points_recursive(*polytree.Childs[i], pts);
    return has_duplicate_points(std::move(pts));
}
#else
// Local test inside each of the contours.
bool has_duplicate_points(const ClipperLib::PolyTree& polytree)
{
    struct Helper
    {
        static bool has_duplicate_points_recursive(const ClipperLib::PolyNode& polynode)
        {
            if (has_duplicate_points(polynode.Contour))
                return true;
            for (int i = 0; i < polynode.ChildCount(); ++i)
                if (has_duplicate_points_recursive(*polynode.Childs[i]))
                    return true;
            return false;
        }
    };
    ClipperLib::Path pts;
    for (int i = 0; i < polytree.ChildCount(); ++i)
        if (Helper::has_duplicate_points_recursive(*polytree.Childs[i]))
            return true;
    return false;
}
#endif

// [INTENT] raw_offset — applies ClipperOffset to each path independently.
// CCW contours (outer) are offset outwards; CW contours (holes) are offset inwards.
// No union is performed after offsetting — the output may have overlapping paths.
// This is the primitive used by all expand/shrink/offset calls.
// [HAZARD H444] raw_offset creates ONE ClipperOffset object per path (co.Clear() in loop).
//   For N input paths this is N ClipperOffset::Execute calls, each allocating its own
//   internal structures. For high polygon-count layers this is significant heap churn.
//   An alternative approach would be AddPath(all) then Execute — but that would union
//   the outputs, which raw_offset intentionally avoids.
// [HAZARD H445] The orientation flip trick: ClipperOffset internally reorients each path
//   to CCW before offsetting, then applies the sign of `offset`. For CW paths (holes),
//   it negates the offset internally. This function reverses the output back to CW after
//   the fact (std::reverse). If a future ClipperLib version changes this behavior,
//   the reversal here will produce incorrect results with no compile-time warning.
// [STATE] `co` is stack-allocated and re-initialized via co.Clear() per iteration.
//   co.ArcTolerance and co.MiterLimit are set once before the loop — correct because
//   co.Clear() only clears paths, not these parameters.
template<typename PathsProvider>
static ClipperLib::Paths raw_offset(PathsProvider&&      paths,
                                    float                offset,
                                    ClipperLib::JoinType joinType,
                                    double               miterLimit,
                                    ClipperLib::EndType  endType = ClipperLib::etClosedPolygon)
{
    ClipperLib::ClipperOffset co;
    ClipperLib::Paths         out;
    out.reserve(paths.size());
    ClipperLib::Paths out_this;
    if (joinType == jtRound)
        co.ArcTolerance = miterLimit;
    else
        co.MiterLimit = miterLimit;
    co.ShortestEdgeLength = std::abs(offset * ClipperOffsetShortestEdgeFactor);
    for (const ClipperLib::Path& path : paths) {
        co.Clear();
        // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
        // contours will be CCW oriented even though the input paths are CW oriented.
        // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
        co.AddPath(path, joinType, endType);
        bool ccw = endType == ClipperLib::etClosedPolygon ? ClipperLib::Orientation(path) : true;
        co.Execute(out_this, ccw ? offset : -offset);
        if (!ccw) {
            // Reverse the resulting contours.
            for (ClipperLib::Path& path : out_this)
                std::reverse(path.begin(), path.end());
        }
        append(out, std::move(out_this));
    }
    return out;
}

// Offset outside by 10um, one by one.
template<typename PathsProvider> static ClipperLib::Paths safety_offset(PathsProvider&& paths)
{
    return raw_offset(std::forward<PathsProvider>(paths), ClipperSafetyOffset, DefaultJoinType, DefaultMiterLimit);
}

template<class TResult, class TSubj, class TClip>
TResult clipper_do(const ClipperLib::ClipType clipType, TSubj&& subject, TClip&& clip, const ClipperLib::PolyFillType fillType)
{
    ClipperLib::Clipper clipper;
    clipper.AddPaths(std::forward<TSubj>(subject), ClipperLib::ptSubject, true);
    clipper.AddPaths(std::forward<TClip>(clip), ClipperLib::ptClip, true);
    TResult retval;
    clipper.Execute(clipType, retval, fillType, fillType);
    return retval;
}

template<class TResult, class TSubj, class TClip>
TResult clipper_do(const ClipperLib::ClipType     clipType,
                   TSubj&&                        subject,
                   TClip&&                        clip,
                   const ClipperLib::PolyFillType fillType,
                   const ApplySafetyOffset        do_safety_offset)
{
    // Safety offset only allowed on intersection and difference.
    assert(do_safety_offset == ApplySafetyOffset::No || clipType != ClipperLib::ctUnion);
    return do_safety_offset == ApplySafetyOffset::Yes ?
               clipper_do<TResult>(clipType, std::forward<TSubj>(subject), safety_offset(std::forward<TClip>(clip)), fillType) :
               clipper_do<TResult>(clipType, std::forward<TSubj>(subject), std::forward<TClip>(clip), fillType);
}

template<class TResult, class TSubj>
TResult clipper_union(
    TSubj&& subject,
    // fillType pftNonZero and pftPositive "should" produce the same result for "normalized with implicit union" set of polygons
    const ClipperLib::PolyFillType fillType = ClipperLib::pftNonZero)
{
    ClipperLib::Clipper clipper;
    clipper.AddPaths(std::forward<TSubj>(subject), ClipperLib::ptSubject, true);
    TResult retval;
    clipper.Execute(ClipperLib::ctUnion, retval, fillType, fillType);
    return retval;
}

// Perform union of input polygons using the positive rule, convert to ExPolygons.
// FIXME is there any benefit of not doing the boolean / using pftEvenOdd?
inline ExPolygons ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths& input, bool do_union)
{
    return PolyTreeToExPolygons(clipper_union<ClipperLib::PolyTree>(input, do_union ? ClipperLib::pftNonZero : ClipperLib::pftEvenOdd));
}

template<typename PathsProvider>
static ClipperLib::Paths raw_offset_polyline(PathsProvider&&      paths,
                                             float                offset,
                                             ClipperLib::JoinType joinType,
                                             double               miterLimit,
                                             ClipperLib::EndType  end_type = ClipperLib::etOpenButt)
{
    assert(offset > 0);
    return raw_offset<PathsProvider>(std::forward<PathsProvider>(paths), offset, joinType, miterLimit, end_type);
}

template<class TResult, typename PathsProvider>
static TResult expand_paths(PathsProvider&& paths, float offset, ClipperLib::JoinType joinType, double miterLimit)
{
    // BBS
    // assert(offset > 0);
    return clipper_union<TResult>(raw_offset(std::forward<PathsProvider>(paths), offset, joinType, miterLimit));
}

// [INTENT] shrink_paths — applies a negative offset (inward shrink) using the bounding-box
// sentinel trick to handle contours that may split during negative offsetting.
// Algorithm:
//   1. Apply raw_offset with -offset (inward). This may produce CW wound paths for
//      contours that collapse, and may split one contour into multiple.
//   2. Add a large outer rectangle (bbox + 10 units) as an additional subject path.
//   3. Flip ReverseSolution to invert winding convention.
//   4. Run pftNegative union — the outer rectangle "wraps" all resulting paths.
//   5. Remove the outermost polygon (the sentinel rectangle) from the output.
// [HAZARD H446] The sentinel rectangle uses +/-10 scaled units (10nm). If the offset
//   paths extend beyond GetBounds() by more than 10 units, the sentinel will not fully
//   contain them and the pftNegative union will produce garbage. In practice offset
//   expansions can only reduce bounds, so this is safe — but it is a silent assumption.
// [HAZARD H447] remove_outermost_polygon() for Paths erases solution.begin() (index 0).
//   Clipper's pftNegative union returns the outermost path first. If the inner paths
//   happen to be ordered differently in a future Clipper version, erase(begin()) removes
//   the wrong polygon. This is a fragile coupling to ClipperLib's internal ordering.
// [COUPLING] expand_paths + shrink_paths are the two halves of offset_paths. All
//   public offset() / offset_ex() / opening() / closing() calls route through these.
// used by shrink_paths()
template<class Container> static void remove_outermost_polygon(Container& solution);
template<> void                       remove_outermost_polygon<ClipperLib::Paths>(ClipperLib::Paths& solution)
{
    if (!solution.empty())
        solution.erase(solution.begin());
}
template<> void remove_outermost_polygon<ClipperLib::PolyTree>(ClipperLib::PolyTree& solution) { solution.RemoveOutermostPolygon(); }

template<class TResult, typename PathsProvider>
static TResult shrink_paths(PathsProvider&& paths, float offset, ClipperLib::JoinType joinType, double miterLimit)
{
    // BBS
    // assert(offset > 0);
    TResult out;
    if (auto raw = raw_offset(std::forward<PathsProvider>(paths), -offset, joinType, miterLimit); !raw.empty()) {
        ClipperLib::Clipper clipper;
        clipper.AddPaths(raw, ClipperLib::ptSubject, true);
        ClipperLib::IntRect r = clipper.GetBounds();
        clipper.AddPath({{r.left - 10, r.bottom + 10}, {r.right + 10, r.bottom + 10}, {r.right + 10, r.top - 10}, {r.left - 10, r.top - 10}},
                        ClipperLib::ptSubject, true);
        clipper.ReverseSolution(true);
        clipper.Execute(ClipperLib::ctUnion, out, ClipperLib::pftNegative, ClipperLib::pftNegative);
        remove_outermost_polygon(out);
    }
    return out;
}

template<class TResult, typename PathsProvider>
static TResult offset_paths(PathsProvider&& paths, float offset, ClipperLib::JoinType joinType, double miterLimit)
{
    // BBS
    // assert(offset != 0);

    return offset > 0 ? expand_paths<TResult>(std::forward<PathsProvider>(paths), offset, joinType, miterLimit) :
                        shrink_paths<TResult>(std::forward<PathsProvider>(paths), -offset, joinType, miterLimit);
}

Slic3r::Polygons offset(const Slic3r::Polygon& polygon, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(raw_offset(ClipperUtils::SinglePathProvider(polygon.points), delta, joinType, miterLimit));
}

Slic3r::Polygons offset(const Slic3r::Polygons& polygons, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(offset_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta, joinType, miterLimit));
}
Slic3r::ExPolygons offset_ex(const Slic3r::Polygons& polygons, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return PolyTreeToExPolygons(offset_paths<ClipperLib::PolyTree>(ClipperUtils::PolygonsProvider(polygons), delta, joinType, miterLimit));
}

Slic3r::Polygons offset(
    const Slic3r::Polyline& polyline, const float delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::EndType end_type)
{
    assert(delta > 0);
    return to_polygons(clipper_union<ClipperLib::Paths>(
        raw_offset_polyline(ClipperUtils::SinglePathProvider(polyline.points), delta, joinType, miterLimit, end_type)));
}
Slic3r::Polygons offset(
    const Slic3r::Polylines& polylines, const float delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::EndType end_type)
{
    assert(delta > 0);
    return to_polygons(clipper_union<ClipperLib::Paths>(
        raw_offset_polyline(ClipperUtils::PolylinesProvider(polylines), delta, joinType, miterLimit, end_type)));
}

Polygons contour_to_polygons(const Polygon& polygon, const float line_width, ClipperLib::JoinType join_type, double miter_limit)
{
    assert(line_width > 1.f);
    return to_polygons(clipper_union<ClipperLib::Paths>(
        raw_offset(ClipperUtils::SinglePathProvider(polygon.points), line_width / 2, join_type, miter_limit, ClipperLib::etClosedLine)));
}
Polygons contour_to_polygons(const Polygons& polygons, const float line_width, ClipperLib::JoinType join_type, double miter_limit)
{
    assert(line_width > 1.f);
    return to_polygons(clipper_union<ClipperLib::Paths>(
        raw_offset(ClipperUtils::PolygonsProvider(polygons), line_width / 2, join_type, miter_limit, ClipperLib::etClosedLine)));
}

// [INTENT] offset_expolygon_inner — offsets a single ExPolygon (contour + holes) by delta.
// Returns 0 if the result is empty (fully collapsed), 1 otherwise.
// Three-step algorithm:
//   1. Offset the outer contour by +delta.
//   2. Offset each hole by -delta (ClipperOffset negates internally for CW paths).
//   3. Combine: for negative offset -> full Clipper difference (holes may intersect contour).
//              for positive offset -> just reverse holes in-place (no new intersections expected).
// [HAZARD H448] The positive-offset path assumes offset holes cannot intersect the expanded
//   contour. This is mathematically true only if the original ExPolygon is valid (non-self-
//   intersecting, holes fully inside contour). For malformed input (e.g., after file import
//   repair), the assumption breaks and overlapping regions will not be subtracted.
// [HAZARD H449] Returns raw ClipperLib::Paths, not ExPolygons. The caller must union or
//   convert. This means contour orientation is not enforced here — the caller's
//   clipper_union or PolyTreeToExPolygons must impose it.
// returns number of expolygons collected (0 or 1).
static int offset_expolygon_inner(
    const Slic3r::ExPolygon& expoly, const float delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::Paths& out)
{
    // 1) Offset the outer contour.
    ClipperLib::Paths contours;
    {
        ClipperLib::ClipperOffset co;
        if (joinType == jtRound)
            co.ArcTolerance = miterLimit;
        else
            co.MiterLimit = miterLimit;
        co.ShortestEdgeLength = std::abs(delta * ClipperOffsetShortestEdgeFactor);
        co.AddPath(expoly.contour.points, joinType, ClipperLib::etClosedPolygon);
        co.Execute(contours, delta);
    }
    if (contours.empty())
        // No need to try to offset the holes.
        return 0;

    if (expoly.holes.empty()) {
        // No need to subtract holes from the offsetted expolygon, we are done.
        append(out, std::move(contours));
    } else {
        // 2) Offset the holes one by one, collect the offsetted holes.
        ClipperLib::Paths holes;
        {
            for (const Polygon& hole : expoly.holes) {
                ClipperLib::ClipperOffset co;
                if (joinType == jtRound)
                    co.ArcTolerance = miterLimit;
                else
                    co.MiterLimit = miterLimit;
                co.ShortestEdgeLength = std::abs(delta * ClipperOffsetShortestEdgeFactor);
                co.AddPath(hole.points, joinType, ClipperLib::etClosedPolygon);
                ClipperLib::Paths out2;
                // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
                // contours will be CCW oriented even though the input paths are CW oriented.
                // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
                co.Execute(out2, -delta);
                append(holes, std::move(out2));
            }
        }

        // 3) Subtract holes from the contours.
        if (holes.empty()) {
            // No hole remaining after an offset. Just copy the outer contour.
            append(out, std::move(contours));
        } else if (delta < 0) {
            // Negative offset. There is a chance, that the offsetted hole intersects the outer contour.
            // Subtract the offsetted holes from the offsetted contours.
            if (auto output = clipper_do<ClipperLib::Paths>(ClipperLib::ctDifference, contours, holes, ClipperLib::pftNonZero);
                !output.empty()) {
                append(out, std::move(output));
            } else {
                // The offsetted holes have eaten up the offsetted outer contour.
                return 0;
            }
        } else {
            // Positive offset. As long as the Clipper offset does what one expects it to do, the offsetted hole will have a smaller
            // area than the original hole or even disappear, therefore there will be no new intersections.
            // Just collect the reversed holes.
            out.reserve(contours.size() + holes.size());
            append(out, std::move(contours));
            // Reverse the holes in place.
            for (size_t i = 0; i < holes.size(); ++i)
                std::reverse(holes[i].begin(), holes[i].end());
            append(out, std::move(holes));
        }
    }

    return 1;
}

static int offset_expolygon_inner(
    const Slic3r::Surface& surface, const float delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::Paths& out)
{
    return offset_expolygon_inner(surface.expolygon, delta, joinType, miterLimit, out);
}
static int offset_expolygon_inner(
    const Slic3r::Surface* surface, const float delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::Paths& out)
{
    return offset_expolygon_inner(surface->expolygon, delta, joinType, miterLimit, out);
}

ClipperLib::Paths expolygon_offset(const Slic3r::ExPolygon& expolygon, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    ClipperLib::Paths out;
    offset_expolygon_inner(expolygon, delta, joinType, miterLimit, out);
    return out;
}

// This is a safe variant of the polygons offset, tailored for multiple ExPolygons.
// It is required, that the input expolygons do not overlap and that the holes of each ExPolygon don't intersect with their respective outer
// contours. Each ExPolygon is offsetted separately. For outer offset, the the offsetted ExPolygons shall be united outside of this function.
template<typename ExPolygonVector>
static std::pair<ClipperLib::Paths, size_t> expolygons_offset_raw(const ExPolygonVector& expolygons,
                                                                  const float            delta,
                                                                  ClipperLib::JoinType   joinType,
                                                                  double                 miterLimit)
{
    // Offsetted ExPolygons before they are united.
    ClipperLib::Paths output;
    output.reserve(expolygons.size());
    // How many non-empty offsetted expolygons were actually collected into output?
    // If only one, then there is no need to do a final union.
    size_t expolygons_collected = 0;
    for (const auto& expoly : expolygons)
        expolygons_collected += offset_expolygon_inner(expoly, delta, joinType, miterLimit, output);
    return std::make_pair(std::move(output), expolygons_collected);
}

// See comment on expolygon_offsets_raw. In addition, for positive offset the contours are united.
template<typename ExPolygonVector>
static ClipperLib::Paths expolygons_offset(const ExPolygonVector& expolygons,
                                           const float            delta,
                                           ClipperLib::JoinType   joinType,
                                           double                 miterLimit)
{
    auto [output, expolygons_collected] = expolygons_offset_raw(expolygons, delta, joinType, miterLimit);
    // Unite the offsetted expolygons.
    return expolygons_collected > 1 && delta > 0 ?
               // There is a chance that the outwards offsetted expolygons may intersect. Perform a union.
               clipper_union<ClipperLib::Paths>(output) :
               // Negative offset. The shrunk expolygons shall not mutually intersect. Just copy the output.
               output;
}

// See comment on expolygons_offset_raw. In addition, the polygons are always united to conver to polytree.
template<typename ExPolygonVector>
static ClipperLib::PolyTree expolygons_offset_pt(const ExPolygonVector& expolygons,
                                                 const float            delta,
                                                 ClipperLib::JoinType   joinType,
                                                 double                 miterLimit)
{
    auto [output, expolygons_collected] = expolygons_offset_raw(expolygons, delta, joinType, miterLimit);
    // Unite the offsetted expolygons for both the
    return clipper_union<ClipperLib::PolyTree>(output);
}

Slic3r::Polygons offset(const Slic3r::ExPolygon& expolygon, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(expolygon_offset(expolygon, delta, joinType, miterLimit));
}
Slic3r::Polygons offset(const Slic3r::ExPolygons& expolygons, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(expolygons_offset(expolygons, delta, joinType, miterLimit));
}
Slic3r::Polygons offset(const Slic3r::Surfaces& surfaces, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(expolygons_offset(surfaces, delta, joinType, miterLimit));
}
Slic3r::Polygons offset(const Slic3r::SurfacesPtr& surfaces, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(expolygons_offset(surfaces, delta, joinType, miterLimit));
}
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygon& expolygon, const float delta, ClipperLib::JoinType joinType, double miterLimit)
// FIXME one may spare one Clipper Union call.
{
    return ClipperPaths_to_Slic3rExPolygons(expolygon_offset(expolygon, delta, joinType, miterLimit));
}
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygons& expolygons, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return PolyTreeToExPolygons(expolygons_offset_pt(expolygons, delta, joinType, miterLimit));
}
Slic3r::ExPolygons offset_ex(const Slic3r::Surfaces& surfaces, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return PolyTreeToExPolygons(expolygons_offset_pt(surfaces, delta, joinType, miterLimit));
}
Slic3r::ExPolygons offset_ex(const Slic3r::SurfacesPtr& surfaces, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    return PolyTreeToExPolygons(expolygons_offset_pt(surfaces, delta, joinType, miterLimit));
}

Polygons offset2(const ExPolygons& expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(
        offset_paths<ClipperLib::Paths>(expolygons_offset(expolygons, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
ExPolygons offset2_ex(const ExPolygons& expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    return PolyTreeToExPolygons(
        offset_paths<ClipperLib::PolyTree>(expolygons_offset(expolygons, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
ExPolygons offset2_ex(const Surfaces& surfaces, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    // FIXME it may be more efficient to offset to_expolygons(surfaces) instead of to_polygons(surfaces).
    return PolyTreeToExPolygons(
        offset_paths<ClipperLib::PolyTree>(expolygons_offset(surfaces, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}

// Offset outside, then inside produces morphological closing. All deltas should be positive.
Slic3r::Polygons closing(
    const Slic3r::Polygons& polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return to_polygons(shrink_paths<ClipperLib::Paths>(expand_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta1,
                                                                                       joinType, miterLimit),
                                                       delta2, joinType, miterLimit));
}
Slic3r::ExPolygons closing_ex(
    const Slic3r::Polygons& polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return PolyTreeToExPolygons(shrink_paths<ClipperLib::PolyTree>(expand_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons),
                                                                                                   delta1, joinType, miterLimit),
                                                                   delta2, joinType, miterLimit));
}
Slic3r::ExPolygons closing_ex(
    const Slic3r::Surfaces& surfaces, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    // FIXME it may be more efficient to offset to_expolygons(surfaces) instead of to_polygons(surfaces).
    return PolyTreeToExPolygons(shrink_paths<ClipperLib::PolyTree>(expand_paths<ClipperLib::Paths>(ClipperUtils::SurfacesProvider(surfaces),
                                                                                                   delta1, joinType, miterLimit),
                                                                   delta2, joinType, miterLimit));
}

// Offset inside, then outside produces morphological opening. All deltas should be positive.
Slic3r::Polygons opening(
    const Slic3r::Polygons& polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return to_polygons(expand_paths<ClipperLib::Paths>(shrink_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta1,
                                                                                       joinType, miterLimit),
                                                       delta2, joinType, miterLimit));
}
Slic3r::Polygons opening(
    const Slic3r::ExPolygons& expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return to_polygons(expand_paths<ClipperLib::Paths>(shrink_paths<ClipperLib::Paths>(ClipperUtils::ExPolygonsProvider(expolygons), delta1,
                                                                                       joinType, miterLimit),
                                                       delta2, joinType, miterLimit));
}
Slic3r::Polygons opening(
    const Slic3r::Surfaces& surfaces, const float delta1, const float delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    // FIXME it may be more efficient to offset to_expolygons(surfaces) instead of to_polygons(surfaces).
    return to_polygons(expand_paths<ClipperLib::Paths>(shrink_paths<ClipperLib::Paths>(ClipperUtils::SurfacesProvider(surfaces), delta1,
                                                                                       joinType, miterLimit),
                                                       delta2, joinType, miterLimit));
}

// [INTENT] clipper_do_polytree — fractal pyramid workaround (GitHub issue #117).
// Problem: ClipperLib::JoinCommonEdges() is O(N²) when the PolyTree output mode is used
//   with overlapping edges (e.g., fractal-like geometries where many edges coincide).
//   This caused extremely long slice times on complex models.
// Solution: Two-pass approach:
//   Pass 1: clipper_do<Paths> — fast path, produces flat Paths (no PolyTree). Handles
//           overlapping edges in O(N log N) sweep-line time.
//   Pass 2: clipper_union<PolyTree> — builds PolyTree from the clean Pass 1 output.
//           Since Pass 1 already resolved overlaps, Pass 2 has no overlapping edges,
//           so JoinCommonEdges runs in acceptable time.
// [COUPLING] All *_ex() boolean operation wrappers (diff_ex, intersection_ex, union_ex)
//   route through clipper_do_polytree. Any bug here affects the entire boolean op layer.
// [HAZARD H450] This doubles Clipper calls for every ExPolygon-output operation.
//   The overhead is acceptable for normal geometry but for extremely simple cases
//   (e.g., single-polygon inputs) the double invocation is wasteful.
// Fix of #117: A large fractal pyramid takes ages to slice
// The Clipper library has difficulties processing overlapping polygons.
// Namely, the function ClipperLib::JoinCommonEdges() has potentially a terrible time complexity if the output
// of the operation is of the PolyTree type.
// This function implemenets a following workaround:
// 1) Peform the Clipper operation with the output to Paths. This method handles overlaps in a reasonable time.
// 2) Run Clipper Union once again to extract the PolyTree from the result of 1).
template<typename PathProvider1, typename PathProvider2>
inline ClipperLib::PolyTree clipper_do_polytree(const ClipperLib::ClipType     clipType,
                                                PathProvider1&&                subject,
                                                PathProvider2&&                clip,
                                                const ClipperLib::PolyFillType fillType)
{
    // Perform the operation with the output to input_subject.
    // This pass does not generate a PolyTree, which is a very expensive operation with the current Clipper library
    // if there are overapping edges.
    if (auto output = clipper_do<ClipperLib::Paths>(clipType, subject, clip, fillType); !output.empty())
        // Perform an additional Union operation to generate the PolyTree ordering.
        return clipper_union<ClipperLib::PolyTree>(output, fillType);
    return ClipperLib::PolyTree();
}
template<typename PathProvider1, typename PathProvider2>
inline ClipperLib::PolyTree clipper_do_polytree(const ClipperLib::ClipType     clipType,
                                                PathProvider1&&                subject,
                                                PathProvider2&&                clip,
                                                const ClipperLib::PolyFillType fillType,
                                                const ApplySafetyOffset        do_safety_offset)
{
    assert(do_safety_offset == ApplySafetyOffset::No || clipType != ClipperLib::ctUnion);
    return do_safety_offset == ApplySafetyOffset::Yes ?
               clipper_do_polytree(clipType, std::forward<PathProvider1>(subject), safety_offset(std::forward<PathProvider2>(clip)),
                                   fillType) :
               clipper_do_polytree(clipType, std::forward<PathProvider1>(subject), std::forward<PathProvider2>(clip), fillType);
}

template<class TSubj, class TClip>
static inline Polygons _clipper(ClipperLib::ClipType clipType, TSubj&& subject, TClip&& clip, ApplySafetyOffset do_safety_offset)
{
    return to_polygons(clipper_do<ClipperLib::Paths>(clipType, std::forward<TSubj>(subject), std::forward<TClip>(clip),
                                                     ClipperLib::pftNonZero, do_safety_offset));
}

Slic3r::Polygons diff(const Slic3r::Polygon& subject, const Slic3r::Polygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points),
                    ClipperUtils::SinglePathProvider(clip.points), do_safety_offset);
}
Slic3r::Polygons diff(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons diff_clipped(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return diff(subject, ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(SCALED_EPSILON)),
                do_safety_offset);
}
Slic3r::ExPolygons diff_clipped(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return diff_ex(subject, ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(SCALED_EPSILON)),
                   do_safety_offset);
}
Slic3r::ExPolygons diff_clipped(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return diff_ex(subject, ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(SCALED_EPSILON)),
                   do_safety_offset);
}
Slic3r::Polygons diff(const Slic3r::Polygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons diff(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons diff(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons diff(const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::Polygon& subject, const Slic3r::Polygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points),
                    ClipperUtils::SinglePathProvider(clip.points), do_safety_offset);
}
Slic3r::Polygons intersection_clipped(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return intersection(subject, ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(SCALED_EPSILON)),
                        do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::Polygons& subject, const Slic3r::ExPolygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::ExPolygon& subject, const Slic3r::ExPolygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::ExPolygonProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip),
                    do_safety_offset);
}
Slic3r::Polygons intersection(const Slic3r::Surfaces& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                    do_safety_offset);
}
// BBS
Slic3r::Polygons intersection(const Slic3r::Polygons& subject, const Slic3r::Polygon& clip, ApplySafetyOffset do_safety_offset)
{
    Slic3r::Polygons clip_temp;
    clip_temp.push_back(clip);
    return intersection(subject, clip_temp, do_safety_offset);
}

Slic3r::Polygons union_(const Slic3r::Polygons& subject)
{
    return _clipper(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ApplySafetyOffset::No);
}
Slic3r::Polygons union_(const Slic3r::ExPolygons& subject)
{
    return _clipper(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(),
                    ApplySafetyOffset::No);
}
Slic3r::Polygons union_(const Slic3r::Polygons& subject, const ClipperLib::PolyFillType fillType)
{
    return to_polygons(clipper_do<ClipperLib::Paths>(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject),
                                                     ClipperUtils::EmptyPathsProvider(), fillType, ApplySafetyOffset::No));
}
Slic3r::Polygons union_(const Slic3r::Polygons& subject, const Slic3r::Polygons& subject2)
{
    // BBS
    Polygons polys = subject;
    for (const Polygon& poly : subject2)
        polys.push_back(poly);
    return union_(polys);
}

template<typename TSubject, typename TClip>
static ExPolygons _clipper_ex(ClipperLib::ClipType     clipType,
                              TSubject&&               subject,
                              TClip&&                  clip,
                              ApplySafetyOffset        do_safety_offset,
                              ClipperLib::PolyFillType fill_type = ClipperLib::pftNonZero)
{
    return PolyTreeToExPolygons(
        clipper_do_polytree(clipType, std::forward<TSubject>(subject), std::forward<TClip>(clip), fill_type, do_safety_offset));
}

Slic3r::ExPolygons diff_ex(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons& subject, const Slic3r::Surfaces& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::SurfacesProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::Polygon& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon& subject, const Slic3r::Polygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::SinglePathProvider(clip.points),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject, const Slic3r::Surfaces& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::SurfacesProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces& subject, const Slic3r::Surfaces& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::SurfacesProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesPtrProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesPtrProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
// BBS
inline Slic3r::ExPolygons diff_ex(const Slic3r::Polygon& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    Slic3r::Polygons subject_temp;
    subject_temp.push_back(subject);

    return diff_ex(subject_temp, clip, do_safety_offset);
}

inline Slic3r::ExPolygons diff_ex(const Slic3r::Polygon& subject, const Slic3r::Polygon& clip, ApplySafetyOffset do_safety_offset)
{
    Slic3r::Polygons subject_temp;
    Slic3r::Polygons clip_temp;

    subject_temp.push_back(subject);
    clip_temp.push_back(clip);
    return diff_ex(subject_temp, clip_temp, do_safety_offset);
}

Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon& subject, const Slic3r::ExPolygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::ExPolygonProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces& subject, const Slic3r::Polygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces& subject, const Slic3r::Surfaces& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::SurfacesProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons intersection_ex(const Slic3r::SurfacesPtr& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesPtrProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}
// May be used to "heal" unusual models (3DLabPrints etc.) by providing fill_type (pftEvenOdd, pftNonZero, pftPositive, pftNegative).
Slic3r::ExPolygons union_ex(const Slic3r::Polygons& subject, ClipperLib::PolyFillType fill_type)
{
    return _clipper_ex(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(),
                       ApplySafetyOffset::No, fill_type);
}
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons& subject)
{
    return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject),
                                                    ClipperUtils::EmptyPathsProvider(), ClipperLib::pftNonZero));
}
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& subject2)
{
    return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject),
                                                    ClipperUtils::PolygonsProvider(subject2), ClipperLib::pftNonZero));
}
Slic3r::ExPolygons union_ex(const Slic3r::Surfaces& subject)
{
    return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::SurfacesProvider(subject),
                                                    ClipperUtils::EmptyPathsProvider(), ClipperLib::pftNonZero));
}
// BBS
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons& poly1, const Slic3r::ExPolygons& poly2, bool safety_offset_)
{
    ExPolygons expolys = poly1;
    for (const ExPolygon& expoly : poly2)
        expolys.push_back(expoly);
    return union_ex(expolys);
}

Slic3r::ExPolygons xor_ex(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygon& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctXor, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonProvider(clip),
                       do_safety_offset);
}
Slic3r::ExPolygons xor_ex(const Slic3r::ExPolygons& subject, const Slic3r::ExPolygons& clip, ApplySafetyOffset do_safety_offset)
{
    return _clipper_ex(ClipperLib::ctXor, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip),
                       do_safety_offset);
}

template<typename PathsProvider1, typename PathsProvider2>
Polylines _clipper_pl_open(ClipperLib::ClipType clipType, PathsProvider1&& subject, PathsProvider2&& clip)
{
    ClipperLib::Clipper clipper;
    clipper.AddPaths(std::forward<PathsProvider1>(subject), ClipperLib::ptSubject, false);
    clipper.AddPaths(std::forward<PathsProvider2>(clip), ClipperLib::ptClip, true);
    ClipperLib::PolyTree retval;
    clipper.Execute(clipType, retval, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    return PolyTreeToPolylines(std::move(retval));
}

// [INTENT] _clipper_pl_recombine — reconnects split polyline fragments after a Clipper
// polygon-clipping operation. When a closed polygon is converted to an open path by
// duplicating the first point at the end (see _clipper_pl_closed), Clipper may split
// the path at the duplicated point if it lies inside the clip region. This produces
// two consecutive fragments that should be one polyline.
// [HAZARD H451] O(N²) algorithm: for each polyline i, scans all j > i for endpoint
//   coincidence. For large polyline sets (e.g., hundreds of travel lines being clipped),
//   this is quadratic in the number of output fragments. If one long path is split into
//   K pieces, the inner loop runs K*(K-1)/2 times per outer polyline. No upper bound
//   on K is enforced. A spatial hash or sorted endpoint map would reduce this to O(N log N).
// [HAZARD H452] Uses vector erase inside a nested loop. Each erase() is O(N) (shifts
//   subsequent elements). Combined with the outer loop this is O(N²) data movement
//   in addition to the O(N²) comparisons.
// [HAZARD H453] Clipper does not preserve polyline orientation. All four endpoint
//   combinations are checked (back==front, front==back, front==front, back==back)
//   to handle reversed fragments. However, if both endpoints of i equal both endpoints
//   of j (a zero-length degenerate case), the loop may merge incorrectly.
// If the split_at_first_point() call above happens to split the polygon inside the clipping area
// we would get two consecutive polylines instead of a single one, so we go through them in order
// to recombine continuous polylines.
static void _clipper_pl_recombine(Polylines& polylines)
{
    for (size_t i = 0; i < polylines.size(); ++i) {
        for (size_t j = i + 1; j < polylines.size(); ++j) {
            if (polylines[i].points.back() == polylines[j].points.front()) {
                /* If last point of i coincides with first point of j,
                   append points of j to i and delete j */
                polylines[i].points.insert(polylines[i].points.end(), polylines[j].points.begin() + 1, polylines[j].points.end());
                polylines.erase(polylines.begin() + j);
                --j;
            } else if (polylines[i].points.front() == polylines[j].points.back()) {
                /* If first point of i coincides with last point of j,
                   prepend points of j to i and delete j */
                polylines[i].points.insert(polylines[i].points.begin(), polylines[j].points.begin(), polylines[j].points.end() - 1);
                polylines.erase(polylines.begin() + j);
                --j;
            } else if (polylines[i].points.front() == polylines[j].points.front()) {
                /* Since Clipper does not preserve orientation of polylines,
                   also check the case when first point of i coincides with first point of j. */
                polylines[j].reverse();
                polylines[i].points.insert(polylines[i].points.begin(), polylines[j].points.begin(), polylines[j].points.end() - 1);
                polylines.erase(polylines.begin() + j);
                --j;
            } else if (polylines[i].points.back() == polylines[j].points.back()) {
                /* Since Clipper does not preserve orientation of polylines,
                   also check the case when last point of i coincides with last point of j. */
                polylines[j].reverse();
                polylines[i].points.insert(polylines[i].points.end(), polylines[j].points.begin() + 1, polylines[j].points.end());
                polylines.erase(polylines.begin() + j);
                --j;
            }
        }
    }
}

template<typename PathProvider1, typename PathProvider2>
Polylines _clipper_pl_closed(ClipperLib::ClipType clipType, PathProvider1&& subject, PathProvider2&& clip)
{
    // Transform input polygons into open paths.
    ClipperLib::Paths paths;
    paths.reserve(subject.size());
    for (const Points& poly : subject) {
        // Emplace polygon, duplicate the 1st point.
        paths.push_back({});
        ClipperLib::Path& path = paths.back();
        path.reserve(poly.size() + 1);
        path = poly;
        path.emplace_back(poly.front());
    }
    // perform clipping
    Polylines retval = _clipper_pl_open(clipType, paths, std::forward<PathProvider2>(clip));
    _clipper_pl_recombine(retval);
    return retval;
}

Slic3r::Polylines diff_pl(const Slic3r::Polyline& subject, const Slic3r::Polygons& clip)
{
    return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::PolygonsProvider(clip));
}
Slic3r::Polylines diff_pl(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip)
{
    return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::PolylinesProvider(subject), ClipperUtils::PolygonsProvider(clip));
}
Slic3r::Polylines diff_pl(const Slic3r::Polyline& subject, const Slic3r::ExPolygon& clip)
{
    return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::ExPolygonProvider(clip));
}
Slic3r::Polylines diff_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygon& clip)
{
    return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonProvider(clip));
}
Slic3r::Polylines diff_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygons& clip)
{
    return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonsProvider(clip));
}
Slic3r::Polylines diff_pl(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip)
{
    return _clipper_pl_closed(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polylines& subject, const Slic3r::Polygon& clip)
{
    return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject),
                            ClipperUtils::SinglePathProvider(clip.points));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polyline& subject, const Slic3r::ExPolygon& clip)
{
    return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::ExPolygonProvider(clip));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygon& clip)
{
    return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonProvider(clip));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polyline& subject, const Slic3r::Polygons& clip)
{
    return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points),
                            ClipperUtils::PolygonsProvider(clip));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip)
{
    return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::PolygonsProvider(clip));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygons& clip)
{
    return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonsProvider(clip));
}
Slic3r::Polylines intersection_pl(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip)
{
    return _clipper_pl_closed(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip));
}

Lines _clipper_ln(ClipperLib::ClipType clipType, const Lines& subject, const Polygons& clip)
{
    // convert Lines to Polylines
    Polylines polylines;
    polylines.reserve(subject.size());
    for (const Line& line : subject)
        polylines.emplace_back(Polyline(line.a, line.b));

    // perform operation
    polylines = _clipper_pl_open(clipType, ClipperUtils::PolylinesProvider(polylines), ClipperUtils::PolygonsProvider(clip));

    // convert Polylines to Lines
    Lines retval;
    for (Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline)
        if (polyline->size() >= 2)
            // FIXME It may happen, that Clipper produced a polyline with more than 2 collinear points by clipping a single line with
            // polygons. It is a very rare issue, but it happens, see GH #6933.
            retval.push_back({polyline->front(), polyline->back()});
    return retval;
}

// Convert polygons / expolygons into ClipperLib::PolyTree using ClipperLib::pftEvenOdd, thus union will NOT be performed.
// If the contours are not intersecting, their orientation shall not be modified by union_pt().
ClipperLib::PolyTree union_pt(const Polygons& subject)
{
    return clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject),
                                            ClipperUtils::EmptyPathsProvider(), ClipperLib::pftEvenOdd);
}
ClipperLib::PolyTree union_pt(const ExPolygons& subject)
{
    return clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject),
                                            ClipperUtils::EmptyPathsProvider(), ClipperLib::pftEvenOdd);
}

// Simple spatial ordering of Polynodes
ClipperLib::PolyNodes order_nodes(const ClipperLib::PolyNodes& nodes)
{
    // collect ordering points
    Points ordering_points;
    ordering_points.reserve(nodes.size());

    for (const ClipperLib::PolyNode* node : nodes)
        ordering_points.emplace_back(Point(node->Contour.front().x(), node->Contour.front().y()));

    // perform the ordering
    ClipperLib::PolyNodes ordered_nodes = chain_clipper_polynodes(ordering_points, nodes);

    return ordered_nodes;
}

static void traverse_pt_noholes(const ClipperLib::PolyNodes& nodes, Polygons* out)
{
    foreach_node<e_ordering::ON>(nodes, [&out](const ClipperLib::PolyNode* node) {
        traverse_pt_noholes(node->Childs, out);
        out->emplace_back(node->Contour);
        if (node->IsHole())
            out->back().reverse(); // ccw
    });
}

static void traverse_pt_outside_in(ClipperLib::PolyNodes&& nodes, Polygons* retval)
{
    // collect ordering points
    Points ordering_points;
    ordering_points.reserve(nodes.size());
    for (const ClipperLib::PolyNode* node : nodes)
        ordering_points.emplace_back(node->Contour.front().x(), node->Contour.front().y());

    // Perform the ordering, push results recursively.
    // FIXME pass the last point to chain_clipper_polynodes?
    for (ClipperLib::PolyNode* node : chain_clipper_polynodes(ordering_points, nodes)) {
        retval->emplace_back(std::move(node->Contour));
        if (node->IsHole())
            // Orient a hole, which is clockwise oriented, to CCW.
            retval->back().reverse();
        // traverse the next depth
        traverse_pt_outside_in(std::move(node->Childs), retval);
    }
}

Polygons union_pt_chained_outside_in(const Polygons& subject)
{
    Polygons retval;
    traverse_pt_outside_in(union_pt(subject).Childs, &retval);
    return retval;
}

Polygons simplify_polygons(const Polygons& subject)
{
    ClipperLib::Paths   output;
    ClipperLib::Clipper c;
    //    c.PreserveCollinear(true);
    // FIXME StrictlySimple is very expensive! Is it needed?
    c.StrictlySimple(true);
    c.AddPaths(ClipperUtils::PolygonsProvider(subject), ClipperLib::ptSubject, true);
    c.Execute(ClipperLib::ctUnion, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

    // convert into Slic3r polygons
    return to_polygons(std::move(output));
}

ExPolygons simplify_polygons_ex(const Polygons& subject)
{
    ClipperLib::PolyTree polytree;
    ClipperLib::Clipper  c;
    //    c.PreserveCollinear(true);
    // FIXME StrictlySimple is very expensive! Is it needed?
    c.StrictlySimple(true);
    c.AddPaths(ClipperUtils::PolygonsProvider(subject), ClipperLib::ptSubject, true);
    c.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

    // convert into ExPolygons
    return PolyTreeToExPolygons(std::move(polytree));
}

Polygons top_level_islands(const Slic3r::Polygons& polygons)
{
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    // perform union
    clipper.AddPaths(ClipperUtils::PolygonsProvider(polygons), ClipperLib::ptSubject, true);
    ClipperLib::PolyTree polytree;
    clipper.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);
    // Convert only the top level islands to the output.
    Polygons out;
    out.reserve(polytree.ChildCount());
    for (int i = 0; i < polytree.ChildCount(); ++i)
        out.emplace_back(std::move(polytree.Childs[i]->Contour));
    return out;
}

// Outer offset shall not split the input contour into multiples. It is expected, that the solution will be non empty and it will contain
// just a single polygon.
ClipperLib::Paths fix_after_outer_offset(const ClipperLib::Path& input,
                                         // combination of default prameters to correspond to void ClipperOffset::Execute(Paths& solution,
                                         // double delta) to produce a CCW output contour from CCW input contour for a positive offset.
                                         ClipperLib::PolyFillType filltype, // = ClipperLib::pftPositive
                                         bool                     reverse_result)               // = false
{
    ClipperLib::Paths solution;
    if (!input.empty()) {
        ClipperLib::Clipper clipper;
        clipper.AddPath(input, ClipperLib::ptSubject, true);
        clipper.ReverseSolution(reverse_result);
        clipper.Execute(ClipperLib::ctUnion, solution, filltype, filltype);
    }
    return solution;
}

// Inner offset may split the source contour into multiple contours, but one resulting contour shall not lie inside the other.
ClipperLib::Paths fix_after_inner_offset(const ClipperLib::Path& input,
                                         // combination of default prameters to correspond to void ClipperOffset::Execute(Paths& solution,
                                         // double delta) to produce a CCW output contour from CCW input contour for a negative offset.
                                         ClipperLib::PolyFillType filltype, // = ClipperLib::pftNegative
                                         bool                     reverse_result)               // = true
{
    ClipperLib::Paths solution;
    if (!input.empty()) {
        ClipperLib::Clipper clipper;
        clipper.AddPath(input, ClipperLib::ptSubject, true);
        ClipperLib::IntRect r = clipper.GetBounds();
        r.left -= 10;
        r.top -= 10;
        r.right += 10;
        r.bottom += 10;
        if (filltype == ClipperLib::pftPositive)
            clipper.AddPath({ClipperLib::IntPoint(r.left, r.bottom), ClipperLib::IntPoint(r.left, r.top),
                             ClipperLib::IntPoint(r.right, r.top), ClipperLib::IntPoint(r.right, r.bottom)},
                            ClipperLib::ptSubject, true);
        else
            clipper.AddPath({ClipperLib::IntPoint(r.left, r.bottom), ClipperLib::IntPoint(r.right, r.bottom),
                             ClipperLib::IntPoint(r.right, r.top), ClipperLib::IntPoint(r.left, r.top)},
                            ClipperLib::ptSubject, true);
        clipper.ReverseSolution(reverse_result);
        clipper.Execute(ClipperLib::ctUnion, solution, filltype, filltype);
        if (!solution.empty())
            solution.erase(solution.begin());
    }
    return solution;
}

// [INTENT] mittered_offset_path_scaled — per-vertex variable-width miter offset.
// For each vertex of `contour`, offsets by the corresponding `deltas[i]` using a miter
// join. This is the core primitive for Arachne variable-width perimeter offsets where each
// vertex has an individually computed inset/outset distance.
// Algorithm:
//   For each vertex pt[i]:
//     - Compute normals to the previous and next edges (perp vectors).
//     - Classify the corner as concave, nearly-parallel, or convex.
//     - For concave: emit 3 points (prev-normal, raw vertex, next-normal) to avoid
//       folded geometry.
//     - For nearly-parallel: emit just the prev-normal offset point.
//     - For convex with miter within limit: emit the bisector intersection point.
//     - For convex exceeding miter limit: bevel with two points computed via atan2/tan.
// [STATE] `miter_limit` is remapped internally: values > 2.0 become 2/(ml*ml),
//   values <= 2.0 become 0.5. This means the effective miter threshold is nonlinear
//   and counter-intuitive (higher miter_limit → smaller internal threshold → fewer
//   miter exceptions). Documented as HAZARD H438 in ClipperUtils.hpp.
// [MEMORY] The output `out` is reserved to 2*contour.size() — worst case two output
//   points per input vertex (bevel or concave case). Actual output may be smaller.
// [HAZARD H454] The short-edge skip logic uses `*std::max_element(deltas)` to compute
//   lmin. For mixed-sign deltas (caught in Debug only by H437), max_element may return
//   a very large positive delta when most are negative, leading to an over-aggressive
//   edge skip that may eliminate valid vertices entirely.
// [HAZARD H455] The `sin_min_parallel = 1.0` hardcoded threshold means a corner must
//   have cross-product exactly 1.0 to be classified as truly parallel. Commented-out
//   alternative uses EPSILON + 1/CLIPPER_OFFSET_SCALE. The current value causes
//   corners with cross-product slightly < 1.0 to fall into the convex-corner branch,
//   which is safe but means near-parallel edges get an extra offset point.
ClipperLib::Path mittered_offset_path_scaled(const Points& contour, const std::vector<float>& deltas, double miter_limit)
{
    assert(contour.size() == deltas.size());

#ifndef NDEBUG
    // Verify that the deltas are either all positive, or all negative.
    bool positive = false;
    bool negative = false;
    for (float delta : deltas)
        if (delta < 0.f)
            negative = true;
        else if (delta > 0.f)
            positive = true;
    assert(!(negative && positive));
#endif /* NDEBUG */

    ClipperLib::Path out;

    if (deltas.size() > 2) {
        out.reserve(contour.size() * 2);

        // Clamp miter limit to 2.
        miter_limit = (miter_limit > 2.) ? 2. / (miter_limit * miter_limit) : 0.5;

        // perpenduclar vector
        auto perp = [](const Vec2d& v) -> Vec2d { return Vec2d(v.y(), -v.x()); };

        // Add a new point to the output, scale by CLIPPER_OFFSET_SCALE and round to ClipperLib::cInt.
        auto add_offset_point = [&out](Vec2d pt) {
            pt += Vec2d(0.5 - (pt.x() < 0), 0.5 - (pt.y() < 0));
            out.emplace_back(ClipperLib::cInt(pt.x()), ClipperLib::cInt(pt.y()));
        };

        // Minimum edge length, squared.
        double lmin  = *std::max_element(deltas.begin(), deltas.end()) * ClipperOffsetShortestEdgeFactor;
        double l2min = lmin * lmin;
        // Minimum angle to consider two edges to be parallel.
        // Vojtech's estimate.
        //		const double sin_min_parallel = EPSILON + 1. / double(CLIPPER_OFFSET_SCALE);
        // Implementation equal to Clipper.
        const double sin_min_parallel = 1.;

        // Find the last point further from pt by l2min.
        Vec2d  pt    = contour.front().cast<double>();
        size_t iprev = contour.size() - 1;
        Vec2d  ptprev;
        for (; iprev > 0; --iprev) {
            ptprev = contour[iprev].cast<double>();
            if ((ptprev - pt).squaredNorm() > l2min)
                break;
        }

        if (iprev != 0) {
            size_t ilast = iprev;
            // Normal to the (pt - ptprev) segment.
            Vec2d nprev = perp(pt - ptprev).normalized();
            for (size_t i = 0;;) {
                // Find the next point further from pt by l2min.
                size_t j = i + 1;
                Vec2d  ptnext;
                for (; j <= ilast; ++j) {
                    ptnext    = contour[j].cast<double>();
                    double l2 = (ptnext - pt).squaredNorm();
                    if (l2 > l2min)
                        break;
                }
                if (j > ilast) {
                    assert(i <= ilast);
                    // If the last edge is too short, merge it with the previous edge.
                    i      = ilast;
                    ptnext = contour.front().cast<double>();
                }

                // Normal to the (ptnext - pt) segment.
                Vec2d nnext = perp(ptnext - pt).normalized();

                double delta  = deltas[i];
                double sin_a  = std::clamp(cross2(nprev, nnext), -1., 1.);
                double convex = sin_a * delta;
                if (convex <= -sin_min_parallel) {
                    // Concave corner.
                    add_offset_point(pt + nprev * delta);
                    add_offset_point(pt);
                    add_offset_point(pt + nnext * delta);
                } else {
                    double dot = nprev.dot(nnext);
                    if (convex < sin_min_parallel && dot > 0.) {
                        // Nearly parallel.
                        add_offset_point((nprev.dot(nnext) > 0.) ? (pt + nprev * delta) : pt);
                    } else {
                        // Convex corner, possibly extremely sharp if convex < sin_min_parallel.
                        double r = 1. + dot;
                        if (r >= miter_limit)
                            add_offset_point(pt + (nprev + nnext) * (delta / r));
                        else {
                            double dx     = std::tan(std::atan2(sin_a, dot) / 4.);
                            Vec2d  newpt1 = pt + (nprev - perp(nprev) * dx) * delta;
                            Vec2d  newpt2 = pt + (nnext + perp(nnext) * dx) * delta;
#ifndef NDEBUG
                            Vec2d  vedge     = 0.5 * (newpt1 + newpt2) - pt;
                            double dist_norm = vedge.norm();
                            assert(std::abs(dist_norm - std::abs(delta)) < SCALED_EPSILON);
#endif /* NDEBUG */
                            add_offset_point(newpt1);
                            add_offset_point(newpt2);
                        }
                    }
                }

                if (i == ilast)
                    break;

                ptprev = pt;
                nprev  = nnext;
                pt     = ptnext;
                i      = j;
            }
        }
    }

#if 0
	{
		ClipperLib::Path polytmp(out);
		unscaleClipperPolygon(polytmp);
		Slic3r::Polygon offsetted(std::move(polytmp));
		BoundingBox bbox = get_extents(contour);
		bbox.merge(get_extents(offsetted));
		static int iRun = 0;
		SVG svg(debug_out_path("mittered_offset_path_scaled-%d.svg", iRun ++).c_str(), bbox);
		svg.draw_outline(Polygon(contour), "blue", scale_(0.01));
		svg.draw_outline(offsetted, "red", scale_(0.01));
		svg.draw(contour, "blue", scale_(0.03));
		svg.draw((Points)offsetted, "blue", scale_(0.03));
	}
#endif

    return out;
}

// [INTENT] variable_offset_inner — shrink an ExPolygon with per-vertex delta values.
// deltas[0] = per-vertex offsets for outer contour (all must be <= 0).
// deltas[1..N] = per-vertex offsets for holes[0..N-1] (all must be <= 0).
// Used by Arachne for inward variable-width perimeter generation.
// Three-step algorithm mirrors offset_expolygon_inner:
//   1. Miter-offset contour inward, fix orientation via fix_after_inner_offset.
//   2. Miter-offset each hole inward (which means outward for CW hole geometry),
//      fix orientation via fix_after_outer_offset.
//   3. Subtract the offsetted holes from the offsetted contours via Clipper difference.
// [COUPLING] Depends on mittered_offset_path_scaled for the raw offset and
//   fix_after_inner_offset/fix_after_outer_offset for orientation normalization.
// [HAZARD H456] deltas vector must have exactly expoly.holes.size()+1 entries. This is
//   asserted in Debug only. In Release, mismatched delta vector causes undefined behavior
//   (out-of-bounds access in `deltas[1 + &hole - expoly.holes.data()]`).
Polygons variable_offset_inner(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit)
{
#ifndef NDEBUG
    // Verify that the deltas are all non positive.
    for (const std::vector<float>& ds : deltas)
        for (float delta : ds)
            assert(delta <= 0.);
    assert(expoly.holes.size() + 1 == deltas.size());
#endif /* NDEBUG */

    // 1) Offset the outer contour.
    ClipperLib::Paths contours = fix_after_inner_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit),
                                                        ClipperLib::pftNegative, true);
#ifndef NDEBUG
    for (auto& c : contours)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 2) Offset the holes one by one, collect the results.
    ClipperLib::Paths holes;
    holes.reserve(expoly.holes.size());
    for (const Polygon& hole : expoly.holes)
        append(holes, fix_after_outer_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()], miter_limit),
                                             ClipperLib::pftNegative, false));
#ifndef NDEBUG
    for (auto& c : holes)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 3) Subtract holes from the contours.
    ClipperLib::Paths output;
    if (holes.empty())
        output = std::move(contours);
    else {
        ClipperLib::Clipper clipper;
        clipper.Clear();
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }

    return to_polygons(std::move(output));
}

// [INTENT] variable_offset_outer — expand an ExPolygon with per-vertex delta values.
// deltas[0] = per-vertex offsets for outer contour (all must be >= 0).
// deltas[1..N] = per-vertex offsets for holes[0..N-1] (all must be >= 0).
// Mirror of variable_offset_inner but with reversed orientation logic:
//   contour: fix_after_outer_offset (pftPositive, no reverse)
//   holes:   fix_after_inner_offset (pftPositive, with reverse)
// [HAZARD H457] Comment in source says "Verify that the deltas are all non positive"
//   but the assertion actually checks delta >= 0 (non-negative). This is a copy-paste
//   comment error. The code is correct; the comment is wrong.
Polygons variable_offset_outer(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit)
{
#ifndef NDEBUG
    // Verify that the deltas are all non positive.
    for (const std::vector<float>& ds : deltas)
        for (float delta : ds)
            assert(delta >= 0.);
    assert(expoly.holes.size() + 1 == deltas.size());
#endif /* NDEBUG */

    // 1) Offset the outer contour.
    ClipperLib::Paths contours = fix_after_outer_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit),
                                                        ClipperLib::pftPositive, false);
#ifndef NDEBUG
    for (auto& c : contours)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 2) Offset the holes one by one, collect the results.
    ClipperLib::Paths holes;
    holes.reserve(expoly.holes.size());
    for (const Polygon& hole : expoly.holes)
        append(holes, fix_after_inner_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()], miter_limit),
                                             ClipperLib::pftPositive, true));
#ifndef NDEBUG
    for (auto& c : holes)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 3) Subtract holes from the contours.
    ClipperLib::Paths output;
    if (holes.empty())
        output = std::move(contours);
    else {
        ClipperLib::Clipper clipper;
        clipper.Clear();
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }

    return to_polygons(std::move(output));
}

ExPolygons variable_offset_outer_ex(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit)
{
#ifndef NDEBUG
    // Verify that the deltas are all non positive.
    for (const std::vector<float>& ds : deltas)
        for (float delta : ds)
            assert(delta >= 0.);
    assert(expoly.holes.size() + 1 == deltas.size());
#endif /* NDEBUG */

    // 1) Offset the outer contour.
    ClipperLib::Paths contours = fix_after_outer_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit),
                                                        ClipperLib::pftPositive, false);
#ifndef NDEBUG
    for (auto& c : contours)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 2) Offset the holes one by one, collect the results.
    ClipperLib::Paths holes;
    holes.reserve(expoly.holes.size());
    for (const Polygon& hole : expoly.holes)
        append(holes, fix_after_inner_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()], miter_limit),
                                             ClipperLib::pftPositive, true));
#ifndef NDEBUG
    for (auto& c : holes)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 3) Subtract holes from the contours.
    ExPolygons output;
    if (holes.empty()) {
        output.reserve(contours.size());
        for (ClipperLib::Path& path : contours)
            output.emplace_back(std::move(path));
    } else {
        ClipperLib::Clipper clipper;
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        ClipperLib::PolyTree polytree;
        clipper.Execute(ClipperLib::ctDifference, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        output = PolyTreeToExPolygons(std::move(polytree));
    }

    return output;
}

ExPolygons variable_offset_inner_ex(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit)
{
#ifndef NDEBUG
    // Verify that the deltas are all non positive.
    for (const std::vector<float>& ds : deltas)
        for (float delta : ds)
            assert(delta <= 0.);
    assert(expoly.holes.size() + 1 == deltas.size());
#endif /* NDEBUG */

    // 1) Offset the outer contour.
    ClipperLib::Paths contours = fix_after_inner_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit),
                                                        ClipperLib::pftNegative, true);
#ifndef NDEBUG
    for (auto& c : contours)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 2) Offset the holes one by one, collect the results.
    ClipperLib::Paths holes;
    holes.reserve(expoly.holes.size());
    for (const Polygon& hole : expoly.holes)
        append(holes, fix_after_outer_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()], miter_limit),
                                             ClipperLib::pftNegative, false));
#ifndef NDEBUG
    for (auto& c : holes)
        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

    // 3) Subtract holes from the contours.
    ExPolygons output;
    if (holes.empty()) {
        output.reserve(contours.size());
        for (ClipperLib::Path& path : contours)
            output.emplace_back(std::move(path));
    } else {
        ClipperLib::Clipper clipper;
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        ClipperLib::PolyTree polytree;
        clipper.Execute(ClipperLib::ctDifference, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        output = PolyTreeToExPolygons(std::move(polytree));
    }

    return output;
}

// [INTENT] make_counter_clockwise — ensures a Pointfs (double-coordinate polygon)
// is wound counter-clockwise. Delegates to Polygon::new_scale() to convert to int32
// scaled coordinates for Clipper-compatible is_clockwise() test.
// [HAZARD H439] (documented in .hpp) Polygon::new_scale() converts double mm → int32
// scaled units. For coordinates > ~2147mm (SCALED_EPSILON * INT32_MAX boundary),
// the int32 cast overflows, corrupting the winding test result. Bed-scale polygons
// on large-format printers (300mm+ beds) are safe, but theoretical 2147mm+ would not be.
// [COUPLING] Only called from bed shape/polygon utilities, not the slicer hot path.
Pointfs make_counter_clockwise(const Pointfs& pointfs)
{
    Pointfs ps = pointfs;
    if (Polygon::new_scale(pointfs).is_clockwise()) {
        std::reverse(ps.begin(), ps.end());
    }

    return ps;
}

} // namespace Slic3r
