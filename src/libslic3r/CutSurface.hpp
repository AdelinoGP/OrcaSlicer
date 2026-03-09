// [INTENT] Public interface for surface-cutting operations: extract a patch of model surface
//          that lies under a projected 2D shape (e.g. embossed glyph), and convert it back to a
//          3D mesh for emboss/engrave operations.
// [COUPLING] Depends on: indexed_triangle_set (admesh), ExPolygon (Slic3r 2D geometry),
//            Emboss::IProjection / IProject3d (projection contracts from Emboss.hpp).
//            Heavy implementation coupling to CGAL (Surface_mesh, corefinement) lives in CutSurface.cpp.
// [STATE] No module-level state; all functions are pure transformations on their inputs.
// [CONCURRENCY] Functions are stateless; safe to call concurrently on independent data.
//               However CGAL corefinement (used in the .cpp) is NOT thread-safe on shared meshes.

#ifndef slic3r_CutSurface_hpp_
#define slic3r_CutSurface_hpp_

#include <vector>
#include <admesh/stl.h> // indexed_triangle_set
#include "ExPolygon.hpp"
#include "Emboss.hpp" // IProjection

namespace Slic3r {

/// <summary>
/// Represents cutted surface from object
/// Extend index triangle set by outlines
/// </summary>
// [INTENT] Augments indexed_triangle_set (vertices + triangle indices) with surface contours:
//          closed loops of vertex indices that form the boundary of the cut patch.
//          Used downstream by cut2model() to stitch side walls onto the patch.
struct SurfaceCut : public indexed_triangle_set
{
    // vertex indices(index to mesh vertices)
    using Index    = unsigned int;
    using Contour  = std::vector<Index>;
    using Contours = std::vector<Contour>;
    // list of circulated open surface
    Contours contours;
};

/// <summary>
/// Cut surface shape from models.
/// </summary>
/// <param name="shapes">Multiple shape to cut from model</param>
/// <param name="models">Multi mesh to cut, need to be in same coordinate system</param>
/// <param name="projection">Define transformation 2d shape into 3d</param>
/// <param name="projection_ratio">Define ideal ratio between front and back projection to cut
/// 0 .. means use closest to front projection
/// 1 .. means use closest to back projection
/// value from <0, 1>
/// </param>
/// <returns>Cutted surface from model</returns>
// [INTENT] Main entry point: projects `shapes` (2D ExPolygons, e.g. glyph outlines) onto each
//          mesh in `models` using `projection`, then extracts the intersecting surface patches,
//          differentiates multi-volume overlaps, selects the best patch per shape, and returns a
//          merged SurfaceCut.
// [COUPLING] Internally: to_cgal() (ITS→CGAL mesh), corefinement (CGAL PMP), flood-fill
//            inside/outside classification, diff_models(), select_patches(), merge_patches().
// [MEMORY] CGAL meshes are temporary; returned SurfaceCut owns its own vertex/index arrays.
// [H1039 cross-ref] its_cut_AoI() (called internally) only does 2D XY bounding-box filtration;
//   triangles far outside the Z range of the projection may be included, increasing CGAL work.
SurfaceCut cut_surface(const ExPolygons&                        shapes,
                       const std::vector<indexed_triangle_set>& models,
                       const Emboss::IProjection&               projection,
                       float                                    projection_ratio);

/// <summary>
/// Create model from surface cuts by projection
/// </summary>
/// <param name="cut">Surface from model with outlines</param>
/// <param name="projection">Way of emboss</param>
/// <returns>Mesh</returns>
// [INTENT] Given a surface patch (top face) and a projection, extrude the contour loops
//          to form the side walls and bottom face, producing a closed solid mesh for emboss.
indexed_triangle_set cut2model(const SurfaceCut& cut, const Emboss::IProject3d& projection);

/// <summary>
/// Separate (A)rea (o)f (I)nterest .. AoI from model
/// NOTE: Only 2d filtration, do not filtrate by Z coordinate
/// </summary>
/// <param name="its">Input model</param>
/// <param name="bb">Bounding box to project into space</param>
/// <param name="projection">Define tranformation of BB into space</param>
/// <returns>Triangles lay at least partialy inside of projected Bounding box</returns>
// [H1039 P2/Medium] FILTRATION CAVEAT: only XY bounding-box test applied; Z coordinate is
//   NOT checked. Triangles whose XY projection overlaps `bb` but are far above/below the
//   actual text surface will be included in the output ITS, bloating CGAL input meshes and
//   potentially adding spurious corefine intersections. Severity: medium — CGAL handles them
//   correctly but at cost of performance and edge-case robustness.
// [COUPLING] Uses Emboss::IProjection to project bb corners into 3D space for triangle tests.
indexed_triangle_set its_cut_AoI(const indexed_triangle_set& its, const BoundingBox& bb, const Emboss::IProjection& projection);

/// <summary>
/// Separate triangles by mask
/// </summary>
/// <param name="its">Input model</param>
/// <param name="mask">Mask - same size as its::indices</param>
/// <returns>Copy of indices by mask(with their vertices)</returns>
// [INTENT] Utility: filter an ITS to only the triangles flagged true in `mask`, copying
//          the selected triangles and their referenced vertices into a fresh ITS.
indexed_triangle_set its_mask(const indexed_triangle_set& its, const std::vector<bool>& mask);

// [INTENT] Integration/debug helper: runs corefinement between a model mesh file (.obj/.off)
//          and a shape mesh file, returning true if CGAL corefinement succeeds without error.
//          Not called in production code paths.
bool corefine_test(const std::string& model_path, const std::string& shape_path);

} // namespace Slic3r
#endif // slic3r_CutSurface_hpp_
