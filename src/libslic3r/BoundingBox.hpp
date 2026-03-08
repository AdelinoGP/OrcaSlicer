// [INTENT] BoundingBox template hierarchy for 2D and 3D axis-aligned bounding boxes (AABBs).
//          Three concrete types are commonly used:
//            - BoundingBox     : 2D, coord_t (scaled integer), uses Points allocator
//            - BoundingBoxf    : 2D, double (mm), uses std::vector<Vec2d>
//            - BoundingBoxf3   : 3D, double (mm), provides transformed() for non-axis-aligned xforms
//          The `defined` flag distinguishes "no points merged yet" from "valid AABB with area > 0".
// [STATE]   min, max: bounding corners. Semantics depend on PointType scalar (scaled int vs. mm).
//           defined: false until at least two distinct points have been merged (or IncludeBoundary=true).
// [HAZARD H603] BoundingBox3Base iterator constructor THROWS Slic3r::InvalidArgument on empty input.
//               All 2D BoundingBoxBase constructors silently return `defined=false` on empty input.
//               This asymmetry is a trap: code that works with 2D boxes silently and then switches
//               to 3D boxes may crash when given empty geometry.
// [COUPLING] BoundingBox is the return type of get_extents() overloads called throughout the slicer.
//            Changing `defined` semantics would break every caller that checks bb.defined before use.
#ifndef slic3r_BoundingBox_hpp_
#define slic3r_BoundingBox_hpp_

#include "libslic3r.h"
#include "Exception.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include <ostream>

namespace Slic3r {

// [INTENT] BoundingBoxBase<PointType>: generic 2D AABB for any Eigen 2D vector type.
//          APointsType defaults to std::vector<PointType>; specialized to Points (TBB allocator)
//          for the coord_t BoundingBox.
// [STATE]  min, max: initialized to Zero(); defined=false until at least one merge() expands the box.
// [HAZARD] The two-point constructor sets defined = (pmin.x < pmax.x && pmin.y < pmax.y).
//          If pmin == pmax (single point with IncludeBoundary semantics), defined will be FALSE.
//          Callers expecting a defined single-point bbox must use the IncludeBoundary template path.
template<typename PointType, typename APointsType = std::vector<PointType>> class BoundingBoxBase
{
public:
    using PointsType = APointsType;
    PointType min;
    PointType max;
    bool      defined;

    BoundingBoxBase() : min(PointType::Zero()), max(PointType::Zero()), defined(false) {}
    BoundingBoxBase(const PointType& pmin, const PointType& pmax)
        : min(pmin), max(pmax), defined(pmin.x() < pmax.x() && pmin.y() < pmax.y())
    {}
    BoundingBoxBase(const PointType& p1, const PointType& p2, const PointType& p3) : min(p1), max(p1), defined(false)
    {
        merge(p2);
        merge(p3);
    }

    template<class It, class = IteratorOnly<It>> BoundingBoxBase(It from, It to) { construct(*this, from, to); }

    BoundingBoxBase(const PointsType& points) : BoundingBoxBase(points.begin(), points.end()) {}

    void reset()
    {
        this->defined = false;
        this->min     = PointType::Zero();
        this->max     = PointType::Zero();
    }
    void      merge(const PointType& point);
    void      merge(const PointsType& points);
    void      merge(const BoundingBoxBase<PointType, PointsType>& bb);
    void      scale(double factor);
    PointType size() const;
    double    radius() const;
    // [INTENT] area(): returns the (possibly scaled-integer-squared) area. For BoundingBox (scaled int),
    //          this is in (coord_t)^2 — NOT in mm^2. Callers must unscale twice to get mm^2.
    double area() const { return double(this->max(0) - this->min(0)) * (this->max(1) - this->min(1)); } // BBS
    // [HAZARD] translate(coordf_t, coordf_t) has an assert(this->defined). If called on an undefined
    //          bbox in a release build (no assertions), it will silently produce wrong results.
    void translate(coordf_t x, coordf_t y)
    {
        assert(this->defined);
        PointType v(x, y);
        this->min += v;
        this->max += v;
    }
    void translate(const PointType& v)
    {
        this->min += v;
        this->max += v;
    }
    void                                   offset(coordf_t delta);
    BoundingBoxBase<PointType, PointsType> inflated(coordf_t delta) const throw()
    {
        BoundingBoxBase<PointType, PointsType> out(*this);
        out.offset(delta);
        return out;
    }
    PointType center() const;
    // [INTENT] contains(point): inclusive boundary test (point ON the edge counts as inside).
    bool contains(const PointType& point) const
    {
        return point.x() >= this->min.x() && point.x() <= this->max.x() && point.y() >= this->min.y() && point.y() <= this->max.y();
    }
    bool contains(const BoundingBoxBase<PointType, PointsType>& other) const { return contains(other.min) && contains(other.max); }
    bool overlap(const BoundingBoxBase<PointType, PointsType>& other) const
    {
        return !(this->max.x() < other.min.x() || this->min.x() > other.max.x() || this->max.y() < other.min.y() ||
                 this->min.y() > other.max.y());
    }
    // [INTENT] operator[]: returns the 4 corners of the bounding box in CCW order:
    //          [0]=min, [1]=(max.x,min.y), [2]=max, [3]=(min.x,max.y).
    //          Returns default-constructed PointType for out-of-range index (silent failure).
    PointType operator[](size_t idx) const
    {
        switch (idx) {
        case 0: return min; break;
        case 1: return PointType(max(0), min(1)); break;
        case 2: return max; break;
        case 3: return PointType(min(0), max(1)); break;
        default: return PointType(); break;
        }
        return PointType();
    }
    bool operator==(const BoundingBoxBase<PointType, PointsType>& rhs) { return this->min == rhs.min && this->max == rhs.max; }
    bool operator!=(const BoundingBoxBase<PointType, PointsType>& rhs) { return !(*this == rhs); }
    friend std::ostream& operator<<(std::ostream& os, const BoundingBoxBase& bbox)
    {
        os << "[" << bbox.max(0) - bbox.min(0) << " x " << bbox.max(1) - bbox.min(1) << "] from (" << bbox.min(0) << ", " << bbox.min(1)
           << ")";
        return os;
    }

private:
    // to access construct()
    friend BoundingBox get_extents<false>(const Points& pts);
    friend BoundingBox get_extents<true>(const Points& pts);

    // [INTENT] construct<IncludeBoundary>: builds the bounding box by scanning all points.
    //          If IncludeBoundary=true, a single point makes defined=true (useful for collision tests).
    //          If IncludeBoundary=false (default), defined requires strictly positive area (min < max).
    // [STATE] Output `out` must be initialized to defined=false before this call — not enforced.
    // if IncludeBoundary, then a bounding box is defined even for a single point.
    // otherwise a bounding box is only defined if it has a positive area.
    // The output bounding box is expected to be set to "undefined" initially.
    template<bool IncludeBoundary = false, class BoundingBoxType, class It, class = IteratorOnly<It>>
    static void construct(BoundingBoxType& out, It from, It to)
    {
        if (from != to) {
            auto it = from;
            out.min = it->template cast<typename PointType::Scalar>();
            out.max = out.min;
            for (++it; it != to; ++it) {
                auto vec = it->template cast<typename PointType::Scalar>();
                out.min  = out.min.cwiseMin(vec);
                out.max  = out.max.cwiseMax(vec);
            }
            out.defined = IncludeBoundary || (out.min.x() < out.max.x() && out.min.y() < out.max.y());
        }
    }
};

// [INTENT] BoundingBox3Base<PointType>: generic 3D AABB extending BoundingBoxBase with a Z dimension.
//          Adds translate(z), merge(z), contains(z), intersects(), max_size().
// [HAZARD H603] The iterator constructor THROWS Slic3r::InvalidArgument on empty range.
//               The 2D base constructor silently returns defined=false on empty range.
//               This is asymmetric and a trap: 3D usage requires explicit empty-set guards.
template<class PointType> class BoundingBox3Base : public BoundingBoxBase<PointType, std::vector<PointType>>
{
public:
    using PointsType = std::vector<PointType>;

    BoundingBox3Base() : BoundingBoxBase<PointType>() {}
    BoundingBox3Base(const PointType& pmin, const PointType& pmax) : BoundingBoxBase<PointType>(pmin, pmax)
    {
        if (pmin.z() >= pmax.z())
            BoundingBoxBase<PointType>::defined = false;
    }
    BoundingBox3Base(const PointType& p1, const PointType& p2, const PointType& p3) : BoundingBoxBase<PointType>(p1, p1)
    {
        merge(p2);
        merge(p3);
    }

    // [HAZARD H603] THROWS on empty range — unlike 2D base which silently returns defined=false.
    template<class It, class = IteratorOnly<It>> BoundingBox3Base(It from, It to)
    {
        if (from == to)
            throw Slic3r::InvalidArgument("Empty point set supplied to BoundingBox3Base constructor");

        auto it   = from;
        this->min = it->template cast<typename PointType::Scalar>();
        this->max = this->min;
        for (++it; it != to; ++it) {
            auto vec  = it->template cast<typename PointType::Scalar>();
            this->min = this->min.cwiseMin(vec);
            this->max = this->max.cwiseMax(vec);
        }
        this->defined = (this->min.x() < this->max.x()) && (this->min.y() < this->max.y()) && (this->min.z() < this->max.z());
    }

    BoundingBox3Base(const PointsType& points) : BoundingBox3Base(points.begin(), points.end()) {}

    Polygon   polygon(bool is_scaled = false) const; // BBS: 2D footprint polygon
    void      merge(const PointType& point);
    void      merge(const PointsType& points);
    void      merge(const BoundingBox3Base<PointType>& bb);
    PointType size() const;
    double    radius() const;
    void      translate(coordf_t x, coordf_t y, coordf_t z)
    {
        assert(this->defined);
        PointType v(x, y, z);
        this->min += v;
        this->max += v;
    }
    void translate(const Vec3d& v)
    {
        this->min += v;
        this->max += v;
    }
    void                        offset(coordf_t delta);
    BoundingBox3Base<PointType> inflated(coordf_t delta) const throw()
    {
        BoundingBox3Base<PointType> out(*this);
        out.offset(delta);
        return out;
    }
    PointType center() const;
    coordf_t  max_size() const;

    bool contains(const PointType& point) const
    {
        return BoundingBoxBase<PointType>::contains(point) && point.z() >= this->min.z() && point.z() <= this->max.z();
    }

    bool contains(const BoundingBox3Base<PointType>& other) const { return contains(other.min) && contains(other.max); }

    // Intersects without boundaries.
    bool intersects(const BoundingBox3Base<PointType>& other) const
    {
        return this->min.x() < other.max.x() && this->max.x() > other.min.x() && this->min.y() < other.max.y() &&
               this->max.y() > other.min.y() && this->min.z() < other.max.z() && this->max.z() > other.min.z();
    }
};

// Will prevent warnings caused by non existing definition of template in hpp
extern template void     BoundingBoxBase<Point, Points>::scale(double factor);
extern template void     BoundingBoxBase<Vec2d>::scale(double factor);
extern template void     BoundingBoxBase<Vec3d>::scale(double factor);
extern template void     BoundingBoxBase<Point, Points>::offset(coordf_t delta);
extern template void     BoundingBoxBase<Vec2d>::offset(coordf_t delta);
extern template void     BoundingBoxBase<Point, Points>::merge(const Point& point);
extern template void     BoundingBoxBase<Vec2f>::merge(const Vec2f& point);
extern template void     BoundingBoxBase<Vec2d>::merge(const Vec2d& point);
extern template void     BoundingBoxBase<Point, Points>::merge(const Points& points);
extern template void     BoundingBoxBase<Vec2d>::merge(const Pointfs& points);
extern template void     BoundingBoxBase<Point, Points>::merge(const BoundingBoxBase<Point, Points>& bb);
extern template void     BoundingBoxBase<Vec2f>::merge(const BoundingBoxBase<Vec2f>& bb);
extern template void     BoundingBoxBase<Vec2d>::merge(const BoundingBoxBase<Vec2d>& bb);
extern template Point    BoundingBoxBase<Point, Points>::size() const;
extern template Vec2f    BoundingBoxBase<Vec2f>::size() const;
extern template Vec2d    BoundingBoxBase<Vec2d>::size() const;
extern template double   BoundingBoxBase<Point, Points>::radius() const;
extern template double   BoundingBoxBase<Vec2d>::radius() const;
extern template Point    BoundingBoxBase<Point, Points>::center() const;
extern template Vec2f    BoundingBoxBase<Vec2f>::center() const;
extern template Vec2d    BoundingBoxBase<Vec2d>::center() const;
extern template void     BoundingBox3Base<Vec3f>::merge(const Vec3f& point);
extern template void     BoundingBox3Base<Vec3d>::merge(const Vec3d& point);
extern template void     BoundingBox3Base<Vec3d>::merge(const Pointf3s& points);
extern template void     BoundingBox3Base<Vec3d>::merge(const BoundingBox3Base<Vec3d>& bb);
extern template Vec3f    BoundingBox3Base<Vec3f>::size() const;
extern template Vec3d    BoundingBox3Base<Vec3d>::size() const;
extern template double   BoundingBox3Base<Vec3d>::radius() const;
extern template void     BoundingBox3Base<Vec3d>::offset(coordf_t delta);
extern template Vec3f    BoundingBox3Base<Vec3f>::center() const;
extern template Vec3d    BoundingBox3Base<Vec3d>::center() const;
extern template coordf_t BoundingBox3Base<Vec3f>::max_size() const;
extern template coordf_t BoundingBox3Base<Vec3d>::max_size() const;

// [INTENT] BoundingBox: concrete 2D integer-coordinate AABB.
//          Adds polygon() (returns the 4-corner CCW Polygon), rotated() (recomputes AABB after rotation),
//          align_to_grid() (snaps min corner down to nearest grid cell).
// [HAZARD] rotated() does NOT preserve the exact bbox of a rotated shape — it computes the AABB of the
//          4 rotated corners, which is always correct for rectangles. For non-rectangular shapes this
//          would be wrong, but since the input IS always a rectangle, this is correct.
class BoundingBox : public BoundingBoxBase<Point, Points>
{
public:
    void        polygon(Polygon* polygon) const;
    Polygon     polygon() const;
    BoundingBox rotated(double angle) const;
    BoundingBox rotated(double angle, const Point& center) const;
    void        rotate(double angle) { (*this) = this->rotated(angle); }
    void        rotate(double angle, const Point& center) { (*this) = this->rotated(angle, center); }
    // Align the min corner to a grid of cell_size x cell_size cells,
    // to encompass the original bounding box.
    void align_to_grid(const coord_t cell_size);

    BoundingBox() : BoundingBoxBase<Point, Points>() {}
    BoundingBox(const Point& pmin, const Point& pmax) : BoundingBoxBase<Point, Points>(pmin, pmax) {}
    BoundingBox(const Points& points) : BoundingBoxBase<Point, Points>(points) {}

    BoundingBox inflated(coordf_t delta) const noexcept
    {
        BoundingBox out(*this);
        out.offset(delta);
        return out;
    }

    BoundingBox scaled(double factor) const;

    friend BoundingBox get_extents_rotated(const Points& points, double angle);
};

using BoundingBoxes = std::vector<BoundingBox>;

// [INTENT] BoundingBox3: concrete 3D integer-coordinate AABB. Rarely used; most 3D work uses BoundingBoxf3.
class BoundingBox3 : public BoundingBox3Base<Vec3crd>
{
public:
    BoundingBox3() : BoundingBox3Base<Vec3crd>() {}
    BoundingBox3(const Vec3crd& pmin, const Vec3crd& pmax) : BoundingBox3Base<Vec3crd>(pmin, pmax) {}
    BoundingBox3(const Points3& points) : BoundingBox3Base<Vec3crd>(points) {}
};

// [INTENT] BoundingBoxf: concrete 2D floating-point (double) AABB in mm. Used for bed/plate geometry.
class BoundingBoxf : public BoundingBoxBase<Vec2d>
{
public:
    BoundingBoxf() : BoundingBoxBase<Vec2d>() {}
    BoundingBoxf(const Vec2d& pmin, const Vec2d& pmax) : BoundingBoxBase<Vec2d>(pmin, pmax) {}
    BoundingBoxf(const std::vector<Vec2d>& points) : BoundingBoxBase<Vec2d>(points) {}
};

// [INTENT] BoundingBoxf3: concrete 3D floating-point (double mm) AABB. The dominant 3D bbox type.
//          Provides transformed() which correctly recomputes the AABB after an arbitrary affine transform
//          by transforming all 8 box corners via the matrix and taking their new AABB.
// [HAZARD] transformed() returns the AXIS-ALIGNED bbox of the transformed box, which is generally
//          larger than the transformed object's actual tight bbox. This is correct for AABB queries
//          but may be overly conservative for culling/intersection tests.
class BoundingBoxf3 : public BoundingBox3Base<Vec3d>
{
public:
    using BoundingBox3Base::BoundingBox3Base;

    BoundingBoxf3 transformed(const Transform3d& matrix) const;
};

// [INTENT] empty(): returns true if bbox is undefined or has zero/negative area in any dimension.
//          This is the canonical emptiness check — prefer over checking `!bb.defined` directly,
//          since defined can be true but min == max (degenerate single-point bbox with IncludeBoundary).
template<typename PointType, typename PointsType> inline bool empty(const BoundingBoxBase<PointType, PointsType>& bb)
{
    return !bb.defined || bb.min.x() >= bb.max.x() || bb.min.y() >= bb.max.y();
}

template<typename PointType> inline bool empty(const BoundingBox3Base<PointType>& bb)
{
    return !bb.defined || bb.min.x() >= bb.max.x() || bb.min.y() >= bb.max.y() || bb.min.z() >= bb.max.z();
}

// [INTENT] scaled()/unscaled() bbox converters: apply the coord_t ↔ mm conversion to both corners.
//          These are thin wrappers around the per-point scaled<>/unscaled<> templates from Point.hpp.
// [COUPLING] Uses the same SCALING_FACTOR semantics as Point::new_scale(). See hazard notes there.
inline BoundingBox scaled(const BoundingBoxf& bb) { return {scaled(bb.min), scaled(bb.max)}; }

template<class T = coord_t> BoundingBoxBase<Vec<2, T>> scaled(const BoundingBoxf& bb) { return {scaled<T>(bb.min), scaled<T>(bb.max)}; }

template<class T = coord_t> BoundingBox3Base<Vec<3, T>> scaled(const BoundingBoxf3& bb) { return {scaled<T>(bb.min), scaled<T>(bb.max)}; }

template<class T = double> BoundingBoxBase<Vec<2, T>> unscaled(const BoundingBox& bb) { return {unscaled<T>(bb.min), unscaled<T>(bb.max)}; }

template<class T = double> BoundingBox3Base<Vec<3, T>> unscaled(const BoundingBox3& bb)
{
    return {unscaled<T>(bb.min), unscaled<T>(bb.max)};
}

// [INTENT] cast<Tout>(): converts both corners of a bbox to a different scalar type.
//          Useful for converting between integer and floating-point bbox types without the
//          SCALING_FACTOR semantics of scaled()/unscaled() — raw numeric cast only.
template<class Tout, class Tin> auto cast(const BoundingBoxBase<Tin>& b)
{
    return BoundingBoxBase<Vec<2, Tout>>{b.min.template cast<Tout>(), b.max.template cast<Tout>()};
}

template<class Tout, class Tin> auto cast(const BoundingBox3Base<Tin>& b)
{
    return BoundingBox3Base<Vec<3, Tout>>{b.min.template cast<Tout>(), b.max.template cast<Tout>()};
}

} // namespace Slic3r

// [INTENT] Cereal serialization for all four concrete BoundingBox types.
//          Serializes min, max, defined as a tuple — positional, not named.
//          A change to field order or addition of a new field would silently corrupt saved files.
// [COUPLING] The `defined` flag is part of the serialized state; if a file is loaded and defined=false
//            but min/max have valid values, any code that checks !bb.defined first will skip valid data.
// Serialization through the Cereal library
namespace cereal {
template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBox& bb) { archive(bb.min, bb.max, bb.defined); }
template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBox3& bb) { archive(bb.min, bb.max, bb.defined); }
template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBoxf& bb) { archive(bb.min, bb.max, bb.defined); }
template<class Archive> void serialize(Archive& archive, Slic3r::BoundingBoxf3& bb) { archive(bb.min, bb.max, bb.defined); }
} // namespace cereal

#endif
