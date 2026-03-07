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
