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
