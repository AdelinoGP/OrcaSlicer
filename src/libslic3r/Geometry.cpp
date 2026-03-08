// [INTENT] Geometry.cpp — core geometric utility implementations for OrcaSlicer.
// Provides:
//   - Angle comparison helpers (directions_parallel, directions_perpendicular)
//   - Polygon simplification wrapper (Douglas-Peucker via Clipper)
//   - Legacy bed-arrangement algorithm (arrange())
//   - Transform3d assembly/decomposition helpers
//   - Transformation class methods (get/set rotation, scale, mirror, offset, SVD)
//   - Euler-angle extraction (eulerAngles ZYX decomposition)
//   - 3MF/AMF transform-string parser
//   - TransformationSVD (Jacobi SVD for rotation/scale decomposition)
//   - Point-in-polygon corner test helper
//
// [STATE] All functions are stateless (no static/global mutable state in this file).
//         Side effects are limited to output parameters and returned values.
//
// [COUPLING] Depends on:
//   - Eigen (Transform3d, Matrix3d, Quaterniond, AngleAxisd, JacobiSVD)
//   - ClipperUtils (Slic3r::simplify_polygons)
//   - ExPolygon, Line, Point (geometric primitives)
//   - boost::algorithm (string split for transform3d_from_string)
//
// [HAZARD H528] The `arrange()` function exists in two implementations inside an
//   `#if 0 / #else / #endif` block (lines ~78–283). The `#if 0` branch is dead code
//   (a cleaner algorithm). The active `#else` branch uses a binary insertion sort
//   with a `goto ENDSORT` label — a pre-C++11 pattern. The label-goto pattern is
//   unusual in modern C++ and error-prone during porting.
//
// [HAZARD H529] `extract_euler_angles()` calls `rotation_matrix.eulerAngles(2,1,0)`
//   (ZYX order) then swaps angles(0) and angles(2) to produce an XYZ-indexed triplet.
//   Eigen's `eulerAngles()` has documented discontinuities (gimbal-lock, angle range
//   ambiguity). For rotations near the gimbal-lock singularity (Y ≈ ±90°), the returned
//   angles may differ from the "expected" values by ±π, causing non-round-trip behaviour
//   in set_rotation→get_rotation cycles. This is the root cause of H511 in Model.cpp.
//
// [HAZARD H530] `transform3d_from_string()` calls `::atof()` with a C-string.
//   `atof()` uses the locale's decimal separator. If the locale decimal separator is
//   not '.' (e.g. European locales using ','), parsing of 3MF transform strings
//   will silently produce zero for all components after the first.
//   The assert `is_decimal_separator_point()` fires in Debug builds only.
//
// [HAZARD H531] `Transformation::volume_to_bed_transformation()`: the least-squares
//   scaling path computes `scale(i) = pts.col(i).dot(qs.col(i)) / pts.col(i).dot(pts.col(i))`.
//   If the bounding box is degenerate on any axis (min == max → col(i) is all-zeros),
//   the denominator is zero and the division produces NaN/inf scale factors with no
//   error check. The result is silently stored and later applied.
//
// [HAZARD H532] `mat_around_a_point_rotate()`: calls `temp_world.get_matrix().inverse()`
//   without checking if the matrix is invertible. If `temp_world` has a zero-scale axis
//   (degenerate transformation), the inverse is undefined and produces NaN values that
//   corrupt `temp_xyz` and `new_pos`.
//
// [HAZARD H533] `Transformation::set_mirror()` silently normalises any non-{-1,0,1}
//   mirror value by dividing by its absolute value. If `mirror(i) == 0.0`, it resets
//   to 1.0. This lossy normalisation is not documented in the API, so callers passing
//   partial mirror values (e.g. 0.5) get silently corrected behaviour without any warning.
//
// [HAZARD H534] `Transformation::reset_rotation()` and `reset_scaling_factor()` both
//   construct a `TransformationSVD` (Jacobi SVD). SVD is O(N³) for 3×3 — fast in
//   absolute terms but called from UI-thread rotation/scale reset operations that may
//   fire on every slider tick. Not a correctness hazard but a latency concern.

#include "libslic3r.h"
#include "Exception.hpp"
#include "Geometry.hpp"
#include "ClipperUtils.hpp"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "clipper.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <utility>
#include <stack>
#include <vector>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/log/trivial.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

namespace Slic3r { namespace Geometry {

// [INTENT] Returns true if two angles are within max_diff of being parallel (or anti-parallel).
// Adds EPSILON to max_diff to account for floating-point rounding in the diff computation.
// [STATE] Stateless — pure function.
bool directions_parallel(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return diff < max_diff || fabs(diff - PI) < max_diff;
}

// [INTENT] Returns true if two angles are within max_diff of being perpendicular (90° or 270°).
// [STATE] Stateless — pure function.
bool directions_perpendicular(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return fabs(diff - 0.5 * PI) < max_diff || fabs(diff - 1.5 * PI) < max_diff;
}

// [INTENT] Checks whether any element of a vector<T> contains a Point.
// Explicitly instantiated for ExPolygons.
// [COUPLING] Requires T to have a contains(Point) method.
template<class T> bool contains(const std::vector<T>& vector, const Point& point)
{
    for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
        if (it->contains(point))
            return true;
    }
    return false;
}
template bool contains(const ExPolygons& vector, const Point& point);

// [INTENT] Converts a direction angle (radians, 0=East CCW) to compass-style degrees
// (0=North CW) for display purposes. Used for printing angle annotations.
// [STATE] Stateless — pure function.
double rad2deg_dir(double angle)
{
    angle = (angle < PI) ? (-angle + PI / 2.0) : (angle + PI / 2.0);
    if (angle < 0)
        angle += PI;
    return rad2deg(angle);
}

// [INTENT] Simplifies a set of polygons using Douglas-Peucker with the given tolerance,
// then unions the result via Slic3r::simplify_polygons to remove self-intersections.
// [MEMORY] Allocates a temporary copy (pp) of all polygons; O(N·vertices) overhead.
// [COUPLING] Calls MultiPoint::_douglas_peucker (internal) and Slic3r::simplify_polygons
//   (ClipperUtils wrapper).
void simplify_polygons(const Polygons& polygons, double tolerance, Polygons* retval)
{
    Polygons pp;
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it) {
        Polygon p = *it;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.push_back(p);
    }
    *retval = Slic3r::simplify_polygons(pp);
}

// [INTENT] Linear interpolation: maps value from [oldmin, oldmax] to [newmin, newmax].
// [STATE] Stateless — pure function.
double linint(double value, double oldmin, double oldmax, double newmin, double newmax)
{
    return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

// [HAZARD H528] Dead-code block: the first `#if 0` branch is an older, cleaner
// implementation of the arrange() algorithm. It is never compiled.
// The active `#else` branch below is the one actually used by the build.
#if 0
// Point with a weight, by which the points are sorted.
// If the points have the same weight, sort them lexicographically by their positions.
struct ArrangeItem {
    ArrangeItem() {}
    Vec2d    pos;
    coordf_t  weight;
    bool operator<(const ArrangeItem &other) const {
        return weight < other.weight ||
            ((weight == other.weight) && (pos(1) < other.pos(1) || (pos(1) == other.pos(1) && pos(0) < other.pos(0))));
    }
};

Pointfs arrange(size_t num_parts, const Vec2d &part_size, coordf_t gap, const BoundingBoxf* bed_bounding_box)
{
    // Use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm.
    const Vec2d       cell_size(part_size(0) + gap, part_size(1) + gap);

    const BoundingBoxf bed_bbox = (bed_bounding_box != NULL && bed_bounding_box->defined) ? 
        *bed_bounding_box :
        // Bogus bed size, large enough not to trigger the unsufficient bed size error.
        BoundingBoxf(
            Vec2d(0, 0),
            Vec2d(cell_size(0) * num_parts, cell_size(1) * num_parts));

    // This is how many cells we have available into which to put parts.
    size_t cellw = size_t(floor((bed_bbox.size()(0) + gap) / cell_size(0)));
    size_t cellh = size_t(floor((bed_bbox.size()(1) + gap) / cell_size(1)));
    if (num_parts > cellw * cellh)
        throw Slic3r::InvalidArgument("%zu parts won't fit in your print area!\n", num_parts);
    
    // Get a bounding box of cellw x cellh cells, centered at the center of the bed.
    Vec2d       cells_size(cellw * cell_size(0) - gap, cellh * cell_size(1) - gap);
    Vec2d       cells_offset(bed_bbox.center() - 0.5 * cells_size);
    BoundingBoxf cells_bb(cells_offset, cells_size + cells_offset);
    
    // List of cells, sorted by distance from center.
    std::vector<ArrangeItem> cellsorder(cellw * cellh, ArrangeItem());
    for (size_t j = 0; j < cellh; ++ j) {
        // Center of the jth row on the bed.
        coordf_t cy = linint(j + 0.5, 0., double(cellh), cells_bb.min(1), cells_bb.max(1));
        // Offset from the bed center.
        coordf_t yd = cells_bb.center()(1) - cy;
        for (size_t i = 0; i < cellw; ++ i) {
            // Center of the ith column on the bed.
            coordf_t cx = linint(i + 0.5, 0., double(cellw), cells_bb.min(0), cells_bb.max(0));
            // Offset from the bed center.
            coordf_t xd = cells_bb.center()(0) - cx;
            // Cell with a distance from the bed center.
            ArrangeItem &ci = cellsorder[j * cellw + i];
            // Cell center
            ci.pos(0) = cx;
            ci.pos(1) = cy;
            // Square distance of the cell center to the bed center.
            ci.weight = xd * xd + yd * yd;
        }
    }
    // Sort the cells lexicographically by their distances to the bed center and left to right / bttom to top.
    std::sort(cellsorder.begin(), cellsorder.end());
    cellsorder.erase(cellsorder.begin() + num_parts, cellsorder.end());

    // Return the (left,top) corners of the cells.
    Pointfs positions;
    positions.reserve(num_parts);
    for (std::vector<ArrangeItem>::const_iterator it = cellsorder.begin(); it != cellsorder.end(); ++ it)
        positions.push_back(Vec2d(it->pos(0) - 0.5 * part_size(0), it->pos(1) - 0.5 * part_size(1)));
    return positions;
}
#else
// [INTENT] arrange() — places `total_parts` rectangular objects of `part_size` on a bed,
// separating them by `dist`. Returns false if the parts don't fit.
// Algorithm: compute a grid of cells, rank cells by Euclidean distance from bed center
// (with a column-bias tiebreak), binary-insert into a sorted list, then assign parts
// to the closest cells.
// [STATE] Stateless — pure function (no global state accessed or modified).
// [HAZARD H528] Uses `goto ENDSORT` inside a nested loop for the binary-insert fast path.
//   The goto jumps forward out of the `while` loop to skip the trailing insertion when
//   an exact index match is found. Correct but unusual in modern C++; a refactored port
//   should replace with a proper loop-break or std::lower_bound insertion.
// [MEMORY] Allocates cellw*cellh ArrangeItemIndex objects; all on the stack-frame heap.
//   For very large grids (e.g. 50×50 = 2500 cells) this is a few hundred KB — acceptable.
class ArrangeItem
{
public:
    Vec2d    pos = Vec2d::Zero();
    size_t   index_x, index_y;
    coordf_t dist;
};
class ArrangeItemIndex
{
public:
    coordf_t    index;
    ArrangeItem item;
    ArrangeItemIndex(coordf_t _index, ArrangeItem _item) : index(_index), item(_item) {};
};

bool arrange(size_t total_parts, const Vec2d& part_size, coordf_t dist, const BoundingBoxf* bb, Pointfs& positions)
{
    positions.clear();

    Vec2d part = part_size;

    // use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
    part(0) += dist;
    part(1) += dist;

    Vec2d area(Vec2d::Zero());
    if (bb != NULL && bb->defined) {
        area = bb->size();
    } else {
        // bogus area size, large enough not to trigger the error below
        area(0) = part(0) * total_parts;
        area(1) = part(1) * total_parts;
    }

    // this is how many cells we have available into which to put parts
    size_t cellw = floor((area(0) + dist) / part(0));
    size_t cellh = floor((area(1) + dist) / part(1));
    if (total_parts > (cellw * cellh))
        return false;

    // total space used by cells
    Vec2d cells(cellw * part(0), cellh * part(1));

    // bounding box of total space used by cells
    BoundingBoxf cells_bb;
    cells_bb.merge(Vec2d(0, 0)); // min
    cells_bb.merge(cells);       // max

    // center bounding box to area
    cells_bb.translate((area(0) - cells(0)) / 2, (area(1) - cells(1)) / 2);

    // list of cells, sorted by distance from center
    std::vector<ArrangeItemIndex> cellsorder;

    // work out distance for all cells, sort into list
    for (size_t i = 0; i <= cellw - 1; ++i) {
        for (size_t j = 0; j <= cellh - 1; ++j) {
            coordf_t cx = linint(i + 0.5, 0, cellw, cells_bb.min(0), cells_bb.max(0));
            coordf_t cy = linint(j + 0.5, 0, cellh, cells_bb.min(1), cells_bb.max(1));

            coordf_t xd = fabs((area(0) / 2) - cx);
            coordf_t yd = fabs((area(1) / 2) - cy);

            ArrangeItem c;
            c.pos(0)  = cx;
            c.pos(1)  = cy;
            c.index_x = i;
            c.index_y = j;
            c.dist    = xd * xd + yd * yd - fabs((cellw / 2) - (i + 0.5));

            // binary insertion sort
            {
                coordf_t index = c.dist;
                size_t   low   = 0;
                size_t   high  = cellsorder.size();
                while (low < high) {
                    size_t   mid    = (low + ((high - low) / 2)) | 0;
                    coordf_t midval = cellsorder[mid].index;

                    if (midval < index) {
                        low = mid + 1;
                    } else if (midval > index) {
                        high = mid;
                    } else {
                        cellsorder.insert(cellsorder.begin() + mid, ArrangeItemIndex(index, c));
                        goto ENDSORT;
                    }
                }
                cellsorder.insert(cellsorder.begin() + low, ArrangeItemIndex(index, c));
            }
        ENDSORT:;
        }
    }

    // the extents of cells actually used by objects
    coordf_t lx = 0;
    coordf_t ty = 0;
    coordf_t rx = 0;
    coordf_t by = 0;

    // now find cells actually used by objects, map out the extents so we can position correctly
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c  = cellsorder[i - 1];
        coordf_t         cx = c.item.index_x;
        coordf_t         cy = c.item.index_y;
        if (i == 1) {
            lx = rx = cx;
            ty = by = cy;
        } else {
            if (cx > rx)
                rx = cx;
            if (cx < lx)
                lx = cx;
            if (cy > by)
                by = cy;
            if (cy < ty)
                ty = cy;
        }
    }
    // now we actually place objects into cells, positioned such that the left and bottom borders are at 0
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder.front();
        cellsorder.erase(cellsorder.begin());
        coordf_t cx = c.item.index_x - lx;
        coordf_t cy = c.item.index_y - ty;

        positions.push_back(Vec2d(cx * part(0), cy * part(1)));
    }

    if (bb != NULL && bb->defined) {
        for (Pointfs::iterator p = positions.begin(); p != positions.end(); ++p) {
            p->x() += bb->min(0);
            p->y() += bb->min(1);
        }
    }

    return true;
}
#endif

// [INTENT] Euclidean distance between two boost::polygon points.
// Template over coordinate type T (typically int64_t or double).
// [STATE] Stateless — pure function.
template<typename T> T dist(const boost::polygon::point_data<T>& p1, const boost::polygon::point_data<T>& p2)
{
    T dx = p2(0) - p1(0);
    T dy = p2(1) - p1(1);
    return sqrt(dx * dx + dy * dy);
}

// [INTENT] Projects point `px` onto segment `seg` and returns the foot point.
// Uses the parametric projection t = dot(dir, dproj) / dot(dir, dir).
// [HAZARD H535] The assert checks t in [-1e-6, 1+1e-6] but does NOT clamp t to [0,1].
//   If `px` is beyond the segment endpoints, the foot point will be outside the segment.
//   The assert fires in Debug builds only — Release builds silently extrapolate.
//   Callers that expect a clamped foot point will get wrong results.
template<typename segment_type, typename point_type> inline point_type project_point_to_segment(segment_type& seg, point_type& px)
{
    typedef typename point_type::coordinate_type T;
    const point_type&                            p0 = low(seg);
    const point_type&                            p1 = high(seg);
    const point_type                             dir(p1(0) - p0(0), p1(1) - p0(1));
    const point_type                             dproj(px(0) - p0(0), px(1) - p0(1));
    const T                                      t = (dir(0) * dproj(0) + dir(1) * dproj(1)) / (dir(0) * dir(0) + dir(1) * dir(1));
    assert(t >= T(-1e-6) && t <= T(1. + 1e-6));
    return point_type(p0(0) + t * dir(0), p0(1) + t * dir(1));
}

// [INTENT] assemble_transform() — builds a Transform3d from translation, rotation (ZYX Euler),
// scale, and mirror vectors. The rotation is applied as AngleAxisd(Z) * AngleAxisd(Y) * AngleAxisd(X).
// [STATE] Output parameter `transform` is overwritten (not accumulated).
// [COUPLING] The ZYX multiplication order is tightly coupled to extract_euler_angles() (also ZYX).
//   Mixing these with other rotation conventions will produce incorrect transforms.
void assemble_transform(Transform3d& transform, const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
    transform.rotate(Eigen::AngleAxisd(rotation(2), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation(1), Vec3d::UnitY()) *
                     Eigen::AngleAxisd(rotation(0), Vec3d::UnitX()));
    transform.scale(scale.cwiseProduct(mirror));
}

// [INTENT] Value-return overload of assemble_transform.
Transform3d assemble_transform(const Vec3d& translation, const Vec3d& rotation, const Vec3d& scale, const Vec3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

// [INTENT] Matrix-component overload: composes transform from four pre-built Transform3d
// matrices (translation * rotation * scale * mirror).
void assemble_transform(Transform3d&       transform,
                        const Transform3d& translation,
                        const Transform3d& rotation,
                        const Transform3d& scale,
                        const Transform3d& mirror)
{
    transform = translation * rotation * scale * mirror;
}

Transform3d assemble_transform(const Transform3d& translation,
                               const Transform3d& rotation,
                               const Transform3d& scale,
                               const Transform3d& mirror)
{
    Transform3d transform;
    assemble_transform(transform, translation, rotation, scale, mirror);
    return transform;
}

// [INTENT] translation_transform — builds a pure-translation Transform3d.
void translation_transform(Transform3d& transform, const Vec3d& translation)
{
    transform = Transform3d::Identity();
    transform.translate(translation);
}

Transform3d translation_transform(const Vec3d& translation)
{
    Transform3d transform;
    translation_transform(transform, translation);
    return transform;
}

// [INTENT] rotation_transform — builds a pure-rotation Transform3d from ZYX Euler angles.
// Rotation order: AngleAxisd(Z) * AngleAxisd(Y) * AngleAxisd(X).
// [COUPLING] Must use the same ZYX convention as assemble_transform and extract_euler_angles.
void rotation_transform(Transform3d& transform, const Vec3d& rotation)
{
    transform = Transform3d::Identity();
    transform.rotate(Eigen::AngleAxisd(rotation.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rotation.y(), Vec3d::UnitY()) *
                     Eigen::AngleAxisd(rotation.x(), Vec3d::UnitX()));
}

Transform3d rotation_transform(const Vec3d& rotation)
{
    Transform3d transform;
    rotation_transform(transform, rotation);
    return transform;
}

// [INTENT] scale_transform — builds a pure-scale Transform3d (uniform or per-axis).
void scale_transform(Transform3d& transform, double scale) { return scale_transform(transform, scale * Vec3d::Ones()); }

void scale_transform(Transform3d& transform, const Vec3d& scale)
{
    transform = Transform3d::Identity();
    transform.scale(scale);
}

Transform3d scale_transform(double scale) { return scale_transform(scale * Vec3d::Ones()); }

Transform3d scale_transform(const Vec3d& scale)
{
    Transform3d transform;
    scale_transform(transform, scale);
    return transform;
}

// [INTENT] extract_euler_angles(Matrix3d) — extracts ZYX Euler angles from a 3×3 rotation
// matrix using Eigen's eulerAngles(2,1,0), then swaps indices 0 and 2 to produce an
// XYZ-indexed Vec3d where angles[0]=X, angles[1]=Y, angles[2]=Z.
// [HAZARD H529] Eigen's eulerAngles() has documented range ambiguities and gimbal-lock
//   singularities near Y ≈ ±90°. At the singularity, X and Z are not uniquely determined;
//   Eigen returns one valid solution but it may not match the "expected" angle decomposition.
//   Any code that assumes round-trip consistency (set_rotation → get_rotation → set_rotation)
//   may accumulate drift near the singularity. See H511 in Model.cpp for a concrete instance.
Vec3d extract_euler_angles(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix)
{
    // The extracted "rotation" is a triplet of numbers such that Geometry::rotation_transform
    // returns the original transform. Because of the chosen order of rotations, the triplet
    // is not equivalent to Euler angles in the usual sense.
    Vec3d angles = rotation_matrix.eulerAngles(2, 1, 0);
    std::swap(angles(0), angles(2));
    return angles;
}

// [INTENT] extract_euler_angles(Transform3d) — strips scale from the matrix by normalising
// each column, then delegates to the 3×3 overload.
// [STATE] Does not modify the input transform; creates a local 3×3 copy.
Vec3d extract_euler_angles(const Transform3d& transform)
{
    // use only the non-translational part of the transform
    Eigen::Matrix<double, 3, 3, Eigen::DontAlign> m = transform.matrix().block(0, 0, 3, 3);
    // remove scale
    m.col(0).normalize();
    m.col(1).normalize();
    m.col(2).normalize();
    return extract_euler_angles(m);
}

// [INTENT] rotation_from_two_vectors() — computes the axis-angle rotation that maps `from` to
// `to` using Eigen::Quaterniond::setFromTwoVectors(). Optionally returns the full 3×3 matrix.
// [STATE] Stateless — pure function.
void rotation_from_two_vectors(Vec3d from, Vec3d to, Vec3d& rotation_axis, double& phi, Matrix3d* rotation_matrix)
{
    const Matrix3d          m = Eigen::Quaterniond().setFromTwoVectors(from, to).toRotationMatrix();
    const Eigen::AngleAxisd aa(m);
    rotation_axis = aa.axis();
    phi           = aa.angle();
    if (rotation_matrix)
        *rotation_matrix = m;
}

// [INTENT] get_offset_matrix() — returns the translation component of this Transformation
// as a pure-translation Transform3d.
Transform3d Transformation::get_offset_matrix() const { return translation_transform(get_offset()); }

// [INTENT] extract_rotation_matrix() — decomposes trafo into rotation and scale via Eigen's
// computeRotationScaling (polar decomposition). Returns only the rotation part.
// [MEMORY] computeRotationScaling allocates internally (SVD computation).
static Transform3d extract_rotation_matrix(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(rotation);
}

// [INTENT] extract_scale() — polar decomposition; returns only the scale/shear matrix.
static Transform3d extract_scale(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return Transform3d(scale);
}

// [INTENT] extract_rotation_scale() — polar decomposition returning both rotation and scale
// as a pair, avoiding double decomposition in callers that need both.
static std::pair<Transform3d, Transform3d> extract_rotation_scale(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);
    return {Transform3d(rotation), Transform3d(scale)};
}

// [INTENT] contains_skew() — checks whether a transform includes skew (non-orthogonal scaling)
// by testing whether the scale matrix from polar decomposition is diagonal.
// Special case: if determinant < 0 (mirror), the diagonal check on the raw scale matrix would
// fail; instead checks off-diagonal ratios of scale vs original matrix.
// [COUPLING] Used by Transformation::has_skew() and TransformationSVD (skew detection).
static bool contains_skew(const Transform3d& trafo)
{
    Matrix3d rotation;
    Matrix3d scale;
    trafo.computeRotationScaling(&rotation, &scale);

    if (scale.isDiagonal())
        return false;

    if (scale.determinant() >= 0.0)
        return true;

    // the matrix contains mirror
    const Matrix3d ratio = scale.cwiseQuotient(trafo.matrix().block<3, 3>(0, 0));

    auto check_skew = [&ratio](int i, int j, bool& skew) {
        if (!std::isnan(ratio(i, j)) && !std::isnan(ratio(j, i)))
            skew |= std::abs(ratio(i, j) * ratio(j, i) - 1.0) > EPSILON;
    };

    bool has_skew = false;
    check_skew(0, 1, has_skew);
    check_skew(0, 2, has_skew);
    check_skew(1, 2, has_skew);
    return has_skew;
}

// [INTENT] get_rotation() — extracts ZYX Euler angles from the current matrix (via polar
// decomposition then eulerAngles). See H529 for gimbal-lock hazard.
Vec3d Transformation::get_rotation() const { return extract_euler_angles(extract_rotation_matrix(m_matrix)); }

// [INTENT] get_rotation_matrix() — returns the rotation-only 4×4 transform (via polar decomp).
Transform3d Transformation::get_rotation_matrix() const { return extract_rotation_matrix(m_matrix); }

// [INTENT] get_rotation_by_quaternion() — alternative Euler extraction via Quaterniond.
// Used by BBS assembly view to avoid the eulerAngles ambiguity in some cases.
// [HAZARD H529 cross-ref] Also calls eulerAngles(2,1,0) internally — same gimbal-lock
//   risk as get_rotation(), just via a different Eigen code path.
Vec3d Transformation::get_rotation_by_quaternion() const
{
    Matrix3d           rotation_matrix = m_matrix.matrix().block(0, 0, 3, 3);
    Eigen::Quaterniond quaternion(rotation_matrix);
    quaternion.normalize();
    Vec3d temp_rotation = quaternion.matrix().eulerAngles(2, 1, 0);
    std::swap(temp_rotation(0), temp_rotation(2));
    return temp_rotation;
}

// [INTENT] set_rotation(Vec3d) — replaces the rotation component of m_matrix while preserving
// scale and offset. Rebuilds: rotation_transform(rotation) * extract_scale(m_matrix), then
// restores the translation column.
// [STATE] Scale and offset are preserved; existing rotation is fully replaced.
void Transformation::set_rotation(const Vec3d& rotation)
{
    const Vec3d offset     = get_offset();
    m_matrix               = rotation_transform(rotation) * extract_scale(m_matrix);
    m_matrix.translation() = offset;
}

// [INTENT] set_rotation(Axis, double) — sets a single-axis rotation component while keeping
// others. Clamps the angle to [0, 2π) using angle_to_0_2PI; treats exactly 2π as 0.
// Rebuilds the full rotation from current Euler angles with the single axis replaced.
void Transformation::set_rotation(Axis axis, double rotation)
{
    rotation = angle_to_0_2PI(rotation);
    if (is_approx(std::abs(rotation), 2.0 * double(PI)))
        rotation = 0.0;

    auto [curr_rotation, scale] = extract_rotation_scale(m_matrix);
    Vec3d angles                = extract_euler_angles(curr_rotation);
    angles[axis]                = rotation;

    const Vec3d offset     = get_offset();
    m_matrix               = rotation_transform(angles) * scale;
    m_matrix.translation() = offset;
}

// [INTENT] get_scaling_factor() — extracts per-axis absolute scale values from the polar
// decomposition. Returns absolute values (always positive) — sign information (mirror) is
// stripped. Use get_mirror() to recover sign.
Vec3d Transformation::get_scaling_factor() const
{
    const Transform3d scale = extract_scale(m_matrix);
    return {std::abs(scale(0, 0)), std::abs(scale(1, 1)), std::abs(scale(2, 2))};
}

// [INTENT] get_scaling_factor_matrix() — same as get_scaling_factor but returns as a 4×4
// diagonal Transform3d (with off-diagonals zeroed).
Transform3d Transformation::get_scaling_factor_matrix() const
{
    Transform3d scale = extract_scale(m_matrix);
    scale(0, 0)       = std::abs(scale(0, 0));
    scale(1, 1)       = std::abs(scale(1, 1));
    scale(2, 2)       = std::abs(scale(2, 2));
    return scale;
}

// [INTENT] set_scaling_factor(Vec3d) — replaces scale while preserving rotation and offset.
// [HAZARD H536] Asserts all components > 0. If any component is zero (degenerate object),
//   the matrix becomes non-invertible. Downstream code that calls get_matrix().inverse()
//   (e.g. mat_around_a_point_rotate, H532) will produce NaN. The assertion is Debug-only.
void Transformation::set_scaling_factor(const Vec3d& scaling_factor)
{
    assert(scaling_factor.x() > 0.0 && scaling_factor.y() > 0.0 && scaling_factor.z() > 0.0);

    const Vec3d offset     = get_offset();
    m_matrix               = extract_rotation_matrix(m_matrix) * scale_transform(scaling_factor);
    m_matrix.translation() = offset;
}

// [INTENT] set_scaling_factor(Axis, double) — sets a single scale component without touching
// the others or the rotation. Rebuilds from extracted rotation and scale matrices.
void Transformation::set_scaling_factor(Axis axis, double scaling_factor)
{
    assert(scaling_factor > 0.0);

    auto [rotation, scale] = extract_rotation_scale(m_matrix);
    scale(axis, axis)      = scaling_factor;

    const Vec3d offset     = get_offset();
    m_matrix               = rotation * scale;
    m_matrix.translation() = offset;
}

// [INTENT] get_mirror() — extracts the sign of each scale diagonal component as ±1.
// Division by absolute value — if scale(i,i) == 0 this produces NaN (not guarded here;
// see H536 for the related assertion on set_scaling_factor).
Vec3d Transformation::get_mirror() const
{
    const Transform3d scale = extract_scale(m_matrix);
    return {scale(0, 0) / std::abs(scale(0, 0)), scale(1, 1) / std::abs(scale(1, 1)), scale(2, 2) / std::abs(scale(2, 2))};
}

// [INTENT] get_mirror_matrix() — returns a 4×4 diagonal mirror matrix (±1 per axis).
Transform3d Transformation::get_mirror_matrix() const
{
    Transform3d scale = extract_scale(m_matrix);
    scale(0, 0)       = scale(0, 0) / std::abs(scale(0, 0));
    scale(1, 1)       = scale(1, 1) / std::abs(scale(1, 1));
    scale(2, 2)       = scale(2, 2) / std::abs(scale(2, 2));
    return scale;
}

// [INTENT] set_mirror(Vec3d) — updates the sign of each scale axis according to the mirror
// vector, preserving rotation and offset.
// [HAZARD H533] Any mirror component with |value| == 0 is silently set to 1.0.
//   Any mirror component with |value| != 1 is silently normalised to ±1.
//   This lossy normalisation is not documented in the API header. Callers passing
//   fractional mirror values (e.g. from a serialised transform with drift) get silently
//   corrected behaviour without any warning or error.
void Transformation::set_mirror(const Vec3d& mirror)
{
    Vec3d       copy(mirror);
    const Vec3d abs_mirror = copy.cwiseAbs();
    for (int i = 0; i < 3; ++i) {
        if (abs_mirror(i) == 0.0)
            copy(i) = 1.0;
        else if (abs_mirror(i) != 1.0)
            copy(i) /= abs_mirror(i);
    }

    auto [rotation, scale]  = extract_rotation_scale(m_matrix);
    const Vec3d curr_scales = {scale(0, 0), scale(1, 1), scale(2, 2)};
    const Vec3d signs       = curr_scales.cwiseProduct(copy);

    if (signs[0] < 0.0)
        scale(0, 0) = -scale(0, 0);
    if (signs[1] < 0.0)
        scale(1, 1) = -scale(1, 1);
    if (signs[2] < 0.0)
        scale(2, 2) = -scale(2, 2);

    const Vec3d offset     = get_offset();
    m_matrix               = rotation * scale;
    m_matrix.translation() = offset;
}

// [INTENT] set_mirror(Axis, double) — single-axis mirror setter; applies the same
// normalisation as set_mirror(Vec3d) but for one axis only.
// [HAZARD H533 cross-ref] Same silent normalisation as the Vec3d overload.
void Transformation::set_mirror(Axis axis, double mirror)
{
    double abs_mirror = std::abs(mirror);
    if (abs_mirror == 0.0)
        mirror = 1.0;
    else if (abs_mirror != 1.0)
        mirror /= abs_mirror;

    auto [rotation, scale]  = extract_rotation_scale(m_matrix);
    const double curr_scale = scale(axis, axis);
    const double sign       = curr_scale * mirror;

    if (sign < 0.0)
        scale(axis, axis) = -scale(axis, axis);

    const Vec3d offset     = get_offset();
    m_matrix               = rotation * scale;
    m_matrix.translation() = offset;
}

// [INTENT] has_skew() — delegates to contains_skew() to check for non-orthogonal scaling.
bool Transformation::has_skew() const { return contains_skew(m_matrix); }

// [INTENT] reset() — resets the transformation matrix to identity.
void Transformation::reset() { m_matrix = Transform3d::Identity(); }

// [INTENT] reset_rotation() — removes rotation while preserving scale and offset.
// Uses SVD: reconstructs as offset * V * S * V^T * mirror_matrix.
// [HAZARD H534] Constructs a TransformationSVD (Jacobi SVD computation) on every call.
//   Called from UI slider events. Each SVD is fast for 3×3 but may accumulate on
//   high-frequency input events.
void Transformation::reset_rotation()
{
    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.v * svd.s * svd.v.transpose()) * svd.mirror_matrix();
}

// [INTENT] reset_scaling_factor() — removes scale while preserving rotation and offset.
// Uses SVD: reconstructs as offset * U * V^T * mirror_matrix.
// [HAZARD H534 cross-ref] Same SVD overhead as reset_rotation().
void Transformation::reset_scaling_factor()
{
    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * Transform3d(svd.v.transpose()) * svd.mirror_matrix();
}

// [INTENT] reset_skew() — replaces anisotropic scale with uniform scale (geometric mean of
// the three singular values), preserving rotation and offset direction.
// The new uniform scale = (S_0 * S_1 * S_2)^(1/3).
void Transformation::reset_skew()
{
    auto new_scale_factor = [](const Matrix3d& s) {
        return pow(s(0, 0) * s(1, 1) * s(2, 2), 1. / 3.); // scale average
    };

    const Geometry::TransformationSVD svd(*this);
    m_matrix = get_offset_matrix() * Transform3d(svd.u) * scale_transform(new_scale_factor(svd.s)) * Transform3d(svd.v.transpose()) *
               svd.mirror_matrix();
}

// [INTENT] get_matrix_no_offset() — returns the matrix with translation zeroed out.
// Creates a copy, calls reset_offset(), returns the resulting matrix.
Transform3d Transformation::get_matrix_no_offset() const
{
    Transformation copy(*this);
    copy.reset_offset();
    return copy.get_matrix();
}

// [INTENT] get_matrix_no_scaling_factor() — returns the matrix with scale reset to 1.
// Creates a copy, calls reset_scaling_factor(), returns the resulting matrix.
// [HAZARD H534 cross-ref] reset_scaling_factor() calls SVD; see H534.
Transform3d Transformation::get_matrix_no_scaling_factor() const
{
    Transformation copy(*this);
    copy.reset_scaling_factor();
    return copy.get_matrix();
}

// [INTENT] get_matrix_with_applied_shrinkage_compensation() — returns a transform with
// per-axis shrinkage compensation applied (Orca filament shrink feature).
// Applies the compensation scale BEFORE the transformation's rotation/scale, then restores
// the XY offset. Z offset is not compensated (bed adhesion preserves Z position).
// [COUPLING] Orca-specific extension not present in upstream PrusaSlicer.
// Orca: Implement prusa's filament shrink compensation approach
Transform3d Transformation::get_matrix_with_applied_shrinkage_compensation(const Vec3d& shrinkage_compensation) const
{
    const Transform3d shrinkage_trafo = Geometry::scale_transform(shrinkage_compensation);
    const Vec3d       trafo_offset    = this->get_offset();
    const Vec3d       trafo_offset_xy = Vec3d(trafo_offset.x(), trafo_offset.y(), 0.);

    Transformation copy(*this);
    copy.set_offset(Axis::X, 0.);
    copy.set_offset(Axis::Y, 0.);

    Transform3d trafo_after_shrinkage = (shrinkage_trafo * copy.get_matrix());
    trafo_after_shrinkage.translation() += trafo_offset_xy;

    return trafo_after_shrinkage;
}

// [INTENT] operator*() — composes two Transformations by multiplying their matrices.
// Returns a new Transformation wrapping the product matrix.
Transformation Transformation::operator*(const Transformation& other) const { return Transformation(get_matrix() * other.get_matrix()); }

// [INTENT] volume_to_bed_transformation() — computes the inverse transformation to apply to
// a ModelVolume so that it occupies a specific bounding box in bed space when combined with
// the given instance transformation.
// Three cases:
//   1. Uniform scale: just invert the instance transform (no-offset).
//   2. Anisotropic scale + 90° rotation: compute per-axis scale via least-squares fit of bbox corners.
//   3. General anisotropic: use cwiseInverse of instance scale (approximate).
// [HAZARD H531] In case 2, the least-squares fit computes:
//   scale(i) = pts.col(i).dot(qs.col(i)) / pts.col(i).dot(pts.col(i))
//   If the bounding box is degenerate on axis i (min == max → col(i) = all-zeros), the
//   denominator is zero → scale(i) = NaN. No check is performed; NaN is stored silently.
// [COUPLING] Calls is_rotation_ninety_degrees() (Geometry.hpp helper) which uses is_approx()
//   with EPSILON — tight tolerance, may misclassify near-90° rotations.
Transformation Transformation::volume_to_bed_transformation(const Transformation& instance_transformation, const BoundingBoxf3& bbox)
{
    Transformation out;

    if (instance_transformation.is_scaling_uniform()) {
        // No need to run the non-linear least squares fitting for uniform scaling.
        // Just set the inverse.
        out.set_matrix(instance_transformation.get_matrix_no_offset().inverse());
    } else if (is_rotation_ninety_degrees(instance_transformation.get_rotation())) {
        // Anisotropic scaling, rotation by multiples of ninety degrees.
        Eigen::Matrix3d instance_rotation_trafo = (Eigen::AngleAxisd(instance_transformation.get_rotation().z(), Vec3d::UnitZ()) *
                                                   Eigen::AngleAxisd(instance_transformation.get_rotation().y(), Vec3d::UnitY()) *
                                                   Eigen::AngleAxisd(instance_transformation.get_rotation().x(), Vec3d::UnitX()))
                                                      .toRotationMatrix();
        Eigen::Matrix3d volume_rotation_trafo = (Eigen::AngleAxisd(-instance_transformation.get_rotation().x(), Vec3d::UnitX()) *
                                                 Eigen::AngleAxisd(-instance_transformation.get_rotation().y(), Vec3d::UnitY()) *
                                                 Eigen::AngleAxisd(-instance_transformation.get_rotation().z(), Vec3d::UnitZ()))
                                                    .toRotationMatrix();

        // 8 corners of the bounding box.
        auto pts  = Eigen::MatrixXd(8, 3);
        pts(0, 0) = bbox.min.x();
        pts(0, 1) = bbox.min.y();
        pts(0, 2) = bbox.min.z();
        pts(1, 0) = bbox.min.x();
        pts(1, 1) = bbox.min.y();
        pts(1, 2) = bbox.max.z();
        pts(2, 0) = bbox.min.x();
        pts(2, 1) = bbox.max.y();
        pts(2, 2) = bbox.min.z();
        pts(3, 0) = bbox.min.x();
        pts(3, 1) = bbox.max.y();
        pts(3, 2) = bbox.max.z();
        pts(4, 0) = bbox.max.x();
        pts(4, 1) = bbox.min.y();
        pts(4, 2) = bbox.min.z();
        pts(5, 0) = bbox.max.x();
        pts(5, 1) = bbox.min.y();
        pts(5, 2) = bbox.max.z();
        pts(6, 0) = bbox.max.x();
        pts(6, 1) = bbox.max.y();
        pts(6, 2) = bbox.min.z();
        pts(7, 0) = bbox.max.x();
        pts(7, 1) = bbox.max.y();
        pts(7, 2) = bbox.max.z();

        // Corners of the bounding box transformed into the modifier mesh coordinate space, with inverse rotation applied to the modifier.
        auto qs = (pts * (instance_rotation_trafo *
                          Eigen::Scaling(instance_transformation.get_scaling_factor().cwiseProduct(instance_transformation.get_mirror())) *
                          volume_rotation_trafo)
                             .inverse()
                             .transpose())
                      .eval();
        // Fill in scaling based on least squares fitting of the bounding box corners.
        Vec3d scale;
        for (int i = 0; i < 3; ++i)
            scale(i) = pts.col(i).dot(qs.col(i)) / pts.col(i).dot(pts.col(i));

        out.set_rotation(Geometry::extract_euler_angles(volume_rotation_trafo));
        out.set_scaling_factor(Vec3d(std::abs(scale.x()), std::abs(scale.y()), std::abs(scale.z())));
        out.set_mirror(Vec3d(scale.x() > 0 ? 1. : -1, scale.y() > 0 ? 1. : -1, scale.z() > 0 ? 1. : -1));
    } else {
        // General anisotropic scaling, general rotation.
        // Keep the modifier mesh in the instance coordinate system, so the modifier mesh will not be aligned with the world.
        // Scale it to get the required size.
        out.set_scaling_factor(instance_transformation.get_scaling_factor().cwiseInverse());
    }

    return out;
}

// [INTENT] transform3d_from_string() — parses a space-separated 16-element string into a
// column-major 4×4 Transform3d. Used by 3MF and AMF loaders for the <transform> element.
// [HAZARD H530] Uses `::atof()` which is locale-dependent. If the current locale uses ','
//   as the decimal separator (e.g. de_DE), parsing will produce zeros for all fractional
//   parts. The assert fires in Debug only. Release builds silently load corrupt transforms
//   on non-'.' decimal locales.
// [STATE] Returns Identity if input string is empty or doesn't have exactly 16 tokens.
//   Silent fallback — no error is signalled to the caller.
// For parsing a transformation matrix from 3MF / AMF.
Transform3d transform3d_from_string(const std::string& transform_str)
{
    assert(is_decimal_separator_point()); // for atof
    Transform3d transform = Transform3d::Identity();

    if (!transform_str.empty()) {
        std::vector<std::string> mat_elements_str;
        boost::split(mat_elements_str, transform_str, boost::is_any_of(" "), boost::token_compress_on);

        const unsigned int size = (unsigned int) mat_elements_str.size();
        if (size == 16) {
            unsigned int i = 0;
            for (unsigned int r = 0; r < 4; ++r) {
                for (unsigned int c = 0; c < 4; ++c) {
                    transform(r, c) = ::atof(mat_elements_str[i++].c_str());
                }
            }
        }
    }

    return transform;
}

// [INTENT] rotation_xyz_diff() — computes the quaternion that rotates from rot_xyz_from to
// rot_xyz_to (both in ZYX Euler angle representation). Used by rotation_diff_z().
// [STATE] Stateless — pure function.
Eigen::Quaterniond rotation_xyz_diff(const Vec3d& rot_xyz_from, const Vec3d& rot_xyz_to)
{
    return
        // From the current coordinate system to world.
        Eigen::AngleAxisd(rot_xyz_to.z(), Vec3d::UnitZ()) * Eigen::AngleAxisd(rot_xyz_to.y(), Vec3d::UnitY()) *
        Eigen::AngleAxisd(rot_xyz_to.x(), Vec3d::UnitX()) *
        // From world to the initial coordinate system.
        Eigen::AngleAxisd(-rot_xyz_from.x(), Vec3d::UnitX()) * Eigen::AngleAxisd(-rot_xyz_from.y(), Vec3d::UnitY()) *
        Eigen::AngleAxisd(-rot_xyz_from.z(), Vec3d::UnitZ());
}

// [INTENT] rotation_diff_z() — extracts the Z-axis rotation component of the difference
// between two ZYX Euler rotations. Called ONLY when the two rotations are known to differ
// only in Z. Asserts in Debug that X and Y components of the axis are near-zero.
// [CONCURRENCY] Stateless — safe to call from any thread.
// This should only be called if it is known, that the two rotations only differ in rotation around the Z axis.
double rotation_diff_z(const Vec3d& rot_xyz_from, const Vec3d& rot_xyz_to)
{
    const Eigen::AngleAxisd angle_axis(rotation_xyz_diff(rot_xyz_from, rot_xyz_to));
    const Vec3d             axis  = angle_axis.axis();
    const double            angle = angle_axis.angle();
#ifndef NDEBUG
    if (std::abs(angle) > 1e-8) {
        assert(std::abs(axis.x()) < 1e-8);
        assert(std::abs(axis.y()) < 1e-8);
    }
#endif /* NDEBUG */
    return (axis.z() < 0) ? -angle : angle;
}

// [INTENT] TransformationSVD constructor — performs Jacobi SVD decomposition of the linear
// (rotation+scale) part of a Transform3d. Computes:
//   - u, s, v: SVD factors (m = u * s * v^T)
//   - mirror: true if determinant < 0 (left-handed)
//   - scale, anisotropic_scale, rotation, rotation_90_degrees, skew: diagnostic flags
// Used by reset_rotation(), reset_scaling_factor(), reset_skew(), and the UI gizmo.
// [STATE] All output fields are const-initialised in the constructor; the struct is immutable.
// [HAZARD H534 cross-ref] Jacobi SVD is O(N³) — fast for 3×3 but called per UI event.
// [HAZARD H537] When `mirror` is true, the m0 matrix is pre-multiplied by diag(-1,1,1)
//   before SVD to keep the SVD result positive-definite. The mirror information is stored
//   in the `mirror` flag and reconstructed via mirror_matrix(). If a refactoring does not
//   replicate this pre-multiplication exactly, mirror detection breaks silently.
TransformationSVD::TransformationSVD(const Transform3d& trafo)
{
    const Matrix3d m0 = trafo.matrix().block<3, 3>(0, 0);
    mirror            = m0.determinant() < 0.0;

    Matrix3d m;
    if (mirror)
        m = m0 * Eigen::DiagonalMatrix<double, 3, 3>(-1.0, 1.0, 1.0);
    else
        m = m0;
    const Eigen::JacobiSVD<Matrix3d> svd(m, Eigen::ComputeFullU | Eigen::ComputeFullV);
    u = svd.matrixU();
    v = svd.matrixV();
    s = svd.singularValues().asDiagonal();

    scale             = !s.isApprox(Matrix3d::Identity());
    anisotropic_scale = !is_approx(s(0, 0), s(1, 1)) || !is_approx(s(1, 1), s(2, 2));
    rotation          = !v.isApprox(u);

    if (anisotropic_scale) {
        rotation_90_degrees = true;
        for (int i = 0; i < 3; ++i) {
            const Vec3d  row       = v.row(i).cwiseAbs();
            const size_t num_zeros = is_approx(row[0], 0.) + is_approx(row[1], 0.) + is_approx(row[2], 0.);
            const size_t num_ones  = is_approx(row[0], 1.) + is_approx(row[1], 1.) + is_approx(row[2], 1.);
            if (num_zeros != 2 || num_ones != 1) {
                rotation_90_degrees = false;
                break;
            }
        }
        // Detect skew by brute force: check if the axes are still orthogonal after transformation
        const Matrix3d             trafo_linear = trafo.linear();
        const std::array<Vec3d, 3> axes         = {Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ()};
        std::array<Vec3d, 3>       transformed_axes;
        for (int i = 0; i < 3; ++i) {
            transformed_axes[i] = trafo_linear * axes[i];
        }
        skew = std::abs(transformed_axes[0].dot(transformed_axes[1])) > EPSILON ||
               std::abs(transformed_axes[1].dot(transformed_axes[2])) > EPSILON ||
               std::abs(transformed_axes[2].dot(transformed_axes[0])) > EPSILON;

        // This following old code does not work under all conditions. The v matrix can become non diagonal (see SPE-1492)
        //        skew = ! rotation_90_degrees;
    } else
        skew = false;
}

// [INTENT] mat_around_a_point_rotate() — rotates a Transformation around a fixed world-space
// point `pt` by `rotate_theta_radian` around `axis`.
// Algorithm:
//   1. Translate to origin (subtract InMat's offset).
//   2. Apply rotation quaternion.
//   3. Compute new world position by: temp_world^-1 * xyz → rotate → temp_world * result.
//   4. Set the new offset.
// [HAZARD H532] `temp_world.get_matrix().inverse()` is called without checking invertibility.
//   If `temp_world` has a zero-scale component (degenerate), the inverse produces NaN,
//   corrupting `temp_xyz` and `new_pos` silently.
// [COUPLING] BBS assembly-view specific function; not present in upstream PrusaSlicer.
Transformation mat_around_a_point_rotate(const Transformation& InMat, const Vec3d& pt, const Vec3d& axis, float rotate_theta_radian)
{
    auto           xyz = InMat.get_offset();
    Transformation left;
    left.set_offset(-xyz); // at world origin
    auto curMat = left * InMat;

    auto qua = Eigen::Quaterniond(Eigen::AngleAxisd(rotate_theta_radian, axis));
    qua.normalize();
    Transform3d    cur_matrix;
    Transformation rotateMat4;
    rotateMat4.set_matrix(cur_matrix.fromPositionOrientationScale(Vec3d(0., 0., 0.), qua, Vec3d(1., 1., 1.)));

    curMat = rotateMat4 * curMat; // along_fix_axis
    // rotate mat4 along fix pt
    Transformation temp_world;
    auto           qua_world = Eigen::Quaterniond(Eigen::AngleAxisd(0, axis));
    qua_world.normalize();
    Transform3d cur_matrix_world;
    temp_world.set_matrix(cur_matrix_world.fromPositionOrientationScale(pt, qua_world, Vec3d(1., 1., 1.)));
    Vec3d temp_xyz = temp_world.get_matrix().inverse() * xyz;
    Vec3d new_pos  = temp_world.get_matrix() * (rotateMat4.get_matrix() * temp_xyz);
    curMat.set_offset(new_pos);

    return curMat;
}

// [INTENT] generate_transform() — constructs a Transformation from explicit X, Y, Z direction
// vectors and an origin. Columns of the 3×3 block are set to the normalised direction vectors.
// [HAZARD H538] Direction vectors are normalised inside the function (x_dir.normalized() etc.)
//   but if any input direction is a zero vector, `normalized()` returns a zero vector and the
//   resulting 3×3 matrix is singular (non-orthonormal). No assertion or guard is present.
Transformation generate_transform(const Vec3d& x_dir, const Vec3d& y_dir, const Vec3d& z_dir, const Vec3d& origin)
{
    Matrix3d m;
    m.col(0) = x_dir.normalized();
    m.col(1) = y_dir.normalized();
    m.col(2) = z_dir.normalized();
    Transform3d    mm(m);
    Transformation tran(mm);
    tran.set_offset(origin);
    return tran;
}

// [INTENT] is_point_inside_polygon_corner() — tests whether a query point Q lies inside
// the angular corner formed by vectors BA and BC (CCW polygon convention).
// Uses normalized dot-product projections onto the normal and direction of BQ to classify
// Q relative to the bisector and edges of the corner.
// [STATE] Stateless — pure function.
// [MEMORY] All computation on stack — no heap allocation.
// [COUPLING] Called by Arachne variable-width perimeter computation for corner-type
//   classification during toolpath generation.
bool is_point_inside_polygon_corner(const Point& a, const Point& b, const Point& c, const Point& query_point)
{
    // Cast all input points into int64_t to prevent overflows when points are close to max values of coord_t.
    const Vec2i64 a_i64           = a.cast<int64_t>();
    const Vec2i64 b_i64           = b.cast<int64_t>();
    const Vec2i64 c_i64           = c.cast<int64_t>();
    const Vec2i64 query_point_i64 = query_point.cast<int64_t>();

    // Shift all points to have a base in vertex B.
    // Then construct normalized vectors to ensure that we will work with vectors with endpoints on the unit circle.
    const Vec2d ba = (a_i64 - b_i64).cast<double>().normalized();
    const Vec2d bc = (c_i64 - b_i64).cast<double>().normalized();
    const Vec2d bq = (query_point_i64 - b_i64).cast<double>().normalized();

    // Points A and C has to be different.
    assert(ba != bc);

    // Construct a normal for the vector BQ that points to the left side of the vector BQ.
    const Vec2d bq_left_normal = perp(bq);

    const double proj_a_on_bq_normal = ba.dot(bq_left_normal); // Project point A on the normal of BQ.
    const double proj_c_on_bq_normal = bc.dot(bq_left_normal); // Project point C on the normal of BQ.
    if ((proj_a_on_bq_normal > 0. && proj_c_on_bq_normal <= 0.) || (proj_a_on_bq_normal <= 0. && proj_c_on_bq_normal > 0.)) {
        // Q is between points A and C or lies on one of those vectors (BA or BC).

        // Based on the CCW order of polygons (contours) and order of corner ABC,
        // when this condition is met, the query point is inside the corner.
        return proj_a_on_bq_normal > 0.;
    } else {
        // Q isn't between points A and C, but still it can be inside the corner.

        const double proj_a_on_bq = ba.dot(bq); // Project point A on BQ.
        const double proj_c_on_bq = bc.dot(bq); // Project point C on BQ.

        // The value of proj_a_on_bq_normal is the same when we project the vector BA on the normal of BQ.
        // So we can say that the Q is on the right side of the vector BA when proj_a_on_bq_normal > 0, and
        // that the Q is on the left side of the vector BA proj_a_on_bq_normal < 0.
        // Also, the Q is on the right side of the bisector of oriented angle ABC when proj_c_on_bq < proj_a_on_bq, and
        // the Q is on the left side of the bisector of oriented angle ABC when proj_c_on_bq > proj_a_on_bq.

        // So the Q is inside the corner when one of the following conditions is met:
        //  *  The Q is on the right side of the vector BA, and the Q is on the right side of the bisector of the oriented angle ABC.
        //  *  The Q is on the left side of the vector BA, and the Q is on the left side of the bisector of the oriented angle ABC.
        return (proj_a_on_bq_normal > 0. && proj_c_on_bq < proj_a_on_bq) || (proj_a_on_bq_normal <= 0. && proj_c_on_bq >= proj_a_on_bq);
    }
}

}} // namespace Slic3r::Geometry
