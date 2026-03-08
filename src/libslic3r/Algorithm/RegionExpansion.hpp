// [INTENT] RegionExpansion.hpp — public API for the wave-propagation region-expansion algorithm.
// This module expands "source" ExPolygon regions into adjacent "boundary" regions via
// iterated ClipperOffset inflation (wave propagation). Primary use case: expanding the
// perimeter-infill seam bridge anchors into the solid infill region, and expanding
// support anchors into the print bed or printed part surface.
//
// [COUPLING] Depends on: Point.hpp (coord_t, Points), Polygon.hpp, ExPolygon.hpp.
// All coordinates are in scaled integer units (coord_t, 1e6 per mm).
//
// [CONCURRENCY] All exported functions are stateless pure functions (no global state).
// Individual calls are safe to run from different threads; however the RegionExpansionParameters
// struct is passed by value/const-ref so there is no shared mutable state.

#ifndef SRC_LIBSLIC3R_ALGORITHM_REGION_EXPANSION_HPP_
#define SRC_LIBSLIC3R_ALGORITHM_REGION_EXPANSION_HPP_

#include <cstdint>
#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>

namespace Slic3r { namespace Algorithm {

// [INTENT] RegionExpansionParameters: tuning knobs for the wave propagation.
// Constructed via the static `build()` factory, which computes all derived fields
// (num_other_steps, initial_step, max_inflation, arc_tolerance, shortest_edge_length)
// from three user-facing scalars.
//
// [HAZARD H669] All float fields (`tiny_expansion`, `initial_step`, `other_step`,
// `max_inflation`) are in *scaled* coord_t units (mm × 1e6), NOT in millimetres.
// Callers that pass mm values directly will expand by 1e6× less than intended,
// producing near-zero-width seeds and no visible expansion. The type is `float`
// not `coord_t`, making the unit invisible to the compiler.
struct RegionExpansionParameters
{
    // Initial expansion of src to make the source regions intersect with boundary regions just a bit.
    float tiny_expansion;
    // How much to inflate the seed lines to produce the first wave area.
    float initial_step;
    // How much to inflate the first wave area and the successive wave areas in each step.
    float other_step;
    // Number of inflate steps after the initial step.
    size_t num_other_steps;
    // Maximum inflation of seed contours over the boundary. Used to trim boundary to speed up
    // clipping during wave propagation.
    float max_inflation;

    // Accuracy of the offsetter for wave propagation.
    double arc_tolerance;
    double shortest_edge_length;

    // [INTENT] Factory: compute all parameters from three user-facing scalars.
    // full_expansion: total desired expansion in scaled units.
    // expansion_step: maximum per-step inflation (smaller → more steps, smoother wavefront).
    // max_nr_expansion_steps: cap on number of steps to prevent infinite loops for tiny step sizes.
    // [HAZARD H670] If full_expansion <= tiny_expansion (e.g. very small expansion), nsteps = 0
    // after ceil(), and the assert(nsteps > 0) fires in debug. In release it proceeds with
    // nsteps==0, setting initial_step = (full_expansion - tiny_expansion) / 0 → NaN or Inf.
    // Callers must ensure full_expansion > scaled<float>(0.05f) * 1.25 for non-degenerate results.
    static RegionExpansionParameters build(
        // Scaled expansion value
        float full_expansion,
        // Expand by waves of expansion_step size (expansion_step is scaled).
        float expansion_step,
        // Don't take more than max_nr_steps for small expansion_step.
        size_t max_nr_expansion_steps);
};

// [INTENT] WaveSeed: a single seed polyline (open or closed) annotated with which
// source ExPolygon and which boundary ExPolygon it belongs to.
// `path` is a plain 2D polyline in scaled integer coordinates.
// `src` is the index into the `src` ExPolygons array.
// `boundary` is the index into the `boundary` ExPolygons array.
// [COUPLING] WaveSeeds are produced by wave_seeds() and consumed by propagate_waves().
// The src/boundary indices are used to look up the correct ExPolygon for clipping.
struct WaveSeed
{
    uint32_t src;
    uint32_t boundary;
    Points   path;
};
using WaveSeeds = std::vector<WaveSeed>;

// [INTENT] Comparators for sorting WaveSeeds. Two orderings are provided:
//   lower_by_boundary_and_src: used when iterating by boundary in propagate_waves()
//   lower_by_src_and_boundary: used when iterating by source in expand_expolygons()
// [HAZARD H671] These comparators are strict weak orderings but NOT stable — equal
// {boundary, src} seeds may appear in any relative order. If two seeds for the same
// (src, boundary) pair produce overlapping paths, the order they are fed to
// ClipperOffset affects the final union result. The algorithm unions them together,
// so the final polygon should be the same, but intermediate floating-point rounding
// in ClipperOffset can differ per order.
inline bool lower_by_boundary_and_src(const WaveSeed& l, const WaveSeed& r)
{
    return l.boundary < r.boundary || (l.boundary == r.boundary && l.src < r.src);
}

inline bool lower_by_src_and_boundary(const WaveSeed& l, const WaveSeed& r)
{
    return l.src < r.src || (l.src == r.src && l.boundary < r.boundary);
}

// [INTENT] wave_seeds: expand `src` ExPolygons outward by `tiny_expansion` to guarantee
// intersection with `boundary`, clip the expanded outlines against `boundary`, and
// return the clipped polylines annotated with their (src, boundary) indices.
// If sorted=true, result is sorted by (boundary, src) for use in propagate_waves().
// [HAZARD H672] tiny_expansion is in scaled units (see H669). The function asserts
// tiny_expansion > 0 but does not verify the value is sane relative to the geometry.
// Too-large tiny_expansion will cause seeds to cross multiple boundary regions,
// potentially producing WaveSeeds with incorrect boundary attribution.
WaveSeeds wave_seeds(
    // Source regions that are supposed to touch the boundary.
    const ExPolygons& src,
    // Boundaries of source regions touching the "boundary" regions will be expanded into the "boundary" region.
    const ExPolygons& boundary,
    // Initial expansion of src to make the source regions intersect with boundary regions just a bit.
    float tiny_expansion,
    bool  sorted);

// [INTENT] RegionExpansion: a single expanded polygon fragment, annotated with its
// originating (src_id, boundary_id) pair.
// A single (src, boundary) pair may produce multiple RegionExpansion fragments if
// the wavefront wraps around a hole or splits around an obstacle.
struct RegionExpansion
{
    Polygon  polygon;
    uint32_t src_id;
    uint32_t boundary_id;
};

// [INTENT] propagate_waves overloads: drive the wave propagation.
// Seeds are iterated grouped by (boundary, src); for each group the seed polylines
// are inflated step-by-step via ClipperOffset and clipped to the boundary ExPolygon.
// [HAZARD H673] propagate_waves(seeds, ...) requires seeds to be sorted by
// (boundary, src) — it uses a linear group-scan not random access. Passing unsorted
// seeds will silently merge incorrect seed groups and produce wrong expansion polygons.
std::vector<RegionExpansion> propagate_waves(const WaveSeeds& seeds, const ExPolygons& boundary, const RegionExpansionParameters& params);
std::vector<RegionExpansion> propagate_waves(const ExPolygons& src, const ExPolygons& boundary, const RegionExpansionParameters& params);

std::vector<RegionExpansion> propagate_waves(const ExPolygons& src,
                                             const ExPolygons& boundary,
                                             // Scaled expansion value
                                             float expansion,
                                             // Expand by waves of expansion_step size (expansion_step is scaled).
                                             float expansion_step,
                                             // Don't take more than max_nr_steps for small expansion_step.
                                             size_t max_nr_steps);

// [INTENT] RegionExpansionEx: same as RegionExpansion but carries a full ExPolygon
// (with holes) rather than a bare Polygon. Produced by propagate_waves_ex which
// unions per-(src,boundary) polygon fragments and converts to ExPolygons.
struct RegionExpansionEx
{
    ExPolygon expolygon;
    uint32_t  src_id;
    uint32_t  boundary_id;
};

std::vector<RegionExpansionEx> propagate_waves_ex(const WaveSeeds&                 seeds,
                                                  const ExPolygons&                boundary,
                                                  const RegionExpansionParameters& params);

std::vector<RegionExpansionEx> propagate_waves_ex(const ExPolygons& src,
                                                  const ExPolygons& boundary,
                                                  // Scaled expansion value
                                                  float expansion,
                                                  // Expand by waves of expansion_step size (expansion_step is scaled).
                                                  float expansion_step,
                                                  // Don't take more than max_nr_steps for small expansion_step.
                                                  size_t max_nr_steps);

// [INTENT] expand_expolygons: convenience wrapper. Returns a vector of Polygons per
// source ExPolygon (indexed by src position in src[]), containing all polygon fragments
// that the wave expanded into boundary.
std::vector<Polygons> expand_expolygons(const ExPolygons& src,
                                        const ExPolygons& boundary,
                                        // Scaled expansion value
                                        float expansion,
                                        // Expand by waves of expansion_step size (expansion_step is scaled).
                                        float expansion_step,
                                        // Don't take more than max_nr_steps for small expansion_step.
                                        size_t max_nr_steps);

// [INTENT] merge_expansions_into_expolygons: merge the expanded polygon fragments back
// into the original source ExPolygons by taking the union (via union_safety_offset_ex).
// Both src and expanded are consumed (moved from).
// [HAZARD H674] If union_safety_offset_ex produces more than 1 ExPolygon for a single
// source ExPolygon, the code falls back to picking the one that contains a sample point
// from the original source contour. If the sample point lies in a hole of the merged
// result, sample_in_expolygons returns -1 and the assert fires (debug) or the source
// ExPolygon is silently omitted from the output (release: id == -1 branch skips emplace_back).
std::vector<ExPolygon> merge_expansions_into_expolygons(ExPolygons&& src, std::vector<RegionExpansion>&& expanded);

// [INTENT] expand_merge_expolygons: convenience wrapper for the complete pipeline:
// propagate_waves() then merge_expansions_into_expolygons(). src is moved from.
std::vector<ExPolygon> expand_merge_expolygons(ExPolygons&& src, const ExPolygons& boundary, const RegionExpansionParameters& params);

}} // namespace Slic3r::Algorithm

#endif /* SRC_LIBSLIC3R_ALGORITHM_REGION_EXPANSION_HPP_ */
