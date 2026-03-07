# 03 — Algorithmic Complexities

## 1. Core Slicing Algorithm

**Files:** [`TriangleMeshSlicer.cpp`](../src/libslic3r/TriangleMeshSlicer.cpp), [`TriangleMeshSlicer.hpp`](../src/libslic3r/TriangleMeshSlicer.hpp)

### Overview

The slicing algorithm intersects a triangle mesh with a set of horizontal (constant Z) planes to produce 2D polygons per layer. It is a **per-triangle plane-sweep** algorithm, parallelized across triangles using Intel TBB.

### Phase 1: Per-Triangle Intersection (`slice_facet`, line ~156)

For each triangle and each Z plane that intersects it:

1. **Bounding Z interval:** Compute `min_z` / `max_z` of the triangle's three vertices
2. **Binary search:** Use `std::lower_bound` / `std::upper_bound` to find the range of Z planes that can intersect this triangle — O(log N) per triangle where N = number of layers
3. **Intersection computation:** For each Z plane in the range, call `slice_facet()`:
   - Identify which edges cross the plane (edge `a→b` crosses if one vertex is above and one below)
   - Linear interpolation: `t = (slice_z - b.z) / (a.z - b.z)` gives the parameter along the edge
   - Result: an `IntersectionLine` — a 2D line segment tagged with source vertex/edge IDs
4. **Degenerate cases:** Special handling for horizontal faces, vertices exactly on the plane, and edges shared between two triangles (FacetEdgeType: General, Top, Bottom, TopBottom, Horizontal, Slab)

**Concurrency:** The outer loop `tbb::parallel_for` processes triangles in parallel. `IntersectionLine` outputs are written to per-layer `lines[]` arrays protected by per-layer mutexes (one mutex per 2 layers to reduce contention).

**Coordinate handling:** The mesh Z coordinates remain in floating-point mm; only X/Y are cast to `coord_t` (scaled integer) for Clipper compatibility.

### Phase 2: Line Chaining into Polygons

After all intersection lines are collected per layer, they are chained into closed polygons:

1. For each layer, build a hash map `{endpoint_id → line_index}` to find adjacent lines in O(1)
2. **Greedy chain assembly:** Start from a "seed" line (not flagged `NO_SEED`), follow edges by matching `b_id` of one line to `a_id` of the next, building a polygon
3. Handle T-junctions and duplicates using the `SKIP` flag on already-consumed lines
4. Degenerate segments (zero-length after rounding) are discarded

**Complexity:** O(E) per layer where E = number of intersection line segments per layer.

### Phase 3: Polygon Union and Cleanup (in `slice_mesh_ex`)

After per-layer `Polygons` are produced by `slice_mesh()`, `slice_mesh_ex()` applies:
1. **ClipperLib union** (`pftNonZero` or `pftEvenOdd` depending on `SlicingMode`) to merge touching/overlapping contours
2. **Morphological closing** (expand+shrink) if `closing_radius > 0` — fills hairline gaps
3. **Douglas-Peucker simplification** if `resolution > 0` — reduces polygon point count
4. **Positive offset** if `extra_offset > 0`

**Result:** `std::vector<ExPolygons>` — one `ExPolygons` per Z layer.

### Complexity Summary

| Operation | Complexity |
|---|---|
| Z-range lookup per triangle | O(log L) — binary search on L layer heights |
| Intersection computation | O(T × avg_layers_per_triangle) — dominated by triangle count T |
| TBB parallelism | Work divided across CPU cores |
| Line chaining per layer | O(E) where E = edges on that layer |
| Clipper union per layer | O(E log E) — Clipper uses scan-line algorithm |
| **Total** | **O(T log L + E log E) per layer** |

---

## 2. Perimeter Generation

**Files:** [`PerimeterGenerator.cpp`](../src/libslic3r/PerimeterGenerator.cpp), [`Arachne/WallToolPaths.cpp`](../src/libslic3r/Arachne/)

### Classic Perimeter Generator

1. **Input:** `ExPolygons` from layer slices (the `fill_surfaces` after classification)
2. **Inward offsetting:** Apply successive negative offsets of `extrusion_width/2` using Clipper to generate inner shells:
   ```
   outer_contour → offset(-spacing) → perimeter_1 → offset(-spacing) → perimeter_2 → ...
   ```
3. **Thin wall detection:** Where the offset would collapse a thin region, the medial axis is computed instead
4. **Loop extraction:** Each offset ring becomes an `ExtrusionLoop` with role `erPerimeter` or `erExternalPerimeter`
5. **Ordering:** Loops ordered outer-to-inner for standard mode, inner-outer-inner for "sandwich" mode
6. **Seam placement:** `SeamPlacer` selects the loop start point to minimize visible seam (using history of previous layer seams, overhang detection, and user preferences)

### Arachne Variable-Width Generator

- Computes a **straight skeleton** (medial axis) of the polygon
- Distributes wall widths continuously, allowing walls to taper rather than abruptly stop
- Produces `ExtrusionPath` segments with varying `width` values
- Used when `perimeter_generator = arachne` in config

---

## 3. Infill Algorithms

**Files:** `src/libslic3r/Fill/` directory

### Factory Pattern

`Fill::Create(InfillPattern)` in [`Fill.cpp`](../src/libslic3r/Fill/Fill.cpp) returns a `FillBase*` subclass based on config enum.

### Common Infill Patterns

#### Rectilinear (`FillRectilinear`)
- Generates parallel lines at a given angle
- Algorithm: scan-line intersection of the infill polygon boundary with parallel lines at `line_spacing` intervals
- Alternate layers rotate by 90° (or configured `fill_angle`)
- Complexity: O(N log N) for sorting intersection points

#### Grid / Cross Hatch (`FillCrossHatch`)
- Two passes of rectilinear at 90° relative to each other
- Lines are interleaved and connected

#### Gyroid (`FillGyroid`)
- Approximates the triply periodic minimal surface: `sin(x)cos(y) + sin(y)cos(z) + sin(z)cos(x) = 0`
- Implementation generates the 2D cross-section of the gyroid by computing intersections at the current Z height
- The pattern is mathematically continuous across layers — no layer boundaries

#### 3D Honeycomb (`Fill3DHoneycomb`)
- Projects 3D honeycomb cell boundaries onto layer planes
- Direction rotates with layer index to create the 3D interlocking structure

#### Adaptive Cubic (`FillAdaptive`)
- Uses an **octree** data structure (`FillAdaptive::Octree`) precomputed from the model geometry
- Octree nodes correspond to cubic cells; cells closer to the surface are denser
- The octree is computed once per `PrintObject` and shared across layers

#### Lightning Infill (`FillLightning`)
- Generates a tree-like minimal spanning structure that reaches all interior points above a threshold distance from a surface
- Algorithm: iteratively extend "branches" toward uncovered interior points
- Minimizes material while ensuring no interior point is unsupported for top surface

#### TPMS Infill (`FillTpmsD`, `FillTpmsFK`)
- Triply Periodic Minimal Surfaces: D-surface (diamond) and FK (Fourier-Kitaev) approximations
- Similar to Gyroid but different mathematical surfaces

### Complexity Notes
- Most patterns: O(area / line_spacing²) — scales with infill density
- Adaptive cubic: precomputation is O(V log V) where V = mesh vertex count; lookup per layer is O(k) where k = cells intersecting that layer
- Lightning: O(n log n) where n = number of "unlit" points requiring coverage

---

## 4. Bridge and Overhang Detection

**File:** [`BridgeDetector.cpp`](../src/libslic3r/BridgeDetector.cpp)

### Algorithm

1. **Overhang detection:** Compare current layer `ExPolygons` to the union of the layer below, expanded by a configurable threshold:
   ```
   overhang = current_layer_polygons - (lower_layer_polygons expanded by threshold)
   ```
2. **Bridge span detection:** Identify overhang regions that are supported on two sides (can be bridged)
3. **Bridge angle optimization:** For each bridge region, try multiple angles and select the one that minimizes unsupported span length. Uses Clipper offset to simulate extrusion coverage at each angle.
4. **Output:** `bridge_angle` stored in `Surface` objects; bridging extrusions use this angle for `erBridgeInfill` paths

### Curled Extrusion Estimation (`GCode/ExtrusionProcessor.hpp`)

A newer BBS addition: estimates which extrusions will curl upward due to cooling, marking them in `Layer::curled_lines` to allow the travel planner to avoid fast moves over them.

---

## 5. Support Structure Generation

### Traditional Support (`Support/SupportMaterial.cpp`)

1. **Overhang detection:** Find regions overhanging by more than the configured angle (typically >45°-55°)
2. **Support islands:** Grow downward from overhangs layer-by-layer, clipping against the model
3. **Interface layers:** Generate denser interface layers (top N layers of support) for better detachability
4. **Pattern generation:** Support infill uses rectilinear or grid pattern at configurable density
5. **Raft generation:** Optional raft layers under the model for adhesion

**COUPLING:** `SupportMaterial` directly accesses `PrintObject` internal state (`m_layers`), making it tightly coupled to the print pipeline.

### Traditional Support Pipeline Detail (Session 5)

**File:** [`src/libslic3r/Support/SupportMaterial.cpp`](../src/libslic3r/Support/SupportMaterial.cpp)

#### `generate()` — 11-Step Serial Orchestration

```
detect_overhangs()
  → detect_contacts()
  → new_contact_layer()
  → bottom_contact_layers_and_layer_support_areas()
  → generate_base_layers()
  → generate_interface_layers()
  → generate_base_interface_layers()
  → trim_support_layers_by_object()
  → generate_support_toolpaths()
  → generate_raft_base()
  → buildplate_covered()
```

The top-level orchestration is **fully serial**. Parallelism occurs **inside** individual steps.

#### `detect_overhangs()` — BBS Sharp-Tail Detection

**Pass 1 (TBB parallel_for, per-layer):**
- Layer 0: marks small-footprint polygons (area < 0.5 mm²) as sharp-tail seeds.
- Layer N: marks "floating" expolygons (centroid over empty space below, small area) as sharp-tail seeds.

**Pass 2 (serial, upward from layer 1):**
- Propagates sharp-tail membership upward layer-by-layer.
- Propagation conditions: bbox < 2.5 mm in both dimensions, area growth < 50%/layer, accumulated height < 16 mm.
- Sharp-tail regions force bottom-contact support regardless of overhang angle.

#### `SupportGridPattern` — Two Active Modes

| Mode | Algorithm | Use Case |
|------|-----------|----------|
| `smsGrid` | AGG scanline rasterize → marching-squares vectorize | Standard grid support pattern |
| `smsSnug` | Clipper morphological close (expand + shrink) | Tight-fitting organic support outline |

Note: `smsTreeSlim`, `smsTreeStrong`, `smsTreeHybrid`, `smsOrganic` all `assert(false)` — routing stubs only; tree support handled by `TreeSupport.cpp`.

#### AGG Rasterization Path

1. Polygon boundary rendered into `uint8_t` byte grid at `SUPPORT_GRID_OVERSAMPLING × 4` resolution using AGG anti-aliased scanline fill.
2. Cells with value > 128 are marked inside.
3. `seed_fill_block()` flood-fills from seeds within macro-blocks, bounded by dilated trimming mask.
4. `contours_simplified()` runs marching-squares over the byte grid to produce polygon outlines.

#### `bottom_contact_layers_and_layer_support_areas()` — Parallel Pairs

Iterates **top-to-bottom** with concurrent `tbb::task_group` pairs per layer:
```
detect_bottom_contacts(layer)   ← concurrent with →   project_support_to_grid(layer)
```
Result vector built descending, then `std::reverse()`-d.

#### Complexity Summary

| Step | Complexity | Notes |
|------|------------|-------|
| `detect_overhangs()` pass 1 | O(L × P) parallel | L = layers, P = polygons/layer |
| `detect_overhangs()` pass 2 | O(L) serial | Upward propagation walk |
| `OverhangCluster` membership | O(N²) worst case | N = total overhang polygons |
| `rasterize_polygons()` | O(W × H) | W×H = grid resolution |
| `contours_simplified()` | O(W × H) | Marching-squares |
| `generate_base_layers()` | O(L × P) parallel | TBB parallel_for |
| `trim_support_layers_by_object()` | O(L × P × log P) | Clipper per-layer |
| `buildplate_covered()` | O(L × P) serial | Known FIXME: should be parallel prefix |

---

### Tree Support (`Support/TreeSupport.cpp`, `TreeSupport3D.cpp`)

A Bambu Lab addition implementing tree-like organic support structures:

1. **"Influence areas":** For each overhang point, compute a conical influence area reaching down to the build plate
2. **Branch merging:** Influence areas are merged when they intersect — branches combine toward a common trunk
3. **Collision avoidance:** Branches are clipped against the model mesh using AABB lookups
4. **`TreeModelVolumes`:** Precomputes per-layer "collision volumes" and "avoidance volumes" using Clipper offsets — the most memory-intensive part of tree support
5. **Branch instantiation:** Final tree branches converted to circular cross-section extrusion paths

**Complexity:** O(L × P × log P) where L = layers, P = overhang points per layer

---

## 6. Spatial Acceleration Structures

### AABB Tree (`AABBTreeIndirect.hpp`, `AABBMesh.hpp`)

- Template-based axis-aligned bounding box tree for mesh intersection queries
- Used by support generation and `TriangleMeshSlicer` for ray-casting operations
- Partitions triangles into a binary tree by splitting along the longest axis
- Build: O(N log N), query: O(log N) average

### KD-Tree (`KDTreeIndirect.hpp`)

- Template-based k-dimensional tree for nearest-neighbor queries on 2D/3D point sets
- Used by seam placement (nearest previous seam), support point placement

### EdgeGrid (`EdgeGrid.cpp`)

- Regular 2D grid of polygon edge buckets
- Accelerates point-in-polygon and nearest-edge queries during infill generation
- Build: O(E) where E = polygon edge count, query: O(1) amortized

### OpenVDB (`OpenVDBUtils.cpp`)

- Voxel grid used for mesh boolean operations (union, intersection, subtraction between models)
- Also used for support structure volume computation
- Provides O(1) voxel access; volumetric operations scale with voxel count

---

## 7. G-Code Generation

**Files:** [`GCode.cpp`](../src/libslic3r/GCode.cpp), [`GCodeWriter.cpp`](../src/libslic3r/GCodeWriter.cpp)

### Emission Strategy

G-code is assembled as a **growing std::string** — no streaming abstraction. The `GCodeWriter` class maintains printer state (current position, E value, temperature) and returns G-code strings for each motion.

### Key Sub-Algorithms

#### Tool Ordering (`GCode/ToolOrdering.cpp`)

For multi-material prints, determines the optimal tool change sequence per layer to minimize total tool changes. Uses a greedy algorithm with look-ahead.

#### Wipe Tower (`GCode/WipeTower.cpp`, `WipeTower2.cpp`)

Generates the purge/wipe tower geometry: a rectangular tower printed at layer start to purge filament during tool changes. `WipeTower2` is a newer implementation with improved volume calculation.

#### Seam Placement (`GCode/SeamPlacer.cpp`)

- Evaluates candidate seam positions based on: overhang angle, concave corners (natural hiding spots), user preference (random, aligned, rear, nearest)
- Maintains cross-layer seam history to align seams vertically (less visible)

#### Cooling Buffer (`GCode/CoolingBuffer.cpp`)

Post-processes G-code to adjust speeds based on estimated cooling time per layer. Ensures minimum layer time for proper cooling.

#### Pressure Equalizer (`GCode/PressureEqualizer.cpp`)

Smooths rapid changes in extrusion rate to reduce pressure advance artifacts. Scans ahead in the G-code buffer and redistributes speed changes.

#### Arc Fitting (`ArcFitter.cpp`)

Converts sequences of short line segments to arc commands (`G2`/`G3`) where the polyline approximates a circular arc within tolerance.

---

## 7. SeamPlacer Algorithm (Sessions 3 Details)

**File:** [`src/libslic3r/GCode/SeamPlacer.cpp`](../src/libslic3r/GCode/SeamPlacer.cpp)

### Visibility Scoring

1. **Surface sampling:** 30,000 points are sampled on the mesh surface using Poisson-disk distribution.
2. **Per-sample raycasting:** For each sample point, 25 rays are cast (5×5 hemisphere grid) using the `Frame` class to rotate hemisphere directions into world space.
3. **Visibility score:** Number of rays that hit another surface / 25. Low score = hidden = preferred seam location.

### Angle Penalty

`compute_angle_penalty(angle)` combines:
- A **Gaussian trough** for concave corners (negative angle) → very low penalty.
- A **sigmoid ramp** for convex corners (positive angle) → high penalty.

The combined function strongly biases seam placement toward concave features (the natural "hiding spot").

### Cross-Layer Alignment

`align_seam_points()` fits a B-spline to the seam positions across layers, using Z as the spline parameter. This creates a vertical seam line. The alignment assumes Z increases monotonically within each seam string (hazard for non-monotonic print orders).

### Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Surface sampling | O(V) | V = mesh vertex count |
| Per-sample raycasting | O(30,000 × 25) = O(750,000) | TBB parallel_for |
| Per-perimeter scoring | O(P × V_p) | P = perimeters, V_p = vertices per perimeter |
| B-spline alignment | O(L²) | L = layers with aligned seams |

---

## 8. WipeTower2 Planning Algorithm (Session 4 Details)

**File:** [`src/libslic3r/GCode/WipeTower2.cpp`](../src/libslic3r/GCode/WipeTower2.cpp)

### Two-Phase Architecture

**Phase 1 — Planning (run 5× for convergence):**
1. `plan_toolchange()` builds `m_plan[layer].tool_changes[]` with `required_depth = ramming_depth + wiping_depth`.
2. `plan_tower()` propagates depths downward: lower layers must be at least as deep as deeper layers above (O(n²) depth propagation).
3. `save_on_last_wipe()` steals finish_layer extrusion volume from the last wipe on each layer to reduce total tower height.

**Phase 2 — Generation:**
1. `generate()` iterates `m_plan` once, calling `tool_change()` and `finish_layer()` per layer.
2. Each `tool_change()` sequences: `toolchange_Unload()` → `toolchange_Change()` → `toolchange_Load()` → `toolchange_Wipe()`.
3. `finish_layer()` and adjacent toolchange TCR are merged via `merge_tcr()`.

### Wipe Volume → Tower Depth Conversion

```
length_to_extrude = wipe_volume / (layer_height × (perimeter_width - layer_height × (1 - π/4))) × filament_area
rows = ceil(length_to_extrude / tower_width)
depth = rows × perimeter_width × extra_spacing
```

### Ramming Speed Profile

The ramming speed is defined as a `std::vector<float>` of speeds, one entry per 0.25-second segment. The total ramming distance is the integral: `Σ(speed_i × 0.25)` mm.

### Cooling Move Schedule

For SEMM printers, `N = cooling_moves` back-and-forth moves are made across the cooling tube. Speed ramps linearly from `cooling_initial_speed` to `cooling_final_speed` across the N moves.

### Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| plan_toolchange() | O(1) per call | Amortized; builds m_plan incrementally |
| plan_tower() | O(n²) per run | n = number of layers; propagates depths downward |
| save_on_last_wipe() | O(n × k) per run | n = layers, k = toolchanges per layer |
| generate() | O(n × k) | One writer pass per layer-toolchange pair |
| Full convergence | O(5 × n²) | 5 iterations of plan_tower |
