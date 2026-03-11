# Pseudocode: Arachne Straight Skeleton and Variable-Width Wall Extraction (T4024)

**Source files:**
- `src/libslic3r/Arachne/WallToolPaths.cpp` — polygon preprocessing, strategy construction, final stitching
- `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp` — medial-axis graph construction, transition placement, segment generation
- `src/libslic3r/Arachne/SkeletalTrapezoidation.hpp` — phase ordering and dataflow contract
- `src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.cpp` — beading-strategy decorator dispatch
- `src/libslic3r/Arachne/utils/ExtrusionJunction.hpp` — width-bearing junction sample format
- `src/libslic3r/Arachne/utils/ExtrusionLine.hpp` — output wall polyline format

**Purpose:** Convert one slice polygon island into printable perimeter bands whose line width changes continuously with local part thickness.

---

## Main Pipeline

1. `function build_arachne_toolpaths(outline, wall_params)` - `O(V log V + E x B)` total
   1. If no walls are requested, return empty output. - `O(1)`
   2. Preprocess `outline` with topology-repair passes: triple-offset snap, simplify, self-intersection repair, degenerate-vertex removal, near-colinear cleanup, small-area culling, final union. - `O(V log V)`
   3. If the repaired outline is empty, return empty output. - `O(1)`
   4. Build `beading_strategy = make_beading_strategy(wall_params)`. - `O(1)`
   5. Build `graph = construct_skeletal_trapezoidation(repaired_outline, wall_params.transition_angle, wall_params.discretization_step)`. - `O(V log V)`
   6. Run `generate_variable_width_paths(graph, beading_strategy, wall_params)`. - `O(E x B)`
   7. Stitch, prune, contour-split, and simplify the emitted lines. - `O(P log P)` typical
   8. Return the final `vector<VariableWidthLines>`. - `O(1)`

---

## Beading Strategy Selection

2. `function make_beading_strategy(params)` - `O(1)`
   1. Choose `optimal_width`:
      - If `max_bead_count <= 2`, use the outer-wall preferred width.
      - Otherwise use the inner-wall preferred width.
   2. Create `strategy = DistributedBeadingStrategy(optimal_width, transition_length, transition_angle, split_threshold, add_threshold, inward_center_count)`. - `O(1)`
   3. Wrap with `RedistributeBeadingStrategy(outer_width, minimum_variable_line_ratio, strategy)`. - `O(1)`
   4. If thin-wall printing is enabled, wrap with `WideningBeadingStrategy(strategy, min_feature_size, min_bead_width)`. - `O(1)`
   5. If outer-wall offset is non-zero, wrap with `OuterWallInsetBeadingStrategy(offset, strategy)`. - `O(1)`
   6. Wrap last with `LimitedBeadingStrategy(max_bead_count, strategy)`. - `O(1)`
   7. Return the composed strategy.

Result: later phases can ask only two questions at each local diameter `2R`:
- `optimal_bead_count = strategy.getOptimalBeadCount(2R)`
- `beading = strategy.compute(2R, bead_count)`

---

## Straight Skeleton / Skeletal Trapezoidation Construction

3. `function construct_skeletal_trapezoidation(polygons, transition_angle, discretization_step)` - `O(V log V + E)`
   1. Enumerate all polygon segments. - `O(V)`
   2. Build a Voronoi diagram over those segments. - `O(V log V)`
   3. Create an empty half-edge graph with stable node/edge storage. - `O(1)`
   4. For each Voronoi edge that lies inside the polygon:
      1. If the Voronoi arc is straight, keep it as one graph edge. - `O(1)`
      2. If the Voronoi arc is parabolic, discretize it into short line segments. - `O(k)` for `k` samples
      3. Transfer the segment chain into the half-edge graph.
      4. Add rib edges so the graph still knows which boundary edges each skeleton segment belongs to.
   5. Collapse zero-length or otherwise degenerate graph cells introduced by Voronoi rounding. - `O(E)` amortized
   6. Repair pointy end-node topology so later quad traversals see consistent entry/exit edges. - `O(E)`
   7. Return the graph.

Interpretation:
- Central edges represent the usable medial axis.
- Rib edges connect the medial axis back to the original polygon boundary.
- Distance-to-boundary `R` is stored at graph nodes and acts as the local thickness scalar field.

---

## Main Straight-Skeleton Loop

4. `function generate_variable_width_paths(graph, strategy, params)` - `O(E x B)`
   1. `update_is_central(graph, strategy.transition_angle)` - `O(E)`
   2. `filter_small_central_whiskers(graph, central_filter_distance)` - `O(E)` typical
   3. Optionally `filter_outer_central_edges(graph)` if the caller wants sharp corners to loop. - `O(E)`
   4. `assign_bead_counts_to_central_nodes(graph, strategy)` - `O(E)`
   5. `fill_noncentral_regions(graph)` so ribs and peninsulas inherit coherent bead counts. - `O(E)` typical
   6. `generate_transition_ribs(graph, strategy, transition_filter_distance)` - `O(E)` to `O(E log E)` depending on inserted nodes
   7. `generate_extra_ribs_for_non_linear_bead_profiles(graph)` - `O(E x B)`
   8. `return emit_extrusion_lines(graph, strategy, params)` - `O(E x B)`

5. `function update_is_central(graph, transition_angle)` - `O(E)`
   1. For each half-edge, compute:
      - `dR = abs(R_to - R_from)`
      - `dD = geometric_length(edge)`
      - `cap = sin(transition_angle / 2)`
   2. Mark the edge central when `dR < dD * cap` and it is not an extra edge and not too close to the boundary.
   3. Mirror the result onto the twin edge.

Meaning: edges whose boundary-distance changes slowly enough are treated as part of the medial axis; steep radius changes are treated as ribs.

6. `function assign_bead_counts_to_central_nodes(graph, strategy)` - `O(E)`
   1. For each central node or edge neighborhood with local diameter `2R`, ask the strategy for the optimal bead count.
   2. Store that integer count at the node.
   3. Leave unresolved degenerate tips at `-1`; they are fixed later by fallback propagation.

7. `function generate_transition_ribs(graph, strategy, transition_filter_distance)` - `O(E)` typical
   1. For each edge where bead count changes between its endpoints:
      1. Compute one or more transition midpoints where the lower bead count stops being valid.
      2. Filter nearby transitions that would produce tiny oscillating regions.
      3. Insert `TransitionEnd` nodes on the edge and mirror them across twins.
   2. Split edges at those transition points so later phases can treat each side as a stable local wall-count region.

---

## Output Assembly

8. `function emit_extrusion_lines(graph, strategy, params)` - `O(E x B)`
   1. Collect all upward quad-middle edges and sort them by decreasing radius `R`. - `O(E log E)`
   2. For each node whose bead count is already known:
      1. If the node lies exactly on a discrete bead-count region, store `strategy.compute(2R, bead_count)`. - `O(1)`
      2. If the node lies on a transition, interpolate between `compute(2R, bead_count)` and `compute(2R, bead_count + 1)`. - `O(B)`
   3. Propagate beadings upward toward unresolved maxima. - `O(E x B)`
   4. Propagate beadings downward across non-central edges, blending with upward-propagated beadings when both exist. - `O(E x B)`
   5. Generate junction lists on every relevant edge. Each junction is:
      - position `(x, y)`
      - local width `w`
      - perimeter band index
   6. Connect junction lists quad-by-quad into `ExtrusionLine` polylines. - `O(E x B)`
   7. Emit local-maxima single-bead circles for isolated tips. - `O(M)`
   8. Return the banded line vector.

9. `function connect_junctions_into_lines(graph_edge_junctions)` - `O(E x B)`
   1. Identify every quad start in the polygon-domain decomposition.
   2. For each quad chain:
      1. Find the edge whose destination radius is maximal.
      2. Read the junction lists on the two sides of the quad.
      3. Pair the junctions from innermost to outermost.
      4. For each pair, append a segment to the matching `VariableWidthLines[inset_idx]`.
      5. Reuse the previous line if the last emitted junction matches the next segment endpoint within tolerance; otherwise start a new line.
   3. Keep odd-centerline segments from being emitted twice when both twins visit the same region.

10. `function finalize_wall_lines(raw_lines, bead_width)` - `O(P log P)` typical
    1. Stitch fragmented open polylines with nearest-endpoint matching. - `O(P log P)` typical
    2. Remove very short leftovers. - `O(P)`
    3. Separate 0-width contour markers from printable walls. - `O(P)`
    4. Simplify each resulting polyline while preserving both centerline error and deposited-area error. - `O(P)`
    5. Return the cleaned result.

---

## Translation Notes

- `TN 1` The graph is not just a centerline. It is a half-edge trapezoidation with ribs, twins, `prev/next`, and stable pointer identity. Replacing it with a plain adjacency list will break quad walks, transition mirroring, and junction pairing unless those concepts are reintroduced explicitly.
- `TN 2` Width assignment is two-stage: first a discrete bead-count field, then continuous per-node/per-junction interpolation. Porting only the integer bead-count logic will lose Arachne's main feature: smooth tapering between wall counts.
- `TN 3` `ExtrusionLine` output is semantic, not cosmetic. Closed loops duplicate endpoints, odd centerlines remain open, and `perimeter_index` is the stable wall-band identity. Later print-planning code depends on those conventions.
