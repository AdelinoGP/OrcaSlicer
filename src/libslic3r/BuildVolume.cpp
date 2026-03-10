// [INTENT] BuildVolume.cpp — implementation of printer build volume geometry classification
// and collision detection for objects and G-code paths.
//
// Responsibilities:
//   - Constructor: classifies the printable area polygon as Rectangle/Circle/Convex/Custom;
//     builds convex hull decomposition for Convex/Custom tests; initialises per-extruder
//     volume descriptors and the shared rendering volume.
//   - object_state_templ<InsideFn>: generic O(V) ITS vertex test with optional Z-plane
//     clipping for objects that straddle the print bed surface.
//   - object_state(): dispatches to the appropriate containment test per build volume type.
//   - volume_state_bbox(): fast bounding-box containment test for rectangular beds only.
//   - all_paths_inside(): tests every G-code extrusion vertex (O(moves)) for bed containment.
//   - check_object_state_with_extruder_area(): per-extruder reachability test.
//
// [HAZARD H796] Constructor assert(printable_height >= 0) is a no-op in release builds.
//              Negative height would produce bboxf.max.z < 0, causing all objects to
//              report ObjectState::Below regardless of actual position.
//
// [HAZARD H797] object_state_templ: num_above counts vertices, not triangles. A mesh
//              whose vertices are all above the bed but whose triangles span below it
//              (concave underside) passes may_be_below_bed=false safely, but if may_be_below_bed
//              is true and a vertex-in/edge-cross scenario exists, the edge-cross loop detects
//              it. For very large meshes the O(V×T) worst case is bounded by early exit.
//
// [HAZARD H798] BuildVolume_Type::Custom uses the convex hull decomposition of the CONVEX HULL
//              of the polygon, not the polygon itself. A non-convex print bed (e.g., L-shaped)
//              is tested against its convex hull — objects in the concave "notch" are falsely
//              reported Inside. FIXME comment in the code acknowledges this.
//
// [HAZARD H799] all_paths_inside() for Rectangle returns immediately if paths_bbox fits in the
//              build volume — it does NOT iterate per-move. This is an O(1) fast path but means
//              it can miss a move that is just outside the bbox (if the bbox was computed wrong).
//
// [HAZARD H800] check_object_state_with_extruder_area: for extruder shapes not classified as
//              Rectangle or Circle (e.g., Convex/Custom), the switch falls through to default
//              and return_state stays Inside. The check is silently skipped for non-rectangular
//              non-circular extruder areas. This can mask extruder reachability violations.
//
// [HAZARD H801] BuildVolume_Type::Custom shares the same convex hull test as Convex. No
//              efficient non-convex containment test is implemented. The FIXME on line 401
//              explicitly acknowledges this limitation.
//
// [CONCURRENCY] BuildVolume is read-only after construction. All query methods are const.
//              Safe to call from multiple threads simultaneously. The constructor is not
//              thread-safe (lambda captures and shared_volume mutation occur).

#include "BuildVolume.hpp"
#include "ClipperUtils.hpp"
#include "TriangleMesh.hpp"
#include "Geometry/ConvexHull.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "Point.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r {

// [INTENT] Primary constructor: classifies the printable area and builds all derived metrics.
// Steps:
//   1. Scale printable_area to coord_t (m_polygon), assert counter-clockwise orientation.
//   2. Compute convex hull, bounding box, area.
//   3. Classify: Rectangle (area ≈ bbox area) → Circle (RANSAC fit) → Convex/Custom (convex hull area ≈ polygon area).
//   4. For Convex/Custom: decompose convex hull into top/bottom half-planes for inside tests.
//   5. For each extruder: classify extruder_shape the same way; record into m_extruder_volumes.
//   6. Update m_shared_volume as the running min/max intersection of all extruder bboxfs.
BuildVolume::BuildVolume(const std::vector<Vec2d>&              printable_area,
                         const double                           printable_height,
                         const std::vector<std::vector<Vec2d>>& extruder_areas,
                         const std::vector<double>&             extruder_printable_heights)
    : m_bed_shape(printable_area)
    , m_max_print_height(printable_height)
    , m_extruder_shapes(extruder_areas)
    , m_extruder_printable_height(extruder_printable_heights)
{
    assert(printable_height >= 0);
    // assert(extruder_printable_heights.size() == extruder_areas.size());

    m_polygon = Polygon::new_scale(printable_area);
    assert(m_polygon.is_counter_clockwise());

    // Calcuate various metrics of the input polygon.
    m_convex_hull = Geometry::convex_hull(m_polygon.points);
    m_bbox        = get_extents(m_convex_hull);
    m_area        = m_polygon.area();

    BoundingBoxf bboxf = get_extents(printable_area);
    m_bboxf            = BoundingBoxf3{to_3d(bboxf.min, 0.), to_3d(bboxf.max, printable_height)};

    if (printable_area.size() >= 4 && std::abs((m_area - double(m_bbox.size().x()) * double(m_bbox.size().y()))) < sqr(SCALED_EPSILON)) {
        // Square print bed, use the bounding box for collision detection.
        m_type          = BuildVolume_Type::Rectangle;
        m_circle.center = 0.5 * (m_bbox.min.cast<double>() + m_bbox.max.cast<double>());
        m_circle.radius = 0.5 * m_bbox.size().cast<double>().norm();
    } else if (printable_area.size() > 3) {
        // Circle was discretized, formatted into text with limited accuracy, thus the circle was deformed.
        // RANSAC is slightly more accurate than the iterative Taubin / Newton method with such an input.
        //        m_circle = Geometry::circle_taubin_newton(printable_area);
        m_circle       = Geometry::circle_ransac(printable_area);
        bool is_circle = true;
#ifndef NDEBUG
        // Measuring maximum absolute error of interpolating an input polygon with circle.
        double max_error = 0;
#endif // NDEBUG
        Vec2d prev = printable_area.back();
        for (const Vec2d& p : printable_area) {
#ifndef NDEBUG
            max_error = std::max(max_error, std::abs((p - m_circle.center).norm() - m_circle.radius));
#endif           // NDEBUG
            if ( // Polygon vertices must lie very close the circle.
                std::abs((p - m_circle.center).norm() - m_circle.radius) > 0.005 ||
                // Midpoints of polygon edges must not undercat more than 3mm. This corresponds to 72 edges per circle generated by
                // BedShapePanel::update_shape().
                m_circle.radius - (0.5 * (prev + p) - m_circle.center).norm() > 3.) {
                is_circle = false;
                break;
            }
            prev = p;
        }
        if (is_circle) {
            m_type          = BuildVolume_Type::Circle;
            m_circle.center = scaled<double>(m_circle.center);
            m_circle.radius = scaled<double>(m_circle.radius);
        }
    }

    if (printable_area.size() >= 3 && m_type == BuildVolume_Type::Invalid) {
        // Circle check is not used for Convex / Custom shapes, fill it with something reasonable.
        m_circle = Geometry::smallest_enclosing_circle_welzl(m_convex_hull.points);
        m_type   = (m_convex_hull.area() - m_area) < sqr(SCALED_EPSILON) ? BuildVolume_Type::Convex : BuildVolume_Type::Custom;
        // Initialize the top / bottom decomposition for inside convex polygon check. Do it with two different epsilons applied.
        auto convex_decomposition = [](const Polygon& in, double epsilon) {
            Polygon            src = expand(in, float(epsilon)).front();
            std::vector<Vec2d> pts;
            pts.reserve(src.size());
            for (const Point& pt : src.points)
                pts.emplace_back(unscaled<double>(pt.cast<double>().eval()));
            return Geometry::decompose_convex_polygon_top_bottom(pts);
        };
        m_top_bottom_convex_hull_decomposition_scene = convex_decomposition(m_convex_hull, SceneEpsilon);
        m_top_bottom_convex_hull_decomposition_bed   = convex_decomposition(m_convex_hull, BedEpsilon);
    }

    if (m_extruder_shapes.size() > 0) {
        m_shared_volume.data[0] = m_bboxf.min.x();
        m_shared_volume.data[1] = m_bboxf.min.y();
        m_shared_volume.data[2] = m_bboxf.max.x();
        m_shared_volume.data[3] = m_bboxf.max.y();
        m_shared_volume.zs[1]   = m_bboxf.max.z();
        for (unsigned int index = 0; index < m_extruder_shapes.size(); index++) {
            std::vector<Vec2d>& extruder_shape = m_extruder_shapes[index];
            BuildExtruderVolume extruder_volume;

            if (extruder_shape.empty()) {
                // should not happen
                BOOST_LOG_TRIVIAL(warning) << boost::format("Found invalid extruder_printable_area of index %1%") % index;
                assert(false);
                m_extruder_shapes.clear();
                return;
            }

            if ((extruder_shape == printable_area) && (extruder_printable_heights[index] == printable_height)) {
                extruder_volume.same_with_bed = true;
                extruder_volume.type          = m_type;
                extruder_volume.bbox          = m_bbox;
                extruder_volume.bboxf         = m_bboxf;
                extruder_volume.circle        = m_circle;
            } else {
                Polygon poly = Polygon::new_scale(extruder_shape);

                double poly_area        = poly.area();
                extruder_volume.bbox    = get_extents(poly);
                BoundingBoxf temp_bboxf = get_extents(extruder_shape);
                extruder_volume.bboxf = BoundingBoxf3{to_3d(temp_bboxf.min, 0.), to_3d(temp_bboxf.max, extruder_printable_heights[index])};

                if (extruder_shape.size() >= 4 &&
                    std::abs((poly_area - double(extruder_volume.bbox.size().x()) * double(extruder_volume.bbox.size().y()))) <
                        sqr(SCALED_EPSILON)) {
                    extruder_volume.type          = BuildVolume_Type::Rectangle;
                    extruder_volume.circle.center = 0.5 *
                                                    (extruder_volume.bbox.min.cast<double>() + extruder_volume.bbox.max.cast<double>());
                    extruder_volume.circle.radius = 0.5 * extruder_volume.bbox.size().cast<double>().norm();
                } else if (extruder_shape.size() > 3) {
                    extruder_volume.circle = Geometry::circle_ransac(extruder_shape);
                    bool is_circle         = true;

                    Vec2d prev = extruder_shape.back();
                    for (const Vec2d& p : extruder_shape) {
                        if ( // Polygon vertices must lie very close the circle.
                            std::abs((p - extruder_volume.circle.center).norm() - extruder_volume.circle.radius) > 0.005 ||
                            // Midpoints of polygon edges must not undercat more than 3mm. This corresponds to 72 edges per circle generated
                            // by BedShapePanel::update_shape().
                            extruder_volume.circle.radius - (0.5 * (prev + p) - extruder_volume.circle.center).norm() > 3.) {
                            is_circle = false;
                            break;
                        }
                        prev = p;
                    }
                    if (is_circle) {
                        extruder_volume.type          = BuildVolume_Type::Circle;
                        extruder_volume.circle.center = scaled<double>(extruder_volume.circle.center);
                        extruder_volume.circle.radius = scaled<double>(extruder_volume.circle.radius);
                    }
                }

                if (m_type == BuildVolume_Type::Invalid) {
                    // not supported currently, use the same as bed
                    extruder_volume.same_with_bed = true;
                    extruder_volume.type          = m_type;
                    extruder_volume.bbox          = m_bbox;
                    extruder_volume.bboxf         = m_bboxf;
                    extruder_volume.circle        = m_circle;
                }
                // always ignore z
                extruder_volume.bboxf.min.z() = -std::numeric_limits<double>::max();
            }
            m_extruder_volumes.push_back(std::move(extruder_volume));

            if (m_shared_volume.data[0] < extruder_volume.bboxf.min.x())
                m_shared_volume.data[0] = extruder_volume.bboxf.min.x();
            if (m_shared_volume.data[1] < extruder_volume.bboxf.min.y())
                m_shared_volume.data[1] = extruder_volume.bboxf.min.y();
            if (m_shared_volume.data[2] > extruder_volume.bboxf.max.x())
                m_shared_volume.data[2] = extruder_volume.bboxf.max.x();
            if (m_shared_volume.data[3] > extruder_volume.bboxf.max.y())
                m_shared_volume.data[3] = extruder_volume.bboxf.max.y();
            if (m_shared_volume.zs[1] > extruder_volume.bboxf.max.z())
                m_shared_volume.zs[1] = extruder_volume.bboxf.max.z();
        }

        m_shared_volume.type  = static_cast<int>(m_type);
        m_shared_volume.zs[0] = 0.f;
        // m_shared_volume.zs[1] = printable_height;
    }

    BOOST_LOG_TRIVIAL(debug) << "BuildVolume printable_area clasified as: " << this->type_name();
}

#if 0
// [INTENT] rectangle_test: a more accurate O(T) triangle-vs-rectangle intersection test
// that handles non-convex object × rectangular volume correctly by projecting triangles.
// [HAZARD H802] This function is permanently disabled (#if 0). The active object_state()
// Rectangle branch uses the simpler vertex-only object_state_templ, which may miss edge
// crossings for non-convex objects. The FIXME comment at line 468 acknowledges the gap.
// Activating this would require restoring the world_min_z reference used inside.
// Not used, slower than simple bounding box collision check and nobody complained about the inaccuracy of the simple test.
static inline BuildVolume::ObjectState rectangle_test(const indexed_triangle_set &its, const Transform3f &trafo, const Vec2f min, const Vec2f max, const float max_z)
{
    bool inside = false;
    bool outside = false;

    auto sign = [](const Vec3f& pt) -> char { return pt.z() > 0 ? 1 : pt.z() < 0 ? -1 : 0; };

    // Returns true if both inside and outside are set, thus early exit.
    auto test_intersection = [&inside, &outside, min, max, max_z](const Vec3f& p1, const Vec3f& p2, const Vec3f& p3) -> bool {
        // First test whether the triangle is completely inside or outside the bounding box.
        Vec3f pmin = p1.cwiseMin(p2).cwiseMin(p3);
        Vec3f pmax = p1.cwiseMax(p2).cwiseMax(p3);
        bool tri_inside = false;
        bool tri_outside = false;
        if (pmax.x() < min.x() || pmin.x() > max.x() || pmax.y() < min.y() || pmin.y() > max.y()) {
            // Separated by one of the rectangle sides.
            tri_outside = true;
        } else if (pmin.x() >= min.x() && pmax.x() <= max.x() && pmin.y() >= min.y() && pmax.y() <= max.y()) {
            // Fully inside the rectangle.
            tri_inside = true;
        } else {
            // Bounding boxes overlap. Test triangle sides against the bbox corners.
            Vec2f v1(- p2.y() + p1.y(), p2.x() - p1.x());
            Vec2f v2(- p2.y() + p2.y(), p3.x() - p2.x());
            Vec2f v3(- p1.y() + p3.y(), p1.x() - p3.x());
            bool  ccw = cross2(v1, v2) > 0;
            for (const Vec2f &p : { Vec2f{ min.x(), min.y() }, Vec2f{ min.x(), max.y() }, Vec2f{ max.x(), min.y() }, Vec2f{ max.x(), max.y() } }) {
                auto dot = v1.dot(p);
                if (ccw ? dot >= 0 : dot <= 0)
                    tri_inside = true;
                else
                    tri_outside = true;
            }
        }
        inside  |= tri_inside;
        outside |= tri_outside;
        return inside && outside;
    };

    // Edge crosses the z plane. Calculate intersection point with the plane.
    auto clip_edge = [](const Vec3f &p1, const Vec3f &p2) -> Vec3f {
        const float t = (world_min_z - p1.z()) / (p2.z() - p1.z());
        return { p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_min_z };
    };

    // Clip at (p1, p2), p3 must be on the clipping plane.
    // Returns true if both inside and outside are set, thus early exit.
    auto clip_and_test1 = [&test_intersection, &clip_edge](const Vec3f &p1, const Vec3f &p2, const Vec3f &p3, bool p1above) -> bool {
        Vec3f pa = clip_edge(p1, p2);
        return p1above ? test_intersection(p1, pa, p3) : test_intersection(pa, p2, p3);
    };

    // Clip at (p1, p2) and (p2, p3).
    // Returns true if both inside and outside are set, thus early exit.
    auto clip_and_test2 = [&test_intersection, &clip_edge](const Vec3f &p1, const Vec3f &p2, const Vec3f &p3, bool p2above) -> bool {
        Vec3f pa = clip_edge(p1, p2);
        Vec3f pb = clip_edge(p2, p3);
        return p2above ? test_intersection(pa, p2, pb) : test_intersection(p1, pa, p3) || test_intersection(p3, pa, pb);
    };

    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f pts[3] = { trafo * its.vertices[tri(0)], trafo * its.vertices[tri(1)], trafo * its.vertices[tri(2)] };
        char signs[3] = { sign(pts[0]), sign(pts[1]), sign(pts[2]) };
        bool clips[3] = { signs[0] * signs[1] == -1, signs[1] * signs[2] == -1, signs[2] * signs[0] == -1 };
        if (clips[0]) {
            if (clips[1]) {
                // Clipping at (pt0, pt1) and (pt1, pt2).
                if (clip_and_test2(pts[0], pts[1], pts[2], signs[1] > 0))
                    break;
            } else if (clips[2]) {
                // Clipping at (pt0, pt1) and (pt0, pt2).
                if (clip_and_test2(pts[2], pts[0], pts[1], signs[0] > 0))
                    break;
            } else {
                // Clipping at (pt0, pt1), pt2 must be on the clipping plane.
                if (clip_and_test1(pts[0], pts[1], pts[2], signs[0] > 0))
                    break;
            }
        } else if (clips[1]) {
            if (clips[2]) {
                // Clipping at (pt1, pt2) and (pt0, pt2).
                if (clip_and_test2(pts[0], pts[1], pts[2], signs[1] > 0))
                    break;
            } else {
                // Clipping at (pt1, pt2), pt0 must be on the clipping plane.
                if (clip_and_test1(pts[1], pts[2], pts[0], signs[1] > 0))
                    break;
            }
        } else if (clips[2]) {
            // Clipping at (pt0, pt2), pt1 must be on the clipping plane.
            if (clip_and_test1(pts[2], pts[0], pts[1], signs[2] > 0))
                break;
        } else if (signs[0] >= 0 && signs[1] >= 0 && signs[2] >= 0) {
            // The triangle is above or on the clipping plane.
            if (test_intersection(pts[0], pts[1], pts[2]))
                break;
        }
    }
    return inside ? (outside ? BuildVolume::ObjectState::Colliding : BuildVolume::ObjectState::Inside) : BuildVolume::ObjectState::Outside;
}
#endif

// [INTENT] object_state_templ: generic O(V) ITS collision test.
// Template parameter InsideFn is callable(const Vec3f&) → bool.
//
// Two paths:
//   may_be_below_bed=true  — clips edges against z=world_min_z to detect intersections
//     of meshes straddling the print surface. Returns Below if all vertices below bed.
//   may_be_below_bed=false — fast path: tests all vertices only, no edge-crossing logic.
//
// Returns: Below / Inside / Colliding / Outside.
// [HAZARD H797] (see header): edge-crossing only runs if may_be_below_bed && num_above < total.
// Trim the input transformed triangle mesh with print bed and test the remaining vertices with is_inside callback.
// Return inside / colliding / outside state.
template<typename InsideFn>
BuildVolume::ObjectState object_state_templ(
    const indexed_triangle_set& its, const Transform3f& trafo, bool may_be_below_bed, bool convex, InsideFn is_inside)
{
    size_t                      num_inside  = 0;
    size_t                      num_above   = 0;
    bool                        inside      = false;
    bool                        outside     = false;
    static constexpr const auto world_min_z = float(-BuildVolume::SceneEpsilon);

    if (may_be_below_bed) {
        // Slower test, needs to clip the object edges with the print bed plane.
        // 1) Allocate transformed vertices with their position with respect to print bed surface.
        std::vector<char> sides;
        sides.reserve(its.vertices.size());

        const auto sign = [](const stl_vertex& pt) { return pt.z() > world_min_z ? 1 : pt.z() < world_min_z ? -1 : 0; };

        bool below_outside = false;

        for (const stl_vertex& v : its.vertices) {
            stl_vertex pt = trafo * v;
            const int  s  = sign(pt);
            sides.emplace_back(s);
            if (s >= 0) {
                // Vertex above or on print bed surface. Test whether it is inside the build volume.
                ++num_above;
                if (is_inside(pt))
                    ++num_inside;
            } else if (convex && !below_outside) {
                pt.z() = 0;
                if (!is_inside(pt))
                    below_outside = true;
            }
        }

        if (num_above == 0)
            // Special case, the object is completely below the print bed, thus it is outside,
            // however we want to allow an object to be still printable if some of its parts are completely below the print bed.
            return BuildVolume::ObjectState::Below;

        // 2) Calculate intersections of triangle edges with the build surface.
        inside  = num_inside > 0;
        outside = num_inside < num_above;
        // Orca: for convex shape, if everything inside then don't bother check intersection
        if (num_above < its.vertices.size() && !(inside && outside) && (!(inside && !below_outside) || !convex)) {
            // Not completely above the build surface and status may still change by testing edges intersecting the build platform.
            for (const stl_triangle_vertex_indices& tri : its.indices) {
                const int s[3] = {sides[tri(0)], sides[tri(1)], sides[tri(2)]};
                if (std::min(s[0], std::min(s[1], s[2])) < 0 && std::max(s[0], std::max(s[1], s[2])) > 0) {
                    // Some edge of this triangle intersects the build platform. Calculate the intersection.
                    int iprev = 2;
                    for (int iedge = 0; iedge < 3; ++iedge) {
                        if (s[iprev] * s[iedge] == -1) {
                            // edge intersects the build surface. Calculate intersection point.
                            const stl_vertex p1 = trafo * its.vertices[tri(iprev)];
                            const stl_vertex p2 = trafo * its.vertices[tri(iedge)];
                            assert(sign(p1) == s[iprev]);
                            assert(sign(p2) == s[iedge]);
                            assert(p1.z() * p2.z() < 0);
                            // Edge crosses the z plane. Calculate intersection point with the plane.
                            const float t = (world_min_z - p1.z()) / (p2.z() - p1.z());
                            (is_inside(Vec3f(p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_min_z)) ?
                                 inside :
                                 outside) = true;
                        }
                        iprev = iedge;
                    }
                    if (inside && outside)
                        break;
                }
            }
        }
    } else {
        // Much simpler and faster code, not clipping the object with the print bed.
        assert(!may_be_below_bed);
        num_above = its.vertices.size();
        for (const stl_vertex& v : its.vertices) {
            const stl_vertex pt = trafo * v;
            assert(pt.z() >= world_min_z);
            if (is_inside(pt))
                ++num_inside;
        }
        inside  = num_inside > 0;
        outside = num_inside < num_above;
    }

    return inside ? (outside ? BuildVolume::ObjectState::Colliding : BuildVolume::ObjectState::Inside) : BuildVolume::ObjectState::Outside;
}

// [INTENT] object_state: public dispatch entry point. Selects the containment test
// appropriate for the classified bed type and delegates to object_state_templ.
// Rectangle → bbox test (O(1) inflation, O(V) vertices).
// Circle    → circle containment (O(V) vertices).
// Convex/Custom → convex-hull half-plane decomposition (O(V×H) where H=hull edges).
// [HAZARD H798] Custom uses convex hull — non-convex "notch" areas are incorrectly Inside.
// [HAZARD H801] FIXME comment at Rectangle branch: non-convex object×rectangular volume
// intersection is not detected (vertex test only, no edge-crossing for XY extent).
BuildVolume::ObjectState BuildVolume::object_state(const indexed_triangle_set& its,
                                                   const Transform3f&          trafo,
                                                   bool                        may_be_below_bed,
                                                   bool                        ignore_bottom) const
{
    switch (m_type) {
    case BuildVolume_Type::Rectangle: {
        BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(SceneEpsilon);
        if (m_max_print_height == 0.0)
            build_volume.max.z() = std::numeric_limits<double>::max();
        if (ignore_bottom)
            build_volume.min.z() = -std::numeric_limits<double>::max();
        BoundingBox3Base<Vec3f> build_volumef(build_volume.min.cast<float>(), build_volume.max.cast<float>());
        // The following test correctly interprets intersection of a non-convex object with a rectangular build volume.
        // return rectangle_test(its, trafo, to_2d(build_volume.min), to_2d(build_volume.max), build_volume.max.z());
        // FIXME This test does NOT correctly interprets intersection of a non-convex object with a rectangular build volume.
        return object_state_templ(its, trafo, may_be_below_bed, true,
                                  [build_volumef](const Vec3f& pt) { return build_volumef.contains(pt); });
    }
    case BuildVolume_Type::Circle: {
        Geometry::Circlef circle{unscaled<float>(m_circle.center), unscaled<float>(m_circle.radius + SceneEpsilon)};
        return m_max_print_height == 0.0 ?
                   object_state_templ(its, trafo, may_be_below_bed, true, [circle](const Vec3f& pt) { return circle.contains(to_2d(pt)); }) :
                   object_state_templ(its, trafo, may_be_below_bed, true, [circle, z = m_max_print_height + SceneEpsilon](const Vec3f& pt) {
                       return pt.z() < z && circle.contains(to_2d(pt));
                   });
    }
    case BuildVolume_Type::Convex:
    // FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
    case BuildVolume_Type::Custom:
        return m_max_print_height == 0.0 ?
                   object_state_templ(its, trafo, may_be_below_bed, m_type == BuildVolume_Type::Convex,
                                      [this](const Vec3f& pt) {
                                          return Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_scene,
                                                                                 to_2d(pt).cast<double>());
                                      }) :
                   object_state_templ(its, trafo, may_be_below_bed, m_type == BuildVolume_Type::Convex,
                                      [this, z = m_max_print_height + SceneEpsilon](const Vec3f& pt) {
                                          return pt.z() < z && Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_scene,
                                                                                               to_2d(pt).cast<double>());
                                      });
    case BuildVolume_Type::Invalid:
    default: return ObjectState::Inside;
    }
}

// [INTENT] volume_state_bbox: fast bounding-box containment test for rectangular beds.
// Only valid for Rectangle beds (asserted). Tests the given 3D bbox against the build volume bbox.
// [HAZARD H799] Returns instantly if the full paths_bbox fits — does NOT check per-vertex.
// This is a conservative O(1) test: if the path bbox exceeds the build volume, returns Colliding.
BuildVolume::ObjectState BuildVolume::volume_state_bbox(const BoundingBoxf3& volume_bbox, bool ignore_bottom) const
{
    assert(m_type == BuildVolume_Type::Rectangle);
    BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(SceneEpsilon);
    if (m_max_print_height == 0.0)
        build_volume.max.z() = std::numeric_limits<double>::max();
    if (ignore_bottom)
        build_volume.min.z() = -std::numeric_limits<double>::max();
    return build_volume.max.z() <= -SceneEpsilon ? ObjectState::Below :
           build_volume.contains(volume_bbox)    ? ObjectState::Inside :
           build_volume.intersects(volume_bbox)  ? ObjectState::Colliding :
                                                   ObjectState::Outside;
}

// [INTENT] get_extruder_area_volume: simple index accessor into m_extruder_volumes.
// [HAZARD] No bounds-checked public overload — callers must guard index < get_extruder_area_count().
// [COUPLING] Result reference invalidated if m_extruder_volumes is resized (constructor only mutates it).
const BuildVolume::BuildExtruderVolume& BuildVolume::get_extruder_area_volume(int index) const
{
    assert(index >= 0 && index < m_extruder_volumes.size());
    return m_extruder_volumes[index];
}

// [INTENT] check_object_state_with_extruder_area: test one ITS mesh against one
// specific extruder's reachable area. Returns Limited if not inside.
// [HAZARD H800] For Convex/Custom/Invalid extruder shapes, falls through to default
// and returns Inside silently — no test performed for non-Rect/Circle extruder areas.
BuildVolume::ObjectState BuildVolume::check_object_state_with_extruder_area(const indexed_triangle_set& its,
                                                                            const Transform3f&          trafo,
                                                                            int                         index) const
{
    const BuildExtruderVolume& extruder_volume = get_extruder_area_volume(index);
    ObjectState                return_state    = ObjectState::Inside;

    if (!extruder_volume.same_with_bed) {
        switch (extruder_volume.type) {
        case BuildVolume_Type::Rectangle: {
            BoundingBox3Base<Vec3d> build_volume = extruder_volume.bboxf.inflated(SceneEpsilon);
            if (m_max_print_height == 0.0)
                build_volume.max.z() = std::numeric_limits<double>::max();
            BoundingBox3Base<Vec3f> build_volumef(build_volume.min.cast<float>(), build_volume.max.cast<float>());

            return_state = object_state_templ(its, trafo, false, true,
                                              [build_volumef](const Vec3f& pt) { return build_volumef.contains(pt); });
            break;
        }
        case BuildVolume_Type::Circle: {
            Geometry::Circlef circle{unscaled<float>(extruder_volume.circle.center),
                                     unscaled<float>(extruder_volume.circle.radius + SceneEpsilon)};
            return_state = (m_max_print_height == 0.0) ?
                               object_state_templ(its, trafo, false, true,
                                                  [circle](const Vec3f& pt) { return circle.contains(to_2d(pt)); }) :
                               object_state_templ(its, trafo, false, true, [circle, z = m_max_print_height + SceneEpsilon](const Vec3f& pt) {
                                   return pt.z() < z && circle.contains(to_2d(pt));
                               });
            break;
        }
        case BuildVolume_Type::Invalid:
        default: break;
        }
    }

    if (return_state != ObjectState::Inside)
        return_state = ObjectState::Limited;

    return return_state;
}

// [INTENT] check_object_state_with_extruder_areas: iterate all extruder volumes and
// aggregate per-extruder reachability into the inside_extruders boolean vector.
// Returns Limited if any extruder cannot reach a part of the object.
// [COUPLING] Calls check_object_state_with_extruder_area() per extruder; inherits H800.
// [STATE] inside_extruders is resized here — callers must not pre-size it differently.
BuildVolume::ObjectState BuildVolume::check_object_state_with_extruder_areas(const indexed_triangle_set& its,
                                                                             const Transform3f&          trafo,
                                                                             std::vector<bool>&          inside_extruders) const
{
    ObjectState result              = ObjectState::Inside;
    int         extruder_area_count = get_extruder_area_count();
    inside_extruders.resize(extruder_area_count, true);
    for (int index = 0; index < extruder_area_count; index++) {
        ObjectState state = check_object_state_with_extruder_area(its, trafo, index);

        if (state == ObjectState::Limited) {
            inside_extruders[index] = false;
            result                  = ObjectState::Limited;
        }
    }

    return result;
}

// [INTENT] check_volume_bbox_state_with_extruder_area: O(1) bbox-level extruder reachability
// check for a single extruder. Returns Inside if extruder_bbox contains volume_bbox.
// [HAZARD] Uses only the inflated bboxf — non-rectangular extruder shapes get their bbox
// tested, which is looser than the actual extruder polygon. May pass objects in corners
// of a non-rectangular extruder shape that are not actually reachable.
// [COUPLING] Called by check_volume_bbox_state_with_extruder_areas() (fan-out).
BuildVolume::ObjectState BuildVolume::check_volume_bbox_state_with_extruder_area(const BoundingBoxf3& volume_bbox, int index) const
{
    const BuildExtruderVolume& extruder_volume = get_extruder_area_volume(index);
    BoundingBox3Base<Vec3d>    extruder_bbox   = extruder_volume.bboxf.inflated(SceneEpsilon);
    if (extruder_volume.same_with_bed || extruder_bbox.contains(volume_bbox))
        return ObjectState::Inside;
    else
        return ObjectState::Limited;
}

// [INTENT] check_volume_bbox_state_with_extruder_areas: fan-out bbox reachability check
// across all extruders. Aggregates results into inside_extruders and returns Limited if any fail.
// [STATE] inside_extruders is resized here; initialized to true then set false per-extruder.
// [COUPLING] Mirrors check_object_state_with_extruder_areas but uses bbox instead of ITS mesh.
BuildVolume::ObjectState BuildVolume::check_volume_bbox_state_with_extruder_areas(const BoundingBoxf3& volume_bbox,
                                                                                  std::vector<bool>&   inside_extruders) const
{
    ObjectState result              = ObjectState::Inside;
    int         extruder_area_count = get_extruder_area_count();
    inside_extruders.resize(extruder_area_count, true);
    for (int index = 0; index < extruder_area_count; index++) {
        ObjectState state = check_volume_bbox_state_with_extruder_area(volume_bbox, index);

        if (state == ObjectState::Limited) {
            inside_extruders[index] = false;
            result                  = ObjectState::Limited;
        }
    }

    return result;
}

// [INTENT] all_paths_inside: post-slicing check — tests every valid extrusion move vertex
// against the build volume. Called after GCodeProcessor runs to validate exported G-code.
//
// Rectangle → O(1) bbox containment test (conservative: entire paths_bbox must fit).
// Circle    → O(M) per-move radial distance test (M = number of moves).
// Convex/Custom → O(M×H) convex-hull half-plane test per move.
// [HAZARD H799] Rectangle fast path: build_volume.contains(paths_bbox) — if bbox fits, all
//              moves are declared inside without checking them individually. Any move that
//              individually exceeds the volume but whose bbox still fits would be missed
//              (impossible by definition of bbox, but worth noting the O(1) abstraction).
// [COUPLING] Reads GCodeProcessorResult::moves (built in Phase 1 of GCodeProcessor pipeline).
//            move_valid lambda filters out travel, custom, and zero-width/height moves.
bool BuildVolume::all_paths_inside(const GCodeProcessorResult& paths, const BoundingBoxf3& paths_bbox, bool ignore_bottom) const
{
    auto move_valid = [](const GCodeProcessorResult::MoveVertex& move) {
        return move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width != 0.f && move.height != 0.f;
    };
    static constexpr const double epsilon = BedEpsilon;

    switch (m_type) {
    case BuildVolume_Type::Rectangle: {
        BoundingBox3Base<Vec3d> build_volume = this->bounding_volume().inflated(epsilon);
        if (m_max_print_height == 0.0)
            build_volume.max.z() = std::numeric_limits<double>::max();
        if (ignore_bottom)
            build_volume.min.z() = -std::numeric_limits<double>::max();
        return build_volume.contains(paths_bbox);
    }
    case BuildVolume_Type::Circle: {
        const Vec2f c  = unscaled<float>(m_circle.center);
        const float r  = unscaled<double>(m_circle.radius) + epsilon;
        const float r2 = sqr(r);
        return m_max_print_height == 0.0 ?
                   std::all_of(paths.moves.begin(), paths.moves.end(),
                               [move_valid, c, r2](const GCodeProcessorResult::MoveVertex& move) {
                                   return !move_valid(move) || (to_2d(move.position) - c).squaredNorm() <= r2;
                               }) :
                   std::all_of(paths.moves.begin(), paths.moves.end(),
                               [move_valid, c, r2, z = m_max_print_height + epsilon](const GCodeProcessorResult::MoveVertex& move) {
                                   return !move_valid(move) || ((to_2d(move.position) - c).squaredNorm() <= r2 && move.position.z() <= z);
                               });
    }
    case BuildVolume_Type::Convex:
    // FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
    case BuildVolume_Type::Custom:
        return m_max_print_height == 0.0 ?
                   std::all_of(paths.moves.begin(), paths.moves.end(),
                               [move_valid, this](const GCodeProcessorResult::MoveVertex& move) {
                                   return !move_valid(move) || Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_bed,
                                                                                               to_2d(move.position).cast<double>());
                               }) :
                   std::all_of(paths.moves.begin(), paths.moves.end(),
                               [move_valid, this, z = m_max_print_height + epsilon](const GCodeProcessorResult::MoveVertex& move) {
                                   return !move_valid(move) || (Geometry::inside_convex_polygon(m_top_bottom_convex_hull_decomposition_bed,
                                                                                                to_2d(move.position).cast<double>()) &&
                                                                move.position.z() <= z);
                               });
    default: return true;
    }
}

// [INTENT] all_inside_vertices_normals_interleaved: helper for GL rendering path.
// Input is a flat float array with interleaved vertex/normal pairs (6 floats per point:
// vx,vy,vz, nx,ny,nz). Skips vertex, reads normal, applies fn to each normal vector.
// [COUPLING] Used by the GUI layer (GLCanvas) to validate bed mesh normals for rendering.
// [UNCLEAR → RESOLVED] The `paths` parameter is a flat interleaved vertex/normal float buffer, and this helper tests only each normal triplet.
// The naming mismatch from the public-facing all_paths_inside() could confuse maintainers.
template<typename Fn> inline bool all_inside_vertices_normals_interleaved(const std::vector<float>& paths, Fn fn)
{
    for (auto it = paths.begin(); it != paths.end();) {
        it += 3;
        if (!fn({*it, *(it + 1), *(it + 2)}))
            return false;
        it += 3;
    }
    return true;
}

// [INTENT] type_name: debug/logging helper. Maps BuildVolume_Type enum → string_view.
// [COUPLING] String literals used in BOOST_LOG_TRIVIAL at constructor end and in GUI status.
// [HAZARD] No exhaustive coverage: unrecognized enum values hit assert(false) + return {}.
//          In release builds this is silent UB (assert disabled). Add a default return.
std::string_view BuildVolume::type_name(BuildVolume_Type type)
{
    using namespace std::literals;
    switch (type) {
    case BuildVolume_Type::Invalid: return "Invalid"sv;
    case BuildVolume_Type::Rectangle: return "Rectangle"sv;
    case BuildVolume_Type::Circle: return "Circle"sv;
    case BuildVolume_Type::Convex: return "Convex"sv;
    case BuildVolume_Type::Custom: return "Custom"sv;
    }
    // make visual studio happy
    assert(false);
    return {};
}

// [INTENT] bounding_mesh: generates an axis-aligned cube ITS mesh representing the build
// volume bounding box. Used for GL scene rendering (the translucent volume indicator).
// scale=true → outputs in scaled integer units (for libslic3r polygon ops);
// scale=false → outputs in mm (for OpenGL upload).
// [COUPLING] Ignores m_polygon shape — always returns a cube even for circle/custom beds.
//            The GL code overlays the actual bed polygon separately.
// [UNCLEAR → RESOLVED] `bounding_mesh()` builds an axis-aligned box from the origin to `m_bboxf.max`, so it ignores any nonzero `m_bboxf.min` bed offset.
indexed_triangle_set BuildVolume::bounding_mesh(bool scale) const
{
    auto max_pt3 = m_bboxf.max;
    if (scale) {
        return its_make_cube(scale_(max_pt3.x()), scale_(max_pt3.y()), scale_(max_pt3.z()));
    } else {
        return its_make_cube(max_pt3.x(), max_pt3.y(), max_pt3.z());
    }
}

} // namespace Slic3r
