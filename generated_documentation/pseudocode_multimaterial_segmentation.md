# Pseudocode: Multi-Material Segmentation by Painting

**Source file:** `src/libslic3r/MultiMaterialSegmentation.cpp` (2539 lines)  
**Task:** T205C  
**Related docs:** `03_algorithmic_complexities.md` §14, `04_refactoring_hazards.md` H561–H570

---

## Overview

Paint-based segmentation converts per-facet color annotations on the 3D mesh into per-extruder
`ExPolygon` regions for every 2D layer. Two public entry points share a common inner pipeline:

| Entry point | `num_facets_states` | Top/bottom | Use |
|---|---|---|---|
| `multi_material_segmentation_by_painting()` | `num_filaments + 1` | Yes | MMU colour gizmo |
| `fuzzy_skin_segmentation_by_painting()` | `2` | No | Fuzzy-skin gizmo |

Index `0` always means "unpainted / use default extruder". Indices `1..N` map to extruder slots.

---

## Public Entry Points

### `multi_material_segmentation_by_painting(print_object, throw_on_cancel)` — line 2490

```
num_facets_states = print_object.print().config.filament_colour.size() + 1
max_width         = mmu_segmented_region_max_width
interlocking_depth = mmu_segmented_region_interlocking_depth
interlocking_beam  = interlocking_beam config flag

extract_facets_info = lambda(mv) -> { mv.mmu_segmentation_facets, mv.is_mm_painted(), false }

return segmentation_by_painting(
    print_object,
    extract_facets_info,
    num_facets_states,
    max_width,
    interlocking_depth,
    interlocking_beam,
    IncludeTopAndBottomLayers::Yes,
    throw_on_cancel
)
```

### `fuzzy_skin_segmentation_by_painting(print_object, throw_on_cancel)` — line 2516

```
num_facets_states = 2   // 0=unpainted, 1=fuzzy-skin

extract_facets_info = lambda(mv) -> { mv.fuzzy_skin_facets, mv.is_fuzzy_skin_painted(), false }

// Limit segmentation depth to widest external perimeter across all regions
max_external_perimeter_width = 0.0
for each printing_region in print_object:
    w = region.flow(frExternalPerimeter, layer_height).width()
    max_external_perimeter_width = max(max_external_perimeter_width, w)

return segmentation_by_painting(
    print_object,
    extract_facets_info,
    num_facets_states,
    max_width = max_external_perimeter_width,
    interlocking_depth = 0,
    interlocking_beam = false,
    IncludeTopAndBottomLayers::No,
    throw_on_cancel
)
```

> **[HAZARD H570]** `flow()` uses `config.layer_height` (uniform), not per-layer actual height.
> For variable-layer-height prints this gives a slightly inaccurate max width but is a safe upper bound.

---

## Core Pipeline: `segmentation_by_painting(...)` — line 2213

```
INPUTS:
  print_object
  extract_facets_info  : ModelVolume -> ModelVolumeFacetsInfo
  num_facets_states    : size_t
  segmentation_max_width       : float (mm, 0 = unlimited)
  segmentation_interlocking_depth : float (mm, 0 = disabled)
  segmentation_interlocking_beam  : bool
  include_top_and_bottom_layers   : enum { Yes, No }
  throw_on_cancel

ALLOCATE:
  segmented_regions[num_layers][num_facets_states]  -- output
  painted_lines[num_layers]                          -- per-layer projected triangle lines
  painted_lines_mutex[64]                            -- hashed by (layer_idx & 0x3F)
  edge_grids[num_layers]                             -- EdgeGrid per layer
  input_expolygons[num_layers]                       -- preprocessed layer slices
  layer_bboxes[num_layers]

// ─── Phase 1: Slice Preprocessing (TBB parallel_for over layers) ───
// lines 2240–2269
for each layer_idx in parallel:
    throw_on_cancel()
    ex_polygons = union of all region slices, each expanded by 10*SCALED_EPSILON
    ex_polygons = union_ex(ex_polygons)
    remove_small_and_small_holes(ex_polygons, 0.1 mm²)
    input_expolygons[layer_idx] = simplify and deduplicate(
        offset_ex(ex_polygons, -10*SCALED_EPSILON), 5*SCALED_EPSILON
    )
    // Purpose: eliminate self-intersections and near-duplicate points that
    // would corrupt the Voronoi diagram in Phase 4.

// ─── Phase 2: EdgeGrid Construction (sequential) ───
// lines 2272–2293
for each layer_idx (sequential):
    layer_bboxes[layer_idx] = extents of layer regions ∪ input_expolygons[layer_idx]

for each layer_idx (sequential):
    bbox = layer_bboxes[layer_idx]
    if layer_idx > 1:  bbox.merge(layer_bboxes[layer_idx - 1])
    if layer_idx < last: bbox.merge(layer_bboxes[layer_idx + 1])
    // Adjacent-layer merge handles triangles that straddle layer boundaries (GH #7299)
    bbox.offset(20 * SCALED_EPSILON)
    edge_grids[layer_idx] = EdgeGrid(input_expolygons[layer_idx], cell_size=10mm)

// ─── Phase 3: Triangle Projection (nested TBB: outer=extruder_idx, inner=facet_idx) ───
// lines 2295–2395
for each ModelVolume mv in print_object.model_object().volumes:
    facets_info = extract_facets_info(mv)

    parallel_for extruder_idx in [1 .. num_facets_states):  // skip extruder 0 (default)
        custom_facets = facets_info.facets_annotation.get_facets(mv, EnforcerBlockerType(extruder_idx))
        if mv is not model_part or custom_facets is empty: continue

        tr = print_object.trafo() * mv.get_matrix()   // world transform (float)

        parallel_for facet_idx in [0 .. custom_facets.indices.size()):
            // Transform triangle vertices to world space
            facet[0..2] = tr * custom_facets.vertices[...]
            min_z = min(facet[*].z),  max_z = max(facet[*].z)
            if min_z ≈ max_z: continue  // horizontal triangle → no cross-section line

            // Sort vertices by Z for parametric cross-section computation
            sort facet vertices ascending by Z

            // Binary search for first/last layers that intersect this triangle
            first_layer = first layer with slice_z > (min_z - EPSILON)
            last_layer  = last  layer with slice_z < (max_z + EPSILON)

            for each layer_it in [first_layer .. last_layer]:
                layer    = *layer_it
                layer_idx = index of layer_it

                if input_expolygons[layer_idx] is empty: continue
                if slice_z < facet[0].z or slice_z > facet[2].z: continue

                // Compute cross-section line at this slice_z
                // Uses linear interpolation: t = (slice_z - P0.z) / (P2.z - P0.z)
                t            = (slice_z - facet[0].z) / (facet[2].z - facet[0].z)
                line_start   = facet[0] + t * (facet[2] - facet[0])

                if facet[0].z ≈ facet[1].z ≈ slice_z  OR  facet[1].z ≈ facet[2].z ≈ slice_z:
                    line_end = facet[1]          // degenerate horizontal edge
                elif facet[1].z > slice_z:
                    t1 = (slice_z - facet[0].z) / (facet[1].z - facet[0].z)
                    line_end = facet[0] + t1 * (facet[1] - facet[0])
                else:
                    t2 = (slice_z - facet[1].z) / (facet[2].z - facet[1].z)
                    line_end = facet[1] + t2 * (facet[2] - facet[1])

                // Scale and offset to print-object coordinate frame
                line_to_test = Line(scale(line_start.xy), scale(line_end.xy))
                line_to_test.translate(-print_object.center_offset())

                // Clip to EdgeGrid bbox (handles negative-volume clipping, GH #7618)
                if line_to_test not fully inside edge_grid_bbox:
                    if no overlap or cannot clip: continue

                // Project onto nearby contour edges via EdgeGrid visitor
                mutex_idx = layer_idx & 0x3F
                visitor = PaintedLineVisitor(edge_grids[layer_idx], painted_lines[layer_idx],
                                             painted_lines_mutex[mutex_idx])
                visitor.line_to_test = line_to_test
                visitor.color = extruder_idx
                edge_grids[layer_idx].visit_cells_intersecting_line(
                    line_to_test.a, line_to_test.b, visitor
                )
                // PaintedLineVisitor writes projected PaintedLine records into painted_lines[layer_idx]
                // under the per-layer mutex (see PaintedLineVisitor below)

// ─── Phase 4: Layer Segmentation (TBB parallel_for over layers) ───
// lines 2401–2448
for each layer_idx in parallel:
    throw_on_cancel()
    if painted_lines[layer_idx] is empty: continue

    // 4a. Sort and merge painted line segments per contour edge
    post_processed = post_process_painted_lines(
        edge_grids[layer_idx].contours(),
        move(painted_lines[layer_idx])
    )

    // 4b. Assign color to every sub-segment of every contour edge
    color_poly = colorize_contours(edge_grids[layer_idx].contours(), post_processed)
    // color_poly[contour_idx] = vector<ColoredLine> (each line tagged with extruder color or 0)

    if has_layer_only_one_color(color_poly):
        // Fast path: entire layer is one color, skip Voronoi
        single_color = color_poly.front().front().color
        segmented_regions[layer_idx][single_color] = input_expolygons[layer_idx]
    else:
        // Full Voronoi segmentation path
        graph = build_graph(layer_idx, color_poly)             // see below
        remove_multiple_edges_in_vertices(graph, color_poly)   // prune concave VD branches
        graph.remove_nodes_with_one_arc()                       // prune dangling VD branches
        segmented_regions[layer_idx] = extract_colored_segments(graph, num_facets_states)
        // see below

// ─── Phase 5: Width Limiting (optional, sequential dispatch, TBB inner) ───
// lines 2452–2456
if segmentation_max_width > 0 or interlocking_depth > 0 (and not interlocking_beam mode):
    cut_segmented_layers(
        input_expolygons,
        segmented_regions,
        scale(segmentation_max_width),
        scale(interlocking_depth),
        throw_on_cancel
    )

// ─── Phase 6: Top/Bottom Layer Propagation (optional) ───
// lines 2459–2464
if include_top_and_bottom_layers == Yes:
    top_and_bottom_layers = segmentation_top_and_bottom_layers(
        print_object, input_expolygons, extract_facets_info, num_facets_states, throw_on_cancel
    )

// ─── Phase 7: Merge Side + Top/Bottom Results ───
// lines 2466–2468
segmented_regions_merged = merge_segmented_layers(
    segmented_regions,
    move(top_and_bottom_layers),   // empty if include_top_and_bottom_layers == No
    num_facets_states,
    throw_on_cancel
)

return segmented_regions_merged
// Indexed [extruder_idx][layer_idx] → ExPolygons
```

---

## Sub-Algorithm: `PaintedLineVisitor` — line 617

EdgeGrid visitor called from Phase 3 for each grid cell crossed by a painted triangle cross-section.

```
FOR each contour edge (grid_line) in the intersected grid cell:
    heuristic_thr_sqr = (line_to_test.length + append_threshold + grid_line.length)²
    // Cheap early-out: skip if ALL four endpoint pairs are farther than threshold
    if (grid_line.a - line_to_test.a)² > heuristic_thr_sqr  AND
       (grid_line.b - line_to_test.a)² > heuristic_thr_sqr  AND
       (grid_line.a - line_to_test.b)² > heuristic_thr_sqr  AND
       (grid_line.b - line_to_test.b)² > heuristic_thr_sqr:
        continue   // too far apart

    // Collinearity test (within 30°)
    if (v1 · v2)² > cos²(30°) * |v1|² * |v2|²:
        // Lines are nearly parallel — check proximity
        if grid_line not already in painted_lines_set:
            if any endpoint within append_threshold (50 * SCALED_EPSILON):
                // Project line_to_test onto grid_line
                line_to_test_projected = project_line_on_line(grid_line, line_to_test)
                ensure projected line is oriented same as grid_line

                painted_lines_set.insert(this contour edge)
                lock(painted_lines_mutex)
                painted_lines.push_back({
                    contour_idx, segment_idx, line_to_test_projected, color
                })
                unlock(painted_lines_mutex)

return true  // continue grid traversal
```

> **[HAZARD H563]** The heuristic pre-filter uses AND logic — a near-parallel line is only skipped
> if ALL four endpoint pairs exceed the threshold. This is conservative; it may pass lines to the
> collinearity check that are ultimately rejected there. No false rejections observed, but the
> comment in the code acknowledges this is a heuristic, not an exact filter.

---

## Sub-Algorithm: `post_process_painted_lines(contours, painted_lines)` — line 793

```
// Sort all PaintedLine records: by contour_idx, then line_idx,
// then by distance of projected_line.a from contour start point
sort painted_lines by (contour_idx, line_idx, projected_line.a distance from segment start)

// Group by contour + segment, then filter each group
filtered_painted_lines[num_contours]
for each run of records sharing the same (contour_idx, line_idx):
    filtered = filter_painted_lines(contour_segment, this_run)
    append filtered to filtered_painted_lines[contour_idx]

return filtered_painted_lines
```

`filter_painted_lines` merges same-color adjacent projected segments (within `0.1mm` gap) and
trims the first/last projected segment to the exact contour segment endpoints.

---

## Sub-Algorithm: `colorize_contours(contours, painted_contours)` — line 1064

Turns raw `PaintedLine` annotations into a fully-covered sequence of `ColoredLine` objects for
every contour, assigning default color `0` to unpainted gaps.

```
for each contour_idx:
    colorized_contours[contour_idx] = colorize_contour(contours[contour_idx], painted_contours[contour_idx])
    // Unpainted segments before first PaintedLine → color 0
    // For each painted segment group on the same contour edge → colorize_line()
    // Gaps between painted groups → color 0
    // Unpainted segments after last PaintedLine → color 0
    // Post-filter: absorb isolated short (<0.2mm) color islands (filter_colorized_polygon)

// Assign poly_idx and local_line_idx to each ColoredLine for later Voronoi index mapping
assign poly_idx, local_line_idx across all colorized_contours

return colorized_contours  // [contour_idx] → vector<ColoredLine>
```

---

## Sub-Algorithm: `build_graph(layer_idx, color_poly)` — line 1714

Constructs the `MMU_Graph` for one layer from its colorized polygon contours.

```
INPUT: color_poly[contour_idx][line_idx] = ColoredLine

color_poly_tmp = polygons from color_poly (just points, no color)
lines_colored  = flat vector of all ColoredLines (Boost segment concept)

// Identify monochrome polygons (need forced edge for hole-with-different-color case)
for each c_poly in color_poly:
    force_edge_adding[poly_idx] = all lines in c_poly share same color

// Step 1: Build Voronoi diagram
vd = construct_voronoi(lines_colored)   // Boost.Polygon VD from line segments

// Step 2: Pre-populate graph nodes from all contour points
for each point in color_poly_tmp:
    graph.nodes.push_back(Vec2d(point))

// Step 3: Add BORDER arcs (one directed arc per contour edge, same winding as polygon)
graph.add_contours(color_poly)
// Now graph.nodes[0 .. all_border_points-1] = contour vertices
// graph.arcs[0 .. all_border_points-1]      = one BORDER arc per contour edge

// Step 4: Post-process VD vertices
// Merge near-duplicates (within SCALED_EPSILON), discard out-of-bbox vertices,
// store resulting node index in vertex.color() [HAZARD H561]
graph.append_voronoi_vertices(vd, color_poly_tmp, bbox)
// After this, vertex.color() encodes graph node index (not VD_ANNOTATION enum value)

// Step 5: Build VD segments copy (double precision) for clipping
segments = double-precision copy of color_poly_tmp lines

// Step 6: Iterate VD edges, add NON_BORDER arcs
bbox.offset(scale(10mm))
for each edge_it in vd.edges():
    skip if second half-edge (source_index > twin source_index) or already processed

    if edge is infinite (has at least one null vertex):
        // Clip the infinite ray against the enlarged bbox
        samples = clip_infinite_edge(points, segments, edge, bbox_dim_max)
        if samples empty: continue
        edge_line = Line(samples[0], samples[1])
        if edge_line intersects contour_line:
            from_idx = vertex.color() of the non-null endpoint
            to_idx   = nearest contour node (start or end of contour line)
            if from_idx != to_idx and both valid:
                graph.append_edge(from_idx, to_idx)   // NON_BORDER, no color (-1)
                mark_processed(edge_it)

    elif edge is finite:
        if both vertices are on contour, or vertex colors are equal: skip
        edge_line = clip_finite_voronoi_edge(edge, bbox_clip)
        contour_line = lines_colored[edge.cell.source_index].line

        // Determine color for this Voronoi edge:
        // Use point_inside() to classify which side the edge point lies on
        from_idx = vertex0.color()
        to_idx   = vertex1.color()
        if from_idx valid AND to_idx valid AND from_idx != to_idx:
            color = determine_edge_color(edge_it, color_poly, lines_colored, graph)
            graph.append_edge(from_idx, to_idx, color)   // NON_BORDER
            mark_processed(edge_it)

// Step 7: Forced edge for monochrome polygons
for each poly_idx where force_edge_adding[poly_idx] == true:
    // Add one Voronoi edge that starts inside this polygon, enabling extract_colored_segments
    // to assign the polygon to its single color
    add forced edge via first available VD edge for this polygon's cells

return graph
```

> **[HAZARD H561]** `vertex.color()` is dual-use: during `append_voronoi_vertices()` it is first
> initialized to `-1` (clearing any `VD_ANNOTATION` sentinel), then overwritten with a node index
> (0 = contour node, ≥ all_border_points = Voronoi interior node). Code that reads `vertex.color()`
> before `append_voronoi_vertices()` completes will see stale annotation values.
>
> **[HAZARD H566]** `append_edge()` deduplication is O(degree) per call — quadratic for
> high-valence Voronoi vertices. Degree is empirically small (<10) but is not asserted.
>
> **[HAZARD H567]** `force_edge_adding[]` is indexed by pointer arithmetic into `color_poly`.
> Empty polygon entries in `color_poly` would produce duplicate global indices.

---

## Sub-Algorithm: `remove_multiple_edges_in_vertices(graph, color_poly)` — line 1253 (called line 2436)

When a Voronoi vertex is connected to more than one VD edge, keep only the longest-total-length
chain going toward a contour vertex; delete the rest.

```
for each VD vertex:
    if vertex has ≤ 1 non-DELETED edge: continue

    // Compute chain lengths: for each edge, follow nearly-straight continuations
    // (within 15° of straight, degree ≤ 2 at intermediate nodes) and sum their lengths
    edges_to_check = [(edge, calc_total_edge_length(edge)) for each non-DELETED edge at vertex]
    sort edges_to_check descending by total_length

    // Keep the longest; delete all others (cascading into dangling sub-trees)
    while edges_to_check.size() > 1:
        edge_to_delete = edges_to_check.back()
        mark edge_to_delete and twin as DELETED
        if far end can_vertex_be_deleted(): delete_vertex_deep(far end)
        pop edges_to_check.back()
```

---

## Sub-Algorithm: `MMU_Graph::remove_nodes_with_one_arc()` — line 173

BFS pruning of Voronoi interior nodes with exactly one arc (dangling branches).

```
// Seed queue with all Voronoi interior nodes (index >= all_border_points) having exactly 1 arc
update_queue = {node_idx for node_idx >= all_border_points where arc_idxs.size() == 1}

while queue not empty:
    node_from_idx = queue.front(); pop
    if node_from.arc_idxs is empty: continue   // already pruned
    assert node_from has exactly 1 arc
    node_to_idx = arcs[node_from.arc_idxs.front()].to_idx
    remove_edge(node_from_idx, node_to_idx)     // removes arc from both adjacency lists
    if node_to is Voronoi interior AND now has exactly 1 arc:
        queue.push(node_to_idx)                 // propagate pruning
```

---

## Sub-Algorithm: `extract_colored_segments(graph, num_facets_states)` — line 494

Extracts per-extruder ExPolygon regions by walking the graph using a leftmost-arc rule.

```
used_arcs[graph.arcs.size()] = all false

expolygons_segments[num_facets_states] = empty

// Walk starts only from contour border nodes
for node_idx in [0 .. graph.all_border_points):
    for each arc_idx in node.arc_idxs:
        arc = graph.arcs[arc_idx]
        if arc.type == NON_BORDER: continue      // start only from BORDER arcs
        if used_arcs[arc_idx]: continue

        // Begin a new polygon walk
        used_arcs[arc_idx] = true
        arc_id_to_face_lines = [(arc_idx, Line(from_node.point, to_node.point))]
        start_p = arc.from_node.point
        p_vec = initial line
        p_arc = &arc
        flag = false

        // Walk loop: follow leftmost unused arc at each junction
        LOOP:
            nexts = get_next_arc(graph, used_arcs, p_vec, p_arc, arc.color)
            // get_next_arc: among eligible arcs at p_arc.to_node (same color, not revisiting start
            // unless all arcs there are used), pick the one with smallest left-turn angle
            // relative to current direction.

            for next in nexts:
                if used_arcs[next_arc_idx]:
                    flag = true; break
            if flag: break

            for next in nexts:
                arc_id_to_face_lines.append((next_arc_idx, Line(next.from, next.to)))
                used_arcs[next_arc_idx] = true

            p_vec = last next line
            p_arc = last next arc

        WHILE p_arc.to_node.point != start_p  OR  NOT all_arc_used(p_arc.to_node)

        // Validate and emit polygon
        poly = to_polygon(arc_id_to_face_lines)
        if poly.is_counter_clockwise() AND poly.is_valid():
            expolygons_segments[arc.color].push_back(poly)
        else:
            // Repair path: backtrack one arc at a time, add closing chord, retry
            while arc_id_to_face_lines.size() > 1:
                last = arc_id_to_face_lines.pop_back()
                used_arcs[last.arc_idx] = false              // un-consume last arc
                // Add synthetic closing chord (arc index = SIZE_MAX sentinel)
                add_chord = Line(arc_id_to_face_lines.back.b, arc_id_to_face_lines.front.a)
                arc_id_to_face_lines.push_back((-1, add_chord))  // [HAZARD H562]
                poly = to_polygon(arc_id_to_face_lines)
                if NOT self_intersecting AND CCW AND valid:
                    expolygons_segments[arc.color].push_back(poly)
                    break
                arc_id_to_face_lines.pop_back()   // remove chord, try again

return expolygons_segments
```

> **[HAZARD H562]** The repair path stores arc index `(size_t)(-1)` = `SIZE_MAX` as a sentinel
> for the synthetic closing chord in `arc_id_to_face_lines`. Currently safe because no code
> dereferences `used_arcs[SIZE_MAX]` in the repair loop. Any refactor that iterates
> `arc_id_to_face_lines` and dereferences the first field will cause an out-of-bounds access.

---

## Sub-Algorithm: `cut_segmented_layers(...)` — line 1281

Limits color region width by eroding inward; alternates depth between even/odd layers for
mechanical interlocking of the boundary zone.

```
interlocking_cut_width = (interlocking_depth > 0) ? max(cut_width - interlocking_depth, 0) : 0

parallel_for layer_idx in [0 .. num_layers):
    throw_on_cancel()
    // Alternating depth: even layers use interlocking_depth, odd layers use full cut_width
    region_cut_width = ((layer_idx % 2 == 0) AND interlocking_depth != 0) ?
                           interlocking_depth : cut_width

    if region_cut_width > 0:
        for each extruder_idx:
            segmented_regions[layer_idx][extruder_idx] =
                diff_ex(
                    segmented_regions[layer_idx][extruder_idx],
                    offset_ex(input_expolygons[layer_idx], -region_cut_width)
                )
        // Result: only regions within `region_cut_width` of the layer boundary survive
```

---

## Sub-Algorithm: `segmentation_top_and_bottom_layers(...)` — line 1331

Propagates paint colour upward/downward through top/bottom shell layers using `slice_mesh_slabs()`.

```
// Compute max shell depths across all print regions
max_top_layers = max(config.top_shell_layers)
max_bottom_layers = max(config.bottom_shell_layers)
granularity = max(max_top_layers, max_bottom_layers) - 1

// For each model volume and each extruder color, extract top and bottom projection polygons
top_raw[num_facets_states][num_layers]
bottom_raw[num_facets_states][num_layers]

for each ModelVolume mv:
    for each extruder_idx in [0 .. num_facets_states):
        painted = facets_annotation.get_facets_strict(mv, EnforcerBlockerType(extruder_idx))
        if painted is empty: continue

        if volume is sinking below z=0:
            // Add z=0 layer to zs before slicing, then remove it from results
            [top, bottom] = slice_mesh_slabs(painted, [0]+zs, volume_trafo, ...)
            bottom[0] = union(bottom[0], slice_mesh(painted, zs[0]))
            top.erase(begin);  bottom.erase(begin)
        else:
            [top, bottom] = slice_mesh_slabs(painted, zs, volume_trafo, ...)

        merge top into top_raw[extruder_idx]
        merge bottom into bottom_raw[extruder_idx]

// Filter sub-0.01mm² polygons (unprintable, cause dimples on primers, GH #7104)
filter_out_small_polygons(top_raw, 0.1mm²)
filter_out_small_polygons(bottom_raw, 0.1mm²)

// Remove top projections that are occluded by the layer above,
// remove bottom projections that are occluded by the layer below
for each extruder_idx, layer_idx:
    top_raw[extruder_idx][layer_idx] = diff(top_raw[...], input_expolygons[layer_idx + 1])
    bottom_raw[extruder_idx][layer_idx] = diff(bottom_raw[...], input_expolygons[layer_idx - 1])

// TWO-ARRAY interleave trick to avoid TBB write conflicts [HAZARD H564]:
// triangles_by_color_top/bottom has size num_layers * 2
// group_idx iterates over bands of `granularity` layers
// layer_idx_offset = (group_idx & 1) * num_layers — alternates 0 and num_layers
// Within each group, TBB writes to offset [layer_idx_offset + layer_idx]
// Merge step reads both halves

triangles_by_color_bottom[num_facets_states][num_layers * 2]
triangles_by_color_top[num_facets_states][num_layers * 2]

parallel_for group_idx in [0 .. ceil(num_layers / granularity)):
    layer_idx_offset = (group_idx & 1) * num_layers
    for each layer_idx in this group:
        for each extruder_idx:
            layer_color_stat = compute_extrusion_widths_and_shell_counts(layer_idx, extruder_idx)
            // [HAZARD H565] layer_color_stat always uses nozzle_diameter.get_at(0) — hardcoded
            // extruder 0, incorrect for multi-extruder setups with different nozzle diameters.

            // Propagate top color downward through top_shell_layers
            for shell_layer in [1 .. layer_color_stat.top_shell_layers]:
                src_layer = layer_idx - shell_layer
                if src_layer valid and top_raw[extruder_idx][src_layer] not empty:
                    top_region = intersection_ex(top_raw[extruder_idx][src_layer],
                                                 input_expolygons[layer_idx])
                    // morphological open to remove unprintable thin regions
                    top_region = opening_ex(top_region, small_region_threshold)
                    triangles_by_color_top[extruder_idx][layer_idx_offset + layer_idx]
                        = union_ex(triangles_by_color_top[...], top_region)

            // Propagate bottom color upward through bottom_shell_layers (symmetric)

// Merge both halves of interleaved arrays
result[num_facets_states][num_layers]
for each extruder_idx, layer_idx:
    result[extruder_idx][layer_idx] = union_ex(
        triangles_by_color_top[extruder_idx][layer_idx],
        triangles_by_color_top[extruder_idx][layer_idx + num_layers],
        triangles_by_color_bottom[extruder_idx][layer_idx],
        triangles_by_color_bottom[extruder_idx][layer_idx + num_layers]
    )

return result   // indexed [extruder_idx][layer_idx] → ExPolygons
```

> **[HAZARD H564]** The two-array interleave pattern is a non-obvious TBB synchronization trick.
> If TBB changes its task-splitting strategy such that two groups with the same `(group_idx & 1)`
> bit write to the same half at the same time, results will be silently incomplete.
>
> **[HAZARD H565]** `layer_color_stat()` lambda uses `nozzle_diameter.get_at(0)` unconditionally.
> For a print with extruder 1 using a 0.6mm nozzle and extruder 2 using a 0.4mm nozzle, extruder 2's
> region widths are computed using the 0.6mm nozzle. This causes slightly over-wide color propagation
> for the narrower extruder.

---

## Key Data Structures

### `MMU_Graph` — line 72

```
struct MMU_Graph {
    nodes[]           // Vec2d positions; [0..all_border_points-1] = contour vertices,
                      // [all_border_points..] = Voronoi interior vertices
    arcs[]            // directed arcs: {from_idx, to_idx, color, type}
                      // BORDER: one arc per contour edge (same direction as polygon winding)
                      // NON_BORDER: two arcs per VD edge (both directions)
    all_border_points // split point between contour nodes and VD interior nodes

    polygon_idx_offset[]  // global node index of polygon[i][0]
    polygon_sizes[]       // number of points in polygon[i]
}
```

### `PaintedLine` — line 597

```
struct PaintedLine {
    contour_idx    : size_t   // which contour in the EdgeGrid
    line_idx       : size_t   // which segment on that contour
    projected_line : Line     // sub-segment of the contour edge that was painted
    color          : int      // extruder index (1..N)
}
```

### `ColoredLine` — (defined elsewhere, used throughout)

```
struct ColoredLine {
    line          : Line
    color         : int      // 0 = default/unpainted, 1..N = extruder index
    poly_idx      : int      // contour index (assigned by colorize_contours)
    local_line_idx: int      // segment index within contour
}
```

---

## Translation Notes

### TN-1: 64-Mutex Hash Scheme
`painted_lines_mutex[layer_idx & 0x3F]` means layers 0, 64, 128, … share a mutex bucket. With
typical print heights of 200–500 layers, this gives ~3–8 layers per bucket. Collision probability
is low, but not zero. The scheme is a performance trade-off between granularity (64 mutexes) and
overhead (one `std::mutex` per layer would need heap allocation or a large array).

### TN-2: Voronoi Diagram Coordinate Precision
`construct_voronoi()` is called with `ColoredLine` objects in `coord_t` (32-bit scaled integers).
The VD is traversed in `double`. All intermediate results use `mk_point()` / `mk_vec2()` helpers
that convert between the two representations. Precision loss is bounded by `SCALED_EPSILON` and
the `ClosestPointInRadiusLookup` merging in `append_voronoi_vertices`.

### TN-3: `vertex.color()` Phase Dependency
Boost Voronoi's `vertex.color()` field is a 64-bit integer reused for multiple purposes:
- **Before `append_voronoi_vertices()`**: holds `VD_ANNOTATION` sentinel values
  (`VERTEX_ON_CONTOUR=1`, `DELETED=2`, or `-1` unset).
- **After `append_voronoi_vertices()`**: holds graph node index.
- **During `remove_multiple_edges_in_vertices()`**: edges and vertices are marked `DELETED` (=2).
Reading `vertex.color()` without knowing the current phase gives wrong results. The code is
internally consistent but any new code touching the VD must be phase-aware.

### TN-4: Nested TBB parallel_for in Phase 3
Phase 3 runs two nested TBB `parallel_for` loops (outer: extruder, inner: facet). TBB may flatten
this into a task tree, which is generally safe. However, the outer loop captures `extruder_idx` by
reference from its range begin/end, while the inner lambda captures it from the outer closure.
Both lambdas capture `painted_lines_mutex` by reference — this is safe because `painted_lines_mutex`
is a stack-allocated `std::array<std::mutex, 64>` that outlives all tasks.

### TN-5: Force Edge for Monochrome Polygons
A polygon whose every edge has the same color yields no color-boundary lines, so the Voronoi
diagram produces no interior arcs that cross a color boundary. Without a forced edge, the polygon
would be assigned to whichever color happens to be returned by the greedy walk's first arc — not
the actual painted color. `force_edge_adding[]` ensures at least one arc enters the polygon so
`extract_colored_segments` assigns the correct color.

### TN-6: Repair Path in `extract_colored_segments`
The CCW + valid polygon check can fail for CW "hole" polygons (which represent holes in the
region, not the region itself) and for degenerate walks that revisit a node. The repair path
backracks one arc at a time and closes the polygon with a synthetic chord. This is a heuristic:
it does not guarantee a valid result for all degenerate inputs — if the loop exhausts all arcs
without finding a valid CCW polygon, no polygon is emitted for that arc.

### TN-7: `get_next_arc` — Leftmost Turn Rule
At each node during the walk, `get_next_arc` collects all eligible arcs (same color, not revisiting
start unless all arcs consumed, not already used), computes the signed angle each arc makes with
the reversed current direction, sorts by angle ascending, and returns the arc with the smallest
angle (i.e., the leftmost / most counter-clockwise turn). This implements a planar-graph
boundary-tracing algorithm equivalent to following the left boundary of a face.

### TN-8: Extruder Index 0 Convention
Index 0 always means "unpainted" / "use default extruder assignment". The painting gizmo starts
from index 1. `segmented_regions[layer][0]` accumulates all uncoloured geometry. This convention
is assumed throughout: `colorize_line()` inserts gaps as color 0, `build_graph()` skips extruder 0
in forced-edge logic, and `extract_colored_segments()` assigns CCW polygons to `arc.color` which
can be 0 for default-colored regions.
