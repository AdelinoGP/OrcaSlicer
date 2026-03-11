#ifndef libslic3r_Timer_hpp_
#define libslic3r_Timer_hpp_

#include "TriangleMesh.hpp"

// [INTENT] Mesh smoothing operations for triangle meshes.
// Provides static method to apply smoothing algorithm to repair or improve mesh quality.
// [COUPLING] Depends on TriangleMesh from core mesh types.
// [HAZARD] Header guard uses wrong name (Timer_hpp_) - copy-paste error.
namespace Slic3r {
class TriangleMeshDeal
{
public:
    // [INTENT] Apply smoothing algorithm to input mesh - likely Laplacian or similar.
    // Returns ok=true if smoothing succeeded, false on failure (e.g., invalid mesh).
    static TriangleMesh smooth_triangle_mesh(const TriangleMesh& mesh, bool& ok);
};
} // namespace Slic3r

#endif // libslic3r_Timer_hpp_
