# OrcaSlicer External Dependencies

> **Porting strategy legend**
> - `REPLICATE` — must be re-implemented from scratch; no equivalent library exists or the library is deeply embedded
> - `FIND_EQUIVALENT` — functionally replaceable; drop-in or near-drop-in alternative likely available
> - `PORT_REQUIRED` — algorithmic logic is embedded in the dependency; extracted pseudocode is needed before porting
> - `GUI_ONLY` — used exclusively in the GUI/rendering layer; irrelevant for headless slicing ports

---

## Table of Contents

1. [Clipper (1.x) — Polygon Boolean Operations](#1-clipper-1x)
2. [Clipper2 — Polygon Boolean Operations (v2)](#2-clipper2)
3. [TBB — Intel Threading Building Blocks](#3-tbb)
4. [Eigen — Linear Algebra](#4-eigen)
5. [admesh — STL Mesh Repair](#5-admesh)
6. [OpenVDB — Volumetric SDF Operations](#6-openvdb)
7. [Boost — Utilities, Voronoi, Spirit X3](#7-boost)
8. [cereal — Serialization](#8-cereal)
9. [CGAL — Computational Geometry](#9-cgal)
10. [NLopt — Numerical Optimization](#10-nlopt)
11. [libnest2d — 2D Bin-Packing / NFP](#11-libnest2d)
12. [libigl — Geometry Processing](#12-libigl)
13. [OpenCV — Color Clustering](#13-opencv)
14. [Qhull — Convex Hull](#14-qhull)
15. [Draco — 3D Mesh Compression](#15-draco)
16. [OCCT (OpenCASCADE) — CAD/STEP Import](#16-occt)
17. [wxWidgets — GUI Framework](#17-wxwidgets)
18. [GLEW / OpenGL — Rendering](#18-glew--opengl)
19. [libpng / JPEG / ZLIB — Image I/O](#19-libpng--jpeg--zlib)
20. [Blosc — Data Compression (OpenVDB)](#20-blosc)
21. [NanoSVG — SVG Parsing](#21-nanosvg)
22. [OpenCSG — CSG Rendering Preview](#22-opencsg)
23. [CURL — HTTP Networking](#23-curl)
24. [OpenSSL — TLS / Cryptography](#24-openssl)

---

## 1. Clipper (1.x)

**Source location:** `src/libslic3r/clipper.cpp`, `src/libslic3r/clipper.hpp` (vendored)  
**Wrapper:** `src/libslic3r/ClipperUtils.hpp/.cpp`, `src/libslic3r/ClipperZUtils.hpp`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Function / Type | Usage site | Purpose |
|----------------|-----------|---------|
| `ClipperLib::Clipper` | ClipperUtils.cpp | All polygon boolean ops (union, diff, intersection, XOR) |
| `ClipperLib::ClipperOffset` | ClipperUtils.cpp | Polygon inflation / deflation (offsets) |
| `ClipperLib::Paths`, `ClipperLib::Path` | Throughout libslic3r | Integer-coordinate polygon storage |
| `ClipperLib::PolyFillType` | ClipperUtils.cpp | Winding rule (EvenOdd, NonZero, Positive, Negative) |
| `ClipperLib_Z::Clipper` | ClipperZUtils.hpp, Algorithm/LineSplit.cpp | Z-metadata tagging for intersection provenance |
| `ClipperLib::PolyTree` | ClipperUtils.cpp | Hierarchical hole/contour tree for ExPolygon extraction |

### Algorithmic Role

Clipper v1 is the primary polygon boolean engine for all 2D slice operations:
- Layer intersection, union, difference for `ExPolygon` collections
- Perimeter offset (inset/outset for wall thicknesses)
- Infill clip to expolygon boundary
- Bridge area computation
- Seam placement polygon shrinkage

The `ClipperZUtils` variant carries per-vertex Z metadata through intersections, used by `Algorithm/LineSplit.cpp` to track whether intersection points originated from the subject path or the clip polygon (see Hazard H658 and [RESOLVED Q4]).

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H016 | P1 | Integer coordinate overflow for polys spanning >46 m in scaled units |
| H658 | P2 | CLIP_IDX sentinel collides with max(cInt) source path index |

### Porting Notes

- Clipper uses `int64_t` coordinates scaled by `SCALING_FACTOR` (1e6). All input floats must be scaled before passing in.
- The `PolyFillType` winding rule must be preserved exactly — several callers switch between `pftEvenOdd` and `pftPositive` for different operations.
- `ClipperOffset` arc tolerance values affect corner rounding quality; they are tuned constants.

---

## 2. Clipper2

**Source location:** `deps/` (external CMake project)  
**Wrapper:** `src/libslic3r/Clipper2Utils.hpp/.cpp`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Function / Type | Usage site | Purpose |
|----------------|-----------|---------|
| `Clipper2Lib::Clipper64` | Clipper2Utils.cpp | Boolean ops on `Paths64` |
| `Clipper2Lib::ClipperOffset` | Clipper2Utils.cpp | Polygon offset (v2 API) |
| `Clipper2Lib::Paths64`, `Path64` | Clipper2Utils.cpp | Integer path type |
| `Clipper2Lib::BooleanOp` | Clipper2Utils.cpp | Union/diff/intersection via functional API |

### Algorithmic Role

Clipper2 is used alongside Clipper v1 for specific operations where v2's improved numerical robustness is needed (notably in the Arachne perimeter generator and some infill operations). Both versions coexist; they use the same integer coordinate convention.

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H1142–H1154 | various | Session 83 Clipper2Utils hazards: coordinate conversion, overflow at large scales |

### Porting Notes

- Clipper2 and Clipper1 APIs are not interchangeable despite similar naming.
- `Clipper2Lib::InflatePaths` is the v2 replacement for `ClipperOffset` and uses a different arc-tolerance model.

---

## 3. TBB

**Library:** Intel Threading Building Blocks (oneAPI TBB)  
**Source location:** `deps/TBB`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Type / Function | Usage sites | Purpose |
|----------------|------------|---------|
| `tbb::parallel_for` | Print.cpp, PrintObject.cpp, TriangleMeshSlicer.cpp, Fill/, Support/ | Loop-level parallelism |
| `tbb::blocked_range<T>` | Throughout | Grain-size partitioning for parallel_for |
| `tbb::task_group` | PrintBase.cpp | Dynamic task spawning for background print steps |
| `tbb::task_arena` | Print.cpp | Scoped thread count limits |
| `tbb::concurrent_vector` | Support/TreeSupport.cpp | Lock-free concurrent appends |
| `tbb::mutex`, `tbb::spin_mutex` | Various | Mutual exclusion in parallel sections |
| `tbb::scalable_allocator` | Internal (Eigen, Clipper) | Scalable memory allocator |
| `tbb::enumerable_thread_specific` | GCodeProcessor.cpp | Per-thread storage |
| `tbb::cache_aligned_allocator` | Various | False-sharing prevention |

### Algorithmic Role

TBB is the parallelism runtime for the entire slicing engine. Key parallel regions:
- Mesh slicing across Z-levels (`TriangleMeshSlicer.cpp`)
- Per-object print step execution (`Print.cpp:2260`)
- Support contact detection across layers (`SupportMaterial.cpp`)
- Lightning infill tree generation per layer
- GCode post-processing passes

The cancellation protocol uses `throw_on_cancel_callback` — a callable that throws `Slic3r::SlicingCancelledException`, propagated through TBB task infrastructure (see Hazard H017).

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H017 | P1 | Exception-as-cancellation: TBB propagates through tasks but outer `parallel_for` swallows all exceptions except the first |
| H040 | P1 | `SupportNode::diameter_angle_scale_factor` is static — multi-object parallel race |
| H041 | P1 | `TreeSupportProfiler` file-scope global — not thread-safe under parallel use |
| H090 | P1 | `g_last_timestamp` global unsynchronized — written from parallel worker threads |

### Porting Notes

- All `tbb::parallel_for` loops must be replaced with equivalent thread-pool constructs in the target language.
- The cancellation model requires care: exceptions thrown inside TBB tasks are caught by the outer `task_group`/`parallel_for`; the first exception is re-thrown after all tasks complete. A port must replicate this "finish all, then re-throw" semantics.
- `tbb::task_arena` is used to isolate the slicing thread pool from the UI thread pool — important for responsive UIs.

---

## 4. Eigen

**Library:** Eigen 3 (header-only)  
**Source location:** vendored in deps  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Type / Function | Usage | Purpose |
|----------------|-------|---------|
| `Eigen::Matrix<float,3,1>` (`Vec3f`) | TriangleMesh, normals | 3D float vector |
| `Eigen::Matrix<double,3,1>` (`Vec3d`) | Transforms, bounds | 3D double vector |
| `Eigen::Matrix<coord_t,2,1>` (`Point`) | 2D polygon coords | 2D integer point |
| `Eigen::Affine3d`, `Transform3d` | Model.cpp, Print transforms | Affine transforms |
| `Eigen::AlignedBox3f` | AABB tree, BoundingBox | Bounding box |
| `Eigen::SelfAdjointEigenSolver` | Circle.cpp, PrincipalComponents2D | Eigendecomposition |
| `Eigen::AngleAxisd` | PrintObject.cpp | Rotation |
| `Eigen::Map<>` | Various | Zero-copy reinterpretation |

### Algorithmic Role

Eigen is used for all linear-algebra operations: coordinate transforms, bounding boxes, normal computation, principal-component analysis, and eigendecomposition for circle fitting. It is a ubiquitous dependency throughout `libslic3r`.

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H156 | P2 | `SelfAdjointEigenSolver::info()` not checked — degenerate input silently accepted |
| H182 | P2 | `compute_principal_components()` returns unnormalised eigenvectors for degenerate input |
| H171 | P2 | `compute_normal()`: zero-area triangles produce NaN via `normalize(zero_vector)` |

### Porting Notes

- Eigen's `EIGEN_VECTORIZE` macros enable SIMD; the resulting code is not portable to non-x86 without recompilation.
- `Eigen::Map<>` is used to reinterpret raw float arrays as vectors — a port must handle pointer aliasing carefully.

---

## 5. admesh

**Library:** admesh (vendored fork)  
**Source location:** `src/libslic3r/admesh/`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Function | Usage site | Purpose |
|----------|-----------|---------|
| `stl_repair()` | TriangleMesh.cpp:100 | Full mesh repair pipeline |
| `stl_check_faces_exact()` | TriangleMesh.cpp | Verify face connectivity |
| `stl_fix_normal_directions()` | TriangleMesh.cpp | Outward-normal normalization |
| `stl_fill_holes()` | TriangleMesh.cpp | Topological hole repair |
| `stl_remove_degenerate_facets()` | TriangleMesh.cpp | Remove zero-area triangles |
| `its_face_neighbors_par()` | TriangleMeshSlicer.cpp:2437 | Face-neighbor table (rebuilt each call, not from admesh cache) |
| `stl_stats` | TriangleMesh.hpp | Mesh statistics (bounding box, number of facets, etc.) |

### Algorithmic Role

admesh is used as the mesh repair/validation layer in the import pipeline. When a mesh is loaded from STL/OBJ/3MF, it may have non-manifold edges, missing faces, or inverted normals. `stl_repair()` runs a sequence of fixup passes. The admesh adjacency data (`its_neighbors_par`) is distinct from the face-neighbor table used in slicing — the slicer rebuilds it independently (see [RESOLVED Q1]).

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H220 | P2 | admesh repair may break face connectivity; slicer refreshes it post-repair (FIXME comment at TriangleMesh.cpp:223) |

### Porting Notes

- admesh's repair is a best-effort heuristic; it does not guarantee manifold output.
- The `stl_file` struct (admesh's internal format) is separate from OrcaSlicer's `indexed_triangle_set`; conversion happens at import time.

---

## 6. OpenVDB

**Library:** OpenVDB  
**Source location:** `deps/OpenVDB`  
**Wrapper:** `src/libslic3r/OpenVDBUtils.hpp/.cpp`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Function | Usage site | Purpose |
|----------|-----------|---------|
| `openvdb::tools::meshToVolume()` | OpenVDBUtils.cpp | Convert triangle mesh → signed distance field (SDF) FloatGrid |
| `openvdb::tools::volumeToMesh()` | OpenVDBUtils.cpp | Marching cubes: SDF FloatGrid → triangle mesh |
| `openvdb::tools::levelSetRebuild()` | OpenVDBUtils.cpp | Repair/resample an SDF grid |
| `openvdb::tools::csgUnion()` | MeshBoolean.cpp | SDF-based CSG union |
| `openvdb::tools::csgDifference()` | MeshBoolean.cpp | SDF-based CSG difference |
| `openvdb::initialize()` | OpenVDBUtils.cpp | One-time library initialization (serialized in practice) |

### Algorithmic Role

OpenVDB is used for two distinct purposes:
1. **SDF conversion:** Mesh repair and remeshing via `meshToVolume` + `volumeToMesh`. Used when a mesh needs to be converted to a watertight representation.
2. **CSG operations:** Boolean union/difference on complex meshes via SDF, used in `MeshBoolean.cpp` as an alternative to CGAL for operations where CGAL fails on degenerate inputs.

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H026 | P2 | `openvdb::initialize()` is called at the top of each conversion function — concurrent callers serialize unnecessarily; should be called once at startup |
| H999–H1021 | various | Session 71 OpenVDB/MeshBoolean hazards |

### Porting Notes

- OpenVDB grids are large in-memory structures; mesh-to-SDF conversion is expensive.
- The voxel size parameter in `meshToVolume` directly controls output mesh quality and memory use.
- `openvdb::tools::csgUnion` operates in-place, modifying the first grid argument.
- Blosc (dep #20) is required as an OpenVDB compression backend.

---

## 7. Boost

**Library:** Boost (multi-module)  
**Source location:** `deps/Boost`  
**Porting strategy:** `FIND_EQUIVALENT` (most modules) / `PORT_REQUIRED` (Voronoi, Spirit X3)

### API Surface Used

| Module | Usage sites | Purpose |
|--------|------------|---------|
| `boost::polygon::voronoi_builder` | Geometry/Voronoi*.cpp | Voronoi diagram construction (used by Arachne) |
| `boost::polygon::voronoi_diagram` | Geometry/Voronoi*.cpp | Voronoi graph traversal |
| `boost::spirit::x3` | PlaceholderParser.cpp | Expression parser for G-code placeholder values |
| `boost::filesystem` | Various | Path manipulation (largely replaceable with `std::filesystem`) |
| `boost::nowide` | Various | UTF-8 file I/O on Windows |
| `boost::log` | Various | Logging (replaceable) |
| `boost::multiprecision::int128_t` | MinAreaBoundingBox.cpp | 128-bit integer arithmetic for convex-hull area |
| `boost::thread` | Various | Thread utilities (largely supplanted by `std::thread`) |
| `boost::property_tree` | Config loading | INI/XML/JSON property tree (partially) |
| `boost::algorithm` | Various | String utilities |

### Algorithmic Role

- **Voronoi:** `boost::polygon::voronoi_builder` is the Voronoi diagram backend for `SkeletalTrapezoidation`, which is the core of the Arachne variable-width perimeter algorithm. The output is a half-edge graph traversed to compute medial axes.
- **Spirit X3:** Used in `PlaceholderParser.cpp` to parse and evaluate G-code placeholder expressions (e.g., `{layer_num * 0.2}`).

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H168 | P1 | `MinAreaBoundingBox` uses `boost::multiprecision::int128_t`; naive int64 overflows |
| H174 | P2 | Voronoi diagram construction requires integer inputs; float→int conversion must preserve precision |

### Porting Notes

- `boost::polygon` Voronoi is the only known C++ implementation that handles the specific input format (point + segment soup) needed for Arachne. Replacing it requires a compatible Voronoi library or a full reimplementation.
- `boost::spirit::x3` produces a parse tree at compile time; the parser grammar is non-trivial to replicate. See `PlaceholderParser.cpp` for the grammar definition.
- `boost::nowide` can be replaced with standard UTF-8 handling on non-Windows platforms.

---

## 8. cereal

**Library:** cereal (header-only serialization)  
**Source location:** `deps/Cereal`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Type / Macro | Usage sites | Purpose |
|-------------|------------|---------|
| `CEREAL_REGISTER_TYPE` | PrintConfig.cpp, Model.hpp | Polymorphic type registration |
| `cereal::JSONInputArchive` / `JSONOutputArchive` | AppConfig.cpp, PresetBundle.cpp | JSON serialization |
| `cereal::BinaryInputArchive` / `BinaryOutputArchive` | UndoRedo stack | Fast binary snapshot |
| `cereal::access` + `serialize()` template | Model.hpp, PrintConfig.hpp | Member serialization |
| Version numbers (cereal `version` tag) | PrintConfig, Model | Schema versioning |

### Algorithmic Role

cereal drives OrcaSlicer's undo/redo system and preset persistence. The `UndoRedo::StackImpl` stores binary cereal snapshots of `Model` + `PrintConfig` state. At each undo/redo step, the snapshot is deserialized and applied.

Version numbers in cereal archives enable forward compatibility: a new slicer can read old presets by handling older version values. Hardcoded version constants (e.g., `CustomGCode::Info` version 1, Hazard H161) must be manually incremented on schema changes.

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H161 | P3 | `CustomGCode::Info` cereal version 1 hardcoded — must be manually incremented |
| H010 | P2 | cereal serialization coupling: any struct layout change breaks binary snapshots |

### Porting Notes

- cereal is header-only; porting requires reimplementing the serialization protocol for each serialized type.
- The polymorphic type registry (`CEREAL_REGISTER_TYPE`) is essential — without it, polymorphic deserialization silently fails.
- The binary archive format is not self-describing; version mismatch produces undefined behavior, not a clean error.

---

## 9. CGAL

**Library:** Computational Geometry Algorithms Library  
**Source location:** `deps/CGAL`  
**Usage:** `src/libslic3r/Geometry/VoronoiUtilsCgal.cpp`, `src/libslic3r/MeshBoolean.cpp`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Type / Function | Usage site | Purpose |
|----------------|-----------|---------|
| `CGAL::Simple_cartesian<double>` | VoronoiUtilsCgal.cpp | Exact/filtered kernel for Voronoi validation |
| `CGAL::Simple_cartesian<CGAL::MP_Float>` | VoronoiUtilsCgal.cpp | Multiprecision exact kernel |
| `CGAL::Delaunay_triangulation_2` | VoronoiUtilsCgal.cpp | Delaunay triangulation for Voronoi verification |
| `CGAL::Nef_polyhedron_3` | MeshBoolean.cpp | Exact CSG on 3D polyhedra |
| `CGAL::Polygon_mesh_processing::*` | MeshBoolean.cpp | Mesh Boolean operations |

### Algorithmic Role

CGAL serves two roles:
1. **Voronoi validation:** `VoronoiUtilsCgal.cpp` uses CGAL's exact arithmetic kernels to verify and correct degenerate Voronoi edges produced by `boost::polygon` for the Arachne generator.
2. **Exact CSG:** `MeshBoolean.cpp` uses `CGAL::Nef_polyhedron_3` for Boolean mesh operations when exact results are required (as opposed to the approximate SDF-based OpenVDB path).

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H512 | P1 | CGAL `Nef_polyhedron_3` operations can be very slow on complex meshes (near-polynomial complexity for certain configurations) |

### Porting Notes

- CGAL is a large dependency with a complex build. Most targets link only a small subset.
- Exact arithmetic kernels (`CGAL::MP_Float`) are significantly slower than floating-point; they are used only when precision is critical.
- `CGAL::Polygon_mesh_processing` requires `CGAL::Surface_mesh` format; conversion from `indexed_triangle_set` is required.

---

## 10. NLopt

**Library:** NLopt (nonlinear optimization)  
**Source location:** `deps/NLopt`  
**Usage:** `src/libslic3r/Optimize/NLoptOptimizer.hpp`, `src/libslic3r/SLA/SupportTree.cpp`, `Arrange.cpp`  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Function | Usage site | Purpose |
|----------|-----------|---------|
| `nlopt_create()` / `nlopt_optimize()` | NLoptOptimizer.hpp | Generic nonlinear optimization wrapper |
| `NLOPT_GN_ISRES` (genetic algorithm) | SLA/SupportTree.cpp | SLA support point placement |
| `NLOPT_LN_SBPLX` (Nelder-Mead variant) | SLA/SupportTree.cpp, Arrange.cpp | SLA support orientation, arrangement optimization |

### Algorithmic Role

NLopt is used for:
- **SLA support tree placement:** Optimizing the angle and position of support pillars to minimize material and avoid collisions.
- **Plate arrangement:** Optimizing object placement within the print bed (via libnest2d's NLopt backend).

### Hazards

No critical NLopt-specific hazards identified. The optimizer is invoked with user-configurable stop criteria (max iterations, objective tolerance).

### Porting Notes

- NLopt is a thin C API; the C++ wrapper in `NLoptOptimizer.hpp` is straightforward to replace with any gradient-free optimizer.
- The genetic algorithm `NLOPT_GN_ISRES` is non-deterministic; different runs may produce different support layouts.

---

## 11. libnest2d

**Library:** libnest2d (2D bin-packing, NFP-based)  
**Source location:** vendored in src or deps  
**Usage:** `src/libslic3r/Arrange.cpp`, `src/libslic3r/Support/TreeSupport.cpp`  
**Porting strategy:** `PORT_REQUIRED`

### API Surface Used

| Type / Function | Usage | Purpose |
|----------------|-------|---------|
| `libnest2d::nest()` | Arrange.cpp | Arrange objects on print bed using NFP |
| `libnest2d::NfpPlacer` | Arrange.cpp | No-fit polygon placer |
| `libnest2d::backends::Libslic3r` | Arrange.cpp, TreeSupport.cpp | Geometry backend using Slic3r polygons |
| NLopt optimizers | via libnest2d | Per-item placement optimization |

### Algorithmic Role

libnest2d implements the No-Fit Polygon (NFP) algorithm for 2D bin-packing, used to arrange print objects on the bed with minimal bounding box and collision avoidance. The `backends::Libslic3r` backend reuses OrcaSlicer's `ExPolygon` type directly, avoiding coordinate conversion.

### Porting Notes

- NFP computation is the expensive step; it is O(N·M) in polygon vertex count.
- The NLopt dependency (for placement optimization within each NFP) creates a transitive dependency chain.

---

## 12. libigl

**Library:** libigl (header-only geometry processing)  
**Source location:** vendored (conditionally compiled)  
**Usage:** `src/libslic3r/AABBTreeIndirect.hpp`, `src/libslic3r/SLA/SupportPointGenerator.cpp` (commented out)  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Type | Usage site | Purpose |
|------|-----------|---------|
| `igl::Hit` | AABBTreeIndirect.hpp | Ray-triangle intersection result type |
| `igl::AABB` | SupportPointGenerator.cpp (disabled `#include`) | AABB tree (unused; OrcaSlicer has its own) |

### Algorithmic Role

libigl usage is minimal. Only `igl::Hit` (a simple struct) is actively used, via `AABBTreeIndirect.hpp`. OrcaSlicer has its own AABB tree implementation (`AABBTreeIndirect.hpp`, `AABBTreeLines.hpp`); the libigl AABB tree was removed.

### Porting Notes

- `igl::Hit` can be trivially replaced with a local struct.
- The `DEP_BUILD_IGL_STATIC` CMake option is disabled by default and flagged as unsafe (see CMakeLists.txt comment about conflicting Eigen versions).

---

## 13. OpenCV

**Library:** OpenCV (opencv_world)  
**Source location:** `deps/OpenCV`  
**Usage:** `src/libslic3r/ObjColorUtils.hpp/.cpp`  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Type / Function | Usage | Purpose |
|----------------|-------|---------|
| `cv::Mat` | ObjColorUtils.hpp | Image matrix storage for color data |
| `cv::kmeans()` | ObjColorUtils.cpp | K-means color clustering |
| Color space conversion (`cv::cvtColor`) | ObjColorUtils.cpp | RGB ↔ Lab/HSV conversion before clustering |

### Algorithmic Role

OpenCV is used exclusively for **color clustering on textured OBJ imports**. When an OBJ file with a texture is loaded, `ObjColorUtils` uses k-means clustering to quantize the texture colors into a user-specified number of filament colors, enabling multi-material painting.

### Porting Notes

- This is a GUI/import feature; it can be stripped from a headless slicer port without affecting G-code output.
- `cv::kmeans` can be replaced with any k-means implementation; the algorithm is standard.
- The `opencv_world` monolithic library is large (~100 MB); only a fraction is used.

---

## 14. Qhull

**Library:** Qhull (convex hull / Delaunay triangulation)  
**Source location:** `deps/Qhull`  
**Usage:** `src/libslic3r/TriangleMesh.hpp/.cpp` (convex hull), `src/libslic3r/SLA/`  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Function | Usage site | Purpose |
|----------|-----------|---------|
| `qh_new_qhull()` / `qh_triangulate()` | TriangleMesh.cpp | 3D convex hull of point cloud |
| Convex hull facet iteration | TriangleMesh.cpp | Extract triangulated hull faces |

### Algorithmic Role

Qhull computes the 3D convex hull of mesh vertices, used in `its_convex_hull_3d()` and `TriangleMesh::convex_hull_3d()`. The convex hull is used for:
- Collision/overlap detection in arrangement
- SLA tilting/rotation bounding volume computation

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H401 | P2 | Qhull may produce non-manifold output for near-degenerate point sets (documented in TriangleMesh.hpp:262) |

### Porting Notes

- Qhull is a well-known library with many language bindings; replacement is straightforward.
- The non-manifold output hazard (H401) means callers must validate or repair the hull before use.

---

## 15. Draco

**Library:** Google Draco (3D mesh compression)  
**Source location:** `deps/Draco`  
**Usage:** `src/libslic3r/Format/DRC.cpp`  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Function | Usage site | Purpose |
|----------|-----------|---------|
| `draco::Encoder` + `draco::EncoderBuffer` | Format/DRC.cpp | Encode mesh to Draco binary |
| `draco::Decoder` + `draco::DecoderBuffer` | Format/DRC.cpp | Decode Draco binary to mesh |
| `draco::Mesh` | Format/DRC.cpp | Draco mesh representation |

### Algorithmic Role

Draco is used exclusively for `.drc` (Draco-compressed mesh) import/export. It is a standalone format handler with no coupling to the slicing pipeline.

### Porting Notes

- Draco format support can be made optional; removing it does not affect slicing.
- The `.drc` format is used in some 3MF variants for mesh compression.

---

## 16. OCCT

**Library:** OpenCASCADE Technology  
**Source location:** `deps/OCCT`  
**Usage:** `src/libslic3r/Format/STEP.cpp`, `src/libslic3r/Format/OBJ.cpp` (CAD import)  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Type / Function | Usage site | Purpose |
|----------------|-----------|---------|
| `BRep_Builder`, `TopoDS_Shape` | Format/STEP.cpp | STEP/IGES B-Rep representation |
| `BRepMesh_IncrementalMesh` | Format/STEP.cpp | Tessellate B-Rep to triangle mesh |
| `STEPControl_Reader` | Format/STEP.cpp | STEP file parser |
| `IGESControl_Reader` | Format/STEP.cpp | IGES file parser |

### Algorithmic Role

OCCT handles CAD file import (`.step`, `.stp`, `.iges`). The B-Rep geometry is tessellated to a triangle mesh via `BRepMesh_IncrementalMesh` and then handed to the standard slicing pipeline. OCCT is not involved in slicing or G-code generation.

### Porting Notes

- OCCT is a very large dependency (~500 MB). If STEP/IGES import is not required, it can be excluded.
- Tessellation quality is controlled by the `BRepMesh_IncrementalMesh` linear deflection and angular deflection parameters.
- FREETYPE is a transitive dependency of OCCT for text-in-CAD rendering.

---

## 17. wxWidgets

**Library:** wxWidgets (GUI framework)  
**Source location:** `deps/wxWidgets`  
**Usage:** `src/slic3r/GUI/` (all GUI code)  
**Porting strategy:** `GUI_ONLY`

### API Surface Used

All standard wxWidgets widgets, events, and utilities are used throughout `src/slic3r/GUI/`. Key usage includes:
- `wxApp`, `wxFrame`, `wxPanel` — application/window lifecycle
- `wxGLCanvas` — OpenGL rendering surface
- `wxThread`, `wxEvent` — background thread / UI thread communication
- `wxImage`, `wxBitmap` — image handling
- `wxFileDialog`, `wxDirDialog` — file pickers

### Algorithmic Role

wxWidgets is the cross-platform GUI toolkit. It is entirely isolated from `libslic3r`; the slicing engine has no wxWidgets dependency.

### Porting Notes

- A headless port of the slicer (CLI only) can be built without wxWidgets by configuring `SLIC3R_BUILD_SLIC3R_STATIC=ON` and excluding the `slic3r/GUI` directory.
- The `src/slic3r/GUI/` layer communicates with `libslic3r` through the `Print` and `Model` APIs only.

---

## 18. GLEW / OpenGL

**Library:** GLEW (OpenGL Extension Wrangler)  
**Source location:** `deps/GLEW`  
**Usage:** `src/slic3r/GUI/GLCanvas3D.cpp`, `src/slic3r/GUI/OpenGLManager.cpp`  
**Porting strategy:** `GUI_ONLY`

### API Surface Used

| Usage | Purpose |
|-------|---------|
| `GL/glew.h` | Load all OpenGL extensions at runtime |
| `glDrawArrays`, `glBufferData`, etc. | 3D preview rendering |
| Shader compilation (`glCreateShader`, `glShaderSource`) | GLSL shaders for 3D visualization |

### Algorithmic Role

GLEW + OpenGL are used exclusively for the 3D preview renderer (plate view, layer preview, gizmos). No slicing logic depends on OpenGL.

### Porting Notes

- On macOS, GLEW may be replaced with Metal or Vulkan depending on the GUI framework used.
- A headless port needs no OpenGL dependency.

---

## 19. libpng / JPEG / ZLIB

**Libraries:** libpng, libjpeg-turbo, zlib  
**Source locations:** `deps/PNG`, `deps/JPEG`, `deps/ZLIB`  
**Usage:** `src/libslic3r/PNGReadWrite.cpp`, thumbnail generation, SLA layer preview  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Library | Usage | Purpose |
|---------|-------|---------|
| libpng | PNGReadWrite.cpp | Encode/decode PNG thumbnails and SLA layer bitmaps |
| libjpeg | PNGReadWrite.cpp (JPEG path) | Encode JPEG thumbnails for 3MF/GCode headers |
| zlib | 3MF zip, cereal binary compression | Deflate/inflate for file formats |

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H1179 | P1 | `write_rgb_or_gray_to_file()`: libpng uses `setjmp`/`longjmp` for error handling — UB with C++ objects on stack |
| H1178 | P3 | `decode_png()` returns false silently for non-8-bit-GRAY PNGs |

### Porting Notes

- The `setjmp`/`longjmp` error-handling pattern in libpng is inherently unsafe in C++. A port should wrap libpng calls in a C shim or use a C++ PNG library.
- zlib is also used transitively by many other libraries (Boost, cereal, OCCT).

---

## 20. Blosc

**Library:** Blosc (block-oriented lossless compression)  
**Source location:** `deps/Blosc`  
**Usage:** Transitive dependency of OpenVDB  
**Porting strategy:** `FIND_EQUIVALENT`

### Algorithmic Role

Blosc provides block-level compression for OpenVDB grid data serialization. It is not called directly by OrcaSlicer code; it is linked because OpenVDB's serialization layer uses it.

### Porting Notes

- Blosc can be replaced by disabling OpenVDB's Blosc backend (`OPENVDB_BUILD_PYTHON_MODULE=OFF`, `USE_BLOSC=OFF`) at the cost of larger serialized grid files.

---

## 21. NanoSVG

**Library:** NanoSVG (header-only SVG parser/rasterizer)  
**Source location:** `deps/NanoSVG` / vendored in src  
**Usage:** `src/libslic3r/NSVGUtils.cpp`, GUI icon loading  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Function | Usage | Purpose |
|----------|-------|---------|
| `nsvgParse()` | NSVGUtils.cpp | Parse SVG XML into NanoSVG shape tree |
| `nsvgRasterize()` | NSVGUtils.cpp / GUI | Rasterize SVG shapes to pixel buffer |
| `nsvgDelete()` | NSVGUtils.cpp | Free parsed shape tree |

### Algorithmic Role

NanoSVG is used to:
1. Load SVG icons in the GUI (toolbar icons, etc.)
2. Convert SVG paths to `ExPolygon` outlines in `NSVGUtils.cpp` for features like custom supports painted from SVG files.

### Hazards

| ID | Severity | Description |
|----|----------|-------------|
| H1173 | P2 | `stroke_to_expolygons()`: ArcTolerance computed as `cbrt(tesselation_tolerance)` — variable named `mitter`; semantics undocumented |

---

## 22. OpenCSG

**Library:** OpenCSG (Constructive Solid Geometry rendering)  
**Source location:** `deps/OpenCSG`  
**Usage:** `src/slic3r/GUI/` (3D preview CSG rendering)  
**Porting strategy:** `GUI_ONLY`

### Algorithmic Role

OpenCSG enables the SLA / FDM CSG preview in the 3D viewport, rendering Boolean combinations of mesh primitives (modifiers, negative volumes) correctly via OpenGL stencil and depth buffer techniques. It has no involvement in the slicing pipeline.

---

## 23. CURL

**Library:** libcurl  
**Source location:** `deps/CURL`  
**Usage:** `src/slic3r/` network operations (printer communication, update checks, cloud slice)  
**Porting strategy:** `FIND_EQUIVALENT`

### API Surface Used

| Usage | Purpose |
|-------|---------|
| HTTP GET/POST | Bambu/Prusa Connect API communication |
| FTP upload | Direct printer file upload |
| SSL (via OpenSSL) | HTTPS for cloud services |

### Algorithmic Role

CURL is used for all network I/O: uploading G-code to network printers (Bambu LAN, PrusaLink), checking for slicer updates, and cloud slicing. It is entirely separate from the slicing engine.

---

## 24. OpenSSL

**Library:** OpenSSL  
**Source location:** `deps/OpenSSL`  
**Usage:** Transitive dependency of CURL; `src/slic3r/` for certificate verification  
**Porting strategy:** `FIND_EQUIVALENT`

### Algorithmic Role

OpenSSL provides TLS for all HTTPS connections. OrcaSlicer also uses `<openssl/md5.h>` for MD5 hashing in a few non-cryptographic contexts (file checksums).

### Porting Notes

- MD5 usage can be replaced with any checksum library.
- The OpenSSL version is pinned due to ABI sensitivity; see CMakeLists.txt comments.

---

## Summary Table

| # | Library | Porting Strategy | Slicing Critical | Notes |
|---|---------|-----------------|-----------------|-------|
| 1 | Clipper 1.x | PORT_REQUIRED | Yes | Core polygon boolean engine |
| 2 | Clipper2 | PORT_REQUIRED | Yes | Arachne + robustness-critical ops |
| 3 | TBB | PORT_REQUIRED | Yes | All parallelism; cancellation protocol |
| 4 | Eigen | FIND_EQUIVALENT | Yes | Linear algebra throughout |
| 5 | admesh | PORT_REQUIRED | Yes | Mesh repair pipeline |
| 6 | OpenVDB | PORT_REQUIRED | Yes | SDF conversion, CSG |
| 7 | Boost | FIND_EQUIVALENT / PORT_REQUIRED | Yes (Voronoi, Spirit) | Voronoi for Arachne; Spirit for PlaceholderParser |
| 8 | cereal | PORT_REQUIRED | Yes (undo/redo) | Binary + JSON serialization |
| 9 | CGAL | PORT_REQUIRED | Yes | Voronoi validation, exact CSG |
| 10 | NLopt | FIND_EQUIVALENT | SLA only | Nonlinear optimization |
| 11 | libnest2d | PORT_REQUIRED | Yes | NFP bed arrangement |
| 12 | libigl | FIND_EQUIVALENT | No | Only `igl::Hit` struct used |
| 13 | OpenCV | FIND_EQUIVALENT | No | Color clustering (OBJ import) |
| 14 | Qhull | FIND_EQUIVALENT | Yes | 3D convex hull |
| 15 | Draco | FIND_EQUIVALENT | No | `.drc` format only |
| 16 | OCCT | FIND_EQUIVALENT | No | STEP/IGES import only |
| 17 | wxWidgets | GUI_ONLY | No | GUI framework |
| 18 | GLEW/OpenGL | GUI_ONLY | No | 3D preview rendering |
| 19 | libpng/JPEG/zlib | FIND_EQUIVALENT | Partial (SLA thumbnails) | File I/O |
| 20 | Blosc | FIND_EQUIVALENT | No | OpenVDB transitive dep |
| 21 | NanoSVG | FIND_EQUIVALENT | No | SVG icons + path import |
| 22 | OpenCSG | GUI_ONLY | No | CSG preview rendering |
| 23 | CURL | FIND_EQUIVALENT | No | Network I/O |
| 24 | OpenSSL | FIND_EQUIVALENT | No | TLS / MD5 hashing |
