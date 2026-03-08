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
