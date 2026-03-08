// [INTENT] RegionExpansion.cpp — implementation of the wave-propagation expansion algorithm.
// Expands src ExPolygon boundaries into adjacent boundary ExPolygons via iterated
// ClipperOffset inflation, using ClipperLib_Z to tag seed segments with (src, boundary)
// indices so that multiple waves can be processed in one pass.
//
// High-level pipeline:
//   wave_seeds()      → clip tiny-expanded src outlines against boundary → tagged seeds
//   propagate_waves() → for each (boundary, src) group, inflate seed paths step by step
//   merge_expansions_into_expolygons() → union expanded fragments back into src ExPolygons
//
// [MEMORY] Heavy use of ClipperLib_Z::Paths (vector<vector<IntPoint3>>). All intermediate
// paths are stack-allocated vectors of heap-allocated paths — no shared ownership.
// ExPolygons passed by const-ref; src passed by value (moved) in merge functions.
//
// [CONCURRENCY] No global mutable state. Individual function calls are re-entrant.
// However ClipperLib::ClipperOffset (co) is created per propagate_wave_from_boundary call
// and not shared across threads — safe.

#include "RegionExpansion.hpp"

#include <libslic3r/AABBTreeIndirect.hpp>
#include <libslic3r/ClipperZUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Utils.hpp>

#include <numeric>

namespace Slic3r { namespace Algorithm {

// [INTENT] clipper_round_offset_error: estimate the maximum radial error introduced
// by ClipperOffset when offsetting by `offset` with a given `arc_tolerance`.
// This mirrors the internal discretization formula in ClipperOffset::DoOffset().
// Used only in the commented-out max_inflation formula (currently dead code).
// [HAZARD H675] This function is referenced in a commented-out line (line 71 of
// the original). The currently-active max_inflation formula uses a simpler 1.1×
// multiplier. If the arc-error formula is ever uncommented, it may produce a
// significantly smaller max_inflation than the 1.1× formula for large offsets,
// causing boundary clipping to trim too aggressively and producing truncated waves.
inline double clipper_round_offset_error(double offset, double arc_tolerance)
{
    static constexpr const double def_arc_tolerance = 0.25;
    const double                  y                 = arc_tolerance <= 0                         ? def_arc_tolerance :
                                                      arc_tolerance > offset * def_arc_tolerance ? offset * def_arc_tolerance :
                                                                                                   arc_tolerance;
    double                        steps             = std::min(M_PI / std::acos(1. - y / offset), offset * M_PI);
    return offset * (1. - cos(M_PI / steps));
}

// [INTENT] RegionExpansionParameters::build: compute all wave parameters from
// user-facing scalars. The function adjusts step sizes so that:
//   - tiny_expansion ≤ 25% of full_expansion and ≤ 50 µm (scale_(0.05))
//   - initial_step ≥ 4 × tiny_expansion (prevents cusps at boundary entry)
//   - max_inflation = 1.1 × (tiny_expansion + nsteps × initial_step)
//     (adds 10% margin for ClipperOffset rounding errors)
//
// [STATE] All output fields set here; none modified after return.
// [HAZARD H670] If full_expansion ≤ tiny_expansion after the min() computation,
// (full_expansion - tiny_expansion) ≤ 0 and ceil() of that / expansion_step = 0.
// The assert(nsteps > 0) fires in debug; in release nsteps = 0 → NaN field values.
RegionExpansionParameters RegionExpansionParameters::build(
    // Scaled expansion value
    float full_expansion,
    // Expand by waves of expansion_step size (expansion_step is scaled).
    float expansion_step,
    // Don't take more than max_nr_steps for small expansion_step.
    size_t max_nr_expansion_steps)
{
    assert(full_expansion > 0);
    assert(expansion_step > 0);
    assert(max_nr_expansion_steps > 0);

    RegionExpansionParameters out;
    // Initial expansion of src to make the source regions intersect with boundary regions just a bit.
    // The expansion should not be too tiny, but also small enough, so the following expansion will
    // compensate for tiny_expansion and bring the wave back to the boundary without producing
    // ugly cusps where it touches the boundary.
    out.tiny_expansion = std::min(0.25f * full_expansion, scaled<float>(0.05f));
    size_t nsteps      = size_t(ceil((full_expansion - out.tiny_expansion) / expansion_step));
    if (max_nr_expansion_steps > 0)
        nsteps = std::min(nsteps, max_nr_expansion_steps);
    assert(nsteps > 0);
    out.initial_step = (full_expansion - out.tiny_expansion) / nsteps;
    if (nsteps > 1 && 0.25 * out.initial_step < out.tiny_expansion) {
        // Decrease the step size by lowering number of steps.
        nsteps           = std::max<size_t>(1, (floor((full_expansion - out.tiny_expansion) / (4. * out.tiny_expansion))));
        out.initial_step = (full_expansion - out.tiny_expansion) / nsteps;
    }
    if (0.25 * out.initial_step < out.tiny_expansion || nsteps == 1) {
        // [INTENT] Fallback for very small or single-step expansions: use fixed 20%/80% split.
        out.tiny_expansion = 0.2f * full_expansion;
        out.initial_step   = 0.8f * full_expansion;
    }
    out.other_step      = out.initial_step;
    out.num_other_steps = nsteps - 1;

    // Accuracy of the offsetter for wave propagation.
    out.arc_tolerance        = scaled<double>(0.1);
    out.shortest_edge_length = out.initial_step * ClipperOffsetShortestEdgeFactor;

    // Maximum inflation of seed contours over the boundary. Used to trim boundary to speed up
    // clipping during wave propagation. Needs to be in sync with the offsetter accuracy.
    // Clipper positive round offset should rather offset less than more.
    // Still a little bit of additional offset was added.
    // [INTENT] 1.1× safety margin absorbs ClipperOffset rounding error.
    // [SEE H675] alternate arc-error formula is commented out below.
    out.max_inflation = (out.tiny_expansion + nsteps * out.initial_step) * 1.1;
    //                (clipper_round_offset_error(out.tiny_expansion, co.ArcTolerance) + nsteps *
    //                clipper_round_offset_error(out.initial_step, co.ArcTolerance) * 1.5; // Account for uncertainty

    return out;
}

// [INTENT] expolygons_to_zpaths_expanded_opened: for each contour/hole of each ExPolygon
// in src, offset the contour outward by `expansion` (or inward for holes), then convert
// to a ZPath with base_idx as Z tag, and "open" it by appending a duplicate of the
// first point at the end.
//
// Opening the path is necessary because Clipper treats closed polygon subjects
// differently from open polyline subjects. By making the path open-but-self-closing,
// the intersection with boundary regions correctly produces split endpoints.
//
// base_idx is incremented by 1 per ExPolygon (not per contour). This groups all
// contours of a single ExPolygon under the same base Z index.
//
// [HAZARD H676] offset sign convention: contours (icontour==0) are expanded by
// +expansion; holes (icontour>0) are contracted by -expansion. The comment notes
// that ClipperOffset reorients contours CCW before offsetting, reversing the sign.
// If an ExPolygon has an improperly wound hole (CCW instead of CW), the sign is
// still applied based on icontour index, not winding — the hole gets +expansion
// (expanded outward) rather than -expansion, potentially breaking the seed boundary.
//
// [HAZARD H677] base_idx is a coord_t reference incremented by 1 per ExPolygon.
// It starts at idx_boundary_end in wave_seeds(). If src has more ExPolygons than
// (numeric_limits<coord_t>::max() - idx_boundary_end), base_idx overflows silently.
// In practice, src sizes are small; this is a theoretical concern for very large models.
static ClipperLib_Z::Paths expolygons_to_zpaths_expanded_opened(const ExPolygons& src, const float expansion, coord_t& base_idx)
{
    ClipperLib_Z::Paths out;
    out.reserve(2 * std::accumulate(src.begin(), src.end(), size_t(0),
                                    [](const size_t acc, const ExPolygon& expoly) { return acc + expoly.num_contours(); }));
    ClipperLib::ClipperOffset offsetter;
    offsetter.ShortestEdgeLength = expansion * ClipperOffsetShortestEdgeFactor;
    ClipperLib::Paths expansion_cache;
    for (const ExPolygon& expoly : src) {
        for (size_t icontour = 0; icontour < expoly.num_contours(); ++icontour) {
            // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
            // contours will be CCW oriented even though the input paths are CW oriented.
            // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
            offsetter.Clear();
            offsetter.AddPath(expoly.contour_or_hole(icontour).points, ClipperLib::jtSquare, ClipperLib::etClosedPolygon);
            expansion_cache.clear();
            offsetter.Execute(expansion_cache, icontour == 0 ? expansion : -expansion);
            // [INTENT] to_zpaths<true> = "open" mode: appends first point at end to open the closed polygon.
            append(out, ClipperZUtils::to_zpaths<true>(expansion_cache, base_idx));
        }
        // [INTENT] base_idx increments once per ExPolygon (not per contour) — all
        // contours of the same ExPolygon share the same Z tag.
        ++base_idx;
    }
    return out;
}

// [INTENT] merge_splits: after clipping, some expanded-and-opened polylines may be
// split at their start/end points (which are the same, since the source was a closed
// polygon converted to an open polyline by duplicating the first point). This function
// reconnects those split pieces by matching endpoints via the `splits` lookup table.
//
// `splits` maps IntPoint (the duplicated start/end coordinate) → index of the other
// half of the split in `paths`. Initially all entries have second == -1 (unmatched).
// When both halves are found, they are joined via polylines_merge() and one is erased.
//
// [HAZARD H678] polylines_merge() is called with `std::move(path)`, which leaves
// `path` (in the original paths vector) in a moved-from state. The function immediately
// erases or swaps it with paths.back(). If paths.back() is the same element that was
// moved from (i.e. end->second == int(paths.end()-1)), the swap copies a moved-from
// vector, which is safe (empty) but may cause an extra empty path entry if the size
// check is skipped. In practice Clipper should not produce self-adjacent split pairs.
//
// [HAZARD H679] The `splits` vector is sorted by coordinate (zpoint_lower). If two
// separate source ExPolygon contours happen to share the exact same expanded start
// coordinate (coordinate collision after ClipperOffset), their split entries will be
// lexicographically equal and lower_bound will pick one arbitrarily — potentially
// merging two unrelated pieces.
static inline void merge_splits(ClipperLib_Z::Paths& paths, std::vector<std::pair<ClipperLib_Z::IntPoint, int>>& splits)
{
    for (auto it_path = paths.begin(); it_path != paths.end();) {
        ClipperLib_Z::Path& path = *it_path;
        assert(path.size() >= 2);
        bool merged = false;
        if (path.size() >= 2) {
            const ClipperLib_Z::IntPoint& front = path.front();
            const ClipperLib_Z::IntPoint& back  = path.back();
            // The path before clipping was supposed to cross the clipping boundary or be fully out of it.
            // Thus the clipped contour is supposed to become open, with one exception: The anchor expands into a closed hole.
            if (front.x() != back.x() || front.y() != back.y()) {
                // Look up the ends in "splits", possibly join the contours.
                // "splits" maps into the other piece connected to the same end point.
                auto find_end = [&splits](const ClipperLib_Z::IntPoint& pt) -> std::pair<ClipperLib_Z::IntPoint, int>* {
                    auto it = std::lower_bound(splits.begin(), splits.end(), pt,
                                               [](const auto& l, const auto& r) { return ClipperZUtils::zpoint_lower(l.first, r); });
                    return it != splits.end() && it->first == pt ? &(*it) : nullptr;
                };
                auto* end       = find_end(front);
                bool  end_front = true;
                if (!end) {
                    end_front = false;
                    end       = find_end(back);
                }
                if (end) {
                    // This segment ends at a split point of the source closed contour before clipping.
                    if (end->second == -1) {
                        // Open end was found, not matched yet.
                        end->second = int(it_path - paths.begin());
                    } else {
                        // Open end was found and matched with end->second
                        ClipperLib_Z::Path& other_path = paths[end->second];
                        polylines_merge(other_path, other_path.front() == end->first, std::move(path), end_front);
                        if (std::next(it_path) == paths.end()) {
                            paths.pop_back();
                            break;
                        }
                        path = std::move(paths.back());
                        paths.pop_back();
                        merged = true;
                    }
                }
            }
        }
        if (!merged)
            ++it_path;
    }
}

// [INTENT] AABBTreeBBoxes: 2D AABB tree over coord_t bounding boxes of ExPolygon contours.
// Used for fast point-in-expolygon lookup via bounding box pre-filter.
using AABBTreeBBoxes = AABBTreeIndirect::Tree<2, coord_t>;

// [INTENT] build_aabb_tree_over_expolygons: build an AABB tree over the bounding boxes
// of each ExPolygon's contour. Used in wave_seeds() to classify closed seeds by their
// containing boundary region, and in merge_expansions_into_expolygons() for sample-point
// containment lookup.
// [HAZARD H680] The AABB tree indexes only the CONTOUR bounding box (not the full
// ExPolygon with holes). A sample point inside a hole of an ExPolygon will pass the
// bounding box filter and reach sample_in_expolygons() → ExPolygon::contains(), which
// correctly rejects it. However if two ExPolygons have overlapping contour bboxes,
// both will be traversed and the first match is returned. This is correct but means
// the tree does NOT guarantee a unique result for overlapping ExPolygons.
static AABBTreeBBoxes build_aabb_tree_over_expolygons(const ExPolygons& expolygons)
{
    // Calculate bounding boxes of internal slices.
    std::vector<AABBTreeIndirect::BoundingBoxWrapper> bboxes;
    bboxes.reserve(expolygons.size());
    for (size_t i = 0; i < expolygons.size(); ++i)
        bboxes.emplace_back(i, get_extents(expolygons[i].contour));
    // Build AABB tree over bounding boxes of boundary expolygons.
    AABBTreeBBoxes out;
    out.build_modify_input(bboxes);
    return out;
}

// [INTENT] sample_in_expolygons: find the index of the first ExPolygon (in expolygons[])
// that contains the sample point, using the AABB tree for bounding-box pre-filtering.
// Returns -1 if no containing ExPolygon is found.
// [HAZARD H681] If multiple ExPolygons contain the sample (overlapping regions), only
// the first hit encountered in AABB traversal order is returned. The traversal order
// is determined by the AABB tree construction (modified input — not guaranteed stable).
// Callers that expect a deterministic choice between overlapping regions will see
// non-deterministic results depending on geometry layout.
static int sample_in_expolygons(
    // AABB tree over boundary expolygons
    const AABBTreeBBoxes& aabb_tree,
    const ExPolygons&     expolygons,
    const Point&          sample)
{
    int out = -1;
    AABBTreeIndirect::traverse(
        aabb_tree, [&sample](const AABBTreeBBoxes::Node& node) { return node.bbox.contains(sample); },
        [&expolygons, &sample, &out](const AABBTreeBBoxes::Node& node) {
            assert(node.is_leaf());
            assert(node.is_valid());
            if (expolygons[node.idx].contains(sample)) {
                out = int(node.idx);
                // Stop traversal.
                return false;
            }
            // Continue traversal.
            return true;
        });
    return out;
}

// [INTENT] wave_seeds: the first stage of the expansion pipeline.
// 1. Expands each src ExPolygon outward by tiny_expansion (using jtSquare offset).
//    The expanded contours are "opened" (first==last point) so they become open polylines.
// 2. Uses ClipperLib_Z intersection to clip these open polylines against the boundary
//    ExPolygons. Each intersection point is tagged with a negative Z that encodes the
//    two Z values of the intersecting edges (via ClipperZIntersectionVisitor).
// 3. Reconnects split pieces via merge_splits().
// 4. Classifies each clipped segment by (src_id, boundary_id) from the Z tags.
//    Closed seeds (fully inside a boundary, no intersection with boundary edge) are
//    classified via AABB tree point-in-boundary lookup.
//
// [STATE] `aabb_tree` is lazily constructed on first closed-seed encounter.
// [COUPLING] Z encoding in ClipperZIntersectionVisitor (ClipperZUtils.hpp) must be
// consistent with the decoding in wave_seeds() (intersections[-z-1] lookup).
std::vector<WaveSeed> wave_seeds(
    // Source regions that are supposed to touch the boundary.
    const ExPolygons& src,
    // Boundaries of source regions touching the "boundary" regions will be expanded into the "boundary" region.
    const ExPolygons& boundary,
    // Initial expansion of src to make the source regions intersect with boundary regions just a bit.
    float tiny_expansion,
    // Sort output by boundary ID and source ID.
    bool sorted)
{
    assert(tiny_expansion > 0);

    if (src.empty() || boundary.empty())
        return {};

    using Intersection  = ClipperZUtils::ClipperZIntersectionVisitor::Intersection;
    using Intersections = ClipperZUtils::ClipperZIntersectionVisitor::Intersections;

    ClipperLib_Z::Paths segments;
    Intersections       intersections;

    // [INTENT] Z index ranges:
    //   [1, idx_boundary_end)  → boundary ExPolygon contours (1-based)
    //   [idx_boundary_end, idx_src_end) → src ExPolygon contours (after expansion)
    //   negative Z → intersection points (decoded via intersections[])
    coord_t idx_boundary_begin = 1;
    coord_t idx_boundary_end   = idx_boundary_begin;
    coord_t idx_src_end;

    {
        ClipperLib_Z::Clipper                      zclipper;
        ClipperZUtils::ClipperZIntersectionVisitor visitor(intersections);
        zclipper.ZFillFunction(visitor.clipper_callback());
        // as closed contours
        zclipper.AddPaths(ClipperZUtils::expolygons_to_zpaths(boundary, idx_boundary_end), ClipperLib_Z::ptClip, true);
        // as open contours
        std::vector<std::pair<ClipperLib_Z::IntPoint, int>> zsrc_splits;
        {
            idx_src_end              = idx_boundary_end;
            ClipperLib_Z::Paths zsrc = expolygons_to_zpaths_expanded_opened(src, tiny_expansion, idx_src_end);
            zclipper.AddPaths(zsrc, ClipperLib_Z::ptSubject, false);
            // [INTENT] Record the "split points" — the duplicated first point of each
            // opened path. These are where the reconnection in merge_splits() will join
            // the two halves of each clipped contour.
            zsrc_splits.reserve(zsrc.size());
            for (const ClipperLib_Z::Path& path : zsrc) {
                assert(path.size() >= 2);
                assert(path.front() == path.back());
                zsrc_splits.emplace_back(path.front(), -1);
            }
            std::sort(zsrc_splits.begin(), zsrc_splits.end(),
                      [](const auto& l, const auto& r) { return ClipperZUtils::zpoint_lower(l.first, r.first); });
        }
        ClipperLib_Z::PolyTree polytree;
        zclipper.Execute(ClipperLib_Z::ctIntersection, polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
        ClipperLib_Z::PolyTreeToPaths(std::move(polytree), segments);
        merge_splits(segments, zsrc_splits);
    }

    // [INTENT] AABBTree over boundary bboxes — lazily built only for closed seeds.
    AABBTreeBBoxes aabb_tree;

    // Sort paths into their respective islands.
    // Each src x boundary will be processed (wave expanded) independently.
    // Multiple pieces of a single src may intersect the same boundary.
    WaveSeeds out;
    out.reserve(segments.size());
    int iseed = 0;
    for (const ClipperLib_Z::Path& path : segments) {
        assert(path.size() >= 2);
        ClipperLib_Z::IntPoint front = path.front();
        ClipperLib_Z::IntPoint back  = path.back();
        // Both ends of a seed segment are supposed to be inside a single boundary expolygon.
        // Thus as long as the seed contour is not closed, it should be open at a boundary point.
        assert((front == back && front.z() >= idx_boundary_end && front.z() < idx_src_end) ||
               //(front.z() < 0 && back.z() < 0));
               // Hope that at least one end of an open polyline is clipped by the boundary, thus an intersection point is created.
               (front.z() < 0 || back.z() < 0));

        if (front != back && front.z() >= 0 && back.z() >= 0) {
            // [INTENT] Very rare case: both endpoints coincide with existing boundary vertices
            // → ZFillFunction was not called for either end → no intersection Z tag.
            // Cannot determine which boundary region this seed belongs to → skip.
            // [HAZARD H682] This skip silently drops a seed that genuinely intersects the
            // boundary. The affected src ExPolygon will not be expanded at all for this seed.
            // Very rare in practice (exact vertex coincidence after float→coord_t rounding).
            continue;
        } else if (front == back && (front.z() < idx_boundary_end)) {
            // This should be a very rare exception.
            // See https://github.com/prusa3d/PrusaSlicer/issues/12469.
            // Segement is open, yet its first point seems to be part of boundary polygon.
            // Take the first point with src polygon index.
            for (const ClipperLib_Z::IntPoint& point : path) {
                if (point.z() >= idx_boundary_end) {
                    front = point;
                    back  = point;
                }
            }
        }

        const Intersection* intersection             = nullptr;
        auto                intersection_point_valid = [idx_boundary_end, idx_src_end](const Intersection& is) {
            return is.first >= 1 && is.first < idx_boundary_end && is.second >= idx_boundary_end && is.second < idx_src_end;
        };
        if (front.z() < 0) {
            // [INTENT] front is an intersection point. Decode via intersections[].
            // first = boundary Z (1-based), second = src Z.
            const Intersection& is = intersections[-front.z() - 1];
            assert(intersection_point_valid(is));
            if (intersection_point_valid(is))
                intersection = &is;
        }
        if (!intersection && back.z() < 0) {
            const Intersection& is = intersections[-back.z() - 1];
            assert(intersection_point_valid(is));
            if (intersection_point_valid(is))
                intersection = &is;
        }
        if (intersection) {
            // The path intersects the boundary contour at least at one side.
            // [INTENT] intersection->first is the boundary Z (1-based → subtract 1 for 0-based index).
            // intersection->second is the src Z (subtract idx_boundary_end for 0-based src index).
            out.push_back(
                {uint32_t(intersection->second - idx_boundary_end), uint32_t(intersection->first - 1), ClipperZUtils::from_zpath(path)});
        } else {
            // This should be a closed contour.
            assert(front == back && front.z() >= idx_boundary_end && front.z() < idx_src_end);
            // Find a source boundary expolygon of one sample of this closed path.
            // [INTENT] Closed seed: fully inside a boundary region. Use front coordinate
            // as the sample point for AABB-tree containment lookup.
            if (aabb_tree.empty())
                aabb_tree = build_aabb_tree_over_expolygons(boundary);
            int boundary_id = sample_in_expolygons(aabb_tree, boundary, Point(front.x(), front.y()));
            // Boundary that contains the sample point was found.
            assert(boundary_id >= 0);
            if (boundary_id >= 0)
                out.push_back({uint32_t(front.z() - idx_boundary_end), uint32_t(boundary_id), ClipperZUtils::from_zpath(path)});
        }
        ++iseed;
    }

    if (sorted)
        // Sort the seeds by their intersection boundary and source contour.
        std::sort(out.begin(), out.end(), lower_by_boundary_and_src);
    return out;
}

// [INTENT] wavefront_initial: inflate seed polylines (open or closed) by `offset` using
// ClipperOffset with round joins and open-round end caps (for open paths) or closed-line
// end caps (for closed paths). Each path is inflated independently (co.Clear per path).
// [HAZARD H683] Inflating each path independently (Clear per iteration) means that
// ClipperOffset cannot detect and merge overlapping outputs from adjacent seed paths.
// For densely packed seeds, the union of individual inflations may self-intersect.
// The subsequent wavefront_clip() call resolves this by intersecting with the boundary,
// effectively performing the union. However the ClipperOffset step may generate
// slightly different output than a single multi-path inflation would.
static ClipperLib::Paths wavefront_initial(ClipperLib::ClipperOffset& co, const ClipperLib::Paths& polylines, float offset)
{
    ClipperLib::Paths out;
    out.reserve(polylines.size());
    ClipperLib::Paths out_this;
    for (const ClipperLib::Path& path : polylines) {
        assert(path.size() >= 2);
        co.Clear();
        co.AddPath(path, jtRound, path.front() == path.back() ? ClipperLib::etClosedLine : ClipperLib::etOpenRound);
        co.Execute(out_this, offset);
        append(out, std::move(out_this));
    }
    return out;
}

// [INTENT] wavefront_step: inflate an existing wavefront polygon by `offset`.
// Uses jtRound join and etClosedPolygon end type.
// Critical subtlety: ClipperOffset reorients all paths CCW before offsetting.
// CW-wound polygons (holes) are detected via ClipperLib::Orientation() and their
// offset sign is negated, then the output is reversed back to CW orientation.
// This ensures holes contract inward while contours expand outward.
//
// [HAZARD H684] Orientation() checks winding by signed area formula. For very thin
// slivers (nearly degenerate polygons), the signed area may be near-zero and the
// orientation test may return the wrong value due to floating-point rounding in
// Clipper's integer arithmetic. A misidentified CW polygon gets expanded instead of
// contracted, causing a spurious filled region inside the wavefront.
static ClipperLib::Paths wavefront_step(ClipperLib::ClipperOffset& co, const ClipperLib::Paths& polygons, float offset)
{
    ClipperLib::Paths out;
    out.reserve(polygons.size());
    ClipperLib::Paths out_this;
    for (const ClipperLib::Path& polygon : polygons) {
        co.Clear();
        // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
        // contours will be CCW oriented even though the input paths are CW oriented.
        // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
        co.AddPath(polygon, jtRound, ClipperLib::etClosedPolygon);
        bool ccw = ClipperLib::Orientation(polygon);
        co.Execute(out_this, ccw ? offset : -offset);
        if (!ccw) {
            // Reverse the resulting contours.
            for (ClipperLib::Path& path : out_this)
                std::reverse(path.begin(), path.end());
        }
        append(out, std::move(out_this));
    }
    return out;
}

// [INTENT] wavefront_clip: intersect the inflated wavefront with the clipping region
// (pre-trimmed boundary polygons). Uses pftPositive fill rule — wavefront polygons
// that have been unioned before clipping will correctly remain unioned in the output.
// [HAZARD H685] pftPositive fill rule requires correctly wound polygons (CCW outer,
// CW holes). If wavefront_step() produced CW outer contours (see H684), pftPositive
// will treat them as holes and the intersection will return empty. The bug would
// manifest as sudden gaps in the expanded region at a specific wave step.
static ClipperLib::Paths wavefront_clip(const ClipperLib::Paths& wavefront, const Polygons& clipping)
{
    ClipperLib::Clipper clipper;
    clipper.AddPaths(wavefront, ClipperLib::ptSubject, true);
    clipper.AddPaths(ClipperUtils::PolygonsProvider(clipping), ClipperLib::ptClip, true);
    ClipperLib::Paths out;
    clipper.Execute(ClipperLib::ctIntersection, out, ClipperLib::pftPositive, ClipperLib::pftPositive);
    return out;
}

// [INTENT] propagate_wave_from_boundary: run the complete per-(src,boundary) wave
// propagation loop for one group of seed paths within one boundary ExPolygon.
//
// Steps:
//   1. Trim boundary to a bbox around the seed paths inflated by max_inflation (perf opt).
//   2. Initial inflation: wavefront_initial() → wavefront_clip() with clipping region.
//   3. Successive inflations: num_other_steps × (wavefront_step() → wavefront_clip()).
//   4. Convert result to Polygons (strip Clipper internal format).
//
// [STATE] co (ClipperOffset) is shared across seed groups in propagate_waves() for
// efficiency (arc tolerance and shortest edge length are set once). co.Clear() is
// called at the start of wavefront_initial/step per path iteration — safe.
//
// [HAZARD H686] clip_clipper_polygons_with_subject_bbox trims the boundary to a bbox
// around the seed inflated by max_inflation. If max_inflation underestimates the
// actual ClipperOffset output (possible for small arc_tolerance + many steps), the
// wave will be truncated before reaching the full intended expansion depth. The 1.1×
// safety factor in max_inflation is intended to prevent this, but is not guaranteed
// for all arc_tolerance settings.
static Polygons propagate_wave_from_boundary(ClipperLib::ClipperOffset& co,
                                             // Seed of the wave: Open polylines very close to the boundary.
                                             const ClipperLib::Paths& seed,
                                             // Boundary inside which the waveform will propagate.
                                             const ExPolygon& boundary,
                                             // How much to inflate the seed lines to produce the first wave area.
                                             const float initial_step,
                                             // How much to inflate the first wave area and the successive wave areas in each step.
                                             const float other_step,
                                             // Number of inflate steps after the initial step.
                                             const size_t num_other_steps,
                                             // Maximum inflation of seed contours over the boundary. Used to trim boundary to speed up
                                             // clipping during wave propagation.
                                             const float max_inflation)
{
    assert(!seed.empty() && seed.front().size() >= 2);
    Polygons clipping = ClipperUtils::clip_clipper_polygons_with_subject_bbox(boundary, get_extents<true>(seed).inflated(max_inflation));
    ClipperLib::Paths polygons = wavefront_clip(wavefront_initial(co, seed, initial_step), clipping);
    // Now offset the remaining
    for (size_t ioffset = 0; ioffset < num_other_steps; ++ioffset)
        polygons = wavefront_clip(wavefront_step(co, polygons, other_step), clipping);
    return to_polygons(polygons);
}

// [INTENT] propagate_waves(seeds, boundary, params): main wave propagation driver.
// Iterates seeds grouped by (boundary, src) — requires seeds to be sorted by
// lower_by_boundary_and_src ordering (see H673 in header).
// For each (boundary, src) group, all seed paths are collected and passed to
// propagate_wave_from_boundary(). Each resulting polygon is emitted as a RegionExpansion.
//
// [COUPLING] The group scan (advance until boundary or src changes) is a linear pass
// with no random access. Seeds not sorted by (boundary, src) will merge wrong groups.
// [HAZARD H687] If two seeds for the same (boundary, src) group produce non-overlapping
// inflated polygons (e.g. the src ExPolygon touches the boundary at two disconnected
// locations), each is emitted as a separate RegionExpansion. This is correct behavior
// but the caller (merge_expansions_into_expolygons) must union them correctly.

// Resulting regions are sorted by boundary id and source id.
std::vector<RegionExpansion> propagate_waves(const WaveSeeds& seeds, const ExPolygons& boundary, const RegionExpansionParameters& params)
{
    std::vector<RegionExpansion> out;
    ClipperLib::Paths            paths;
    ClipperLib::ClipperOffset    co;
    co.ArcTolerance       = params.arc_tolerance;
    co.ShortestEdgeLength = params.shortest_edge_length;
    for (auto it_seed = seeds.begin(); it_seed != seeds.end();) {
        auto it = it_seed;
        paths.clear();
        for (; it != seeds.end() && it->boundary == it_seed->boundary && it->src == it_seed->src; ++it)
            paths.emplace_back(it->path);
        // Propagate the wavefront while clipping it with the trimmed boundary.
        // Collect the expanded polygons, merge them with the source polygons.
        RegionExpansion re;
        for (Polygon& polygon : propagate_wave_from_boundary(co, paths, boundary[it_seed->boundary], params.initial_step, params.other_step,
                                                             params.num_other_steps, params.max_inflation))
            out.push_back({std::move(polygon), it_seed->src, it_seed->boundary});
        it_seed = it;
    }

    return out;
}

std::vector<RegionExpansion> propagate_waves(const ExPolygons& src, const ExPolygons& boundary, const RegionExpansionParameters& params)
{
    return propagate_waves(wave_seeds(src, boundary, params.tiny_expansion, true), boundary, params);
}

std::vector<RegionExpansion> propagate_waves(const ExPolygons& src,
                                             const ExPolygons& boundary,
                                             // Scaled expansion value
                                             float expansion,
                                             // Expand by waves of expansion_step size (expansion_step is scaled).
                                             float expansion_step,
                                             // Don't take more than max_nr_steps for small expansion_step.
                                             size_t max_nr_steps)
{
    return propagate_waves(src, boundary, RegionExpansionParameters::build(expansion, expansion_step, max_nr_steps));
}

// [INTENT] propagate_waves_ex: drives propagate_waves() then converts the flat
// RegionExpansion list to RegionExpansionEx (ExPolygon fragments per (src, boundary)).
// For each (src, boundary) group with 1 fragment: wraps directly.
// For each (src, boundary) group with >1 fragments: runs union_ex() to merge them.
// [HAZARD H688] union_ex() may produce multiple ExPolygons for a single (src, boundary)
// group if the expanded fragments are non-contiguous. Each fragment is emitted as a
// separate RegionExpansionEx with the same (src_id, boundary_id). Callers that expect
// exactly one ExPolygon per (src, boundary) pair will receive multiple entries.

// Returns regions per source ExPolygon expanded into boundary.
std::vector<RegionExpansionEx> propagate_waves_ex(const WaveSeeds&                 seeds,
                                                  const ExPolygons&                boundary,
                                                  const RegionExpansionParameters& params)
{
    std::vector<RegionExpansion> expanded = propagate_waves(seeds, boundary, params);
    assert(std::is_sorted(seeds.begin(), seeds.end(), [](const auto& l, const auto& r) {
        return l.boundary < r.boundary || (l.boundary == r.boundary && l.src < r.src);
    }));
    Polygons                       acc;
    std::vector<RegionExpansionEx> out;
    for (auto it = expanded.begin(); it != expanded.end();) {
        auto it2 = it;
        acc.clear();
        for (; it2 != expanded.end() && it2->boundary_id == it->boundary_id && it2->src_id == it->src_id; ++it2)
            acc.emplace_back(std::move(it2->polygon));
        size_t size = it2 - it;
        if (size == 1)
            out.push_back({ExPolygon{std::move(acc.front())}, it->src_id, it->boundary_id});
        else {
            ExPolygons expolys = union_ex(acc);
            reserve_more_power_of_2(out, expolys.size());
            for (ExPolygon& ex : expolys)
                out.push_back({std::move(ex), it->src_id, it->boundary_id});
        }
        it = it2;
    }
    return out;
}

// Returns regions per source ExPolygon expanded into boundary.
std::vector<RegionExpansionEx> propagate_waves_ex(
    // Source regions that are supposed to touch the boundary.
    // Boundaries of source regions touching the "boundary" regions will be expanded into the "boundary" region.
    const ExPolygons& src,
    const ExPolygons& boundary,
    // Scaled expansion value
    float full_expansion,
    // Expand by waves of expansion_step size (expansion_step is scaled).
    float expansion_step,
    // Don't take more than max_nr_steps for small expansion_step.
    size_t max_nr_expansion_steps)
{
    auto params = RegionExpansionParameters::build(full_expansion, expansion_step, max_nr_expansion_steps);
    return propagate_waves_ex(wave_seeds(src, boundary, params.tiny_expansion, true), boundary, params);
}

// [INTENT] expand_expolygons: convenience wrapper that returns a per-source-ExPolygon
// vector of expanded polygon fragments. src[i] → out[i] contains all expansion polygons
// that originated from src[i]. Source ExPolygons not touching any boundary have empty
// Polygons{} in the output (no expansion).
std::vector<Polygons> expand_expolygons(const ExPolygons& src,
                                        const ExPolygons& boundary,
                                        // Scaled expansion value
                                        float expansion,
                                        // Expand by waves of expansion_step size (expansion_step is scaled).
                                        float expansion_step,
                                        // Don't take more than max_nr_steps for small expansion_step.
                                        size_t max_nr_steps)
{
    std::vector<Polygons> out(src.size(), Polygons{});
    for (RegionExpansion& r : propagate_waves(src, boundary, expansion, expansion_step, max_nr_steps))
        out[r.src_id].emplace_back(std::move(r.polygon));
    return out;
}

// [INTENT] merge_expansions_into_expolygons: merge expanded polygon fragments back into
// their originating src ExPolygons.
// 1. Sort expanded by src_id.
// 2. Walk src ExPolygons in order. For each src ExPolygon:
//    a. If no expansion for this src: emit src unchanged.
//    b. Otherwise: collect all expansion polygons for this src, append the src ExPolygon
//       to them, run union_safety_offset_ex(), emit the merged ExPolygon(s).
// 3. If union produces >1 ExPolygon, use sample-point containment to select the right one.
//
// [STATE] `src` is consumed (moved from). Cannot be used after this call.
// [HAZARD H674 — see header] If sample_in_expolygons returns -1 in the >1 branch, the
// source ExPolygon is silently dropped from the output.
// [HAZARD H689] The `last` index is a uint32_t loop variable that advances through src[].
// If src.size() > UINT32_MAX (not physically possible), the loop would overflow and
// wrap around, silently emitting the wrong ExPolygons.
std::vector<ExPolygon> merge_expansions_into_expolygons(ExPolygons&& src, std::vector<RegionExpansion>&& expanded)
{
    // expanded regions will be merged into source regions, thus they will be re-sorted by source id.
    std::sort(expanded.begin(), expanded.end(), [](const auto& l, const auto& r) { return l.src_id < r.src_id; });
    uint32_t   last = 0;
    Polygons   acc;
    ExPolygons out;
    out.reserve(src.size());
    for (auto it = expanded.begin(); it != expanded.end();) {
        for (; last < it->src_id; ++last)
            out.emplace_back(std::move(src[last]));
        acc.clear();
        assert(it->src_id == last);
        for (; it != expanded.end() && it->src_id == last; ++it)
            acc.emplace_back(std::move(it->polygon));
        // FIXME offset & merging could be more efficient, for example one does not need to copy the source expolygon
        ExPolygon& src_ex = src[last++];
        assert(!src_ex.contour.empty());
#if 0
        {
            static int iRun = 0;
            BoundingBox bbox = get_extents(acc);
            bbox.merge(get_extents(src_ex));
            SVG svg(debug_out_path("expand_merge_expolygons-failed-union=%d.svg", iRun ++).c_str(), bbox);
            svg.draw(acc);
            svg.draw_outline(acc, "black", scale_(0.05));
            svg.draw(src_ex, "red");
            svg.Close();
        }
#endif
        // [INTENT] sample: pick the front vertex of the source contour to identify which
        // merged ExPolygon is the "correct" one in case union produces >1 result.
        Point sample = src_ex.contour.front();
        append(acc, to_polygons(std::move(src_ex)));
        ExPolygons merged = union_safety_offset_ex(acc);
        // Expanding one expolygon by waves should not change connectivity of the source expolygon:
        // Single expolygon should be produced possibly with increased number of holes.
        if (merged.size() > 1) {
            // assert(merged.size() == 1);
            // There is something wrong with the initial waves. Most likely the bridge was not valid at all
            // or the boundary region was very close to some bridge edge, but not really touching.
            // Pick only a single merged expolygon, which contains one sample point of the source expolygon.
            // [SEE H674] if sample_in_expolygons returns -1 here, the source ExPolygon is silently dropped.
            auto aabb_tree = build_aabb_tree_over_expolygons(merged);
            int  id        = sample_in_expolygons(aabb_tree, merged, sample);
            assert(id != -1);
            if (id != -1)
                out.emplace_back(std::move(merged[id]));
        } else if (merged.size() == 1)
            out.emplace_back(std::move(merged.front()));
    }
    for (; last < uint32_t(src.size()); ++last)
        out.emplace_back(std::move(src[last]));
    return out;
}

// [INTENT] expand_merge_expolygons: complete pipeline convenience wrapper.
// Calls propagate_waves() with the (moved) src and then merge_expansions_into_expolygons().
// src is consumed (moved from) and must not be used after this call.
std::vector<ExPolygon> expand_merge_expolygons(ExPolygons&& src, const ExPolygons& boundary, const RegionExpansionParameters& params)
{
    // expanded regions are sorted by boundary id and source id
    std::vector<RegionExpansion> expanded = propagate_waves(src, boundary, params);
    return merge_expansions_into_expolygons(std::move(src), std::move(expanded));
}

}} // namespace Slic3r::Algorithm
