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
