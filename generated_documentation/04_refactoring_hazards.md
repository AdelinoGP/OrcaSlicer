# OrcaSlicer Refactoring Hazards

**Purpose:** Catalogue every class of hazard that must be solved before or during a port to another language. Each section names the pattern, locates it in the codebase, explains why it is dangerous to translate naively, and proposes a mitigation strategy.

---

## Table of Contents

1. [Raw-Pointer Ownership Chains](#1-raw-pointer-ownership-chains)
2. [Dual Coordinate System](#2-dual-coordinate-system)
3. [TBB Parallelism Model](#3-tbb-parallelism-model)
4. [Virtual Dispatch & Clone Pattern](#4-virtual-dispatch--clone-pattern)
5. [Implicit Polygon Closure Convention](#5-implicit-polygon-closure-convention)
6. [Winding-Order Contracts](#6-winding-order-contracts)
7. [Shared Mutable State in Background Processing](#7-shared-mutable-state-in-background-processing)
8. [g_last_timestamp Global (Unsynchronized)](#8-g_last_timestamp-global-unsynchronized)
9. [Template-Heavy Configuration System](#9-template-heavy-configuration-system)
10. [cereal Serialization Coupling](#10-cereal-serialization-coupling)
11. [Clipper Integer Overflow Risks](#11-clipper-integer-overflow-risks)
12. [OpenVDB / Eigen Vectorization Dependencies](#12-openvdb--eigen-vectorization-dependencies)
13. [TBB Scalable Allocator on Points](#13-tbb-scalable-allocator-on-points)
14. [Exception-as-Cancellation Protocol](#14-exception-as-cancellation-protocol)
15. [Multi-Language / Localization Indirection](#15-multi-language--localization-indirection)
16. [ExtrusionEntitiesPtr — Manual Heap Ownership](#16-extrusionentitiesptr--manual-heap-ownership)
17. [PrintObjectRegions Reference Counting](#17-printobjectregions-reference-counting)
18. [Two Perimeter Generators (Classic + Arachne)](#18-two-perimeter-generators-classic--arachne)
19. [Two Support Generators (Traditional + Tree)](#19-two-support-generators-traditional--tree)
20. [G-code State Machine (GCodeWriter)](#20-g-code-state-machine-gcodewriter)
21. [Plate / Multi-Instance Shift Arithmetic](#21-plate--multi-instance-shift-arithmetic)
22. [Config Option Key Strings as Identifiers](#22-config-option-key-strings-as-identifiers)
23. [Large Monolithic Files](#23-large-monolithic-files)
24. [Hidden Floating-Point Precision Contracts](#24-hidden-floating-point-precision-contracts)

---

## 1. Raw-Pointer Ownership Chains

**Location:**
- [`Print.hpp:175`](../src/libslic3r/Print.hpp#L175) — `typedef std::vector<Layer*> LayerPtrs`
- [`Print.hpp:182`](../src/libslic3r/Print.hpp#L182) — `typedef std::vector<SupportLayer*> SupportLayerPtrs`
- [`Model.hpp:123-126`](../src/libslic3r/Model.hpp#L123) — `ModelMaterialMap`, `ModelObjectPtrs`, `ModelVolumePtrs`, `ModelInstancePtrs`
- [`ExtrusionEntity.hpp:148`](../src/libslic3r/ExtrusionEntity.hpp#L148) — `typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr`

**Hazard:** Raw pointer vectors establish ownership by convention, not by type. The owner (e.g., `PrintObject` for `LayerPtrs`) must manually call `delete` in its destructor. Any early return, exception, or refactoring that creates a second owner causes either double-free or leak. There is no type-level enforcement.

**Affected classes:** `PrintObject`, `Print`, `Model`, `ModelObject`, `ModelVolume`, `ExtrusionEntityCollection`.

**Translation risk:** In GC languages this becomes "just objects," losing the explicit ownership signal. In Rust, these must become `Vec<Box<Layer>>`. In any language, the destructor logic must be audited to match exactly who deletes what.

**Mitigation:** Before porting, map every raw-pointer vector to its owning class. Replace with `unique_ptr` vectors in C++ first as a safe intermediate step; this will surface any hidden aliasing.

---

## 2. Dual Coordinate System

**Location:**
- [`Point.hpp:56`](../src/libslic3r/Point.hpp#L56) — `using PointsAllocator = tbb::scalable_allocator<BaseType>`
- `libslic3r.h` — defines `SCALING_FACTOR = 1e-6` and `scale_()` / `unscale()` macros
- `TriangleMesh.hpp` — 3-D coordinates in `float` mm (Eigen `Vec3f`)
- `Polygon.hpp` — 2-D coordinates in `coord_t` = `int32_t` scaled integers (1 unit = 1e-6 mm)

**Hazard:** Two completely different numeric domains coexist. Mesh data is `float` millimetres; all 2-D polygon/path data is `int32_t` with an implicit 1e6 scale factor. Passing one kind to a function expecting the other silently compiles but produces geometrically wrong results. The conversion functions (`scale_()`, `unscale()`, `scaled<T>()`) are scattered across call sites; missing a conversion goes undetected until print artefacts appear.

**Critical invariant:** Any polygon arithmetic that overflows `int32_t` range (~±2147 mm in scaled units = ~±2.1 mm before overflow) is silently wrong. The Clipper library relies on `int64_t` intermediate results.

**Translation risk:** Languages without strong typedef/newtype systems (Python, Go, JS) will silently mix the two, producing garbage geometry.

**Mitigation:** Introduce `ScaledCoord` and `FloatMm` newtypes immediately in the target language to enforce the boundary at compile time.

---

## 3. TBB Parallelism Model

**Location:**
- [`pchheader.hpp:103-105`](../src/libslic3r/pchheader.hpp#L103) — global TBB includes
- [`TriangleMeshSlicer.cpp:521`](../src/libslic3r/TriangleMeshSlicer.cpp#L521) — `tbb::parallel_for` over triangles
- [`TreeSupport.cpp:769`](../src/libslic3r/Support/TreeSupport.cpp#L769) — `tbb::parallel_for` over layers
- [`TreeSupport3D.cpp:218`](../src/libslic3r/Support/TreeSupport3D.cpp#L218) — nested `tbb::parallel_for`
- [`TreeModelVolumes.cpp:244`](../src/libslic3r/Support/TreeModelVolumes.cpp#L244) — `tbb::task_group` for concurrent avoidance + wall restriction passes

**Hazard:** TBB's task-stealing scheduler is deeply embedded. At least 386 uses of `tbb::parallel_for` / `tbb::parallel_for_each` / `tbb::task_group` exist in `.cpp` files alone. The scheduler assigns work dynamically; algorithms assume TBB grain sizes and partitioning strategies are tuned for the problem size. Nested parallelism (parallel_for inside parallel_for) is intentional and common.

**Additional TBB features used:**
- `tbb::concurrent_unordered_map` (TreeSupport collision/avoidance caches)
- `tbb::concurrent_vector` (TreeSupport node collection)
- `tbb::spin_mutex` (TreeSupport layer mutex)
- `tbb::scalable_allocator` (Points, SupportLayer)

**Translation risk:** No direct equivalent exists in most languages. Rust has rayon (structurally similar); Go has goroutines (very different semantics); Python/JVM have their own thread pools. The grain-size tuning and work-stealing semantics must be re-profiled in the target.

**Mitigation:** Document each `parallel_for` usage with the data it iterates, the read/write sets, and the cancellation hook used. This creates a parallelism map for the target runtime.

---

## 4. Virtual Dispatch & Clone Pattern

**Location:**
- [`ExtrusionEntity.hpp:105-146`](../src/libslic3r/ExtrusionEntity.hpp#L105) — abstract base with pure virtual `clone()` and `clone_move()`
- Concrete classes: `ExtrusionPath`, `ExtrusionMultiPath`, `ExtrusionLoop`, `ExtrusionPathSloped`, `ExtrusionPathOriented`, `ExtrusionLoopSloped`
- [`Model.hpp:128-150`](../src/libslic3r/Model.hpp#L128) — `OBJECTBASE_DERIVED_COPY_MOVE_CLONE` macro generating `new_copy`, `new_clone`, `assign_copy`, `assign_clone`

**Hazard:** The virtual `clone()` / `clone_move()` methods allocate heap objects and return raw pointers. The caller is responsible for ownership. The `OBJECTBASE_DERIVED_COPY_MOVE_CLONE` macro distinguishes between "copy with same ID" (for internal `Print::apply()` deep copies) and "clone with new ID" (for user-visible duplication). Confusing the two produces ObjectID aliasing bugs that corrupt undo/redo history and incremental slicing.

**Translation risk:** Languages without value-semantics copies (Java, Python) will default to reference semantics, silently aliasing objects that should be independent. Languages without macros will need the clone/copy distinction handled via traits or interfaces.

**Mitigation:** Treat ObjectID propagation as a first-class concern. Document every call site of `new_copy` vs. `new_clone` and verify the ID contract is preserved in translation.

---

## 5. Implicit Polygon Closure Convention

**Location:**
- [`Polygon.hpp`](../src/libslic3r/Polygon.hpp) — `struct Polygon { Points points; }` — last point is **not** repeated
- [`Polyline.hpp`](../src/libslic3r/Polyline.hpp) — `struct Polyline { Points points; }` — open by definition
- [`ExtrusionEntity.hpp:585-586`](../src/libslic3r/ExtrusionEntity.hpp#L585) — `extrusion_entities_append_loops` manually appends `points.front()` to close: `path.polyline.points.push_back(path.polyline.points.front())`

**Hazard:** `Polygon` is implicitly closed — iteration over its `N` points must assume an `N+1`th closing edge from `points.back()` to `points.front()`. This is inconsistent with `ExtrusionLoop`, whose embedded `ExtrusionPath` polylines explicitly carry the repeated closing point. Algorithms that iterate polygon edges must use `Polygon::lines()` (which synthesizes the closing edge) rather than directly iterating `.points` pairwise.

**Translation risk:** Any port that stores polygons as explicit rings (GeoJSON-style, Shapely, etc.) and directly converts will either double the closing point or miss the closing edge.

**Mitigation:** Audit all polygon-edge-iteration code. Enforce a wrapper type in the target language that always exposes the closing edge through an iterator, regardless of whether the point is stored.

---

## 6. Winding-Order Contracts

**Location:**
- `Polygon.hpp` — `is_clockwise()`, `is_counter_clockwise()`, `make_clockwise()`, `make_counter_clockwise()`
- `ExPolygon.hpp` — `contour` (CCW) + `holes` (CW)
- `ClipperUtils.hpp` — Clipper enforces: outer contours CCW, holes CW

**Hazard:** The entire Clipper pipeline (boolean ops, offsets) requires:
- **Outer contours:** counter-clockwise (CCW)
- **Holes:** clockwise (CW)

This is the **opposite** of some other polygon libraries (e.g., PostGIS / OGC which use CW for outer rings in geodetic conventions). Violating this silently inverts clip results — holes become filled, filled areas become holes.

**Translation risk:** Any library swap (e.g., using Shapely in Python, which uses a different default) can silently flip winding and corrupt all boolean geometry operations.

**Mitigation:** Encode winding expectations as explicit assertions or type invariants at Clipper call boundaries.

---

## 7. Shared Mutable State in Background Processing

**Location:**
- [`PrintBase.hpp:46-104`](../src/libslic3r/PrintBase.hpp#L46) — `PrintStateBase` with `StateWithTimeStamp`, `StateWithWarnings`
- [`PrintBase.hpp:150-231`](../src/libslic3r/PrintBase.hpp#L150) — `set_started()`, `set_done()`, `invalidate()` all take `std::mutex &mtx`
- `Print.hpp` — `m_state_mutex` guards all step state transitions
- Background worker thread (OrcaSlicer.cpp) calls `Print::process()` while UI thread calls `Print::apply()` with new config

**Hazard:** The slicing pipeline runs in a background worker thread. The UI thread can simultaneously call `Print::apply()` to update configuration. The protocol is:
1. `apply()` acquires `m_state_mutex`, marks invalidated steps, and fires cancellation.
2. Worker thread checks `throw_if_canceled()` at TBB grain boundaries.
3. Worker catches `CanceledException` and re-enters the pipeline from the first invalidated step.

Missing a `throw_if_canceled()` call in a long-running algorithm means the UI hangs waiting for cancellation. Missing a mutex lock on state read means a data race.

**Translation risk:** This is a sophisticated cooperative cancellation protocol. Languages with async/await or structured concurrency must carefully map `throw_if_canceled()` to cancellation tokens or coroutine cancellation points.

**Mitigation:** Map every long-running loop and identify where `throw_if_canceled` is (or should be) called. This becomes a checklist for implementing cancellation in the target language.

---

## 8. g_last_timestamp Global (Unsynchronized)

**Location:**
- [`PrintBase.hpp:104`](../src/libslic3r/PrintBase.hpp#L104) — `static size_t g_last_timestamp`
- Comment: *"if multiple Print or SLAPrint instances are executed in parallel, modification of g_last_timestamp is not synchronized!"*

**Hazard:** This is a documented, acknowledged race condition. Multiple `Print` instances running in parallel will race on `g_last_timestamp`, potentially producing duplicate or out-of-order timestamps, which could corrupt the incremental invalidation logic.

**Translation risk:** Any port must either fix this (atomic counter, or per-instance counter) or explicitly document it as a known limitation when only one slicing job runs at a time.

**Mitigation:** Replace with `std::atomic<size_t>` in C++ first; use atomic/lock-free counter in the target language.

---

## 9. Template-Heavy Configuration System

**Location:**
- `PrintConfig.hpp` / `PrintConfig.cpp` — hundreds of config options declared via macro + template
- `ConfigBase.hpp` — base template hierarchy: `ConfigBase` → `StaticPrintConfig` → `PrintConfig`, `PrintObjectConfig`, `PrintRegionConfig`, `GCodeConfig`, etc.
- `t_config_option_keys` = `std::vector<std::string>` — config keys are runtime strings

**Hazard:** The configuration system uses C++ templates to generate typed getters (`config.layer_height.value`) while also supporting runtime string-key access (`config.opt<ConfigOptionFloat>("layer_height")`). This dual static/dynamic interface is used for:
- Preset diffing (which keys changed?)
- CLI parsing
- Undo/redo snapshots
- Profile serialization

There are 200+ config options. Any port must replicate both interfaces.

**Translation risk:** Languages without C++ templates (Python, Go) will either lose static typing on config access or require significant boilerplate. Languages with reflection (Java, Python) can recover some of this via annotations or descriptors.

**Mitigation:** Generate a schema file (JSON/TOML) from the C++ config declarations. Use this schema to auto-generate typed accessors in the target language.

---

## 10. cereal Serialization Coupling

**Location:**
- `Model.hpp:40-47` — cereal archive forward declarations
- `ModelConfigObject` — `template<class Archive> void serialize(Archive &ar)`
- `UndoRedo::StackImpl` — friend of all Model classes for undo/redo serialization

**Hazard:** The `cereal` header-only library is deeply integrated into the Model hierarchy for undo/redo state snapshots. Classes grant `cereal::access` friendship, and their internal state is exposed to cereal serialization. This means object invariants can be violated during deserialization if restore order is wrong (e.g., a pointer being restored before the pointed-to object).

**Translation risk:** Undo/redo is implemented as deep-copy snapshots via `cereal::BinaryOutputArchive`. A port must reimplement this, either via a command pattern or by manually specifying snapshot semantics for each class.

**Mitigation:** Treat undo/redo as a separate subsystem. Define a minimal serializable state struct for each Model class; do not rely on full object serialization.

---

## 11. Clipper Integer Overflow Risks

**Location:**
- `ClipperUtils.hpp` / `ClipperUtils.cpp` — wrapper around Clipper2 library
- All polygon boolean operations, offsets, and Minkowski sums

**Hazard:** Clipper's integer arithmetic uses `int64_t` for intermediate products of coordinate differences. With `coord_t = int32_t` and scale factor 1e6, object dimensions up to ~2147 mm fit. However, after **offset** operations, coordinates temporarily exceed input bounds. The Clipper documentation warns that offsets can produce intermediate values requiring 64-bit multiplication of 64-bit values, which overflows 64-bit integers for large coordinates.

In practice OrcaSlicer limits the build volume, but this is not enforced at the polygon level; there are no asserts guarding overflow. A pathologically large object or a very large offset value (e.g., in support generation) can silently produce wrapped coordinates.

**Translation risk:** Languages that use arbitrary-precision integers by default (Python, Haskell) avoid this; those with fixed-width 32-bit integers (Go's `int32`, some Rust types) must explicitly use `i64` for intermediate results.

**Mitigation:** Add range assertions at Clipper call sites. In the target language, enforce coordinate saturation or use 64-bit coords throughout.

---

## 12. OpenVDB / Eigen Vectorization Dependencies

**Location:**
- `deps/` — OpenVDB, Eigen, NanoVDB
- `AdaptiveCubicInfill.cpp`, `FillLightning.cpp` — use OpenVDB voxel grids
- `TriangleMesh.cpp` — Eigen `Affine3d` transforms, AABB trees
- `IGL/` — libigl for SLA hollowing, mesh repair

**Hazard:** OpenVDB uses its own memory allocator, thread pool, and data layout. Eigen requires 16-byte-aligned memory allocations for SIMD (SSE/AVX). Any code that stores Eigen matrices inside STL containers must use `Eigen::aligned_allocator`; otherwise SIMD operations on misaligned memory cause SIGBUS on older CPUs or silently wrong results on others.

**Translation risk:** Most target languages lack SIMD intrinsics or aligned allocator concepts. The voxel-based algorithms (adaptive cubic, lightning infill) must be rewritten using the target language's geometry/voxel library, not just translated line-by-line.

**Mitigation:** Isolate OpenVDB / Eigen usage to adapter modules. Define clean interfaces (e.g., "voxelize mesh," "flood fill," "compute signed distance field") that can be reimplemented independently.

---

## 13. TBB Scalable Allocator on Points

**Location:**
- [`Point.hpp:56`](../src/libslic3r/Point.hpp#L56) — `using PointsAllocator = tbb::scalable_allocator<BaseType>`
- `Points` type alias uses this allocator globally

**Hazard:** `Points` (the fundamental polygon vertex array) uses TBB's scalable allocator instead of `std::allocator`. This provides significant performance benefits in multi-threaded contexts (thread-local allocation pools, reduced lock contention). However, it creates a dependency on TBB at the type level, not just the algorithm level. Any code that creates a `Points` value implicitly requires TBB to be initialized.

**Translation risk:** In a target language, all polygon vertex arrays must use an equivalent pool-allocated container, or the per-thread allocation advantage is lost, causing performance regressions under parallelism.

**Mitigation:** Benchmark `Points` operations with and without the custom allocator. If the performance difference is significant, the target language must use a slab/arena allocator for vertex data.

---

## 14. Exception-as-Cancellation Protocol

**Location:**
- [`PrintBase.hpp:40-44`](../src/libslic3r/PrintBase.hpp#L40) — `class CanceledException : public std::exception`
- Throughout `TriangleMeshSlicer.cpp`, `TreeSupport.cpp`, `GCode.cpp` — `throw_on_cancel()` lambda calls
- TBB grain boundaries — each `tbb::parallel_for` lambda checks cancellation

**Hazard:** The cancellation protocol uses C++ exception throwing to unwind the call stack from inside a TBB parallel task. This is generally safe in C++ (TBB propagates exceptions from task bodies), but has subtle risks:
1. If a task body catches `std::exception` broadly, it will swallow the cancellation.
2. RAII destructors will run during unwinding, which is correct, but any destructor that calls back into the slicing engine will re-enter an inconsistent state.
3. The background thread's catch block must distinguish `CanceledException` from genuine errors.

**Translation risk:** Languages without exception semantics (Go, Rust without panics) must implement cancellation differently. Languages with async/await must map `throw_if_canceled()` to cooperative await points or cancellation tokens.

**Mitigation:** Replace every `throw_if_canceled()` call site with an explicit `if (is_canceled()) return early_result;` pattern before porting. This eliminates the exception-as-control-flow antipattern.

---

## 15. Multi-Language / Localization Indirection

**Location:**
- `src/slic3r/GUI/I18N.hpp` — `_L()` and `_u8L()` macros wrapping wxWidgets `wxGetTranslation()`
- Config option descriptions throughout `PrintConfig.cpp`
- Error messages in `PrintBase.hpp` `StringObjectException`

**Hazard:** Localizable strings are wrapped in macros. In the GUI, these are runtime-resolved via wxWidgets. In the CLI/library, some strings bypass localization. The two paths are not cleanly separated; `libslic3r` occasionally references GUI translation macros through indirect includes.

**Translation risk:** A port of the core library must either remove all `_L()` calls or provide a stub implementation, or it will fail to compile without the GUI dependency.

**Mitigation:** Audit all `_L()` / `_u8L()` usage in `src/libslic3r/` (core library). Replace with plain string literals or a lightweight i18n abstraction that does not depend on wxWidgets.

---

## 16. ExtrusionEntitiesPtr — Manual Heap Ownership

**Location:**
- [`ExtrusionEntity.hpp:148`](../src/libslic3r/ExtrusionEntity.hpp#L148) — `typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr`
- `ExtrusionEntityCollection.hpp` — owns an `ExtrusionEntitiesPtr entities` member
- `ExtrusionEntityCollection.cpp` — destructor manually deletes all pointers
- `extrusion_entities_append_paths` family — allocate `new ExtrusionPath(...)` and push raw pointer

**Hazard:** `ExtrusionEntityCollection` is the primary owner of all extrusion paths in a layer. It holds a flat `std::vector<ExtrusionEntity*>` and deletes them in its destructor. However:
1. Collections can be nested — an `ExtrusionEntityCollection` can contain another `ExtrusionEntityCollection*` which itself owns children.
2. The `clone()` method returns a raw `new` pointer; the caller must take ownership.
3. The `no_sort` flag on `ExtrusionEntityCollection` affects traversal order but not ownership.

Any code that copies an `ExtrusionEntityCollection` by value will produce a shallow copy of the pointer vector — double-free on destruction.

**Translation risk:** This is one of the most complex ownership patterns in the codebase. In Rust it maps to `Vec<Box<dyn ExtrusionEntity>>`. In Java/Python it maps to a list of heterogeneous objects, with GC handling deletion.

**Mitigation:** Refactor to `std::vector<std::unique_ptr<ExtrusionEntity>>` in C++ before porting.

---

## 17. PrintObjectRegions Reference Counting

**Location:**
- [`Print.hpp:291-310`](../src/libslic3r/Print.hpp#L291) — `PrintObjectRegions` with manual `ref_cnt_inc()` / `ref_cnt_dec()` (where `ref_cnt_dec()` calls `delete this`)

**Hazard:** `PrintObjectRegions` uses manual reference counting with a `delete this` pattern when the count reaches zero. The ref count can only be modified by the main thread (per comment), so it is not atomic. This is correct only as long as the invariant holds. If the object is ever shared across threads (e.g., during `Print::apply()` parallelism), the non-atomic refcount is a data race.

**Translation risk:** Most target languages should replace this with `Arc<T>` (Rust), `shared_ptr<T>` (C++ upgrade), or plain GC references. The `delete this` idiom is not expressible in most non-C++ languages.

**Mitigation:** Replace with `std::shared_ptr<PrintObjectRegions>` in C++ as an intermediate refactoring step.

---

## 18. Two Perimeter Generators (Classic + Arachne)

**Location:**
- `src/libslic3r/PerimeterGenerator.cpp` — classic offset-based generator
- `src/libslic3r/Arachne/` — variable-width perimeter generator (straight skeleton)
- `PrintObject.cpp` — selects between them via `config().wall_generator`

**Hazard:** Both generators exist as completely separate code paths producing the same output type (`ExtrusionEntityCollection`). They share no implementation. The Arachne path is significantly more complex (straight skeleton algorithm, variable-width extrusion paths). Maintaining feature parity between them (e.g., inner/outer/inner ordering, seam placement, overhang detection) is a known maintenance burden.

**Translation risk:** A port must either translate both generators or choose one. Choosing only classic loses variable-width capability; choosing only Arachne loses the simpler codepath. The selection logic in `PrintObject::make_perimeters()` must route correctly.

**Mitigation:** Treat the two generators as separate modules with a common interface. Document the shared contract (inputs: `ExPolygons + config`, outputs: `ExtrusionEntityCollection`) explicitly.

---

## 19. Two Support Generators (Traditional + Tree)

**Location:**
- `src/libslic3r/Support/SupportMaterial.cpp` — traditional pillar/interface supports
- `src/libslic3r/Support/TreeSupport.cpp` + `TreeSupport3D.cpp` — organic tree supports
- `src/libslic3r/Support/TreeModelVolumes.cpp` — collision/avoidance volume caches for tree support

**Hazard:** Tree support is the most computationally complex module in OrcaSlicer. It uses:
- `tbb::concurrent_unordered_map` for thread-safe collision/avoidance caching
- `tbb::task_group` for concurrent avoidance + wall restriction computation
- Manual graph algorithms (MST for branch merging)
- 3D sphere collision meshes

The tree support's `TreeModelVolumes` caches are keyed by `(radius, layer_index)` pairs and invalidated per-object. Incorrect invalidation causes stale collision data and phantom supports.

**Translation risk:** The caching strategy in `TreeModelVolumes` is TBB-specific (`tbb::concurrent_unordered_map`). A port must use thread-safe hashmaps with equivalent invalidation semantics.

**Mitigation:** Profile which cache operations are the bottleneck before choosing a replacement data structure in the target language.

---

## 20. G-code State Machine (GCodeWriter)

**Location:**
- `src/libslic3r/GCode/GCodeWriter.cpp` — maintains current extruder state, position, temperature, fan speed
- `src/libslic3r/GCode.cpp` — orchestrates multi-material moves, wipe tower, seam logic

**Hazard:** `GCodeWriter` is a stateful machine: it tracks the last emitted position (X, Y, Z, E), current extruder index, relative/absolute extrusion mode, and firmware-specific flags. Commands are emitted only when state changes (e.g., `set_speed()` is a no-op if speed is unchanged). This implicit state compression is critical for output file size.

Any translation that emits absolute commands unconditionally will produce valid but bloated G-code. Any translation that misses a state transition (e.g., forgetting to track the last E position) will produce incorrect extrusion amounts.

**Translation risk:** State machines are straightforward to port, but the implicit "emit only on change" pattern must be explicitly modeled, not incidentally preserved.

**Mitigation:** Document all state fields in `GCodeWriter` with their valid ranges and the events that update them. Implement as a proper state machine with explicit transition functions.

---

## 21. Plate / Multi-Instance Shift Arithmetic

**Location:**
- [`Print.hpp:210`](../src/libslic3r/Print.hpp#L210) — `PrintInstance::shift` — world coordinate shift
- [`Print.hpp:211`](../src/libslic3r/Print.hpp#L211) — comment: *"instance_shift is too large because of multi-plate, apply without plate offset"*
- `PrintInstance::shift_without_plate_offset()` — compensates for BBL multi-plate coordinate system

**Hazard:** OrcaSlicer (Bambu fork) has a multi-plate system where each build plate has an offset. `PrintInstance::shift` includes the plate offset; `shift_without_plate_offset()` does not. Different G-code generation paths use one or the other, and using the wrong one silently shifts all geometry by the plate offset (up to hundreds of mm for plate 2+).

**Translation risk:** The distinction between plate-relative and absolute coordinates is implicit in which accessor is called. A port that uses `shift` everywhere will produce correct output for single-plate prints but broken output for multi-plate.

**Mitigation:** Introduce distinct typed coordinate structs: `PlateRelativePoint` and `AbsolutePoint`. Enforce the distinction at type level.

---

## 22. Config Option Key Strings as Identifiers

**Location:**
- `PrintConfig.hpp` — config option keys are string literals (e.g., `"layer_height"`, `"wall_loops"`)
- `ConfigBase::opt<T>(const std::string &key)` — runtime string lookup
- `t_config_option_keys` = `std::vector<std::string>` — used for invalidation sets

**Hazard:** Config option keys are string literals scattered throughout the codebase. Typos in option key strings produce runtime `nullptr` returns rather than compile errors. The invalidation set (which config changes require re-slicing which steps) is defined by string lists, making it easy to miss an option.

**Translation risk:** Any port should use enum or compile-time constant identifiers for config keys, not strings. Preserving string keys for serialization compatibility while using enums internally requires a mapping layer.

**Mitigation:** Generate a config key enum from the option declarations. Use the enum internally; convert to/from strings only at serialization boundaries.

---

## 23. Large Monolithic Files

**Location:**
- `src/OrcaSlicer.cpp` — 7429 lines (entry point + GUI init + CLI + background worker)
- `src/libslic3r/GCode.cpp` — several thousand lines (full G-code generation pipeline)
- `src/libslic3r/Support/TreeSupport.cpp` — ~3500 lines
- `src/libslic3r/Support/TreeSupport3D.cpp` — ~4000 lines
- `src/libslic3r/PrintObject.cpp` — ~2000 lines

**Hazard:** These files mix multiple concerns. `OrcaSlicer.cpp` contains GUI initialization, headless CLI parsing, background thread management, and application lifecycle. `GCode.cpp` contains path planning, multi-material sequencing, wipe tower coordination, and G-code emission. Extracting one concern requires tracing implicit dependencies throughout thousands of lines.

**Translation risk:** A direct file-by-file translation will preserve the monolithic structure in the target language. The port is an opportunity to decompose these, but doing so without understanding all dependencies risks breaking invariants.

**Mitigation:** Before translating each large file, produce a responsibility map: list every distinct concern in the file and draw the dependency graph. Decompose in C++ first via refactoring before porting.

---

## 24. Hidden Floating-Point Precision Contracts

**Location:**
- `TriangleMeshSlicer.cpp` — slice plane intersection uses linear interpolation of `float` vertex coordinates
- `Flow.cpp` — `Flow::mm3_per_mm()` computes extrusion volume from `float` width/height
- `GCode.cpp` — E-axis distances accumulated as `double`

**Hazard:** Mesh slicing uses `float` (32-bit) for vertex coordinates, which gives ~7 decimal digits of precision. At layer heights of 0.1 mm and object sizes of 200 mm, this is ~5 significant digits of precision for edge-plane intersection. The Clipper library uses `int32_t` for polygon coordinates (after scaling to 1e-6 mm units), which provides exact arithmetic for the polygon operations but loses sub-unit precision.

The accumulation of E-axis distances in `GCode.cpp` uses `double`, which is critical — using `float` would cause drift over a long print.

**Translation risk:** Languages that default to `double` everywhere (Java, Python) will give more precision than C++ `float` in mesh math, which may subtly change slice results at degenerate geometry. Languages that default to `float32` may produce worse E-axis drift.

**Mitigation:** Audit every floating-point type choice. Document which operations require `double` (E-axis, length accumulation) and which tolerate `float` (mesh vertex positions, normals).

---

## Summary Risk Matrix

| Hazard | Severity | Difficulty to Port | Priority |
|--------|----------|--------------------|----------|
| Raw-pointer ownership chains | Critical | High | P0 |
| Dual coordinate system | Critical | Medium | P0 |
| ExtrusionEntitiesPtr heap ownership | Critical | High | P0 |
| TBB parallelism model | High | High | P1 |
| Exception-as-cancellation | High | Medium | P1 |
| Shared mutable state / background thread | High | High | P1 |
| Winding-order contracts | High | Low | P1 |
| Implicit polygon closure | High | Low | P1 |
| Virtual dispatch + clone pattern | Medium | Medium | P2 |
| PrintObjectRegions refcount | Medium | Low | P2 |
| g_last_timestamp race | Medium | Low | P2 |
| G-code state machine | Medium | Medium | P2 |
| Plate shift arithmetic | Medium | Low | P2 |
| Config key strings | Medium | Medium | P2 |
| Two perimeter generators | Low | High | P3 |
| Two support generators | Low | High | P3 |
| Template config system | Low | High | P3 |
| cereal serialization | Low | Medium | P3 |
| OpenVDB/Eigen vectorization | Low | High | P3 |
| TBB scalable allocator on Points | Low | Medium | P3 |
| Clipper integer overflow | Low | Low | P3 |
| FP precision contracts | Low | Low | P3 |
| Localization indirection | Low | Low | P3 |
| Large monolithic files | Low | High | P3 |

---

## New Hazards (Sessions 3 & 4)

### 25. `const_cast` UB in ToolOrdering (ToolOrdering.cpp)

**Location:** `src/libslic3r/GCode/ToolOrdering.cpp` — `collect_extruders()`.

**Hazard:** `collect_extruders()` receives a `const Print&` argument but uses `const_cast<Print&>` to call `PrintObject::invalidate_step()`, which mutates state. This is undefined behaviour if the original object was declared `const`. Compilers are permitted to cache reads through `const` references and never observe the mutation.

**Translation risk:** Any language with explicit mutability (Rust, Swift) will reject this pattern at the type level. In C++ the UB is silent.

**Mitigation:** Change `collect_extruders()` to take `Print&` (non-const). Audit all callers.

---

### 26. Extruder ID 0-vs-1 Index Confusion (ToolOrdering.cpp)

**Location:** `src/libslic3r/GCode/ToolOrdering.cpp` — multiple sites in `collect_extruders()`, `fill_wipe_tower_partitions()`, tool-change loops.

**Hazard:** Extruder IDs switch between 0-based (internal array indices) and 1-based (config/display values) without a type-level distinction. Mixing the two produces off-by-one array access or wrong extruder selection.

**Translation risk:** Any port that normalizes to one convention will break callers that assumed the other. The mixing is viral — fixing one site exposes others.

**Mitigation:** Introduce a typed wrapper `ExtruderIdx0` vs `ExtruderIdx1` (or a newtype/enum in the target language) and audit every conversion.

---

### 27. WipeTower2 Global Static Side Effect (WipeTower2.cpp)

**Location:** `src/libslic3r/GCode/WipeTower2.cpp` — `WipeTowerWriter2` constructor.

**Hazard:** `GCodeProcessor::s_IsBBLPrinter = false` is set inside `WipeTowerWriter2`'s constructor. This is a process-wide global static that affects G-code output format for all downstream processing. Any BBL printer that goes through WipeTower2 gets its mode silently overridden.

**Translation risk:** Global mutable statics are a concurrency hazard (not mutex-protected) and make testing impossible without re-initializing global state between tests.

**Mitigation:** Pass printer type as a parameter through the call stack; remove the global static entirely.

---

### 28. WipeTower2 Dry-Run Hazard in `save_on_last_wipe()` (WipeTower2.cpp)

**Location:** `src/libslic3r/GCode/WipeTower2.cpp:save_on_last_wipe()`.

**Hazard:** `save_on_last_wipe()` calls `tool_change()` and `finish_layer()` as a dry run to measure `total_extrusion_length_in_plane()`. The generated G-code strings are discarded, but all writer side effects run (including used-filament accounting, `m_num_tool_changes` increments, etc.). Any new side effect added to these functions will silently corrupt the dry-run accounting.

**Translation risk:** In a port, this pattern is opaque — it looks like normal execution but is actually a measurement pass. Must be refactored into a side-effect-free measurement API.

**Mitigation:** Extract a `measure_layer_extrusion()` function that computes the depth/length without any state mutation.

---

### 29. SeamPlacer `end_index` Comment Inversion (SeamPlacer.cpp)

**Location:** `src/libslic3r/GCode/SeamPlacer.cpp` — `Perimeter::end_index`.

**Hazard:** The field is commented "inclusive!" but all consuming code treats it as exclusive (past-the-end C++ convention). A refactor that takes the comment literally and adjusts loop bounds by ±1 will produce off-by-one seam placement errors.

**Mitigation:** Correct the comment to "exclusive (past-the-end)". Add a unit test covering the boundary case.

---

### 30. SeamPlacer Score Overflow in `spAlignedBack` Mode (SeamPlacer.cpp)

**Location:** `src/libslic3r/GCode/SeamPlacer.cpp` — `spAlignedBack` score override.

**Hazard:** In `spAlignedBack` mode the visibility score is pushed above 1.0 to override other modes. This breaks the [0,1] unit interpretation and implicitly couples `spAlignedBack` to be the highest-priority mode. Any new mode added that also pushes > 1.0 would silently conflict.

**Mitigation:** Replace the score override with an explicit priority/mode enum field in the seam point data structure.

---

### 31. CoolingBuffer G1-Only Removal Bug (Pre-existing) (CoolingBuffer.cpp)

**Location:** `src/libslic3r/GCode/CoolingBuffer.cpp` — G1-line removal logic.

**Hazard:** The removal check scans the entire buffer rather than just the last line, potentially matching and removing incorrect G1 lines. This is a pre-existing PrusaSlicer upstream bug. Under OrcaSlicer's pressure-advance post-processor, the impact may be different from upstream.

**Mitigation:** Scope the check to the last N lines only. Add regression test with a buffer containing multiple G1 lines.

---

### Updated Risk Matrix (Sessions 3 & 4 additions)

| Hazard | Severity | Difficulty to Port | Priority |
|--------|----------|--------------------|----------|
| `const_cast` UB in ToolOrdering | High | Low | P1 |
| Extruder ID 0-vs-1 confusion | High | Medium | P1 |
| WipeTower2 global static side effect | High | Low | P1 |
| WipeTower2 dry-run in save_on_last_wipe | Medium | Medium | P2 |
| SeamPlacer end_index comment inversion | Medium | Low | P2 |
| SeamPlacer spAlignedBack score overflow | Low | Low | P3 |
| CoolingBuffer G1-only removal bug | Medium | Medium | P2 |

---

## New Hazards (Session 5 — SupportMaterial)

### 32. `OverhangCluster` O(N²) Membership Scan (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — `OverhangCluster` struct, `detect_overhangs()`.

**Hazard:** For each new overhang polygon, all existing clusters are scanned linearly to find a matching cluster. On models with many small overhangs (lattice structures, organic shapes), this is O(N²) where N = total overhang polygon count across all layers. For a model with 10,000 overhang polygons, this performs ~50 million comparisons.

**Translation risk:** Any port that naively replicates this scan will have quadratic performance on complex geometries. A spatial index (KD-tree, grid hash) is needed.

**Mitigation:** Replace the linear scan with a spatial hash map keyed by layer index + approximate centroid. O(1) average lookup.

---

### 33. `OverhangCluster` Dangling Raw Pointer (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — `OverhangCluster::expolygons` member.

**Hazard:** `OverhangCluster` stores `ExPolygon*` raw pointers into a `std::vector<ExPolygon>` that was built during `detect_overhangs()`. The vector is not modified after construction (currently safe), but any future change that reallocates this vector (e.g., pushing more elements after cluster construction) would silently invalidate all stored pointers, causing use-after-free.

**Translation risk:** Any language with reference stability guarantees (Rust borrow checker) will catch this; languages with GC (Java, Python, Go) are safe by default. Raw pointer storage is specific to C++.

**Mitigation:** Replace `ExPolygon*` with indices into the source vector, or store copies rather than pointers.

---

### 34. `new_contact_layer()` Nullable Return (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — `new_contact_layer()`.

**Hazard:** `new_contact_layer()` returns `nullptr` when no contact is needed. When `thick_bridges` is enabled, it may return two allocated layers. Callers must null-check but there is no `[[nodiscard]]` or similar enforcement — a caller that forgets the null check will dereference a null pointer.

**Translation risk:** Languages that use `Option<T>` / `Maybe<T>` / nullable types will surface this explicitly. A port should use an optional return type to force callers to handle the no-contact case.

**Mitigation:** Return `std::optional<SupportLayer*>` (or a small `struct` with two optionals for the thick-bridges case).

---

### 35. `buildplate_covered()` Serial FIXME (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — `buildplate_covered()`.

**Hazard:** The cumulative union of build-plate coverage is computed **serially** layer-by-layer. The codebase has a `// FIXME` comment acknowledging this should be a parallel prefix-sum. For tall prints (500+ layers, complex geometry), this serial pass creates a sequential bottleneck that cannot be parallelized by TBB.

**Translation risk:** A port should implement the parallel prefix-sum from the start. The serial version is a known performance debt.

**Mitigation:** Implement as `tbb::parallel_scan` (parallel prefix) with union as the associative operation.

---

### 36. `SupportGridParams::support_closing_radius` Hardcoded (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — `SupportGridParams` constructor; `PrintConfig.hpp` — commented-out field.

**Hazard:** `support_closing_radius` is hardcoded to `2.0` (mm). The corresponding config field in `PrintConfig.hpp` is commented out. This value may be inappropriate for non-standard nozzle diameters (e.g., 0.8 mm nozzle where 2.0 mm closing radius produces overly thick support interfaces). There is no way for users to override this value.

**Translation risk:** A port that copies this hardcode perpetuates an unexposed tuning parameter. The correct fix is to restore the config field.

**Mitigation:** Uncomment the config field, add to invalidation list for `posSupportMaterial`, and expose in UI.

---

### 37. `SUPPORT_USE_AGG_RASTERIZER` Dead Code Path (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — top of file; `SupportGridPattern` class.

**Hazard:** The entire EdgeGrid-based `SupportGridPattern` code path is conditionally compiled out by `#ifdef SUPPORT_USE_AGG_RASTERIZER`, which is unconditionally defined. The EdgeGrid path represents a large body of dead code that is never tested. If the macro is ever removed or conditioned on runtime state, the EdgeGrid path would need independent validation.

**Translation risk:** A port should either delete the dead EdgeGrid path entirely or explicitly mark it as "not ported / not validated."

**Mitigation:** Delete the `#else` branch of `SUPPORT_USE_AGG_RASTERIZER` in `SupportGridPattern`. Document the AGG path as the only supported production code path.

---

### 38. Sharp-Tail Detection Non-Configurable Constants (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — `detect_overhangs()` BBS sharp-tail pass.

**Hazard:** The sharp-tail algorithm uses several magic constants:
- `sharp_tail_min_area = 0.5 mm²` — minimum footprint to trigger sharp-tail detection
- `sharp_tail_max_support_width = 2.5 mm` — maximum bbox dimension for propagation
- `sharp_tail_max_support_height = 16 mm` — maximum height to propagate sharp-tail support
- Area growth rate threshold: 50% per layer

These are not exposed as config options. A model with unusual geometry (very thin tall spires, miniature figurines) may need different thresholds. There is no override path.

**Translation risk:** A port should either expose these as config parameters or document them as algorithm-level constants with justification for the specific values.

**Mitigation:** Move constants to a `SupportMaterialConfig` struct with documented defaults and override hooks.

---

### 39. Commented-Out Perl-Era Dead Code at EOF (SupportMaterial.cpp)

**Location:** `src/libslic3r/Support/SupportMaterial.cpp` — end of file (`clip_by_pillars`, `clip_with_shape`).

**Hazard:** A large block of commented-out C++ code at the end of `SupportMaterial.cpp` represents functions from the original Perl Slic3r codebase that were never ported to the C++ data structures. This code is not compiled, not tested, and not reachable. It creates confusion for maintainers trying to understand the active support algorithm.

**Translation risk:** A naïve line-by-line port might attempt to translate the commented code as if it were active logic.

**Mitigation:** Delete the commented block entirely. The git history preserves the original Perl lineage for reference.

---

### Updated Risk Matrix (Session 5 additions)

| Hazard | Severity | Difficulty to Port | Priority |
|--------|----------|--------------------|----------|
| OverhangCluster O(N²) scan | High | Medium | P1 |
| OverhangCluster dangling raw pointer | High | Low | P1 |
| `new_contact_layer()` nullable return | Medium | Low | P2 |
| `buildplate_covered()` serial FIXME | Medium | Medium | P2 |
| `support_closing_radius` hardcoded | Low | Low | P3 |
| `SUPPORT_USE_AGG_RASTERIZER` dead path | Low | Low | P3 |
| Sharp-tail non-configurable constants | Low | Low | P3 |
| Commented-out Perl-era dead code | Low | Low | P3 |

---

## Session 6 — TreeSupport Hazards

### 40. `SupportNode::diameter_angle_scale_factor` Static — Multi-Object Race Condition

**Location:** `src/libslic3r/Support/TreeSupport.hpp` — `SupportNode` struct.

**Hazard:** `diameter_angle_scale_factor` is declared `static double`. This is a class-wide variable shared across all `SupportNode` instances. If OrcaSlicer ever slices two `PrintObject` instances concurrently with different tree-support angle configurations, whichever thread writes last wins — the other object silently uses the wrong value.

**Translation risk:** A port that parallelizes per-object slicing would trigger this race. The fix is to make this an instance variable or pass it explicitly through the call chain.

**Mitigation:** Convert to a non-static instance member; initialize it in `SupportNode`'s constructor from a config parameter.

---

### 41. `TreeSupportProfiler` File-Scope Global — Not Thread-Safe

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — line ~156 `TreeSupportProfiler profiler;`

**Hazard:** `profiler` is a file-scope (translation-unit global) instance of `TreeSupportProfiler`. Multiple concurrent `PrintObject` slicing operations all write to this same profiler. Timing data from interleaved operations will be corrupted, and any non-atomic write to shared members may produce UB.

**Translation risk:** In any port that parallelizes multi-object slicing, this global must be removed or replaced with per-object instances or TLS.

**Mitigation:** Pass a `profiler` reference as a parameter to `TreeSupport::generate()` or use `thread_local` storage.

---

### 42. `USE_SUPPORT_3D` Macro Hardcoded to 0 — Dead Code Throughout

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — top of file `#define USE_SUPPORT_3D 0`.

**Hazard:** Every `#if USE_SUPPORT_3D` block in the file is permanently dead code. This includes alternate implementations of `get_avoidance()`, `get_collision()`, `get_collision_polys()`, and `SupportNode` usage patterns. The dead branches may contain outdated or incorrect logic that would confuse a port author.

**Translation risk:** A naïve port may translate the dead branches as if they were active alternatives, leading to ambiguity about the correct implementation.

**Mitigation:** Delete all `#if USE_SUPPORT_3D` dead branches before porting. The active `#else` path is the only one that matters.

---

### 43. `insert_dropped_node()` O(N) std::find — O(N²) Hazard on Dense Layers

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — `insert_dropped_node()`.

**Hazard:** `insert_dropped_node()` calls `std::find` to check for duplicates before inserting a node into a `std::vector`. This is O(N) per insertion. `drop_nodes()` calls this for every node on every layer, making the total `drop_nodes()` phase O(L × N²) where N is the number of nodes per layer. For models with many support contacts, this is a significant performance bottleneck.

**Translation risk:** A port that reproduces the same data structure (linear scan vector) will inherit the same quadratic behavior.

**Mitigation:** Replace the `std::vector` + `std::find` combination with an `std::unordered_set` for O(1) duplicate detection.

---

### 44. `calculate_avoidance()` Deep Recursion — Stack Depth Risk

**Location:** `src/libslic3r/Support/TreeSupportData` — `calculate_avoidance()`.

**Hazard:** `calculate_avoidance()` is recursive: to compute avoidance for layer N, it calls itself for layer N-1. The code pre-computes layer N-100 first to cap the recursion depth. However, if pre-computation is bypassed or the cache miss triggers a re-entry, the call stack can grow to depth 100. On platforms with small default stacks (e.g., 1 MB), this is borderline safe at 100 frames but becomes a risk with any additional nesting from callers.

**Translation risk:** Languages with limited stack sizes (e.g., Java default 512 KB, JavaScript) will need this rewritten as an iterative bottom-up computation.

**Mitigation:** Rewrite as iterative, processing layers bottom-up and populating the cache in order.

---

### 45. `m_layer_outlines_below` Serial O(N²) Cumulative Union in Constructor

**Location:** `src/libslic3r/Support/TreeSupportData` constructor.

**Hazard:** `m_layer_outlines_below[i]` = union of all object outlines from layer 0 to layer i. This is computed serially: each layer's entry depends on the layer below. For a 500-layer print, this is 500 union operations in sequence, and each union operation itself is potentially O(P) where P is polygon complexity. The FIXME comment in the code acknowledges this should be a parallel prefix-sum but is not.

**Translation risk:** Identical to the `buildplate_covered()` hazard in `SupportMaterial.cpp` (Hazard 36).

**Mitigation:** Parallel prefix-sum: compute all intermediate unions in O(log N) rounds using a binary-tree reduction.

---

### 46. `config_detect_sharp_tails` Timeout Disables Detection Mid-Parallel-Run

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — `detect_overhangs()`.

**Hazard:** A wall-clock timer runs during sharp-tail detection. If 30 seconds elapse before all layers are processed, `config_detect_sharp_tails` is set to `false` and detection stops. This means:
- Layers processed before the timeout have sharp-tail annotations.
- Layers processed after the timeout do not.
- The output is non-deterministic — the same model on a faster machine may produce different support structures.

**Translation risk:** Any port that reproduces this timeout logic will inherit the same non-determinism. A port that removes the timeout may be significantly slower on complex models.

**Mitigation:** Either remove the timeout and accept the latency, or abort the detection entirely (rather than partial application) when the timeout triggers, then disable sharp-tail for the full run.

---

### 47. `plan_layer_heights()` Negative Sentinel for Multi-Layer Gap

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — `plan_layer_heights()`.

**Hazard:** `node->distance_to_top = -num_layers` is used as a sentinel value indicating that a support node spans multiple adaptive layers (a "gap node"). The field's type is presumably numeric, and negative values encode special semantics. Any consumer of `distance_to_top` that does not explicitly check for negative values will misinterpret gap nodes as nodes at positive layer positions.

**Translation risk:** In a port with a typed enum or domain-restricted integer, this sentinel encoding would be illegal. The semantics must be made explicit.

**Mitigation:** Replace the negative sentinel with a dedicated `bool is_gap_node` field or a `std::optional<int>` distance.

---

### 48. `draw_circles()` Hole-Propagation Loop — Dangling `Polygon*` Keys

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — `draw_circles()` post-main-loop hole-propagation.

**Hazard:** After the main TBB parallel_for loop, a serial hole-propagation loop builds a `std::map<Polygon*, HolePropagationInfo>`. The keys are raw `Polygon*` pointers into vectors that were populated during the main loop. If any of those vectors reallocate between the main loop and the serial loop, the pointer keys dangle. The current code likely avoids this by reserving vector capacity upfront, but this is a fragile invariant with no explicit assertion or comment.

**Translation risk:** A port that modifies the vector population strategy (e.g., uses different append patterns) may silently introduce use-after-free.

**Mitigation:** Replace raw pointer keys with stable identifiers (e.g., layer + node index pair), or restructure to avoid the two-phase pointer pattern entirely.

---

### 49. `get_radius()` Lazy Mutable Cache — Technically UB Data Race

**Location:** `src/libslic3r/Support/TreeSupport.hpp` — `SupportNode::get_radius()`.

**Hazard:** `radius` is declared `mutable`. `get_radius()` checks if `radius == 0`, computes it if so, and stores the result. If two threads simultaneously call `get_radius()` on the same node with `radius == 0`, both compute the same value and write it to the same memory location without synchronization. The value written is always the same (the computation is deterministic), so the result is practically correct. However, concurrent write to a non-atomic variable without synchronization is undefined behavior under the C++ memory model.

**Translation risk:** Languages with strict memory models (Java, Rust) would require either `AtomicDouble` or a mutex here.

**Mitigation:** Add `std::atomic<double>` with a compare-exchange pattern, or use a mutex around the lazy initialization.

---

### 50. `create_node()` Raw Mutex lock/unlock — Exception-Safety Deadlock Risk

**Location:** `src/libslic3r/Support/TreeSupportData::create_node()`.

**Hazard:** `m_mutex.lock()` is called at the top of `create_node()`, and `m_mutex.unlock()` is called at the bottom. If any exception is thrown between the lock and unlock (e.g., from a memory allocation inside the node construction), `unlock()` is never reached, and the mutex remains locked permanently — deadlocking all future threads that try to acquire it.

**Translation risk:** In Rust, this pattern is impossible (RAII enforced). In Go, it is idiomatic to use `defer`. In Java/C#, a `finally` block is required. Any port must address this.

**Mitigation:** Replace with `std::lock_guard<std::mutex> guard(m_mutex);` for RAII-safe automatic unlock.

---

### 51. `move_out_expolys()` Dead Variable `from0`

**Location:** `src/libslic3r/Support/TreeSupport.cpp` — `move_out_expolys()`.

**Hazard:** Local variable `from0` is assigned at the beginning of the function and is never referenced again. It represents dead code — likely a leftover from a refactoring where the logic was changed but the variable was not removed.

**Translation risk:** Minor. A port author may incorrectly infer that `from0` carries semantic meaning and attempt to use it.

**Mitigation:** Delete the `from0` variable. Run with compiler warnings enabled (`-Wunused-variable`) to catch similar issues across the codebase before porting.

---

### Updated Risk Matrix (Session 6 additions)

| Hazard | Severity | Difficulty to Port | Priority |
|--------|----------|--------------------|----------|
| `diameter_angle_scale_factor` static race | High | Low | P1 |
| `insert_dropped_node()` O(N²) hazard | High | Medium | P1 |
| `create_node()` raw lock/unlock deadlock risk | High | Low | P1 |
| `TreeSupportProfiler` global not thread-safe | High | Low | P1 |
| `holePropagationInfos` dangling Polygon* keys | High | Medium | P1 |
| `config_detect_sharp_tails` timeout non-determinism | Medium | Medium | P2 |
| `plan_layer_heights()` negative sentinel | Medium | Low | P2 |
| `calculate_avoidance()` deep recursion | Medium | Medium | P2 |
| `m_layer_outlines_below` serial O(N²) | Medium | Medium | P2 |
| `get_radius()` mutable UB data race | Low | Low | P3 |
| `USE_SUPPORT_3D 0` dead code throughout | Low | Low | P3 |
| `move_out_expolys()` dead variable `from0` | Low | Low | P3 |

---

## TreeSupport3D Hazards (Hazards 52–65)

### Hazard 52 — `discretize_circle()` Degenerate Normal (NaN Vertices)

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3056)
**Severity:** High

```cpp
Vec3f x = normal.cross(Vec3f(0.f, -1.f, 0.f)).normalized();
```

If `normal ≈ (0, ±1, 0)` (a near-vertical branch segment in the XZ plane), the cross product approaches zero. After `normalized()`, the result is NaN or undefined. All circle vertices emitted in the subsequent loop become NaN. The resulting mesh is silently invalid — the slicer may emit empty or garbage cross-sections for that branch, producing missing or malformed support.

**Fix:** Before computing `x`, test `|normal.y| > threshold` and fall back to crossing with `(1, 0, 0)` instead.

---

### Hazard 53 — `discretize_circle()` Zero/Negative Radius

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3051)
**Severity:** Medium

```cpp
float angle_step = 2. * acos(1. - eps / radius);
```

If `radius ≤ 0`, the argument to `acos` goes outside `[-1, 1]`, returning NaN. `nsteps` via `ceil` of NaN is undefined. The subsequent loop body may be skipped entirely, leaving `extrude_branch()` with an empty strip range `{begin, begin}` which causes assertion failures in `triangulate_strip()`.

**Fix:** Assert `radius > 0` at the call site in `extrude_branch()` before each `discretize_circle()` call.

---

### Hazard 54 — `organic_smooth_branches_avoid_collisions()` `min_element_radius` Always Zero

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3211–3212)
**Severity:** Medium

```cpp
// FIXME
l.min_element_radius = 0;
```

The `min_element_radius` field is computed correctly from all elements at that layer, but then unconditionally overwritten to 0 immediately after. `get_collision_lower_bound_area()` is then called with radius 0, fetching the maximally conservative (tightest) avoidance polygon regardless of actual sphere size. The intent was to use the smallest sphere radius at each layer for a tighter fit that would allow more room to route branches. This FIXME has been present since the feature was written.

---

### Hazard 55 — `organic_smooth_branches_avoid_collisions()` Nudge Distance Double-Applied

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3337–3338)
**Severity:** Medium

```cpp
double nudge_dist = std::min(std::max(0., ...), max_nudge_collision_avoidance);
Vec2d nudge_vector = (...).normalized() * nudge_dist;
collision_sphere.position.head<2>() += (nudge_vector * nudge_dist).cast<float>();
```

`nudge_vector` is already `normalized * nudge_dist`. Multiplying it by `nudge_dist` again squares the nudge distance. For a 0.5mm nudge cap, the actual displacement is at most 0.25mm² of movement in the normalized direction — which has no coherent physical meaning. The collision nudge is less effective than designed, potentially requiring more iterations.

**Fix:** Change to either `+= nudge_vector.cast<float>()` or `+= (normalized_dir * nudge_dist).cast<float>()`.

---

### Hazard 56 — `generate_support_infill_lines()` Operator Precedence Bug

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~651, in the infill lines generator)
**Severity:** High

```cpp
(support_params.interface_angle + (layer_idx & 1) ? float(-M_PI/4.) : float(+M_PI/4.))
```

Due to C++ operator precedence, the ternary binds to the entire addition, not just `(layer_idx & 1)`. The actual evaluation is:
```
(support_params.interface_angle + (layer_idx & 1)) ? -π/4 : +π/4
```
Since `interface_angle + 0` or `interface_angle + 1` is almost always non-zero (truthy), the result is always `-π/4`. The `+π/4` branch and `interface_angle` are both completely ignored. Interface fill lines always use a fixed -45° angle.

**Fix:** Add parentheses: `support_params.interface_angle + ((layer_idx & 1) ? float(-M_PI/4.) : float(+M_PI/4.))`.

---

### Hazard 57 — `ensure_maximum_distance_polyline()` O(N²) Inner Loop for Closed Polylines

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~473)
**Severity:** Medium

The function resamples a polyline so no two consecutive points are farther apart than `distance`. For closed polylines, it searches the full polyline for the optimal insertion point, making the inner pass O(N). Combined with the outer O(N) walk, the total is O(N²) for closed polylines. For open polylines it is O(N). For large polygons with many vertices, this is a bottleneck in `generate_initial_areas()`.

---

### Hazard 58 — `group_meshes()` Multi-Group Logic Disabled

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~186)
**Severity:** Low

```cpp
#if 0
    // group objects by similar TreeSupportSettings
#endif
```

The `#if 0` block that groups multiple `PrintObject`s with identical settings (to share influence area computation) is permanently disabled. Each object always gets its own group. For prints with many identical objects, this is a significant missed optimization — all influence area and collision data is recomputed per object.

---

### Hazard 59 — `generate_overhangs()` Entirely Dead Code

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~277)
**Severity:** Low

```cpp
#if 0
static void generate_overhangs(...) { ... }
#endif
```

The overhang detection function in TreeSupport3D is completely disabled. Active code delegates to `TreeSupport::detect_overhangs()` from the non-3D tree support class. The `#if 0` block was presumably a work-in-progress that was superseded.

---

### Hazard 60 — OpenVDB Collision Nudge Path Dead

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3386–3487)
**Severity:** Low

```cpp
#else // TREE_SUPPORT_ORGANIC_NUDGE_NEW
static void organic_smooth_branches_avoid_collisions(...) { /* OpenVDB */ }
#endif
```

The entire OpenVDB-based collision smoothing path is dead code (`TREE_SUPPORT_ORGANIC_NUDGE_NEW=1` at line ~45). The OpenVDB path builds a full signed-distance field from the mesh and uses `ClosestSurfacePoint<FloatGrid>::searchAndReplace()`. It is functionally correct but extremely slow. When porting to another language, this entire `#else` block can be omitted.

---

### Hazard 61 — `convert_lines_to_internal()` Silently Drops Invalid Points

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~371)
**Severity:** Medium

Points in a support contour line that cannot reach either the build plate OR the model are silently dropped, causing the original support line to be split into multiple shorter segments. This can produce support structures with unexpected gaps. No warning or log message is emitted; the caller receives fewer line segments than input without indication of loss.

---

### Hazard 62 — `TreeSupportSettings::soluble` Static Member Race

**File:** `src/libslic3r/Support/TreeSupportCommon.hpp`
**Severity:** High

`TreeSupportSettings::soluble` is declared `static`, meaning it is shared across all instances. In multi-object prints where objects have different `TreeSupportSettings` (e.g., one object uses soluble support, another does not), concurrent access or even sequential construction could overwrite the previous object's setting. Same hazard as `diameter_angle_scale_factor` in `TreeSupport.cpp`.

---

### Hazard 63 — BBS Bed-Area Clip Hack (`generate_support_areas()`)

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3639)
**Severity:** Low

```cpp
// BBS: clip the support areas to the bed area
for (auto& slice : support_layer.support_fills.entities)
    ...
```

All support polygons are clipped to `volumes.m_bed_area` at the end of generation. This is a BBS-specific workaround for issue #4769 where organic support could extend slightly outside the physical bed boundary. The clip is applied unconditionally and can silently truncate support structures for objects placed near the bed edge.

---

### Hazard 64 — `organic_draw_branches()` Tip Roof Extraction Disabled

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3950)
**Severity:** Medium

```cpp
#if 0
    // Extract roof tips (interface layers above contact point)
#endif
```

The code that would generate interface (roof) layers for tip contact points is disabled. All branch tips use the base support material. This means organic tree supports do not benefit from interface layers at contact points, potentially causing worse surface quality at the contact face. The feature was planned but never completed.

---

### Hazard 65 — `extrude_branch()` Zero-Length Segment NaN Direction

**File:** `src/libslic3r/Support/TreeSupport3D.cpp` (line ~3097)
**Severity:** Medium

```cpp
v1 = (p2 - p1).normalized();
```

If two consecutive path elements have identical `result_on_layer` XY AND identical layer Z (i.e., duplicate points), `(p2 - p1)` is the zero vector and `normalized()` returns NaN. The branch normal `nprev = v1` becomes NaN, which propagates through `discretize_circle()`, emitting NaN vertices and producing a fully invisible/broken tube in the mesh. This could occur if `create_nodes_from_area()` places two consecutive nodes at the same position (e.g., due to integer rounding of very small movements).

---

## Summary Table (Hazards 52–65)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 52 | `discretize_circle()` degenerate normal → NaN mesh | TreeSupport3D.cpp:3056 | High | P1 |
| 53 | `discretize_circle()` zero radius → empty ring + assertion | TreeSupport3D.cpp:3051 | Medium | P2 |
| 54 | `min_element_radius` FIXME always 0 | TreeSupport3D.cpp:3212 | Medium | P2 |
| 55 | Nudge distance double-applied (squared) | TreeSupport3D.cpp:3338 | Medium | P2 |
| 56 | Operator precedence bug in `generate_support_infill_lines()` | TreeSupport3D.cpp:651 | High | P1 |
| 57 | `ensure_maximum_distance_polyline()` O(N²) closed polyline | TreeSupport3D.cpp:473 | Medium | P2 |
| 58 | `group_meshes()` multi-group `#if 0` disabled | TreeSupport3D.cpp:186 | Low | P3 |
| 59 | `generate_overhangs()` entirely dead `#if 0` | TreeSupport3D.cpp:277 | Low | P3 |
| 60 | OpenVDB nudge path dead (`#else` branch) | TreeSupport3D.cpp:3386 | Low | P3 |
| 61 | `convert_lines_to_internal()` silently drops points | TreeSupport3D.cpp:371 | Medium | P2 |
| 62 | `TreeSupportSettings::soluble` static race | TreeSupportCommon.hpp | High | P1 |
| 63 | BBS bed-area clip unconditional truncation | TreeSupport3D.cpp:3639 | Low | P3 |
| 64 | Tip roof extraction disabled (`#if 0`) | TreeSupport3D.cpp:3950 | Medium | P2 |
| 65 | `extrude_branch()` zero-length segment → NaN direction | TreeSupport3D.cpp:3097 | Medium | P2 |

---

## Session 8 Hazards — PressureEqualizer + TreeModelVolumes

### Hazard 66 — `m_layer_results` Public Raw Pointer Queue (PressureEqualizer)

**File:** `src/libslic3r/GCode/PressureEqualizer.hpp`
**Severity:** High

`m_layer_results` (a `std::queue<LayerResult*>`) is declared `public` in `GCodePressureEqualizer`. The TBB pipeline stage (`PressureEqualizerFilter`) writes raw pointers into this queue directly from outside the class. There is no ownership contract — the caller is responsible for not double-freeing and for eventually consuming all results. In a port this is a critical encapsulation boundary: the queue must be made private with proper push/pop accessors and ownership semantics must be made explicit (e.g., `unique_ptr` or move-only tokens).

---

### Hazard 67 — `goto single_slope_fallback` in `adjust_volumetric_rate()`

**File:** `src/libslic3r/GCode/PressureEqualizer.cpp` (line ~800)
**Severity:** Medium

The only `goto` in the entire G-code pipeline appears inside `adjust_volumetric_rate()`. It jumps from Case B (accel-peak-decel quadratic solver) backward to the Case C single-slope fallback when the discriminant is negative or the computed peak rate is out of range. While valid C++ and intentionally structured, a `goto` across local variable declarations is extremely difficult to mechanically translate to Go/Rust/Swift (which may not support `goto` at all, or forbid jumping over initializations). Mitigation: refactor into `try_quadratic_solve()` returning `std::optional<double>` and fall through naturally to the linear case.

---

### Hazard 68 — `output_buffer` Never Shrinks (PressureEqualizer)

**File:** `src/libslic3r/GCode/PressureEqualizer.cpp`
**Severity:** Low

`m_output_buffer` uses a power-of-2 resize strategy (`capacity *= 2`) and grows unboundedly across the lifetime of the `GCodePressureEqualizer` object, which spans the entire print. For very large prints with many long layers, this buffer can hold megabytes of data that will never be released until the object destructs. The pattern is a latent memory-growth hazard for port environments where arenas or slabs are not used.

---

### Hazard 69 — `is_just_line_with_extrude_set_speed_tag()` Inverted Empty Check

**File:** `src/libslic3r/GCode/PressureEqualizer.cpp` (line ~630)
**Severity:** Medium

```cpp
if (!line.raw.empty()) return false;  // should be: if (line.raw.empty()) return false;
```

The guard is inverted: a non-empty `raw` string causes early return `false`, but the intent (from context and the tag-check code that follows) is to bail out when the raw string *is* empty. The current code means the tag check is skipped for lines that have non-empty raw data — the exact lines that *would* need the check. This is a latent logic bug that could cause the pressure equalizer to misidentify speed-tag lines and either over- or under-process them. Should be verified against the true intent and fixed.

---

### Hazard 70 — Hardcoded Pipeline Constants in PressureEqualizer

**File:** `src/libslic3r/GCode/PressureEqualizer.cpp`
**Severity:** Low

Three constants are embedded without named symbolic definitions:
- `max_look_back_limit = 128` segments
- `max_ignored_gap = 3.0 mm` (bridging small gaps)
- `NON_TRIVIAL_RATE_DELTA = 10.0 mm³/min`

These directly affect smoothing quality and cannot be tuned by user config. In a port, these should be promoted to named constants or configurable parameters to allow per-material or per-geometry tuning. The 128-segment look-back is particularly arbitrary and could cause under-smoothing on very fine features.

---

### Hazard 71 — `calculateCollision()` `processing_last_mesh` Bug (TreeModelVolumes)

**File:** `src/libslic3r/Support/TreeModelVolumes.cpp` (constructor)
**Severity:** High

When processing a single-mesh print, the variable `processing_last_mesh` is initialized to `false` and only set `true` after the loop body executes — meaning it is never `true` during the loop iteration for the sole mesh. The `anti_overhang` (support-blocker volumes) expansion is gated on `processing_last_mesh`, so **support blockers are silently never applied** in single-mesh prints. Multi-mesh prints are unaffected because the flag becomes `true` on the final iteration. This is a correctness bug, not just a hazard, and likely produces support material inside volumes the user explicitly blocked.

---

### Hazard 72 — `const_cast` Lazy Cache Pattern in TreeModelVolumes

**File:** `src/libslic3r/Support/TreeModelVolumes.cpp`
**Severity:** Medium

All six cache accessors (`getCollision`, `getAvoidance`, `getPlaceableAreas`, `getWallRestriction`, `getCollisionSphere`, `getCollisionHollow`) are declared `const` but use `const_cast<TreeModelVolumes*>(this)->calculate*()` to trigger lazy computation. Each has its own per-cache `std::mutex`. While thread-safe by inspection, this pattern defeats `const`-correctness guarantees: any refactor that passes `const TreeModelVolumes&` will silently allow mutation. A port should use `mutable` fields with `mutable std::mutex` instead, or restructure as an explicit cache manager.

---

## Summary Table (Hazards 66–72)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 66 | `m_layer_results` public raw pointer queue | PressureEqualizer.hpp | High | P1 |
| 67 | `goto single_slope_fallback` in adjust_volumetric_rate() | PressureEqualizer.cpp | Medium | P2 |
| 68 | `output_buffer` never shrinks | PressureEqualizer.cpp | Low | P3 |
| 69 | `is_just_line_with_extrude_set_speed_tag()` inverted empty check | PressureEqualizer.cpp | Medium | P1 |
| 70 | Hardcoded pipeline constants (128, 3mm, 10 mm³/min) | PressureEqualizer.cpp | Low | P3 |
| 71 | `processing_last_mesh` bug — anti_overhang never applied (single mesh) | TreeModelVolumes.cpp | High | P1 |
| 72 | `const_cast` lazy cache pattern defeats const-correctness | TreeModelVolumes.cpp | Medium | P2 |

---

## TriangleMeshSlicer.cpp Hazards (Session 9)

### Hazard 73 — Dual `slice_facet` / `slice_facet_for_cut_mesh` Divergence

**File:** `src/libslic3r/TriangleMeshSlicer.cpp` (~line 216 and ~line 383)
**Severity:** Medium

Two nearly-identical functions handle triangle-plane intersection: `slice_facet()` uses bit-exact float equality (`==`) for vertex-on-plane detection, while `slice_facet_for_cut_mesh()` uses `is_equal()` with a 1e-3 mm epsilon. Both implement the same conceptual algorithm but have diverged in their epsilon handling, edge-ID assignment, and some degenerate-case branches. Any bug fix or behavioural improvement applied to one is very likely to be needed in the other. A refactor should unify them into a single templated or parameterized function with an epsilon policy parameter.

**Mitigation:** Extract shared logic into a single `slice_facet_impl<EpsilonPolicy>()` template. The `cut_mesh()` path uses epsilon for better robustness on real meshes with floating-point near-misses; the normal slicing path uses exact equality for determinism. Both policies should coexist cleanly under a single implementation.

---

### Hazard 74 — Hardcoded 2mm Gap in `chain_open_polylines_close_gaps()`

**File:** `src/libslic3r/TriangleMeshSlicer.cpp` (~line 1407)
**Severity:** Low

The third-pass open-polyline stitcher uses a hardcoded maximum gap of 2mm (passed as `max_gap` from `make_loops()`). This value is not derived from any mesh quality metric or user setting. For very fine-detail meshes or meshes with deliberate thin walls close to but not touching, this may incorrectly bridge across intentional gaps. For very coarse or damaged meshes, 2mm may be insufficient.

**Mitigation:** Expose this as a `MeshSlicingParams` field with a default of 2mm, or derive it from the layer height × a scale factor.

---

### Hazard 75 — `triangulate_slice()` O(N²) Vertex Lookup

**File:** `src/libslic3r/TriangleMeshSlicer.cpp` (`triangulate_slice()`, ~line 2411)
**Severity:** Medium

The triangulation pass inside `triangulate_slice()` performs a 4-pass vertex lookup for each new cap triangle vertex:
1. Linear scan of `section_vertices_map` (O(V_section))
2. Forward scan from `lower_bound` in sorted `map_vertex_to_index` using `is_equal()` (O(V_section) worst case)
3. Backward scan from `lower_bound` (O(V_section) worst case)
4. Linear scan of newly added cap vertices (O(V_cap))

For a mesh cut that produces N intersection vertices, this yields O(N²) per `triangulate_slice()` call. In practice the intersection vertex count is small (typically hundreds), but for meshes with many coplanar faces at the cut height, this degrades significantly.

**Mitigation:** Replace pass 1 with an O(1) hash map. Passes 2/3 could be replaced by a spatial hash or sorted range with true O(log N) lookup once the epsilon comparison is encapsulated.

---

### Hazard 76 — `slice_facet()` Zero-Length Edges From Integer Rounding

**File:** `src/libslic3r/TriangleMeshSlicer.cpp` (`slice_facet()`, ~line 335)
**Severity:** Medium

A `// FIXME` comment at this location acknowledges that when two interpolated intersection points round to the same integer scaled coordinate, a zero-length `IntersectionLine` segment is produced. This degenerate line is passed into `chain_lines_by_triangle_connectivity()`, where it can create a zero-area loop or break the stitching chain. The FIXME has been present since the PrusaSlicer codebase and is not yet resolved. The exact trigger requires two edges of the same triangle to intersect the Z plane at XY positions within 1 scaled unit (1e-6 mm) of each other — extremely rare in practice but possible for very thin triangles.

**Mitigation:** Filter zero-length IntersectionLines immediately after `slice_facet()` returns, before adding to the `IntersectionLines` output vector.

---

### Hazard 77 — Mixed Scaled/Unscaled Z Contract Between Callers

**File:** `src/libslic3r/TriangleMeshSlicer.cpp` (all public API functions)
**Severity:** High

The slicing API has a subtle and undocumented coordinate contract:
- `slice_mesh()` / `slice_mesh_ex()` / `slice_mesh_slabs()` / `project_mesh()`: accept **unscaled Z** (mm float) in their `zs` parameter vectors.
- Inside `slice_facet()`: XY coordinates are **scaled** (`coord_t`, ×1e6), but Z is **unscaled** (mm float).
- Inside `cut_mesh()`: the `z` parameter is **unscaled** mm float.
- `transform_mesh_vertices_for_slicing()`: scales XY only, leaves Z as-is.

This asymmetry means a caller passing a scaled Z value (e.g., `scale_(layer_z)`) to `slice_mesh()` will produce geometrically nonsensical results with no runtime error or assertion. The asymmetry exists for numerical reasons (Z comparisons use float arithmetic with EPSILON, XY uses integer arithmetic for exactness), but it is not enforced by the type system.

**Mitigation:** Introduce strongly-typed wrappers: `ScaledCoord` for XY integer coordinates and `UnscaledZ` (or just `float`) for Z, and enforce at all public API boundaries. Alternatively, document the contract prominently at each function signature with a `// Z is UNSCALED mm float` comment.

---

## Summary Table (Hazards 73–77)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 73 | Dual `slice_facet`/`slice_facet_for_cut_mesh` divergence | TriangleMeshSlicer.cpp | Medium | P2 |
| 74 | Hardcoded 2mm gap in `chain_open_polylines_close_gaps()` | TriangleMeshSlicer.cpp | Low | P3 |
| 75 | `triangulate_slice()` O(N²) vertex lookup | TriangleMeshSlicer.cpp | Medium | P2 |
| 76 | Zero-length edges from integer rounding in `slice_facet()` | TriangleMeshSlicer.cpp | Medium | P2 |
| 77 | Mixed scaled/unscaled Z contract at public API boundaries | TriangleMeshSlicer.cpp | High | P1 |

---

## Session 10 — AvoidCrossingPerimeters.cpp (Hazards 78–80)

---

### Hazard 78 — Dead `#if 0` Alternative Implementation (~400 lines)

**File:** `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` (lines ~1522–1912)
**Severity:** Medium

A large `#if 0` block wraps an entire parallel implementation of the travel-rerouting system:
- A second `avoid_perimeters_inner()` (simpler: no `extend_for_closest_lines` fallback)
- `simplify_travel_heuristics()` — a more aggressive multi-segment shortcut pass not present in the active code
- A second `avoid_perimeters()` that calls `simplify_travel_heuristics` forward then backward
- A second `travel_to()` that uses `Geometry::liang_barsky_line_clipping` for bbox intersection
- A second `init_layer()` that eagerly builds both `m_internal` and `m_external` at layer start (vs the current lazy approach)

Neither version has any comment explaining why one was chosen over the other, or whether the disabled version was intentionally preserved for future use. If the active code is modified (e.g., to fix a routing bug), this block silently diverges further.

**Mitigation:** If the dead code is not planned to be revived, delete it. If it represents a valid alternative to benchmark against, move it to a feature-flagged test path with a clear comment explaining the design tradeoff (eager vs lazy boundary init, heuristic vs greedy simplification).

---

### Hazard 79 — Lazy Boundary Re-Init on Every Out-of-Bounds Travel

**File:** `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` (`travel_to()`, ~lines 1413–1435)
**Severity:** Medium

`travel_to()` lazily initialises `m_internal` (and `m_external`) on the first travel that needs them per layer. However, if start or end falls outside the current `bbox`, the entire boundary is cleared and rebuilt:

```cpp
} else if (!(m_internal.bbox.contains(startf) && m_internal.bbox.contains(endf))) {
    m_internal.clear();
    init_boundary(&m_internal, ...);
}
```

`init_boundary()` calls `get_boundary()` → `inner_offset()` → `variable_offset_inner_ex()` (Clipper), `EdgeGrid::Grid::create()`, and `precompute_polygon_distances()`. This entire pipeline runs again for each bbox miss. On prints with frequent wide-area travel moves — or when `max_travel_detour_distance` causes a fallback that still shifts the bbox — this can trigger repeated expensive recomputations within the same layer.

**Mitigation:** Size the initial bbox generously (e.g., full layer slice extents) so that out-of-bounds misses are rare. Alternatively, expand the bbox incrementally rather than rebuilding from scratch.

---

### Hazard 80 — Hardcoded 1mm EdgeGrid Cell Size

**File:** `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` (`init_boundary()` ~line 1364, `init_layer()` ~line 1518)
**Severity:** Low

All three EdgeGrid instances (`m_internal.grid`, `m_external.grid`, `m_grid_lslice`) are built with a hardcoded cell size of `coord_t(scale_(1.))` — exactly 1mm. Each has an acknowledged `// FIXME 1mm grid?` comment.

- For prints with very fine features (e.g., 0.2mm nozzle, 0.1mm perimeter spacing), 1mm cells are ≈5–10× too coarse, causing each grid cell to contain many boundary edges and making the O(k) `cell_data_range` loop proportionally more expensive.
- For large-format prints (300×300mm+), 1mm cells may be appropriate but the absolute cell count becomes large, increasing memory use.
- The `m_grid_lslice` grid is particularly critical: it is used in `any_expolygon_contains()` on every single travel move.

**Mitigation:** Derive cell size from `perimeter_spacing` (e.g., `max(0.5 * perimeter_spacing, scale_(0.5))`) to adapt to the actual feature size. This is a low-risk change as it only affects query performance, not correctness.

---

## Summary Table (Hazards 78–80)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 78 | Dead `#if 0` alternative implementation (~400 lines) | AvoidCrossingPerimeters.cpp | Medium | P2 |
| 79 | Lazy boundary re-init on every out-of-bounds travel | AvoidCrossingPerimeters.cpp | Medium | P2 |
| 80 | Hardcoded 1mm EdgeGrid cell size | AvoidCrossingPerimeters.cpp | Low | P3 |

---

## GCodeProcessor — Session 12 Hazards (81–95)

These hazards were discovered while completing the annotation pass on `GCodeProcessor.cpp`.

---

### Hazard 81 — `finalize()` Post-Process Atomic Rename: No Backup

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`finalize()` ~line 2870, `run_post_process()` ~line 1118)
**Severity:** High

When `finalize(post_process=true)` is called, `run_post_process()` writes a temporary file then renames it over the original G-code file. This is an atomic rename on POSIX filesystems — but **no backup of the original is kept**. If the process is killed between the write and the rename, or if the rename fails on a cross-device or network filesystem, the original file may be lost or the output may be corrupt.

Additionally, the post-processor rewrites time-estimation comment tags in-place (inserting elapsed time strings). If `calculate_time()` has not been called prior to `run_post_process()`, the replacement values will all be zero.

**Mitigation:** In a refactored implementation, write to a `.tmp` file, verify integrity (file size ≥ original, no truncation), then rename. Keep the original as `.bak` for one generation. Callers must guarantee `calculate_time()` has run before `run_post_process()`.

---

### Hazard 82 — `store_move_vertex()` Invalidates All Move Indices

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`calculate_time()` ~line 6139, `store_move_vertex()` ~line 5841)
**Severity:** High

`calculate_time()` inserts synthetic "actual-speed marker" vertices into `m_result.moves` via `vector::insert()` at arbitrary positions. After each call, every previously obtained index or iterator into `m_result.moves` is invalidated. The insertion updates `TimeBlock.move_id` values through an `id_map`, but any external code holding a cached index or pointer to a MoveVertex will silently use stale data.

This is called both by `simulate_st_synchronize()` (incrementally, on every M-command that drains blocks) and by `finalize()` (once at the end). The incremental calls mean that indices can shift multiple times during parsing — not just once.

**Mitigation:** In a refactored system, either:
1. Defer all synthetic-move insertion to a single post-parse phase (no incremental inserts during parsing), or
2. Use stable handles (e.g., array of unique IDs) rather than positional indices for cross-referencing between moves and blocks.

---

### Hazard 83 — `m_result.moves` Unbounded Memory Growth

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`store_move_vertex()` ~line 5841)
**Severity:** High

`m_result.moves` is a `std::vector<MoveVertex>`. Each `MoveVertex` is ~200 bytes. A large multi-extruder print with 10M line segments produces ~2 GB of move data, all held in a single contiguous heap allocation until `GCodeProcessorResult` is destroyed. There is no streaming, chunked-processing, or eviction mechanism.

The synthetic moves inserted by `calculate_time()` can increase total count by 10–30% above the raw parsed count on prints with many velocity changes.

**Mitigation:** For a refactored system targeting memory-constrained environments, implement a sliding window: process moves in chunks of N, emit completed chunks to disk/GPU, and retain only the look-ahead window needed for trapezoidal planning. The current architecture does not allow this without significant restructuring.

---

### Hazard 84 — `G92 E` vs `G92 X/Y/Z`: Asymmetric Origin Update

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_G92()` ~line 5222)
**Severity:** High

`G92 X/Y/Z` updates `m_origin[axis]` to shift the virtual coordinate system. `G92 E` instead directly sets `m_end_position[E]`, bypassing `m_origin[E]`. This is intentional (prevents float precision drift at large cumulative E values), but it creates an asymmetric implementation: a refactor that naively unifies all axes to use origin shifting will produce wrong E tracking after any G92 E reset — which is emitted at every layer start by OrcaSlicer's own G-code writer.

**Mitigation:** The E axis must be treated as a running accumulator with periodic absolute resets, not as an origin-shifted coordinate. Document this distinction explicitly in the refactored coordinate-tracking data structure.

---

### Hazard 85 — `process_G28()` Re-Parses a Synthetic Raw String

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_G28()` ~line 5192)
**Severity:** Medium

The home handler constructs a raw G-code string (e.g., `"G28 X0  Y0  Z0"`) and re-parses it through a freshly constructed `GCodeReader` to produce a `GCodeLine`. This pattern is fragile: if `GCodeReader::parse_line` changes its whitespace handling or parameter parsing, the synthetic string may not parse as expected. The double-space gap `"X0  Y0"` in the no-axis-specified case is particularly suspicious — it was likely introduced as a workaround and could silently fail with a strict parser.

**Mitigation:** Replace with a direct call to `process_G1()` with a pre-built axes array, eliminating the string round-trip entirely.

---

### Hazard 86 — `process_M204()` T-Parameter Dual Meaning (Legacy vs Modern)

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_M204()` ~line 5464)
**Severity:** Medium

In M204 legacy format (S present), parameter T means **retract acceleration**. In M204 modern format (no S), parameter T means **travel acceleration**. The disambiguation is purely based on whether S is present in the same command.

OrcaSlicer's own G-code writer emits `M204 S<val>` (legacy format). Third-party slicers (e.g., PrusaSlicer in Marlin 2 mode) may emit `M204 P<print> T<travel>` (modern format). If a G-code file mixes both in start/end scripts, the T interpretation will be inconsistent. A refactored parser must document and preserve this context-sensitive parsing.

---

### Hazard 87 — `process_SET_VELOCITY_LIMIT()`: Per-Call Regex Construction

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_SET_VELOCITY_LIMIT()` ~line 5525)
**Severity:** Medium

`process_SET_VELOCITY_LIMIT()` and `process_SET_PRESSURE_ADVANCE()` each construct `std::regex` objects on every function call. For a Klipper print file emitting `SET_VELOCITY_LIMIT` or `SET_PRESSURE_ADVANCE` thousands of times (e.g., every extrusion segment with Pressure Advance enabled), this incurs repeated pattern compilation cost — typically 10–100 µs per regex construction on common hardware.

The three patterns in `process_SET_VELOCITY_LIMIT()` are simple enough to replace with `std::string_view::find` + manual number parsing, which would be 100× faster.

**Mitigation:** Pre-compile all regex patterns to `static const std::regex` members (or equivalent static-local variables). In a non-C++ port, use compiled regex or simple string scanning for these hot paths.

---

### Hazard 88 — `process_G29()` Hard-Coded 260s Bed-Leveling Time

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_G29()` ~line 5146)
**Severity:** Medium

The G29 handler hard-codes a 260-second dwell for BBS printers with an explicit `// Todo: use a machine related setting when we have second kind of BBL printer`. New BBL models (A1, X1E, etc.) may have significantly different bed scan times. The current code emits the same 260s regardless of printer model once `s_IsBBLPrinter` is true.

For non-BBL printers the same 260s is used unconditionally regardless of any machine config, which is almost certainly wrong for RepRapFirmware / Klipper / Duet machines that may have multi-pass mesh leveling routines.

**Mitigation:** Replace with a configurable `bed_leveling_time` machine parameter. Default to 0 (no wait) when the parameter is absent, and make the BBS default explicit.

---

### Hazard 89 — `calculate_time()` O(n²) Synthetic-Move Insertion

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`calculate_time()` ~line 6139)
**Severity:** Medium

The actual-speed move insertion loop calls `result.moves.insert(begin + base_id, ...)` at an arbitrary index inside a `std::vector`. For a vector of N elements, each insert is O(N). If there are M synthetic move groups to insert, total complexity is O(M·N) — quadratic in the number of velocity splits for a large print.

For typical prints this is acceptable (M is small relative to N), but for Klipper prints with per-segment pressure advance changes, M can approach N, making this O(N²).

**Mitigation:** Replace with a single-pass merge: build a new vector by interleaving original moves and synthetic clusters in one O(N) pass, then swap. This is the standard "merge two sorted sequences" approach.

---

### Hazard 90 — `m_extruder_id` Sentinel Relies on `unsigned char` Overflow

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`get_extruder_id()` ~line 6372, `get_filament_id()` ~line 6348)
**Severity:** Medium

`m_extruder_id` is `unsigned char` and uses the value `(unsigned char)(-1)` = `0xFF` = `255` as a sentinel meaning "not yet initialized". The check `m_extruder_id == (unsigned char)(-1)` is correct C++ (both sides are 255u after promotion) but relies on the type being exactly `unsigned char`. If a refactor changes the type to `int` or `uint8_t` with different sign semantics, the uninitialized sentinel must be explicitly updated. Languages without guaranteed unsigned char wrap (e.g., Rust's `u8`) would need an explicit `u8::MAX` constant.

---

### Hazard 91 — `update_slice_warnings()` Duplicate Timelapse Warning Emission

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`update_slice_warnings()` ~line 6241)
**Severity:** Low–Medium

The `NOT_SUPPORT_TRADITIONAL_TIMELAPSE` warning is emitted **twice** with different error codes and severity levels ("10018003" level 2, "1000C003" level 3) in a single code path. This was done for backward compatibility with older A-series firmware. Any downstream parser that deduplicates on the warning message string rather than the error_code will silently drop one of them. Any code that counts warnings will report 2 warnings when logically there is one condition.

**Mitigation:** Use a versioned warning dispatch: if firmware version < threshold, emit old code; if ≥ threshold, emit new code. Never emit both. If both are genuinely required simultaneously by different firmware revisions, document this explicitly in the data model.

---

### Hazard 92 — `M106` 8-bit PWM Assumption

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_M106()` ~line 5275)
**Severity:** Low

Fan speed is converted as `(100.0f / 255.0f) * S_value`, assuming 8-bit PWM (0–255 range). Some firmware (e.g., RepRapFirmware with high-resolution fans, or Klipper with `max_power`) uses 0–1.0 or higher-bit-depth fan control. An S value of 1.0 in Klipper's normalized range would produce ~0.4% fan speed instead of 100%.

**Mitigation:** Gate on firmware flavor: for Klipper, map S 0.0–1.0 → 0–100%; for Marlin/BBS, use 0–255 map. Add a config flag `fan_pwm_bits` for explicit range specification.

---

### Hazard 93 — `finalize()` `gcode_time.cache` Flushed with `ColorChange` Sentinel

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`finalize()` ~line 2870)
**Severity:** Low

The tail-end gcode_time.cache flush in `finalize()` always uses `CustomGCode::ColorChange` as the segment type regardless of the actual last custom event type. For a print that ends after a ToolChange (not a ColorChange), the final time segment will be misclassified as a color-change segment in the `gcode_time.times` list, affecting per-segment time breakdown UI.

**Mitigation:** Track the most recent `CustomGCode::Type` in a member variable and use it as the flush type in `finalize()`.

---

### Hazard 94 — Seam Next-Line-Id Off-by-One in `store_move_vertex()`

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`store_move_vertex()` ~line 5841)
**Severity:** Low

The line_id assignment logic has three cases:
- `Color_change / Pause_Print / Custom_GCode`: `m_line_id + 1` (one ahead)
- `Seam`: `m_last_line_id` (unchanged from previous)
- All others: `m_line_id`

The Seam case reuses `m_last_line_id` rather than advancing, creating a shared line ID between the seam vertex and the preceding move. In the G-code viewer, clicking the seam vertex highlights the same line as the preceding move. This is likely intentional (seams don't have their own G-code line), but the implicit coupling means any refactor that normalizes line IDs will need to explicitly preserve this "seam inherits previous line" behavior.

---

### Hazard 95 — `process_filaments()` Resets Remaining Volume on ToolChange

**File:** `src/libslic3r/GCode/GCodeProcessor.cpp` (`process_filaments()` ~line 6124)
**Severity:** Low

On ToolChange, `process_filaments()` resets `m_remaining_volume[last_extruder_id] = m_nozzle_volume[last_extruder_id]`. This models the nozzle as having a "full" remaining volume after each tool change, seeding the flush-volume FIFO. If the tool change does not actually flush the nozzle to empty (e.g., a direct swap without a purge move), this reset overcounts the flush volume for the next segment and may produce incorrect filament usage statistics.

**Mitigation:** The model is a simplification. A refactored implementation should allow configuring whether a tool change includes a full flush, partial flush, or no flush, and adjust the remaining-volume reset accordingly.

---

## Summary Table (Hazards 81–95)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 81 | `finalize()` post-process no backup before atomic rename | GCodeProcessor.cpp | High | P1 |
| 82 | `calculate_time()` invalidates all move indices | GCodeProcessor.cpp | High | P1 |
| 83 | `m_result.moves` unbounded RAM growth (2GB+ for large prints) | GCodeProcessor.cpp | High | P1 |
| 84 | `G92 E` vs `G92 XYZ` asymmetric origin update | GCodeProcessor.cpp | High | P1 |
| 85 | `process_G28()` re-parses synthetic raw string | GCodeProcessor.cpp | Medium | P2 |
| 86 | `M204 T` dual meaning: retract (legacy S) vs travel (modern P) | GCodeProcessor.cpp | Medium | P2 |
| 87 | Per-call regex construction in SET_VELOCITY_LIMIT / SET_PRESSURE_ADVANCE | GCodeProcessor.cpp | Medium | P2 |
| 88 | Hard-coded 260s G29 bed-leveling dwell time | GCodeProcessor.cpp | Medium | P2 |
| 89 | O(n²) synthetic-move insertion in `calculate_time()` | GCodeProcessor.cpp | Medium | P2 |
| 90 | `m_extruder_id` sentinel relies on unsigned char overflow | GCodeProcessor.cpp | Medium | P2 |
| 91 | Duplicate timelapse warning emission with same message | GCodeProcessor.cpp | Low | P3 |
| 92 | M106 assumes 8-bit PWM (0–255), breaks Klipper normalized range | GCodeProcessor.cpp | Low | P3 |
| 93 | Final gcode_time cache flushed with wrong `ColorChange` type | GCodeProcessor.cpp | Low | P3 |
| 94 | Seam vertex inherits previous line_id silently | GCodeProcessor.cpp | Low | P3 |
| 95 | ToolChange resets nozzle volume to full regardless of actual flush | GCodeProcessor.cpp | Low | P3 |

---

## FanMover Post-Processor Hazards (Hazards 96–110)

### Hazard 96 — `change_axis_value()`: Silent Corruption on Missing Axis

**File:** `src/libslic3r/GCode/FanMover.cpp` (`change_axis_value()` line 71)
**Severity:** Critical

When the target axis letter is not present in the line string, `line.find(match)` returns `std::string::npos` (= SIZE_MAX on 64-bit). Adding 2 wraps around to 1 (`pos = 1`). `line.replace(1, end - 1, ss.str())` then silently overwrites the second character onward of the string with the numeric value, corrupting the G-code line without any assertion, exception, or log message. Callers in `_put_in_middle_G1` / `_print_in_middle_G1` only call `change_axis_value` when `item->dx != 0` etc., so the axis should always be present — but this guarantee is not enforced by any runtime check.

**Mitigation:** Add a `assert(pos != std::string::npos + 2)` check or use a boolean return value to signal failure. In a refactored implementation, parse the G-code into a structured AST before mutation; never do string-replace on raw G-code.

---

### Hazard 97 — `change_axis_value()`: Catastrophic `end` Computation When No Trailing Space/Semicolon

**File:** `src/libslic3r/GCode/FanMover.cpp` (`change_axis_value()` line 81)
**Severity:** High

`end = std::min(line.find(' ', pos+1), line.find(';', pos+1))`. If neither a space nor semicolon follows the numeric value (e.g. the axis is the last token on the line), both finds return `npos` and `std::min(npos, npos) = npos`. Then `line.replace(pos, npos - pos, ss.str())` replaces from `pos` to end-of-string with the new value — coincidentally correct for the last token! However if only one of the two finds returns npos and the other returns a valid position, `std::min` picks the valid position, which is also correct. The dangerous case is if `pos` itself is npos+2 (Hazard 96), in which case `npos - pos` = `npos - (npos+2)` = SIZE_MAX - 1 — an enormous replace length causing heap corruption.

**Mitigation:** Same as Hazard 96 — structured AST approach eliminates this entire class of bug.

---

### Hazard 98 — `get_axis_value()`: Leading-Space Assumption Breaks on Line-Start Axes

**File:** `src/libslic3r/GCode/FanMover.cpp` (`get_axis_value()` line 48)
**Severity:** Medium

The search pattern is `" X"` (space + letter). If the axis value is the very first character of the string (no leading space), `find()` returns npos and `NAN` is returned, silently ignoring the parameter. In normal OrcaSlicer G-code output all axis letters follow the command word with a space (e.g. `G1 X10 Y20`), so this is unlikely — but firmware-generated G-code that OrcaSlicer reads back (e.g. M503 responses) may not follow this convention.

**Mitigation:** Also search for the pattern at position 0 (start of string) or use a proper G-code token parser.

---

### Hazard 99 — Time Estimation Ignores Acceleration; Causes Early Fan Arrival

**File:** `src/libslic3r/GCode/FanMover.cpp` (`_process_gcode_line()` line 296)
**Severity:** Medium

Move time is computed as `time = dist / m_current_speed` using the last-seen F feedrate. This ignores acceleration, deceleration, and junction deviation. For short moves (jerk-limited), actual time is significantly longer than computed. The buffer drains based on this underestimated time, causing fan commands to arrive a few milliseconds early. For most prints the error is acceptable (< 100ms), but at high speeds with frequent direction changes (e.g. infill) the error compounds.

**Mitigation:** A correct implementation would use the same trapezoidal motion profile as GCodeProcessor (with `m_time_processor`), or at minimum a simplified junction-velocity model. Accept the known ±100ms error as a design trade-off and document it.

---

### Hazard 100 — `m_buffer_time_size` Float Drift Corrected Only Per-Chunk, Not Per-Line

**File:** `src/libslic3r/GCode/FanMover.cpp` (`process_gcode()` lines 27-28)
**Severity:** Medium

`m_buffer_time_size` is the running sum of all `BufferData::time` values. It is incremented/decremented by `put_in_buffer`/`remove_from_buffer`. Due to floating-point rounding, after thousands of additions and subtractions the sum drifts. The drift is corrected by recomputing from scratch at the start of each `process_gcode()` call. However within a single call, the assertion `abs(m_buffer_time_size - sum) < 0.01` at line 510 fires in debug builds if drift exceeds 10ms **within** a single chunk. For very long G-code chunks (e.g. flush=true on an end-of-print call) this threshold may be hit, causing debug-build assertion failures.

**Mitigation:** Use `double` (not `float`) for `BufferData::time` and `m_buffer_time_size`. Alternatively, recompute from scratch after every N operations rather than once per chunk.

---

### Hazard 101 — `regex_fan_speed` Dead Member Wastes Resources

**File:** `src/libslic3r/GCode/FanMover.hpp` (line 33)
**Severity:** Low

The `const std::regex regex_fan_speed("S[0-9]+")` member is constructed in the initializer list (compiling the regex at FanMover construction time) but is never referenced in `FanMover.cpp`. The actual fan speed parsing is done by `get_fan_speed()` + `get_axis_value()`. This wastes ~40 bytes of memory per instance and regex compilation time (usually < 1ms but nonzero). In a multi-threaded context where multiple FanMover instances are constructed, this multiplies.

**Mitigation:** Remove `regex_fan_speed` from the class. A future cleanup PR should verify no other translation units reference it.

---

### Hazard 102 — `with_D_option` Dead Configuration Parameter

**File:** `src/libslic3r/GCode/FanMover.hpp` (line 35) and `FanMover.cpp`
**Severity:** Low

`with_D_option` is accepted as a constructor parameter and stored as a `const bool` member, but it is never read anywhere in `FanMover.cpp`. The `D` parameter may have been planned for Duet/RepRap firmware-style M106 (where D sets a ramp duration) but was never implemented. Any caller passing `with_D_option=true` gets no different behaviour from `with_D_option=false`.

**Mitigation:** Either implement the intended D-option behaviour or remove the parameter from the public API and all call sites. The dead parameter creates false expectations for maintainers.

---

### Hazard 103 — `BufferData` Constructor: Dead `line.pop_back()` Does Not Affect `raw`

**File:** `src/libslic3r/GCode/FanMover.hpp` (`BufferData` constructor, line 24)
**Severity:** Low

The constructor initializer list copies `line` into `raw` first, then the constructor body does `if(!line.empty() && line.back() == '\n') line.pop_back()`. The pop operates on the local parameter `line`, NOT on the already-initialized `raw`. So `raw` always retains the trailing `\n` if the caller passed one. When `raw` is written to output, callers unconditionally append `"\n"` — producing double newlines for lines that had a trailing newline. In practice, `GCodeReader::GCodeLine::raw()` does not include a trailing newline, so this bug is dormant but would activate if the calling convention changes.

**Mitigation:** Move the pop-back before the member initialization, or operate on `raw` directly in the body.

---

### Hazard 104 — `is_kickstart` Constructor Parameter Is `float` but Member Is `bool`

**File:** `src/libslic3r/GCode/FanMover.hpp` (`BufferData` constructor, line 24)
**Severity:** Low

The constructor signature is `BufferData(std::string line, float time, int16_t fan_speed, float is_kickstart)` but the member is `bool is_kickstart`. This type mismatch compiles silently (float→bool implicit conversion). Any nonzero float (including 0.001f) sets `is_kickstart = true`, which is coincidentally correct for all current callers passing `true`/`false` (which become 1.0/0.0). A future caller accidentally passing a fractional value would silently get `is_kickstart = true`.

**Mitigation:** Change the constructor parameter type to `bool`.

---

### Hazard 105 — Fan Speed Normalisation: Integer Truncation `100 * S / 255`

**File:** `src/libslic3r/GCode/FanMover.cpp` (`_process_gcode_line()` line 306)
**Severity:** Low

`fan_speed = 100 * fan_speed / 255` uses integer arithmetic. S127 → 49 (not 50), S1 → 0 (rounds to zero, treated as fan-off). Any firmware that produces S values < 3 will have their fan speed silently zeroed, which could suppress a valid low-speed fan command. The normalisation is stored in `m_back_buffer_fan_speed` and `m_front_buffer_fan_speed`, so the 10% kickstart threshold (`fan_speed - 10`) is in these truncated units, not in raw 0–255 units.

**Mitigation:** Use `(100 * fan_speed + 127) / 255` for round-to-nearest, or store raw 0–255 values and compare thresholds in that space. Any refactoring must preserve the exact rounding to avoid changing kickstart trigger conditions.

---

### Hazard 106 — Kickstart Duration Scales Linearly With Speed Delta; Sub-Millisecond at Low Deltas

**File:** `src/libslic3r/GCode/FanMover.cpp` (`_process_gcode_line()` line 336)
**Severity:** Low

`kickstart_duration = kickstart * (fan_speed - m_front_buffer_fan_speed) / 100.f`. For a 1% speed increase, the kickstart is `kickstart / 100` seconds — at a typical kickstart of 0.1s, this is 1ms. A 1ms M106 S255 pulse is below the firmware's PWM update rate and will have no physical effect. The threshold check at line 387 (`fan_speed - m_back_buffer_fan_speed > 10`) partially mitigates this for the non-delay path, but the delay path (lines 325-354) has no such guard and will emit a sub-millisecond kickstart pulse for any positive delta.

**Mitigation:** Add a minimum kickstart duration threshold (e.g. 50ms) below which kickstart is suppressed.

---

### Hazard 107 — `_process_T()`: Malformed Tool Command Resets to Extruder 0

**File:** `src/libslic3r/GCode/FanMover.cpp` (`_process_T()` line 265)
**Severity:** Low

If `parse_number()` fails (e.g. `T!` or `Txx`) and the firmware is not RepRap, `m_currrent_extruder` is reset to 0 as a fallback. This means a corrupt or non-standard tool command silently selects extruder 0, potentially causing FanMover to use the wrong fan command format for subsequent lines on a multi-extruder printer.

**Mitigation:** Leave `m_currrent_extruder` unchanged on parse failure and log a warning. Since `with_D_option` / multi-extruder fan is not yet implemented anyway, this is a dormant bug.

---

### Hazard 108 — `parse_number()` Uses Costly `std::string` Copy from `string_view`

**File:** `src/libslic3r/GCode/FanMover.cpp` (`parse_number()` line 233)
**Severity:** Low

Explicitly documented in the source as "Legacy conversion, which is costly due to having to make a copy of the string before conversion." Constructs a full `std::string` from a `string_view` every call. For a print with millions of tool-change commands this is measurable, though tool changes are rare. The C++17 `std::from_chars` alternative would be zero-allocation and faster.

**Mitigation:** Replace `std::string str{sv}; std::stoi(str, &read)` with `std::from_chars(sv.data(), sv.data() + sv.size(), out)`.

---

### Hazard 109 — Custom G-code Detection Uses `rfind` Not `find` for Substring Check

**File:** `src/libslic3r/GCode/FanMover.cpp` (`_process_gcode_line()` line 424)
**Severity:** Low

```cpp
if (line.raw().rfind("; custom gcode", 0) != std::string::npos)
```
`rfind(pattern, 0)` returns `npos` unless the string **starts with** the pattern (position 0). This is semantically equivalent to `starts_with`. However, the code uses `!= std::string::npos` as the condition, which is correct for this usage — if rfind at pos 0 finds the pattern, it returns 0 (not npos). The variable naming is potentially confusing (`rfind` reads as "search backward" to most readers), but the logic is correct. The nested check `rfind("; custom gcode end", 0)` must match first and takes priority, so "custom gcode end" correctly disables the flag.

**Mitigation:** Replace with `line.raw().starts_with("; custom gcode")` (C++20) for clarity. No functional change needed.

---

### Hazard 110 — `only_overhangs` Role Detection Depends on `;TYPE:` Comment Contract

**File:** `src/libslic3r/GCode/FanMover.cpp` (`_process_gcode_line()` line 418)
**Severity:** Medium

When `only_overhangs = true`, the fan delay is only active while `current_role == erOverhangPerimeter`. Role is tracked via `;TYPE:<role>` comments injected by the G-code generator. If:
- The generator stops emitting `;TYPE:` comments (e.g. when spaghetti detection is disabled)
- The role string changes (e.g. a refactor renames `"Overhang perimeter"`)
- The comment is emitted after the first extrusion line rather than before

...then `current_role` will be stale and the fan delay will either always fire or never fire. There is no compile-time or runtime check that the comment contract is maintained.

**Mitigation:** Define the role comment string as a named constant shared between the G-code generator and FanMover. Add a regression test that checks FanMover correctly fires only on overhang sections.

---

## Summary Table (Hazards 96–110)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 96 | `change_axis_value()` wraps npos+2, silently corrupts line | FanMover.cpp | Critical | P1 |
| 97 | `change_axis_value()` catastrophic replace if no trailing space/semicolon | FanMover.cpp | High | P1 |
| 98 | `get_axis_value()` misses axis at position 0 (no leading space) | FanMover.cpp | Medium | P2 |
| 99 | Time estimation ignores acceleration; fan arrives slightly early | FanMover.cpp | Medium | P2 |
| 100 | `m_buffer_time_size` float drift only corrected per-chunk | FanMover.cpp | Medium | P2 |
| 101 | `regex_fan_speed` dead member compiled but never used | FanMover.hpp | Low | P3 |
| 102 | `with_D_option` stored but never read; dead API | FanMover.hpp | Low | P3 |
| 103 | `BufferData` constructor `pop_back` operates on copy, not `raw` | FanMover.hpp | Low | P3 |
| 104 | `is_kickstart` constructor parameter is `float` but member is `bool` | FanMover.hpp | Low | P3 |
| 105 | Fan speed truncation `100*S/255` zeroes S < 3; shifts thresholds | FanMover.cpp | Low | P3 |
| 106 | Kickstart duration sub-millisecond for small speed deltas | FanMover.cpp | Low | P3 |
| 107 | Malformed `Tnn` resets extruder to 0 silently | FanMover.cpp | Low | P3 |
| 108 | `parse_number()` copies string_view; should use from_chars | FanMover.cpp | Low | P3 |
| 109 | `rfind(pattern, 0)` idiom confusing; should be `starts_with` | FanMover.cpp | Low | P3 |
| 110 | `only_overhangs` role detection relies on `;TYPE:` comment contract | FanMover.cpp | Medium | P2 |
| 111 | CSV silent discard on any parse exception — no partial recovery | AdaptivePAInterpolator.cpp | Critical | P1 |
| 112 | CSV column order (PA, flow, accel) undocumented; wrong order poisons model silently | AdaptivePAInterpolator.cpp | High | P1 |
| 113 | `accel_value` is `unsigned int`; populated via `std::stod()` — silent truncation of fractional values | AdaptivePAProcessor.cpp | High | P1 |
| 114 | `m_next_feedrate = 0` reset in outer loop is fragile; any added `continue` carries stale feedrate | AdaptivePAProcessor.cpp | Medium | P2 |
| 115 | `operator()` reconstructs `PchipInterpolatorHelper` (accel axis) on every call — repeated allocation | AdaptivePAInterpolator.cpp | Medium | P2 |
| 116 | Return value `-1.0` sentinel for failure — not type-safe; should be `std::optional<double>` | AdaptivePAInterpolator.cpp | Medium | P2 |
| 117 | Bridge PA override is hard-replacement (`0.0`), not a blend with interpolated value | AdaptivePAProcessor.cpp | Medium | P2 |
| 118 | Partially-parsed CSV line (1 or 2 of 3 fields read) contributes flowRate=0 or accel=0 entry | AdaptivePAInterpolator.cpp | High | P1 |
| 119 | Lookahead scan in `process_layer()` is O(n²) for streams with many zero-feedrate lines | AdaptivePAProcessor.cpp | Medium | P2 |
| 120 | Single-acceleration model skips accel interpolation and returns flow-only result — undocumented shortcut | AdaptivePAInterpolator.cpp | Low | P3 |
| 121 | `m_isInitialised` has no mutex; concurrent read during reparse would data-race | AdaptivePAInterpolator.hpp | Low | P3 |
| 122 | PCHIP requires sorted inputs; inner (flow_rate, PA) pairs per acceleration group are unsorted | AdaptivePAInterpolator.cpp | High | P1 |
| 123 | `std::map<double>` keyed on floating-point acceleration — equality-sensitive; rounding in caller can miss bucket | AdaptivePAInterpolator.cpp | Medium | P2 |
| 124 | `std::round(x * 1000.0) / 1000.0` rounding near x.0005 boundaries is non-deterministic in IEEE 754 | AdaptivePAInterpolator.cpp | Low | P3 |
| 125 | `accelerations_` vector is a redundant parallel structure to `flow_interpolators_` keys — drift risk | AdaptivePAInterpolator.hpp | Low | P3 |
| 126 | Triple-offset in generate() destroys features < ~11500nm before SkeletalTrapezoidation | WallToolPaths.cpp | High | P1 |
| 127 | `transition_filter_dist` hardcoded to 100mm — may suppress all wall-count transitions on small parts | WallToolPaths.cpp | High | P1 |
| 128 | `contour_paths` vector allocated and reserved in separateOutInnerContour() but never populated — dead code | WallToolPaths.cpp | Low | P3 |
| 129 | `removeEmptyToolPaths()` return value: true = toolpaths IS empty (inverted / non-intuitive semantics) | WallToolPaths.cpp | Medium | P2 |
| 130 | `is_top_or_bottom_layer` hardcoded false in `make_paths_params()`; caller must manually set for top/bottom layers | WallToolPaths.cpp | Medium | P2 |
| 131 | `outline` stored as `const Polygons&` — dangling reference if caller's polygon is a temporary or goes out of scope | WallToolPaths.hpp | High | P1 |
| 132 | `simplify()` uses int64_t area accumulation across many removed vertices — overflow risk for pathological inputs | WallToolPaths.cpp | Low | P3 |
| 133 | `removeColinearEdges()` can introduce new self-intersections; requires second `fixSelfIntersections()` pass | WallToolPaths.cpp | Medium | P2 |
| 134 | `generateToolpaths()` sorted assertion may fire if post-processing reorders insets | WallToolPaths.cpp | Medium | P2 |
| 135 | `min_feature_size` and `min_bead_width` are float→scaled coord_t; sub-1nm values silently become 0 | WallToolPaths.cpp | Medium | P2 |
| 136 | `stitchToolPaths`: `stitch_distance = bead_width_x - 1`; if bead_width_x is 0, distance = -1 (UB in PolylineStitcher) | WallToolPaths.cpp | High | P1 |
| 137 | `fixSelfIntersections(epsilon < 1)` uses Clipper pftEvenOdd without point-nudging step | WallToolPaths.cpp | Low | P3 |
| 138 | `getRegionOrder()` uses SparsePointGrid not SparseLineGrid — adjacency constraints can be missed for simplified insets | WallToolPaths.cpp | Medium | P2 |
| 139 | `separateOutInnerContour()` classifies entire inset based only on first junction of first line | WallToolPaths.cpp | Medium | P2 |
| 140 | `make_paths_params()` uses min_nozzle_diameter for ALL Arachne parameters in multi-nozzle setups | WallToolPaths.cpp | Medium | P2 |

---

## Session 16 Hazards: SkeletalTrapezoidation.cpp (Hazards 141–175)

| # | Hazard | File | Severity | Priority |
|---|--------|------|----------|----------|
| 141 | `filterCentral()` overload has tautological always-false condition: `edge.to->isLocalMaximum() && !edge.to->isLocalMaximum()` — recursive filter body is dead code; inherited bug from CuraEngine | SkeletalTrapezoidation.cpp | High | P1 |
| 142 | `dissolveNearbyTransitions()` recursion depth unbounded; up to ~500 levels (100mm / 0.2mm) — stack overflow on complex, fine-grained meshes | SkeletalTrapezoidation.cpp | High | P1 |
| 143 | `transition_filter_dist` = 100mm hardcoded in WallToolPaths::generate() — dissolves nearly all transitions on small/complex parts | WallToolPaths.cpp / SkeletalTrapezoidation.cpp | High | P1 |
| 144 | `filterNoncentralRegions()` max_dist hardcoded to 0.4mm — not scaled by nozzle size; may over-filter for large nozzles | SkeletalTrapezoidation.cpp | Medium | P2 |
| 145 | `beading_strategy` stored as `const BeadingStrategy&` — dangling reference if caller destroys strategy before SkeletalTrapezoidation | SkeletalTrapezoidation.hpp | Critical | P0 |
| 146 | `p_generated_toolpaths` is a raw non-owning pointer set in `generateToolpaths()` — null-deref if accessed before `generateToolpaths()` or after caller destroys the vector | SkeletalTrapezoidation.hpp | Critical | P0 |
| 147 | `SKELETAL_TRAPEZOIDATION_BEAD_SEARCH_MAX` = 1000 cap in `getNearestBeading()` — silent fallback to fresh beading beyond this creates zero-width gap in toolpaths | SkeletalTrapezoidation.cpp | High | P1 |
| 148 | `vd_edge_to_he_edge` / `vd_node_to_he_node` maps persist as class members but become stale after `constructFromPolygons()` completes — accidental use post-construction causes corrupt lookup | SkeletalTrapezoidation.cpp | Medium | P2 |
| 149 | `emplace_back()` vs `emplace_front()` inconsistency in list ordering between `separatePointyQuadEndNodes()` and `makeNode()` — safe due to std::list stability but semantically surprising | SkeletalTrapezoidation.cpp | Low | P3 |
| 150 | `generateTransitionEnd()`: mixed float × int64_t in `transition_mid_position * int64_t(transition_length)` — result narrowed to coord_t; potential truncation for large transition lengths | SkeletalTrapezoidation.cpp | Medium | P2 |
| 151 | `generateTransitionEnd()`: `(end_pos - ab_size) / (start_pos - end_pos)` — division by zero if start_pos == end_pos (zero-length transition) | SkeletalTrapezoidation.cpp | High | P1 |
| 152 | `isGoingDown()` source comment explicitly acknowledges "logic is not fully thought through" and doesn't account for transition mids on intermediate edges | SkeletalTrapezoidation.cpp | Medium | P2 |
| 153 | `normal()` static helper: returns `Point(len, 0)` when input vector is near-zero (< 1 unit) — degenerate +X fallback; causes misplaced transition nodes on ultra-short edges | SkeletalTrapezoidation.cpp | Medium | P2 |
| 154 | `applyTransitions()`: `snap_dist()` threshold silently discards transition-end nodes too close to existing topology — transitions near corners are silently dropped | SkeletalTrapezoidation.cpp | Medium | P2 |
| 155 | `generateExtraRibs()`: iterates `graph.edges` while `insertNode()` modifies graph.edges — relies on std::list iterator stability; newly added edges are visited but harmlessly skipped | SkeletalTrapezoidation.cpp | Low | P3 |
| 156 | `generateSegments()` / `propagateBeadingsDownward()`: `beading_propagation_transition_dist` is a class-level constant; not user-configurable | SkeletalTrapezoidation.hpp | Low | P3 |
| 157 | `propagateBeadingsDownward(edge_t*)`: `merged_beading.total_thickness == distance_to_boundary * 2` assertion can fail due to floating-point drift in `interpolate()` | SkeletalTrapezoidation.cpp | Medium | P2 |
| 158 | `interpolate(left, ratio, right, switching_radius)`: re-interpolation with `new_ratio + 0.1` overshoot — clamped by min(1.0, ...) but creates non-linear jump in bead widths | SkeletalTrapezoidation.cpp | Medium | P2 |
| 159 | `interpolate()` two-argument overload: beads from the LARGER Beading beyond the shared indices are left at their original values — discontinuous width at blend boundary | SkeletalTrapezoidation.cpp | Medium | P2 |
| 160 | `generateJunctions()`: integer rounding in junction position: `ab * int64_t(bead_R - start_R) / int64_t(end_R - start_R)` — protected by early-continue but edge case for flat edges | SkeletalTrapezoidation.cpp | Low | P3 |
| 161 | `generateJunctions()`: "snap to start node" at 0.005 mm can make multiple junctions coincide on short edges — zero-length downstream segments | SkeletalTrapezoidation.cpp | Medium | P2 |
| 162 | `getOrCreateBeading()`: `bead_count == -1` degenerate case — nearest beading fallback can return a beading computed for a different R value, creating width mismatch | SkeletalTrapezoidation.cpp | High | P1 |
| 163 | `getNearestBeading()`: local `priority_queue<DistEdge>` can grow to O(graph_edges) for sparse skeletons — unbounded memory on pathological inputs | SkeletalTrapezoidation.cpp | Medium | P2 |
| 164 | `addToolpathSegment()`: reverse-continue path for CCW-wound even walls logs error but continues — incorrect polygon winding order persists in output | SkeletalTrapezoidation.cpp | High | P1 |
| 165 | `connectJunctions()`: do-while with comma-operator relies on `getNextUnconnected()` never returning null in valid DCEL — malformed DCEL causes null-deref | SkeletalTrapezoidation.cpp | Critical | P0 |
| 166 | `connectJunctions()`: mismatched from/to junction sizes (diff > 1) are logged but not corrected — broken extrusion paths in output | SkeletalTrapezoidation.cpp | High | P1 |
| 167 | `connectJunctions()`: `from_junctions` / `to_junctions` copied by value per quad — O(beads × quads) allocation overhead for high bead-count parts | SkeletalTrapezoidation.cpp | Medium | P2 |
| 168 | `generateLocalMaximaSingleBeads()`: hexagon dot uses float sin/cos — accumulated rounding on scaled integer coordinates | SkeletalTrapezoidation.cpp | Low | P3 |
| 169 | `generateLocalMaximaSingleBeads()`: writes open ExrusionLine directly to generated_toolpaths bypassing addToolpathSegment() — inconsistent line ownership / closure semantics | SkeletalTrapezoidation.cpp | Low | P3 |
| 170 | `getQuadMaxRedgeTo()`: fallback `ret = ret->prev` uses 0.005 mm epsilon — workaround for float near-equality at flat quad tops; may misidentify peak on legitimate near-flat edges | SkeletalTrapezoidation.cpp | Medium | P2 |
| 171 | `propagateBeadingsUpward()` uses `is_upward_propagated_only = true` flag to indicate unresolved beadings — no assertion that this flag is cleared before generateJunctions; could produce phantom beadings | SkeletalTrapezoidation.cpp | Medium | P2 |
| 172 | `generateSegments()` beading sort: two flat edges with equal `dist_to_go_up` values use `optional::value_or(max)` — std::numeric_limits<coord_t>::max() in subtraction can overflow | SkeletalTrapezoidation.cpp | Medium | P2 |
| 173 | `discretize()`: `max_angle` and `discretization_step_size` both used as thresholds but have no joint validation — inconsistent precision on large-radius Voronoi arcs | SkeletalTrapezoidation.cpp | Low | P3 |
| 174 | `constructFromPolygons()`: Voronoi diagram built from integer coords but `discretize()` uses float sin/cos — mixed-precision coordinate system throughout the pipeline | SkeletalTrapezoidation.cpp | Medium | P2 |
| 175 | `generateTransitionMids()` uses `coord_t` for `lower_bead_count` (semantically a count, not a length) — misleading type; in refactor use a distinct integral type | SkeletalTrapezoidation.cpp | Low | P3 |

## Session 17 — BeadingStrategy Module (all files)

| ID | Description | File | Severity | Priority |
|----|-------------|------|----------|----------|
| 176 | `BeadingStrategy::getTransitionAnchorPos()` divides by `(upper_optimum - lower_optimum)`; if `optimal_width == 0` this is division by zero producing NaN/inf — guarded only by assumption of positive optimal_width | BeadingStrategy.cpp | High | P1 |
| 177 | `BeadingStrategy::getTransitioningLength()` returns `scaled<coord_t>(0.01)` (10 µm) for `lower_bead_count == 0` as a division-by-zero guard — this magic constant is not documented or parameterised | BeadingStrategy.cpp | Low | P3 |
| 178 | `BeadingStrategy::getTransitionThickness()` hysteresis: split/add threshold asymmetry prevents bead-count oscillation, but if both thresholds are set to 0.0 or 1.0 the hysteresis collapses and the system can oscillate | BeadingStrategy.cpp | Medium | P2 |
| 179 | `DistributedBeadingStrategy` ctor: when `distribution_radius == 1`, `(r-1)² == 0`, guarded by `>= 2` branch — silently falls back to uniform distribution (1/1²); callers passing `r=1` get unexpected behaviour | DistributedBeadingStrategy.cpp | Medium | P2 |
| 180 | `DistributedBeadingStrategy::compute()` for `bead_count > 2`: last bead absorbs all integer rounding (`width = thickness - accumulated_width`), meaning the innermost bead can differ from its weight-computed value by ±1 nm — toolpath_locations also off by ±1 | DistributedBeadingStrategy.cpp | Low | P3 |
| 181 | `DistributedBeadingStrategy::compute()` assert at line ~88 uses `std::as_const` in a lambda — this is valid C++17 but some compilers/LSPs flag it as `std::is_const`; assert fires in debug only | DistributedBeadingStrategy.cpp | Low | P3 |
| 182 | `DistributedBeadingStrategy::compute()`: negative `to_be_divided` (thickness < count * optimal_width) can produce negative bead widths when weights are large — callers must pre-validate via `LimitedBeadingStrategy` to avoid degenerate output | DistributedBeadingStrategy.cpp | High | P1 |
| 183 | `LimitedBeadingStrategy` ctor warns on odd `max_bead_count` but does NOT reject it — odd counts produce asymmetric 0-width sentinel insertion at `max_bead_count/2` (integer division off-centre) | LimitedBeadingStrategy.cpp | Medium | P2 |
| 184 | `LimitedBeadingStrategy::compute()` for `bead_count > max_bead_count + 1`: the overflow path is only guarded by `assert` (debug-only) + log warning — in release builds, proceeds with `max_bead_count + 1` as if it were the cap+1 case, producing garbled geometry | LimitedBeadingStrategy.cpp | High | P1 |
| 185 | `LimitedBeadingStrategy::compute()`: two 0-width sentinels are inserted at `innermost_toolpath_location ± innermost_toolpath_width / 2` — integer division truncates for odd widths, sentinel positions off by ±1 nm | LimitedBeadingStrategy.cpp | Low | P3 |
| 186 | `LimitedBeadingStrategy::getOptimalThickness()` returns `scaled<coord_t>(1000.)` (1 m) for `bead_count > max_bead_count` — sentinel value will propagate silently into any geometry computation that uses this thickness without bounds-checking | LimitedBeadingStrategy.cpp | High | P1 |
| 187 | `LimitedBeadingStrategy::getTransitionThickness()` returns `parent->getOptimalThickness(max+1) - scaled<coord_t>(0.01)` — the hardcoded -10 µm offset is scale-invariant but NOT unit-invariant; must be preserved verbatim in any refactor | LimitedBeadingStrategy.cpp | Medium | P2 |
| 188 | `LimitedBeadingStrategy::getTransitionThickness()` returns `scaled<coord_t>(900.)` (0.9 m) as assert-false sentinel — same propagation risk as hazard 186 | LimitedBeadingStrategy.cpp | Medium | P2 |
| 189 | `RedistributeBeadingStrategy::compute()`: `inner_bead_count = bead_count - 2` without underflow guard; for `bead_count == 1` this gives `-1` but is safe because the `inner_bead_count > 0` guard blocks the parent call — subtle, relies on signed arithmetic | RedistributeBeadingStrategy.cpp | Low | P3 |
| 190 | `RedistributeBeadingStrategy::compute()`: `actual_outer_thickness = thickness / bead_count` for `bead_count <= 2` — integer division; for odd thickness + `bead_count == 2`, result truncates, `left_over` accumulates 1 nm rounding | RedistributeBeadingStrategy.cpp | Low | P3 |
| 191 | `RedistributeBeadingStrategy::getTransitionThickness()` case 1 uses `parent->getSplitMiddleThreshold()` — this calls the BASE class getter, not a virtual. If a decorator modifies wall_split_middle_threshold without overriding getSplitMiddleThreshold(), the wrong threshold is used | RedistributeBeadingStrategy.cpp | Medium | P2 |
| 192 | `WideningBeadingStrategy::compute()`: bead width may be set to `min_output_width` even when `thickness < min_output_width` — deliberate over-extrusion; any downstream width validation that asserts `bead_width <= thickness` will fire | WideningBeadingStrategy.cpp | Medium | P2 |
| 193 | `WideningBeadingStrategy::getNonlinearThicknesses()`: unconditionally prepends `min_output_width` for ALL `lower_bead_count` values — intended only for count==0 (thin-wall zone); for count > 0 this inserts spurious support ribs | WideningBeadingStrategy.cpp | Medium | P2 |
| 194 | `WideningBeadingStrategy`: mismatch between `compute()` intercept threshold (`thickness < optimal_width`) and `getTransitionThickness(0)` = `min_input_width` — if `min_input_width > optimal_width`, count transition fires before widening logic; combination is not expected in practice but not validated | WideningBeadingStrategy.hpp | Medium | P2 |
| 195 | `OuterWallInsetBeadingStrategy` name BUG: `name = "OuterWallOfsetBeadingStrategy"` (missing 'f' in "Offset") — same typo in `toString()`. Pre-existing upstream bug. Any serialisation / log parsing that depends on this string will carry the typo | OuterWallInsetBeadingStrategy.cpp | Low | P3 |
| 196 | `OuterWallInsetBeadingStrategy::compute()`: only `toolpath_locations[0]` (outer wall, one side) is shifted — the opposite outer wall is NOT mirrored, producing intentional asymmetry. A refactor that mirrors both sides would change print quality | OuterWallInsetBeadingStrategy.cpp | Medium | P2 |
| 197 | `OuterWallInsetBeadingStrategy::compute()`: zero-width bead filter (`count_if(width > 0)`) is defensive dead code in the current stack order (Limited is outermost, not innermost) — a stack reorder would silently activate it | OuterWallInsetBeadingStrategy.cpp | Low | P3 |
| 198 | `OuterWallInsetBeadingStrategy::compute()`: clamp `std::min(..., thickness / 2)` — integer division; for odd thickness the clamp is 1 nm inside true midpoint | OuterWallInsetBeadingStrategy.cpp | Low | P3 |
| 199 | `BeadingStrategyFactory::makeStrategy()`: SPECIAL CASE `max_bead_count <= 2` uses `preferred_bead_width_outer` as `optimal_width` for `DistributedBeadingStrategy` — this case MUST be preserved; removing it causes width mismatch in single/double-wall parts | BeadingStrategyFactory.cpp | High | P1 |
| 200 | `BeadingStrategyFactory::makeStrategy()`: stack order is fixed at compile time — no runtime configuration of decorator order. A refactor to a configurable pipeline must preserve: Distributed → Redistribute → [Widening] → [OuterWallInset] → Limited | BeadingStrategyFactory.cpp | High | P1 |
| 201 | `BeadingStrategyFactory::makeStrategy()`: `outer_wall_offset != 0` (not `> 0`) activates `OuterWallInsetBeadingStrategy` — Orca extension for negative offsets; upstream CuraEngine only supports positive inset. Any port to upstream must gate on `> 0` | BeadingStrategyFactory.cpp | Medium | P2 |
| 202 | `BeadingStrategyFactory::makeStrategy()`: `LimitedBeadingStrategy` warns on odd `max_bead_count` but factory does not validate or clamp — caller responsibility to pass even count is undocumented | BeadingStrategyFactory.cpp | Low | P3 |
| 203 | All BeadingStrategy decorators copy base-class fields via `BeadingStrategy(*parent)` copy-constructor — if a decorator changes a field after construction (e.g. sets `name`), those changes are local only to that decorator level; sub-decorators won't see the updated field | BeadingStrategy.hpp | Low | P3 |
| 204 | `BeadingStrategyPtr = unique_ptr<BeadingStrategy>`: move-only semantics throughout the stack — any code that copies a strategy (e.g. for undo/redo or config snapshots) must use the copy constructor, which only copies the BASE fields, not derived fields | BeadingStrategy.hpp | Medium | P2 |

---

## Session 18 — SkeletalTrapezoidationGraph.cpp (graph mutation layer)

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 205 | `STHalfEdge::canGoUp()` and `distToGoUp()` recurse through equidistant-edge fans with no cycle guard and no depth limit. A cycle of equidistant edges (possible after `collapseSmallEdges` on degenerate input) causes infinite recursion. Refactor MUST add a visited-set or convert to iterative BFS with a cycle-break | SkeletalTrapezoidationGraph.cpp:35 | High | P1 |
| 206 | `STHalfEdge::isUpward()` tie-break uses `to->p < from->p` (lexicographic Point comparison) when no direction can reach a higher node. If two nodes share identical coordinates after collapse, the comparison is non-deterministic across platforms — different orderings produce different toolpaths | SkeletalTrapezoidationGraph.cpp:75 | Medium | P2 |
| 207 | `STHalfEdge::getNextUnconnected()` guards only against returning to `this` — a closed next-chain not containing `this` loops forever. The constraint that "open chain" always terminates must be maintained as an invariant; refactors must add a visited-set guard | SkeletalTrapezoidationGraph.cpp:115 | Medium | P2 |
| 208 | `STHalfEdgeNode::isMultiIntersection()` returns `false` immediately on null `outgoing` (boundary-node guard). Boundary nodes with many central incident edges are permanently excluded from multi-intersection classification — intentional but undocumented | SkeletalTrapezoidationGraph.cpp:131 | Low | P3 |
| 209 | `collapseSmallEdges()` Pattern A: re-linking loop breaks at count > 1000. Edges beyond index 1000 retain dangling `from` pointers to the deleted node, silently corrupting the graph. Pre-existing upstream bug; no fix exists in OrcaSlicer | SkeletalTrapezoidationGraph.cpp:247 | High | P1 |
| 210 | `collapseSmallEdges()` Pattern B: only collapses when BOTH side edges are short. If only one side is within snap_dist the quad is left as-is — very thin quads survive and may produce zero-width beads downstream. The comment acknowledges this but no action is taken | SkeletalTrapezoidationGraph.cpp:288 | Medium | P2 |
| 211 | `makeRib()`: `dist` computed as `(Point - Point).cast<int64_t>().norm()` returns `double`, implicitly truncated to `coord_t`. Sub-nanometer precision loss is expected and benign but refactors should make the cast explicit | SkeletalTrapezoidationGraph.cpp:325 | Low | P3 |
| 212 | `insertRib()`: `assert(dist > 0)` on line 364 — if `mid_node` lies exactly on the source segment, release builds silently produce a zero-length rib. Zero-length ribs cause degenerate extrusion junctions in `generateJunctions()` | SkeletalTrapezoidationGraph.cpp:363 | Medium | P2 |
| 213 | `insertRib()` exits with `first->twin == nullptr` and `second->twin == nullptr` (comment: "we don't know these yet!"). `insertNode()` patches these immediately after. Any code that calls `insertRib()` in isolation without twin-patching will crash on null-deref in any traversal that follows a twin pointer | SkeletalTrapezoidationGraph.cpp:424 | High | P1 |

---

## Session 19 — GCodeProcessor.hpp (G-code analysis engine header)

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 214 | Six slice-warning keys are bare `#define` macros (not typed constants or `string_view`). Any consumer that parses warnings by string must replicate the exact spelling (including underscores and casing); typo at definition or call site silently suppresses the warning | GCodeProcessor.hpp:23-28 | Low | P3 |
| 215 | `PrintEstimatedStatistics::reset()` iterates `modes` by **value** (`for (auto m : modes)`) so `m.reset()` is called on a copy — the actual `modes[]` array elements are NOT cleared. `time`, `prepare_time`, and `custom_gcode_times` inside each Mode survive `reset()`. Pre-existing upstream bug | GCodeProcessor.hpp:97 | High | P1 |
| 216 | `ConflictResult::_obj1` / `_obj2` are raw `void*` into `PrintObject` instances, valid only within the slicer session. Serialising `ConflictResult` (e.g. to disk or across a process boundary) produces dangling pointers | GCodeProcessor.hpp:106 | Medium | P2 |
| 217 | `GCodeCheckResult::error_code` is a plain `int` bitfield. Bit semantics are described only in a comment. Adding new check types requires coordinated changes across all bitfield consumers; no compile-time enforcement exists | GCodeProcessor.hpp:119 | Low | P3 |
| 218 | `FilamentSequenceHash` uses `uint64_t(1) << f` — overflows for filament index ≥ 64. Printers with >64 filament slots silently produce hash collisions in `layer_filaments` map | GCodeProcessor.hpp:148 | Medium | P2 |
| 219 | `GCodeProcessorResult::filament_printable_reuslt` — typo "reuslt" (pre-existing). Serialisation or log parsing that references this field by name carries the typo | GCodeProcessor.hpp:154 | Low | P3 |
| 220 | `GCodeProcessorResult::result_mutex` is `mutable`; `lock()`/`unlock()` are `const`. Any const accessor is silently un-threadsafe. Viewer calls `get_result()` (returns const ref) and reads without holding the mutex. Use `std::shared_mutex` + RAII for a correct refactor | GCodeProcessor.hpp:257 | High | P1 |
| 221 | `GCodeProcessorResult::operator=()` omits copying: `backtrace_enabled`, `extruder_areas`, `extruder_heights`, `nozzle_hrc`, `nozzle_type`, `required_nozzle_HRC`, `filament_vitrification_temperature`, `z_offset`, `support_traditional_timelapse`. These fields are stale/zero after copy-assignment | GCodeProcessor.hpp:258 | High | P1 |
| 222 | `ETags` values are raw array indices via `static_cast<unsigned char>(tag)`. Adding/removing/reordering enum values without updating both `Reserved_Tags[]` arrays simultaneously silently reads the wrong tag string and corrupts all tag-driven G-code analysis | GCodeProcessor.hpp:327 | High | P1 |
| 223 | `s_IsBBLPrinter` is a mutable `static bool` — shared global state across all GCodeProcessor instances and threads; not mutex-protected. If two processors are constructed on different threads for different printer types (e.g. BBL + non-BBL plates), they will race | GCodeProcessor.hpp:365 | Medium | P2 |
| 224 | `TimeMachine::Planner::queue_size = 64` is hardcoded. Modern Klipper firmware uses much larger look-ahead queues. Hardcoded queue_size underestimates acceleration-phase time for Klipper printers producing systematically optimistic time estimates | GCodeProcessor.hpp:592 | Medium | P2 |
| 225 | `OptionsZCorrector::update()` calls `moves.emplace_back()` then `moves.erase(begin+id)` — O(N) on the entire moves vector. For large prints (1M+ moves) and many color-changes/pauses this becomes a hot path with quadratic total cost | GCodeProcessor.hpp:660 | Medium | P2 |
| 226 | `m_print` is a raw non-owning pointer to Print. If Print is destroyed before GCodeProcessor finishes (e.g. cancellation race), accessing `m_print` is use-after-free. Should be `std::weak_ptr<Print>` or protected by a cancel token | GCodeProcessor.hpp:831 | High | P1 |
| 227 | Tag dispatch in `process_tags()` is a fixed linear call chain through 7 parser functions. Each parser does string prefix matching on every comment line — O(k·n) for k parsers and n comment lines. Hot path for dense-comment G-code files | GCodeProcessor.hpp:900 | Low | P3 |

---

## Session 20 — Fill/FillRectilinear.cpp (scan-line infill engine)

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 228 | `connect_segment_intersections_by_contours()` inner search loop is O(N) per intersection point, O(N²) total for complex surfaces. A FIXME comment acknowledges this but no fix exists. For models with many perimeter contours this becomes the dominant infill cost | FillRectilinear.cpp:1148 | Medium | P2 |
| 229 | `monotonic_3_opt()` is called inside the ACO loop (`chain_monotonic_regions`) but its function body is entirely comments — a stub. The ACO never performs 3-opt improvement; final monotonic paths are locally suboptimal. Callers believe 3-opt ran but it did not | FillRectilinear.cpp:2418 | High | P1 |
| 230 | `AntPathMatrix` allocates a dense `(n_regions×2)² ` array of `AntPath` structs — O(4·N²) memory. A comment suggests sparse representation but it is not implemented. Surfaces with many MonotonicRegions (complex organic models) can exhaust memory | FillRectilinear.cpp:1787 | Medium | P2 |
| 231 | `INFILL_OVERLAP_OVER_SPACING = 0.45` is a hardcoded magic constant controlling infill-to-perimeter overlap distance. It is used in `fill_surface_by_lines()` without documentation. Changing it alters dimensional accuracy and bonding strength; not exposed as a config parameter | FillRectilinear.cpp:2977 | Low | P3 |
| 232 | `FillLockedZag`: the overlap region between skeleton and skin is computed as `intersection_ex` + `offset_ex(..., overlap_threshold)`. If `overlap_threshold` is set too large (per-region config), the skeleton and skin both extrude over the same area causing over-extrusion and surface defects | FillRectilinear.cpp:4036 | Medium | P2 |
| 233 | Dead code block `FillMonotonicLineWGapFill` (lines 3692–3885) is entirely commented out. It was replaced by `FillMonotonicLines` + gap fill in `FillBase`. The block is large (~200 lines), slows comprehension, and may cause merge conflicts with upstream changes | FillRectilinear.cpp:3692 | Low | P3 |
| 234 | `fill_surface_by_multilines()` divides `params.density` by `sweep_params.size()` before processing each sweep. Callers that do not pre-multiply density by sweep count will produce half/third/etc. of the intended infill density. The contract is not documented at the call sites | FillRectilinear.cpp:3235 | Medium | P2 |
| 235 | `fill_surface_trapezoidal()` period computation uses `coord_t` (integer) arithmetic derived from float inputs. Rounding at large print coordinates (prints >250mm) may produce period drift, causing visible banding in trapezoidal grid infill | FillRectilinear.cpp:3304 | Low | P3 |
| 236 | `SegmentIntersection::pos_q == 0` is a zero-denominator rational producing UB in `pos()` (integer division by zero). An `assert(pos_q != 0)` exists in `operator<` but not in `pos()`. Malformed scan-line input (degenerate polygon edge exactly on a vertical line) can trigger this silently in release builds | FillRectilinear.cpp:179 | High | P1 |

---

## Session 21 — Fill/FillBase.cpp (infill factory, gap fill, boundary connection engine)

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 237 | `BoundaryInfillGraph::map_infill_end_point_to_boundary` is a `std::vector<ContourIntersectionPoint>`. All linked-list `next_on_contour` / `prev_on_contour` pointers address elements **inside** this vector. Any operation that reallocates the vector (push_back, resize, reserve) after pointer construction produces dangling pointers and UB with no compile-time enforcement. The graph must be treated as immutable after `create_boundary_infill_graph()` returns | FillBase.cpp:1543 | High | P1 |
| 238 | `create_boundary_infill_graph()` silently emits `boundary_idx_unconnected` nodes when infill endpoint snapping fails (nearest boundary point distance > 3 scaled units). No release-build warning or log. Failed snaps cause those infill lines to remain unconnected, producing visible floating line segments in the output | FillBase.cpp:1741 | Medium | P2 |
| 239 | `assert(infill_polyline.size() == 2)` in `take()` and `take_limited()` fires only in debug builds. If called with multi-segment polylines (from non-rectilinear fills), the arc walk silently uses only the first and last point, corrupting boundary walking distance calculations | FillBase.cpp:1637 | Medium | P2 |
| 240 | `extended_object_bounding_box()` expands the bounding box by the diagonal factor `sqrt(2)`. The expansion uses `BoundingBox::scaled()` which casts the float product to `coord_t`. For objects with coordinates near `INT32_MAX / sqrt(2)` (~1.5 m in scaled units), the cast silently overflows, producing an inverted or truncated bounding box and incorrect max-connection-distance limits | FillBase.cpp:1895 | Medium | P2 |
| 241 | `connect_infill()`: `params.multiline > 1` guard skips arcs shorter than `spacing × multiline` — an Orca-specific extension not present in PrusaSlicer. Any port that omits this guard will emit very short arcs on multiline infill, degrading arc quality and potentially causing head collisions | FillBase.cpp:1934 | Low | P3 |
| 242 | `base_support_extend_infill_lines()`: `dist_y_prev < dist_y_next ? extend_prev_idx : extend_next_idx = -1;` uses ternary-as-lvalue (C++ extension; legal in C++17 with lvalue references). This idiom has no equivalent in Rust, Python, Go, or Swift and MUST be rewritten as an explicit `if/else` assignment in any port | FillBase.cpp:2215 | Medium | P2 |
| 243 | `emit_loops_in_band()`: `add_interpolated_point()` computes `(band_x - p1->x()) / (p2->x() - p1->x())`. If a contour segment is exactly vertical (`p1->x() == p2->x()`) and its X value equals the band boundary, this produces integer division by zero (UB). No guard exists; degenerate inputs from Clipper output can trigger this | FillBase.cpp:2354 | High | P1 |
| 244 | `emit_loops_in_band()` finalize path: `m_polyline.points.erase(begin() + m_polyline_end, ...)` — if a logic error in `add_interpolated_point` ordering causes `m_polyline_end >= m_polyline.points.size()`, the iterator is out of bounds (UB). Only guarded by `assert` in debug builds | FillBase.cpp:2357 | Medium | P2 |
| 245 | `connect_base_support()`: `static const double cost_low`, `cost_high`, `cost_veryhigh` are declared `static` inside the function body but reference `line_spacing` which varies per call. The `static` keyword is misleading (these constants ARE re-evaluated each call because the static initializer uses `line_spacing`). If this function is ever called from multiple threads simultaneously, `static` local initialization becomes a data race. Currently single-threaded; this is a latent hazard | FillBase.cpp:2728 | Medium | P2 |
| 246 | `multiline_fill()`: `static_cast<int>(polylines.size())` silently overflows for > `INT_MAX` polylines. Not a realistic concern for slicer workloads, but the cast is unguarded and produces a negative `n_polylines`, causing the offset loop to not execute | FillBase.cpp:3231 | Low | P3 |
| 247 | `multiline_fill()`: A single Clipper2 `ClipperOffset` instance is constructed once and `Execute()` is called in a loop with different offset values. This is correct Clipper2 usage (multiple `Execute` calls after one `AddPaths` are valid), but violates the common assumption that `Execute` consumes the input. A port that creates a new offsetter per call OR calls `Clear()` between iterations will produce empty output for all but the first offset distance | FillBase.cpp:3233 | Medium | P2 |
| 248 | `Fill::new_from_type()` returns a raw owning pointer with no `unique_ptr` wrapper. Every call site must manually `delete` the result or assign it to a smart pointer. Missing `delete` = memory leak; double-assignment = double-free. No null return path exists for the enum overload (unrecognised enum → `assert(false)`) | FillBase.cpp:67 | Medium | P2 |
| 249 | `use_bridge_flow_initializer` global forces lazy initialization of the `cached` bridge-flow vector at static init time to prevent first-call data races. However, this relies on C++ static initialization order across translation units — if `FillBase.cpp` is linked after a TU that calls `use_bridge_flow()` at static init time, the cache may be in an intermediate state | FillBase.cpp:118 | Low | P3 |
| 250 | `Fill::infill_anchor` / `Fill::infill_anchor_max` are static member variables, written by the first `PrintObject` that configures a `Fill`. If two `Print` instances with different profiles run concurrently (multi-printer UI), both share these statics — last write wins, silently corrupting anchor lengths for one printer | FillBase.cpp:57 | Medium | P2 |

---

## Session 22 — Fill/FillAdaptive.cpp (adaptive cubic infill)

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 251 | `BOOST_POOL_NO_MT` disables pool mutex — `boost::object_pool<Cube>` is NOT thread-safe for concurrent `construct()` calls. Safe now (build_octree is single-threaded), but any future parallelization of triangle insertion would cause a data race with no compile-time warning | FillAdaptive.cpp:42 | Medium | P2 |
| 252 | `Intersection*` pointers in `intersections` vector carry raw `Polyline*` (into `lines`) and `Line*` (into `lines_src`). Neither `lines` nor `lines_src` may be reallocated after these raw pointers are taken. Invariant upheld by construction order only — no compile-time enforcement. Reallocation = dangling pointer UB | FillAdaptive.cpp:600 | High | P1 |
| 253 | `generate_infill_lines_recursive()`: line-extension gap threshold is `> 1000` scaled units (not `SCALED_EPSILON = 100`). The comment says SCALED_EPSILON is insufficient. A port using SCALED_EPSILON will produce incorrect line merging (too-aggressive extension across non-adjacent cells) | FillAdaptive.cpp:489 | Medium | P2 |
| 254 | `adaptive_fill_line_spacing()`: averages density and extrusion width across all regions using adaptive fill. Multiple regions with wildly different settings produce a single octree cell size that may be suboptimal for all regions. Architectural limitation inherited from PrusaSlicer | FillAdaptive.cpp:295 | Low | P3 |
| 255 | `adaptive_fill_line_spacing()`: `n_multiline` reads only `printing_region(0).config().fill_multiline` — ignores per-region multiline settings. For objects with mixed multiline regions, all regions get the cell spacing derived from region 0's multiline count | FillAdaptive.cpp:365 | Medium | P2 |
| 256 | `update_merged_polyline_idx()` is iterative path-compression union-find without rank balancing. Worst-case O(depth) per lookup, O(N) per call for adversarial merge orders. For adversarial inputs (many sequential merges into a chain), total cost is O(N²). Acceptable for current infill line counts (~100s) | FillAdaptive.cpp:1077 | Low | P3 |
| 257 | `rtree_t` uses `float` coordinates (to avoid coord_t overflow in `bgi::intersects`). For slicer-scale coordinates (~10⁶ units), float precision loss is ~0.1 units (~0.0001 mm). Hook endpoints may be off by up to 0.1 scaled unit. Not user-visible but relevant for a port targeting integer geometry | FillAdaptive.cpp:673 | Low | P3 |
| 258 | `create_offset_line()` extends lines by factor `1.16 ≈ 1/cos(π/6)`. This is specific to 60° infill crossing angles. If infill angles change (e.g., for a square-cubic variant), this factor must be recomputed analytically | FillAdaptive.cpp:658 | Medium | P2 |
| 259 | `is_overhang_triangle()`: uses `n.norm()` (one sqrt per triangle). Could use `n.squaredNorm()` with threshold `(0.707)² × n.squaredNorm()` to avoid sqrt at zero precision cost. Minor performance note | FillAdaptive.cpp:1507 | Low | P3 |
| 260 | `build_octree()`: if `cubes_properties.size() <= 1` (possible for tiny meshes before Orca guard), the entire triangle insertion block is skipped — octree has only a root cube and all layers get empty infill. The Orca guard in `make_cubes_properties()` fixes this but the root cause (line_spacing > mesh size) is not reported to the user | FillAdaptive.cpp:1537 | Medium | P2 |
| 261 | `Octree::insert_triangle()`: `--depth` occurs before the 8-child loop. If called with `depth == 0` (guarded by `assert(depth > 0)` in debug), `cubes_properties[depth - 1]` = `cubes_properties[-1]` — undefined behavior. Only asserted in debug builds; release build silently reads out-of-bounds memory | FillAdaptive.cpp:1573 | High | P1 |
| 262 | `connect_lines_using_hooks()` `filter_itself` lambda: uses `intersection.intersect_line - lines_src.data()` (pointer subtraction) to get the current line's index. If `lines_src` is ever changed to a non-contiguous container (deque, list), this is UB. Currently `std::vector<Line>` so contiguous; the requirement must be documented | FillAdaptive.cpp:722 | Medium | P2 |
| 263 | Dead `#if 0` block in `connect_lines_using_hooks()` (self-intersection avoidance for bridge connections). Disabled because trim-after-connection was deemed sufficient. Re-enabling without the corresponding trim logic would produce bridges that self-intersect with adjacent hooks | FillAdaptive.cpp:1219 | Low | P3 |

---

## Session 23 — Fill/FillLightning.cpp + Fill/Lightning/* (Lightning Infill subsystem)

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 264 | `GeneratorDeleter` pattern: if Generator.hpp is ever included before FillLightning.hpp in the same TU, the compiler may silently allow `unique_ptr<Generator>` without the custom deleter. On MSVC `/MT` (static CRT), destructor called in the wrong TU heap → heap corruption crash. Always use `GeneratorPtr`, never `unique_ptr<Generator>` | FillLightning.hpp:52 | High | P1 |
| 265 | NAMESPACE COMMENT BUG in FillLightning.cpp line 151: `} // namespace Slic3r::FillAdaptive` — wrong namespace name (copy-paste from FillAdaptive.cpp). The closing brace is syntactically correct but the comment misleads automated namespace-extraction tools and documentation generators | FillLightning.cpp:151 | Low | P3 |
| 266 | NAMESPACE COMMENT BUG in FillLightning.hpp: `} // namespace FillAdaptive` — same copy-paste error. The Filler class is in `FillLightning` not `FillAdaptive` | FillLightning.hpp:125 | Low | P3 |
| 267 | `Filler::generator` is a raw non-owning pointer. If PrintObject is destroyed or Generator is rebuilt between `prepare_infill()` and `_fill_surface_single()` (e.g., settings changed mid-slice), `generator` is dangling. No null-check inside `_fill_surface_single()` | FillLightning.cpp:96 | High | P1 |
| 268 | `this->layer_id` is used as an index into `Generator::m_lightning_layers`. If `layer_id >= m_lightning_layers.size()` (layer count changed between generator construction and fill call), `getTreesForLayer()` fires `assert` in debug but reads out-of-bounds in release (UB) | FillLightning.cpp:96 | High | P1 |
| 269 | Orca's `multiline_fill()` in `_fill_surface_single()` may expand lines outside the expolygon boundary; the subsequent `intersection_pl()` re-clips. This double-clip is correct but hidden — a port that skips `intersection_pl()` after `multiline_fill()` will produce lines outside the part boundary | FillLightning.cpp:112 | Medium | P2 |
| 270 | Two Generator constructors have DIFFERENT `m_supporting_radius` formulas: infill uses `extrusion_width * 100 * n_multiline / density`; support uses `extrusion_width / density` (density clamped ≥ 0.15). Using the infill formula for support (or vice versa) produces wildly wrong branch density. Must be ported as two separate code paths | Generator.hpp:31 | High | P1 |
| 271 | `throw_on_cancel_callback` is called periodically during construction. If it throws `Slic3r::SlicingCancelledException` (or any exception), the Generator is partially constructed. The `GeneratorPtr` owner must not call `getTreesForLayer()` on it. No partial-construction guard exists | Generator.hpp:93 | Medium | P2 |
| 272 | `m_wall_supporting_radius`, `m_prune_length`, and `m_straightening_max_distance` are all hardcoded at 45° (`M_PI/4`). In CuraEngine these are separate configurable settings. OrcaSlicer has no UI sliders for lightning infill angles; any port that exposes per-parameter configuration will have behaviour differences with the original Cura implementation | Generator.hpp:226 | Medium | P2 |
| 273 | `Node::convertToPolylines()` uses `rand() % m_children.size()` to randomly select which child extends the "long line". The C RNG is seeded by `srand(time(NULL))` in `get_svg_filename()` — a debug helper. If the debug helper is never called, `rand()` uses the default seed 1 (deterministic). In non-debug builds the infill is deterministic but the branch that happens to be "long" varies per-machine if the debug path was ever hit. A port should replace `rand()` with a seeded PRNG or eliminate the randomness | TreeNode.cpp:convertToPolylines | Medium | P2 |
| 274 | `get_svg_filename()` in Generator.cpp: `rand_num = rand() % 1000000` is computed but never used (the filename doesn't include it). Dead code with side effect (advances RNG state) | Generator.cpp:77 | Low | P3 |
| 275 | `generateInitialInternalOverhangs()`: `diff(offset(..., -m_wall_supporting_radius), infill_above)` may produce negative-area polygons for very thin layer regions, which Clipper returns as empty. Empty overhang is silently acceptable (no infill needed), but the caller has no way to distinguish "no overhang" from "Clipper produced degenerate result" | Generator.cpp:generateInitialInternalOverhangs | Medium | P2 |
| 276 | `generateTrees()` uses a single `EdgeGrid::Grid outline_locator` rebuilt for every layer. The grid construction is O(N_edges) per layer. For complex outlines with thousands of edges across hundreds of layers, this is a significant fraction of total lightning-infill build time | Generator.cpp:generateTrees | Low | P3 |
| 277 | `generateTrees()`: `throw_on_cancel_callback` is called once per layer inside the loop. If cancellation is checked infrequently (long-running layers), there can be a several-second lag before cancel is noticed | Generator.cpp:generateTrees | Low | P3 |
| 278 | `bboxs` (typo for "bboxes") is a private `std::vector<BoundingBox>` in Generator, populated alongside `m_lightning_layers`. Its size must match `m_lightning_layers.size()` at all times. If ever accessed by index without the matching layer entry, this is an out-of-bounds read | Generator.hpp:255 | Medium | P2 |
| 279 | `Generator::generateTreesforSupport()` (support-mode second constructor) calls `generateTrees`-equivalent logic but reads from externally-provided `contours` and `overhangs` vectors. These vectors are passed by non-const reference and may be mutated during the call. The caller must keep them alive for the duration — no copy is taken | Generator.hpp:168 | Medium | P2 |
| 280 | `DistanceField` (used inside `generateNewTrees`) is not documented in this pass; it's in `Fill/Lightning/DistanceField.hpp/.cpp`. It stores overhang sample points sorted by distance-to-boundary. If DistanceField is ported to a language without mutable priority queues, the growth order (interior-first) must be explicitly preserved | Layer.cpp:generateNewTrees | Medium | P2 |
| 281 | `SparseNodeGrid` is an `unordered_multimap<Point, weak_ptr<Node>, PointHash>`. After `attach()` inserts a node, the locator is NOT immediately updated for the new node — the caller updates it manually. There is a brief window where `tree_roots` contains a node not yet in the locator. If `getBestGroundingLocation` is ever called in this window, the new node is invisible to it | Layer.hpp:37 | Medium | P2 |
| 282 | `getBestGroundingLocation()`: closest-boundary scan iterates all polygon vertices (O(N_vertices) per call). For complex outlines, this is a per-unsupported-point cost. No spatial index (e.g., k-d tree) is used for the boundary scan | Layer.hpp:106 | Medium | P2 |
| 283 | `getBestGroundingLocation()` uses `tbb::parallel_for` with a `std::mutex` for result aggregation. The parallel loop iterates over a 2D grid of SparseNodeGrid cells. If the grid has few cells (small outline), parallelism overhead exceeds the benefit. No minimum-cell-count guard exists | Layer.cpp:getBestGroundingLocation | Low | P3 |
| 284 | `getBestGroundingLocation()` tie-breaking: when two nodes have equal weighted distance, the lexicographically smallest `current_dist_grid_addr` is chosen. This preserves determinism between parallel and serial execution, but the "correct" choice (which node produces better infill quality) is not considered — it's a purely algorithmic tie-break | Layer.cpp:getBestGroundingLocation | Low | P3 |
| 285 | `reconnectRoots()`: uses `std::find(tree_roots.begin(), tree_roots.end(), root)` (O(N_roots) linear scan) to locate a root before erasing it. For large forests (many roots), this is O(N²) total. Acceptable given typical root counts (< 100) | Layer.cpp:reconnectRoots | Low | P3 |
| 286 | `SparseNodeGrid` uses `PointHash` which must be declared in `Polygon.hpp` or `Point.hpp`. If the hash specialisation is missing or produces a poor distribution (many collisions), lookup degrades to O(N) per query instead of O(1). No collision-rate diagnostic exists | Layer.hpp:37 | Low | P3 |
| 287 | `GroundingLocation::p()`: `assert(tree_node || boundary_location)` is debug-only. A default-constructed `GroundingLocation` (both null) calls `tree_node->getLocation()` on nullptr in release → crash / UB | Layer.hpp:46 | High | P1 |
| 288 | `Layer::tree_roots` is `public`. Any caller can modify the forest without updating the `SparseNodeGrid` locator, leaving it stale. This violates the class invariant silently | Layer.hpp:72 | Medium | P2 |
| 289 | `getBestGroundingLocation()` step 1 (closest boundary point) iterates all polygon vertices in a linear scan. No EdgeGrid shortcut is used for this step despite an `outline_locator` being available. O(N_vertices) per unsupported point | Layer.hpp:106 | Medium | P2 |
| 290 | `Layer::attach()`: no capacity pre-reservation on `tree_roots`. A call that triggers reallocation of `tree_roots` will invalidate any iterators or pointers held by a concurrent caller. Currently safe (single-threaded call) but latent | Layer.hpp:129 | Low | P3 |
| 291 | `reconnectRoots()`: if a root in `to_be_reconnected_tree_roots` was already removed from `this->tree_roots` before this call, `std::find()` returns `end()`. Calling `erase(end_iterator)` is UB. No guard exists; correctness depends on caller contract | Layer.hpp:140 | High | P1 |
| 292 | `convertToLines()`: calls `intersection_pl()` (Clipper) on the full set of polylines from ALL trees in the forest. For layers with many trees and complex outlines, this single Clipper call dominates runtime. No per-tree early-exit exists | Layer.hpp:154 | Low | P3 |
| 293 | `convertToPolylines()` polyline order is non-deterministic when `rand()` is unseeded (default seed 1 = deterministic) vs seeded via debug path. Different machines may produce different polyline orderings, changing G-code output file-for-file but not print quality | Layer.hpp:154 | Low | P3 |
| 294 | `Layer::getWeightedDistance()`: casts `double` norm to `coord_t`. Overflows for distances > ~2.1 km in scaled units (~INT32_MAX = 2,147,483,647 nm = 2.1 km). Safe for all practical printer bed sizes but the truncation is silent | Layer.cpp:37 | Low | P3 |
| 295 | `to_grid_point()` computes `(point - bbox.min) / locator_cell_size()`. If `point` is outside `bbox` (e.g., a node that was snapped outside the boundary), the result is a negative coordinate. Negative grid keys are valid in the `unordered_multimap` but the lookup area `[key-1, key+1]` in `getBestGroundingLocation` may include cells with negative coordinates — correct but confusing for a port that assumes non-negative keys | Layer.cpp:52 | Low | P3 |
| 296 | `fillLocator()` does NOT clear `tree_node_locator` before populating it. If the caller passes a stale (non-empty) locator, old entries accumulate. All current callers pass a fresh locator, but this is undocumented | Layer.cpp:62 | Low | P3 |
| 297 | Lightning Infill `DistanceField` (not annotated in this session): samples overhang area with a regular grid at cell_size spacing. If overhang area is very small (sub-cell), no sample points are generated and no infill is placed — the overhang is silently not supported. User sees no error | Layer.cpp:generateNewTrees | Medium | P2 |
| 298 | `generateNewTrees()` calls `throw_on_cancel_callback` once per unsupported point. For densely overhanging layers with thousands of points, this is many callback calls per second. If the callback performs I/O (e.g., logging), this is a throughput bottleneck | Layer.cpp:generateNewTrees | Low | P3 |
| 299 | `generateNewTrees()` loop: after `attach()`, the new node is inserted into `tree_node_locator` inline. If `attach()` returns `true` (new root added), `new_root` is inserted separately. If `new_root` is null despite `attach()` returning true (shouldn't happen but no assert in release), the locator entry is skipped — stale state | Layer.cpp:generateNewTrees | Low | P3 |
| 300 | `locator_cell_size()` is a free function returning `scaled<coord_t>(4.)` (4 mm in scaled units). It is called in an inner loop and is expected to be inlined. On compilers that don't inline it, there is function-call overhead per node insertion/lookup | TreeNode.hpp:46 | Low | P3 |
| 301 | `Node::create()` uses the `EnableMakeShared` workaround to call `make_shared` with a protected constructor. This is C++-specific. A port to Rust (Arc::new), Java (private constructor + static factory), or Python must replicate the "no public direct construction" intent without this pattern | TreeNode.hpp:84 | Medium | P2 |
| 302 | `Node::addChild(NodeSPtr&)`: no cycle check in release builds. If new_child is an ancestor of `this`, a directed cycle forms in the tree. Only the debug `assert(!hasOffspring(new_child))` in callers prevents this | TreeNode.hpp:125 | Medium | P2 |
| 303 | `Node::visitNodes()`, `hasOffspring()`, `deepCopy()`, `realign()`, `reroot()`, `closestNode()`, `prune()`, `straighten()`, `convertToPolylines()` are all recursive DFS functions. For adversarial inputs (very deep linear single-child trees), these can overflow the call stack. Lightning trees are typically wide and shallow (< 50 levels) so this is benign in practice, but any port to a language with small default stack sizes (e.g., Go: 8 KB goroutine stack) must be re-implemented iteratively | TreeNode.hpp:recursive methods | Medium | P2 |
| 304 | `getWeightedDistance()` hardcoded constants: `min_valence_for_boost = 0`, `max_valence_for_boost = 4`, `valence_boost_multiplier = 4`. These are not user-configurable. A port must preserve these exact values or recalibrate the tree branching behaviour | TreeNode.cpp:44 | Low | P3 |
| 305 | `Node::reroot()` is O(depth) recursive. For reroot calls on middle-tree nodes in large trees, every ancestor is visited. Works correctly but deep stacks possible for very linear trees (same concern as H303) | TreeNode.hpp:230 | Low | P3 |
| 306 | `Node::closestNode()` is an O(N_nodes) DFS linear scan with no spatial index. Called from `reconnectRoots()` per orphaned root. For large forests with many orphaned roots, this is O(N_roots × N_nodes) = O(N²) | TreeNode.hpp:240 | Medium | P2 |
| 307 | Multiple recursive DFS functions (deepCopy, realign, reroot, straighten, prune, convertToPolylines): all use the C++ call stack. Go goroutines default to 8 KB; Python has a ~1000-frame recursion limit; Rust has 8 MB stack by default. Trees deeper than ~500 levels will overflow Go/Python. Must be ported iteratively if targeting those languages | TreeNode.hpp:multiple | High | P1 |
| 308 | `straighten()` recursive overload: node positions adjusted via `double` normalization then cast back to `coord_t`. Rounding errors accumulate over many straighten passes (one per layer). Not user-visible at typical print scales but relevant for high-precision industrial prints | TreeNode.hpp:326 | Low | P3 |
| 309 | `removeJunctionOverlap()`: modifies `polylines` in-place using swap-and-pop (`std::swap` + `pop_back`). This destroys polyline ordering. Any caller that depends on stable polyline order must re-sort after this call. Current callers (convertToPolylines) do not depend on order, but a future caller might | TreeNode.hpp:398 | Low | P3 |
| 310 | `m_parent` is a `weak_ptr<Node>`. If a Node's parent is destroyed while the child still holds external shared_ptr handles (only possible via `tree_roots` holding intermediate nodes during `reconnectRoots`), `m_parent.lock()` returns nullptr. Code that calls `lock()` without null-checking may crash or silently use a null node | TreeNode.hpp:409 | Medium | P2 |

---

## Session 24 — Fill/FillConcentric.hpp/.cpp + Fill/FillGyroid.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 311 | `FillConcentric::_fill_surface_single()` uses `fill_params.dont_adjust = true` unconditionally. This prevents line-spacing adjustment to fit an integer number of loops inside the expolygon. For thin-walled regions, this means the innermost loop may partially clip or leave a gap — the user cannot override this behaviour | FillConcentric.cpp:29 | Medium | P2 |
| 312 | `FillConcentric::_fill_surface_single()`: `expolygons = union_ex(expolygons)` is called before the per-expolygon loop. For a single-expolygon input (the common case), this is a no-op Clipper call. Orca applies this defensively to handle degenerate multi-region inputs, but it adds Clipper overhead on every fill call | FillConcentric.cpp:34 | Low | P3 |
| 313 | `Polylines all_polylines` is populated by appending moves from each ExPolygon's loops. The final `chain_or_connect_infill()` sorts and connects these. If the union in H312 changes the polygon count, the resulting polyline order is non-deterministic with respect to input polygon ordering | FillConcentric.cpp:44 | Low | P3 |
| 314 | `FillConcentric::_fill_surface_single()` inner loop: `loops` is built from `offset2_ex()` shrinking by `distance`. If `distance` is very large or the polygon is very narrow, `offset2_ex()` may return empty. The `if (loops.empty()) break` guard handles this, but Clipper's `offset2_ex()` can also return degenerate single-point polygons (area ≈ 0) which pass the empty check and are silently added to `all_polylines` as zero-length segments | FillConcentric.cpp:46 | Medium | P2 |
| 315 | `FillConcentric::_fill_surface_single()`: the loop uses `expolygons` (the union result) but the inner contour-to-polyline conversion uses `expolygon.contour` and `expolygon.holes` directly. If any hole in the input has the same winding as the contour (degenerate Clipper output), the hole is incorrectly treated as a concentric loop, producing an intersecting polyline | FillConcentric.cpp:52-70 | Medium | P2 |
| 316 | `FillConcentric::_fill_surface_single()` does NOT call `multiline_fill()`. All other Orca infill types support multi-extrusion passes; FillConcentric silently ignores `params.multiline`. If `params.multiline > 1` is ever passed to FillConcentric, the user's multi-line setting is silently ignored with no warning | FillConcentric.cpp:29-75 | Medium | P2 |
| 317 | `FillConcentricInternal::fill_surface()`: distinct from `FillConcentric::_fill_surface_single()`. The internal variant is used for support interface layers and does not call `chain_or_connect_infill()`. Port must track that these are DIFFERENT code paths with different optimizations | FillConcentric.hpp:38 | Medium | P2 |
| 318 | `FillConcentric` inherits `Fill::loop_clipping` but the clipping is applied only inside `chain_or_connect_infill()`. For FillConcentric the polylines are loops (closed or nearly closed), and `loop_clipping` shortens the start/end of each polyline to prevent over-extrusion at the seam join. A port that doesn't apply loop_clipping will over-extrude at every concentric loop seam | FillConcentric.cpp:75 | Medium | P2 |
| 319 | `FillGyroid::use_bridge_flow()` returns `false`. The header comment says "require bridge flow since most of this pattern hangs in air" — this is a dead comment contradicting the implementation. In practice, Gyroid infill does NOT use bridge flow. Any port that re-enables bridge flow based on the comment will produce incorrect pressure-advance settings for Gyroid layers | FillGyroid.hpp:48 | Medium | P2 |
| 320 | `constexpr double FillGyroid::PatternTolerance` requires an out-of-class definition in FillGyroid.cpp due to C++ pre-17 ODR rules. The `.cpp` definition is marked "FIXME: needed to fix build on Mac on buildserver". In C++17 this definition is redundant. In C++14, omitting it causes a linker error on some platforms. A port to C++17 or later may safely remove the out-of-class definition | FillGyroid.cpp:192 | Low | P3 |
| 321 | `FillGyroid.cpp` static `f()`: `asin(a/r)` and `asin(res/r)` — if floating-point rounding pushes `|a/r|` or `|res/r|` above 1.0, `asin()` returns NaN. The algebraic construction guarantees `r ≥ |a|` and `r ≥ |res|`, but no explicit clamp guard exists. NaN would silently propagate to all downstream polyline point coordinates | FillGyroid.cpp:58-65 | High | P1 |
| 322 | `make_one_period()` adaptive refinement: each pass appends midpoints to a flat `std::vector`, then calls `std::sort()` on the entire vector. Total cost is O(N² log N) in the number of sample points. Acceptable for practice (N < 100 per period), but a port should prefer an ordered insertion structure (e.g., `std::map<double, double>`) to reduce worst-case complexity | FillGyroid.cpp:78-103 | Low | P3 |
| 323 | `make_gyroid_waves()` swaps `width` and `height` (local copies) for vertical orientation. This is intentional and correct. However, `make_wave()` is then called with the swapped values — the `width` and `height` parameters have "transposed" meaning. A port that documents these parameters as "X extent" and "Y extent" will be wrong for the vertical branch | FillGyroid.cpp:127 | Medium | P2 |
| 324 | `static inline double f()` implements a simplified 2D cross-section of the Gyroid TPMS. The mathematical derivation is not documented in comments. A port that attempts to optimize or replace this function must independently derive the correct formula from the TPMS implicit equation | FillGyroid.cpp:12-30 | Medium | P2 |
| 325 | `make_wave()` tiles `one_period` by copying points from `points[points.size()-n]` into the same `points` vector being appended to. `std::vector::emplace_back()` may reallocate; however, since we use index-based access (not iterators), no iterator invalidation occurs. This is correct, but the pattern is fragile — switching to iterator access (e.g., range-for) would cause UB during reallocation | FillGyroid.cpp:88 | Medium | P2 |
| 326 | `make_wave()` clamps y-coordinates to `[0, height]` via `std::clamp()`. This introduces flat horizontal segments at the boundary — the wave is artificially truncated. The resulting G-code has flat ends where the wave would naturally exit the bounding box. This is intentional but can cause slight density variation near the polygon boundary | FillGyroid.cpp:99 | Low | P3 |
| 327 | `make_one_period()` uses `cross2(ip - lp, ip - rp)` as the curve-deviation test. This is the triangle area heuristic, not the true chord-height (Hausdorff) error. For waves with large curvature, it may over-refine (adds extra points). For waves with small but non-zero curvature, it may under-refine. The result is geometrically functional but not optimally adaptive | FillGyroid.cpp:87 | Low | P3 |
| 328 | `make_gyroid_waves()` loop: `y0 += M_PI` inside the loop body (for even rows) also advances the loop variable before the header adds another `M_PI`. Net step per full iteration is 2π. This is intentional (one odd row + one even row per 2π period) but looks like a double-increment bug at first reading. A port must preserve this double-step logic | FillGyroid.cpp:135-143 | Medium | P2 |
| 329 | `_fill_surface_single()` expands the bounding box by `10 × scale_(spacing)` before wave generation. For very low densities (large spacing), this can expand the generation area by 10–50 mm, causing wave generation far outside the actual polygon. All extra waves are clipped away by `intersection_pl()` but the computation is wasted | FillGyroid.cpp:172-173 | Low | P3 |
| 330 | `_fill_surface_single()`: `density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline)`. If `params.multiline == 0` (division by zero), this is UB. The `FillParams` invariant should guarantee `multiline >= 1`, but there is no assert or guard at this call site. A port must add an explicit guard | FillGyroid.cpp:206 | Medium | P2 |
