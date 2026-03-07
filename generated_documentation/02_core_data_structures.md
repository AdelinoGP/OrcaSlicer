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
