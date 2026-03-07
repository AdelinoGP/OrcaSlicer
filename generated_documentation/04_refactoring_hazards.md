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
