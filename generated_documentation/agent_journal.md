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
