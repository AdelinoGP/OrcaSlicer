// [INTENT] FillAdaptive.hpp — public interface for the Adaptive Cubic Infill pattern.
//          Declares the OctreePtr handle, the three utility functions (adaptive_fill_line_spacing,
//          transform_to_world/octree, build_octree) that are called by PrintObject during
//          print preparation, and the Filler class that Fill::new_from_type() returns for
//          icAdaptiveCubic and icSupportCubic pattern types.
//
// [COUPLING] PrintObject calls adaptive_fill_line_spacing() and build_octree() once per object
//            before TBB fill tasks begin. The resulting OctreePtr is stored on PrintObject and
//            passed to each Filler instance via Filler::adapt_fill_octree (set in FillBase.cpp).
//            transform_to_world/octree are used by PrintObjectSlice to rotate overhang triangles
//            into the octree coordinate system before passing them to build_octree().
//
// [MEMORY]   OctreePtr = std::unique_ptr<Octree, OctreeDeleter>. Octree definition is opaque
//            here (forward-declared) to avoid exposing boost::object_pool in the header.
//            OctreeDeleter provides the custom delete through FillAdaptive.cpp.
// [STATE]    This header itself has no mutable globals, but the declared helpers are part of the
//            PrintObject preparation pipeline: spacing is computed first, then the octree is built,
//            then each per-layer FillAdaptive::Filler borrows the finished tree through Fill::adapt_fill_octree.
//
// Adaptive cubic infill was inspired by the work of @mboerwinkle
// as implemented for Cura.
// https://github.com/Ultimaker/CuraEngine/issues/381
// https://github.com/Ultimaker/CuraEngine/pull/401
//
// Our implementation is more accurate (discretizes a bit less cubes than Cura's)
// by splitting only such cubes which contain a triangle.
// Our line extraction is time optimal instead of O(n^2) when connecting extracted lines,
// and we also implemented adaptivity for supporting internal overhangs only.

#ifndef slic3r_FillAdaptive_hpp_
#define slic3r_FillAdaptive_hpp_

#include "FillBase.hpp"

struct indexed_triangle_set;

namespace Slic3r {

class PrintObject;

namespace FillAdaptive {

// [INTENT] Octree — opaque forward declaration. Full definition lives in FillAdaptive.cpp to
//          hide the boost::object_pool and internal Cube struct from consumers of this header.
// [MEMORY] OctreeDeleter — custom deleter for OctreePtr (unique_ptr). Calls `delete p` which
//          triggers ~Octree(), destroying the pool and all Cube nodes atomically.
// [HAZARD] Octree is NOT copyable (pool is non-copyable). OctreePtr must not be copied;
//          only moved. PrintObject stores exactly one OctreePtr per pattern type.
struct Octree;
// To keep the definition of Octree opaque, we have to define a custom deleter.
struct OctreeDeleter
{
    void operator()(Octree* p);
};
using OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;

// [INTENT] adaptive_fill_line_spacing — computes the global octree cell spacing for both
//          icAdaptiveCubic and icSupportCubic by averaging density/width across all matching
//          print regions. Returns {adaptive_spacing, support_spacing}, each 0.0 if that type
//          is not used.
// [HAZARD] Multi-region averaging: a single spacing is returned for all regions; regions with
//          widely different settings produce suboptimal octree cell sizes for all.
// Calculate line spacing for
// 1) adaptive cubic infill
// 2) adaptive internal support cubic infill
// Returns zero for a particular infill type if no such infill is to be generated.
std::pair<double, double> adaptive_fill_line_spacing(const PrintObject& print_object);

// [INTENT] transform_to_world / transform_to_octree — Eigen quaternions for the fixed rotation
//          that maps between world coordinates and the octree's canonical frame (cube on corner).
//          Used to: (a) rotate overhang triangles into octree space before build_octree(), and
//          (b) rotate all Cube::center values back to world space after the octree is built.
// [COUPLING] Both functions use the file-static octree_rot[3] angles defined in FillAdaptive.cpp.
//            They are inverses of each other.
// Rotation of the octree to stand on one of its corners.
Eigen::Quaterniond transform_to_world();
// Inverse roation of the above.
Eigen::Quaterniond transform_to_octree();

// [INTENT] build_octree — constructs the adaptive infill octree from the PrintObject's triangle
//          mesh. Called once per PrintObject, before any concurrent fill tasks.
//          If support_overhangs_only=true, only triangles passing the 45° overhang test are
//          inserted (for ipSupportCubic mode).
// [CONCURRENCY] Must be called single-threaded. The returned OctreePtr is subsequently read
//               concurrently by multiple TBB fill tasks without locking.
// [MEMORY] The returned OctreePtr transfers ownership to the caller (PrintObject).
FillAdaptive::OctreePtr build_octree(
    // Mesh is rotated to the coordinate system of the octree.
    const indexed_triangle_set& triangle_mesh,
    // Overhang triangles extracted from fill surfaces with stInternalBridge type,
    // rotated to the coordinate system of the octree.
    const std::vector<Vec3d>& overhang_triangles,
    coordf_t                  line_spacing,
    // If true, octree is densified below internal overhangs only.
    bool support_overhangs_only);

//
// Some of the algorithms used by class FillAdaptive were inspired by
// Cura Engine's class SubDivCube
// https://github.com/Ultimaker/CuraEngine/blob/master/src/infill/SubDivCube.h
//
// [INTENT] Filler — the Fill subclass for Adaptive Cubic Infill.
//          Inherits Fill (FillBase.hpp) and overrides _fill_surface_single().
//          Accesses the shared Octree via adapt_fill_octree (set by Fill::set_expolygon() or
//          similar setup in FillBase; the pointer is not null when _fill_surface_single runs).
// [CONCURRENCY] Filler instances are created per fill-task (TBB parallel_for); each task has
//               its own Filler. adapt_fill_octree is a read-only pointer to a shared immutable
//               Octree — no locking required.
// [COUPLING] no_sort() returns false: G-code exporter may reorder lines. A FIXME notes this
//            could be suboptimal for anchor lines. is_self_crossing() returns true because
//            the three infill directions produce a self-intersecting cross-hatch pattern.
// [HAZARD] The class relies on inherited Fill state (`spacing`, `z`, `layer_id`, `adapt_fill_octree`)
//          being populated externally before _fill_surface_single() runs. Construction alone does
//          not yield a usable filler object.
class Filler : public Slic3r::Fill
{
public:
    ~Filler() override {}

protected:
    Fill* clone() const override { return new Filler(*this); }
    void  _fill_surface_single(const FillParams&              params,
                               unsigned int                   thickness_layers,
                               const std::pair<float, Point>& direction,
                               ExPolygon                      expolygon,
                               Polylines&                     polylines_out) override;
    // Let the G-code export reoder the infill lines.
    // FIXME letting the G-code exporter to reorder infill lines of Adaptive Cubic Infill
    // may not be optimal as the internal infill lines may get extruded before the long infill
    // lines to which the short infill lines are supposed to anchor.
    bool no_sort() const override { return false; }
    bool is_self_crossing() override { return true; }
};

} // namespace FillAdaptive
} // namespace Slic3r

#endif // slic3r_FillAdaptive_hpp_
