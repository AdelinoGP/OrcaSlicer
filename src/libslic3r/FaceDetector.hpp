#ifndef slic3r_FaceDetector_hpp_
#define slic3r_FaceDetector_hpp_

#include "Point.hpp"

namespace Slic3r {
class ModelObject;
class TriangleMesh;

// [INTENT] Public facade for exterior-face detection on one logical model object made of
// multiple transformed TriangleMesh volumes. The implementation performs ray-cast sampling
// and marks faces classified as outward-facing/visible.
// [STATE] Holds non-owning references to caller-owned mesh and transform arrays; detection
// mutates per-face metadata directly on those meshes.
// [MEMORY] No ownership transfer: references must outlive FaceDetector.
// [COUPLING] Depends on TriangleMesh face property layout and Transform3d from core geometry.
// [HAZARD] H1211: Constructor assumes m_meshes and m_transfos are index-aligned vectors;
// mismatch or later resizing by caller can cause out-of-bounds/incorrect transforms.
class FaceDetector
{
public:
    // [INTENT] sample_interval controls ray grid density. Smaller values increase detection
    // coverage but raise runtime roughly with sampled X*Y + X*Z + Y*Z ray counts.
    FaceDetector(std::vector<TriangleMesh>& tms, std::vector<Transform3d>& transfos, double sample_interval)
        : m_meshes(tms), m_transfos(transfos), m_sample_interval(sample_interval)
    {}

    // [STATE] Mutates TriangleMesh face type properties in-place on referenced meshes.
    void detect_exterior_face();

private:
    std::vector<TriangleMesh>& m_meshes;
    std::vector<Transform3d>&  m_transfos;
    double                     m_sample_interval;
};

} // namespace Slic3r

#endif // #ifndef slic3r_FaceDetector_hpp_
