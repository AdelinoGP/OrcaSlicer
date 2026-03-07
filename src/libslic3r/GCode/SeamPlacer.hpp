// [INTENT] SeamPlacer.hpp — public interface and shared data structures for seam placement.
// The seam is the point on each perimeter loop where the extruder starts/stops; its position
// affects both surface quality (visible scar) and structural strength.
// SeamPlacer runs once per PrintObject during init(), then is queried per-loop during G-code export.
//
// High-level algorithm:
//   1. Sample ~30 000 points on the mesh surface (TriangleSetSampling).
//   2. Cast 25 hemisphere rays per sample to build per-point visibility scores (raycast_visibility).
//   3. For each layer, create SeamCandidate for every external perimeter vertex.
//   4. Compute overhang, layer-embedding, corner-angle penalty, and enforcer/blocker flags per candidate.
//   5. Pick the lowest-penalty candidate as the seam for each perimeter.
//   6. For spAligned/spRear: cluster chosen seams across layers into "strings" and fit a cubic B-spline
//      through them so the seam traces a smooth vertical path (align_seam_points).
//   7. At G-code export time, place_seam() splits the ExtrusionLoop at the chosen point.

#ifndef libslic3r_SeamPlacer_hpp_
#define libslic3r_SeamPlacer_hpp_

#include <limits>
#include <optional>
#include <vector>
#include <memory>
#include <atomic>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/KDTreeIndirect.hpp"

namespace Slic3r {

class PrintObject;
class ExtrusionLoop;
class Print;
class Layer;

namespace EdgeGrid {
class Grid;
}

namespace SeamPlacerImpl {


struct GlobalModelInfo;
struct SeamComparator;

// [INTENT] Three-state enum classifying each seam candidate relative to user-painted regions.
// Enforced > Neutral > Blocked in comparator priority ordering.
// [COUPLING] Painted regions come from ModelVolume::seam_facets via EnforcerBlockerType.
enum class EnforcedBlockedSeamPoint {
  Blocked = 0,
  Neutral = 1,
  Enforced = 2,
};

// [INTENT] Represents a single closed perimeter loop for seam selection purposes.
// All SeamCandidate points of one loop share a reference to the same Perimeter instance.
// [STATE] start_index/end_index index into the per-layer SeamCandidate flat vector.
//         end_index is *exclusive* (past-the-end) for iteration, but the comment says "inclusive!" —
//         the actual usage in loops is "index < end_index" (exclusive).
// [HAZARD] The "inclusive!" comment on end_index is misleading; code uses end_index as exclusive.
//          Callers must verify which convention applies before modifying iteration logic.
// [STATE] finalized=true means align_seam_points() has stored a fitted position; place_seam() uses
//         final_seam_position instead of re-computing from seam_index.
// [MEMORY] Perimeters are stored in a Slic3r::deque<Perimeter> (PrintObjectSeamData::LayerSeams::perimeters).
//          SeamCandidate holds a non-owning reference (&) to its Perimeter — the deque must never
//          reallocate after candidates are created (deque preserves references; vector would not).
struct Perimeter {
  size_t start_index{};
  size_t end_index{}; //inclusive!
  size_t seam_index{};
  float flow_width{};

  // During alignment, a final position may be stored here. In that case, finalized is set to true.
  // Note that final seam position is not limited to points of the perimeter loop. In theory it can be any position
  // Random position also uses this flexibility to set final seam point position
  bool finalized = false;
  Vec3f final_seam_position = Vec3f::Zero();
};

// [INTENT] Per-vertex data computed during the seam scoring phase.
//   position        — 3D float mm coords (mesh coordinate system).
//   visibility      — fraction [0,1] of hemisphere rays that escape without hitting mesh (1=fully visible=bad for seam).
//                     For spAlignedBack an adjustment bias is added making back-facing points preferred.
//   overhang        — positive = hangs over air (mm past threshold). Zero-clamped; drives avoidance.
//   unsupported_dist— raw signed distance to previous layer outline used for overhang reporting to caller.
//   embedded_distance — signed distance inside merged layer slices. Negative = hidden inside print (multimaterial join).
//                     Hidden points (< -0.5mm) are preferred as the seam is invisible.
//   local_ccw_angle — interior angle of the perimeter vertex in CCW convention (radians).
//                     Negative = concave corner (good — hidden). Positive = convex corner (bad — visible ridge).
//   type            — Blocked/Neutral/Enforced from user painting.
//   central_enforcer— true for the single "anchor" point in the longest enforced segment; used by alignment.
// [COUPLING] perimeter is a mutable reference; comparator and alignment mutate perimeter.seam_index/finalized
//            through this reference even though the point itself is const during scoring.
//Struct over which all processing of perimeters is done. For each perimeter point, its respective candidate is created,
// then all the needed attributes are computed and finally, for each perimeter one point is chosen as seam.
// This seam position can be then further aligned
struct SeamCandidate {
  SeamCandidate(const Vec3f &pos, Perimeter &perimeter,
                float local_ccw_angle,
                EnforcedBlockedSeamPoint type) :
                                                 position(pos), perimeter(perimeter), visibility(0.0f), overhang(0.0f), embedded_distance(0.0f), local_ccw_angle(
                                                                                                                                                     local_ccw_angle), type(type), central_enforcer(false) {
  }
  const Vec3f position;
  // pointer to Perimeter loop of this point. It is shared across all points of the loop
  Perimeter &perimeter;
  float visibility;
  float overhang;
  float unsupported_dist;
  // distance inside the merged layer regions, for detecting perimeter points which are hidden indside the print (e.g. multimaterial join)
  // Negative sign means inside the print, comes from EdgeGrid structure
  float embedded_distance;
  float local_ccw_angle;
  EnforcedBlockedSeamPoint type;
  bool central_enforcer; //marks this candidate as central point of enforced segment on the perimeter - important for alignment
};

// [INTENT] Adapter making SeamCandidate positions addressable by index+dimension for KDTreeIndirect.
// [COUPLING] Holds a const reference to the candidates vector — must not outlive it.
struct SeamCandidateCoordinateFunctor {
  SeamCandidateCoordinateFunctor(const std::vector<SeamCandidate> &seam_candidates) :
                                                                                      seam_candidates(seam_candidates) {
  }
  const std::vector<SeamCandidate> &seam_candidates;
  float operator()(size_t index, size_t dim) const {
    return seam_candidates[index].position[dim];
  }
};
} // namespace SeamPlacerImpl

// [INTENT] Holds all per-PrintObject seam data. One instance per PrintObject, stored inside SeamPlacer::m_seam_per_object.
// Destroyed when SeamPlacer is cleared or re-initialized.
// [MEMORY] This is the dominant memory consumer of seam placement:
//   ~30 000 mesh samples × sizeof(SeamCandidate) across all layers.
//   The points_tree (KDTreeIndirect) adds another O(N log N) nodes per layer.
// [CONCURRENCY] Written by gather_seam_candidates() (TBB parallel_for over layers), then read-only
//   during place_seam() (single-threaded per G-code export call). No synchronization needed after init.
struct PrintObjectSeamData
{
  using SeamCandidatesTree = KDTreeIndirect<3, float, SeamPlacerImpl::SeamCandidateCoordinateFunctor>;

  // [STATE] One LayerSeams entry per physical layer of the PrintObject.
  //   perimeters — deque preserves stable references needed by SeamCandidate::perimeter.
  //   points     — flat vector of all candidates for the layer, across all perimeter loops.
  //   points_tree— KD-tree for O(log N) nearest-candidate lookup during place_seam().
  struct LayerSeams
  {
    Slic3r::deque<SeamPlacerImpl::Perimeter> perimeters;
    std::vector<SeamPlacerImpl::SeamCandidate> points;
    std::unique_ptr<SeamCandidatesTree> points_tree;
  };
  // Map of PrintObjects (PO) -> vector of layers of PO -> vector of perimeter
  std::vector<LayerSeams> layers;
  // Map of PrintObjects (PO) -> vector of layers of PO -> unique_ptr to KD
  // tree of all points of the given layer

  void clear()
  {
    layers.clear();
  }
};

// [INTENT] Main seam placement controller. Lifetime matches the GCode object (one per print job).
// init() is called once before G-code export begins; place_seam() is called for each ExtrusionLoop.
// [CONCURRENCY] init() uses TBB internally. place_seam() is called single-threaded from GCode::process_layer().
// [COUPLING] Reads PrintObject::config().seam_position, PrintObject::layers(), ModelVolume::seam_facets.
//            Writes into ExtrusionLoop (splits the loop at the chosen seam point).
class SeamPlacer {
public:
  // [STATE] Tuning constants — all compile-time, not user-configurable at runtime.
  // Number of samples generated on the mesh. There are sqr_rays_per_sample_point*sqr_rays_per_sample_point rays casted from each samples
  static constexpr size_t raycasting_visibility_samples_count = 30000;
  // [INTENT] Decimation target for mesh simplification before raycasting. Reduces ray-triangle cost.
  static constexpr size_t fast_decimation_triangle_count_target = 16000;
  //square of number of rays per sample point
  // [INTENT] 5×5 = 25 rays per sample in a stratified hemisphere grid. Total ray budget: 30000 × 25 = 750 000.
  static constexpr size_t sqr_rays_per_sample_point = 5;

  // snapping angle - angles larger than this value will be snapped to during seam painting
  // [INTENT] ~55°: corners sharper than this are treated as "real" corners and preferred for alignment anchoring.
  static constexpr float sharp_angle_snapping_threshold = 55.0f * float(PI) / 180.0f;
  // overhang angle for seam placement that still yields good results, in degrees, measured from vertical direction
  // [INTENT] 45° overhang: beyond this the seam penalty kicks in (overhang > 0).
  static constexpr float overhang_angle_threshold = 45.0f * float(PI) / 180.0f;

  // determines angle importance compared to visibility ( neutral value is 1.0f. )
  // [INTENT] For aligned mode, visibility and angle contribute roughly equally.
  //          For nearest mode, angle dominates (1.0 vs 0.6) to reduce noise from sparse visibility samples.
  static constexpr float angle_importance_aligned = 0.6f;
  static constexpr float angle_importance_nearest = 1.0f; // use much higher angle importance for nearest mode, to combat the visibility info noise

  // For long polygon sides, if they are close to the custom seam drawings, they are oversampled with this step size
  // [INTENT] 0.2 mm oversampling ensures enforcer boundaries are captured even for long polygon edges.
  static constexpr float enforcer_oversampling_distance = 0.2f;

  // When searching for seam clusters for alignment:
  // following value describes, how much worse score can point have and still be picked into seam cluster instead of original seam point on the same layer
  static constexpr float seam_align_score_tolerance = 0.3f;
  // seam_align_tolerable_dist_factor - how far to search for seam from current position, final dist is seam_align_tolerable_dist_factor * flow_width
  static constexpr float seam_align_tolerable_dist_factor = 4.0f;
  // minimum number of seams needed in cluster to make alignment happen
  // [INTENT] Prevents fitting splines on very short objects where alignment would be meaningless.
  static constexpr size_t seam_align_minimum_string_seams = 6;
  // millimeters covered by spline; determines number of splines for the given string
  // [HAZARD] Declared as size_t but assigned a float literal (4.0f). Compiles (implicit narrowing to 4),
  //          but intent is clearly float (mm per spline segment). The division
  //          total_length / seam_align_mm_per_segment is done in float context so the result is correct,
  //          but the type mismatch is a latent maintenance hazard.
  static constexpr size_t seam_align_mm_per_segment = 4.0f;

  //The following data structures hold all perimeter points for all PrintObject.
  // [STATE] Key = raw PrintObject pointer (stable for print lifetime). Not thread-safe to mutate during place_seam().
  std::unordered_map<const PrintObject*, PrintObjectSeamData> m_seam_per_object;

  // [INTENT] Entry point. Runs the full seam analysis pipeline for all PrintObjects in the print.
  //   throw_if_canceled_func is called at safe cancellation checkpoints.
  // [CONCURRENCY] Uses TBB internally. Must complete before any place_seam() calls.
  void init(const Print &print, std::function<void(void)> throw_if_canceled_func);

  // [INTENT] Called per ExtrusionLoop during G-code export. Splits the loop at the chosen seam position.
  //   last_pos: current nozzle position (used for spNearest mode only).
  //   overhang: output — unsupported distance at the chosen seam point, reported to caller for bridge fan decisions.
  // [COUPLING] Mutates ExtrusionLoop in-place via loop.split_at() or loop.split_at_vertex().
  void place_seam(const Layer *layer, ExtrusionLoop &loop, const Point &last_pos, float& overhang) const;
private:
  // [INTENT] Phase 1 of init(). For each layer, extract external perimeter polygons, compute per-vertex
  //   angles and enforcer/blocker classifications, then build per-layer KD-trees.
  void gather_seam_candidates(const PrintObject *po, const SeamPlacerImpl::GlobalModelInfo &global_model_info);

  // [INTENT] Phase 2. For each candidate, query the GlobalModelInfo KD-tree + weighted interpolation
  //   to transfer mesh-sample visibility scores to the perimeter vertex positions.
  void calculate_candidates_visibility(const PrintObject *po,
                                       const SeamPlacerImpl::GlobalModelInfo &global_model_info);

  // [INTENT] Phase 3. Compute overhang (distance past previous-layer outline) and embedded_distance
  //   (hidden inside multi-material join) for each candidate. Both drive seam avoidance.
  void calculate_overhangs_and_layer_embedding(const PrintObject *po);

  // [INTENT] Phase 4 (spAligned/spRear only). Cluster seam points across layers into vertical strings,
  //   fit a cubic B-spline, and store the smoothed position in Perimeter::final_seam_position.
  void align_seam_points(const PrintObject *po, const SeamPlacerImpl::SeamComparator &comparator);

  // [INTENT] Extends a seam cluster string by searching upward then downward from start_seam.
  //   Returns vector of (layer_idx, point_idx) pairs forming the cluster.
  std::vector<std::pair<size_t, size_t>> find_seam_string(const PrintObject *po,
                                                          std::pair<size_t, size_t> start_seam,
                                                          const SeamPlacerImpl::SeamComparator &comparator) const;

  // [INTENT] For a given layer, find the best unfinalized seam candidate within max_distance of projected_position.
  //   Returns {} if no acceptable candidate found. Used by find_seam_string().
  std::optional<std::pair<size_t, size_t>> find_next_seam_in_layer(
      const std::vector<PrintObjectSeamData::LayerSeams> &layers,
      const Vec3f& projected_position,
      const size_t layer_idx, const float max_distance,
      const SeamPlacerImpl::SeamComparator &comparator) const;
};

} // namespace Slic3r

#endif // libslic3r_SeamPlacer_hpp_
