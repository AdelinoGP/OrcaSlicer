// [INTENT] RasterToPolygons.hpp — Converts a rasterized SLA layer image back into
// ExPolygons using the Marching Squares algorithm. This is used to verify rasterization
// fidelity and to generate the actual boundary geometry for pad/support computation
// at layers where the vector polygon data was not retained.
//
// [COUPLING] Depends on `RasterGrayscaleAA` — a concrete type from AGGRaster.hpp.
//   The `windowsize` parameter controls the marching-squares grid cell size (in pixels).
//   Default {1,1} = per-pixel resolution; larger values trade accuracy for speed.
//
// [MEMORY] Returns a newly allocated ExPolygons vector. No persistent state.
// [CONCURRENCY] Stateless — safe to call concurrently on separate rasters.

#ifndef RASTERTOPOLYGONS_HPP
#define RASTERTOPOLYGONS_HPP

#include "libslic3r/ExPolygon.hpp"

namespace Slic3r { namespace sla {

class RasterGrayscaleAA;

// [INTENT] Reconstruct ExPolygons from the rasterized pixel buffer.
// `windowsize` = marching-squares window in pixels; {1,1} = full resolution.
// Inverse transform (mirror, flipXY, center) is applied to the reconstructed polygons
// to undo the raster coordinate transform and return world-space polygons.
ExPolygons raster_to_polygons(const RasterGrayscaleAA& rst, Vec2i32 windowsize = {1, 1});

}} // namespace Slic3r::sla

#endif // RASTERTOPOLYGONS_HPP
