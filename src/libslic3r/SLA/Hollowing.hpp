// [INTENT] Public API header for SLA hollowing subsystem.
// Declares the HollowingConfig, DrainHole, Interior, and all free
// functions that comprise the two-phase hollowing pipeline:
//   Phase 1 — generate_interior(): builds OpenVDB signed-distance grid,
//              closes small crevices, returns an Interior object.
//   Phase 2 — hollow_mesh(): subtracts the interior shell from the original
//              mesh, optionally cuts drain-holes.
// [COUPLING] Depends on TriangleMesh (libslic3r) and JobController (SLA).
//            Interior is a forward-declared opaque type; its implementation
//            lives in Hollowing.cpp to hide OpenVDB headers from consumers.

#ifndef SLA_HOLLOWING_HPP
#define SLA_HOLLOWING_HPP

#include <memory>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/JobController.hpp>

namespace Slic3r { namespace sla {

// [INTENT] Tuning knobs for the interior shell generator.
// min_thickness:    wall thickness in mm (thinner walls weaken prints).
// quality:          controls OpenVDB voxel resolution (0=fast/coarse, 1=slow/fine).
// closing_distance: morphological closing radius — fills thin features/crevices.
struct HollowingConfig
{
    double min_thickness    = 2.;
    double quality          = 0.5;
    double closing_distance = 0.5;
    bool   enabled          = true;
};

enum HollowingFlags { hfRemoveInsideTriangles = 0x1 };

// [INTENT] Opaque handle to all data produced by generate_interior().
// Hides OpenVDB and mesh internals from consumers.
// [MEMORY] Heap-allocated; managed by InteriorPtr (unique_ptr with custom
//          deleter). The custom deleter is necessary because Interior is
//          defined in Hollowing.cpp (incomplete type here).
// [HAZARD H925] Interior::accessor is mutable and explicitly NOT thread-safe.
//   Concurrent calls to get_distance() / get_distance_raw() race on the
//   accessor. See Hollowing.cpp struct Interior for full annotation.
struct Interior;
struct InteriorDeleter
{
    void operator()(Interior* p);
};
using InteriorPtr = std::unique_ptr<Interior, InteriorDeleter>;

indexed_triangle_set&       get_mesh(Interior& interior);
const indexed_triangle_set& get_mesh(const Interior& interior);

// [INTENT] Describes a cylindrical drain hole to be cut through the shell.
// Drain holes allow resin to escape from the hollow cavity during printing.
// [STATE] pos/normal/radius/height are in world-space float mm coordinates.
//         'failed' marks the hole as un-cuttable (ray cast found no intersection).
// [HAZARD H926] get_intersections() iterates: `for (size_t i=1; i<=1; --i)`.
//   Due to unsigned underflow on decrement the loop runs exactly once (i wraps
//   to SIZE_MAX after first iteration, which is > 1, terminating correctly).
//   This is technically correct-by-accident; any signed refactor will break it.
struct DrainHole
{
    Vec3f pos;
    Vec3f normal;
    float radius;
    float height;
    bool  failed = false;

    DrainHole() : pos(Vec3f::Zero()), normal(Vec3f::UnitZ()), radius(5.f), height(10.f) {}

    DrainHole(Vec3f p, Vec3f n, float r, float h, bool fl = false) : pos(p), normal(n), radius(r), height(h), failed(fl) {}

    DrainHole(const DrainHole& rhs) : DrainHole(rhs.pos, rhs.normal, rhs.radius, rhs.height, rhs.failed) {}

    bool operator==(const DrainHole& sp) const;

    bool operator!=(const DrainHole& sp) const { return !(sp == (*this)); }

    bool is_inside(const Vec3f& pt) const;

    bool get_intersections(const Vec3f& s, const Vec3f& dir, std::array<std::pair<float, Vec3d>, 2>& out) const;

    indexed_triangle_set to_mesh() const;

    template<class Archive> inline void serialize(Archive& ar) { ar(pos, normal, radius, height, failed); }

    static constexpr size_t steps = 32;
};

using DrainHoles = std::vector<DrainHole>;

constexpr float HoleStickOutLength = 1.f;

// [INTENT] Phase 1 of hollowing. Builds an OpenVDB signed-distance field from
// the mesh and produces an interior shell. Double-swaps normals around the mesh
// simplification step (see Hollowing.cpp generate_interior).
// [HAZARD H927] The single-argument overload hollow_mesh(mesh, cfg) creates a
//   default JobController{} internally, silently discarding the caller's
//   cancellation mechanism. Prefer the two-arg overload with an explicit Interior.
InteriorPtr generate_interior(const TriangleMesh& mesh, const HollowingConfig& = {}, const JobController& ctl = {});

// [INTENT] Convenience overload — generates Interior internally then merges.
// [HAZARD H927] Creates default JobController{} — no cancellation support.
void hollow_mesh(TriangleMesh& mesh, const HollowingConfig& cfg, int flags = 0);

// [INTENT] Phase 2: merges the pre-computed Interior shell into the mesh.
// If hfRemoveInsideTriangles is set, removes triangles inside the cavity
// using a parallel ray-casting scan (ccr_seq, currently single-threaded).
void hollow_mesh(TriangleMesh& mesh, const Interior& interior, int flags = 0);

void remove_inside_triangles(TriangleMesh& mesh, const Interior& interior, const std::vector<bool>& exclude_mask = {});

double get_distance(const Vec3f& p, const Interior& interior);

template<class T> FloatingOnly<T> get_distance(const Vec<3, T>& p, const Interior& interior)
{
    return get_distance(Vec3f(p.template cast<float>()), interior);
}

void cut_drainholes(std::vector<ExPolygons>&  obj_slices,
                    const std::vector<float>& slicegrid,
                    float                     closing_radius,
                    const sla::DrainHoles&    holes,
                    std::function<void(void)> thr);

// [INTENT] Helper — flips all triangle windings in an ITS mesh by swapping
// first and third vertex index. Used in generate_interior() to produce the
// correct winding for the interior shell (see double-swap note in Hollowing.cpp).
inline void swap_normals(indexed_triangle_set& its)
{
    for (auto& face : its.indices)
        std::swap(face(0), face(2));
}

}} // namespace Slic3r::sla

#endif // HOLLOWINGFILTER_H
