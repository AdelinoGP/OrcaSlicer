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

## 2b. Model Loading in Detail

**Primary dispatch:** [`Model::read_from_file`](../src/libslic3r/Model.cpp#L353) selects the loader mostly by extension and normalizes every successful import into the same `Model -> ModelObject -> ModelVolume -> TriangleMesh` scene graph.

### Pipeline classes

| Class | Formats | Core construction path | Notes |
|---|---|---|---|
| Direct triangle ingestion | OBJ, AMF, 3MF, BBS 3MF, DRC | Parser builds `indexed_triangle_set` directly, then wraps it in `TriangleMesh` / `ModelVolume` | Minimal geometric normalization before the generic mesh pipeline |
| Triangle-import bridge | STL, STEP, SVG, ModelIO fallback | Format-specific reader first produces admesh `stl_file`-style triangles, then `TriangleMesh::from_stl()` converts that to shared-vertex ITS | These are the only importers that automatically run the admesh repair pass on load |
| Slice-reconstruction bridge | SL1 / SL1S | PNG rasters -> marching-squares contours -> `ExPolygons` -> `slices_to_mesh()` -> ITS | Geometry is reconstructed from layer silhouettes, not original triangles |

### What admesh repair actually guarantees

The STL conversion path is defined in [`TriangleMesh::trianglemesh_repair_on_import`](../src/libslic3r/TriangleMesh.cpp#L100) and reused by any importer that ends in `TriangleMesh::from_stl()`.

- Exact shared-edge matching: `stl_check_facets_exact()` welds bitwise-identical triangle edges first.
- Nearby-edge stitching: `stl_check_facets_nearby()` retries with a tolerance derived from shortest edge and bounding diameter.
- Non-manifold cleanup: `stl_remove_unconnected_facets()` drops facets that still fail connectivity checks.
- Normal/orientation normalization: `stl_fix_normal_directions()`, `stl_fix_normal_values()`, and `stl_calculate_volume()` enforce outward winding when the signed volume is negative.
- No hole filling: `stl_fill_holes()` is explicitly disabled, so watertightness is not guaranteed even after repair.

This means "pre-admesh repair" differs sharply by parser: STL/STEP/SVG/ModelIO can promise eventual edge welding + normal cleanup before slicing, while the direct-ITS parsers generally only promise index validity plus, at most, a global winding flip.

### Parser-by-parser construction map

| Parser | Produces `indexed_triangle_set` directly? | Intermediate representation before final mesh | Mesh quality guarantees before admesh repair |
|---|---|---|---|
| [`STL.cpp`](../src/libslic3r/Format/STL.cpp#L39) | No | `TriangleMesh::ReadSTLFile()` reads binary/ASCII STL into admesh `stl_file`, then `TriangleMesh::from_stl()` converts to ITS | Strongest normalization path: edge matching, nearby welding, unconnected-face removal, normal fix, signed-volume flip, but no hole fill |
| [`OBJ.cpp`](../src/libslic3r/Format/OBJ.cpp#L58) | Yes | `ObjParser` token stream -> direct ITS assembly -> `TriangleMesh(std::move(its))` | Rejects faces with fewer than 3 or more than 4 vertices; quads are fan-triangulated; only global negative-volume winding is corrected |
| [`AMF.cpp`](../src/libslic3r/Format/AMF.cpp#L675) | Yes | SAX parser builds shared object vertex table, then each `<volume>` is compacted into its own ITS | Validates face indices, compacts referenced vertex span, and flips the whole ITS if signed volume is negative; no edge welding or manifold repair |
| [`3mf.cpp`](../src/libslic3r/Format/3mf.cpp#L447) | Yes | ZIP + streaming XML -> one geometry buffer per object -> sidecar triangle ranges split geometry back into per-volume ITS blocks in [`_generate_volumes`](../src/libslic3r/Format/3mf.cpp#L2168) | Validates triangle/vertex index ranges, preserves stored mesh-repair stats metadata, and flips whole-volume winding if negative; geometry itself is not re-repaired on import |
| [`bbs_3mf.cpp`](../src/libslic3r/Format/bbs_3mf.cpp#L4798) | Yes | Same core 3MF geometry idea, but with Bambu sidecars, split object files, shared-mesh references, and per-volume extension metadata | Similar to base 3MF: validates indices, reconstructs per-volume ITS blocks, reapplies stored mesh stats/extensions, flips negative-volume meshes, but does not run admesh repair unless a later tool explicitly does so |
| [`STEP.cpp`](../src/libslic3r/Format/STEP.cpp#L528) | No | OCCT B-Rep document -> `BRepMesh_IncrementalMesh` tessellation -> copied into admesh `stl_file` -> `TriangleMesh::from_stl()` | Tessellation quality is controlled by OCCT linear/angular deflection; after tessellation it inherits the STL/admesh repair guarantees |
| [`svg.cpp`](../src/libslic3r/Format/svg.cpp#L1) | No | NanoSVG paths -> sampled polylines -> Clipper stroke offsets -> OCCT prism extrusion -> admesh `stl_file` -> `TriangleMesh::from_stl()` | Guarantees only sampled/extruded approximation before repair; fidelity depends on Bezier sampling and fixed extrusion depth, then admesh cleans topology like STL |
| [`DRC.cpp`](../src/libslic3r/Format/DRC.cpp#L45) | Yes | Draco decoder -> direct ITS reconstruction from decoded position/face arrays | Verifies geometry kind is triangular mesh and remaps Draco point IDs back to position indices; only global winding is normalized |
| [`SL1.cpp`](../src/libslic3r/Format/SL1.cpp#L344) | Yes, but only after raster reconstruction | ZIP archive -> PNG decode -> marching squares -> `ExPolygons` -> [`slices_to_mesh()`](../src/libslic3r/Format/SL1.cpp#L396) -> ITS | Guarantees a contour-lofted shell consistent with archived slice rasters; geometry is intentionally lossy and bounded by raster resolution/window smoothing |
| [`ModelIO.hpp`](../src/libslic3r/Format/ModelIO.hpp#L4) | No | Apple-only foreign format -> temporary STL file -> normal STL loader | No format-native guarantee at all; the fallback intentionally inherits STL parsing + admesh repair semantics |

### Practical translation takeaway

- If the target runtime already has a robust manifold-triangle repair library, only the STL/STEP/SVG/ModelIO branch truly needs it at import time.
- OBJ/AMF/3MF/BBS 3MF/DRC are primarily schema-to-ITS translators; the port must preserve their rebasing, volume-splitting, and global winding-correction rules more than it needs a heavyweight repair pass.
- SL1 is not a mesh parser in the normal sense. It is a slice-stack reconstructor and should be ported next to SLA raster logic, not next to STL/OBJ readers.

---

## 2. Perimeter Generation

**Files:** [`PerimeterGenerator.cpp`](../src/libslic3r/PerimeterGenerator.cpp), [`WallToolPaths.cpp`](../src/libslic3r/Arachne/WallToolPaths.cpp#L553), [`SkeletalTrapezoidation.cpp`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L604)

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

- **Entry point:** [`WallToolPaths::generate()`](../src/libslic3r/Arachne/WallToolPaths.cpp#L553) preprocesses the polygon, builds the beading-strategy decorator chain via [`BeadingStrategyFactory::makeStrategy()`](../src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.cpp#L50), and then hands the cleaned outline to [`SkeletalTrapezoidation::generateToolpaths()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L604).
- **Medial axis / straight skeleton:** [`SkeletalTrapezoidation::constructFromPolygons()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.hpp#L312) builds a Boost Voronoi diagram over polygon segments, discretizes parabolic point-segment arcs into linear pieces, and transfers the result into a half-edge graph backed by [`HalfEdgeGraph`](../src/libslic3r/Arachne/utils/HalfEdgeGraph.hpp#L27). The working graph mixes true skeleton edges with rib edges that connect the skeleton back to the original boundary.
- **Central-edge detection:** [`SkeletalTrapezoidation::updateIsCentral()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L672) classifies which half-edges belong to the usable medial axis by comparing edge length against change in distance-to-boundary. This is the actual "straight skeleton" filter: equidistant and slowly changing regions remain central; ribs and near-boundary artifacts are filtered away.
- **Beading strategy dispatch:** The decorator chain decides how many walls fit at each local thickness `2R` and how the leftover width is distributed. `DistributedBeadingStrategy` computes the base split, `RedistributeBeadingStrategy` protects outer-vs-inner wall roles, `WideningBeadingStrategy` optionally expands thin features, `OuterWallInsetBeadingStrategy` applies Orca's outer-wall offset, and `LimitedBeadingStrategy` caps bead count and injects the 0-width sentinel contour.
- **Variable-width assignment:** [`SkeletalTrapezoidation::generateSegments()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L1666) computes or interpolates per-node beadings, propagates them upward and downward through the skeleton, emits per-edge junction samples, and connects those samples into [`ExtrusionLine`](../src/libslic3r/Arachne/utils/ExtrusionLine.hpp#L52) polylines built from [`ExtrusionJunction`](../src/libslic3r/Arachne/utils/ExtrusionJunction.hpp#L29). Each junction carries its own local width, so one wall can smoothly narrow or widen along its length.
- **Post-processing and stitching:** [`WallToolPaths::stitchToolPaths()`](../src/libslic3r/Arachne/WallToolPaths.cpp#L716) uses [`PolylineStitcher`](../src/libslic3r/Arachne/utils/PolylineStitcher.hpp#L38) plus sparse-grid helpers ([`SparseGrid`](../src/libslic3r/Arachne/utils/SparseGrid.hpp#L34), [`SquareGrid`](../src/libslic3r/Arachne/utils/SquareGrid.hpp#L37), [`PolygonsPointIndex`](../src/libslic3r/Arachne/utils/PolygonsPointIndex.hpp#L158), [`PolygonsSegmentIndex`](../src/libslic3r/Arachne/utils/PolygonsSegmentIndex.hpp#L27)) to merge fragmented wall fragments back into printable loops and open centerlines.
- **When it wins over classic perimeters:** Arachne pays extra `O(V log V)` setup cost to preserve thin features and gradual wall-count transitions that the classic repeated-offset generator would collapse or quantize.

**For full algorithm details see Section 15 — Arachne: Straight Skeleton and Variable-Width Perimeters.**

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
- Both patterns generate a full-plane TPMS cross-section first, then run the same finishing stages: optional `-45°` pattern rotation, multiline widening, polygon clipping, short-fragment pruning, and infill connection ([`FillTpmsD::_fill_surface_single()`](../src/libslic3r/Fill/FillTpmsD.cpp#L196), [`FillTpmsFK::_fill_surface_single()`](../src/libslic3r/Fill/FillTpmsFK.cpp#L213)).

##### TPMS D / Schwartz Diamond (`FillTpmsD`)

- **Implicit surface:** `sin(x)sin(y)sin(z) - cos(x)cos(y)cos(z) = 0` ([`FillTpmsD.hpp`](../src/libslic3r/Fill/FillTpmsD.hpp#L1), [`FillTpmsD.cpp`](../src/libslic3r/Fill/FillTpmsD.cpp#L37)).
- **Analytic cross-section extraction:** the implementation rewrites the surface into `a*cos(u) = b*cos(v)` with `u = x - y`, `v = x + y`, `a = sin(z) - cos(z)`, and `b = sin(z) + cos(z)`. For a fixed layer `z`, it solves one branch explicitly as `v = acos((a / b) * cos(u))`; if `|a| > |b|`, it swaps `u` and `v` first so the `acos` argument stays in `[-1, 1]` ([`FillTpmsD.cpp`](../src/libslic3r/Fill/FillTpmsD.cpp#L39)).
- **Adaptive sampling algorithm:** `make_waves()` seeds one `2pi` period with 16 uniform segments, evaluates the true midpoint on every chord, and inserts a midpoint whenever the deviation exceeds `min(line_spacing / 2, PatternTolerance) / scaleFactor`. This repeats until every segment satisfies the chord-error bound, then the single-period wave is tiled across the bounding box and mirrored into `+v` and `-v` branches ([`FillTpmsD.cpp`](../src/libslic3r/Fill/FillTpmsD.cpp#L114)).
- **Density mapping:** requested density is converted into spatial frequency through `DensityAdjust = 2.1`, so denser infill means a smaller effective TPMS cell size rather than tighter 2D hatch spacing ([`FillTpmsD.hpp`](../src/libslic3r/Fill/FillTpmsD.hpp#L66), [`FillTpmsD.cpp`](../src/libslic3r/Fill/FillTpmsD.cpp#L209)).
- **Complexity:** approximately `O(P + R x S)` per region, where `P` is the number of tiled wave periods, `R` is the number of `vShift` bands crossing the bounding box, and `S` is the refined sample count per period. Because refinement is curvature-driven, high-density or high-curvature layers cost more than flat layers, but the work still scales with covered area rather than triangle count.

##### TPMS FK / Fischer-Koch S (`FillTpmsFK`)

- **Implicit surface:** `cos(2x)sin(y)cos(z) + cos(2y)sin(z)cos(x) + cos(2z)sin(x)cos(y) = 0` ([`FillTpmsFK.hpp`](../src/libslic3r/Fill/FillTpmsFK.hpp#L1), [`FillTpmsFK.cpp`](../src/libslic3r/Fill/FillTpmsFK.cpp#L7)).
- **Cross-section extraction method:** unlike TPMS D, FK is not converted into an explicit wave equation. OrcaSlicer builds a sampled scalar field over an expanded bounding box, evaluates the implicit field at the current layer height, and extracts the zero-isocontour with Marching Squares ([`ScalarField`](../src/libslic3r/Fill/FillTpmsFK.cpp#L38), [`get_polylines()`](../src/libslic3r/Fill/FillTpmsFK.cpp#L143)).
- **Sampling grid:** the field uses a coarse marching cell size of `0.40 mm` and a raster accuracy of `0.004 mm`; Marching Squares runs with TBB parallelism, returns closed rings, and simplifies them with `SCALED_SPARSE_INFILL_RESOLUTION` before clipping ([`FillTpmsFK.cpp`](../src/libslic3r/Fill/FillTpmsFK.cpp#L51), [`FillTpmsFK.cpp`](../src/libslic3r/Fill/FillTpmsFK.cpp#L166), [`FillTpmsFK.cpp`](../src/libslic3r/Fill/FillTpmsFK.cpp#L251)).
- **Density mapping:** the FK period is `vari_T = 4.18 * spacing * multiline / density_factor`, with `density_factor = min(0.9, params.density)`. So density changes the 3D field period directly, and the implementation intentionally caps effective density at 90% to avoid a degenerate field near solid fill ([`FillTpmsFK.cpp`](../src/libslic3r/Fill/FillTpmsFK.cpp#L225)).
- **Complexity:** approximately `O(G + C)` per region, where `G` is the number of sampled grid cells in the expanded bounding box and `C` is the total contour length emitted by Marching Squares. In practice this is more predictable than TPMS D because cost is dominated by raster area, not adaptive subdivision.

##### Practical Difference Between The Two TPMS Paths

- **TPMS D** is an analytic slicer of the 3D surface: it derives one exact layer curve family and only samples enough points to approximate curvature.
- **TPMS FK** is a raster-contour slicer: it samples the scalar field first, then extracts contours numerically.
- The translator consequence is important: TPMS D must preserve the coordinate transform and midpoint-refinement loop, while TPMS FK must preserve the scalar-field sampling contract, Marching Squares resolution, and the 90% density clamp.
- See `generated_documentation/pseudocode_tpms_infill.md` for language-agnostic pseudocode of both extraction paths.

### Complexity Notes
- Most patterns: O(area / line_spacing²) — scales with infill density
- Adaptive cubic: precomputation is O(V log V) where V = mesh vertex count; lookup per layer is O(k) where k = cells intersecting that layer
- Lightning: O(n log n) where n = number of "unlit" points requiring coverage
- TPMS D: adaptive-subdivision cost varies with curvature; TPMS FK: marching-grid cost varies with expanded bbox area

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

## 9. TreeSupport Pipeline Algorithm (Session 6 Details)

**File:** [`src/libslic3r/Support/TreeSupport.cpp`](../src/libslic3r/Support/TreeSupport.cpp)
**Header:** [`src/libslic3r/Support/TreeSupport.hpp`](../src/libslic3r/Support/TreeSupport.hpp)

### Pipeline Overview

`TreeSupport::generate()` runs 7 serial stages:

```
detect_overhangs()
  → generate_contact_points()
    → plan_layer_heights()
      → drop_nodes()
        → smooth_nodes()
          → draw_circles()
            → generate_toolpaths()
```

`smsTreeOrganic` short-circuits immediately to `generate_tree_support_3D()` (TreeSupport3D.cpp). Only `smsTreeSlim`, `smsTreeStrong`, `smsTreeHybrid` run this pipeline.

### Stage 1 — `detect_overhangs()`

- Two TBB `parallel_for` passes over all layers.
- Serial pass: sharp-tail propagation (bottom-up, layer by layer).
- Serial pass: overhang-cluster grouping (`OverhangCluster` O(N²) scan).
- **Timeout escape hatch**: if sharp-tail detection exceeds 30 wall-clock seconds, `config_detect_sharp_tails` is disabled mid-run. Partial state may leave some layers with annotations and others without.

### Stage 2 — `generate_contact_points()`

- TBB `parallel_for` over overhang layers.
- Generates candidate contact points on a 22°-rotated grid over the overhang polygon.
- Grid rotation deliberately avoids alignment with model geometry.
- `tbb::spin_mutex` guards insertion into shared node list.

### Stage 3 — `plan_layer_heights()`

- Computes adaptive support layer heights.
- Remaps `contact_nodes` to new support layer indices after planning.
- **Negative sentinel**: `node->distance_to_top = -num_layers` encodes a multi-layer gap. Any consumer of `distance_to_top` must handle negative values.

### Stage 4 — `drop_nodes()` (Performance Critical)

The most CPU-intensive stage. Proceeds **fully serial** top→bottom.

Pre-computation phase (parallel):
- TBB `parallel_for` pre-computes avoidance radii for all layers.

Per-layer serial phases:
1. Group nodes by ExPolygon island ("part").
2. Compute MST per group to find merge candidates.
3. **Pass 1**: Merge nodes closer than `merge_radius`.
4. **Pass 2**: Move nodes toward neighbors and/or toward exterior.

**`insert_dropped_node()` O(N²) hazard**: uses `std::find` — O(N) per call — making the full drop O(N²) on layers with many support nodes.

### Stage 5 — `smooth_nodes()`

- 100 Laplacian smoothing iterations per branch chain.
- Sets `need_extra_wall` flag on nodes displaced beyond a threshold.

### Stage 6 — `draw_circles()`

- TBB `parallel_for` over layers.
- Generates `branch_circle` polygon:
  - 100 vertices normally.
  - 4 vertices if `avg_node_per_layer > 200` (performance fallback for very dense support).
- After main TBB loop: serial hole-propagation loop uses raw `Polygon*` as map keys — **dangling pointer risk** if the vector backing those pointers reallocates.
- Optional lightning infill generation in global mode.

### Stage 7 — `generate_toolpaths()`

- TBB `parallel_for` over non-raft layers.
- Handles: raft layers, raft interface layers, base layers between raft and object, then tree support layers.
- `make_perimeter_and_inner_brim()` for roof/floor interface layers.
- `make_perimeter_and_infill()` for base layers.

### `TreeSupportData` — Collision/Avoidance Cache

```
tbb::concurrent_unordered_map<RadiusLayerPair, Polygons>
  m_collision_cache
  m_avoidance_cache
```

- `calculate_collision()` / `calculate_avoidance()`: recursive, capped at depth 100 via pre-computing layer N-100.
- Constructor builds `m_layer_outlines_below` as serial O(N²) cumulative union.
- `ceil_radius()`: snaps radius to the nearest pre-defined step to maximize cache hit rate.

### `calc_branch_radius()` — Taper Profile

Two overloads (layer-count-based and mm-based):
```
if layer < tip_layers:
    radius = min_radius + (target_radius - min_radius) × (layer / tip_layers)
else:
    radius = target_radius + (layer - tip_layers) × diameter_angle_scale_factor
```
- `slim` mode doubles the `tip_layers` range (slower taper).
- Output clamped to `[MIN_BRANCH_RADIUS, MAX_BRANCH_RADIUS]`.

### Complexity Summary

| Stage | Parallelism | Complexity | Notes |
|-------|-------------|------------|-------|
| detect_overhangs | TBB parallel_for (2 passes) | O(L × P) | L=layers, P=polygon ops per layer |
| generate_contact_points | TBB parallel_for | O(L × A/g²) | A=overhang area, g=grid spacing |
| plan_layer_heights | Serial | O(L) | simple height planning |
| drop_nodes | Serial (avoidance: TBB) | O(L × N²) worst case | N=nodes per layer; insert_dropped_node O(N) |
| smooth_nodes | Serial | O(100 × chains × len) | 100 Laplacian iterations |
| draw_circles | TBB parallel_for | O(L × N × V) | V=vertices per circle |
| generate_toolpaths | TBB parallel_for | O(L × N) | per-layer polygon ops |

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

---

## Section 10 — TreeSupport3D Organic Pipeline

**File:** `src/libslic3r/Support/TreeSupport3D.cpp`

### Overview

`TreeSupport3D` implements the organic tree support mode (`smsTreeOrganic`). It is only called via the thin adapter `generate_tree_support_3D()` from `TreeSupport.cpp`. The pipeline runs on one `PrintObject` at a time, in a single call to `generate_support_areas()`.

### Pipeline Steps

```
generate_overhangs()        — DEAD CODE (#if 0). Active code uses TreeSupport::detect_overhangs().
generate_initial_areas()    — TBB parallel: place contact circles at overhang tips
create_layer_pathing()      — Top-down serial: grow & merge influence areas layer by layer
set_points_on_areas()       — Propagate result_on_layer bottom-up from children to parents
set_to_model_contact_to_model_gracious() — Walk up to highest valid layer for model-contact
create_nodes_from_area()    — Bottom-up serial: finalize result_on_layer points
organic_smooth_branches_avoid_collisions() — 100-iter TBB sphere nudge + Laplacian smooth
organic_draw_branches()     — Flatten to paths → extrude_branch() → slice → union → merge
```

### Influence Area Propagation

Each element at layer L has an **influence area** — the set of XY positions reachable on layer L-1 while respecting the support angle and collision constraints. The propagation loop in `increase_areas_one_layer()` tries up to 4 `AreaIncreaseSettings` configurations per element in priority order:

1. Slow move, increase radius
2. Slow move, keep radius
3. Fast move, increase radius
4. Fast move, keep radius

Each configuration calls `increase_single_area()`, which:
1. Optionally grows the element's radius (`effective_radius_height += 1`)
2. Optionally offsets the area by `maximum_move_distance` (fast) or `maximum_move_distance_slow` (slow)
3. Clips against `getAvoidance()` (build-plate path) and/or `getCollision()` (model-contact path)
4. Returns `std::nullopt` if the result is below `tiny_area_threshold`

### Sphere Smoothing Algorithm (`organic_smooth_branches_avoid_collisions`)

After `create_nodes_from_area()` places `result_on_layer` XY positions, the smoothing pass refines them to avoid collisions and produce a smooth organic tube shape.

**Data structures:**
- `CollisionSphere`: one per element in `elements_with_link_down`. Stores current XY position, radius, neighbor links, and Z span for collision testing.
- `LayerCollisionCache`: one per layer. Contains `vector<Linef>` (object contour edges) + `AABBTreeIndirect::Tree` for fast nearest-line query.

**Per-iteration (100 iterations max):**
```
parallel_for each unlocked sphere:
  1. Scan collision cache layers within [layer_begin, layer_end)
     - Compute 2D cross-section radius at each layer (sphere slice)
     - AABBTree nearest-line query
     - If closest line is inside the sphere, record collision depth
  2. If collision detected: nudge XY away by min(depth + 0.1mm, 0.5mm)
  3. Laplacian smooth: weighted avg of neighbor positions → shift by min(|Δ|, 0.2mm)
  Stop early if num_moved == 0
```

**Complexity:** O(100 × N × L) where N = elements, L = layers per element's Z span. For tall complex prints, L can be large (hundreds of layers), making this the dominant cost.

### Mesh Generation (`extrude_branch`)

Each branch path (list of `SupportElement*` sorted bottom→top) is turned into a 3D capsule tube:

1. **Bottom hemisphere:** fan of `discretize_circle()` rings around path[0], stepping from polar angle 0 → π/2
2. **Body:** `discretize_circle()` ring at each intermediate waypoint; rings stitched by `triangulate_strip()`
3. **Top hemisphere:** fan of rings around path[-1], stepping from π/2 → 0

The cross-section normal at waypoints is the bisector direction: `(v1 + v2).normalized()`. Capsule geometry is assembled into `indexed_triangle_set` and then sliced layer-by-layer to produce 2D support polygons.

### Complexity Summary

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `generate_initial_areas()` | O(N_tips × M_rotations) | TBB parallel; M = 1 (22° grid fixed) |
| `increase_areas_one_layer()` | O(N × 4 × Clipper) | 4 settings tried per element per layer |
| `merge_influence_areas()` | O(N log N) amortized | Divide-and-conquer with AABBTree |
| `organic_smooth_branches_avoid_collisions()` | O(100 × N × L) | Dominant cost for tall complex models |
| `extrude_branch()` | O(path_len × nsteps) | nsteps ∝ 1/eps ≈ 50–200 vertices/ring |
| `organic_draw_branches()` TBB slice | O(B × layer_count) | B = number of branches |

---

## 8. Overhang Extra Perimeters (PerimeterGenerator.cpp)

**Files:** [`PerimeterGenerator.cpp`](../src/libslic3r/PerimeterGenerator.cpp)

### paths_touch()

Checks whether any point of either path is within `limit_distance` of the other path's line segments. Builds two `AABBTreeLines::LinesDistancer` objects locally per call:

- AABB build: O(n + m) where n, m = segment counts of the two paths
- Distance queries: O(n × log m) + O(m × log n)
- Called O(p²) times from `sort_extra_perimeters()` for p overhang path fragments
- **Total:** O(p² × (n + m) × log(max(n,m))) per overhang region

### reconnect_polylines()

Greedy endpoint-merge of clipped overhang arc fragments:

- Nested O(p²) loop over all polyline pairs
- Acceptable for small p (typical: 5–30 fragments per overhang)
- Degrades for high-resolution complex overhangs

### sort_extra_perimeters()

Two-phase algorithm:
1. Dependency graph construction: O(p²) `paths_touch()` calls
2. Topological ordering (while-change loop): O(p²) worst case
3. Nearest-neighbour reconnection: O(p²) with 5mm cutoff heuristic

**Total:** O(p²) where p = number of overhang perimeter fragments per region.

### generate_extra_perimeters_over_overhangs()

For each overhang ExPolygon:
1. Iterative inward offset (while perimeter_polygon not empty): O(k) iterations, k = continuation_loops (= 2 + overhang_depth/spacing)
2. Each iteration: Clipper intersection + offset — O(v log v) where v = vertex count
3. gap fill: `ExPolygon::medial_axis()` — O(v log v)

**Total per overhang:** O(k × v log v)

### findAllTouchingPerimeters() / reorderPerimetersByProximity()

BFS-style wall ordering for Arachne extrusions:
- `findAllTouchingPerimeters()`: O(|refIndices| × N) per call, where N = total extrusion count
- `reorderPerimetersByProximity()` calls it D times (D = max inset depth)
- `MultiPoint::minimumDistanceBetweenLinesDefinedByPoints()`: O(|ref_pts| × |pts|) per pair
- **Total:** O(D × N² × avg_pts_per_extrusion) per island

For a typical island with 8 walls and 20 extrusion segments this is manageable. For complex variable-width islands (Arachne thin-wall mode) with 200+ segments across 15 inset levels, this becomes the dominant cost in `process_arachne()`.

### Complexity Summary (PerimeterGenerator.cpp)

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `paths_touch()` | O(n log m) per call | Called O(p²) times |
| `reconnect_polylines()` | O(p²) | p = fragment count |
| `sort_extra_perimeters()` | O(p²) | Including dependency graph |
| `generate_extra_perimeters_over_overhangs()` | O(k × v log v) | k ≈ 3–10, v = vertices |
| `findAllTouchingPerimeters()` | O(|ref| × N × pts) | Per BFS level |
| `reorderPerimetersByProximity()` | O(D × N² × pts) | D = inset depth |
| `process_classic()` full island | O(wall_loops × v log v) | Clipper offset chain |
| `process_arachne()` full island | O(v log v) + O(D×N²) | Arachne + ordering |

---

## Section 9 — PrintObject.cpp Algorithmic Complexities

**Files:** [`PrintObject.cpp`](../src/libslic3r/PrintObject.cpp)

### project_triangles_to_slabs()

Parallel projection of painted triangle meshes to per-layer 2D polygon slabs:

- **Input:** T triangles, L layers
- **Phase 1 (parallel):** For each triangle, binary-search the layer array for the first and last intersecting layer — O(log L) per triangle. Then interpolate slab-boundary intersection points — O(layers_spanned) per triangle.
  - Total Phase 1: O(T × (log L + avg_layers_spanned)) — parallelised with TBB blocked_range
- **Phase 2 (serial):** Merge triangle projections into per-layer output — O(T × avg_layers_spanned) = O(T × L) worst case (all triangles span all layers)
- **Typical case:** avg_layers_spanned << L, so Phase 2 is O(T)
- LightPolygon pre-reserves 5 points (triangle–slab intersection is at most a pentagon) to minimise heap allocations inside the parallel kernel

### discover_vertical_shells()

Shell-promotion algorithm: promotes stInternal fill surfaces to stInternalSolid within a shell width above/below each top/bottom surface:

- Per-region, per-layer nested loop: O(R × L) outer iterations
- For each layer with solid surfaces: collect top/bottom polygons, project to neighboring N layers
- Each projection: Clipper offset + intersection — O(v log v) where v = polygon vertex count
- **Total:** O(R × L × N × v log v) where N = top_shell_layers or bottom_shell_layers
- Serial — cross-layer dependencies prevent TBB parallelisation

### discover_horizontal_shells()

Scatters solid shell coverage across neighboring layers:

- Per-region, per-layer serial sweep: O(R × L × 3) for top/bottom/bridge surface types
- For each shell type at each layer, propagates to N neighboring layers:
  - Clipper intersections per layer: O(v log v)
- **Total:** O(R × L × N × v log v) — effectively the same as discover_vertical_shells
- Uses `goto EXTERNAL` for early loop exit — O(1) skip cost

### combine_infill()

N-layer infill combination (infill every N layers feature):

- Computes combine[] assignment array: O(L) linear pass
- For each combined group (at most L/N groups):
  - Multi-layer intersection of ExPolygons: O(N × v log v)
  - Clearance offset + diff for each of N layers: O(N × v log v)
- **Total:** O(L × v log v) — linear in layer count, Clipper-dominated

### invalidate_state_by_config_options()

Hand-maintained config-key → slicing-step mapping:

- **Complexity:** O(k × S) where k = number of changed config keys, S = number of steps to check
- In practice the switch is O(k) with O(1) per key (linear table lookup by string hash)
- No algorithmic concern — pure dispatch table

### Complexity Summary (PrintObject.cpp)

| Operation | Complexity | Parallelism | Notes |
|-----------|------------|-------------|-------|
| `project_triangles_to_slabs()` | O(T × log L) | TBB parallel | T = triangles, L = layers |
| `discover_vertical_shells()` | O(R × L × N × v log v) | Serial | N = shell layers |
| `discover_horizontal_shells()` | O(R × L × N × v log v) | Serial | N = shell layers |
| `combine_infill()` | O(L × v log v) | Serial | Per-region |
| `bridge_over_infill()` | O(L × v log v) | Serial | Clipper per layer |
| `clip_fill_surfaces()` | O(L × v log v) | Serial | Top-down pass |
| `detect_surfaces_type()` | O(L × R × v log v) | TBB parallel | Per-layer parallel |
| `process_external_surfaces()` | O(L × R × v log v) | TBB parallel | Per-layer parallel |

---

## Section 9 — G-code Export Pipeline (GCode.cpp, GCodeWriter.cpp, CoolingBuffer.cpp)

### Overview

G-code export is the final serialisation stage: it converts sliced layer data (ExtrusionEntityCollections, toolpaths, config) into a linear stream of G-code text. The pipeline is driven from `Print::export_gcode()` → `GCode::do_export()` → `GCode::_do_export()`.

### TBB Pipeline Architecture

`_do_export()` builds a `tbb::parallel_pipeline` with **all `serial_in_order` filters** — providing pipelining overlap (I/O and string construction can overlap across layers), but **no true parallelism**:

```
generator (serial)  →  [spiral_mode (serial)]  →  [pressure_equalizer (serial)]  →  cooling (serial)  →  output (serial)
```

- **generator:** calls `process_layer()` once per layer; emits `LayerResult` (a `std::string` of G-code for that layer)
- **spiral_mode filter:** optional; applies vase-mode stitching — O(v) per layer
- **pressure_equalizer filter:** optional; scans G-code string with regex to adjust E values — O(chars) per layer
- **cooling filter:** `CoolingBuffer::process_layer()` — adjusts fan speeds and print speeds — O(segments) per layer
- **output filter:** appends string to `GCodeOutputStream` (tee to file + `GCodeProcessor`) — O(chars)

Because all filters are `serial_in_order`, `m_writer`, `m_last_pos`, and all other `GCode` state are safe despite the pipeline wrapper. The pipeline purely provides overlap between the I/O stage and the string-building stage.

**Combinatorial pipeline hazard:** Four pipeline configurations are hardcoded (`{spiral × pressure_equalizer} = 4` variants). Adding a new optional stage doubles the required pipeline count.

### process_layer() Complexity

The core per-layer function. Complexity breakdown:

- **Island ordering:** `sort_print_object_instances()` — O(I log I) where I = print instances in layer
- **Perimeter/infill extrusion:** iterates `ExtrusionEntity` tree — O(E) where E = total extrusion entities
- **Travel path generation:** `AvoidCrossingPerimeters::travel_to()` — O(v log v) per travel move (Clipper + BFS on boundary graph)
- **Seam picking:** `SeamPlacer::place_seam()` — O(1) amortised (pre-built per-object cost map)
- **Wipe / retract:** O(1) per extrusion entity

**Total per layer:** O(E + V × log V) where V = total vertices in layer boundary polygons

### CoolingBuffer.process_layer() Complexity

CoolingBuffer rescans G-code strings to apply time-based fan and speed adjustments:

- Parse G-code string token by token: O(chars)
- Build time-estimate segment list: O(segments)
- Scan forward/backward over segments to compute fan activation time: O(segments × lookahead_window)
- Rescale F-values in affected segments: O(affected_segments)

**Total:** O(chars + segments²) worst-case (when the entire layer is within the fan lookahead window). For typical layers: O(chars).

The resonance-avoidance band check is O(1) per segment — frequency lookup in two config floats.

### PlaceholderParser — Template G-code Expansion

`PlaceholderParser` (PlaceholderParser.cpp, 2453 lines) expands template strings (start G-code, end G-code, tool-change G-code, etc.) that contain `[variable_name]` and `{expression}` placeholders:

- **Parsing:** Spirit X3 grammar (Boost.Spirit) — O(chars) for well-formed templates
- **Expression evaluation:** full arithmetic + comparison + if/else/for expression language — O(expression_depth) per expression node
- **Variable resolution:** hash map lookup — O(1) amortised
- **Output:** single-pass expansion — O(output_chars)

**Key hazard:** The expression language supports arbitrary loops (`{for i = 0 to N}...{endfor}`). A misconfigured start G-code can loop indefinitely. There is no iteration-count limit guard.

### Complexity Summary (G-code Export)

| Operation | Complexity | Parallelism | Notes |
|-----------|------------|-------------|-------|
| `process_layer()` | O(E + V log V) | Serial (pipeline) | E = extrusions, V = boundary vertices |
| `CoolingBuffer::process_layer()` | O(chars) typical | Serial | O(segments²) worst case |
| `PlaceholderParser::process()` | O(chars) | Serial | Spirit X3; unbounded for-loops |
| TBB pipeline overhead | O(L) | Pipelined | All filters serial_in_order |
| `GCodeProcessor::process()` | O(chars) | Serial | Post-export statistics pass |

---

## Section 10 — Wipe Tower Planning and G-code Generation (WipeTower2.cpp)

### Overview

The wipe (purge) tower is a disposable multi-material artifact printed alongside the actual model. It purges residual filament from the nozzle between tool changes. `WipeTower2` handles both **planning** (computing tower geometry and toolchange order) and **G-code generation** (emitting the purge extrusions).

### Two-Phase Design

**Phase 1 — Planning:** `Print::process()` calls `Print::_make_wipe_tower()` which calls `WipeTower2::plan_toolchange()` for each toolchange event (in Z-ascending order), then calls `WipeTower2::plan_tower()` to finalize tower dimensions.

**Phase 2 — G-code generation:** `GCode::_do_export()` calls `WipeTower2::tool_change()` at each layer boundary to generate the actual purge extrusions.

### plan_toolchange() Complexity

Called once per toolchange event (total T toolchanges across the print):

- Computes `required_depth = ramming_depth + wiping_depth` for this toolchange: O(1)
- Appends to per-layer plan list: O(1) amortised

**Total planning pass:** O(T)

### plan_tower() Complexity

Called once after all toolchanges are registered. Computes final `m_wipe_tower_depth`:

- Scans all layers top-down to find the maximum accumulated depth: O(L)
- **Depth propagation loop:** For each new deeper layer, visits all prior layers to update their depth record
  - This is noted in the source (`[HAZARD]`) as O(L²) worst case — when tower depth increases monotonically
- **Typical case:** O(L) when tower depth stabilizes quickly

### tool_change() Complexity

Called once per toolchange during G-code export. Sequences:

1. `toolchange_Unload()`: ramp filament out — O(ramming_steps) where ramming_steps ≈ 15
2. `toolchange_Change()`: emit toolchange placeholder — O(1)
3. `toolchange_Load()`: push new filament in — O(1) for multi-material; O(retraction_steps) for SEMM
4. `toolchange_Wipe()`: purge by extruding rows — O(purge_volume / row_volume) rows, each O(1)
5. `finish_layer()`: perimeter + infill — O(tower_perimeter_vertices)

**Total per toolchange:** O(rows) where rows ∝ purge_volume / (tower_width × layer_height × line_width)

### Wipe Volume Matrix

Purge volumes are stored in `flush_volumes_matrix` (N×N flat float array). For each toolchange from filament A to filament B:

- Lookup: `flush_volumes_matrix[A * N + B]` — O(1)
- N reconstructed from `sqrt(matrix.size())` — O(1) but fragile (H699)

### Complexity Summary (Wipe Tower)

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `plan_toolchange()` × T | O(T) total | T = total toolchanges |
| `plan_tower()` | O(L²) worst, O(L) typical | L = layers with toolchanges |
| `tool_change()` | O(rows) | rows ∝ purge_volume |
| `finish_layer()` | O(tower_perimeter_vertices) | Per layer |
| Volume matrix lookup | O(1) | Flat N×N array |

---

## Section 11 — Adaptive Layer Heights (SlicingAdaptive.cpp)

### Overview

`SlicingAdaptive` computes a non-uniform layer height profile that maximizes layer heights in low-detail regions and minimizes them in high-curvature regions, subject to a quality threshold.

### Algorithm

1. **Face normal classification:** For each mesh triangle, compute the deviation angle from horizontal. Store the maximum angle at each Z height band — O(T) where T = triangles.
2. **Height profile sweep:** Binary search for maximum allowable layer height at each Z level such that the chord deviation error < threshold. Height candidates are sampled from a precomputed `layer_height_profile_adaptive` curve — O(L log L) where L = layers.
3. **Smoothing pass:** Apply a running-average smoothing over the height profile to prevent abrupt layer-height transitions — O(L).

**Total:** O(T + L log L)

### Complexity Summary

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Face normal scan | O(T) | T = triangles |
| Height profile sweep | O(L log L) | Binary search per layer candidate |
| Smoothing | O(L) | Running average |

---

## Section 12 — Jump-Point Search Pathfinding (JumpPointSearch.cpp)

### Overview

`JumpPointSearch` implements the Jump Point Search (JPS) algorithm on a 2D grid to compute collision-free travel paths for nozzle moves (used in `AvoidCrossingPerimeters`).

### Algorithm

JPS is an optimized A* variant for uniform-cost grids:

- **Grid:** Boolean occupancy grid at configurable resolution (typically 0.5–1.0 mm/cell)
- **Heuristic:** Octile distance — O(1) per node
- **Jump point detection:** Horizontal/vertical/diagonal scans until a wall or jump point is found — O(grid_width) per scan in worst case
- **Open set:** Binary heap — O(log N) per insert/extract where N = open set size

**Complexity:** O(N log N) where N = expanded nodes. JPS typically expands far fewer nodes than A* on obstacle-sparse grids.

**Key hazard:** Grid resolution is fixed at construction time. Very dense obstacle configurations (many thin perimeters) may require a finer grid, increasing N quadratically.

---

## Section 13 — Arc Fitting: Segment-to-Arc Conversion (ArcFitter.cpp)

**File:** [`src/libslic3r/ArcFitter.cpp`](../src/libslic3r/ArcFitter.cpp), [`src/libslic3r/ArcFitter.hpp`](../src/libslic3r/ArcFitter.hpp)

### Overview

`ArcFitter` converts dense polylines (sequences of `coord_t` scaled-integer Points) into mixed sequences of arc moves (G2/G3) and linear moves (G1), stored as `PathFittingData`. The algorithm is a greedy sliding-window approach that is O(n) amortized over the input point count.

This is a BBS addition — PrusaSlicer does not include arc fitting. The output `PathFittingData` vector is consumed by `GCode.cpp` / `GCodeWriter.cpp` for arc-move emission.

### Algorithm: `do_arc_fitting()`

**Input:** `points` (dense polyline in scaled integers), `tolerance` (chord-error threshold in scaled units)

**Output:** `result` — a vector of `PathFittingData`, each entry covering a contiguous range of `points[]` with a type of `Linear_move`, `Arc_move_cw`, or `Arc_move_ccw`.

**Steps:**

1. **Degenerate guard:** If `points.size() < 3`, emit a single `Linear_move` spanning `[0, size-1]` and return. (Note: `size == 0` is UB — callers must guard against empty input.)

2. **Greedy window expansion:** Maintain `front_index` (window start) and `back_index` (current end). For each new point added:
   - Attempt `ArcSegment::try_create_arc(current_segment, target_arc, length, MAX_RADIUS, tolerance, LENGTH_PERCENT_TOLERANCE)`.
   - If the fit succeeds: save `target_arc` as `last_arc`. If we've reached the last point, commit immediately.
   - If the fit fails:
     - **Window size > 3:** The previous arc (up to `back_index - 1`) was valid — commit it. Reset window to `[back_index - 1, back_index]`.
     - **Window size == 3:** Three points could not form an arc — emit/extend a `Linear_move` for `[front_index, front_index+1]`. Adjacent linear entries are **merged** by extending `end_point_index` rather than appending a new entry. Reset window to `[back_index - 1, back_index]`.

3. **Tail flush:** After the loop, if `front_index != back_index`, any uncommitted tail is emitted as a linear segment (or merged with a preceding linear entry).

**Tolerance parameters (from `ArcSegment::try_create_arc`):**
- Chord error: All intermediate points must lie within `tolerance` of the fitted circle's circumference.
- `DEFAULT_ARC_LENGTH_PERCENT_TOLERANCE` (≈ 5%): The ratio of the straight chord length to the arc length must be above this threshold. This guards against near-degenerate arcs where a near-straight segment would produce a circle with enormous radius — such arcs are rejected and emitted as linear moves instead.
- `DEFAULT_SCALED_MAX_RADIUS`: Arc radius ceiling. Arcs with radius above this threshold are rejected (too shallow to be worth emitting as G2/G3).

### Algorithm: `do_arc_fitting_and_simplify()`

A two-pass pipeline combining arc fitting with Douglas-Peucker simplification:

1. **Pass 1:** Call `do_arc_fitting()` to classify the polyline into arc and linear spans.
2. **Pass 2 (mixed path):** For each span independently, apply `MultiPoint::_douglas_peucker()` at the same `tolerance`. This thins both linear spans (removes redundant collinear points) and arc spans (reduces intermediate points while preserving wipe-compatible path accuracy).
3. **Index remapping:** After DP, each span has fewer points. A prefix-sum of per-span `reduce_count[]` values is computed and subtracted from `end_point_index` in each `PathFittingData` entry. Correctness depends on spans being contiguous and non-overlapping (see HAZARD H762).

**Special case (all-linear):** If the entire result is one linear span, a global DP is applied directly and the single entry's index is updated — no per-span loop needed.

**HAZARD H761:** `points` is modified **in place** during `do_arc_fitting_and_simplify()`. Callers that pass a reference to a shared `Points` array will see the array mutated. The function signature does not communicate this mutation.

**HAZARD H762:** Index remapping relies on `PathFittingData` entries being in ascending order with contiguous, non-overlapping index ranges. There is no assertion to verify this. If `do_arc_fitting()` produces out-of-order or overlapping entries (a bug in the greedy loop), the prefix-sum remapping will silently produce wrong indices without any error.

**HAZARD H763:** `points.size() == 0` is not guarded — `points[size - 1]` in the degenerate path is UB for empty input. The `size == 1` case emits `PathFittingData{0, 0, Linear_move}` which is valid but produces a zero-length segment.

### Complexity Summary

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `do_arc_fitting` | O(n) amortized | Greedy window; each point committed at most twice |
| `try_create_arc` per window | O(w) | w = window size; circle fit is O(w) |
| Douglas-Peucker per span | O(k log k) | k = points in span |
| Index prefix-sum | O(s) | s = number of result spans |
| **Total `do_arc_fitting_and_simplify`** | **O(n log n)** | Dominated by DP passes |

---

## Section 14 — Multi-Material Segmentation by Painting (MultiMaterialSegmentation.cpp)

**File:** [`src/libslic3r/MultiMaterialSegmentation.cpp`](../src/libslic3r/MultiMaterialSegmentation.cpp)

### Overview

`multi_material_segmentation_by_painting()` converts 3D per-facet color annotations (painted by the user in the MMU painting gizmo) into per-layer, per-extruder `ExPolygon` regions. These regions are used by `PrintObject::slice()` to assign different filaments to different parts of each layer.

This is an entirely BBS-original algorithm — PrusaSlicer uses a different region-assignment mechanism. The algorithm is absent from the original PrusaSlicer `03_algorithmic_complexities` documentation despite being noted in the Session 2 journal.

### Data Flow

```
ModelVolume::mmu_segmentation_facets (TriangleFacetAnnotations)
    ↓  [TBB parallel: per extruder × per facet]
    → Project painted triangle edges onto 2D layer planes using EdgeGrid
    → PaintedLine[] per layer (color-tagged line segments on slice contours)
    ↓  [TBB parallel: per layer]
    → post_process_painted_lines(): resolve overlaps, assign dominant color per contour segment
    → colorize_contours(): produce ColoredLines[] (per-edge color assignments on polygon contours)
    → build_graph(): construct MMU_Graph (Voronoi + contour arcs)
    → remove_multiple_edges_in_vertices() + remove_nodes_with_one_arc(): prune graph
    → extract_colored_segments(): flood-fill graph to assign extruder colors to enclosed regions
    ↓
segmented_regions[layer_idx][extruder_idx] = ExPolygons
```

### Algorithm Phases

**Phase 1 — Slice preprocessing (parallel over layers):**
- Merge all `LayerRegion::slices` into a single `ExPolygons` per layer.
- Apply `offset_ex(+ε)` → `union_ex` → `offset_ex(-ε)` to close hairline gaps.
- Remove regions with area < 0.1 mm².
- Run `expolygons_simplify` to remove self-intersections that would corrupt the Voronoi diagram.
- Build an `EdgeGrid::Grid` per layer for spatial queries during triangle projection.

**Phase 2 — Triangle projection (nested parallel: extruder × facet):**
- For each `ModelVolume`, for each extruder color (1..N), for each painted facet:
  - Transform the triangle to print coordinates.
  - Find the layer range that intersects the triangle's Z extent.
  - For each intersecting layer, compute the 2D cross-section of the triangle at that Z height.
  - Project the resulting line segment onto the layer's `EdgeGrid` to find which polygon edges it crosses (via `PaintedLineVisitor::visit_cells_intersecting_line`).
  - Write the resulting `PaintedLine` to `painted_lines[layer_idx]` under a per-layer-bucket mutex (`layer_idx & 0x3F` selects 1 of 64 mutexes).

**Phase 3 — Layer segmentation (parallel over layers):**
- For each layer with non-empty `painted_lines`:
  1. `post_process_painted_lines`: Resolve color conflicts on edges where multiple painted lines overlap — assigns a single dominant color per contour segment.
  2. `colorize_contours`: Annotates each contour edge with its assigned color, producing `ColoredLines[]`.
  3. **Fast path:** If all edges have the same color, assign `input_expolygons` directly to that extruder's slot without building a Voronoi diagram.
  4. `build_graph`: Constructs `MMU_Graph`:
     - BORDER arcs follow input polygon edges (directed, one per edge).
     - NON_BORDER arcs come from the Voronoi diagram edges (bidirectional) computed from the colored contour lines.
     - Voronoi vertices are appended as interior nodes; their indices are stored in `vertex.color()` fields during construction.
  5. `remove_multiple_edges_in_vertices`: Removes duplicate arcs at high-valence nodes.
  6. `graph.remove_nodes_with_one_arc`: Prunes dead-end Voronoi branches (queue-based iterative pruning).
  7. `extract_colored_segments`: Flood-fill traversal of the `MMU_Graph` to assign extruder regions. Each enclosed face in the graph receives the color of its bounding BORDER arcs.

**Phase 4 — Optional top/bottom layer propagation:**
- For `IncludeTopAndBottomLayers::Yes` (MMU mode): propagate segmentation upward/downward to cover top/bottom surfaces that were not directly painted.

**Phase 5 — Optional width limiting (`cut_segmented_layers`):**
- If `segmentation_max_width > 0`: trim each colored region to a maximum width from the color boundary using a negative offset, preventing very thin painted regions from being discarded but also preventing them from growing arbitrarily wide.

### Complexity Summary

| Phase | Complexity | Notes |
|-------|------------|-------|
| Slice preprocessing | O(L × P log P) | L = layers, P = polygon points per layer |
| Triangle projection | O(E × F × L_f) | E = extruders, F = painted facets, L_f = layers per facet |
| Mutex contention | 64 buckets | Fixed; not a function of input size |
| Graph construction | O(P + V log V) per layer | P = contour points, V = Voronoi vertices |
| Graph pruning | O(degree) per node | Typically < 10; see HAZARD H566 |
| Flood-fill segmentation | O(nodes + arcs) per layer | Linear traversal |
| **Total** | **O(L × (P log P + V log V) + E × F × L_f)** | Memory O(L × E) for segmented_regions |

### Key Hazards

**HAZARD H561:** `MMU_Graph::Arc::color()` is overloaded across construction phases. Before node indices are assigned, `color()` holds `VD_ANNOTATION` sentinel values (1 = ON_CONTOUR, 2 = DELETED). After assignment, it holds node indices. Code that reads `color()` without tracking the current construction phase will misinterpret the value.

**HAZARD H562 (memory):** `segmented_regions` is allocated as `num_layers × num_facets_states` `ExPolygons`. For a 500-layer, 5-extruder print with complex geometry, this can exceed hundreds of MB before any simplification. No memory cap exists on this structure.

**HAZARD H566:** `MMU_Graph::append_edge` deduplicates by scanning adjacency lists — O(degree) per call. For high-valence Voronoi vertices, this degrades to O(degree²) per vertex. In practice degree stays below 10, but no assertion enforces this.

---

## Section 15 — Arachne: Straight Skeleton and Variable-Width Perimeters

**Files:** [`src/libslic3r/Arachne/WallToolPaths.cpp`](../src/libslic3r/Arachne/WallToolPaths.cpp), [`src/libslic3r/Arachne/SkeletalTrapezoidation.cpp`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp)

### Overview

The Arachne perimeter generator (activated when `perimeter_generator = arachne`) produces variable-width extrusion paths that taper smoothly at thin regions and junctions, rather than abruptly stopping as the classic generator does. The core is a **straight skeleton** (also called medial axis) computation implemented via the `SkeletalTrapezoidation` algorithm.

The Arachne algorithm was developed by Kuipers et al. (TU Delft / Ultimaker) and is described in the paper "Variable-width contouring for additive manufacturing" (ACM SIGGRAPH 2022). OrcaSlicer inherits it from PrusaSlicer, which ported it from the original CuraEngine implementation.

The implementation in OrcaSlicer is not a single black-box function. It is a pipeline with three reusable layers:

- Geometry transfer and graph storage: [`HalfEdgeGraph`](../src/libslic3r/Arachne/utils/HalfEdgeGraph.hpp#L27) plus [`SkeletalTrapezoidationGraph`](../src/libslic3r/Arachne/SkeletalTrapezoidationGraph.hpp#L78)
- Width-bearing output types: [`ExtrusionJunction`](../src/libslic3r/Arachne/utils/ExtrusionJunction.hpp#L29) and [`ExtrusionLine`](../src/libslic3r/Arachne/utils/ExtrusionLine.hpp#L52)
- Spatial/post-processing utilities: [`PolylineStitcher`](../src/libslic3r/Arachne/utils/PolylineStitcher.hpp#L38), [`SparseGrid`](../src/libslic3r/Arachne/utils/SparseGrid.hpp#L34), [`SquareGrid`](../src/libslic3r/Arachne/utils/SquareGrid.hpp#L37), [`PolygonsPointIndex`](../src/libslic3r/Arachne/utils/PolygonsPointIndex.hpp#L158), and [`PolygonsSegmentIndex`](../src/libslic3r/Arachne/utils/PolygonsSegmentIndex.hpp#L27)

Translation agents should preserve these as separate concerns even if the target language does not keep the exact class layout.

### Phase 1 — Preprocessing (`WallToolPaths::generate()`)

1. Apply `simplifyPolygons` to remove near-degenerate vertices that cause numerical instability in the Voronoi phase.
2. Compute the minimum bead count and wall-width constraints from `BeadingStrategyFactory` based on configured `min_bead_width`, `min_feature_size`, `wall_transition_angle`, and `inward_distributed_beads`.
3. Call `SkeletalTrapezoidation::generateToolpaths()` on the processed polygon.

The real preprocessing sequence in [`WallToolPaths::generate()`](../src/libslic3r/Arachne/WallToolPaths.cpp#L553) is heavier than the three-step summary above:

1. Triple offset `(-e, +2e, -e)` snaps tiny gaps and self-touching spikes.
2. Simplify, self-intersection repair, degenerate-vertex removal, and near-colinear cleanup run in sequence.
3. `union_()` produces a final valid polygon set before Voronoi construction.

This means Arachne never sees the raw slice polygon. It sees a mesh-fixed version biased toward topological stability, which is one reason its output differs slightly from the classic offset-only perimeter path.

### Phase 1.5 — Beading Strategy Construction

[`BeadingStrategyFactory::makeStrategy()`](../src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.cpp#L50) builds the width-policy chain once per island:

1. `DistributedBeadingStrategy` decides the nominal bead count and baseline per-bead widths from local thickness.
2. `RedistributeBeadingStrategy` preserves outer-wall quality by biasing excess or missing width toward less visible inner beads.
3. `WideningBeadingStrategy` optionally keeps thin printable features alive by widening them instead of deleting them.
4. `OuterWallInsetBeadingStrategy` applies the configured outer-wall inset/outset after width planning.
5. `LimitedBeadingStrategy` caps the count and adds the 0-width contour marker used later by `separateOutInnerContour()`.

This dispatch layer is why "Arachne" is not one algorithm but a geometry engine plus a width-allocation policy stack.

### Phase 2 — Straight Skeleton via Voronoi (`SkeletalTrapezoidation`)

The straight skeleton is computed indirectly using the Voronoi diagram of the polygon edges:

1. **Voronoi construction:** `boost::polygon::construct_voronoi()` is run over the polygon segments. Point-segment cells represent corner-to-wall interactions; point-point cells represent corner-to-corner interactions.
2. **Edge transfer:** [`transferEdge()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.hpp#L355) copies only the Voronoi arcs that lie inside the polygon into the half-edge graph. Straight edges stay straight; parabolic edges are discretized into short linear segments so the rest of the pipeline can stay piecewise-linear.
3. **Rib insertion:** Every skeleton segment gets companion rib edges back to the source boundary. The output is therefore a trapezoidation/decomposition of the polygon interior, not just a naked centerline graph.
4. **Central-edge marking:** [`updateIsCentral()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L672) marks which half-edges are part of the true medial axis. The test compares geometric slope in radius-space (`dR / dD`) against the configured transition angle, so sharp corners naturally produce short non-central ribs while broad valleys remain central.
5. **Central cleanup:** `filterCentral()` removes tiny whiskers; `filterOuterCentral()` can optionally demote outermost central edges so sharp tips loop instead of ending as a single strand.
6. **Local bead counts:** `updateBeadCount()` queries the composed beading strategy with the local diameter `2 * distance_to_boundary`. Each central node now knows how many walls should fit at that thickness.
7. **Non-central fill:** `filterNoncentralRegions()` extends bead counts across ribs and narrow peninsulas so each trapezoid side has a coherent target wall count.
8. **Transition placement:** [`generateTransitioningRibs()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L881) and [`generateAllTransitionEnds()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L1218) insert explicit transition markers wherever the optimal bead count changes along an edge.

The crucial mental model is: Arachne does not directly offset the polygon by a fixed spacing. It first converts the interior into a graph whose scalar field is "local printable thickness," then extracts equal-bead contours from that field.

### Phase 3 — Toolpath Extraction

1. **Beading propagation:** [`generateSegments()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L1666) first stores concrete `Beading` objects at nodes with known counts, then propagates them upward to unresolved maxima and downward across non-central edges. This produces a continuous local wall-width plan across the graph even where no direct `compute()` result existed.
2. **Transition interpolation:** Nodes introduced on transition ribs may hold blended beadings rather than an exact discrete wall count. This is how Arachne changes from, for example, 3 walls to 2 walls without an abrupt step.
3. **Junction generation:** Each graph edge is sampled into `ExtrusionJunction` points whose fields are `(x, y, local_width, perimeter_index)`. Conceptually, each junction is one point on one isocontour of the local-thickness field.
4. **Line assembly:** [`connectJunctions()`](../src/libslic3r/Arachne/SkeletalTrapezoidation.cpp#L2254) walks each trapezoid/quad, pairs the junction lists on its two sides, and emits segments into `VariableWidthLines`. Those lines are stored as `ExtrusionLine` polylines, one inset band at a time.
5. **Post-stitching:** [`WallToolPaths::stitchToolPaths()`](../src/libslic3r/Arachne/WallToolPaths.cpp#L716) merges fragmented lines with `PolylineStitcher`. Then `removeSmallLines()`, `separateOutInnerContour()`, and `simplifyToolPaths()` convert the raw skeleton output into printable perimeter loops plus the zero-width inner contour marker.
6. **Print-pipeline handoff:** [`WallToolPaths::getToolPaths()`](../src/libslic3r/Arachne/WallToolPaths.cpp#L877) lazily caches the result, and `PerimeterGenerator` later orders these lines alongside other extrusion roles.

### Output Contract

- The output is not a polygon offset tree. It is a vector of inset bands, each containing `ExtrusionLine` polylines with per-junction widths.
- `perimeter_index` is the stable identity of a band; translators should not infer wall role from list order alone.
- Closed loops duplicate the first/last junction, while odd centerlines stay open. This distinction is preserved all the way into later variable-width extrusion conversion.

### Beading Strategies

The `BeadingStrategy` abstraction controls how wall widths are distributed. OrcaSlicer uses:

| Strategy | Description |
|----------|-------------|
| `DistributedBeadingStrategy` | Distributes width evenly across all beads |
| `RedistributeBeadingStrategy` | Keeps outer/inner beads at nominal width; redistributes excess to inner beads |
| `OuterWallInsetBeadingStrategy` | Applies a configured inset to the outermost bead for overhang compensation |
| `WideningBeadingStrategy` | Allows beads to widen beyond nominal when there are too few beads for the local thickness |
| `LimitedBeadingStrategy` | Caps bead count to a configured maximum |

Strategies are composed by wrapping (decorator pattern) in `BeadingStrategyFactory::makeStrategy()`. The order matters because later decorators assume the invariants created by earlier ones; for example, `LimitedBeadingStrategy` must run last so its 0-width sentinel contour is not widened or redistributed.

### Complexity Summary

| Phase | Complexity | Notes |
|-------|------------|-------|
| Polygon simplification | O(V) | V = polygon vertices |
| Voronoi diagram construction | O(V log V) | boost::polygon Voronoi |
| Graph walk and annotation | O(E) | E = Voronoi edges ≈ O(V) |
| Bead count propagation | O(E × max_beads) | max_beads typically 2–20 |
| Transition insertion | O(E) | One pass over all edges |
| Toolpath extraction | O(E × max_beads) | One isocurve trace per bead per edge |
| **Total per polygon** | **O(V log V + E × B)** | B = max bead count; E ≈ 2V for simple polygons |

For a typical layer with 10 islands averaging 200 polygon vertices each and B = 6:

- Voronoi: O(2000 log 2000) ≈ 22,000 ops
- Toolpath: O(4000 × 6) = 24,000 ops

Arachne is the dominant cost in `PerimeterGenerator` for layers with many thin features; it replaces the cheaper O(V) Clipper offset of the classic generator with an O(V log V) Voronoi-based computation.

### Relationship to Section 2

Section 2 of this document describes the classic perimeter generator and notes Arachne uses a "straight skeleton (medial axis)" without explaining the implementation. The Voronoi-based straight skeleton construction described here is that implementation. The key insight: the Voronoi diagram of polygon edges **is** the straight skeleton; Arachne does not run a separate medial axis algorithm.

---
