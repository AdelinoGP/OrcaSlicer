// [INTENT] VoronoiDiagram: wrapper around boost::polygon::voronoi_diagram<double> that adds
//   self-repair logic for degenerate inputs. Repair is attempted by rotating input segments
//   by empirically chosen angles (PI/6, PI/5, PI/7, PI/11) to escape near-degenerate configurations.
// [COUPLING] Depends on VoronoiUtils (cell-range helpers, is_finite), VoronoiUtilsCgal (planarity
//   check), and Arachne::PolygonsSegmentIndex (one of the three explicit template instantiations).
// [STATE] After construction: m_is_modified=false → accessors delegate to m_voronoi_diagram.
//   After repair: m_is_modified=true → accessors serve from local m_vertices/m_edges/m_cells copies.
// [HAZARD] H583 (Medium): is_valid() returns true for State::REPAIR_NOT_NEEDED, REPAIR_SUCCESSFUL,
//   and UNKNOWN — callers treating UNKNOWN as valid may operate on a diagram with undetected issues
//   (points-only and mixed overloads always set State::UNKNOWN and skip all checks).
#ifndef slic3r_Geometry_Voronoi_hpp_
#define slic3r_Geometry_Voronoi_hpp_

#include <boost/polygon/polygon.hpp>
#include <cstddef>
#include <iterator>
#include <vector>

#include "../Line.hpp"
#include "../Polyline.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#ifdef _MSC_VER
// Suppress warning C4146 in OpenVDB: unary minus operator applied to unsigned type, result still unsigned
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include "boost/polygon/voronoi.hpp"

namespace boost { namespace polygon {
template<typename Segment> struct segment_traits;
}} // namespace boost::polygon
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

namespace Slic3r::Geometry {

// [INTENT] VoronoiDiagram wraps boost::polygon::voronoi_diagram and provides three construct_voronoi
//   overloads: (1) segments-only with optional repair, (2) points-only (no repair), (3) mixed
//   points+segments (no repair). Repair consists of detect → rotate → rebuild → detect cycle.
// [MEMORY] Two storage paths: original m_voronoi_diagram (Boost-owned) and local copies
//   (m_vertices, m_edges, m_cells). Only one path is live at a time, gated by m_is_modified.
//   copy_to_local() deep-copies all Boost VD structs, then clears m_voronoi_diagram to free memory.
// [CONCURRENCY] No synchronisation. Construction is single-threaded; callers in Arachne and MedialAxis
//   instantiate one VoronoiDiagram per thread via TBB parallel_for loops.
class VoronoiDiagram
{
public:
    // [INTENT] coord_type=double matches Boost VD's floating-point output. Input segments use
    //   coord_t (int64 scaled integers) via segment_traits specialisation below.
    using coord_type           = double;
    using voronoi_diagram_type = boost::polygon::voronoi_diagram<coord_type>;
    using point_type           = boost::polygon::point_data<voronoi_diagram_type::coordinate_type>;
    using segment_type         = boost::polygon::segment_data<voronoi_diagram_type::coordinate_type>;
    using rect_type            = boost::polygon::rectangle_data<voronoi_diagram_type::coordinate_type>;

    using coordinate_type = voronoi_diagram_type::coordinate_type;
    using vertex_type     = voronoi_diagram_type::vertex_type;
    using edge_type       = voronoi_diagram_type::edge_type;
    using cell_type       = voronoi_diagram_type::cell_type;

    using const_vertex_iterator = voronoi_diagram_type::const_vertex_iterator;
    using const_edge_iterator   = voronoi_diagram_type::const_edge_iterator;
    using const_cell_iterator   = voronoi_diagram_type::const_cell_iterator;

    using vertex_container_type = voronoi_diagram_type::vertex_container_type;
    using edge_container_type   = voronoi_diagram_type::edge_container_type;
    using cell_container_type   = voronoi_diagram_type::cell_container_type;

    // [INTENT] IssueType enumerates the specific Boost VD pathologies observed in production.
    //   UNKNOWN is the sentinel used when repair was disabled (points-only/mixed overloads).
    // [HAZARD] H584 (Low): UNKNOWN is used for two distinct semantics — "disabled" and "unexpected
    //   issue type" in the error-log branch of construct_voronoi. Same enum value, different meaning.
    enum class IssueType {
        NO_ISSUE_DETECTED,
        FINITE_EDGE_WITH_NON_FINITE_VERTEX,
        MISSING_VORONOI_VERTEX,
        NON_PLANAR_VORONOI_DIAGRAM,
        VORONOI_EDGE_INTERSECTING_INPUT_SEGMENT,
        PARABOLIC_VORONOI_EDGE_WITHOUT_FOCUS_POINT,
        UNKNOWN // Repairs are disabled in the constructor.
    };

    // [INTENT] State summarises the repair outcome for the segments-only overload.
    //   UNKNOWN means the points-only or mixed overload was used; no attempt was made.
    enum class State {
        REPAIR_NOT_NEEDED,   // The original Voronoi diagram doesn't have any issue.
        REPAIR_SUCCESSFUL,   // The original Voronoi diagram has some issues, but it was repaired.
        REPAIR_UNSUCCESSFUL, // The original Voronoi diagram has some issues, but it wasn't repaired.
        UNKNOWN              // Repairs are disabled in the constructor.
    };

    VoronoiDiagram() = default;

    virtual ~VoronoiDiagram() = default;

    IssueType get_issue_type() const { return m_issue_type; }

    State get_state() const { return m_state; }

    // [INTENT] is_valid() allows callers to skip downstream work on irreparably broken diagrams.
    // [HAZARD] H583 (see above): UNKNOWN state passes is_valid() — points-only/mixed callers never fail.
    bool is_valid() const { return m_state != State::REPAIR_UNSUCCESSFUL; }

    void clear();

    // [INTENT] Accessors transparently dispatch to either the original Boost VD (m_is_modified=false)
    //   or the local deep copies (m_is_modified=true). Callers never need to know which path is live.
    const vertex_container_type& vertices() const { return m_is_modified ? m_vertices : m_voronoi_diagram.vertices(); }

    const edge_container_type& edges() const { return m_is_modified ? m_edges : m_voronoi_diagram.edges(); }

    const cell_container_type& cells() const { return m_is_modified ? m_cells : m_voronoi_diagram.cells(); }

    std::size_t num_vertices() const { return m_is_modified ? m_vertices.size() : m_voronoi_diagram.num_vertices(); }

    std::size_t num_edges() const { return m_is_modified ? m_edges.size() : m_voronoi_diagram.num_edges(); }

    std::size_t num_cells() const { return m_is_modified ? m_cells.size() : m_voronoi_diagram.num_cells(); }

    // [INTENT] Segments-only overload: build VD, detect issues, attempt repair if needed.
    //   try_to_repair_if_needed=false skips all detection and repair (sets UNKNOWN state).
    // [COUPLING] Explicitly instantiated in Voronoi.cpp for LinesIt, ColoredLinesConstIt,
    //   PolygonsSegmentIndexConstIt — template body lives in Voronoi.cpp, not here.
    template<typename SegmentIterator>
    typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        void>::type
    construct_voronoi(SegmentIterator segment_begin, SegmentIterator segment_end, bool try_to_repair_if_needed = true);

    // [INTENT] Points-only overload: no repair, always sets State::UNKNOWN / IssueType::UNKNOWN.
    //   Delegates directly to boost::polygon::construct_voronoi.
    template<typename PointIterator>
    typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_point_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<PointIterator>::value_type>::type>::type>::type,
        void>::type
    construct_voronoi(const PointIterator first, const PointIterator last)
    {
        boost::polygon::construct_voronoi(first, last, &m_voronoi_diagram);
        m_state      = State::UNKNOWN;
        m_issue_type = IssueType::UNKNOWN;
    }

    // [INTENT] Mixed points+segments overload: no repair, always sets State::UNKNOWN.
    template<typename PointIterator, typename SegmentIterator>
    typename boost::polygon::enable_if<
        typename boost::polygon::gtl_and<
            typename boost::polygon::gtl_if<typename boost::polygon::is_point_concept<
                typename boost::polygon::geometry_concept<typename std::iterator_traits<PointIterator>::value_type>::type>::type>::type,
            typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<typename boost::polygon::geometry_concept<
                typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type>::type,
        void>::type
    construct_voronoi(const PointIterator p_first, const PointIterator p_last, const SegmentIterator s_first, const SegmentIterator s_last)
    {
        boost::polygon::construct_voronoi(p_first, p_last, s_first, s_last, &m_voronoi_diagram);
        m_state      = State::UNKNOWN;
        m_issue_type = IssueType::UNKNOWN;
    }

    // Try to detect cases when some Voronoi vertex is missing, when the Voronoi diagram
    // is not planar or some Voronoi edge is intersecting input segment.
    // [INTENT] Public static helper used by Arachne/MedialAxis callers that build VD externally
    //   and want to check validity without going through the construct_voronoi wrapper.
    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        IssueType>::type
    detect_known_issues(const VoronoiDiagram& voronoi_diagram, SegmentIterator segment_begin, SegmentIterator segment_end);

    // [INTENT] Single-angle rotation repair: rotate segments by fix_angle, rebuild VD, copy to local,
    //   detect issues in rotated space, then rotate vertices back. Endpoint vertices are snapped to
    //   original integer coordinates via encode/decode color trick to avoid accumulating float error.
    template<typename SegmentIterator>
    typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        VoronoiDiagram::IssueType>::type
    try_to_repair_degenerated_voronoi_diagram_by_rotation(SegmentIterator segment_begin, SegmentIterator segment_end, double fix_angle);

    // [INTENT] Outer repair loop: tries fix_angles = {PI/6, PI/5, PI/7, PI/11} in order;
    //   stops and returns NO_ISSUE_DETECTED on the first successful repair.
    template<typename SegmentIterator>
    typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        VoronoiDiagram::IssueType>::type
    try_to_repair_degenerated_voronoi_diagram(SegmentIterator segment_begin, SegmentIterator segment_end);

private:
    // [INTENT] Internal segment type used exclusively during rotation repair. Stores Slic3r::Point
    //   (integer scaled coords) so that endpoint snapping back to original coords is exact.
    struct Segment
    {
        Point from;
        Point to;

        Segment() = delete;
        explicit Segment(const Point& from, const Point& to) : from(from), to(to) {}
    };

    // [INTENT] Deep-copies voronoi_diagram into m_vertices/m_edges/m_cells, rebuilding all internal
    //   pointers by index arithmetic. Sets m_is_modified=true; clears m_voronoi_diagram to free memory.
    // [HAZARD] H585 (Low): pointer rebuild assumes contiguous storage in Boost VD containers
    //   (uses pointer subtraction to compute indices). If Boost ever changes to non-contiguous
    //   containers this breaks silently with UB pointer arithmetic.
    void copy_to_local(voronoi_diagram_type& voronoi_diagram);

    // Detect issues related to Voronoi cells, or that can be detected by iterating over Voronoi cells.
    // The first type of issue that can be detected is a missing Voronoi vertex, especially when it is
    // missing at one of the endpoints of the input segment.
    // The second type of issue that can be detected is a Voronoi edge that intersects the input segment.
    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        IssueType>::type
    detect_known_voronoi_cell_issues(const VoronoiDiagram& voronoi_diagram, SegmentIterator segment_begin, SegmentIterator segment_end);

    // Detect issues related to Voronoi edges, or that can be detected by iterating over Voronoi edges.
    // The first type of issue that can be detected is a finite Voronoi edge with a non-finite vertex.
    // The second type of issue that can be detected is a parabolic Voronoi edge without a focus point (produced by two segments).
    static IssueType detect_known_voronoi_edge_issues(const VoronoiDiagram& voronoi_diagram);

    // [STATE] m_voronoi_diagram: live when m_is_modified=false (original Boost VD, unrepaired).
    //   m_vertices/m_edges/m_cells: live when m_is_modified=true (post-repair local copy).
    //   m_state / m_issue_type: both UNKNOWN until construct_voronoi(segments) is called with repair.
    voronoi_diagram_type  m_voronoi_diagram;
    vertex_container_type m_vertices;
    edge_container_type   m_edges;
    cell_container_type   m_cells;
    bool                  m_is_modified = false;
    State                 m_state       = State::UNKNOWN;
    IssueType             m_issue_type  = IssueType::UNKNOWN;

public:
    using SegmentIt = std::vector<Slic3r::Geometry::VoronoiDiagram::Segment>::iterator;

    friend struct boost::polygon::segment_traits<Slic3r::Geometry::VoronoiDiagram::Segment>;
};

} // namespace Slic3r::Geometry

// [INTENT] Boost.Polygon concept/traits specialisations for VoronoiDiagram::Segment so that
//   the internal Segment type (used during repair) can be passed directly to construct_voronoi.
//   coordinate_type is coord_t (int64 scaled) matching OrcaSlicer's integer geometry system.
// [COUPLING] This specialisation is what allows VoronoiDiagram::Segment to satisfy
//   boost::polygon::is_segment_concept, enabling the rotation repair loop to call
//   boost::polygon::construct_voronoi on the rotated copy.
namespace boost::polygon {
template<> struct geometry_concept<Slic3r::Geometry::VoronoiDiagram::Segment>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::Geometry::VoronoiDiagram::Segment>
{
    using coordinate_type = coord_t;
    using point_type      = Slic3r::Point;
    using segment_type    = Slic3r::Geometry::VoronoiDiagram::Segment;

    static inline point_type get(const segment_type& segment, direction_1d dir) { return dir.to_int() ? segment.to : segment.from; }
};
} // namespace boost::polygon

#endif // slic3r_Geometry_Voronoi_hpp_
