// [INTENT] FilamentGroup: assigns filaments (spools) to extruders on multi-extruder machines
// (e.g., dual-AMS Bambu X1E). The core problem is a combinatorial optimisation: given N used
// filaments, E extruders, and a flush-volume cost matrix C[i][j] (cost to purge from filament i
// to j), partition the filaments into E groups to minimise total purge volume across the print.
// Secondary objectives: respect filament-type printability constraints per extruder, and for
// AMS "Match Mode" prefer groupings that closely match the actual filaments loaded in the machine.
//
// [STATE] FilamentGroup holds:
//   ctx              — FilamentGroupContext: read-only inputs (layer_filaments, flush_matrix,
//                      unprintable constraints, max_group_size, master_extruder_id, mode, strategy)
//   m_memoryed_groups — top-K near-optimal group assignments retained from the last solve, used
//                       as candidates for the AMS color-matching pass in calc_filament_group_for_flush()
//
// [COUPLING] Depends on:
//   GCode/ToolOrderUtils.hpp — reorder_filaments_for_minimum_flush_volume(), extract_unprintable_limit_indices()
//   FlushVolPredictor.hpp   — FlushPredict::MatchModeGroupSolver (MCMF solver), calc_color_distance()
//   FilamentGroupUtils (HPP) — MemoryedGroupHeap, FGStrategy, FlushDistanceEvaluator
//
// [CONCURRENCY] FilamentGroup is constructed per-Print and called from the UI thread (print settings
// update) and potentially from the slicing thread. No internal locking — callers must serialize.
//
// Algorithm selection in calc_min_flush_group():
//   used_filaments < 10  → calc_min_flush_group_by_enum()   — O(2^N) exhaustive bit-enumeration
//   used_filaments >= 10 → calc_min_flush_group_by_pam2()   — PAM2 (K-Medoids) clustering
//
// [HAZARD] The threshold of 10 filaments for algorithm selection is a hardcoded magic constant.
// At exactly 10 filaments, 2^10 = 1024 enumerations — still fast. The PAM2 path is O(N^2) per
// iteration and uses a 500 ms timeout to avoid UI blocking.
//
// [HAZARD] calc_min_flush_group_by_enum() uses uint64_t bitmask, limiting it to at most 64 filaments.
// At 9 filaments (2^9=512) this is fine; the >=10 branch avoids the exponential blowup.
//
// AMS Match Mode (calc_filament_group_for_match()):
// Three-pass MCMF fallback chain:
//   Pass 1 (full constraints): require type AND printability compatibility
//   Pass 2 (type relaxed):     require printability only (handles type-mismatches gracefully)
//   Pass 3 (no constraints):   assign remaining filaments anywhere
// Each pass only processes filaments that were unmatched by the prior pass.
#include "FilamentGroup.hpp"
#include "GCode/ToolOrderUtils.hpp"
#include "FlushVolPredictor.hpp"
#include <queue>
#include <random>
#include <cassert>
#include <sstream>

namespace Slic3r {
using namespace FilamentGroupUtils;
// [INTENT] change_memoryed_heaps_to_arrays(): drains a MemoryedGroupHeap priority queue into
// a vector of full-length label arrays indexed by total_filament_num. The heap holds compact
// label vectors indexed by used_filaments[]; this re-maps them back to filament global indices.
// [STATE] Clears arrs, then fills it with one entry per heap element (heap is fully drained).
// clear the array and heap,save the groups in heap to the array
static void change_memoryed_heaps_to_arrays(MemoryedGroupHeap&               heap,
                                            const int                        total_filament_num,
                                            const std::vector<unsigned int>& used_filaments,
                                            std::vector<std::vector<int>>&   arrs)
{
    // switch the label idx
    arrs.clear();
    while (!heap.empty()) {
        auto top = heap.top();
        heap.pop();
        std::vector<int> labels_tmp(total_filament_num, 0);
        for (size_t idx = 0; idx < top.group.size(); ++idx)
            labels_tmp[used_filaments[idx]] = top.group[idx];
        arrs.emplace_back(std::move(labels_tmp));
    }
}

static std::unordered_map<int, int> get_merged_filament_map(const std::unordered_map<int, std::vector<int>>& merged_filaments)
{
    std::unordered_map<int, int> filament_merge_map;
    for (auto elem : merged_filaments) {
        for (auto f : elem.second) {
            // traverse filaments in merged group
            filament_merge_map[f] = elem.first;
        }
    }
    return filament_merge_map;
}

// [INTENT] calc_filament_group_for_tpu(): special-case assignment for TPU filaments.
// TPU filaments require the master extruder (which has a direct drive or compatible setup);
// all other filaments go to the other extruder. This is a hard constraint bypass — no flush
// optimisation is performed when TPU is present.
std::vector<int> calc_filament_group_for_tpu(const std::set<int>& tpu_filaments, const int filament_nums, const int master_extruder_id)
{
    std::vector<int> ret(filament_nums);
    for (size_t fidx = 0; fidx < filament_nums; ++fidx) {
        if (tpu_filaments.count(fidx))
            ret[fidx] = master_extruder_id;
        else
            ret[fidx] = 1 - master_extruder_id;
    }
    return ret;
}

// [INTENT] can_swap_groups(): returns true if two filament groups (assigned to two extruders) can
// have their extruder assignments swapped without violating printability constraints or capacity.
// Used by optimize_group_for_master_extruder() to prefer putting more filaments on the master extruder.
// Checks:
//   1. No filament in group_0 is unprintable on extruder_id_1 (post-swap destination)
//   2. No filament in group_1 is unprintable on extruder_id_0 (post-swap destination)
//   3. Capacity: if the original assignment already fits (group_0 ≤ max[0], group_1 ≤ max[1])
//      but the swap would violate capacity, refuse the swap.
bool can_swap_groups(const int                   extruder_id_0,
                     const std::set<int>&        group_0,
                     const int                   extruder_id_1,
                     const std::set<int>&        group_1,
                     const FilamentGroupContext& ctx)
{
    std::vector<std::set<int>> extruder_unprintables(2);
    {
        std::vector<std::set<int>> unprintable_filaments = ctx.model_info.unprintable_filaments;
        if (unprintable_filaments.size() > 1)
            remove_intersection(unprintable_filaments[0], unprintable_filaments[1]);

        std::map<int, std::vector<int>> unplaceable_limts;
        for (auto& group_id : {extruder_id_0, extruder_id_1})
            for (auto f : unprintable_filaments[group_id])
                unplaceable_limts[f].emplace_back(group_id);

        for (auto& elem : unplaceable_limts)
            sort_remove_duplicates(elem.second);

        for (auto& elem : unplaceable_limts) {
            for (auto& eid : elem.second) {
                if (eid == extruder_id_0) {
                    extruder_unprintables[0].insert(elem.first);
                }
                if (eid == extruder_id_1) {
                    extruder_unprintables[1].insert(elem.first);
                }
            }
        }
    }

    // check printable limits
    for (auto fid : group_0) {
        if (extruder_unprintables[1].count(fid) > 0)
            return false;
    }

    for (auto fid : group_1) {
        if (extruder_unprintables[0].count(fid) > 0)
            return false;
    }

    // check extruder capacity ,if result before exchange meets the constraints and the result after exchange does not meet the constraints,
    // return false
    if (ctx.machine_info.max_group_size[extruder_id_0] >= group_0.size() &&
        ctx.machine_info.max_group_size[extruder_id_1] >= group_1.size() &&
        (ctx.machine_info.max_group_size[extruder_id_0] < group_1.size() || ctx.machine_info.max_group_size[extruder_id_1] < group_0.size()))
        return false;

    return true;
}

// only support extruder nums with 2, try to swap the master extruder id with the other extruder id
std::vector<int> optimize_group_for_master_extruder(const std::vector<unsigned int>& used_filaments,
                                                    const FilamentGroupContext&      ctx,
                                                    std::vector<int>&                filament_map)
{
    std::vector<int>                       ret = filament_map;
    std::unordered_map<int, std::set<int>> groups;
    for (size_t idx = 0; idx < used_filaments.size(); ++idx) {
        int filament_id = used_filaments[idx];
        int group_id    = ret[filament_id];
        groups[group_id].insert(filament_id);
    }

    int none_master_extruder_id = 1 - ctx.machine_info.master_extruder_id;
    assert(0 <= none_master_extruder_id && none_master_extruder_id <= 1);

    if (can_swap_groups(none_master_extruder_id, groups[none_master_extruder_id], ctx.machine_info.master_extruder_id,
                        groups[ctx.machine_info.master_extruder_id], ctx) &&
        groups[none_master_extruder_id].size() > groups[ctx.machine_info.master_extruder_id].size()) {
        for (auto fid : groups[none_master_extruder_id])
            ret[fid] = ctx.machine_info.master_extruder_id;
        for (auto fid : groups[ctx.machine_info.master_extruder_id])
            ret[fid] = none_master_extruder_id;
    }
    return ret;
}

/**
 * @brief Select the group that best fit the filaments in AMS
 *
 * Calculate the total color distance between the grouping results and the AMS filaments through
 * minimum cost maximum flow. Only those with a distance difference within the threshold are
 * considered valid.
 *
 * @param map_lists Group list with similar flush count
 * @param used_filaments Idx of used filaments
 * @param used_filament_info Information of filaments used
 * @param machine_filament_info Information of filaments loaded in printer
 * @param color_threshold Threshold for considering colors to be similar
 * @return The group that best fits the filament distribution in AMS
 */
// [INTENT] select_best_group_for_ams(): given a set of near-optimal flush-cost group assignments
// (map_lists, produced by the memoryed_groups mechanism), select the one that best matches the
// actual filaments loaded in the AMS. "Best match" is defined as minimum total RGB color distance
// between the model filaments and the machine filaments in the same extruder group, computed via
// MCMF (MatchModeGroupSolver). Each unmatched filament (no match found or distance > threshold)
// incurs a fail_cost = 9999 penalty, ensuring valid matches always win over no-match.
// [STATE] Reads map_lists, used_filament_info, machine_filament_info. No mutation.
// [COUPLING] Uses FlushPredict::MatchModeGroupSolver for MCMF, calc_color_distance() for RGB distance.
// [HAZARD] machine_filament_info is resized to exactly 2 (one entry per extruder). If more than 2
// extruders are present in future hardware, this will silently truncate the machine filament list.
std::vector<int> select_best_group_for_ams(const std::vector<std::vector<int>>&                 map_lists,
                                           const std::vector<unsigned int>&                     used_filaments,
                                           const std::vector<FilamentInfo>&                     used_filament_info,
                                           const std::vector<std::vector<MachineFilamentInfo>>& machine_filament_info_,
                                           const double                                         color_threshold)
{
    using namespace FlushPredict;

    const int fail_cost = 9999;

    // these code is to make we machine filament info size is 2
    std::vector<std::vector<MachineFilamentInfo>> machine_filament_info = machine_filament_info_;
    machine_filament_info.resize(2);

    int              best_cost = std::numeric_limits<int>::max();
    std::vector<int> best_map;

    for (auto& map : map_lists) {
        std::vector<std::vector<int>>   group_filaments(2);
        std::vector<std::vector<Color>> group_colors(2);

        for (size_t i = 0; i < used_filaments.size(); ++i) {
            int target_group = map[used_filaments[i]] == 0 ? 0 : 1;
            group_colors[target_group].emplace_back(used_filament_info[i].color);
            group_filaments[target_group].emplace_back(i);
        }

        int group_cost = 0;
        for (size_t i = 0; i < 2; ++i) {
            if (group_colors[i].empty())
                continue;
            if (machine_filament_info[i].empty()) {
                group_cost += group_colors.size() * fail_cost;
                continue;
            }
            std::vector<std::vector<float>> distance_matrix(group_colors[i].size(), std::vector<float>(machine_filament_info[i].size()));

            // calculate color distance matrix
            for (size_t src = 0; src < group_colors[i].size(); ++src) {
                for (size_t dst = 0; dst < machine_filament_info[i].size(); ++dst) {
                    distance_matrix[src][dst] = calc_color_distance(RGBColor(group_colors[i][src].r, group_colors[i][src].g,
                                                                             group_colors[i][src].b),
                                                                    RGBColor(machine_filament_info[i][dst].color.r,
                                                                             machine_filament_info[i][dst].color.g,
                                                                             machine_filament_info[i][dst].color.b));
                }
            }

            // get min cost by min cost max flow
            std::vector<int> l_nodes(group_colors[i].size()), r_nodes(machine_filament_info[i].size());
            std::iota(l_nodes.begin(), l_nodes.end(), 0);
            std::iota(r_nodes.begin(), r_nodes.end(), 0);

            std::unordered_map<int, std::vector<int>> unlink_limits;
            for (size_t from = 0; from < group_filaments[i].size(); ++from) {
                for (size_t to = 0; to < machine_filament_info[i].size(); ++to) {
                    if (used_filament_info[group_filaments[i][from]].type != machine_filament_info[i][to].type ||
                        used_filament_info[group_filaments[i][from]].is_support != machine_filament_info[i][to].is_support) {
                        unlink_limits[from].emplace_back(to);
                    }
                }
            }

            MatchModeGroupSolver mcmf(distance_matrix, l_nodes, r_nodes, std::vector<int>(r_nodes.size(), l_nodes.size()), unlink_limits);
            auto                 ams_map = mcmf.solve();

            for (size_t idx = 0; idx < ams_map.size(); ++idx) {
                if (ams_map[idx] == MaxFlowGraph::INVALID_ID || distance_matrix[idx][ams_map[idx]] > color_threshold) {
                    group_cost += fail_cost;
                } else {
                    group_cost += distance_matrix[idx][ams_map[idx]];
                }
            }
        }

        if (best_map.empty() || group_cost < best_cost) {
            best_cost = group_cost;
            best_map  = map;
        }
    }

    return best_map;
}

void FilamentGroupUtils::update_memoryed_groups(const MemoryedGroup& item, const double gap_threshold, MemoryedGroupHeap& groups)
{
    auto emplace_if_accepatle = [gap_threshold](MemoryedGroupHeap& heap, const MemoryedGroup& elem, const MemoryedGroup& best) {
        if (best.cost == 0) {
            if (std::abs(elem.cost - best.cost) <= ABSOLUTE_FLUSH_GAP_TOLERANCE)
                heap.push(elem);
            return;
        }
        double gap_rate = (double) std::abs(elem.cost - best.cost) / (double) best.cost;
        if (gap_rate < gap_threshold)
            heap.push(elem);
    };

    if (groups.empty()) {
        groups.push(item);
    } else {
        auto top = groups.top();
        // we only memory items with the highest prefer level
        if (top.prefer_level > item.prefer_level)
            return;
        else if (top.prefer_level == item.prefer_level) {
            if (top.cost <= item.cost) {
                emplace_if_accepatle(groups, item, top);
            }
            // find a group with lower cost, rebuild the heap
            else {
                MemoryedGroupHeap new_heap;
                new_heap.push(item);
                while (!groups.empty()) {
                    auto top = groups.top();
                    groups.pop();
                    emplace_if_accepatle(new_heap, top, item);
                }
                groups = std::move(new_heap);
            }
        }
        // find a group with the higher prefer level, rebuild the heap
        else {
            groups = MemoryedGroupHeap();
            groups.push(item);
        }
    }
}

std::vector<unsigned int> collect_sorted_used_filaments(const std::vector<std::vector<unsigned int>>& layer_filaments)
{
    std::set<unsigned int> used_filaments_set;
    for (const auto& lf : layer_filaments)
        for (const auto& f : lf)
            used_filaments_set.insert(f);
    std::vector<unsigned int> used_filaments(used_filaments_set.begin(), used_filaments_set.end());
    std::sort(used_filaments.begin(), used_filaments.end());
    return used_filaments;
}

// [INTENT] FlushDistanceEvaluator constructor: precomputes a NxN weighted distance matrix used by
// the PAM2 K-Medoids clustering. The distance between two filaments i and j is:
//   dist(i,j) = (max(C[i][j], C[j][i]) * p + min(C[i][j], C[j][i]) * (1-p)) * count(i,j)
// where C[i][j] is the purge volume from filament i to j, p is a weighting parameter (default 0.5),
// and count(i,j) is the number of layers where both filaments appear together.
// [INTENT] This makes often co-occurring filaments with high flush volumes "far apart" in the
// clustering, so PAM2 places them in different groups to minimise total purge cost.
// [HAZARD] The count_matrix accumulation is O(L * F^2) where L = layer count, F = filament count.
// For prints with many layers and many filaments this may be slow (~N=10, L=1000 → 100K iterations).
// [MEMORY] m_distance_matrix is a dense NxN float matrix; at N=64 this is 64*64*4 = 16KB.
FlushDistanceEvaluator::FlushDistanceEvaluator(const FlushMatrix&                            flush_matrix,
                                               const std::vector<unsigned int>&              used_filaments,
                                               const std::vector<std::vector<unsigned int>>& layer_filaments,
                                               double                                        p)
{
    // calc pair counts
    std::vector<std::vector<int>> count_matrix(used_filaments.size(), std::vector<int>(used_filaments.size()));
    for (const auto& lf : layer_filaments) {
        for (auto iter = lf.begin(); iter != lf.end(); ++iter) {
            auto id_iter1 = std::find(used_filaments.begin(), used_filaments.end(), *iter);
            if (id_iter1 == used_filaments.end())
                continue;
            auto idx1 = id_iter1 - used_filaments.begin();
            for (auto niter = std::next(iter); niter != lf.end(); ++niter) {
                auto id_iter2 = std::find(used_filaments.begin(), used_filaments.end(), *niter);
                if (id_iter2 == used_filaments.end())
                    continue;
                auto idx2 = id_iter2 - used_filaments.begin();
                count_matrix[idx1][idx2] += 1;
                count_matrix[idx2][idx1] += 1;
            }
        }
    }

    m_distance_matrix.resize(used_filaments.size(), std::vector<float>(used_filaments.size()));

    for (size_t i = 0; i < used_filaments.size(); ++i) {
        for (size_t j = 0; j < used_filaments.size(); ++j) {
            if (i == j)
                m_distance_matrix[i][j] = 0;
            else {
                // TODO: check m_flush_matrix
                float max_val           = std::max(flush_matrix[used_filaments[i]][used_filaments[j]],
                                                   flush_matrix[used_filaments[j]][used_filaments[i]]);
                float min_val           = std::min(flush_matrix[used_filaments[i]][used_filaments[j]],
                                                   flush_matrix[used_filaments[j]][used_filaments[i]]);
                m_distance_matrix[i][j] = (max_val * p + min_val * (1 - p)) * count_matrix[i][j];
            }
        }
    }
}

double FlushDistanceEvaluator::get_distance(int idx_a, int idx_b) const
{
    assert(0 <= idx_a && idx_a < m_distance_matrix.size());
    assert(0 <= idx_b && idx_b < m_distance_matrix.size());

    return m_distance_matrix[idx_a][idx_b];
}

std::vector<int> KMediods2::cluster_small_data(const std::map<int, int>& unplaceable_limits, const std::vector<int>& group_size)
{
    std::vector<int> labels(m_elem_count, -1);
    std::vector<int> new_group_size = group_size;

    for (auto& [elem, center] : unplaceable_limits) {
        if (labels[elem] == -1) {
            int gid      = 1 - center;
            labels[elem] = gid;
            new_group_size[gid] -= 1;
        }
    }

    for (auto& label : labels) {
        if (label == -1) {
            int gid = -1;
            for (size_t idx = 0; idx < new_group_size.size(); ++idx) {
                if (new_group_size[idx] > 0) {
                    gid = idx;
                    break;
                }
            }
            if (gid != -1) {
                label = gid;
                new_group_size[gid] -= 1;
            } else {
                label = m_default_group_id;
            }
        }
    }

    return labels;
}

std::vector<int> KMediods2::assign_cluster_label(const std::vector<int>&   center,
                                                 const std::map<int, int>& unplaceable_limtis,
                                                 const std::vector<int>&   group_size,
                                                 const FGStrategy&         strategy)
{
    struct Comp
    {
        bool operator()(const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.second > b.second; }
    };

    std::vector<std::set<int>> groups(2);
    std::vector<int>           new_max_group_size = group_size;
    // store filament idx and distance gap between center 0 and center 1
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, Comp> min_heap;

    for (int i = 0; i < m_elem_count; ++i) {
        if (auto it = unplaceable_limtis.find(i); it != unplaceable_limtis.end()) {
            int gid = it->second;
            assert(gid == 0 || gid == 1);
            groups[1 - gid].insert(i);                                                  // insert to group
            new_max_group_size[1 - gid] = std::max(new_max_group_size[1 - gid] - 1, 0); // decrease group_size
            continue;
        }
        int distance_to_0 = m_evaluator->get_distance(i, center[0]);
        int distance_to_1 = m_evaluator->get_distance(i, center[1]);
        min_heap.push({i, distance_to_0 - distance_to_1});
    }

    bool have_enough_size = (min_heap.size() <= (new_max_group_size[0] + new_max_group_size[1]));

    if (have_enough_size || strategy == FGStrategy::BestFit) {
        while (!min_heap.empty()) {
            auto top = min_heap.top();
            min_heap.pop();
            if (groups[0].size() < new_max_group_size[0] && (top.second <= 0 || groups[1].size() >= new_max_group_size[1]))
                groups[0].insert(top.first);
            else if (groups[1].size() < new_max_group_size[1] && (top.second > 0 || groups[0].size() >= new_max_group_size[0]))
                groups[1].insert(top.first);
            else {
                if (top.second <= 0)
                    groups[0].insert(top.first);
                else
                    groups[1].insert(top.first);
            }
        }
    } else {
        while (!min_heap.empty()) {
            auto top = min_heap.top();
            min_heap.pop();
            if (top.second <= 0)
                groups[0].insert(top.first);
            else
                groups[1].insert(top.first);
        }
    }

    std::vector<int> labels(m_elem_count);
    for (auto& f : groups[0])
        labels[f] = 0;
    for (auto& f : groups[1])
        labels[f] = 1;

    return labels;
}

int KMediods2::calc_cost(const std::vector<int>& labels, const std::vector<int>& medoids)
{
    int total_cost = 0;
    for (int i = 0; i < m_elem_count; ++i)
        total_cost += m_evaluator->get_distance(i, medoids[labels[i]]);
    return total_cost;
}

// [INTENT] KMediods2::do_clustering(): PAM2 (Partitioning Around Medoids) implementation for
// partitioning N filaments into 2 groups minimising total flush cost. Unlike K-Means, PAM uses
// actual data points as cluster centers (medoids), which is critical here because only real
// filament indices have meaningful flush costs.
//
// Algorithm: exhaustive O(N^2) search over all pairs (center_0, center_1) from the filament set.
// For each candidate pair, assign all filaments to their nearest center via assign_cluster_label()
// (greedy assignment respecting unplaceable constraints and group size limits), then compute total cost.
// Track the best assignment and also feed near-optimal results into memoryed_groups for the AMS pass.
//
// [HAZARD] This is PAM build phase only — there is NO swap/refinement phase (PAM2 iterative
// improvement). The algorithm is a greedy O(N^2) initial assignment, not the full PAM algorithm.
// This may miss locally optimal solutions that require swapping medoids.
//
// [HAZARD] If m_elem_count < m_k (fewer filaments than clusters), falls back to cluster_small_data()
// which assigns by constraint first, then fills remaining capacity — no distance optimisation.
//
// [STATE] Mutates m_cluster_labels, memoryed_groups. Uses FlushTimeMachine for wall-clock timeout.
// [CONCURRENCY] Not thread-safe; called from a single thread per FilamentGroup instance.
void KMediods2::do_clustering(const FGStrategy& g_strategy, int timeout_ms)
{
    FlushTimeMachine T;
    T.time_machine_start();

    if (m_elem_count < m_k) {
        m_cluster_labels = cluster_small_data(m_unplaceable_limits, m_max_cluster_size);
        {
            std::vector<int> cluster_center(m_k, -1);
            for (size_t idx = 0; idx < m_cluster_labels.size(); ++idx) {
                if (cluster_center[m_cluster_labels[idx]] == -1)
                    cluster_center[m_cluster_labels[idx]] = idx;
            }
            MemoryedGroup g(m_cluster_labels, calc_cost(m_cluster_labels, cluster_center), 1);
            update_memoryed_groups(g, memory_threshold, memoryed_groups);
        }
        return;
    }

    std::vector<int> best_labels;
    int              best_cost = std::numeric_limits<int>::max();

    for (int center_0 = 0; center_0 < m_elem_count; ++center_0) {
        if (auto iter = m_unplaceable_limits.find(center_0); iter != m_unplaceable_limits.end() && iter->second == 0)
            continue;
        for (int center_1 = 0; center_1 < m_elem_count; ++center_1) {
            if (center_0 == center_1)
                continue;
            if (auto iter = m_unplaceable_limits.find(center_1); iter != m_unplaceable_limits.end() && iter->second == 1)
                continue;

            std::vector<int> new_centers = {center_0, center_1};
            std::vector<int> new_labels  = assign_cluster_label(new_centers, m_unplaceable_limits, m_max_cluster_size, g_strategy);

            int new_cost = calc_cost(new_labels, new_centers);
            if (new_cost < best_cost) {
                best_cost   = new_cost;
                best_labels = new_labels;
            }

            {
                MemoryedGroup g(new_labels, new_cost, 1);
                update_memoryed_groups(g, memory_threshold, memoryed_groups);
            }

            if (T.time_machine_end() > timeout_ms)
                break;
        }
        if (T.time_machine_end() > timeout_ms)
            break;
    }
    this->m_cluster_labels = best_labels;
}

// [INTENT] calc_min_flush_group(): dispatcher — selects algorithm based on filament count.
// < 10 filaments: exhaustive O(2^N) bit-enumeration (always optimal within time)
// >= 10 filaments: PAM2 K-Medoids with 500ms timeout (heuristic, not guaranteed optimal)
// [HAZARD] Threshold of 10 is a magic constant. 2^9=512 enumerations (fast); 2^10=1024 (still fast).
// The real reason for PAM2 at >=10 is likely UI responsiveness; 500ms cap may produce suboptimal
// groupings for complex prints with many filaments.
std::vector<int> FilamentGroup::calc_min_flush_group(int* cost)
{
    auto used_filaments    = collect_sorted_used_filaments(ctx.model_info.layer_filaments);
    int  used_filament_num = used_filaments.size();

    if (used_filament_num < 10)
        return calc_min_flush_group_by_enum(used_filaments, cost);
    else
        return calc_min_flush_group_by_pam2(used_filaments, cost, 500);
}

std::unordered_map<int, std::vector<int>> FilamentGroup::try_merge_filaments()
{
    std::unordered_map<int, std::vector<int>> merged_filaments;

    std::unordered_map<std::string, std::vector<int>> merge_filament_map;

    auto unprintable_stat_to_str = [unprintable_filaments = this->ctx.model_info.unprintable_filaments](int idx) {
        std::string str;
        for (size_t eid = 0; eid < unprintable_filaments.size(); ++eid) {
            if (unprintable_filaments[eid].count(idx)) {
                if (eid > 0)
                    str += ',';
                str += std::to_string(idx);
            }
        }
        return str;
    };

    for (size_t idx = 0; idx < ctx.model_info.filament_ids.size(); ++idx) {
        std::string id              = ctx.model_info.filament_ids[idx];
        Color       color           = ctx.model_info.filament_info[idx].color;
        std::string unprintable_str = unprintable_stat_to_str(idx);

        std::string key = id + "," + color.to_hex_str(true) + "," + unprintable_str;
        merge_filament_map[key].push_back(idx);
    }

    for (auto& elem : merge_filament_map) {
        if (elem.second.size() > 1) {
            merged_filaments[elem.second.front()] = elem.second;
        }
    }
    return merged_filaments;
}

std::vector<int> FilamentGroup::seperate_merged_filaments(const std::vector<int>&                          filament_map,
                                                          const std::unordered_map<int, std::vector<int>>& merged_filaments)
{
    std::vector<int> ret_map = filament_map;
    for (auto& elem : merged_filaments) {
        int src = elem.first;
        for (auto f : elem.second) {
            ret_map[f] = ret_map[src];
        }
    }
    return ret_map;
}

void FilamentGroup::rebuild_context(const std::unordered_map<int, std::vector<int>>& merged_filaments)
{
    if (merged_filaments.empty())
        return;

    FilamentGroupContext new_ctx = ctx;

    std::unordered_map<int, int> filament_merge_map = get_merged_filament_map(merged_filaments);

    // modify layer filaments
    for (auto& layer_filament : new_ctx.model_info.layer_filaments) {
        for (auto& f : layer_filament) {
            if (auto iter = filament_merge_map.find((int) (f)); iter != filament_merge_map.end()) {
                f = iter->second;
            }
        }
    }

    for (auto& unprintables : new_ctx.model_info.unprintable_filaments) {
        std::set<int> new_unprintables;
        for (auto f : unprintables) {
            if (auto iter = filament_merge_map.find((int) (f)); iter != filament_merge_map.end()) {
                new_unprintables.insert(iter->second);
            } else {
                new_unprintables.insert(f);
            }
        }
    }

    ctx = new_ctx;
    return;
}

std::vector<int> FilamentGroup::calc_filament_group(int* cost)
{
    try {
        if (FGMode::MatchMode == ctx.group_info.mode)
            return calc_filament_group_for_match(cost);
    } catch (const FilamentGroupException& e) {}

    auto merged_map = try_merge_filaments();
    rebuild_context(merged_map);
    auto filamnet_map = calc_filament_group_for_flush(cost);
    return seperate_merged_filaments(filamnet_map, merged_map);
}

std::vector<int> FilamentGroup::calc_filament_group_for_match(int* cost)
{
    using namespace FlushPredict;

    auto                      used_filaments = collect_sorted_used_filaments(ctx.model_info.layer_filaments);
    std::vector<FilamentInfo> used_filament_list;
    for (auto f : used_filaments)
        used_filament_list.emplace_back(ctx.model_info.filament_info[f]);

    std::vector<MachineFilamentInfo>             machine_filament_list;
    std::map<MachineFilamentInfo, std::set<int>> machine_filament_set;
    for (size_t eid = 0; eid < ctx.machine_info.machine_filament_info.size(); ++eid) {
        for (auto& filament : ctx.machine_info.machine_filament_info[eid]) {
            machine_filament_set[filament].insert(machine_filament_list.size());
            machine_filament_list.emplace_back(filament);
        }
    }

    if (machine_filament_list.empty())
        throw FilamentGroupException(FilamentGroupException::EmptyAmsFilaments, "Empty ams filament in For-Match mode.");

    std::map<int, int> unprintable_limit_indices; // key stores filament idx in used_filament, value stores unprintable extruder
    extract_unprintable_limit_indices(ctx.model_info.unprintable_filaments, used_filaments, unprintable_limit_indices);

    std::vector<std::vector<float>> color_dist_matrix(used_filament_list.size(), std::vector<float>(machine_filament_list.size()));
    for (size_t i = 0; i < used_filament_list.size(); ++i) {
        for (size_t j = 0; j < machine_filament_list.size(); ++j) {
            color_dist_matrix[i][j] = calc_color_distance(RGBColor(used_filament_list[i].color.r, used_filament_list[i].color.g,
                                                                   used_filament_list[i].color.b),
                                                          RGBColor(machine_filament_list[j].color.r, machine_filament_list[j].color.g,
                                                                   machine_filament_list[j].color.b));
        }
    }

    std::vector<int> l_nodes(used_filaments.size());
    std::iota(l_nodes.begin(), l_nodes.end(), 0);
    std::vector<int> r_nodes(machine_filament_list.size());
    std::iota(r_nodes.begin(), r_nodes.end(), 0);
    std::vector<int> machine_filament_capacity(machine_filament_list.size(), l_nodes.size());
    std::vector<int> extruder_filament_count(2, 0);

    auto is_extruder_filament_compatible = [&unprintable_limit_indices](int filament_idx, int extruder_id) {
        auto iter = unprintable_limit_indices.find(filament_idx);
        if (iter != unprintable_limit_indices.end() && iter->second == extruder_id)
            return false;
        return true;
    };

    auto build_unlink_limits = [](const std::vector<int>& l_nodes, const std::vector<int>& r_nodes,
                                  const std::function<bool(int, int)>& can_link) {
        std::unordered_map<int, std::vector<int>> unlink_limits;
        for (size_t i = 0; i < l_nodes.size(); ++i) {
            std::vector<int> unlink_filaments;
            for (size_t j = 0; j < r_nodes.size(); ++j) {
                if (!can_link(l_nodes[i], r_nodes[j]))
                    unlink_filaments.emplace_back(j);
            }
            if (!unlink_filaments.empty())
                unlink_limits.emplace(i, std::move(unlink_filaments));
        }
        return unlink_limits;
    };

    auto optimize_map_to_machine_filament = [&](const std::vector<int>& map_to_machine_filament, const std::vector<int>& l_nodes,
                                                const std::vector<int>& r_nodes, std::vector<int>& filament_map, bool consider_capacity) {
        std::vector<int> ungrouped_filaments;
        std::vector<int> filaments_to_optimize;

        auto map_filament_to_machine_filament = [&](int filament_idx, int machine_filament_idx) {
            auto& machine_filament                          = machine_filament_list[machine_filament_idx];
            machine_filament_capacity[machine_filament_idx] = std::max(0, machine_filament_capacity[machine_filament_idx] -
                                                                              1);           // decrease machine filament capacity
            filament_map[used_filaments[filament_idx]]      = machine_filament.extruder_id; // set extruder id to filament map
            extruder_filament_count[machine_filament.extruder_id] += 1;                     // increase filament count in extruder
        };
        auto unmap_filament_to_machine_filament = [&](int filament_idx, int machine_filament_idx) {
            auto& machine_filament = machine_filament_list[machine_filament_idx];
            machine_filament_capacity[machine_filament_idx] += 1;       // increase machine filament capacity
            extruder_filament_count[machine_filament.extruder_id] -= 1; // increase filament count in extruder
        };

        for (size_t idx = 0; idx < map_to_machine_filament.size(); ++idx) {
            if (map_to_machine_filament[idx] == MaxFlowGraph::INVALID_ID) {
                ungrouped_filaments.emplace_back(l_nodes[idx]);
                continue;
            }
            int   used_filament_idx    = l_nodes[idx];
            int   machine_filament_idx = r_nodes[map_to_machine_filament[idx]];
            auto& machine_filament     = machine_filament_list[machine_filament_idx];
            if (machine_filament_set[machine_filament].size() > 1 && unprintable_limit_indices.count(used_filament_idx) == 0)
                filaments_to_optimize.emplace_back(idx);

            map_filament_to_machine_filament(used_filament_idx, machine_filament_idx);
        }
        // try to optimize the result
        for (auto idx : filaments_to_optimize) {
            int   filament_idx             = l_nodes[idx];
            int   old_machine_filament_idx = r_nodes[map_to_machine_filament[idx]];
            auto& old_machine_filament     = machine_filament_list[old_machine_filament_idx];

            int curr_gap = std::abs(extruder_filament_count[0] - extruder_filament_count[1]);
            unmap_filament_to_machine_filament(filament_idx, old_machine_filament_idx);

            auto optional_filaments = machine_filament_set[old_machine_filament];
            auto iter               = optional_filaments.begin();
            for (; iter != optional_filaments.end(); ++iter) {
                int new_extruder_id = machine_filament_list[*iter].extruder_id;
                int new_gap         = std::abs(extruder_filament_count[new_extruder_id] + 1 - extruder_filament_count[1 - new_extruder_id]);
                if (new_gap < curr_gap && (!consider_capacity || machine_filament_capacity[*iter] > 0)) {
                    map_filament_to_machine_filament(filament_idx, *iter);
                    break;
                }
            }

            if (iter == optional_filaments.end())
                map_filament_to_machine_filament(filament_idx, old_machine_filament_idx);
        }
        return ungrouped_filaments;
    };

    std::vector<int> group(ctx.group_info.total_filament_num, ctx.machine_info.master_extruder_id);
    std::vector<int> ungrouped_filaments;

    auto unlink_limits_full = build_unlink_limits(
        l_nodes, r_nodes,
        [&used_filament_list, &machine_filament_list, is_extruder_filament_compatible](int used_filament_idx, int machine_filament_idx) {
            return used_filament_list[used_filament_idx].type == machine_filament_list[machine_filament_idx].type &&
                   used_filament_list[used_filament_idx].is_support == machine_filament_list[machine_filament_idx].is_support &&
                   is_extruder_filament_compatible(used_filament_idx, machine_filament_list[machine_filament_idx].extruder_id);
        });

    {
        MatchModeGroupSolver s(color_dist_matrix, l_nodes, r_nodes, machine_filament_capacity, unlink_limits_full);
        ungrouped_filaments = optimize_map_to_machine_filament(s.solve(), l_nodes, r_nodes, group, false);
        if (ungrouped_filaments.empty())
            return group;
    }

    // additionally remove type limits
    {
        l_nodes = ungrouped_filaments;
        auto unlink_limits =
            build_unlink_limits(l_nodes, r_nodes,
                                [&machine_filament_list, is_extruder_filament_compatible](int used_filament_idx, int machine_filament_idx) {
                                    return is_extruder_filament_compatible(used_filament_idx,
                                                                           machine_filament_list[machine_filament_idx].extruder_id);
                                });

        MatchModeGroupSolver s(color_dist_matrix, l_nodes, r_nodes, machine_filament_capacity, unlink_limits);
        ungrouped_filaments = optimize_map_to_machine_filament(s.solve(), l_nodes, r_nodes, group, false);
        if (ungrouped_filaments.empty())
            return group;
    }

    // remove all limits
    {
        l_nodes = ungrouped_filaments;
        MatchModeGroupSolver s(color_dist_matrix, l_nodes, r_nodes, machine_filament_capacity, {});
        auto                 ret = optimize_map_to_machine_filament(s.solve(), l_nodes, r_nodes, group, false);
        for (size_t idx = 0; idx < ret.size(); ++idx) {
            if (ret[idx] == MaxFlowGraph::INVALID_ID)
                assert(false);
            else
                group[used_filaments[l_nodes[idx]]] = machine_filament_list[r_nodes[ret[idx]]].extruder_id;
        }
    }

    return group;
}

std::vector<int> FilamentGroup::calc_filament_group_for_flush(int* cost)
{
    auto used_filaments = collect_sorted_used_filaments(ctx.model_info.layer_filaments);

    std::vector<int>              ret           = calc_min_flush_group(cost);
    std::vector<std::vector<int>> memoryed_maps = this->m_memoryed_groups;
    memoryed_maps.insert(memoryed_maps.begin(), ret);

    std::vector<int> optimized_ret = optimize_group_for_master_extruder(used_filaments, ctx, ret);
    if (optimized_ret != ret)
        memoryed_maps.insert(memoryed_maps.begin(), optimized_ret);

    std::vector<FilamentGroupUtils::FilamentInfo> used_filament_info;
    for (auto f : used_filaments) {
        used_filament_info.emplace_back(ctx.model_info.filament_info[f]);
    }

    ret = select_best_group_for_ams(memoryed_maps, used_filaments, used_filament_info, ctx.machine_info.machine_filament_info);
    return ret;
}

// [INTENT] calc_min_flush_group_by_enum(): exhaustive enumeration of all 2^N group assignments.
// Each integer i in [0, 2^N) encodes a bitmask: bit j=1 → filament j goes to extruder 1, bit j=0 → extruder 0.
// For each assignment, computes a lexicographic (prefer_level, total_cost) score:
//   prefer_level accumulates:
//     +UNPLACEABLE_LIMIT_REWARD(100) if printability constraints are satisfied
//     +MAX_SIZE_LIMIT_REWARD(10) if both group sizes fit within extruder capacity
//     +BEST_FIT_LIMIT_REWARD(1) if BestFit strategy: both groups fill their max capacity
//   total_cost: flush volume from reorder_filaments_for_minimum_flush_volume()
// Best assignment maximises (prefer_level, -total_cost). Near-optimal results are retained in
// memoryed_groups (MemoryedGroupHeap) for the downstream AMS color-matching pass.
//
// [HAZARD] Bitmask is uint64_t → works up to 64 filaments, but only called when N < 10.
// [HAZARD] bit_count_one() lambda is defined but never called — dead code.
// [HAZARD] Constraint rewards use arbitrary magic constants (100, 10, 1). Their relative values
// imply: printability always dominates capacity, which dominates best-fit. This ordering is
// correct but undocumented in the codebase.
// sorted used_filaments
std::vector<int> FilamentGroup::calc_min_flush_group_by_enum(const std::vector<unsigned int>& used_filaments, int* cost)
{
    static constexpr int UNPLACEABLE_LIMIT_REWARD = 100; // reward value if the group result follows the unprintable limit
    static constexpr int MAX_SIZE_LIMIT_REWARD    = 10;  // reward value if the group result follows the max size per extruder
    static constexpr int BEST_FIT_LIMIT_REWARD    = 1;   // reward value if the group result try to fill the max size per extruder

    MemoryedGroupHeap memoryed_groups;

    auto bit_count_one = [](uint64_t n) {
        int count = 0;
        while (n != 0) {
            n &= n - 1;
            count++;
        }
        return count;
    };

    std::map<int, int> unplaceable_limit_indices;
    extract_unprintable_limit_indices(ctx.model_info.unprintable_filaments, used_filaments, unplaceable_limit_indices);

    int      used_filament_num = used_filaments.size();
    uint64_t max_group_num     = (static_cast<uint64_t>(1) << used_filament_num);

    int              best_cost = std::numeric_limits<int>::max();
    std::vector<int> best_label;
    int              best_prefer_level = 0;

    for (uint64_t i = 0; i < max_group_num; ++i) {
        std::vector<std::set<int>> groups(2);
        for (int j = 0; j < used_filament_num; ++j) {
            if (i & (static_cast<uint64_t>(1) << j))
                groups[1].insert(j);
            else
                groups[0].insert(j);
        }

        int prefer_level = 0;

        if (check_printable(groups, unplaceable_limit_indices))
            prefer_level += UNPLACEABLE_LIMIT_REWARD;
        if (groups[0].size() <= ctx.machine_info.max_group_size[0] && groups[1].size() <= ctx.machine_info.max_group_size[1])
            prefer_level += MAX_SIZE_LIMIT_REWARD;
        if (FGStrategy::BestFit == ctx.group_info.strategy && groups[0].size() >= ctx.machine_info.max_group_size[0] &&
            groups[1].size() >= ctx.machine_info.max_group_size[1])
            prefer_level += BEST_FIT_LIMIT_REWARD;

        std::vector<int> filament_maps(used_filament_num);
        for (int i = 0; i < used_filament_num; ++i) {
            if (groups[0].find(i) != groups[0].end())
                filament_maps[i] = 0;
            if (groups[1].find(i) != groups[1].end())
                filament_maps[i] = 1;
        }

        int total_cost = reorder_filaments_for_minimum_flush_volume(used_filaments, filament_maps, ctx.model_info.layer_filaments,
                                                                    ctx.model_info.flush_matrix, get_custom_seq, nullptr);

        if (prefer_level > best_prefer_level || (prefer_level == best_prefer_level && total_cost < best_cost)) {
            best_prefer_level = prefer_level;
            best_cost         = total_cost;
            best_label        = filament_maps;
        }

        {
            MemoryedGroup mg(filament_maps, total_cost, prefer_level);
            update_memoryed_groups(mg, ctx.group_info.max_gap_threshold, memoryed_groups);
        }
    }

    if (cost)
        *cost = best_cost;

    std::vector<int> filament_labels(ctx.group_info.total_filament_num, 0);
    for (size_t i = 0; i < best_label.size(); ++i)
        filament_labels[used_filaments[i]] = best_label[i];

    change_memoryed_heaps_to_arrays(memoryed_groups, ctx.group_info.total_filament_num, used_filaments, m_memoryed_groups);

    return filament_labels;
}

// [INTENT] calc_min_flush_group_by_pam2(): PAM2 K-Medoids clustering for >=10 filaments.
// Constructs a FlushDistanceEvaluator (flush-volume weighted distance matrix), then runs
// KMediods2::do_clustering() with a timeout_ms=500ms wall-clock limit. After clustering,
// the actual flush cost is recomputed by reorder_filaments_for_minimum_flush_volume().
// Near-optimal group assignments from the PAM2 memoryed_groups are saved into m_memoryed_groups
// for the AMS color-matching pass in calc_filament_group_for_flush().
//
// [HAZARD] The PAM2 result is not guaranteed to be globally optimal — it is a heuristic.
// For prints where minimal purge volume matters (e.g., large dual-colour prints), this may
// produce visibly worse results than enumeration would for the same filament count.
// [HAZARD] timeout_ms=500 is a hardcoded constant passed from calc_min_flush_group().
// If called from a non-UI context, 500ms may be too long or too short.
// sorted used_filaments
std::vector<int> FilamentGroup::calc_min_flush_group_by_pam2(const std::vector<unsigned int>& used_filaments, int* cost, int timeout_ms)
{
    std::vector<int> filament_labels_ret(ctx.group_info.total_filament_num, ctx.machine_info.master_extruder_id);

    std::map<int, int> unplaceable_limits;
    extract_unprintable_limit_indices(ctx.model_info.unprintable_filaments, used_filaments, unplaceable_limits);

    auto      distance_evaluator = std::make_shared<FlushDistanceEvaluator>(ctx.model_info.flush_matrix[0], used_filaments,
                                                                            ctx.model_info.layer_filaments);
    KMediods2 PAM((int) used_filaments.size(), distance_evaluator, ctx.machine_info.master_extruder_id);
    PAM.set_max_cluster_size(ctx.machine_info.max_group_size);
    PAM.set_unplaceable_limits(unplaceable_limits);
    PAM.set_memory_threshold(ctx.group_info.max_gap_threshold);
    PAM.do_clustering(ctx.group_info.strategy, timeout_ms);

    std::vector<int> filament_labels = PAM.get_cluster_labels();

    {
        auto memoryed_groups = PAM.get_memoryed_groups();
        change_memoryed_heaps_to_arrays(memoryed_groups, ctx.group_info.total_filament_num, used_filaments, m_memoryed_groups);
    }

    if (cost)
        *cost = reorder_filaments_for_minimum_flush_volume(used_filaments, filament_labels, ctx.model_info.layer_filaments,
                                                           ctx.model_info.flush_matrix, std::nullopt, nullptr);

    for (int i = 0; i < filament_labels.size(); ++i)
        filament_labels_ret[used_filaments[i]] = filament_labels[i];
    return filament_labels_ret;
}

} // namespace Slic3r
