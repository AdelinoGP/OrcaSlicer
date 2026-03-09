// [INTENT] ToolOrderUtils.hpp — multi-extruder tool-ordering optimization.
// Minimises total wipe/flush volume when switching between filaments during a print.
//
// Provides three layers of solvers with increasing capability:
//
//   1. MaxFlowSolver — pure max-flow (Edmonds-Karp BFS augmentation) bipartite
//      matching. Used for feasibility checks and extruder group assignment.
//
//   2. MinCostMaxFlow (MCMF, defined in .cpp) — SPFA-based (Bellman-Ford queue)
//      min-cost max-flow. Backbone for the three MCMF-based solvers below.
//      Costs are scaled flush volumes from the FlushMatrix.
//
//   3. GeneralMinCostSolver — MCMF bipartite matching with uniform capacity.
//      Solves the "assign each used-filament on curr layer to a slot on next layer
//      minimizing total flush cost" problem.
//
//   4. MinFlushFlowSolver — MCMF with capacity constraints and link/unlink limits.
//      Used to enforce AMS slot constraints (e.g. slot can hold at most k filaments).
//
//   5. MatchModeGroupSolver — MCMF variant where right-side nodes have non-uniform
//      capacities; used for group-mode multi-nozzle matching.
//
// Public algorithms:
//   get_extruders_order() — dispatches to one of three strategies based on input size:
//     - use_forcast=true → O(N!·M!) brute-force over curr×next permutations (N≤5)
//     - curr_size ≤ 20   → bitmask DP Hamiltonian path (TSP), O(2^N · N²)
//     - curr_size > 20   → greedy nearest-neighbour, O(N²)
//
//   reorder_filaments_for_minimum_flush_volume() — top-level scheduler. Groups
//   filaments by AMS group (group 0 / group 1), then for each layer calls
//   get_extruders_order() per group, caches results by (curr_set, next_set, prev_id)
//   hash key (boost::multiprecision::uint128_t bit-packing), and interleaves group
//   sequences to minimise cross-group transitions.
//
// [HAZARD H850] solve_extruder_order (Hamiltonian path DP) is gated at ≤20 extruders.
// At 20 extruders: 2^20 × 20² = ~419M iterations, ~6.4 GB for cache[][] (float).
// This will crash the slicer on machines with <8 GB RAM for exactly 20 extruders.
// Should be bounded lower (≤16) or use int16 for cache values.
//
// [HAZARD H851] solve_extruder_order_with_forcast is O((N!)² where N = curr/next layer
// extruder count. Gated at N≤5, giving at most 120² = 14,400 iterations. The gate is
// (curr_size ≤ 5 AND next_size ≤ 5), but the variable `use_forcast` is checked at the
// caller, and `max_n_with_forcast = 5` is defined in .cpp not exposed here — any
// future increase of this constant without updating the gate could cause factorial blowup.
//
// [CONCURRENCY] All solvers are stateless between invocations (state is constructor-local).
// reorder_filaments_for_minimum_flush_volume is NOT thread-safe (modifies filament_sequences).
//
// [COUPLING] FlushMatrix is vector<vector<float>> indexed by [from_filament][to_filament].
// Sizes must match the total number of filaments; no bounds checking.

#ifndef TOOL_ORDER_UTILS_HPP
#define TOOL_ORDER_UTILS_HPP

#include <vector>
#include <optional>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_set>

namespace Slic3r {

using FlushMatrix = std::vector<std::vector<float>>;

namespace MaxFlowGraph {
const int INF        = std::numeric_limits<int>::max();
const int INVALID_ID = -1;
} // namespace MaxFlowGraph

class MaxFlowSolver
{
private:
    struct Edge
    {
        int from, to, capacity, flow;
        Edge(int u, int v, int cap) : from(u), to(v), capacity(cap), flow(0) {}
    };

public:
    MaxFlowSolver(const std::vector<int>&                          u_nodes,
                  const std::vector<int>&                          v_nodes,
                  const std::unordered_map<int, std::vector<int>>& uv_link_limits   = {},
                  const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {},
                  const std::vector<int>&                          u_capacity       = {},
                  const std::vector<int>&                          v_capacity       = {});
    std::vector<int> solve();

private:
    void add_edge(int from, int to, int capacity);

    int                           total_nodes;
    int                           source_id;
    int                           sink_id;
    std::vector<Edge>             edges;
    std::vector<int>              l_nodes;
    std::vector<int>              r_nodes;
    std::vector<std::vector<int>> adj;
};

struct MinCostMaxFlow;

class GeneralMinCostSolver
{
public:
    GeneralMinCostSolver(const std::vector<std::vector<float>>& matrix_, const std::vector<int>& u_nodes, const std::vector<int>& v_nodes);

    std::vector<int> solve();
    ~GeneralMinCostSolver();

private:
    std::unique_ptr<MinCostMaxFlow> m_solver;
};

class MinFlushFlowSolver
{
public:
    MinFlushFlowSolver(const std::vector<std::vector<float>>&           matrix_,
                       const std::vector<int>&                          u_nodes,
                       const std::vector<int>&                          v_nodes,
                       const std::unordered_map<int, std::vector<int>>& uv_link_limits   = {},
                       const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {},
                       const std::vector<int>&                          u_capacity       = {},
                       const std::vector<int>&                          v_capacity       = {});
    std::vector<int> solve();
    ~MinFlushFlowSolver();

private:
    std::unique_ptr<MinCostMaxFlow> m_solver;
};

class MatchModeGroupSolver
{
public:
    MatchModeGroupSolver(const std::vector<std::vector<float>>&           matrix_,
                         const std::vector<int>&                          u_nodes,
                         const std::vector<int>&                          v_nodes,
                         const std::vector<int>&                          v_capacity,
                         const std::unordered_map<int, std::vector<int>>& uv_unlink_limits = {});

    std::vector<int> solve();
    ~MatchModeGroupSolver();

private:
    std::unique_ptr<MinCostMaxFlow> m_solver;
};

std::vector<unsigned int> get_extruders_order(const std::vector<std::vector<float>>& wipe_volumes,
                                              const std::vector<unsigned int>&       curr_layer_extruders,
                                              const std::vector<unsigned int>&       next_layer_extruders,
                                              const std::optional<unsigned int>&     start_extruder_id,
                                              bool                                   use_forcast = false,
                                              float*                                 cost        = nullptr);

int reorder_filaments_for_minimum_flush_volume(const std::vector<unsigned int>&                           filament_lists,
                                               const std::vector<int>&                                    filament_maps,
                                               const std::vector<std::vector<unsigned int>>&              layer_filaments,
                                               const std::vector<FlushMatrix>&                            flush_matrix,
                                               std::optional<std::function<bool(int, std::vector<int>&)>> get_custom_seq,
                                               std::vector<std::vector<unsigned int>>*                    filament_sequences);

} // namespace Slic3r
#endif // !TOOL_ORDER_UTILS_HPP
