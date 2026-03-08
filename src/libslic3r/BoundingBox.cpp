// [INTENT] Implementation of BoundingBox template methods and concrete class methods.
//          Contains explicit template instantiations to avoid link-time "undefined reference"
//          errors for the extern template declarations in BoundingBox.hpp.
// [COUPLING] Depends on Polygon.hpp (polygon() methods) and Eigen/Dense for BoundingBoxf3::transformed().
#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include <algorithm>
#include <assert.h>

#include <Eigen/Dense>

namespace Slic3r {

// [INTENT] Explicit template instantiations for the constructor specializations.
//          These force the compiler to emit the constructor bodies for BoundingBoxBase<Point,Points>
//          and BoundingBoxBase<Vec2d> in this translation unit, satisfying the extern declarations.
template BoundingBoxBase<Point, Points>::BoundingBoxBase(const Points& points);
template BoundingBoxBase<Vec2d>::BoundingBoxBase(const std::vector<Vec2d>& points);

template BoundingBox3Base<Vec3d>::BoundingBox3Base(const std::vector<Vec3d>& points);

// [INTENT] polygon(Polygon*): fills the given polygon with the 4 corners of the AABB in CCW order:
//          min → (max.x, min.y) → max → (min.x, max.y).
// [HAZARD] Does NOT check `defined`. If called on an undefined bbox (min==max==0), it will produce
//          a degenerate zero-area polygon without error.
// [COUPLING] Output polygon must be pre-constructed; the method overwrites its `points` field.
void BoundingBox::polygon(Polygon* polygon) const
{
    polygon->points = {this->min, {this->max.x(), this->min.y()}, this->max, {this->min.x(), this->max.y()}};
}

Polygon BoundingBox::polygon() const
{
    Polygon p;
    this->polygon(&p);
    return p;
}

// [INTENT] rotated(angle): returns the AABB of this bounding box after rotating all 4 corners.
//          Since the input is always a rectangle, merging the 4 rotated corners gives the exact
//          new AABB. No scaling or translation is involved — rotation around origin.
// [HAZARD H608] Each corner rotation uses Point::rotated(angle) which internally calls rotate(cos,sin),
//               accumulating rounding error. For cascaded rotations this can drift, but for a single
//               rotation call per corner it is acceptable.
BoundingBox BoundingBox::rotated(double angle) const
{
    BoundingBox out;
    out.merge(this->min.rotated(angle));
    out.merge(this->max.rotated(angle));
    out.merge(Point(this->min.x(), this->max.y()).rotated(angle));
    out.merge(Point(this->max.x(), this->min.y()).rotated(angle));
    return out;
}

// [INTENT] rotated(angle, center): same as above but rotates around an explicit center point.
BoundingBox BoundingBox::rotated(double angle, const Point& center) const
{
    BoundingBox out;
    out.merge(this->min.rotated(angle, center));
    out.merge(this->max.rotated(angle, center));
    out.merge(Point(this->min.x(), this->max.y()).rotated(angle, center));
    out.merge(Point(this->max.x(), this->min.y()).rotated(angle, center));
    return out;
}

BoundingBox BoundingBox::scaled(double factor) const
{
    BoundingBox out(*this);
    out.scale(factor);
    return out;
}

// [INTENT] scale(): multiplies min and max by factor in-place. For integer coord_t this truncates
//          the result — fractional scaling of integer coordinates loses sub-unit precision.
// [HAZARD] No rounding — truncation to coord_t. Scaled boxes may be slightly smaller than expected.
template<class PointType, typename APointsType> void BoundingBoxBase<PointType, APointsType>::scale(double factor)
{
    this->min *= factor;
    this->max *= factor;
}
template void BoundingBoxBase<Point, Points>::scale(double factor);
template void BoundingBoxBase<Vec2d>::scale(double factor);
template void BoundingBoxBase<Vec3d>::scale(double factor);

// [INTENT] merge(point): expands the AABB to include `point`.
//          If box is undefined, initializes min=max=point and sets defined=true.
//          After merging a single point, min==max, so defined=true but area=0
//          (consistent with IncludeBoundary=true semantics).
template<class PointType, typename APointsType> void BoundingBoxBase<PointType, APointsType>::merge(const PointType& point)
{
    if (this->defined) {
        this->min = this->min.cwiseMin(point);
        this->max = this->max.cwiseMax(point);
    } else {
        this->min     = point;
        this->max     = point;
        this->defined = true;
    }
}
template void BoundingBoxBase<Point, Points>::merge(const Point& point);
template void BoundingBoxBase<Vec2f>::merge(const Vec2f& point);
template void BoundingBoxBase<Vec2d>::merge(const Vec2d& point);

// [INTENT] merge(PointsType): constructs a temporary bbox from all points and merges it.
//          If points is empty, the temporary bbox is undefined and merge(undefined) is a no-op.
template<class PointType, typename APointsType> void BoundingBoxBase<PointType, APointsType>::merge(const PointsType& points)
{
    this->merge(BoundingBoxBase(points));
}
template void BoundingBoxBase<Point, Points>::merge(const Points& points);
template void BoundingBoxBase<Vec2d>::merge(const Pointfs& points);

// [INTENT] merge(BoundingBoxBase): merges another AABB. Only expands if the other is defined.
//          If this is undefined, copies the other's min/max and sets defined=true.
//          The assert checks that an undefined bb either has min>=max (which is expected to be true
//          for the default-constructed state).
template<class PointType, typename APointsType>
void BoundingBoxBase<PointType, APointsType>::merge(const BoundingBoxBase<PointType, PointsType>& bb)
{
    assert(bb.defined || bb.min.x() >= bb.max.x() || bb.min.y() >= bb.max.y());
    if (bb.defined) {
        if (this->defined) {
            this->min = this->min.cwiseMin(bb.min);
            this->max = this->max.cwiseMax(bb.max);
        } else {
            this->min     = bb.min;
            this->max     = bb.max;
            this->defined = true;
        }
    }
}
template void BoundingBoxBase<Point, Points>::merge(const BoundingBoxBase<Point, Points>& bb);
template void BoundingBoxBase<Vec2f>::merge(const BoundingBoxBase<Vec2f>& bb);
template void BoundingBoxBase<Vec2d>::merge(const BoundingBoxBase<Vec2d>& bb);

// [INTENT] BoundingBox3Base<PointType>::polygon(is_scaled): returns a 2D XY footprint polygon
//          (4-corner CCW rectangle) of this 3D AABB, optionally scaling coordinates by 1/SCALING_FACTOR.
//          Added by BBS (Bambu) for bed-collision and plate footprint checks.
// [HAZARD] When is_scaled=true, scale_factor = 1/SCALING_FACTOR; when is_scaled=false, scale_factor=1.
//          This naming is confusing: "is_scaled" means "the bbox coords ARE already scaled integers",
//          so when true the output is divided by SCALING_FACTOR to get mm. When false, no conversion.
//          A port must carefully read callers to confirm the is_scaled semantics.
// BBS
template<class PointType> Polygon BoundingBox3Base<PointType>::polygon(bool is_scaled) const
{
    Polygon polygon;
    polygon.points.clear();
    polygon.points.resize(4);
    double scale_factor  = 1 / (is_scaled ? SCALING_FACTOR : 1);
    polygon.points[0](0) = this->min(0) * scale_factor;
    polygon.points[0](1) = this->min(1) * scale_factor;
    polygon.points[1](0) = this->max(0) * scale_factor;
    polygon.points[1](1) = this->min(1) * scale_factor;
    polygon.points[2](0) = this->max(0) * scale_factor;
    polygon.points[2](1) = this->max(1) * scale_factor;
    polygon.points[3](0) = this->min(0) * scale_factor;
    polygon.points[3](1) = this->max(1) * scale_factor;
    return polygon;
}
template Polygon BoundingBox3Base<Vec3f>::polygon(bool is_scaled) const;
template Polygon BoundingBox3Base<Vec3d>::polygon(bool is_scaled) const;

template<class PointType> void BoundingBox3Base<PointType>::merge(const PointType& point)
{
    if (this->defined) {
        this->min = this->min.cwiseMin(point);
        this->max = this->max.cwiseMax(point);
    } else {
        this->min     = point;
        this->max     = point;
        this->defined = true;
    }
}
template void BoundingBox3Base<Vec3f>::merge(const Vec3f& point);
template void BoundingBox3Base<Vec3d>::merge(const Vec3d& point);

// [INTENT] merge(PointsType) for 3D: constructs a temporary 3D bbox and merges it.
//          THROWS if points is empty (via BoundingBox3Base iterator constructor — H603).
// [HAZARD H603] Callers must guard against empty inputs or catch Slic3r::InvalidArgument.
template<class PointType> void BoundingBox3Base<PointType>::merge(const PointsType& points) { this->merge(BoundingBox3Base(points)); }
template void                  BoundingBox3Base<Vec3d>::merge(const Pointf3s& points);

template<class PointType> void BoundingBox3Base<PointType>::merge(const BoundingBox3Base<PointType>& bb)
{
    assert(bb.defined || bb.min.x() >= bb.max.x() || bb.min.y() >= bb.max.y() || bb.min.z() >= bb.max.z());
    if (bb.defined) {
        if (this->defined) {
            this->min = this->min.cwiseMin(bb.min);
            this->max = this->max.cwiseMax(bb.max);
        } else {
            this->min     = bb.min;
            this->max     = bb.max;
            this->defined = true;
        }
    }
}
template void BoundingBox3Base<Vec3d>::merge(const BoundingBox3Base<Vec3d>& bb);

template<class PointType, typename APointsType> PointType BoundingBoxBase<PointType, APointsType>::size() const
{
    return this->max - this->min;
}
template Point BoundingBoxBase<Point, Points>::size() const;
template Vec2f BoundingBoxBase<Vec2f>::size() const;
template Vec2d BoundingBoxBase<Vec2d>::size() const;

template<class PointType> PointType BoundingBox3Base<PointType>::size() const { return this->max - this->min; }
template Vec3f                      BoundingBox3Base<Vec3f>::size() const;
template Vec3d                      BoundingBox3Base<Vec3d>::size() const;

// [INTENT] radius(): half the Euclidean diagonal. Useful as a bounding sphere radius.
//          Casts to double for the norm() computation to avoid integer overflow for large coord_t values.
template<class PointType, typename APointsType> double BoundingBoxBase<PointType, APointsType>::radius() const
{
    assert(this->defined);
    return 0.5 * (this->max - this->min).template cast<double>().norm();
}
template double BoundingBoxBase<Point, Points>::radius() const;
template double BoundingBoxBase<Vec2d>::radius() const;

template<class PointType> double BoundingBox3Base<PointType>::radius() const
{
    return 0.5 * (this->max - this->min).template cast<double>().norm();
}
template double BoundingBox3Base<Vec3d>::radius() const;

// [INTENT] offset(delta): expands the AABB by delta in all directions (positive delta = grow).
//          PointType v(delta, delta) constructs a 2D offset vector; 3D override uses (delta,delta,delta).
// [HAZARD] No check that `defined` is true. Calling on undefined bbox shifts min/max away from Zero
//          incorrectly. The caller is responsible for ensuring the bbox is defined first.
template<class PointType, typename APointsType> void BoundingBoxBase<PointType, APointsType>::offset(coordf_t delta)
{
    PointType v(delta, delta);
    this->min -= v;
    this->max += v;
}
template void BoundingBoxBase<Point, Points>::offset(coordf_t delta);
template void BoundingBoxBase<Vec2d>::offset(coordf_t delta);

template<class PointType> void BoundingBox3Base<PointType>::offset(coordf_t delta)
{
    PointType v(delta, delta, delta);
    this->min -= v;
    this->max += v;
}
template void BoundingBox3Base<Vec3d>::offset(coordf_t delta);

// [INTENT] center(): midpoint of min and max. Uses integer division for coord_t types,
//          which rounds toward zero — a 1-unit error is possible for odd-sized integer bboxes.
template<class PointType, typename APointsType> PointType BoundingBoxBase<PointType, APointsType>::center() const
{
    return (this->min + this->max) / 2;
}
template Point BoundingBoxBase<Point, Points>::center() const;
template Vec2f BoundingBoxBase<Vec2f>::center() const;
template Vec2d BoundingBoxBase<Vec2d>::center() const;

template<class PointType> PointType BoundingBox3Base<PointType>::center() const { return (this->min + this->max) / 2; }
template Vec3f                      BoundingBox3Base<Vec3f>::center() const;
template Vec3d                      BoundingBox3Base<Vec3d>::center() const;

// [INTENT] max_size(): the longest dimension of the 3D AABB. Useful for level-of-detail and
//          object-space bounding sphere radius approximations.
template<class PointType> coordf_t BoundingBox3Base<PointType>::max_size() const
{
    PointType s = size();
    return std::max(s.x(), std::max(s.y(), s.z()));
}
template coordf_t BoundingBox3Base<Vec3f>::max_size() const;
template coordf_t BoundingBox3Base<Vec3d>::max_size() const;

// [INTENT] align_to_grid(): snaps the min corner DOWN to the nearest cell boundary.
//          Delegates to Slic3r::align_to_grid(coord, spacing) which handles negative coordinates.
//          The max corner is NOT modified — the box may grow to reach the next grid boundary.
void BoundingBox::align_to_grid(const coord_t cell_size)
{
    if (this->defined) {
        min.x() = Slic3r::align_to_grid(min.x(), cell_size);
        min.y() = Slic3r::align_to_grid(min.y(), cell_size);
    }
}

// [INTENT] BoundingBoxf3::transformed(): computes the AABB of this box after an arbitrary 3D
//          affine transformation. Transforms all 8 corners of the box using the matrix and
//          then takes their new AABB. This is the only correct way to get the post-transform AABB
//          for non-axis-aligned transforms (e.g., rotations, shears).
// [MEMORY] Uses a local 3×8 Eigen matrix for the 8 corners — stack allocated, no heap overhead.
// [HAZARD] For a rotation by 45 degrees, the resulting AABB will be ~41% larger than the original.
//          This over-approximation is expected and correct for AABB semantics.
// [COUPLING] Used by TriangleMesh::transformed_bounding_box() for view-frustum culling and
//            model placement checks. Any change to this must retest all callers.
BoundingBoxf3 BoundingBoxf3::transformed(const Transform3d& matrix) const
{
    typedef Eigen::Matrix<double, 3, 8, Eigen::DontAlign> Vertices;

    Vertices src_vertices;
    src_vertices(0, 0) = min.x();
    src_vertices(1, 0) = min.y();
    src_vertices(2, 0) = min.z();
    src_vertices(0, 1) = max.x();
    src_vertices(1, 1) = min.y();
    src_vertices(2, 1) = min.z();
    src_vertices(0, 2) = max.x();
    src_vertices(1, 2) = max.y();
    src_vertices(2, 2) = min.z();
    src_vertices(0, 3) = min.x();
    src_vertices(1, 3) = max.y();
    src_vertices(2, 3) = min.z();
    src_vertices(0, 4) = min.x();
    src_vertices(1, 4) = min.y();
    src_vertices(2, 4) = max.z();
    src_vertices(0, 5) = max.x();
    src_vertices(1, 5) = min.y();
    src_vertices(2, 5) = max.z();
    src_vertices(0, 6) = max.x();
    src_vertices(1, 6) = max.y();
    src_vertices(2, 6) = max.z();
    src_vertices(0, 7) = min.x();
    src_vertices(1, 7) = max.y();
    src_vertices(2, 7) = max.z();

    Vertices dst_vertices = matrix * src_vertices.colwise().homogeneous();

    Vec3d v_min(dst_vertices(0, 0), dst_vertices(1, 0), dst_vertices(2, 0));
    Vec3d v_max = v_min;

    for (int i = 1; i < 8; ++i) {
        for (int j = 0; j < 3; ++j) {
            v_min(j) = std::min(v_min(j), dst_vertices(j, i));
            v_max(j) = std::max(v_max(j), dst_vertices(j, i));
        }
    }

    return BoundingBoxf3(v_min, v_max);
}

} // namespace Slic3r
