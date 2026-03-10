# 02 — Core Data Structures

## Coordinate System and Scaling

This is the single most important thing to understand before analyzing any data structure.

**OrcaSlicer uses a dual coordinate system:**

| Domain | Type | Unit | Where Used |
|---|---|---|---|
| 3D mesh vertices | `Vec3f` (Eigen float) | millimetres (real) | `indexed_triangle_set`, `TriangleMesh` |
| 3D transforms | `Vec3d` / `Transform3d` (Eigen double) | millimetres (real) | `ModelInstance`, mesh transforms |
| 2D sliced polygons | `coord_t` (int32 or int64) | **scaled integers** (1 unit = 1/1,000,000 mm) | All `Polygon`, `Point`, `ExPolygon` types |
| Layer Z height | `coordf_t` (double) | millimetres (real) | `Layer::print_z`, `Layer::height` |

The `SCALING_FACTOR = 1e-6` constant converts between real mm and scaled integers:
```cpp
// From libslic3r.h:
coord_t scaled = static_cast<coord_t>(real_mm / SCALING_FACTOR);  // mm → scaled
double real_mm = unscaled<double>(coord_t scaled);                 // scaled → mm
```

**Why:** Clipper library (2D polygon operations) operates exclusively on integers. Scaling by 1e6 gives micron-level precision while staying within int64 range.

---

## Primary Data Structures

### `indexed_triangle_set` (from admesh/stl.h)

```cpp
// Defined in admesh/stl.h (vendored)
struct indexed_triangle_set {
    std::vector<stl_vertex> vertices;   // Vec3f — XYZ in millimetres
    std::vector<Vec3i32>    indices;    // Triangle face: 3 indices into vertices[]
};
```

**Purpose:** The fundamental triangle mesh representation. Compact indexed format — each vertex appears once in `vertices[]`; `indices[i]` contains the 3 vertex indices for triangle `i`.

**Invariants:** After mesh repair, winding order of all triangles is consistent (outward normal follows right-hand rule). `open_edges == 0` for a valid watertight mesh.

**Lifecycle:**
- Created by format parsers (`Format/STL.cpp`, `Format/3mf.cpp`, etc.)
- Stored inside `TriangleMesh` (value semantics)
- Passed by `const &` to `slice_mesh()` and other geometry algorithms
- Can be transformed by applying a `Transform3f` to all vertices

**Consumers:** `TriangleMeshSlicer`, `AABBMesh`, `MeshBoolean`, support generators, `Orient`

---

### `TriangleMesh` (`src/libslic3r/TriangleMesh.hpp`)

```cpp
class TriangleMesh {
public:
    indexed_triangle_set its;           // [STATE] The actual mesh data
    // ... methods for repair, I/O, transforms
private:
    TriangleMeshStats m_stats;          // [STATE] Cached statistics (facet count, bbox, volume)
};

struct TriangleMeshStats {
    uint32_t      number_of_facets;
    stl_vertex    max, min, size;       // Bounding box in mm
    float         volume;               // -1 if not computed
    int           number_of_parts;      // Connected component count
    int           open_edges;           // 0 = watertight manifold
    RepairedMeshErrors repaired_errors; // Counts of fixes applied
};

struct RepairedMeshErrors {
    int edges_fixed;        // Vertices merged by ε proximity
    int degenerate_facets;  // Zero-area triangles removed
    int facets_removed;     // Total removed (includes degenerate)
    int facets_reversed;    // Winding order corrections
    int backwards_edges;    // Shared edges with mismatched orientation
};
```

**Purpose:** Value-type wrapper around `indexed_triangle_set` that also tracks repair statistics and provides I/O methods.

**Lifecycle:**
- Created by `ModelVolume` (owns a `TriangleMesh` by value)
- Repaired lazily via `TriangleMesh::repair()` which calls admesh routines
- Copied with value semantics (full deep copy)

**Memory:** No heap allocation beyond the internal `std::vector` members. Copying a `TriangleMesh` copies all vertex and face data — potentially expensive for large meshes.

**HAZARD:** `volume()` is mutable and caches to `-1` if not yet computed — `[STATE]` mutation through a conceptually-const operation.

---

### `Model`, `ModelObject`, `ModelVolume`, `ModelInstance`
**File:** `src/libslic3r/Model.hpp`

```
Model
 ├── ModelObjectPtrs objects          // std::vector<ModelObject*>  [MEMORY: raw ptrs, Model OWNS]
 ├── ModelMaterial materials          // std::map<t_model_material_id, ModelMaterial*>
 └── ModelWipeTower wipe_tower

ModelObject : ObjectBase, ModelConfigObject
 ├── ModelVolumePtrs volumes          // std::vector<ModelVolume*>  [MEMORY: raw ptrs, ModelObject OWNS]
 ├── ModelInstancePtrs instances      // std::vector<ModelInstance*> [MEMORY: raw ptrs, ModelObject OWNS]
 ├── PrintConfig-derived layer_config_ranges
 ├── std::string name / input_file
 └── BoundingBoxf3 bounding_box (cached)

ModelVolume : ObjectBase, ModelConfigObject
 ├── TriangleMesh mesh                // [MEMORY: owned by value — FULL COPY on assign]
 ├── ModelVolumeType type             // ModelPart, NegativeVolume, Modifier, SupportBlocker, SupportEnforcer, etc.
 ├── std::optional<TextConfiguration> text_configuration  // For embossed text volumes
 └── TriangleSelector *m_triangle_selector  // [MEMORY: raw ptr, lazily created]

ModelInstance : ObjectBase
 ├── Geometry::Transformation m_transformation  // 3D pose (translation, rotation, scale, mirror)
 └── ModelObject *m_object           // [MEMORY: back-ptr, non-owning]
```

**Lifecycle:**
- `Model` is the top-level container. Created by format loaders or GUI.
- `ModelObject`/`ModelVolume`/`ModelInstance` created via `Model::add_object()` / `ModelObject::add_volume()` etc.
- Deletion: Model destructor cascades through all raw-pointer owned collections
- Undo/Redo: `cereal` binary serialization used; `ObjectBase::id()` stable IDs maintained across serialize/deserialize cycles

**HAZARD:** Raw-pointer ownership chains with manual deletion in destructors. A missing `delete` would leak; double-free impossible only if ownership discipline is maintained. `TriangleSelector` raw pointer on `ModelVolume` created lazily — null check required everywhere.

---

### `Print`, `PrintObject`, `PrintRegion`
**File:** `src/libslic3r/Print.hpp`

```
Print : PrintBase
 ├── PrintObjectPtrs m_objects        // std::vector<PrintObject*>  [MEMORY: Print OWNS]
 ├── PrintRegionPtrs m_regions        // std::vector<PrintRegion*>  [MEMORY: Print OWNS]
 ├── Model &m_model                   // [STATE] borrowed ref — Print does NOT own Model
 ├── PrintConfig m_config
 └── PlaceholderParser m_placeholder_parser

PrintObject : PrintObjectBase
 ├── LayerPtrs m_layers               // std::vector<Layer*>  [MEMORY: PrintObject OWNS raw ptrs]
 ├── SupportLayerPtrs m_support_layers
 ├── PrintObjectRegions *m_shared_regions  // [MEMORY: shared, PrintObject does NOT always own]
 ├── PrintObjectConfig m_config
 └── Transform3d m_trafo              // Object world transform

PrintRegion (value type)
 ├── PrintRegionConfig config         // Per-region slicing parameters
 └── size_t m_ref_cnt                 // Reference counting (!!! manual refcount)
```

**Lifecycle:**
- `Print` created by the GUI/CLI on each slice request
- `PrintObject` created per `ModelObject` × `ModelInstance` cluster
- `Layer` objects created during `posSlice` step, owned by `PrintObject` via raw pointers
- **HAZARD:** `m_layers` as `std::vector<Layer*>` with manual memory management is a refactoring risk. Missing `delete` in error paths could leak.

---

### `Layer` and `LayerRegion`
**File:** `src/libslic3r/Layer.hpp`

```
Layer
 ├── size_t m_id                      // Sequential index in PrintObject::m_layers
 ├── coordf_t slice_z                 // Z of the slicing plane (mm, unscaled)
 ├── coordf_t print_z                 // Z of the printed layer top (mm, unscaled)
 ├── coordf_t height                  // Layer height (mm)
 ├── Layer *upper_layer               // [MEMORY: non-owning raw ptr, doubly-linked list]
 ├── Layer *lower_layer               // [MEMORY: non-owning raw ptr]
 ├── ExPolygons lslices               // Merged cross-sections from all volumes on this layer
 ├── ExPolygons loverhangs            // BBS: overhang regions for lift detection
 ├── CurledLines curled_lines         // Estimated malformed extrusion regions
 └── LayerRegionPtrs m_regions        // std::vector<LayerRegion*> [MEMORY: Layer OWNS]

LayerRegion
 ├── SurfaceCollection slices         // [STATE] surfaces by type (top/bottom/internal)
 ├── ExPolygons raw_slices            // Backup before type assignment
 ├── ExtrusionEntityCollection perimeters  // Wall extrusion paths
 ├── ExtrusionEntityCollection fills       // Infill extrusion paths
 ├── ExtrusionEntityCollection thin_fills  // Gap fills (from perimeter generator)
 ├── ExPolygons fill_expolygons       // Fill region polygons
 ├── SurfaceCollection fill_surfaces  // Fill surfaces with typed classification
 ├── Polylines unsupported_bridge_edges
 └── Layer *m_layer                   // [MEMORY: non-owning back-ptr]
     const PrintRegion *m_region      // [MEMORY: non-owning ptr to PrintRegion]
```

**HAZARD:** `Layer::upper_layer` / `lower_layer` are raw non-owning pointers forming a doubly-linked list. These must be manually maintained when layers are added/removed. A stale pointer after `PrintObject::m_layers` is resized is a use-after-free risk.

---

### `Point` (2D scaled integer coordinate)
**File:** `src/libslic3r/Point.hpp`

```cpp
class Point : public Vec2crd {  // Eigen::Matrix<coord_t, 2, 1, DontAlign>
    // coord_t = int32_t (scaled integer, 1 unit = 1e-6 mm)
    static Point new_scale(double x, double y);  // converts mm to scaled int
};
using Points = std::vector<Point, tbb::scalable_allocator<Point>>;
```

**Note:** `Points` uses TBB's scalable allocator for better multi-threaded allocation performance — a performance-critical decision that translation must replicate or substitute.

---

### `Polygon` and `ExPolygon`
**Files:** `src/libslic3r/Polygon.hpp`, `src/libslic3r/ExPolygon.hpp`

```cpp
class Polygon : public MultiPoint {
    Points points;  // [IMPORTANT] Last point implicitly closes to first — NOT duplicated in storage
    // Invariant: contour CCW, holes CW
};
using Polygons = std::vector<Polygon, PointsAllocator<Polygon>>;

class ExPolygon {
    Polygon  contour;  // Outer boundary — CCW winding
    Polygons holes;    // Inner holes — CW winding (each hole is a Polygon)
};
using ExPolygons = std::vector<ExPolygon>;
```

**Key invariant:** In Clipper's convention (enforced throughout), outer contours are counter-clockwise (CCW) and holes are clockwise (CW). Violation produces incorrect boolean operations.

**HAZARD:** The implicit closure (last point NOT stored) is invisible to naïve iteration. Code that iterates `polygon.points` must manually handle the closing edge from `points.back()` to `points.front()`.

---

### `ExtrusionEntity` Hierarchy
**File:** `src/libslic3r/ExtrusionEntity.hpp`

```
ExtrusionEntity (abstract)
 ├── ExtrusionPath                    // A single open polyline with extrusion metadata
 │     ├── Polyline polyline          // Points (scaled int)
 │     ├── double mm3_per_mm         // Volumetric flow rate (mm³ per mm of travel)
 │     ├── float width               // Extrusion width (mm)
 │     ├── float height              // Layer height (mm)
 │     └── ExtrusionRole m_role      // erPerimeter, erInternalInfill, erBridgeInfill, etc.
 ├── ExtrusionLoop                    // A closed loop (perimeter)
 │     ├── ExtrusionPaths paths      // std::vector<ExtrusionPath> forming the loop
 │     └── ExtrusionLoopRole m_loop_role  // elrDefault, elrHole, elrInternal
 ├── ExtrusionMultiPath               // Multiple connected open paths
 └── ExtrusionEntityCollection       // Ordered container (composite pattern)
       └── ExtrusionEntitiesPtr entities  // std::vector<ExtrusionEntity*> [MEMORY: OWNS pointers]

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;
```

**ExtrusionRole values (key subset):**
- `erPerimeter` — inner wall
- `erExternalPerimeter` — outermost wall
- `erOverhangPerimeter` — wall over void
- `erInternalInfill` — sparse infill
- `erSolidInfill` — solid fill region
- `erTopSolidInfill` — top surface fill
- `erBridgeInfill` — unsupported bridge
- `erGapFill` — narrow gap between perimeters
- `erSupportMaterial` / `erSupportMaterialInterface`

**MEMORY HAZARD:** `ExtrusionEntityCollection::entities` is `std::vector<ExtrusionEntity*>` — raw owning pointers. The collection deletes via its destructor but the ownership model requires care. Nested collections (collection of collections) are common.

**COUPLING:** `mm3_per_mm` and `width`/`height` are stored per-path, meaning the G-code generator reads flow directly from the extrusion entity rather than recomputing from config. This couples the toolpath representation to the specific printer config used during slicing.

---

### `Flow` (`src/libslic3r/Flow.hpp`)

```cpp
struct Flow {
    float width;         // Extrusion width (mm, unscaled)
    float height;        // Layer height (mm, unscaled)
    float nozzle_diameter;
    bool  bridge;        // Is this a bridge extrusion?
    
    float spacing() const;       // Line spacing for parallel lines
    float mm3_per_mm() const;    // Volumetric flow rate
    float mm3_per_mm_scaled() const;
};
```

**Purpose:** Encapsulates the relationship between nozzle diameter, layer height, and extrusion width to compute the volumetric flow rate. Critical for correct under/over-extrusion.

---

### `SurfaceCollection` and `Surface`
**File:** `src/libslic3r/Surface.hpp`, `SurfaceCollection.hpp`

```cpp
enum SurfaceType : uint8_t {
    stTop, stBottom, stBottomBridge,
    stInternal, stInternalSolid, stInternalBridge, stInternalVoid,
    stPerimeter, stModifier
};

class Surface {
    SurfaceType surface_type;
    ExPolygon   expolygon;        // The actual 2D shape
    double      thickness;        // In mm (for multi-layer bridging)
    unsigned    thickness_layers; // In layer count
    double      bridge_angle;     // Detected bridge direction
    unsigned    extra_perimeters; // Additional perimeters for this surface
};
```

**Purpose:** Tagged 2D region on a layer. After the perimeter generator runs, raw slices are classified into top/bottom/internal surfaces based on layer-to-layer comparison.

---

### `PrintConfig` and Configuration System
**File:** `src/libslic3r/PrintConfig.hpp`, `Config.hpp`

The configuration system is a large property-map-based system:

```
ConfigBase (abstract)
 └── StaticPrintConfig (static field layout)
       ├── PrintConfig          // Printer profile (speeds, temperature, etc.)
       ├── PrintObjectConfig    // Per-object settings (layer height, walls, infill %)
       ├── PrintRegionConfig    // Per-region settings (material assignment, etc.)
       ├── FullPrintConfig      // Union of all config types
       └── GCodeConfig          // G-code specific settings
```

**Config values** are strongly typed via template specialization:
- `ConfigOptionFloat` / `ConfigOptionInt` / `ConfigOptionBool`
- `ConfigOptionEnum<T>` for enum settings
- `ConfigOptionFloatOrPercent` for values like "10% or 2mm"
- `ConfigOptionPoint` / `ConfigOptionPoints`

**HAZARD:** Config is accessed globally through the `Print` object. The `PlaceholderParser` interpolates config values into G-code template strings — a runtime string templating system that couples G-code emission to the config system in ways that are hard to statically analyze.

---

### `GCodeWriter` (`src/libslic3r/GCodeWriter.hpp`)

```cpp
class GCodeWriter {
    GCodeConfig config;              // Speed, temperature, firmware dialect
    std::string m_extrusion_axis;    // "E" or "A" depending on firmware
    double m_current_e_pos;          // [STATE] current extruder position
    double m_lifted;                 // [STATE] current Z-lift
    Extruder *m_extruder;            // [STATE] active extruder
    std::vector<Extruder> m_extruders;
};
```

**Output:** All methods return `std::string` — G-code is built by string concatenation throughout. No streaming or buffering abstraction.

**HAZARD:** `m_current_e_pos` is a critical piece of stateful tracking — if this drifts from the printer's actual E position, all subsequent extrusions will be wrong. This state is reset by retraction/recovery logic.

---

### `SupportLayer` / `SupportLayerPtrs` (`src/libslic3r/Layer.hpp:441`)

```cpp
class SupportLayer : public Layer {
public:
    ExPolygons                  support_islands;   // Polygons covered by support — used to suppress retraction
    ExtrusionEntityCollection   support_fills;     // All support extrusion paths (base + interface + contacts)
    SupportInnerType            support_type;      // stInnerNormal / stInnerTree
    ExPolygons                  base_areas;        // Tree support: base contact regions
protected:
    friend class PrintObject;
    friend class TreeSupport;
    size_t                      m_interface_id;    // Zero-based index for alternating interface direction
    ExPolygons                  roof_areas;        // Tree support: roof contact regions
    ExPolygons                  roof_1st_layer;    // Layer immediately below roof (PolySupport: printed with model material)
    ExPolygons                  floor_areas;       // Tree support: floor contact regions
    ExPolygons                  roof_gap_areas;    // Regions in gap between roof and overhang
    std::vector<AreaGroup>      area_groups;       // Flattened view: pointers into the ExPolygon vectors above
    SupportLayer(size_t id, size_t interface_id, PrintObject* object,
                 coordf_t height, coordf_t print_z, coordf_t slice_z);
};
using SupportLayerPtrs = std::vector<SupportLayer*>;
```

**Purpose:** Represents a single Z-slice of support material. Extends `Layer` with support-specific geometry. The `area_groups` vector provides a unified iteration interface over all the individual `ExPolygon` area vectors, used by extrusion generation.

**Invariants:**
- `area_groups[i].area` always points into one of `base_areas`, `roof_areas`, `floor_areas`, or `roof_gap_areas` of the same `SupportLayer`. It must never outlive those vectors.
- Constructor is `protected`; only `PrintObject` and `TreeSupport` may create instances.
- `m_interface_id` is zero-based and used exclusively for direction alternation in interface printing.

**Lifecycle:**
- Heap-allocated in `PrintObject` via `new SupportLayer(...)` and stored as raw pointers in `PrintObject::m_support_layers` (`SupportLayerPtrs`).
- Destroyed in `PrintObject::clear_support_layers()`, which calls `delete` on each raw pointer.
- `area_groups` is populated after initial geometry is placed in the area vectors; consumers must not reallocate those vectors after population.

**Consumers:** `Support::SupportMaterial`, `Support::TreeSupport`, `Support::TreeSupport3D`, `GCode.cpp` (iterates `support_fills` for G-code output).

**HAZARD H351:** `AreaGroup::area` is a raw `ExPolygon*` into the owning `SupportLayer`'s area vectors. Any `push_back` or `clear` on `base_areas`, `roof_areas`, `floor_areas`, or `roof_gap_areas` after `area_groups` is built will produce dangling pointers. Callers must complete all mutations to area vectors before calling code that reads `area_groups`.

---

### `FillAdaptive::Octree` (`src/libslic3r/Fill/FillAdaptive.cpp:325`)

```cpp
struct Octree {
    boost::object_pool<Cube>    pool;             // Owns all Cube nodes; bulk-freed on destruction
    Cube*                       root_cube;        // Non-owning pointer to pool-allocated root
    Vec3d                       origin;           // World-space origin of the octree bounding box
    std::vector<CubeProperties> cubes_properties; // Per-depth geometry parameters (indexed by depth)
};
using OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;

struct CubeProperties {
    double edge_length;        // mm — side length of cubes at this depth
    double height;             // mm — XY diagonal / sqrt(2)
    double diagonal_length;    // mm — face diagonal
    double line_z_distance;    // Max Z distance from cube center where infill lines are generated
    double line_xy_distance;   // Max XY distance from cube center where infill lines are generated
};
```

**Purpose:** Spatial acceleration structure for adaptive cubic infill. The octree partitions the print volume so that infill density adapts to local model complexity. Leaf nodes near model surfaces receive finer (denser) infill; interior nodes receive coarser infill. The tree is built once per `PrintObject` and shared read-only across all parallel fill tasks.

**Invariants:**
- `pool` owns all `Cube` nodes. `root_cube` and all `Cube::children` are non-owning pointers into `pool`.
- After `build_octree()` returns, `origin` is in world coordinates and the tree is immutable.
- `cubes_properties[d]` holds geometry parameters for octree depth `d`; depth 0 is the finest (leaf) level.

**Lifecycle:**
- Created by `FillAdaptive::build_octree()`, which takes the `PrintObject` mesh and target line-spacing.
- Stored in `PrintObject` as `OctreePtr` (a `unique_ptr<Octree, OctreeDeleter>`).
- Destroyed when `PrintObject` is destroyed or when a new print configuration is applied; `OctreeDeleter::operator()` calls `delete p`, which destroys the `pool` and bulk-frees all nodes.

**Consumers:** `FillAdaptive::Filler` (read-only during `_fill_surface_single`), `FillAdaptive::Filler` support variant.

**HAZARD:** The single octree is computed with an **averaged** line spacing across all printing regions (see `adaptive_fill_line_spacing()`). If multiple regions have significantly different infill densities, the resulting octree is a compromise that may not accurately represent any individual region. A refactor should compute per-region octrees.

---

### `FillLightning::Generator` (`src/libslic3r/Fill/Lightning/Generator.hpp:74`)

```cpp
class Generator {
protected:
    float                       m_infill_extrusion_width;    // Scaled coord_t (result of scaled<float>(line_width_mm))
    coord_t                     m_supporting_radius;         // Radius of influence: how far a line can support overhang
    coord_t                     m_prune_length;              // Min length below which dead branches are pruned
    coord_t                     m_straightening_max_distance;// Max lateral move when straightening a branch
    std::vector<Polygons>       m_overhang_per_layer;        // Internal overhangs that must be reached per layer
    std::vector<Layer>          m_lightning_layers;          // Built tree structure per layer
    std::vector<BoundingBox>    bboxs;                       // AABB per layer for EdgeGrid accelerator
};
using GeneratorPtr = std::unique_ptr<Generator, GeneratorDeleter>;
```

**Purpose:** Pre-computes the full lightning infill tree structure for a `PrintObject`. The constructor runs the entire tree-building algorithm; `getTreesForLayer()` is then a simple indexed read used during fill generation. The pattern produces minimal material usage by growing branch networks from model walls inward toward unsupported points.

**Invariants:**
- `m_overhang_per_layer.size() == m_lightning_layers.size() == bboxs.size()` (all indexed identically by layer index, not global Z).
- After construction, the object is immutable; no method writes to `m_lightning_layers` after construction.
- `m_supporting_radius` is hardcoded from the 45° overhang angle: `extrusion_width * 100 * n_multiline / density`.
- Minimum density clamped to 0.15 in the support-mode constructor to prevent near-zero division.

**Lifecycle:**
- Created by `FillLightning::build_generator()` or directly by tree-support code.
- Stored in `PrintObject` as `GeneratorPtr`.
- `FillLightning::Filler::generator` holds a **raw non-owning** `const Generator*` set during fill initialisation.

**Consumers:** `FillLightning::Filler::_fill_surface_single()` (calls `getTreesForLayer(layer_id)`), `Support::TreeSupport3D` (secondary constructor path).

**HAZARD H267:** `FillLightning::Filler::generator` is a raw non-owning `const Generator*`. It is set in `Filler::init()` and used during `_fill_surface_single()`. If the owning `GeneratorPtr` in `PrintObject` is destroyed or reset between those two calls (e.g., during a concurrent reconfiguration), the pointer becomes dangling. No lifetime checks are present.

**HAZARD H268:** `getTreesForLayer()` uses `assert(layer_id < m_lightning_layers.size())` in debug only. In release builds, an out-of-range `layer_id` produces `std::vector::operator[]` UB with no diagnostic.

---

### `UndoRedo::StackImpl` (`src/slic3r/Utils/UndoRedo.cpp:546`)

```cpp
class StackImpl {
private:
    size_t                                              m_memory_limit;           // Default: 10% of physical RAM
    std::map<ObjectID, std::unique_ptr<ObjectHistoryBase>> m_objects;             // Per-object serialization history
    std::map<const void*, ObjectID>                     m_shared_ptr_to_object_id;// Temporary IDs for immutable shared objects
    std::vector<Snapshot>                               m_snapshots;              // Named checkpoints with timestamps
    size_t                                              m_active_snapshot_time;   // Timestamp of the currently active snapshot
    size_t                                              m_current_time;           // Monotonically increasing logical clock
    size_t                                              m_saved_snapshot_time;    // Timestamp of the last saved-to-disk state
    Selection                                           m_selection;              // Last deserialized selection state
    std::vector<ObjectBase*>                            m_reusable_objects;       // Object pool for reducing allocation churn
};

// I/O uses cereal binary archives with a StackImpl user-data adapter:
using InputArchive  = cereal::UserDataAdapter<StackImpl, cereal::BinaryInputArchive>;
using OutputArchive = cereal::UserDataAdapter<StackImpl, cereal::BinaryOutputArchive>;
```

**Purpose:** The implementation core of the GUI undo/redo system. Each `take_snapshot()` serializes the full application state (Model, Selection, GLGizmosManager, PartPlateList) into per-object binary archives. The memory footprint is managed by evicting least-recently-used snapshots when `m_memory_limit` is exceeded.

**Invariants:**
- `m_snapshots` is kept sorted by `timestamp` (ascending). `m_snapshots.back()` is always the topmost sentinel.
- `m_active_snapshot_time` is always within `[m_snapshots.front().timestamp, m_snapshots.back().timestamp]`.
- Mutable objects are tracked by their `ObjectID` (assigned at construction). Immutable shared objects (`shared_ptr<const T>`) are assigned temporary `ObjectID`s stored in `m_shared_ptr_to_object_id`.
- `m_saved_snapshot_time = size_t(-1)` indicates no known saved state; `project_modified()` uses this to determine dirty status.

**Lifecycle:**
- Owned by `Slic3r::GUI::Plater` via `std::unique_ptr<UndoRedo::Stack>` (the pImpl wrapper around `StackImpl`).
- `clear()` resets all fields without releasing `m_memory_limit`.
- `release_least_recently_used()` is called automatically when `memsize() > m_memory_limit` inside `take_snapshot()`.

**Consumers:** `GUI::Plater` (calls `take_snapshot`, `undo`, `redo`), `GUI::GLCanvas3D` (reads `has_undo_snapshot` / `has_redo_snapshot` to enable/disable toolbar buttons).

**HAZARD:** The `StackImpl` class is `friend`ed by multiple GUI types that directly access private fields via the `save_mutable_object` / `load_mutable_object` template methods. These templates are instantiated for `Model`, `ModelObject`, `ModelInstance`, `ModelVolume`, `TriangleMesh`, `Selection`, and `GLGizmosManager`. Any new object type added to the snapshot must add a corresponding `cereal` serialization method and a new template instantiation, or the snapshot will silently omit that state.

---

### `TriangleSelector` (`src/libslic3r/TriangleSelector.hpp:45`)

```cpp
class TriangleSelector {
protected:
    std::vector<Vertex>         m_vertices;          // Working vertex set (original + subdivision-generated)
    std::vector<Triangle>       m_triangles;         // Working triangle set (original + subdivision-generated)
    const TriangleMesh         &m_mesh;              // Reference to the external mesh — NOT owned
    const std::vector<Vec3i32>  m_neighbors;         // Face adjacency table (copied at construction)
    const std::vector<Vec3f>    m_face_normals;      // Per-face normals (copied at construction)
    float                       m_edge_limit;        // Max subdivision edge length in mm (BBS, default 0.6)
    float                       m_edge_limit_sqr;    // Squared version for fast distance tests
    int                         m_invalid_triangles; // Count of marked-invalid triangles (GC trigger)
    int                         m_orig_size_vertices;// Original vertex count before any subdivision
    int                         m_orig_size_indices; // Original triangle count before any subdivision
    std::unique_ptr<Cursor>     m_cursor;            // Active paint cursor (CIRCLE/SPHERE/POINTER/HEIGHT_RANGE/GAP_FILL)
    float                       m_old_cursor_radius_sqr; // Cached previous cursor radius
};

// Triangle state:
struct Triangle {
    std::array<int, 3>  verts_idxs;        // Indices into m_vertices
    int                 children[3];        // Indices of child triangles (-1 if leaf)
    EnforcerBlockerType state;             // Enforcer / Blocker / Extruder assignment (leaf only)
    char                number_of_splits;  // 0 = leaf, 1–3 = split
    char                special_side_idx;
    bool                m_selected_by_seed_fill : 1;
    bool                m_valid : 1;
};
```

**Purpose:** Implements the paint-on triangle selection system used by seam painting, support painting, and multi-material painting tools. The mesh is subdivided on demand as the user paints, producing finer triangles in painted regions. Each leaf triangle carries an `EnforcerBlockerType` state (Enforcer, Blocker, or extruder assignment index).

**Invariants:**
- `m_mesh` is borrowed — the `TriangleSelector` must not outlive the `TriangleMesh` it references.
- `m_vertices[0..m_orig_size_vertices-1]` and `m_triangles[0..m_orig_size_indices-1]` mirror the original mesh; all higher-indexed entries are subdivision products.
- A triangle with `number_of_splits > 0` is not a leaf; `state` is only meaningful for leaf triangles.
- `Triangle::state` is deliberately placed after all bitfield members to prevent compiler data-race packing (see comment in source).

**Lifecycle:**
- Constructed by GUI paint gizmos (`GLGizmoSeam`, `GLGizmoFdmSupports`, `GLGizmoPainterBase`) by passing a `const TriangleMesh&` borrowed from `ModelVolume`.
- Destroyed when the gizmo is deactivated or the model volume is deleted.
- `reset()` restores all triangles to `EnforcerBlockerType::NONE` without deallocating subdivision data.
- Garbage collection (compaction of `m_invalid_triangles`) runs when the invalid count exceeds a threshold.

**Consumers:** `GLGizmoPainterBase` (painting operations), `ModelVolume` (stores serialized per-triangle state for undo/redo via `TriangleSelector::serialize()`).

**HAZARD:** `m_mesh` is a `const TriangleMesh&` — a reference, not a pointer. If the owning `ModelVolume` is moved or the mesh is reallocated (e.g., after mesh repair or model transform application), the reference becomes dangling. No runtime check exists. The GUI must ensure the gizmo is deactivated before any mesh mutation.

---

### `PlaceholderParser` (`src/libslic3r/PlaceholderParser.hpp`)

```cpp
class PlaceholderParser {
public:
    struct ContextData {
        std::mt19937                    rng;           // RNG for {random} expressions
        std::unique_ptr<DynamicConfig>  global_config; // User variables persisted across evaluations
    };
private:
    DynamicConfig           m_config;          // Owned: higher-priority config values (set via set(...))
    const DynamicConfig    *m_external_config; // Borrowed: lower-priority fallback (PrintConfig, etc.)
};
```

**Purpose:** Evaluates G-code template strings containing `{expressions}`. Used to expand start/end G-code, custom G-code snippets, filament-change scripts, and any user-defined string that references print configuration values. The expression evaluator is implemented using **Boost.Spirit v2 (`qi` grammar)** — not Spirit X3. Uses `iso8859_1` character encoding instead of `ascii` to avoid crashes on negative `char` values in UTF-8 strings.

**Invariants:**
- `m_config` takes priority over `m_external_config` in symbol lookup. `process()` merges them at evaluation time with `config_override` taking the highest priority.
- `process()` is `const` and therefore thread-safe for concurrent evaluations, provided each thread supplies its own `ContextData` (the `rng` is mutable state that must not be shared).
- `m_external_config` is a non-owning pointer; `PlaceholderParser` must not outlive the object it points to.
- The `ContextData::global_config` persists user-defined variables (via `{variable = value}` script syntax) across multiple `process()` calls on the same context.

**Lifecycle:**
- Owned by `Print` (one instance) and updated via `apply_config()` whenever print settings change.
- `apply_env_variables()` injects OS environment variables as string config keys at startup.
- `process()` builds and evaluates the Spirit grammar on every call — no grammar caching. For hot paths (many G-code scripts per layer), this is a potential performance concern.

**Consumers:** `GCode::_do_export()` (custom start/end G-code), `GCode::change_layer()` (layer-change G-code), `ToolOrdering` (tool-change scripts), `PrintConfig` validation.

**HAZARD:** The Spirit `qi` grammar parser is instantiated fresh on each `process()` call. For models with many objects and custom G-code at every layer, this becomes a repeated grammar compilation cost. Additionally, using `iso8859_1` encoding means the parser will accept and silently pass through non-ASCII bytes rather than erroring. Template authors writing UTF-8 G-code comments inside `{...}` blocks may get unexpected token matches.

---

## Cross-Module Data Transfer Summary

| Boundary | Data Type | Transfer Mechanism |
|---|---|---|
| Format loader → Model | `TriangleMesh` | By value (moved into `ModelVolume`) |
| Model → PrintObject setup | `ModelObject&` | By reference (borrowed, not copied) |
| Slicing → Layer | `ExPolygons` (per Z) | By value, stored in `Layer::lslices` |
| Perimeter gen → LayerRegion | `ExtrusionEntityCollection` | By value, moved into `LayerRegion::perimeters` |
| Infill gen → LayerRegion | `ExtrusionEntityCollection` | By value, moved into `LayerRegion::fills` |
| LayerRegion → GCode | `ExtrusionPath&` | By const reference iteration |
| GCode → output | `std::string` | Concatenation into growing string buffer |

**Deep copies at boundaries:**
- `TriangleMesh` copy is a full deep copy of all vertex/index data — expensive
- `ExPolygons` deep copy occurs when slices are stored per layer — one copy per Z level
- `ExtrusionEntityCollection` owns its `ExtrusionEntity*` pointers; `clone()` performs deep copies when needed for multi-material segmentation
