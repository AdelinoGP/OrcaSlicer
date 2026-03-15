# 01 — System Architecture

## Overview

OrcaSlicer is a C++17 FDM (and SLA) 3D printer slicing application. It is a fork of **Bambu Studio**, which is itself a fork of **PrusaSlicer** (Prusa Research), which descends from the original **Slic3r** (Alessandro Ranellucci). The codebase therefore carries accumulated design decisions from approximately four generations of development.

**License:** GNU AGPL v3

---

## High-Level Module Map

```
src/
├── OrcaSlicer.cpp / .hpp     [ENTRY_POINT]   Main entry; GUI + CLI dispatch
├── libslic3r/                [CORE_LIB]      Core slicing library (no GUI deps)
│   ├── Format/               [PARSERS]       File format I/O (STL, OBJ, 3MF, AMF, STEP, SL1, SVG)
│   ├── Fill/                 [INFILL]        13+ infill pattern implementations
│   ├── GCode/                [GCODE]         G-code generation, tool ordering, post-processing
│   ├── Support/              [SUPPORT]       FDM support structures (traditional + tree)
│   ├── Arachne/              [PERIMETERS]    Variable-width perimeter generator
│   ├── SLA/                  [SLA]           Resin printer support (mostly separate pipeline)
│   ├── Algorithm/            [ALGO]          Generic geometry algorithms
│   ├── Geometry/             [GEOMETRY]      Primitives (circle, convex hull, voronoi, etc.)
│   ├── Optimize/             [OPTIMIZE]      NLopt-based optimization wrappers
│   ├── Shape/                [SHAPES]        Shape utilities
│   ├── CSGMesh/              [CSG]           CSG (constructive solid geometry) mesh operations
│   └── Feature/              [FEATURES]      Feature detection utilities
├── slic3r/
│   ├── GUI/                  [GUI]           wxWidgets + OpenGL UI layer
│   ├── Utils/                [UTILS_GUI]     GUI utilities (networking, update, etc.)
│   └── Config/               [CONFIG_GUI]    Configuration UI components
├── libvgcode/                [VGCODE_VIZ]    G-code path visualization library
└── dev-utils/                [PLATFORM]      Platform-specific glue (Unix FHS, Windows, etc.)
```

---

## Namespace Structure

The codebase primarily uses a single top-level namespace:

- **`Slic3r`** — all core library code (`libslic3r/`)
- `Slic3r::GUI` — GUI layer
- `Slic3r::FillAdaptive` — adaptive cubic infill (octree-based)
- `Slic3r::FillLightning` — lightning infill
- `Slic3r::sla` — SLA print pipeline
- `Slic3r::AABBMesh` — AABB-accelerated mesh queries
- `Slic3r::GCode` — G-code namespace (partial; some G-code classes are in `Slic3r::`)

---

## Class Dependency Graph (Adjacency List)

### Core Data Model

```
Model
  └── ModelObject (1..*) ──────────── owns
        ├── ModelVolume (1..*) ──────── owns
        │     └── TriangleMesh ─────── owns (via indexed_triangle_set)
        ├── ModelInstance (1..*) ────── owns
        └── ModelConfigObject ───────── owns

Print ──────────────────────────────── aggregates Model (borrowed ref)
  └── PrintObject (1..*) ─────────────── owns
        ├── Layer (1..*) ──────────────── owns (via LayerPtrs)
        │     └── LayerRegion (1..*) ──── owns
        │           └── ExtrusionEntityCollection
        └── SupportLayer (0..*) ─────────── owns
```

### Slicing Chain

```
PrintObject::slice()
  └── TriangleMeshSlicer (free functions: slice_mesh / slice_mesh_ex)
        ├── indexed_triangle_set (input)
        └── std::vector<ExPolygons> (output, one per Z layer)

PrintObject::make_perimeters()
  └── PerimeterGenerator (traditional) | Arachne::WallToolPaths (variable-width)
        └── ExtrusionEntityCollection → stored in LayerRegion

PrintObject::infill()
  └── Fill::FillBase (abstract) ← Fill factory (Fill.cpp)
        ├── FillRectilinear
        ├── FillGyroid
        ├── Fill3DHoneycomb
        ├── FillAdaptive (octree-guided)
        ├── FillLightning
        ├── FillHoneycomb / FillCrossHatch / FillConcentric
        ├── FillLine / FillPlanePath
        └── FillTpmsD / FillTpmsFK (TPMS surfaces)

PrintObject::generate_support_material()
  └── SupportMaterial (traditional) | TreeSupport (tree-like)
```

### G-Code Generation Chain

```
Print::export_gcode()
  └── GCode (main orchestrator)
        ├── GCodeWriter (low-level G-code text emission)
        ├── GCode::ToolOrdering (multi-material tool change sequencing)
        ├── GCode::WipeTower / WipeTower2 (purge tower)
        ├── GCode::CoolingBuffer (fan/speed post-processing)
        ├── GCode::SpiralVase (vase mode path transformation)
        ├── GCode::AvoidCrossingPerimeters (travel path optimization)
        ├── GCode::SeamPlacer (seam position optimization)
        ├── GCode::RetractWhenCrossingPerimeters
        ├── GCode::PressureEqualizer (PA smoothing)
        └── GCode::PostProcessor (custom scripts)
```

---

## Dominant C++ Paradigms

### 1. Inheritance Hierarchies
- `PrintBase` → `Print` (FDM) / `SLAPrint` (resin)
- `ExtrusionEntity` → `ExtrusionPath` / `ExtrusionLoop` / `ExtrusionEntityCollection` (composite pattern)
- `Fill::FillBase` → 13+ concrete infill implementations (strategy pattern)
- `ObjectBase` → `ModelObject`, `ModelVolume`, `ModelMaterial`, `ModelInstance` (for undo/redo ID tracking)

### 2. Template Metaprogramming
- Heavy use of Eigen template library (`Vec3f` = `Eigen::Matrix<float,3,1>`, `Transform3d` = `Eigen::Transform<double,3,Affine>`)
- `AABBTreeIndirect.hpp`: template-based AABB tree with policy types
- `KDTreeIndirect.hpp`: KD-tree as class template
- `enum_bitmask.hpp`: CRTP-based type-safe bitmask for enums
- `MTUtils.hpp`: TBB task parallelism wrappers using templates
- `MutablePriorityQueue.hpp`: intrusive priority queue using templates

### 3. Smart Pointers vs Raw Pointers
- `std::unique_ptr` used for owned resources with custom deleters (e.g., `FillAdaptive::OctreePtr`, `FillLightning::GeneratorPtr`)
- `std::shared_ptr` used for shared model data (e.g., model configs in undo/redo)
- **Raw pointers still common** for non-owning references, especially in the older PrusaSlicer-lineage code
- `LayerPtrs` = `std::vector<Layer*>` (raw) in `PrintObject` — **potential hazard**
- Some vectors of raw pointers in `Model` hierarchy

### 4. RAII Patterns
- `Slic3r::Zipper` RAII wrapper around zip archive
- `Slic3r::Timer` RAII performance timer
- `Slic3r::TryCatchSignal` RAII signal handler (for crash recovery)
- Mutex locks via `std::unique_lock` / RAII guard consistently used

### 5. Operator Overloading for Domain Logic
- `Point` / `Vec2crd` arithmetic operators map to scaled-integer 2D operations
- `BoundingBox` operators for union/contains
- `Polygon` operators for translation/scaling

### 6. Friend Classes (Undo/Redo)
- `ModelConfigObject` and related classes use `friend class cereal::access` and `friend class UndoRedo::StackImpl` for serialization bypass

---

## External Library Inventory

| Library | Version | Purpose | Used By |
|---|---|---|---|
| **Boost** | 1.83+ | filesystem, logging, threads, regex, locale, asio, process | Throughout |
| **Intel TBB** | — | Parallel loops (`tbb::parallel_for`), task arenas | Slicing, infill, supports |
| **Eigen** | 3.3+ | Linear algebra, 3D transforms, matrices | All 3D geometry |
| **Clipper (v1)** | — | 2D polygon boolean ops, offsetting | Slicing output, perimeters, infill |
| **Clipper2** | — | Updated polygon lib (being introduced alongside v1) | ClipperUtils |
| **admesh** | — | STL loading, mesh repair (manifold fixing, winding) | TriangleMesh, Format/STL |
| **OpenVDB** | 5.0+ | Voxel grids (supports, boolean ops) | MeshBoolean, support gen |
| **libigl** | — | Geometry processing (triangle mesh algorithms) | Mesh repair, boolean |
| **NLopt** | 1.4+ | Nonlinear optimization | Orient, SLA |
| **OpenCASCADE (OCCT)** | — | STEP file format I/O | Format/STEP |
| **wxWidgets** | — | Cross-platform GUI framework | slic3r/GUI/ |
| **OpenGL/GLEW/GLFW** | — | 3D rendering and visualization | slic3r/GUI/OpenGL* |
| **Cereal** | — | Binary serialization (undo/redo, caching) | Model, Print state |
| **libcurl** | — | Network (printer connectivity, updates) | slic3r/Utils/ |
| **OpenSSL** | — | TLS for network connections | slic3r/Utils/ |
| **Freetype** | — | Font rendering (text embossing on models) | Emboss.cpp |
| **libpng** | — | PNG image I/O (thumbnails, textures) | PNGReadWrite |
| **zlib** | — | ZIP/gzip compression | Zipper, 3MF I/O |
| **expat** | — | XML parsing (3MF, AMF) | Format/3mf, Format/AMF |
| **nlohmann/json** | — | JSON parsing (Linux CLI pipe protocol) | OrcaSlicer.cpp |
| **nanosvg** | — | SVG parsing (SVG import) | NSVGUtils |
| **miniz** | — | Zip/deflate (alternative to zlib for 3MF) | miniz_extension |

---

## Build System Summary

**Build System:** CMake 3.13+, C++17

### Compilation Units (top-level `add_subdirectory`)
1. `deps_src/` — vendored third-party libraries (admesh, expat, etc.)
2. `src/` — application code (libslic3r, slic3r GUI, OrcaSlicer executable)
3. `sandboxes/` — optional development sandboxes (`SLIC3R_BUILD_SANDBOXES`)
4. `tests/` — Catch2 unit tests (`BUILD_TESTS`)

### Key CMake Options
| Option | Default | Effect |
|---|---|---|
| `SLIC3R_STATIC` | 1 | Link Boost/TBB/GLEW statically |
| `SLIC3R_GUI` | 1 | Include wxWidgets GUI (defines `SLIC3R_GUI`) |
| `SLIC3R_FHS` | 0 | FHS install layout (Linux packages) |
| `SLIC3R_ASAN` | 0 | AddressSanitizer |
| `SLIC3R_PROFILE` | 0 | Shiny profiler injection |
| `BUILD_TESTS` | OFF | Catch2 unit tests |

### Conditional Compilation
| Macro | Source | Effect |
|---|---|---|
| `SLIC3R_GUI` | CMake option | Includes entire wxWidgets + OpenGL GUI layer |
| `BBL_RELEASE_TO_PUBLIC` | CMake / build type | Release vs. development behavior |
| `HAS_WIN10SDK` | Windows SDK detection | Netfabb STL repair service integration |
| `HAVE_SPNAV` | spnav.h detection | 3D SpaceNavigator mouse support |
| `SLIC3R_PROFILE` | CMake option | Shiny profiler `PROFILE_FUNC()` macros |
| `WIN32`, `__APPLE__`, `__linux__` | Compiler | Platform-specific code paths |
| `NDEBUG` / `DEBUG` | Build type | Assertion behavior |

### Library Resolution
- Dependencies expected at `${DEP_BUILD_DIR}/OrcaSlicer_dep/usr/local`
- On Windows: additional DLLs required at runtime (GMP, MPFR, OCCT, freetype, WebView2)
- Cross-compilation supported for macOS ARM vs Intel (detected via `CMAKE_OSX_ARCHITECTURES`)

---

## Data Flow Summary (End-to-End)

```
[Input Files]
    .3mf / .stl / .obj / .amf / .step / .svg
          │
          ▼
[Model Loading] — Format/bbs_3mf.cpp, Format/STL.cpp, Format/OBJ.cpp, etc.
    → Model { ModelObject[] { ModelVolume[] { TriangleMesh (indexed_triangle_set) } } }
          │
          ▼
[Mesh Repair] — admesh (via TriangleMesh::repair())
    → degenerate faces removed, edges fixed, winding corrected
          │
          ▼
[Print Setup] — Print.cpp, PrintConfig.hpp
    → PrintObject per ModelObject-instance, layers computed
          │
          ▼
[Mesh Segmentation] — clips triangles at sub-facet paint stroke boundaries
          │
          ▼
[Slicing] — TriangleMeshSlicer (slice_mesh_ex)
    → per-layer ExPolygons (2D polygons with holes)
          │
          ▼
[Perimeter Generation] — PerimeterGenerator.cpp | Arachne/WallToolPaths
    → ExtrusionLoops (outer/inner walls) per LayerRegion
          │
          ▼
[Infill] — Fill/Fill.cpp factory → FillBase subclass
    → ExtrusionPaths (fill lines) per LayerRegion
          │
          ▼
[Support Generation] — Support/SupportMaterial.cpp | Support/TreeSupport.cpp
    → SupportLayer[] with ExtrusionEntityCollections
          │
          ▼
[Support Post-Processing] — support surface ironing
          │
          ▼
[Layer Finalization] — wipe tower, skirt/brim (cross-layer, sequential)
          │
          ▼
[G-Code Export] — GCode.cpp orchestrates → GCodeWriter.cpp emits text
    → .gcode / .bgcode file
```
