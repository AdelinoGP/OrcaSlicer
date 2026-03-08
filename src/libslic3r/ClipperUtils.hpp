#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

// [INTENT] ClipperUtils.hpp — type-safe wrapper layer over ClipperLib (Angus Johnson's polygon
// clipping library). All geometry operations in OrcaSlicer that involve 2-D polygon Boolean
// algebra (union, difference, intersection, XOR) and Minkowski-sum offsets flow through this
// file. It also exposes morphological operators (opening, closing) and variable-width miter
// offsets used by the Arachne perimeter generator.
//
// [COUPLING] Virtually every module in libslic3r includes this header:
//   GCode, PerimeterGenerator, SupportMaterial, TreeSupport, Fill/*, LayerRegion, etc.
//   Changing any of the public API signatures here has cascading compilation impact across the
//   entire codebase.
//
// [STATE] This header is stateless — no mutable globals. All functions take polygons by value
// or const-ref and return new polygon collections.  State lives in the caller.
//
// [MEMORY] All polygon types (Polygons, ExPolygons, etc.) are std::vector-based and returned
// by value.  ClipperLib::PolyTree is heap-allocated by ClipperLib itself and moved through
// PolyTreeToExPolygons().  No raw owning pointers are exposed in this header.
//
// [CONCURRENCY] The free functions are stateless and thread-safe at the call level.
// However ClipperLib objects (Clipper, ClipperOffset) are NOT thread-safe internally;
// each call creates its own local Clipper instance so concurrent calls on different inputs
// are safe.
//
// [HAZARD H422] DefaultMiterLimit = 3.0 is acknowledged in a FIXME comment as "extreme".
// Cura uses 1.2. A high miter limit extends sharp corners excessively for large positive or
// negative offsets. A port should expose this as a parameter or lower the default.

#include "libslic3r.h"
#include "clipper.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Surface.hpp"

// import these wherever we're included
using Slic3r::ClipperLib::jtMiter;
using Slic3r::ClipperLib::jtRound;
using Slic3r::ClipperLib::jtSquare;

namespace Slic3r {

// [INTENT] Safety offset applied to *clipping* polygons only (not the subject) before diff/
// intersection operations.  The 10 nm outward nudge prevents Clipper from discarding
// subject edges that lie exactly on a clip boundary.
// [HAZARD H423] If both subject and clip share a common boundary edge, the safety offset
// moves the clip outward by 10 nm, meaning the subject edge is NOT clipped away.  This is
// the desired behaviour for perimeter/infill clipping but can silently leave a 10 nm sliver
// of "unclipped" subject in geometrically sensitive operations (e.g., bridge detection).
static constexpr const float ClipperSafetyOffset = 10.f;

// [INTENT] Default join type for closed-polygon offsets.  jtMiter extends corners as a
// sharp spike up to MiterLimit * offset distance before falling back to jtSquare behaviour.
static constexpr const Slic3r::ClipperLib::JoinType DefaultJoinType = Slic3r::ClipperLib::jtMiter;

// [INTENT] Default end type for open-path (polyline) offsets.  etOpenButt produces a flat
// cap at each end — correct for extrusion paths.
static constexpr const Slic3r::ClipperLib::EndType DefaultEndType = Slic3r::ClipperLib::etOpenButt;

// FIXME evaluate the default miter limit. 3 seems to be extreme, Cura uses 1.2.
//  Mitter Limit 3 is useful for perimeter generator, where sharp corners are extruded without needing a gap fill.
//  However such a high limit causes issues with large positive or negative offsets, where a sharp corner
//  is extended excessively.
//  [HAZARD H422] See file-level note above.
static constexpr const double DefaultMiterLimit = 3.;

// [INTENT] Join type for open-path offsets: jtSquare caps are geometrically predictable and
// avoid spikes at polyline endpoints.
static constexpr const Slic3r::ClipperLib::JoinType DefaultLineJoinType = Slic3r::ClipperLib::jtSquare;
// Miter limit is ignored for jtSquare.
static constexpr const double DefaultLineMiterLimit = 0.;

// [INTENT] During ClipperOffset operations, edges shorter than
// (offset_distance * ClipperOffsetShortestEdgeFactor) are skipped.  This prevents
// degenerate tiny facets from being generated at near-collinear corners.
// [HAZARD H424] The 0.005 factor was tuned empirically.  For very large offsets (e.g.,
// support interface expansion of several mm) the minimum edge length becomes ~5 µm, which
// is below the Clipper integer grid resolution of 1 µm.  For very small offsets (thin-wall
// perimeter spacing <0.05mm) the factor would filter out legitimate short edges.
static constexpr const double ClipperOffsetShortestEdgeFactor = 0.005;

// [INTENT] Enum used as a named boolean parameter to diff/intersection overloads to indicate
// whether the 10 nm safety offset should be applied to the clipping polygon.
// ApplySafetyOffset::Yes should be used whenever the clip and subject polygons may share a
// common boundary edge.
enum class ApplySafetyOffset { No, Yes };

namespace ClipperUtils {
// [INTENT] PathsProviderIteratorBase — CRTP-like base for lazy polygon iterators.
// These iterators allow the Clipper wrapper templates (clipper_do, raw_offset, etc.)
// to accept heterogeneous polygon collections (Polygons, ExPolygons, Surfaces,
// SurfacesPtr, single paths) without copying the polygon data into a common
// ClipperLib::Paths container first.
//
// [MEMORY] Iterator holds a const reference or const pointer to the source collection.
// The source must outlive all iterators derived from it — no lifetime extension.
//
// [COUPLING] All ClipperLib template wrappers in ClipperUtils.cpp accept these
// iterators via template parameters; the contract is iterator::value_type == Points.
class PathsProviderIteratorBase
{
public:
    using value_type        = Points;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const Points*;
    using reference         = const Points&;
    using iterator_category = std::input_iterator_tag;
};

// [INTENT] EmptyPathsProvider — sentinel provider that yields zero paths.
// Used as the "clip" argument for union_ operations where no clip polygon exists.
// operator*() asserts(false) — iterating past end() is always a bug.
class EmptyPathsProvider
{
public:
    struct iterator : public PathsProviderIteratorBase
    {
    public:
        const Points& operator*()
        {
            assert(false);
            return s_empty_points;
        }
        // all iterators point to end.
        constexpr bool operator==(const iterator& rhs) const { return true; }
        constexpr bool operator!=(const iterator& rhs) const { return false; }
        const Points&  operator++(int)
        {
            assert(false);
            return s_empty_points;
        }
        const iterator& operator++()
        {
            assert(false);
            return *this;
        }
    };

    constexpr EmptyPathsProvider() {}
    static constexpr iterator cend() throw() { return iterator{}; }
    static constexpr iterator end() throw() { return cend(); }
    static constexpr iterator cbegin() throw() { return cend(); }
    static constexpr iterator begin() throw() { return cend(); }
    static constexpr size_t   size() throw() { return 0; }

    static Points s_empty_points;
};

// [INTENT] SinglePathProvider — wraps a single Points vector as a one-element sequence.
// Used for single-Polygon overloads of diff(), intersection(), offset(), etc.
// [MEMORY] Holds a const Points& — caller must keep the Points alive for the provider's lifetime.
class SinglePathProvider
{
public:
    SinglePathProvider(const Points& points) : m_points(points) {}

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(const Points& points) : m_ptr(&points) {}
        const Points& operator*() const { return *m_ptr; }
        bool          operator==(const iterator& rhs) const { return m_ptr == rhs.m_ptr; }
        bool          operator!=(const iterator& rhs) const { return !(*this == rhs); }
        const Points& operator++(int)
        {
            auto out = m_ptr;
            m_ptr    = &s_end;
            return *out;
        }
        iterator& operator++()
        {
            m_ptr = &s_end;
            return *this;
        }

    private:
        const Points* m_ptr;
    };

    iterator cbegin() const { return iterator(m_points); }
    iterator begin() const { return this->cbegin(); }
    iterator cend() const { return iterator(s_end); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return 1; }

private:
    const Points& m_points;
    static Points s_end;
};

// [INTENT] PathsProvider<PathType> — wraps a std::vector<PathType> as a flat sequence of
// Points.  PathType must have an implicit conversion to Points (used for Polyline, etc.)
// [MEMORY] Holds a const reference to the source vector.
template<typename PathType> class PathsProvider
{
public:
    PathsProvider(const std::vector<PathType>& paths) : m_paths(paths) {}

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(typename std::vector<PathType>::const_iterator it) : m_it(it) {}
        const Points& operator*() const { return *m_it; }
        bool          operator==(const iterator& rhs) const { return m_it == rhs.m_it; }
        bool          operator!=(const iterator& rhs) const { return !(*this == rhs); }
        const Points& operator++(int) { return *(m_it++); }
        iterator&     operator++()
        {
            ++m_it;
            return *this;
        }

    private:
        typename std::vector<PathType>::const_iterator m_it;
    };

    iterator cbegin() const { return iterator(m_paths.begin()); }
    iterator begin() const { return this->cbegin(); }
    iterator cend() const { return iterator(m_paths.end()); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return m_paths.size(); }

private:
    const std::vector<PathType>& m_paths;
};

// [INTENT] MultiPointsProvider<MultiPointsType> — exposes the .points member of each
// element in a collection of multi-point types (Polygons, Polylines).
// Used for PolygonsProvider and PolylinesProvider aliases below.
template<typename MultiPointsType> class MultiPointsProvider
{
public:
    MultiPointsProvider(const MultiPointsType& multipoints) : m_multipoints(multipoints) {}

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(typename MultiPointsType::const_iterator it) : m_it(it) {}
        const Points& operator*() const { return m_it->points; }
        bool          operator==(const iterator& rhs) const { return m_it == rhs.m_it; }
        bool          operator!=(const iterator& rhs) const { return !(*this == rhs); }
        const Points& operator++(int) { return (m_it++)->points; }
        iterator&     operator++()
        {
            ++m_it;
            return *this;
        }

    private:
        typename MultiPointsType::const_iterator m_it;
    };

    iterator cbegin() const { return iterator(m_multipoints.begin()); }
    iterator begin() const { return this->cbegin(); }
    iterator cend() const { return iterator(m_multipoints.end()); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return m_multipoints.size(); }

private:
    const MultiPointsType& m_multipoints;
};

using PolygonsProvider  = MultiPointsProvider<Polygons>;
using PolylinesProvider = MultiPointsProvider<Polylines>;

// [INTENT] ExPolygonProvider — iterates over a single ExPolygon as a flat sequence of
// ClipperLib paths: contour first (index 0), then holes (indices 1..N).
// This is the ClipperLib path encoding: contour is CCW, holes are CW.
// [HAZARD H425] The iterator exposes holes with their original CW orientation.
// ClipperLib requires holes to be CW for correct pftNonZero fill classification.
// Any caller that reverses the iterator output without adjusting orientation will
// incorrectly classify holes as outer contours.
struct ExPolygonProvider
{
    ExPolygonProvider(const ExPolygon& expoly) : m_expoly(expoly) {}

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(const ExPolygon& expoly, int idx) : m_expoly(expoly), m_idx(idx) {}
        const Points& operator*() const { return (m_idx == 0) ? m_expoly.contour.points : m_expoly.holes[m_idx - 1].points; }
        bool          operator==(const iterator& rhs) const
        {
            assert(m_expoly == rhs.m_expoly);
            return m_idx == rhs.m_idx;
        }
        bool          operator!=(const iterator& rhs) const { return !(*this == rhs); }
        const Points& operator++(int)
        {
            const Points& out = **this;
            ++m_idx;
            return out;
        }
        iterator& operator++()
        {
            ++m_idx;
            return *this;
        }

    private:
        const ExPolygon& m_expoly;
        int              m_idx;
    };

    iterator cbegin() const { return iterator(m_expoly, 0); }
    iterator begin() const { return this->cbegin(); }
    // [INTENT] cend() points one past the last hole (holes.size() + 1 total paths).
    iterator cend() const { return iterator(m_expoly, m_expoly.holes.size() + 1); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return m_expoly.holes.size() + 1; }

private:
    const ExPolygon& m_expoly;
};

// [INTENT] ExPolygonsProvider — iterates over an ExPolygons collection as a flat sequence
// of ClipperLib paths: for each ExPolygon, contour (idx 0) then holes (idx 1..N).
// size() is pre-computed in the constructor for O(1) access.
struct ExPolygonsProvider
{
    ExPolygonsProvider(const ExPolygons& expolygons) : m_expolygons(expolygons)
    {
        m_size = 0;
        for (const ExPolygon& expoly : expolygons)
            m_size += expoly.holes.size() + 1;
    }

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(ExPolygons::const_iterator it) : m_it_expolygon(it), m_idx_contour(0) {}
        const Points& operator*() const
        {
            return (m_idx_contour == 0) ? m_it_expolygon->contour.points : m_it_expolygon->holes[m_idx_contour - 1].points;
        }
        bool operator==(const iterator& rhs) const { return m_it_expolygon == rhs.m_it_expolygon && m_idx_contour == rhs.m_idx_contour; }
        bool operator!=(const iterator& rhs) const { return !(*this == rhs); }
        iterator& operator++()
        {
            if (++m_idx_contour == m_it_expolygon->holes.size() + 1) {
                ++m_it_expolygon;
                m_idx_contour = 0;
            }
            return *this;
        }
        const Points& operator++(int)
        {
            const Points& out = **this;
            ++(*this);
            return out;
        }

    private:
        ExPolygons::const_iterator m_it_expolygon;
        size_t                     m_idx_contour;
    };

    iterator cbegin() const { return iterator(m_expolygons.cbegin()); }
    iterator begin() const { return this->cbegin(); }
    iterator cend() const { return iterator(m_expolygons.cend()); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return m_size; }

private:
    const ExPolygons& m_expolygons;
    size_t            m_size;
};

// [INTENT] SurfacesProvider — same as ExPolygonsProvider but for Surfaces (which wrap an
// expolygon in a Surface struct with a surface_type tag). The type tag is ignored for
// ClipperLib operations; only the geometry is forwarded.
struct SurfacesProvider
{
    SurfacesProvider(const Surfaces& surfaces) : m_surfaces(surfaces)
    {
        m_size = 0;
        for (const Surface& surface : surfaces)
            m_size += surface.expolygon.holes.size() + 1;
    }

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(Surfaces::const_iterator it) : m_it_surface(it), m_idx_contour(0) {}
        const Points& operator*() const
        {
            return (m_idx_contour == 0) ? m_it_surface->expolygon.contour.points : m_it_surface->expolygon.holes[m_idx_contour - 1].points;
        }
        bool      operator==(const iterator& rhs) const { return m_it_surface == rhs.m_it_surface && m_idx_contour == rhs.m_idx_contour; }
        bool      operator!=(const iterator& rhs) const { return !(*this == rhs); }
        iterator& operator++()
        {
            if (++m_idx_contour == m_it_surface->expolygon.holes.size() + 1) {
                ++m_it_surface;
                m_idx_contour = 0;
            }
            return *this;
        }
        const Points& operator++(int)
        {
            const Points& out = **this;
            ++(*this);
            return out;
        }

    private:
        Surfaces::const_iterator m_it_surface;
        size_t                   m_idx_contour;
    };

    iterator cbegin() const { return iterator(m_surfaces.cbegin()); }
    iterator begin() const { return this->cbegin(); }
    iterator cend() const { return iterator(m_surfaces.cend()); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return m_size; }

private:
    const Surfaces& m_surfaces;
    size_t          m_size;
};

// [INTENT] SurfacesPtrProvider — same as SurfacesProvider but for a vector of raw Surface*
// pointers.  Used where the caller owns Surface objects by pointer (e.g., SupportMaterial.cpp).
// [HAZARD H426] Raw pointer vector — the pointed-to Surface objects must remain valid for
// the provider's lifetime.  No lifetime tracking is performed.
struct SurfacesPtrProvider
{
    SurfacesPtrProvider(const SurfacesPtr& surfaces) : m_surfaces(surfaces)
    {
        m_size = 0;
        for (const Surface* surface : surfaces)
            m_size += surface->expolygon.holes.size() + 1;
    }

    struct iterator : public PathsProviderIteratorBase
    {
    public:
        explicit iterator(SurfacesPtr::const_iterator it) : m_it_surface(it), m_idx_contour(0) {}
        const Points& operator*() const
        {
            return (m_idx_contour == 0) ? (*m_it_surface)->expolygon.contour.points :
                                          (*m_it_surface)->expolygon.holes[m_idx_contour - 1].points;
        }
        bool      operator==(const iterator& rhs) const { return m_it_surface == rhs.m_it_surface && m_idx_contour == rhs.m_idx_contour; }
        bool      operator!=(const iterator& rhs) const { return !(*this == rhs); }
        iterator& operator++()
        {
            if (++m_idx_contour == (*m_it_surface)->expolygon.holes.size() + 1) {
                ++m_it_surface;
                m_idx_contour = 0;
            }
            return *this;
        }
        const Points& operator++(int)
        {
            const Points& out = **this;
            ++(*this);
            return out;
        }

    private:
        SurfacesPtr::const_iterator m_it_surface;
        size_t                      m_idx_contour;
    };

    iterator cbegin() const { return iterator(m_surfaces.cbegin()); }
    iterator begin() const { return this->cbegin(); }
    iterator cend() const { return iterator(m_surfaces.cend()); }
    iterator end() const { return this->cend(); }
    size_t   size() const { return m_size; }

private:
    const SurfacesPtr& m_surfaces;
    size_t             m_size;
};

// For ClipperLib with Z coordinates.
// [INTENT] ZPoint / ZPoints — 3-D integer coordinates used when tracking Z through
// ClipperLib operations (e.g., for TriangleMeshSlicer plane intersections).
// The Z component is carried as a third int32 and is NOT part of any Clipper Boolean
// logic — Clipper only uses X and Y.
using ZPoint  = Vec3i32;
using ZPoints = std::vector<Vec3i32>;

// [INTENT] clip_clipper_polygon_with_subject_bbox() — fast pre-clipping step that
// removes vertices of a clipping polygon that lie entirely outside the subject's
// bounding box.  This avoids sending thousands of irrelevant clipping polygon edges
// into ClipperLib when most of the clipping polygon is elsewhere on the layer.
//
// [HAZARD H427] This is NOT a true polygon clip — it is a vertex-level filter based
// on outcode classification.  Vertices where all three consecutive points (prev, this,
// next) are outside the same side are removed.  For very elongated clip polygons that
// cross the bbox boundary diagonally, this can incorrectly remove a vertex that is
// outside but connects two vertices on different sides — discarding a relevant edge.
// The comment "edge possibly cuts corner of the bounding box" acknowledges this but
// relies on the adjacent vertices being inside to prevent incorrect removal.
// If the entire clip polygon is a large CCW rectangle and the subject is a small
// polygon in one corner, all clip vertices are removed and the clip result is empty —
// which silently leaves the subject unclipped.
void clip_clipper_polygon_with_subject_bbox(const Points& src, const BoundingBox& bbox, Points& out, const bool get_entire_polygons = false);
void                   clip_clipper_polygon_with_subject_bbox(const ZPoints& src, const BoundingBox& bbox, ZPoints& out);
[[nodiscard]] Points   clip_clipper_polygon_with_subject_bbox(const Points& src, const BoundingBox& bbox);
[[nodiscard]] ZPoints  clip_clipper_polygon_with_subject_bbox(const ZPoints& src, const BoundingBox& bbox);
void                   clip_clipper_polygon_with_subject_bbox(const Polygon& src, const BoundingBox& bbox, Polygon& out);
[[nodiscard]] Polygon  clip_clipper_polygon_with_subject_bbox(const Polygon&     src,
                                                              const BoundingBox& bbox,
                                                              const bool         get_entire_polygons = false);
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const Polygons& src, const BoundingBox& bbox);
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygon&   src,
                                                               const BoundingBox& bbox,
                                                               const bool         get_entire_polygons = false);
[[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygons&  src,
                                                               const BoundingBox& bbox,
                                                               const bool         get_entire_polygons = false);

} // namespace ClipperUtils

// [INTENT] ClipperPaths_to_Slic3rExPolygons — convert a flat ClipperLib::Paths result
// (as returned by Clipper::Execute into Paths) into a structured ExPolygons tree.
// If do_union is true, a NonZero union is performed to resolve winding ambiguity.
// If do_union is false, EvenOdd fill rule is used (no union performed).
// [HAZARD H428] Using pftEvenOdd on overlapping paths produces XOR-like semantics —
// overlapping sub-regions are subtracted.  Callers must ensure paths are non-overlapping
// or explicitly request do_union = true.
ExPolygons ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths& input, bool do_union = false);

// offset Polygons
// Wherever applicable, please use the expand() / shrink() variants instead, they convey their purpose better.
// [INTENT] offset(polygon, +delta) → expand; offset(polygon, -delta) → shrink.
// All coordinates are in scaled integer units (1 unit = 1e-6 mm).
// [HAZARD H429] Input polygons for NEGATIVE offset must be "normalized" — no overlaps or
// self-intersections between the input polygons.  Overlapping inputs with a negative delta
// can produce incorrect interior topology because shrink_paths() wraps the result in a
// union with a bounding box sentinel to flip winding.
Slic3r::Polygons offset(const Slic3r::Polygon& polygon,
                        const float            delta,
                        ClipperLib::JoinType   joinType   = DefaultJoinType,
                        double                 miterLimit = DefaultMiterLimit);

// offset Polylines
// Wherever applicable, please use the expand() / shrink() variants instead, they convey their purpose better.
// Input polygons for negative offset shall be "normalized": There must be no overlap / intersections between the input polygons.
Slic3r::Polygons   offset(const Slic3r::Polyline& polyline,
                          const float             delta,
                          ClipperLib::JoinType    joinType   = DefaultLineJoinType,
                          double                  miterLimit = DefaultLineMiterLimit,
                          ClipperLib::EndType     end_type   = DefaultEndType);
Slic3r::Polygons   offset(const Slic3r::Polylines& polylines,
                          const float              delta,
                          ClipperLib::JoinType     joinType   = DefaultLineJoinType,
                          double                   miterLimit = DefaultLineMiterLimit,
                          ClipperLib::EndType      end_type   = DefaultEndType);
Slic3r::Polygons   offset(const Slic3r::Polygons& polygons,
                          const float             delta,
                          ClipperLib::JoinType    joinType   = DefaultJoinType,
                          double                  miterLimit = DefaultMiterLimit);
Slic3r::Polygons   offset(const Slic3r::ExPolygon& expolygon,
                          const float              delta,
                          ClipperLib::JoinType     joinType   = DefaultJoinType,
                          double                   miterLimit = DefaultMiterLimit);
Slic3r::Polygons   offset(const Slic3r::ExPolygons& expolygons,
                          const float               delta,
                          ClipperLib::JoinType      joinType   = DefaultJoinType,
                          double                    miterLimit = DefaultMiterLimit);
Slic3r::Polygons   offset(const Slic3r::Surfaces& surfaces,
                          const float             delta,
                          ClipperLib::JoinType    joinType   = DefaultJoinType,
                          double                  miterLimit = DefaultMiterLimit);
Slic3r::Polygons   offset(const Slic3r::SurfacesPtr& surfaces,
                          const float                delta,
                          ClipperLib::JoinType       joinType   = DefaultJoinType,
                          double                     miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset_ex(const Slic3r::Polygons& polygons,
                             const float             delta,
                             ClipperLib::JoinType    joinType   = DefaultJoinType,
                             double                  miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygon& expolygon,
                             const float              delta,
                             ClipperLib::JoinType     joinType   = DefaultJoinType,
                             double                   miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygons& expolygons,
                             const float               delta,
                             ClipperLib::JoinType      joinType   = DefaultJoinType,
                             double                    miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset_ex(const Slic3r::Surfaces& surfaces,
                             const float             delta,
                             ClipperLib::JoinType    joinType   = DefaultJoinType,
                             double                  miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset_ex(const Slic3r::SurfacesPtr& surfaces,
                             const float                delta,
                             ClipperLib::JoinType       joinType   = DefaultJoinType,
                             double                     miterLimit = DefaultMiterLimit);
// BBS
// [INTENT] BBS convenience overload: wraps a single Polygon into a Polygons vector before
// calling offset_ex.  Avoids the caller needing to construct a temporary vector.
// [HAZARD H430] The temporary Polygons vector is constructed and immediately destroyed.
// For hot-path callers this is a needless heap allocation.  Consider moving the
// implementation to call a direct single-polygon path.
inline Slic3r::ExPolygons offset_ex(const Slic3r::Polygon& polygon,
                                    const float            delta,
                                    ClipperLib::JoinType   joinType   = DefaultJoinType,
                                    double                 miterLimit = DefaultMiterLimit)
{
    Slic3r::Polygons temp;
    temp.push_back(polygon);

    return offset_ex(temp, delta, joinType, miterLimit);
}

// convert stroke to path by offsetting of contour
// [INTENT] contour_to_polygons — convert a polygon outline (zero-width) into a thick
// stroked path of width line_width, using etClosedLine end type so both sides of the
// polygon edge are offset symmetrically.  Used for generating extrusion footprints from
// centerline polygons.
Polygons contour_to_polygons(const Polygon&       polygon,
                             const float          line_width,
                             ClipperLib::JoinType join_type   = DefaultJoinType,
                             double               miter_limit = DefaultMiterLimit);
Polygons contour_to_polygons(const Polygons&      polygon,
                             const float          line_width,
                             ClipperLib::JoinType join_type   = DefaultJoinType,
                             double               miter_limit = DefaultMiterLimit);

// [INTENT] union_safety_offset — union via a +10nm expand pass, then return. This ensures
// that polygons with shared or near-touching edges are correctly merged rather than
// producing slivers or zero-area intersection artifacts.  Used before diff/intersection
// when the input may have been assembled from parts with near-coincident edges.
// [HAZARD H431] The safety offset inflates all outlines by 10nm before union. For
// extremely tight perimeter gaps (< 20nm total clearance), two adjacent walls may merge
// unintentionally.  In practice, minimum feature sizes are >100µm so this is not an issue.
inline Slic3r::Polygons   union_safety_offset(const Slic3r::Polygons& polygons) { return offset(polygons, ClipperSafetyOffset); }
inline Slic3r::Polygons   union_safety_offset(const Slic3r::ExPolygons& expolygons) { return offset(expolygons, ClipperSafetyOffset); }
inline Slic3r::ExPolygons union_safety_offset_ex(const Slic3r::Polygons& polygons) { return offset_ex(polygons, ClipperSafetyOffset); }
inline Slic3r::ExPolygons union_safety_offset_ex(const Slic3r::ExPolygons& expolygons)
{
    return offset_ex(expolygons, ClipperSafetyOffset);
}

Slic3r::Polygons   union_safety_offset(const Slic3r::Polygons& expolygons);
Slic3r::Polygons   union_safety_offset(const Slic3r::ExPolygons& expolygons);
Slic3r::ExPolygons union_safety_offset_ex(const Slic3r::Polygons& polygons);
Slic3r::ExPolygons union_safety_offset_ex(const Slic3r::ExPolygons& expolygons);

// [INTENT] expand/shrink — named wrappers over offset() that assert the sign of delta and
// convey intent.  prefer these over raw offset() in new code.
// Aliases for the various offset(...) functions, conveying the purpose of the offset.
inline Slic3r::Polygons expand(const Slic3r::Polygon& polygon,
                               const float            delta,
                               ClipperLib::JoinType   joinType   = DefaultJoinType,
                               double                 miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset(polygon, delta, joinType, miterLimit);
}
inline Slic3r::Polygons expand(const Slic3r::Polygons& polygons,
                               const float             delta,
                               ClipperLib::JoinType    joinType   = DefaultJoinType,
                               double                  miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset(polygons, delta, joinType, miterLimit);
}
inline Slic3r::Polygons expand(const Slic3r::ExPolygons& polygons,
                               const float               delta,
                               ClipperLib::JoinType      joinType   = DefaultJoinType,
                               double                    miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset(polygons, delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons expand_ex(const Slic3r::Polygons& polygons,
                                    const float             delta,
                                    ClipperLib::JoinType    joinType   = DefaultJoinType,
                                    double                  miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset_ex(polygons, delta, joinType, miterLimit);
}
// Input polygons for shrinking shall be "normalized": There must be no overlap / intersections between the input polygons.
inline Slic3r::Polygons shrink(const Slic3r::Polygons& polygons,
                               const float             delta,
                               ClipperLib::JoinType    joinType   = DefaultJoinType,
                               double                  miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset(polygons, -delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons shrink_ex(const Slic3r::Polygons& polygons,
                                    const float             delta,
                                    ClipperLib::JoinType    joinType   = DefaultJoinType,
                                    double                  miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset_ex(polygons, -delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons shrink_ex(const Slic3r::ExPolygons& polygons,
                                    const float               delta,
                                    ClipperLib::JoinType      joinType   = DefaultJoinType,
                                    double                    miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset_ex(polygons, -delta, joinType, miterLimit);
}

// [INTENT] offset2 / offset2_ex — two-pass offset: apply delta1 then delta2 to the result.
// Used as the building block for morphological opening() and closing() operators.
// Wherever applicable, please use the opening() / closing() variants instead, they convey their purpose better.
// Input polygons for negative offset shall be "normalized": There must be no overlap / intersections between the input polygons.
Slic3r::Polygons   offset2(const Slic3r::ExPolygons& expolygons,
                           const float               delta1,
                           const float               delta2,
                           ClipperLib::JoinType      joinType   = DefaultJoinType,
                           double                    miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset2_ex(const Slic3r::ExPolygons& expolygons,
                              const float               delta1,
                              const float               delta2,
                              ClipperLib::JoinType      joinType   = DefaultJoinType,
                              double                    miterLimit = DefaultMiterLimit);
Slic3r::ExPolygons offset2_ex(const Slic3r::Surfaces& surfaces,
                              const float             delta1,
                              const float             delta2,
                              ClipperLib::JoinType    joinType   = DefaultJoinType,
                              double                  miterLimit = DefaultMiterLimit);

// BBS
// [INTENT] _clipper_ex — BBS-added direct access to the internal clipper operation, bypassing
// the subject-only offset path.  Used in BBS-specific code that needs to control the clip
// polygon type independently.
Slic3r::ExPolygons _clipper_ex(ClipperLib::ClipType    clipType,
                               const Slic3r::Polygons& subject,
                               const Slic3r::Polygons& clip,
                               bool                    safety_offset_ = false);

// [INTENT] closing() — morphological closing: expand then shrink by the same radius.
// Fills gaps and rounds concave corners inward.  Equivalent to Minkowski sum with a disk
// followed by Minkowski difference with the same disk.
// All deltas should be positive.
// Offset outside, then inside produces morphological closing. All deltas should be positive.
Slic3r::Polygons        closing(const Slic3r::Polygons& polygons,
                                const float             delta1,
                                const float             delta2,
                                ClipperLib::JoinType    joinType   = DefaultJoinType,
                                double                  miterLimit = DefaultMiterLimit);
inline Slic3r::Polygons closing(const Slic3r::Polygons& polygons,
                                const float             delta,
                                ClipperLib::JoinType    joinType   = DefaultJoinType,
                                double                  miterLimit = DefaultMiterLimit)
{
    return closing(polygons, delta, delta, joinType, miterLimit);
}
Slic3r::ExPolygons        closing_ex(const Slic3r::Polygons& polygons,
                                     const float             delta1,
                                     const float             delta2,
                                     ClipperLib::JoinType    joinType   = DefaultJoinType,
                                     double                  miterLimit = DefaultMiterLimit);
inline Slic3r::ExPolygons closing_ex(const Slic3r::Polygons& polygons,
                                     const float             delta,
                                     ClipperLib::JoinType    joinType   = DefaultJoinType,
                                     double                  miterLimit = DefaultMiterLimit)
{
    return closing_ex(polygons, delta, delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons closing_ex(const Slic3r::ExPolygons& polygons,
                                     const float               delta,
                                     ClipperLib::JoinType      joinType   = DefaultJoinType,
                                     double                    miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset2_ex(polygons, delta, -delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons closing_ex(const Slic3r::Surfaces& surfaces,
                                     const float             delta,
                                     ClipperLib::JoinType    joinType   = DefaultJoinType,
                                     double                  miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset2_ex(surfaces, delta, -delta, joinType, miterLimit);
}

// [INTENT] opening() — morphological opening: shrink then expand by the same radius.
// Removes thin protrusions and rounds convex corners outward.  Equivalent to Minkowski
// difference followed by Minkowski sum.
// Input polygons for opening shall be "normalized": There must be no overlap / intersections between the input polygons.
// Offset inside, then outside produces morphological opening. All deltas should be positive.
// Input polygons for opening shall be "normalized": There must be no overlap / intersections between the input polygons.
Slic3r::Polygons        opening(const Slic3r::Polygons& polygons,
                                const float             delta1,
                                const float             delta2,
                                ClipperLib::JoinType    joinType   = DefaultJoinType,
                                double                  miterLimit = DefaultMiterLimit);
Slic3r::Polygons        opening(const Slic3r::ExPolygons& expolygons,
                                const float               delta1,
                                const float               delta2,
                                ClipperLib::JoinType      joinType   = DefaultJoinType,
                                double                    miterLimit = DefaultMiterLimit);
Slic3r::Polygons        opening(const Slic3r::Surfaces& surfaces,
                                const float             delta1,
                                const float             delta2,
                                ClipperLib::JoinType    joinType   = DefaultJoinType,
                                double                  miterLimit = DefaultMiterLimit);
inline Slic3r::Polygons opening(const Slic3r::Polygons& polygons,
                                const float             delta,
                                ClipperLib::JoinType    joinType   = DefaultJoinType,
                                double                  miterLimit = DefaultMiterLimit)
{
    return opening(polygons, delta, delta, joinType, miterLimit);
}
inline Slic3r::Polygons opening(const Slic3r::ExPolygons& expolygons,
                                const float               delta,
                                ClipperLib::JoinType      joinType   = DefaultJoinType,
                                double                    miterLimit = DefaultMiterLimit)
{
    return opening(expolygons, delta, delta, joinType, miterLimit);
}
inline Slic3r::Polygons opening(const Slic3r::Surfaces& surfaces,
                                const float             delta,
                                ClipperLib::JoinType    joinType   = DefaultJoinType,
                                double                  miterLimit = DefaultMiterLimit)
{
    return opening(surfaces, delta, delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons opening_ex(const Slic3r::ExPolygons& polygons,
                                     const float               delta,
                                     ClipperLib::JoinType      joinType   = DefaultJoinType,
                                     double                    miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset2_ex(polygons, -delta, delta, joinType, miterLimit);
}
inline Slic3r::ExPolygons opening_ex(const Slic3r::Surfaces& surfaces,
                                     const float             delta,
                                     ClipperLib::JoinType    joinType   = DefaultJoinType,
                                     double                  miterLimit = DefaultMiterLimit)
{
    assert(delta > 0);
    return offset2_ex(surfaces, -delta, delta, joinType, miterLimit);
}

// [INTENT] _clipper_ln — clip a set of Lines (pairs of Points) against a polygon set.
// Lines are converted to Polylines, clipped, then converted back to Lines.
// A Clipper GH#6933 note says multi-point collinear results may occur — only first/last
// points of each resulting polyline are used.
Slic3r::Lines _clipper_ln(ClipperLib::ClipType clipType, const Slic3r::Lines& subject, const Slic3r::Polygons& clip);

// [INTENT] diff / diff_ex — Boolean difference: subject minus clip.
// Safety offset is applied to the clipping polygons only.
// Polygons variants return flat Polygons (holes represented as CW sub-polygons in the
// flat list).  *_ex variants return ExPolygons with explicit hole attribution.
Slic3r::Polygons diff(const Slic3r::Polygon& subject,
                      const Slic3r::Polygon& clip,
                      ApplySafetyOffset      do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons diff(const Slic3r::Polygons& subject,
                      const Slic3r::Polygons& clip,
                      ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons diff(const Slic3r::Polygons&   subject,
                      const Slic3r::ExPolygons& clip,
                      ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
// Optimized version clipping the "clipping" polygon using clip_clipper_polygon_with_subject_bbox().
// To be used with complex clipping polygons, where majority of the clipping polygons are outside of the source polygon.
// [INTENT] diff_clipped — pre-filters the clip polygon by the subject bounding box before
// performing the Boolean difference.  Significant speedup when clip polygon covers the
// whole layer and subject is a small island.
Slic3r::Polygons   diff_clipped(const Slic3r::Polygons& src,
                                const Slic3r::Polygons& clipping,
                                ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_clipped(const Slic3r::ExPolygons& src,
                                const Slic3r::Polygons&   clipping,
                                ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_clipped(const Slic3r::ExPolygons& src,
                                const Slic3r::ExPolygons& clipping,
                                ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   diff(const Slic3r::ExPolygons& subject,
                        const Slic3r::Polygons&   clip,
                        ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   diff(const Slic3r::ExPolygons& subject,
                        const Slic3r::ExPolygons& clip,
                        ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   diff(const Slic3r::Surfaces& subject,
                        const Slic3r::Polygons& clip,
                        ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons& subject,
                           const Slic3r::Polygons& clip,
                           ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons&   subject,
                           const Slic3r::ExPolygons& clip,
                           ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons& subject,
                           const Slic3r::Surfaces& clip,
                           ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygon&    subject,
                           const Slic3r::ExPolygons& clip,
                           ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon& subject,
                           const Slic3r::Polygon&   clip,
                           ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon& subject,
                           const Slic3r::Polygons&  clip,
                           ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject,
                           const Slic3r::Polygons&   clip,
                           ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject,
                           const Slic3r::ExPolygons& clip,
                           ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces& subject,
                           const Slic3r::Polygons& clip,
                           ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces&   subject,
                           const Slic3r::ExPolygons& clip,
                           ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject,
                           const Slic3r::Surfaces&   clip,
                           ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces& subject,
                           const Slic3r::Surfaces& clip,
                           ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr& subject,
                           const Slic3r::Polygons&    clip,
                           ApplySafetyOffset          do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr& subject,
                           const Slic3r::ExPolygons&  clip,
                           ApplySafetyOffset          do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polylines  diff_pl(const Slic3r::Polyline& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polyline& subject, const Slic3r::ExPolygon& clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygon& clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygons& clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip);

// BBS
// [INTENT] BBS convenience wrappers for single ExPolygon diff — constructs temporary
// vectors to match the vector overloads.  Same heap-allocation concern as offset_ex above.
inline Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon& subject,
                                  const Slic3r::ExPolygon& clip,
                                  ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No)
{
    Slic3r::ExPolygons subject_temp;
    Slic3r::ExPolygons clip_temp;

    subject_temp.push_back(subject);
    clip_temp.push_back(clip);
    return diff_ex(subject_temp, clip_temp);
}

inline Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon&  subject,
                                  const Slic3r::ExPolygons& clip,
                                  ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No)
{
    Slic3r::ExPolygons subject_temp;
    subject_temp.push_back(subject);

    return diff_ex(subject_temp, clip, do_safety_offset);
}

inline Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons& subject,
                                  const Slic3r::ExPolygon&  clip,
                                  ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No)
{
    Slic3r::ExPolygons clip_temp;
    clip_temp.push_back(clip);

    return diff_ex(subject, clip_temp, do_safety_offset);
}

inline Slic3r::Lines diff_ln(const Slic3r::Lines& subject, const Slic3r::Polygons& clip)
{
    return _clipper_ln(ClipperLib::ctDifference, subject, clip);
}

// [INTENT] intersection / intersection_ex — Boolean intersection: subject AND clip.
// Safety offset is applied to the clipping polygons only.
// [HAZARD H432] intersection() returns flat Polygons including CW hole sub-polygons.
// Callers that treat all output polygons as CCW contours will incorrectly process holes.
// Use intersection_ex() when hole attribution is needed.
Slic3r::Polygons intersection(const Slic3r::Polygon& subject,
                              const Slic3r::Polygon& clip,
                              ApplySafetyOffset      do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::Polygons&  subject,
                              const Slic3r::ExPolygon& clip,
                              ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::Polygons& subject,
                              const Slic3r::Polygons& clip,
                              ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::ExPolygon& subject,
                              const Slic3r::ExPolygon& clip,
                              ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No);
// Optimized version clipping the "clipping" polygon using clip_clipper_polygon_with_subject_bbox().
// To be used with complex clipping polygons, where majority of the clipping polygons are outside of the source polygon.
Slic3r::Polygons intersection_clipped(const Slic3r::Polygons& subject,
                                      const Slic3r::Polygons& clip,
                                      ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::ExPolygons& subject,
                              const Slic3r::Polygons&   clip,
                              ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::ExPolygons& subject,
                              const Slic3r::ExPolygons& clip,
                              ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::Surfaces& subject,
                              const Slic3r::Polygons& clip,
                              ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons intersection(const Slic3r::Surfaces&   subject,
                              const Slic3r::ExPolygons& clip,
                              ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
// BBS
Slic3r::Polygons   intersection(const Slic3r::Polygons& subject,
                                const Slic3r::Polygon&  clip,
                                ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons& subject,
                                   const Slic3r::Polygons& clip,
                                   ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon& subject,
                                   const Slic3r::Polygons&  clip,
                                   ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon& subject,
                                   const Slic3r::ExPolygon& clip,
                                   ApplySafetyOffset        do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons&   subject,
                                   const Slic3r::ExPolygons& clip,
                                   ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons& subject,
                                   const Slic3r::Polygons&   clip,
                                   ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons& subject,
                                   const Slic3r::ExPolygon&  clip,
                                   ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon&  subject,
                                   const Slic3r::ExPolygons& clip,
                                   ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons& subject,
                                   const Slic3r::ExPolygons& clip,
                                   ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces& subject,
                                   const Slic3r::Polygons& clip,
                                   ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces&   subject,
                                   const Slic3r::ExPolygons& clip,
                                   ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces& subject,
                                   const Slic3r::Surfaces& clip,
                                   ApplySafetyOffset       do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::SurfacesPtr& subject,
                                   const Slic3r::ExPolygons&  clip,
                                   ApplySafetyOffset          do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polylines  intersection_pl(const Slic3r::Polylines& subject, const Slic3r::Polygon& clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polyline& subject, const Slic3r::ExPolygon& clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygon& clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polyline& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polylines& subject, const Slic3r::ExPolygons& clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polygons& subject, const Slic3r::Polygons& clip);

inline Slic3r::Lines intersection_ln(const Slic3r::Lines& subject, const Slic3r::Polygons& clip)
{
    return _clipper_ln(ClipperLib::ctIntersection, subject, clip);
}

inline Slic3r::Lines intersection_ln(const Slic3r::Line& subject, const Slic3r::Polygons& clip)
{
    Slic3r::Lines lines;
    lines.emplace_back(subject);
    return _clipper_ln(ClipperLib::ctIntersection, lines, clip);
}

// [INTENT] union_ / union_ex — Boolean union of polygons.
// union_(Polygons, pftEvenOdd) can "heal" unusual models (3DLabPrints etc.).
// [HAZARD H433] union_() with pftEvenOdd makes overlapping regions cancel out (XOR
// semantics) instead of merging.  Callers that intend a true union must use pftNonZero.
Slic3r::Polygons union_(const Slic3r::Polygons& subject);
Slic3r::Polygons union_(const Slic3r::ExPolygons& subject);
Slic3r::Polygons union_(const Slic3r::Polygons& subject, const ClipperLib::PolyFillType fillType);
Slic3r::Polygons union_(const Slic3r::Polygons& subject, const Slic3r::Polygons& subject2);
// May be used to "heal" unusual models (3DLabPrints etc.) by providing fill_type (pftEvenOdd, pftNonZero, pftPositive, pftNegative).
Slic3r::ExPolygons union_ex(const Slic3r::Polygons& subject, ClipperLib::PolyFillType fill_type = ClipperLib::pftNonZero);
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons& subject);
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons& subject, const Slic3r::Polygons& subject2);
Slic3r::ExPolygons union_ex(const Slic3r::Surfaces& subject);

Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons& poly1, const Slic3r::ExPolygons& poly2, bool safety_offset_ = false);

// [INTENT] union_pt / union_pt_chained_outside_in — produce a ClipperLib::PolyTree from
// a union operation using pftEvenOdd (no merging of overlapping regions).
// Used when the caller needs the PolyTree structure directly (e.g., traverse_pt()).
// [HAZARD H434] union_pt() uses pftEvenOdd — see H433.
// Convert polygons / expolygons into ClipperLib::PolyTree using ClipperLib::pftEvenOdd, thus union will NOT be performed.
// If the contours are not intersecting, their orientation shall not be modified by union_pt().
ClipperLib::PolyTree union_pt(const Slic3r::Polygons& subject);
ClipperLib::PolyTree union_pt(const Slic3r::ExPolygons& subject);

// [INTENT] xor_ex — Boolean XOR: regions in either but not both inputs.
// Used for computing symmetric differences (e.g., detecting areas that changed between
// two configurations).
Slic3r::ExPolygons xor_ex(const Slic3r::ExPolygons& subject,
                          const Slic3r::ExPolygon&  clip,
                          ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons xor_ex(const Slic3r::ExPolygons& subject,
                          const Slic3r::ExPolygons& clip,
                          ApplySafetyOffset         do_safety_offset = ApplySafetyOffset::No);

// [INTENT] union_pt_chained_outside_in — union followed by an outside-in spatial ordering
// of the resulting polygons (outer contours before their inner holes).
// Used by infill generators that need to know which polygon enclosures contain which.
Slic3r::Polygons union_pt_chained_outside_in(const Slic3r::Polygons& subject);

ClipperLib::PolyNodes order_nodes(const ClipperLib::PolyNodes& nodes);

// [INTENT] foreach_node / traverse_pt — generic traversal helpers for ClipperLib::PolyTree.
// e_ordering::ON performs a spatial chain ordering of nodes before visiting (expensive).
// e_ordering::OFF visits nodes in their natural ClipperLib order (faster).
//
// traverse_pt<Polygons*> collects all contours (holes CW, contours CCW).
// traverse_pt<ExPolygons*> assembles full ExPolygon structures with explicit hole attribution.
//
// [HAZARD H435] _foreach_node<ON> calls order_nodes() then iterates over the original
// 'nodes' (not the ordered_nodes result) — the ordering call is a no-op bug.
// See ClipperUtils.cpp:589 — 'for (auto &n : nodes)' should be 'for (auto &n : ordered_nodes)'.
// This means e_ordering::ON is silently equivalent to e_ordering::OFF.
// Implementing generalized loop (foreach) over a list of nodes which can be
// ordered or unordered (performance gain) based on template parameter
enum class e_ordering { ON, OFF };

// Create a template struct, template functions can not be partially specialized
template<e_ordering o, class Fn> struct _foreach_node
{
    void operator()(const ClipperLib::PolyNodes& nodes, Fn&& fn);
};

// Specialization with NO ordering
template<class Fn> struct _foreach_node<e_ordering::OFF, Fn>
{
    void operator()(const ClipperLib::PolyNodes& nodes, Fn&& fn)
    {
        for (auto& n : nodes)
            fn(n);
    }
};

// Specialization with ordering
// [HAZARD H435] Bug: iterates over 'nodes' (unordered), not 'ordered_nodes'. The
// order_nodes() call is wasted work. e_ordering::ON behaves identically to OFF.
template<class Fn> struct _foreach_node<e_ordering::ON, Fn>
{
    void operator()(const ClipperLib::PolyNodes& nodes, Fn&& fn)
    {
        auto ordered_nodes = order_nodes(nodes);
        for (auto& n : nodes)
            fn(n);
    }
};

// Wrapper function for the foreach_node which can deduce arguments automatically
template<e_ordering o, class Fn> void foreach_node(const ClipperLib::PolyNodes& nodes, Fn&& fn)
{
    _foreach_node<o, Fn>()(nodes, std::forward<Fn>(fn));
}

// Collecting polygons of the tree into a list of Polygons, holes have clockwise
// orientation.
template<e_ordering ordering = e_ordering::OFF> void traverse_pt(const ClipperLib::PolyNode* tree, Polygons* out)
{
    if (!tree)
        return; // terminates recursion

    // Push the contour of the current level
    out->emplace_back(tree->Contour);

    // Do the recursion for all the children.
    traverse_pt<ordering>(tree->Childs, out);
}

// Collecting polygons of the tree into a list of ExPolygons.
// [INTENT] Holes in the PolyTree are collected with their children (sub-contours nested
// inside holes).  The tree is traversed depth-first: at each contour level, holes are
// collected as ExPolygon holes and their sub-children are recursively assembled into new
// ExPolygons at the next output level.
template<e_ordering ordering = e_ordering::OFF> void traverse_pt(const ClipperLib::PolyNode* tree, ExPolygons* out)
{
    if (!tree)
        return;
    else if (tree->IsHole()) {
        // Levels of holes are skipped and handled together with the
        // contour levels.
        traverse_pt<ordering>(tree->Childs, out);
        return;
    }

    ExPolygon level;
    level.contour.points = tree->Contour;

    foreach_node<ordering>(tree->Childs, [out, &level](const ClipperLib::PolyNode* node) {
        // Holes are collected here.
        level.holes.emplace_back(node->Contour);

        // By doing a recursion, a new level expoly is created with the contour
        // and holes of the lower level. Doing this for all the childs.
        traverse_pt<ordering>(node->Childs, out);
    });

    out->emplace_back(level);
}

template<e_ordering o = e_ordering::OFF, class ExOrJustPolygons>
void traverse_pt(const ClipperLib::PolyNodes& nodes, ExOrJustPolygons* retval)
{
    foreach_node<o>(nodes, [&retval](const ClipperLib::PolyNode* node) { traverse_pt<o>(node, retval); });
}

/* OTHER */
// [INTENT] simplify_polygons — self-union with StrictlySimple=true to remove duplicate
// points and self-intersections.  StrictlySimple is noted as "very expensive" in the
// FIXME comments; consider whether callers need it for their geometry.
// [HAZARD H436] simplify_polygons() uses StrictlySimple mode which is O(N²) in Clipper 1.
// For large complex polygons (>10k vertices) this can take seconds.  Clipper 2 (not yet
// integrated) has a faster simplification algorithm.
Slic3r::Polygons   simplify_polygons(const Slic3r::Polygons& subject);
Slic3r::ExPolygons simplify_polygons_ex(const Slic3r::Polygons& subject);

// [INTENT] top_level_islands — extract only the outermost non-nested polygons from a set.
// Performs a pftEvenOdd union and returns only the root-level PolyTree children.
// Used for computing layer islands (slices that are not nested within other slices).
Polygons top_level_islands(const Slic3r::Polygons& polygons);

// [INTENT] mittered_offset_path_scaled — compute a variable-width miter offset of a single
// contour.  Each vertex has its own offset delta in the 'deltas' vector.
// This is the core algorithm for Arachne's variable-width perimeter inset/outset.
// The function operates in scaled integer coordinates (all deltas must be pre-scaled).
//
// [HAZARD H437] Mixed sign in deltas is asserted (Debug) but not checked in Release.
// If a caller passes deltas with mixed positive/negative values, the offset geometry
// is undefined — the concave/convex corner logic assumes uniform sign.
//
// [HAZARD H438] miter_limit is transformed to '2 / (miter_limit² )' internally, clamped
// at miter_limit > 2. For miter_limit ≤ 2 the formula gives ≥ 0.5, which is the Clipper
// convention for "allow all miters."  This internal rescaling is non-obvious and differs
// from the ClipperLib miter limit semantics — a port must replicate this formula exactly.
ClipperLib::Path mittered_offset_path_scaled(const Points& contour, const std::vector<float>& deltas, double miter_limit);

// [INTENT] variable_offset_inner/outer — apply per-vertex variable offsets to an ExPolygon.
// The 'deltas' vector has one sub-vector per polygon (contour first, then each hole).
// Inner shrinks contour inward and holes outward; outer does the opposite.
// Used by the Arachne perimeter generator for bead width transitions.
Polygons   variable_offset_inner(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit = 2.);
Polygons   variable_offset_outer(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit = 2.);
ExPolygons variable_offset_outer_ex(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit = 2.);
ExPolygons variable_offset_inner_ex(const ExPolygon& expoly, const std::vector<std::vector<float>>& deltas, double miter_limit = 2.);

// [INTENT] make_counter_clockwise — ensure a Pointfs (double-precision point vector)
// has CCW winding by reversing if the polygon is CW.  Uses integer-scaled area via
// Polygon::new_scale() to classify winding direction.
// [HAZARD H439] Polygon::new_scale() converts Pointfs (double mm) to scaled integers
// (int32 units = 1e-6 mm).  For polygons larger than ~2147 mm in extent, this conversion
// overflows int32.  The is_clockwise() result would be wrong for bed-scale polygons.
Pointfs make_counter_clockwise(const Pointfs& pointfs);

} // namespace Slic3r

#endif // slic3r_ClipperUtils_hpp_
