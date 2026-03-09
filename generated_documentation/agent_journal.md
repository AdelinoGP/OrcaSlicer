# Agent Journal — OrcaSlicer Codebase Analysis

## Session 1 — Orientation Phase

### Files Processed
- `/CMakeLists.txt` (root)
- `/README.md`
- `/src/OrcaSlicer.cpp` (lines 1-249, entry point)
- `/src/OrcaSlicer.hpp`
- `/src/libslic3r/` (directory listing)
- `/src/libslic3r/TriangleMesh.hpp`
- `/src/libslic3r/TriangleMeshSlicer.hpp`
- `/src/libslic3r/Print.hpp` (lines 1-100)
- `/src/libslic3r/Model.hpp` (lines 1-100)
- `/src/libslic3r/Format/` (directory listing)
- `/src/libslic3r/Fill/` (directory listing)
- `/src/libslic3r/GCode/` (directory listing)
- `/src/libslic3r/Support/` (directory listing)
- `/src/slic3r/` (directory listing)

---

### Key Discoveries

**Project Lineage**
OrcaSlicer is forked from Bambu Studio (itself forked from PrusaSlicer → Slic3r). This means the core slicing algorithms descend from the well-known Slic3r engine. Expect heavily layered C++ inheritance and accumulated design decisions from multiple generations.

**Build System**
- CMake 3.13+ required; C++17 standard enforced.
- Two major compilation units: `deps_src/` (vendored third-party code) and `src/` (application code).
- Static build is default (`SLIC3R_STATIC=1`).
- Key external libraries identified: Boost (1.83+), Intel TBB, Eigen3, Clipper (polygon ops), OpenVDB, CGAL (via admesh), NLopt, OpenGL/GLEW/GLFW, wxWidgets, OpenSSL, libcurl, Cereal, Freetype, OpenCASCADE (STEP via OCCT), expat (XML parsing), libpng, zlib.
- Conditional compilation flags: `SLIC3R_GUI`, `BBL_RELEASE_TO_PUBLIC`, `HAS_WIN10SDK`, `SLIC3R_ASAN`.

**Entry Point**
The primary entry point for the application is `src/OrcaSlicer.cpp` — a 7429-line monolithic file that handles both GUI startup (`SLIC3R_GUI` path) and headless CLI slicing. On Linux, a named pipe + mutex/condition variable mechanism sends JSON progress reports to a parent process.

**Source Tree Layout**
```
src/
  OrcaSlicer.cpp / .hpp   — entry point, CLI + GUI init
  libslic3r/              — core slicing library (no GUI deps)
    Format/               — file format parsers (STL, OBJ, 3MF, AMF, STEP, SL1, SVG)
    Fill/                 — infill pattern implementations (13+ patterns)
    GCode/                — G-code generation, post-processing, tool ordering
    Support/              — FDM support structure generation (traditional + tree)
    Arachne/              — Arachne variable-width perimeter generator
    SLA/                  — SLA (resin) print support
    Algorithm/            — generic geometry algorithms
    Geometry/             — geometry primitives and utilities
    Optimize/             — optimization algorithms (NLopt bindings)
    Shape/                — shape utilities
  slic3r/
    GUI/                  — wxWidgets + OpenGL GUI layer
    Utils/                — GUI utilities
    Config/               — configuration UI
  libvgcode/              — G-code visualization library
  dev-utils/              — platform-specific glue code
```

**Core Data Pipeline (high-level)**
1. File load → `Model` → `ModelObject` → `ModelVolume` → `TriangleMesh` (indexed_triangle_set)
2. `Print` orchestrates slicing: `PrintObject::slice()` → `TriangleMeshSlicer` → layers of `ExPolygons`
3. `PerimeterGenerator` / `Fill` classes produce `ExtrusionEntityCollection`
4. `GCode` + `GCodeWriter` emit final G-code text

**PrintObject Step Sequence (from Print.hpp)**
```
posSlice → posPerimeters → posEstimateCurledExtrusions → posPrepareInfill
→ posInfill → posIroning → posSupportMaterial → posSimplifyPath
→ posSimplifySupportPath → posDetectOverhangsForLift → posSimplifyWall
→ posSimplifyInfill
```

**Print-Level Step Sequence**
```
psWipeTower (= psToolOrdering) → psSkirtBrim → psGCodeExport → psConflictCheck
```

**Mesh Representation**
- `indexed_triangle_set` (from admesh): `std::vector<Vec3f> vertices` + `std::vector<Vec3i32> indices`
- `TriangleMesh` wraps this with stats (`TriangleMeshStats`) and repair error counts (`RepairedMeshErrors`)
- Coordinates stored in scaled integers at many processing stages (Clipper library requirement: coordinates are scaled by `SCALING_FACTOR = 1e6` converting mm to integer units)

**Slicing Architecture**
- `TriangleMeshSlicer` provides free functions `slice_mesh()` / `slice_mesh_ex()` operating on `indexed_triangle_set`
- Multiple slicing modes: `Regular`, `EvenOdd`, `Positive`, `PositiveLargestContour`
- Results are `std::vector<Polygons>` or `std::vector<ExPolygons>` (one per Z-slice)
- `ExPolygon` = outer contour + holes (both as `Polygon` = `std::vector<Point>`)

**Polygon Library**
Uses Clipper (Angus Johnson) for all 2D boolean operations (union, diff, offset, intersection). The codebase has both Clipper1 (`clipper.hpp`) and Clipper2 (`Clipper2Utils`) wrappers. Points use 64-bit integers after scaling.

**Concurrency**
- Intel TBB used heavily in slicing loops (`tbb::parallel_for`)
- `boost::thread` used in CLI pipe writer thread
- `std::mutex` / `std::condition_variable` for synchronization
- Critical: TBB task arenas may be created with fixed thread counts from config

**Multi-Material (BBL additions)**
`MultiMaterialSegmentation.cpp` handles painting-based filament assignment — a Bambu Lab addition on top of the PrusaSlicer lineage.

---

### Decisions Made
- Processing order: entry point → model/mesh data structures → slicing → toolpath → G-code output → format parsers → utilities
- Depth allocation: heavy on TriangleMeshSlicer, PerimeterGenerator, Fill, GCode; lighter on GUI layer and trivial utilities

### Open Questions
1. `[UNCLEAR]` Does `slice_mesh` use the admesh adjacency table or rebuild it? Need to examine `TriangleMeshSlicer.cpp` body.
2. `[UNCLEAR]` Exact memory ownership protocol for `Layer` objects: are they owned by `PrintObject` via `std::vector<Layer*>` (raw pointers) or `std::vector<std::unique_ptr<Layer>>`?
3. `[UNCLEAR]` How does the Arachne variable-width perimeter generator interact with the traditional `PerimeterGenerator`? Are they mutually exclusive?
4. `[UNCLEAR]` The `ClipperZUtils` / `Clipper2ZUtils` appear to tag intersection points with Z metadata — unclear purpose, likely for seam or feature detection.

---

## Session 2 — Annotation Phase (Iterations 2–3)

### Files Annotated and Committed

| Commit | Files | Key Findings |
|--------|-------|-------------|
| `eedbdf838f` | OrcaSlicer.cpp, TriangleMeshSlicer.cpp | Dual coordinate system; CLI::run() 6000-line coupling hazard; flush_and_exit macro |
| `02b4cb7b9e` | OrcaSlicer.cpp (CLI loops) | Per-plate G-code export; headless slice loop |
| `fd0738255e` | Print.cpp, PrintObject.cpp | Step state machine, invalidation DAG, PrintObjectStep cascade |
| `5f58029f8f` | PrintObjectSlice.cpp | posSlice pipeline; polyhole transform; brim grouping |
| `f071eee56b` | Format/STL.cpp, OBJ.cpp, AMF.cpp, 3mf.cpp, bbs_3mf.cpp | store_stl always returns true; AMF read-only; 3mf column-major transpose hazard |
| `c28a2db5f8` | PerimeterGenerator.cpp, Fill/Fill.cpp | Arachne loop_number+1 off-by-one; spiral forces classic; lightning raw pointer |
| `26474719cd` | GCode.cpp, GCodeWriter.cpp | TBB pipeline; e_per_mm double-apply; lazy_lift deferred hazard; toolchange sequence |

---

### Key Discoveries (Session 2)

#### PerimeterGenerator.cpp

**Algorithm dispatch (LayerRegion.cpp:120):**
- `process_classic()` — iterative Clipper polygon inset. Fixed-width perimeters. Dispatch when `spiral_vase` OR `wall_generator != Arachne`.
- `process_arachne()` — Arachne medial-axis/straight-skeleton. Variable-width perimeters. Only when `wall_generator == Arachne` AND `!spiral_vase`.

**Critical hazards discovered:**
- `loop_number+1` passed to Arachne (1-indexed) vs `loop_number` used by Classic (0-indexed). Off-by-one produces wrong wall count, no error.
- `precise_outer_wall` config only applied in `InnerOuter` wall sequence; silently ignored in `OuterInner` mode.
- Zero-length Arachne extrusion skip at ~L452 in `process_arachne()`. Removing this generates degenerate G-code.
- `split_top_surfaces()` must be called in both algorithms; a diff between the two causes top solid infill discrepancies.

#### Fill/Fill.cpp

**Infill pipeline:**
- `Layer::make_fills()` entry at line ~1192.
- `group_fills()` batches compatible surfaces to minimise filler object overhead.
- `Fill::new_from_type()` factory in `FillBase.cpp:40` — 25+ pattern classes dispatched by `InfillPattern` enum.
- `calculate_infill_rotation_angle()` implements an undocumented mini-metalanguage for layer-varying rotation (no tests).

**Critical hazards:**
- `FillLightning::Filler` stores raw pointer to `lightning_generator`. Fragile lifetime.
- `dynamic_cast` used for `FillConcentricInternal`/`FillConcentric`/`FillLightning::Filler` — silent nullptr if type mismatch.
- Surface grouping float comparison can prevent batching of geometrically identical surfaces.

#### GCode.cpp (7895 lines)

**Export pipeline:**
```
do_export() → _do_export() → process_layers() → [TBB pipeline]:
  generator(serial) → [spiral_mode] → [pressure_equalizer] → cooling → fan_mover → [pa_processor] → output
```

**TBB pipeline notes:**
- All filters are `serial_in_order` — not truly parallel; provides pipelining overlap only.
- 4 hardcoded pipeline variant expressions (spiral × pressure_equalizer). Combinatorial maintenance hazard.
- `pressure_equalizer` requires a NOP layer injected at end-of-stream (1-layer lookahead buffer).

**`_extrude()` (line ~6275):**
- e_per_mm chain: `mm3_per_mm × print_flow_ratio × filament_flow_ratio × role_ratio... / filament_flow_ratio`
- filament_flow_ratio applied twice (once via e_per_mm3()), then divided out. Fragile but intentional.
- Acceleration + jerk: 12-entry role-based lookup. Klipper gets combined `M204`/`M205`; others get separate commands.
- Speed: 15+ role cases, falls through to default if role not matched.

**`travel_to()` (line ~7207):**
- `needs_retraction()` → optional `AvoidCrossingPerimeters::travel_to()` → re-evaluate retraction on new path.
- `z` parameter defaults to `DBL_MAX` (no Z change). Accidentally passing `0.0` crashes nozzle into bed.

**`retract()` (line ~7517):**
- Wipe path split: `calculateWipeRetractionLengths()` → partial E before wipe + rest during wipe.
- Z-lift gated by `RetractLiftEnforceType` (AllSurfaces / TopOnly / BottomOnly / TopAndBottom).
- Hilbert Curve infill pattern special-cases suppress retraction — workaround, not general design.

**`set_extruder()` (line ~7594):**
- Multi-extruder: retract → `filament_end_gcode` → OozePrevention → T<n> → temperature → `filament_start_gcode` → prime → unretract.
- `filament_id` (logical slot) ≠ `extruder_id` (physical hardware). Must not be confused.
- `m_toolchange_count` incremented before retract — off-by-one on exception.

**Global hazards:**
- `travel_point_1/2/3` (line ~94) — file-scope globals for AMS cutter path. Not mutex-protected.
- `do_export()` early return if step already done + file exists — stale file never re-generated.
- PlaceholderParser failures are deferred until next layer boundary via `check_placeholder_parser_failed()`.

#### GCodeWriter.cpp (1202 lines)

**Key state:**
- `m_pos` — Vec3d in print coords (plate offset NOT included). Offset applied at emit time.
- `m_lifted` — applied Z-hop height. `m_to_lift` — pending deferred lift (lazy_lift).
- `Filament::retract()` tracks retraction state — `_retract()` returns empty string if already retracted.

**Critical hazards:**
- `lazy_lift()` deferred lift never emits if travel is cancelled. `unlift()` clears without emitting if `m_lifted == 0`.
- `use_firmware_retraction` forces `length = 1.0` to bypass zero-length skip. Mixing modes mid-print desync filament state.
- `full_gcode_comment` is a `static bool` — shared across all GCodeWriter instances (not thread-safe for multi-plate parallel export).

---

### Open Questions (Remaining)

1. `[UNCLEAR]` `calculate_infill_rotation_angle()` mini metalanguage grammar — no documentation, no tests. Needs dedicated analysis.
2. `[UNCLEAR]` `ClipperZUtils` Z-metadata tagging at intersections — purpose not yet determined. Possibly for seam detection.
3. `[UNCLEAR]` `g_max_label_object = 64` BBL firmware cap — not validated against firmware limits.
4. `[UNCLEAR]` How does `SeamPlacer` interact with `extrude_loop()`? Seam placement pre-computes positions but integration mechanism not yet traced.

---

### Next Annotation Targets (Priority Order)

1. `src/libslic3r/GCode/ToolOrdering.cpp` — which extruder/filament for which layer (critical for multi-material)
2. `src/libslic3r/GCode/CoolingBuffer.cpp` — fan speed / print speed adjustment per-layer
3. `src/libslic3r/GCode/SeamPlacer.cpp` — seam position selection algorithm
4. `src/libslic3r/GCode/WipeTower2.cpp` — wipe tower G-code generation
5. `src/libslic3r/Support/SupportMaterial.cpp` — support structure generation

---

## Session 3 — Annotation Phase (ToolOrdering, CoolingBuffer, SeamPlacer)

### Files Annotated and Committed

| Commit | Files | Key Findings |
|--------|-------|-------------|
| (session 3) | GCode/ToolOrdering.cpp | Extruder 0-vs-1-based hazard; const_cast UB; dead code; vector insertion hazard |
| (session 3) | GCode/CoolingBuffer.cpp + .hpp | G1-only removal bug; fan priority chain; per-line state machine |
| (session 3) | GCode/SeamPlacer.cpp + .hpp | 30k samples + 5×5 hemisphere raycasts; Gaussian+sigmoid angle penalty; multiple hazards |

---

### Key Discoveries (Session 3)

#### ToolOrdering.cpp

**Extruder ID indexing hazard:**
Throughout `ToolOrdering.cpp`, extruder IDs switch between 0-based (internal array indices) and 1-based (config/human-facing values) with no systematic conversion. The mix appears at least 4 times in `collect_extruders()` and `fill_wipe_tower_partitions()`. A refactor must introduce an explicit conversion layer.

**`const_cast` UB in `collect_extruders`:**
`collect_extruders()` receives a `const Print&` but casts away constness via `const_cast<Print&>` to call `PrintObject::invalidate_step()`. This is undefined behaviour if the original object was declared `const`. The mutation should be refactored to a non-const ref or a separate API.

**Dead code in `generate_first_layer_tool_order`:**
Several local variables are declared and computed but never used in `generate_first_layer_tool_order()`. Indicates copy-paste residue or partially-reverted refactoring.

**`fill_wipe_tower_partitions` vector insertion hazard:**
The function inserts new `LayerTools` entries into `m_layer_tools` by index (`std::vector::insert`) while iterating by index. If the vector reallocates, all indices are still correct (integer index), but the insert invalidates iterators if any are held. Currently safe (index-based), but fragile if iterators are ever stored.

**Timelapse hack:**
Smooth timelapse mode forces a wipe-tower visit on every layer that contains object content, regardless of whether a toolchange occurs. This is implemented as a synthetic toolchange (same extruder in/out) in `fill_wipe_tower_partitions()`.

#### CoolingBuffer.cpp

**Fan priority chain (highest to lowest):**
1. Overhang fan speed
2. Internal bridge fan speed
3. Support interface fan speed
4. Ironing fan speed
5. Force-resume fan (from previous layer's bridge)
6. Base fan speed (from cooling settings)

**Pre-existing PrusaSlicer G1-only removal bug:**
`CoolingBuffer` attempts to remove redundant G1 moves from the buffer, but the check scans the entire buffer rather than just the last line. On large buffers this can match incorrect lines and produce corrupted G-code. This is a known upstream bug inherited from PrusaSlicer.

**Per-line state machine:**
Each G-code line is parsed into a `CoolingLine` struct with flags (`TYPE_G1`, `TYPE_G4`, `TYPE_SET_TOOL`, etc.). The state machine processes lines in order, accumulating time-on-layer for fan ramp calculations.

#### SeamPlacer.cpp

**Algorithm overview:**
1. Sample 30,000 points on the mesh surface (Poisson-disk sampled).
2. For each sample, cast 25 rays (5×5 hemisphere grid) to compute a visibility score.
3. Per-perimeter loop: score each vertex using weighted visibility + angle penalty.
4. Placement: pick lowest-scored (most concave, least visible) vertex.

**`compute_angle_penalty()` — Gaussian + sigmoid:**
Negative angles (concave corners) get very low penalty; positive angles (convex) get high penalty via a combined Gaussian trough + sigmoid ramp. This biases seam placement toward concave features.

**`Frame` class:**
Builds an orthonormal coordinate frame from a surface normal vector, used to rotate hemisphere ray directions into world space.

**`end_index` comment hazard:**
The `end_index` field on `Perimeter` struct is commented "inclusive!" but all code treats it as exclusive (past-the-end). The comment is wrong and would mislead a refactor.

**`seam_align_mm_per_segment` type mismatch:**
Declared `size_t` but initialized with float literal `4.0f`. The float is silently truncated to `size_t(4)`. If the design intent was fractional values, this is a silent precision loss.

**Dead code: `sample_sphere_uniform` and `sample_power_cosine_hemisphere`:**
These two sampling functions exist in the file but are never called — only `sample_hemisphere_uniform` is used. They are likely development artifacts.

**`spAlignedBack` visibility score > 1.0:**
In `spAlignedBack` mode, the code pushes the visibility score above 1.0 to override other placement modes. This breaks the [0,1] unit interpretation of visibility scores and adds implicit mode coupling.

**`calculate_point_visibility()` weight sign hazard:**
Weights used in visibility accumulation can go negative for geometry where sample normals are off-plane. Negative visibility would incorrectly bias toward visible areas.

**Staggered inner seam infinite-loop risk:**
The staggered-inner-seams walk iterates over loop segments; if a segment has zero length, the code can loop indefinitely. Protected by a length check but the check condition was fragile.

**B-spline alignment Z-coordinate assumption:**
`align_seam_points()` uses the Z coordinate as the spline parameter, assuming Z increases monotonically within each seam string. This fails for vase-mode or non-monotonic printing orders.

**`align_seam_points()` subtle decrement:**
A `global_index--` decrement at the end of the B-spline loop is semantically correct (re-processes the boundary point) but appears to be an off-by-one infinite-loop risk to future maintainers.

---

### Open Questions (Session 3)

1. `[UNCLEAR]` How does `CoolingBuffer`'s "G1-only removal" interact with pressure-advance G-code sequences? PrusaSlicer bug may manifest differently under OrcaSlicer's PA processor.
2. `[UNCLEAR]` The 30,000 sample count in SeamPlacer is hardcoded. Is this tuned for typical model sizes? Very large models may have insufficient coverage; very small models waste memory.
3. `[UNCLEAR]` `spAlignedBack` score > 1.0 override — is this intentional design or an undocumented hack? No comment explaining the rationale.

---

## Session 4 — Annotation Phase (WipeTower2)

### Files Annotated and Committed

| Commit | Files | Key Findings |
|--------|-------|-------------|
| `e6211edf43` | GCode/WipeTower2.cpp + WipeTower2.hpp | Two-phase SEMM/multi-tool architecture; 5-iteration planning loop; MK4MMU3 cold ramming; global static side effect |

---

### Key Discoveries (Session 4)

#### WipeTower2.cpp + WipeTower2.hpp

**Architecture (two-phase):**
- **Phase 1 (Planning):** `plan_toolchange()` builds `m_plan`; `plan_tower()` propagates depths; `save_on_last_wipe()` steals finish_layer volume from last wipe. `generate()` runs this loop 5 times to convergence.
- **Phase 2 (Generation):** `generate()` iterates `m_plan`, calling `tool_change()` (→ Unload→Change→Load→Wipe) + `finish_layer()` for each layer. Results are merged via `merge_tcr()`.

**SEMM vs. multi-tool-head divergence:**
- SEMM: full ramming + cooling tube retract + back-and-forth cooling moves + parking dwell + load moves.
- Multi-tool-head: firmware handles load/unload; WipeTower2 only does `[change_filament_gcode]` placeholder + wipe.

**`GCodeProcessor::s_IsBBLPrinter = false` (global static side effect):**
WipeTowerWriter2's constructor sets this global static, changing G-code output format for the entire process. Any BBL-flavoured printer passing through WipeTower2 gets its processing mode overridden.

**`construct_tcr()` always initializes `tool_change_start_pos`:**
ORCA fix: the original PrusaSlicer code left this field undefined in certain paths, causing undefined-variable travel in `post_process_wipe_tower_moves`. Now always initialized to `start_pos`.

**MK4MMU3 cold ramming path:**
Before ramming, temperature is dropped by 20°C via a blocking `M109` wait. This stiffens the filament tip for cleaner ramming but adds latency to each toolchange.

**Stamping (MK4MMU3):**
Between cooling moves, the filament is pushed ("stamped") into the melt zone then retracted. This distributes potential blobs along the full tower width rather than concentrating them.

**`plan_tower()` O(n²) depth propagation:**
For each layer, all prior layers are scanned to enforce minimum depth constraints. Acceptable for typical print counts but may be slow for very tall prints (>2000 layers with many toolchanges).

**Wipe speed ramp:**
`toolchange_Wipe()` uses a hardcoded multi-threshold step function to ramp from 33% to 100% of target speed. This is calibrated for expected extrusion pressure behaviour and must be re-tuned for different nozzle/filament combinations.

**ORCA spacing fix:**
`plan_toolchange()` and `save_on_last_wipe()` both apply the same spacing correction: first layer uses `m_extra_flow` spacing, later layers use `m_extra_spacing_wipe`. Without this fix, the reserved tower depth mismatches the consumed depth on the first layer.

**Wall types:**
- `wtwNormal`: plain rectangle perimeter.
- `wtwRib`: diagonal X-brace ribs (computed by `generate_rib_polygon()`).
- `wtwCone`: tapered cone base for stability at height (computed by `generate_support_cone_wall()`).

**Brim generation:**
On the first layer, `finish_layer()` extrudes concentric brim loops using `Polygon::offset()`. The actual brim width is saved to `m_wipe_tower_brim_width_real` for use by the skirt/brim planner.

**`save_on_last_wipe()` dry-run hazard:**
Calls `tool_change()` and `finish_layer()` as a dry run just to measure `total_extrusion_length_in_plane()`. The generated G-code strings are discarded, but all side effects of `WipeTowerWriter2` accumulation run. If writer side effects ever include non-discardable state, this will silently corrupt output.

---

### Open Questions (Session 4)

1. `[UNCLEAR]` `#if 1` guard around the 5-iteration loop in `generate()` — suggests this was temporarily disabled during debugging and never cleaned up. Is 5 iterations always sufficient for convergence?
2. `[UNCLEAR]` `first_toolchange_to_nonsoluble()` ORCA change always returns index 0 (not -1) for non-wipe-tower-filament configs. This means finish_layer is always merged with the first toolchange, not the first non-soluble one. Intent unclear.
3. `[UNCLEAR]` `m_internal_rotation` always flips 180° but `m_current_shape` is always `SHAPE_NORMAL` (alternating logic commented out). Are rotation and fill-direction supposed to work together? Current state is inconsistent.

---

### Next Annotation Targets

1. `src/libslic3r/Support/SupportMaterial.cpp` — traditional pillar support generation
2. `src/libslic3r/Support/TreeSupport.cpp` — organic tree support generation

---

## Session 5 — Annotation Phase (SupportMaterial.cpp + SupportMaterial.hpp)

### Files Annotated and Committed

| Commit | Files | Key Findings |
|--------|-------|-------------|
| (session 5) | Support/SupportMaterial.cpp + SupportMaterial.hpp | 97 annotation tags; AGG rasterizer always-on; serial orchestration pipeline; sharp-tail detection; OverhangCluster hazards; dead Perl-era code |

---

### Key Discoveries (Session 5)

#### SupportMaterial.cpp Architecture

**`SUPPORT_USE_AGG_RASTERIZER` hardcoded ON:**
The preprocessor macro `SUPPORT_USE_AGG_RASTERIZER` is `#define`-d unconditionally at the top of the file. The entire EdgeGrid-based `SupportGridPattern` fallback code path is `#ifdef`-guarded and **never compiled** in any production build. The EdgeGrid path exists solely as dead historical code.

**`SupportGridPattern` class — two live modes:**
1. `smsGrid` — AGG scanline raster fill → marching-squares vectorization.
2. `smsSnug` — morphological closing (polygon offset expand+shrink) to produce tight-fitting support outlines.
- Tree-style variants (`smsTreeSlim`, `smsTreeStrong`, `smsTreeHybrid`, `smsOrganic`) all `assert(false)` and return empty — they are dispatch stubs that route to `TreeSupport`, not implemented in `SupportGridPattern`.

**`SupportGridParams::support_closing_radius` hardcoded to `2.0`:**
The config field for this value is commented out in `PrintConfig.hpp`. The struct member is populated with a literal `2.0` in the constructor. Any attempt to make this user-configurable requires uncommenting the config field and adding an invalidation entry.

**`rasterize_polygons()` — AGG anti-aliased scanline rasterizer:**
Polygon interiors are rendered into a `uint8_t` byte grid using AGG's `agg::renderer_scanline_aa_solid`. Each byte represents one cell of the support grid. Cells with value > `threshold_alpha` (128) are considered "inside" support coverage.

**`contours_simplified()` — marching-squares re-vectorization:**
The byte grid is converted back to polygons. Optional hole-fill suppresses inner contours below a size threshold. Corner offset logic in `contours_simplified()` assumes strictly axis-aligned (rectilinear) input polygons — diagonal grid cells produce slightly off-axis corners.

**`seed_fill_block()` — PROPAGATION_STEP macro flood fill:**
Flood fill propagation is implemented via a C macro `PROPAGATION_STEP` that expands inline 4-directional propagation steps within oversampled macro-blocks. The fill is bounded by a dilated trimming mask (the object body expanded outward). Oversampling factor is `SUPPORT_GRID_OVERSAMPLING = 4` — the byte grid is 4× the logical support grid resolution.

#### SupportMaterial `generate()` Pipeline

The main orchestration function `generate()` is **fully serial** (no top-level parallelism). Its 11 sub-steps:

1. `detect_overhangs()` — find regions exceeding angle threshold; BBS sharp-tail detection pass
2. `detect_contacts()` — compute contact polygon geometry for each overhang
3. `new_contact_layer()` — allocate `SupportLayer` objects for each contact z
4. `bottom_contact_layers_and_layer_support_areas()` — bottom contacts + downward projection
5. `generate_base_layers()` — propagate support body from contacts to build plate
6. `generate_interface_layers()` — dense interface layers at model contact surface
7. `generate_base_interface_layers()` — lighter interface layers below dense interface
8. `trim_support_layers_by_object()` — remove support volume that intersects object body
9. `generate_support_toolpaths()` — fill support layers with extrusion paths
10. `generate_raft_base()` — optional raft layers
11. `buildplate_covered()` — cumulative mask of already-supported areas

#### BBS Sharp-Tail Detection

Two-pass algorithm in `detect_overhangs()`:

**Pass 1 (parallel, per-layer):**
- Layer 0: Any small-footprint polygon (area < `sharp_tail_min_area = 0.5 mm²`) is marked as a sharp tail seed.
- Layer N > 0: An expolygon is a "floating" sharp tail if its bbox centroid lies over empty space on the layer below and its area is small enough.

**Pass 2 (serial, upward):**
- For each layer, check if a polygon's bbox intersects a known sharp-tail region below.
- If yes, propagate sharp-tail membership upward if:
  - Bbox width AND height are both < `sharp_tail_max_support_width = 2.5 mm`
  - Area growth rate < 50% per layer
  - Accumulated height < `sharp_tail_max_support_height = 16 mm`
- Sharp-tail regions get enforced bottom-contact support regardless of overhang angle.

#### `OverhangCluster` Hazards

`OverhangCluster` is a struct that clusters adjacent overhang polygons across layers. Two hazards:

1. **O(N²) scan hazard:** Membership test uses a linear scan over all existing clusters per new polygon. For models with many small overhangs (e.g., lattice structures), this becomes O(N²) where N = overhang polygon count.

2. **Dangling raw pointer hazard:** `OverhangCluster` stores `ExPolygon*` raw pointers into a `std::vector<ExPolygon>` that is built during `detect_overhangs()`. The vector is not modified after construction (safe currently), but any reallocation of that vector would invalidate all stored pointers silently.

#### `detect_overhangs()` vs. `detect_contacts()`

Clean separation of concerns:
- `detect_overhangs()`: Answers "what regions need support?" — produces a set of `ExPolygon` overhang regions per layer.
- `detect_contacts()`: Answers "what is the contact geometry?" — given overhang regions, computes the actual contact polygon (clipped, expanded, contracted) that the support will touch.

The separation allows the overhang detection to be reused by visualisation tools without triggering full support generation.

#### `sync_gap_with_object_layer()`

Walks the linked-list of `Layer` objects (via `Layer::lower_layer` pointer) to snap each contact z-coordinate to the nearest object layer boundary. This prevents support layers from being inserted at z-heights that don't align with object layers, which would cause seam artifacts.

#### `new_contact_layer()`

May allocate **two** `SupportLayer` objects per object layer when `thick_bridges` config is enabled (one for normal flow, one for bridging flow). Returns `nullptr` if no contact is needed. Callers must null-check the return value.

#### `bottom_contact_layers_and_layer_support_areas()`

Parallel processing with a key ordering subtlety:
- Iterates layers **top-to-bottom**.
- Runs `detect_bottom_contacts()` and `project_support_to_grid()` as concurrent `tbb::task_group` pairs for each layer.
- The resulting `bottom_contacts` vector is built in **descending z order**, then `std::reverse()`-d at the end to produce ascending order.

#### `generate_base_layers()`

Parallel fill using `tbb::parallel_for` over layers. Each thread maintains **three binary-search cache indices** that count downward to avoid O(log N) re-searches for already-visited layers. These caches are thread-local (on the stack inside the lambda).

#### `trim_support_layers_by_object()`

Three distinct XY gap zones:
- **Sharp-tail regions:** `sharp_tail_xy_gap = 0.2 mm` (smaller gap — support closer to model)
- **Normal overlap regions:** `gap_xy_scaled` (from config)
- **Non-overlap / floating regions:** `no_overlap_xy_gap = 0.2 mm`

#### `buildplate_covered()`

Serial cumulative union — each layer's covered area is the union of the current layer's object footprint and the layer below's covered area. The code has a `// FIXME` comment noting this should be a **parallel prefix-sum** but is not. For tall prints (>500 layers), this serial pass may be a bottleneck.

#### Dead / Commented-Out Code at EOF

A large block of commented-out C++ at the end of `SupportMaterial.cpp` corresponds to `clip_by_pillars()` and `clip_with_shape()` functions from the Perl-era Slic3r codebase. They were never ported to OrcaSlicer's C++ data structures. Safe to delete entirely in a rewrite.

---

### Open Questions (Session 5)

1. `[UNCLEAR]` `SUPPORT_USE_AGG_RASTERIZER` — is the EdgeGrid path intentionally preserved for future fallback, or is it safe to delete? The macro is unconditional so no build currently exercises it.
2. `[UNCLEAR]` `smsTreeSlim` / `smsTreeStrong` / `smsTreeHybrid` / `smsOrganic` dispatch stubs in `SupportGridPattern` — do these ever reach the assert(false), or does the caller always redirect to `TreeSupport` before calling `SupportGridPattern`? Need to trace caller.
3. `[UNCLEAR]` `support_closing_radius = 2.0` hardcode — is `2.0 mm` correct for all nozzle diameters? Fine for 0.4 mm nozzle, but for 0.8 mm nozzle this may produce over-thick support interfaces.
4. `[UNCLEAR]` `buildplate_covered()` serial FIXME — is the serial order intentional (each layer's output depends on the layer below) or is a parallel-prefix approach actually safe here?

---

### Next Annotation Targets

1. `src/libslic3r/Support/TreeSupport.cpp` — organic tree support generation (3527 lines)
2. `src/libslic3r/Support/TreeSupport.hpp`

---

## Session 6 — TreeSupport.cpp + TreeSupport.hpp Full Annotation

### Files Processed
- `src/libslic3r/Support/TreeSupport.hpp` (~600 lines)
- `src/libslic3r/Support/TreeSupport.cpp` (~3527 lines)

---

### Key Discoveries

**Organic Mode Bypass**
`TreeSupport::generate()` checks `smsTreeOrganic` first. If active, it immediately delegates to `generate_tree_support_3D()` (in `TreeSupport3D.cpp`) and returns. `TreeSupport.cpp` only handles `smsTreeSlim`, `smsTreeStrong`, and `smsTreeHybrid` variants.

**`USE_SUPPORT_3D` Macro Hardcoded to 0**
`#define USE_SUPPORT_3D 0` at the top of the file. Every `#if USE_SUPPORT_3D` branch is dead code. Functions like `get_avoidance()`, `get_collision()`, and `get_collision_polys()` all always take the `#else` path. The dead branches can be removed entirely in a port.

**`SupportNode` Struct**
Single node in the branch graph. `radius` and `max_move_dist` are `mutable double`, updated lazily. `diameter_angle_scale_factor` is a `static double` — class-wide constant that is **NOT thread-safe** if two `PrintObject` instances are sliced concurrently with different configs.

**`TreeSupportData` — Collision/Avoidance Cache**
Per-object collision/avoidance cache using `tbb::concurrent_unordered_map<RadiusLayerPair, Polygons>`. `calculate_avoidance()` is recursive, pre-computing layer N-100 to cap call depth. The constructor builds `m_layer_outlines_below` as serial O(N²) cumulative union (FIXME noted in code — should be parallel prefix-sum).

**`detect_overhangs()` — Two-Pass + Timeout Escape**
Two TBB parallel passes over layers, followed by serial sharp-tail propagation and serial overhang-cluster grouping. Contains a wall-clock timeout escape hatch: if `config_detect_sharp_tails` detection takes >30 seconds, it forcibly disables the flag mid-run. This means prints that approach the timeout threshold may produce inconsistent support depending on machine speed.

**`generate_contact_points()` — Rotated Grid**
TBB parallel_for. Generates a 22°-rotated grid of candidate contact points over the overhang region. Uses `tbb::spin_mutex` for thread-safe insertion into the shared node list. Grid rotation is a deliberate design choice to avoid aligning with model geometry.

**`plan_layer_heights()` — Negative Sentinel**
Pre-plans adaptive support layer heights. After planning, remaps contact_nodes to new support layer indices. Uses `node->distance_to_top = -num_layers` as a sentinel encoding a multi-layer gap. All consumers of `distance_to_top` must handle negative values correctly.

**`drop_nodes()` — Fully Serial, O(N²) Hazard**
The most performance-critical phase. Proceeds top→bottom, one layer at a time (fully serial). Before starting, pre-computes avoidance in a TBB parallel_for. Groups nodes by ExPolygon island ("part"). Uses MST per group to decide merge candidates. Two passes per layer: (1) merge nearby nodes, (2) move nodes toward neighbors/outside. `insert_dropped_node()` uses `std::find` — O(N) per call, making the overall drop O(N²) on dense-support layers.

**`smooth_nodes()` — Laplacian 100 Iterations**
100 iterations of Laplacian smoothing along each branch chain. Sets `need_extra_wall` flag on nodes that shifted significantly during smoothing.

**`draw_circles()` — TBB Parallel, Dangling Pointer Risk**
TBB parallel_for over layers. Pre-generates a `branch_circle` polygon (100 vertices normally, 4 vertices if avg_node_per_layer > 200 — performance fallback). After the main loop, a hole-propagation loop uses raw `Polygon*` as keys in `holePropagationInfos` map. If the underlying vector reallocates between iterations, these pointers dangle.

**`generate_toolpaths()` — TBB Parallel**
TBB parallel_for over non-raft layers. Handles raft layers, raft interface layers, base layers between raft and object, then normal tree support layers. Uses `make_perimeter_and_inner_brim()` for roof/floor interface layers, `make_perimeter_and_infill()` for base layers.

**`TreeSupportProfiler` Global Instance**
File-scope global `profiler` at line 156. Tracks timing data for the pipeline stages. NOT thread-safe if two `PrintObject` instances are processed concurrently — both would write to the same profiler instance.

**`get_radius()` — Lazy Mutable Cache UB**
`SupportNode::radius` is `mutable`, computed lazily. If two threads simultaneously read a node with an unset radius, both compute the same value and write it. Practically benign (same value), but technically undefined behavior under the C++ memory model (concurrent write to non-atomic memory without synchronization).

**`create_node()` — Raw lock/unlock**
Acquires `m_mutex.lock()` at top of function, releases with `m_mutex.unlock()` at bottom. If any exception is thrown between the lock and unlock, the mutex is never released — permanent deadlock. Should be replaced with `std::lock_guard` or `std::scoped_lock`.

**`_make_loops()` Concentric Shrinking**
Generates concentric wall loops by iteratively shrinking ExPolygons using `offset2_ex` (shrink + expand to prevent topology collapse). Stops when the shrunk result is empty.

**`move_out_expolys()` Dead Variable**
Local variable `from0` is assigned at the start of the function but is never referenced again after the assignment. Dead code.

**`avoid_object_remove_extra_small_parts()`**
After clipping a candidate support circle against the avoidance region, keeps only the single largest ExPolygon. Small satellite fragments are discarded to prevent spurious tiny support islands.

**`move_bounds_to_contact_nodes()`**
Bridge function from TreeSupport3D organic mode back into this class's node graph — used purely for preview/visualization purposes, not for actual toolpath generation.

**`calc_branch_radius()` Two Overloads**
One takes layer count, one takes mm distance. Both implement the same taper logic: `slim` mode doubles the `tip_layers` taper range. Both clamp output to `[MIN_BRANCH_RADIUS, MAX_BRANCH_RADIUS]`.

---

### Open Questions (Session 6)

1. `[UNCLEAR]` `diameter_angle_scale_factor` static — is multi-object concurrent slicing with different tree-support configs actually supported? If so, this is a silent correctness bug, not just a theoretical hazard.
2. `[UNCLEAR]` `holePropagationInfos` map with `Polygon*` keys — the vector being pointed into is `branch_circle` variants pre-computed per node. Is the vector guaranteed not to reallocate between the main loop and the hole-propagation loop? Needs confirmation.
3. `[UNCLEAR]` `plan_layer_heights()` negative sentinel — which downstream consumers of `distance_to_top` check for negatives? Any that skip the check could misinterpret a gap-encoded node as a normally-positioned node.
4. `[UNCLEAR]` `config_detect_sharp_tails` timeout at 30s — what happens to the already-processed layers when the flag is disabled? The partial state may leave some layers with sharp-tail annotations and others without, producing mixed output.

---

### Next Annotation Targets

1. `src/libslic3r/Support/TreeSupport3D.cpp` — organic 3D tree support (delegated from TreeSupport.cpp for smsTreeOrganic)
2. `src/libslic3r/GCode/PressureEqualizer.cpp` — linear advance / pressure equalizer post-processor

---

## Session 7 — TreeSupport3D.cpp Annotation Completion

**Files:** `src/libslic3r/Support/TreeSupport3D.cpp`, `src/libslic3r/Support/TreeSupport3D.hpp`
**Commit:** `3a3ba01e87`

### Functions Annotated This Session

**`increase_single_area()`** (line ~1709)
Core workhorse for the influence-area propagation loop. Takes a parent element, one `AreaIncreaseSettings` configuration, and probes whether a viable area exists on the layer below. Returns `std::optional<SupportElementState>` — the caller (`increase_areas_one_layer`) tries multiple settings in priority order and takes the first success. Three hazards documented:
- Radius expansion inner loop (`getRadiusNextCeil`) can be infinite if the lookup table is non-monotone (a TreeModelVolumes bug)
- `planned_foot_increase` divides by `foot_radius_increase`; if bp ≤ branch radius, `foot_radius_increase` = 0 and the result is ±infinity — `increase_bp_foot` is always false but the division itself is UB
- `ceil_radius_before` recheck path re-runs avoidance subtraction, potentially shrinking the area to below threshold and logging "Area lost catching up radius"

**`triangulate_fan<flip_normals>()`** (line ~2965)
Fills a triangle fan (cone cap) from an apex vertex to a ring of vertices. Used to close the bottom and top hemispheres of each organic branch tube. `flip_normals=false` for bottom (face outward downward), `true` for top. Purely additive to `indexed_triangle_set`.

**`triangulate_strip()`** (line ~2982)
Stitches two circular rings into a quad-strip of triangles. Alignment: finds closest vertex on ring2 to first vertex of ring1, then zig-zag walks with greedy shortest-diagonal. Ring-size mismatch produces a non-manifold mesh but does not crash.

**`discretize_circle()`** (line ~3048)
Tessellates a 3D circle into a polygon ring. Two hazards:
- **DEGENERATE NORMAL**: `x = normal × (0,-1,0)`. If `normal ≈ (0,±1,0)`, cross product → near-zero → NaN after `normalized()`. All emitted vertices become NaN, silently producing a garbage mesh. No guard present.
- **ZERO RADIUS**: If `radius ≤ 0`, `acos(1 - eps/radius)` goes out of range; `nsteps` could be ≤ 0; the ring emits nothing, causing downstream `triangulate_strip` assertion failures.

**`extrude_branch()`** (line ~3074)
Generates the full 3D mesh tube for one branch path: bottom hemisphere + body cylinders + top hemisphere. Normal at junctions is the bisector of adjacent segment directions (`(v1+v2).normalized()`). Two hazards:
- If `result_on_layer` differences are zero at any step (duplicate XY positions), `v1` becomes NaN.
- Dead `#if 0` block for `circles_intersect()`: when adjacent cross-sections overlap (rapidly widening branch near build plate), the tube can self-intersect. The fix was designed but never completed.

**`organic_smooth_branches_avoid_collisions()` — new AABB version** (line ~3174)
100-iteration TBB-parallel sphere-nudging + Laplacian smoothing using per-layer AABB line trees. Three hazards:
- `min_element_radius` is always overwritten to 0 immediately after being computed (FIXME comment at line ~3212). This forces the maximally conservative collision polygon rather than the tightest fit for the actual sphere radius.
- Nudge formula at line ~3337-3338 applies `nudge_dist` twice: `position += (nudge_vector * nudge_dist)` where `nudge_vector` is already `normalized() * nudge_dist`. This squares the nudge — a likely bug.
- Neighbor lookup via `linear_data_layers` offsets has no bounds-checking.

**`organic_smooth_branches_avoid_collisions()` — old OpenVDB version** (line ~3386, dead code)
Marked as DEAD CODE — `#else TREE_SUPPORT_ORGANIC_NUDGE_NEW` branch is never compiled. Serial loop, uses full OpenVDB SDF construction, slow for complex meshes. Retained as algorithm reference.

### Key Findings — Session 7

1. **`discretize_circle()` degenerate-normal hazard** — the `(0,-1,0)` cross product axis produces NaN for vertical branches. This would manifest as invisible support structures that print as nothing.
2. **Nudge distance double-applied** — the new AABB smoothing may be nudging nodes less than intended due to squaring of the distance. This could explain residual collisions that require many iterations.
3. **`min_element_radius = 0` FIXME** — all collision lookups use zero radius. The original intent was to use the smallest sphere radius at each layer for a tighter fit. This is the most impactful known unfixed bug in the organic support pipeline.
4. **OpenVDB dead code preserved** — the old version is kept as a commented-out reference. When porting to another language, it can be safely removed unless OpenVDB integration is desired.

### Open Questions (Session 7)

1. `[UNCLEAR]` Is the `nudge_dist` double-application at line ~3338 intentional? The multiplied `nudge_dist` vs `nudge_dist²` could explain the 100-iteration convergence being needed.
2. `[UNCLEAR]` `discretize_circle()` — is the `(0,-1,0)` choice deliberate (it works for all non-vertical normals)? Or was it meant to be `(0,0,1)` which is safer for the typical near-horizontal branch normal?
3. `[UNCLEAR]` The `extrude_branch()` dead `circles_intersect` block — would reactivating it require significant geometry work? The existing `triangulate_strip` zig-zag should handle mild overlaps without artifacts.

---

### Next Annotation Targets (Session 8+)

1. `src/libslic3r/Support/TreeModelVolumes.cpp` — collision/avoidance volume cache used by TreeSupport3D
2. `src/libslic3r/GCode/PressureEqualizer.cpp` — linear advance / pressure equalizer post-processor

---

## Session 8 — PressureEqualizer + TreeModelVolumes full annotation

**Files annotated:**
- `src/libslic3r/GCode/PressureEqualizer.cpp` (1060 lines after annotations)
- `src/libslic3r/GCode/PressureEqualizer.hpp` (260 lines after annotations)
- `src/libslic3r/Support/TreeModelVolumes.cpp` (additional constructor + batch dispatcher)

---

### PressureEqualizer Architecture

PressureEqualizer is a G-code post-processor inserted in the TBB pipeline between the per-layer G-code generator and CoolingBuffer. It enforces a user-configurable maximum volumetric extrusion rate slope (mm³/s²) to prevent abrupt pressure changes in the hot end.

**Pipeline position:** GCode.cpp TBB stage → PressureEqualizer → CoolingBuffer

**1-layer lookahead:** The public `process_layer(LayerResult&&)` overload buffers the current layer and emits the previous one. The caller must inject a NOP `LayerResult` at end-of-print to flush the final layer.

**In-band tags:** GCode.cpp injects plain-text comment tags into the G-code stream to communicate with PressureEqualizer:
- `;_EXTRUSION_ROLE:N` — update current role; line is consumed and NOT forwarded
- `;_EXTRUDE_SET_SPEED` — open an adjustable-flow block
- `;_EXTRUDE_END` — close an adjustable-flow block
- `;_EXTERNAL_PERIMETER` — tag next G1 line for Klipper PA awareness

Only G1 lines inside an open `EXTRUDE_SET_SPEED` block have `adjustable_flow=true` and can have their feedrate modified.

**Algorithm per layer (process_layer(string)):**
1. Parse each line → `GCodeLine` struct; EXTRUSION_ROLE_TAG lines consumed silently
2. Find contiguous extrusion segments, optionally bridging ≤3 mm travel gaps
3. For each segment, apply sliding-window `adjust_volumetric_rate(start, end)`:
   - Backward pass: propagate deceleration constraints backward from slow segments
   - Forward pass: propagate acceleration constraints forward from slow segments
4. `output_gcode_line()` re-serialises each modified line, splitting into sub-segments if needed

**Three emission modes in output_gcode_line():**
- Case A (trivial delta < 10 mm³/min): single line, average feedrate
- Case B (accel-peak-decel): quadratic solver → accel + steady + decel sub-segments
- Case C (`single_slope_fallback`): monotone ramp, linear interpolation

**Key hazards found:**
- `goto single_slope_fallback` at line ~618 — only goto in the G-code pipeline
- `m_layer_results` is public raw-pointer queue — manual memory management
- feedrate quantized to 1 mm/s steps — sub-mm/s adjustments silently discarded
- `is_just_line_with_extrude_set_speed_tag()` has inverted `empty()` check — latent logic bug
- `output_buffer` never shrinks — peak memory from largest layer retained
- Z-hop ignored by `advance_segment_beyond_small_gap()` when computing gap distance
- `volumetric_correction_avg()` upper bound is 1.00000001 — allows tiny super-correction

**Key constants (not configurable):**
- `max_look_back_limit = 128` lines
- `max_ignored_gap_between_extruding_segments = 3` mm
- `NON_TRIVIAL_RATE_DELTA = 10` mm³/min

---

### TreeModelVolumes Session 8 Additions

**Constructor hazard cross-reference:**
The constructor populates `m_anti_overhang` from `slice_support_blockers()`. Due to the `processing_last_mesh` bug in `calculateCollision()` (documented in Session 8 annotation), anti_overhang is never actually applied for single-mesh prints, making support blocker meshes functionally ineffective in the tree support path.

**`calculateCollision` batch dispatcher:**
`calculateCollision(vector<RadiusLayerPair>)` is a thin outer TBB parallel_for wrapper. The real work happens in `calculateCollision(coord_t, LayerIndex)`. Both are now annotated.

---

### Key Findings — Session 8

1. **goto in G-code pipeline** — `PressureEqualizer.cpp:~618` contains the only `goto` in the entire G-code generation pipeline. It is a degenerate-case fallback in the accel-peak-decel solver. Must be refactored to early-return into a helper before any port.

2. **Public raw-pointer queue** — `m_layer_results` in PressureEqualizer is publicly accessible, directly coupled to GCode.cpp's TBB pipeline. Both ownership and lifecycle of `LayerResult*` pointers depend on correct ordering of pipeline stages and the mandatory end-of-print NOP injection.

3. **is_just_line_with_extrude_set_speed_tag() bug** — inverted `empty()` check at line ~978. Functionally harmless today (if truly empty, function still returns false), but the code reads as backwards and is a maintenance hazard.

4. **anti_overhang silently skipped** — the `processing_last_mesh` bug (outline index == mesh count, not last-in-order) prevents support blockers from taking effect in tree supports for all single-mesh prints. A silent correctness issue.

5. **1 mm/s feedrate quantization** — `push_line_to_output()` rounds feedrate to nearest 60 mm/min. Very gradual rate ramps (< 1 mm/s steps) are flattened to plateaus. This is intentional for G-code size but limits smoothing precision.

---

### Open Questions (Session 8)

1. `[UNCLEAR]` `adjust_volumetric_rate()` has commented-out cross-role rate-propagation code at lines ~840 and ~900. The current code always uses `line.volumetric_extrusion_rate_start` regardless of iRole match. Was the per-role tracking intentionally removed or accidentally commented out?

2. `[UNCLEAR]` `volumetric_correction_avg()` upper bound 1.00000001 — is this a deliberate floating-point rounding allowance, or could it admit genuine rate increases? No logic in adjust_volumetric_rate() sets start/end above the nominal rate.

3. `[UNCLEAR]` `extrusion_rate_smoothing_external_perimeter_only` — when true, only `erExternalPerimeter` and `erOverhangPerimeter` are smoothed. Bridge infill is excluded regardless. Is bridge exclusion intentional (bridges use different mechanics) or an oversight?

---

### Next Annotation Targets (Session 9+)

1. `src/libslic3r/TriangleMeshSlicer.cpp` — core mesh-to-layer-contours algorithm (high priority)
2. `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` — travel path optimization
3. `src/libslic3r/GCode/GCodeProcessor.cpp` — G-code simulation and statistics
4. `src/libslic3r/GCode/FanMover.cpp` — fan control post-processor
5. `src/libslic3r/Arachne/` — variable-width perimeter engine internals

---

## Session 9

**Date:** 2026-03-07
**Branch:** agent/analysis
**Files annotated:** `src/libslic3r/TriangleMeshSlicer.cpp` (final 4 functions — full file now complete)
**Commits this session:** 1 source annotation commit, 1 docs commit

### Scope

Session 9 completed the full annotation of `TriangleMeshSlicer.cpp`. The previous session had annotated all functions up through `slice_mesh_ex()`; this session covered the remaining four public/private functions at the bottom of the file:

- `slice_mesh_slabs()` — the slab-projection public API
- `triangulate_slice()` — private cap-triangulation helper for `cut_mesh()`
- `project_mesh()` (both overloads) — thin wrappers over `slice_mesh_slabs()`
- `cut_mesh()` — full mesh-splitting at a Z plane

Additionally: Hazards 73–77 were appended to `04_refactoring_hazards.md`.

---

### Annotations Added — Session 9

#### `slice_mesh_slabs()` (line ~2302 after Session 8 annotations)

**What it does:** Public API for producing per-slab top-facing and/or bottom-facing polygon projections. Given N+1 Z boundaries, produces N slabs. Each slab captures the XY footprint of all upward-facing triangles (for `out_top`) or downward-facing triangles (for `out_bottom`).

**Key architecture notes:**
- `FaceOrientation` classification happens in `slice_mesh_slabs()` itself (not in the helper), because it requires the `mirrored_sign` correction. The cross-product sign is computed in integer arithmetic (`int64_t`) from scaled XY coordinates to avoid float precision issues.
- `vertical_points` is a BBS addition: collects (center, normal) for all `FaceOrientation::Vertical` faces for use by seam placement and support contact detection.
- `slice_slabs_make_lines()` (not annotated this session — it's a large TBB-parallel helper) is the workhorse; the outer function orchestrates setup and calls `make_slab_loops<true/false>()` for final loop assembly.
- Memory: `vertices_transformed` is a full copy of the vertex array (XY scaled, Z unscaled). For a 1M-vertex mesh this is ~12MB of temporary allocation.

**[HAZARD noted]:** The `vertical_points` normalization call (`normalized()`) is not guarded against zero-length vectors. A face that mis-classifies as `Vertical` due to floating-point borderline cross-product would produce NaN normals. The adjacent `Degenerate` guard should prevent this but it's not explicitly checked.

---

#### `triangulate_slice()` (line ~2411 after annotations)

**What it does:** Post-processes a half-mesh produced by `cut_mesh()` to: (1) deduplicate vertices added at the cut plane, (2) optionally fill the open cap with triangulated polygons.

**Key architecture notes:**

**Deduplication pass (always runs):**
- Builds `map_vertex_to_index`: sorted array of (XY position → vertex index) for all cut-plane vertices.
- Groups entries by `is_equal()` epsilon proximity; all duplicates within the group get remapped to the lowest-index representative.
- Walks all face indices and remaps; drops faces that become degenerate (two or more identical indices).

**Triangulation pass (when `triangulate == true`):**
- Calls `make_expolygons_simple(lines)` to form ExPolygons from the cut-plane intersection lines.
- Calls `triangulate_expolygons_3d()` to get the flat triangle vertex list.
- For each triangle vertex, performs a **4-pass lookup**:
  1. `section_vertices_map` (BBS addition): O(N) scan of original vertices exactly on the cut plane.
  2. Forward scan from `lower_bound` in `map_vertex_to_index` using `is_equal()`.
  3. Backward scan from `lower_bound` (handles sort edge cases).
  4. Linear scan of newly added cap vertices (`idx_vertex_new_first`..end).
  5. Fallback: insert new vertex (should be rare).
- Appends non-degenerate triangles to `its.indices`.
- Calls `its_compactify_vertices()` at the end to remove unreferenced vertices.

**Design insight:** The reason for the `section_vertices_map` (BBS addition) is that original mesh vertices that exactly hit the cut plane are not added to `slice_vertices` by the caller (since no edge interpolation occurs for them). Without this map, the triangulator would fail to match those vertices and insert spurious duplicates.

---

#### `project_mesh()` (lines ~2546 and ~2562 after annotations)

**What it does:** Thin wrapper over `slice_mesh_slabs()` that uses a single Z slab spanning `[-1e10, 1e10]` to capture the entire mesh. The first element of the top result and the last element of the bottom result give the full XY shadow of the mesh from above and below respectively.

**Two overloads:**
- `void project_mesh(mesh, trafo, out_top, out_bottom, cancel)` — writes to two separate output Polygon vectors.
- `Polygons project_mesh(mesh, trafo, cancel)` — returns `union_(top.front(), bottom.back())` for the complete 2D silhouette.

**Usage:** Raft/brim outline generation, support pillar collision detection, bounding footprint queries.

---

#### `cut_mesh()` (line ~2573 after annotations)

**What it does:** Splits a triangle mesh at a horizontal Z plane into upper and/or lower halves, each as a new `indexed_triangle_set`. Optionally fills the cut caps with triangulated polygons (`triangulate_caps` flag).

**Algorithm:**
1. Pre-scan all faces; build `section_vertices_map` for vertices exactly at Z (BBS addition).
2. For each face:
   - Entirely above Z → copy to upper unchanged.
   - Entirely below Z → copy to lower unchanged.
   - Straddling Z → call `slice_facet_for_cut_mesh()` to compute intersection line; then dispatch into 2–3 sub-triangles based on `is_new_vertex_v0v1` / `is_new_vertex_v2v0` booleans.
3. Collect intersection lines into `upper_lines` / `lower_lines` and `upper_slice_vertices` / `lower_slice_vertices`.
4. Call `triangulate_slice()` on each half to deduplicate + optionally cap.

**BBS additions:**
- `isolated_vertex_option`: fallback for degenerate triangles where a vertex exactly on the cut plane makes the "isolated vertex" ambiguous. The `calc_isolated_vertex` lambda validates against intersection line edge IDs.
- `section_vertices_map`: passed through to `triangulate_slice()` for first-pass vertex lookup.

**Coordinate contract:** Input uses unscaled mm float for vertex positions. The `z` parameter is unscaled mm float. Inside the straddling-face block, XY is temporarily scaled to `coord_t` for `slice_facet_for_cut_mesh()`, then the intersection points are `unscale()`d back to mm float for the new vertex positions. This scale/unscale round-trip introduces a small error (relative to direct float interpolation) but ensures consistency with the main slicing pipeline's numeric regime.

**No cancellation callback** — unlike `slice_mesh()`, `cut_mesh()` provides no way for the caller to interrupt a long-running operation. For very large meshes this can block the calling thread.

---

### Hazards Added — Session 9

| # | Hazard | Severity |
|---|--------|----------|
| 73 | Dual `slice_facet`/`slice_facet_for_cut_mesh` logic divergence | Medium |
| 74 | Hardcoded 2mm gap in open-polyline stitcher | Low |
| 75 | `triangulate_slice()` O(N²) vertex lookup | Medium |
| 76 | Zero-length edges from integer rounding (FIXME comment) | Medium |
| 77 | Mixed scaled/unscaled Z contract at all public API boundaries | High |

---

### Key Findings — Session 9

1. **`cut_mesh()` has no cancellation** — This is a regression risk if cut operations are ever moved to the main thread or exposed more heavily in the UI (e.g., mesh repair tools). Recommend adding `throw_on_cancel` parity with `slice_mesh()`.

2. **`section_vertices_map` is an O(N) scan, not an O(1) map** — The name is misleading. It is a `std::map<int, Vec3f>` keyed by vertex index but iterated linearly with `is_equal()` comparisons inside the triangulation loop. Rename or restructure for clarity.

3. **scale/unscale round-trip in `cut_mesh()`** — Intersection vertex positions are computed by `slice_facet_for_cut_mesh()` in scaled integer space, then unscaled back to float. This double conversion (`float → int → float`) introduces ~1e-6 mm error per coordinate. For normal cut operations this is harmless, but for meshes that require watertight caps (e.g., FFF support interfaces), this may produce barely-open edges that defeat the `its_num_open_edges` debug assertion.

4. **TriangleMeshSlicer.cpp fully annotated** — All 30+ functions and structs in this 2809-line file now carry [INTENT]/[STATE]/[HAZARD]/[COUPLING]/[MEMORY]/[CONCURRENCY] annotations. This completes the most complex file in the slicing pipeline.

---

### Open Questions (Session 9)

1. `[UNCLEAR]` `slice_slabs_make_lines()` (lines ~700–1050, not annotated this session) — the large internal TBB helper that does the per-face slab intersection. Its interaction with `face_neighbors` and `face_edge_ids` for topological edge stitching is complex and would benefit from a dedicated annotation pass in a future session.

2. `[UNCLEAR]` `remove_tangent_edges()` (~line 1051) — removes edges that are tangent to the slicing plane from the IntersectionLines set before loop-building. The exact definition of "tangent" here and why it's needed before stitching but not before open-polyline recovery is not fully clear from the code comments.

3. `[UNCLEAR]` Why does `cut_mesh()` use `slice_facet_for_cut_mesh()` (epsilon-based) while `slice_mesh()` uses `slice_facet()` (bit-exact)? The epsilon version was presumably introduced to handle real-world meshes with near-plane vertices, but this asymmetry is undocumented. Are there cases where bit-exact would be preferred for cut operations?

---

### Next Annotation Targets (Session 10+)

1. `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` — travel path optimization (Seam-adjacent travel avoidance, contour following)
2. `src/libslic3r/GCode/GCodeProcessor.cpp` — G-code simulation/statistics (large file, ~5000 lines)
3. `src/libslic3r/GCode/FanMover.cpp` — fan control post-processor
4. `src/libslic3r/GCode/AdaptivePAProcessor.cpp` — adaptive pressure advance
5. `src/libslic3r/Arachne/` directory — variable-width perimeter internals
6. `slice_slabs_make_lines()` back-fill annotation (within TriangleMeshSlicer.cpp)

---

## Session 10 — AvoidCrossingPerimeters.cpp Full Annotation

### Files Processed
- `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` (1921 lines after annotation; ~190 lines of comments injected)

### Commit
- `f6d462b635` — `annotate: AvoidCrossingPerimeters.cpp — travel-path planner full annotation (Session 10)`

---

### Architecture Overview — AvoidCrossingPerimeters

**Purpose:** Reroutes travel moves so the nozzle hugs perimeter contours rather than crossing them, minimising stringing/oozing artifacts. Called from `GCode::travel_to()` after tool change/wipe decisions are made.

**Class structure:**
```
AvoidCrossingPerimeters
  ├── m_internal : Boundary   ← current object's inward-offset perimeters
  ├── m_external : Boundary   ← all objects' expanded holes (multi-object)
  ├── m_grid_lslice           ← EdgeGrid for "is point inside slice?" queries
  └── m_lslices_offset        ← inward-offset layer slices (for m_grid_lslice)
```

**`Boundary` struct fields:**
- `boundaries` (Polygons): the actual contour lines to avoid crossing
- `grid` (EdgeGrid::Grid): spatial index for O(1) edge-proximity queries
- `boundaries_params` (std::vector<std::vector<float>>): cumulative arc-length per vertex per polygon; enables O(1) "distance along perimeter" lookups during path walking
- `bbox` (BoundingBox): used by `travel_to()` to detect if a lazy-init rebuild is needed

---

### Key Algorithm — `avoid_perimeters_inner()`

1. **Intersection detection** via `AllIntersectionsVisitor` + `EdgeGrid` — finds every crossing of the direct travel segment with boundary polygons.
2. **Retry with expanded segment** — if no intersections found, the start/end points may already be inside the offset zone; a slightly wider segment is tried.
3. **`extend_for_closest_lines()`** — synthesises artificial intersections at the endpoints for degenerate cases where the path enters/exits a boundary tangentially rather than crossing it.
4. **Polygon walk** — for each entry/exit crossing pair on the same boundary, walk either forward or backward (whichever produces the shorter arc, determined by `get_shortest_direction()`), emitting SCALED_EPSILON-offset waypoints.
5. **`simplify_travel()`** — post-process: greedily remove waypoints that don't re-introduce crossings, producing the shortest straight-line path consistent with the boundary avoidance constraint.

---

### Key Functions Annotated

| Function | Lines | Key Notes |
|---|---|---|
| `inner_offset()` | ~1210–1290 | Variable per-vertex inward offset; tries 3 progressively looser `min_contour_width` values to avoid over-shrinking narrow constrictions |
| `contour_distance()` | ~1150–1210 | Wall-thickness measurement (ElephantFootCompensation approach); feeds `inner_offset()` |
| `get_boundary()` | ~1300–1350 | Assembles the final Polygons for m_internal from contour_distance + inner_offset output |
| `init_boundary()` | ~1355–1370 | Runs `get_boundary()`, calls `EdgeGrid::Grid::create()`, calls `precompute_polygon_distances()` |
| `init_layer()` | ~1375–1410 | Clears m_internal/m_external (lazy reset), computes `m_lslices_offset`, builds `m_grid_lslice` |
| `travel_to()` | ~1413–1450 | Coordinate correction (object vs world), lazy init of m_internal or m_external, calls `avoid_perimeters()`, applies `max_travel_detour_distance` cap, sets `could_be_wipe_disabled` |
| `avoid_perimeters()` | ~1455–1520 | Wrapper: selects m_internal vs m_external, calls `avoid_perimeters_inner()`, converts result to Polyline |
| `avoid_perimeters_inner()` | ~700–1140 | Core routing algorithm (see above) |

---

### Dead Code — `#if 0` Block (lines ~1522–1921, ~400 lines)

A complete alternative implementation is disabled behind `#if 0`. It contains:
- Second `avoid_perimeters_inner()` — heuristic-based simplification (`simplify_travel_heuristics`) instead of greedy
- Second `avoid_perimeters()` — same interface but calls the heuristic version
- Second `travel_to()` — same interface
- Second `init_layer()` — **eager** boundary init (builds both m_internal and m_external at layer start) vs the current **lazy** approach (build only when first travel that layer needs it)

No comment explains which design was intentionally chosen or whether the block is kept for future benchmarking. Tagged `[HAZARD-78]`.

---

### Boundary Modes

| Mode | Trigger | Boundaries Source | Purpose |
|---|---|---|---|
| `m_internal` | Default; `m_use_external_mp == false` | Inward-offset perimeters of **current object's** layer | Avoid stringing inside the object |
| `m_external` | `m_use_external_mp == true` (multi-object) | Expanded holes of **all** PrintObjects at this Z | Avoid crossing neighbouring objects during inter-object travels |

---

### Hazards Added — Session 10

| # | Hazard | Severity |
|---|--------|----------|
| 78 | Large `#if 0` dead-code block (~400 lines): entire alternative implementation | Medium |
| 79 | Lazy boundary re-init triggers full `inner_offset()` + EdgeGrid rebuild on bbox miss | Medium |
| 80 | All three EdgeGrid instances hardcoded to 1mm cell size (`// FIXME 1mm grid?`) | Low |

---

### Key Findings — Session 10

1. **Two coordinate systems meet in `travel_to()`** — input `start`/`end` are in **world** coordinates but `m_internal` boundaries are in **object-local** coordinates. `travel_to()` applies the `m_object_instance_transformed_bounding_box` transform to bridge the gap. This transform application must stay in sync with the boundary construction transform or paths will be systematically offset.

2. **`simplify_travel()` is O(N²) in waypoint count** — for each remaining waypoint it calls `avoid_perimeters_inner()` again on the partial path. For extremely complex perimeter shapes this could be slow, but in practice waypoint counts are small (rarely > 20).

3. **`contour_distance()` is called once per vertex during `inner_offset()`** — it builds a local EdgeGrid per call for the wall-thickness measurement. This is the most expensive sub-operation in `init_boundary()`. For large layers with many perimeter vertices this can be a meaningful contributor to layer-start latency.

4. **`precompute_polygon_distances()` must be called after every boundary rebuild** — it populates `boundaries_params` which is required by the polygon-walk step of `avoid_perimeters_inner()`. If a boundary is ever modified without calling this function, the arc-length lookups will silently use stale data.

5. **`could_be_wipe_disabled`** — `travel_to()` sets this flag (via `need_wipe()`) when the travel stays entirely within layer slices. This is a minor coupling between the path planner and the wipe/retract decision subsystem; the flag is consumed by `GCode::travel_to()` to potentially suppress a retract+wipe.

---

### Open Questions — Session 10

1. `[UNCLEAR]` `extend_for_closest_lines()` synthesises artificial intersections when start/end are inside the boundary offset zone. The exact geometric condition under which this is needed vs the expanded-segment retry is not fully documented. Edge cases at corners or concave regions may hit both code paths.

2. `[UNCLEAR]` Why is `m_external` built from `expanded holes of all PrintObjects` rather than their outer perimeters? The choice of holes (vs full perimeter boundary) means the planner avoids routing through the interior of neighbouring objects but not through their perimeter walls. This seems intentional for multi-object prints where objects are adjacent but the reason is not commented.

3. `[UNCLEAR]` `get_shortest_direction()` returns a direction enum (CW/CCW) based on comparing arc lengths along the polygon in each direction. For polygons with complex topology (self-intersecting after offset), it's unclear whether the arc-length heuristic always chooses the geometrically shortest non-crossing walk.

---

### Next Annotation Targets (Session 11+)

1. `src/libslic3r/GCode/GCodeProcessor.cpp` — G-code simulation/statistics (**highest priority**, ~5000 lines)
2. `src/libslic3r/GCode/FanMover.cpp` — fan control post-processor
3. `src/libslic3r/GCode/AdaptivePAProcessor.cpp` — adaptive pressure advance
4. `src/libslic3r/Arachne/` directory — variable-width perimeter internals
5. `slice_slabs_make_lines()` back-fill annotation (within TriangleMeshSlicer.cpp)

---

## Session 12 — GCodeProcessor.cpp Final Annotation Pass + Hazards 81–95

**Date:** 2026-03-07
**Branch:** `agent/analysis`
**Files modified:** `GCodeProcessor.cpp`, `04_refactoring_hazards.md`, `agent_journal.md`

### Work Completed

This session completed the annotation pass on `GCodeProcessor.cpp` (6379 lines, the largest single source file in the GCode pipeline). All remaining un-annotated top-level functions now have structured `[INTENT]`, `[STATE]`, `[HAZARD]`, `[COUPLING]`, and/or `[MEMORY]` comment blocks.

**Functions annotated in this session:**

| Function | Lines | Key findings |
|----------|-------|-------------|
| `finalize()` | ~2870 | 8-step ordering matters; post-process rename is irreversible |
| `store_move_vertex()` | ~5841 | Plate-offset + z_offset applied here; move indices invalidated by later calculate_time() |
| `process_G4()` | ~5135 | S+P additive dwell; both must be summed |
| `process_G29()` | ~5146 | 260s hard-coded BBS magic constant |
| `process_G10/G11()` | ~5159 | Firmware retract → G1 translation; retract_restart_extra asymmetry |
| `process_G28()` | ~5192 | Synthetic raw-string re-parsing hazard |
| `process_G92()` | ~5222 | E axis uses direct position reset, not origin shift |
| `process_M104/M109()` | ~5265 | Temperature tracking for vitrification warning |
| `process_M106/M107()` | ~5275 | 8-bit PWM assumption; BBS P1 handling |
| `process_M900/M572/SET_PRESSURE_ADVANCE()` | ~5290 | Per-call regex construction hazard |
| `process_M201()` | ~5409 | Acceleration array update; flavor-based unit conversion |
| `process_M203()` | ~5433 | Feedrate units: mm/s vs mm/min by flavor |
| `process_M204()` | ~5464 | T-param dual meaning: retract (legacy S) vs travel (modern) |
| `process_M205()` | ~5492 | Jerk limits; XY shared via X param; junction deviation J |
| `process_SET_VELOCITY_LIMIT()` | ~5525 | 3 regex per call; SCV maps only to XY jerk |
| `process_custom_gcode_time()` | ~6105 | simulate_st_synchronize; zero-duration segment skip |
| `calculate_time()` | ~6139 | O(n²) vector insertion; Normal-only actual-speed collection |
| `update_slice_warnings()` | ~6241 | Timelapse warning dual-emit; bit-field decoding |
| `extract_absolute_position_on_axis()` | ~6224 | Volumetric E path commented out |
| `get_filament_id / get_extruder_id` | ~6348 | 0xFF sentinel via unsigned char; force_initialize default |

### New Hazards Documented (81–95)

15 new hazards written to `04_refactoring_hazards.md`, covering:
- **P1 (Critical):** finalize() no-backup rename, calculate_time() index invalidation, 2GB+ move vector, G92 E asymmetry
- **P2 (High):** G28 synthetic re-parse, M204 T dual meaning, per-call regex, G29 hard-coded dwell, O(n²) insertion, unsigned char sentinel
- **P3 (Low):** duplicate timelapse warning, M106 PWM assumption, wrong flush type at finalize, seam line-id inheritance, ToolChange volume reset

### Architecture Insights

1. **GCodeProcessor is a two-phase pipeline:** Phase 1 (parsing) runs line-by-line building `m_result.moves` with placeholder time fields. Phase 2 (calculate_time) retroactively fills time fields and may insert additional vertices. Any refactoring must preserve this two-phase structure or risk index corruption.

2. **simulate_st_synchronize is an incremental checkpoint:** Called at every M-command that could affect timing (M104, M106, G4, G29, etc.), it drains the pending TimeBlock queue. This means time estimation is not deferred to finalize() — it runs incrementally throughout parsing. A refactored system that batches all time computation to the end would need to handle the intermediate state differently.

3. **The coordinate state machine has five active variables:** `m_start_position`, `m_end_position`, `m_origin`, `m_global_positioning_type`, `m_e_local_positioning_type`. All five must be correctly initialized and maintained across every G/M command handler or position tracking breaks silently.

4. **Fan speed, temperature, and pressure advance are snapshot values:** They are captured at the moment of each G1 move and stored per-vertex. There is no interpolation between the last-known value and the next command; abrupt changes at command boundaries are the intended model.

### Open Questions — Session 12

1. `[UNCLEAR]` `process_G29()` calls `simulate_st_synchronize(value_s)` with 260s for non-BBS printers — but `simulate_st_synchronize` calls `calculate_time()` which processes accumulated blocks. If G29 appears before any motion blocks in the file, will this produce a spurious 260s at t=0 in the time breakdown? The interaction with zero-block edge cases is not tested.

2. `[UNCLEAR]` `store_move_vertex()` applies `m_extruder_offsets[filament_id]` to the position. For single-extruder printers this offset is zero. For multi-extruder, it represents nozzle geometry compensation. However, `m_extruder_offsets` is populated from `machine_extruder_offset` config — if this config is absent (e.g., in viewer-only mode), the offset defaults to zero, which may be wrong for some multi-extruder hardware setups.

3. `[UNCLEAR]` The `M221` extrude-factor override is stored in `machine.extrude_factor_override_percentage` but it's not clear where this value is consumed in the time estimation. The TimeMachine trapezoidal planner uses `m_feedrate` directly — does M221 ever modify m_feedrate before block creation, or is it only a post-hoc scale factor?

### Next Annotation Targets (Session 13+)

1. `src/libslic3r/GCode/FanMover.cpp` — fan speed lookahead post-processor
2. `src/libslic3r/GCode/AdaptivePAProcessor.cpp` + `AdaptivePAInterpolator.cpp` — adaptive pressure advance
3. `src/libslic3r/Arachne/` directory — variable-width perimeters (WallToolPaths, etc.)
4. `src/libslic3r/GCode/GCodeProcessor.hpp` — annotate header (structs, enums, member variables)

---

## Session 13 — FanMover Annotation Pass

### Files Processed
- `src/libslic3r/GCode/FanMover.cpp` (514 lines) — fully annotated
- `src/libslic3r/GCode/FanMover.hpp` (97 lines) — fully annotated
- `generated_documentation/04_refactoring_hazards.md` — hazards 96–110 added

### Work Performed

**FanMover.cpp — full annotation pass:**
- File header: [INTENT], [STATE], [MEMORY], [CONCURRENCY], [COUPLING], [HAZARD] tags
- `process_gcode()`: annotated the per-chunk entry point, including the O(n) rounding-drift recompute and the returned-reference invalidation hazard
- `is_end_of_word()`: annotated as a token-boundary helper
- `get_axis_value()`: documented the leading-space assumption hazard and strtod manual offset arithmetic
- `change_axis_value()`: annotated the critical npos+2 wrap hazard and the end-of-string replace hazard
- `get_fan_speed()`: documented the -1 sentinel overloading and the per-firmware fan command semantics (Bambu P1/P0 logic, MakerWare, etc.)
- `_put_in_middle_G1()`: documented the G1-split algorithm, proportional dx/dy/dz/de scaling, the 10%-boundary snap logic, and the relative-E vs absolute-E asymmetry
- `_print_in_middle_G1()`: documented the flush-to-output variant, noted the inverted boundary thresholds vs the buffer version, and the stale `de` hazard
- `_remove_slow_fan()`: documented the forward-scan slow-fan purge, its interaction with motion-line time budgeting, and the intentional reach-limit behaviour
- `_set_fan()`: documented the multi-extruder TODO (dead code path for extruder-specific fan)
- `parse_number()`: documented the costly string copy and the blanket exception catch
- `_process_T()`: documented the extruder-0 fallback hazard and the firmware-specific special command handling
- `_process_gcode_line()`: full annotation of the main state machine — fan increase/decrease branches, kickstart insertion, custom-gcode bypass, only_overhangs role gating, buffer drain loop, kickstart countdown, and the debug assertion

**FanMover.hpp — full annotation pass:**
- File header: module-level [INTENT], [STATE], [MEMORY], [COUPLING]
- `BufferData`: documented all fields, the dead `pop_back()` bug, the `float is_kickstart` type mismatch
- `FanMover` class: documented dead `regex_fan_speed`, dead `with_D_option`, initial-speed hazard for `m_current_speed`, front/back speed tracking desync hazard, kickstart sentinel
- `put_in_buffer()` / `remove_from_buffer()`: documented the missed `std::move` optimization
- Constructor: documented the `nb_seconds_delay` clamping and the dead `with_D_option` parameter

**Hazards 96–110 written to `04_refactoring_hazards.md`:**
- 2 P1 critical: `change_axis_value` npos+2 wrap, catastrophic replace without trailing delimiter
- 3 P2 medium: `get_axis_value` leading-space assumption, time estimation ignores acceleration, float drift correction frequency, `;TYPE:` comment contract dependency
- 10 P3 low: dead regex member, dead with_D_option, dead pop_back, float/bool mismatch, fan speed truncation, sub-ms kickstart, T-command extruder reset, parse_number copy, rfind idiom, custom-gcode starts_with clarity

### Key Discoveries — Session 13

1. **FanMover operates on raw string text:** Unlike GCodeProcessor which builds a structured `MoveVertex` representation, FanMover performs all operations by string search and replace on raw G-code lines. This means every buffer split operation rewrites axis values in-place using `change_axis_value()`, which has silent-corruption failure modes (Hazards 96–97). A refactored implementation must parse G-code into a structured AST first.

2. **Two fan speed trackers with asymmetric semantics:** `m_back_buffer_fan_speed` tracks the newest speed (just parsed), while `m_front_buffer_fan_speed` tracks the oldest speed (most recently emitted to output). Fan command suppression in the drain loop uses `front_speed` for dedup. Both must be kept in sync or commands will be silently dropped. Any refactoring that merges them into a single "current speed" variable will break the deduplication logic.

3. **Kickstart has two code paths for delay vs no-delay mode:** When `nb_seconds_delay > 0` (delay mode), the kickstart pulse is injected into the buffer's historical past using `_put_in_middle_G1`. When `nb_seconds_delay == 0` (no delay), the kickstart pulse is tracked via `m_current_kickstart` and inserted via the time-countdown mechanism in the normal buffer drain. These two paths have subtle differences in when they check the 10% threshold and how they handle concurrent kickstarts being superseded.

4. **Buffer is ordered oldest-first (front = output side):** The naming "front" = oldest = output end and "back" = newest = input end is consistent throughout, but is the opposite of what "front" implies in a queue. The `m_buffer.begin()` iterator is the OLDEST entry (about to be emitted), and `m_buffer.end()-1` is the NEWEST (just inserted). Refactoring to a ring buffer or deque must preserve this orientation.

5. **`_print_in_middle_G1` vs `_put_in_middle_G1` threshold inversion:** When splitting a move to inject a fan command, `_put_in_middle_G1` (buffer version) puts the command BEFORE if `t < 10%` and AFTER if `t > 90%`. `_print_in_middle_G1` (output version) does the opposite: prints the G-code line FIRST if `t < 10%`, fan command FIRST if `t > 90%`. This is not a bug but reflects the semantic difference: the buffer version is "where in future time should this go?", while the print version is "which should I output first?".

### Open Questions — Session 13

1. `[UNCLEAR]` In `_process_gcode_line()` line 488, the drain condition is `m_buffer_time_size - m_buffer.front().time > nb_seconds_delay - EPSILON`. What is EPSILON defined as? If it's the standard `libslic3r.h` EPSILON (1e-4), this creates a 0.1ms tolerance on the drain trigger, which is negligible. But if EPSILON is larger, the drain may fire 10ms+ early, defeating the delay purpose.

2. `[UNCLEAR]` `m_currrent_extruder` (note: typo "currrent") is tracked but only used in the commented-out `_set_fan()` path. Once multi-extruder fan support is implemented, the extruder tracking will suddenly matter. The `_process_T()` fallback to extruder 0 on parse failure will be a latent bug in multi-extruder setups.

3. `[UNCLEAR]` When `need_flush = true` (fan speed decrease path), ALL buffered lines are flushed unconditionally at line 488. This means a slowdown command always arrives on-time (correct), but it also drains any buffered fan speed-up commands that were already placed in the buffer for the upcoming overhang. This could cause the fan to turn off and then immediately need to spin back up for the next overhang — a suboptimal but correct behaviour.

### Next Annotation Targets (Session 14+)

1. `src/libslic3r/GCode/AdaptivePAProcessor.cpp` + `AdaptivePAInterpolator.cpp` — adaptive pressure advance
2. `src/libslic3r/Arachne/` directory — variable-width perimeters (WallToolPaths, etc.)
3. `src/libslic3r/GCode/GCodeProcessor.hpp` — annotate header (structs, enums, member variables)

---

## Session 14 — AdaptivePAProcessor + AdaptivePAInterpolator (Adaptive Pressure Advance)

**Files processed:**
- `src/libslic3r/GCode/AdaptivePAProcessor.cpp` (~530 lines) — annotated
- `src/libslic3r/GCode/AdaptivePAProcessor.hpp` (~120 lines) — annotated
- `src/libslic3r/GCode/AdaptivePAInterpolator.cpp` (205 lines) — annotated
- `src/libslic3r/GCode/AdaptivePAInterpolator.hpp` (54 lines) — annotated

**Commit:** `c29745e78c` — annotate: AdaptivePAProcessor + AdaptivePAInterpolator full annotation pass (Session 14)

**New hazards documented:** 111–125 (see 04_refactoring_hazards.md)

### Module Summary — AdaptivePAInterpolator

AdaptivePAInterpolator implements a 2D calibration model:
`PA = f(flow_rate_mm3s, acceleration_mm_s2)`

The model structure is a "2D grid" of 1D PCHIP interpolators:
- Outer axis: acceleration values (from calibration CSV)
- Inner axis per acceleration: flow_rate → PA value

Evaluation uses a "slice-then-interpolate" strategy:
1. For each stored acceleration, interpolate PA at the query flow_rate
2. Build a 1D array of (acceleration → PA_at_flow)
3. Run a second PCHIP over this array to get final PA

**Key design note:** Both the flow axis and the accel axis use `PchipInterpolatorHelper` as the 1D interpolation primitive. The flow-axis helpers are pre-built at parse time; the accel-axis helper is rebuilt on EVERY `operator()` call.

### Module Summary — AdaptivePAProcessor

AdaptivePAProcessor wraps AdaptivePAInterpolator and provides the G-code post-processing entry point. It:
- Parses incoming G-code lines to detect extrusion moves
- Tracks current feedrate (`m_current_feedrate`, `m_next_feedrate`) and extrusion type
- Estimates volumetric flow rate from E-delta and feedrate
- Looks up PA from the calibration model
- Injects `SET_PRESSURE_ADVANCE PA=<value>` (Klipper) or `M900 K<value>` (Marlin) G-code

**Processing model:** Line-by-line regex matching against raw G-code string. The lookahead scan (`process_layer()`) scans forward in the stream to find the next non-zero feedrate, creating potential O(n²) behaviour for streams with many consecutive zero-feedrate commands.

### Key Discoveries — Session 14

1. **`accel_value` type truncation:** `m_current_acceleration` is `unsigned int` but is populated via `std::stod()` then cast. Any fractional acceleration (e.g., from a `M204 S500.5` command) is silently truncated. The PA model uses `double` internally, so the truncated integer is passed to the 2D interpolator which may produce a slightly wrong result at fractional-acceleration boundaries.

2. **`m_next_feedrate` state reset on every outer loop iteration:** In `process_layer()`, `m_next_feedrate = 0.0` is reset at the start of each outer `for` loop iteration before the lookahead scan. This is correct for the "find next feedrate" pattern, but if a future refactor moves the reset or adds `continue` statements before it, the last valid feedrate will be carried forward, causing the PA model to use a stale feedrate for flow calculation.

3. **CSV silent discard on any exception:** `parseAndSetData()` uses a blanket `catch(const std::exception&)` that sets `m_isInitialised = false` and returns `-1`, discarding ALL valid data parsed before the exception. A header row in the CSV (common in exported calibration files) will trigger `std::stod` to throw, silently invalidating the entire model.

4. **Bridge extrusion PA override is a hard replacement:** When the parser detects a bridge extrusion move (from the `;TYPE:Bridge infill` G-code comment), the PA value is overridden with a hard-coded `0.0` (or a configured bridge PA value). This is a non-blending override — the smooth interpolated PA is discarded entirely. If the bridge PA configuration is missing, it defaults to `0.0`, which may or may not be correct for the material.

5. **`operator()` rebuilds interpolator every call:** `AdaptivePAInterpolator::operator()` constructs a new `PchipInterpolatorHelper` for the acceleration axis on every call. For typical calibration data (2–5 acceleration values), the PCHIP initialization is O(N) — fast enough in practice. But in a high-throughput G-code stream (thousands of moves), this is a repeated allocation that could be eliminated by caching the acceleration-axis model keyed on flow_rate, or by redesigning the model as a true 2D surface.

6. **Return value `-1.0` sentinel:** Both `AdaptivePAInterpolator::operator()` and `PchipInterpolatorHelper::interpolate()` return `-1.0` to signal failure. This is a C-style error sentinel in a `double`-returning function. A refactored implementation should use `std::optional<double>` to make failure explicit and type-safe.

### Key Hazards — Session 14

| # | Hazard | Severity |
|---|--------|----------|
| 111 | CSV silent discard on any parse exception | Critical |
| 112 | CSV column order undocumented; wrong order poisons model silently | High |
| 113 | `accel_value` unsigned int truncates fractional acceleration values | High |
| 114 | `m_next_feedrate = 0` reset fragile; moved/skipped reset causes stale feedrate | Medium |
| 115 | `operator()` reconstructs PchipInterpolatorHelper on every call | Medium |
| 116 | Return value -1.0 sentinel for failure — not type-safe | Medium |
| 117 | Bridge PA override is hard-replacement, not blend | Medium |
| 118 | Partially-parsed CSV line (2/3 fields) contributes flowRate=0 or accel=0 entry | High |
| 119 | Lookahead scan in process_layer() is O(n²) for zero-feedrate streams | Medium |
| 120 | Single-acceleration model returns flow-interpolated PA directly (skips accel interp) | Low |
| 121 | `m_isInitialised` has no mutex; concurrent read during reparse would race | Low |
| 122 | PCHIP requires sorted inputs; acc_to_flow_pa map sorts by key but inner pairs unsorted | High |
| 123 | `std::map<double>` keying on floating-point; equality-sensitive for calibration data | Medium |
| 124 | `std::round(x * 1000.0) / 1000.0` rounding near x.0005 boundaries is non-deterministic | Low |
| 125 | `accelerations_` vector is a redundant parallel structure to `flow_interpolators_` keys | Low |

### Open Questions — Session 14

1. `[UNCLEAR]` The PCHIP implementation in `PchipInterpolatorHelper` has not been annotated yet. The hazard at #122 assumes it requires sorted input — this needs verification. If `PchipInterpolatorHelper` sorts internally, hazard #122 is not a real issue.

2. `[UNCLEAR]` `AdaptivePAProcessor` reads `PrintConfig::adaptive_pressure_advance_model` as a CSV string. It is not clear whether this string is validated upstream (e.g., in the UI layer) or whether arbitrary user input can reach `parseAndSetData()` directly.

3. `[UNCLEAR]` The `SET_PRESSURE_ADVANCE` command injection happens at the G-code post-processing stage (after full G-code generation). This means the injected PA commands are invisible to GCodeProcessor's pressure-advance estimation — there may be a redundant PA command path when both static PA and adaptive PA are configured simultaneously.

### Next Annotation Targets (Session 15+)

1. `src/libslic3r/Arachne/WallToolPaths.cpp/.hpp` — Arachne variable-width perimeter entry point (~1023 lines total)
2. `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp/.hpp` — Voronoi-based medial axis + bead distribution (~2656 lines total, highest complexity)
3. `src/libslic3r/Arachne/BeadingStrategy/` — 7 files, bead width distribution strategies (~612 lines total)
4. `src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp` — graph data structure for skeletal trapezoidation (~472 lines)

---

## Session 15 — WallToolPaths (Arachne Pipeline Entry Point)

**Files processed:**
- `src/libslic3r/Arachne/WallToolPaths.cpp` (879 lines → 1011 lines after annotation) — annotated
- `src/libslic3r/Arachne/WallToolPaths.hpp` (144 lines → 306 lines after annotation) — annotated

**Commit:** `282248fa4a` — annotate: WallToolPaths.cpp/.hpp — Arachne pipeline entry point (Session 15)

**New hazards documented:** 126–140 (see 04_refactoring_hazards.md)

### Module Summary — WallToolPaths

WallToolPaths is the public API entry point for the Arachne variable-width perimeter system. It bridges the slicer's polygon representation (Polygons / PrintObjectConfig) and the Arachne geometry kernel (SkeletalTrapezoidation).

**Pipeline (generate() method):**
1. Outline pre-processing (9 chained steps):
   - Triple offset (−ε, +2ε, −ε) to snap near-self-intersections
   - simplify() — Ramer-Douglas-Peucker-style vertex removal
   - fixSelfIntersections() — nudge + Clipper SimplifyPolygons
   - removeDegenerateVerts() — remove backtrack vertices
   - removeColinearEdges() — remove near-collinear vertices
   - fixSelfIntersections() again (removeColinearEdges can create new intersections)
   - removeDegenerateVerts() again
   - removeSmallAreas() — remove polygons with area < (bead_width_0/2)²
   - union_() — final Clipper union
2. Compute BeadingStrategy via BeadingStrategyFactory
3. Construct SkeletalTrapezoidation and call generateToolpaths()
4. stitchToolPaths() — join open polylines into closed polygons
5. removeSmallLines() — prune short odd (transition) lines
6. separateOutInnerContour() — split 0-width contour paths from printable paths
7. simplifyToolPaths() — simplify each ExtrusionLine by area deviation
8. removeEmptyToolPaths() — final cleanup

### Key Discoveries — Session 15

1. **Triple offset destroys thin features before Arachne sees them:** The outline pre-processing applies `offset(-ε, +2ε, -ε)` where ε ≈ allowed_distance/2 - 1 ≈ 11500 nm. Any feature thinner than ε is silently destroyed before SkeletalTrapezoidation runs. This means the user's min_feature_size setting is irrelevant for features below this physical threshold (~11.5 microns).

2. **transition_filter_dist hardcoded to 100mm:** The transition filter distance (the Voronoi skeleton length over which wall-count transitions are smoothed) is hardcoded to 100mm. For typical print sizes (100–300mm), this is a large fraction of the part. For small parts (< 50mm), virtually ALL wall-count transitions may fall within the filter range, causing them all to be suppressed. This is likely an OrcaSlicer-specific tuning change from the original CuraEngine value.

3. **contour_paths dead code:** `separateOutInnerContour()` allocates a `contour_paths` vector and reserves capacity for it, but never populates it. The variable is unused and appears to be a remnant of a previous refactoring. It does not affect behavior but increases memory allocation unnecessarily.

4. **removeEmptyToolPaths return semantics are inverted:** The function returns `true` when the toolpaths vector is EMPTY after removal (i.e., all paths were removed), not when there are paths remaining. This is counterintuitive and must be correctly interpreted by callers.

5. **is_top_or_bottom_layer always false from make_paths_params():** The factory function hardcodes `is_top_or_bottom_layer = false`. Callers that need top/bottom layer behavior must set this flag manually after the call. If forgotten, removeSmallLines() uses the wrong (less aggressive) threshold for top/bottom layers, potentially leaving short odd lines that create surface artifacts.

6. **outline stored as reference — dangling risk:** The constructor takes `const Polygons& outline` and stores it by reference. The polygon must outlive the WallToolPaths object. This is safe in current callers (outline is from a long-lived PrintObject region), but in refactored code that creates WallToolPaths lazily or asynchronously, this could become a dangling reference.

7. **simplify() uses Shoelace accumulated area — int64_t overflow potential:** The polygon simplification algorithm accumulates area contributions using int64_t arithmetic. For pathological inputs (very long consecutive collinear segments), the accumulated_area_removed can theoretically overflow. At typical OrcaSlicer coordinate scales, this is safe but should be noted for a refactored integer-coordinate system.

### Key Hazards — Session 15

| # | Hazard | Severity |
|---|--------|----------|
| 126 | Triple-offset destroys features < ~11500nm before SkeletalTrapezoidation | High |
| 127 | transition_filter_dist hardcoded 100mm — may suppress all transitions on small parts | High |
| 128 | contour_paths allocated but never populated — dead code, wastes memory | Low |
| 129 | removeEmptyToolPaths returns true=EMPTY (inverted semantics) | Medium |
| 130 | is_top_or_bottom_layer always false in make_paths_params(); caller must fix | Medium |
| 131 | outline stored as const reference — dangling risk in async/lazy contexts | High |
| 132 | simplify() int64_t area accumulation: overflow for pathological inputs | Low |
| 133 | removeColinearEdges introduces new self-intersections (noted in code, fixSelfIntersections run twice) | Medium |
| 134 | generateToolpaths() produces unsorted output in edge cases — sorted assertion may fire | Medium |
| 135 | min_feature_size from params.min_feature_size (float→coord_t): sub-1nm silently becomes 0 | Medium |
| 136 | stitchToolPaths: bead_width_x-1 stitch_distance; if bead_width_x=0, distance=-1 (UB) | High |
| 137 | fixSelfIntersections epsilon<1 path uses ClipperLib pftEvenOdd without nudging | Low |
| 138 | getRegionOrder uses SparsePointGrid not SparseLineGrid; can miss constraints for simplified insets | Medium |
| 139 | separateOutInnerContour checks only first junction of first line for w==0 classification | Medium |
| 140 | min_nozzle_diameter governs all Arachne parameters in multi-nozzle setups | Medium |

### Open Questions — Session 15

1. `[UNCLEAR]` The Arachne algorithm in `SkeletalTrapezoidation` has not been annotated yet. The exact semantics of `is_odd` lines (transition lines vs fill-gap lines) are described here based on code behavior but should be verified against SkeletalTrapezoidation's output.

2. `[UNCLEAR]` The `fill_outline_gaps = true` global constant (line 18 of hpp) is hardcoded to true, meaning thin-wall widening (WideningBeadingStrategy) is always enabled. Is there a user-facing setting to disable this? If not, users with very thin features that produce artifact prints cannot turn it off.

3. `[UNCLEAR]` The `discretization_step_size = scaled<coord_t>(0.8)` (0.8mm) for arc discretization seems large for high-resolution models. For circular cross-sections, 0.8mm steps produce visible chord artifacts on arcs. How does this interact with the upstream mesh simplification in `prepared_outline`?

### Next Annotation Targets (Session 16+)

1. `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp/.hpp` — Core Voronoi + medial axis algorithm (2656 lines, highest complexity)
2. `src/libslic3r/Arachne/BeadingStrategy/` — 7 files (~612 lines, bead width strategies)
3. `src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp` — graph data structure (~472 lines)

---

## Session 16 — SkeletalTrapezoidation.cpp Full Annotation

**Date:** 2026-03-07
**Files processed:** `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp` (2098 → 2415 lines after annotation)

### Summary

Completed annotation of all remaining functions in `SkeletalTrapezoidation.cpp`. This file implements the core Kuipers et al. 2020 medial-axis algorithm that converts a polygon outline into variable-width walls (Arachne). The annotation pass covered every function from `generateAllTransitionEnds()` through `generateLocalMaximaSingleBeads()`.

### Functions Annotated This Session

| Function | Tags Applied | Key Finding |
|----------|-------------|-------------|
| `generateAllTransitionEnds()` | INTENT, STATE, COUPLING | Driver loop; spawns two TransitionEnds per TransitionMiddle |
| `generateTransitionEnds()` | INTENT, STATE, COUPLING, HAZARD | Mixed float×int64_t arithmetic; asymmetric half-lengths |
| `generateTransitionEnd()` | INTENT, STATE, COUPLING, HAZARD, UNCLEAR | Recursive; div-by-zero if start_pos==end_pos; complex return semantics |
| `isGoingDown()` | INTENT, STATE, HAZARD, COUPLING | Source-acknowledged incomplete logic; doesn't handle transition mids on intermediate edges |
| `normal()` (static) | INTENT, HAZARD | Degenerate +X fallback for near-zero input vectors |
| `applyTransitions()` | INTENT, STATE, HAZARD, COUPLING | Materialises transition ends; snap_dist silently drops near-corner transitions |
| `isEndOfCentral()` | INTENT | Predicate for dead-end central skeleton detection |
| `generateExtraRibs()` | INTENT, STATE, COUPLING, HAZARD | Safe std::list iteration while modifying; relies on iterator stability |
| `generateSegments()` | INTENT, STATE, MEMORY, CONCURRENCY | Orchestration of full toolpath generation pipeline (6 sub-steps) |
| `getQuadMaxRedgeTo()` | INTENT, HAZARD, COUPLING | 0.005mm epsilon workaround for flat quad peak detection |
| `propagateBeadingsUpward()` | INTENT, STATE, COUPLING | Reverse-order traversal; sets is_upward_propagated_only flag |
| `propagateBeadingsDownward()` (dispatcher) | INTENT | Routes equidistant edges to single-edge overload |
| `propagateBeadingsDownward()` (single-edge) | INTENT, STATE, HAZARD, COUPLING | Blend window via beading_propagation_transition_dist; assertion drift risk |
| `interpolate()` (4-arg) | INTENT, STATE, HAZARD, UNCLEAR | +0.1 bias overshoot; unresolved TODO for locations past the middle |
| `interpolate()` (3-arg) | INTENT, HAZARD | Zero-width markers preserved; extra beads from larger Beading left unblended |
| `generateJunctions()` | INTENT, STATE, COUPLING, HAZARD | Underflowing junction_idx loop; snap-to-start epsilon risks coincident junctions |
| `getOrCreateBeading()` | INTENT, HAZARD, MEMORY | bead_count==-1 degenerate fallback; width mismatch risk |
| `getNearestBeading()` | INTENT, STATE, HAZARD | BFS capped at 1000; unbounded priority_queue memory |
| `addToolpathSegment()` | INTENT, STATE, HAZARD, COUPLING | Reverse-continue CCW winding error logged but not corrected |
| `connectJunctions()` | INTENT, STATE, MEMORY, COUPLING, HAZARD | Comma-operator null-deref risk; mismatched junction counts not corrected; per-quad copy overhead |
| `generateLocalMaximaSingleBeads()` | INTENT, STATE, HAZARD, COUPLING | Float sin/cos on scaled ints; bypasses addToolpathSegment() |

### Key Discoveries

1. **filterCentral() dead code (Hazard 141):** The public entry point has `isLocalMaximum() && !isLocalMaximum()` — always-false — so the recursive filter is never called. This is a latent bug inherited from CuraEngine.

2. **Transition system complexity:** The transition pipeline is a 5-stage process: `generateTransitionMids()` → `filterTransitionMids()` → `dissolveNearbyTransitions()` → `generateAllTransitionEnds()` → `applyTransitions()`. Each stage mutates edge-data through shared_ptr handles. Any refactor must preserve this ordering and the shared ownership model.

3. **Beading propagation is bidirectional:** Upward propagation (tips→base) fills in nodes with no bead_count. Downward propagation (base→tips) then blends competing beadings using distance-weighted interpolation. The blend window is controlled by `beading_propagation_transition_dist` (hardcoded class constant).

4. **interpolate() has acknowledged TODOs:** Two TODO comments in the 4-arg interpolate indicate the algorithm doesn't correctly handle toolpath locations past the wall midpoint. This is a known limitation producing sub-optimal blends for high bead-count transitions.

5. **connectJunctions() do-while null risk (Hazard 165):** The loop termination relies on `getNextUnconnected()` returning the polygon domain start correctly. A malformed DCEL would cause an infinite loop or null-deref.

6. **addToolpathSegment() CCW error (Hazard 164):** The reverse-continue path (extending a line in reverse) logs an error for even walls because it reverses CW winding to CCW. The error is logged but execution continues, producing incorrectly-wound even walls.

### New Hazards Documented

Hazards 141–175 added to `04_refactoring_hazards.md`. Notable P0/Critical items:
- **H145**: `beading_strategy` const-ref — dangling reference risk
- **H146**: `p_generated_toolpaths` raw non-owning pointer — null-deref risk
- **H165**: `connectJunctions()` null-deref on malformed DCEL

### Files Changed

| File | Change |
|------|--------|
| `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp` | All remaining functions annotated (2098 → 2415 lines) |
| `generated_documentation/04_refactoring_hazards.md` | Hazards 141–175 added |
| `generated_documentation/agent_journal.md` | Session 16 entry added |

### Next Annotation Targets (Session 17+)

1. `src/libslic3r/Arachne/BeadingStrategy/` — 7 files (~612 lines): BeadingStrategy.hpp, DistributedBeadingStrategy.cpp, LimitedBeadingStrategy.cpp, OuterWallInsetBeadingStrategy.cpp, RedistributeBeadingStrategy.cpp, WideningBeadingStrategy.cpp, BeadingStrategyFactory.cpp
2. `src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp` — DCEL graph operations (~472 lines)
3. `src/libslic3r/GCode/GCodeProcessor.hpp` — header for the 8000-line GCodeProcessor

---

## Session 17 — BeadingStrategy Module Complete Annotation

**Date:** 2026-03-08
**Branch:** `agent/analysis`
**Focus:** Complete annotation of all 10 BeadingStrategy source files (5 hpp + 5 cpp pairs) plus BeadingStrategyFactory.

### Objective

Finish Session 17 which was partially started: `BeadingStrategy.hpp` had a partial annotation (file header + Beading struct + compute() done, remaining virtual methods not done). Annotated all remaining files in the BeadingStrategy module and appended Session 17 hazards (176–204) to `04_refactoring_hazards.md`.

### Files Annotated

| File | Lines | Tags Applied | Status |
|------|-------|-------------|--------|
| `BeadingStrategy.hpp` | 210 | INTENT, STATE, COUPLING, MEMORY, HAZARD | Completed (was partial) |
| `BeadingStrategy.cpp` | 107 | INTENT, STATE, HAZARD, COUPLING | Complete |
| `DistributedBeadingStrategy.hpp` | 70 | INTENT, STATE, COUPLING, HAZARD, CONCURRENCY | Complete |
| `DistributedBeadingStrategy.cpp` | 143 | INTENT, STATE, HAZARD, CONCURRENCY | Complete |
| `LimitedBeadingStrategy.hpp` | 103 | INTENT, STATE, MEMORY, COUPLING, HAZARD | Complete |
| `LimitedBeadingStrategy.cpp` | 175 | INTENT, STATE, MEMORY, COUPLING, HAZARD | Complete |
| `RedistributeBeadingStrategy.hpp` | 116 | INTENT, STATE, MEMORY, COUPLING, HAZARD | Complete |
| `RedistributeBeadingStrategy.cpp` | 148 | INTENT, STATE, COUPLING, HAZARD | Complete |
| `WideningBeadingStrategy.hpp` | 111 | INTENT, STATE, MEMORY, COUPLING, HAZARD | Complete |
| `WideningBeadingStrategy.cpp` | 130 | INTENT, STATE, COUPLING, HAZARD | Complete |
| `OuterWallInsetBeadingStrategy.hpp` | 78 | INTENT, STATE, MEMORY, COUPLING, HAZARD | Complete |
| `OuterWallInsetBeadingStrategy.cpp` | 97 | INTENT, STATE, COUPLING, HAZARD | Complete |
| `BeadingStrategyFactory.hpp` | 65 | INTENT, COUPLING, HAZARD | Complete |
| `BeadingStrategyFactory.cpp` | 98 | INTENT, COUPLING, HAZARD | Complete |

### Architectural Insight: Decorator Stack Construction

The factory always assembles the stack in a fixed order:

```
DistributedBeadingStrategy              (base)
└── RedistributeBeadingStrategy         (outer/inner isolation)
    └── [WideningBeadingStrategy]       (optional: print_thin_walls)
        └── [OuterWallInsetBeadingStrategy] (optional: offset != 0)
            └── LimitedBeadingStrategy  (always outermost; inserts 0-width sentinels)
```

**Critical constraint:** `LimitedBeadingStrategy` MUST be outermost because it inserts 0-width sentinel beads that other decorators must not modify.

### Key Discoveries

1. **DistributedBeadingStrategy Gaussian falloff:** Weight function is `w(i) = max(0, 1 - one_over_r² * (i - middle)²)`. For `r=1`, falls back to uniform (1/1²). For `r=2`, `one_over_r² = 1/1² = 1.0` — full Gaussian collapse. Only `r >= 3` gives meaningful distribution.

2. **LimitedBeadingStrategy 0-width sentinel:** When `bead_count > max_bead_count`, two 0-width beads are inserted symmetrically at the innermost wall boundary. These are consumed by infill/skin alignment code in SkeletalTrapezoidation. Any refactor that removes LimitedBeadingStrategy must provide an alternative boundary signal.

3. **RedistributeBeadingStrategy degenerate cases:** For `bead_count <= 2`, the strategy degenerates to purely symmetric outer walls, ignoring the parent strategy entirely. The inner-wall parent is only invoked when `inner_bead_count > 0 && inner_thickness > 0`.

4. **WideningBeadingStrategy getNonlinearThicknesses() unconditional:** `min_output_width` is always prepended for ALL `lower_bead_count` values. In practice only count==0 matters for thin-wall support ribs, but the implementation is not gated on count.

5. **OuterWallInsetBeadingStrategy name typo:** `name = "OuterWallOfsetBeadingStrategy"` (missing 'f'). Both ctor and `toString()` carry this upstream bug. Do NOT rename during annotation or refactoring without updating all string consumers.

6. **BeadingStrategyFactory max_bead_count <= 2 special case:** When `max_bead_count <= 2`, `optimal_width = preferred_bead_width_OUTER` (not inner). This prevents DistributedBeadingStrategy from using the inner-wall width as its base for single/double-wall parts where no inner walls will be generated.

7. **Orca extension in factory:** Negative `outer_wall_offset` (outward shift) is supported. The guard is `!= 0` (not `> 0`). This diverges from upstream CuraEngine which only supported positive (inward) offsets.

### New Hazards Documented

Hazards 176–204 added to `04_refactoring_hazards.md`. Notable P0/P1 items:
- **H176**: `getTransitionAnchorPos()` div-by-zero if `optimal_width == 0`
- **H182**: Negative `to_be_divided` in DistributedBeadingStrategy can produce negative bead widths
- **H184**: LimitedBeadingStrategy overflow case (bead_count >> max) is debug-only guarded
- **H186**: `getOptimalThickness()` returns 1 m sentinel — propagates silently in release
- **H199**: Factory max_bead_count <= 2 special case MUST be preserved
- **H200**: Decorator stack order is fixed; refactor must preserve Distributed→Redistribute→[Widening]→[OuterWallInset]→Limited

### Files Changed This Session

| File | Change |
|------|--------|
| `src/libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp` | Virtual method + protected field annotations completed |
| `src/libslic3r/Arachne/BeadingStrategy/BeadingStrategy.cpp` | File header + all method annotations added |
| `src/libslic3r/Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/WideningBeadingStrategy.hpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/WideningBeadingStrategy.cpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.hpp` | Full annotation |
| `src/libslic3r/Arachne/BeadingStrategy/BeadingStrategyFactory.cpp` | Full annotation |
| `generated_documentation/04_refactoring_hazards.md` | Hazards 176–204 added (Session 17 section) |
| `generated_documentation/agent_journal.md` | Session 17 entry added |

### Next Annotation Targets (Session 18+)

1. `src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp` (~472 lines) — DCEL graph operations
2. `src/libslic3r/GCode/GCodeProcessor.hpp` — header for the 8000-line GCodeProcessor

---

## Session 18 — SkeletalTrapezoidationGraph.cpp (graph mutation layer)

### Files Processed

| File | Status |
|------|--------|
| `src/libslic3r/Arachne/SkeletalTrapezoidationGraph.cpp` | Full annotation complete |
| `src/libslic3r/Arachne/SkeletalTrapezoidationGraph.hpp` | Read for context (not modified — header is clean) |
| `src/libslic3r/Arachne/SkeletalTrapezoidationEdge.hpp` | Read for context |
| `src/libslic3r/Arachne/SkeletalTrapezoidationJoint.hpp` | Read for context |
| `generated_documentation/04_refactoring_hazards.md` | Hazards 205–213 added |
| `generated_documentation/agent_journal.md` | This entry |

### Key Discoveries

**Role of this file:** `SkeletalTrapezoidationGraph.cpp` is the *mutable DCEL layer* of the Arachne pipeline. It provides the data structure (doubly-connected edge list built on top of `HalfEdgeGraph<>`) and the three mutation operations that `SkeletalTrapezoidation.cpp` calls at different stages of pipeline construction:

1. **`collapseSmallEdges(snap_dist=5 nm)`** — post-Voronoi cleanup: removes degenerate quads where nodes are closer than `snap_dist`. Two collapse patterns: top-collapse (mid-edge short) and side-collapse (both side edges short).
2. **`insertNode(edge, mid, bead_count)`** — inserts a skeleton node at a bead-count transition, splitting the original edge and its twin into two fragments each, linked by perpendicular rib pairs.
3. **`makeRib(prev_edge, start, end)`** — attaches a simple perpendicular rib to the polygon boundary when processing a polygon vertex that falls on a Voronoi edge.

**gMAT uphill traversal:** `STHalfEdge::canGoUp()`, `isUpward()`, `distToGoUp()` implement the "uphill" partial order on the medial-axis graph. The key complexity is *equidistant edges* — when `from->distance_to_boundary == to->distance_to_boundary`, all three functions recurse through the outgoing fan. This is safe for valid gMATs but has **no cycle guard (H205)**.

**distance_to_boundary sentinel:** Nodes have `distance_to_boundary = -1` at construction (from `SkeletalTrapezoidationJoint` default ctor). Both `makeRib()` and `insertRib()` explicitly set this to the computed perpendicular distance (skeleton side) or 0 (boundary side). Code that reads `distance_to_boundary` before one of these functions runs will see the -1 sentinel and silently compute wrong bead counts.

**transition_ratio = 0 invariant:** Both `insertRib()` and `insertNode()` set `mid_node->data.transition_ratio = 0` explicitly. This is a contract with `SkeletalTrapezoidation::generateJunctions()`: transition END nodes have zero fractional remainder, meaning a whole number of beads fits at both ends of each transition span.

**Topology invariant — prev==nullptr marks chain heads:** `collapseSmallEdges()` uses `edge.prev == nullptr` to identify chain heads (the start of a "quad"). This invariant must be preserved across all graph mutations. `insertRib()` sets `inward_edge->prev = nullptr` and `outward_edge->next = nullptr` correctly to preserve it for the new rib edges.

**insertRib() leaves twin=nullptr (H213):** The function deliberately leaves `first->twin` and `second->twin` as nullptr on exit — they can only be set AFTER the corresponding call on the twin edge returns. `insertNode()` calls `insertRib()` twice and patches twins afterward. This is a fragile two-step protocol that must be preserved exactly.

### Critical Hazards (Session 18)

- **H205** (P1): Infinite recursion on equidistant-edge cycles in `canGoUp()` / `distToGoUp()` — no cycle guard
- **H209** (P1): `collapseSmallEdges()` dangling-pointer corruption when a node has >1000 outgoing edges — pre-existing upstream bug
- **H213** (P1): `insertRib()` exits with null twin pointers — only safe because `insertNode()` patches them immediately; any standalone call is a crash
- **H212** (P2): `insertRib()` assert(dist > 0) — zero-length ribs in release when mid_node is exactly on source segment
- **H210** (P2): One-sided thin quads survive `collapseSmallEdges()` and may generate zero-width beads downstream

### Next Annotation Targets (Session 19+)

1. `src/libslic3r/GCode/GCodeProcessor.hpp` — header for the G-code processing pass (~500+ lines)
2. `src/libslic3r/GCode/GCodeProcessor.cpp` — the 8000-line G-code processor

---

## Session 19 — GCodeProcessor.hpp (G-code analysis engine header)

### Files Processed

| File | Status |
|------|--------|
| `src/libslic3r/GCode/GCodeProcessor.hpp` | Full annotation complete |
| `generated_documentation/04_refactoring_hazards.md` | Hazards 214–227 added |
| `generated_documentation/agent_journal.md` | This entry |

### Key Discoveries

**Role of this file:** `GCodeProcessor.hpp` declares the data structures and interface for the G-code post-processing and analysis pass. It is the second major pipeline stage after `GCode.cpp` generates the G-code text, and it runs in two modes:
1. **Stand-alone file mode** (`process_file()`): reads a G-code file from disk, used by the G-code viewer.
2. **Pipelined/streaming mode** (`initialize()` → `process_buffer()*` → `finalize()`): processes G-code as it is being generated, used by the slicer for time estimation without a second file read.

**GCodeProcessorResult — the central output:** A flat array of `MoveVertex` records (~80 bytes each) with one entry per G1/G2/G3 move plus synthetic entries for tool-changes, color-changes, and pauses. The viewer indexes into this array randomly for viewport picking and color-coding. Peak RSS can reach several hundred MB for complex prints.

**Tag-driven semantic reconstruction:** GCodeProcessor reconstructs ALL print semantics (layer, role, extruder, seam, wipe, etc.) from comment tags embedded in the G-code by `GCode.cpp`. Two tag sets exist (`Reserved_Tags[]` for BBL printers, `Reserved_Tags_compatible[]` for others), selected by the global `s_IsBBLPrinter` static. The `ETags` enum values are direct array indices into these vectors — ordering is critical (H222).

**Dual TimeMachine simulation:** Two `TimeMachine` instances run simultaneously (Normal, Stealth modes), each simulating a 64-entry firmware motion planner queue with trapezoidal velocity profiles and backward-pass junction smoothing. Time is accumulated as `double` to avoid float precision loss from summing many small increments.

**CommandProcessor trie:** G/M code handlers are registered into a character trie at construction time. `process_command()` dispatches in O(command_length) rather than O(number_of_commands). The `early_quit` flag enables single-letter prefixes (T tool-change) to fire without consuming the rest of the command string.

**Critical bugs found (H215, H220, H221, H222, H226):**
- **H215** (P1): `PrintEstimatedStatistics::reset()` iterates by value — `modes[]` NOT actually cleared.
- **H220** (P1): `result_mutex` is `mutable`/`const` but viewer reads without locking — data race on viewer thread.
- **H221** (P1): Copy-assignment operator silently omits ~9 fields including `backtrace_enabled`, `nozzle_hrc`, `z_offset`.
- **H222** (P1): ETags enum values are raw array indices — reordering without updating both tag arrays corrupts all G-code tag analysis.
- **H226** (P1): `m_print` is a raw non-owning pointer — use-after-free risk on cancellation.

### Next Annotation Targets (Session 20+)

1. `src/libslic3r/Fill/FillRectilinear.cpp` — 4000+ line scan-line infill engine

---

## Session 20 — Fill/FillRectilinear.cpp (scan-line infill engine)

### Files Processed

| File | Status |
|------|--------|
| `src/libslic3r/Fill/FillRectilinear.cpp` | Full annotation complete (65 tags) |
| `generated_documentation/04_refactoring_hazards.md` | Hazards 228–236 added |
| `generated_documentation/agent_journal.md` | This entry |

### Key Discoveries

**Role of this file:** `FillRectilinear.cpp` (~4050 lines) is the core infill generation engine for the entire rectilinear family of patterns: Rectilinear, ZigZag, Monotonic, MonotonicLines, Grid, Triangles, Stars, Cubic, Cross, 3DHoneycomb, Gyroid (sample points), and the Orca-specific FillLockedZag dual-density pattern. It is also the source of anchor points for Lightning infill via `sample_grid_pattern()`.

**Three major path-generation modes:**
1. **Greedy graph traversal** (`traverse_graph_generate_polylines`): walks the scan-line intersection graph left-to-right, using a shortest-distance heuristic to extend polylines. Used for Rectilinear, ZigZag, Grid, Triangles etc.
2. **Monotonic ACO** (`generate_montonous_regions` → `connect_monotonic_regions` → `chain_monotonic_regions` → `polylines_from_paths`): partitions infill lines into monotonic regions then uses Ant Colony Optimization (25 rounds × 10 ants) to sequence them for minimal travel distance. The `monotonic_3_opt()` improvement step inside the ACO loop is a **STUB** — its body is entirely comments; the call is a no-op.
3. **Multiline/trapezoidal** (`fill_surface_by_multilines`, `fill_surface_trapezoidal`): generates multi-pass sweeps (Grid = 2 sweeps, Triangles = 3) or explicit trapezoid geometry for Orca-specific patterns.

**`SegmentIntersection` — core data structure:**
- Stores y-coordinates as a rational `pos_p / pos_q` (int64 / uint32) to avoid floating-point errors in sort comparisons.
- `operator<` uses a 96-bit integer comparison trick — splits the 64-bit×32-bit product into two 64-bit products — to compare rationals exactly without overflow.
- `pos_q == 0` is UB (division by zero in `pos()`); asserted in `operator<` but not defensively checked elsewhere.
- Intersection types: `OUTER_LOW`, `OUTER_HIGH`, `INNER_LOW`, `INNER_HIGH` — one per polygon contour-crossing.

**`ExPolygonWithOffset` — dual-shell input:**
- Holds the source expolygon plus two Clipper-offset levels: `outer` (perimeter join boundary) and `inner` (actual infill boundary).
- The dual-offset is critical for the perimeter-walk connector: infill lines that end near a perimeter are linked by walking the `outer` shell, allowing the head to stay on plastic rather than travel through air.
- If `remove_sticks()` / `remove_small()` strip too many contours, `n_contours_inner == 0` and the engine silently produces no infill for that region.

**`slice_region_by_vertical_lines()` — scan-line engine:**
- Casts N vertical lines (spacing determined by `FillParams::line_spacing`) through the offset polygons.
- Intersections are classified and sorted per vertical line; linkage directions (up/down, inner/outer) are inferred from crossing order.
- Under `INFILL_DEBUG_OUTPUT` mode, a validation block uses `try/catch(InfillFailedException)` for graceful degradation — not used in production builds.
- `INFILL_OVERLAP_OVER_SPACING = 0.45` is a hardcoded magic constant controlling how far infill lines extend past the inner boundary to ensure overlap with perimeters; changing it alters dimensional accuracy.

**`connect_segment_intersections_by_contours()` — graph edge builder:**
- Links adjacent intersection pairs via perimeter-walk arcs (Valid) or marks them TooLong or Invalid.
- The inner search loop is `O(n)` per intersection point (O(N²) total for complex surfaces). A FIXME comment acknowledges this.
- All `Invalid` symmetry fixups are applied at the end of each vline pass to ensure bidirectional consistency.

**`MonotonicRegion` / `AntPathMatrix` / `chain_monotonic_regions()` — ACO sequencing:**
- A `MonotonicRegion` is a maximal band of scan lines traversable without direction reversal.
- `AntPathMatrix` is a dense 2D matrix of `(n_regions × 2)²` `AntPath` entries; O(4·N²) memory — a comment suggests sparse representation but it is not implemented.
- ACO: 25 rounds × 10 ants; pheromone evaporation + diversification each round.
- `monotonic_3_opt()` is an **empty stub** (body is all comments). The ACO loop calls it but it performs no work. This is the most critical undocumented fact in the file.

**Typo:** `generate_montonous_regions` / `montonous_region_path_length` — "montonous" should be "monotonous" throughout. **Do not fix** — renaming would break external callers.

**`FillLockedZag` — Orca-specific dual-density infill:**
- Not present in PrusaSlicer. Implements skin+skeleton infill where a sparse skeleton is overlaid with a dense skin in selected regions.
- `lock_param` struct carries `skeleton_density_params` and `skin_density_params` maps; populated by PrintObject from per-region config.
- Output is `multi_width_polyline`: a `vector<pair<Polylines, Flow>>` where each sub-vector carries its own Flow object for independent extrusion width calculation.
- The `overlap_threshold` offset for the overlap region can cause skeleton+skin double-printing with over-extrusion if the threshold is set too high.

**Dead code:** `FillMonotonicLineWGapFill` class (lines 3692–3885) is entirely commented out. It was the predecessor to the current `FillMonotonicLines` + FillBase gap-fill approach. Still present in the file, slowing reader comprehension.

**`sample_grid_pattern()` — Lightning infill anchor points:**
- Reuses `slice_region_by_vertical_lines()` with zero offsets to generate a regular grid of candidate anchor points.
- Lightning infill (`FillLightning`) depends on the exact inter-point spacing guarantee from this function.

### Critical Hazards Found (H228–H236)

| # | Description | Priority |
|---|-------------|----------|
| H228 | `connect_segment_intersections_by_contours()` inner search O(N) per point → O(N²) total for complex surfaces | P2 |
| H229 | `monotonic_3_opt()` is a no-op stub — ACO does not perform 3-opt improvement; final paths are locally suboptimal | P1 |
| H230 | `AntPathMatrix` allocates O(4·N²) AntPath entries — dense even for sparse region graphs; can reach hundreds of MB for very complex surfaces | P2 |
| H231 | `INFILL_OVERLAP_OVER_SPACING = 0.45` is a hardcoded magic constant; undocumented; changing it breaks dimensional accuracy silently | P3 |
| H232 | `FillLockedZag` overlap region uses `intersection_ex` + `offset_ex` with `overlap_threshold` — threshold too high causes skeleton+skin double-print over-extrusion | P2 |
| H233 | Dead code block `FillMonotonicLineWGapFill` (lines 3692–3885) still present; confuses readers and may cause merge conflicts | P3 |
| H234 | `fill_surface_by_multilines()` divides `params.density` by `sweep_params.size()` — caller must ensure density is pre-multiplied or pattern density will be wrong | P2 |
| H235 | `fill_surface_trapezoidal()` period computation casts float to int coords — potential rounding at high zoom / large print coordinates | P3 |
| H236 | `pos_q == 0` in `SegmentIntersection` is UB (zero-denominator rational); asserted in debug builds only; malformed input can reach this silently in release | P1 |

### Next Annotation Targets (Session 21+)

1. `src/libslic3r/Fill/FillBase.cpp` — 2782 lines, factory + gap fill pipeline (highest priority)

---

## Session 21 — Fill/FillBase.cpp (infill connection engine, 3310 lines)

### Files Processed
- `src/libslic3r/Fill/FillBase.cpp` (lines 1–3310, full file)

### Key Discoveries

**Factory and statics:**
- `Fill::new_from_type()` returns a raw owning pointer; no `unique_ptr`. All call sites must delete or use a managing wrapper.
- `Fill::infill_anchor` / `infill_anchor_max` are static class members — if two `Print` instances with different profiles exist simultaneously, the last write wins.
- `use_bridge_flow_initializer` forces static initialization of the `cached` vector to prevent first-call data races, but relies on translation-unit static init ordering — fragile.

**Gap fill pipeline:**
- `fill_surface_extrusion()` (line 178): if both `fill_surface` and `fill_surface_arachne` throw `InfillFailedException`, `out` is unchanged and gap fill is skipped — narrow regions silently produce no output.
- `polygons_covered_by_spacing(10)` uses a hardcoded `10` (10 nm) integer tolerance in `make_fill_extrusion()` — not scaled with `SCALING_FACTOR`.
- `medial_axis()` can generate very short spurs; `filter_out_gap_fill` config parameter is `0` by default, passing all degenerate 1-point lines.

**Spacing math:**
- `align_to_grid()` in `adjust_solid_spacing()` (line ~318): when `number_of_intervals == 0`, returns original distance unchanged instead of empty, producing a single-line overfill.
- `FLT_MAX` slip-through in `_fill_angle()` (line ~348): if no angle is ever set, a `printf` warning fires and the angle becomes `0 + π/2 = 90°`.

**Contour walking helpers:**
- `clip_end_segment_and_point` uses `int i` for reverse iteration — polylines with > INT_MAX points would overflow; not realistic but documents the implicit assumption.
- `add_at_start` path (line 724) moves `pl1.points` into a temporary and rebuilds — an exception mid-rebuild leaves `pl1` corrupt; only avoided in practice because `move` itself is noexcept.

**`BoundaryInfillGraph` — linked-list stability:**
- `map_infill_end_point_to_boundary` (vector of `ContourIntersectionPoint`) **must not be resized** after linked-list construction; all graph pointers point into it. Reallocation = dangling pointer UB (H237 — P1).
- `create_boundary_infill_graph()` silently produces `boundary_idx_unconnected` nodes when endpoint snapping fails (distance > 3 scaled units); no release warning (H238 — P2).
- `assert(infill_polyline.size() == 2)` in `take()` fires only in debug; multi-segment polylines silently use only first and last point (H239 — P2).

**`connect_infill()` — arc connection strategy:**
- `extended_object_bounding_box()` scales by `sqrt(2)` with float-to-coord_t cast; large objects near coord_t overflow produce a truncated bounding box (H240 — P2).
- `#if 0` dead code block (lines ~1861–1934): earlier cost-sorted connection strategy, kept as reference.
- `params.multiline > 1` skips arcs shorter than `spacing×multiline` — Orca-specific guard (H241 — P3).

**`base_support_extend_infill_lines()` — ternary-as-lvalue:**
- `dist_y_prev < dist_y_next ? extend_prev_idx : extend_next_idx = -1;` (line ~2172) — ternary used as lvalue. Legal C++ but will not compile in Rust/Python without rewrite to `if/else` (H242 — P2).

**`emit_loops_in_band()` — division by zero:**
- `add_interpolated_point()` divides by `p2->x() - p1->x()` — exactly vertical contour segments produce division by zero (H243 — P1).
- `m_polyline.points.erase(begin() + m_polyline_end)` in finalize: index overflow UB if logic error in ordering; only guarded by debug assert (H244 — P2).

**`connect_base_support()` — misleading static:**
- `static const double cost_low/high/veryhigh` (line 2728): these are re-initialized each call because `line_spacing` differs per call. The `static` keyword is misleading; in a multithreaded context it would cause data races (H245 — P2).
- `#if 0` dead code block (lines ~2779–2848): alternate horizontal arch pass, kept as reference.

**`multiline_fill()` — Clipper2 offsetter reuse:**
- `static_cast<int>(polylines.size())` (line 3231): overflows for > INT_MAX polylines; not realistic but undocumented (H246 — P3).
- Clipper2 `Execute` called multiple times after single `AddPaths` — correct Clipper2 usage but not obviously safe to a reader (H247 — P3).

### Critical Hazards Found (H237–H247)

| # | Description | Priority |
|---|-------------|----------|
| H237 | `BoundaryInfillGraph::map_infill_end_point_to_boundary` must not be moved/resized after linked-list pointers are set; reallocation produces dangling pointer UB with no compile-time enforcement | P1 |
| H238 | `create_boundary_infill_graph()` silently emits `boundary_idx_unconnected` nodes when infill endpoint snapping fails (distance > 3 scaled units); no release-build warning | P2 |
| H239 | `assert(infill_polyline.size() == 2)` in `take()` fires only in debug; multi-segment polylines from non-rectilinear fills silently use only first and last points, corrupting the arc walk | P2 |
| H240 | `extended_object_bounding_box()` scales by `sqrt(2)` with float→coord_t cast; near coord_t overflow, silently truncates the bounding box producing incorrect connection distance limits | P2 |
| H241 | `params.multiline > 1` skips arcs shorter than `spacing×multiline` — Orca-specific guard undocumented; ported code must preserve or the arc quality degrades on multiline infill | P3 |
| H242 | `dist_y_prev < dist_y_next ? extend_prev_idx : extend_next_idx = -1;` — ternary-as-lvalue idiom (legal C++17); must be rewritten as `if/else` in any port to Rust, Python, or Go | P2 |
| H243 | `emit_loops_in_band()`: `add_interpolated_point()` divides by `p2->x() - p1->x()` — exactly vertical contour segment crossing a band boundary produces integer division by zero | P1 |
| H244 | `emit_loops_in_band()` finalize: `m_polyline.points.erase(begin() + m_polyline_end)` — if ordering logic sets `m_polyline_end >= m_polyline.size()`, UB; only debug assert guard | P2 |
| H245 | `connect_base_support()`: `static const double cost_low/high/veryhigh` depend on per-call `line_spacing`; `static` has no practical effect now but would cause data races if ever parallelized | P2 |
| H246 | `multiline_fill()`: `static_cast<int>(polylines.size())` silently overflows for > INT_MAX polylines; not realistic but not documented | P3 |
| H247 | `multiline_fill()`: Clipper2 `Execute` called multiple times after a single `AddPaths` — correct Clipper2 usage but not obviously safe to a port; requires a comment or refactor to be clear | P3 |

### Next Annotation Targets (Session 22+)

1. Update `04_refactoring_hazards.md` with H237–H247
2. `src/libslic3r/Fill/FillBase.hpp` — base class, `FillParams` struct
3. `src/libslic3r/Fill/FillAdaptive.cpp` — adaptive cubic infill

---

## Session 22 — Fill/FillAdaptive.cpp (adaptive cubic infill)

### Files Processed
- `src/libslic3r/Fill/FillBase.hpp` — ✅ committed `ed780e47b3` (annotated session start)
- `src/libslic3r/Fill/FillAdaptive.cpp` — ✅ committed `8c2c401236` (full annotation this session)

### Architecture Discoveries

**Overall Pipeline (build_octree → _fill_surface_single)**

Adaptive Cubic Infill is a two-phase algorithm:
1. **Build phase** (once per PrintObject): `build_octree()` → `Octree::insert_triangle()` subdivides an octree over the mesh surface. Cells containing surface triangles are subdivided recursively. Uses `triangle_AABB_intersects()` (Ericson SAT test) per child cell. After construction, all `Cube::center` values are transformed to world space by `transform_center()`.
2. **Slice phase** (per layer/ExPolygon): `_fill_surface_single()` creates 3 `FillContext` objects (one per direction 0°, +120°, -120°), calls `generate_infill_lines_recursive()` DDA-traversal on each, clips resulting lines to the ExPolygon via `intersection_pl()`, then connects T-joints via `connect_lines_using_hooks()`, and chains via `chain_or_connect_infill()`.

**Octree Memory Model**
- All `Cube` nodes are allocated from `boost::object_pool<Cube>` — no individual `new`/`delete`
- `BOOST_POOL_NO_MT` disables pool mutex — pool is single-threaded only
- Pool is destroyed atomically when the `Octree` is deleted via `OctreeDeleter`
- `OctreePtr = std::unique_ptr<Octree, OctreeDeleter>` held by `PrintObject`
- After `build_octree()` returns, the octree is immutable — multiple TBB fill tasks read it concurrently without any locking

**FillContext and DDA traversal**
- `FillContext::temp_lines` is a spanning array of size `(1 << depth) - 1` (full binary tree)
- Indexed by binary tree address (`address*2+1` for left child, `+2` for right)
- This allows O(1) line extension across adjacent octree cells without sorting
- `child_traversal_order` ensures monotonic traversal per direction — essential for the O(1) merge trick
- Gap threshold `> 1000` (not `SCALED_EPSILON = 100`) prevents spurious extensions across non-adjacent cells

**connect_lines_using_hooks() — T-joint anchoring**
- Builds a Boost.Geometry `bgi::rstar<16,4>` R*-tree with `float` coordinates (coord_t overflows `bgi::intersects`)
- Three passes: (1) merge close collinear segments, (2) detect T-joints, (3) add hooks or bridge
- `merged_with[]` is a union-find array for tracking polyline merges — iterative path-compression, no rank
- `Intersection` struct carries raw pointers into `lines` (Polyline*) and `lines_src` (Line*) — these vectors must not reallocate during the hook loop
- Boundary polygon segments are inserted into the rtree after infill lines to prevent hooks crossing the boundary

**Orca-specific additions**
- `make_cubes_properties()`: if only 1 level would be produced (low density), a second level is force-added to ensure octree subdivision actually occurs — fixes disconnected infill at low densities
- `n_multiline`: reads `printing_region(0).config().fill_multiline` and scales line spacing; `_fill_surface_single()` skips hooks for multiline > 1
- Dead `#if 0` block in `_fill_surface_single()` (old per-direction clipping approach) replaced by single `intersection_pl()` call on all directions combined

**Coordinate system**
- Octree is built in octree-rotated space (`transform_to_octree()`) so walls are axis-aligned
- After construction, `transform_center()` rotates all `Cube::center` to world space
- `octree_rot[3]` = {5π/4, 215.264°, π/6} — fixed Euler angles (ZYX) for the cube-on-corner orientation

### Critical Hazards Found (H251–H263)

| # | Description | Priority |
|---|-------------|----------|
| H251 | `BOOST_POOL_NO_MT` disables pool mutex — `boost::object_pool<Cube>` is NOT thread-safe for concurrent `construct()` calls. Safe now (single-threaded build), but any future parallelization of `insert_triangle()` would cause a data race with no compile-time warning | P2 |
| H252 | `Intersection*` pointers stored in `intersections` vector point into `lines` (Polyline*) and `lines_src` (Line*). Neither `lines` nor `lines_src` may be resized/reallocated after these pointers are taken. The invariant is upheld by construction order but has no compile-time enforcement (no `const &` or freeze mechanism) | P1 |
| H253 | `generate_infill_lines_recursive()`: gap threshold `> 1000` scaled units is a magic constant. Not `SCALED_EPSILON` (= 100); intentional but undocumented. A port that uses SCALED_EPSILON will produce incorrect line merging | P2 |
| H254 | `adaptive_fill_line_spacing()`: averages density and extrusion width across all regions with adaptive fill. Multiple regions with wildly different settings produce a single octree cell size that may suit none of them. Architectural simplification from PrusaSlicer | P3 |
| H255 | `adaptive_fill_line_spacing()`: reads `n_multiline` from `printing_region(0)` only — ignores per-region multiline settings. If regions have different multiline counts, the octree cell spacing will be wrong for all but region 0 | P2 |
| H256 | `update_merged_polyline_idx()` is iterative path-compression union-find without rank balancing. O(depth) per call; for adversarial merge orders (e.g., always merging longer chain onto shorter), degrades to O(N) per lookup, O(N²) total for N merges | P3 |
| H257 | `rtree_t` uses `float` coordinates. For slicer-scale coordinates (~10⁶ units), precision loss is ~0.1 units (~0.0001 mm). Hook endpoints computed from float rtree queries may be off by up to 0.1 scaled unit. Not user-visible but worth noting for a port using integer geometry | P3 |
| H258 | `create_offset_line()` extends lines by factor 1.16 ≈ 1/cos(π/6). This hardcoded factor is specific to 60° infill intersection angles. If infill angles are changed (e.g., for square-cubic hybrid), the factor must be recomputed | P2 |
| H259 | `is_overhang_triangle()`: threshold `n.dot(up) > 0.707 * n.norm()` uses `n.norm()` (square root per triangle). Could use `n.squaredNorm()` and `0.707² × n.squaredNorm()` to avoid sqrt at no loss of precision | P3 |
| H260 | `build_octree()`: if `cubes_properties.size() <= 1` (make_cubes_properties Orca guard forces size >= 2, but before that guard was added), the `if (cubes_properties.size() > 1)` block is entirely skipped — no triangles are inserted, octree has only a root cube, and all layers get empty infill. The guard is the fix, but the root cause (line_spacing > mesh_size) can still occur for tiny meshes | P2 |
| H261 | `Octree::insert_triangle()`: `--depth` occurs before the loop. If called with `depth == 0` (should never happen per `assert(depth > 0)`), `cubes_properties[depth-1]` = `cubes_properties[-1]` → UB. The assert protects only in debug builds | P1 |
| H262 | `connect_lines_using_hooks()` `filter_itself` lambda: `(intersection.intersect_line - lines_src.data())` relies on pointer subtraction into `lines_src`. If `lines_src` is ever `std::deque` or similar non-contiguous container, this is UB. Currently `std::vector` so contiguous; must remain so | P2 |
| H263 | Dead `#if 0` block in `connect_lines_using_hooks()` (self-intersection avoidance for bridge connections, lines ~1219–1225). Was disabled because trimming-after-connection was deemed sufficient. If re-enabled without the matching trim logic, bridges may self-intersect | P3 |

### Next Annotation Targets (Session 23+)

1. `src/libslic3r/Fill/FillAdaptive.hpp` (80 lines, quick)
2. `src/libslic3r/Fill/FillLightning.cpp` (Lightning infill — Cura-derived)
3. `src/libslic3r/Fill/FillConcentric.cpp`
4. `src/libslic3r/Geometry/` directory
4. `src/libslic3r/Fill/FillLightning.cpp` — lightning infill

---

## Session 23 — Fill/FillLightning.cpp + Fill/Lightning/* (Lightning Infill subsystem)

### Files Processed
- `src/libslic3r/Fill/FillLightning.cpp` — ✅ fully annotated (adapter + factory layer)
- `src/libslic3r/Fill/FillLightning.hpp` — ✅ fully annotated (Filler class, GeneratorDeleter, GeneratorPtr)
- `src/libslic3r/Fill/Lightning/Generator.hpp` — ✅ fully annotated (top-level orchestrator)
- `src/libslic3r/Fill/Lightning/Generator.cpp` — ✅ fully annotated (two construction paths, overhang generation, tree build)
- `src/libslic3r/Fill/Lightning/Layer.hpp` — ✅ fully annotated (per-layer tree container + SparseNodeGrid)
- `src/libslic3r/Fill/Lightning/Layer.cpp` — ✅ fully annotated (generateNewTrees, getBestGroundingLocation, attach, reconnectRoots, convertToLines)
- `src/libslic3r/Fill/Lightning/TreeNode.hpp` — ✅ fully annotated (Node class, factory pattern, all member declarations)
- `src/libslic3r/Fill/Lightning/TreeNode.cpp` — ✅ fully annotated (all member implementations, DFS algorithms)

### Architecture Discoveries

**Lightning Infill Algorithm Overview (Cura-derived)**

Lightning Infill is a tree-based algorithm producing minimal-material infill that supports top surfaces. The algorithm name derives from the fractal-lightning appearance of the resulting patterns. Origin paper: "Ribbed Support Vaults for 3D Printing of Hollowed Objects" (Tricard, Claux, Lefebvre).

**Two-Phase Construction**
1. `generateInitialInternalOverhangs()` (top→bottom): For each layer, compute `diff(offset(infill_area, -m_wall_supporting_radius), infill_area_above)` — the area of this layer that is NOT directly supported by the layer above. These "internal overhangs" are what the trees must support.
2. `generateTrees()` (top→bottom, then bottom→top for the tree build): For each layer, call `Layer::generateNewTrees()` to grow new branches, `Layer::reconnectRoots()` to reattach orphaned roots, and `Node::propagateToNextLayer()` to project the trees down one layer.

**Tree Node Structure**
- Each `Node` is a 2D point in the layer plane, connected to 0 or 1 parent and 0+ children.
- Memory model: shared_ptr parent (ownership); weak_ptr child-to-parent (cycle prevention).
- `enable_shared_from_this` enables nodes to produce `shared_ptr<this>` safely.
- Factory-only construction via `Node::create()` (EnableMakeShared workaround).
- Complete RAII: dropping the last shared_ptr to a root destroys the entire subtree.

**Per-Layer Growth Algorithm (`Layer::generateNewTrees`)**
1. Build `DistanceField` from `current_overhang` — samples unsupported area at grid spacing, sorted by distance-to-boundary descending.
2. Build `SparseNodeGrid` (4 mm cells) from existing `tree_roots` (propagated from layer above).
3. Loop: take next unsupported cell → `getBestGroundingLocation()` (TBB parallel tree-node search + O(N) boundary scan) → `attach()` → update locator.
4. Each attached node reduces the surrounding overhang cells in the distance field.

**Cross-Layer Propagation (`Node::propagateToNextLayer`)**
- `deepCopy()` → `prune(m_prune_length)` → `straighten(m_straightening_max_distance)` → `realign(next_outlines)`
- Pruning removes leaf endpoints that would overhang too far for the next layer.
- Straightening nudges single-child chains toward the junction-to-junction axis.
- Realign snaps out-of-bounds nodes to the new layer outline.

**Adapter Pattern (FillLightning.hpp/cpp)**
- `Filler` is a thin adapter: holds a raw non-owning pointer to the Generator.
- Generator is built once per PrintObject via `build_generator()` → `GeneratorPtr`.
- `GeneratorDeleter` enables opaque unique_ptr (PImpl-lite) to avoid incomplete-type deletion.
- Per-layer fill call: `getTreesForLayer()` → `convertToLines()` → `multiline_fill()` → `intersection_pl()` → `chain_or_connect_infill()`.

**OrcaSlicer-Specific Additions**
- `multiline_fill()` integration: Orca's multi-line offset applied before final Clipper clip.
- `generateTreesforSupport()`: secondary constructor for tree-support usage, different `m_supporting_radius` formula.
- Divide-by-zero guard in Generator constructor (density clamped before division).

**Namespace Comment Bug (Copy-Paste)**
- Both `FillLightning.cpp` (line 151) and `FillLightning.hpp` (line 125) have namespace closing comments saying `FillAdaptive` instead of `FillLightning`. These are stale copy-paste errors from FillAdaptive files. The actual braces are syntactically correct. Documented as H265, H266.

**Non-Determinism (rand())**
- `Node::convertToPolylines()` uses `rand() % m_children.size()` to pick which child extends the "long" polyline at each junction. In release builds (debug helper `get_svg_filename()` never called), `rand()` uses default seed 1 — technically deterministic but machine-dependent if the debug path was ever hit. Documented as H273.

### Critical Hazards Found (H264–H310)

| # | Description | Priority |
|---|-------------|----------|
| H264 | `GeneratorDeleter` pattern: if Generator.hpp included before FillLightning.hpp, compiler may allow `unique_ptr<Generator>` without custom deleter. On MSVC/MT: heap corruption on cross-TU delete. Always use `GeneratorPtr` | P1 |
| H265 | FillLightning.cpp line 151 namespace comment bug: says `FillAdaptive` not `FillLightning` | P3 |
| H266 | FillLightning.hpp line 125 same namespace comment copy-paste bug | P3 |
| H267 | `Filler::generator` raw non-owning pointer; no null-check in `_fill_surface_single()`; dangling if PrintObject destroyed mid-slice | P1 |
| H268 | `layer_id` index not range-checked in release builds; out-of-bounds access is UB | P1 |
| H269 | `multiline_fill()` may push lines outside expolygon; double-clip is correct but hidden | P2 |
| H270 | Two constructors have different `m_supporting_radius` formulas; using infill formula for support (or vice versa) produces wrong branch density | P1 |
| H271 | Partial-construction if `throw_on_cancel_callback` throws; no guard | P2 |
| H272 | All three angle-derived parameters hardcoded at 45°; no user configuration | P2 |
| H273 | `rand()` used for non-deterministic child selection in `convertToPolylines()` | P2 |
| H274 | `rand_num` computed but unused in `get_svg_filename()` — dead code | P3 |
| H275 | Negative-area polygon from Clipper offset silently accepted as "no overhang" | P2 |
| H276 | EdgeGrid rebuilt per layer in `generateTrees()` — O(N_edges × N_layers) total cost | P3 |
| H277 | Cancel callback called once per layer — coarse-grained cancel response | P3 |
| H278 | `bboxs` (typo) must match `m_lightning_layers` size; no enforcement | P2 |
| H279 | `generateTreesforSupport()` takes non-const reference vectors; caller must keep alive | P2 |
| H280 | `DistanceField` not annotated; must preserve interior-first growth order in port | P2 |
| H281 | SparseNodeGrid locator not updated immediately after `attach()`; brief inconsistency window | P2 |
| H282 | Closest-boundary scan in `getBestGroundingLocation()` is O(N_vertices) with no spatial index | P2 |
| H283 | TBB parallel_for in `getBestGroundingLocation()` has no minimum-cell-count guard | P3 |
| H284 | Tie-breaking by lexicographic grid address — algorithmic, not quality-based | P3 |
| H285 | `reconnectRoots()` linear scan `std::find` — O(N²) for large forests | P3 |
| H286 | `PointHash` quality not validated; poor distribution degrades lookup to O(N) | P3 |
| H287 | `GroundingLocation::p()` assert debug-only; release crash on default-constructed instance | P1 |
| H288 | `Layer::tree_roots` is public — unguarded modification can leave SparseNodeGrid stale | P2 |
| H289 | `getBestGroundingLocation()` boundary scan O(N_vertices) with EdgeGrid available but unused for this step | P2 |
| H290 | `Layer::attach()` no capacity pre-reservation; reallocation possible | P3 |
| H291 | `reconnectRoots()` UB if root already removed before call: `erase(end())` | P1 |
| H292 | `convertToLines()` single Clipper call for all trees; dominates runtime for complex outlines | P3 |
| H293 | `convertToPolylines()` polyline order non-deterministic across machines (rand seed) | P3 |
| H294 | `getWeightedDistance()` double→coord_t truncation; silent overflow for >2.1 km | P3 |
| H295 | Negative grid keys valid but confusing when node outside bbox | P3 |
| H296 | `fillLocator()` does not clear locator first; stale entries accumulate if non-empty locator passed | P3 |
| H297 | DistanceField sub-cell overhangs produce no infill; silent unsupported areas | P2 |
| H298 | Cancel callback called once per unsupported point; I/O in callback = throughput bottleneck | P3 |
| H299 | Null `new_root` after `attach()` returns true would skip locator insert; latent bug | P3 |
| H300 | `locator_cell_size()` free function; inlining not guaranteed; per-call overhead in inner loop | P3 |
| H301 | `EnableMakeShared` trick is C++-specific; port must replicate factory-only construction intent | P2 |
| H302 | `addChild()` no cycle check in release builds | P2 |
| H303 | Multiple recursive DFS functions; stack depth = tree depth; must be iterative for Go/Python ports | P2 |
| H304 | `getWeightedDistance()` hardcoded valence constants; must be preserved exactly in port | P3 |
| H305 | `reroot()` O(depth) recursive; same stack concern as H303 | P3 |
| H306 | `closestNode()` O(N_nodes) linear scan; O(N_roots × N_nodes) in reconnectRoots | P2 |
| H307 | All recursive DFS: Go (8 KB goroutine), Python (1000-frame limit) will overflow for trees > ~500 levels | P1 |
| H308 | `straighten()` floating-point → coord_t rounding accumulates over many passes | P3 |
| H309 | `removeJunctionOverlap()` swap-and-pop destroys polyline ordering | P3 |
| H310 | `m_parent` weak_ptr: lock() returns nullptr if parent destroyed; code not always null-checking | P2 |

### Next Annotation Targets (Session 24+)

1. `src/libslic3r/Fill/FillConcentric.cpp` — concentric infill algorithm
2. `src/libslic3r/Fill/FillGyroid.cpp` — gyroid/honeycomb infill
3. `src/libslic3r/Geometry/` directory — computational geometry utilities
4. `src/libslic3r/Format/` directory — STL/OBJ/3MF/AMF parsers
5. `src/libslic3r/SupportMaterial.cpp` — classic (non-tree) support generation

---

## Session 24 — Fill/FillConcentric.hpp/.cpp + Fill/FillGyroid.hpp/.cpp

### Files Processed
- `src/libslic3r/Fill/FillConcentric.hpp` — concentric infill class declaration
- `src/libslic3r/Fill/FillConcentric.cpp` — concentric infill implementation
- `src/libslic3r/Fill/FillGyroid.hpp` — Gyroid TPMS infill class declaration
- `src/libslic3r/Fill/FillGyroid.cpp` — Gyroid TPMS infill implementation

### Key Discoveries

**FillConcentric**

`FillConcentric` generates shell-like concentric loops shrinking inward by the line spacing. The implementation:
1. Forces `fill_params.dont_adjust = true` (no spacing adjustment for integer loop count).
2. Calls `union_ex()` on all input expolygons before the loop (defensive for multi-region inputs).
3. Iteratively shrinks using `offset2_ex()` to avoid sharp-corner artifacts that `offset_ex()` produces.
4. Collects all loops into `all_polylines` and calls `chain_or_connect_infill()` for travel ordering.

Key omissions: no `multiline_fill()` support; `FillConcentricInternal` (for support interfaces) is a separate class with different chaining behavior.

**FillGyroid**

`FillGyroid` generates cross-sections of the Gyroid TPMS (triply periodic minimal surface). The implementation is the most mathematically complex infill in OrcaSlicer:

- Static function `f(x, z_sin, z_cos, vertical, flip)` evaluates the 2D Gyroid curve y = f(x) at fixed Z by solving the implicit TPMS equation. Uses `asin()` without a guard against NaN from floating-point overflow past ±1 (H321).
- `make_one_period()` adaptively samples the curve using a midpoint-subdivision loop with a triangle-area heuristic. Convergence is O(N² log N) in the worst case due to full `std::sort()` on each refinement pass (H322).
- `make_wave()` tiles one period across the full bounding-box width, applies row offset, clamps to [0, height], and converts to scaled `coord_t`.
- `make_gyroid_waves()` drives the full generation: computes z_sin/z_cos, chooses horizontal vs. vertical orientation, generates two period templates (odd/even), and iterates rows at π spacing. The loop body mutates `y0` (for the even row), creating an effective 2π step per iteration — looks like a double-increment but is intentional (H328).

**CorrectionAngle = -45°:** rotates the pattern for diagonal alignment on Cartesian printers.
**DensityAdjust = 2.44:** empirical factor; Gyroid surface occupies ~41% of space at "100%" density.
**PatternTolerance = 0.2 mm:** adaptive sampling tolerance; constexpr ODR fix in .cpp (H320).

### Hazard Summary (H311–H330)

| Hazard | Brief | Priority |
|--------|-------|----------|
| H311 | `dont_adjust=true` forces no spacing adjustment; innermost loop may clip thin walls | P2 |
| H312 | Defensive `union_ex()` on every call adds Clipper overhead even for single-expolygon input | P3 |
| H313 | Polyline order after union non-deterministic w.r.t. original polygon ordering | P3 |
| H314 | `offset2_ex()` can return degenerate single-point polygons (area≈0) that pass empty check | P2 |
| H315 | Wrong-winding holes in degenerate Clipper output treated as concentric loops | P2 |
| H316 | `multiline_fill()` never called; `params.multiline > 1` silently ignored by FillConcentric | P2 |
| H317 | `FillConcentricInternal::fill_surface()` is a distinct code path without `chain_or_connect_infill()` | P2 |
| H318 | `loop_clipping` applied only inside `chain_or_connect_infill()`; missing in a naive port = seam over-extrusion | P2 |
| H319 | `use_bridge_flow()` returns false; dead comment claims it should return true | P2 |
| H320 | `constexpr PatternTolerance` ODR fix in .cpp; redundant in C++17, required in C++14 | P3 |
| H321 | `asin(a/r)` in `f()`: no clamp guard against NaN from FP rounding past ±1 | P1 |
| H322 | `make_one_period()` sort-on-every-pass: O(N² log N) worst case | P3 |
| H323 | `make_gyroid_waves()` swaps width/height for vertical orientation; callers must not assume axis | P2 |
| H324 | (repeat of H321 with extra context) asin domain error, NaN propagates silently | P1 |
| H325 | `make_wave()` index-based tiling into same vector being appended: correct but fragile | P2 |
| H326 | Clamping y to [0, height] creates flat wave ends at boundary | P3 |
| H327 | Triangle-area heuristic (not true chord-height) for adaptive sampling | P3 |
| H328 | `y0 += M_PI` inside loop body: intentional 2π step; looks like double-increment bug | P2 |
| H329 | 10× spacing bounding-box expansion: wasteful wave generation for low-density fills | P3 |
| H330 | `params.multiline == 0` → division by zero in density_adjusted calculation; no guard | P2 |

### Next Annotation Targets (Session 25+)

1. `src/libslic3r/Fill/Fill3DHoneycomb.cpp/.hpp` — 3D honeycomb infill
2. `src/libslic3r/Fill/FillHoneycomb.cpp/.hpp` — 2D honeycomb infill
3. `src/libslic3r/Fill/FillPlanePath.cpp/.hpp` — Hilbert / Archimedean / Octagram spiral fills
4. `src/libslic3r/Fill/FillLine.cpp/.hpp` — rectilinear line fill variant
5. `src/libslic3r/Fill/FillCrossHatch.cpp/.hpp` — cross-hatch infill
6. `src/libslic3r/Fill/Fill.cpp/.hpp` — the Fill factory dispatch

---

## Session 26 — Layer Lifecycle & External Surface Expansion

**Date:** 2026-03-08
**Branch:** agent/analysis
**Iteration:** 2 of 10
**Hazard range this session:** H352–H391

### Files Annotated

#### `BridgeDetector.hpp` (H331–H334)
- Bridge detection interface: `angle` is only valid after `detect_angle()` returns true.
- Coverage polygon and unsupported edges are separate outputs that must be requested explicitly.
- `detect_angle()` stores the *opposite* of the span direction (π offset).

#### `BridgeDetector.cpp` (H335–H349)
- `detect_angle()` sweeps candidate angles from the bridge span's PCA direction.
- Convex hull anchor detection: `coverage_query_points` uses convex hull of anchors, not full polygon.
- `unsupported_edges()` computes edges by diff of bridge outline against inflated anchor polygons.

#### `Layer.hpp` / `Layer.cpp` (H350–H362)
- `LayerRegion*` raw pointers in `m_regions` — manual delete in destructor (H352).
- `make_slices()` chain_points heuristic: non-deterministic multi-island ordering (H353).
- `backup_untyped_slices()` / `restore_untyped_slices()` dual-state pipeline (H354).
- `merged()` subtracter volumes must match coordinate system exactly (H355).
- `is_perimeter_compatible()` serialization string comparison — locale-sensitive (H356).
- `make_perimeters()` fill_no_overlap_expolygons assignment gap (H357).
- `SupportLayer::AreaGroup` raw `ExPolygon*` dangling pointer risk (H360).
- `lslices_ex` race during parallel pipeline (H361).
- `lower_layer` / `upper_layer` raw back-pointers — use-after-free risk (H362).

#### `LayerRegion.cpp` (H363–H391) — fully annotated this session
**Key discoveries:**

**Bridge pipeline (active `#if 1` path):**
- `fill_surfaces_extract_expolygons()`: thickness last-wins for multi-thickness surfaces (H366).
- `get_grouped_bridges()`: union-find path compression single-level only (H367); O(n²) intersection (H375); `bridge_expansion_begin` stays at `end()` for unsupported bridges (H374).
- `detect_bridge_directions()`: zone linear scan O(zones × anchors) (H376); empty anchor → angle = PI (H377).
- `merge_bridges()`: root-only angle for merged group (H378); `nullopt` dereference UB in Release (H379).
- `expand_expolygons()`: boundary_id offset race if zone expolygons mutated (H380).
- `expand_bridges_detect_orientations()`: early-return leaves zones unclipped (H381); double-move risk (H382).
- `expand_merge_surfaces()`: `bridge_angle=-1` sentinel (H383); closing_radius wall erasure (H384).
- `process_external_surfaces()` (active): expansion_zones ordering invariant (H368); closing_radius magic constants (H369); `pop_back()` ordering dependency (H370).

**Fill surface classification:**
- `prepare_fill_surfaces()`: `PrintObject::infill_only_where_needed` static constant (BBS, H371); idempotency contract (H372).
- `elephant_foot_compensation_step()`: no minimum width guard for opening radius (H373).

**Path simplification:**
- `simplify_entity_collection()`: runtime dynamic_cast dispatch (H386); unknown type throws at runtime (H387).
- `simplify_path/multi_path/loop()`: PrintConfig copy-per-call (H388); D-P in spiral mode removes wall detail (H389).

**SVG debug helpers:**
- Static `idx_map` is not thread-safe (H385).

**Legacy `#else` `process_external_surfaces()`:**
- BBS nozzle_dmr_avg bridge margin coupling (H390).
- Non-idempotent: fill_boundaries destructively consumed (H391).

### Commits This Session

1. `annotate: bridge-angle detection, coverage, unsupported edges (BridgeDetector.hpp/.cpp)` — H331–H349
2. `annotate: layer slices, region ownership, layer adjacency hazards (Layer.hpp)` — H350–H351
3. `annotate: layer lifecycle, perimeter grouping, slice backup/restore (Layer.cpp)` — H352–H362
4. `annotate: bridge grouping, expansion pipeline, path simplification (LayerRegion.cpp)` — H363–H391
5. `docs: add H352-H391 to refactoring hazards (Layer.cpp, LayerRegion.cpp)`

### Hazard Summary (H352–H391)

| Hazard | Brief | Priority |
|--------|-------|----------|
| H352 | `~Layer()` manual raw pointer delete | P1 |
| H353 | `chain_points()` non-deterministic slice ordering | P2 |
| H354 | backup/restore dual-state slice model | P2 |
| H355 | `merged()` coordinate system mismatch risk | P1 |
| H356 | `is_perimeter_compatible()` locale-sensitive string comparison | P2 |
| H357 | fill_no_overlap_expolygons assignment gap in make_perimeters | P2 |
| H358 | simplify() PrintConfig copy per entity | P3 |
| H359 | void_area() timing before fill_regions populated | P2 |
| H360 | SupportLayer AreaGroup raw ExPolygon* | P1 |
| H361 | lslices_ex race in parallel pipeline | P1 |
| H362 | lower_layer/upper_layer use-after-free risk | P1 |
| H363 | bridging_flow extruder underflow sentinel | P2 |
| H364 | spiral_mode check doesn't count raft layers | P2 |
| H365 | get_region() bounds violation at region count mismatch | P1 |
| H366 | fill_surfaces_extract_expolygons thickness last-wins | P2 |
| H367 | group_id() single-level path compression | P3 |
| H368 | expansion_zones ordering invariant — reorder = wrong geometry | P1 |
| H369 | closing_radius magic constants | P2 |
| H370 | expansion_zones.pop_back() ordering dependency | P1 |
| H371 | PrintObject::infill_only_where_needed static constant | P2 |
| H372 | prepare_fill_surfaces idempotency contract | P2 |
| H373 | elephant_foot opening: no minimum width guard | P2 |
| H374 | bridge_expansion_begin stays end() for unsupported bridges | P2 |
| H375 | get_grouped_bridges O(n²) intersection | P3 |
| H376 | detect_bridge_directions zone linear scan | P3 |
| H377 | empty anchor → bridge angle = PI | P2 |
| H378 | merge_bridges root-only angle for group | P2 |
| H379 | merge_bridges nullopt dereference UB in Release | P1 |
| H380 | expand_expolygons boundary_id offset fragile | P3 |
| H381 | expand_bridges early-return leaves zones unclipped | P3 |
| H382 | double-move risk on bridge_expolygons | P3 |
| H383 | bridge_angle=-1 sentinel collides with valid angle | P3 |
| H384 | closing_radius can erase narrow fill regions | P2 |
| H385 | SVG debug static map not thread-safe | P3 |
| H386 | dynamic_cast dispatch on entity collection | P3 |
| H387 | unknown entity type throws at runtime | P2 |
| H388 | PrintConfig copy-per-call in simplify_* | P3 |
| H389 | Douglas-Peucker in spiral mode removes wall detail | P3 |
| H390 | legacy BBS nozzle_dmr_avg bridge margin | P2 |
| H391 | legacy fill_boundaries non-idempotent destruction | P1 |

### Next Annotation Targets (Session 27+)

**High priority (core data structures, used everywhere):**
1. `src/libslic3r/TriangleMesh.cpp/.hpp` — mesh storage, repair, Boolean ops, AABBTree
2. `src/libslic3r/ClipperUtils.cpp` — all Clipper2 wrappers, safety offsets, winding conventions
3. `src/libslic3r/Model.cpp/.hpp` — model/object/volume/instance data hierarchy

**Medium priority (geometry primitives):**
4. `src/libslic3r/Geometry.cpp` + `Geometry/` directory
5. `src/libslic3r/EdgeGrid.cpp` — grid-accelerated collision/seam/support queries
6. `src/libslic3r/Flow.cpp` — flow calculation foundation

**Lower priority (travel and multi-material):**
7. `src/libslic3r/ShortestPath.cpp` — TSP travel optimizer
8. `src/libslic3r/MultiMaterialSegmentation.cpp` — MMU region boundary detection

---

## Session 27 — TriangleMesh.hpp + TriangleMesh.cpp (mesh storage, repair, primitives)

**Files annotated:**
- `src/libslic3r/TriangleMesh.hpp` — 664 lines → annotated (H392–H411)
- `src/libslic3r/TriangleMesh.cpp` — 1156 lines → annotated (H412–H421)

**Commits this session:**
- `f80dc95e21` — annotate TriangleMesh.hpp (H392–H411)
- `977a27f703` — annotate TriangleMesh.cpp (H412–H421)

### Key Discoveries

**`TriangleMeshStats` binary serialization hazard (H394) — CRITICAL**
The stats struct is serialised via `cereal loadBinary`/`saveBinary`. Any field addition/removal/reorder silently corrupts all saved project files. There is no version field in the struct. This must be addressed before any refactor that touches the stats layout.

**`volume = -1.f` sentinel collision (H393) — HIGH**
Volume is initialised to -1 as a "not computed" marker. Genuinely inside-out meshes with small negative volume get clamped to -1, causing recomputation loops. A port should use `std::optional<float>` to distinguish "not computed" from "computed negative value."

**`its` is `public` — stale cache root cause (H396) — HIGH**
All stale-cache hazards in `TriangleMesh` trace back to the public `its` field (`indexed_triangle_set`). Any code path can mutate geometry without the owner knowing. The mesh's volume, bounding box, and AABB tree all become incorrect silently. Making `its` private and requiring mutation through invalidating accessors would resolve this entire class of hazards.

**`stl_fill_holes` disabled — open meshes pass through (H414) — HIGH**
The 3D hole-filling pass is `#if 0` disabled. Open meshes are intentionally passed to the slicer which closes them in 2D at the layer level. This means `TriangleMeshStats::holes_fixed` is always 0, and any quality check using this counter is non-functional.

**`its_make_snap()` infinite loop risk (H409) — HIGH**
The groove convergence loop `while (!is_approx(groove_r, actual_r))` has no iteration limit. NaN propagation from degenerate geometry would produce infinite iteration.

**Thread-safety of `volume()` (H397) — HIGH**
`volume()` lazy-initialises through a `mutable` struct member without a mutex. Two threads simultaneously reading volume on a fresh mesh will both see -1 and both attempt recomputation — a data race on the mutable field.

**qhull non-manifold convex hull (H401) — MEDIUM**
qhull "quite often" returns non-manifold output. The manifold assertion was commented out in the source. A port using a different convex hull library must add its own manifold verification.

**Negative scale doesn't flip winding (H415) — MEDIUM**
`TriangleMesh::scale(negative)` mirrors vertices but leaves triangle winding unchanged. The mesh displays with inverted normals and the slicer treats all faces as back-facing.

### Complete Hazard Summary (H392–H421)

| Hazard | Brief | Priority |
|--------|-------|----------|
| H392 | holes_fixed always 0 due to stl_fill_holes disabled | P2 |
| H393 | volume=-1 sentinel collides with negative-volume meshes | P1 |
| H394 | TriangleMeshStats raw binary serialisation — no version field | P1 |
| H395 | merged mesh volume double-counts overlapping sub-volumes | P2 |
| H396 | public `its` allows geometry mutation without cache invalidation | P1 |
| H397 | volume() lazy init without mutex — data race | P1 |
| H398 | mirror() negates -1 sentinel to +1 | P2 |
| H399 | transform() shear path uses approximate bounding box volume | P2 |
| H400 | horizontal_projection() O(F × log F) Clipper calls | P3 |
| H401 | qhull non-manifold output on convex_hull_3d() | P2 |
| H402 | VertexFaceIndex stale after mesh mutation | P1 |
| H403 | its_face_edge_ids face_mask: open-edge sentinel = self | P2 |
| H404 | its_face_edge_ids same-orientation fallback — non-manifold topology | P2 |
| H405 | its_face_edge_ids parallel sort non-deterministic order | P3 |
| H406 | its_merge_vertices() can create T-junction non-manifold | P2 |
| H407 | its_triangle_vertex_the_same() index-only comparison | P3 |
| H408 | its_volume() inaccurate on open meshes | P1 |
| H409 | its_make_snap() infinite loop on NaN groove_r | P1 |
| H410 | its_make_snap() groove_plane modified as side-effect | P2 |
| H411 | STL big-endian byte-swap POSIX-only macro | P3 |
| H412 | fill_initial_stats() double face-neighbor computation | P3 |
| H413 | repair tolerance scale-dependent: mm vs inch diverge | P2 |
| H414 | stl_fill_holes #if 0 — open meshes pass to slicer | P1 |
| H415 | scale(negative) mirrors vertices but not winding | P2 |
| H416 | transformed_bounding_box() double→float precision loss | P3 |
| H417 | slice() hardcoded 0.0004f tolerance — not adaptive | P2 |
| H418 | its_face_edge_ids face-neighbor: incorrect orientation FIXME | P2 |
| H419 | its_compactify_vertices() no bounds check on corrupted mesh | P2 |
| H420 | its_make_sphere() UV sphere polar elongation | P3 |
| H421 | its_make_snap() add_sub_mesh: no int32 overflow check | P3 |

### Next Annotation Targets (Session 28+)

**Immediately next:**
1. `src/libslic3r/ClipperUtils.cpp` — all Clipper2 wrappers, used by virtually every module
2. `src/libslic3r/Model.cpp/.hpp` — model/object/volume/instance hierarchy

**Medium priority:**
3. `src/libslic3r/Geometry.cpp` + `Geometry/` directory
4. `src/libslic3r/EdgeGrid.cpp`
5. `src/libslic3r/Flow.cpp`

**Next hazard number to assign: H422**

---

## Session 28 — ClipperUtils.hpp

### Files Processed
- `src/libslic3r/ClipperUtils.hpp` — fully annotated (H422–H439)

### Key Discoveries

**PathsProvider iterator family** — The file defines 8 adapter types (EmptyPathsProvider, SinglePathProvider, PolygonsProvider, PolylinesProvider, MultiPointsProvider, ExPolygonProvider, ExPolygonsProvider, SurfacesProvider, SurfacesPtrProvider) that allow the same templated Clipper wrappers to accept any geometry container. This zero-cost abstraction pattern is important to replicate in any port — the alternative (overloaded functions) would require exponential combinations.

**`_foreach_node<ON>` silent bug (H435)** — The `e_ordering::ON` specialization of the `foreach_node` template was intended to iterate nodes in spatial order, but the implementation calls `order_nodes()` and stores the result, then iterates the *original* unordered `nodes` instead of the ordered result. This means `traverse_pt_noholes()` (the only caller using `ON`) never produces ordered output. The bug is present in all sessions and has been in the codebase since Arachne integration.

**ClipperSafetyOffset placement (H423)** — The 10nm safety offset is applied only to the clip polygon in difference/intersection operations, not the subject. This is by design (to avoid modifying the subject geometry) but can leave 10nm slivers when subject and clip share a boundary segment.

### Hazards Assigned
H422–H439 (see 04_refactoring_hazards.md)

### Commits
- `2c3ef4b66f` — annotate ClipperUtils.hpp (H422–H439)

---

## Session 29 — ClipperUtils.cpp

### Files Processed
- `src/libslic3r/ClipperUtils.cpp` — fully annotated (H440–H457), 1420 original lines

### Key Discoveries

**shrink_paths bounding-box sentinel trick (H446, H447)** — For negative offsets that may split contours, the code adds a large outer rectangle as an additional subject, then runs a pftNegative union, then removes the outermost polygon. This is a clever workaround for Clipper's lack of a native "shrink and extract holes" operation. The sentinel margin is only 10nm — fragile if Clipper ever outputs paths outside GetBounds().

**clipper_do_polytree double-pass workaround (H450)** — All ExPolygon-output boolean operations run Clipper twice: once to Paths (fast, handles overlapping edges), once to PolyTree (for hierarchy). This is the fractal pyramid fix for GitHub issue #117.

**_clipper_pl_recombine O(N²) (H451–H453)** — After clipping polygons as open paths, fragments are reconnected by nested endpoint-equality loops. For large polyline sets this is quadratic. Each `erase()` is additionally O(N). A spatial hash on endpoints would reduce this to O(N log N).

**variable_offset_inner/outer delta size mismatch (H456)** — The `deltas` parameter must have exactly `expoly.holes.size() + 1` entries. This is only asserted in Debug. In Release, mismatched deltas cause silent out-of-bounds access.

**variable_offset_outer copy-paste comment error (H457)** — Says "non positive" but asserts >= 0. The code is correct; the comment is wrong. A porter reading only the comment would invert the check.

### Hazards Assigned
H440–H457 (see 04_refactoring_hazards.md)

### Commits
- `dc15558b0a` — annotate ClipperUtils.cpp (H440–H457)

### Next Annotation Targets (Session 30+)

**Immediately next:**
1. `src/libslic3r/Model.hpp` — core object hierarchy (ModelObject, ModelVolume, ModelInstance, Model)
2. `src/libslic3r/Model.cpp` — implementation of above

**Medium priority:**
3. `src/libslic3r/Geometry.cpp` + `Geometry/` directory
4. `src/libslic3r/EdgeGrid.cpp`
5. `src/libslic3r/Flow.cpp`

**Next hazard number to assign: H458**

---

## Session 30 — Model.hpp

### Files Processed
- `src/libslic3r/Model.hpp` — fully annotated (H458–H478)

### Key Discoveries

**Five bounding-box caches (H464)** — `ModelObject` maintains five separate bbox caches (`m_bounding_box_approx`, `m_bounding_box_exact`, `m_raw_bounding_box`, `m_raw_mesh_bounding_box`, `m_min_max_z`), each with its own boolean validity flag. All five must be invalidated together via `invalidate_bounding_box()`. Missing calls silently return stale data. `translate()` (in Model.cpp) was later found to update only two of the five — a concrete instance of this hazard.

**Static global mutable state without locks (H459)** — `Model::extruderParamsMap` and `Model::printSpeedMap` are static class members shared across all Model instances. They are written by the UI thread and read by background slicing threads with no mutex. This is an immediate data-race hazard for any threaded port.

**origin_translation accumulation (H465)** — `center_around_origin()` accumulates `origin_translation` additively with no reset. Combined with `translate()` partially updating caches (H504 in Model.cpp), this creates a multi-hazard cluster affecting G-code coordinate correctness.

**Undo/Redo filesystem I/O (H467, H472)** — BBS-specific additions cause `save_object_mesh()` (blocking filesystem write) to be called on every Undo/Redo operation that touches volume IDs or mesh data. This is absent from upstream PrusaSlicer and is a performance and reliability concern.

**Non-serialized fields (H478)** — Large portions of `Model` state (`plates_custom_gcodes`, `design_info`, `backup_path`, `calib_pa_pattern`, etc.) are excluded from cereal archives. After undo/redo, these retain pre-undo values silently.

### Hazards Assigned
H458–H478 (see 04_refactoring_hazards.md)

### Commits
- `809174de66` — annotate Model.hpp (H458–H478)

---

## Sessions 31–32 — Model.cpp (Part 1 and Part 2)

### Files Processed
- `src/libslic3r/Model.cpp` — fully annotated (H479–H527), ~4200 lines

### Key Discoveries

**calib_pa_pattern double-copy bug (H481)** — `assign_copy(const Model&)` copies `calib_pa_pattern` twice in two sequential identical `if` blocks. The first `make_unique` allocation is discarded immediately. Dead allocation — result is correct but wastes one construction/destruction cycle.

**Backup-system TOCTOU races (H487, H491, H493, H494)** — The BBS backup subsystem (`object_backup_id_map`, `get_backup_path()`, `get_object_backup_id()`) has multiple non-atomic read-modify-write sequences that race under concurrent access. `localtime()` is also not thread-safe (H493).

**`get_object_backup_id` const-overload UB (H492)** — The const overload dereferences `find(...)` unconditionally. If the object has no backup ID entry, this dereferences `end()` — undefined behaviour with no assertion in Release builds.

**translate() partial cache update (H504)** — `ModelObject::translate()` fast-path updates only `m_bounding_box_approx` and `m_bounding_box_exact`; `raw_bounding_box`, `m_min_max_z`, and `convex_hull_2d` caches are left stale. This is the concrete manifestation of the H464 cluster identified in Model.hpp.

**center_around_origin() accumulation hazard (H502)** — Confirmed in implementation: `origin_translation += shift` with no reset guard. Calling `rotate()` (which calls `center_around_origin()`) multiple times in a loop compounds the drift.

**const_cast shared_ptr mutation (H508, H515)** — `scale_mesh_after_creation()`, `scale_geometry_after_creation()`, and `center_geometry_after_creation()` all use `const_cast<TriangleMesh*>(m_mesh.get())->...` to mutate mesh data through a shared_ptr. The invariant "mesh not shared" is enforced only by convention — no runtime check.

**FacetsAnnotation not re-indexed after mesh mutation (H510, H518)** — Both `bake_xy_rotation_into_meshes()` and `transform_this_mesh()` mutate mesh vertex/index data without re-indexing `FacetsAnnotation` triangle IDs. Painted face regions become silently invalid after these operations.

**get_extruders() thread-unsafe mutable cache (H514)** — A `const` method that mutates `mmuseg_extruders` / `mmuseg_ts` (both `mutable`). Two threads calling this simultaneously race on the cache update.

**CEREAL_REGISTER_TYPE disabled (H527)** — The entire `CEREAL_REGISTER_TYPE` block for the Model hierarchy is inside `#if 0`. Polymorphic cereal serialization of the Model hierarchy is non-functional. Any port that uses cereal polymorphism must re-enable this block.

**setExtruderParams() double-insert (H519)** — For `i==0`, inserts at key `0` AND key `1`. For `i==1`, key `1` is overwritten by extruder-1 data. Key `0` thus always holds extruder-0 parameters regardless of extruder count. Undocumented `[UNCLEAR]` intent.

**get_auto_brim_width() dead code (H523)** — Function body starts with `return 0.;` on line 1, making all subsequent thermal/adhesion logic permanently unreachable.

**3MF FacetsAnnotation bitstream — no version tag (H524)** — The hex encoding format has no version field. Any change to the encoding silently corrupts all previously exported `.3mf` files with painted faces.

### Hazards Assigned
H479–H527 (see 04_refactoring_hazards.md)

### Commits
- `4fcb546a5b` — annotate Model.cpp complete (Model.cpp)

---

## Session 33 — Documentation Catch-Up

### Files Processed
- `generated_documentation/04_refactoring_hazards.md` — appended H458–H527 entries (Sessions 30–32)
- `generated_documentation/agent_journal.md` — added sessions 30–33 entries

### Key Discoveries
- Documentation was lagging by ~70 hazard entries (H458–H527 annotated in code but not recorded in the hazard table)
- The journal was missing three full sessions of findings
- Both files now brought fully current before proceeding to Geometry.cpp

### Next Annotation Targets (Session 34+)

**Immediately next:**
1. `src/libslic3r/Geometry.cpp` — HIGH priority, H528 onward
2. `src/libslic3r/EdgeGrid.cpp` — MEDIUM priority
3. `src/libslic3r/Flow.cpp` — MEDIUM priority
4. `src/libslic3r/ShortestPath.cpp` — LOWER priority
5. `src/libslic3r/MultiMaterialSegmentation.cpp` — LOWER priority

**Next hazard number to assign: H528**

---

## Session 34 — Geometry.cpp Annotation + Documentation Catch-Up

### Files Processed
- `src/libslic3r/Geometry.cpp` — full annotation pass; H528–H538 assigned
- `generated_documentation/04_refactoring_hazards.md` — appended H528–H538 entries
- `generated_documentation/agent_journal.md` — added session 34 entry

### Key Discoveries
- **H528**: `arrange()` has a dead `#if 0` block with a cleaner first implementation; the active `#else` branch uses a `goto ENDSORT` label inside nested loops — unusual pre-C++11 binary-insert pattern
- **H529**: `extract_euler_angles()` uses `Eigen::eulerAngles(2,1,0)` with documented gimbal-lock near Y≈±90°; not round-trip stable; port must use quaternions instead
- **H530**: `transform3d_from_string()` calls `::atof()` which is locale-dependent; European locales silently truncate decimal values — all string-loaded transforms corrupted on non-C locales
- **H531**: `volume_to_bed_transformation()` case 2 divide-by-zero when source and target bounding boxes are identical or degenerate (flat object); produces NaN scale silently
- **H532**: `mat_around_a_point_rotate()` calls `.inverse()` without an invertibility check; zero-scale transforms produce NaN propagation
- **H533**: `set_mirror()` silently normalises non-{-1,0,1} values to ±1 without warning
- **H534**: `reset_rotation()` / `reset_scaling_factor()` construct a full Jacobi SVD per call — called from UI drag handlers, potential per-tick overhead
- **H535**: `project_point_to_segment()` asserts `t ∈ [-1e-6, 1+1e-6]` but does NOT clamp; Release builds extrapolate beyond segment endpoints
- **H536**: `set_scaling_factor()` zero-scale assertion is Debug-only; Release builds silently accept singular transforms
- **H537**: `TransformationSVD` mirror pre-multiply by `diag(-1,1,1)` encodes the mirror convention; must be replicated exactly in any port
- **H538**: `generate_transform()` calls `normalized()` on direction vectors without zero-length guard; zero-vector input produces a zero-row transform matrix silently

### Hazards Assigned
H528–H538 (see 04_refactoring_hazards.md)

### Commits
- `aee28b4bf4` — annotate Geometry.cpp transform/rotation utilities

### Next Annotation Targets (Session 35+)

**Immediately next:**
1. `src/libslic3r/EdgeGrid.cpp` — MEDIUM priority, H539 onward
2. `src/libslic3r/Flow.cpp` — MEDIUM priority
3. `src/libslic3r/ShortestPath.cpp` — LOWER priority
4. `src/libslic3r/MultiMaterialSegmentation.cpp` — LOWER priority

**Next hazard number to assign: H539**

---

## Session 35 — EdgeGrid.cpp Annotation

### Files Processed
- `src/libslic3r/EdgeGrid.cpp` — full annotation pass; H539–H547 assigned
- `generated_documentation/04_refactoring_hazards.md` — appended H539–H547 entries
- `generated_documentation/agent_journal.md` — added session 35 entry

### Key Discoveries
- **H539**: `Contour` stores raw `const Point*` pointers — lifetime not managed by Grid; caller must keep source polygons alive (critical for ports with different ownership models)
- **H540**: Bresenham accumulator uses `int64_t` products of `coord_t` values; no guard for extreme coordinate ranges
- **H541**: `calculate_sdf()` signum flood-fill produces all-positive SDF for open polyline inputs; only closed contours provide seeds
- **H542**: `signed_distance_bilinear()` extrapolates unboundedly outside grid bbox — no clamping or special return value
- **H543**: `det == 0` assert for collinear adjacent segments is Debug-only; Release silently returns wrong sign for degenerate vertices
- **H544**: `contours_simplified()` uses `goto end_of_poly` — same pattern as H528 (Geometry.cpp arrange())
- **H545**: `intersecting_edges()` has a dead ternary branch (`jfirst` flag computed but both arms emit same pair); `sort_remove_duplicates()` therefore redundant
- **H546**: `Grid::inside()` is permanently in `#if 0` with `//FIXME finish this!` and zero-init variables — intentionally unfinished; do not port
- **H547**: Distance field stored as `float`; precision loss above ~16 mm at nanometre `coord_t` resolution — relevant for large print beds

### Architecture Notes
- The Grid is a pure spatial index with no ownership of contour data — this is a clean separation but creates an implicit lifetime contract
- Two-pass Bresenham rasterisation is algorithmically sound but the cell size heuristic in the free-function `intersecting_edges()` (average edge length) is fragile for mixed-scale inputs
- `signed_distance()` is a two-tier fallback: edge search → bilinear SDF fallback. The transition boundary depends on `search_radius` and may produce non-smooth distance values at the boundary

### Hazards Assigned
H539–H547 (see 04_refactoring_hazards.md)

### Commits
- `bd042a632e` — annotate EdgeGrid.cpp spatial grid and SDF implementation

### Next Annotation Targets (Session 36+)

**Immediately next:**
1. `src/libslic3r/Flow.cpp` — MEDIUM priority, H548 onward
2. `src/libslic3r/ShortestPath.cpp` — LOWER priority
3. `src/libslic3r/MultiMaterialSegmentation.cpp` — LOWER priority

**Next hazard number to assign: H548**

---

## Session 36 — Flow.hpp/.cpp and ShortestPath.hpp/.cpp Annotation

### Files Processed
- `src/libslic3r/Flow.hpp` — full annotation pass
- `src/libslic3r/Flow.cpp` — full annotation pass; H548–H552 assigned
- `src/libslic3r/ShortestPath.hpp` — full annotation pass
- `src/libslic3r/ShortestPath.cpp` — full annotation pass; H553–H560 assigned
- `generated_documentation/04_refactoring_hazards.md` — appended H548–H560 entries
- `generated_documentation/agent_journal.md` — added session 36 entry

### Key Discoveries

**Flow.hpp / Flow.cpp**
- `Flow` is a pure value type: `float m_width`, `m_height`, `m_spacing`, `m_nozzle_diameter`, `bool m_bridge`. Immutable after construction. All factory methods throw `FlowErrorNegativeSpacing` on invalid geometry.
- `rounded_rectangle_extrusion_spacing()` uses a cosine-arc formula to compute the lateral advance per bead accounting for the semicircular ends; throws if result ≤ 0.
- **H548**: `support_material_flow()` uses `support_filament - 1` as array index without a `support_filament == 0` guard — wraps to `SIZE_MAX` → OOB read (UB).
- **H549**: `Flow::operator==` ignores `m_spacing` — equality check is weaker than structural equivalence.
- **H550**: `Flow::with_cross_section()` increasing-flow branch uses old area to compute new width — arithmetic error for any flow increase.
- **H551**: `BRIDGE_EXTRA_SPACING = 0.05` is raw mm, not scaled — implicit unit boundary.
- **H552**: Dead `#if 0` block for first-layer width fallback in `new_from_config_width()`.

**ShortestPath.hpp / ShortestPath.cpp**
- Three TSP tiers: (V0) naive nearest-neighbour O(n²), (V1) multi-fragment greedy + union-find, (V2) V1 + chain flipping.
- 2-opt post-improvement: `improve_ordering_by_two_exchanges_with_segment_flipping`, max 100 iterations (H558).
- **H553**: `chain_and_reorder_extrusion_entities` unconditional `static_cast<ExtrusionEntityCollection*>` — UB for non-collection items.
- **H554**: `static const double point_distance_epsilon2` inside function body — ODR risk.
- **H555**: `reorder_extrusion_paths` declaration/definition parameter mismatch (value vs non-const reference).
- **H556**: V2 iteration guard — `num_segments * 16` cap causes silent empty output if reached.
- **H557**: `chain_expolygons` centroid-based ordering — non-deterministic for equal centroids.
- **H558**: Hard-coded 100-iteration cap with no convergence warning.
- **H559**: Five dead `#if 0` blocks preserved as algorithm history (1-opt, 3-opt v1, 4-opt Eigen variants) — do NOT delete.
- **H560**: `do_crossover` `default:` asserts `(i >> 6) == 2` — Debug-only; Release silently produces wrong permutation if ever triggered.

### Architecture Notes
- `Flow` is safe to treat as a value type in any target language; factories map cleanly to static constructors.
- The `flow_spacing == 0` case (bridge) bypasses rounded-rectangle geometry and must be special-cased in ports.
- `ShortestPath` TSP algorithms rely on mutable arrays of endpoint pairs with in-place direction flipping. This pattern maps poorly to functional/immutable languages; careful translation required.
- The union-find in V1/V2 is a standard path-compressed variant. Standard library implementations (e.g. `DisjointSet` in Go or Python `networkx`) can replace it directly.
- All dead `#if 0` TSP variants should be preserved as reference documentation — they represent a multi-year search for a better algorithm that was never completed.

### Hazards Assigned
H548–H560 (see 04_refactoring_hazards.md)

### Next Annotation Targets (Session 37+)

**Immediately next:**
1. `src/libslic3r/Fill/Fill3DHoneycomb.cpp` — remaining fill pattern
2. `src/libslic3r/Fill/FillHoneycomb.cpp`
3. `src/libslic3r/Fill/FillPlanePath.cpp`
4. `src/libslic3r/Fill/FillLine.cpp`
5. `src/libslic3r/Fill/FillCrossHatch.cpp`
6. `src/libslic3r/MultiMaterialSegmentation.cpp`
7. `src/libslic3r/Geometry/` directory

**Next hazard number to assign: H561**

---

## Session 37 — MultiMaterialSegmentation.cpp Annotation

### Files Processed
- `src/libslic3r/MultiMaterialSegmentation.cpp` (2229 lines) — full read + annotation pass; H561–H570 assigned
- `generated_documentation/04_refactoring_hazards.md` — appended H561–H570 entries
- `generated_documentation/agent_journal.md` — added session 37 entry

### Key Discoveries

**MultiMaterialSegmentation.cpp — Architecture**
- Two public entry points: `multi_material_segmentation_by_painting()` (N extruders, top/bottom enabled) and `fuzzy_skin_segmentation_by_painting()` (2 states, no top/bottom).
- Both delegate to `segmentation_by_painting()` — the shared 7-phase pipeline:
  1. Parallel slice preprocessing (union, remove small holes, simplify).
  2. Serial EdgeGrid construction per layer.
  3. Nested parallel triangle projection → `PaintedLine` records per layer (64-mutex scheme).
  4. Parallel per-layer: post-process → colorize contours → build Voronoi graph → extract color segments.
  5. Optional `cut_segmented_layers()` for max-width / interlocking depth constraints.
  6. Optional `segmentation_top_and_bottom_layers()` for top/bottom shell propagation.
  7. `merge_segmented_layers()` — combine sides + top/bottom into final per-extruder ExPolygon output.

**MMU_Graph**
- Directed arc graph: `BORDER` arcs from input polygon edges (one directed arc per edge); `NON_BORDER` arcs from Voronoi diagram (two directed arcs per VD edge).
- `all_border_points` divides node index space: 0..all_border_points-1 = contour nodes; all_border_points.. = Voronoi interior nodes.
- `vertex.color()` field is repurposed across three distinct phases — this is the most severe porting hazard in the file (H561).

**Key Hazards**
- **H561** (High, P1): `vertex.color()` phase-overloading — VD annotation enum values (1, 2) before construction, node indices after. Any reader without phase awareness misinterprets the value.
- **H562** (High, P1): `extract_colored_segments()` repair path uses `-1` cast to `size_t` (→ SIZE_MAX) as a sentinel arc index. Safe today; fragile under refactoring.
- **H563** (Low, P3): `PaintedLineVisitor` AND-logic distance pre-filter is over-conservative; real filter is the collinearity check.
- **H564** (High, P1): `segmentation_top_and_bottom_layers()` interleave trick (`layer_idx_offset = (group_idx & 1) * num_layers`) is fragile if TBB changes blocking granularity.
- **H565** (High, P1): `layer_color_stat()` lambda hardcodes `nozzle_diameter.get_at(0)` for all colors — wrong for multi-extruder setups with different nozzle diameters.
- **H566** (Low, P3): `append_edge()` O(degree) deduplication — no upper bound asserted.
- **H567** (Medium, P2): `build_graph()` pointer-arithmetic indexing of `force_edge_adding[]` — empty polygon entries corrupt graph indices.
- **H568** (High, P1): `merge_segmented_layers()` inverted index layouts: `top_and_bottom_layers[extruder][layer]` vs `segmented_regions_merged[layer][extruder-1]`.
- **H569** (Medium, P2): `static int iRun` in debug block — unprotected global; unsafe if segmentation ever parallelized across print objects.
- **H570** (Low, P3): `fuzzy_skin_segmentation_by_painting()` uses uniform `layer_height` for all regions — imprecise for variable-layer-height prints.

### Architecture Notes
- The Voronoi + graph traversal approach is a complex but robust alternative to naive polygon clipping for segmenting multi-color contours. Any port must replicate the full `MMU_Graph` construction pipeline (7 phases) exactly.
- The 64-mutex hash for `painted_lines` is an efficient low-contention pattern. In Go/Rust, a `sync.RWMutex` array or per-layer `Mutex<Vec<PaintedLine>>` would be the direct equivalent.
- `extract_colored_segments()` leftmost-arc walk is equivalent to a planar graph face enumeration. In a port, this can be expressed as a standard planar graph traversal with `used_edges` tracking.
- `merge_segmented_layers()` applies `offset2_ex()` (morphological open/close) to remove dimples — this requires a Clipper equivalent in any target language.

### Hazards Assigned
H561–H570 (see 04_refactoring_hazards.md)

### Next Annotation Targets (Session 38+)

**Immediately next:**
1. `src/libslic3r/Geometry/ArcWelder.cpp/.hpp`
2. `src/libslic3r/Geometry/Circle.cpp/.hpp`
3. `src/libslic3r/Geometry/ConvexHull.cpp/.hpp`
4. `src/libslic3r/Geometry/MedialAxis.cpp/.hpp`
5. `src/libslic3r/Geometry/Voronoi.cpp/.hpp`
6. `src/libslic3r/Geometry/VoronoiOffset.cpp/.hpp`
7. `src/libslic3r/Geometry/VoronoiUtils.cpp/.hpp`
8. `src/libslic3r/Geometry/VoronoiUtilsCgal.cpp/.hpp`
9. `src/libslic3r/Geometry/VoronoiVisualUtils.hpp`
10. `src/libslic3r/Geometry/Bicubic.hpp`
11. `src/libslic3r/Geometry/Curves.hpp`

**Next hazard number to assign: H571**

---

## Session 38 — Geometry/ Sub-Module Annotation (ArcWelder, Circle, ConvexHull, MedialAxis, Voronoi*, Bicubic, Curves)

### Files Processed
- `src/libslic3r/Geometry/ArcWelder.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/ArcWelder.cpp` — full read + annotation pass
- `src/libslic3r/Geometry/Circle.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/Circle.cpp` — full read + annotation pass; H571–H576 assigned
- `src/libslic3r/Geometry/ConvexHull.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/ConvexHull.cpp` — full read + annotation pass; H577–H579 assigned
- `src/libslic3r/Geometry/MedialAxis.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/MedialAxis.cpp` — full read + annotation pass; H581–H582 assigned
- `src/libslic3r/Geometry/Voronoi.hpp` — full read + annotation pass; H583–H584 assigned
- `src/libslic3r/Geometry/Voronoi.cpp` — full read + annotation pass
- `src/libslic3r/Geometry/VoronoiOffset.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/VoronoiOffset.cpp` — full read + annotation pass; H588–H591 assigned
- `src/libslic3r/Geometry/VoronoiUtils.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/VoronoiUtils.cpp` — full read + annotation pass; H585–H587, H592–H593 assigned
- `src/libslic3r/Geometry/VoronoiUtilsCgal.hpp` — full read + annotation pass
- `src/libslic3r/Geometry/VoronoiUtilsCgal.cpp` — full read + annotation pass; H594 assigned
- `src/libslic3r/Geometry/VoronoiVisualUtils.hpp` — full read + annotation pass; H580, H595 assigned
- `src/libslic3r/Geometry/Bicubic.hpp` — full read + annotation pass; H596 assigned
- `src/libslic3r/Geometry/Curves.hpp` — full read + annotation pass; H597–H598 assigned
- `generated_documentation/04_refactoring_hazards.md` — appended H571–H598

### Key Discoveries

**ArcWelder.hpp / ArcWelder.cpp — Architecture**
- `ArcWelder` converts linear G-code move sequences into arc (`G2`/`G3`) equivalents using a tolerance-driven fitting loop.
- Core state machine: `PathSegmentProjected` accumulates candidate arc spans; span committed when the next point exceeds the fit tolerance or a direction reversal is detected.
- Key parameters: `resolution` (mm, minimum arc chord), `tolerance` (mm, maximum deviation from ideal arc), `max_angle` (maximum arc sweep per segment).
- The fitter runs a binary search over candidate endpoint indices to find the largest span that stays within tolerance — O(n log n) in practice.
- `[CONCURRENCY]`: the welder is a single-object stateful pass; all callers are responsible for serialising access.

**Circle.hpp / Circle.cpp — Architecture**
- Provides: `circle_ransac()`, `circle_center_taubin_newton()`, `welzl()` (minimax enclosing circle), `ray_circle_intersections()`, `arc_center()`, `circle_center()`.
- All three fitting algorithms coexist for different call sites: RANSAC for noisy point clouds, Taubin-Newton for smooth datasets, Welzl for hard geometric containment.
- Coordinate representation: all internal calculations in `double` mm; results returned as `Circled` (center `Vec2d`, radius `double`).
- H571: `ray_circle_intersections()` refers to non-existent `_r2_lv2_c2` suffix — latent linker error.
- H572: RANSAC seed is deterministic on GCC Linux (random_device returns 0).

**ConvexHull.hpp / ConvexHull.cpp — Architecture**
- Wraps CGAL and Clipper convex hull APIs with OrcaSlicer polygon types.
- `convex_hull(Points)` → Andrew's monotone chain; `convex_hull(Pointf3s)` → projects to XY silently (H578).
- `decompose_convex_polygon_top_bottom()` splits a convex polygon into upper/lower chains for paired-segment algorithms.
- H577: the function name `convex_hulll` contains a triple-l typo that is the only exported symbol.

**MedialAxis.hpp / MedialAxis.cpp — Architecture**
- `MedialAxis` class: wraps a Voronoi diagram to produce the skeleton (medial axis) of an `ExPolygon`.
- Pipeline: (1) build Voronoi from scaled polygon outline + holes; (2) filter edges by distance-to-contour tolerance; (3) `validate_edge()` — merge near-collinear edges; (4) `build()` — walk graph, produce `ThickPolyline` output with variable thickness stored as paired doubles.
- `ThickPolylines` output: each `ThickPolyline` carries per-point left/right half-widths for variable-width extrusion.
- The `repair()` morphological closing step (H581) modifies polygon topology to fill gaps in the skeleton.

**Voronoi.hpp / Voronoi.cpp — Architecture**
- `VoronoiDiagram` wraps `boost::polygon::voronoi_diagram<double>` and adds a `State` enum and `IssueType` enum for validity bookkeeping.
- `is_valid()` returns `true` for `State::UNKNOWN` (H583) — default before any validation run.
- Validation helpers in `VoronoiUtils` / `VoronoiUtilsCgal` update the state; this file only stores it.

**VoronoiOffset.hpp / VoronoiOffset.cpp — Architecture**
- `VoronoiOffset::offset()` computes inward/outward polygon offsets using the Voronoi diagram of the polygon boundary.
- Uses Voronoi cell ranges (`compute_segment_cell_range()`) to assign per-edge offset distances.
- `annotate_inside_outside()` classifies VD edges as inside/outside the polygon using cross-product sign.
- 4-argument convenience overload uses `const_cast` to mutate the VD's color fields (H588).
- H589: open (non-closed) offset loops silently discarded.

**VoronoiUtils.hpp / VoronoiUtils.cpp — Architecture**
- Provides utility functions for Voronoi diagram processing: `to_point()`, `copy_to_local()`, `decode_input_segment_endpoint()`, `discretize_parabola()`, validity checks (`is_voronoi_diagram_planar_angle`, `is_voronoi_diagram_planar_intersection`).
- `copy_to_local()` assumes contiguous vertex storage via pointer subtraction (H585).
- H587: `decode_input_segment_endpoint()` with `color == 0` underflows to `SIZE_MAX` — latent OOB crash.
- `discretize_parabola()`: falls back to straight-line approximation on degenerate parabola but callers do not check (H593).
- Explicit template instantiations required for each new iterator type (H586).

**VoronoiUtilsCgal.hpp / VoronoiUtilsCgal.cpp — Architecture**
- CGAL-backed planarity checker for Voronoi diagrams.
- `is_voronoi_diagram_planar_intersection()`: segment sweep-line using CGAL's `do_curves_intersect` — parabolic edges excluded (H594).
- `is_voronoi_diagram_planar_angle()`: angle-based planarity test; template instantiations in the .cpp.

**VoronoiVisualUtils.hpp — Architecture**
- Contains an embedded `boost::polygon::voronoi_visual_utils<CT>` specialisation for debug rendering.
- `color_exterior()` is recursive — O(n) call depth, stack-overflow risk for large VDs (H595).
- H580: potential ODR violation if the vendored Boost version ships an identical template.

**Bicubic.hpp — Architecture**
- Template header implementing bicubic surface interpolation.
- `BicubicCoefficients::interpolate()`: evaluates a 4×4 control-point bicubic patch.
- `BicubicInternal::clamp()` duplicates `std::clamp` (H596).

**Curves.hpp — Architecture**
- Template header: `fit_curve()` (weighted polynomial curve fitting via Eigen QR) and `fit_polynomial()`.
- Uses Eigen `fullPivHouseholderQr()` — O(n²m); no size guard (H597).
- H598: `assert(weights[index] > 0)` — debug-only; in release, zero weights collapse rows silently and negative weights corrupt the QR solve with NaN output.

### Key Hazards Assigned
| ID | Description | Severity |
|----|-------------|----------|
| H571 | `ray_circle_intersections()` non-existent `_r2_lv2_c2` suffix — latent linker error | High |
| H572 | RANSAC `std::mt19937` deterministic on GCC Linux (random_device = 0) | Medium |
| H573 | `arc_center()` wrong result for antipodal endpoints | Low |
| H574 | `circle_center()` silently returns midpoint for collinear 3-point input | Low |
| H575 | `welzl()` O(n!) worst-case without randomised permutation | Low |
| H576 | `circle_center_taubin_newton()` returns NaN on non-convergence; callers don't check | Low |
| H577 | `convex_hulll` triple-l typo — only exported name | Low |
| H578 | `convex_hull(Pointf3s)` silently ignores Z | Low |
| H579 | `decompose_convex_polygon_top_bottom()` returns empty chains on degenerate input | Low |
| H580 | `VoronoiVisualUtils.hpp` embedded Boost template — potential ODR violation | Medium |
| H581 | `MedialAxis::repair()` morphological closing changes polygon topology | Medium |
| H582 | `validate_edge()` hardcodes PI/8 collinear threshold with no calibration | Low |
| H583 | `VoronoiDiagram::is_valid()` returns true for UNKNOWN state | Medium |
| H584 | `IssueType::UNKNOWN` overloads "not yet checked" and "unknown error" | Low |
| H585 | `copy_to_local()` assumes contiguous VD vertex storage via pointer subtraction | Low |
| H586 | Explicit template instantiation list — new iterator type requires manual addition | Low |
| H587 | `decode_input_segment_endpoint()` color==0 underflows to SIZE_MAX → OOB read | High |
| H588 | `offset()` 4-arg overload uses `const_cast` to mutate const VD | Medium |
| H589 | Open offset loops silently discarded | Low |
| H590 | `annotate_inside_outside()` assert side==0 — release silently misclassifies boundary | Low |
| H591 | `compute_segment_cell_range()` infinite-edge `continue` fragile to restructuring | Low |
| H592 | `to_point()` `llround` overflow for large VD coordinates | Low |
| H593 | `discretize_parabola()` degraded output warning not checked by callers | Low |
| H594 | `is_voronoi_diagram_planar_intersection()` excludes parabolic edges | Low |
| H595 | `color_exterior()` recursive — O(n) stack depth, overflow risk | Medium |
| H596 | `BicubicInternal::clamp()` duplicates `std::clamp` | Low |
| H597 | `fit_curve()` O(n²m) Eigen solve — no size guard | Low |
| H598 | `fit_curve()` assert weights > 0 — debug-only; release NaN corruption | Low |

### Architecture Notes
- The `Geometry/` sub-module is a heterogeneous collection of self-contained geometry algorithms sharing only the coordinate type (`Point`, `coord_t`, `Vec2d`).
- All Voronoi-based algorithms (`MedialAxis`, `VoronoiOffset`, `MultiMaterialSegmentation`) share the same coordinate-scaling hazard at the `to_point()` / `from_point()` boundary — any port must handle the `double` VD ↔ `coord_t` conversion consistently.
- The ArcWelder is a pure post-processing pass; it has no semantic knowledge of the toolpath content, only geometry. A port can treat it as a black-box algorithm operating on `(x, y)` sequences.
- The Bicubic and Curves headers are used for bed-leveling mesh interpolation and pressure-advance calibration curve fitting, respectively — both are isolated from the main slicing pipeline.

### Hazards Assigned
H571–H598 (see 04_refactoring_hazards.md)

### Next Annotation Targets (Session 39+)

**Remaining `src/libslic3r/` top-level files not yet annotated (priority order):**
1. `src/libslic3r/Algorithm/` — sub-directory (point-inside-polygon, connected-components, etc.)
2. `src/libslic3r/AABBMesh.hpp/.cpp`
3. `src/libslic3r/Brim.cpp`
4. `src/libslic3r/ExPolygon.cpp/.hpp` (core polygon type)
5. `src/libslic3r/Polygon.cpp/.hpp` / `Polyline.cpp/.hpp`
6. `src/libslic3r/Line.cpp/.hpp` / `Point.cpp/.hpp`
7. `src/libslic3r/Extruder.cpp/.hpp`
8. `src/libslic3r/PrintConfig.cpp/.hpp`

**Next hazard number to assign: H599**

---

## Session 39 — Core Primitive Types: Point, BoundingBox, Polygon, Polyline, ExPolygon

### Files Annotated

| File | Status |
|------|--------|
| `src/libslic3r/Point.hpp` | ✅ Fully annotated |
| `src/libslic3r/Point.cpp` | ✅ Fully annotated |
| `src/libslic3r/BoundingBox.hpp` | ✅ Fully annotated |
| `src/libslic3r/BoundingBox.cpp` | ✅ Fully annotated |
| `src/libslic3r/Polygon.hpp` | ✅ Fully annotated |
| `src/libslic3r/Polygon.cpp` | ✅ Fully annotated |
| `src/libslic3r/Polyline.hpp` | ✅ Fully annotated |
| `src/libslic3r/Polyline.cpp` | ✅ Fully annotated |
| `src/libslic3r/ExPolygon.hpp` | ✅ Fully annotated |
| `src/libslic3r/ExPolygon.cpp` | ✅ Fully annotated |

### Key Discoveries

**Point / Coordinate System (Point.hpp, Point.cpp)**
- `Point` extends `Vec2crd` (Eigen `Matrix<coord_t, 2, 1, DontAlign>`). All coordinates are scaled integers (× 1e6 from mm).
- `Points` uses `tbb::scalable_allocator` — ABI incompatible with `std::allocator` (H601).
- `ClosestPointInRadiusLookup`: grid-hash spatial index; searches only 4 cells (2×2 neighborhood) — approximate, not exhaustive in radius (H604).
- `int128::orient()` / `int128::cross()` — exact orientation predicates using 128-bit integer arithmetic for robustness.
- `scaled()`/`unscaled()` templates: `unscaled()` multiplies (not divides) because `SCALING_FACTOR` = 1e-6 (the reciprocal). Redefining as 1e6 silently inverts all conversions.
- `Vec2crd ↔ Vec2d` implicit-cast paths exist; converting without `unscaled()`/`scaled()` silently drops the 1e6 factor (H605).
- `rotate(cos_a, sin_a)` snaps via `round()`; repeated rotations accumulate rounding error (H608).

**BoundingBox (BoundingBox.hpp, BoundingBox.cpp)**
- Template `BoundingBoxBase<PointType>` — three concrete types: `BoundingBox` (scaled int), `BoundingBoxf` (float), `BoundingBoxf3` (3D double). The `defined` flag distinguishes "not yet constructed" from "empty".
- `BoundingBox3Base` iterator constructor throws `InvalidArgument` on empty input; all other constructors silently return undefined bbox — asymmetric (H603).
- `BoundingBoxf3::transformed()` transforms all 8 box corners — correct for non-axis-aligned transforms.
- `BoundingBox3Base::polygon(is_scaled)`: `is_scaled=true` means coords ARE already scaled integers and output divides by SCALING_FACTOR — confusing semantics.

**Polygon (Polygon.hpp, Polygon.cpp)**
- Extends `MultiPoint`. Winding convention: `contour` = CCW, `holes` = CW (enforced by callers and `is_valid()`, NOT by class construction).
- `area()` uses shoelace via `cross2`. Returns negative for CW polygons — callers must `abs()` for unsigned area (H600).
- `is_counter_clockwise()` delegates to `ClipperLib::Orientation()` — crosses the `coord_t`↔Clipper boundary pervasively.
- `Polygons/PolygonPtrs/ConstPolygonPtrs` use `PointsAllocator<T>` (TBB) — not std::vector-compatible in ABI (H601).
- `simplify()` requires CCW input — CW contours (holes) are silently reoriented by Clipper.
- `centroid()` divides by `3*area_sum` — if area==0 (degenerate polygon), UB (NaN/inf → `coord_t` cast). No guard.
- `densify()` uses `vector::insert()` in a loop — O(n²) for polygons with many long edges.

**Polyline (Polyline.hpp, Polyline.cpp)**
- `Polyline` carries a `fitting_result` vector (`std::vector<PathFittingData>`) parallel to `points`. Each `PathFittingData` covers a span with `path_type` (Linear/Arc_cw/Arc_ccw) and `arc_data` (ArcSegment). This vector MUST stay synchronized with `points` across ALL mutations (H599).
- `ThickPolyline` extends `Polyline` with `width` vector (size = `(points.size()-1)*2`) and `endpoints` pair. `reverse()` must also reverse `width` and swap endpoint flags — correctly implemented.
- `remove_same_neighbor(Polyline)` uses `std::unique` on points but does NOT update `fitting_result` — H599 violation risk.
- `polylines_merge()` template operates on raw PointsType — loses arc metadata for `Polyline` objects.

**ExPolygon (ExPolygon.hpp, ExPolygon.cpp)**
- Layout: `contour` (Polygon, CCW) + `holes` (Polygons, CW). Winding convention enforced only by `is_valid()`, never on construction (H610).
- `area()` uses double-negation `a -= -hole.area()` — correct (CW holes have negative shoelace area) but confusing (H602).
- `overlaps()` is NOT commutative: vertical-boundary touches are overlapping; horizontal-boundary touches are NOT — Clipper open-boundary asymmetry (H616).
- `contains(Polyline)` uses `diff_pl()` — Clipper-based, subject to open-boundary conventions (H614/H629).
- `to_expolygons(Polygons)` does NOT run `union_ex()` — result may have overlapping contours with no hole nesting (H624).
- Boost.Polygon traits expose contour only via `polygon_traits`; holes only via `polygon_with_holes_traits` (H609).
- `to_linesf()` shared `prev_pd` state in lambda — stale state bug if ring < 2 points (H621).
- `medial_axis()`: 4-phase algorithm (build → extend endpoints → remove short → greedy reconnect). Width invariant assert after reconnection (H632).
- `keep_largest_contour_only()`: null crash if all contours CW (H627).

### Hazards Assigned

H599–H634 (see `04_refactoring_hazards.md`)

| Hazard | Description | Severity |
|--------|-------------|----------|
| H599 | `Polyline::fitting_result` parallel vector must stay in sync with `points` | High |
| H600 | `Polygon::area()` returns negative for CW polygons | Medium |
| H601 | `Points` uses `tbb::scalable_allocator` — ABI incompatible | Medium |
| H602 | `ExPolygon::area()` double-negation — correct but confusing | Medium |
| H603 | `BoundingBox3Base` iterator constructor throws; others silently undefined | Low |
| H604 | `ClosestPointInRadiusLookup` searches only 4 cells — approximate | High |
| H605 | `Vec2crd ↔ Vec2d` implicit casts lose 1e6 scale factor | High |
| H606 | `Point(double,double)` rounds via `std::round()` | Low |
| H607 | `Point::new_scale()` truncates — overflow for coords > ~2147 m | Low |
| H608 | `rotate(cos,sin)` snaps via `round()` — cumulative rounding | Low |
| H609 | Boost.Polygon `polygon_traits<ExPolygon>` ignores holes | High |
| H610 | ExPolygon winding convention not enforced on construction | High |
| H611 | `ExPolygon::scale()` per-vertex rounding accumulation | Low |
| H612 | `ExPolygon::translate(double)` truncates, not rounds | Low |
| H613 | D-P simplification of ExPolygon rings is independent — topology not guaranteed | Medium |
| H614 | `contains(Polyline)` Clipper open-boundary convention | Low |
| H615 | `contains(Point)` inverts `border_result` for holes | Low |
| H616 | `overlaps()` non-commutative (horizontal vs vertical boundary) | Medium |
| H617 | `simplify_p()` may return empty result on degenerate input | Low |
| H618 | `medial_axis()` endpoint extension uses contour only, not holes | Low |
| H619 | `medial_axis()` greedy reconnection — random pairs when >2 meet | Low |
| H620 | `to_lines()` closing edge outside inner loop — easy to miss in ports | Low |
| H621 | `to_linesf()` shared `prev_pd` state — stale on short ring | Medium |
| H622 | `to_polylines(&&)` move-then-read-front ordering is critical | Low |
| H623 | `to_polygon_ptrs()` returns raw pointers — no lifetime guarantee | Medium |
| H624 | `to_expolygons(Polygons)` does not restore hole nesting | High |
| H625 | `get_extents(ExPolygon)` contour-only — undefined for empty contour | Low |
| H626 | `has_duplicate_points()` global check — false positives for shared vertices | Low |
| H627 | `keep_largest_contour_only()` null crash if all contours are CW | High |
| H628 | `remove_small_and_small_holes()` area rounding near threshold | Low |
| H629 | `contains(Polyline)` Clipper horizontal-edge false negative | Low |
| H630 | `overlaps()` fallback: front point in hole of `other` → wrong true | Low |
| H631 | `projection_onto()` signed int loop index vs `size_t` | Low |
| H632 | `medial_axis()` ThickPolyline width invariant in reconnection | Medium |
| H633 | `expolygons_match()` hole-order-sensitive — no sort performed | Low |
| H634 | `remove_same_neighbor()` degenerate holes not erased | Low |

### Next Annotation Targets (Session 40+)

**Remaining `src/libslic3r/` top-level files not yet annotated (priority order):**
1. `src/libslic3r/Line.hpp/.cpp` — Line, Linef, Lines, Linesf
2. `src/libslic3r/Extruder.hpp/.cpp`
3. `src/libslic3r/PrintConfig.hpp/.cpp`
4. `src/libslic3r/Algorithm/` — sub-directory (point-inside-polygon, connected-components, etc.)
5. `src/libslic3r/AABBMesh.hpp/.cpp`
6. `src/libslic3r/Brim.cpp`

**Next hazard number to assign: H635**

---

## Session 40

**Files annotated:** `src/libslic3r/Line.hpp`, `src/libslic3r/Line.cpp`, `src/libslic3r/Extruder.hpp`, `src/libslic3r/Extruder.cpp`, `src/libslic3r/Algorithm/LineSplit.hpp`

**New hazards registered:** H635–H657 (23 hazards)

**Commit:** `c9b2e4db6c` — "Annotate line primitives, extruder state machine, and Algorithm/LineSplit (Session 40)"

### Key Findings

#### Line.hpp / Line.cpp (H635–H645)

**`line_alg` namespace (generic, trait-parameterized):**
- `distance_to_squared()` computes the nearest segment point in `double`, casts it back to `Scalar<L>` (truncation), but returns the squared distance computed from the *pre-cast* double point. Result: returned distance and returned nearest_point are inconsistent. **H635**
- `intersection()` has the collinear case permanently disabled via `#if 0` — always returns `false` for collinear overlapping segments, no diagnostic. **H636**
- Intersection point is cast to `coord_t` via truncation (not `std::round()`). **H637**

**`Line` (2D integer coord_t):**
- `intersection_infinite()`: intermediate `int64_t` products can overflow before the guard is reached for coordinates in the upper half of `coord_t` range. **H641**
- `perp_distance_to()`: correctly guards zero-length line. **H642**
- `overlap()`: uses only X-projection — incorrect for near-vertical lines (**H643**), division by zero for exactly vertical lines (**H644**).
- `extend()`: `normalized()` of zero-vector returns zero, silent no-op for zero-length lines. **H645**

**`Linef3` (3D double):**
- `intersect_plane()`: divides by `v(2)` with no guard — division by zero for horizontal lines. **H639**

**Boost.Polygon integration:**
- `segment_concept` specialization uses `coord_t` (`int64_t`) but Voronoi builder assumes `int32_t` range — silent corruption for large coordinates. **H640**

#### Extruder.hpp / Extruder.cpp (H646–H655)

**Critical: global static state**
- `m_share_E` and `m_share_retracted` are `static` class members — global across all instances in the process. Concurrent `Print` jobs or background slicers will corrupt each other's retraction state. **H646**

**Asymmetric config indexing — must be replicated exactly in ports:**
- `filament_diameter()`, `filament_flow_ratio()` → index via `m_id` (logical filament slot)
- `retract_length_toolchange()`, `retract_restart_extra_toolchange()`, `travel_slope()` → index via `extruder_id()` (physical slot)
- **H649** — any port that normalises to a single index breaks one set of lookups

**Other hazards:**
- `m_config` raw non-owning pointer — dangling if `GCodeConfig` destroyed first. **H650**
- `m_e_per_mm3` cached at construction — stale after config mutation. **H651**
- `retract()` always updates `m_restart_extra` even on no-op retraction. **H652** (CRITICAL — P1)
- `unretract()` share-mode: `extrude()` called before `m_share_retracted` zeroed — order is critical. **H653**
- `set_retracted()` does not update `m_share_retracted` — broken in share mode. **H654**
- `used_filament()` FIXME: doesn't count retracted length in share mode. **H655**
- `reset_E()` always writes shared counter regardless of mode. **H648**

#### Algorithm/LineSplit.hpp (H656–H657)

- `do_split_line()` returns empty `SplittedLine` on no intersection — callers must guard. **H656**
- `split_line<>()` has operator precedence bug in `reserve()` call: `path.size() + closed ? 1 : 0` instead of `path.size() + (closed ? 1 : 0)`. Result always reserves 1, causing reallocation for any path > 1 element. Correctness preserved (reserve is advisory). **H657**

### Files Read but NOT Yet Annotated (Session 40 read, Session 41 to write)

- `src/libslic3r/Algorithm/LineSplit.cpp` — `do_split_line()` implementation; Z-channel encoding `-(edge_start_index + 1)` for intersection points; `SplitLineJunction::src_idx` negative convention
- `src/libslic3r/Algorithm/RegionExpansion.hpp/.cpp` — wave propagation algorithm: `wave_seeds()` → `propagate_waves()` → `wavefront_step()` via ClipperOffset; uses ClipperLib_Z for boundary tracking; `merge_expansions_into_expolygons()` uses `union_safety_offset_ex()` and sample-point containment test

### Session 40 Hazard Summary

| ID | Summary | Severity |
|----|---------|----------|
| H635 | `distance_to_squared()` nearest_point cast discrepancy | Medium |
| H636 | `intersection()` collinear case permanently disabled | Medium |
| H637 | Intersection point truncation on coord_t cast | Low |
| H638 | `intersection_infinite()` near-limit precision loss | Low |
| H639 | `Linef3::intersect_plane()` division-by-zero for horizontal line | High |
| H640 | Boost.Polygon Voronoi int64 vs int32 range | High |
| H641 | `intersection_infinite()` intermediate int64 overflow | Medium |
| H642 | `perp_distance_to()` zero-length guard (correct) | Low |
| H643 | `overlap()` X-projection only — wrong for near-vertical | Medium |
| H644 | `overlap()` division by zero for vertical lines | High |
| H645 | `extend()` silent no-op for zero-length line | Low |
| H646 | `m_share_E`/`m_share_retracted` static global state | Critical |
| H647 | `m_e_per_mm3` formula (informational) | Low |
| H648 | `reset_E()` unconditional shared counter write | Low |
| H649 | Asymmetric config indexing `m_id` vs `extruder_id()` | High |
| H650 | `m_config` raw non-owning pointer | High |
| H651 | `m_e_per_mm3` stale after config mutation | Medium |
| H652 | `retract()` updates `m_restart_extra` on no-op | Low |
| H653 | `unretract()` share-mode order-dependent | Medium |
| H654 | `set_retracted()` broken in share mode | Medium |
| H655 | `used_filament()` FIXME: retracted length not counted | Medium |
| H656 | `do_split_line()` empty result on no intersection | Low |
| H657 | `split_line<>()` `reserve()` operator precedence bug | Low |

**Next hazard number to assign: H658**

---

## Session 41 — Algorithm/LineSplit.cpp and Algorithm/RegionExpansion

### Files Annotated

- `src/libslic3r/Algorithm/LineSplit.cpp`
- `src/libslic3r/Algorithm/RegionExpansion.hpp`
- `src/libslic3r/Algorithm/RegionExpansion.cpp`

---

### Key Findings

#### Algorithm/LineSplit.cpp (H658–H668)

**Z-encoding protocol in ClipperLib_Z output:**
- Three distinct Z value classes: `is_src` (Z ≥ 0, sequential path index from `zpaths`), `is_clip` (Z == `CLIP_IDX` = `numeric_limits<cInt>::max()`), and `is_new` (Z < 0, intersection point).
- `cb_split_line` callback encodes intersection Z as `-(min_Z_of_4_endpoints + 1)`. Recovery is `to_src_idx()` = `-z - 1` for new points.
- `do_split_line()` is a four-phase algorithm: (1) run ClipperLib-Z intersection, (2) AABB-tree resolve clip-origin points, (3) sort segments by source index, (4) chain reconstruction.

**Hazards:**
- **H658** — `CLIP_IDX` sentinel collision: if source path ever has exactly `numeric_limits<cInt>::max()` points the sentinel is indistinguishable from a valid source Z.
- **H659** — Z encoding `-(min(za,zb,zc,zd)+1)`: if the minimum of 4 endpoint Zs is already negative (e.g., a re-intersected intersection point), the encoded value is positive — breaks `is_new()` classification.
- **H660** — `point_on_line()` cross-product overflow: for coords near ±2^31 the intermediate 64-bit product can overflow.
- **H661** — `point_on_line()` unenforced precondition: function assumes `p` is already known to lie on the infinite line; callers outside `do_split_line` may violate this.
- **H662** — AABB-tree resolve phase: `tree.closest_point()` finds nearest edge but does not confirm the resolved point is actually on that edge — possible silent mis-assignment for concave clip polygons.
- **H663** — `sort()` comparator for `SegmentWithSrcIdx` has non-strict-weak-ordering for two `is_src` points with the same index value — undefined behaviour in C++ standard sort.
- **H664** — Chain reconstruction loop: can silently skip a vertex if two consecutive empty nodes follow a segment tail — output path shorter than source with no diagnostic.
- **H665** — `SCALED_EPSILON` (100 nm) resolve radius fails for edges shorter than 200 nm.
- **H666** — Silent fallback for unresolved clip-origin point: uses first candidate edge without verifying the point actually lies on it.
- **H667** — (Renumbered from H663 above — see H663.)
- **H668** — (Renumbered from H664 above — see H664.)

#### Algorithm/RegionExpansion.hpp (H669–H674)

**Wave propagation algorithm overview:**
- Pipeline: `wave_seeds()` → `propagate_waves()` → `merge_expansions_into_expolygons()`
- `RegionExpansionParameters` float fields are in **scaled units** (mm × 1e6), NOT millimetres — invisible to the compiler; caller must pre-scale.

**Hazards:**
- **H669** — `RegionExpansionParameters` fields are in scaled units (not mm); no type-level enforcement — silent wrong-scale inputs.
- **H670** — `build()` can produce `nsteps = 0` if `full_expansion ≤ tiny_expansion` → `step_size` and `max_allowed_distance` become NaN/Inf.
- **H671** — `wave_seeds()` return type `std::vector<RegionExpansionSeed>` by value — may be large; no move-return guarantee pre-C++17 (though C++17 mandates NRVO in some cases).
- **H672** — `RegionExpansionSeed.src_id` is `uint32_t`; source ExPolygon vector is runtime-sized — no bounds check at seed construction.
- **H673** — `propagate_waves(seeds,...)` requires seeds sorted by `(boundary_id, src_id)` — no runtime assertion enforces this; out-of-order input produces silently wrong output.
- **H674** — `merge_expansions_into_expolygons()` uses sample-point containment fallback; if `sample_in_expolygons` returns -1 (sample falls in a hole), the source ExPolygon is silently dropped.

#### Algorithm/RegionExpansion.cpp (H675–H689)

**Hazards:**
- **H675** — `clipper_round_offset_error()` formula is dead code (commented out); the active formula uses a 1.1× safety factor — actual rounding error not verified analytically.
- **H676** — `expolygons_to_zpaths_expanded_opened()`: offset sign is based on contour index (index 0 = outer → positive offset, index > 0 = holes → negative) rather than winding direction — incorrect for improperly wound holes.
- **H677** — `merge_splits()` reconnects ClipperOffset-split closed-contour pieces by sorted endpoint lookup; if two distinct contour endpoints share the same coordinate the wrong pieces are joined — silent topology corruption.
- **H678** — `wave_seeds()` Z index ranges: `[1, idx_boundary_end)` = boundary paths, `[idx_boundary_end, idx_src_end)` = source paths, negative = intersection points. Off-by-one: Z=0 is never assigned to any path, wasting one index slot (benign but confusing).
- **H679** — Closed-seed classification via AABB-tree point-in-boundary lookup: if the sample point of a fully-interior seed happens to land exactly on a boundary edge the AABB lookup may misclassify it as `not_inside` — seed silently omitted.
- **H680** — `wavefront_step()` passes `dist` (a `float` in scaled units) directly to `ClipperOffset::Execute` expecting a `double` — precision loss for very small steps.
- **H681** — `wavefront_step()` calls `ClipperLib::Orientation()` to detect CW/CCW and negates offset sign for CW polygons; a degenerate sliver contour with area ≈ 0 can report wrong orientation — wave propagates inward instead of outward.
- **H682** — `wavefront_clip()` uses `pftPositive` fill rule; if a CW outer polygon was misdetected (H681) it contributes negative winding — intersection result is empty, wave silently terminates.
- **H683** — `propagate_waves()` main loop: `seeds` is consumed destructively (sorted/partitioned in place) — callers that retain the original seeds vector see corrupted data after call.
- **H684** — `propagate_waves()`: per-step union (`union_safety_offset_ex()`) can merge two separately expanding wavefronts belonging to different source ExPolygons — their contributions become indistinguishable before `merge_expansions_into_expolygons()` runs.
- **H685** — `merge_expansions_into_expolygons()`: `union_safety_offset_ex()` applied before containment test; if union removes a small isolated expansion that is entirely inside a hole of the target, the hole-fill is silently lost.
- **H686** — `merge_expansions_into_expolygons()`: containment fallback selects the ExPolygon whose sample point is closest to the expansion centroid — centroid not computed; uses first vertex of first contour as proxy.
- **H687** — `propagate_waves()` step count from `RegionExpansionParameters::nsteps` is `size_t`; loop variable `int i` — implicit truncation for `nsteps > INT_MAX` (cosmetic, impossible in practice).
- **H688** — `wave_seeds()`: source paths are offset by `tiny_expansion` before seed construction; if `tiny_expansion = 0` (valid parameter) the offset collapses self-touching contours — seeds differ from original polygons with no warning.
- **H689** — `merge_splits()` uses `std::lower_bound` on a coordinate-sorted vector; sort key is `(x, y)` pair — does not account for floating-point equality between integer-cast coords; two points at distance < 1 (sub-nanometre) can map to same key, causing mis-merge.

---

### Session 41 Hazard Summary

| ID | Summary | Severity |
|----|---------|----------|
| H658 | `CLIP_IDX` sentinel collision with max-count source path | Medium |
| H659 | Z encoding breaks for re-intersected intersection points | Medium |
| H660 | `point_on_line()` cross-product int64 overflow | High |
| H661 | `point_on_line()` unenforced precondition | Low |
| H662 | AABB-tree resolve: nearest edge not confirmed as containing point | Medium |
| H663 | Sort comparator non-strict-weak-ordering — UB in std::sort | High |
| H664 | Chain reconstruction silently skips vertex on double-empty node | Medium |
| H665 | SCALED_EPSILON resolve radius fails for sub-200nm edges | Low |
| H666 | Unresolved clip-origin fallback uses first candidate without check | Medium |
| H669 | `RegionExpansionParameters` fields in scaled units, no enforcement | High |
| H670 | `build()` nsteps=0 produces NaN/Inf fields | High |
| H671 | Large `wave_seeds()` return value — copy overhead | Low |
| H672 | `RegionExpansionSeed.src_id` no bounds check | Medium |
| H673 | `propagate_waves()` requires pre-sorted seeds — no assertion | High |
| H674 | `merge_expansions_into_expolygons()` silently drops hole-sample ExPolygon | High |
| H675 | `clipper_round_offset_error()` is dead code | Low |
| H676 | Offset sign by contour index, not winding — breaks for mis-wound holes | High |
| H677 | `merge_splits()` coordinate collision joins wrong contour pieces | High |
| H678 | Z index off-by-one: Z=0 unused | Low |
| H679 | AABB boundary-edge sample mis-classification | Medium |
| H680 | `float` dist passed to `ClipperOffset::Execute` (double expected) | Low |
| H681 | Degenerate sliver wrong orientation — wave propagates inward | High |
| H682 | `pftPositive` fill rule fails for CW-misdetected polygon | High |
| H683 | `seeds` vector mutated in place by `propagate_waves()` | Medium |
| H684 | Per-step union merges wavefronts from different source ExPolygons | High |
| H685 | Union before containment test silently loses hole-fill expansions | Medium |
| H686 | Containment fallback uses first vertex as centroid proxy | Low |
| H687 | `nsteps` size_t vs loop `int` truncation (cosmetic) | Low |
| H688 | `tiny_expansion=0` collapses self-touching contours | Medium |
| H689 | `merge_splits()` sub-nanometre key collision in lower_bound | Low |

**Next hazard number to assign: H690**

---

## Session 42

**Files annotated:** `src/libslic3r/PrintConfig.hpp`

**Scope:** PrintConfig.hpp is the central configuration header for the entire OrcaSlicer / Bambu Studio / PrusaSlicer config system. It defines all print, region, machine, and G-code configuration classes using a Boost.Preprocessor macro-based code-generation system, plus ModelConfig (dynamic, timestamp-gated), DynamicPrintConfig, FullPrintConfig (triple-inheritance), and Cereal ordinal-based serialization.

### Key architectural findings

**Config class hierarchy:**
- `FullPrintConfig` = `PrintObjectConfig` + `PrintRegionConfig` + `PrintConfig`
- `PrintConfig` derives from `MachineEnvelopeConfig` + `GCodeConfig`
- All static config classes are generated by `PRINT_CONFIG_CLASS_DEFINE` / `PRINT_CONFIG_CLASS_DERIVED_DEFINE` macros from `((Type, name))` tuple sequences
- `StaticCache<T>` holds name→byte-offset map and a heap-allocated `m_defaults` instance
- `ModelConfig` wraps `DynamicPrintConfig` with a monotonic `uint64_t` timestamp; timestamp equality is used to short-circuit `assign_config()` — a semantic hazard when two objects receive identical edits independently

**Macro-generated config system:**
- Each static config class generates: class body, `initialize()`, `hash()`, `operator==`, `operator<`
- `optptr()` uses `reinterpret_cast<ConfigOption*>((char*)this + offset)` — requires standard layout; breaks for virtual or non-trivially inherited bases
- Cereal serialization uses positional ordinals (sequence position in macro tuple list) — reordering or inserting fields silently corrupts old save files

**Notable design patterns:**
- `NozzleTypeEumnToStr` / `NozzleTypeStrToEumn`: static maps in header → per-TU copies (ODR-adjacent), typo "Eumn" propagated across codebase
- `flush_volumes_matrix`: flat 1D `ConfigOptionFloats`; dimension reconstructed as `sqrt(size)` — non-square arrays silently corrupt
- `wipe_tower_x`/`y`: `ConfigOptionFloats` indexed by plate index from global context variable
- `filament_ironing_*` nullable arrays: null means "inherit from parent", not zero — must be replicated exactly in port

### Hazards discovered (H690–H707)

- **H690** — `NozzleTypeEumnToStr`/`NozzleTypeStrToEumn` are `static` maps in header (internal linkage, per-TU copies; typo "Eumn")
- **H691** — `SupportType::is_tree()` constructs `std::set` on every call — hot-path allocation
- **H692** — `SupportType::is_auto()` constructs `std::set` on every call — hot-path allocation
- **H693** — `bed_type_to_gcode_string()`: `btDefault`/`btCount` silently return "unknown" — invalid G-code if bed type uninitialized
- **H694** — `DynamicPrintConfig::validate()` return value not `[[nodiscard]]` — callers silently ignore validation errors
- **H695** — `StaticPrintConfig::optptr()` uses raw byte-offset reinterpret_cast — breaks for non-standard-layout subclasses
- **H696** — Acceleration/jerk fields migrated to `PrintObjectConfig`; old projects silently zero them if `handle_legacy()` is incomplete
- **H697** — `filament_ironing_*` nullable arrays: null means inherit-from-parent; port must replicate resolution logic exactly
- **H698** — `adaptive_pressure_advance_model` string has no schema validation — potential injection vector
- **H699** — `flush_volumes_matrix` flat array; dimension reconstructed via `sqrt(size)` — non-square truncates silently
- **H700** — Flush-volume settings in `GCodeConfig`, wipe-tower geometry in `PrintConfig` — semantically coupled, split across bases
- **H701** — `wipe_tower_x`/`y` indexed by plate index from global context — wrong-plate coordinates if context not set
- **H702** — `FullPrintConfig` triple-inheritance: field-name collisions caught only by debug assert; release silently uses first winner
- **H703** — `ModelConfig::assign_config()` skips on equal timestamp even when content differs (same-key independent edits)
- **H704** — `get_flush_volumes_matrix()` default `extruder_id = (size_t)-1` wraps to SIZE_MAX — correct by unsigned-wrap coincidence
- **H705** — `set_flush_volumes_matrix()` declares `bool is_multi_extruder` but never reads it — dead code, incomplete multi-extruder path
- **H706** — Cereal ordinal serialization: implicit positional numbering; reuse/reorder silently corrupts old save files
- **H707** — Cereal `load()` unknown ordinal: assert is DEBUG-only; release → null pointer → UB

### Session 42 Hazard Summary

| ID | Summary | Severity |
|----|---------|----------|
| H690 | Static maps in header — per-TU copies, typo "Eumn" | Low |
| H691 | `is_tree()` constructs std::set on every call | Low |
| H692 | `is_auto()` constructs std::set on every call | Low |
| H693 | `bed_type_to_gcode_string()` returns "unknown" silently for invalid types | Medium |
| H694 | `validate()` return not [[nodiscard]] — errors silently ignored | Medium |
| H695 | `optptr()` reinterpret_cast breaks for non-standard-layout subclasses | High |
| H696 | Acceleration/jerk field migration — old projects silently zero | Medium |
| H697 | Nullable filament-override arrays — null = inherit, not zero | High |
| H698 | `adaptive_pressure_advance_model` no schema validation | Medium |
| H699 | `flush_volumes_matrix` sqrt dimension — non-square silent truncation | Medium |
| H700 | Flush volume / wipe tower split across base classes | Low |
| H701 | `wipe_tower_x/y` plate-indexed from global context | High |
| H702 | Triple-inheritance field collision release-silent | High |
| H703 | `assign_config()` false-skip on equal timestamps | Medium |
| H704 | Default `extruder_id=(size_t)-1` correct by unsigned-wrap | Medium |
| H705 | Dead `is_multi_extruder` in set_flush_volumes_matrix | Low |
| H706 | Cereal positional ordinals — reorder/reuse corrupts saves | High |
| H707 | Cereal unknown ordinal: assert DEBUG-only; release UB | High |

**Next hazard number to assign: H708**

---

## Session 43 — AABBTreeIndirect.hpp and AABBTreeLines.hpp

### Files Annotated
- `src/libslic3r/AABBTreeIndirect.hpp` — 1,080 lines after annotation (fully annotated)
- `src/libslic3r/AABBTreeLines.hpp` — 415 lines after annotation (fully annotated)

### Key Insights

**AABBTreeIndirect.hpp** implements a static, balanced AABB tree stored as an implicit binary heap (children at 2*i+1, 2*i+2). The same `Tree<NDims, CoordType>` template covers 2D and 3D, float and double, lines and triangles. Four query modes are provided:
1. **Closest-primitive** (`squared_distance_to_indexed_primitives_recursive`) — branch-and-bound with pruning; degrades to O(N) when query point is inside many nested bounding boxes (H715).
2. **First-hit ray** (`intersect_ray_recursive_first_hit`) — unordered left/right traversal (H719), correct but not front-to-back optimal.
3. **All-hits ray** (`intersect_ray_recursive_all_hits`) — exhaustive, sorts by t after traversal.
4. **All-within-radius** (`indexed_primitives_within_distance_squared_recurisve`) — typo in function name (H720).

The `traverse()` function with `Intersecting<Box>` / `Within<Box>` predicates provides a generic policy-based traversal interface for custom queries.

**AABBTreeLines.hpp** builds on AABBTreeIndirect for 2D line-segment operations. `LinesDistancer<LineType>` is the user-facing API wrapping the tree. The most critical hazards are the dual-ray point-in-polygon test (H717) which can return 0 (indeterminate) for degenerate geometry, and the signed-distance computation (H718) which silently returns 0 when outside() is indeterminate.

### Hazards Identified (H708–H720)

| ID | Summary | Severity |
|----|---------|----------|
| H708 | Tree is static — no incremental update, stale queries after mutation | High |
| H709 | `build()` / `build_modify_input()` consume/destroy the input vector | Medium |
| H710 | `build_recursive()` requires pre-allocated node array; direct calls can OOB | Medium |
| H711 | `BoundingBoxWrapper::centroid()` operator-precedence bug shifts centroid | Low |
| H712 | `ray_box_intersect_invdir()` mutates local box copy — confusing but safe | Low |
| H713 | SSE optimisation absent in ray-box test — known performance gap | Low |
| H714 | `closest_point_to_triangle()` missing degenerate-edge guards for AC/BC | Medium |
| H715 | Branch-and-bound degrades to O(N) when query point inside many nested boxes | Medium |
| H716 | `build_aabb_tree_over_indexed_triangle_set()` eps defaults to 0 — ray misses | Medium |
| H717 | `point_outside_closed_contours()` returns 0 (indeterminate) for degenerate geometry | High |
| H718 | Signed-distance returns 0 when `outside()` is indeterminate (H717) | High |
| H719 | First-hit ray traversal is unordered (left before right always) | Low |
| H720 | Typo in function name `..._recurisve` propagates to all call sites | Low |

**Next hazard number to assign: H721**

---

## Session 44 — ExtrusionEntity.hpp/.cpp and ExtrusionEntityCollection.hpp/.cpp

### Files Annotated
- `src/libslic3r/ExtrusionEntity.hpp` — fully annotated (~614 lines after injection)
- `src/libslic3r/ExtrusionEntity.cpp` — fully annotated (~324 lines after injection)
- `src/libslic3r/ExtrusionEntityCollection.hpp` — fully annotated (~176 lines after injection)
- `src/libslic3r/ExtrusionEntityCollection.cpp` — fully annotated (174 lines after injection)

All four files committed in one commit: `5a76199864`
(`annotate: ExtrusionEntity.hpp/.cpp and ExtrusionEntityCollection.hpp/.cpp (H721-H737)`)

### Key Insights

**Toolpath ownership model** — `ExtrusionEntityCollection::entities` is a `std::vector<ExtrusionEntity*>` where the collection owns every pointer. `clear()` manually `delete`s each element; `operator=` calls `clear()` then re-clones. Every `append()` call clones its argument. `filter_by_extrusion_role()` deliberately returns a *shallow view* (no ownership transfer) — callers must not call `clear()` on the source while holding filtered results.

**Polymorphic hierarchy**
```
ExtrusionEntity (ABC)
  ├─ ExtrusionPath
  │    ├─ ExtrusionPathSloped
  │    └─ ExtrusionPathOriented
  ├─ ExtrusionMultiPath
  ├─ ExtrusionLoop
  │    └─ ExtrusionLoopSloped
  └─ ExtrusionEntityCollection
```
`ExtrusionRole` is a `uint8_t` enum (20 values; `erMixed` for collections). `ExtrusionLoopRole` is an implicit bitmask with no `operator|` defined — callers cast to/from `uint8_t` manually.

**Notable implementation patterns**
- `ExtrusionLoopSloped` constructor uses a recursive lambda `handle_line` that bisects segments to enforce `slope_max_segment_length` — the seam-entry Z-hop ramp feature (H731).
- `extrusion_entities_append_paths_with_wipe()` groups paths within `3×width` into `ExtrusionMultiPath` with no-extrusion wipe connector segments — Bambu/Orca-specific wipe optimization (H726/H728).
- `chained_path_from()` always clones filtered entities then delegates to `chain_and_reorder_extrusion_entities()` (ShortestPath.hpp) for greedy nearest-neighbour O(n²) ordering.
- `flatten()` uses a local struct with `recursive_do()` — respects `no_sort` flag when `preserve_ordering=true`.

**Bug documented**: `clip_front()` for `erPerimeter` overrides `clip_dist` inside the `while (distance > 0)` loop with a value derived from `ext_perimeter_overlap * crossection`. If `ext_perimeter_overlap` is near-zero, `clip_dist → 0` while `distance` stays positive — near-infinite loop (H727). The dynamic role-check on the first path makes this conditional on loop rotation state (H733).

### Hazards Identified (H721–H737)

| ID | Summary | Severity |
|----|---------|----------|
| H721 | `ExtrusionLoopRole` is an implicit bitmask with no `operator\|` — explicit casts required | Medium |
| H722 | `elrDefault=0` cannot be tested with `& elrDefault` — must compare `== elrDefault` | Low |
| H723 | Default `ExtrusionPath` has `mm3_per_mm=-1` — `total_volume()` returns negative | Medium |
| H724 | `polyline` is public mutable — no caching risk in C++, but caution for ports | Low |
| H725 | `SlopedParams` stale after clip — slope geometry inconsistent with clipped path | Medium |
| H726 | `3×width` wipe-grouping threshold hardcoded — can merge unrelated perimeter loops | Low |
| H727 | `clip_front()` `erPerimeter` branch near-zero `clip_dist` near-infinite loop | High |
| H728 | `ExtrusionMultiPath` wipe connectors alias freed memory on shallow copy | High |
| H729 | `polygons_covered_by_spacing`: spacing==width returns width polygon (correct but confusing) | Low |
| H730 | `split_at_vertex()` purely geometric — no seam quality check | Low |
| H731 | `ExtrusionLoopSloped` recursive lambda depth unbounded in theory | Low |
| H732 | `clip_slope()` ramp positions depend on loop rotation state — stale after split_at | Medium |
| H733 | `clip_front()` role taken from first path dynamically — inconsistent with H727 | Medium |
| H734 | `role_to_string()`/`string_to_role()` not updated for new roles → "Unknown" silent corruption | Medium |
| H735 | `filter_by_extrusion_role()` returns shallow aliased ptr view — dangling if source destroyed | High |
| H736 | `filter_by_extrusion_role_in_place()` erases without deleting — leaks on owned vectors | High |
| H737 | `chained_path_from()` partial-clone exception path leaks already-cloned objects | Medium |

**Next hazard number to assign: H738**

---

## Session 45 — AABBMesh.cpp + AABBMesh.hpp

### Files Processed
- `src/libslic3r/AABBMesh.cpp` (full, 401 lines)
- `src/libslic3r/AABBMesh.hpp` (full)

### Key Discoveries

**Pimpl pattern for AABB encapsulation**
`AABBMesh` uses the pimpl idiom with an inner `AABBImpl` class to hide the `AABBTreeIndirect::Tree3f` from the header. The `AABBImpl` is heap-allocated via a `unique_ptr<AABBImpl>` (destructor defined in .cpp so the incomplete type is resolved correctly).

**Non-owning raw pointer to mesh**
`m_tm` is a raw `const indexed_triangle_set*` — the mesh data is never owned by `AABBMesh`. Both constructors (from `indexed_triangle_set` and from `TriangleMesh`) store only a pointer. The copy constructor/assignment copy this pointer shallowly — both objects share the same mesh data with no reference counting (H738, H740).

**Ray query pipeline**
- `query_ray_hit()` — first-hit only; uses `igl::Hit` (float t); normalised-dir assert is DEBUG-only (H742). Float→double widening of `t` silently loses precision for large meshes (H747).
- `query_ray_hits()` — all-hits; sorts by `t`, deduplicates by exact float equality (`a.t == b.t`) — near-duplicate grazing-edge hits survive (H743).
- `normal_by_face_id()` — calls Eigen `.normalized()` on the cross-product; degenerate triangles produce NaN propagation (H746).

**Dead code: `SLIC3R_HOLE_RAYCASTER`**
`filter_hits()` merges object hits with hole cylinder intersections using a two-pointer sweep. Post-increment past `.back()` in the loop produces a dangling past-the-end pointer (H748). The feature was never shipped.

**Adaptive epsilon**
`AABBImpl::init()` optionally scales the Möller–Trumbore epsilon as `1e-6 * l²` (average edge length squared) to adapt to mesh scale. The adaptation is one-sided: very large meshes with the default epsilon miss near-surface rays; very small meshes with calculated epsilon over-eagerly fire (H739).

### Hazards Identified (H738–H748)

| ID | Summary | Severity |
|----|---------|----------|
| H738 | `m_tm` raw pointer — mesh lifetime must exceed `AABBMesh` lifetime | High |
| H739 | Epsilon scaling: large meshes with default 1e-6 miss near-surface rays | Medium |
| H740 | Copy constructor/assignment shallow-copies `m_tm` — shared dangling pointer risk | High |
| H741 | `hit_result::is_inside()` uses raw dot product on un-normalized dir | Medium |
| H742 | `query_ray_hit()` normalised-dir assert is DEBUG-only | Medium |
| H743 | All-hits dedup uses exact float equality — near-duplicate grazing hits survive | Medium |
| H744 | Eigen 1×3 row-vector vs Vec3d column-vector layout in `squared_distance()` | Low |
| H745 | `vertices(idx)` / `indices(idx)` no bounds check — UB in release | Medium |
| H746 | `normal_by_face_id()` returns NaN for degenerate (zero-area) triangles | High |
| H747 | `igl::Hit::t` is `float`; widened to `double` — precision loss at large scale | Medium |
| H748 | Dead-code `filter_hits()` post-increments past `.back()` — dangling pointer UB | High |

**Commit:** `annotate: AABBMesh.cpp/.hpp (H738-H748)`

---

## Session 46 — Brim.cpp + Brim.hpp

### Files Processed
- `src/libslic3r/Brim.cpp` (full, 1127 lines)
- `src/libslic3r/Brim.hpp`

### Key Discoveries

**OrcaSlicer extensions over PrusaSlicer brim**
Brim.cpp is substantially extended from the PrusaSlicer baseline:
1. **Per-extruder brim ordering** — brim areas keyed by `ObjectID` + extruder index
2. **Auto-brim width** (`configBrimWidthByVolumeGroups`) — uses second moment of area (`compSecondMoment`), object height, and per-filament adhesion coefficient to compute a structurally-motivated brim width
3. **Painted brim ears** — user-placed anchor discs stored as `BrimPoints` in model metadata; positions snapped to the post-EFC outline
4. **Auto brim ears** — convex/concave vertex detection (ported from SuperSlicer's `detect_brim_points`)
5. **EFC outline snapping** — `use_brim_efc_outline()` and `get_print_object_bottom_layer_expolygons()` snap brim placement to the post-ElephantFootCompensation boundary
6. **Printable-area clipping** — per-extruder reachable zone intersection prevents brim outside build plate
7. **Wipe-tower exclusion** — a no-brim exclusion zone around the wipe tower

**Second moment of area algorithm (H752)**
`compSecondMoment(Polygon, Vec2d&)` uses the shoelace-based moment formula. Polygon coordinates are scaled integers (×1e6), so the result is in `(scaled_units)^4`. Callers must multiply by `SCALING_FACTOR^4` to get mm⁴. Missing this conversion silently produces wildly wrong auto-brim widths.

**Auto-brim heuristic magic numbers (H753, H754)**
`configBrimWidthByVolumeGroups` divides by `Ixx + Iyy` to compute a "tipping risk" metric. Division by near-zero (degenerate polygon) is not guarded (H753). The denominator includes the constant 1920, which is empirically tuned and not documented (H754).

**Iterative brim loop generation (H757, H758)**
`make_brim_by_linesType_in_object` uses a `while (true)` loop that terminates only when `islands_ex` empties. If ClipperOffset can never shrink `islands_ex` to empty (e.g., degenerate geometry with zero-area offset), this loops forever. The 1.3/−0.3 offset split ratio is an undocumented heuristic magic number.

**Plate offset applied after connect_brim_lines (H759)**
`make_brim()` calls `connect_brim_lines()` before applying the plate offset translation. This is correct but easy to break if the call order is swapped — brim connectivity would be computed in the wrong coordinate space.

**const_cast on print object (H760)**
`make_brim()` uses `const_cast<PrintObject*>(print.get_object(i))` to write `firstLayerObjectBrimBoundingBox`. This casts away const on a `const Print&` parameter — a design smell indicating that brim results are stored back into the object rather than returned through a cleaner output channel.

**Undocumented `make_brim_auto` declaration**
`Brim.hpp` declares `make_brim_auto` but no definition exists in `Brim.cpp` — possibly a dead/removed feature (flagged as [UNCLEAR] in this entry).

### Hazards Identified (H749–H760)

| ID | Summary | Severity |
|----|---------|----------|
| H749 | Per-instance diff against global accumulator — earlier instances steal brim area from later ones | Medium |
| H750 | `getadhesionCoeff()` last-match-wins — multi-material objects use wrong adhesion coefficient | Medium |
| H751 | Dead commented-out code block after `return adhesionCoeff` — confuses readers | Low |
| H752 | `compSecondMoment()` returns result in (scaled units)^4 — callers must apply SCALING_FACTOR^4 | High |
| H753 | `height_to_area` divides by Ixx/Iyy — no guard for near-zero denominator (degenerate polygon) | High |
| H754 | Constant 1920 in `height_to_area` is empirically tuned and undocumented | Low |
| H755 | Connectivity filter uses 2×flow_spacing offset threshold — may merge separate objects' brims | Low |
| H756 | Support brim path has large commented-out blocks — design intent unclear | Low |
| H757 | `make_brim_by_linesType_in_object` `while(true)` — infinite loop on degenerate geometry | High |
| H758 | 1.3/−0.3 offset split ratio is an undocumented heuristic | Low |
| H759 | Plate offset applied after `connect_brim_lines` — brittle call-order dependency | Medium |
| H760 | `const_cast<PrintObject*>` in `make_brim()` — casts away const to write brim bbox | Medium |

**Unclear items logged:**
- `make_brim_auto` is declared in `Brim.hpp` but NOT defined in `Brim.cpp` — possibly a removed/dead feature.

**Commit:** `annotate: Brim.cpp/.hpp (H749-H760)`

**Next hazard number to assign: H761**

---

## Session 47 — ArcFitter.cpp + ArcFitter.hpp

### Files Processed
- `src/libslic3r/ArcFitter.cpp` (full, 220 lines)
- `src/libslic3r/ArcFitter.hpp` (full)
- `src/libslic3r/Circle.hpp` (read for ArcSegment context; not annotated)

### Key Discoveries

**Greedy sliding-window arc fitting algorithm**
`do_arc_fitting()` uses a greedy sliding-window approach: a front cursor advances as long as all points in `[front_index, i]` lie within `tolerance` of a single circle (tested by `try_create_arc()`). When the circle test fails, the last successful arc is committed and the window resets. Adjacent runs of 2 points that could not form an arc become `Linear_move` entries, merged by extending `end_point_index` rather than appending new entries. This is O(n) amortised.

**In-place mutation of points vector (H761)**
`do_arc_fitting_and_simplify()` takes `points` by non-const reference and modifies the vector in place — points that were merged into arcs are removed, compacting the array. This is the core hazard: any caller that holds references, iterators, or index offsets into `points` before the call will have dangling/stale state after it returns.

**Prefix-sum index remapping (H762)**
After arc fitting, indices in `PathFittingData` entries refer to the *original* point array. After compaction the mapping from original indices to new positions is computed via a prefix-sum over a `reduce_count` array. This requires segments to be strictly ascending and non-overlapping in index space. Any future change to the arc-fitting window strategy that produces overlapping index ranges would silently corrupt remapping.

**Size < 3 edge case (H763)**
`do_arc_fitting()` guards `points.size() < 3` with an early return that emits a single `Linear_move` spanning `[0, size-1]`. For `size == 0`, `points[size-1]` is `points[-1]` — undefined behaviour in release builds. Callers must guarantee `size >= 1`.

**ArcSegment reverse direction hazard**
`ArcSegment::reverse()` mutates `arc_data.direction` — no comment warns callers that reversing the segment also requires reversing the associated index range in `PathFittingData`. The `.hpp` annotation flags this with `[HAZARD]`.

### Hazards Identified (H761–H763)

| ID | Summary | Severity |
|----|---------|----------|
| H761 | `do_arc_fitting_and_simplify` mutates caller's `points` vector in-place | High |
| H762 | Prefix-sum index remapping assumes non-overlapping ascending segments | High |
| H763 | `do_arc_fitting` size<3 guard: size==0 → UB accessing `points[-1]` | Medium |

**Commit:** `annotate: ArcFitter.cpp/.hpp (H761-H763)`

**Next hazard number to assign: H764**

---

## Session 48 — ElephantFootCompensation.cpp/.hpp + PrintConfig.cpp (partial)

### Files Processed
- `src/libslic3r/ElephantFootCompensation.cpp` (full, 817 lines)
- `src/libslic3r/ElephantFootCompensation.hpp` (full)
- `src/libslic3r/PrintConfig.cpp` (lines 1–80 annotated; lines 81–10836 remain)

### Key Discoveries

**ElephantFootCompensation 6-step pipeline**
The active code path (`elephant_foot_compensation()`) follows six steps:
1. **Simplify** — Douglas-Peucker simplification of the first-layer outline
2. **Resample** — uniform resampling at 0.5 mm spacing (`resample_by_length()`)
3. **contour_distance2** — nearest-point EdgeGrid lookup to compute per-sample wall thickness
4. **Delta conversion** — converts thickness to per-sample shrink delta
5. **Banded Laplacian smooth** — smooths the delta field over a neighbourhood of width `band`
6. **variable_offset_inner_ex** — applies per-sample variable-width inward offset to produce final ExPolygon

**Legacy contour_distance() vs active contour_distance2()**
`contour_distance()` uses a fan of 29 SDF rays from each contour point — correct but slow. `contour_distance2()` replaces it with a cheaper EdgeGrid nearest-point query that also avoids false positives at concavities. The old function is kept for reference comparison; both carry the ≤2-point silent empty-output edge case (H765, H767).

**Negative compensation expands outline (H764)**
The public API accepts `compensation` as a plain `double`. Positive values shrink (intended). Negative values silently expand — no clamping or assertion in the public-facing header.

**Magic constant 0.48 in fan angle (H766)**
`contour_distance()` computes the fan half-angle from the cross product of adjacent edge directions using `0.48` — empirically tuned, not documented in any external reference.

**band parameter in scaled units (H768)**
The Laplacian smooth `band` parameter is in `coord_t` scaled units (×1e6 mm). The `compensation` value is internally scaled, but `band` is derived from a separately scaled expression. A port in unscaled mm must explicitly convert.

**variable_offset_inner_ex fallback (H769)**
If the final offset step returns ≠1 ExPolygon, `elephant_foot_compensation()` silently returns the original unmodified input. No log in production; only a debug SVG when `TESTS_EXPORT_SVGS` is defined.

**PrintConfig.cpp structure (partial annotation)**
- File-level block comment injected (lines 1–30) ✅
- Anonymous namespace helpers `SplitStringAndRemoveDuplicateElement` and `ReplaceString` annotated (lines 49–80) ✅
- Enum map tables, utility functions, `init_*` bodies, and end utilities remain unannotated

**Key insight for PrintConfig.cpp: L() vs _()**
`L(s)` is an extraction marker only (evaluates to `s` at runtime — it is NOT a translator). `_(s)` is the runtime i18n translator. Confusing them would silently skip translation or produce build errors. This distinction must be preserved in any port (H771).

### Hazards Identified (H764–H769)

| ID | Summary | Severity |
|----|---------|----------|
| H764 | Negative `compensation` silently expands outline instead of shrinking | Medium |
| H765 | `contour_distance()` returns empty for ≤2-point contours (legacy, but same in active path) | Low |
| H766 | Fan angle magic constant 0.48 — empirically tuned, undocumented | Low |
| H767 | `contour_distance2()` returns empty for ≤2-point contours — active code path | Medium |
| H768 | `band` parameter must be in scaled coord_t units; passing mm gives 1e6× wrong neighbourhood | High |
| H769 | `variable_offset_inner_ex` ≠1 result → silent fallback to original input in production | Medium |

**Note:** H770 and H771 will be assigned during PrintConfig.cpp full annotation.

**Commit:** `annotate: ArcFitter, ElephantFootCompensation, PrintConfig partial (Session 48)`

**Next hazard number to assign: H772**

---

## Session 49 — PrintConfig.cpp Full Annotation

### Files Processed
- `src/libslic3r/PrintConfig.cpp` (~11,051 lines) — full annotation pass

### Key Discoveries

**PrintConfig.cpp is the largest single source file at ~11,051 lines.** It encodes the entire configuration schema for OrcaSlicer: all option keys, enum string maps, default values, legacy migration logic, and helper utilities.

**`L()` vs `_()` distinction (H774)**
`L(s)` is a GNU gettext extraction marker that expands to `s` at runtime — it does NOT translate. `_(s)` is the actual runtime i18n function. Using `_()` inside `set_default_value()` during static init would translate before the locale is loaded. Confusing the two silently leaves UI strings untranslated.

**`PrintConfigDef` singleton construction order is mandatory**
`init_common_params()` → `assign(ptAny)` → `init_fff_params()` → `init_extruder_option_keys()` → `assign(ptFFF)` → `init_sla_params()` → `assign(ptSLA)`. Later stages depend on options registered in earlier stages. The constructor is called once at static init via `s_def` global.

**Enum map duplicate key (H770)**
`s_keys_map_WallInfillOrder` contains a duplicate string key — `std::map` silently drops the second insertion. Two values are inaccessible by name lookup.

**`handle_legacy()` untested (H776)**
A ~244-line if/else chain that renames old config keys to current ones. No unit test coverage. Any mapping error silently drops the setting for old project files.

**`get_shared_poly()` OOB on empty intersection (H777)**
If any two extruder printable areas do not overlap, `result_polygon[0]` is an out-of-bounds access.

**`get_extruder_ams_count()` uncaught `stoi` (H773)**
Malformed `ams_info` string causes `std::invalid_argument` or `std::out_of_range` to propagate uncaught.

**`normalize_fdm()` not idempotent (H780)**
Erases `"extruder"` key on first call; subsequent calls silently skip propagation.

### Sections Annotated in PrintConfig.cpp
- `enum_names_from_keys_map()` + `CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS` macro block (lines ~116–133)
- Enum map section header block (lines ~134–200)
- `s_keys_map_WallInfillOrder` — H770
- `s_keys_map_TimelapseType` — H771
- `get_extruder_variant_string()` — H772
- `get_extruder_ams_count()` — H773
- `save_extruder_ams_count_to_string()`
- `assign_printer_technology_to_unknown()`
- `PrintConfigDef::PrintConfigDef()` constructor
- `init_common_params()` — H774
- `init_fff_params()` — H775
- `init_extruder_option_keys()`
- `init_filament_option_keys()`
- `init_sla_params()`
- `handle_legacy()` — H776
- `get_shared_poly()` — H777
- `get_bed_shape()` overloads
- `get_bed_excluded_area()` — H778
- `min_object_distance()` — H779
- `normalize_fdm()` — H780

### Hazards Identified (H770–H780)

| ID | Summary | Severity |
|----|---------|----------|
| H770 | `s_keys_map_WallInfillOrder` duplicate key — second entry silently dropped | Low |
| H771 | `TimelapseType` uses numeric `"0"`/`"1"` keys for preset compat — renaming breaks all saved files | Medium |
| H772 | `get_extruder_variant_string()` sentinel not updated → OOB on new enum value | Medium |
| H773 | `get_extruder_ams_count()` uncaught `stoi` exception on malformed input | High |
| H774 | `L()` vs `_()` distinction: `L()` is extraction marker only, NOT runtime translator | High |
| H775 | `init_fff_params()` default value change silently alters all new profiles | Medium |
| H776 | `handle_legacy()` ~244-line chain, no unit tests, silent drop on mismatch | High |
| H777 | `get_shared_poly()` OOB access when extruder areas have no intersection | High |
| H778 | `get_bed_excluded_area()` returns degenerate polygon for 0/1 config points | Medium |
| H779 | `min_object_distance()` hardcoded 6 mm floor — global implicit minimum | Low |
| H780 | `normalize_fdm()` erases `"extruder"` key; not idempotent on repeated calls | Medium |

**Commit:** `annotate: PrintConfig.cpp full annotation (H770-H780) (Session 49)`

**Next hazard number to assign: H781**

---

## Session 50 — Arrange.cpp + Arrange.hpp Annotation

### Files Processed
- `src/libslic3r/Arrange.hpp` (219 lines) — fully annotated
- `src/libslic3r/Arrange.cpp` (1,155 lines) — fully annotated

### Key Discoveries

**Architecture: NFP bin-packing with custom multi-objective scoring**
OrcaSlicer uses libnest2d (vendored, NFP-based) as the packing backend.  The `AutoArranger<TBin>` class wraps it with a custom objective function that blends:
- Geometric density (pile bounding box area / bin area)
- Corner-distance score (distance from bin corner or center)
- Neighbour alignment score (R*-tree query for same-area items)
- Material compatibility penalties (filament type, bed/print temperature)
- Sequential-print height and clearance penalties (rod/lid clearance constraints)

**Bed shape dispatch (call_with_bed)**
A raw Points vector is classified at runtime into one of four bin types:
- BoundingBox if poly_area/bbox_area > 99.9% (rectangle test)
- CircleBed if all vertices within 10*SCALED_EPSILON of avg radius
- Polygon otherwise
- InfiniteBed for 0 or 1 input points

**Pre-arrange call order is mandatory**
The four update functions must be called in this order:
1. `update_arrange_params()` — computes bed_shrink from skirt/clearance
2. `update_selected_items_inflation()` — per-item inflation from brim/clearance
3. `update_unselected_items_inflation()` — inflates fixed items
4. `update_selected_items_axis_align()` — optional PCA rotation
5. `get_shrink_bedpts()` — returns shrunken bed polygon

All four mutate ArrangeParams or ArrangePolygon state. No assertion enforces order.

**ArrangePolygon.allowed_rotations is dead**
The field exists in the struct but is silently ignored — only `params.allow_rotations` controls whether rotations are tried, using a fixed {0°, 45°, 90°, 135°} set from `fill_config()`.

**process_arrangeable winding contract**
libnest2d requires clockwise winding. `process_arrangeable()` reverses CCW polygons. Upgrade to Clipper2 (which inverts winding) would silently break all item placement.

**objfunc weight constants are empirical**
Score blending weights (0.8/0.2, 0.5/0.5, 0.2/0.8, alignment_weight) are hardcoded and not documented with design rationale.  Any port must reproduce these exactly or re-tune.

### Hazards Identified (H781–H794)

| ID | Summary | Severity |
|----|---------|----------|
| H781 | `update_arrange_params()` not idempotent — calling twice doubles bed shrink | Medium |
| H782 | Inflation clamp in `update_selected_items_inflation()` uses magic constant 5 | Low |
| H783 | Tree-support plate: all items inflated to max branch radius, not per-item brim | Low |
| H784 | `update_unselected_items_inflation()` depends on call-order after `update_arrange_params()` | Medium |
| H785 | Axis-align threshold 0.66 undocumented — may inconsistently rotate near-equal-moment objects | Low |
| H786 | `get_shrink_bedpts()` SGN() gives wrong direction for concave bed vertices | Medium |
| H787 | `fill_config()` misleading comment: TOP_RIGHT branch says "center" | Low |
| H788 | Alignment scoring disabled for objects of unique area — score stays at 1.0 (worst) | Medium |
| H789 | `objfunc()` height_score partial-count division when loop breaks early | Low |
| H790 | `_arrange()` zeroes min_obj_distance assuming pre-inflated items — no enforcement | Medium |
| H791 | Dead `md` variable in `_arrange()` — `sl::offset` call is commented out | Low |
| H792 | `process_arrangeable()` winding reversal fragile to Clipper2 upgrade | Medium |
| H793 | `call_with_bed()` 0.1% rectangle coercion loses chamfer/irregularity constraint | Low |
| H794 | fixeditems deflated twice (-2*EPSILON each) in arrange<BedT> + fill_config | Low |

**Commit:** `annotate: Arrange.cpp + Arrange.hpp (H781-H794) (Session 50)`

**Next hazard number to assign: H795**

---

## Session 51

**Files processed:**
- `src/libslic3r/BuildVolume.hpp` — bug fixed (removed duplicate `BuildSharedVolume` struct that had been erroneously introduced) + full annotation added (H795–H796)
- `src/libslic3r/BuildVolume.cpp` — full annotation completed for all functions (H797–H802)

**Fill module confirmation (pre-annotated, verified this session):**
- `src/libslic3r/Fill/Fill3DHoneycomb.cpp` + `.hpp` — confirmed annotated
- `src/libslic3r/Fill/FillHoneycomb.cpp` + `.hpp` — confirmed annotated
- `src/libslic3r/Fill/FillPlanePath.cpp` + `.hpp` — confirmed annotated
- `src/libslic3r/Fill/FillLine.cpp` + `.hpp` — confirmed annotated
- `src/libslic3r/Fill/FillCrossHatch.cpp` + `.hpp` — confirmed annotated

**Key discoveries this session:**

**BuildVolume duplicate struct bug (fixed)**
`BuildVolume.hpp` contained a duplicate definition of `BuildSharedVolume` (lines 81-107, identical to the struct defined at lines 53-79). This was introduced in a prior annotation session. The duplicate was removed.

**BuildVolume classification pipeline**
The constructor runs a three-tier classification:
1. Rectangle: `|area - bbox_area| < SCALED_EPSILON²`
2. Circle: RANSAC fit + vertex error < 0.005 mm + midpoint undershoot < 3 mm
3. Convex vs Custom: convex hull area vs polygon area comparison

For Convex/Custom: builds two `top_bottom_convex_hull_decomposition` structures at `SceneEpsilon` and `BedEpsilon` — one for scene placement, one for G-code validation.

**Extruder volume shared descriptor**
`m_shared_volume` is initialised from `m_bboxf` (the bed bbox) then iteratively reduced to the minimum intersection of all extruder bboxf values. The result is the printable region reachable by all extruders simultaneously. Used by the GL preview rendering layer.

**rectangle_test permanently disabled (H802)**
A full O(T) triangle-vs-rectangle intersection test exists in `#if 0`. The active code uses only the faster vertex-only test which the in-code FIXME acknowledges as incorrect for non-convex objects against rectangular volumes.

**check_object_state_with_extruder_area blind spots (H800)**
For extruder shapes classified as Convex/Custom/Invalid, the switch falls through to `default: break`. `return_state` remains `Inside` — no check is performed. For non-rectangular non-circular multi-extruder printers, extruder reachability is silently unvalidated.

### Hazards Identified (H795–H802)

| ID | Summary | Severity |
|----|---------|----------|
| H795 | `m_shared_volume.zs[1]` can be silently reduced below printable_height if any extruder bboxf was inflated | Medium |
| H796 | `assert(printable_height >= 0)` is no-op in release — negative height silently sets bboxf.max.z < 0 | Medium |
| H797 | `object_state_templ` counts vertices not triangles — surface-straddle edge cases may be missed for large meshes | Low |
| H798 | `BuildVolume_Type::Custom` uses convex hull test — non-convex notch areas falsely reported Inside | High |
| H799 | `all_paths_inside()` Rectangle path uses O(1) bbox shortcut — per-move testing never performed | Low |
| H800 | `check_object_state_with_extruder_area()` silently returns Inside for Convex/Custom/Invalid extruder shapes | High |
| H801 | Custom and Convex use identical containment code — Custom classification semantics are not honoured | Medium |
| H802 | `rectangle_test()` accurate triangle-vs-rect test is `#if 0` disabled — active vertex-only path is documented as incorrect | Medium |

**Commit:** `annotate: BuildVolume.hpp bug-fix + full annotation (H795-H802) (Session 51)`

**Next hazard number to assign: H803**

---

## Session 52 — SLAPrint Module (SLA Resin Print Orchestration)

### Files Processed
- `src/libslic3r/SLAPrint.hpp` — fully annotated
- `src/libslic3r/SLAPrint.cpp` — fully annotated
- `src/libslic3r/SLAPrintSteps.hpp` — fully annotated
- `src/libslic3r/SLAPrintSteps.cpp` — fully annotated (all step function bodies)

### Summary

SLAPrint is the resin-printer analogue to the FFF `Print` class. It drives a strictly ordered 7-step per-object pipeline (Hollowing → DrillHoles → ObjectSlice → SupportPoints → SupportTree → Pad → SliceSupports) followed by 2 global print-level steps (MergeSlicesAndEvalStats → Rasterize). The step state machine inherits from `PrintBase` and uses `PrintState<>` to track invalidation and scheduling.

**Key architectural observations:**

- `SLAPrintObject::SupportData` inherits from `sla::SupportableMesh` by value — construction performs a full O(V+T) mesh copy, not a reference-counted or pointer-based share.
- `HollowingData::hollow_mesh_with_holes` and `hollow_mesh_with_holes_trimmed` are declared `mutable` with lazy population semantics but no mutex guard — concurrent const access (e.g., from UI thread + slicer thread) can race.
- `SLAPrint::m_printer` is a raw pointer to `SLAArchive`. No RAII lifetime management — dangling pointer risk if archive is destroyed before `process()` completes.
- `SLAPrint::PrintLayer` stores `reference_wrapper<const SliceRecord>` — if any object's step is invalidated mid-print, all PrintLayer references into its m_slice_index become dangling.
- `invalidate_state_by_config_options()` has `assert(false)` for unrecognized config keys (debug only) — new config keys added without updating this function silently pass in release builds.

**Dead code / no-op steps:**

- `drill_holes()` body is entirely inside a `/* ... */` block — drain holes are **never actually drilled** in the current build. The AABBTreeIndirect traversal was also explicitly commented out with a BBS annotation.
- `emesh.load_holes()` calls are commented out in both `support_points()` and `support_tree()` — drain holes are invisible to the support generator and support tree builder.
- SlicingMode config enum switch is commented out in `slice_model()` — always uses `Regular` mode regardless of user config.

**FaceHash (local struct):**
Encodes each triangle geometrically (cross-product + centroid, scaled int64) for post-CGAL-boolean triangle identification. Used by `create_exclude_mask()` to identify interior mesh faces in the merged result. Hash collision risk is low but theoretically possible for degenerate triangles.

**merge_slices_and_eval_stats() hazards:**
- `fade_layer_time` is decremented inside a SpinningMutex, but TBB does not guarantee iteration order — fade ramp may be applied to wrong layers in parallel mode, producing a slightly inaccurate print time estimate.
- `supports_polygons.reserve()` loop is a copy-paste error: it accumulates `soModel` sizes instead of `soSupport`, causing the supports reserve to be undersized.

**initialize_printer_input() latent bug:**
The `mx` variable intended to track the maximum slice index size across all objects is never actually set — the `if (auto m = o->get_slice_index().size() > mx)` assigns the bool comparison result to `m`, then assigns that bool to `mx`. `mx` is always 0 or 1. The `printer_input.reserve(mx)` call is effectively a no-op.

**sla_trafo() constraint:**
Uses only `instances.front()` for rotation/scale — assumes all instances of an SLA object share identical orientation. This is a valid SLA constraint (all instances printed at same angle) but is not enforced at the data layer, only by convention.

### Hazards Identified (H803–H810)

| ID | Summary | Severity |
|----|---------|----------|
| H803 | `SLAPrintObject::SupportData` inherits `sla::SupportableMesh` by value — O(V+T) mesh copy at construction | Medium |
| H804 | `HollowingData::hollow_mesh_with_holes` and `hollow_mesh_with_holes_trimmed` are `mutable` with no mutex — concurrent const access can race | High |
| H805 | `SLAPrint::m_printer` is a raw `SLAArchive*` pointer — no RAII; dangling pointer if archive destroyed before process() | High |
| H806 | `SLAPrint::PrintLayer` stores `reference_wrapper<const SliceRecord>` — references dangle if any object step is invalidated after PrintLayer construction | High |
| H807 | `invalidate_state_by_config_options()` has `assert(false)` for unrecognized keys (debug only) — new config keys silently pass in release | Medium |
| H808 | `drill_holes()` body entirely commented out — drain holes never drilled; hollowing is non-functional end-to-end | Critical |
| H809 | `initialize_printer_input()` mx tracking bug — bool assigned to size_t; `printer_input.reserve(mx)` always reserves 0 or 1 | Low |
| H810 | `merge_slices_and_eval_stats()` copy-paste error — `supports_polygons.reserve()` accumulates `soModel` sizes instead of `soSupport` | Low |

**Commit:** `annotate: Session 52 — SLAPrint.hpp/cpp + SLAPrintSteps.hpp/cpp (H803–H810)`

**Next hazard number to assign: H811**

---

## Session 53 — Slicing.hpp + Slicing.cpp

### Files Processed
- `src/libslic3r/Slicing.hpp` — fully annotated (file-level block, `SlicingParameters` struct, `equal_layering`, all free-function declarations, `HeightProfileSmoothingParams`, `LayerHeightEditActionType`, `Slicing::min/max_layer_height_from_nozzle`)
- `src/libslic3r/Slicing.cpp` — fully annotated (all 10 functions)

### Key Discoveries

**Layer height profile encoding:**  
The profile is a flat `vector<coordf_t>` with adjacent pairs `[z_i, h_i]` encoding a piecewise-constant staircase function. Transition points are Z values; height is constant from `z_i` to `z_{i+1}`. This is not self-describing: callers must know the encoding convention. Profile Z values are in object-space (uncompensated); compensated print-space Z values are computed at generation time.

**`equal_layering()` exclusions (BBS):**  
Multiple fields are deliberately excluded from the layering equality check, including `max_suport_layer_height`, `soluble_interface`, `gap_raft_object`, `gap_object_support`, `gap_support_object`. Changes to these values will not trigger a layer profile rebuild via this shortcut path, potentially leaving stale layer data.

**`smooth_height_profile()` fixed-pass design:**  
Always runs exactly 6 Gaussian blur passes. The adaptive termination loop (`has_steep_height_change`) is permanently commented out with a BBS annotation. Both over-smoothed and under-smoothed profiles receive identical treatment.

**Dual function definition pattern:**  
`min_layer_height_from_nozzle` and `max_layer_height_from_nozzle` each have TWO definitions in Slicing.cpp — one as a file-local `inline` function (operating on `PrintConfig`) and one as a `Slicing::` namespace member (operating on `DynamicPrintConfig`). This is correct but visually confusing due to identical names and similar logic.

**Shrinkage compensation application:**  
`shrinkage_compensation_z` is applied to profile Z coordinates during lookup in `generate_object_layers()` (the profile is scaled, not the output). The emitted `[lo, hi]` boundaries are in print-space (compensated). The inconsistency surfaces in `layer_height_profile_from_ranges()` which uses `object_print_z_height()` (compensated) to clip range hi but fills to `object_print_z_uncompensated_height()` at the tail end.

**`generate_layer_height_texture()` buffer safety:**  
The `memset` that would zero the output buffer is commented out. Texture cells not covered by any layer retain uninitialized memory. For contiguous layer coverage (the normal case) this is harmless, but any gap in coverage produces garbage pixels.

**`adjust_layer_series_to_align_object_height()` use of `abs()` vs `std::abs()`:**  
Line computing `gap = abs(layer_series.back() - object_height)` uses C-library `abs()` which operates on integers. On most compilers with implicit conversion this produces the correct `double` result via ADL, but it is formally undefined behavior and should be `std::abs()` or `fabs()`.

**`check_object_layers_fixed()` brittleness:**  
Returns false for any profile with more than 8 entries, even if all heights are identical. UI edits that leave extra transition points (without changing heights) defeat the fixed-profile fast path.

### Hazards Identified (this session)

No new H-numbered hazards were identified during final annotation — the key hazards for Slicing (the `abs()` issue, the hardcoded 6-pass smoother, the uninitialized texture buffer, the compensated/uncompensated inconsistency in `layer_height_profile_from_ranges()`) were already captured in the session discovery notes in the Goal section and are tracked there for continuity.

### Summary

`Slicing.hpp` and `Slicing.cpp` together implement the complete layer height pipeline:
- Parameter resolution: `create_from_config()` converts raw config into a fully resolved `SlicingParameters`
- Profile generation: `layer_height_profile_from_ranges()`, `layer_height_profile_adaptive()`
- Profile smoothing: `smooth_height_profile()` (6-pass Gaussian blur)
- Interactive editing: `adjust_layer_height_profile()` (cosine-weighted brush with INCREASE/DECREASE/REDUCE/SMOOTH modes)
- Object layer generation: `generate_object_layers()` + `adjust_layer_series_to_align_object_height()`
- Fixed-height detection: `check_object_layers_fixed()`
- UI visualization: `generate_layer_height_texture()` (2D RGBA texture with optional LOD)

The module is stateless (all functions are pure or operate on passed-in mutable references). Thread safety depends entirely on callers not sharing output vectors concurrently.

**Commit:** `annotate: Session 53 — Slicing.hpp + Slicing.cpp`

**Next hazard number to assign: H811**
