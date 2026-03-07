// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.
//
// [INTENT] TreeModelVolumes is a lazy-with-precalculation cache for three types of
// 2D constraint polygons used by TreeSupport3D to route branch paths:
//
//   1. COLLISION:   The XY+Z-expanded model outline at each layer. Any branch node
//                   whose center falls inside collision(radius, layer) would intersect
//                   the model. Two variants: regular and "holefree" (holes filled in).
//
//   2. AVOIDANCE:   The region that a branch CANNOT pass through if it wants to reach
//                   the build plate (or a model surface). Derived by propagating the
//                   collision upward layer by layer, shrinking by max_move per step.
//                   Three sub-variants (AvoidanceType): Slow, FastSafe (hole-free), Fast.
//                   Two targets: to_buildplate=true, to_model=true.
//
//   3. WALL RESTRICTION: Intersection of current-layer collision (radius 0) with
//                   previous-layer collision (at branch radius). Used to prevent
//                   branches from tunneling through thin walls between layers.
//
//   4. PLACEABLE AREAS: Where a branch foot can rest on top of the model surface.
//                   Only computed when support_rests_on_model is true.
//
// CACHING STRATEGY:
//   All results are keyed by (radius, layer_idx) and stored in RadiusLayerPolygonCache
//   instances (one per constraint type). RadiusLayerPolygonCache is a std::vector<std::map>
//   protected by a single std::mutex for thread-safe lazy inserts.
//
//   Radii are NOT stored continuously — they are snapped to a coarse grid via
//   ceilRadius(). The grid has two phases:
//     - Linear: m_radius_0 to SUPPORT_TREE_EXPONENTIAL_THRESHOLD (0.5mm steps)
//     - Exponential: × SUPPORT_TREE_EXPONENTIAL_FACTOR (1.5×) steps thereafter
//   This means cache entries for neighboring radii may alias to the same key, and
//   a radius lookup returns the polygon for the SMALLEST cached radius >= requested.
//
// PRECALCULATE vs LAZY:
//   If precalculate() is called, all required (radius, layer) pairs are computed
//   upfront in TBB parallel (recommended for multi-core systems).
//   If NOT called, each getXxx() call triggers calculateXxx() on the calling thread
//   with a performance warning. The latter is much slower on multi-core systems.
//
// [COUPLING] Constructor reads PrintObject::lslices, PrintConfig, TreeSupportSettings.
//   calculateCollision() reads m_layer_outlines, m_machine_border, m_anti_overhang.
//   calculateAvoidance() reads collision cache (via getCollision/getCollisionHolefree).
//   All public getXxx() methods use const_cast to call calculateXxx() lazily.
//
// [CONCURRENCY] All cache insert() methods lock m_mutex (per RadiusLayerPolygonCache).
//   calculateCollision/Avoidance/WallRestrictions all use tbb::parallel_for internally.
//   The nested TBB parallelism (parallel_for inside parallel_for in calculateCollision)
//   is intentional — TBB handles work-stealing across the nested pools.
//
// [HAZARD] calculateMachineBorderCollision() always returns empty Polygons (#if 1 branch).
//   Machine borders are IGNORED — tree support legs can collide with the print bed boundary.
//   The #else branch (offsetting by 1000mm) is disabled because it overflows int32 coords.
//
// [HAZARD] calculateCollision() bug at line ~472:
//   `processing_last_mesh = outline_idx == layer_outline_indices.size()`
//   This compares the value of outline_idx (a size_t) to layer_outline_indices.size().
//   Since outline_idx is iterated over VALUES (not indices) of layer_outline_indices,
//   this is never true unless one mesh index happens to equal the total mesh count.
//   The intended check is `outline_idx == layer_outline_indices.back()`.
//   This means placeable areas and anti_overhang are NEVER applied properly when
//   there is more than one mesh group (currently always one group due to #if 0 in
//   group_meshes(), so this bug is latent).
//
// [HAZARD] Multi-mesh group code in constructor (lines 70-92) and calculateCollision
//   (line ~127-153) are both inside #if 0 blocks. Currently always single-mesh.
//   If multi-mesh is ever re-enabled, the xy_distance selection logic at line ~441-448
//   uses an approximation (m_current_min_xy_dist_delta added at request time) that
//   introduces small inaccuracies for non-primary mesh groups.
//
// [MEMORY] RadiusLayerPolygonCache stores Polygons by value in std::map<coord_t, Polygons>
//   per layer. For tall prints (1000+ layers) × many radius buckets, total memory can
//   be tens to hundreds of MB. clear() and clear_all_but_object_collision() are provided
//   but only called at explicit pipeline boundaries.


#include "TreeModelVolumes.hpp"
#include "TreeSupportCommon.hpp"

#include "../BuildVolume.hpp"
#include "../ClipperUtils.hpp"
#include "../Flow.hpp"
#include "../Layer.hpp"
#include "../Point.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Utils.hpp"
#include "../format.hpp"

#include <string_view>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/task_group.h>

namespace Slic3r::TreeSupport3D
{

using namespace std::literals;

// or warning
// had to use a define beacuse the macro processing inside macro BOOST_LOG_TRIVIAL()
#define error_level_not_in_cache debug

// [INTENT] DEAD CODE — machine border is always ignored (#if 1 returns empty).
// The #else branch would offset the build volume polygon outward by 1000mm (giving a
// 1m safety ring) and then subtract the original bed polygon to create a border wall.
// Disabled because 1000mm in scaled int32 coordinates overflows (max ~32mm at 1e6 scale).
// [HAZARD] Without machine borders, tree support legs can extend beyond the physical
// print bed. On printers with bed clips or limited print area, this can cause print failures.
static Polygons calculateMachineBorderCollision(Polygon machine_border)
{
    // Put a border of 1m around the print volume so that we don't collide.
#if 1
    //FIXME just returning no border will let tree support legs collide with print bed boundary
    return {};
#else
    //FIXME offsetting by 1000mm easily overflows int32_tr coordinate.
    Polygons out = offset(machine_border, scaled<float>(1000.), jtMiter, 1.2);
    machine_border.reverse(); // Makes the polygon negative so that we subtract the actual volume from the collision area.
    out.emplace_back(std::move(machine_border));
    return out;
#endif
}

TreeModelVolumes::TreeModelVolumes(
    const PrintObject &print_object,
    const BuildVolume &build_volume,
    const coord_t max_move, const coord_t max_move_slow, size_t current_mesh_idx, 
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    double progress_multiplier, double progress_offset, 
#endif // SLIC3R_TREESUPPORTS_PROGRESS
    const std::vector<Polygons>& additional_excluded_areas) :
    // -2 to avoid rounding errors
    m_max_move{ std::max<coord_t>(max_move - 2, 0) }, m_max_move_slow{ std::max<coord_t>(max_move_slow - 2, 0) },
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    m_progress_multiplier{ progress_multiplier }, m_progress_offset{ progress_offset },
#endif // SLIC3R_TREESUPPORTS_PROGRESS
    m_machine_border{ calculateMachineBorderCollision(build_volume.polygon()) }
{
    m_bed_area = build_volume.polygon();
#if 0
    std::unordered_map<size_t, size_t> mesh_to_layeroutline_idx;
    for (size_t mesh_idx = 0; mesh_idx < storage.meshes.size(); ++ mesh_idx) {
        SliceMeshStorage mesh = storage.meshes[mesh_idx];
        bool added = false;
        for (size_t idx = 0; idx < m_layer_outlines.size(); ++ idx)
            if (TreeSupport::TreeSupportSettings(m_layer_outlines[idx].first) == TreeSupport::TreeSupportSettings(mesh.settings)) {
                added = true;
                mesh_to_layeroutline_idx[mesh_idx] = idx;
            }
        if (! added) {
            mesh_to_layeroutline_idx[mesh_idx] = m_layer_outlines.size();
            m_layer_outlines.emplace_back(mesh.settings, std::vector<Polygons>(storage.support.supportLayers.size(), Polygons()));
        }
    }
    for (size_t idx = 0; idx < m_layer_outlines.size(); ++ idx) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layer_outlines[idx].second.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (const size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                m_layer_outlines[idx].second[layer_idx] = union_(m_layer_outlines[idx].second[layer_idx]); 
        });
    }
    m_current_outline_idx = mesh_to_layeroutline_idx[current_mesh_idx];

#else
    {
        m_anti_overhang = print_object.slice_support_blockers();
        TreeSupportMeshGroupSettings mesh_settings(print_object);
        const TreeSupportSettings config{ mesh_settings, print_object.slicing_parameters() };
        m_current_min_xy_dist = config.xy_min_distance;
        m_current_min_xy_dist_delta = config.xy_distance - m_current_min_xy_dist;
        assert(m_current_min_xy_dist_delta >= 0);
        m_increase_until_radius = config.increase_radius_until_radius;
        m_radius_0 = config.getRadius(0);
        m_raft_layers = config.raft_layers;
        m_current_outline_idx = 0;

        m_layer_outlines.emplace_back(mesh_settings, std::vector<Polygons>{});
        std::vector<Polygons> &outlines = m_layer_outlines.front().second;
        size_t num_raft_layers = m_raft_layers.size();
        size_t num_layers = print_object.layer_count() + num_raft_layers;
        outlines.assign(num_layers, Polygons{});
        tbb::parallel_for(tbb::blocked_range<size_t>(num_raft_layers, num_layers, std::min<size_t>(1, std::max<size_t>(16, num_layers / (8 * tbb::this_task_arena::max_concurrency())))),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                outlines[layer_idx] = polygons_simplify(to_polygons(print_object.get_layer(layer_idx - num_raft_layers)->lslices), mesh_settings.resolution, polygons_strictly_simple);
        });
    }
#endif

    m_support_rests_on_model = false;
    m_min_resolution  = std::numeric_limits<coord_t>::max();
    for (auto data_pair : m_layer_outlines) {
        m_support_rests_on_model |= ! data_pair.first.support_material_buildplate_only;
        m_min_resolution = std::min(m_min_resolution, data_pair.first.resolution);
    }

#if 0
    for (size_t mesh_idx = 0; mesh_idx < storage.meshes.size(); mesh_idx++) {
        SliceMeshStorage mesh = storage.meshes[mesh_idx];
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layer_outlines[mesh_to_layeroutline_idx[mesh_idx]].second.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                if (layer_idx < mesh.layer_nr_max_filled_layer) {
                    Polygons outline = extractOutlineFromMesh(mesh, layer_idx);
                    append(m_layer_outlines[mesh_to_layeroutline_idx[mesh_idx]].second[layer_idx], outline);
                }
        });
    }
    if (! additional_excluded_areas.empty()) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_anti_overhang.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                if (layer_idx < coord_t(additional_excluded_areas.size()))
                    append(m_anti_overhang[layer_idx], additional_excluded_areas[layer_idx]);
    //          if (SUPPORT_TREE_AVOID_SUPPORT_BLOCKER)
    //              append(m_anti_overhang[layer_idx], storage.support.supportLayers[layer_idx].anti_overhang);
    //FIXME block wipe tower
    //          if (storage.primeTower.enabled)
    //              append(m_anti_overhang[layer_idx], layer_idx == 0 ? storage.primeTower.outer_poly_first_layer : storage.primeTower.outer_poly);
                m_anti_overhang[layer_idx] = union_(m_anti_overhang[layer_idx]);
            }
        });
    }
#endif
}

// [INTENT] Upfront batch computation of all collision and avoidance polygons up to
// max_layer. Called once at the start of tree support generation. Determines which
// (radius, layer) pairs are actually needed by simulating the branch radius growth
// schedule, then dispatches calculateCollision(), calculateCollisionHolefree(),
// calculatePlaceables(), calculateAvoidance(), and calculateWallRestrictions() in
// parallel via a tbb::task_group.
//
// [STATE] Sets m_precalculated = true. After this, any cache miss triggers a
// BOOST_LOG_TRIVIAL(debug) warning and falls back to lazy calculation.
//
// [COUPLING] Reads m_layer_outlines[m_current_outline_idx].first for TreeSupportSettings.
//   Reads config.getRadius(), config.recommendedMinRadius(), config.tip_layers to
//   enumerate all radius buckets needed.
//
// [MEMORY] The radius_until_layer map can have O(tip_layers + log(max_layer)) entries.
//   Each entry maps to a (radius, max_layer) pair; the Cartesian product × 3 avoidance
//   types × 2 targets = up to 6× that count of avoidance tasks.
//
// [HAZARD] SVG paint-cache debugging block at lines ~256-291 is behind #if 0.
//   Generating these SVGs for large prints would write thousands of files to disk.
void TreeModelVolumes::precalculate(const PrintObject& print_object, const coord_t max_layer, std::function<void()> throw_on_cancel)
{
    auto t_start = std::chrono::high_resolution_clock::now();
    m_precalculated = true;

    // Get the config corresponding to one mesh that is in the current group. Which one has to be irrelevant.
    // Not the prettiest way to do this, but it ensures some calculations that may be a bit more complex
    // like inital layer diameter are only done in once.
    TreeSupportSettings config(m_layer_outlines[m_current_outline_idx].first, print_object.slicing_parameters());

    {
        // calculate which radius each layer in the tip may have.
        std::vector<coord_t> possible_tip_radiis;
        for (size_t distance_to_top = 0; distance_to_top <= config.tip_layers; ++ distance_to_top) {
            possible_tip_radiis.emplace_back(ceilRadius(config.getRadius(distance_to_top)));
            possible_tip_radiis.emplace_back(ceilRadius(config.getRadius(distance_to_top) + m_current_min_xy_dist_delta));
        }
        sort_remove_duplicates(possible_tip_radiis);
        // It theoretically may happen in the tip, that the radius can change so much in-between 2 layers, 
        // that a ceil step is skipped (as in there is a radius r so that ceilRadius(radius(dtt))<ceilRadius(r)<ceilRadius(radius(dtt+1))). 
        // As such a radius will not reasonable happen in the tree and it will most likely not be requested,
        // there is no need to calculate them. So just skip these.
        for (coord_t radius_eval = m_radius_0; radius_eval <= config.branch_radius; radius_eval = ceilRadius(radius_eval + 1))
            if (! std::binary_search(possible_tip_radiis.begin(), possible_tip_radiis.end(), radius_eval))
                m_ignorable_radii.emplace_back(radius_eval);
    }

    if (throw_on_cancel)
        throw_on_cancel();

    // it may seem that the required avoidance can be of a smaller radius when going to model (no initial layer diameter for to model branches)
    // but as for every branch going towards the bp, the to model avoidance is required to check for possible merges with to model branches, this assumption is in-fact wrong.
    std::unordered_map<coord_t, LayerIndex> radius_until_layer;
    // while it is possible to calculate, up to which layer the avoidance should be calculated, this simulation is easier to understand, and does not need to be adjusted if something of the radius calculation is changed.
    // Overhead with an assumed worst case of 6600 layers was about 2ms
    for (LayerIndex distance_to_top = 0; distance_to_top <= max_layer; ++ distance_to_top) {
        const LayerIndex current_layer = max_layer - distance_to_top;
        auto update_radius_until_layer = [&radius_until_layer, current_layer](coord_t r) {
            auto it = radius_until_layer.find(r);
            if (it == radius_until_layer.end())
                radius_until_layer.emplace_hint(it, r, current_layer);
            else
                assert(it->second >= current_layer);
        };
        // regular radius
        update_radius_until_layer(ceilRadius(config.getRadius(distance_to_top, 0) + m_current_min_xy_dist_delta));
        // the maximum radius that the radius with the min_xy_dist can achieve
        update_radius_until_layer(ceilRadius(config.getRadius(distance_to_top, 0)));
        update_radius_until_layer(ceilRadius(config.recommendedMinRadius(current_layer) + m_current_min_xy_dist_delta));
    }

    if (throw_on_cancel)
        throw_on_cancel();

    // Copy to deque to use in parallel for later.
    std::vector<RadiusLayerPair> relevant_avoidance_radiis{ radius_until_layer.begin(), radius_until_layer.end() };

    // Append additional radiis needed for collision.
    // To calculate collision holefree for every radius, the collision of radius m_increase_until_radius will be required.
    radius_until_layer[ceilRadius(m_increase_until_radius + m_current_min_xy_dist_delta)] = max_layer;
    // Collision for radius 0 needs to be calculated everywhere, as it will be used to ensure valid xy_distance in drawAreas.
    radius_until_layer[0] = max_layer;
    if (m_current_min_xy_dist_delta != 0)
        radius_until_layer[m_current_min_xy_dist_delta] = max_layer;

    // Now that required_avoidance_limit contains the maximum of ild and regular required radius just copy.
    std::vector<RadiusLayerPair> relevant_collision_radiis{ radius_until_layer.begin(), radius_until_layer.end() };

    // Calculate the relevant collisions
    calculateCollision(relevant_collision_radiis, throw_on_cancel);

    // calculate a separate Collisions with all holes removed. These are relevant for some avoidances that try to avoid holes (called safe)
    std::vector<RadiusLayerPair> relevant_hole_collision_radiis;
    for (RadiusLayerPair key : relevant_avoidance_radiis)
        if (key.first < m_increase_until_radius + m_current_min_xy_dist_delta)
            relevant_hole_collision_radiis.emplace_back(key);

    // Calculate collisions without holes, built from regular collision
    calculateCollisionHolefree(relevant_hole_collision_radiis, throw_on_cancel);
    // Let placables be calculated from calculateAvoidance() for better parallelization.
    if (m_support_rests_on_model)
        calculatePlaceables(relevant_avoidance_radiis, throw_on_cancel);

    auto t_coll = std::chrono::high_resolution_clock::now();

    // Calculate the relevant avoidances in parallel as far as possible
    {
        tbb::task_group task_group;
        task_group.run([this, relevant_avoidance_radiis, throw_on_cancel]{ calculateAvoidance(relevant_avoidance_radiis, true, m_support_rests_on_model, throw_on_cancel); });
        task_group.run([this, relevant_avoidance_radiis, throw_on_cancel]{ calculateWallRestrictions(relevant_avoidance_radiis, throw_on_cancel); });
        task_group.wait();
    }
    auto t_end = std::chrono::high_resolution_clock::now();
    auto dur_col = 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(t_coll - t_start).count();
    auto dur_avo = 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_coll).count();

//    m_precalculated = true;
    BOOST_LOG_TRIVIAL(info) << "Precalculating collision took" << dur_col << " ms. Precalculating avoidance took " << dur_avo << " ms.";

#if 0
    // Paint caches into SVGs:
    auto paint_cache_into_SVGs = [this](const RadiusLayerPolygonCache &cache, std::string_view name) {
        const std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> sorted = cache.sorted();
        static constexpr const std::string_view colors[] = {
            "red", "green", "blue", "magenta", "orange"
        };
        static constexpr const size_t num_colors = sizeof(colors) / sizeof(colors[0]);
        for (size_t i = 0; i < sorted.size();) {
            // Find range of cache items with the same layer index.
            size_t j = i;
            for (++ j; j < sorted.size() && sorted[i].first.second == sorted[j].first.second; ++ j) ;
            // Collect expolygons in reverse order (largest to smallest).
            std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
            for (int k = int(j - 1); k >= int(i); -- k) {
                std::string legend = format("radius-%1%", unscaled<float>(sorted[k].first.first));
                expolygons_with_attributes.push_back({ union_ex(sorted[k].second), SVG::ExPolygonAttributes(legend, std::string(colors[(k - int(i)) % num_colors]), 1.) });
                SVG::export_expolygons(debug_out_path("treesupport_cache-%s-%d-%s.svg", name.data(), sorted[i].first.second, legend.c_str()), { expolygons_with_attributes.back() });
            }
            // Render the range of per radius collision polygons into a common SVG.
            SVG::export_expolygons(debug_out_path("treesupport_cache-%s-%d.svg", name.data(), sorted[i].first.second), expolygons_with_attributes);
            i = j;
        }
    };
    paint_cache_into_SVGs(m_collision_cache,                    "collision_cache");
    paint_cache_into_SVGs(m_collision_cache_holefree,           "collision_cache_holefree");
    paint_cache_into_SVGs(m_avoidance_cache,                    "avoidance_cache");
    paint_cache_into_SVGs(m_avoidance_cache_slow,               "avoidance_cache_slow");
    paint_cache_into_SVGs(m_avoidance_cache_to_model,           "avoidance_cache_to_model");
    paint_cache_into_SVGs(m_avoidance_cache_to_model_slow,      "avoidance_cache_to_model_slow");
    paint_cache_into_SVGs(m_placeable_areas_cache,              "placable_areas_cache");
    paint_cache_into_SVGs(m_avoidance_cache_holefree,           "avoidance_cache_holefree");
    paint_cache_into_SVGs(m_avoidance_cache_holefree_to_model,  "avoidance_cache_holefree_to_model");
    paint_cache_into_SVGs(m_wall_restrictions_cache,            "wall_restrictions_cache");
    paint_cache_into_SVGs(m_wall_restrictions_cache_min,        "wall_restrictions_cache_min");
#endif
}

// [INTENT] Cache accessor for collision regions. Snaps orig_radius to the nearest
// ceil-radius bucket, then looks up m_collision_cache. If not found and precalculate()
// was already called, logs a performance warning, then falls back to lazy calculateCollision().
// [COUPLING] Uses const_cast to call calculateCollision() from a const method.
//   This is safe as long as no concurrent readers hold references to the same
//   (radius, layer_idx) entry — which is guaranteed by the per-cache mutex.
// [CONCURRENCY] Thread-safe: RadiusLayerPolygonCache::getArea() locks its mutex.
const Polygons& TreeModelVolumes::getCollision(const coord_t orig_radius, LayerIndex layer_idx, bool min_xy_dist) const
{
    const coord_t radius = this->ceilRadius(orig_radius, min_xy_dist);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_collision_cache.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate collision at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Collision requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateCollision(radius, layer_idx, []{});
    return getCollision(orig_radius, layer_idx, min_xy_dist);
}

// Get a collision area at a given layer for a radius that is a lower or equial to the key radius.
// It is expected that the collision area is precalculated for a given layer at least for the radius zero.
// Used for pushing tree supports away from object during the final Organic optimization step.
std::optional<std::pair<coord_t, std::reference_wrapper<const Polygons>>> TreeModelVolumes::get_collision_lower_bound_area(LayerIndex layer_id, coord_t max_radius) const
{
    return m_collision_cache.get_lower_bound_area({ max_radius, layer_id });
}

// Private. Only called internally by calculateAvoidance() and calculateAvoidanceToModel(), radius is already snapped to grid.
const Polygons& TreeModelVolumes::getCollisionHolefree(coord_t radius, LayerIndex layer_idx) const
{
    assert(radius == this->ceilRadius(radius));
    assert(radius < m_increase_until_radius + m_current_min_xy_dist_delta);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_collision_cache_holefree.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate collision holefree at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Holefree Collision requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateCollisionHolefree({ radius, layer_idx });
    return getCollisionHolefree(radius, layer_idx);
}

// [INTENT] Cache accessor for avoidance regions. Returns the polygon set that a branch of
// orig_radius must avoid at layer_idx to eventually reach the build plate (to_model=false)
// or some model surface (to_model=true).
// Layer 0 is a special case: avoidance at layer 0 equals collision (no further path to route).
// FastSafe type is automatically downgraded to Fast if radius >= m_increase_until_radius
// (no holes exist at that radius by definition).
// Falls back to lazy calculateAvoidance() if the entry is missing and m_precalculated is true.
// [COUPLING] Uses avoidance_cache() dispatch table to select among 6 cache instances by
// (type × to_model). The dispatch is a switch inside a helper that returns a non-const ref.
// [CONCURRENCY] Thread-safe via RadiusLayerPolygonCache::getArea() mutex.
// [HAZARD] const_cast<TreeModelVolumes*>(this)->calculateAvoidance() — see getCollision notes.
const Polygons& TreeModelVolumes::getAvoidance(const coord_t orig_radius, LayerIndex layer_idx, AvoidanceType type, bool to_model, bool min_xy_dist) const
{
    if (layer_idx == 0) // What on the layer directly above buildplate do i have to avoid to reach the buildplate ...
        return getCollision(orig_radius, layer_idx, min_xy_dist);

    const coord_t radius = this->ceilRadius(orig_radius, min_xy_dist);
    if (type == AvoidanceType::FastSafe && radius >= m_increase_until_radius + m_current_min_xy_dist_delta)
        // no holes anymore by definition at this request
        type = AvoidanceType::Fast;

    if (std::optional<std::reference_wrapper<const Polygons>> result = 
            this->avoidance_cache(type, to_model).getArea({ radius, layer_idx }); 
        result)
        return (*result).get();

    if (m_precalculated) {
        if (to_model) {
            BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Avoidance to model at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
            tree_supports_show_error("Not precalculated Avoidance(to model) requested."sv, false);
        } else {
            BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Avoidance at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
            tree_supports_show_error("Not precalculated Avoidance(to buildplate) requested."sv, false);
        }
    }
    const_cast<TreeModelVolumes*>(this)->calculateAvoidance({ radius, layer_idx }, ! to_model, to_model);
    // Retrive failed and correct result was calculated. Now it has to be retrived.
    return getAvoidance(orig_radius, layer_idx, type, to_model, min_xy_dist);
}

// [INTENT] Cache accessor for placeable areas — where a branch foot of orig_radius can
// actually rest on a model surface. For radius==0, delegates directly to getCollision(0,...)
// which computes placable areas as a side-effect of calculateCollision().
// For radius>0, calls calculatePlaceables() lazily if not found.
// [COUPLING] Depends on placable-area side-effect in calculateCollision(radius=0).
//   The lazy fallback path calls calculatePlaceables(radius, layer_idx) which
//   calls getPlaceableAreas(0, layer_idx) internally — a recursive call chain.
// [CONCURRENCY] Thread-safe via cache mutex.
// [HAZARD] throw_on_cancel lambda is passed through the lazy calculatePlaceables()
//   call but NOT used in the fast-path cache lookup. Cancellation checking is coarse.
const Polygons& TreeModelVolumes::getPlaceableAreas(const coord_t orig_radius, LayerIndex layer_idx, std::function<void()> throw_on_cancel) const
{
    const coord_t radius = ceilRadius(orig_radius);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_placeable_areas_cache.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Placeable Areas at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error(format("Not precalculated Placeable areas requested, radius %1%, layer %2%", radius, layer_idx), false);
    }
    if (orig_radius == 0)
        // Placable areas for radius 0 are calculated in the general collision code.
        return this->getCollision(0, layer_idx, true);
    const_cast<TreeModelVolumes*>(this)->calculatePlaceables(radius, layer_idx, throw_on_cancel);
    return getPlaceableAreas(orig_radius, layer_idx, throw_on_cancel);
}

// [INTENT] Cache accessor for wall-restriction areas — the intersection of current-layer
// collision (radius 0) with previous-layer collision (at branch radius). This region
// represents printed walls that a branch cannot tunnel through between layers.
// Two caches: m_wall_restrictions_cache (regular xy_dist) and m_wall_restrictions_cache_min
// (min xy_dist). The min variant is only used when m_current_min_xy_dist_delta > 0.
// [COUPLING] layer_idx == 0 is guarded by assert(layer_idx > 0); returns collision as fallback
//   (safe but semantically wrong — wall restrictions don't exist below layer 0).
// [CONCURRENCY] Thread-safe via cache mutex. const_cast for lazy calculateWallRestrictions().
// [HAZARD] min_xy_dist is masked: `min_xy_dist &= m_current_min_xy_dist_delta > 0`.
//   If delta is 0, the min cache is always bypassed, which is correct but subtle.
const Polygons& TreeModelVolumes::getWallRestriction(const coord_t orig_radius, LayerIndex layer_idx, bool min_xy_dist) const
{
    assert(layer_idx > 0);
    if (layer_idx == 0) 
        // Should never be requested as there will be no going below layer 0 ..., 
        // but just to be sure some semi-sane catch. Alternative would be empty Polygon.
        return getCollision(orig_radius, layer_idx, min_xy_dist);

    min_xy_dist &= m_current_min_xy_dist_delta > 0;

    const coord_t radius = ceilRadius(orig_radius);
    if (std::optional<std::reference_wrapper<const Polygons>> result = 
        (min_xy_dist ? m_wall_restrictions_cache_min : m_wall_restrictions_cache).getArea({ radius, layer_idx });
        result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Wall restricions at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error(
            min_xy_dist ? 
                "Not precalculated Wall restriction of minimum xy distance requested )." :
                "Not precalculated Wall restriction requested )."sv
            , false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateWallRestrictions({ radius, layer_idx });
    return getWallRestriction(orig_radius, layer_idx, min_xy_dist); // Retrieve failed and correct result was calculated. Now it has to be retrieved.
}

void TreeModelVolumes::calculateCollision(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t> &range) {
        for (size_t ikey = range.begin(); ikey != range.end(); ++ ikey) {
            const LayerIndex radius        = keys[ikey].first;
            const size_t     max_layer_idx = keys[ikey].second;
            // recursive call to parallel_for.
            calculateCollision(radius, max_layer_idx, throw_on_cancel);
        }
    });
}

// [INTENT] Core collision computation for a single radius up to max_layer_idx.
// Three-phase per mesh-group:
//   Phase 1 — parallel offset of each layer's union(machine_border + outlines) by (radius+xy_dist).
//   Phase 2 — parallel summation over z_distance_top/bottom_layers bands, then union with
//             anti_overhang (support blockers) on the last mesh group.
//   Phase 3 — (only when radius==0 and m_support_rests_on_model) compute placeable areas as
//             diff(inflated_below_layer, current_collision+anti_overhang).
// Stores results to m_collision_cache (and m_placeable_areas_cache for radius==0).
//
// [HAZARD] BUG: processing_last_mesh at line 578:
//   `const bool processing_last_mesh = outline_idx == layer_outline_indices.size();`
//   outline_idx is a VALUE from layer_outline_indices (a sorted permutation of mesh indices),
//   NOT the loop counter. layer_outline_indices.size() is the count of meshes.
//   Unless a mesh's actual index (e.g. mesh #3) equals the total number of meshes,
//   this is NEVER true. The intended check is:
//     `outline_idx == layer_outline_indices.back()`
//   (i.e., are we processing the last mesh in the sorted order?)
//   Consequence: anti_overhang is never applied, and polygon simplification / final union
//   only happens if the condition is accidentally true. For the current single-mesh case
//   (layer_outline_indices = {0}, size = 1), this fires correctly ONLY if mesh index == 1,
//   which it isn't (index is 0). So for ALL normal single-mesh prints, anti_overhang is
//   silently skipped and the final simplification does NOT run. Latent because multi-mesh
//   grouping is disabled, but still affects the single-mesh path.
//
// [CONCURRENCY] Nested TBB parallel_for (outer over mesh groups, inner over layers).
//   TBB handles work-stealing for nested task arenas. The outer for is sequential (range
//   over mesh groups); only the inner parallel_for is dispatched to TBB threads.
//
// [MEMORY] collision_areas_offsetted is a stack-local LayerPolygonCache; data is a
//   stack-local LayerPolygonCache. Both are moved into cache on function exit.
//   Peak memory during execution = 2× the layer range × sizeof(Polygons).
void TreeModelVolumes::calculateCollision(const coord_t radius, const LayerIndex max_layer_idx, std::function<void()> throw_on_cancel)
{
//    assert(radius == this->ceilRadius(radius));

    // Process the outlines from least layers to most layers so that the final union will run over the longest vector.
    std::vector<size_t> layer_outline_indices(m_layer_outlines.size(), 0);
    std::iota(layer_outline_indices.begin(), layer_outline_indices.end(), 0);
    std::sort(layer_outline_indices.begin(), layer_outline_indices.end(),
        [this](size_t i, size_t j) { return m_layer_outlines[i].second.size() < m_layer_outlines[j].second.size(); });

    // Layer range for which the collisions will be calculated.
    LayerPolygonCache           data;
    data.allocate(m_collision_cache.getMaxCalculatedLayer(radius) + 1, max_layer_idx + 1);

    const bool                  calculate_placable = m_support_rests_on_model && radius == 0;
    LayerPolygonCache           data_placeable;
    if (calculate_placable)
        data_placeable.allocate(data.begin(), data.end());

    for (size_t outline_idx : layer_outline_indices)
        if (const std::vector<Polygons> &outlines = m_layer_outlines[outline_idx].second; ! outlines.empty()) {
            const TreeSupportMeshGroupSettings  &settings = m_layer_outlines[outline_idx].first;
            const coord_t       layer_height              = settings.layer_height;
            const int           z_distance_bottom_layers  = int(round(double(settings.support_bottom_distance) / double(layer_height)));
            const int           z_distance_top_layers     = int(round(double(settings.support_top_distance) / double(layer_height)));
            const coord_t       xy_distance               = outline_idx == m_current_outline_idx ? m_current_min_xy_dist : 
                // technically this causes collision for the normal xy_distance to be larger by m_current_min_xy_dist_delta for all 
                // not currently processing meshes as this delta will be added at request time.
                // avoiding this would require saving each collision for each outline_idx separately.
                // and later for each avoidance... But avoidance calculation has to be for the whole scene and can NOT be done for each outline_idx separately and combined later.
                // so avoiding this inaccuracy seems infeasible as it would require 2x the avoidance calculations => 0.5x the performance.
                //FIXME support_xy_distance is not corrected for "soluble" flag, see TreeSupportSettings constructor.
                settings.support_xy_distance;

            // 1) Calculate offsets of collision areas in parallel.
            LayerPolygonCache collision_areas_offsetted;
            collision_areas_offsetted.allocate(
                std::max<LayerIndex>(0, data.begin() - z_distance_bottom_layers),
                std::min<LayerIndex>(outlines.size(), data.end() + z_distance_top_layers));
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(collision_areas_offsetted.begin(), collision_areas_offsetted.end()),
                [&outlines, &machine_border = std::as_const(m_machine_border), offset_value = radius + xy_distance, &collision_areas_offsetted, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex> &range) {
                for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                    Polygons collision_areas = machine_border;
                    append(collision_areas, outlines[layer_idx]);
                    // jtRound is not needed here, as the overshoot can not cause errors in the algorithm, because no assumptions are made about the model.
                    // if a key does not exist when it is accessed it is added!
                    collision_areas_offsetted[layer_idx] = offset_value == 0 ?
                            union_(collision_areas) :
                            offset(union_ex(collision_areas), offset_value, ClipperLib::jtMiter, 1.2);
                    if(throw_on_cancel)
                        throw_on_cancel();
                }
            });

            // 2) Sum over top / bottom ranges.
            const bool processing_last_mesh = outline_idx == layer_outline_indices.size();
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(data.begin(), data.end()),
                [&collision_areas_offsetted, &outlines, &machine_border = m_machine_border, &anti_overhang = m_anti_overhang, radius, 
                    xy_distance, z_distance_bottom_layers, z_distance_top_layers, min_resolution = m_min_resolution, &data, processing_last_mesh, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex>& range) {
                    for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                        Polygons collisions;
                        for (int i = - z_distance_bottom_layers; i <= 0; ++ i)
                            if (int j = layer_idx + i; collision_areas_offsetted.has(j))
                                append(collisions, collision_areas_offsetted[j]);
                        for (int i = 1; i <= z_distance_top_layers; ++ i)
                            if (int j = layer_idx + i; j < int(outlines.size())) {
                                Polygons collision_areas_original = machine_border;
                                append(collision_areas_original, outlines[j]);

                                // If just the collision (including the xy distance) of the layers above is accumulated, it leads to the
                                // following issue:
                                // Example: assuming the z distance is 2 layer
                                // + = xy_distance
                                // - = model
                                // o = overhang of the area two layers above that should result in tips on this layer
                                //
                                //  +-----+
                                //   +-----+
                                //    +-----+
                                //   o +-----+
                                // If just the collision above is accumulated the overhang will get overwritten by the xy_distance of the
                                // layer below the overhang...
                                //
                                // This only causes issues if the overhang area is thinner than xy_distance
                                // Just accumulating areas of the model above without the xy distance is also problematic, as then support
                                // may get closer to the model (on the diagonal downwards) than the user intended. Example (s = support):
                                //  +-----+
                                //   +-----+
                                //   +-----+
                                //   s+-----+

                                // technically the calculation below is off by one layer, as the actual distance between plastic one layer
                                // down is 0 not layer height, as this layer is filled with said plastic. But otherwise a part of the
                                // overhang that is expected to be supported is overwritten by the remaining part of the xy distance of the
                                // layer below the to be supported area.
                                coord_t required_range_x =
                                    (xy_distance - ((i - (z_distance_top_layers == 1 ? 0.5 : 0)) * xy_distance / z_distance_top_layers)); 
                                    // the conditional -0.5 ensures that plastic can never touch on the diagonal
                                    // downward when the z_distance_top_layers = 1. It is assumed to be better to
                                    // not support an overhang<90 degree than to risk fusing to it.
                                append(collisions, offset(union_ex(collision_areas_original), radius + required_range_x, ClipperLib::jtMiter, 1.2));
                            }
                        collisions = processing_last_mesh && layer_idx < int(anti_overhang.size()) ? 
                                union_(collisions, offset(union_ex(anti_overhang[layer_idx]), radius, ClipperLib::jtMiter, 1.2)) : 
                                union_(collisions);
                        auto &dst = data[layer_idx];
                        if (processing_last_mesh) {
                            if (! dst.empty())
                                collisions = union_(collisions, dst);
                            dst = polygons_simplify(collisions, min_resolution, polygons_strictly_simple);
                        } else
                            append(dst, std::move(collisions));
                        if (throw_on_cancel)
                            throw_on_cancel();
                    }
                });

            // 3) Optionally calculate placables.
            if (calculate_placable) {
                // Now calculate the placable areas.
                tbb::parallel_for(tbb::blocked_range<LayerIndex>(std::max(z_distance_bottom_layers + 1, data.begin()), data.end()),
                    [&collision_areas_offsetted, &outlines, &anti_overhang = m_anti_overhang, processing_last_mesh,
                     min_resolution = m_min_resolution, z_distance_bottom_layers, xy_distance, &data_placeable, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex>& range) {
                    for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                        LayerIndex layer_idx_below = layer_idx - z_distance_bottom_layers - 1;
                        assert(layer_idx_below >= 0);
                        const Polygons &current = collision_areas_offsetted[layer_idx];
                        const Polygons &below   = outlines[layer_idx_below];
                        Polygons placable = diff(
                            // Inflate the surface to sit on by the separation distance to increase chance of a support being placed on a sloped surface.
                            offset(below, xy_distance), 
                            layer_idx_below < int(anti_overhang.size()) ? union_(current, anti_overhang[layer_idx_below]) : current);
                        auto &dst     = data_placeable[layer_idx];
                        if (processing_last_mesh) {
                            if (! dst.empty())
                                placable = union_(placable, dst);
                            dst = polygons_simplify(placable, min_resolution, polygons_strictly_simple);
                        } else
                            append(dst, placable);
                        if (throw_on_cancel)
                            throw_on_cancel();
                    }
                });
            } else {
                // Calculating just the collision areas.
            }
        }
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    {
        std::lock_guard<std::mutex> critical_section(*m_critical_progress);
        if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL) {
            m_precalculation_progress += TREE_PROGRESS_PRECALC_COLL / keys.size();
            Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
        }
    }
#endif
    if (throw_on_cancel)
        throw_on_cancel();
    m_collision_cache.insert(std::move(data), radius);
    if (calculate_placable)
        m_placeable_areas_cache.insert(std::move(data_placeable), radius);
}

// [INTENT] Build the holefree (no-holes) collision cache by shrinking the regular collision
// at m_increase_until_radius down to each requested smaller radius. This ensures that
// branches at small radii avoid holes that they would fall into if they were grown to
// m_increase_until_radius.
// Algorithm: collision_holefree[r] = simplified(offset(collision[m_increase_until_radius],
//                                               5 - ceil(m_increase_until_radius - r), jtRound))
// The "5 - increase_radius_ceil" offset is a small contraction to remove boundary noise.
// [STATE] Reads m_collision_cache (via getCollision); writes m_collision_cache_holefree.
// [CONCURRENCY] Outer tbb::parallel_for over layer ranges; per-layer inner loop over keys.
// [HAZARD] increase_radius_ceil is computed as ceilRadius(m_increase_until_radius, false) - r.
//   For very small r this offset can be large; for r ≈ m_increase_until_radius it approaches 0.
//   If increase_radius_ceil > 5, the inner offset becomes positive (expansion, not contraction)
//   — this is by design for small radii but may produce unexpected polygons for degenerate inputs.
void TreeModelVolumes::calculateCollisionHolefree(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    LayerIndex max_layer = 0;
    for (long long unsigned int i = 0; i < keys.size(); i++)
        max_layer = std::max(max_layer, keys[i].second);

    tbb::parallel_for(tbb::blocked_range<LayerIndex>(0, max_layer + 1, keys.size()),
        [&](const tbb::blocked_range<LayerIndex> &range) {
        std::vector<std::pair<RadiusLayerPair, Polygons>> data;
        data.reserve(range.size() * keys.size());
        for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            for (RadiusLayerPair key : keys)
                if (layer_idx <= key.second) {
                    // Logically increase the collision by m_increase_until_radius
                    coord_t radius = key.first;
                    assert(radius == this->ceilRadius(radius));
                    assert(radius < m_increase_until_radius + m_current_min_xy_dist_delta);
                    coord_t increase_radius_ceil = ceilRadius(m_increase_until_radius, false) - radius;
                    assert(increase_radius_ceil > 0);
                    // this union is important as otherwise holes(in form of lines that will increase to holes in a later step) can get unioned onto the area.
                    data.emplace_back(RadiusLayerPair(radius, layer_idx), polygons_simplify(
                        offset(union_ex(this->getCollision(m_increase_until_radius, layer_idx, false)),
                            5 - increase_radius_ceil, ClipperLib::jtRound, m_min_resolution),
                        m_min_resolution, polygons_strictly_simple));
                    if (throw_on_cancel)
                        throw_on_cancel();
                }
        }
        m_collision_cache_holefree.insert(std::move(data));
    });
}

// [INTENT] Compute all avoidance regions for the given (radius, max_layer) keys.
// For each key × 3 AvoidanceTypes × 2 targets (to_buildplate, to_model), creates an
// AvoidanceTask struct. Tasks are dispatched to tbb::parallel_for, but each task's
// inner loop (propagating avoidance bottom-up layer by layer) is INHERENTLY SERIAL.
//
// Avoidance propagation algorithm per task:
//   latest_avoidance[layer] = union(collision[layer], offset(latest_avoidance[layer-1], -move_step))
//   repeated move_steps times per layer to avoid tunneling through walls.
//   For to_model=true: diff with placeableAreas[layer] to allow landing on model surfaces.
//
// [STATE] Reads m_collision_cache or m_collision_cache_holefree (depending on task type).
//   Reads m_placeable_areas_cache. Writes avoidance_cache(type, to_model).
//
// [CONCURRENCY] Outer tbb::parallel_for dispatches independent tasks concurrently.
//   Each task's inner for-loop is serial (layer dependency chain).
//   Tasks that share the same avoidance cache call cache.insert() which locks m_mutex.
//
// [HAZARD] iter_idx computation for task assignment (lines 738-744) is hardcoded
//   for 2 targets × keys.size() × 3 types = 6 * keys.size() tasks.
//   If to_build_plate or to_model is false, the corresponding tasks are filtered
//   by the (task.to_model ? to_model : to_build_plate) guard. This is correct but
//   fragile — the "6×" constant embeds assumptions about AvoidanceType::Count == 3.
//
// [HAZARD] move_step = 1.9 × max(radius, m_current_min_xy_dist). If radius is very
//   small and m_current_min_xy_dist is also small, move_step approaches 0 and
//   the guard `if (move_step < EPSILON) return;` prevents division-by-zero, but
//   that task's avoidance is silently skipped — left as empty in the cache.
void TreeModelVolumes::calculateAvoidance(const std::vector<RadiusLayerPair> &keys, bool to_build_plate, bool to_model, std::function<void()> throw_on_cancel)
{
    // For every RadiusLayer pair there are 3 avoidances that have to be calculated.
    // Prepare tasks for parallelization.
    struct AvoidanceTask {
        AvoidanceType   type;
        coord_t         radius;
        LayerIndex      max_required_layer;
        bool            to_model;
        LayerIndex      start_layer;

        bool slow()     const { return this->type == AvoidanceType::Slow; }
        bool holefree() const { return this->type == AvoidanceType::FastSafe; }
    };

    std::vector<AvoidanceTask> avoidance_tasks;
    avoidance_tasks.reserve((int(to_build_plate) + int(to_model)) * keys.size() * size_t(AvoidanceType::Count));

    for (int iter_idx = 0; iter_idx < 2 * int(keys.size()) * int(AvoidanceType::Count); ++ iter_idx) {
        AvoidanceTask task{
            AvoidanceType(iter_idx % int(AvoidanceType::Count)),
            keys[iter_idx / 6].first,  // radius
            keys[iter_idx / 6].second, // max_layer
            ((iter_idx / 3) & 1) != 0  // to_model
        };
        // Ensure start_layer is at least 1 as if no avoidance was calculated yet getMaxCalculatedLayer() returns -1.
        task.start_layer = std::max<LayerIndex>(1, 1 + avoidance_cache(task.type, task.to_model).getMaxCalculatedLayer(task.radius));
        if (task.start_layer > task.max_required_layer) {
            BOOST_LOG_TRIVIAL(debug) << "Calculation requested for value already calculated?";
            continue;
        }
        if ((task.to_model ? to_model : to_build_plate) &&
            (! task.holefree() || task.radius < m_increase_until_radius + m_current_min_xy_dist_delta))
            avoidance_tasks.emplace_back(task);
    }

    if(throw_on_cancel)
        throw_on_cancel();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, avoidance_tasks.size(), 1),
        [this, &avoidance_tasks, &throw_on_cancel](const tbb::blocked_range<size_t> &range) {
        for (size_t task_idx = range.begin(); task_idx < range.end(); ++ task_idx) {
            const AvoidanceTask &task = avoidance_tasks[task_idx];
            assert(! task.holefree() || task.radius < m_increase_until_radius + m_current_min_xy_dist_delta);
            if (task.to_model)
                // ensuring Placeableareas are calculated
                //FIXME pass throw_on_cancel
                getPlaceableAreas(task.radius, task.max_required_layer, throw_on_cancel);
            // The following loop propagating avoidance regions bottom up is inherently serial.
            const bool  collision_holefree = (task.slow() || task.holefree()) && task.radius < m_increase_until_radius + m_current_min_xy_dist_delta;
            const float max_move           = task.slow() ? m_max_move_slow : m_max_move;
            // Limiting the offset step so that unioning the shrunk latest_avoidance with the current layer collisions
            // will not create gaps in the resulting avoidance region letting a tree support branch tunneling through an object wall.
            float move_step      = 1.9 * std::max(task.radius, m_current_min_xy_dist);
            if (move_step < EPSILON) return;
            int   move_steps     = round_up_divide<int>(max_move, move_step);
            assert(move_steps > 0);
            float last_move_step = max_move - (move_steps - 1) * move_step;
            if (last_move_step < scaled<float>(0.05)) {
                assert(move_steps > 1);
                if (move_steps > 1) {
                    // Avoid taking a very short last step, stretch the other steps a bit instead.
                    move_step = max_move / (-- move_steps);
                    last_move_step = move_step;
                }
            }
            // minDist as the delta was already added, also avoidance for layer 0 will return the collision.
            Polygons    latest_avoidance   = getAvoidance(task.radius, task.start_layer - 1, task.type, task.to_model, true);
            std::vector<std::pair<RadiusLayerPair, Polygons>> data;
            data.reserve(task.max_required_layer + 1 - task.start_layer);
            for (LayerIndex layer_idx = task.start_layer; layer_idx <= task.max_required_layer; ++ layer_idx) {
                // Merge current layer collisions with shrunk last_avoidance.
                const Polygons &current_layer_collisions = collision_holefree ? getCollisionHolefree(task.radius, layer_idx) : getCollision(task.radius, layer_idx, true);
                // For mildly steep branch angles only one step will be taken.
                for (int istep = 0; istep < move_steps; ++ istep)
                    latest_avoidance = union_(current_layer_collisions,
                        offset(latest_avoidance,
                            istep + 1 == move_steps ? - last_move_step : - move_step,
                            ClipperLib::jtRound, m_min_resolution));
                if (task.to_model)
                    latest_avoidance = diff(latest_avoidance, getPlaceableAreas(task.radius, layer_idx, throw_on_cancel));
                latest_avoidance = polygons_simplify(latest_avoidance, m_min_resolution, polygons_strictly_simple);
                data.emplace_back(RadiusLayerPair{task.radius, layer_idx}, latest_avoidance);
                if (throw_on_cancel)
                    throw_on_cancel();
            }
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
            {
                std::lock_guard<std::mutex> critical_section(*m_critical_progress);
                if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL + TREE_PROGRESS_PRECALC_AVO) {
                    m_precalculation_progress += to_model ? 
                        0.4 * TREE_PROGRESS_PRECALC_AVO / (keys.size() * 3) :
                        m_support_rests_on_model ? 0.4 : 1 * TREE_PROGRESS_PRECALC_AVO / (keys.size() * 3);
                    Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
                }
            }
#endif
            avoidance_cache(task.type, task.to_model).insert(std::move(data));
        }
    });
}


// [INTENT] Batch dispatch: parallel_for over keys, calling calculatePlaceables(radius, max_layer).
void TreeModelVolumes::calculatePlaceables(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&, keys](const tbb::blocked_range<size_t>& range) {
            for (size_t key_idx = range.begin(); key_idx < range.end(); ++ key_idx)
                this->calculatePlaceables(keys[key_idx].first, keys[key_idx].second, throw_on_cancel);
        });
}

// [INTENT] Compute placeable areas for a specific (radius, max_required_layer) pair.
// Placeable areas at layer L = offset(placeableAreas(radius=0, layer=L), -(radius+xy_dist)).
// The inner shrink by (radius + xy_dist) removes thin slivers that look flat but are
// actually the far edge of xy_distance that can't genuinely hold a branch foot.
// Layer 0 special case: placeables = diff(machine_border, collision(radius=0, layer=0)).
// [COUPLING] Reads m_placeable_areas_cache (via getPlaceableAreas(0, layer_idx)).
//   This is a recursive call: calculatePlaceables(r>0) → getPlaceableAreas(0) → [from cache].
//   The radius-0 placeables must already be in cache (set as a side-effect of calculateCollision).
// [CONCURRENCY] tbb::parallel_for over layers. Each layer is independent.
// [HAZARD] If m_machine_border is empty (always, per calculateMachineBorderCollision()),
//   layer 0 placeables = diff({}, collision) = {} always empty. Branches can never start
//   from layer 0 when using to_model=true with radius>0. Acceptable workaround since
//   support at layer 0 is edge case.
void TreeModelVolumes::calculatePlaceables(const coord_t radius, const LayerIndex max_required_layer, std::function<void()> throw_on_cancel)
{
    LayerIndex start_layer = 1 + m_placeable_areas_cache.getMaxCalculatedLayer(radius);
    if (start_layer > max_required_layer) {
        BOOST_LOG_TRIVIAL(debug) << "Requested calculation for value already calculated ?";
        return;
    }

    std::vector<Polygons> data(max_required_layer + 1 - start_layer, Polygons{});

    if (start_layer == 0)
        data[0] = diff(m_machine_border, getCollision(radius, 0, true));

    tbb::parallel_for(tbb::blocked_range<LayerIndex>(std::max(1, start_layer), max_required_layer + 1),
        [this, &data, radius, start_layer, &throw_on_cancel](const tbb::blocked_range<LayerIndex>& range) {
            for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                data[layer_idx - start_layer] = offset(
                    union_ex(getPlaceableAreas(0, layer_idx, throw_on_cancel)),
                    // As a placeable area is calculated by (collision of the layer below) - (collision of the current layer) and the collision is offset by xy_distance, 
                    // it can happen that a small line is considered a flat area to place something onto, even though it is mostly 
                    // xy_distance that cant support it. Making the area smaller by xy_distance fixes this.
                    - (radius + m_current_min_xy_dist + m_current_min_xy_dist_delta),
                    jtMiter, 1.2);
                if(throw_on_cancel)
                    throw_on_cancel();
            }
        });
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    {
        std::lock_guard<std::mutex> critical_section(*m_critical_progress);
        if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL + TREE_PROGRESS_PRECALC_AVO) {
            m_precalculation_progress += 0.2 * TREE_PROGRESS_PRECALC_AVO / (keys.size());
            Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
        }
    }
#endif
    m_placeable_areas_cache.insert(std::move(data), start_layer, radius);
}

// [INTENT] Compute wall restriction areas: regions a branch cannot pass through between
// consecutive layers. Defined as intersection(collision(r=0, layer), collision(radius, layer-1)).
// Conceptually: if a wall is printed at layer L (collision at r=0), and at layer L-1
// the branch's swept area (collision at radius) overlaps that wall, the branch would
// have moved through the wall — which is physically impossible.
//
// Two variants:
//   - Regular: intersection(collision(0, layer, false), collision(radius, layer-1, true))
//   - Min:     intersection(collision(0, layer, true),  collision(radius, layer-1, true))
// The "true" on the second arg uses min_xy_dist (tighter fit), "false" uses the regular dist.
//
// [COUPLING] Reads m_collision_cache via getCollision(). Must be populated first by calculateCollision().
// [CONCURRENCY] Outer tbb::parallel_for over keys (independent radius groups).
//   Inner tbb::parallel_for over layers within each radius group — nested TBB.
// [HAZARD] The inner TBB nesting means for large key sets, TBB may spawn many nested
//   parallel_for tasks. Depends on TBB's task arena for fair work-stealing.
void TreeModelVolumes::calculateWallRestrictions(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    // Wall restrictions are mainly important when they represent actual walls that are printed, and not "just" the configured z_distance, because technically valid placement is no excuse for moving through a wall.
    // As they exist to prevent accidentially moving though a wall at high speed between layers like thie (x = wall,i = influence area,o= empty space,d = blocked area because of z distance) Assume maximum movement distance is two characters and maximum safe movement distance of one character

    /* Potential issue addressed by the wall restrictions: Influence area may lag through a wall
     *  layer z+1:iiiiiiiiiiioooo
     *  layer z+0:xxxxxiiiiiiiooo
     *  layer z-1:ooooixxxxxxxxxx
     */

    // The radius for the upper collission has to be 0 as otherwise one may not enter areas that may be forbidden on layer_idx but not one below (c = not an influence area even though it should ):
    /*
     *  layer z+1:xxxxxiiiiiioo
     *  layer z+0:dddddiiiiiiio
     *  layer z-1:dddocdddddddd
     */
    // Also there can not just the collision of the lower layer be used because if it were:
    /*
     *  layer z+1:dddddiiiiiiiiiio
     *  layer z+0:xxxxxddddddddddc
     *  layer z-1:dddddxxxxxxxxxxc
     */
    // Or of the upper layer be used because if it were:
    /*
     *  layer z+1:dddddiiiiiiiiiio
     *  layer z+0:xxxxcddddddddddc
     *  layer z-1:ddddcxxxxxxxxxxc
     */

    // And just offseting with maximum movement distance (and not in multiple steps) could cause:
    /*
     *  layer z:   oxiiiiiiiiioo
     *  layer z-1: ixiiiiiiiiiii
     */

    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&, keys](const tbb::blocked_range<size_t> &range) {
        for (size_t key_idx = range.begin(); key_idx < range.end(); ++ key_idx) {
            const coord_t    radius             = keys[key_idx].first;
            const LayerIndex max_required_layer = keys[key_idx].second;
            const coord_t    min_layer_bottom   = std::max(1, m_wall_restrictions_cache.getMaxCalculatedLayer(radius));
            const size_t     buffer_size        = max_required_layer + 1 - min_layer_bottom;
            std::vector<Polygons> data(buffer_size, Polygons{});
            std::vector<Polygons> data_min;
            if (m_current_min_xy_dist_delta > 0)
                data_min.assign(buffer_size, Polygons{});
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(min_layer_bottom, max_required_layer + 1),
                [this, &data, &data_min, radius, min_layer_bottom, &throw_on_cancel](const tbb::blocked_range<LayerIndex> &range) {
                for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    data[layer_idx - min_layer_bottom] = polygons_simplify(
                        // radius contains m_current_min_xy_dist_delta already if required
                        intersection(getCollision(0, layer_idx, false), getCollision(radius, layer_idx - 1, true)),
                        m_min_resolution, polygons_strictly_simple);
                    if (! data_min.empty())
                        data_min[layer_idx - min_layer_bottom] = 
                            polygons_simplify(
                                intersection(getCollision(0, layer_idx, true), getCollision(radius, layer_idx - 1, true)),
                                m_min_resolution, polygons_strictly_simple);
                    if (throw_on_cancel)
                        throw_on_cancel();
                }
            });
            m_wall_restrictions_cache.insert(std::move(data), min_layer_bottom, radius);
            if (! data_min.empty())
                m_wall_restrictions_cache_min.insert(std::move(data_min), min_layer_bottom, radius);
        }
    });
}

// [INTENT] Snap a radius upward to the nearest grid point used by the cache.
// Two-phase grid:
//   Phase 1 (linear): from m_radius_0 to SUPPORT_TREE_EXPONENTIAL_THRESHOLD
//     steps of stepsize = initial_radius_delta / num_steps (approx 0.5mm).
//   Phase 2 (exponential): multiply by SUPPORT_TREE_EXPONENTIAL_FACTOR (1.5×) each step
//     until out >= radius.
// Radii in m_ignorable_radii (pre-computed set of tip-range radii that will never be
// requested) are skipped (they alias to the next grid point above them).
//
// [HAZARD] Aliasing: two distinct branch radii that both ceil to the same grid point
//   will share the same cache entry. The collision polygon is LARGER than needed for
//   the smaller radius — conservative but may prevent valid branch placements.
//   This is intentional to bound cache size; the trade-off is reduced reachability.
//
// [HAZARD] The linear phase has a subtle off-by-one risk: if initial_radius_delta <=
//   SUPPORT_TREE_COLLISION_RESOLUTION, the code falls through to `out += SUPPORT_TREE_COLLISION_RESOLUTION`
//   and then enters the exponential loop. For most printers initial_radius_delta >> COLLISION_RESOLUTION,
//   so this path is rarely exercised.
coord_t TreeModelVolumes::ceilRadius(const coord_t radius) const
{
    if (radius == 0)
        return 0;

    coord_t out = m_radius_0;
    if (radius > m_radius_0) {
        // generate SUPPORT_TREE_PRE_EXPONENTIAL_STEPS of radiis before starting to exponentially increase them.
        coord_t initial_radius_delta = SUPPORT_TREE_EXPONENTIAL_THRESHOLD - m_radius_0;
        auto ignore = [this](coord_t r) { return std::binary_search(m_ignorable_radii.begin(), m_ignorable_radii.end(), r); };
        if (initial_radius_delta > SUPPORT_TREE_COLLISION_RESOLUTION) {
            const int num_steps = round_up_divide(initial_radius_delta, SUPPORT_TREE_EXPONENTIAL_THRESHOLD);
            const int stepsize  = initial_radius_delta / num_steps;
            out += stepsize;
            for (auto step = 0; step < num_steps; ++ step) {
                if (out >= radius && ! ignore(out))
                    return out;
                out += stepsize;
            }
        } else
            out += SUPPORT_TREE_COLLISION_RESOLUTION;
        while (out < radius || ignore(out)) {
            assert(out * SUPPORT_TREE_EXPONENTIAL_FACTOR > out + SUPPORT_TREE_COLLISION_RESOLUTION);
            out = out * SUPPORT_TREE_EXPONENTIAL_FACTOR;
        }
    }
    return out;
}

void TreeModelVolumes::RadiusLayerPolygonCache::allocate_layers(size_t num_layers)
{
    if (num_layers > m_data.size()) {
        if (num_layers > m_data.capacity())
            reserve_power_of_2(m_data, num_layers);
        m_data.resize(num_layers, {});
    }
}

// For debugging purposes, sorted by layer index, then by radius.
std::vector<std::pair<TreeModelVolumes::RadiusLayerPair, std::reference_wrapper<const Polygons>>> TreeModelVolumes::RadiusLayerPolygonCache::sorted() const
{
    std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> out;
    for (auto &layer : m_data) {
        auto layer_idx = LayerIndex(&layer - m_data.data());
        for (auto &radius_polygons : layer)
            out.emplace_back(std::make_pair(radius_polygons.first, layer_idx), radius_polygons.second);
    }
    assert(std::is_sorted(out.begin(), out.end(), [](auto &l, auto &r){ return l.first.second < r.first.second || (l.first.second == r.first.second) && l.first.first < r.first.first; }));
    return out;
}

} // namespace Slic3r::TreeSupport3D
