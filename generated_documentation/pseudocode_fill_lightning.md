# Pseudocode: Lightning Infill Tree Generation (T205B)

**Source files:**
- `src/libslic3r/Fill/Lightning/Generator.cpp` — top-level orchestrator
- `src/libslic3r/Fill/Lightning/Layer.cpp` — per-layer tree growth
- `src/libslic3r/Fill/Lightning/TreeNode.cpp` — Node operations (propagate, prune, straighten, convert)
- `src/libslic3r/Fill/Lightning/DistanceField.hpp` — overhang sampling grid
- `src/libslic3r/Fill/FillLightning.cpp` — fill adapter and raw pointer lifecycle

---

## Overview

Lightning Infill grows a forest of branching polylines downward through the
object (top-to-bottom), starting from unsupported overhang regions on each
layer and rooting branches onto the outline of the layer below. The result
is a minimal-material structure that supports every overhang point within a
configurable "supporting radius."

**Key concept — supporting radius:**  
Each infill branch segment "supports" all overhang points within
`supporting_radius` of the line. Points covered by a branch are removed from
the distance field; the algorithm terminates when all remaining overhang
points are covered.

---

## Data Structures

```
Generator:
  m_infill_extrusion_width : coord_t  // scaled line width
  m_supporting_radius      : coord_t  // circle-of-influence per infill line
  m_wall_supporting_radius : coord_t  // = layer_thickness * tan(45°)
  m_prune_length           : coord_t  // leaf retraction per layer
  m_straightening_max_distance : coord_t  // max node shift per pass
  m_overhang_per_layer     : vector<Polygons>   // per-layer unsupported regions
  m_lightning_layers       : vector<Layer>      // per-layer tree forests
  bboxs                    : vector<BoundingBox>

Layer:
  tree_roots : vector<NodeSPtr>  // roots of all lightning trees on this layer

Node (TreeNode):
  m_p                     : Point             // 2D position (scaled integer µm)
  m_is_root               : bool
  m_parent                : weak_ptr<Node>    // null for roots
  m_children              : vector<NodeSPtr>
  m_last_grounding_location : optional<Point> // boundary anchor for reconnect

DistanceField:
  m_cell_size              : coord_t          // = supporting_radius
  m_supporting_radius      : coord_t
  m_unsupported_points     : vector<UnsupportedCell>   // sampled overhang grid
  m_unsupported_points_erased : vector<bool>
  m_unsupported_points_grid : UnsupportedPointsGrid    // flat 2D index

UnsupportedCell:
  loc              : Point    // grid sample position
  dist_to_boundary : coord_t  // distance to nearest outline edge

GroundingLocation:
  tree_node         : NodeSPtr          // non-null → attach to existing tree
  boundary_location : optional<Point>   // non-null → attach to outline

SparseNodeGrid:
  multimap<Point, weak_ptr<Node>>  // keyed by grid-cell address
```

---

## Phase 1: Parameter Initialisation

```
function Generator(print_object, throw_on_cancel):
    // Read extrusion width with fallback chain
    width = region_config.sparse_infill_line_width scaled
    if width < EPSILON:
        width = object_config.line_width scaled
        if width < EPSILON:
            width = Flow::auto_extrusion_width(nozzle_diameter)
    m_infill_extrusion_width = width

    // Supporting radius: spacing per infill line × multiline count
    // [HAZARD H277] division by density — no guard if density == 0.0
    m_supporting_radius = width * 100 * n_multiline / sparse_infill_density

    // All three geometric radii use hardcoded 45° angles
    // [HAZARD H272] angles are not user-configurable in OrcaSlicer
    layer_thickness = object_config.layer_height scaled
    m_wall_supporting_radius     = layer_thickness * tan(45°)  // = layer_thickness
    m_prune_length               = layer_thickness * tan(45°)
    m_straightening_max_distance = layer_thickness * tan(45°)

    generateInitialInternalOverhangs(print_object, throw_on_cancel)
    generateTrees(print_object, throw_on_cancel)
```

---

## Phase 2: Overhang Computation (`generateInitialInternalOverhangs`)

Iterates from **top to bottom**. Each layer's overhang is the part of its
infill area not covered by the layer above, eroded by the wall-support radius.

```
function generateInitialInternalOverhangs(print_object, throw_on_cancel):
    resize m_overhang_per_layer to N layers
    infill_area_above = empty Polygons

    for layer_nr = N-1 downto 0:
        throw_on_cancel()

        // Collect all stInternal + stInternalVoid fill surfaces
        infill_area_here = union of expolygons where surface_type in
                           {stInternal, stInternalVoid}

        // Erode by wall_supporting_radius → remove parts already near walls.
        // Subtract infill_area_above → keep only truly overhanging regions.
        // [HAZARD H282] narrow infill (< wall_supporting_radius) collapses to empty
        overhang = diff(
            offset(infill_area_here, -m_wall_supporting_radius),
            infill_area_above
        )

        m_overhang_per_layer[layer_nr] = overhang
        infill_area_above = infill_area_here   // move for next iteration
```

---

## Phase 3: Tree Construction (`generateTrees`)

Builds the lightning forest **top-to-bottom**. The loop maintains an
`EdgeGrid` of the current layer's infill outline for fast boundary queries.

```
function generateTrees(print_object, throw_on_cancel):
    resize m_lightning_layers, bboxs to N layers
    infill_outlines[i] = collect stInternal + stInternalVoid polygons for layer i

    // Initialise EdgeGrid for the top layer
    top_id = N - 1
    outlines_locator = EdgeGrid built from infill_outlines[top_id]

    for layer_id = top_id downto 0:
        throw_on_cancel()
        current_layer    = m_lightning_layers[layer_id]
        current_outlines = infill_outlines[layer_id]
        bboxs[layer_id]  = bounding box of current_outlines

        // Roots propagated from the layer above (set in previous iteration)
        to_reconnect = current_layer.tree_roots  // snapshot before growth

        // Step A: Grow new branches for all unsupported overhang points
        current_layer.generateNewTrees(
            m_overhang_per_layer[layer_id],
            current_outlines, bboxs[layer_id],
            outlines_locator,
            m_supporting_radius, m_wall_supporting_radius,
            throw_on_cancel)

        // Step B: Reattach orphaned roots from the layer above
        current_layer.reconnectRoots(
            to_reconnect, current_outlines, bboxs[layer_id],
            outlines_locator, m_supporting_radius, m_wall_supporting_radius)

        // Step C: Propagate trees to layer below (skip for layer 0)
        if layer_id == 0: return

        below_outlines = infill_outlines[layer_id - 1]
        // Expand bbox to cover both outlines and any dangling tree nodes
        // [HAZARD H283] empty top layer → degenerate EdgeGrid box
        below_bbox = get_extents(below_outlines).inflated(SCALED_EPSILON)
        below_bbox.merge(outlines_locator.bbox())
        if current_layer.tree_roots not empty:
            below_bbox.merge(get_extents(current_layer.tree_roots))

        outlines_locator = EdgeGrid rebuilt from below_outlines with below_bbox

        // [HAZARD H284] lower_trees is a reference into m_lightning_layers;
        // safe only because the vector is never reallocated after resize()
        lower_trees = m_lightning_layers[layer_id - 1].tree_roots
        for each tree in current_layer.tree_roots:
            tree.propagateToNextLayer(
                lower_trees, below_outlines, outlines_locator,
                m_prune_length, m_straightening_max_distance,
                locator_cell_size / 2)
```

---

## Phase 4: Branch Growth Loop (`Layer::generateNewTrees`)

The core algorithm. Asks the distance field for the "most interior"
unsupported point, attaches a new branch, then marks nearby cells as
supported.

```
function Layer::generateNewTrees(current_overhang, current_outlines,
                                  outlines_bbox, outlines_locator,
                                  supporting_radius, wall_supporting_radius,
                                  throw_on_cancel):

    // Build distance field: samples current_overhang at cell_size intervals,
    // sorted by dist_to_boundary descending (interior-first order)
    distance_field = DistanceField(supporting_radius, current_outlines,
                                   outlines_bbox, current_overhang)
    throw_on_cancel()

    // Build spatial index of already-existing tree nodes (propagated from above)
    tree_node_locator = SparseNodeGrid()
    fillLocator(tree_node_locator, outlines_bbox)

    unsupported_cell_idx = 0
    // [HAZARD H295] cancel check only once per outer iteration; slow iterations
    // can delay cancellation by several seconds
    while distance_field.tryGetNextPoint(
              &unsupported_location, &unsupported_cell_idx,
              start=unsupported_cell_idx):
        throw_on_cancel()

        // Find the best place to attach the new branch
        grounding = getBestGroundingLocation(
            unsupported_location, current_outlines, outlines_bbox,
            outlines_locator, supporting_radius, wall_supporting_radius,
            tree_node_locator)

        // Attach: create Node(s) and insert into tree_roots / child list
        (new_child, new_parent) = attach(unsupported_location, grounding)

        // Insert new node(s) into spatial index for future iterations
        tree_node_locator.insert(grid_cell(new_child), new_child)
        if new_parent != null:
            tree_node_locator.insert(grid_cell(new_parent), new_parent)

        // Mark cells within supporting_radius of the new branch as supported
        distance_field.update(grounding.p(), unsupported_location)
```

---

## Phase 5: Grounding Location Selection (`Layer::getBestGroundingLocation`)

Chooses between attaching to the outline boundary or to an existing tree
node. TBB parallel search over a grid bounding box of candidate nodes.

```
function getBestGroundingLocation(unsupported_location, current_outlines,
                                   outlines_bbox, outline_locator,
                                   supporting_radius, wall_supporting_radius,
                                   tree_node_locator,
                                   exclude_tree = null):

    // Step 1: Linear scan for closest boundary point
    // [HAZARD H289] O(N_vertices) — no spatial acceleration
    node_location = closest point on any edge of current_outlines to unsupported_location
    current_dist  = Euclidean distance(node_location, unsupported_location)
    sub_tree      = null

    // Step 2: Search tree nodes only when not already close to the wall
    if current_dist >= wall_supporting_radius:
        search_radius = min(current_dist, dist(node_location, unsupported_location))
        region = grid cells in box [unsupported ± search_radius]

        // [HAZARD H297] current_dist captured both by-ref (shared) and by-value
        // (task-local copy). Tasks compete against the initial baseline, not against
        // each other's intermediate results.
        current_dist_mutex = Mutex()
        current_dist_grid_addr = (-∞, -∞)  // for deterministic tie-breaking

        tbb::parallel_for over 2D grid cell range [region.min .. region.max]:
            for each (grid_addr_y, grid_addr_x) in range:
                local_best_dist = current_dist_copy   // task-local snapshot
                local_sub_tree  = null
                for each candidate_node in tree_node_locator[grid_addr]:
                    if candidate_node is expired/null: skip
                    if candidate_node == exclude_tree: skip
                    if exclude_tree contains candidate_node (hasOffspring): skip
                    if segment [unsupported→candidate] crosses outline: skip
                    candidate_dist = candidate_node.getWeightedDistance(
                        unsupported_location, supporting_radius)
                    if candidate_dist < local_best_dist:
                        local_best_dist = candidate_dist
                        local_sub_tree  = candidate_node

                // Merge into shared best with deterministic tie-breaking
                lock(current_dist_mutex):
                    if local_best_dist < current_dist OR
                       (equal AND this grid_addr < current_dist_grid_addr):
                        current_dist           = local_best_dist
                        sub_tree               = local_sub_tree
                        current_dist_grid_addr = (grid_addr_y, grid_addr_x)

    // Step 3: Return result
    if sub_tree != null:
        return GroundingLocation{tree_node=sub_tree, boundary=null}
    else:
        return GroundingLocation{tree_node=null, boundary=node_location}
```

### Weighted Distance Heuristic (`Node::getWeightedDistance`)

Encourages attaching to partially-used nodes (bushy over linear trees):

```
valence = (1 if not root) + child_count
// boost applied only for 0 < valence < 4
valence_boost = (valence in (0,4)) ? 4 * supporting_radius : 0
return Euclidean_distance(self, unsupported) - valence_boost
```

---

## Phase 6: Branch Attachment (`Layer::attach`)

```
function attach(unsupported_location, grounding_loc) -> (new_child, new_root):
    if grounding_loc.boundary_location != null:
        // Create a new root pinned to the outline, child hangs inward
        new_root  = Node::create(grounding_loc.p(), last_grounding=grounding_loc.p())
        new_child = new_root.addChild(unsupported_location)
        tree_roots.push_back(new_root)
        return (new_child, new_root)
    else:
        // Graft unsupported point onto an existing tree node
        new_child = grounding_loc.tree_node.addChild(unsupported_location)
        return (new_child, null)
    // [HAZARD H290] push_back may reallocate tree_roots
```

---

## Phase 7: Cross-Layer Propagation (`Node::propagateToNextLayer`)

Each tree is deep-copied then pruned, straightened, and realigned to the
outline of the layer below.

```
function Node::propagateToNextLayer(next_trees, next_outlines,
                                     outline_locator,
                                     prune_distance, smooth_magnitude,
                                     max_remove_colinear_dist):
    tree_below = deepCopy()        // O(N_nodes) allocation
    tree_below.prune(prune_distance)
    tree_below.straighten(smooth_magnitude, max_remove_colinear_dist)
    if tree_below.realign(next_outlines, outline_locator, next_trees):
        next_trees.push_back(tree_below)
    // rerooted subtrees were already appended inside realign()
```

### Prune (`Node::prune`)

Removes leaf segments inward until `pruning_distance` microns have been
consumed, or moves the leaf to the cutpoint:

```
function prune(pruning_distance) -> dist_pruned:
    if pruning_distance <= 0: return 0
    max_dist_pruned = 0
    for each child (iterator loop, erase-safe):
        child_pruned = child.prune(pruning_distance)
        if child_pruned >= pruning_distance:
            // child survived; keep
            max_dist_pruned = max(max_dist_pruned, child_pruned)
        else:
            seg_len = length(this → child)
            if child_pruned + seg_len <= pruning_distance:
                // entire child segment consumed — erase child
                max_dist_pruned = max(max_dist_pruned, child_pruned + seg_len)
                erase child
            else:
                // pruning boundary is inside this segment; move child to cutpoint
                cutpoint = child.p + (this.p - child.p).normalized
                             * (pruning_distance - child_pruned)
                child.setLocation(cutpoint)
                max_dist_pruned = max(max_dist_pruned, pruning_distance)
    return max_dist_pruned
```

### Straighten (`Node::straighten`)

Smooths single-child chains by interpolating toward the straight line between
the nearest junctions above and below. Junctions (multi-child nodes) are
nudged toward the weighted centroid of their adjacent directions.

```
function straighten(magnitude, junction_above, accumulated_dist,
                    max_remove_colinear_dist2) -> RectilinearJunction:
    if children.size() == 1:
        child = children[0]
        child_dist = length(this → child)
        junction_below = child.straighten(magnitude, junction_above,
                             accumulated_dist + child_dist,
                             max_remove_colinear_dist2)
        total_dist = junction_below.total_recti_dist
        a = junction_above;  b = junction_below.junction_loc
        if a != b:
            // Interpolate ideal position along [a, b]
            // [HAZARD H308] integer rounding ±1 nm per pass
            destination = a + (b-a) * accumulated_dist / total_dist
            move this.p toward destination by at most magnitude
        // Collinearity removal: if this node lies on the line parent→child
        // within close_enough=10 nm AND distance(parent, child) < max_remove,
        // splice this node out of the chain
        if parent and Line.distance_to_sq(this.p, parent.p, child.p) < 100:
            child.m_parent = this.m_parent
            replace this node with child in parent.m_children
        return junction_below
    else:
        // Junction node: nudge toward weighted centroid of adjacent directions
        junction_moving_dir = normalised(junction_above - this.p) * weight
        prevent_move = false
        for each child:
            below = child.straighten(...)
            junction_moving_dir += normalised(below.junction_loc - this.p) * weight
            if below.total_recti_dist < magnitude:
                prevent_move = true  // short branch → freeze junction
        if not prevent_move and junction_moving_dir != (0,0):
            clamp junction_moving_dir to junction_magnitude (= 3/4 * magnitude)
            this.p += junction_moving_dir
        return RectilinearJunction{accumulated_dist, this.p}
```

### Realign (`Node::realign`)

Snaps the tree to the new outline. Nodes outside the outline are discarded;
connected segments that cross the outline are severed and promoted as orphan
roots (handled by `reconnectRoots` in the next pass).

```
function realign(outlines, outline_locator, rerooted_parts) -> bool:
    if outlines is empty: return false
    if inside(outlines, this.p):
        reground_me = false
        for each child (erase_if loop):
            child_inside = child.realign(outlines, outline_locator, rerooted_parts)
            if child_inside and segment [child→this] crosses outline:
                // Sever child; promote as orphan root for reconnectRoots
                child.m_last_grounding_location = null
                child.m_is_root = true
                rerooted_parts.push_back(child)
                reground_me    = true
                child_inside   = false
            keep child iff child_inside
        if reground_me: this.m_last_grounding_location = null
        return true
    else:
        // This node is outside; promote any inside children as orphan roots
        for each child:
            if child.realign(outlines, outline_locator, rerooted_parts):
                child.m_last_grounding_location = this.p
                child.m_is_root = true
                rerooted_parts.push_back(child)
        clear m_children
        return false
```

---

## Phase 8: Root Reconnection (`Layer::reconnectRoots`)

After propagation, some roots land outside the new outline. This function
reattaches them either via a fast boundary-snap or the full grounding search.

```
function reconnectRoots(to_be_reconnected_tree_roots, current_outlines,
                         outlines_bbox, outline_locator,
                         supporting_radius, wall_supporting_radius):
    tree_connecting_ignore_offset = 100 nm  // prevents perpetual re-rooting
    tree_node_locator = SparseNodeGrid built from current tree_roots

    within_max_dist = outline_locator.resolution() * 2

    for each root_ptr in to_be_reconnected_tree_roots:
        old_root_it = find(tree_roots, root_ptr)
        // [HAZARD H291] if root_ptr not in tree_roots, old_root_it == end → UB on deref

        if root_ptr.last_grounding_location exists:
            ground_loc = root_ptr.last_grounding_location
            if ground_loc != root_ptr.p:
                // Fast path: find where segment [root_ptr.p → ground_loc]
                // crosses the new outline within within_max_dist of ground_loc
                if lineSegmentPolygonsIntersection(root_ptr.p, ground_loc,
                                                   outline_locator,
                                                   new_root_pt,
                                                   within_max_dist):
                    new_root = Node::create(new_root_pt, new_root_pt)
                    root_ptr.addChild(new_root)
                    new_root.reroot()           // flip parent-child chain
                    tree_node_locator.insert(new_root)
                    *old_root_it = new_root     // replace in tree_roots
                    continue

        // General path: full grounding search (excludes self-tree)
        reduced_wall_r = wall_supporting_radius - tree_connecting_ignore_offset
        ground = getBestGroundingLocation(root_ptr.p, ..., tree_node_locator,
                                          exclude_tree=root_ptr)
        if ground.boundary_location:
            if *ground.boundary_location == root_ptr.p: continue  // already on boundary
            new_root   = Node::create(ground.p(), ground.p())
            attach_ptr = root_ptr.closestNode(new_root.p())
            attach_ptr.reroot()
            new_root.addChild(attach_ptr)
            *old_root_it = new_root
        else:
            // Attach to another tree: reroot and graft
            attach_ptr = root_ptr.closestNode(ground.tree_node.p())
            attach_ptr.reroot()
            ground.tree_node.addChild(attach_ptr)
            // Remove old root via pop-back swap
            // [HAZARD H298] destroys tree_roots ordering
            *old_root_it = tree_roots.back()
            tree_roots.pop_back()
```

---

## Phase 9: Tree → Polylines (`Layer::convertToLines` / `Node::convertToPolylines`)

Converts the final per-layer forest to Polylines for G-code generation.

```
function Layer::convertToLines(limit_to_outline, line_overlap) -> Polylines:
    if tree_roots is empty: return []
    result_lines = []
    for each tree in tree_roots:
        tree.convertToPolylines(result_lines, line_overlap)
    // Clip all lines to the infill outline boundary
    // [HAZARD H292] costly for many trees + complex outline
    return intersection_pl(result_lines, limit_to_outline)

function Node::convertToPolylines(output, line_overlap):
    result = [empty Polyline]
    convertToPolylines_recursive(long_line_idx=0, result)
    removeJunctionOverlap(result, line_overlap)
    append result to output

function convertToPolylines_recursive(long_line_idx, output):
    if children is empty:
        output[long_line_idx].push_back(this.p)
        return
    // [HAZARD H273] rand() — non-deterministic child selection
    first_child_idx = rand() % children.size()
    children[first_child_idx].convertToPolylines_recursive(long_line_idx, output)
    output[long_line_idx].push_back(this.p)  // append junction after long-line child

    for each other child (idx_offset = 1..N-1):
        child_idx = (first_child_idx + idx_offset) % children.size()
        output.emplace_back()  // new polyline for branch
        children[child_idx].convertToPolylines_recursive(output.size()-1, output)
        output.back().push_back(this.p)

function removeJunctionOverlap(result_lines, line_overlap):
    // Walk backward along each polyline from the junction end,
    // trimming 'line_overlap' microns. Degenerate (≤1 point) lines
    // are removed via swap-and-pop.
    // [HAZARD H309] swap-and-pop destroys polyline ordering
```

---

## Phase 10: Fill Adapter (`FillLightning::Filler::_fill_surface_single`)

Called once per ExPolygon per layer during G-code generation. The Generator
is already built; this is read-only access to the precomputed trees.

```
function _fill_surface_single(params, thickness_layers, direction,
                               expolygon, polylines_out):
    // [HAZARD H267] generator is a raw non-owning pointer; no null-check
    layer     = generator.getTreesForLayer(this.layer_id)
    // [HAZARD H268] no range check in release; UB if layer_id out of bounds

    // Convert tree branches to Polylines; clip to expolygon outline
    line_overlap = scaled(0.5 * this.spacing - this.overlap)
    fill_lines   = layer.convertToLines(to_polygons(expolygon), line_overlap)

    // Orca multiline extension (no-op if params.multiline == 1)
    multiline_fill(fill_lines, params, spacing)

    // Re-clip (multiline offsets may push lines outside boundary)
    fill_lines = intersection_pl(move(fill_lines), expolygon)

    // Reorder / connect for travel-move minimisation
    chain_or_connect_infill(move(fill_lines), expolygon, polylines_out,
                            this.spacing, params)
```

---

## Complexity Analysis

| Phase | Complexity | Notes |
|---|---|---|
| `generateInitialInternalOverhangs` | O(N_layers × N_surfaces) | Clipper offset + diff per layer |
| `DistanceField` construction | O(N_samples × N_contour_verts) | Grid sampling |
| `generateNewTrees` outer loop | O(N_unsupported) per layer | Driven by distance field |
| `getBestGroundingLocation` step 1 | O(N_vertices) per call | No spatial index [H289] |
| `getBestGroundingLocation` step 2 | O(K) per TBB task | K = nodes in search box |
| `propagateToNextLayer` | O(N_nodes) per tree | deepCopy + prune + straighten + realign |
| `convertToLines` | O(N_nodes + N_clip) | DFS + Clipper intersection |
| **Total** | **O(N_layers × N_overhang_points)** | Dominant cost |

---

## Translation Notes

### TN1 — Raw Generator Pointer Lifecycle [HAZARD H267]
`FillLightning::Filler::generator` is a **raw non-owning pointer** set by
`PrintObject::prepare_infill` and never cleared. If the PrintObject is
destroyed, the generator is rebuilt, or a settings change triggers
recomputation between `prepare_infill` and the fill loop, the pointer is
dangling. The `GeneratorDeleter` / `GeneratorPtr` (`unique_ptr` with custom
deleter) pattern in `FillLightning.hpp` manages the owned copy on
`PrintObject`; the `Filler` only borrows it. A port **must** preserve the
ownership split: `PrintObject` owns, `Filler` borrows. Using a `shared_ptr`
in `Filler` would be the safe idiomatic alternative.
Source: `src/libslic3r/Fill/FillLightning.cpp:74–78`

### TN2 — Two Separate Constructor Paths with Different `supporting_radius` Formulas
The infill constructor multiplies by 100 (`width * 100 * n_multiline / density`);
the support constructor does not (`width / density`). These produce
**numerically different** radii for the same nominal density. The ×100 in the
infill path is a coordinate-scaling factor (density is a pure fraction 0–1;
the ×100 converts it to match Clipper's integer-µm unit system). A port must
treat these two paths as separate cases. Source: `Generator.cpp:175, 241`

### TN3 — Distance Field Ordering (Interior-First)
`DistanceField` sorts unsupported cells by `dist_to_boundary` **descending**
so that the most interior (highest risk) cells are served first. This means
`tryGetNextPoint` returns cells from the centre of an overhang outward.
The algorithm does **not** restart the scan from index 0 each iteration;
`unsupported_cell_idx` advances monotonically, giving O(N_cells) total scan
work across all iterations. Source: `DistanceField.hpp:43–54`

### TN4 — TBB Parallel Search with Deterministic Tie-Breaking
`getBestGroundingLocation` uses `tbb::parallel_for` over a 2D grid range.
To produce the same result as a serial scan, the merge step tracks
`current_dist_grid_addr` and breaks ties by (row, col) lexicographic order.
The `current_dist_copy` (value capture before the parallel loop) is the
baseline each task competes against, preventing a task from being influenced
by another task's in-progress update. A port that drops the tie-breaking
will produce non-deterministic tree shapes.
Source: `Layer.cpp:245–290`

### TN5 — `rand()` in `convertToPolylines` [HAZARD H273]
`Node::convertToPolylines_recursive` calls `rand() % m_children.size()` to
select which child continues the "long line". This is deliberately
non-deterministic — the functional output (infill coverage) is identical
regardless of which child is chosen. Ports may replace `rand()` with a seeded
PRNG for reproducible output. Source: `TreeNode.cpp:541`

### TN6 — `prune` / `straighten` / `realign` All Recurse on the Node Tree
All three operations use unbounded recursion with call depth = tree depth.
Lightning trees are typically shallow (branching factor > 1 limits depth),
but pathological input (a single long chain) could cause stack overflow.
Iterative equivalents using an explicit stack should be used in safety-
critical ports. Source: `TreeNode.cpp:59, 155, 301, 467`

### TN7 — `reconnectRoots` Pop-Back Swap [HAZARD H298]
When a root is reattached to another tree (not to the boundary), the old
root entry is removed from `tree_roots` via `*old_root_it = tree_roots.back();
tree_roots.pop_back()`. This is O(1) but **destroys the iteration order** of
`tree_roots`. Any downstream code that processes `tree_roots` in a stable
order (e.g., for deterministic G-code output) must re-sort after
`reconnectRoots`. Source: `Layer.cpp:424–425`

### TN8 — Hardcoded 45° Angles [HAZARD H272]
`m_wall_supporting_radius`, `m_prune_length`, and `m_straightening_max_distance`
are all `layer_thickness * tan(45°) = layer_thickness`. In Cura these
correspond to user-configurable `lightning_infill_overhang_angle`,
`lightning_infill_prune_angle`, and `lightning_infill_straightening_angle`
settings. OrcaSlicer has not exposed these controls. A port should expose them
as parameters rather than hardcoding 45°.
Source: `Generator.cpp:181–187`

### TN9 — `GeneratorDeleter` Custom Deleter [HAZARD H264]
`GeneratorPtr = unique_ptr<Generator, GeneratorDeleter>`. The custom deleter
exists to ensure `~Generator()` is called in the TU where `Generator` is fully
defined (`Generator.cpp`), not in the header-only context. Removing it and
using plain `unique_ptr<Generator>` is safe in a single-TU build but causes
heap corruption in MSVC `/MT` (multi-threaded static CRT) DLL builds where each
TU has its own heap. A port should keep the custom deleter or ensure the full
type definition is visible at every deletion point.
Source: `src/libslic3r/Fill/FillLightning.cpp:127–136`

### TN10 — `FillAdaptive` Namespace Comment Bug [HAZARD H265 / H266]
`FillLightning.cpp` and `FillLightning.hpp` contain closing-brace comments
`// namespace FillAdaptive` (copy-paste from `FillAdaptive.cpp`). The
**code** is correct (the actual namespace is `FillLightning`); only the
comment is wrong. Automated namespace-extraction or documentation tools that
parse comments will produce incorrect results. Source: `FillLightning.cpp:151`
