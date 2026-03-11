#ifndef SRC_LIBSLIC3R_TRIANGLESETSAMPLING_HPP_
#define SRC_LIBSLIC3R_TRIANGLESETSAMPLING_HPP_

#include <admesh/stl.h>
#include "libslic3r/Point.hpp"

// [INTENT] Triangle mesh surface sampling utilities.
// Provides uniform sampling of triangle mesh surfaces for various purposes
// (rendering, analysis, visualization).
// [COUPLING] Uses admesh library for STL reading, Eigen for vector types.

namespace Slic3r {

// [INTENT] Stores sampled surface data: positions, normals, and source triangle indices.
// [MEMORY] Vectors allocated dynamically - size proportional to samples_count.
struct TriangleSetSamples
{
    // Total surface area of the mesh being sampled.
    float total_area;
    // Sampled positions in 3D space.
    std::vector<Vec3f> positions;
    // Surface normals at each sampled position.
    std::vector<Vec3f> normals;
    // Source triangle index for each sample (for debugging/mapping).
    std::vector<size_t> triangle_indices;
};

// [INTENT] Generate uniform random samples across triangle mesh surface.
// Uses area-weighted probability for truly uniform distribution.
// [MEMORY] Returns TriangleSetSamples by value - vectors contain samples_count entries.
// [CONCURRENCY] Implementation likely uses parallel processing (_parallel suffix).
TriangleSetSamples sample_its_uniform_parallel(size_t samples_count, const indexed_triangle_set& triangle_set);

} // namespace Slic3r

#endif /* SRC_LIBSLIC3R_TRIANGLESETSAMPLING_HPP_ */
