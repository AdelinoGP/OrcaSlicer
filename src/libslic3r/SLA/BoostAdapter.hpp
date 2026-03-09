// [INTENT] BoostAdapter.hpp — Boost.Geometry concept specializations so that
// Slic3r's native point and bounding-box types can be used directly with
// boost::geometry algorithms and spatial indices (rtree).
//
// Without these specializations, every boost::geometry call site would need to
// manually convert Slic3r types to boost types and back, producing O(N) copies.
// With them, rtree and geometry algorithms work on Slic3r types natively.
//
// [COUPLING] This header injects specializations into boost::geometry::traits
// and boost:: namespaces. Any type (Point, Vec2d, Vec3d, BoundingBox) registered
// here is globally recognized by ALL boost::geometry algorithms throughout the
// codebase. Changing the coordinate representation of any of these types will
// silently break every spatial index and geometry operation in SLA and elsewhere.
//
// [HAZARD] H910 — `range_value<std::vector<Slic3r::Vec2d>>` specialization is placed
// in the `boost::` namespace (not `boost::geometry::traits::`). This is a Boost
// internal extension point that Boost may change between versions without notice.
// The specialization also hardcodes the Allocator as default; custom-allocator
// Vec2d vectors will not match this specialization.
//
// [MEMORY] No allocations. All specializations are pure type-trait mappings.
// [CONCURRENCY] Trait specializations are compile-time; no runtime state.

#ifndef SLA_BOOSTADAPTER_HPP
#define SLA_BOOSTADAPTER_HPP

#include <libslic3r/Point.hpp>
#include <libslic3r/BoundingBox.hpp>

#include <boost/geometry.hpp>

namespace boost {
namespace geometry { namespace traits {

/* ************************************************************************** */
/* Point concept adaptation ************************************************* */
/* ************************************************************************** */

// [COUPLING] Slic3r::Point (int32 coord_t) registered as a 2D cartesian point.
// All SLA spatial indices that store Point values use integer coordinates. This
// means Euclidean distance queries in boost::geometry use integer arithmetic
// and may overflow for points far from origin.
template<> struct tag<Slic3r::Point>
{
    using type = point_tag;
};

template<> struct coordinate_type<Slic3r::Point>
{
    using type = coord_t;
};

template<> struct coordinate_system<Slic3r::Point>
{
    using type = cs::cartesian;
};

template<> struct dimension<Slic3r::Point> : boost::mpl::int_<2>
{};

template<std::size_t d> struct access<Slic3r::Point, d>
{
    static inline coord_t get(Slic3r::Point const& a) { return a(d); }

    static inline void set(Slic3r::Point& a, coord_t const& value) { a(d) = value; }
};

// For Vec2d ///////////////////////////////////////////////////////////////////
// [COUPLING] Vec2d (double) — used for world-space 2D coordinates in SLA pad/hull geometry.

template<> struct tag<Slic3r::Vec2d>
{
    using type = point_tag;
};

template<> struct coordinate_type<Slic3r::Vec2d>
{
    using type = double;
};

template<> struct coordinate_system<Slic3r::Vec2d>
{
    using type = cs::cartesian;
};

template<> struct dimension<Slic3r::Vec2d> : boost::mpl::int_<2>
{};

template<std::size_t d> struct access<Slic3r::Vec2d, d>
{
    static inline double get(Slic3r::Vec2d const& a) { return a(d); }

    static inline void set(Slic3r::Vec2d& a, double const& value) { a(d) = value; }
};

// For Vec3d ///////////////////////////////////////////////////////////////////
// [COUPLING] Vec3d (double) — used for 3D support point spatial indexing.
// PointIndex and Clustering both store Vec3d entries in Boost R*-trees.

template<> struct tag<Slic3r::Vec3d>
{
    using type = point_tag;
};

template<> struct coordinate_type<Slic3r::Vec3d>
{
    using type = double;
};

template<> struct coordinate_system<Slic3r::Vec3d>
{
    using type = cs::cartesian;
};

template<> struct dimension<Slic3r::Vec3d> : boost::mpl::int_<3>
{};

template<std::size_t d> struct access<Slic3r::Vec3d, d>
{
    static inline double get(Slic3r::Vec3d const& a) { return a(d); }

    static inline void set(Slic3r::Vec3d& a, double const& value) { a(d) = value; }
};

/* ************************************************************************** */
/* Box concept adaptation *************************************************** */
/* ************************************************************************** */

// [COUPLING] Slic3r::BoundingBox (int32 min/max) registered as a 2D box.
// BoxIndex uses this for rectangular containment/intersection queries.
// Overflow risk: large beds at nanometer resolution may exceed int32 range.
template<> struct tag<Slic3r::BoundingBox>
{
    using type = box_tag;
};

template<> struct point_type<Slic3r::BoundingBox>
{
    using type = Slic3r::Point;
};

template<std::size_t d> struct indexed_access<Slic3r::BoundingBox, 0, d>
{
    static inline coord_t get(Slic3r::BoundingBox const& box) { return box.min(d); }
    static inline void    set(Slic3r::BoundingBox& box, coord_t const& coord) { box.min(d) = coord; }
};

template<std::size_t d> struct indexed_access<Slic3r::BoundingBox, 1, d>
{
    static inline coord_t get(Slic3r::BoundingBox const& box) { return box.max(d); }
    static inline void    set(Slic3r::BoundingBox& box, coord_t const& coord) { box.max(d) = coord; }
};

}} // namespace geometry::traits

// [HAZARD] H910 — specialization in boost:: root namespace (not traits::).
// This is a Boost-internal extension point. If Boost changes range_value's
// home namespace in a future version, this specialization silently stops applying.
template<> struct range_value<std::vector<Slic3r::Vec2d>>
{
    using type = Slic3r::Vec2d;
};

} // namespace boost

#endif // SLABOOSTADAPTER_HPP
