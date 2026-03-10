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

## Critical Blockers (P1/High — Read First)

The 270 entries below are every hazard in this file rated **High/P1** or **Critical/P0**.
They are listed in H-number order. Fix or consciously accept each one before porting.

Format: `H<id> · [SEVERITY/PRIORITY] · <description> · <file:location>`

P0/Critical entries are marked `[CRITICAL/P0]` and must be resolved first.

---

H52 · [HIGH/P1] · `discretize_circle()` degenerate normal → NaN mesh · `TreeSupport3D.cpp:3056`
H56 · [HIGH/P1] · Operator precedence bug in `generate_support_infill_lines()` · `TreeSupport3D.cpp:651`
H62 · [HIGH/P1] · `TreeSupportSettings::soluble` static race · `TreeSupportCommon.hpp`
H66 · [HIGH/P1] · `m_layer_results` public raw pointer queue · `PressureEqualizer.hpp`
H71 · [HIGH/P1] · `processing_last_mesh` bug — anti_overhang never applied (single mesh) · `TreeModelVolumes.cpp`
H77 · [HIGH/P1] · Mixed scaled/unscaled Z contract at public API boundaries · `TriangleMeshSlicer.cpp`
H81 · [HIGH/P1] · `finalize()` post-process no backup before atomic rename · `GCodeProcessor.cpp`
H82 · [HIGH/P1] · `calculate_time()` invalidates all move indices · `GCodeProcessor.cpp`
H83 · [HIGH/P1] · `m_result.moves` unbounded RAM growth (2GB+ for large prints) · `GCodeProcessor.cpp`
H84 · [HIGH/P1] · `G92 E` vs `G92 XYZ` asymmetric origin update · `GCodeProcessor.cpp`
H96 · [CRITICAL/P1] · `change_axis_value()` wraps npos+2, silently corrupts line · `FanMover.cpp`
H97 · [HIGH/P1] · `change_axis_value()` catastrophic replace if no trailing space/semicolon · `FanMover.cpp`
H111 · [CRITICAL/P1] · CSV silent discard on any parse exception — no partial recovery · `AdaptivePAInterpolator.cpp`
H112 · [HIGH/P1] · CSV column order (PA, flow, accel) undocumented; wrong order poisons model silently · `AdaptivePAInterpolator.cpp`
H113 · [HIGH/P1] · `accel_value` is `unsigned int`; populated via `std::stod()` — silent truncation of fractional values · `AdaptivePAProcessor.cpp`
H118 · [HIGH/P1] · Partially-parsed CSV line (1 or 2 of 3 fields read) contributes flowRate=0 or accel=0 entry · `AdaptivePAInterpolator.cpp`
H122 · [HIGH/P1] · PCHIP requires sorted inputs; inner (flow_rate, PA) pairs per acceleration group are unsorted · `AdaptivePAInterpolator.cpp`
H126 · [HIGH/P1] · Triple-offset in generate() destroys features < ~11500nm before SkeletalTrapezoidation · `WallToolPaths.cpp`
H127 · [HIGH/P1] · `transition_filter_dist` hardcoded to 100mm — may suppress all wall-count transitions on small parts · `WallToolPaths.cpp`
H131 · [HIGH/P1] · `outline` stored as `const Polygons&` — dangling reference if caller's polygon is a temporary · `WallToolPaths.hpp`
H136 · [HIGH/P1] · `stitchToolPaths`: `stitch_distance = bead_width_x - 1`; if bead_width_x is 0, distance = -1 (UB) · `WallToolPaths.cpp`
H141 · [HIGH/P1] · `filterCentral()` tautological always-false condition — recursive filter body is dead code · `SkeletalTrapezoidation.cpp`
H142 · [HIGH/P1] · `dissolveNearbyTransitions()` recursion depth unbounded; up to ~500 levels — stack overflow risk · `SkeletalTrapezoidation.cpp`
H143 · [HIGH/P1] · `transition_filter_dist` = 100mm hardcoded — dissolves nearly all transitions on small parts · `WallToolPaths.cpp / SkeletalTrapezoidation.cpp`
H145 · [CRITICAL/P0] · `beading_strategy` stored as `const BeadingStrategy&` — dangling reference risk · `SkeletalTrapezoidation.hpp`
H146 · [CRITICAL/P0] · `p_generated_toolpaths` raw non-owning pointer — null-deref if accessed before generateToolpaths() · `SkeletalTrapezoidation.hpp`
H147 · [HIGH/P1] · `SKELETAL_TRAPEZOIDATION_BEAD_SEARCH_MAX` = 1000 cap — silent fallback creates zero-width gap · `SkeletalTrapezoidation.cpp`
H151 · [HIGH/P1] · `generateTransitionEnd()`: division by zero if start_pos == end_pos (zero-length transition) · `SkeletalTrapezoidation.cpp`
H162 · [HIGH/P1] · `getOrCreateBeading()`: bead_count == -1 degenerate — fallback returns wrong-R beading · `SkeletalTrapezoidation.cpp`
H164 · [HIGH/P1] · `addToolpathSegment()`: CCW-wound even walls logs error but continues — incorrect winding persists · `SkeletalTrapezoidation.cpp`
H165 · [CRITICAL/P0] · `connectJunctions()`: do-while with comma-operator — malformed DCEL causes null-deref · `SkeletalTrapezoidation.cpp`
H166 · [HIGH/P1] · `connectJunctions()`: mismatched junction sizes logged but not corrected — broken paths in output · `SkeletalTrapezoidation.cpp`
H176 · [HIGH/P1] · `BeadingStrategy::getTransitionAnchorPos()` divides by (upper_optimum - lower_optimum) — div-by-zero if optimal_width == 0 · `BeadingStrategy.cpp`
H182 · [HIGH/P1] · `DistributedBeadingStrategy::compute()`: negative to_be_divided can produce negative bead widths · `DistributedBeadingStrategy.cpp`
H184 · [HIGH/P1] · `LimitedBeadingStrategy::compute()` overflow path guarded by assert only — release builds proceed garbled · `LimitedBeadingStrategy.cpp`
H186 · [HIGH/P1] · `LimitedBeadingStrategy::getOptimalThickness()` returns 1 m sentinel — propagates silently into geometry · `LimitedBeadingStrategy.cpp`
H199 · [HIGH/P1] · `BeadingStrategyFactory::makeStrategy()`: SPECIAL CASE max_bead_count <= 2 must be preserved · `BeadingStrategyFactory.cpp`
H200 · [HIGH/P1] · `BeadingStrategyFactory::makeStrategy()`: decorator stack order fixed at compile time — no runtime config · `BeadingStrategyFactory.cpp`
H205 · [HIGH/P1] · `STHalfEdge::canGoUp()` / `distToGoUp()` recurse through equidistant fans — no cycle guard · `SkeletalTrapezoidationGraph.cpp:35`
H209 · [HIGH/P1] · `collapseSmallEdges()` Pattern A: re-linking loop breaks at count > 1000 — dangling from-pointers · `SkeletalTrapezoidationGraph.cpp:247`
H213 · [HIGH/P1] · `insertRib()` exits with twin == nullptr — twin-patching contract must be preserved in port · `SkeletalTrapezoidationGraph.cpp:424`
H215 · [HIGH/P1] · `PrintEstimatedStatistics::reset()` iterates modes by value — actual modes[] elements NOT cleared · `GCodeProcessor.hpp:97`
H220 · [HIGH/P1] · `GCodeProcessorResult::result_mutex` mutable — const accessors are silently un-threadsafe · `GCodeProcessor.hpp:257`
H221 · [HIGH/P1] · `GCodeProcessorResult::operator=()` omits copying many fields — stale/zero after copy-assignment · `GCodeProcessor.hpp:258`
H222 · [HIGH/P1] · `ETags` values are raw array indices — adding/removing/reordering enum silently reads wrong tag · `GCodeProcessor.hpp:327`
H226 · [HIGH/P1] · `m_print` is a raw non-owning pointer to Print — use-after-free on cancellation race · `GCodeProcessor.hpp:831`
H229 · [HIGH/P1] · `monotonic_3_opt()` function body is entirely comments — stub, 3-opt improvement never runs · `FillRectilinear.cpp:2418`
H236 · [HIGH/P1] · `SegmentIntersection::pos_q == 0` zero-denominator — integer division UB in release builds · `FillRectilinear.cpp:179`
H237 · [HIGH/P1] · `BoundaryInfillGraph::map_infill_end_point_to_boundary` linked-list pointers invalidated on vector realloc · `FillBase.cpp:1543`
H243 · [HIGH/P1] · `emit_loops_in_band()`: `add_interpolated_point()` division by zero if p1->x() == p2->x() · `FillBase.cpp:2354`
H252 · [HIGH/P1] · `Intersection*` pointers in intersections vector carry raw `Polyline*` — dangling on realloc · `FillAdaptive.cpp:600`
H261 · [HIGH/P1] · `Octree::insert_triangle()`: `--depth` before 8-child loop — depth = 0 causes infinite recursion · `FillAdaptive.cpp:1573`
H264 · [HIGH/P1] · `GeneratorDeleter` pattern: include order dependency between FillLightning.hpp and Generator.hpp · `FillLightning.hpp:52`
H267 · [HIGH/P1] · `Filler::generator` raw non-owning pointer — dangling if PrintObject destroyed first · `FillLightning.cpp:96`
H268 · [HIGH/P1] · `this->layer_id` used as index into `Generator::m_lightning_layers` — no bounds check · `FillLightning.cpp:96`
H270 · [HIGH/P1] · Two Generator constructors have DIFFERENT `m_supporting_radius` formulas — inconsistent geometry · `Generator.hpp:31`
H287 · [HIGH/P1] · `GroundingLocation::p()`: assert(tree_node || boundary_location) debug-only — release UB · `Layer.hpp:46`
H291 · [HIGH/P1] · `reconnectRoots()`: root already removed from tree but iterator still used — UB · `Layer.hpp:140`
H307 · [HIGH/P1] · Multiple recursive DFS functions (deepCopy, realign, reroot, etc.) with no depth limit · `TreeNode.hpp:multiple`
H321 · [HIGH/P1] · `FillGyroid.cpp` static f(): asin(a/r) — argument may exceed [-1,1] causing NaN · `FillGyroid.cpp:58-65`
H331 · [HIGH/P1] · `Fill3DHoneycomb::_fill_surface_single()` casts params.density to coord_t via scale_() — precision loss · `Fill3DHoneycomb.cpp:_fill_surface_single`
H334 · [HIGH/P1] · `_fill_surface_single()` hardcodes layerHeight = scale_(1.0) — always 1 mm regardless of config · `Fill3DHoneycomb.cpp:_fill_surface_single`
H336 · [HIGH/P1] · `zip()` assert(a.size() == b.size()) debug-only — release UB on mismatched arrays · `Fill3DHoneycomb.cpp:zip`
H339 · [HIGH/P1] · `FillHoneycomb` geometry cache not invalidated on config change — stale geometry silently reused · `FillHoneycomb.cpp:cache`
H352 · [HIGH/P1] · `~Layer()` manually deletes raw `LayerRegion*` pointers — no RAII · `Layer.cpp:destructor`
H355 · [HIGH/P1] · `merged()` returns ExPolygons via Clipper subtraction — may drop coincident surfaces · `Layer.cpp:merged`
H356 · [HIGH/P1] · `generate_archimedean_chords()`: if resolution <= 0 after normalization, dθ = acos(1) → infinite loop · `FillPlanePath.cpp:generate_archimedean_chords:188`
H360 · [HIGH/P1] · `SupportLayer::AreaGroup` contains raw `ExPolygon*` — dangling on vector realloc · `Layer.cpp:SupportLayer`
H361 · [HIGH/P1] · `Layer::lslices_ex` cleared and rebuilt inside `make_slices()` — data race if multi-threaded · `Layer.hpp/Layer.cpp`
H362 · [HIGH/P1] · `Layer::lower_layer` / `upper_layer` raw back-pointers — dangle after PrintObject rebuild · `Layer.hpp`
H365 · [HIGH/P1] · `make_perimeters()`: `upper_layer->get_region(region_id)` has no bounds check · `LayerRegion.cpp:154`
H368 · [HIGH/P1] · `process_external_surfaces()`: expansion_zones mutated in-place — order-dependent and fragile · `LayerRegion.cpp:559-589`
H370 · [HIGH/P1] · `expansion_zones.pop_back()` removes top zone after tops expanded — leaves zones mismatched · `LayerRegion.cpp:583`
H379 · [HIGH/P1] · `merge_bridges()`: `assert(false && "Bridge angle must be pre-calculated!")` debug-only · `LayerRegion.cpp:356-360`
H391 · [HIGH/P1] · Legacy `process_external_surfaces()`: fill_boundaries computed from fill_expolygons then overwritten · `LayerRegion.cpp:727`
H393 · [HIGH/P1] · `TriangleMeshStats::volume = -1.f` sentinel — any code comparing to 0 misreads uninitialized volume · `TriangleMesh.hpp:TriangleMeshStats`
H394 · [HIGH/P1] · `TriangleMeshStats` raw-binary serialised via cereal — field addition/removal silently corrupts saves · `TriangleMesh.hpp:TriangleMeshStats`
H396 · [HIGH/P1] · `TriangleMesh::its` is public — any code can mutate triangle/vertex data directly · `TriangleMesh.hpp:TriangleMesh`
H397 · [HIGH/P1] · `TriangleMesh::volume()` lazily initialised through mutable stats — not thread-safe · `TriangleMesh.hpp:volume()`
H402 · [HIGH/P1] · `VertexFaceIndex` CSR index invalidated by any mesh mutation — no notification mechanism · `TriangleMesh.hpp:VertexFaceIndex`
H408 · [HIGH/P1] · `its_volume()` on open meshes: signed-tetrahedra formula gives incorrect result · `TriangleMesh.hpp:its_volume()`
H409 · [HIGH/P1] · `its_make_snap()` groove alignment loop may fail to converge — no iteration cap · `TriangleMesh.hpp:its_make_snap()`
H414 · [HIGH/P1] · `stl_fill_holes()` is `#if 0` disabled — open meshes pass repair pipeline unrepaired · `TriangleMesh.cpp:trianglemesh_repair_on_import`
H425 · [HIGH/P1] · `ExPolygonProvider` requires holes CW — wrong winding silently misassigns contour/hole · `ClipperUtils.hpp:ExPolygonProvider`
H426 · [HIGH/P1] · `SurfacesPtrProvider` holds raw pointers to Surface objects — dangling if owning container reallocates · `ClipperUtils.hpp:SurfacesPtrProvider`
H435 · [HIGH/P1] · `_foreach_node<ON>` iterates original nodes vector instead of output — silent no-op bug · `ClipperUtils.hpp:_foreach_node`
H437 · [HIGH/P1] · `mittered_offset_path_scaled()`: mixed positive/negative deltas asserted debug-only — release UB · `ClipperUtils.hpp:mittered_offset_path_scaled`
H443 · [HIGH/P1] · `PolyTreeToExPolygons` silently misassigns contours and holes if PolyTree depth is wrong · `ClipperUtils.cpp:PolyTreeToExPolygons`
H454 · [HIGH/P1] · `mittered_offset_path_scaled` short-edge skip uses max_element(deltas) — wrong for mixed-sign deltas · `ClipperUtils.cpp:mittered_offset_path_scaled`
H456 · [HIGH/P1] · `variable_offset_inner/outer`: deltas vector size mismatch is debug-only assert · `ClipperUtils.cpp:variable_offset_inner/outer`
H458 · [HIGH/P1] · Raw-pointer ownership vectors (`ModelObjectPtrs`, `ModelVolumePtrs`, `ModelInstancePtrs`) — emplace_back(new T) leaks on exception · `Model.hpp:ModelObjectPtrs / ModelVolumePtrs`
H459 · [HIGH/P1] · Static members `Model::extruderParamsMap` and `printSpeedMap` — global mutable state, no sync · `Model.hpp:Model::extruderParamsMap / printSpeedMap`
H464 · [HIGH/P1] · Five separate bounding-box caches each with independent dirty flags — easy to desync · `Model.hpp:ModelObject bounding box caches`
H487 · [HIGH/P1] · TOCTOU on `object_backup_id_map`: find() + emplace() + erase() not atomic · `Model.cpp:Model::add_object(const ModelObject&)`
H489 · [HIGH/P1] · `Model::clear_objects()` calls delete_object_mesh() (filesystem I/O) inside loop — no error handling · `Model.cpp:Model::clear_objects`
H491 · [HIGH/P1] · `Model::get_object_backup_id()` non-const: find + insert not atomic — concurrent callers race · `Model.cpp:Model::get_object_backup_id`
H492 · [HIGH/P1] · `Model::get_object_backup_id()` const: find() without end() check — null-deref if not found · `Model.cpp:Model::get_object_backup_id (const)`
H493 · [HIGH/P1] · `Model::get_backup_path()` calls `std::localtime()` — not thread-safe on most platforms · `Model.cpp:Model::get_backup_path:localtime`
H498 · [HIGH/P1] · `ModelObject::add_volume_with_shared_mesh()`: shared TriangleMesh mutated via one volume affects other · `Model.cpp:ModelObject::add_volume_with_shared_mesh`
H502 · [HIGH/P1] · `ModelObject::center_around_origin()` accumulates `origin_translation` on each call without reset · `Model.cpp:ModelObject::center_around_origin`
H504 · [HIGH/P1] · `ModelObject::translate()` partially updates caches — raw_bounding_box and m_min_max_z left stale · `Model.cpp:ModelObject::translate`
H506 · [HIGH/P1] · `ModelObject::rotate(Axis)` calls `center_around_origin()` after every axis-aligned rotation — accumulation · `Model.cpp:ModelObject::rotate(Axis)`
H507 · [HIGH/P1] · `ModelObject::rotate(Vec3d axis)` same center_around_origin() accumulation risk as H506 · `Model.cpp:ModelObject::rotate(Vec3d)`
H508 · [HIGH/P1] · `scale_mesh_after_creation()` / `scale_geometry_after_creation()` use const_cast to mutate through shared_ptr · `Model.cpp:scale_mesh_after_creation / scale_geometry_after_creation`
H510 · [HIGH/P1] · `ModelObject::bake_xy_rotation_into_meshes()`: after baking, mesh vertices in new coordinate but transform unchanged · `Model.cpp:ModelObject::bake_xy_rotation_into_meshes`
H512 · [HIGH/P1] · `ModelObject::split()` multi-volume path: meshes std::moved into all_meshes — original volumes left empty · `Model.cpp:ModelObject::split`
H513 · [HIGH/P1] · `ModelObject::split()` transform-correction: multiple floating-point matrix inversions — degenerate if singular · `Model.cpp:ModelObject::split`
H514 · [HIGH/P1] · `ModelVolume::get_extruders()` const method mutates mmuseg_extruders and mmuseg_ts — not thread-safe · `Model.cpp:ModelVolume::get_extruders`
H515 · [HIGH/P1] · `ModelVolume::center_geometry_after_creation()` uses const_cast to mutate m_mesh and m_convex_hull · `Model.cpp:ModelVolume::center_geometry_after_creation`
H518 · [HIGH/P1] · `ModelVolume::transform_this_mesh()`: FacetsAnnotation data not re-projected after transform · `Model.cpp:ModelVolume::transform_this_mesh`
H521 · [HIGH/P1] · `obj_import_vertex_color_deal()`: vertex_filament_ids[] array-indexed without bounds checking · `Model.cpp:obj_import_vertex_color_deal`
H524 · [HIGH/P1] · `FacetsAnnotation::get_triangle_as_string()` 4-bit nibble hex encoding has no version header · `Model.cpp:FacetsAnnotation::get_triangle_as_string`
H525 · [HIGH/P1] · `FacetsAnnotation::set_triangle_from_string()` strict ordering asserted debug-only — release proceeds corrupted · `Model.cpp:FacetsAnnotation::set_triangle_from_string`
H526 · [HIGH/P1] · `model_volume_list_changed()` compares by (type, id, transform) but NOT by mesh content · `Model.cpp:model_volume_list_changed`
H529 · [HIGH/P1] · `extract_euler_angles()` uses Eigen ZYX eulerAngles() — documented gimbal-lock singularity · `Geometry.cpp:extract_euler_angles`
H530 · [HIGH/P1] · `transform3d_from_string()` uses ::atof() — locale-dependent decimal separator · `Geometry.cpp:transform3d_from_string:atof`
H531 · [HIGH/P1] · `Transformation::volume_to_bed_transformation()` case 2: denominator (source_size - 1) can be zero · `Geometry.cpp:volume_to_bed_transformation:degenerate-bbox`
H532 · [HIGH/P1] · `mat_around_a_point_rotate()` calls get_matrix().inverse() without checking invertibility · `Geometry.cpp:mat_around_a_point_rotate:unchecked-inverse`
H536 · [HIGH/P1] · `Transformation::set_scaling_factor()` asserts all components > 0 debug-only — release accepts zero · `Geometry.cpp:Transformation::set_scaling_factor:debug-only-assert`
H537 · [HIGH/P1] · `TransformationSVD`: mirror pre-multiplies by diag(-1,1,1) — must be preserved in port · `Geometry.cpp:TransformationSVD:mirror-pre-multiply`
H538 · [HIGH/P1] · `generate_transform()` calls normalized() on potentially zero-length vectors — NaN result · `Geometry.cpp:generate_transform:zero-vector-normalized`
H539 · [HIGH/P1] · `EdgeGrid::Contour` stores raw `const Point*` pointers into caller-owned Polygon — dangling risk · `EdgeGrid.cpp:Contour:raw-pointer-lifetime`
H546 · [HIGH/P1] · `Grid::inside()` and `Grid::line_cell_intersect()` inside `#if 0` block — unfinished dead code · `EdgeGrid.cpp:#if0-inside:unfinished-dead-code`
H548 · [HIGH/P1] · `support_material_flow()` uses `support_filament - 1` as array index without empty check · `Flow.cpp:support_material_flow`
H553 · [HIGH/P1] · `chain_and_reorder_extrusion_entities` unconditional static_cast to ExtrusionEntityCollection — UB if wrong type · `ShortestPath.cpp:chain_and_reorder_extrusion_entities`
H556 · [HIGH/P1] · V2 greedy chaining: `num_iter = num_segments * 16` guard — overflow for large segment counts · `ShortestPath.cpp:chain_segments_greedy_constrained_reversals2_`
H561 · [HIGH/P1] · `MMU_Graph::append_voronoi_vertices()`: vertex.color() field repurposed across three distinct phases · `MultiMaterialSegmentation.cpp:append_voronoi_vertices`
H562 · [HIGH/P1] · `extract_colored_segments()` repair path: arc_id -1 cast to size_t — underflow sentinel · `MultiMaterialSegmentation.cpp:extract_colored_segments:repair-path-sentinel`
H564 · [HIGH/P1] · `segmentation_top_and_bottom_layers()` uses layer_idx_offset interleave trick — must preserve in port · `MultiMaterialSegmentation.cpp:segmentation_top_and_bottom_layers:interleave-trick`
H565 · [HIGH/P1] · `layer_color_stat()` always uses nozzle_diameter.get_at(0) — hardcoded extruder 0 · `MultiMaterialSegmentation.cpp:layer_color_stat:hardcoded-extruder-0`
H568 · [HIGH/P1] · `merge_segmented_layers()` indexing: top_and_bottom_layers sized [num_facets_states][num_layers] — 1-based extruder index · `MultiMaterialSegmentation.cpp:merge_segmented_layers:indexing-convention`
H571 · [HIGH/P1] · `ray_circle_intersections()` calls non-existent helper with typo suffix — compile-time failure · `Geometry/Circle.cpp:ray_circle_intersections:typo-suffix`
H587 · [HIGH/P1] · `VoronoiUtils::decode_input_segment_endpoint()`: color == 0 path computes segment_idx via underflow · `Geometry/VoronoiUtils.cpp:decode_input_segment_endpoint:color-0-underflow`
H599 · [HIGH/P1] · `Polyline::fitting_result` parallel vector to points — must be kept in sync on all mutations · `Polyline.hpp:fitting_result:parallel-vector-invariant`
H604 · [HIGH/P1] · `ClosestPointInRadiusLookup` searches only 2×2 cell neighborhood — misses points in corner cells · `Point.hpp:ClosestPointInRadiusLookup:4-cell-approximation`
H605 · [HIGH/P1] · `Vec2crd` and `Vec2d` have implicit-cast paths — unit mismatch (scaled vs mm) compiles silently · `Point.hpp:Vec2crd-Vec2d:implicit-cast-unit-mismatch`
H609 · [HIGH/P1] · Boost.Polygon polygon_traits<ExPolygon> exposes only contour — holes ignored by Boost algorithms · `ExPolygon.hpp:boost-polygon_traits:holes-ignored`
H610 · [HIGH/P1] · `ExPolygon` winding convention (contour CCW, holes CW) not enforced by type system · `ExPolygon.hpp:ExPolygon:winding-not-enforced`
H624 · [HIGH/P1] · `to_expolygons(Polygons)` wraps each Polygon with no hole nesting — incorrect for nested polygons · `ExPolygon.hpp:to_expolygons:no-hole-nesting`
H627 · [HIGH/P1] · `keep_largest_contour_only()` crashes on nullptr contour; misidentifies CW-only polygon · `ExPolygon.cpp:keep_largest_contour_only:nullptr-crash-CW-only`
H639 · [HIGH/P1] · `Linef3::intersect_plane()` divides by v(2) — division by zero for horizontal lines · `Line.hpp:Linef3::intersect_plane:division-by-zero`
H640 · [HIGH/P1] · Boost.Polygon segment_concept for Line uses coord_t — int64 insufficient for Voronoi · `Line.hpp:boost-polygon-segment_concept:int64-voronoi-mismatch`
H644 · [HIGH/P1] · `Line::overlap()` divides by X component — division by zero for vertical lines · `Line.cpp:overlap:vertical-division-by-zero`
H646 · [CRITICAL/P1] · `Extruder::m_share_E` and `m_share_retracted` are static class-level — global shared state · `Extruder.hpp:m_share_E:static-global-state`
H649 · [HIGH/P1] · `Extruder` asymmetric config indexing: filament_diameter uses m_id, others use extruder_id · `Extruder.hpp:config-indexing:asymmetric-m_id-vs-extruder_id`
H650 · [HIGH/P1] · `Extruder::m_config` raw non-owning pointer to GCodeConfig — use-after-free risk · `Extruder.hpp:m_config:raw-non-owning-pointer`
H669 · [HIGH/P1] · `RegionExpansionParameters` float fields are in scaled units — undocumented, easy to pass mm · `Algorithm/RegionExpansion.hpp:parameters:scaled-float-units`
H673 · [HIGH/P1] · `propagate_waves()` requires seeds sorted by (boundary, src) — sort contract undocumented · `Algorithm/RegionExpansion.hpp:propagate_waves:sort-required`
H674 · [HIGH/P1] · `merge_expansions_into_expolygons()`: union_safety_offset_ex > 1 result causes sample-in-hole drop · `Algorithm/RegionExpansion.hpp:merge_expansions:sample-in-hole-drop`
H695 · [HIGH/P1] · `StaticPrintConfig::optptr()` resolves option pointer via byte-offset reinterpret_cast — breaks under virtual/multi-inheritance · `PrintConfig.hpp:StaticPrintConfig:optptr-reinterpret-cast`
H697 · [HIGH/P1] · `PrintRegionConfig` nullable filament-override arrays — null check required before every access · `PrintConfig.hpp:PrintRegionConfig:nullable-filament-arrays`
H699 · [HIGH/P1] · `flush_volumes_matrix` flat array encodes N×N matrix — N computed via sqrt, non-square silently wrong · `PrintConfig.hpp:flush_volumes_matrix-flat-array-N-from-sqrt-non-square-silent-mis-index`
H701 · [HIGH/P1] · `wipe_tower_x` and `wipe_tower_y` are ConfigOptionFloats indexed by plate — not per-extruder · `PrintConfig.hpp:PrintConfig:wipe_tower_xy-plate-indexed`
H702 · [HIGH/P1] · `FullPrintConfig` diamond inheritance — key collision debug-only assert; release returns wrong value · `PrintConfig.hpp:FullPrintConfig-diamond-inheritance-key-collision-debug-only-assert`
H706 · [HIGH/P1] · Config options serialised by cereal ordinal — insertion shifts all subsequent ordinals, corrupts saves · `PrintConfig.hpp:cereal-ordinal-serialisation-insertion-shifts-all-subsequent-ordinals`
H707 · [HIGH/P1] · cereal load(): null ptr deref on unknown ordinal — assert only, release crashes · `PrintConfig.hpp:cereal-load-null-ptr-deref-on-unknown-ordinal-debug-only-assert`
H708 · [HIGH/P1] · `AABBTreeIndirect::Tree<>` is static — adding/removing primitives requires full rebuild · `AABBTreeIndirect.hpp:Tree:static-no-incremental-update`
H717 · [HIGH/P1] · `point_outside_closed_contours()` horizontal ray — indeterminate result for point exactly on edge · `AABBTreeLines.hpp:point_outside_closed_contours:indeterminate-zero`
H718 · [HIGH/P1] · `LinesDistancer::distance_from_lines_extra<SIGNED_DISTANCE>` outside() == 0 returns unsigned distance — ambiguous sign · `AABBTreeLines.hpp:LinesDistancer:distance_from_lines_extra:outside-zero-signed`
H727 · [HIGH/P1] · `ExtrusionLoop::clip_front()` for erPerimeter overrides clip_dist — near-zero clip may loop · `ExtrusionEntity.cpp:clip_front:erPerimeter-clip_dist-near-zero-loop`
H728 · [HIGH/P1] · `extrusion_entities_append_paths_with_wipe()`: ExtrusionMultiPath wipe-connector polylines aliased on shallow copy · `ExtrusionEntity.hpp:ExtrusionMultiPath:wipe-connector-alias-on-shallow-copy`
H735 · [HIGH/P1] · `ExtrusionEntityCollection::filter_by_extrusion_role()` returns raw pointers into original collection — dangling risk · `ExtrusionEntityCollection.hpp:filter_by_extrusion_role:shallow-raw-ptr-view`
H736 · [HIGH/P1] · `filter_by_extrusion_role_in_place()` erases elements without deleting heap objects — memory leak · `ExtrusionEntityCollection.hpp:filter_by_extrusion_role_in_place:no-delete-on-erase`
H738 · [HIGH/P1] · `AABBMesh::m_tm` raw `const indexed_triangle_set*` — mesh lifetime must exceed AABBMesh · `AABBMesh.cpp:AABBMesh:m_tm-raw-pointer`
H740 · [HIGH/P1] · `AABBMesh` copy constructor shallow-copies m_tm raw pointer — double-free / dangling risk · `AABBMesh.cpp:AABBMesh:copy-ctor-shallow-m_tm`
H746 · [HIGH/P1] · `AABBMesh::normal_by_face_id()` degenerate zero-area triangle produces NaN normal · `AABBMesh.cpp:normal_by_face_id:degenerate-triangle-nan`
H748 · [HIGH/P1] · Dead-code `filter_hits()`: post-increments `next_hole_hit` past back() — UB if compiled · `AABBMesh.cpp:filter_hits:post-increment-past-back-ub`
H752 · [HIGH/P1] · `compSecondMoment()` returns scaled-coords 4th power — callers must apply SCALING_FACTOR^4 · `Brim.cpp:compSecondMoment-returns-scaled-coords-4th-power-undocumented`
H753 · [HIGH/P1] · `configBrimWidthByVolumeGroups`: height / (Ixx + Iyy) — divide by near-zero second moment · `Brim.cpp:configBrimWidthByVolumeGroups:divide-by-near-zero-moment`
H757 · [HIGH/P1] · `tryExPolygonOffset()` offset loop with no max iteration cap — potential infinite loop · `Brim.cpp:tryExPolygonOffset-no-max-iteration-cap-potential-infinite-loop`
H761 · [HIGH/P1] · `ArcFitter::do_arc_fitting_and_simplify()` mutates points in-place — caller's vector modified · `ArcFitter.cpp:do_arc_fitting_and_simplify:points-mutated-in-place`
H762 · [HIGH/P1] · `do_arc_fitting_and_simplify()` index-remapping prefix-sum correctness — must be preserved in port · `ArcFitter.cpp:do_arc_fitting_and_simplify:prefix-sum-remapping-correctness`
H768 · [HIGH/P1] · `band` parameter in Laplacian smoothing step passed in scaled units — undocumented · `ElephantFootCompensation.cpp:laplacian-smooth:band-in-scaled-units`
H773 · [HIGH/P1] · `get_extruder_ams_count()` calls stoi() on split strings — no exception handling · `PrintConfig.cpp:get_extruder_ams_count:stoi-uncaught-exception`
H774 · [HIGH/P1] · L(s) vs _(s) translation markers mixed — L() not extracted by gettext toolchain · `PrintConfig.cpp:L-vs-underscore-translation-distinction`
H776 · [HIGH/P1] · `handle_legacy()` ~244-line if/else-if chain — no test coverage, silent drop on unknown key · `PrintConfig.cpp:handle_legacy:no-test-coverage-silent-drop`
H777 · [HIGH/P1] · `get_shared_poly()` iterates extruder printable areas — empty intersection causes OOB · `PrintConfig.cpp:get_shared_poly:empty-intersection-oob`
H798 · [HIGH/P1] · `BuildVolume_Type::Custom` non-convex but containment check uses convex hull · `BuildVolume.cpp:object_state:custom-uses-convex-hull`
H800 · [HIGH/P1] · `check_object_state_with_extruder_area()` switch omits Convex/Custom build volume types · `BuildVolume.cpp:check_object_state_with_extruder_area:convex-custom-untested`
H804 · [HIGH/P1] · `SLAPrintObject::HollowingData` hollow_mesh fields mutable — lazy populate not thread-safe · `SLAPrint.hpp:HollowingData:mutable-no-mutex-race`
H805 · [HIGH/P1] · `SLAPrint::m_printer` raw `SLAArchive*` — no RAII, dangling on exception · `SLAPrint.hpp:SLAPrint:m_printer-raw-pointer-dangling`
H806 · [HIGH/P1] · `SLAPrint::PrintLayer` stores `reference_wrapper<const SliceRecord>` — dangling on invalidation · `SLAPrint.hpp:PrintLayer:reference_wrapper-dangling-on-invalidation`
H808 · [CRITICAL/P1] · `SLAPrint::Steps::drill_holes()` entire function body is commented out — dead code · `SLAPrintSteps.cpp:drill_holes:body-commented-out-dead-code`
H811 · [HIGH/P1] · `adjust_layer_series_to_align_object_height()` uses C-library abs() on double — integer truncation · `Slicing.cpp:adjust_layer_series_to_align_object_height:abs-integer-truncation`
H816 · [HIGH/P1] · `PrintStateBase::g_last_timestamp` static size_t shared across ALL Print instances — data race · `PrintBase.hpp:PrintStateBase:g_last_timestamp-not-atomic-data-race`
H817 · [HIGH/P1] · `StringObjectException::object` raw `ObjectBase const*` — dangles after Print rebuild · `PrintBase.hpp:StringObjectException:raw-pointer-dangles-after-print-rebuild`
H824 · [HIGH/P1] · `ConfigOptionBools::get_at()` reinterpret_cast<bool*>(&values[idx]) — strict-aliasing UB · `Config.hpp:ConfigOptionBools:get_at:reinterpret_cast-bool-ptr-UB`
H825 · [HIGH/P1] · `ConfigOptionPoints` uses cereal saveBinary/loadBinary — not portable across platforms · `Config.hpp:ConfigOptionPoints:saveBinary-not-portable`
H828 · [HIGH/P1] · `PresetCollection::get_preset_base()` recursive with no depth limit or cycle detection · `Preset.cpp:get_preset_base:unbounded-recursion-no-cycle-detection`
H829 · [HIGH/P1] · `PresetCollection::load_user_preset()` manual mutex lock/unlock — not released on exception · `Preset.cpp:load_user_preset:mutex-not-released-on-exception`
H839 · [HIGH/P1] · `ConflictChecker::find_inter_of_lines_in_parallel()` TBB parallel_for writes non-atomic bool — data race · `GCode/ConflictChecker.cpp:find_inter_of_lines_in_parallel:bool-find-non-atomic-data-race`
H840 · [HIGH/P1] · `LinesBucketQueue::line_rasterization()` calls assert(0) on default branch — no return in release · `GCode/ConflictChecker.cpp:line_rasterization:assert0-no-return-and-OOM`
H843 · [HIGH/P1] · `gcode_add_line_number()` reads entire G-code file into RAM — OOM for large prints · `GCode/PostProcessor.cpp:gcode_add_line_number:entire-file-into-RAM-OOM`
H844 · [HIGH/P1] · `run_post_process_scripts()` Win32: WaitForSingleObject(INFINITE) — hangs if script hangs · `GCode/PostProcessor.cpp:run_post_process_scripts:WaitForSingleObject-INFINITE-no-timeout`
H849 · [HIGH/P1] · `SpiralVase::process_layer()` transition_out uses delta E as absolute — incorrect E output · `GCode/SpiralVase.cpp:process_layer:transition_out-absolute-E-incorrect`
H850 · [HIGH/P1] · `ToolOrderUtils` TSP DP bitmask table dp[1<<n][n] — OOM for n >= 20 · `GCode/ToolOrderUtils.cpp:get_order_1_3:TSP-DP-bitmask-OOM-n20`
H856 · [HIGH/P1] · `SmallAreaInfillFlowCompensator::max_modified_length()` calls eLengths.back() — UB on empty vector · `GCode/SmallAreaInfillFlowCompensator.cpp:max_modified_length:back-on-empty-eLengths-UB`
H860 · [HIGH/P1] · All-empty CSV input passes validation — zero-knot model produces undefined interpolation · `GCode/SmallAreaInfillFlowCompensator.cpp:ctor:zero-knot-model-passes-validation`
H862 · [HIGH/P1] · `generate_interface_layers()` calls layer_storage.allocate() inside TBB parallel_for — data race · `Support/SupportCommon.cpp:generate_interface_layers:layer_storage-allocate-inside-TBB-data-race`
H863 · [HIGH/P1] · `generate_support_toolpaths()` Pass 2 reads support_layers[id+1] in parallel — data race · `Support/SupportCommon.cpp:generate_support_toolpaths:ironing-reads-id+1-in-parallel`
H866 · [HIGH/P1] · `modulate_extrusion_by_overlapping_layers()` reduces height but does not update mm3_per_mm · `Support/SupportCommon.cpp:modulate_extrusion_by_overlapping_layers:height-reduced-but-mm3_per_mm-not-updated`
H867 · [HIGH/P1] · `SupportSpotsGenerator.cpp` entire stability analysis (~1100 lines) is block-commented dead code · `Support/SupportSpotsGenerator.cpp:1100-lines-of-live-algorithm-permanently-block-commented`
H868 · [HIGH/P1] · `ExtrusionLine` default constructor: origin_entity = nullptr — is_external_perimeter() asserts on deref · `Support/SupportSpotsGenerator.cpp:ExtrusionLine:default-ctor-nullptr-origin_entity-deref`
H879 · [HIGH/P1] · `SupportParameters` ctor: support_interface_filament - 1 on unsigned — underflow to UINT_MAX · `Support/SupportParameters.hpp:ctor:support_interface_filament-minus-1-unsigned-underflow`
H881 · [HIGH/P1] · `support_layer_height_min` declared coordf_t (unscaled mm) but initialised to scaled value · `Support/SupportParameters.hpp:ctor:support_layer_height_min-scaled-vs-unscaled-unit-confusion`
H882 · [HIGH/P1] · `SupportGridPattern` stores raw non-owning `const Polygons*` — dangling if caller destroys polygon · `Support/SupportMaterial.cpp:SupportGridPattern:raw-non-owning-polygon-pointer-dangling`
H884 · [HIGH/P1] · `rasterize_polygons()` allocates grid_size.x() * grid_size.y() vector — unbounded potential OOM · `Support/SupportMaterial.cpp:rasterize_polygons:grid_size-unbounded-potential-OOM`
H886 · [HIGH/P1] · `OverhangCluster` stores raw `ExPolygon*` — dangling on vector realloc · `Support/SupportMaterial.cpp:OverhangCluster:ExPolygon-raw-pointer-dangling-on-realloc`
H887 · [HIGH/P1] · `top_contact_layers()`: support_interface_filament - 1 on unsigned — underflow to UINT_MAX · `Support/SupportMaterial.cpp:top_contact_layers:support_interface_filament-minus-1-unsigned-underflow`
H892 · [HIGH/P1] · `SupportGeneratorLayerStorage::allocate()` manual lock/unlock mutex — not released on exception · `Support/SupportLayer.hpp:SupportGeneratorLayerStorage:allocate-no-raii-mutex-guard`
H894 · [HIGH/P1] · `TreeSupportSettings::soluble` inline static bool — shared global state across all Print instances · `Support/TreeSupportCommon.hpp:358:TreeSupportSettings-soluble-inline-static-shared-global-state`
H920 · [HIGH/P1] · `reproject_points_and_holes()` dereferences object pointer before null-check — UB · `SLA/ReprojectPointsOnMesh.hpp:reproject_points_and_holes:null-check-after-dereference-UB`
H921 · [HIGH/P1] · `IndexedMesh` holds `m_tm` as raw non-owning pointer — dangling if TriangleMesh destroyed · `SLA/IndexedMesh.hpp:IndexedMesh:m_tm-non-owning-raw-pointer-dangling`
H923 · [HIGH/P1] · `IndexedMesh::query_ray_hits()` deduplication commented out — callers may receive duplicate hits · `SLA/IndexedMesh.cpp:query_ray_hits:deduplication-commented-out-callers-may-receive-duplicates`
H925 · [HIGH/P1] · `Interior::accessor` mutable — get_distance_raw() not thread-safe, data race on parallel access · `SLA/Hollowing.cpp:Interior:accessor-mutable-not-thread-safe-data-race`
H932 · [HIGH/P1] · `SupportTreeBuilder::add_anchor()` sets id from m_junctions.size() instead of m_anchors.size() · `SLA/SupportTreeBuilder.hpp:add_anchor:id-assigned-from-m_junctions-not-m_anchors`
H933 · [HIGH/P1] · `SupportTreeBuilder` copy/move ctors omit junctions, diffbridges, pedestals, anchors · `SLA/SupportTreeBuilder.hpp:copy-move-ctor-omit-junctions-diffbridges-pedestals-anchors`
H938 · [CRITICAL/P0] · `SupportTreeBuildsteps::add_pinheads()` is a completely empty stub — no pin heads ever placed · `SLA/SupportTreeBuildsteps.cpp:add_pinheads:completely-empty-stub-no-code`
H942 · [HIGH/P1] · `bicubic.h` lambda missing parameter type annotation — non-standard extension · `SLA/bicubic.h:lambda-missing-parameter-type-non-standard`
H943 · [HIGH/P1] · `bicubic.h` self-referential lambda capture [&f, ...] — undefined behaviour · `SLA/bicubic.h:self-referential-lambda-capture-undefined-behaviour`
H949 · [HIGH/P1] · `execution::reduce` mergefn must be associative — not checked, wrong results if violated · `Execution/Execution.hpp:reduce:mergefn-must-be-associative-no-check`
H953 · [HIGH/P1] · `StopCriteria::max_iterations(double val)` truncates double to unsigned — negative sentinel wraps to UINT_MAX · `Optimize/Optimizer.hpp:StopCriteria:max_iterations-setter-double-to-unsigned-truncation`
H955 · [CRITICAL/P0] · `NLoptOptimizer::optfunc`: optional gradient dereferenced without has_value() check — UB · `Optimize/NLoptOptimizer.hpp:optfunc:optional-gradient-dereference-without-check`
H956 · [HIGH/P1] · `NLoptOptimizer::optfunc`: raw C gradient pointer not null-checked — UB for gradient-free NLopt · `Optimize/NLoptOptimizer.hpp:optfunc:c-gradient-pointer-not-null-checked`
H959 · [HIGH/P1] · CSGMesh Push/Pop protocol implicit and unchecked — mismatched pairs silently corrupt geometry · `CSGMesh/CSGMesh.hpp:Push-Pop-protocol-unchecked-mismatch-silent-corrupt`
H961 · [HIGH/P1] · `copy_csgrange_shallow()` non-CSGPart path: non-owning mesh pointer — dangling if source destroyed · `CSGMesh/CSGMeshCopy.hpp:copy_csgrange_shallow:non-CSGPart-non-owning-dangling-pointer`
H965 · [HIGH/P1] · `model_to_csgmesh()` non-split path: raw const* to vol->mesh().its — dangling if mesh replaced · `CSGMesh/ModelToCSGMesh.hpp:non-split-path:raw-pointer-to-vol-mesh-dangling`
H969 · [HIGH/P1] · `check_csgmesh_booleans()` TBB parallel writes to non-atomic fail_reason — data race UB · `CSGMesh/PerformCSGMeshBooleans.hpp:check_csgmesh_booleans:unsynchronised-writes-fail_reason-fail_part_name`
H980 · [HIGH/P1] · `detail::perform_csg()` voxel path: null dst not guarded — crash on OpenVDB alloc failure · `CSGMesh/VoxelizeCSGMesh.hpp:perform_csg:null-dst-no-guard-crash`
H1002 · [HIGH/P1] · `progressfn` invoked from TBB worker threads — GUI callback must be thread-safe · `Orient.cpp:progressfn-called-from-TBB-thread-GUI-callback-must-be-thread-safe`
H1013 · [HIGH/P1] · `mesh_to_grid()`: all parts filtered by volume > 0 leaves grid nullptr — null-deref crash · `OpenVDBUtils.cpp:mesh_to_grid-grid-null-deref-if-all-parts-filtered`
H1016 · [HIGH/P1] · `triangle_mesh_to_eigen()` accesses .front() on potentially empty mesh vectors — UB · `MeshBoolean.cpp:triangle_mesh_to_eigen:front-UB-on-empty-mesh`
H1017 · [HIGH/P1] · `_cgal_do()` uses try_catch_signal/longjmp for CGAL crashes — non-portable, bypasses RAII · `MeshBoolean.cpp:_cgal_do:try_catch_signal-longjmp-non-portable-bypasses-RAII`
H1018 · [HIGH/P1] · `triangle_mesh_to_cgal` overloads: asymmetric exception semantics for open meshes · `MeshBoolean.cpp:triangle_mesh_to_cgal-overload-asymmetric-exception-semantics`
H1022 · [HIGH/P1] · `FontFile::operator==` uses == instead of != for descent and linegap fields — inverted equality · `Emboss.hpp:FontFile::operator==-inverted-equality-descent-linegap`
H1026 · [CRITICAL/P0] · Third coordinate system: SHAPE_SCALE 0.001 mm/unit — wrong coords cause 1000x scale error · `Emboss.cpp/Emboss.hpp:SHAPE_SCALE-third-coordinate-system-1-unit-0.001mm-silent-1000x-scale-error`
H1027 · [HIGH/P1] · `heal_dupl_inter()` fallback: bounding-rect hollow replaces degenerate glyph — silent corruption · `Emboss.cpp:heal_dupl_inter-fallback-bounding-rect-hollow-corrupts-glyph`
H1034 · [HIGH/P1] · `g_occt_fonts_maps` static map written by main thread, read by job threads — no sync · `Shape/TextShape.cpp:g_occt_fonts_maps-global-static-no-sync-main-vs-job-threads`
H1035 · [HIGH/P1] · `Prism()` allocates BRepPrimAPI_MakePrism with new, never deleted — memory leak per call · `Shape/TextShape.cpp:Prism-BRepPrimAPI_MakePrism-new-never-deleted-memory-leak`
H1054 · [HIGH/P1] · `area(const Points&)` returns 2× signed area — callers expecting true area get wrong result · `MultiPoint.hpp:area-returns-2x-signed-area-no-divide-by-2`
H1076 · [HIGH/P1] · `apply_extra_perimeters()` calls entities.back() without empty-check — UB on zero-loop island · `PerimeterGenerator.cpp:apply_extra_perimeters-no-guard-loops-entities-empty`
H1079 · [HIGH/P1] · `process_no_bridge()` dangling Surface* after push_back() realloc inside index loop · `PerimeterGenerator.cpp:process_no_bridge-all_surfaces-push_back-dangling-surface-pointer`
H1089 · [HIGH/P1] · `invalidate_state_by_config_options()` ~270-line hand-maintained switch — omissions cause silent stale output · `PrintObject.cpp:invalidate_state_by_config_options-hand-maintained-mapping-silent-stale-output`
H1092 · [HIGH/P1] · `bridge_over_infill()` std::move lightning data without exception safety — fill_surfaces left empty · `PrintObject.cpp:bridge_over_infill-lightning-move-exception-leaves-fill_surfaces-empty`
H1101 · [HIGH/P1] · `Print::wipe_tower_data()` const method uses const_cast to mutate — not thread-safe · `Print.cpp:wipe_tower_data-const-method-uses-const_cast-to-mutate-not-thread-safe`
H1102 · [HIGH/P1] · `Print::get_filament_temp_type()` static map lazy-init without mutex — data race on first call · `Print.cpp:get_filament_temp_type-static-map-lazy-init-no-mutex-data-race-on-first-call`
H1103 · [HIGH/P1] · TriangleSelector free-list: memcpy int bits into float field — strict-aliasing UB · `TriangleSelector.cpp:free-list-memcpy-int-into-float-field-strict-aliasing-UB`
H1104 · [HIGH/P1] · `TriangleSelector::perform_split()` ref into m_triangles invalidated by push_triangle realloc · `TriangleSelector.cpp:perform_split-ref-invalidated-by-push_triangle-reserve-contract`
H1105 · [HIGH/P1] · `TriangleSelector::serialize()` 4-bit nibble format: no version, PrusaSlicer 2.3.1 child-reversal must be preserved · `TriangleSelector.cpp:serialize-no-version-field-PrusaSlicer-2.3.1-compat-child-reversal`
H1106 · [HIGH/P1] · `TriangleSelector::HeightRange::is_pointer_in_triangle()` always returns false — O(N) brute-force fallback · `TriangleSelector.cpp:HeightRange-is_pointer_in_triangle-always-false-O-N-brute-force`
H1108 · [HIGH/P1] · `construct_full_config()` and `full_fff_config()` ~80% duplicated merge logic — must be unified in port · `PresetBundle.cpp:construct_full_config-full_fff_config-~80pct-duplicated-merge-logic`
H1109 · [HIGH/P1] · `PresetBundle::get_required_hrc_by_filament_type()` static map lazy-init without mutex — data race · `PresetBundle.cpp:get_required_hrc_by_filament_type-static-map-lazy-init-no-mutex-data-race`
H1120 · [HIGH/P1] · `AppConfig::load()` two diverged implementations (JSON vs INI) — INI path silently loses multi-filament data · `AppConfig.cpp:load-two-diverged-implementations-JSON-active-INI-legacy-disabling-loses-multi-filament`
H1123 · [HIGH/P1] · `AppConfig::get_country_code()`: "Europe" branch returns "US" — copy-paste bug, wrong cloud endpoint · `AppConfig.cpp:get_country_code-Europe-maps-to-US-copy-paste-bug-wrong-endpoint`
H1134 · [HIGH/P1] · `TriangleInfo::is_deleted()` encodes deletion as n.x() > 2.f — re-normalization resurrects deleted triangles · `QuadricEdgeCollapse.cpp:TriangleInfo-is_deleted-encodes-deletion-in-nx-gt-2-normal-field-aliasing`
H1135 · [HIGH/P1] · `change_neighbors()` complex erase/insert recompaction with no invariant assertions · `QuadricEdgeCollapse.cpp:change_neighbors-complex-erase-insert-recompaction-no-invariant-checks`
H1142 · [HIGH/P1] · `Triangulation::triangulate()` all preconditions asserted debug-only — release CGAL UB on violation · `Triangulation.cpp:triangulate-core-all-precondition-checks-are-assert-only-stripped-in-release`
H1144 · [HIGH/P1] · `predictor_instances` static unordered_map lazy-init without mutex — data race on first call · `FlushVolPredictor.cpp:predictor_instances-static-unordered_map-lazy-init-no-mutex-data-race`
H1152 · [HIGH/P1] · `get_arrange_poly(const Model&)` re-rotates accumulator instead of per-instance copy — wrong hull · `ModelArrange.cpp:get_arrange_poly-Model-overload-re-rotates-accumulator-instead-of-per-instance-copy`
H1154 · [HIGH/P1] · `get_instance_arrange_poly()` accesses extrude_ids.front() without empty check — UB · `ModelArrange.cpp:get_instance_arrange_poly-extrude_ids-front-no-empty-check-UB-on-unassigned`
H1166 · [HIGH/P1] · `get_utf8_sequence_length()` returns -1 for invalid bytes — callers don't check, off-by-one walk · `LocalesUtils.cpp:get_utf8_sequence_length-negative-return-unchecked-by-callers-off-by-one`
H1168 · [HIGH/P1] · `MinAreaBoundingBox` uses boost::multiprecision::int128_t — must retain or port will silently overflow · `MinAreaBoundingBox.cpp:uses-boost-multiprecision-int128-overflow-if-replaced`
H1179 · [HIGH/P1] · `write_rgb_or_gray_to_file()` libpng setjmp/longjmp — C++ objects on stack cause UB on longjmp · `PNGReadWrite.cpp:write_rgb_or_gray_to_file-libpng-setjmp-longjmp-UB-with-cpp-objects-on-stack`
H1190 · [HIGH/P1] · `name_tbb_thread_pool_threads_set_locale()` static bool initialized not thread-safe — double-init race · `Thread.cpp:name_tbb_thread_pool_threads_set_locale-static-bool-initialized-not-thread-safe`

---

## 1. Raw-Pointer Ownership Chains

**Location:**
- [`Print.hpp:175`](../src/libslic3r/Print.hpp#L175) — `typedef std::vector<Layer*> LayerPtrs`
- [`Print.hpp:182`](../src/libslic3r/Print.hpp#L182) — `typedef std::vector<SupportLayer*> SupportLayerPtrs`
- [`Model.hpp:182-185`](../src/libslic3r/Model.hpp#L182-L185) — `ModelMaterialMap`, `ModelObjectPtrs`, `ModelVolumePtrs`, `ModelInstancePtrs`
- [`ExtrusionEntity.hpp:167`](../src/libslic3r/ExtrusionEntity.hpp#L167) — `typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr`

**Hazard:** Raw pointer vectors establish ownership by convention, not by type. The owner (e.g., `PrintObject` for `LayerPtrs`) must manually call `delete` in its destructor. Any early return, exception, or refactoring that creates a second owner causes either double-free or leak. There is no type-level enforcement.

**Affected classes:** `PrintObject`, `Print`, `Model`, `ModelObject`, `ModelVolume`, `ExtrusionEntityCollection`.

**Translation risk:** In GC languages this becomes "just objects," losing the explicit ownership signal. In Rust, these must become `Vec<Box<Layer>>`. In any language, the destructor logic must be audited to match exactly who deletes what.

**Mitigation:** Before porting, map every raw-pointer vector to its owning class. Replace with `unique_ptr` vectors in C++ first as a safe intermediate step; this will surface any hidden aliasing.

---

## 2. Dual Coordinate System

**Location:**
- [`Point.hpp:71`](../src/libslic3r/Point.hpp#L71) — `using PointsAllocator = tbb::scalable_allocator<BaseType>`
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
- [`TriangleMeshSlicer.cpp:590`](../src/libslic3r/TriangleMeshSlicer.cpp#L590) — `tbb::parallel_for` over triangles
- [`TreeSupport.cpp:899`](../src/libslic3r/Support/TreeSupport.cpp#L899) — `tbb::parallel_for` over layers
- [`TreeSupport3D.cpp:2569`](../src/libslic3r/Support/TreeSupport3D.cpp#L2569) — nested `tbb::parallel_for`
- [`TreeModelVolumes.cpp:360`](../src/libslic3r/Support/TreeModelVolumes.cpp#L360) — `tbb::task_group` for concurrent avoidance + wall restriction passes

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
- [`ExtrusionEntity.hpp:115-167`](../src/libslic3r/ExtrusionEntity.hpp#L115-L167) — abstract base with pure virtual `clone()` and `clone_move()`
- Concrete classes: `ExtrusionPath`, `ExtrusionMultiPath`, `ExtrusionLoop`, `ExtrusionPathSloped`, `ExtrusionPathOriented`, `ExtrusionLoopSloped`
- [`Model.hpp:187-227`](../src/libslic3r/Model.hpp#L187-L227) — `OBJECTBASE_DERIVED_COPY_MOVE_CLONE` macro generating `new_copy`, `new_clone`, `assign_copy`, `assign_clone`

**Hazard:** The virtual `clone()` / `clone_move()` methods allocate heap objects and return raw pointers. The caller is responsible for ownership. The `OBJECTBASE_DERIVED_COPY_MOVE_CLONE` macro distinguishes between "copy with same ID" (for internal `Print::apply()` deep copies) and "clone with new ID" (for user-visible duplication). Confusing the two produces ObjectID aliasing bugs that corrupt undo/redo history and incremental slicing.

**Translation risk:** Languages without value-semantics copies (Java, Python) will default to reference semantics, silently aliasing objects that should be independent. Languages without macros will need the clone/copy distinction handled via traits or interfaces.

**Mitigation:** Treat ObjectID propagation as a first-class concern. Document every call site of `new_copy` vs. `new_clone` and verify the ID contract is preserved in translation.

---

## 5. Implicit Polygon Closure Convention

**Location:**
- [`Polygon.hpp`](../src/libslic3r/Polygon.hpp) — `struct Polygon { Points points; }` — last point is **not** repeated
- [`Polyline.hpp`](../src/libslic3r/Polyline.hpp) — `struct Polyline { Points points; }` — open by definition
- [`ExtrusionEntity.hpp:772-780`](../src/libslic3r/ExtrusionEntity.hpp#L772-L780) — `extrusion_entities_append_loops` manually appends `points.front()` to close: `path.polyline.points.push_back(path.polyline.points.front())`

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
- [`PrintBase.hpp:96-180`](../src/libslic3r/PrintBase.hpp#L96-L180) — `PrintStateBase` with `StateWithTimeStamp`, `StateWithWarnings`
- [`PrintBase.hpp:239-320`](../src/libslic3r/PrintBase.hpp#L239-L320) — `set_started()`, `set_done()`, `invalidate()` all take `std::mutex &mtx`
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
- [`PrintBase.hpp:180`](../src/libslic3r/PrintBase.hpp#L180) — `static size_t g_last_timestamp`
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
- [`Point.hpp:71`](../src/libslic3r/Point.hpp#L71) — `using PointsAllocator = tbb::scalable_allocator<BaseType>`
- `Points` type alias uses this allocator globally

**Hazard:** `Points` (the fundamental polygon vertex array) uses TBB's scalable allocator instead of `std::allocator`. This provides significant performance benefits in multi-threaded contexts (thread-local allocation pools, reduced lock contention). However, it creates a dependency on TBB at the type level, not just the algorithm level. Any code that creates a `Points` value implicitly requires TBB to be initialized.

**Translation risk:** In a target language, all polygon vertex arrays must use an equivalent pool-allocated container, or the per-thread allocation advantage is lost, causing performance regressions under parallelism.

**Mitigation:** Benchmark `Points` operations with and without the custom allocator. If the performance difference is significant, the target language must use a slab/arena allocator for vertex data.

---

## 14. Exception-as-Cancellation Protocol

**Location:**
- [`PrintBase.hpp:90-94`](../src/libslic3r/PrintBase.hpp#L90-L94) — `class CanceledException : public std::exception`
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
- [`ExtrusionEntity.hpp:167`](../src/libslic3r/ExtrusionEntity.hpp#L167) — `typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr`
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
- [`Print.hpp:298-299`](../src/libslic3r/Print.hpp#L298-L299) — `PrintObjectRegions` with manual `ref_cnt_inc()` / `ref_cnt_dec()` (where `ref_cnt_dec()` calls `delete this`)

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
- [`Print.hpp:199`](../src/libslic3r/Print.hpp#L199) — `PrintInstance::shift` — world coordinate shift
- [`Print.hpp:210`](../src/libslic3r/Print.hpp#L210) — comment: *"instance_shift is too large because of multi-plate, apply without plate offset"*
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

---

## Session 24 (continued) — Fill/Fill3DHoneycomb.hpp/.cpp + Fill/FillHoneycomb.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 331 | `Fill3DHoneycomb::_fill_surface_single()` casts `params.density` to `coord_t` via `scale_()`. For densities below ~0.005 (< 0.5%), `scale_(...)` returns 0, making `distance` = 0 → division by zero when computing grid cells. No density floor guard exists | Fill3DHoneycomb.cpp:_fill_surface_single | High | P1 |
| 332 | `triWave()` accepts `coordf_t` input but uses `float t`. Silent truncation from `double` to `float` — up to 7 significant digits of precision lost. For large coordinates this causes wave-phase errors | Fill3DHoneycomb.cpp:triWave | Medium | P2 |
| 333 | `gridSize` applies empirical correction factors `0.5` and `M_PI / 3` to derive scale from `distance`. The comment says "not sure about this": the formula is an acknowledged guess, not derived analytically. Different layer spacings invalidate the calibration | Fill3DHoneycomb.cpp:gridSize | Medium | P2 |
| 334 | `_fill_surface_single()` hardcodes `layerHeight = scale_(1.0)` — always 1 mm regardless of actual layer height. The 3D honeycomb phase is therefore wrong for any non-1mm layer. Vertical cell structure is miscalibrated on every standard print | Fill3DHoneycomb.cpp:_fill_surface_single | High | P1 |
| 335 | `colinearPoints()` inner loop double-adds `baseLocation` to `b` at each iteration (starts at 0, then increments correctly). The outer `baseLocation` parameter is effectively unused in the inner loop — a masked bug | Fill3DHoneycomb.cpp:colinearPoints | Medium | P2 |
| 336 | `zip()` assert (`assert(a.size() == b.size())`) is debug-only. Mismatched array sizes in release → silent UB (reads past end of shorter vector) | Fill3DHoneycomb.cpp:zip | High | P1 |
| 337 | `_fill_surface_single()` does not handle the case where the generated polylines are empty after intersection. The `append()` call succeeds (no-op) but no diagnostic is emitted and the caller receives no fill. Silent undetected under-fill | Fill3DHoneycomb.cpp:_fill_surface_single | Low | P3 |
| 338 | `pl.simplify(5 * spacing)` uses unscaled `spacing` — effectively `5 × ~0.4` = 2 units ≈ 0.002 µm. Douglas-Peucker tolerance is irrelevantly small; the call is a no-op | Fill3DHoneycomb.cpp:_fill_surface_single | Low | P3 |
| 339 | `FillHoneycomb` caches geometry in a `std::map<CacheID, CacheData>` member. The cache is not mutex-protected. If two threads fill different layers using the same `FillHoneycomb` instance, simultaneous map writes produce a data race / UB | FillHoneycomb.cpp:cache | High | P1 |
| 340 | `FillHoneycomb` cache key uses `float`-precision members for density and angle. Floating-point map keys require exact bit-equality; even negligible rounding differences between layers cause cache misses and redundant recomputation | FillHoneycomb.hpp:CacheID | Medium | P2 |
| 341 | `_fill_surface_single()` inner X-loop: `x = min.x + std::fmod(...)` — `std::fmod` of large integers may lose precision when `x_offset` is large relative to `distance`. Cache offset drift is possible for very large objects | FillHoneycomb.cpp:_fill_surface_single | Low | P3 |
| 342 | Y-loop in `_fill_surface_single()`: iterates `y < max.y + distance`. The `+ distance` margin can overshoot the bbox by up to one full cell. All extra polylines are clipped by `intersection_pl()` but the generation cost scales with the overshoot | FillHoneycomb.cpp:_fill_surface_single | Low | P3 |
| 343 | `pl.simplify(5 * spacing)` — same as H338: unscaled `spacing` ≈ 0.002 µm tolerance. Douglas-Peucker is a no-op | FillHoneycomb.cpp:_fill_surface_single | Low | P3 |
| 344 | `direction.second` (the translation `Point`) is ignored by `FillHoneycomb`. Only `direction.first` (angle) is used. Any caller that sets a non-zero translation expects it to shift the pattern origin — it does not | FillHoneycomb.cpp:_fill_surface_single | Medium | P2 |

---

## Session 25 — Fill/FillPlanePath.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 345 | `InfillPolylineOutput::result()` moves `m_out` (leaving it empty). Calling `result()` a second time silently returns an empty vector — no assertion or error is raised. Any code that calls `result()` twice (e.g., after a refactor) loses the data silently | FillPlanePath.hpp:54 | Medium | P2 |
| 346 | `FillPlanePath::_layer_angle()` always returns `0.f`. Plane-path fills do not rotate between layers. The infill angle is controlled entirely by `direction.first` at the call site. Any port that wires `_layer_angle()` to a per-layer rotation variable will find it is always zero and the rotation has no effect | FillPlanePath.hpp:103 | Low | P3 |
| 347 | Archimedean spiral: the last point may exceed `rmax` (the loop condition is `r < rmax`, so the final point has `r` slightly above `rmax`). The last point is outside the bounding box and is clipped by `intersection_pl()`. Not a bug, but a port must clip the same way | FillPlanePath.cpp:generate_archimedean_chords | Low | P3 |
| 348 | Hilbert curve rounds the domain up to the nearest power-of-two grid. For a 3×5 region, an 8×8 grid (64 cells) is used — only ~27% of generated points are inside the actual domain; the rest are wasted work clipped later. Generation cost is O(sz²) where sz is the rounded-up power of two — up to 4× the actual area for non-power-of-two geometries | FillPlanePath.cpp:generate_hilbert_curve | Medium | P2 |
| 349 | Octagram spiral: `r_inc = sqrt(2.)` is hardcoded regardless of density or `params.density`. Ring-to-ring spacing in mm is always `sqrt(2) × distance_between_lines`, not the requested line spacing. Density setting changes `distance_between_lines` but not the ratio of rings, so visual density deviates from the requested value at non-default densities | FillPlanePath.cpp:generate_octagram_spiral | Medium | P2 |
| 350 | `InfillPolylineClipper::add_point()`: `m_sides_prev` and `m_sides_this` are uninitialised for the first call. The code path `m_out.empty() ? m_sides_prev : m_sides_this = sides(pt)` initialises only the correct member, but the other member remains uninitialised until the second call. If only 1 point is ever added and `result()` called, `m_sides_this` is garbage. Safe in practice (generators emit ≥ 2 points), but brittle | FillPlanePath.cpp:InfillPolylineClipper::add_point | Low | P3 |
| 351 | `InfillPolylineClipper::add_point()` implements a three-point outcode cull (not a true line-clip). Points outside the bbox may pass through the clipper if they are not part of a three-consecutive all-outside run. The actual clip is done later by `intersection_pl()`. A port that treats `InfillPolylineClipper` as a complete clip (without the subsequent `intersection_pl()`) will produce out-of-bounds infill lines | FillPlanePath.cpp:InfillPolylineClipper | Medium | P2 |
| 352 | `_fill_surface_single()` contains `dynamic_cast<FillArchimedeanChords*>(this)` inside the base class to detect the flow-calibration special case. This is a type-check on `this` inside a base class method — a code smell and a refactoring hazard. A port should replace with a virtual method `bool needs_inside_out_chaining() { return false; }` overridden by `FillArchimedeanChords` | FillPlanePath.cpp:_fill_surface_single:134 | Low | P3 |
| 353 | The flow calibration "center spiral first" heuristic identifies the center spiral as the longest post-clip polyline segment. For non-circular expolygons (e.g., rectangles), the longest post-clip arc may not be the center. The heuristic is undocumented and can misidentify the center in edge cases | FillPlanePath.cpp:_fill_surface_single:142 | Medium | P2 |
| 354 | `center_spiral.first_point().squaredNorm()` compares distance from `(0,0)` in the shifted coordinate frame. The shift moves the origin to the bounding box center (`this->centered()` is true for Archimedean). The check is correct only when the spiral center equals the bbox center, which holds for convex shapes but may drift for non-convex expolygons with offset bboxes | FillPlanePath.cpp:_fill_surface_single:147 | Low | P3 |
| 355 | Archimedean spiral center gap: the spiral starts at `(0,0)` then `(1,0)` with `r` initialized to 1, skipping the true center. For solid infill there is an uncovered circular area of radius ≈ 1 curve unit (≈ `distance_between_lines` mm) at the center. The FIXME comment confirms this is a known issue | FillPlanePath.cpp:generate_archimedean_chords:184 | Medium | P2 |
| 356 | `generate_archimedean_chords()`: if `resolution ≤ 0` after normalization, `dθ = 2·acos(1 - 0/r) = 2·acos(1) = 0` and `theta` never advances → infinite loop. No guard for `resolution ≤ 0` exists. `params.resolution` defaults to a nonzero value in practice, but the invariant is not enforced at this call site | FillPlanePath.cpp:generate_archimedean_chords:188 | High | P1 |
| 357 | `hilbert_n_to_xy()` uses `coord_t` (int32) for x and y. For grids with `sz > 32768` (power level > 15), x and y overflow. Achievable for large objects at fine spacing: a 300mm print at 0.2mm spacing = 1500 cells; `sz` rounds to 2048 (safe). At 0.05mm spacing = 6000 cells → `sz = 8192` (still safe). Would overflow at `sz > 32768` which requires `> 6.5m` print, safe for all real hardware | FillPlanePath.cpp:hilbert_n_to_xy:236 | Low | P3 |
| 358 | Octagram spiral: the final point of each ring is `(r2+r_inc, -rx)` not `(r+r_inc, 0)`. This intentional asymmetric transition creates the outward spiral jump. A port that "fixes" this to `(r+r_inc, 0)` will break the spiral continuity | FillPlanePath.cpp:generate_octagram_spiral:305 | Low | P3 |

---

## Session 25 (continued) — Fill/FillLine.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 359 | `_line()` oscillates odd-index lines diagonally (`x - osc` at y_min, `x + osc` at y_max). The FIXME in the .cpp confirms the Y-axis endpoint extension (30% of min_spacing) is only applied to "horizontal" (Y-axis) endpoints, not to the X-axis component of diagonal lines. For non-zero infill angles, diagonal-line endpoints near the polygon boundary are slightly short, producing a minor under-extrusion at those tips | FillLine.hpp:62 / FillLine.cpp:111 | Low | P3 |
| 360 | `_can_connect()` uses `_diagonal_distance = 2 × _line_spacing` as the max Y gap for connecting adjacent polyline endpoints. This is a heuristic constant — at very low densities (large spacing) it allows connecting lines that are far apart across features without a polygon-interior check. The geometric validity check (expolygon_off.contains) is separate and catches most cases, but the heuristic is not derived from geometry | FillLine.hpp:79 | Medium | P2 |
| 361 | `intersection_pl(polylines_src, offset(expolygon, scale_(0.02)))` — the `scale_(0.02)` outward offset is a hardcoded 20 nm expansion. For expolygons with features narrower than 20 nm (not relevant in practice but possible for degenerate Clipper output), the offset could merge adjacent contour loops. The value is undocumented | FillLine.cpp:101 | Low | P3 |
| 362 | `offset_ex(expolygon, _min_spacing / 2)` may return empty for very thin expolygons where the Minkowski expansion inverts the polygon. When empty, `expolygon_off` is default-constructed and all `expolygon_off.contains()` calls return false — connections are silently disabled and replaced by travel moves. Correct but undiagnosed degradation | FillLine.cpp:138 | Low | P3 |
| 363 | `expolygon_off.contains(Line(last_point, first_point))` tests only whether the two endpoint positions lie inside `expolygon_off`, not whether the full connecting segment is interior. If the direct path crosses a narrow hole, the connection incorrectly bridges it. In practice the `_can_connect()` X-gap check limits connections to immediately adjacent lines, reducing but not eliminating this risk | FillLine.cpp:168 | Medium | P2 |

---

## Session 25 (continued) — Fill/FillCrossHatch.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 364 | `_fill_surface_single()` computes `z_height = fmod(this->z * params.density, period * 2)` to determine the CrossHatch infill phase (straight, diagonal-forward, diagonal-back). `fmod` on a float `z` at exact period boundaries can flip the direction due to floating-point epsilon. Layers at exactly the transition boundary may render as the wrong direction variant | FillCrossHatch.hpp:32 / FillCrossHatch.cpp:250 | Medium | P2 |
| 365 | `offset = progress × period/8`. At `progress=1`, `offset = period/8`. The one_cycle points cluster at `0.25×period` and `0.75×period` along the diagonal — producing non-uniform segment lengths within a single transition sweep. This is intentional for smooth direction blending but means infill line density varies during the transition region | FillCrossHatch.cpp:67 | Low | P3 |
| 366 | `grid_size = ingrid_size * 2` doubles the effective grid internally. The even/odd polyline interleave uses `i` from `0..odd_polylines.size()-1` to index both `odd_polylines[i]` and `even_polylines[i]`. If the two counts differ by 1 (can happen when bbox height is an exact multiple of ingrid_size), the last even or odd line is silently dropped. No bounds check exists | FillCrossHatch.cpp:94 / FillCrossHatch.cpp:158 | Medium | P2 |
| 367 | `num_of_lines = height / grid_size + 1`. If `height` is exactly an integer multiple of `grid_size`, this overshoots by one line. The extra line falls exactly at the top of the bounding box and is clipped by `intersection_pl`, but the generation cost is paid unconditionally | FillCrossHatch.cpp:191 | Low | P3 |
| 368 | `int phase = fmod(z_height, period * 2) - (period - 1)`. Subtraction of `(period - 1)` shifts the phase window so that `phase < 0` → forward-diagonal, `phase > 0` → backward-diagonal. At the exact boundary `phase == 0`, the `phase < 0` branch is taken (straight lines). The logic depends on signed integer comparison of a value derived from `fmod` on a float — floating-point rounding at the boundary can select the wrong branch | FillCrossHatch.cpp:250 | Medium | P2 |
| 369 | `z_height` is offset by `repeat_layer_size/2 + trans_layer_size` to centre the straight-line band at the start of each repeat cycle. This offset must be preserved exactly in a port — removing or rounding it shifts all layer-phase transitions | FillCrossHatch.cpp:257 | Medium | P2 |
| 370 | `(progress + 0.1) * 2` intentionally overshoots to 1.2 at `progress=0.5`. This is an undocumented trick to ensure the transition completes fully before the midpoint. A port that clamps this expression to `[0, 1]` will prevent the last 20% of transition from completing | FillCrossHatch.cpp:295 | Low | P3 |
| 371 | CrossHatch uses `this->angle` directly, not `direction.first`. All other infill types use `direction.first` (which includes per-layer angle variation set by the caller). CrossHatch ignores any per-layer angle offset passed in `direction`, silently producing the same base angle on every layer | FillCrossHatch.cpp:330 | Medium | P2 |
| 372 | `density_adjusted = params.density / 1.08` — the 1.08 divisor is an empirical magic constant with no documented derivation. It compensates for the measured over-density of the CrossHatch pattern relative to a simple grid. A port must preserve this constant or effective density will differ by ~8% | FillCrossHatch.cpp:349 | Medium | P2 |
| 373 | `repeat_ratio = exp(-params.density * k)` — the exponential decay formula uses empirical constants (`k` and the initial `repeat_ratio` clamp). These are calibrated to produce visually smooth transitions at default densities. Changing them alters the transition layer count and is not equivalent to changing density | FillCrossHatch.cpp:365 | Low | P3 |
| 374 | `minlength = scale_(0.8 * this->spacing)` — uses unscaled `this->spacing` (mm) correctly. `scale_()` converts mm to scaled units. The 0.8 factor matches the same pruning threshold used in FillTpmsD, FillTpmsFK, FillGyroid, and others — consistent across the codebase | FillCrossHatch.cpp:392 | Low | P3 |

---

## Session 25 (continued) — Fill/FillTpmsD.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 375 | `FillTpmsD::use_bridge_flow()` returns `false`. The member comment says "require bridge flow since most of this pattern hangs in air" — a dead comment that contradicts the implementation. Same mismatch as FillGyroid H319 and FillTpmsFK H380. TPMS D does NOT use bridge flow; a port that re-enables bridge flow based on the comment will produce incorrect pressure-advance for TPMS D layers | FillTpmsD.hpp:48-50 | Medium | P2 |
| 376 | `FillTpmsD::DensityAdjust = 2.1` is an empirical scale factor applied to the density→wave-spacing mapping. No derivation is documented. Without this factor the measured infill weight underestimates the requested density. A port must preserve this constant or infill density will be wrong by ~2× | FillTpmsD.hpp:73 | Medium | P2 |
| 377 | `FillTpmsD::PatternTolerance` label comment says "Gyroid upper resolution tolerance" — copy-pasted from FillGyroid. This is the Schwartz D pattern. The tolerance value (0.1) may not be optimal for TPMS D's different curvature profile | FillTpmsD.hpp:81 | Low | P3 |
| 378 | `make_waves()` adaptive refinement: `wave.emplace(wave.begin()+current+1, middleU, middleV)` inserts into a `std::vector` during an index-based traversal loop. This is index-safe (uses plain integer `current`) but would cause iterator invalidation UB if refactored to use range-for or iterator-based traversal | FillTpmsD.cpp:73 | Medium | P2 |
| 379 | `density_adjusted = std::max(0., params.density * DensityAdjust / params.multiline)`. If `params.multiline == 0` this is division by zero (UB). The FillParams invariant should guarantee `multiline >= 1` but no assert or guard exists at this call site | FillTpmsD.cpp:114 | Medium | P2 |

---

## Session 25 (continued) — Fill/FillTpmsFK.hpp/.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 380 | `FillTpmsFK::use_bridge_flow()` returns `false`, but the member comment says "require bridge flow since most of this pattern hangs in air." Same dead-comment contradiction as FillTpmsD H375 and FillGyroid H319 | FillTpmsFK.hpp:20-21 | Medium | P2 |
| 381 | `vari_T = 4.18f * spacing * params.multiline / density_factor` — the 4.18 constant is empirical with no documented derivation. It calibrates the FK field period to match the requested density. A port must preserve this value or effective density will deviate | FillTpmsFK.cpp:129 | Medium | P2 |
| 382 | `density_factor = min(0.9f, params.density)` hard-clamps effective density at 90%. Requesting 100% FK infill silently produces ~90% density. No warning is emitted. The cap is intentional (the FK field becomes degenerate near 100%) but must be documented for a port that exposes density as a user parameter | FillTpmsFK.cpp:127 | Medium | P2 |
| 383 | `ScalarField` constructor receives `period = vari_T`. If `density_factor` is near zero (degenerate input) `vari_T → ∞`, `freq = 2π/∞ = 0`, and the FK field evaluates as a constant near-zero across the entire bbox. MarchingSquares on a flat field produces no iso-contours — the layer silently receives no infill. `density_factor = min(0.9, params.density)` prevents this only when `params.density > 0` is guaranteed | FillTpmsFK.cpp:134 | Medium | P2 |
| 384 | `get_scalar(coordf_t x, y, z)` uses `float` arithmetic (`cosf`, `sinf`) despite double-precision inputs. `freq * x` silently narrows `double → float` (7 significant digits). For large field coordinates (prints > 100mm with high-frequency field), float phase errors can exceed 1 ULP in field value, potentially shifting iso-contour positions by up to one raster pixel (0.004mm) | FillTpmsFK.cpp:35-44 | Low | P3 |
| 385 | `to_coordr(const coord_t& x)` uses integer division (`x / rsize`) — truncation, not rounding. Points within the last partial pixel below a boundary map to the pixel below. Introduces a systematic ≤1 pixel (0.004mm ≈ 1% of line spacing) bias in all converted coordinates. Acceptable for infill but a port using rounding would be more accurate | FillTpmsFK.cpp:55 | Low | P3 |
| 386 | `rows() = to_coordr(sf.size.y())` — integer truncation excludes the last partial row if `sf.size.y()` is not divisible by `rsize`. The `bbox.offset(...)` expansion provides margin, but the effective field extent is slightly smaller than the expanded bbox. MarchingSquares may not resolve iso-contours within the last truncated row | FillTpmsFK.cpp:74-75 | Low | P3 |
| 387 | `pts.push_back(pts.front())` closes each MarchingSquares ring into a polyline by appending the first point. If MarchingSquares returns an empty ring, `pts.front()` is UB. In practice MarchingSquares guarantees non-empty rings, but this is not asserted | FillTpmsFK.cpp:98 | Low | P3 |
| 388 | `get_polylines()` default tolerance is `SCALED_EPSILON` (sub-micron). When called from `_fill_surface_single`, `SCALED_SPARSE_INFILL_RESOLUTION` (~0.1mm) is passed instead. Using the wrong tolerance at a call site would retain O(10×) more points per polyline with no quality benefit | FillTpmsFK.cpp:83 | Low | P3 |
| 389 | `vari_T = 4.18f * spacing * params.multiline / density_factor`. If `params.multiline == 0`, `vari_T = 0` → `freq = ∞` → all FK scalar values become NaN/garbage, and MarchingSquares produces undefined output. The FillParams invariant should guarantee `multiline >= 1` but no explicit guard exists here | FillTpmsFK.cpp:129 | Medium | P2 |
| 390 | `bbox.offset(scale_((params.multiline + 1) * spacing))` — the bbox expansion before field generation grows as `O((multiline × spacing)²)` in area. For large multiline values and coarse spacing, MarchingSquares evaluates the FK field over an area many times larger than the actual expolygon, with the excess discarded by `intersection_pl`. Computation cost scales with the expanded area | FillTpmsFK.cpp:133 | Low | P3 |

---

## Session 26 — Layer.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 352 | `~Layer()` manually deletes raw `LayerRegion*` pointers held in `m_regions`. No `unique_ptr` or RAII wrapper. Any exception after partial construction leaves dangling pointers. A port must convert to owned smart pointers (e.g., `vector<unique_ptr<LayerRegion>>`) | Layer.cpp:destructor | High | P1 |
| 353 | `make_slices()` sorts `lslices` using `chain_points()` on bounding-box centers. chain_points is a traveling-salesman heuristic — it produces a locally optimal ordering but not globally optimal. For complex multi-island layers the ordering is non-deterministic across platforms (depends on floating-point comparison). Any downstream code that assumes a stable slice order across build environments may produce different G-code | Layer.cpp:make_slices | Medium | P2 |
| 354 | `backup_untyped_slices()` and `restore_untyped_slices()` swap `slices` ↔ `m_slices_backup` to save/restore the pre-surface-detection geometry. This sidesteps re-slicing but creates a dual-state model where `slices` is sometimes typed and sometimes not. A port must preserve this distinction or the pipeline will either re-slice unnecessarily or work from stale data | Layer.cpp:backup/restore | Medium | P2 |
| 355 | `merged()` returns `ExPolygons` via Clipper subtraction of `LayerSlice::volumes` from the union of all `lslices`. The subtracter volumes are stored as pre-scaled polygons in `LayerSlice`. Their coordinate system must match the scaled-coordinate `lslices` system exactly. Any unit mismatch silently corrupts the merged geometry | Layer.cpp:merged | High | P1 |
| 356 | `is_perimeter_compatible()` compares `PrintRegionConfig` serialized strings. `config_compatible.serialize()` is an O(N) string build per call. For multi-region models with many layers this is called in an inner loop and is a potential hotspot. The serialized string format is locale-sensitive — ports must use the same serialization locale | Layer.cpp:is_perimeter_compatible | Medium | P2 |
| 357 | `make_perimeters()` assigns `layerm->fill_no_overlap_expolygons` from a shared expolygon reference inside a region-pair compatibility loop. If two regions share incompatible perimeter configurations, the overlap assignment skips them — but the caller does not validate that all regions receive a valid assignment. Regions with no compatible neighbor may have an empty `fill_no_overlap_expolygons`, producing no overlap avoidance | Layer.cpp:make_perimeters | Medium | P2 |
| 358 | `simplify()` dispatches to per-region `simplify_entity_collection()`. This is called once per layer post-perimeter. For complex layers with many regions this is O(regions × entities) — no batching. Each call re-fetches PrintConfig by value (copy); see H388 | Layer.cpp:simplify | Low | P3 |
| 359 | `void_area()` estimates the ratio of un-covered slice area to total layer area. Used for sparse infill void detection. The estimate uses `lslices` minus the union of `fill_regions` — but fill_regions may not yet be fully populated at all call sites. Calling `void_area()` before `make_perimeters()` completes returns an inflated estimate | Layer.cpp:void_area | Medium | P2 |
| 360 | `SupportLayer::AreaGroup` contains a raw `ExPolygon*` pointer to an element of `support_fills`. If `support_fills` is reallocated (vector growth), the pointer dangles. Code must never push to `support_fills` after constructing AreaGroups | Layer.cpp:SupportLayer | High | P1 |
| 361 | `Layer::lslices_ex` is cleared and rebuilt inside `make_slices()`. If multiple threads access `lslices_ex` concurrently (e.g., during parallel prepare_infill), reads from one thread and writes from another race. The design assumes single-threaded access during `make_slices` but this is not enforced by any lock | Layer.hpp/Layer.cpp | High | P1 |
| 362 | `Layer::lower_layer` / `upper_layer` are raw `Layer*` back-pointers set by PrintObject. Object lifetime is managed externally. During parallel slicing, if a layer is destroyed while another thread follows a `lower_layer` pointer, this is a use-after-free. The layered pipeline ordering prevents this in practice but the raw pointer is fragile | Layer.hpp | High | P1 |

---

## Session 26 — LayerRegion.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 363 | `bridging_flow()`: `region.extruder(role) - 1` can underflow to `SIZE_MAX` if extruder index is 0. `get_at()` silently falls back to element 0. This undocumented sentinel behavior must be preserved in any port that re-implements extruder index resolution | LayerRegion.cpp:63 | Medium | P2 |
| 364 | `make_perimeters()` spiral_mode check: `layer()->id() >= bottom_shell_layers` relies on layer IDs not counting raft layers. The FIXME comment in the code acknowledges this. A port that adds raft layer support without adjusting this check will incorrectly disable spiral mode for the first few model layers | LayerRegion.cpp:131-134 | Medium | P2 |
| 365 | `make_perimeters()`: `upper_layer->get_region(region_id)` has no bounds check. If the upper layer has fewer regions (e.g., at a region merge boundary), this returns an invalid pointer or throws. The design assumes regions are 1:1 across adjacent layers, but multi-material segmentation can violate this | LayerRegion.cpp:154 | High | P1 |
| 366 | `fill_surfaces_extract_expolygons()`: `thickness` is overwritten by each matching surface. If surfaces of the same type have different thicknesses (combine_infill case), the returned thickness is the last one's value — earlier surfaces' thicknesses are silently discarded | LayerRegion.cpp:175 | Medium | P2 |
| 367 | `group_id()` union-find: path compression is single-level only. For deep chain structures the compression only flattens one level per call, making repeated `group_id()` queries O(depth). Typical bridge counts are small (<100) so this is not a performance problem, but a port should implement full path compression | LayerRegion.cpp:221 | Low | P3 |
| 368 | `process_external_surfaces()` (active): expansion_zones are mutated in-place by each `expand_*()` call. Bridge expansion runs first, clipping shells/sparse/top. Then top expansion runs on the already-clipped zones. Reordering these calls changes output geometry — the implicit ordering is a correctness invariant with no enforcement | LayerRegion.cpp:559-589 | High | P1 |
| 369 | `closing_radius = 0.55 * 0.65 * 1.05 * solid_infill_spacing`. Three empirical magic constants with no derivation comment. This value was tuned to absorb regularization artifacts (the "benchy holes" referenced in comments). Any change to the perimeter regularization algorithm requires re-deriving this constant | LayerRegion.cpp:542 | Medium | P2 |
| 370 | `expansion_zones.pop_back()` removes the top zone after tops are expanded into it. This prevents bottom expansion from also claiming top zone area. The ordering of push_back (shells, sparse, top) and pop_back (remove top) is a silent correctness dependency — re-ordering expansion_zones initialization breaks bottom expansion | LayerRegion.cpp:583 | High | P1 |
| 371 | `prepare_fill_surfaces()`: `PrintObject::infill_only_where_needed` is a static class constant (BBS change from instance config option). A port that restores it as a per-object config option must update all access sites | LayerRegion.cpp:960 | Medium | P2 |
| 372 | `prepare_fill_surfaces()` idempotency: documented in code — fill_surface BOUNDARIES must never change here, only types. Any port that modifies geometry in this function breaks idempotency of the psPrepareInfill pipeline step | LayerRegion.cpp:948-950 | Medium | P2 |
| 373 | `elephant_foot_compensation_step()`: opening radius `elephant_foot_compensation_perimeter_step` has no minimum width guard. If this value exceeds the narrowest wall, the wall's polygon is erased by opening(). No error is reported | LayerRegion.cpp:1008-1016 | Medium | P2 |
| 374 | `get_grouped_bridges()`: initializes all `bridge_expansion_begin` iterators to `bridge_expansions.end()`. If a bridge has no expansions (fully unsupported island), its iterator is never updated in `merge_bridges()`, and the `for (; it != bridge_expansions.end() && it->src_id == bridge_id2; ++it)` loop in merge_bridges silently collects zero anchor polygons. The bridge surface covers only the original expolygon without any anchor expansion | LayerRegion.cpp:232-280 | Medium | P2 |
| 375 | `get_grouped_bridges()`: O(n²) intersection test within each boundary region group. For models with many small bridge surfaces in a single large shell region, this quadratic scan is a performance risk | LayerRegion.cpp:260-278 | Low | P3 |
| 376 | `detect_bridge_directions()`: zone boundary lookup is a linear scan over all expansion zones per anchor id. A prefix-sum array of zone sizes would reduce this to O(1) per lookup. For models with many zones (multi-material prints with many shells) this loop is O(zones × anchors) | LayerRegion.cpp:299-309 | Low | P3 |
| 377 | `detect_bridge_directions()`: when `anchor_areas` is empty (no supported edges), `detect_bridging_direction()` is called with empty lines. It returns a zero direction vector, yielding `angle = PI + atan2(0,0) = PI`. The resulting horizontal direction may be wrong for unsupported islands. No warning is emitted | LayerRegion.cpp:311-313 | Medium | P2 |
| 378 | `merge_bridges()`: only the root bridge's angle is used for the entire merged group. The FIXME comment says "try to be smart and pick the best bridging angle for all?" — bridges that span different optimal directions are merged with a single angle, printing part of the surface at a non-optimal angle | LayerRegion.cpp:356 | Medium | P2 |
| 379 | `merge_bridges()`: `assert(false && "Bridge angle must be pre-calculated!")` is compiled out in Release. If `bridge.angle` is `nullopt`, execution falls through to `*bridges[bridge_id].angle` which dereferences a `nullopt` — undefined behavior in Release | LayerRegion.cpp:356-360 | High | P1 |
| 380 | `expand_expolygons()`: `processed_bridges_count` increments by `expansion_zone.expolygons.size()` after each zone. If zone expolygons were modified during `propagate_waves_ex()`, the boundary id offset would be wrong. Currently safe, but fragile to future in-place pruning changes | LayerRegion.cpp:386-403 | Low | P3 |
| 381 | `expand_bridges_detect_orientations()`: early return when `bridge_expolygons` is empty leaves `expansion_zones` unclipped. This is correct behavior (no bridges = no clipping needed) but a refactor that moves the clip step before the early-return check would incorrectly clip zones even when there are no bridges | LayerRegion.cpp:415-416 | Low | P3 |
| 382 | `expand_bridges_detect_orientations()`: `bridge_expolygons` is consumed by move into `get_grouped_bridges()`, then explicitly cleared. A refactor that reuses `bridge_expolygons` after the `get_grouped_bridges()` call would access a moved-from vector (empty) without compile error | LayerRegion.cpp:421-422 | Low | P3 |
| 383 | `expand_merge_surfaces()`: `bridge_angle` parameter defaults to -1 as a "no angle" sentinel. `Surface::bridge_angle = -1` is used throughout the codebase as a sentinel value. A port using `std::optional<double>` would be type-safe; the current sentinel can collide with a valid angle of -1 radian (≈ -57°) though in practice angles are in [0, 2π) | LayerRegion.cpp:443 | Low | P3 |
| 384 | `expand_merge_surfaces()`: `closing_radius` absorbs small unassigned regions. If `closing_radius` exceeds half the width of a legitimate narrow fill region, that region is incorrectly merged into the surrounding surface type. The constant is tuned empirically | LayerRegion.cpp:469 | Medium | P2 |
| 385 | SVG debug helpers (`export_region_slices_to_svg_debug`, `export_region_fill_surfaces_to_svg_debug`): use `static std::map<std::string, size_t>` counters. These are NOT thread-safe. In a parallelized debug build, concurrent calls from different layer regions race on the map | LayerRegion.cpp:1104/1131 | Low | P3 |
| 386 | `simplify_entity_collection()`: dispatches via `dynamic_cast` for each entity. For large collections this is O(n) dynamic casts per call. A visitor pattern or variant-based entity type would be type-safe at compile time and faster at runtime | LayerRegion.cpp:1139 | Low | P3 |
| 387 | `simplify_entity_collection()`: unknown entity type throws `Slic3r::InvalidArgument` at runtime. A new entity type added to the extrusion hierarchy without updating this function would cause a runtime throw in Release builds rather than a compile-time error | LayerRegion.cpp:1150 | Medium | P2 |
| 388 | `simplify_path()`, `simplify_multi_path()`, `simplify_loop()`: each fetches `PrintConfig` by value (full struct copy) on every call. For large collections this copies the entire PrintConfig per path. Should be cached at the collection-dispatch level | LayerRegion.cpp:1156/1172/1188 | Low | P3 |
| 389 | `simplify_loop()`: arc-fitting is disabled for spiral mode loops. Douglas-Peucker still runs with `scaled_resolution`. For spiral prints at low resolution settings, outer wall detail may be reduced | LayerRegion.cpp:1197 | Low | P3 |
| 390 | Legacy `#else` `process_external_surfaces()`: BBS scales `bridge_margin` by `nozzle_dmr_avg()`. For multi-extruder setups with different nozzle diameters, `nozzle_dmr_avg()` returns an average. The original PrusaSlicer code used a fixed 3mm margin. The BBS change couples bridge margin to nozzle diameter in a way that can be incorrect for mixed-nozzle setups | LayerRegion.cpp:717-720 | Medium | P2 |
| 391 | Legacy `#else` `process_external_surfaces()`: `fill_boundaries` is computed from `fill_expolygons` and then destructively consumed (moved into `top`/`internal` vectors). Non-idempotent: a second call to the legacy path would start with empty `fill_boundaries` and produce no external surfaces | LayerRegion.cpp:727 | High | P1 |

---

## Session 27 — TriangleMesh.hpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 392 | `RepairedMeshErrors` accumulates per-repair-pass error counts. The struct is updated by `stl_repair()` but `stl_fill_holes()` is `#if 0` disabled — `holes_fixed` will always be 0 regardless of how many open mesh holes exist. Downstream callers that use `holes_fixed > 0` as a quality gate will always see "no holes" even on open meshes | TriangleMesh.hpp:RepairedMeshErrors | Medium | P2 |
| 393 | `TriangleMeshStats::volume = -1.f` sentinel: volume is initialized to -1 to signal "not computed." This sentinel collides with genuinely inside-out (negative-volume) meshes: a mesh with slight inside-out winding would produce a small negative volume that gets set to -1 and any guard `if (volume < 0) recompute()` will incorrectly trigger a recompute | TriangleMesh.hpp:TriangleMeshStats | High | P1 |
| 394 | `TriangleMeshStats` is raw-binary serialised via `cereal loadBinary`/`saveBinary`. Adding, removing, or reordering any field silently corrupts all previously saved project files without any version check. A port must introduce an explicit schema version before any struct change | TriangleMesh.hpp:TriangleMeshStats | High | P1 |
| 395 | `TriangleMeshStats::volume` for merged meshes: volume is recomputed as the sum of per-volume `stl_volume`. If sub-meshes overlap (intersecting volumes), the sum double-counts shared space. No intersection test is performed. Reported volume can be greater than actual model volume | TriangleMesh.hpp:TriangleMeshStats | Medium | P2 |
| 396 | `TriangleMesh::its` is `public`. Any code can mutate triangle/vertex data directly without updating `m_stats`. Post-mutation, `stats().volume`, `stats().number_of_triangles`, and all AABBs are stale. No invalidation hook exists. A port must make `its` private or require mutation only through accessors that invalidate derived state | TriangleMesh.hpp:TriangleMesh | High | P1 |
| 397 | `TriangleMesh::volume()` calls `stats().volume` which is lazily initialised through a `mutable` member without a mutex. Concurrent reads from two threads could both see `volume == -1` and both attempt recomputation — a data race on the mutable stats struct | TriangleMesh.hpp:volume() | High | P1 |
| 398 | `TriangleMesh::mirror()` flips vertex coordinates along an axis, reversing winding as a side-effect. The `m_stats.volume` is negated explicitly. However, if `volume == -1` (not yet computed), the negation produces `+1`, not `-1`, breaking the sentinel convention | TriangleMesh.hpp:mirror() | Medium | P2 |
| 399 | `TriangleMesh::transform()` with shear: for non-rigid transforms (shear, non-uniform scale), volume cannot be correctly updated by multiplying by `abs(det(M))`. The code uses this approximation for `m_stats.volume`. For shear transforms the error can be significant | TriangleMesh.hpp:transform() | Medium | P2 |
| 400 | `TriangleMesh::horizontal_projection()` accumulates projected triangles into `ExPolygons` via Clipper union. For high-polygon meshes this is O(F × log F) Clipper calls. Called per-object per-layer in some support paths — potential performance bottleneck for dense meshes | TriangleMesh.hpp:horizontal_projection() | Low | P3 |
| 401 | `TriangleMesh::convex_hull_3d()` wraps qhull. Qhull "quite often" produces non-manifold output (self-adjacent facets). The manifold assertion was commented out (see qhull source note in TriangleMesh.cpp). A port using a different convex hull library must verify manifold output or add its own repair pass | TriangleMesh.hpp:convex_hull_3d() | Medium | P2 |
| 402 | `VertexFaceIndex` builds a Compressed Sparse Row (CSR) index mapping vertices to incident faces. The index has no invalidation mechanism — any modification to the parent `indexed_triangle_set` (add vertex, remove triangle, etc.) silently leaves the index stale with no error | TriangleMesh.hpp:VertexFaceIndex | High | P1 |
| 403 | `its_face_edge_ids(mesh, face_mask)`: the face_mask variant skips masked faces when looking up edge neighbors. An open edge (no matching neighbor) falls back to `face_id` itself — the face becomes its own neighbor on that edge. Downstream code must handle the `neighbor == self` sentinel explicitly | TriangleMesh.hpp:its_face_edge_ids | Medium | P2 |
| 404 | `its_face_edge_ids()` same-orientation fallback: when no counter-oriented neighbor is found (e.g., boundary edges on open meshes), the function falls back to the same-orientation face if present. This can produce incorrect topology (two faces as neighbors that share an edge but with matching winding — a non-manifold condition) | TriangleMesh.hpp:its_face_edge_ids | Medium | P2 |
| 405 | `its_face_edge_ids()` parallel variant uses `tbb::parallel_sort`. Sort order is non-deterministic for equal-key elements across TBB scheduler configurations. Builds without TBB fall back to `std::sort`. The output edge ordering is scheduler-dependent and must not be assumed stable across platforms | TriangleMesh.hpp:its_face_edge_ids | Low | P3 |
| 406 | `its_merge_vertices()`: merging vertices that were previously distinct can create T-junctions (a vertex on an edge interior). These T-junctions produce non-manifold topology where Clipper polygon construction later assumes manifold input | TriangleMesh.hpp:its_merge_vertices() | Medium | P2 |
| 407 | `its_triangle_vertex_the_same()`: compares triangle vertex *indices*, not coordinates. Two triangles sharing index references are "the same vertex" even if coordinates were mutated post-index-assignment. A port must decide whether equality is index-based or coordinate-based and apply consistently | TriangleMesh.hpp:its_triangle_vertex_the_same | Low | P3 |
| 408 | `its_volume()` on open meshes: the signed-tetrahedra formula only yields the correct volume for watertight meshes. For open meshes (missing faces), the sum of signed tetrahedra does not close and the result is mesh-topology-dependent garbage. No open-mesh guard is present | TriangleMesh.hpp:its_volume() | High | P1 |
| 409 | `its_make_snap()`: the groove alignment loop `while (!is_approx(groove_r, actual_r))` converges by iterative snap generation. If the floating-point sequence does not converge (e.g., NaN groove_r from a degenerate input), the loop runs forever. No iteration limit or NaN guard exists | TriangleMesh.hpp:its_make_snap() | High | P1 |
| 410 | `its_make_snap()` modifies `groove_plane` (an output parameter passed by reference) as a side-effect of convergence iteration. The caller receives the final plane after the loop stabilises. Any code that reads `groove_plane` before the call or caches the reference will see a mutating value during iteration | TriangleMesh.hpp:its_make_snap() | Medium | P2 |
| 411 | STL binary write uses a byte-swap pattern for big-endian portability. The swap is conditioned on `#if __BYTE_ORDER == __BIG_ENDIAN`. This preprocessor macro is POSIX-only; on Windows it is undefined and the swap is silently skipped. A port targeting Windows on big-endian hardware (hypothetical) would write corrupt STL files | TriangleMesh.hpp:stl_write_binary | Low | P3 |

---

## Session 27 — TriangleMesh.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 412 | `fill_initial_stats()` calls `stl_check_facets_exact()` which internally allocates and populates a face-neighbors array, then `stl_generate_shared_vertices()` which does the same. Two full O(F log F) neighbor computations occur sequentially. A port can halve this cost by sharing the neighbor structure | TriangleMesh.cpp:fill_initial_stats | Low | P3 |
| 413 | `trianglemesh_repair_on_import()`: the stitch/fill tolerance is `0.0005f * scale`. For meshes in inches (scale ≈ 25.4), this tolerance becomes ~12.7 µm — tighter than for mm meshes. For STL files that mix units, repair quality silently degrades. No unit-normalization step precedes repair | TriangleMesh.cpp:trianglemesh_repair_on_import | Medium | P2 |
| 414 | `stl_fill_holes()` is `#if 0` disabled throughout the repair pipeline. Open meshes are passed directly to the slicer, which closes gaps in 2D (per-layer polygon operations). This is intentional for performance but means the 3D mesh remains open and `TriangleMeshStats::holes_fixed` is always 0. Any code that gates behaviour on hole counts will see false zeros | TriangleMesh.cpp:trianglemesh_repair_on_import | High | P1 |
| 415 | `TriangleMesh::scale(float factor)`: scaling by a negative factor mirrors the mesh geometrically but does NOT flip triangle winding. The resulting mesh has inverted normals. `m_stats.volume` is updated via `volume *= factor³` which correctly goes negative, but the mesh display and slicing operations will treat all faces as back-facing | TriangleMesh.cpp:scale | Medium | P2 |
| 416 | `TriangleMesh::transformed_bounding_box(trafo, world_min_z)`: the transform matrix is `Affine3d` (double precision) but all vertex coordinates are `float`. The matrix is applied per-vertex as `trafo.cast<float>() * v` — the double-precision transform is downcast to float before multiplication, losing ~8 decimal digits of precision. For large scenes with fine details, this can shift vertices by up to 1 ULP at float scale | TriangleMesh.cpp:transformed_bounding_box | Low | P3 |
| 417 | `TriangleMesh::slice()`: the slice plane tolerance is hardcoded to `0.0004f` regardless of mesh units or bounding box scale. For very large or very small meshes this absolute tolerance is either too tight (missed intersections) or too loose (spurious intersections). No adaptive tolerance computation is performed | TriangleMesh.cpp:slice | Medium | P2 |
| 418 | `its_face_edge_ids()` face-neighbor variant: a FIXME comment in the source acknowledges incorrect orientation handling — when two adjacent faces have the same winding direction (non-manifold boundary), the edge-id assignment may swap edge indices 1 and 2. The upstream PrusaSlicer bug tracker records this as a known defect | TriangleMesh.cpp:its_face_edge_ids | Medium | P2 |
| 419 | `its_compactify_vertices()`: iterates over all triangles and remaps vertex indices. No bounds check is performed on the index values. If the mesh is corrupted (index out of range), the remap array access is out-of-bounds — undefined behaviour. A port should add `assert(idx < vertices.size())` guards | TriangleMesh.cpp:its_compactify_vertices | Medium | P2 |
| 420 | `its_make_sphere()`: uses a UV sphere (latitude/longitude grid). Polar triangles are elongated because all longitude edges converge at the same pole vertex. A FIXME comment in the source suggests replacing with an icosphere for uniform triangle distribution. The current mesh has degenerate near-zero-area triangles at both poles | TriangleMesh.cpp:its_make_sphere | Low | P3 |
| 421 | `its_make_snap()` `add_sub_mesh()` lambda: appends sub-mesh vertices and triangles assuming the sub-mesh vertex count fits within `int32_t` index range. No overflow check is performed. For pathological snap geometries with very high segment counts, the vertex count could overflow — indices would silently wrap and corrupt the mesh | TriangleMesh.cpp:its_make_snap | Low | P3 |

---

## Session 28 — ClipperUtils.hpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 422 | `DefaultMiterLimit = 3.0` is extreme (Cura uses 1.2). A FIXME comment in the source acknowledges this. At 3.0, sharp corners extend excessively during large offsets, potentially piercing neighbouring geometry on dense prints | ClipperUtils.hpp:DefaultMiterLimit | Medium | P2 |
| 423 | `ClipperSafetyOffset = 10.f` nm applied only to the clip polygon, not the subject. When subject and clip share a boundary, the 10nm gap can leave unclipped slivers in intersection/difference results | ClipperUtils.hpp:ClipperSafetyOffset | Medium | P2 |
| 424 | `ClipperOffsetShortestEdgeFactor` is empirically tuned; its effective minimum edge length is proportional to the offset distance and scale factor. Scale-dependent: at unusual coordinate scales the tuning may be wrong | ClipperUtils.hpp:ClipperOffsetShortestEdgeFactor | Low | P3 |
| 425 | `ExPolygonProvider` requires holes to be wound CW. This is an unwritten contract — callers that accidentally pass CCW holes will silently produce wrong boolean results | ClipperUtils.hpp:ExPolygonProvider | High | P1 |
| 426 | `SurfacesPtrProvider` holds raw pointers to `Surface` objects. If the owning `SurfacesPtr` vector is reallocated (e.g., push_back), all raw pointers dangle silently | ClipperUtils.hpp:SurfacesPtrProvider | High | P1 |
| 427 | `clip_clipper_polygon_with_subject_bbox()` is a vertex-level outcode filter, NOT a true geometric clip. The result may include vertices outside the bbox (corner-crossing edges kept whole). Callers must not assume exact clipping | ClipperUtils.hpp:clip_clipper_polygon_with_subject_bbox | Medium | P2 |
| 428 | `ClipperPaths_to_Slic3rExPolygons()` with `do_union=false` uses `pftEvenOdd` which XOR-cancels overlapping regions. Self-intersecting input or overlapping polygons silently lose interior areas | ClipperUtils.hpp:ClipperPaths_to_Slic3rExPolygons | Medium | P2 |
| 429 | Negative-offset normalization is undocumented: `offset_paths` dispatches on `offset > 0` vs `< 0` but callers must normalize sign themselves for `ExPolygon` holes. Missing normalization produces inverted results with no error | ClipperUtils.hpp:offset_paths | Medium | P2 |
| 430 | BBS-added inline overloads (e.g., `offset(Polygon&, ...)`) allocate temporary `std::vector` on every call. In hot slicing loops this is measurable heap churn | ClipperUtils.hpp:BBS inline overloads | Low | P3 |
| 431 | `union_safety_offset()` expands the clip by 10nm then unions. Two polygons separated by < 10nm will be merged into one. This is the intended behavior but can lose topologically distinct thin features | ClipperUtils.hpp:union_safety_offset | Medium | P2 |
| 432 | `intersection()` returning flat `Polygons` includes CW-wound holes in the output. The caller is responsible for distinguishing outer contours from holes by winding order. Easy to get wrong when iterating the output directly | ClipperUtils.hpp:intersection | Medium | P2 |
| 433 | `union_()` with `pftEvenOdd` XOR semantics: overlapping input polygons cancel instead of merging. Intended for "even-odd fill" healing but can silently erase geometry if used on regular overlapping output | ClipperUtils.hpp:union_ | Medium | P2 |
| 434 | `union_pt()` uses `pftEvenOdd` to avoid union of non-intersecting polygons. If contours accidentally intersect, they will cancel rather than merge — silent data loss | ClipperUtils.hpp:union_pt | Medium | P2 |
| 435 | **Silent bug**: `_foreach_node<e_ordering::ON>` iterates over the original `nodes` vector instead of `ordered_nodes`, making `ON` silently equivalent to `OFF`. Only `traverse_pt_noholes()` is affected (it uses `ON`) but it never produces ordered output despite the apparent intent | ClipperUtils.hpp:_foreach_node | High | P1 |
| 436 | `simplify_polygons()` with `StrictlySimple(true)` is O(N²) per the Clipper documentation. A FIXME comment in the source acknowledges this. Called on every offset result in some paths | ClipperUtils.hpp:simplify_polygons | Medium | P2 |
| 437 | `mittered_offset_path_scaled()`: mixed positive/negative deltas in one call are only asserted in Debug builds. Release builds silently produce geometrically incorrect miter offsets | ClipperUtils.hpp:mittered_offset_path_scaled | High | P1 |
| 438 | `miter_limit` passed to `mittered_offset_path_scaled` is internally rescaled by ×2 relative to Clipper's own convention. Callers using Clipper documentation values will get a different effective limit | ClipperUtils.hpp:mittered_offset_path_scaled | Low | P3 |
| 439 | `make_counter_clockwise()` calls `Polygon::new_scale()` which converts `double` mm → `int32_t` scaled units. For coordinates > ~2147mm the int32 cast overflows, corrupting the winding test result | ClipperUtils.hpp:make_counter_clockwise | Low | P3 |

---

## Session 29 — ClipperUtils.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 440 | `export_clipper_input_polygons_bin` (debug only): `fwrite` errors during writing are silently ignored. The `err:` label is only reachable on `fopen` failure. A mid-write failure produces a truncated binary debug file with no error return | ClipperUtils.cpp:export_clipper_input_polygons_bin | Low | P3 |
| 441 | `clip_clipper_polygon_with_subject_bbox_templ` is a Cohen-Sutherland outcode vertex filter, NOT a true Sutherland-Hodgman clip. Output may include vertices outside the bbox. Callers that assume exact clipping will get incorrect results | ClipperUtils.cpp:clip_clipper_polygon_with_subject_bbox_templ | Medium | P2 |
| 442 | `get_entire_polygons=true` bypasses all filtering entirely (returns src as-is). The flag name is misleading — it means "do not filter, return unmodified polygon". Easy to misuse in code reviewing/porting | ClipperUtils.cpp:clip_clipper_polygon_with_subject_bbox_templ | Low | P3 |
| 443 | `PolyTreeToExPolygons` silently misassigns contours and holes if the Clipper PolyTree is malformed (e.g., from JoinCommonEdges bugs). No validation of tree depth alternation is performed | ClipperUtils.cpp:PolyTreeToExPolygons | High | P1 |
| 444 | `raw_offset` creates one `ClipperOffset` object per input path. For N paths this is N heap allocations and N `co.Execute()` calls. For high-polygon layers this is significant heap churn — profile before porting to a language without arena allocators | ClipperUtils.cpp:raw_offset | Medium | P2 |
| 445 | `raw_offset` CW-path orientation flip relies on undocumented ClipperLib behavior: `ClipperOffset::Execute()` internally reorients to CCW before offsetting, then negates the sign. If a future Clipper version changes this, the `std::reverse` compensation will produce incorrect results | ClipperUtils.cpp:raw_offset | Medium | P2 |
| 446 | `shrink_paths` sentinel rectangle uses only ±10 scaled units (10nm) margin around GetBounds(). If offset paths extend beyond bounds by more than 10nm (should not happen in practice), the sentinel fails to contain them and the pftNegative union produces garbage | ClipperUtils.cpp:shrink_paths | Low | P3 |
| 447 | `remove_outermost_polygon<Paths>` erases `solution[0]` assuming Clipper's pftNegative union places the outermost polygon first. This relies on Clipper's internal output ordering — a fragile assumption if ClipperLib version changes | ClipperUtils.cpp:remove_outermost_polygon | Medium | P2 |
| 448 | `offset_expolygon_inner` positive-offset path skips the Clipper difference step, assuming offset holes cannot intersect the expanded contour. For malformed input (invalid ExPolygon), overlapping regions are not subtracted — silent geometry error | ClipperUtils.cpp:offset_expolygon_inner | Medium | P2 |
| 449 | `offset_expolygon_inner` returns raw `ClipperLib::Paths` without enforcing contour orientation. Callers must run clipper_union or PolyTreeToExPolygons to impose correct winding | ClipperUtils.cpp:offset_expolygon_inner | Low | P3 |
| 450 | `clipper_do_polytree` doubles Clipper calls for every ExPolygon-output operation (fractal pyramid workaround). For simple single-polygon inputs, this is wasteful overhead | ClipperUtils.cpp:clipper_do_polytree | Low | P3 |
| 451 | `_clipper_pl_recombine` is O(N²) in the number of output polyline fragments — for each fragment i, scans all j > i for endpoint coincidence | ClipperUtils.cpp:_clipper_pl_recombine | Medium | P2 |
| 452 | `_clipper_pl_recombine` uses `vector::erase()` inside a nested loop, each erase is O(N) (element shift). Combined with the O(N²) comparisons, worst case is O(N³) data movement | ClipperUtils.cpp:_clipper_pl_recombine | Medium | P2 |
| 453 | `_clipper_pl_recombine` checks all four endpoint combinations (front/back) because Clipper does not preserve polyline orientation. A zero-length degenerate polyline where `front == back` could trigger all four branches and corrupt the merge | ClipperUtils.cpp:_clipper_pl_recombine | Low | P3 |
| 454 | `mittered_offset_path_scaled` short-edge skip uses `*std::max_element(deltas)` to compute `lmin`. For mixed-sign deltas (caught in Debug by H437), `max_element` may return a large positive value when most deltas are negative, causing over-aggressive edge skipping that eliminates valid vertices | ClipperUtils.cpp:mittered_offset_path_scaled | High | P1 |
| 455 | `sin_min_parallel = 1.0` hardcoded threshold: only perfect cross-product = 1.0 triggers the "truly parallel" classification. The commented-out alternative uses a tolerance. Edges with cross-product just below 1.0 fall into the convex-corner branch and get an extra offset point — harmless but slightly wasteful | ClipperUtils.cpp:mittered_offset_path_scaled | Low | P3 |
| 456 | `variable_offset_inner/outer`: `deltas` vector must have exactly `expoly.holes.size()+1` entries. Mismatch is asserted in Debug only. In Release, out-of-bounds access to `deltas[1 + &hole - expoly.holes.data()]` causes undefined behavior | ClipperUtils.cpp:variable_offset_inner/outer | High | P1 |
| 457 | `variable_offset_outer` has a copy-paste comment error: "Verify that the deltas are all non positive" but the assertion checks `delta >= 0` (non-negative). Comment is wrong; code is correct | ClipperUtils.cpp:variable_offset_outer | Low | P3 |

---

## Session 30 — Model.hpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 458 | Raw-pointer ownership vectors (`ModelObjectPtrs`, `ModelVolumePtrs`, `ModelInstancePtrs`) throughout `Model`. `emplace_back(new T(...))` is not exception-safe: if `emplace_back` throws after the `new`, the allocated object is leaked. No RAII wrapper at the vector-element level | Model.hpp:ModelObjectPtrs / ModelVolumePtrs | High | P1 |
| 459 | Static members `Model::extruderParamsMap` and `Model::printSpeedMap` are global mutable state shared across ALL Model instances. Written by UI thread via `setExtruderParams()` / `setPrintSpeedTable()`; read by slicing thread (`getThermalLength`, `findMaxSpeed`, `getadhesionCoeff`). No mutex protects either map — concurrent read/write is a data race (UB) | Model.hpp:Model::extruderParamsMap / printSpeedMap | High | P1 |
| 460 | `assign_clone` (via `OBJECTBASE_DERIVED_COPY_MOVE_CLONE` macro) calls `assign_copy` first then `assign_new_unique_ids_recursive()`. If `assign_new_unique_ids_recursive()` throws (allocation failure), the object is left in a partially-cloned state with mixed source and new IDs — an invalid state with no rollback | Model.hpp:OBJECTBASE_DERIVED_COPY_MOVE_CLONE | Medium | P2 |
| 461 | `ModelMaterial` is non-moveable (`move ctor = delete`) and non-assignable. This prevents direct STL container storage; `ModelMaterialMap` uses raw pointers as a workaround, reintroducing manual lifetime management | Model.hpp:ModelMaterial | Medium | P2 |
| 462 | `LayerHeightProfile::set()` compares `m_data != data` before assignment — O(N) comparison on every call. For large adaptive-layer profiles this can be slow on high-frequency UI update paths (e.g., slider drag events) | Model.hpp:LayerHeightProfile::set | Low | P3 |
| 463 | `CutConnector` stores a full `Transform3d` (4×4 float matrix, 128 bytes) plus a `Vec3d pos`. Rotation is stored redundantly in both Euler-style fields and as a matrix component. Divergence between `pos` and the matrix's translation component is possible | Model.hpp:CutConnector | Low | P3 |
| 464 | Five separate bounding-box caches (`m_bounding_box_approx`, `m_bounding_box_exact`, `m_raw_bounding_box`, `m_raw_mesh_bounding_box`, `m_min_max_z`) each controlled by a separate boolean flag. Any geometry-mutating method must call `invalidate_bounding_box()` to reset all five. A missed call returns stale cached bounds with no diagnostic | Model.hpp:ModelObject bounding box caches | High | P1 |
| 465 | `origin_translation` is a cumulative offset applied by `center_around_origin()`. New volumes added after centering must manually apply the same offset to stay aligned. There is no enforcement mechanism — only a comment documents this requirement | Model.hpp:ModelObject::origin_translation | Medium | P2 |
| 466 | Bounding-box caches (`m_bounding_box_*_valid` flags) are persisted into the Undo/Redo archive. If geometry changes between save and load (e.g. partial undo of a volume), the loaded validity flags may be stale. The archived valid flag is trusted without re-validation on load | Model.hpp:ModelObject::load (cereal) | Medium | P2 |
| 467 | `ModelObject::load()` compares volume IDs after deserialization. If they changed (undo/redo of a volume-add/delete), `save_object_mesh()` is called unconditionally — filesystem write on every such Undo/Redo load. This is a BBS-specific behaviour absent from upstream PrusaSlicer | Model.hpp:ModelObject::load | Medium | P2 |
| 468 | `FacetsAnnotation::get_facets()` and `get_facets_strict()` reconstruct a full `TriangleSelector` from the bitstream on every call — O(N·triangles) with no intermediate result cache. Hot-path callers in tight loops (per-slice) will incur repeated deserialization overhead | Model.hpp:FacetsAnnotation::get_facets | Medium | P2 |
| 469 | `ModelVolume` 2D convex-hull cache uses exact floating-point matrix equality (`new_matrix.matrix() != m_cached_trans_matrix.matrix()`) for cache invalidation. Floating-point drift from repeated transform compositions can cause spurious cache misses or — with NaN values — permanent cache misses | Model.hpp:ModelVolume::m_cached_trans_matrix | Medium | P2 |
| 470 | `mmuseg_extruders` / `mmuseg_ts` are mutable quick-access caches for MMU-painted extruder IDs. If `mmu_segmentation_facets` is modified without touching its timestamp (via raw `m_data` access from a friend class), the cache is not invalidated and stale extruder IDs are returned | Model.hpp:ModelVolume::mmuseg_extruders | Medium | P2 |
| 471 | If `m_convex_hull` is null after deserialization (released from Undo/Redo stack to conserve memory) and `m_mesh` is non-empty, `calculate_convex_hull()` is called synchronously on the Undo/Redo path. For large meshes this is O(N·log N) and can cause visible UI stutter | Model.hpp:ModelVolume::load | Low | P3 |
| 472 | `ModelVolume::load()` detects mesh changes by comparing transformation and facet annotation timestamps; if changed, `save_object_mesh()` is called — filesystem I/O on every Undo/Redo operation that touches geometry. BBS-specific behaviour | Model.hpp:ModelVolume::load | Low | P3 |
| 473 | `ModelInstance::is_printable()` checks three conditions: `object->printable AND printable AND print_volume_state == Inside`. If `print_volume_state` is stale (not updated after the object was moved), the method may return an incorrect result. Update requires an explicit call to `update_instances_print_volume_state()` | Model.hpp:ModelInstance::is_printable | Medium | P2 |
| 474 | `ModelInstance::m_assemble_initialized` is NOT serialized. After deserialization from an Undo/Redo archive, `m_assemble_initialized` is always `false` even if `m_assemble_transformation` was serialized. The assembly view must re-initialize after every Undo/Redo operation | Model.hpp:ModelInstance serialization | Low | P3 |
| 475 | `Model::plates_custom_gcodes` is indexed by plate index but the Model does not store the plate count. If plates are deleted, stale entries remain without cleanup. Wipe-tower code must range-check before indexing | Model.hpp:Model::plates_custom_gcodes | Medium | P2 |
| 476 | `Model::read_from_file()` is a large multi-format dispatch that modifies the Model in-place. On load failure the Model may be partially populated. There is no atomic "load-or-fail" guarantee; callers must handle partial state | Model.hpp:Model::read_from_file | Medium | P2 |
| 477 | `Model::clear_objects()` iterates and `delete`s all objects in a loop. If any `ModelObject` destructor throws (unlikely but possible with plugin/derived-class overrides), the loop aborts, leaking the remaining objects. No swap-idiom exception-safe cleanup is used | Model.hpp:Model::clear_objects | Low | P3 |
| 478 | Many `Model` fields are NOT serialized into Undo/Redo archives: `plates_custom_gcodes`, `curr_plate_index`, `design_info`, `model_info`, `profile_info`, all `stl_design_*` metadata, `backup_path`, `object_backup_id_map`, `calib_pa_pattern`. After Undo/Redo these fields retain their pre-undo values — the undo stack does not fully restore Model metadata state | Model.hpp:Model::save (cereal) | Medium | P2 |

---

## Sessions 31–32 — Model.cpp

| Hazard # | Description | Location | Severity | Priority |
|----------|-------------|----------|----------|----------|
| 479 | `CONST_FILAMENTS` is a compile-time constant but accessed via runtime index arithmetic (`get_real_filament_id`). An out-of-range `id` silently produces `""` — the same value as the "no colour" sentinel — so a bounds-check failure is indistinguishable from a valid "uncoloured" result | Model.cpp:CONST_FILAMENTS | Medium | P2 |
| 480 | `Model::assign_copy(const Model&)`: the material copy loop first shallow-copies the entire map, then re-allocates each value pointer. If `new ModelMaterial(...)` throws mid-loop, already-reallocated earlier entries are leaked — exception safety is at most basic, not strong | Model.cpp:Model::assign_copy | Medium | P2 |
| 481 | `calib_pa_pattern` is copied TWICE in `assign_copy(const Model&)` — both `if (rhs.calib_pa_pattern)` blocks are identical and execute sequentially. The first `make_unique` allocation is immediately discarded when the second block overwrites it. A waste-allocation paste-duplicate bug; the final result is correct | Model.cpp:Model::assign_copy:calib_pa_pattern | Low | P3 |
| 482 | `Model::assign_copy(Model&&)` (move overload): `design_info`, `model_info`, and `profile_info` are transferred by copy-then-reset of the source `shared_ptr` rather than by `std::move`. This is a "move-by-copy-plus-reset" anti-pattern — functionally correct for shared ownership but misleading and potentially slower | Model.cpp:Model::assign_copy(Model&&) | Low | P3 |
| 483 | `Model::~Model()` calls `remove_backup()` (recursive filesystem deletion) inside a destructor. If the filesystem call throws, `std::terminate` is invoked because exceptions must not propagate out of destructors. The backup directory may be left on disk in that case | Model.cpp:Model::~Model | Medium | P2 |
| 484 | `read_from_file()` invokes the `objFn` callback mid-load, before `read_from_file` returns. If the callback throws or re-enters Model loading (e.g. triggering another file-open dialog), the partially-constructed Model and temporary `plate_data` pointer are still live on the stack — reentrant use is not protected | Model.cpp:Model::read_from_file:objFn | Medium | P2 |
| 485 | The `.oltp` extension is dispatched to `load_stl()` with a special argument of `256` whose meaning is undocumented at the call site. `[UNCLEAR]` — likely a resolution or segment-count hint; requires reading `load_stl` implementation to verify | Model.cpp:Model::read_from_file:.oltp | Low | P3 |
| 486 | `Model::add_object(name, path, mesh)`: `source.object_idx` is set to `this->objects.size() - 1` after `push_back`. Correct for single-threaded use, but if two threads concurrently call `add_object()` on the same Model, both may derive the same index | Model.cpp:Model::add_object | Medium | P2 |
| 487 | TOCTOU on `object_backup_id_map`: the `find()` + `emplace()` + `erase()` sequence in `add_object(const ModelObject&)` is not atomic. Concurrent callers on the same source object can both pass the `find()` check, both `emplace()` the new ID, and the second `erase()` operates on an already-erased iterator — undefined behaviour | Model.cpp:Model::add_object(const ModelObject&) | High | P1 |
| 488 | `Model::delete_object(size_t idx)` has no bounds check. Callers must ensure `idx < objects.size()`. After erase, any stored integer index into `objects[]` for `j > idx` becomes stale (no update hook) | Model.cpp:Model::delete_object | Medium | P2 |
| 489 | `Model::clear_objects()` calls `delete_object_mesh()` (filesystem I/O) inside the loop with no lock. Concurrent calls from a UI thread and a background save thread can race | Model.cpp:Model::clear_objects | High | P1 |
| 490 | After `Model::collect_reusable_objects()`, `this->objects` is empty but the Model still exists. Any code holding raw pointers to the transferred objects must not access them via the original Model — no ownership transfer signal is provided | Model.cpp:Model::collect_reusable_objects | Medium | P2 |
| 491 | `Model::get_object_backup_id()` (non-const): `find` + `insert` is not atomic. Concurrent callers can both observe `end()` and both insert, creating duplicate UUID allocations | Model.cpp:Model::get_object_backup_id | High | P1 |
| 492 | `Model::get_object_backup_id()` (const overload) dereferences `find(...)` without checking for `end()`. Calling on an object with no pre-assigned backup ID causes undefined behaviour (dereference of end iterator) | Model.cpp:Model::get_object_backup_id (const) | High | P1 |
| 493 | `Model::get_backup_path()` calls `std::localtime()` which is not thread-safe on most platforms (uses a static internal buffer). Concurrent calls from multiple threads will race on the `tm*` pointer | Model.cpp:Model::get_backup_path:localtime | High | P1 |
| 494 | `get_backup_path()`: `exists()` + `create_directories()` has a TOCTOU gap. Two concurrent callers can both see "doesn't exist", both call `remove_all()`, and the second's `remove_all` can delete files the first thread just wrote | Model.cpp:Model::get_backup_path:TOCTOU | Medium | P2 |
| 495 | `Model::set_backup_path()` calls `remove_backup()` synchronously. If the filesystem operation fails or throws, `backup_path` is already cleared to prevent double-delete, but the old backup directory is leaked on disk | Model.cpp:Model::set_backup_path | Low | P3 |
| 496 | `Model::load_from()` resets `calib_pa_pattern` on the source model but does NOT transfer (move) it to `this`. The calibration pattern from a loaded project file is silently discarded | Model.cpp:Model::load_from:calib_pa_pattern | Medium | P2 |
| 497 | `ModelObject::add_volume()` calls `save_object_mesh()` on every invocation. For batch operations (e.g. loading a file with many volumes) this triggers O(N) full-mesh serialisation passes — one per volume added. No deferred/batched backup strategy exists | Model.cpp:ModelObject::add_volume | Medium | P2 |
| 498 | `ModelObject::add_volume_with_shared_mesh()`: both volumes share the same `TriangleMesh` instance via `shared_ptr`. Mutating the mesh on either volume (e.g. `transform_this_mesh`) will affect both. No write-protection is in place | Model.cpp:ModelObject::add_volume_with_shared_mesh | High | P1 |
| 499 | `ModelObject::delete_volume()`: the transform-collapse step sets `inst.transformation = inst * v_t` for every instance. If any resulting matrix is non-decomposable (mixed rotation + non-uniform scale), `Geometry::Transformation`'s Euler decomposition may produce incorrect angles, silently corrupting instance placement | Model.cpp:ModelObject::delete_volume | Medium | P2 |
| 500 | `ModelObject::update_min_max_z()` uses ONLY `instances.front()` transform. For objects with multiple instances at different Z offsets, `m_bounding_box_exact` Z values reflect only the first instance. `min_z()` / `max_z()` return incorrect results for all other instances | Model.cpp:ModelObject::update_min_max_z | Medium | P2 |
| 501 | `ModelObject::raw_bounding_box()` uses `instances.front()` only (same single-instance assumption as `update_min_max_z`). Throws `InvalidArgument` if called with no instances; for multi-instance objects at different Z offsets it returns a bbox valid only for instance 0 | Model.cpp:ModelObject::raw_bounding_box | Medium | P2 |
| 502 | `ModelObject::center_around_origin()` accumulates `origin_translation` on each call without resetting. Calling it twice without resetting the translation double-counts the centering shift, misplacing G-code coordinates. No guard or assertion prevents this | Model.cpp:ModelObject::center_around_origin | High | P1 |
| 503 | Dead debug code inside `#if 0 / #else` block in `convex_hull_2d()`: the old vertex-level projection path is retained in the disabled branch alongside commented-out SVG debug output. Dead code adds confusion during porting | Model.cpp:ModelObject::convex_hull_2d:#if0 | Low | P3 |
| 504 | `ModelObject::translate()` incrementally updates only `m_bounding_box_approx` and `m_bounding_box_exact` caches; `raw_bounding_box`, `m_min_max_z`, and `convex_hull_2d` caches are NOT updated. After a `translate()`, `raw_bounding_box()` may return a stale value until explicitly invalidated | Model.cpp:ModelObject::translate | High | P1 |
| 505 | `ModelObject::scale()` invalidates all ModelObject bounding-box caches via `invalidate_bounding_box()` but does NOT invalidate the per-`ModelVolume` `convex_hull_2d` cache. That cache uses a lazy matrix-comparison path on next access; a brief window exists where the 2D hull is stale | Model.cpp:ModelObject::scale | Low | P3 |
| 506 | `ModelObject::rotate(Axis)` calls `center_around_origin()` after every axis-aligned rotation. In a loop over multiple axes, `origin_translation` accumulates each centering shift, potentially producing unexpected mesh placement for G-code export (cross-ref H502) | Model.cpp:ModelObject::rotate(Axis) | High | P1 |
| 507 | `ModelObject::rotate(Vec3d axis)` has the same `center_around_origin()` accumulation risk as H506, compounded if both `rotate` overloads are called in sequence | Model.cpp:ModelObject::rotate(Vec3d) | High | P1 |
| 508 | `ModelObject::scale_mesh_after_creation()` and `ModelVolume::scale_geometry_after_creation()` use `const_cast` to mutate mesh vertices through a `shared_ptr`. The invariant "mesh is not shared" is not enforced at runtime. Calling these methods when `shared_ptr::use_count() > 1` silently corrupts all sibling volumes | Model.cpp:scale_mesh_after_creation / scale_geometry_after_creation | High | P1 |
| 509 | `ModelObject::convert_units()`: `vol->source.object_idx` is assigned `new_objects.size()` before the new object is pushed — correct for single-threaded use. Concurrent calls with the same `new_objects` vector would produce stale indices | Model.cpp:ModelObject::convert_units | Medium | P2 |
| 510 | `ModelObject::bake_xy_rotation_into_meshes()`: after baking, mesh vertices are in a new coordinate space. Bounding boxes are explicitly invalidated, but `FacetsAnnotation` triangle IDs are NOT re-indexed. If the mesh is split or welded during transform, painted face regions silently refer to wrong triangles | Model.cpp:ModelObject::bake_xy_rotation_into_meshes | High | P1 |
| 511 | `bake_xy_rotation_into_meshes()`: the `Eigen::Quaterniond` decomposition path in `extract_euler_angles` may not round-trip exactly through instance rotation → bake → reset. Floating-point drift can make a 90° rotation appear non-90° to the next bake call, triggering redundant re-bakes | Model.cpp:ModelObject::bake_xy_rotation_into_meshes | Medium | P2 |
| 512 | `ModelObject::split()` (multi-volume path): each volume mesh is `std::move`'d into `all_meshes` before `FaceDetector` runs. If `split()` throws after this point (e.g. from `add_object()`), the original volume's mesh is in a moved-from (empty) state with no recovery path | Model.cpp:ModelObject::split | High | P1 |
| 513 | `ModelObject::split()` assembles a transform-correction using multiple floating-point matrix inversions. If any instance transformation matrix is near-singular (e.g. zero scale), the inverse produces NaN/inf values that silently corrupt all `assemble_offset` fields with no error reporting | Model.cpp:ModelObject::split | High | P1 |
| 514 | `ModelVolume::get_extruders()` is a `const` method that mutates `mmuseg_extruders` and `mmuseg_ts` (both `mutable`). Two threads calling `get_extruders()` concurrently on the same volume can both detect a stale timestamp and race on updating the cache — data race (UB) | Model.cpp:ModelVolume::get_extruders | High | P1 |
| 515 | `ModelVolume::center_geometry_after_creation()` uses `const_cast` to mutate `m_mesh` and `m_convex_hull` through their `const shared_ptr`. Assumes `use_count() == 1`; if the mesh is shared (via `add_volume_with_shared_mesh`, H498), both volumes are silently shifted, corrupting the second volume's world position | Model.cpp:ModelVolume::center_geometry_after_creation | High | P1 |
| 516 | `ModelVolume::calculate_convex_hull_2d()` returns early if `m_convex_hull->its.vertices` is empty (degenerate hull), without updating `m_convex_hull_2d`. The caller `get_convex_hull_2d()` may then use a stale `m_convex_hull_2d` from a prior call with a different transform | Model.cpp:ModelVolume::calculate_convex_hull_2d | Medium | P2 |
| 517 | `ModelVolume::get_convex_hull_2d()`: `need_recompute()` compares rotation/scale/mirror via exact `Vec3d` inequality (`!=`). Two transforms that are numerically equivalent but decomposed via different Euler paths will trigger spurious full recomputes (same root cause as H469) | Model.cpp:ModelVolume::get_convex_hull_2d | Medium | P2 |
| 518 | `ModelVolume::transform_this_mesh()`: after the transform, `FacetsAnnotation` data (`supported_facets`, `seam_facets`, `mmu_segmentation_facets`) still refers to the old triangle indexing. If `fix_left_handed` winding repair renumbers triangles, all painted face regions are silently misaligned. No re-indexing or invalidation is performed | Model.cpp:ModelVolume::transform_this_mesh | High | P1 |
| 519 | `Model::setExtruderParams()` double-insert bug: for `i==0`, the loop inserts at key `0` AND key `1` (i+1). For `i==1`, key `1` is overwritten and key `2` is added. Result: key `0` always holds the first extruder's params; key `1` first gets extruder-0 data then extruder-1 data. Documented with `[UNCLEAR]` — the intent of key 0 as a "default" is not stated anywhere | Model.cpp:Model::setExtruderParams | Medium | P2 |
| 520 | `obj_import_vertex_color_deal()`: the hex token-to-filament mapping used in `CONST_FILAMENTS` encodes triangle-subdivision bitstream tokens, not plain filament indices. Changing the Clipper/TriangleSelector bitstream format without updating `CONST_FILAMENTS` silently breaks OBJ colour import | Model.cpp:obj_import_vertex_color_deal:CONST_FILAMENTS | Medium | P2 |
| 521 | `obj_import_vertex_color_deal()`: `vertex_filament_ids[face[0..2]]` are array-indexed without bounds checking. If the OBJ file contains vertex indices ≥ `vertex_filament_ids.size()`, this is an out-of-bounds read — undefined behaviour. Also: on `its.indices.size() != vertex_filament_ids.size()`, `reset()` + `reserve()` leave `FacetsAnnotation` in a partially-initialised state | Model.cpp:obj_import_vertex_color_deal | High | P1 |
| 522 | `obj_import_face_color_deal()`: same partial-initialisation hazard as H521 — `reset()` and `reserve()` are called before the loop, so a mid-loop exception leaves `FacetsAnnotation` in an inconsistent state | Model.cpp:obj_import_face_color_deal | Medium | P2 |
| 523 | `ModelInstance::get_auto_brim_width()` dead code: the function body begins with `return 0.;` on its very first line. All subsequent logic (adhesion coefficient calculation, thermal length heuristic) is unreachable. The dead implementation compiles and links but is never executed | Model.cpp:ModelInstance::get_auto_brim_width | Low | P3 |
| 524 | `FacetsAnnotation::get_triangle_as_string()` / `set_triangle_from_string()`: the 4-bit nibble hex encoding has no version tag. Any change to bit order, nibble grouping, or character set silently corrupts all previously exported `.3mf` files that contain painted faces | Model.cpp:FacetsAnnotation::get_triangle_as_string | High | P1 |
| 525 | `FacetsAnnotation::set_triangle_from_string()` asserts strict triangle-index ordering (`back().triangle_idx < triangle_id`). In Release builds this assertion is absent; out-of-order calls produce a corrupt bitstream that will crash or produce wrong segmentation on the next deserialization | Model.cpp:FacetsAnnotation::set_triangle_from_string | High | P1 |
| 526 | `model_volume_list_changed()` compares volumes by `(type, id, transform)` but NOT by mesh content. If a volume's mesh is replaced (same ID, same transform, different geometry), `Print::apply()` will not detect the change and will skip re-slicing — stale toolpaths after a mesh edit | Model.cpp:model_volume_list_changed | High | P1 |
| 527 | The `CEREAL_REGISTER_TYPE` block for the Model hierarchy is inside `#if 0` — cereal's polymorphic serialization registration is DISABLED. Polymorphic deserialization of `ModelObject` / `ModelVolume` / `ModelInstance` subclasses via cereal base-class pointers cannot work. If any future code attempts it, it will silently fail at runtime | Model.cpp:#if 0 cereal block | Medium | P2 |
| 528 | `arrange()` contains a dead `#if 0` block with a cleaner first implementation; the active `#else` branch uses a `goto ENDSORT` label inside nested loops for a binary-insert fast path. This pre-C++11 pattern must be replicated exactly during porting or replaced with `std::lower_bound` + `insert` | Geometry.cpp:arrange:#if0/goto | Low | P3 |
| 529 | `extract_euler_angles()` uses `rotation_matrix.eulerAngles(2,1,0)` (Eigen ZYX). Eigen's `eulerAngles()` has documented gimbal-lock ambiguities near Y≈±90° and non-unique solutions for Y==0. The returned angles are not round-trip stable; any port must choose a quaternion or rotation-matrix representation instead | Geometry.cpp:extract_euler_angles | High | P1 |
| 530 | `transform3d_from_string()` tokenises with `std::istringstream` then calls `::atof()`. `atof` is locale-dependent: European locales using ',' as decimal separator will silently parse "1.5" as "1" (truncated integer). All transforms loaded from string representation are corrupted on non-C locales | Geometry.cpp:transform3d_from_string:atof | High | P1 |
| 531 | `Transformation::volume_to_bed_transformation()` case 2 (least-squares fit): the denominator is `(source_size - target_size).squaredNorm()`. When the source and target bounding boxes are identical (or degenerate/flat), the denominator is zero and the resulting scale is NaN. No guard or fallback exists | Geometry.cpp:volume_to_bed_transformation:degenerate-bbox | High | P1 |
| 532 | `mat_around_a_point_rotate()` calls `temp_world.get_matrix().inverse()` without checking invertibility. If the world transformation matrix contains zero scale on any axis (degenerate object), the inverse is undefined and produces a matrix of NaN/inf values that silently propagate to all subsequent transforms | Geometry.cpp:mat_around_a_point_rotate:unchecked-inverse | High | P1 |
| 533 | `Transformation::set_mirror()` silently clamps any non-{-1,0,1} mirror component to ±1 without warning. Out-of-range input is normalised without signalling an error; callers with bugs in mirror construction will not be notified | Geometry.cpp:Transformation::set_mirror:silent-normalise | Low | P3 |
| 534 | `Transformation::reset_rotation()` and `reset_scaling_factor()` both construct a `TransformationSVD` object which performs a full Jacobi SVD decomposition. These methods are called from UI interaction handlers (potentially every mouse-drag frame), making SVD decomposition a per-tick overhead cost | Geometry.cpp:TransformationSVD:per-tick-SVD | Medium | P2 |
| 535 | `project_point_to_segment()` asserts `t` is in `[-1e-6, 1+1e-6]` but does NOT clamp `t` to `[0,1]`. In Release builds the assert is absent; `t` values outside `[0,1]` extrapolate beyond the segment endpoints rather than clamping to the nearest endpoint. Callers that assume projected points lie on the segment will receive incorrect results | Geometry.cpp:project_point_to_segment:unclamped-t | Medium | P2 |
| 536 | `Transformation::set_scaling_factor()` only asserts all components > 0 in Debug builds. In Release builds a zero-scale factor is silently accepted, producing a singular (non-invertible) transform matrix that propagates NaN into all downstream mesh, bbox, and slicing computations | Geometry.cpp:Transformation::set_scaling_factor:debug-only-assert | High | P1 |
| 537 | `TransformationSVD`: when `mirror` is true, `m0` is pre-multiplied by `diag(-1,1,1)` before SVD. This pre-multiply encodes the mirror convention into the decomposition. Any port must replicate this exact diagonal pre-multiplication or the rotation/scale/mirror components will be extracted incorrectly | Geometry.cpp:TransformationSVD:mirror-pre-multiply | High | P1 |
| 538 | `generate_transform()` calls `x_dir.normalized()`, `y_dir.normalized()`, `z_dir.normalized()` without checking for zero-length input vectors. `Eigen::Vector3d::normalized()` on a zero vector returns a zero vector (not a NaN, but semantically undefined). The resulting transform matrix has zero rows and will corrupt all subsequent computations silently | Geometry.cpp:generate_transform:zero-vector-normalized | High | P1 |
| 539 | `EdgeGrid::Contour` stores raw `const Point*` pointers (`m_begin`/`m_end`) into caller-owned `Polygon` / `ExPolygon` / `Points` containers. The Grid does not ref-count or track the source data. If the source container is moved, resized, or destroyed while the Grid is alive, all segment lookups produce undefined behaviour. Particularly dangerous when Grid and source polygons are co-members of a struct (C++ move semantics silently invalidate pointers) | EdgeGrid.cpp:Contour:raw-pointer-lifetime | High | P1 |
| 540 | `create_from_m_contours()` Bresenham rasterisation uses `int64_t` accumulators of the form `int64_t(coord_t_value) * m_resolution`. `coord_t` is `int32_t`; each individual product fits in `int64_t`. However in extreme cases (multi-metre print areas in nanometre units with large resolution values) the product could approach `INT64_MAX`. The invariant is not asserted and no documentation exists on the expected maximum `coord_t` range | EdgeGrid.cpp:create_from_m_contours:accumulator-overflow | Low | P3 |
| 541 | `calculate_sdf()` initialises the `signs` array to byte `4` ("not yet propagated"). The flood-fill relies on adjacent grid corners seeded from closed contour edges (bit 1 set). If no closed contours are present (all open polylines) or the contours are degenerate, the seed phase produces no bit-1 entries. The resulting SDF is all-positive (outside) even for points inside holes. The `assert(contour.closed())` guard fires in Debug only | EdgeGrid.cpp:calculate_sdf:open-polyline-signum | Medium | P2 |
| 542 | `signed_distance_bilinear()` clamps out-of-bounds query points to the grid boundary then adds the out-of-bounds offset linearly to the interpolated value. This extrapolation is unbounded — for a point far outside the grid the returned value is arbitrarily large. Callers expecting `FLT_MAX` or `NaN` for out-of-bounds points will receive a finite but arbitrarily large (or large negative) distance | EdgeGrid.cpp:signed_distance_bilinear:unbounded-extrapolation | Medium | P2 |
| 543 | In `closest_point_signed_distance()` and `signed_distance_edges()`, the vertex-closest-point path (t_pt < 0) determines sign by cross product `det` of adjacent segments. `assert(det != 0)` fires in Debug for collinear adjacent segments (degenerate 180° vertex). In Release the assert is absent; `sign_min` retains its prior value or stays 0, producing a silently wrong sign for that vertex | EdgeGrid.cpp:closest_point_signed_distance:det-zero-release | Medium | P2 |
| 544 | `contours_simplified()` uses `goto end_of_poly` to break out of a nested for loop when the polygon chain closes. Same pattern as H528 in Geometry.cpp. Must be restructured (e.g. lambda or flag) during any port to a language without labeled break/goto | EdgeGrid.cpp:contours_simplified:goto-end_of_poly | Low | P3 |
| 545 | `intersecting_edges()` computes a `jfirst` flag to determine canonical pair ordering, but both branches of the ternary operator `?:` emit the identical pair `(icontour, ipt, jcontour, jpt)`. The intended swapped ordering for the `jfirst=true` case is never produced. The subsequent `sort_remove_duplicates()` is therefore redundant. Output is functionally correct but the deduplication step wastes CPU | EdgeGrid.cpp:intersecting_edges:jfirst-dead-branch | Low | P3 |
| 546 | `Grid::inside()` and `Grid::line_cell_intersect()` live inside a `#if 0` block and are permanently dead. `inside()` has `//FIXME finish this!` comments and zero-initialised local variables (`vx = 0`, `det = 0`) making its logic nonfunctional. Do NOT port this implementation; use `signed_distance()` or `cell_inside_or_crossing()` for inside/outside tests | EdgeGrid.cpp:#if0-inside:unfinished-dead-code | High | P1 |
| 547 | `calculate_sdf()` stores the distance field as `std::vector<float>`. With `coord_t = int32_t` in nanometre units, distances above ~16,777,216 nm (~16 mm) lose sub-nanometre precision due to `float`'s 24-bit mantissa. For large print beds (300+ mm) the SDF has only micrometre resolution. Callers using the SDF for sub-millimetre seam or support placement will see quantisation artefacts | EdgeGrid.cpp:calculate_sdf:float-precision | Medium | P2 |
| 548 | `support_material_flow()` uses `support_filament - 1` as array index without checking for 0; when `support_filament == 0` wraps to `SIZE_MAX` → out-of-bounds read (UB) | Flow.cpp:support_material_flow | High | P1 |
| 549 | `Flow::operator==` ignores `m_spacing` — two Flow objects with identical w/h/nozzle/bridge but different spacing compare equal, breaking any deduplication or change-detection that relies on equality | Flow.cpp:Flow::operator== | Medium | P2 |
| 550 | `Flow::with_cross_section()` increasing-flow branch computes new width using old cross-sectional area instead of new area — produces wrong width for any flow increase > original | Flow.cpp:Flow::with_cross_section | Medium | P2 |
| 551 | `BRIDGE_EXTRA_SPACING = 0.05` macro is raw mm, not scaled — compared against scaled values in some contexts but used directly as mm in others; boundary is implicit and untested | Flow.hpp:BRIDGE_EXTRA_SPACING | Low | P3 |
| 552 | Dead `#if 0` block in `Flow::new_from_config_width()` for first-layer width fallback logic — removes a valid fallback path; may cause `FlowErrorNegativeSpacing` for edge-case configs | Flow.cpp:new_from_config_width:#if0 | Low | P3 |
| 553 | `chain_and_reorder_extrusion_entities` calls `static_cast<ExtrusionEntityCollection*>` unconditionally on every entity — UB if any entity in the vector is not an `ExtrusionEntityCollection` | ShortestPath.cpp:chain_and_reorder_extrusion_entities | High | P1 |
| 554 | `chain_lines` declares `static const double point_distance_epsilon2` inside function body — ODR risk if TU is ever split or function is instantiated in multiple TUs | ShortestPath.cpp:chain_lines | Low | P3 |
| 555 | `reorder_extrusion_paths` header declares `chain` parameter by value; `.cpp` definition takes by non-const reference — declaration/definition mismatch; compiles only because template instantiation masks the discrepancy | ShortestPath.hpp/ShortestPath.cpp:reorder_extrusion_paths | Medium | P2 |
| 556 | V2 greedy chaining: `num_iter = num_segments * 16` guard — if limit hit, `first_point` stays `nullptr` and output loop exits immediately returning empty `out` with no error reporting | ShortestPath.cpp:chain_segments_greedy_constrained_reversals2_ | High | P1 |
| 557 | `chain_expolygons` uses bounding-box centroid as representative point — non-deterministic ordering for polygons with identical centroids; also used as representative point for `chain_print_object_instances` with no 2-opt post-improvement | ShortestPath.cpp:chain_expolygons/chain_print_object_instances | Low | P3 |
| 558 | `reorder_by_two_exchanges_with_segment_flipping` hardcodes `max_iterations = min(n, 100)` — if limit reached without convergence, output is silently sub-optimal with no warning; not documented as a known trade-off | ShortestPath.cpp:reorder_by_two_exchanges_with_segment_flipping | Low | P3 |
| 559 | Five dead `#if 0` blocks preserved as algorithm reference: `improve_ordering_by_segment_flipping` (1-opt), `reorder_by_three_exchanges_with_segment_flipping` (3-opt v1, O(n³)), `do_crossover` 4-span variant, `minimum_crossover_cost` Eigen matrix variant, `reorder_by_three_exchanges_with_segment_flipping2` (4-opt Eigen) — do NOT delete; critical TSP algorithm history | ShortestPath.cpp:multiple #if0 blocks | Low | P3 |
| 560 | `do_crossover` switch `default:` asserts `(i >> 6) == 2` — fires in DEBUG if `flip_min` encoding exceeds 191; silent wrong-permutation execution in RELEASE; currently safe because `minimum_crossover_cost` limits `flip_min` to ≤191 | ShortestPath.cpp:do_crossover:default | Medium | P2 |
| 561 | `MMU_Graph::append_voronoi_vertices()`: Boost.Polygon `vertex.color()` field is repurposed in three distinct phases — (1) VD_ANNOTATION enum values (1=ON_CONTOUR, 2=DELETED) during vertex classification; (2) node array indices (≥0) after nodes are written. Any reader of `vertex.color()` that does not know which phase has completed will misinterpret the value. This implicit phase-ordering is a severe porting hazard — a port must make the phase explicit with a separate map or wrapper | MultiMaterialSegmentation.cpp:append_voronoi_vertices | High | P1 |
| 562 | `extract_colored_segments()` repair path: `arc_id_to_face_lines.emplace_back(std::make_pair(-1, add_line))` where `-1` is cast to `size_t` → wraps to `SIZE_MAX`. Currently safe because the sentinel entry is never dereferenced via `used_arcs[SIZE_MAX]`, but any refactored iteration over `arc_id_to_face_lines` that reads `pair.first` as a valid arc index will OOB | MultiMaterialSegmentation.cpp:extract_colored_segments:repair-path-sentinel | High | P1 |
| 563 | `PaintedLineVisitor` distance pre-filter: all four squared-distance checks are AND'd — a line is only skipped if ALL four endpoints exceed `heuristic_thr_sqr`. This is over-conservative; the real filter is the `cos_threshold2` collinearity check below. The pre-filter adds negligible benefit while obscuring the actual acceptance criterion | MultiMaterialSegmentation.cpp:PaintedLineVisitor:heuristic-AND-filter | Low | P3 |
| 564 | `segmentation_top_and_bottom_layers()` uses `layer_idx_offset = (group_idx & 1) * num_layers` to interleave two per-layer result arrays and avoid TBB write conflicts between adjacent group workers. The merge step reads both `layer_idx` and `layer_idx + num_layers` entries. If TBB changes its blocking granularity (e.g. uses 3+ concurrent groups on a specific layer), the interleave scheme silently fails and produces incorrect or missing top/bottom coverage | MultiMaterialSegmentation.cpp:segmentation_top_and_bottom_layers:interleave-trick | High | P1 |
| 565 | `layer_color_stat()` lambda in `segmentation_top_and_bottom_layers()` always uses `nozzle_diameter.get_at(0)` — hardcoded extruder index 0 — for computing `outer_wall_line_width`. For multi-extruder setups where each extruder has a different nozzle diameter, all color slots use extruder 0's nozzle diameter. This produces incorrect wall-width constraints for extruders 2+ | MultiMaterialSegmentation.cpp:layer_color_stat:hardcoded-extruder-0 | High | P1 |
| 566 | `MMU_Graph::append_edge()` deduplication is O(degree) per insertion — scans both adjacency lists linearly. For high-valence Voronoi vertices (rare but possible near closely-spaced contour corners), this is O(n²) per node build. No upper bound on node degree is asserted or documented | MultiMaterialSegmentation.cpp:append_edge:O-degree-dedup | Low | P3 |
| 567 | `build_graph()` `force_edge_adding[]` uses pointer-arithmetic indexing (`&c_poly - &color_poly.front()`). If `color_poly` contains empty polygon entries, `polygon_idx_offset` will contain zero-length entries and `get_global_index()` will return duplicate global indices for consecutive polygons — silently corrupting the graph | MultiMaterialSegmentation.cpp:build_graph:force_edge_adding-pointer-arithmetic | Medium | P2 |
| 568 | `merge_segmented_layers()` indexing convention: `top_and_bottom_layers` is sized `[num_facets_states][num_layers]` (extruder index 1-based in first dimension), while `segmented_regions_merged` is sized `[num_layers][num_facets_states-1]` (extruder index 0-based). These inverted layouts (`[extruder][layer]` vs `[layer][extruder-1]`) must be translated identically in any port, or all extruder assignments shift by 1 | MultiMaterialSegmentation.cpp:merge_segmented_layers:indexing-convention | High | P1 |
| 569 | `segmentation_by_painting()`: `static int iRun` inside `#ifdef MM_SEGMENTATION_DEBUG` is shared across all calls from any thread. Currently safe (segmentation is single-threaded in the print pipeline), but the static is unprotected by a mutex. Any future concurrency of `segmentation_by_painting()` will race on `iRun` | MultiMaterialSegmentation.cpp:segmentation_by_painting:static-iRun | Medium | P2 |
| 570 | `fuzzy_skin_segmentation_by_painting()`: `flow()` called with uniform `print_object.config().layer_height` for all regions when computing `max_external_perimeter_width`. For variable-layer-height prints, individual layers may have different heights and correspondingly different optimal external perimeter widths. Using the uniform height produces an approximation that may over- or under-cut fuzzy skin depth on specific layers | MultiMaterialSegmentation.cpp:fuzzy_skin_segmentation:uniform-layer-height | Low | P3 |
| 571 | `ray_circle_intersections()` calls a non-existent helper `ray_circle_intersections_r2_lv2_c2` (the suffix `2` at the end is a typo; only `ray_circle_intersections_r2_lv2_c1` exists). This will produce a linker error if `ray_circle_intersections()` is ever instantiated. Currently safe only because the call site is guarded by a template argument that happens not to be used | Geometry/Circle.cpp:ray_circle_intersections:typo-suffix | High | P1 |
| 572 | `circle_ransac()` seeds its `std::mt19937` with a default-constructed `std::random_device` that always returns 0 on this build target (GCC Linux), making the RANSAC sample selection fully deterministic and reproducible but also exploitable by adversarial input ordering | Geometry/Circle.cpp:circle_ransac:deterministic-seed | Medium | P2 |
| 573 | `arc_center()`: when the two arc endpoints are antipodal (angle difference = π), the parabola midpoint formula collapses to the segment midpoint, not the arc center. The function returns a geometrically wrong point silently | Geometry/Circle.cpp:arc_center:antipodal-degenerate | Low | P3 |
| 574 | `circle_center()` (3-point version): when three points are collinear, the function returns their midpoint as a fallback rather than ∞/NaN/error. Callers that store the result and treat it as a valid center will operate on a nonsensical point with no indication of failure | Geometry/Circle.cpp:circle_center:collinear-fallback | Low | P3 |
| 575 | `welzl()` minimax circle algorithm has worst-case O(n!) recursive depth with adversarial input ordering despite the stated O(n) expected complexity. The randomized permutation at the call site is critical; removing or weakening it restores exponential behavior | Geometry/Circle.cpp:welzl:worst-case-exponential | Low | P3 |
| 576 | `circle_center_taubin_newton()` returns NaN on divergence (when the Newton iteration does not converge within the maximum number of iterations). Callers must check `std::isnan(result.center.x())` before using the result; none currently do | Geometry/Circle.cpp:circle_center_taubin_newton:NaN-on-divergence | Low | P3 |
| 577 | `convex_hulll()` (triple-l) — function name typo in ConvexHull.hpp/cpp. The misspelled name is the only exported symbol; any future correct-spelling call site will fail to link | Geometry/ConvexHull.cpp:convex_hulll:typo | Low | P3 |
| 578 | `convex_hull(Pointf3s)` silently ignores Z, projects all 3-D points to XY and returns a 2-D convex hull. No documentation or comment warns the caller that Z is lost. A port that passes 3-D points expecting a 3-D hull will silently receive a 2-D result | Geometry/ConvexHull.cpp:convex_hull_3d:ignores-Z | Low | P3 |
| 579 | `decompose_convex_polygon_top_bottom()` returns empty top/bottom chains for degenerate polygons (area = 0, all collinear points, or single-point polygon). Callers that assert non-empty results will crash; those that iterate silently produce no output | Geometry/ConvexHull.cpp:decompose_convex_polygon_top_bottom:empty-on-degenerate | Low | P3 |
| 580 | `VoronoiVisualUtils.hpp` contains an embedded copy of `boost::polygon::voronoi_visual_utils<CT>` inside `namespace boost::polygon`. If the vendored Boost version ships the identical template class, both definitions will be visible to the linker as template instantiations — potential ODR violation if `CT` matches across translation units | Geometry/VoronoiVisualUtils.hpp:voronoi_visual_utils:ODR | Medium | P2 |
| 581 | `MedialAxis::repair()` uses a morphological closing operation (erode then dilate) to fill gaps in the medial axis skeleton. This can change the polygon topology: small holes below the structuring-element radius are filled and thin bridges may be merged. Any port must preserve or explicitly document this topology-modifying step | Geometry/MedialAxis.cpp:repair:morphological-closing-topology | Medium | P2 |
| 582 | `MedialAxis::validate_edge()` hardcodes `PI/8` (22.5°) as the maximum angle deviation for collinear-edge merging. This threshold has no calibration data; it was likely chosen empirically and may be too tight or too loose for different nozzle/layer-height configurations | Geometry/MedialAxis.cpp:validate_edge:PI_over_8_hardcoded | Low | P3 |
| 583 | `VoronoiDiagram::is_valid()` returns `true` for `State::UNKNOWN`. Since `UNKNOWN` is the default state before any validation has run, a diagram that was never validated reports itself as valid. Callers that rely on `is_valid()` for safety guards are silently bypassed | Geometry/Voronoi.hpp:VoronoiDiagram::is_valid:UNKNOWN-is-true | Medium | P2 |
| 584 | `IssueType::UNKNOWN` is overloaded to mean both "validation is disabled" and "unexpected/unknown error". Code that checks `issue == IssueType::UNKNOWN` cannot distinguish "never checked" from "check failed with unknown reason" — the sentinel overloads semantics | Geometry/Voronoi.hpp:IssueType::UNKNOWN:overloaded-semantics | Low | P3 |
| 585 | `VoronoiUtils::copy_to_local()` uses pointer subtraction (`&v - &voronoi_diagram.vertices().front()`) assuming the Boost.Polygon VD vertex container is contiguous in memory. If the container is not a plain `std::vector` (e.g. a deque or a future Boost update changes the storage), this produces undefined behavior | Geometry/VoronoiUtils.cpp:copy_to_local:pointer-subtraction | Low | P3 |
| 586 | `VoronoiUtils.cpp` uses explicit template instantiations for `is_voronoi_diagram_planar_angle` and related functions. Adding a new caller that uses a different iterator type requires adding a matching explicit instantiation here or the build fails with a cryptic undefined-symbol linker error rather than a comprehensible compile-time diagnostic | Geometry/VoronoiUtils.cpp:explicit-instantiation-list | Low | P3 |
| 587 | `VoronoiUtils::decode_input_segment_endpoint()`: when `color == 0`, `segment_idx` is computed as `(0 - 1)` which underflows to `SIZE_MAX` for unsigned arithmetic. The returned index is used immediately as an array subscript — out-of-bounds read / crash | Geometry/VoronoiUtils.cpp:decode_input_segment_endpoint:color-0-underflow | High | P1 |
| 588 | `VoronoiOffset::offset()` 4-argument convenience overload performs `const_cast<VD&>` and mutates the diagram (color fields) before calling the 5-argument version. This makes the overload non-const and not thread-safe despite the `const VD&` parameter in the declaration | Geometry/VoronoiOffset.cpp:offset-4arg:const_cast | Medium | P2 |
| 589 | `VoronoiOffset::offset()` tracing loop: when an open (non-closed) offset loop is detected, the loop is silently discarded with no error return, no log warning, and no quality degradation signal to the caller. Offset curves for concave regions may be silently incomplete | Geometry/VoronoiOffset.cpp:offset:open-loop-silently-discarded | Low | P3 |
| 590 | `VoronoiOffset::annotate_inside_outside()`: `assert(side != 0.)` fires in DEBUG builds on degenerate input (three collinear VD sites producing a zero cross-product). In RELEASE, `side == 0` is silently treated as positive, incorrectly classifying an inside/outside boundary | Geometry/VoronoiOffset.cpp:annotate_inside_outside:assert-side-zero | Low | P3 |
| 591 | `VoronoiOffset::compute_segment_cell_range()`: the `continue` statement for infinite edges is correct behavior but fragile to refactor — moving or restructuring the loop risks converting the skip into a fall-through, which would attempt to compute a cell range for an infinite edge and produce garbage distances | Geometry/VoronoiOffset.cpp:compute_segment_cell_range:infinite-edge-continue | Low | P3 |
| 592 | `VoronoiUtils::to_point()` uses `std::llround()` to convert `double` VD coordinates to `coord_t` (int64_t). If VD coordinates exceed `INT64_MAX / 2` (possible for very large or improperly scaled inputs), `llround` returns implementation-defined behavior (UB in C++) | Geometry/VoronoiUtils.cpp:to_point:llround-overflow | Low | P3 |
| 593 | `VoronoiUtils::discretize_parabola()` logs a warning when it falls back to a straight-line approximation due to degenerate parabola parameters (rot_y near zero), but the callers do not check for this degraded output. The warning is produced to stderr / BOOST_LOG and may be silently swallowed in headless builds | Geometry/VoronoiUtils.cpp:discretize_parabola:warning-not-checked | Low | P3 |
| 594 | `VoronoiUtilsCgal::is_voronoi_diagram_planar_intersection()` includes only linear VD edges in the CGAL sweep-line test; curved (parabolic) edges are silently excluded. A FIXME comment acknowledges this. A VD with crossing parabolic arcs will be incorrectly reported as planar | Geometry/VoronoiUtilsCgal.cpp:is_voronoi_diagram_planar_intersection:parabolic-excluded | Low | P3 |
| 595 | `Voronoi::Internal::color_exterior()` is recursive. For a large VD with many exterior-connected edges, the call stack depth is O(n) in the number of reachable primary edges. This risks stack overflow for large inputs. Should be rewritten with an explicit stack | Geometry/VoronoiVisualUtils.hpp:color_exterior:recursive-stack-overflow | Medium | P2 |
| 596 | `BicubicInternal::clamp()` duplicates `std::clamp` (available since C++17, which this project targets). The duplicate is kept for historical compatibility but creates a maintenance divergence point if the semantics ever differ | Geometry/Bicubic.hpp:BicubicInternal::clamp:duplicates-std-clamp | Low | P3 |
| 597 | `fit_curve()` and `fit_polynomial()` use Eigen's `fullPivHouseholderQr()` / `householderQr()` which are O(n²m) in the number of observations `n` and parameters `m`. No size guard is present. For large calibration datasets (n > 10,000), this is computationally expensive. The full-pivot variant also allocates O(nm) memory | Geometry/Curves.hpp:fit_curve:O-n2m-no-size-guard | Low | P3 |
| 598 | `fit_curve()` contains `assert(weights[index] > 0)` to guard against zero or negative weights. The assert fires only in debug builds; in release, `sqrt(0)` = 0 (degenerate row in the weight matrix, silently reducing effective observations) and `sqrt(negative)` = NaN (corrupts the entire QR solve output with NaN coefficients and no diagnostic) | Geometry/Curves.hpp:fit_curve:assert-weights-positive | Low | P3 |
| 599 | `Polyline::fitting_result` (`std::vector<PathFittingData>`) is a parallel vector to `points` that carries arc-fitting metadata. Each `PathFittingData` entry spans `[start_point_index, end_point_index]` of `points`. ALL mutations of `points` must also update `fitting_result` via the private helpers `append_fitting_result_after_append_points()`, `append_fitting_result_after_append_polyline()`, `split_fitting_result_before_index()`, `split_fitting_result_after_index()`. Any port that treats `Polyline` as a plain point vector will silently lose arc metadata. Operations that bypass these helpers (e.g. direct `points.push_back()`) corrupt the metadata | Polyline.hpp:fitting_result:parallel-vector-invariant | High | P1 |
| 600 | `Polygon::area()` uses the shoelace formula and returns a **negative** value for CW-wound polygons (holes). Callers that accumulate or compare areas without calling `std::abs()` will see sign errors. Callers that rely on negative area to detect CW winding are implicitly coupling to this convention. Any port that makes area() always-positive breaks those callers | Polygon.cpp:area:negative-for-CW | Medium | P2 |
| 601 | `Points` (the primary point container in `Polygon`, `Polyline`, etc.) uses `tbb::scalable_allocator<Point>` as its allocator. This is ABI-incompatible with `std::allocator`. Any port that reimplements `Points` as `std::vector<Point>` (without the TBB allocator) will not be drop-in compatible at the binary level, and any interface that takes `Points&` or `Points*` will silently allocate from a different heap | Point.hpp:Points:tbb-scalable-allocator | Medium | P2 |
| 602 | `ExPolygon::area()` uses the double-negation idiom: `a -= -hole.area()`. Hole areas are negative (CW winding → shoelace negative). The double negation converts negative to positive, then subtracts. The result is correct but unintuitive. A port that rewrites this as `a -= hole.area()` would SUBTRACT a negative value (adding the hole area) and produce a wrong result. See H600 for background on `Polygon::area()` sign convention | ExPolygon.cpp:area:double-negation | Medium | P2 |
| 603 | `BoundingBox3Base` iterator-range constructor throws `InvalidArgument` on empty point sets. All other `BoundingBoxBase` constructors silently return an `undefined` bbox. This asymmetry means code that defensively passes an empty point set to the iterator constructor will receive an exception, while the same set passed to other constructors returns silently. Any port must preserve or explicitly document this asymmetric error-handling | BoundingBox.hpp:BoundingBox3Base:iterator-constructor-throws | Low | P3 |
| 604 | `ClosestPointInRadiusLookup` grid-hash spatial index searches only a 2×2 neighborhood of grid cells (4 cells). For query radii that span more than one cell, points in non-adjacent cells may be missed. The lookup is approximate, not exhaustive. Ports that assume correct nearest-neighbor-in-radius semantics will produce wrong results for any radius > cell_size | Point.hpp:ClosestPointInRadiusLookup:4-cell-approximation | High | P1 |
| 605 | `Vec2crd` and `Vec2d` have implicit-cast paths via Eigen's template machinery. Converting a `Vec2crd` (scaled integers) to `Vec2d` (doubles) without calling `unscaled()` loses the 1e6 scale factor silently. Similarly, assigning a `Vec2d` to a `Point` without `scaled()` silently drops fractional millimetres. These implicit conversions exist throughout the codebase and are a persistent source of unit-mismatch bugs | Point.hpp:Vec2crd-Vec2d:implicit-cast-unit-mismatch | High | P1 |
| 606 | `Point(double x, double y)` constructor rounds via `std::round()`. Sub-nanometre precision (below 0.001 µm in the 1e6 scale) is permanently lost on construction. A port that constructs Points from doubles at mm scale without rounding will accumulate different rounding error than the original | Point.hpp:Point(double,double):rounding | Low | P3 |
| 607 | `Point::new_scale()` truncates via `static_cast<coord_t>()` (no rounding). For coordinates > ~2147 m (when `coord_t = int32_t`), the cast silently overflows. In the `int64_t` `coord_t` variant the overflow threshold is ~9.2 × 10⁹ m — effectively safe for any physical print, but the truncation (vs rounding) still introduces up to 1 ULP error | Point.hpp:new_scale:truncation-overflow | Low | P3 |
| 608 | `rotate(double cos_a, double sin_a)` snaps each coordinate to integer via `std::round()`. Repeated in-place rotations (e.g. spinning a polygon) accumulate rounding error — each rotation introduces up to 0.5 ULP error in each coordinate. After k rotations the error is O(k × 0.5) in scaled integer units, or O(k × 0.5 nm) in physical space | Point.hpp:rotate(cos,sin):round-accumulation | Low | P3 |
| 609 | Boost.Polygon `polygon_traits<ExPolygon>` specialisation exposes only the **contour** as the polygon face (winding reported as `unknown_winding`). Holes are exposed separately via `polygon_with_holes_traits`. Any code that processes `ExPolygon` through the plain `polygon_concept` interface (not `polygon_with_holes_concept`) will silently ignore all holes and operate only on the contour | ExPolygon.hpp:boost-polygon_traits:holes-ignored | High | P1 |
| 610 | `ExPolygon` winding convention (contour CCW, holes CW) is a contract enforced only by `is_valid()`, which is never called on construction. All algorithms that assume valid winding (area accumulation, Clipper operations, perimeter generation) will silently produce wrong results if the convention is violated. No RAII mechanism prevents constructing an invalid ExPolygon | ExPolygon.hpp:ExPolygon:winding-not-enforced | High | P1 |
| 611 | `ExPolygon::scale(double)` multiplies each `coord_t` by a double factor and rounds independently per vertex. Repeated scaling accumulates per-vertex rounding error. Contour and each hole are scaled independently — no correlated error across rings. A port using a single global scale transform (e.g. Affine matrix) would produce different rounding patterns | ExPolygon.cpp:scale:per-vertex-rounding | Low | P3 |
| 612 | `ExPolygon::translate(double x, double y)` converts the double offset to `coord_t` using **truncation** (`coord_t(x)`), not `std::round()`. This differs from the `Point(double, double)` constructor which uses `std::round()`. Sub-unit placement error up to 1 ULP is possible for non-integer mm values. Any port that normalises translation to always round should document this deliberate change | ExPolygon.hpp:translate(double):truncation-vs-round | Low | P3 |
| 613 | `ExPolygon::douglas_peucker(double)` and `ExPolygon::simplify_p(double)` simplify contour and holes **independently**. Topological consistency between rings is NOT guaranteed post-simplification — a simplified hole edge could become coincident with or cross the contour boundary. For topology-safe simplification, use `ExPolygon::simplify()` which follows with `union_ex()` | ExPolygon.cpp:douglas_peucker:independent-ring-simplification | Medium | P2 |
| 614 | `ExPolygon::contains(Polyline)` uses `diff_pl(polyline, *this)` (Clipper-based) for containment. Clipper's open-boundary convention means polyline segments exactly on the contour edge may or may not appear in the diff depending on edge orientation and the horizontal-boundary asymmetry (H616). Containment on the boundary is non-deterministic for horizontal boundary segments | ExPolygon.cpp:contains(Polyline):Clipper-open-boundary | Low | P3 |
| 615 | `ExPolygon::contains(Point, border_result)` inverts `border_result` for the hole test. A point on a hole boundary with `border_result=true` is treated as NOT inside the hole, so it remains "contained" in the ExPolygon. This inversion is intentional and documented in comments, but any port that copies the hole test without inverting will produce incorrect boundary-point containment results | ExPolygon.cpp:contains(Point):inverted-border-result | Low | P3 |
| 616 | `ExPolygon::overlaps()` is **not commutative**. Expolygons touching at a vertical boundary are considered overlapping; those touching at a horizontal boundary are NOT. This asymmetry is Clipper's open-boundary behavior for horizontal edges. Unit test `SCENARIO("Clipper diff with polyline", "[Clipper]")` documents this. Any port must preserve or explicitly test for this asymmetry | ExPolygon.cpp:overlaps:non-commutative | Medium | P2 |
| 617 | `ExPolygon::simplify_p(double)` temporarily adds a duplicate closing point before calling `MultiPoint::_douglas_peucker()`, then removes it. If D-P reduces the ring to < 3 unique points, `simplify_polygons()` will filter it out and return an empty `Polygons`. Callers that do not handle an empty return will propagate empty containers downstream silently | ExPolygon.cpp:simplify_p:empty-result-on-degenerate | Low | P3 |
| 618 | `ExPolygon::medial_axis()` endpoint extension intersects only with `this->contour`, not with holes. If the ExPolygon is concave (re-entrant boundary), the extension ray may cross a concave notch and the intersection with the outer contour may snap to the wrong edge. The endpoint could land outside the intended narrow feature | ExPolygon.cpp:medial_axis:contour-only-intersection | Low | P3 |
| 619 | `ExPolygon::medial_axis()` greedy reconnection (Phase 4) connects random pairs when more than two polylines share an endpoint. Per the source comment: "This has no drawbacks since we optimize later using nearest-neighbor." If a more sophisticated optimizer is used downstream that does NOT reorder arbitrarily, the greedy reconnection may create suboptimal or topologically wrong connections | ExPolygon.cpp:medial_axis:greedy-reconnection | Low | P3 |
| 620 | `to_lines(ExPolygons)` reserves `count_points(src)` elements. Calling `count_points()` is an O(n) traversal; the reserve is a minor perf concern but is correct. The closing edge (`back → front`) is appended outside the inner iterator loop — any port that converts the inner loop to range-based without handling the closing edge will silently drop one edge per polygon | ExPolygon.hpp:to_lines:closing-edge-outside-inner-loop | Low | P3 |
| 621 | `to_linesf()` uses a shared `prev_pd` variable captured by reference in a lambda that is called for each ring. If a ring has `pts.size() < 2`, the lambda returns early leaving `prev_pd` stale with the last value from the previous ring. The next ring then starts with a wrong `prev_pd`. The `assert(pts.size() >= 3)` guard fires only in debug builds; release silently produces a cross-ring "ghost" line | ExPolygon.hpp:to_linesf:shared-prev_pd-state | Medium | P2 |
| 622 | `to_polylines(ExPolygon&&)` (rvalue overload): after `pl.points = std::move(src.contour.points)`, `pl.points.front()` is read to add the closing point. This is safe because `pl.points` now owns the data. However, the order of these two lines is critical — swapping them (reading `src.contour.points.front()` before the move, then assigning) would access a moved-from vector. Any refactor must preserve the post-move read order | ExPolygon.hpp:to_polylines(&&):move-then-read-front | Low | P3 |
| 623 | `to_polygon_ptrs()` returns `ConstPolygonPtrs` (vector of raw `const Polygon*` pointers). These pointers are invalidated if the source `ExPolygon` or `ExPolygons` is moved, resized, or destroyed. The returned vector provides no lifetime guarantee. Only safe for short-lived local iterations where the source outlives all pointer uses | ExPolygon.hpp:to_polygon_ptrs:pointer-lifetime | Medium | P2 |
| 624 | `to_expolygons(Polygons)` wraps each `Polygon` in its own `ExPolygon` with no holes. This does NOT run Clipper union to resolve hole-contour topology. If the input `Polygons` contains both CCW contours and CW holes (as produced by Clipper), the result has all polygons as "contours" with no hole nesting — topologically wrong. Callers requiring correct topology should use `union_ex()` instead | ExPolygon.hpp:to_expolygons:no-hole-nesting | High | P1 |
| 625 | `get_extents(ExPolygon)` returns the bounding box of the **contour only**. This is geometrically correct (holes are inside the contour). However, an `ExPolygon` with an empty contour but non-empty holes (malformed input) returns an `undefined` BoundingBox. Callers that do not check `bbox.defined` before use will access uninitialized min/max values | ExPolygon.cpp:get_extents:contour-only | Low | P3 |
| 626 | `has_duplicate_points(ExPolygon)` uses the global check path (active `#if 1`): it flattens contour + all holes into one vector and sorts globally. A vertex that appears in both the contour and a hole boundary (geometrically degenerate but possible after simplification) will be reported as a duplicate. The inactive per-contour path would NOT report this. Callers may receive false positives from the global check | ExPolygon.cpp:has_duplicate_points:global-vs-per-contour | Low | P3 |
| 627 | `keep_largest_contour_only()` compares contour areas using `Polygon::area()` directly (shoelace, positive for CCW). A CW-wound contour returns negative area and is never selected as maximum. If all contours are CW (malformed input), `max_area_polygon` remains `nullptr` and the `assert` fires in debug; in release this is an immediate null-pointer dereference crash | ExPolygon.cpp:keep_largest_contour_only:nullptr-crash-CW-only | High | P1 |
| 628 | `remove_small_and_small_holes()` uses `std::abs(expolygons[expoly_idx].area())` to handle both CCW (positive area) and CW (negative area) contours gracefully. However, `ExPolygon::area()` accumulates floating-point rounding. For extremely small polygons near `min_area`, rounding may push the computed area just above or below the threshold non-deterministically depending on vertex count and coordinate magnitudes | ExPolygon.cpp:remove_small_and_small_holes:area-rounding-threshold | Low | P3 |
| 629 | `ExPolygon::contains(Polyline)` wraps the polyline in `diff_pl(polyline, *this)`. Clipper's open-polyline difference may produce non-empty output for polylines that are EXACTLY on the boundary, depending on whether the boundary is horizontal (Clipper's top-edge convention). The result is a false negative for "boundary containment" — a polyline along the top edge of the ExPolygon may not be considered contained | ExPolygon.cpp:contains(Polyline):Clipper-horizontal-edge | Low | P3 |
| 630 | `ExPolygon::overlaps()` fallback test uses `other.contains(this->contour.points.front())` to detect the case where `*this` is fully inside `other`. If `*this` has zero points (`empty()` check guards this), or if `this->contour.points.front()` happens to lie inside a hole of `other`, the fallback incorrectly returns true. In practice, Voronoi-produced skeletons avoid this, but geometrically crafted ExPolygons can trigger it | ExPolygon.cpp:overlaps:fallback-front-point-in-hole | Low | P3 |
| 631 | `projection_onto(ExPolygons, Point)` uses a signed `int` loop index for `num_contours()` which returns `size_t`. On LP64 platforms where `int` is 32-bit and `size_t` is 64-bit, the signed/unsigned comparison in `i < poly.num_contours()` triggers a compiler warning and is a latent bug for ExPolygons with > 2^31 − 1 holes (not realistically possible, but a clean port should use `size_t`) | ExPolygon.cpp:projection_onto:int-size_t-comparison | Low | P3 |
| 632 | `ExPolygon::medial_axis()` ThickPolyline reconnection appends `other.width` (all entries) and `other.points[1..end]`. The invariant `polyline.width.size() == polyline.points.size()*2 - 2` is asserted after each merge. A port that copies only `other.points` without also copying `other.width`, or that copies both in the wrong order, will silently violate the width invariant without triggering the assert in release builds | ExPolygon.cpp:medial_axis:width-invariant-reconnection | Medium | P2 |
| 633 | `expolygons_match()` requires holes to appear in the **same order** in both ExPolygons. Two topologically identical ExPolygons with holes in different order will NOT match. No sorting is performed before comparison. Callers that normalise hole order before calling this function (e.g. sorting by centroid) will get different results than those that don't | ExPolygon.cpp:expolygons_match:hole-order-sensitive | Low | P3 |
| 634 | `remove_same_neighbor(ExPolygons)` erases ExPolygons whose contour has ≤ 2 points after deduplication, but does NOT check whether any hole has been reduced to ≤ 2 points. Degenerate 2-point holes (straight-line sticks) remain in the `holes` vector and may cause downstream Clipper or area-computation errors | ExPolygon.cpp:remove_same_neighbor:degenerate-holes-not-erased | Low | P3 |
| 635 | `line_alg::distance_to_squared()`: the nearest point on the segment is computed in `double`, then cast back to `Scalar<L>` (truncation). The squared distance returned is computed from the pre-cast `double` nearest point, NOT from the truncated value. This creates a discrepancy: callers that use the returned nearest_point for further geometry will see a different point than the one used to compute the returned distance | Line.hpp:line_alg::distance_to_squared:nearest-point-cast-discrepancy | Medium | P2 |
| 636 | `line_alg::intersection()`: the collinear-segment case is permanently disabled via `#if 0`. When two segments are collinear and overlapping, the function always returns `false` (no intersection). Callers that expect a valid intersection point for collinear overlapping segments (e.g. to find the midpoint of the shared region) receive a false negative with no diagnostic | Line.hpp:line_alg::intersection:collinear-disabled | Medium | P2 |
| 637 | `line_alg::intersection()`: the computed intersection point is a `double` and is cast to `coord_t` via `static_cast<Scalar<L>>()` (truncation, not rounding). For intersections near integer coordinate boundaries, the truncation can shift the intersection point by up to 1 scaled unit (1 nm). A port that uses `std::round()` or `llround()` instead will produce systematically different intersection coordinates | Line.hpp:line_alg::intersection:intersection-point-truncation | Low | P3 |
| 638 | `Line::intersection_infinite()` checks the computed coordinates against `coord_t` numeric limits before returning, but the check accepts values just inside the limit with truncated precision. Near-boundary input coordinates (within ~1 ULP of INT64_MAX in scaled units) produce intersection points that are numerically valid but geometrically shifted | Line.cpp:intersection_infinite:near-limit-precision-loss | Low | P3 |
| 639 | `Linef3::intersect_plane()` divides by `v(2)` (the Z component of the line direction). If the line is horizontal (v(2) == 0), this is a division by zero, producing ±Inf or NaN in the result with no guard. The related `unit_vector()` returns `Zero()` for a zero-length line — silently degenerate rather than signalling error | Line.hpp:Linef3::intersect_plane:division-by-zero | High | P1 |
| 640 | The Boost.Polygon `segment_concept` specialization for `Line` uses `coord_t` for coordinates. `coord_t` is `int64_t` on 64-bit platforms, but Boost.Polygon's Voronoi builder assumes coordinates fit in `int32_t` range for its integer arithmetic. Inputs with coordinates outside `int32_t` range will silently produce a corrupted Voronoi diagram | Line.hpp:boost-polygon-segment_concept:int64-voronoi-mismatch | High | P1 |
| 641 | `Line::intersection_infinite()` uses `int64_t` intermediate arithmetic with overflow guards that check the final result against coordinate limits. However, intermediate products in the cross-multiply can overflow `int64_t` before the guard is reached for inputs in the upper half of the `coord_t` range. The overflow is silent (UB for signed integers in C++) | Line.cpp:intersection_infinite:intermediate-overflow | Medium | P2 |
| 642 | `Line::perp_distance_to()` correctly guards against the degenerate case where `a == b` (zero-length line) by returning 0.0 immediately. This is one of the few places in Line.cpp where degenerate input is explicitly handled rather than producing NaN or Inf | Line.cpp:perp_distance_to:zero-length-guard-correct | Low | P3 |
| 643 | `Line::overlap()` uses only the **X-projection** to determine segment overlap. For near-vertical lines (|dy| >> |dx|), the X-projection is near-zero and the result is numerically unstable. Two nearly-vertical overlapping segments may be incorrectly reported as non-overlapping (or vice versa) | Line.cpp:overlap:x-projection-only | Medium | P2 |
| 644 | `Line::overlap()` computes the overlap by dividing `t` by the X component of the direction. If the line is **exactly vertical** (dx == 0), this is a division by zero — produces ±Inf or NaN with no guard. Callers pass vertical lines in geometry processing (e.g. fill pattern clipping) | Line.cpp:overlap:vertical-division-by-zero | High | P1 |
| 645 | `Line::extend()` calls `direction().normalized()` to get the unit direction vector. If the line has zero length (a == b), `normalized()` returns a zero vector (Eigen's behavior for zero-norm). Multiplying by the extension distance yields a zero vector, so `a` and `b` are shifted by zero — silent NaN-free degenerate output. The line is not extended but also not signalled as degenerate | Line.cpp:extend:zero-length-normalized | Low | P3 |
| 646 | `Extruder::m_share_E` and `Extruder::m_share_retracted` are **static class-level** members (not instance members). All `Extruder` instances in the same process share these values. If multiple `Print` objects or worker threads operate concurrently (e.g. background slicing), they will corrupt each other's retraction state without any synchronization | Extruder.hpp:m_share_E:static-global-state | Critical | P1 |
| 647 | `Extruder::m_e_per_mm3`: the extrusion multiplier formula `4 / π / d² × flow_ratio` is cached at construction time. If `GCodeConfig` is mutated post-construction (e.g. during preview regeneration), the cached value becomes stale and all subsequent extrusion length calculations use the wrong filament density | Extruder.hpp:m_e_per_mm3:stale-cache | Medium | P2 |
| 648 | `Extruder::reset_E()` always writes to both `m_E` and `m_share_E[extruder_id()]` regardless of whether share mode is active. In non-share mode, the write to `m_share_E` is harmless but silently dirtied state may confuse future share-mode instances that expect `m_share_E` to have been managed only through the proper share-mode paths | Extruder.cpp:reset_E:unconditional-shared-write | Low | P3 |
| 649 | `Extruder` has **asymmetric config indexing**: `filament_diameter()` and `filament_flow_ratio()` use `m_id` (logical extruder 0-based index for the filament slot), while `retract_length_toolchange()`, `retract_restart_extra_toolchange()`, and `travel_slope()` use `extruder_id()` (physical slot index). These two indices differ when the extruder map is non-identity. Any port that normalises to a single index breaks one set of config lookups | Extruder.hpp:config-indexing:asymmetric-m_id-vs-extruder_id | High | P1 |
| 650 | `Extruder::m_config` is a **raw non-owning pointer** to `GCodeConfig`. If the `GCodeConfig` object is destroyed or moved while an `Extruder` is still live, all config accesses become dangling pointer reads (UB). There is no `shared_ptr` or lifetime contract enforced by the type system | Extruder.hpp:m_config:raw-non-owning-pointer | High | P1 |
| 651 | `Extruder::retract()` always updates `m_restart_extra` (the priming amount) even when the retraction length is 0 (nothing was retracted). This means a no-op retraction call silently changes the priming amount that will be used on the next `unretract()`. The behavior is idempotent in the common case but can surprise callers that call `retract()` defensively | Extruder.cpp:retract:restart_extra-updated-on-noop | Low | P3 |
| 652 | `Extruder::unretract()` in share-extruder mode: `extrude()` is called first (advancing the shared E counter), then `m_share_retracted[extruder_id()]` is explicitly zeroed. The order is critical — zeroing first then extruding would produce wrong E values. Any refactor must preserve this exact ordering | Extruder.cpp:unretract:share-mode-order-dependent | Medium | P2 |
| 653 | `Extruder::set_retracted()` only updates `m_retracted` (the instance member). In share-extruder mode, `m_share_retracted[extruder_id()]` is NOT updated. Callers using `set_retracted()` to restore state after a toolchange or serialization/deserialization will leave `m_share_retracted` out of sync with the instance retraction state | Extruder.cpp:set_retracted:share-mode-not-updated | Medium | P2 |
| 654 | `Extruder::used_filament()` has a FIXME comment: in share-extruder mode, the retracted (not-yet-extruded) length is NOT counted. The returned used filament value is always under-reported by the current retraction amount for shared extruders. Filament accounting tools or cost estimation that call this function will see systematically low values for shared extruder configurations | Extruder.cpp:used_filament:retracted-length-missing | Medium | P2 |
| 655 | `Extruder::extrude()` adds to `m_E`, then conditionally also adds to `m_share_E[extruder_id()]`. In share mode, both instance and shared counters advance together. After a toolchange that resets `m_E` via `reset_E()`, the shared counter is also reset. A port that tracks only one counter and derives the other will produce correct values only as long as `reset_E()` is always called at the same time for both | Extruder.cpp:extrude:dual-counter-sync | Low | P3 |
| 656 | `Algorithm::do_split_line()` returns an empty `SplittedLine` struct when no intersection is found. Callers of `split_line<PathType>()` must explicitly handle the empty-result case (check `result.empty()` or `result.size() == 0`). There is no sentinel value or error return to distinguish "no split needed" from "error" — both produce an empty result | Algorithm/LineSplit.hpp:do_split_line:empty-on-no-intersection | Low | P3 |
| 657 | `Algorithm::split_line<PathType>()` reserves capacity with `p.reserve(path.size() + closed ? 1 : 0)`. Due to C++ operator precedence, this parses as `p.reserve((path.size() + closed) ? 1 : 0)`, which is `1` for any non-empty path (since `path.size() + closed > 0`). The intended expression was `p.reserve(path.size() + (closed ? 1 : 0))`. `reserve()` is advisory so correctness is preserved, but the path always causes a reallocation on the first push_back for paths of length > 1 | Algorithm/LineSplit.hpp:split_line:reserve-precedence-bug | Low | P3 |
| 658 | `LineSplit.cpp` CLIP_IDX sentinel: `std::numeric_limits<ClipperLib_Z::cInt>::max()` is used as the Z tag for clip-polygon-origin points. If a source path ever has exactly max(cInt) points, the last point's sequential Z index equals CLIP_IDX — a silent collision that causes that endpoint to be misclassified as a clip-origin point, producing a corrupted split chain | Algorithm/LineSplit.cpp:CLIP_IDX:sentinel-collision | Low | P3 |
| 659 | `LineSplit.cpp` `to_src_idx()`: if called on a clip-origin point (Z == CLIP_IDX) after the assert is stripped in release, returns CLIP_IDX (a very large positive value) as the source index, which is silently used as an out-of-bounds array index into split_chain — undefined behavior | Algorithm/LineSplit.cpp:to_src_idx:assert-only-guard | Medium | P2 |
| 660 | `LineSplit.cpp` `point_on_line()` collinearity test uses cross-product `d1.x() * d2.y() - d1.y() * d2.x()` in coord_t (int64_t) arithmetic. For coordinates near ±2^31 (common in scaled geometry), the intermediate products can overflow int64_t, producing a false "non-collinear" result and causing a valid intersection point to be missed by the AABB-tree resolution step | Algorithm/LineSplit.cpp:point_on_line:cross-product-overflow | Medium | P2 |
| 661 | `LineSplit.cpp` `point_on_line()` between-ness test `(p.x() > l.a.x()) == (p.x() < l.b.x())` is undefined for endpoint-coincident case (p == l.a or p == l.b). The comment states "p cannot be one of the line end" but there is no assertion or guard enforcing this precondition at callsites | Algorithm/LineSplit.cpp:point_on_line:endpoint-precondition | Low | P3 |
| 662 | `do_split_line()` passes ExPolygon holes to Clipper as clip paths without validating winding. If holes are CCW (should be CW), Clipper's nonzero fill rule treats them as filled regions, producing incorrect intersection results that include path segments inside holes | Algorithm/LineSplit.cpp:do_split_line:hole-winding-not-validated | Medium | P2 |
| 663 | `do_split_line()` calls `zclipper.PreserveCollinear(true)` to prevent removal of on-boundary source vertices. Without this, source vertices on a clip edge would be removed, breaking the Z index chain. However, PreserveCollinear may produce duplicate consecutive points in the output, creating zero-length segments in the SplittedLine that downstream callers may not handle | Algorithm/LineSplit.cpp:do_split_line:PreserveCollinear-duplicate-points | Low | P3 |
| 664 | `do_split_line()` uses `PolyTreeToPaths` to flatten the poly tree output. Open-path subjects in Clipper's PolyTree are at root level; closed subjects are nested. A future change adding a closed subject would silently lose nested path segments | Algorithm/LineSplit.cpp:do_split_line:PolyTreeToPaths-open-path-assumption | Low | P3 |
| 665 | `do_split_line()` AABB resolve step uses `SCALED_EPSILON` (100 nm) as search radius for clip-origin point resolution. For very short source edges (< 200 nm), an intersection point near the midpoint may be within SCALED_EPSILON of both endpoints. The code picks the first matching edge candidate — order-dependent and potentially wrong for degenerate short-edge geometry | Algorithm/LineSplit.cpp:resolve_clip_point:SCALED_EPSILON-short-edge | Low | P3 |
| 666 | `do_split_line()` fallback in `resolve_clip_point`: if no source edge matches the clip-origin point, the code silently assigns `-(path[possible_edges[0]].z() + 1)` — tagging the point with the first candidate edge's start index without verifying geometric membership. This can misorder the point in the sort step, producing an incorrectly sequenced SplittedLine | Algorithm/LineSplit.cpp:resolve_clip_point:silent-fallback-misorder | Medium | P2 |
| 667 | `do_split_line()` sort comparator for segment points has a subtle non-strict-weak-ordering edge case: two `is_src` points at the same src index (degenerate zero-length Clipper segment) compare as `is_src(a) = true` vs `is_src(b) = true` → neither is "less", violating strict weak ordering for `std::sort`. UB in C++ | Algorithm/LineSplit.cpp:segment-sort:non-strict-weak-ordering | Medium | P2 |
| 668 | `do_split_line()` final reconstruction loop: the two-level skip logic (`next_idx++` if split_chain is empty) handles at most one empty node. If two consecutive source vertices have no segments AND the segment tail lands exactly at one of them, a vertex may be silently omitted from the result (gap in the SplittedLine) | Algorithm/LineSplit.cpp:reconstruction:two-level-skip-gap | Low | P3 |
| 669 | `RegionExpansionParameters` float fields (`tiny_expansion`, `initial_step`, `other_step`, `max_inflation`) are in *scaled* coord_t units (mm × 1e6), not millimetres. The `float` type makes the unit invisible to the compiler. Callers that pass mm values directly will get near-zero expansions with no diagnostic | Algorithm/RegionExpansion.hpp:parameters:scaled-float-units | High | P1 |
| 670 | `RegionExpansionParameters::build()` can produce nsteps == 0 if `full_expansion ≤ tiny_expansion`. The assert fires in debug; in release, `initial_step = (full_expansion - tiny_expansion) / 0` produces NaN or Inf, silently corrupting all subsequent offset and clip operations | Algorithm/RegionExpansion.hpp:build:nsteps-zero-on-tiny-expansion | Medium | P2 |
| 671 | `lower_by_boundary_and_src` and `lower_by_src_and_boundary` comparators are NOT stable — equal-key seeds can appear in any relative order. For the same (src, boundary) pair, seed order affects intermediate ClipperOffset rounding in wavefront_initial/step, potentially producing slightly different polygon boundaries | Algorithm/RegionExpansion.hpp:comparators:unstable-order | Low | P3 |
| 672 | `wave_seeds()` `tiny_expansion` is asserted > 0 but not bounded above. Too-large tiny_expansion causes expanded src outlines to cross multiple boundary regions, producing WaveSeeds with incorrect boundary attribution that silently cause the wave to expand into the wrong boundary | Algorithm/RegionExpansion.hpp:wave_seeds:tiny_expansion-unbounded | Medium | P2 |
| 673 | `propagate_waves(seeds, ...)` requires seeds sorted by (boundary, src) — uses a linear group scan. Unsorted seeds silently merge wrong groups, producing expansion polygons that cross incorrect boundaries | Algorithm/RegionExpansion.hpp:propagate_waves:sort-required | High | P1 |
| 674 | `merge_expansions_into_expolygons()`: when `union_safety_offset_ex` produces > 1 ExPolygon, the fallback picks the one containing a sample point from the source contour. If `sample_in_expolygons` returns -1 (sample in a hole of merged result), the source ExPolygon is silently dropped — missing from the output with no warning | Algorithm/RegionExpansion.hpp:merge_expansions:sample-in-hole-drop | High | P1 |
| 675 | `clipper_round_offset_error()` is referenced in a commented-out `max_inflation` formula. The active formula uses a simpler 1.1× multiplier. If the arc-error formula is ever uncommented, it may produce a significantly smaller `max_inflation` for large offsets, causing the boundary clipping region to truncate the wave before full expansion | Algorithm/RegionExpansion.cpp:clipper_round_offset_error:dead-code | Low | P3 |
| 676 | `expolygons_to_zpaths_expanded_opened()` applies offset sign based on `icontour index` (0 = contour → +expansion; >0 = hole → -expansion), not winding direction. An ExPolygon with an improperly wound hole (CCW instead of CW) gets +expansion (expanded outward) rather than the intended inward contraction, potentially producing a seed that expands outside the ExPolygon boundary | Algorithm/RegionExpansion.cpp:expolygons_to_zpaths_expanded_opened:hole-winding-assumption | Medium | P2 |
| 677 | `expolygons_to_zpaths_expanded_opened()` `base_idx` is a `coord_t` reference incremented once per ExPolygon. If src has more ExPolygons than `numeric_limits<coord_t>::max() - idx_boundary_end`, base_idx overflows silently, corrupting all Z-tag lookups in wave_seeds() | Algorithm/RegionExpansion.cpp:expolygons_to_zpaths_expanded_opened:base_idx-overflow | Low | P3 |
| 678 | `merge_splits()` calls `polylines_merge(other_path, ..., std::move(path), ...)`, leaving `path` in a moved-from state. The function immediately erases or swaps it with `paths.back()`. If `end->second` points to the last element and it equals the current `it_path` (self-adjacent split), the swap copies from a moved-from vector — safe (empty result) but may leave a spurious empty path | Algorithm/RegionExpansion.cpp:merge_splits:self-adjacent-move | Low | P3 |
| 679 | `merge_splits()` splits vector is sorted by coordinate. Two separate source ExPolygon contours that share the same expanded start coordinate (coordinate collision after ClipperOffset) have equal split entries. `lower_bound` picks one arbitrarily — potentially merging two unrelated polyline pieces | Algorithm/RegionExpansion.cpp:merge_splits:coordinate-collision | Low | P3 |
| 680 | `build_aabb_tree_over_expolygons()` indexes only the CONTOUR bounding box per ExPolygon (not the full ExPolygon with holes). A sample point in a hole passes the bbox pre-filter and reaches ExPolygon::contains(), which correctly rejects it — correct behavior. But for overlapping ExPolygon contour bboxes, the function returns the first match in traversal order, which is non-deterministic | Algorithm/RegionExpansion.cpp:build_aabb_tree:contour-bbox-only | Low | P3 |
| 681 | `sample_in_expolygons()` returns the first ExPolygon containing the sample point, stopping traversal immediately. For overlapping ExPolygons, the choice is non-deterministic (depends on AABB tree construction order). Callers that need a deterministic choice must ensure non-overlapping input | Algorithm/RegionExpansion.cpp:sample_in_expolygons:first-match-non-deterministic | Low | P3 |
| 682 | `wave_seeds()` silently skips seed segments where both endpoints coincide with existing boundary vertices (Z fill callback not invoked → no negative Z tag). The affected src ExPolygon is not expanded at all for that seed — no warning, no diagnostic | Algorithm/RegionExpansion.cpp:wave_seeds:skip-vertex-coincident-seed | Medium | P2 |
| 683 | `wavefront_initial()` inflates each seed path independently (co.Clear per iteration). Adjacent seed paths that would produce overlapping inflations are NOT merged before the clip step. The resulting individual inflations may self-intersect at merge boundaries. wavefront_clip() resolves this, but the intermediate may have slightly different topology than a single multi-path inflation | Algorithm/RegionExpansion.cpp:wavefront_initial:per-path-inflation | Low | P3 |
| 684 | `wavefront_step()` uses `ClipperLib::Orientation()` (signed area) to detect CW-wound polygons and negate the offset sign. For very thin slivers (nearly degenerate polygons), the signed area may be near-zero and orientation detection wrong due to integer rounding. A misidentified CW polygon gets expanded instead of contracted, producing a spurious filled region in the wavefront | Algorithm/RegionExpansion.cpp:wavefront_step:orientation-detection | Medium | P2 |
| 685 | `wavefront_clip()` uses `pftPositive` fill rule. If `wavefront_step()` produced CW outer contours due to H684, pftPositive treats them as holes and the intersection returns empty — sudden gaps in the expanded region at that wave step, with no diagnostic | Algorithm/RegionExpansion.cpp:wavefront_clip:pftPositive-CW-outer | Medium | P2 |
| 686 | `propagate_wave_from_boundary()` trims the boundary to a bbox around the seed inflated by `max_inflation`. If `max_inflation` underestimates actual ClipperOffset output (possible for small arc_tolerance + many steps), the wave is truncated before full depth. The 1.1× safety factor is not guaranteed for all arc_tolerance settings | Algorithm/RegionExpansion.cpp:propagate_wave_from_boundary:max_inflation-underestimate | Low | P3 |
| 687 | `propagate_waves()` emits one RegionExpansion per output polygon of `propagate_wave_from_boundary()`. For a single (boundary, src) group producing non-overlapping expanded polygons (src touches boundary at two disconnected locations), multiple separate RegionExpansion fragments are emitted. `merge_expansions_into_expolygons()` unions them, but `propagate_waves_ex()` may emit separate RegionExpansionEx entries for each fragment | Algorithm/RegionExpansion.cpp:propagate_waves:multiple-fragments | Low | P3 |
| 688 | `propagate_waves_ex()` unions per-(src,boundary) polygon fragments via `union_ex()`. If fragments are non-contiguous, union_ex() produces multiple ExPolygons, each emitted as a separate RegionExpansionEx with the same (src_id, boundary_id). Callers expecting exactly one result per (src,boundary) pair will receive multiple entries | Algorithm/RegionExpansion.cpp:propagate_waves_ex:multiple-expolygon-results | Low | P3 |
| 689 | `merge_expansions_into_expolygons()` uses a `uint32_t last` index. If src.size() > UINT32_MAX (impossible physically), the loop overflows and wraps around, silently emitting wrong ExPolygons. Theoretical concern only; documents the implicit size constraint | Algorithm/RegionExpansion.cpp:merge_expansions:uint32-index | Low | P3 |
| 690 | `NozzleTypeEumnToStr` and `NozzleTypeStrToEumn` are `static` `std::map<>` variables defined inside a `namespace` in the header — each translation unit that includes `PrintConfig.hpp` gets its own copy (internal linkage). The maps are never inconsistent in practice (all TUs include the same initializer), but the duplication wastes memory and the typo "Eumn" (should be "Enum") prevents grepping by correct spelling | PrintConfig.hpp:NozzleTypeEumnToStr:static-map-in-header | Low | P3 |
| 691 | `SupportType::is_tree()` constructs a `std::set<SupportType>` on every call: `{SupportType::stTreeSlicing, SupportType::stTreeHybrid}`. This is a hot-path helper that may be called per-region per-layer. A port should replace with a constexpr bitmask or inline comparison | PrintConfig.hpp:is_tree:set-constructed-per-call | Low | P3 |
| 692 | `SupportType::is_auto()` constructs a `std::set<SupportType>` on every call: `{SupportType::stNormal, stTree, stTreeSlicing, stTreeHybrid}`. Same hot-path allocation hazard as H691 | PrintConfig.hpp:is_auto:set-constructed-per-call | Low | P3 |
| 693 | `bed_type_to_gcode_string()` is a `static inline` function returning a `const char*` literal. `btDefault` and `btCount` (not real bed types) fall through to `return "unknown"` — no assert, no warning. G-code using the literal "unknown" would silently produce invalid output if a bed type is ever uninitialized | PrintConfig.hpp:bed_type_to_gcode_string:btDefault-silent-unknown | Medium | P2 |
| 694 | `DynamicPrintConfig::validate()` returns a `std::map<std::string, std::string>` of field → error-message. All callers are free to ignore the return value. The compiler issues no warning (not `[[nodiscard]]`). Invalid configs silently proceed to slicing | PrintConfig.hpp:DynamicPrintConfig:validate-ignorable | Medium | P2 |
| 695 | `StaticPrintConfig::optptr()` resolves option pointers via a byte-offset reinterpret_cast: `reinterpret_cast<ConfigOption*>((char*)this + offset)`. This requires standard layout for the derived config class. Any introduction of virtual functions, multiple inheritance with non-trivial bases, or compiler-inserted padding will silently corrupt the pointer arithmetic | PrintConfig.hpp:StaticPrintConfig:optptr-reinterpret-cast | High | P1 |
| 696 | Several acceleration/jerk fields (e.g., `default_acceleration`, `outer_wall_acceleration`) were migrated from `PrintConfig` to `PrintObjectConfig` at some point. Old project files that stored these fields at the print level will silently zero them if `handle_legacy()` in PrintConfig.cpp does not map the old key to the new location. The mapping must be kept in sync with every field migration | PrintConfig.hpp:PrintObjectConfig:acceleration-field-migration | Medium | P2 |
| 697 | `PrintRegionConfig` filament-override arrays (`filament_ironing_flow`, `filament_ironing_speed`, etc.) are nullable (`ConfigOptionFloatsNullable`). The override-resolution logic (region config → object config → print config) must replicate the nullable propagation semantics: a null entry in a per-region array means "use the parent value", not zero. A port that treats null as 0 will silently change ironing behavior | PrintConfig.hpp:PrintRegionConfig:nullable-filament-arrays | High | P1 |
| 698 | `adaptive_pressure_advance_model` is a raw `std::string` field with no schema validation at config load time. Any string value is accepted. G-code post-processors that parse this field will receive arbitrary user input — a potential injection vector if the model name is interpolated into shell commands or G-code without sanitization | PrintConfig.hpp:GCodeConfig:adaptive_pressure_advance_model-no-validation | Medium | P2 |
| 699 | `flush_volumes_matrix` is stored as a flat `ConfigOptionFloats` (1-D vector). The N×N matrix dimension is reconstructed as `N = (size_t)sqrt((double)v.size())`. A non-square serialized array (e.g., from a truncated save) silently uses `floor(sqrt(size))` rows, dropping the last partial row without any warning | PrintConfig.hpp:GCodeConfig:flush_volumes_matrix-sqrt-dimension | Medium | P2 |
| 700 | Flush-volume settings live in `GCodeConfig` while wipe-tower geometry (`wipe_tower_x`, `wipe_tower_y`, `wipe_tower_width`, etc.) live in `PrintConfig`. These two groups are semantically coupled (wipe tower behavior) but split across base classes. A port must keep both groups accessible from the same logical context | PrintConfig.hpp:PrintConfig:wipe-tower-flush-split | Low | P3 |
| 701 | `wipe_tower_x` and `wipe_tower_y` are `ConfigOptionFloats` (indexed by plate index) rather than scalar floats. The plate index is stored in a global context variable. Any code path that accesses wipe tower position without the correct plate context active will silently use the wrong plate's coordinates | PrintConfig.hpp:PrintConfig:wipe_tower_xy-plate-indexed | High | P1 |
| 702 | `FullPrintConfig` inherits from `PrintObjectConfig`, `PrintRegionConfig`, and `PrintConfig` (which itself inherits from `MachineEnvelopeConfig` + `GCodeConfig`). Field-name collisions between base classes are caught only by a debug assert in `StaticCache`. In release builds, the first registered field wins silently. Any future addition of a field with an existing name in a different base class will corrupt config lookups in release builds | PrintConfig.hpp:FullPrintConfig:triple-inheritance-collision | High | P1 |
| 703 | `ModelConfig::assign_config()` short-circuits assignment when `this->timestamp() == rhs.timestamp()`. If two `ModelConfig` instances independently receive identical modifications (same key/value, different objects), their timestamps will differ and assignment will proceed. But if a config is cloned and modified in-place without bumping the timestamp, the assignment is silently skipped even though content differs | PrintConfig.hpp:ModelConfig:assign_config-timestamp-false-skip | Medium | P2 |
| 704 | `get_flush_volumes_matrix()` default parameter `extruder_id = (size_t)-1` wraps to `SIZE_MAX`. The function indexes `flush_volumes_vector[extruder_id]` only when `extruder_id < flush_volumes_vector.size()`. Since `SIZE_MAX >= vector.size()` always holds, the default path falls through correctly — but this relies on unsigned wrap-around semantics. A port that uses signed integers or range checks will change the default behavior | PrintConfig.hpp:GCodeConfig:get_flush_volumes_matrix-SIZE_MAX-default | Medium | P2 |
| 705 | `set_flush_volumes_matrix()` declares `bool is_multi_extruder` but never reads it. The variable is dead code. This suggests an incomplete conditional path that was never implemented — multi-extruder flush volume handling may differ from single-extruder but the distinction is currently ignored | PrintConfig.hpp:GCodeConfig:set_flush_volumes_matrix-dead-is_multi_extruder | Low | P3 |
| 706 | Config options are serialized by Cereal using integer ordinals (positions in the `((Type, name))` macro sequence). If an option is removed and its ordinal is reused by a new option, or if the sequence is reordered, old save files will silently load the wrong value into the wrong field. The ordinal assignment is implicit — no explicit numbering in source | PrintConfig.hpp:cereal-serialization:ordinal-reuse-silent-corruption | High | P1 |
| 707 | `FullPrintConfig::load()` Cereal deserializer: unknown ordinals trigger an `assert(false)` which is DEBUG-only. In release builds, unknown ordinals are silently ignored — no exception, no log. If an ordinal collision (H706) produces a valid-but-wrong ordinal, the wrong field is populated without any diagnostic | PrintConfig.hpp:cereal-serialization:unknown-ordinal-release-silent | High | P1 |
| 708 | `AABBTreeIndirect::Tree<>` is static: once built, adding or removing primitives requires a full rebuild. There is no incremental update API. Callers that mutate the underlying geometry after `build()` will silently query a stale tree with incorrect nearest-neighbor and ray-intersection results | AABBTreeIndirect.hpp:Tree:static-no-incremental-update | High | P1 |
| 709 | `build_modify_input()` partitions the input vector in place via QuickSelect; `build(std::move(input))` additionally clears the vector after use. Callers must not assume the input vector is intact after either call. Passing a vector that is still needed elsewhere (e.g., also the mesh face list) causes a subtle use-after-move bug | AABBTreeIndirect.hpp:Tree:build-consumes-input | Medium | P2 |
| 710 | `build_recursive()` requires `node < m_nodes.size()`. The allocation uses `next_highest_power_of_2(N)*2-1` nodes. Any direct call to `build_recursive()` that does not respect this pre-allocated size will assert (DEBUG) or access out-of-bounds memory (release). The public `build()` / `build_modify_input()` paths are safe; internal callers of `build_recursive()` must maintain the invariant | AABBTreeIndirect.hpp:build_recursive:node-index-invariant | Medium | P2 |
| 711 | `BoundingBoxWrapper::centroid()` computes `(m_bbox.min() + m_bbox.max() / 2)` instead of `(m_bbox.min() + m_bbox.max()) / 2`. Integer division on `max()` before adding `min()` produces a centroid shifted toward `min()` by 0–1 scaled units. Benign in practice but mathematically incorrect; may slightly de-balance the tree for wide bounding boxes | AABBTreeIndirect.hpp:BoundingBoxWrapper:centroid-operator-order | Low | P3 |
| 712 | `ray_box_intersect_invdir()` takes `box` by value and swaps its `min/max` faces to handle negative-direction rays. This is intentional but can confuse readers expecting a const reference. The mutation of a local copy is safe, but a port that passes by reference would corrupt the stored tree node | AABBTreeIndirect.hpp:ray_box_intersect_invdir:box-by-value-mutation | Low | P3 |
| 713 | FIXME comment in `ray_box_intersect_invdir()` acknowledges that SSE optimisation is not implemented. The scalar slab-method code is correct but ~4× slower than an SSE implementation for float trees. Ray-casting workloads (overhang detection, support ray tests) are bottlenecked here | AABBTreeIndirect.hpp:ray_box_intersect_invdir:no-SSE | Low | P3 |
| 714 | `closest_point_to_triangle()` guards the edge-AB case with `a != b` to prevent division by zero, but does not guard degenerate edge-AC or edge-BC cases. A fully degenerate triangle (all vertices coincident) returns `a` silently. Partially degenerate triangles (two vertices equal) may produce division by zero or incorrect closest points on unguarded branches | AABBTreeIndirect.hpp:closest_point_to_triangle:degenerate-edge-unguarded | Medium | P2 |
| 715 | In `squared_distance_to_indexed_primitives_recursive()`, `low_sqr_d` is passed in as an early-out guard for the bounding-box check but is ignored when the bounding box contains the query origin (the `bbox.contains()` branch always expands). For a query point inside many nested bounding boxes (e.g., center of a dense mesh), this degrades traversal to O(N) | AABBTreeIndirect.hpp:squared_distance_recursive:contains-expands-unconditionally | Medium | P2 |
| 716 | `build_aabb_tree_over_indexed_triangle_set()` `eps` parameter defaults to 0 (no epsilon expansion on bounding boxes). For meshes with near-zero-area triangles or nearly axis-aligned edges, an eps=0 tree can miss ray intersections that just clip a face corner due to floating-point rounding. The FIXME comment in source acknowledges this is unresolved | AABBTreeIndirect.hpp:build_aabb_tree:eps-zero-default | Medium | P2 |
| 717 | `point_outside_closed_contours()` casts a horizontal ray in both directions and checks that both hit counts are consistently odd (inside) or even (outside). If they disagree it retries with a vertical ray. If the vertical ray also disagrees it returns 0 (indeterminate). This indeterminate result is silently treated as "on boundary" by callers — not as an error. For numerically degenerate geometry (self-intersecting contours, extremely thin slivers) the test can return 0 permanently, silently dropping those regions from inside/outside classification | AABBTreeLines.hpp:point_outside_closed_contours:indeterminate-zero | High | P1 |
| 718 | `LinesDistancer::distance_from_lines_extra<SIGNED_DISTANCE>` multiplies unsigned distance by `outside()`, which returns -1 for inside, 1 for outside, and 0 for indeterminate (H717). If `outside()` returns 0, the signed distance is 0 regardless of the actual geometric distance — silently incorrect for near-boundary query points on degenerate geometry | AABBTreeLines.hpp:LinesDistancer:distance_from_lines_extra:outside-zero-signed | High | P1 |
| 719 | `intersect_ray_recursive_first_hit()` visits left then right child unconditionally (no ordering by box-entry distance). The left child is always expanded first even when the right child box is geometrically closer. For dense meshes where many triangles lie behind the first intersection, unordered traversal can cause O(log N) extra triangle tests compared to a front-to-back ordered traversal | AABBTreeIndirect.hpp:intersect_ray_recursive_first_hit:unordered-traversal | Low | P3 |
| 720 | `indexed_primitives_within_distance_squared_recurisve()` contains a typo in the function name: "recurisve" should be "recursive". The misspelling propagates to all call sites. A refactoring must either preserve the misspelled name (for link compatibility with TUs not in this translation unit) or update all callers simultaneously | AABBTreeIndirect.hpp:indexed_primitives_within_distance_squared_recurisve:typo-name | Low | P3 |
| 721 | `ExtrusionLoopRole` is defined as a plain `uint8_t` enum, but its values are bitmask-composable (e.g., `elrFirstLoop|elrSkinnableLoop`). No `operator|` or `operator&` is declared, so bitmask composition requires explicit casts to `uint8_t` and back. A port that treats `ExtrusionLoopRole` as an exclusive enum will miss combined roles and silently mis-classify loops | ExtrusionEntity.hpp:ExtrusionLoopRole:implicit-bitmask-no-operators | Medium | P2 |
| 722 | `ExtrusionLoopRole` bitmask values are not a power-of-two sequence: `elrFirstLoop=1`, `elrSkinnableLoop=2`, `elrHasOverhangPatch=4`, `elrInternal=8` — but `elrDefault=0` is the "no flags" state. `elrDefault` cannot be tested with `& elrDefault` (always 0). All callers must compare directly `== elrDefault`, not bitwise-test | ExtrusionEntity.hpp:ExtrusionLoopRole:elrDefault-zero-no-bit-test | Low | P3 |
| 723 | `ExtrusionPath` default constructor initializes `mm3_per_mm` to `-1`. Calling `total_volume()` on a default-constructed path returns `length() * -1` — a negative volume. No assertion guards this. Any code path that accumulates volumes without verifying `mm3_per_mm > 0` will produce silently negative totals, corrupting filament usage estimates | ExtrusionEntity.hpp:ExtrusionPath:default-mm3_per_mm-negative | Medium | P2 |
| 724 | `ExtrusionPath::polyline` is a public non-const member. The path length and bounding box are computed on demand from `polyline.points` with no caching. Any mutation of `polyline` after construction (e.g., via `clip_end()` or `reverse()`) is silently reflected in subsequent length/area queries — correct behavior but requires discipline in a port that adds caching | ExtrusionEntity.hpp:ExtrusionPath:polyline-public-mutable | Low | P3 |
| 725 | `ExtrusionPathSloped` holds `SlopedParams` by value and passes `slope_max_segment_length` to `ExtrusionLoopSloped`. The sloped ramp feature subdivides path segments at construction time. If the path geometry is subsequently altered (e.g., clipped), the per-segment slope parameters become inconsistent with the new geometry. No re-validation is performed | ExtrusionEntity.hpp:ExtrusionPathSloped:slope-params-stale-after-clip | Medium | P2 |
| 726 | `extrusion_entities_append_paths_with_wipe()` groups paths that start or end within `3 * width` of each other into `ExtrusionMultiPath` objects. The `3×` threshold is hardcoded. For very large nozzle diameters the grouping radius may incorrectly merge paths from different perimeter loops, producing wipe segments that cross empty space | ExtrusionEntity.hpp:extrusion_entities_append_paths_with_wipe:3x-width-hardcoded | Low | P3 |
| 727 | `ExtrusionLoop::clip_front()` for `erPerimeter` overrides `clip_dist` with `ext_perimeter_overlap * crossection`. The guard `while (distance > 0)` is correct conceptually, but this override uses `clip_dist` in a different unit context than the outer loop expects. If `ext_perimeter_overlap` is near zero, `clip_dist` approaches zero while `distance` is non-zero, creating a near-infinite loop | ExtrusionEntity.cpp:clip_front:erPerimeter-clip_dist-near-zero-loop | High | P1 |
| 728 | `extrusion_entities_append_paths_with_wipe()` creates `ExtrusionMultiPath` objects that share the intermediate wipe-connector polylines. The wipe path point vectors are value-copied into the connector `ExtrusionPath`, then the original paths are moved into `entities`. If the returned `ExtrusionEntityCollection` is shallow-copied (not deep-copied via `clone()`), the connector paths may alias freed memory | ExtrusionEntity.hpp:ExtrusionMultiPath:wipe-connector-alias-on-shallow-copy | High | P1 |
| 729 | `ExtrusionPath::polygons_covered_by_spacing()` calls `polygons_covered_by_width()` internally and then applies `offset_ex()` only if `spacing > width`. The spacing calculation reuses the width buffer. For paths where `spacing == width` exactly, the branch is skipped and the width polygon is returned as-is — this is correct, but the comment "covered by spacing" may mislead a reader expecting a spacing-based polygon rather than a width-based one | ExtrusionEntity.cpp:polygons_covered_by_spacing:spacing-equals-width-passthrough | Low | P3 |
| 730 | `ExtrusionLoop::split_at_vertex()` iterates the loop's paths to find the closest vertex, then calls `split_at()`. For loops that have been clipped or rearranged, the closest vertex may not be at a semantically correct seam position. There is no seam-quality check — purely geometric nearest vertex | ExtrusionEntity.cpp:split_at_vertex:geometric-only-no-seam-quality | Low | P3 |
| 731 | `ExtrusionLoopSloped` constructor contains a recursive lambda `handle_line` that bisects segments until each piece is ≤ `slope_max_segment_length`. This is a stack-recursive lambda — for very short `slope_max_segment_length` and very long input segments, the recursion depth is O(log(segment_length / max_length)). For extreme inputs (1 m segment, 0.001 mm max), this is ~20 levels — safe in practice, but no explicit depth guard | ExtrusionEntity.cpp:ExtrusionLoopSloped:recursive-lambda-depth | Low | P3 |
| 732 | `ExtrusionLoopSloped::clip_slope()` assumes that the first `ClipSlopeParams::clip_dist_front` mm of the loop is the entry ramp and the last `clip_dist_back` mm is the exit ramp. If the loop has been rotated (split_at called) after slope parameter computation, the entry/exit ramp positions shift — the clip distances no longer correspond to the intended ramp segments | ExtrusionEntity.cpp:clip_slope:ramp-position-depends-on-loop-rotation | Medium | P2 |
| 733 | `clip_front()` for `erPerimeter` branches: the condition is `role() == erPerimeter`, but `ExtrusionLoopSloped` inherits `ExtrusionLoop::role()` which returns the role of the first path. If the first path has been replaced or clipped and its role changed, the erPerimeter branch is silently skipped, leaving the near-zero `clip_dist` issue (H727) unaddressed for what is now a non-perimeter role | ExtrusionEntity.cpp:clip_front:role-from-first-path-dynamic | Medium | P2 |
| 734 | `role_to_string()` / `string_to_role()` are used for G-code metadata serialization. If a new `ExtrusionRole` enum value is added without updating both functions, the new role serializes to "Unknown" and deserializes back to `erNone` — silently corrupting role-dependent post-processing (e.g., seam detection, pressure advance tuning) | ExtrusionEntity.cpp:role_to_string:new-role-missing-from-map | Medium | P2 |
| 735 | `ExtrusionEntityCollection::filter_by_extrusion_role()` returns a `std::vector<ExtrusionEntity*>` of raw pointers into the original collection's `entities` vector. The returned vector does NOT own its elements. If the source collection is destroyed or `clear()`-ed before the caller is done with the filtered view, all pointers dangle | ExtrusionEntityCollection.hpp:filter_by_extrusion_role:shallow-raw-ptr-view | High | P1 |
| 736 | `filter_by_extrusion_role_in_place()` erases elements that do not match the requested role using `std::remove_if` + `erase`. Erased elements are NOT deleted — the heap-allocated `ExtrusionEntity` objects they point to are silently leaked. This function must only be called on a vector that does not own its elements (e.g., after `filter_by_extrusion_role`) | ExtrusionEntityCollection.hpp:filter_by_extrusion_role_in_place:no-delete-on-erase | High | P1 |
| 737 | `chained_path_from()` calls `filter_by_extrusion_role()` (returns aliased pointers), then iterates the result replacing each with a clone. If an exception is thrown partway through the clone loop, already-cloned entries are owned but not freed (the `out` collection destructor will handle them), while uncloned entries still alias originals — no double-free, but a memory leak of the partial clones if the exception propagates past the collection's destructor. No RAII guard exists | ExtrusionEntityCollection.cpp:chained_path_from:partial-clone-exception-leak | Medium | P2 |
| 738 | `AABBMesh::m_tm` is a raw `const indexed_triangle_set*` — mesh lifetime must exceed the `AABBMesh` object's lifetime. No `shared_ptr` or ownership transfer is performed. If the mesh is destroyed (e.g., `TriangleMesh` goes out of scope) while `AABBMesh` is still alive, all ray queries and nearest-point queries dereference a dangling pointer | AABBMesh.cpp:AABBMesh:m_tm-raw-pointer | High | P1 |
| 739 | `AABBImpl::init()` adaptive epsilon: default is `1e-6`; with `calculate_epsilon=true` it becomes `1e-6 * l²` (l = average edge length). For very large meshes (l > 1000 mm) the scaled epsilon becomes 1.0, causing near-surface rays to fire false hits (epsilon too large). For very small meshes (l < 0.1 mm) the default `1e-6` is too tight — rays grazing mesh edges are missed | AABBMesh.cpp:AABBImpl:init:epsilon-scale-mismatch | Medium | P2 |
| 740 | `AABBMesh` copy constructor and copy assignment operator both shallow-copy `m_tm` (raw pointer). Both the original and the copy point to the same mesh data with no reference counting. Destroying the original while the copy is alive (or vice versa if the mesh was owned by the original's caller) leaves the surviving object with a dangling `m_tm` | AABBMesh.cpp:AABBMesh:copy-ctor-shallow-m_tm | High | P1 |
| 741 | `AABBMesh::hit_result::is_inside()` computes `dot(m_normal, m_dir)` without normalizing `m_dir`. If `query_ray_hit()` is called with a non-unit direction vector (the debug assert fires but is inactive in release), `is_inside()` returns incorrect results proportional to the direction magnitude | AABBMesh.hpp:hit_result:is_inside:non-unit-dir | Medium | P2 |
| 742 | `AABBMesh::query_ray_hit()` asserts `is_approx(dir.norm(), 1.)` in DEBUG only. Passing a non-unit `dir` in release silently produces incorrect `hit.t` distances (the Möller–Trumbore intersection algorithm assumes a unit direction for the t parameter to represent physical distance) | AABBMesh.cpp:query_ray_hit:non-unit-dir-release-silent | Medium | P2 |
| 743 | `AABBMesh::query_ray_hits()` deduplicates by exact float equality `a.t == b.t`. A ray grazing a shared triangle edge may produce two `igl::Hit` entries with `t` values differing by ~1 ULP. These survive deduplication and create spurious extra entry/exit pairs in inside/outside counting, corrupting hollowing/SLA support ray tests | AABBMesh.cpp:query_ray_hits:dedup-exact-float | Medium | P2 |
| 744 | `AABBImpl::squared_distance()` takes an `Eigen::Matrix<double,1,3>` (1×3 row vector) as the `closest` out-parameter, but `squared_distance_to_indexed_triangle_set` uses `Vec3d` (3×1 column vector) internally. The explicit assignment `closest = closest_vec3d` works because Eigen allows row/column assignment, but this is fragile: enabling `EIGEN_DEFAULT_TO_ROW_MAJOR` globally would transpose the internal computation and produce an incorrect `closest` point | AABBMesh.cpp:AABBImpl:squared_distance:row-vs-column-vector | Low | P3 |
| 745 | `AABBMesh::vertices(size_t idx)` and `AABBMesh::indices(size_t idx)` index directly into `m_tm->vertices` and `m_tm->indices` without bounds checking. Out-of-range `idx` values produce UB in release builds. All callers are expected to pass valid face/vertex indices from the AABB tree — a contract that is not enforced at the API level | AABBMesh.cpp:AABBMesh:vertices-indices-no-bounds-check | Medium | P2 |
| 746 | `AABBMesh::normal_by_face_id()` calls `its_unnormalized_normal(*m_tm, face_id).cast<double>().normalized()`. For degenerate zero-area triangles (collinear or coincident vertices), the cross-product is the zero vector. Eigen's `.normalized()` on a zero vector returns NaN without throwing. The NaN propagates silently to seam placer, support generation, and SLA hollowing ray tests | AABBMesh.cpp:normal_by_face_id:degenerate-triangle-nan | High | P1 |
| 747 | `igl::Hit::t` is a `float`. Both `query_ray_hit()` and `query_ray_hits()` widen `hit.t` to `double` via `double(hit.t)`. For large meshes where ray distances exceed ~16 million units (≈ 16 m at 1 mm/unit), float precision drops below 1 mm. The widening is silent — callers receive a double but with only float resolution | AABBMesh.cpp:query_ray_hit:float-t-double-widening | Medium | P2 |
| 748 | Dead-code `filter_hits()` (behind `#ifdef SLIC3R_HOLE_RAYCASTER`): the event-loop post-increments `next_hole_hit` and `next_mesh_hit` past `.back()`, leaving them one-past-the-end. The next iteration dereferences this dangling past-the-end pointer (UB). The feature was never shipped; the pointer bug is likely the reason it was abandoned | AABBMesh.cpp:filter_hits:post-increment-past-back-ub | High | P1 |
| 749 | `Brim.cpp` `append_and_translate()` (brimAreaMap overload) subtracts the global brim accumulator (`dst`) before inserting the current instance's brim. Processing order is determined by iteration order over print objects and instances — non-deterministic for multi-object plates. Earlier instances in iteration order claim their full brim area; later instances get only the remainder, potentially much less than their configured brim width | Brim.cpp:append_and_translate:instance-order-brim-theft | Medium | P2 |
| 750 | `getadhesionCoeff()` iterates all `ModelVolume`s and all `firstLayerRegions` extruders, overwriting `adhesionCoeff` on every match. For multi-material objects where different volumes have different filament types with different adhesion coefficients, only the last matching (volume, extruder) pair's coefficient is used. The first volume's coefficient is silently discarded | Brim.cpp:getadhesionCoeff:last-match-wins | Medium | P2 |
| 751 | `getadhesionCoeff()` has a large commented-out block of Python-style enum values after the `return adhesionCoeff` statement. The dead code is unreachable but confuses readers. It is a leftover from an earlier design that used a string enum lookup table | Brim.cpp:getadhesionCoeff:dead-commented-code | Low | P3 |
| 752 | `compSecondMoment(Polygon, Vec2d&)` computes the second moment of area using raw scaled integer coordinates (×1e6 mm). The result is in `(scaled_units)^4`. The caller `configBrimWidthByVolumeGroups` must multiply by `SCALING_FACTOR^4` to convert to mm⁴ before comparing against dimensional thresholds. Omitting this conversion produces auto-brim widths ~10^24× too small, silently emitting minimum-width brims for all geometries | Brim.cpp:compSecondMoment:scaled-integer-units-power-4 | High | P1 |
| 753 | `configBrimWidthByVolumeGroups` computes `height_to_area = height / (Ixx + Iyy)`. If the ExPolygon is degenerate (near-zero area → near-zero second moments), the denominator approaches zero. There is no guard, and division by near-zero produces a very large `height_to_area` that clamps to `max_brim_width`, silently maximizing brim width for degenerate geometry | Brim.cpp:configBrimWidthByVolumeGroups:divide-by-near-zero-moment | High | P1 |
| 754 | The constant `1920` in `height_to_area` formula is empirically tuned and has no source citation or dimensional analysis justification in comments. The value implicitly encodes units (combining mm⁴ × 1e24 scaling with an adhesion-coefficient scaling). Any change to the SCALING_FACTOR or the adhesion range would require re-tuning this constant | Brim.cpp:configBrimWidthByVolumeGroups:magic-1920-constant | Low | P3 |
| 755 | The brim connectivity filter uses `2 × flow_spacing` as the proximity threshold to group adjacent object outlines for shared brim generation. This value is hardcoded. For large nozzle diameters (e.g., 1.2 mm) or wide flow, `2 × spacing` may exceed the gap between distinct print objects on the plate, causing their brims to be merged | Brim.cpp:make_brim:2x-spacing-connectivity-threshold | Low | P3 |
| 756 | The support brim code path inside `make_brim()` contains large commented-out blocks (several hundred lines). The active code generates support brim in a simplified form; the commented code represents an alternate (disabled) implementation. The design intent is unclear — it is not documented whether the commented path was superseded or is being actively developed | Brim.cpp:make_brim:support-brim-commented-blocks | Low | P3 |
| 757 | `make_brim_by_linesType_in_object()` uses a `while (true)` loop that terminates only when `islands_ex` becomes empty after repeated `offset_ex()` shrink operations. For degenerate geometry where `offset_ex()` never converges to empty (e.g., geometry where the offset step is smaller than coordinate quantization), this loop runs forever. No iteration limit or fallback exists | Brim.cpp:make_brim_by_linesType_in_object:while-true-no-limit | High | P1 |
| 758 | `make_brim_by_linesType_in_object()` splits the per-loop offset into `+1.3 × spacing` (outer expansion) and `−0.3 × spacing` (inner retraction). The 1.3/0.3 ratio is an undocumented heuristic — it provides a small inward trim to prevent brim lines from slightly overlapping the object footprint. Any change to flow width or spacing scale would require re-tuning these constants | Brim.cpp:make_brim_by_linesType_in_object:1.3-0.3-magic-ratio | Low | P3 |
| 759 | `make_brim()` calls `connect_brim_lines()` on the locally-shifted brim polylines BEFORE applying the plate offset translation. This is functionally correct (connectivity is evaluated in object-local coordinates) but creates a brittle call-order dependency: swapping `connect_brim_lines` and the plate-offset translate would evaluate connectivity in plate-shifted coordinates — producing correct connection logic but silently in a different coordinate frame | Brim.cpp:make_brim:connect_before_plate_offset-order-dep | Medium | P2 |
| 760 | `make_brim()` receives a `const Print&` parameter, but writes `firstLayerObjectBrimBoundingBox` back into `PrintObject` via `const_cast<PrintObject*>(print.get_object(i))`. This violates the const contract of the function signature, making the side-effect invisible to callers. A refactoring that enforces const correctness will fail here without a design change | Brim.cpp:make_brim:const_cast-PrintObject | Medium | P2 |
| 761 | `ArcFitter::do_arc_fitting_and_simplify()` passes `points` by non-const reference and mutates it in place (removes intermediate points that were merged into arcs). The caller's point vector is silently modified. Any code path that holds a reference or iterator into `points` before the call will dangle after it returns | ArcFitter.cpp:do_arc_fitting_and_simplify:points-mutated-in-place | High | P1 |
| 762 | The index-remapping step in `do_arc_fitting_and_simplify()` uses a prefix-sum (`reduce_count`) that assumes output segments are strictly ascending and non-overlapping in the original index space. If two arc-fitting passes produce overlapping index ranges (possible if the greedy window restarts), the prefix-sum remapping produces incorrect indices that point to wrong points in the output path | ArcFitter.cpp:do_arc_fitting_and_simplify:prefix-sum-remapping-correctness | High | P1 |
| 763 | `ArcFitter::do_arc_fitting()` guards `points.size() < 3` with an early-return that emits a single `Linear_move` spanning `[0, size-1]`. For `size == 0`, `points[size-1]` is `points[-1]` — undefined behaviour. Callers must guarantee `points.size() >= 1` before calling; the function does not assert this | ArcFitter.cpp:do_arc_fitting:size-zero-ub | Medium | P2 |
| 764 | `elephant_foot_compensation()` public API accepts `compensation` as a plain `double` with no sign check. A positive value shrinks the outline (intended). A negative value silently expands it — the opposite of the intended behaviour. There is no clamping guard or assertion in the public API; callers must pass a non-negative value by convention | ElephantFootCompensation.hpp:elephant_foot_compensation:negative-compensation-expands | Medium | P2 |
| 765 | `contour_distance()` (legacy SDF ray-cast variant) returns an empty `std::vector<float>` for contours with ≤ 2 points and does not set any error indicator. `contour_distance2()` (active nearest-point variant) has the same edge case at line 300. Callers that iterate the returned vector without size checking silently skip compensation for degenerate single-segment contours | ElephantFootCompensation.cpp:contour_distance:empty-output-le-2-points | Low | P3 |
| 766 | `contour_distance()` computes the fan half-angle `a` using a `0.48` magic constant in the cross-product formula. The constant was tuned empirically and is not documented in any external reference or design note. Any refactoring or re-implementation must reproduce this exact constant experimentally to preserve arc-fitting behaviour | ElephantFootCompensation.cpp:contour_distance:fan-angle-magic-0.48 | Low | P3 |
| 767 | `contour_distance2()` (nearest-point variant, line ~300) silently returns an empty vector for contours with ≤ 2 points — same edge case as H765. This is the active code path used in production; the empty return means the compensation step is silently skipped for those contours with no diagnostic | ElephantFootCompensation.cpp:contour_distance2:empty-output-le-2-points | Medium | P2 |
| 768 | The `band` parameter passed into the Laplacian smoothing step of `elephant_foot_compensation()` must be in scaled coordinate units (`coord_t` magnitude, ×1e6). The public API accepts `compensation` in mm and scales it internally, but `band` is computed as a fraction of a scaled value elsewhere. A port that works in mm throughout must ensure `band` is converted before use; passing mm directly produces a band ~1e6× too narrow | ElephantFootCompensation.cpp:laplacian-smooth:band-in-scaled-units | High | P1 |
| 769 | At the final step of `elephant_foot_compensation()`, `variable_offset_inner_ex()` is expected to return exactly 1 ExPolygon. If it returns 0 or >1 polygons (possible on very complex or degenerate inputs), the function falls back to returning the original unmodified input. This fallback is silent in release builds — no log, no warning. A debug-only SVG is written only when `TESTS_EXPORT_SVGS` is defined | ElephantFootCompensation.cpp:elephant_foot_compensation:variable-offset-returns-ne-1 | Medium | P2 |
| 770 | The `WallInfillOrder` enum map (`s_keys_map_WallInfillOrder`) contains a duplicate string key: `"inner-outer-inner wall/infill"` appears twice (once for `InnerOuterInnerInfill=2` and once for value 5). `std::map` silently keeps the first-inserted value on duplicate key insertion. The second entry is silently dropped. `InfillInnerOuter` and `InfillOuterInner` (values 3 and 4) are inaccessible by name lookup for their duplicated key. Any port must deduplicate this map | PrintConfig.cpp:s_keys_map_WallInfillOrder:duplicate-key | Low | P3 |
| 771 | `TimelapseType` enum uses numeric strings `"0"` and `"1"` as serialisation keys instead of descriptive names. Old project files must contain the literal characters `0` or `1` in the `timelapse_type` field. Renaming these keys to descriptive strings (e.g., `"traditional"`, `"smooth"`) is a silent preset-file format break for all existing saves | PrintConfig.cpp:s_keys_map_TimelapseType:numeric-key-compat | Medium | P2 |
| 772 | `get_extruder_variant_string()` range-checks `extruder_type <= etMaxExtruderType` and `nozzle_volume_type <= nvtMaxNozzleVolumeType`. If a new enum variant is added without updating these sentinel values, the check passes for invalid inputs and indexes `s_keys_names_ExtruderType` or `s_keys_names_NozzleVolumeType` out-of-bounds (UB in release builds) | PrintConfig.cpp:get_extruder_variant_string:sentinel-not-updated | Medium | P2 |
| 773 | `get_extruder_ams_count()` calls `stoi(numbers[0])` and `stoi(numbers[1])` on split strings with no try/catch. Malformed input (non-numeric ams_info segment) propagates `std::invalid_argument` or `std::out_of_range` uncaught. The `assert(numbers.size() == 2)` fires only in DEBUG; in release a single-token segment crashes the process | PrintConfig.cpp:get_extruder_ams_count:stoi-uncaught-exception | High | P1 |
| 774 | Throughout `PrintConfigDef::init_*_params()`, `L(s)` is used as a translation extraction marker that evaluates to `s` at runtime (NOT a translator). `_(s)` is the actual runtime i18n function. Confusing `L()` for `_()` would produce untranslated UI strings. Using `_()` at static init time inside a `set_default_value()` would attempt translation before the locale is loaded, producing garbled or empty defaults | PrintConfig.cpp:L-vs-underscore-translation-distinction | High | P1 |
| 775 | `init_fff_params()` is ~6000 lines and registers hundreds of options with hardcoded default values. Any default value change is a silent behaviour change for all newly-created profiles. Old saved profiles that omit an explicit value inherit the new default on next load, potentially altering print quality without user awareness | PrintConfig.cpp:init_fff_params:default-value-silent-change | Medium | P2 |
| 776 | `handle_legacy()` is a ~244-line if/else-if chain mapping old option keys to current names. There is no unit test coverage for the majority of individual renames. An error in any mapping silently drops the setting when opening old project files — the option-not-found path ignores unknown keys rather than warning | PrintConfig.cpp:handle_legacy:no-test-coverage-silent-drop | High | P1 |
| 777 | `get_shared_poly()` iterates extruder printable areas and intersects them pairwise. If any intersection produces an empty result (non-overlapping extruder reaches), `result_polygon[0]` is an out-of-bounds access — undefined behaviour in release builds. No guard exists for the empty `result_polygon` case | PrintConfig.cpp:get_shared_poly:empty-intersection-oob | High | P1 |
| 778 | `get_bed_excluded_area()` returns a Polygons containing a single polygon built from `bed_exclude_area` config points. If the config contains 0 or 1 points, the returned polygon is degenerate. Callers that pass this into Clipper `diff()` operations will receive UB or incorrect results without any diagnostic | PrintConfig.cpp:get_bed_excluded_area:degenerate-polygon | Medium | P2 |
| 779 | `min_object_distance()` uses a hardcoded `duplicate_distance = 6.0` floor (commented "BBS: duplicate_distance seems to be useless"). This constant is the unconditional minimum separation even when `extruder_clearance_radius < 6`. Changing this constant would silently alter object spacing for all FFF printers globally | PrintConfig.cpp:min_object_distance:hardcoded-6mm-floor | Low | P3 |
| 780 | `normalize_fdm()` erases the `"extruder"` key during its first call and propagates it to `sparse_infill_filament` / `wall_filament`. Subsequent calls on the same config see no `"extruder"` key and silently skip the propagation. Any code path that calls `normalize_fdm()` multiple times or on an already-normalised config relies on idempotency by convention, not enforcement | PrintConfig.cpp:normalize_fdm:extruder-key-erased-not-idempotent | Medium | P2 |
| 781 | `update_arrange_params()` adds brim_skirt_distance to bed_shrink_x/y and subtracts clearance_radius/2 for seq_print. The function is NOT idempotent — calling it twice accumulates the shrink, doubling the skirt offset and clearance subtraction with no warning | Arrange.cpp:update_arrange_params:not-idempotent | Medium | P2 |
| 782 | `update_selected_items_inflation()` clamps item inflation to `min(diffx,diffy)/2 - 5` (scaled), where diff is bed-minus-item bounding box delta. The constant `5` (scaled units = 5e-6 mm) is essentially zero and has no visible effect, but the -5 means items within 5 scaled units of bed width/height silently receive zero inflation instead of their brim_width | Arrange.cpp:update_selected_items_inflation:magic-5-clamp | Low | P3 |
| 783 | When any object on the plate has tree support, ALL items use `brim_max/2` as inflation (max tree branch radius across all items), overriding each item's own brim_width. Non-tree-support items on mixed plates receive over-inflated spacing proportional to the largest tree support branch diameter | Arrange.cpp:update_selected_items_inflation:tree-support-global-inflation | Low | P3 |
| 784 | `update_unselected_items_inflation()` computes exclusion_gap using `params.bed_shrink_x` which must already be updated by `update_arrange_params()`. Calling these out of order produces wrong exclusion_gap — no assertion enforces call order | Arrange.cpp:update_unselected_items_inflation:call-order-dep | Medium | P2 |
| 785 | `update_selected_items_axis_align()` uses ratio threshold 0.66 to decide if the two principal second moments are "too equal" for rotation. This constant is undocumented and empirically chosen. Objects just below or above 0.66 may be treated inconsistently across slicer versions if the constant changes | Arrange.cpp:update_selected_items_axis_align:0.66-magic-threshold | Low | P3 |
| 786 | `get_shrink_bedpts()` uses the SGN() macro to move each bed vertex toward the center. For concave bed shapes, some vertices lie "past" the center in one axis, and the SGN() movement pushes them in the wrong direction, incorrectly shrinking the bed | Arrange.cpp:get_shrink_bedpts:SGN-wrong-for-concave-bed | Medium | P2 |
| 787 | `fill_config()` comment "Start placing the items from the center of the print bed" appears for both BOTTOM_LEFT and TOP_RIGHT branches. The comment is a copy-paste error — TOP_RIGHT starts from the top-right corner, not the center | Arrange.cpp:fill_config:misleading-comment-starting-point | Low | P3 |
| 788 | `objfunc()` BIG_ITEM branch alignment scoring only applies to neighbours with `|1 - parea/item.area()| < 1e-6` (essentially identical area). For objects of unique size, no neighbour qualifies and alignment_score stays 1.0 (worst), permanently applying maximum alignment penalty regardless of actual layout quality | Arrange.cpp:objfunc:alignment-score-disabled-for-unique-size-objects | Medium | P2 |
| 789 | `objfunc()` layered mode height_score divides by valid_items_cnt. But the loop may break early on filament incompatibility, leaving height_score partially accumulated. Dividing partial height_score by partial valid_items_cnt gives a wrong (over-averaged) proximity bonus for incompatible-filament scenarios | Arrange.cpp:objfunc:height-score-partial-count-division | Low | P3 |
| 790 | `_arrange()` sets `mod_params.min_obj_distance = 0` assuming items are pre-inflated. If a caller passes both pre-inflated items AND non-zero min_obj_distance, the effective gap between items will be the inflation alone (min_obj_distance is zeroed). No documentation or assertion enforces this precondition | Arrange.cpp:_arrange:min_obj_distance-zeroed-pre-inflated-contract | Medium | P2 |
| 791 | `_arrange()` computes `md = params.min_obj_distance / 2` but never uses `md` — the `sl::offset(corrected_bin, md)` call is commented out. Dead variable `md` should be removed; its presence implies the bed offset strategy may have been partially reverted | Arrange.cpp:_arrange:dead-md-variable | Low | P3 |
| 792 | `process_arrangeable()` reverses counter-clockwise polygons to meet libnest2d's clockwise winding requirement. If the Clipper output winding convention changes (e.g., upgrade to Clipper2 which uses opposite convention), all items will be placed with incorrect winding without any diagnostic | Arrange.cpp:process_arrangeable:winding-convention-fragile | Medium | P2 |
| 793 | `call_with_bed()` classifies a bed as rectangular if `(1 - poly_area/bbox_area) < 1e-3`. Beds within 0.1% of their bounding box area are treated as exact rectangles (BoundingBox). Chamfered or slightly irregular rectangular beds lose their boundary constraint — objects may be placed in the chamfered corners | Arrange.cpp:call_with_bed:0.1pct-rectangle-coercion | Low | P3 |
| 794 | In `arrange<BedT>()`, `fixeditems` are deflated by `-2*EPSILON` twice: once explicitly here, and once inside `fill_config()` for excluded_regions. Items passed as `excludes` receive double deflation — this is intentional but creates a visible 4*EPSILON gap that may look like a rounding artifact | Arrange.cpp:arrange:fixeditems-double-deflated | Low | P3 |
| 795 | `BuildVolume` constructor initialises `m_shared_volume.zs[1]` from `m_bboxf.max.z()` (the bed printable_height), then iterates extruder volumes and conditionally reduces `zs[1]` to the minimum extruder max-Z. However each extruder's `bboxf.max.z()` was computed with `extruder_printable_heights[index]` but `bboxf.min.z()` is then forcibly set to `-std::numeric_limits<double>::max()`. If an extruder's bboxf was inflated (e.g., inflated() call), `bboxf.max.z()` is correspondingly inflated, causing `m_shared_volume.zs[1]` to be silently reduced below the actual printable_height | BuildVolume.cpp:ctor:shared_volume_zs1_inflation_side_effect | Medium | P2 |
| 796 | `BuildVolume` constructor has `assert(printable_height >= 0)` which is a no-op in release builds. A negative `printable_height` produces `m_bboxf.max.z() < 0`, causing all objects to report `ObjectState::Below` regardless of their actual height. The constructor must clamp or validate `printable_height` for production robustness | BuildVolume.cpp:ctor:assert-printable-height-release-noop | Medium | P2 |
| 797 | `object_state_templ`: `num_above` counts mesh *vertices* above the bed plane, not triangles. A mesh whose vertices straddle the bed (some above, some below) but where every vertex-projected XY lies inside the build volume will return `Inside` even if the actual triangulated surface extends outside. The edge-crossing code only fires if `num_above < total` AND either `inside` or `outside` is unset — for large meshes with mixed vertex positions the early-exit condition may skip valid collision edges | BuildVolume.cpp:object_state_templ:vertex-count-not-triangle-count | Low | P3 |
| 798 | `BuildVolume_Type::Custom` is documented as a non-convex polygon but the containment test (`object_state()` and `all_paths_inside()`) uses `m_top_bottom_convex_hull_decomposition_scene/bed`, which is derived from the *convex hull* of the polygon. For L-shaped, U-shaped, or other non-convex beds, objects in the concave "notch" are falsely reported as Inside. The FIXME comment at the relevant switch cases acknowledges this limitation. No efficient non-convex polygon containment test is implemented | BuildVolume.cpp:object_state:custom-uses-convex-hull | High | P1 |
| 799 | `all_paths_inside()` Rectangle branch returns `build_volume.contains(paths_bbox)` immediately. This is an O(1) bounding-box shortcut: if the entire G-code paths bounding box fits inside the build volume, all moves are declared inside without per-move testing. This is logically correct (a bounding box that fits guarantees all contained points fit) but means any individual move is never explicitly tested — a future refactor adding per-move context (e.g., layer height at position) would need to change this path | BuildVolume.cpp:all_paths_inside:rectangle-o1-bbox-shortcut | Low | P3 |
| 800 | `check_object_state_with_extruder_area()` switch only handles `BuildVolume_Type::Rectangle` and `BuildVolume_Type::Circle`. For `BuildVolume_Type::Convex`, `BuildVolume_Type::Custom`, and `BuildVolume_Type::Invalid` the switch falls through to `default: break` — `return_state` remains `Inside`. The extruder reachability check is silently skipped for non-rectangular non-circular extruder areas, always reporting Inside and masking out-of-reach violations | BuildVolume.cpp:check_object_state_with_extruder_area:convex-custom-untested | High | P1 |
| 801 | `BuildVolume_Type::Custom` shares exactly the same containment algorithm as `BuildVolume_Type::Convex` throughout — both use `m_top_bottom_convex_hull_decomposition_scene`. There is no distinction in behaviour between a shape classified as Convex and one classified as Custom (which may be non-convex). The FIXME comment at both switch cases reads "doing test on convex hull until we learn to do test on non-convex polygons efficiently". The Custom classification carries implied semantics that are not honoured | BuildVolume.cpp:custom-same-as-convex-no-distinction | Medium | P2 |
| 802 | `rectangle_test()` (lines 241–341 in BuildVolume.cpp) is inside a `#if 0` block — permanently disabled. This function correctly handles non-convex object × rectangular volume intersection using projected triangle edge tests, which the active `object_state_templ` vertex-only path does NOT correctly handle. The FIXME comment at the active Rectangle branch explicitly references this as a known limitation. Activating this code would require restoring the missing `world_min_z` symbol and validating the clipping logic | BuildVolume.cpp:rectangle_test:if-0-disabled-accurate-test | Medium | P2 |
| 803 | `SLAPrintObject::SupportData` inherits from `sla::SupportableMesh` by value (not pointer/reference). Construction copies the entire mesh O(V+T) — for large meshes this is a significant memory and time cost. Additionally, if the mesh changes (e.g., hollowing re-runs), the SupportData's internal mesh copy becomes stale and must be reconstructed from scratch | SLAPrint.hpp:SupportData:value-inheritance-mesh-copy | Medium | P2 |
| 804 | `SLAPrintObject::HollowingData::hollow_mesh_with_holes` and `hollow_mesh_with_holes_trimmed` are declared `mutable` to support lazy population from const accessors. No mutex guards these fields. If the UI thread reads via const accessor while the slicer thread is writing (drill_holes step), a data race and undefined behaviour result. The `mutable` pattern without synchronization is a known SLA-specific anti-pattern in this codebase | SLAPrint.hpp:HollowingData:mutable-no-mutex-race | High | P1 |
| 805 | `SLAPrint::m_printer` is a raw `SLAArchive*` pointer. There is no RAII wrapper (unique_ptr, shared_ptr) managing its lifetime. If the SLAArchive object is destroyed (e.g., printer config change) while `SLAPrint::process()` is executing the rasterize step, the pointer becomes dangling. The rasterize step accesses `m_printer->draw_layers()` without any null/validity check beyond the initial `!m_print->m_printer` guard at step entry | SLAPrint.hpp:SLAPrint:m_printer-raw-pointer-dangling | High | P1 |
| 806 | `SLAPrint::PrintLayer` stores `std::reference_wrapper<const SliceRecord>` for each contributing slice record. These references point into `SLAPrintObject::m_slice_index` vectors. If any SLAPrintObject step is invalidated (which rebuilds m_slice_index) after PrintLayer construction but before rasterize completes, all stored references become dangling. The invalidation-vs-rasterize ordering is guarded by step state but not by explicit reference validity checks | SLAPrint.hpp:PrintLayer:reference_wrapper-dangling-on-invalidation | High | P1 |
| 807 | `SLAPrint::invalidate_state_by_config_options()` handles known config option names via explicit string comparisons and returns early. For any option name not in the known list, the function falls through to `assert(false)` (debug builds only). In release builds, the assert is eliminated — new config options added without updating this function are silently treated as having no effect on print state, potentially serving stale cached results after a config change | SLAPrint.cpp:invalidate_state_by_config_options:assert-false-release-noop | Medium | P2 |
| 808 | `SLAPrint::Steps::drill_holes()` entire function body is wrapped in a `/* ... */` block comment. The function executes as a complete no-op. Drain holes are never drilled into the hollowed mesh in the current build. Additionally, the AABBTreeIndirect traversal call within the commented block was itself commented out with a `//BBS` marker, suggesting incremental disabling. Any refactoring target must restore this step before hollowing+drain-holes is usable end-to-end | SLAPrintSteps.cpp:drill_holes:body-commented-out-dead-code | Critical | P1 |
| 809 | `SLAPrint::Steps::initialize_printer_input()` contains a latent bug in the mx/reserve logic: `if (auto m = o->get_slice_index().size() > mx) mx = m;` — the `auto m` captures the bool result of the comparison `size() > mx`, not the size itself. `mx` is therefore always 0 (false) or 1 (true), never the actual slice count. The subsequent `printer_input.reserve(mx)` call reserves at most 1 entry and provides no meaningful pre-allocation benefit for the loop that follows | SLAPrintSteps.cpp:initialize_printer_input:mx-bool-assignment-bug | Low | P3 |
| 810 | `SLAPrint::Steps::merge_slices_and_eval_stats()` contains a copy-paste error: the `c = std::accumulate(...)` call used to compute the reserve size for `supports_polygons` iterates over `sr.get_slice(soModel)` instead of `sr.get_slice(soSupport)`. The reserve is thus sized for model polygons, not support polygons. For objects with more support slices than model slices, `supports_polygons` will be under-reserved, causing extra heap reallocations. For objects with more model slices, it will be over-reserved | SLAPrintSteps.cpp:merge_slices_and_eval_stats:supports-reserve-uses-model-count | Low | P3 |
| 811 | `adjust_layer_series_to_align_object_height()` computes `gap = abs(layer_series.back() - object_height)` using C-library `abs()` instead of `std::abs()` or `fabs()`. For `coordf_t` (double), `abs()` without `<cmath>` in scope performs an implicit double→int truncation before taking the absolute value, returning an integer. When `layer_series.back()` and `object_height` differ by less than 1.0 the result is always 0, causing alignment to incorrectly report success on the first iteration without actually adjusting any layers | Slicing.cpp:adjust_layer_series_to_align_object_height:abs-integer-truncation | High | P1 |
| 812 | `smooth_height_profile()` always executes exactly 6 Gaussian blur passes. The adaptive termination logic (`has_steep_height_change` lambda + while-loop condition) is permanently commented out with a BBS annotation. Profiles that are already smooth after 1–2 passes are over-processed (wasted CPU, excess smoothing); steep profiles that require more than 6 passes are under-processed. A port should restore the adaptive termination to match the documented intent | Slicing.cpp:smooth_height_profile:fixed-6-pass-no-adaptive-termination | Medium | P2 |
| 813 | `generate_layer_height_texture()` does NOT zero-initialize the output `data` buffer before writing. The `memset(data, 0, ...)` call is inside a commented-out block. Texture cells that are not covered by any layer boundary pair (possible for objects with very coarse layers relative to `ncells`) retain uninitialized memory content. In practice, standard FFF layer sequences cover the full height, but any future caller that passes a partially-filled `layers` vector will see garbage pixels in the texture | Slicing.cpp:generate_layer_height_texture:uninitialized-buffer-cells | Low | P3 |
| 814 | `layer_height_profile_from_ranges()` uses `slicing_params.object_print_z_height()` (compensated Z) to clip `hi` of each input range, but fills the trailing gap using `slicing_params.object_print_z_uncompensated_height()`. When `object_shrinkage_compensation_z != 1.0`, the last segment of the profile extends to the uncompensated height while range clipping is done against the compensated height. This creates an inconsistency between the clipping horizon and the fill endpoint: for positive shrinkage compensation (z > 1.0), the profile tail extends beyond the last-clipped range endpoint by `(compensation_z - 1) * object_height` mm | Slicing.cpp:layer_height_profile_from_ranges:compensated-vs-uncompensated-tail-inconsistency | Medium | P2 |
| 815 | `check_object_layers_fixed()` returns false for any profile with more than 8 entries (`size != 4 && size != 8`). The UI layer height editor may generate profiles with redundant but height-preserving transition points after a SMOOTH or REDUCE operation. Such profiles represent a uniform height but are classified as variable, forcing the more expensive variable-layer code path for every subsequent slice. No profile normalisation step collapses redundant points back to the canonical 4-or-8 entry form | Slicing.cpp:check_object_layers_fixed:brittle-size-check-defeats-fixed-path | Low | P3 |
| 816 | `PrintStateBase::g_last_timestamp` is a `static size_t` shared across ALL `Print` and `SLAPrint` instances. It is incremented with `++g_last_timestamp` inside `set_started()`, `set_done()`, `invalidate()`, `invalidate_multiple()`, and `invalidate_all()` — none of which use atomics or a mutex for this specific field. Concurrent multi-plate slicing (two `Print` objects in separate TBB threads) produces unsynchronized read-modify-write races on every timestamp bump. The upstream FIXME comment in the source explicitly acknowledges this | PrintBase.hpp:PrintStateBase:g_last_timestamp-not-atomic-data-race | High | P1 |
| 817 | `StringObjectException::object` is a raw `ObjectBase const*` that points into the Print's internal `PrintObject` / `ModelObject` list. If `Print::clear()` or `Print::apply()` rebuilds or destroys the object list while the UI layer holds a `StringObjectException` returned from `validate()`, the pointer becomes dangling. There is no ObjectID-based indirection to validate liveness | PrintBase.hpp:StringObjectException:raw-pointer-dangles-after-print-rebuild | High | P1 |
| 818 | `PrintState<>::set_started()` sets `m_step_active = static_cast<int>(step)` before calling `throw_if_canceled()`. If cancellation fires inside `set_started()`, `m_step_active` is left pointing at the new step while the step body never actually executes. The debug asserts that would catch this inconsistency (`assert(m_step_active == -1)`) are explicitly commented out in the source because they produce false positives after cancel. A subsequent `active_step_add_warning()` call from a different thread would assert `m_step_active != -1` against a stale value | PrintBase.hpp:PrintState:set_started:m_step_active-stale-after-cancel | Medium | P2 |
| 819 | `PrintState<>::invalidate_multiple()` sets ALL supplied steps to INVALID and bumps their timestamps in one forward loop, then fires `cancel()` in a separate call. Any observer thread that reads step timestamps in the window between the first state flip and the `cancel()` call will see INVALID states with new timestamps while the worker thread has not yet been stopped. This window is inherent to the "set all, then cancel" design. A reader in this window may incorrectly decide it is safe to re-enter a step that is still executing | PrintBase.hpp:PrintState:invalidate_multiple:invalid-before-stop-window | Medium | P2 |
| 820 | `PrintBase::update_object_placeholders()` iterates all instances of each ModelObject but does NOT break after finding the first printable one — `printable` is reassigned for each printable instance. Only the LAST printable instance's scaling factors appear in the "scale" placeholder vector. For multi-instance objects with non-uniform per-instance scaling, the placeholder reports incorrect values and the generated output filename may be misleading | PrintBase.cpp:update_object_placeholders:last-instance-scale-only | Low | P3 |

<!-- Session 55 (PrintRegion.cpp, Surface.hpp, SurfaceCollection.hpp/.cpp, PrintApply.cpp):
     No new H-IDs assigned. Key refactoring traps documented inline and in agent_journal.md:
     - SurfaceType enum used as array index in SVG export (same pattern as ETags H222)
     - SurfaceCollection::group() returns raw pointers into member vector — push_back invalidates
     - Surface::is_bridge() / is_solid() do NOT cover stInternalAfterExternalBridge / stSecondInternalBridge
     - is_printable_filament_changed() skips geometry test in fmmManual mode
     - transform3d_equal() exact float compare — no epsilon (Phase 4 of Print::apply())
     - normalize_fdm_2() called twice in Print::apply() — intentional double-pass design
     Next available ID: H821 -->

<!-- Session 56 (Config.hpp, Config.cpp): H821–H826 assigned below -->

| 821 | `ConfigBase::get_abs_value()` contains a `throw` statement at line 780 that is permanently unreachable: lines 776–778 already cover all branches with unconditional `return` statements. A translator who sees the throw and tries to replicate the error path will be adding permanently dead logic to the translation | Config.cpp:get_abs_value:dead-throw-after-unconditional-return | Low | P3 |
| 822 | `ConfigBase::null_nullables()` calls `set_deserialize_raw(key, value, ForwardCompatibilitySubstitutionRule::Disable)` but the third parameter is typed `bool append`. `ForwardCompatibilitySubstitutionRule::Disable` has integer value 0, which implicitly converts to `bool false`, meaning nullable options are REPLACED (not appended). A translator who reads the call site and sees an enum value where a bool is expected may invert the semantics, causing all nullable option resets to APPEND instead of replace | Config.cpp:null_nullables:implicit-enum-to-bool-conversion | Medium | P2 |
| 823 | `create_default_option()` in Config.cpp: in the `coEnum` branch, a `ConfigOptionEnumGeneric* dft` is heap-allocated and a clone of it is returned. The `delete dft` statement at the end of the function block is after all `return` paths in every branch of the switch, making it permanently unreachable. `dft` leaks on every call that hits the coEnum branch. Affects all programs that dynamically create ConfigDef entries at runtime (e.g. plugin hosts) | Config.cpp:create_default_option:coEnum-delete-unreachable-leak | Medium | P2 |
| 824 | `ConfigOptionBools::get_at()` non-const overload uses `reinterpret_cast<bool*>(&this->values[idx])` where `values` is `std::vector<unsigned char>`. This is defined behaviour only when `sizeof(bool) == 1` and the platform uses the same bit representation for bool and unsigned char. On any platform where `sizeof(bool) != 1` (permitted by the C++ standard), the cast produces a misaligned or wrongly-typed pointer and any write through it is undefined behaviour | Config.hpp:ConfigOptionBools:get_at:reinterpret_cast-bool-ptr-UB | High | P1 |
| 825 | `ConfigOptionPoints` (and `ConfigOptionPointsGroups`) uses cereal `saveBinary` / `loadBinary` for serialising `std::vector<Vec2d>` (pairs of `double`). Binary serialisation of floating-point data is not portable: it embeds the host endianness and `sizeof(double)`. Projects saved on a little-endian 64-bit host will be silently corrupted when loaded on a big-endian target or a platform with `sizeof(double) != 8` | Config.hpp:ConfigOptionPoints:saveBinary-not-portable | High | P1 |
| 826 | `ConfigOptionVector<T>::resize(n)` fills newly added slots by calling `values.front()` (element 0). The inline comment directly above the call says "Fill in the rest with the last value." This is a comment/code divergence: new slots receive the FIRST element's value, not the LAST. Any downstream code that relies on the documented "last value" semantics (e.g., appending a new extruder and expecting it to inherit the previous last extruder's settings) will instead receive the first extruder's settings | Config.hpp:ConfigOptionVector:resize:front-not-back-comment-divergence | Medium | P2 |
| 827 | `PRESET_PROFILES_TEMOLATE_DIR` macro in `Preset.hpp:37` has a typo ("TEMOLATE" vs "TEMPLATE") that has propagated into filesystem path construction wherever this constant is used. Renaming it requires coordinated changes across all translation units that use the macro and any external tooling or scripts that reference the literal string `"profiles_template"` | Preset.hpp:37:PRESET_PROFILES_TEMOLATE_DIR:typo-in-filesystem-path-constant | Low | P3 |
| 828 | `PresetCollection::get_preset_base()` is recursive with no depth limit or cycle detection. A circular `inherits` chain (Preset A inherits from B which inherits from A) will cause a stack overflow. The system does not validate the inheritance graph on load, so circular chains are possible via hand-edited JSON preset files | Preset.cpp:get_preset_base:unbounded-recursion-no-cycle-detection | High | P1 |
| 829 | `PresetCollection::load_user_preset()` acquires `m_mutex` via manual `lock()` and releases via manual `unlock()`. There is no RAII guard (std::lock_guard / std::unique_lock). If any exception is thrown inside the locked section, the mutex is never released and the entire `PresetCollection` becomes permanently deadlocked for the lifetime of the process | Preset.cpp:load_user_preset:mutex-not-released-on-exception | High | P1 |
| 830 | `PresetCollection::get_selected_preset()` (Preset.hpp:668) contains the guard `if ((m_idx_selected < 0) \|\| (m_idx_selected >= m_presets.size()))`. `m_idx_selected` is `size_t` (unsigned), so the comparison `m_idx_selected < 0` is always false — unsigned values are never negative. The intended safety guard for a "negative sentinel" is a no-op; the only active guard is the `>= m_presets.size()` check | Preset.hpp:668:m_idx_selected-less-than-zero-always-false | Medium | P2 |
| 831 | `PhysicalPrinter::has_print_host_information(const DynamicPrintConfig&)` unconditionally returns `false` regardless of the config contents. Any caller that uses this method to determine whether a physical printer has print-host configuration (e.g., for security filtering or UI display decisions) will always receive "no" — the filtering logic is permanently non-functional | Preset.cpp:3955:PhysicalPrinter-has_print_host_information-always-false | Medium | P2 |
| 832 | `PresetCollection::load_user_preset()` return value semantics are counter-intuitive: the function returns `false` for BOTH "preset was already up to date" AND "preset was updated from cloud/disk data", and only returns `true` for the "newly inserted" case. Callers that check the return value to determine "did the collection change?" will miss the "updated" case and fail to trigger necessary refresh logic | Preset.cpp:load_user_preset:return-value-semantics-inverted | Medium | P2 |
| 833 | `PhysicalPrinterCollection::load_printers_from_presets()` entire function body is wrapped in `#if 0`, making it a permanently disabled no-op. This was the legacy migration path that created PhysicalPrinter entries from old Preset configs containing Print-Host upload fields. Users who had such fields in old preset.ini configs will silently lose them on upgrade with no error or migration message | Preset.cpp:4153:load_printers_from_presets:entirely-if-0-dead-code | Medium | P2 |

<!-- Session 58 (GCodeReader.cpp, GCode/ConflictChecker.hpp/.cpp, GCode/PostProcessor.hpp/.cpp, GCode/SpiralVase.hpp/.cpp, GCode/ToolOrderUtils.hpp/.cpp): H834–H853 assigned below -->
<!-- Session 59 (GCode/PrintExtents.hpp/.cpp, GCode/RetractWhenCrossingPerimeters.hpp/.cpp, GCode/SmallAreaInfillFlowCompensator.hpp/.cpp): H854–H861 assigned below -->

| 834 | `GCodeReader::update_coordinates()`: arc moves (G2/G3) update `m_position` with the endpoint coordinates only. No arc-length integration is performed; the actual path length travelled is never accumulated into the position state. Any downstream consumer that expects `m_position` to represent accumulated distance (e.g., for firmware simulation or flow correction) will receive systematically low values whenever the G-code contains arcs | GCodeReader.cpp:update_coordinates:arc-moves-endpoint-only-no-arc-length | Medium | P2 |
| 835 | `GCodeReader::parse_file_raw_internal()` counts lines by scanning for `'\n'`. Lines terminated by bare `'\r'` (classic Mac line endings, no `\n`) are not counted as line separators. The `line_end` callback receives an incorrect (under-counted) line index for any G-code file saved with `\r`-only line endings, breaking any consumer that relies on the reported line numbers for error messages or seeking | GCodeReader.cpp:parse_file_raw_internal:bare-CR-line-endings-not-counted | Low | P3 |
| 836 | `GCodeLine::has_value(char axis, float& value)` extracts the numeric portion via C-library `strtod()`. Unlike the fast-path `has_value(char)` overload which uses `fast_float::from_chars`, this overload is locale-dependent: on systems with a non-C locale that uses a comma as the decimal separator, `strtod` will fail to parse any value containing a period. This creates a latent locale-sensitivity bug for callers that use the overload returning a float reference | GCodeReader.cpp:GCodeLine-has_value-float-ref:strtod-locale-dependent | Medium | P2 |
| 837 | `GCodeLine::set(char axis, float value, unsigned int decimal_digits)` finds the axis letter by searching for `' ' + axis` (space + letter). If the axis token is the first token on the line (no leading space), the search fails and the value is silently not updated. G-code generators that emit lines without a leading space before the first parameter (valid per NIST spec) will produce stale axis values after a `set()` call | GCodeReader.cpp:GCodeLine-set:first-token-no-leading-space-not-found | Medium | P2 |
| 838 | `GCodeLine::set()` searches for the end of the value field by looking for the next `' '` character. If the axis token is the last token on the line (no trailing space), the search returns `std::string::npos` and the value replacement silently truncates the remainder of the string instead of only replacing the numeric portion. Any G-code line whose last token is rewritten via `set()` will lose all characters after the new value | GCodeReader.cpp:GCodeLine-set:last-token-truncation-on-npos | Medium | P2 |
| 839 | `ConflictChecker::find_inter_of_lines_in_parallel()` launches a TBB `parallel_for` that sets a `bool find` flag from multiple worker threads without atomics or a mutex. Multiple threads may concurrently write `find = true` — while all writes have the same value, the non-atomic access to `find` is a C++ data race and constitutes undefined behaviour under the memory model | GCode/ConflictChecker.cpp:find_inter_of_lines_in_parallel:bool-find-non-atomic-data-race | High | P1 |
| 840 | `LinesBucketQueue::line_rasterization()` calls `assert(0)` on the unreachable `default:` branch but does not `return` or `throw`. In release builds the assert is eliminated, execution falls through with `line` uninitialized, and subsequent code writes the uninitialized `line` into the bucket queue. For pathological inputs with new `ExtrusionRole` values not covered by the switch, this is silent memory corruption. Additionally, rasterizing extremely long or dense paths allocates one `RasterizationLine` per pixel step with no upper bound, enabling OOM | GCode/ConflictChecker.cpp:line_rasterization:assert0-no-return-and-OOM | High | P1 |
| 841 | `getExtrusionPathsFromEntity()` dispatches via `dynamic_cast` for every entity in the conflict-check candidate list. For prints with thousands of extrusion entities, this is O(N) RTTI overhead per entity on every conflict check pass. The function is called inside the parallel loop, so RTTI contention from multiple threads compounds the cost on platforms where typeinfo lookup is lock-protected | GCode/ConflictChecker.cpp:getExtrusionPathsFromEntity:dynamic_cast-per-entity-O(N)-perf | Low | P3 |
| 842 | `ConflictChecker` uses `SUPPORT_THRESHOLD = 100` mm (100 000 scaled units) as the minimum distance to flag a support conflict. This value is approximately 100× larger than typical extrusion widths. For any print where support and object are legitimately within 100 mm of each other (e.g., enclosed supports in dense regions), the threshold will generate false-positive conflicts. Conversely, genuine inter-object conflicts below the threshold are silently suppressed | GCode/ConflictChecker.cpp:SUPPORT_THRESHOLD-100mm-nearly-disables-conflict-detection | Medium | P2 |
| 843 | `gcode_add_line_number()` in PostProcessor.cpp reads the entire G-code file into a single `std::string` via `std::istreambuf_iterator`. For large prints (multi-hour jobs can produce 500 MB–2 GB G-code files), this allocation can exhaust available heap on machines with limited RAM. There is no streaming/chunked alternative; the function silently OOMs and throws `std::bad_alloc`, which propagates as an unhandled exception to the caller | GCode/PostProcessor.cpp:gcode_add_line_number:entire-file-into-RAM-OOM | High | P1 |
| 844 | On Win32, `run_post_process_scripts()` calls `WaitForSingleObject(pi.hProcess, INFINITE)`. If the post-process script hangs indefinitely (infinite loop, waiting for user input, blocked on a pipe), the slicer UI thread blocks forever with no timeout, no progress indication, and no cancellation path. The only recovery is to kill the slicer process | GCode/PostProcessor.cpp:run_post_process_scripts:WaitForSingleObject-INFINITE-no-timeout | High | P1 |
| 845 | On POSIX, `run_post_process_scripts()` launches scripts via `boost::process::shell` which resolves to `$SHELL -c <command>`. If `$SHELL` is set to a non-POSIX shell (fish, rc, es) that does not support the `-c` convention, script execution will fail with an opaque error. No fallback to `/bin/sh` is attempted; the error is surfaced as a boost::process exception with no user-friendly message | GCode/PostProcessor.cpp:run_post_process_scripts:SHELL-env-nonportable-nonfallback | Low | P3 |
| 846 | `run_post_process_scripts()` on Win32 calls `GetEnvironmentStringsW()` then iterates the block to build `env_map`. This depends on the environment block ordering being stable and the `=` sign appearing in each variable exactly once. Malformed environment variables (empty name, multiple `=` signs) will produce incorrect key/value splits. The ordering dependency is currently correct but fragile against future OS or CRT changes | GCode/PostProcessor.cpp:run_post_process_scripts:Win32-env-block-ordering-fragile | Low | P3 |
| 847 | `SpiralVase::m_previous_layer` is allocated with `new GCodeReader()` in `process_layer()` at the end of pass 1, and released with `delete m_previous_layer` at the start of `process_layer()` when the previous layer exists. There is no RAII wrapper. If any exception escapes between the `new` and the next `delete` call (e.g., a `CanceledException` from `throw_if_canceled()`), the `GCodeReader` heap object leaks for the lifetime of the process | GCode/SpiralVase.cpp:m_previous_layer:raw-new-delete-exception-leak | Medium | P2 |
| 848 | `SpiralVase::process_layer()` Pass 1 instantiates a `GCodeReader` by value inside a lambda capture `[reader]` per-layer call. Because `GCodeReader` owns its internal state buffer, this copies the entire reader state O(buffer size) on every layer invocation. For a 1000-layer print this is 1000 GCodeReader copy constructions. The reader state from pass 1 is then discarded; the copy is never reused | GCode/SpiralVase.cpp:process_layer:GCodeReader-copy-per-layer-perf | Low | P3 |
| 849 | `SpiralVase::process_layer()` `transition_out` path emits relative E distances using the delta computed during pass 1. If the input G-code uses absolute E coordinates (`M82` mode), the delta computation `(current_e - last_e)` is correct only for the first layer after a reset. For absolute-E G-code that does not reset E between layers, `last_e` accumulates and the delta-based transition_out ramp will emit incorrect (too large) E values | GCode/SpiralVase.cpp:process_layer:transition_out-absolute-E-incorrect | High | P1 |
| 850 | `ToolOrderUtils` TSP bitmask DP (`get_order_1_3`) uses a `dp[1<<n][n]` table. At `n = 20` extruders, the table has `1 048 576 × 20 = 20 971 520` entries. If each entry is a `float` (4 bytes), this is ~84 MB. If the entry is a `double` (8 bytes), ~168 MB. If the entry is a `std::pair<float, int>` (8 bytes), ~168 MB. The current code allocates this on the heap, but for `n = 20` this can exhaust available RAM on embedded or 32-bit targets and is a significant allocation on desktop targets | GCode/ToolOrderUtils.cpp:get_order_1_3:TSP-DP-bitmask-OOM-n20 | High | P1 |
| 851 | `ToolOrderUtils` brute-force permutation `forcast` function (used in `get_order_1_3` for small N) is gated by a runtime `if (n <= 5)` check. The threshold `5` is a magic constant in the runtime code with no compile-time equivalent. If the threshold is raised in a future refactor without adjusting the permutation generator's complexity guarantees, the worst-case execution time of `get_order_1_3` silently scales to `O(n!)` for inputs above the new threshold | GCode/ToolOrderUtils.cpp:forcast:runtime-gate-n5-magic-constant | Low | P3 |
| 852 | `MCMFSolver::add_edge()` in ToolOrderUtils adds forward + reverse edges with indices `idx` and `idx^1` (XOR). This trick is valid only when `add_edge` is called in strictly paired sequence (each even index is a forward edge, the next odd index is its reverse). Any future refactor that adds a single edge (without its pair), reorders calls, or calls `add_edge` conditionally will silently corrupt the residual graph — the XOR pairing assumption is invisible to the caller | GCode/ToolOrderUtils.cpp:MCMFSolver-add_edge:XOR-pairing-assumption-fragile | Medium | P2 |
| 853 | `MCMFSolver::get_distance()` (or equivalent cost accessor) contains a `return 0` in the `l_nodes[i] == -1` branch followed by unreachable code. The dead code after the return performs a lookup that would be needed for the general case. If the early-return guard is ever removed or the condition inverted during maintenance, the subsequent lookup executes against a stale/uninitialized state. The dead code masks the bug silently | GCode/ToolOrderUtils.cpp:get_distance:dead-code-after-return-masked-bug | Low | P3 |
| 854 | `RetractWhenCrossingPerimeters::travel_inside_object()` uses `bbox_travel_eigen` (unexpanded AABB) for the AABBTree descent predicate but uses `bbox_travel` (SCALED_EPSILON-expanded Polygon bounding box) in the leaf predicate for the final `intersects_with()` call. The two bounding boxes are computed differently and may not match, making it possible for the tree traversal to prune a node whose expanded bbox would have intersected. This asymmetry is a maintenance trap: changing one bbox type without updating the other silently breaks the spatial index guarantee | GCode/RetractWhenCrossingPerimeters.cpp:travel_inside_object:bbox-asymmetry-pruning-mismatch | Medium | P2 |
| 855 | `RetractWhenCrossingPerimeters::travel_inside_object()` calls `diff_pl(travel, clipped)` on the entire travel polyline for each candidate island that passes the AABB test. `diff_pl` runs a full Clipper boolean difference. In the worst case (dense model with many nearby islands), this is O(N_candidates × |travel|) Clipper operations per travel move. For long travel moves across a complex model, this can stall the G-code generation thread noticeably | GCode/RetractWhenCrossingPerimeters.cpp:travel_inside_object:O(N*travel)-clipper-per-candidate | Medium | P2 |
| 856 | `SmallAreaInfillFlowCompensator::max_modified_length()` calls `eLengths.back()` unconditionally. If `eLengths` is empty (e.g., the compensator was constructed but the CSV model contains no knots), this is undefined behaviour — `back()` on an empty vector is UB. The constructor validation checks `eFlows.size() < 2` (minimum 2 knots) but does not check `eLengths`, which is populated in a separate code path. A timing window exists where `eLengths` may be empty when `max_modified_length()` is called | GCode/SmallAreaInfillFlowCompensator.cpp:max_modified_length:back-on-empty-eLengths-UB | High | P1 |
| 857 | `nearly_equal(double a, double b)` is defined at file scope in `SmallAreaInfillFlowCompensator.cpp` without `static` or anonymous-namespace qualification. This injects a generic-named utility function into the `Slic3r` namespace where it is visible to all translation units that include a Slic3r header indirectly pulling in this TU. If another TU defines a different `nearly_equal` with a different tolerance, ODR violations or silent behaviour differences arise | GCode/SmallAreaInfillFlowCompensator.cpp:nearly_equal:file-scope-pollutes-Slic3r-namespace | Low | P3 |
| 858 | `SmallAreaInfillFlowCompensator` CSV parsing in the constructor compiles a `std::regex` for each line of the CSV input (one regex per call to the parsing lambda) and calls `std::stod` for each parsed number. `std::regex` is notoriously slow to compile (linear FSM construction); for a CSV with N lines this is O(N) regex compilations. `std::stod` is locale-dependent and will misparse values if the process locale uses a comma decimal separator | GCode/SmallAreaInfillFlowCompensator.cpp:ctor:regex-per-line-and-stod-locale-dependent | Medium | P2 |
| 859 | The inner `catch(...)` in `SmallAreaInfillFlowCompensator`'s constructor swallows all exceptions thrown by the CSV parsing block, including `std::bad_alloc` and other non-parse exceptions. An OOM during CSV parsing is silently converted into a "no model loaded" state and the compensator proceeds with empty knot vectors, producing zero-flow compensation rather than propagating the allocation failure to the caller | GCode/SmallAreaInfillFlowCompensator.cpp:ctor:catch-all-swallows-bad_alloc | Medium | P2 |
| 860 | If all lines of the CSV input are empty or malformed, the compensator constructor completes with empty `eFlows` and `eLengths` vectors. All four validation checks (`eFlows.size() < 2`, `eFlows.front() != 0.0`, etc.) test for specific wrong values but not for the all-empty case. A zero-knot model passes all guards. The subsequent `interpolate()` call on empty vectors is undefined behaviour | GCode/SmallAreaInfillFlowCompensator.cpp:ctor:zero-knot-model-passes-validation | High | P1 |
| 861 | `SmallAreaInfillFlowCompensator::modify_flow()` filters by extrusion role and explicitly excludes `erInternalInfill` and `erBridgeInfill`. Short internal infill and bridge infill lines are therefore never flow-compensated, even if they are shorter than `max_modified_length()`. This is an undocumented design choice: the class name implies it compensates "small area infill" but the most common internal infill role is silently excluded from compensation | GCode/SmallAreaInfillFlowCompensator.cpp:modify_flow:excludes-erInternalInfill-undocumented | Low | P3 |

<!-- Session 60 (Support/SupportCommon.hpp + SupportCommon.cpp): H862–H866 assigned below -->
<!-- Session 61 (Support/SupportSpotsGenerator.cpp): H867–H874 assigned below -->

| 862 | `generate_interface_layers()` in SupportCommon.cpp allocates new support layers via `layer_storage.allocate()` inside a lambda (`insert_layer`) that is called from within a TBB `parallel_for` body. If `SupportGeneratorLayerStorage::allocate()` is not internally thread-safe (uses a mutex or atomic bump allocator), concurrent calls from TBB worker threads constitute a data race on the storage state. The storage type is not documented as thread-safe | Support/SupportCommon.cpp:generate_interface_layers:layer_storage-allocate-inside-TBB-data-race | High | P1 |
| 863 | `generate_support_toolpaths()` Pass 2 TBB `parallel_for` body reads `support_layers[support_layer_id + 1]->support_islands` to build the ironing-related data. Correctness depends on TBB scheduling Pass 2 iterations in ascending `support_layer_id` order so that `id+1` has already been written. TBB does not guarantee iteration ordering within a `parallel_for`. On schedulers that process chunks out of order, `support_layers[id+1]->support_islands` may be in an uninitialised state when read | Support/SupportCommon.cpp:generate_support_toolpaths:ironing-reads-id+1-in-parallel | High | P1 |
| 864 | `generate_support_toolpaths()` in SupportCommon.cpp has `link_max_length_factor` hardcoded to `0.0`. A commented-out alternative value of `3.0` exists immediately above. The value `0.0` effectively disables link-max-length clipping for all support layers without any configuration knob or comment explaining the intent. Any translator that reads the comment will implement 3.0 behaviour, silently changing support geometry | Support/SupportCommon.cpp:generate_support_toolpaths:link_max_length_factor-hardcoded-0.0 | Medium | P2 |
| 865 | The type name `SupporLayerType` (missing 't') is defined in a support header and used throughout the entire Support subsystem. This typo is embedded in public API symbols. Renaming it to `SupportLayerType` requires coordinated changes across every translation unit that uses it, plus any downstream plugin or tooling that inspects the symbol name at runtime or via reflection | Support/SupportCommon.cpp:SupporLayerType-typo-propagated-through-support-subsystem | Low | P3 |
| 866 | `modulate_extrusion_by_overlapping_layers()` in SupportCommon.cpp reduces the extrusion height (`height_new`) for each segment based on the overlap with the layer below. It does NOT update `mm3_per_mm` to match. After modulation, `mm3_per_mm` still reflects the original cross-sectional area while the actual deposited area is smaller. Any consumer that uses `mm3_per_mm` to compute E-axis advance (volumetric flow) will over-extrude in modulated support segments | Support/SupportCommon.cpp:modulate_extrusion_by_overlapping_layers:height-reduced-but-mm3_per_mm-not-updated | High | P1 |
| 867 | `SupportSpotsGenerator.cpp` has the entire stability analysis algorithm (ObjectPart, ActiveObjectParts, check_stability, full_search, check_extrusion_entity_stability, gather_issues — approximately 1100 lines) permanently disabled by a `/* ... */` block comment. The public header still declares the types produced by this algorithm (SupportPointCause, SupportPoint, PartialObject). Any translator who reads only the live code will miss the full algorithm and believe it is unimplemented. No TODO, FIXME, or issue reference accompanies the block comment | Support/SupportSpotsGenerator.cpp:1100-lines-of-live-algorithm-permanently-block-commented | High | P1 |
| 868 | `ExtrusionLine` default constructor initialises `origin_entity = nullptr`. `is_external_perimeter()` asserts `origin_entity != nullptr`. The dead block comment contains multiple call sites that construct `ExtrusionLine{}` as a fallback sentinel (e.g. `nearest_prev_layer_line` at line ~420) and then pass the object into code that calls `origin_entity->role()`. In the live code these sites are unreachable, but restoring the dead code as-is would trigger null-deref in release builds | Support/SupportSpotsGenerator.cpp:ExtrusionLine:default-ctor-nullptr-origin_entity-deref | High | P1 |
| 869 | `get_flow_width()` default branch silently maps any unrecognised `ExtrusionRole` to `frPerimeter` width. Roles added in future (erWipeTower, erMilling, or custom roles) will receive an incorrect flow width in stability calculations with no compile-time or runtime warning | Support/SupportSpotsGenerator.cpp:get_flow_width:default-silent-perimeter-width-fallback | Low | P3 |
| 870 | `estimate_curled_up_height()` curvature model: `curling_t = sqrt(radius / 100)` where `radius = 1 / curvature`. For very large curvature (tight inner corners), `curling_t` may be large relative to `curling_section`, causing `b > a` in `sqrt(a^2 - b^2)`. The `std::max(0.0f, ...)` clamp hides this, but it means extremely tight convex turns produce zero curl contribution from the tension term — counter-intuitive | Support/SupportSpotsGenerator.cpp:estimate_curled_up_height:curvature-model-b-greater-a-clamped | Low | P3 |
| 871 | `estimate_curled_up_height()` takes `Params params` by VALUE, not by const-reference. `Params` contains ~20+ fields. This function is called in the inner loop of `estimate_malformations()` once per ExtendedPoint per extrusion across every layer — potentially millions of calls per print. Each call copies the entire Params struct. This is significant unnecessary overhead that a translator replicating the signature will preserve | Support/SupportSpotsGenerator.cpp:estimate_curled_up_height:Params-passed-by-value-not-const-ref | Low | P3 |
| 872 | In `estimate_malformations()`, when processing the first layer (`l->lower_layer == nullptr`), `boundary_lines` is empty and the `LinesDistancer<Linef> prev_layer_boundary` contains no lines. The sign-correction call `prev_layer_boundary.distance_from_lines<true>(...)` returns a default/zero value, which causes the sign to be determined incorrectly. First-layer curl estimates may use the wrong distance sign, over- or under-estimating curl | Support/SupportSpotsGenerator.cpp:estimate_malformations:first-layer-null-lower_layer-wrong-sign | Medium | P2 |
| 873 | `estimate_supports_malformations()` converts each support extrusion from `Polyline` to `Polygon` via `Polygon pol(pl.points)` and then `pol.make_counter_clockwise()`. This silently closes an open path by connecting the endpoint back to the startpoint, creating a phantom segment. This phantom segment is included in the curl-height analysis, potentially injecting a spurious `malformed_line` entry from the endpoint back to the startpoint of each support line | Support/SupportSpotsGenerator.cpp:estimate_supports_malformations:polyline-to-polygon-phantom-segment | Medium | P2 |
| 874 | `estimate_supports_malformations()` receives a single `flow_width` scalar used for ALL support fill extrusions, regardless of their role (base support, interface, raft, etc.). `get_flow_width()` is not called per-role. If interface layers or raft layers have a different configured width, curl estimates for those layers will be computed against the wrong flow width, producing incorrect malformed_line thresholds | Support/SupportSpotsGenerator.cpp:estimate_supports_malformations:single-flow-width-ignores-per-role-width | Medium | P2 |

<!-- Session 61 continued (SupportSpotsGenerator.hpp): H875–H878 assigned below -->

| 875 | `SupportSpotsGenerator.hpp` include guard is `SRC_LIBSLIC3R_SUPPORTABLEISSUESSEARCH_HPP_` — references the old module name "SupportableIssuesSearch" which no longer matches the file name. Any automated tooling, include-guard scanners, or refactoring scripts that look up headers by guard name will not find this file under its current name | Support/SupportSpotsGenerator.hpp:1:include-guard-old-module-name-mismatch | Low | P3 |
| 876 | `Params::filament_density` and `Params::material_yield_strength` are declared `const double` but initialised from float literals (`1.25e-3f`, `33.0f * 1e6f`). The `f` suffix causes float-precision rounding before widening to double — values have ~7 significant figures of precision rather than ~15. For extreme-scale stability torque calculations (very large or very small objects) this can introduce compounding rounding errors in the elastic section modulus and torque computations | Support/SupportSpotsGenerator.hpp:Params:float-literal-initialised-const-double-precision-loss | Low | P3 |
| 877 | `Params::get_bed_adhesion_yield_strength()` has a misleading indentation defect. The `double yield_strength = 0.02` declaration appears visually indented as if inside an else-block (after the early-return `if (raft_layers_count > 0)`), but structurally it is in the enclosing function body and executes on the non-raft code path. A translator who reads the indentation literally will generate incorrect control flow, placing the declaration inside an else-branch that doesn't exist in the source | Support/SupportSpotsGenerator.hpp:Params:get_bed_adhesion_yield_strength:misleading-indentation | Medium | P2 |
| 878 | `Params::filament_density` = 1.25e-3 g/mm^3 (approximately PLA density) with a comment "common filaments are very lightweight, so precise number is not that important." For high-density materials (metal-fill, gypsum composite, ceramic-fill) the actual density is 3–8× higher. The stability torque model will systematically underestimate weight-induced torque for these materials. No configuration knob exists to override the constant, and the comment discourages adding one | Support/SupportSpotsGenerator.hpp:Params:filament_density-PLA-only-hardcoded-no-override | Medium | P2 |

<!-- Session 61 continued (Support/SupportParameters.hpp): H879–H881 assigned below -->

| 879 | `SupportParameters` constructor calls `print_config.nozzle_diameter.get_at(object_config.support_interface_filament - 1)`. If `support_interface_filament.value == 0` ("use active extruder"), the subtraction underflows to `SIZE_MAX` (unsigned underflow). `get_at()` silently clamps to index 0, returning the first extruder's nozzle diameter instead of the active extruder's. The caller receives a wrong diameter with no error, making support extrusion widths incorrect when `support_interface_filament` is unset | Support/SupportParameters.hpp:ctor:support_interface_filament-minus-1-unsigned-underflow | High | P1 |
| 880 | Variable `differnt_support_interface_filament` (line ~40) has a typo — missing 'e' ("differnt" vs "different"). The variable is locally scoped and used correctly, but automated text searches, grep-based refactoring, and any future attempt to find this variable by its intended name will not locate it | Support/SupportParameters.hpp:ctor:differnt_support_interface_filament-typo | Low | P3 |
| 881 | `support_layer_height_min` is declared as `coordf_t` (unscaled mm) but initialised to `scaled<coord_t>(0.01)`, which returns a scaled integer value (~10000 in integer coord units). Subsequent code then calls `std::min(this->support_layer_height_min, std::max(0.01, layer->height))` comparing this large integer against small unscaled double values. The result is that `support_layer_height_min` is never updated below its initial ~10000 value and the field effectively serves no filtering purpose. This is a unit-system confusion between scaled and unscaled coordinate spaces | Support/SupportParameters.hpp:ctor:support_layer_height_min-scaled-vs-unscaled-unit-confusion | High | P1 |

<!-- Session 62 (Support/SupportMaterial.cpp — re-read to finalise hazard candidates; Support/SupportLayer.hpp; Support/TreeSupportCommon.hpp): H882–H898 assigned below -->

| 882 | `SupportGridPattern` stores a `const Polygons*` raw non-owning pointer to the caller's polygon buffer (`m_support_polygons`, `m_trimming_polygons`). If the caller destroys or reallocates these polygons before calling `extract_support()`, the pointer dereferences into freed memory. No ownership semantics, no lifetime documentation, and no `std::shared_ptr`/`std::span` alternative. The pattern of heap-allocated polygon vectors followed by raw pointer storage is repeated in three constructors | Support/SupportMaterial.cpp:SupportGridPattern:raw-non-owning-polygon-pointer-dangling | High | P1 |
| 883 | `buildplate_covered()` in SupportMaterial.cpp computes a running prefix-union of object slice polygons in a serial O(N²) loop. A FIXME comment in the source acknowledges this should be parallelised. For tall objects with hundreds of layers and complex slices the serial loop is a bottleneck that holds the single calling thread | Support/SupportMaterial.cpp:buildplate_covered:O(N2)-serial-prefix-union-should-be-parallel | Medium | P2 |
| 884 | `rasterize_polygons()` allocates a `std::vector<uint8_t>` of size `grid_size.x() * grid_size.y()`. There is no upper-bound check on `grid_size`. Extremely large support areas combined with a fine `pixel_size` setting (e.g., very small `support_closing_radius`) can produce `grid_size` values that overflow the vector capacity or request multi-gigabyte allocations, throwing `std::bad_alloc` with no graceful fallback | Support/SupportMaterial.cpp:rasterize_polygons:grid_size-unbounded-potential-OOM | High | P1 |
| 885 | `OverhangCluster::add_overhang()` scans the `clusters` vector linearly to find the first cluster whose `merged_overhangs_dilated` intersects the new overhang polygon. This is O(C) per call and O(N × C) total where N is the number of overhangs and C is the number of clusters. For dense models with many small overhangs on the same layers, both N and C grow large and the quadratic scan dominates detection time | Support/SupportMaterial.cpp:OverhangCluster-add_overhang:O(N*C)-linear-scan-quadratic-total | Medium | P2 |
| 886 | `OverhangCluster` stores raw `ExPolygon*` pointers into `overhangs_per_layers[layer_id]` which point into a `std::vector<ExPolygon>` that is appended during iteration. If the outer vector grows beyond its capacity and reallocates, all stored `ExPolygon*` pointers are dangled. The current code avoids this only by accident of allocation order; a future resize would produce silent UB | Support/SupportMaterial.cpp:OverhangCluster:ExPolygon-raw-pointer-dangling-on-realloc | High | P1 |
| 887 | In `top_contact_layers()` (line ~2453), `support_interface_filament - 1` is performed on an unsigned type when `support_interface_filament` can be 0 — the same unsigned underflow pattern as H879 in SupportParameters.hpp. The underflowed value is passed to `nozzle_diameter.get_at()` which silently clamps it, returning the wrong nozzle diameter and computing an incorrect interface extrusion width | Support/SupportMaterial.cpp:top_contact_layers:support_interface_filament-minus-1-unsigned-underflow | High | P1 |
| 888 | `SupportGridParams::support_closing_radius` is hardcoded to `2.0` regardless of the value of `object_config.support_closing_radius`. The config field is commented out with a FIXME. This means the AGG rasterizer always uses a 2mm closing radius; user configuration of this parameter has no effect | Support/SupportMaterial.cpp:SupportGridParams:support_closing_radius-hardcoded-ignores-config | Medium | P2 |
| 889 | `detect_overhangs()` computes `lower_layer_expolys` filtering using `fw` (flow width) from `layer->m_regions[0]` — the first region only. For multi-region objects where different regions use different nozzle diameters or flow widths, the single `fw` will be incorrect for all regions except the first. Overhangs within non-first regions will be detected against the wrong filter threshold | Support/SupportMaterial.cpp:detect_overhangs:first-region-only-fw-multiregion-incorrect | Medium | P2 |
| 890 | `contours_simplified()` in SupportMaterial.cpp adjusts corner positions by `-pixel_size/2` to compensate for rasterization offset. This correction is geometrically correct only for axis-aligned (rectilinear) polygon edges. If the function were ever called with non-grid-aligned polygon data, the corner correction would silently introduce sub-pixel errors in the re-vectorized output polygons | Support/SupportMaterial.cpp:contours_simplified:corner-offset-assumes-axis-aligned-edges | Low | P3 |
| 891 | `SupportLayer.hpp` defines the enum `SupporLayerType` (note: missing 't', identical to H865's finding in SupportCommon.cpp). This typo is in the canonical definition shared by all support subsystem files. It cannot be corrected locally — every reference in SupportMaterial.cpp, TreeSupport.cpp, TreeSupport3D.cpp, SupportCommon.cpp and their headers must be updated simultaneously or compilation fails | Support/SupportLayer.hpp:15:SupporLayerType-typo-canonical-definition | Low | P3 |
| 892 | `SupportGeneratorLayerStorage::allocate()` acquires `m_mutex` via manual `lock()` / `unlock()` with no RAII guard. If any exception is thrown between lock and unlock (e.g., `std::bad_alloc` from `m_storage.emplace_back()`), the mutex is never released and all subsequent calls to `allocate()` or `allocate_unguarded()` on the same storage object deadlock permanently | Support/SupportLayer.hpp:SupportGeneratorLayerStorage:allocate-no-raii-mutex-guard | High | P1 |
| 893 | `SupportGeneratorLayer::operator==` compares only `print_z`, `height`, and `bridging` — it ignores `layer_type`, `bottom_z`, `idx_object_layer_above`, `idx_object_layer_below`, and all polygon fields. Two layers with completely different polygons or type classifications are considered equal if their Z, height, and bridging flag match. Any container or algorithm that deduplicates based on this operator will silently discard layers with different geometry | Support/SupportLayer.hpp:SupportGeneratorLayer:operator==-partial-equality | Medium | P2 |
| 894 | `TreeSupportSettings::soluble` is declared `inline static bool = false`. Being a non-const static member, it is shared across all `TreeSupportSettings` instances in the entire process. In a multi-object print where some objects use soluble support and others do not, the last writer wins: whichever object's `TreeSupportSettings` constructor runs last sets the global flag for all objects. Objects processed before the soluble-object constructor will have had their avoidance zones computed with the wrong `soluble` value | Support/TreeSupportCommon.hpp:358:TreeSupportSettings-soluble-inline-static-shared-global-state | High | P1 |
| 895 | `TreeSupportSettings::operator==` compares 30+ fields but does NOT compare `settings` (the embedded `TreeSupportMeshGroupSettings`) beyond the fields already mirrored into `TreeSupportSettings` itself. The comment "The infill class now wants the settings object and reads a lot of settings" acknowledges that infill generation depends on fields in `settings`, but these are excluded from equality. Two `TreeSupportSettings` objects with different `settings` fields that happen to agree on all mirrored fields will be considered equal, causing incorrect cache hits in `TreeModelVolumes` | Support/TreeSupportCommon.hpp:TreeSupportSettings:operator==-settings-not-compared | Medium | P2 |
| 896 | `TreeSupportMeshGroupSettings::support_bottom_height` is computed as `config.support_interface_bottom_layers.value * layer_height` when `support_interface_bottom_layers > 0`, but falls back to `config.support_interface_top_layers.value * layer_height` when `support_interface_bottom_layers < 0`. The negative-value fallback is a non-obvious convention ("negative means inherit from top") that is not documented or validated. A negative value of `-100` would silently compute a bottom interface height of 100 × layer_height | Support/TreeSupportCommon.hpp:TreeSupportMeshGroupSettings:support_bottom_height-negative-fallback | Low | P3 |
| 897 | `tree_supports_show_error()` is defined as a non-static inline function at namespace scope in `TreeSupportCommon.hpp`. It unconditionally calls `printf()` for every call site. In production release builds with no debug console, `printf` output is silently discarded. On Windows, the `MessageBoxA` branch is compiled only when `TREE_SUPPORT_SHOW_ERRORS_WIN32` is defined (it is not in normal builds), so the function provides no user-visible feedback in release. The comment `// todo Remove! ONLY FOR PUBLIC BETA!!` indicates this was intended as temporary scaffolding that was never removed | Support/TreeSupportCommon.hpp:tree_supports_show_error:printf-only-no-release-feedback-permanent-beta-code | Low | P3 |
| 898 | `getActualZ()` and `setActualZ()` in `TreeSupportSettings` are entirely wrapped in `#if 0`, making them permanently disabled dead code. `getActualZ()` contains a subtle bug even within the disabled block: the ternary `... + known_z.size() ? known_z.back() : 0` has incorrect operator precedence — `+` binds tighter than `?:`, so the `? known_z.back() : 0` is the ternary condition, not the consequent. The disabled code would be UB if ever re-enabled as written | Support/TreeSupportCommon.hpp:getActualZ:if-0-dead-code-with-operator-precedence-bug | Low | P3 |

<!-- Session 63 (PrintConfig.cpp — lower portion, multi-filament helpers, validate, CLI defs): H899–H906 assigned below -->

| 899 | `compute_filament_override_value()` ends with `delete opt_copy` — raw deletion of a cloned `ConfigOption*`. If any exception is thrown between the clone at the start of the function and this `delete`, `opt_copy` leaks. If a future early-return path is added, the same leak occurs. Should use `std::unique_ptr<ConfigOption>` to ensure destruction on all exit paths | PrintConfig.cpp:compute_filament_override_value:raw-delete-opt_copy-exception-leak | Medium | P2 |
| 900 | `normalize_fdm_2()` assigns `opt_long_retraction_default` (a stack local) to `opt_new_filament` via pointer reassignment on the `retraction_distances_when_cut` branch, while `opt_retraction_distance_default` is assigned on the adjacent branch. The two branches do not interact but the pointer aliasing between the two locals and the shared `opt_new_filament` pointer makes the control flow brittle — a future merge of the branches would silently swap which local is referenced | PrintConfig.cpp:normalize_fdm_2:pointer-alias-two-locals-brittle | Low | P3 |
| 901 | `validate(FullPrintConfig&, bool)` validates `bridge_flow` twice under the comment "bridge-flow-ratio". The second check correctly records the error under key `"internal_bridge_flow"`, but compares `bridge_flow` (not `internal_bridge_flow`) against the limits. This is a copy-paste error: the actual `internal_bridge_flow` option value is never validated, while `bridge_flow` is validated twice | PrintConfig.cpp:validate:bridge_flow-checked-twice-internal_bridge_flow-never-checked | Medium | P2 |
| 902 | `validate()` computes the `widths[]` array element count via `sizeof(widths)/sizeof(widths[i])`. The expression uses `widths[i]` (a `const char*` element) rather than `widths[0]`. On all current 64-bit platforms pointer size is uniform so the result is correct, but the idiom is non-portable and style-inconsistent; correct form is `sizeof(widths)/sizeof(widths[0])` | PrintConfig.cpp:validate:sizeof-widths-uses-widths-i-not-widths-0 | Low | P3 |
| 903 | `validate()`'s out-of-range loop handles `coFloat`, `coFloats`, `coInt`, `coInts`, `coPercent`, and `coPercents` but has no `case coFloatOrPercent:` or `case coFloatsOrPercents:`. Options of these types that carry a percentage value exceeding their `max` are silently accepted without error | PrintConfig.cpp:validate:coFloatOrPercent-vectors-not-range-checked | Low | P3 |
| 904 | `PRINT_CONFIG_CACHE_INITIALIZE` macro injects `static volatile int ret = 1;` into the function body. The comment reads "important this function doesn't get optimized out". `volatile` prevents reordering of individual accesses to `ret` but does NOT guarantee the compiler won't inline or discard the function if it determines the side-effect is otherwise unreachable. The correct approach for defeating dead-code elimination is `__attribute__((used))` or a non-const external linkage function | PrintConfig.cpp:PRINT_CONFIG_CACHE_INITIALIZE:volatile-ret-insufficient-dce-guard | Low | P3 |
| 905 | Several removed CLI actions (`export_obj`, `export_amf`, `export_gcode`, `gcodeviewer`, etc.) are retained inside `/* ... */` block comments in `CLIActionsConfigDef`. If a developer re-enables one without also updating the CLI dispatch switch in `CLI.cpp`, the option will appear in `--help` output but will silently do nothing when invoked — the dispatch switch has no corresponding arm | PrintConfig.cpp:CLIActionsConfigDef:commented-out-options-dispatch-gap | Low | P3 |
| 906 | `CLITransformConfigDef` registers `--rotate` (Z-axis rotation) alongside `--rotate_x` and `--rotate_y`. Neither the option descriptions nor any inline documentation specify the rotation order (e.g., XYZ Euler intrinsic vs. per-axis extrinsic). The GUI rotation representation uses a different convention; identical numeric inputs from CLI vs. GUI can produce different physical orientations for combined rotations | PrintConfig.cpp:CLITransformConfigDef:rotate-order-undocumented-gui-mismatch | Low | P3 |

<!-- Session 66 (SLA/SupportPoint.hpp, SLA/JobController.hpp, SLA/Concurrency.hpp, SLA/BoostAdapter.hpp, SLA/SpatIndex.hpp): H907–H911 assigned below -->

| 907 | `SupportPoint::operator==` compares `pos` with `Vec3f ==` — exact bit-equality on float. Two `SupportPoint`s with geometrically identical positions constructed via different floating-point arithmetic paths (e.g., one from a loaded file, one computed fresh) will compare not-equal. Any deduplication logic, cache lookup, or set membership test based on this operator will produce false negatives, allowing duplicate points to accumulate | SLA/SupportPoint.hpp:SupportPoint:operator==-float-bit-equality-pos | Medium | P2 |
| 908 | `JobController` exposes two overlapping cancellation mechanisms: `stopcondition` (flag-poll — algorithm winds down gracefully) and `cancelfn` (throw — hard abort for callers like `TriangleMeshSlicer` that cannot check a flag). No documented contract specifies which to call in which context. New algorithm authors must read existing callers to infer the distinction; incorrect usage (calling `stopcondition` where `cancelfn` is needed, or vice versa) results in either deadlock-like hangs (algorithm never terminates) or unexpected exception propagation | SLA/JobController.hpp:JobController:stopcondition-vs-cancelfn-undocumented-contract | Medium | P2 |
| 909 | `_ccr<true>::max_concurreny()` (note: "concurreny" missing 'c') is a public API with a spelling typo. All call sites use the misspelled name. If a port or refactor introduces a correctly-spelled `max_concurrency()` override without renaming all existing callers, the two symbols coexist and callers silently call the old misspelled version. The deprecated `Concurrency.hpp` is the only home of this symbol — migrating to the `Execution/` API removes it entirely, but all SLA call sites must be updated | SLA/Concurrency.hpp:_ccr:max_concurreny-typo-missing-c | Low | P3 |
| 910 | `BoostAdapter.hpp` places a `range_value<std::vector<Slic3r::Vec2d>>` template specialization in the `boost::` root namespace rather than `boost::geometry::traits::`. This uses a Boost-internal extension point that is not part of the public Boost.Geometry API contract. A Boost upgrade that moves or removes `range_value` from the `boost::` namespace will silently break this specialization with no compile error at the specialization site — only obscure template resolution failures at call sites. Additionally, the specialization only matches `std::vector<Vec2d>` with the default allocator; custom-allocator vectors do not match | SLA/BoostAdapter.hpp:range_value-in-boost-root-namespace-not-traits | Low | P3 |
| 911 | `BoxIndex::query()` pre-allocates its return vector via `ret.reserve(m_impl->m_store.size())` — reserving capacity equal to the total number of elements in the entire tree, regardless of how many results the query will actually return. For a large spatial index (e.g., thousands of layer bounding boxes), every single query allocates O(N) memory even when only 1–3 results are expected. This is measurable overhead in the SLA rasterization loop where `BoxIndex::query` is called per layer | SLA/SpatIndex.hpp:BoxIndex:query-reserve-O(N)-per-query | Medium | P2 |

<!-- Session 67 (SLA/SpatIndex.cpp, SLA/ConcaveHull.hpp/.cpp, SLA/Clustering.hpp/.cpp, SLA/RasterBase.hpp/.cpp, SLA/AGGRaster.hpp, SLA/RasterToPolygons.hpp/.cpp): H912–H919 assigned below -->

| 912 | `ConcaveHull::add_connector_rectangles()` uses `return` (not `continue`) when `dist >= max_dist` for an island. If the **first** evaluated island has no sufficiently close neighbour, the function exits entirely — all subsequent islands also receive no bridge connector, producing a disconnected pad boundary. The fix is `continue` to skip only that island | SLA/ConcaveHull.cpp:add_connector_rectangles:return-instead-of-continue | Medium | P2 |
| 913 | `distance_queryfn` in `Clustering.cpp` uses `it = tmp.erase(it)` while iterating a `std::vector` — correct but brittle. Any refactor that adds `++it` before the erase, or switches the container to a `std::list`-like type without careful review, causes dangling iterator UB. Separately, `cluster_centroid()` is O(n²) over cluster members (distance recomputation per element) — intended only for tiny clusters (≤3 elements); larger clusters passed in from a future call site will regress silently | SLA/Clustering.cpp:distance_queryfn:erase-while-iterate-brittle | Low | P3 |
| 914 | `Trafo::get_mirror()` uses the **literal integer** `roPortrait` (==1, always truthy) as its orientation condition instead of checking the `flipXY` field. The function therefore always evaluates as `!mirror_x` regardless of portrait vs landscape orientation. The correct expression is `flipXY ? !mirror_x : mirror_x` | SLA/RasterBase.hpp:Trafo:get_mirror-roPortrait-literal-bug | Medium | P2 |
| 915 | `Trafo` ctor unconditionally inverts `mirror_y` (`mirror_y = !mirror[1]`). The semantic of the internal `mirror_y` field is **opposite** to the input `TMirroring[1]` value. Callers that read `trafo.mirror_y` directly get the wrong polarity. Only a brief inline comment documents this inversion; the absence of a named accessor makes the trap invisible during refactoring | SLA/RasterBase.hpp:Trafo:ctor-inverts-mirror_y-semantic-inversion | Medium | P2 |
| 916 | `RasterBase.cpp` wraps its entire body in `#ifndef SLARASTER_CPP` … `#endif`. This non-standard pattern allows `#include "RasterBase.cpp"` from another translation unit (which then defines `SLARASTER_CPP` first to suppress the duplicate definitions). Removing the guard without also removing the `#include` causes duplicate-symbol linker errors that are difficult to diagnose. No such pattern should exist in a port | SLA/RasterBase.cpp:include-guard-on-cpp-file-non-standard | Low | P3 |
| 917 | `AGGRaster` ctor skips the `m_pxdim_scaled` scaling-factor division when `pd.w_mm == 0` or `pd.h_mm == 0`, leaving `m_pxdim_scaled` at the raw `SCALING_FACTOR` value. Any raster created with a zero pixel dimension will produce a blank output silently in release builds (the condition is only `assert`-checked in debug). Callers that pass a zero dimension from an uninitialised `PixelDim` struct will observe missing layer images with no error | SLA/AGGRaster.hpp:AGGRaster:ctor-zero-pxdim-silent-blank-raster | Medium | P2 |
| 918 | `AGGRaster::_to_path()` explicitly appends `v.front()` as the final vertex to close the polygon. AGG automatically closes paths when `close_polygon()` is called (which the caller does); this produces a degenerate zero-length duplicate closing edge in the AGG path buffer for every polygon, potentially affecting anti-aliasing and stroke rendering | SLA/AGGRaster.hpp:_to_path:explicit-closure-creates-duplicate-edge | Low | P3 |
| 919 | `raster_to_polygons()` computes `width = scaled(cols * pxd.h_mm)` and `height = scaled(rows * pxd.w_mm)` — the `h_mm` and `w_mm` fields are swapped relative to `cols`/`rows`. For square pixels the result is numerically identical; for non-square pixels (or landscape-mode rasters where `h_mm ≠ w_mm`) the reconstructed polygon coordinates are scaled incorrectly in X vs Y | SLA/RasterToPolygons.cpp:raster_to_polygons:width-height-pxd-h_mm-w_mm-swapped | Medium | P2 |

<!-- Session 68 (SLA/ReprojectPointsOnMesh.hpp, SLA/IndexedMesh.hpp/.cpp, SLA/Hollowing.hpp/.cpp, SLA/Pad.hpp/.cpp, SLA/Rotfinder.hpp/.cpp, SLA/bicubic.h, SLA/SupportTreeMesher.hpp/.cpp, SLA/SupportTree.hpp/.cpp, SLA/SupportTreeBuilder.hpp/.cpp, SLA/SupportTreeBuildsteps.hpp/.cpp, SLA/SupportPointGenerator.hpp/.cpp): H920–H946 assigned below -->

<!-- Session 69 (Execution/Execution.hpp, Execution/ExecutionSeq.hpp, Execution/ExecutionTBB.hpp, Optimize/Optimizer.hpp, Optimize/NLoptOptimizer.hpp, Optimize/BruteforceOptimizer.hpp, CSGMesh/CSGMesh.hpp, CSGMesh/CSGMeshCopy.hpp, CSGMesh/ModelToCSGMesh.hpp, CSGMesh/PerformCSGMeshBooleans.hpp, CSGMesh/SliceCSGMesh.hpp, CSGMesh/TriangleMeshAdapter.hpp, CSGMesh/VoxelizeCSGMesh.hpp): H947–H981 assigned below -->

| 920 | `reproject_points_and_holes()` in `ReprojectPointsOnMesh.hpp` performs `object->sla_support_points` access before the null-check guard: `if (object == nullptr) return;` appears several lines after the first field dereference. If `object` is null the function has already undefined-behaviour'd before reaching the guard. The fix is to move the null-check to the very first line of the function | SLA/ReprojectPointsOnMesh.hpp:reproject_points_and_holes:null-check-after-dereference-UB | High | P1 |
| 921 | `IndexedMesh` holds `m_tm` as a raw `const TriangleMesh*` non-owning pointer. The constructor documentation states the mesh must outlive the `IndexedMesh` instance. No lifetime token (shared_ptr, span, or borrow reference) enforces this. Any refactor that moves a `TriangleMesh` into a container that reallocates (e.g., `std::vector<TriangleMesh>` with push_back), or that destroys the source mesh after constructing an `IndexedMesh` from it, produces a dangling pointer with no compile-time error | SLA/IndexedMesh.hpp:IndexedMesh:m_tm-non-owning-raw-pointer-dangling | High | P1 |
| 922 | The macro `SLIC3R_HOLE_RAYCASTER` is `#undef`d unconditionally at the top of `IndexedMesh.hpp`. All `#ifdef SLIC3R_HOLE_RAYCASTER` blocks in `IndexedMesh.hpp` and `IndexedMesh.cpp` (the entire hole-aware ray-casting subsystem, including `DrainHole`-aware intersection logic) are permanently dead code. This subsystem cannot be activated without modifying the header; the dead code will never receive maintenance and will bit-rot | SLA/IndexedMesh.hpp:SLIC3R_HOLE_RAYCASTER-unconditionally-undef-dead-code | Medium | P2 |
| 923 | `IndexedMesh::query_ray_hits()` previously deduplicated hits by removing consecutive duplicates (within tolerance) and re-sorting. The deduplication block is commented out with the attribution `// BBS STUDIO-2591`. Callers that assumed deduplicated output may now receive duplicate intersection records, leading to incorrect inside/outside parity decisions in the marching-ray algorithm used by `is_point_inside_mesh()` | SLA/IndexedMesh.cpp:query_ray_hits:deduplication-commented-out-callers-may-receive-duplicates | High | P1 |
| 924 | `IndexedMesh::normals()` performs a brute-force O(N) scan over all triangle vertices to find neighbours for edge and vertex hit types. For each ray-triangle intersection that lands on an edge or vertex, the function rescans the entire mesh. A mesh with M triangles and N ray hits produces O(N×M) total work with no adjacency structure. SLA orientation optimisation fires this function thousands of times per candidate rotation | SLA/IndexedMesh.cpp:normals:O(N*M)-brute-force-no-adjacency | Medium | P2 |
| 925 | `Interior::accessor` is declared `mutable` and the `get_distance_raw()` accessor that calls `accessor.getValue()` is explicitly documented "Not a thread safe call!" in the source. The SLA hollowing pipeline invokes distance queries during multi-threaded slicing without synchronisation. Concurrent calls to `get_distance_raw()` via different threads on the same `Interior` object constitute a data race under the C++ memory model, producing undefined behaviour | SLA/Hollowing.cpp:Interior:accessor-mutable-not-thread-safe-data-race | High | P1 |
| 926 | `DrainHole::get_intersections()` contains the loop `for (size_t i=1; i<=1; --i)`. With `size_t` (unsigned), when `i` reaches 0 and is decremented, it wraps to `SIZE_MAX` (e.g., 18446744073709551615), making `i <= 1` false and exiting the loop. The loop therefore correctly iterates `i = 1, 0` — two iterations — but only by coincidence of unsigned wrap-around arithmetic. A reviewer reading this as a signed loop would expect an infinite loop; changing `size_t` to `int` would produce one | SLA/Hollowing.hpp:DrainHole:get_intersections-for-loop-unsigned-wrap | Low | P3 |
| 927 | `hollow_mesh(mesh, cfg, flags)` single-parameter-pack overload constructs a `JobController{}` internally with no cancellation hooks. Any long-running hollowing operation called via this overload cannot be interrupted. The overload is intended as a convenience wrapper but silently discards the caller's ability to cancel — a trap for any integration that assumes hollowing is cancellable | SLA/Hollowing.hpp:hollow_mesh-single-arg-overload-silent-no-cancel | Medium | P2 |
| 928 | `RotOptimizeParams` has `statucb()` as a setter (missing 's' — "statu**cb**" instead of "status**cb**") and `statuscb()` as a getter. The asymmetric names mean call sites that chain `params.statucb(fn).statuscb()` will compile silently but refer to two different operations. A port must reproduce or canonicalise this asymmetry; autocomplete-based refactoring that normalises all to `statuscb` will break the setter call sites | SLA/Rotfinder.hpp:RotOptimizeParams:statucb-setter-typo-vs-statuscb-getter | Low | P3 |
| 929 | `get_misalginment_score()` in `Rotfinder.cpp` has a typo in the function name ("misalginment" vs "misalignment"). The function is file-scope (not exported), so the typo is contained, but any documentation, grep search, or port that looks for "misalignment" will not locate it. A port that corrects the spelling introduces a name mismatch with the original if any external tooling references the mangled symbol | SLA/Rotfinder.cpp:get_misalginment_score-typo-in-function-name | Low | P3 |
| 930 | `get_merge_distance()` in `Pad.cpp` multiplies an input distance by a magic factor `1.8` with no inline justification, comment, or named constant. The factor appears to be an empirical tuning value for pad merging geometry. During a port, a translator has no way to determine whether `1.8` is a physical constant, a heuristic, or a legacy artefact — and whether it should be configurable | SLA/Pad.cpp:get_merge_distance:magic-factor-1.8-no-justification | Low | P3 |
| 931 | In debug builds (`#ifndef NDEBUG`), `create_pad_geometry()` or its callees unconditionally write `pad_skeleton.svg` to the current working directory at every invocation. In a multi-threaded batch conversion or server-side slicing environment running debug builds, this produces filesystem writes per-job with no path configuration. High invocation rates fill disk; the file is always overwritten (no timestamp), so concurrent jobs corrupt each other's debug output | SLA/Pad.cpp:pad_skeleton-svg-unconditional-debug-write | Low | P3 |
| 932 | `SupportTreeBuilder::add_anchor()` sets `id = long(m_junctions.size() - 1)` instead of `long(m_anchors.size() - 1)`. Because `m_junctions` and `m_anchors` grow independently, the anchor receives the wrong ID in any tree where the two collections differ in size. All subsequent operations that look up an anchor by ID (e.g., collision detection, mesh generation) will address the wrong element, producing corrupt support structures | SLA/SupportTreeBuilder.hpp:add_anchor:id-assigned-from-m_junctions-not-m_anchors | High | P1 |
| 933 | `SupportTreeBuilder` copy and move constructors initialise only `m_heads`, `m_head_indices`, `m_pillars`, `m_bridges`, and `m_compact_bridges`. They omit `m_junctions`, `m_diffbridges`, `m_pedestals`, and `m_anchors`. A copied or moved `SupportTreeBuilder` silently loses all junction, diffuse-bridge, pedestal, and anchor data. Any code that copies a builder (e.g., for multi-plate processing or undo/redo snapshots) will produce an incomplete tree that generates malformed support meshes | SLA/SupportTreeBuilder.hpp:copy-move-ctor-omit-junctions-diffbridges-pedestals-anchors | High | P1 |
| 934 | `SupportTreeBuilder::merge_and_cleanup()` calls `clear()` on `m_heads`, `m_pillars`, `m_bridges`, `m_compact_bridges`, and `m_head_indices`, but does NOT clear `m_crossbridges`, `m_diffbridges`, `m_pedestals`, or `m_anchors`. After cleanup these four collections retain stale data. Any subsequent build step that appends to them will produce mixed old+new entries; any mesh-generation pass that iterates them will process stale geometry | SLA/SupportTreeBuilder.cpp:merge_and_cleanup:four-collections-not-cleared | Medium | P2 |
| 935 | `SupportTree::slice()` computes `cap` as `tail.size()` (the tail slice range) before the `std::copy` that populates the head portion of `padgrid`. The subsequent `padgrid.reserve(cap)` therefore reserves for the tail size, not the head size. The `std::copy` into the head may trigger unintended reallocation, negating the reserve optimisation. No correctness hazard but a performance trap that is invisible at the call site | SLA/SupportTree.cpp:slice:cap-reserves-tail-size-but-copy-fills-head | Low | P3 |
| 936 | `SupportTreeBuildsteps::PointRing` constructor computes the ring basis via `a(Z) = -(n(Y)*a(Y)) / n(Z)`. When the direction vector `n` is nearly horizontal (`n(Z) ≈ 0`), this division produces a near-infinite value, making `a` (and all ring points derived from it) numerically unusable. The degenerate case is not detected or clamped, so any near-horizontal support pillar direction will produce a corrupt ring of attachment points | SLA/SupportTreeBuildsteps.hpp:PointRing:ctor-division-by-near-zero-nZ | Medium | P2 |
| 937 | `pairhash()` in `SupportTreeBuildsteps.hpp` asserts that both input `size_t` values fit in half the bit width (32 bits on a 64-bit platform), then combines them via bit-shift. In release builds the `assert` is compiled out. Inputs ≥ 2^32 silently collide, producing hash map false-matches between different pairs. Large meshes with millions of support points can exceed this threshold | SLA/SupportTreeBuildsteps.hpp:pairhash:assert-only-collision-in-release | Medium | P2 |
| 938 | `SupportTreeBuildsteps::add_pinheads()` is a **completely empty stub** — the function body contains no code. All logic for generating pin-head attachment points (the critical first step of SLA support tree construction) is absent. Any SLA print that exercises this code path via the `SupportTreeBuildsteps` pipeline produces supports with no pin heads, resulting in prints that detach from supports or fail entirely | SLA/SupportTreeBuildsteps.cpp:add_pinheads:completely-empty-stub-no-code | Critical | P0 |
| 939 | `SupportTreeMesher::get_mesh(Head)` constructs a rotation quaternion via `Quaternion::FromTwoVectors({0,0,-1}, h.dir)`. When `h.dir == {0,0,1}` (antiparallel to the reference vector), `FromTwoVectors` is mathematically undefined — the rotation axis is the cross product of two antiparallel vectors, which is the zero vector. Eigen's implementation handles this as a special case but the result is implementation-defined and not guaranteed stable across Eigen versions | SLA/SupportTreeMesher.hpp:get_mesh_Head:FromTwoVectors-antiparallel-undefined | Medium | P2 |
| 940 | `sum_score<int_fast64_t>` in `Rotfinder.cpp` accumulates integer-scaled scores from all triangles. The scaling factor is large (position score alone is `10000 * area_ratio * cos_angle`). For meshes with tens of thousands of triangles the accumulated sum can exceed `INT64_MAX`, silently wrapping to a negative or zero score. Rotations of complex meshes that should score high may sort incorrectly, choosing a sub-optimal orientation | SLA/Rotfinder.cpp:sum_score:int_fast64_t-overflow-large-meshes | Medium | P2 |
| 941 | `RotfinderBoilerplate::get_mesh_to_rotate()` retrieves only `instances[0]` from the model object. For multi-instance objects (e.g., copy-array of the same part), all instances beyond the first are ignored during rotation optimisation. The optimal orientation is computed for only the first instance's transformation even if other instances have different rotations applied | SLA/Rotfinder.cpp:get_mesh_to_rotate:instances-0-only-ignores-multi-instance | Medium | P2 |
| 942 | `bicubic.h` line ~152 defines a lambda `[&F,w](x)` — the parameter `x` has no type annotation. This is not valid standard C++ (lambda parameters must be explicitly typed unless `auto` is used). The code compiles only under MSVC extension mode or older GCC/Clang permissive modes. Strict `-std=c++17` compilation rejects it. A port targeting standard-conformant compilers must add `int x` or `auto x` | SLA/bicubic.h:lambda-missing-parameter-type-non-standard | High | P1 |
| 943 | `bicubic.h` line ~176 defines `auto f = [&f, ...]` — a self-referential lambda that captures itself by reference before the lambda object is fully constructed. This is undefined behaviour in standard C++: `f` does not yet exist when the capture list is evaluated. The code appears to work on GCC/Clang because the capture list is only accessed when `f` is called (not during construction), but this is an implementation-specific accident and not guaranteed | SLA/bicubic.h:self-referential-lambda-capture-undefined-behaviour | High | P1 |
| 944 | `SupportPointGenerator::make_layers()` hardcodes `pixel_area = pow(0.047f, 2.f)` (approximately the area of a 47-micron pixel) regardless of the actual configured layer height or pixel size. A FIXME comment in the source acknowledges this should be derived from the actual printer resolution. Every support point density calculation uses this hardcoded value; high-resolution or low-resolution printers receive the same (wrong) pixel area | SLA/SupportPointGenerator.cpp:make_layers:pixel_area-hardcoded-47-micron-FIXME | Medium | P2 |
| 945 | Island overlap linking in `SupportPointGenerator::make_layers()` uses an O(N²) comparison between all islands in adjacent layer pairs. A FIXME comment in the source acknowledges the quadratic complexity. For models with many small islands per layer (e.g., fine lattice structures, dense text geometry) N grows large and the O(N²) comparison becomes the dominant cost of the SLA support-point generation phase | SLA/SupportPointGenerator.cpp:make_layers:O(N2)-island-overlap-linking-FIXME | Medium | P2 |
| 946 | `sample_expolygon_boundary()` declares an `rng` parameter (random number generator) that is never used in the function body — the parameter appears only as a commented-out `/*rng*/` reference inside the function. The function signature accepts and silently discards any RNG passed in. Callers that pass a seeded RNG expecting deterministic sampling will receive non-deterministic output from the internal RNG used instead | SLA/SupportPointGenerator.hpp:sample_expolygon_boundary:rng-parameter-unused | Low | P3 |
| 947 | `Traits<EP>` primary template in `Execution/Execution.hpp` is an empty struct with no `static_assert`. A new execution policy that forgets to specialize `Traits` silently compiles with no-op for every execution primitive, producing sequential behaviour with no error or warning | Execution/Execution.hpp:Traits:empty-primary-template-no-static-assert | Medium | P2 |
| 948 | Default granularity=1 in `for_each` and `reduce` in `Execution/Execution.hpp` causes very fine-grained TBB task creation (one task per element). For large element counts this incurs high scheduler overhead; the intended default for TBB should be `auto`/`proportional` or a machine-tuned grain size | Execution/Execution.hpp:for_each-reduce:default-granularity-1-TBB-overhead | Low | P3 |
| 949 | The `mergefn` passed to `execution::reduce` must be associative and commutative for correct parallel results (TBB reduces in arbitrary sub-range order). No static assertion or documentation enforces this. A non-associative merge function (e.g., string concatenation with a separator, order-dependent averaging) compiles and silently produces wrong results | Execution/Execution.hpp:reduce:mergefn-must-be-associative-no-check | High | P1 |
| 950 | `ExecutionSeq` defines both `SpinningMutex` and `BlockingMutex` as the same no-op `_Mtx` type. Switching a call site from `ExecutionSeq` to `ExecutionTBB` policy without auditing mutex usage implicitly changes the no-op mutex to a real `tbb::spin_mutex` or `tbb::mutex` — but only if the consumer re-queries the mutex type. Call sites that capture the mutex type at template instantiation time and hold it beyond a policy switch lose protection silently | Execution/ExecutionSeq.hpp:SpinningMutex-BlockingMutex-both-noop-swap-hazard | Medium | P2 |
| 951 | `ExecutionTBB::SpinningMutex` is `tbb::spin_mutex` — a busy-wait lock. Using it for critical sections that include I/O, memory allocation, or other long operations causes CPU spin-waste and degrades system-wide throughput. The type alias provides no documentation of the busy-wait semantics; callers may use it where a blocking mutex is appropriate | Execution/ExecutionTBB.hpp:SpinningMutex-tbb-spin_mutex-busy-wait | Medium | P2 |
| 952 | `Bound` default `min` in `Optimize/Optimizer.hpp` is `std::numeric_limits<double>::min()` (smallest positive double, ~2.2e-308), NOT `std::numeric_limits<double>::lowest()` (most negative double). Callers expecting "no lower bound" that use the default will instead have a near-zero lower bound, silently clipping the optimizer search space to positive values | Optimize/Optimizer.hpp:Bound:default-min-is-lowest-positive-not-most-negative | Medium | P2 |
| 953 | `StopCriteria::max_iterations(double val)` setter silently truncates `double` to `unsigned`. Passing a negative value (e.g., `-1.0` as a "use default" sentinel) wraps to `UINT_MAX` (~4 billion), causing the optimizer to spin for an astronomically long time instead of stopping | Optimize/Optimizer.hpp:StopCriteria:max_iterations-setter-double-to-unsigned-truncation | High | P1 |
| 954 | `StopCriteria::max_iterations()` getter returns `double` while the stored field is `unsigned`. Asymmetric types between getter and setter confuse ports; a direct round-trip `set → get` through `double` loses precision for values > 2^53 | Optimize/Optimizer.hpp:StopCriteria:max_iterations-getter-returns-double-stored-unsigned | Medium | P2 |
| 955 | In `NLoptOptimizer.hpp` `optfunc` callback (line ~138): `(*score.gradient)[i]` dereferences `score.gradient` (a `std::optional`) without first checking `score.gradient.has_value()`. When NLopt calls the function without a gradient request (gradient-free algorithm mode), `gradient` is `std::nullopt` and the dereference is undefined behaviour | Optimize/NLoptOptimizer.hpp:optfunc:optional-gradient-dereference-without-check | Critical | P0 |
| 956 | In `NLoptOptimizer.hpp` `optfunc`, the raw C-pointer `gradient` (passed by NLopt) is written via `(*score.gradient)[i] = gradient[i]` without checking whether the C pointer is null. NLopt passes a null gradient pointer for gradient-free algorithms; the write constitutes undefined behaviour | Optimize/NLoptOptimizer.hpp:optfunc:c-gradient-pointer-not-null-checked | High | P1 |
| 957 | Internal struct named `AlgBurteForce` (typo — "Burte" instead of "Brute") in `BruteforceOptimizer.hpp`. The public alias `AlgBruteForce` is correctly spelled, but the internal type name is wrong. Grep-based searches for "AlgBruteForce" within the implementation will miss the typo name; template error messages expose the misspelled internal name | Optimize/BruteforceOptimizer.hpp:AlgBurteForce-typo-internal-struct-name | Low | P3 |
| 958 | `BruteforceOptimizer::num_iter()` uses `std::pow(gridsz, i)` (floating-point exponentiation for integer grid sizing). For `gridsz^i > 2^53` the `double` result loses integer precision, producing a wrong total iteration count. Large grid sizes with many dimensions silently miscalculate the expected iteration budget | Optimize/BruteforceOptimizer.hpp:num_iter:std-pow-floating-point-precision-loss | Low | P3 |
| 959 | CSGMesh Push/Pop stack protocol is implicit and unchecked in all consumers (PerformCSGMeshBooleans, SliceCSGMesh, VoxelizeCSGMesh). Mismatched Push/Pop (extra Push, missing Pop, or Pop without prior Push) silently produces wrong geometry or accesses an empty stack via `opstack.top()` after `opstack.pop()` causes UB | CSGMesh/CSGMesh.hpp:Push-Pop-protocol-unchecked-mismatch-silent-corrupt | High | P1 |
| 960 | Default `get_operation`/`get_mesh`/`get_transform` ADL accessors in `CSGMesh.hpp` access struct members by name. Non-conforming types that don't provide ADL overloads but happen to have identically-named members of wrong types will compile but access wrong fields silently. The contract is enforced only by naming convention, not by concept constraints | CSGMesh/CSGMesh.hpp:ADL-accessors-no-concept-constraint-silent-wrong-field | Medium | P2 |
| 961 | `copy_csgrange_shallow()` in `CSGMeshCopy.hpp`: for non-CSGPart input types, wraps `get_mesh(part)` (a raw const pointer) in `AnyPtr<const indexed_triangle_set>{ptr}` — a non-owning wrapper. If the source range is destroyed before the shallow copy is consumed, every mesh pointer in the copy dangles with no compile-time or runtime check | CSGMesh/CSGMeshCopy.hpp:copy_csgrange_shallow:non-CSGPart-non-owning-dangling-pointer | High | P1 |
| 962 | `copy_csgrange_deep()` allocates a full `indexed_triangle_set` copy for every part. For a CSG scene with many large meshes, this silently doubles peak memory consumption with no bound check or error reporting on allocation failure | CSGMesh/CSGMeshCopy.hpp:copy_csgrange_deep:doubles-peak-memory-no-bound-check | Low | P3 |
| 963 | `is_same()` in `CSGMeshCopy.hpp` uses pointer equality (`get_mesh(*itA) == get_mesh(*itB)`) to compare meshes — NOT structural/content equality. Two CSG ranges that contain copies of the same geometry but at different addresses always compare unequal even if geometrically identical | CSGMesh/CSGMeshCopy.hpp:is_same:pointer-equality-not-content-equality | Medium | P2 |
| 964 | The drill-hole export block in `ModelToCSGMesh.hpp` is entirely commented out. The `mpartsDrillHoles` flag and `do_drillholes` variable are dead code. Any caller that sets `mpartsDrillHoles` receives silently incomplete geometry with no error; drain holes are never subtracted from the CSG scene via this path | CSGMesh/ModelToCSGMesh.hpp:mpartsDrillHoles-dead-code-commented-out | Medium | P2 |
| 965 | Non-split path in `model_to_csgmesh()` stores a raw `const*` to `vol->mesh().its` in CSGPart. If the ModelVolume mesh is replaced or the ModelObject is modified after the CSG range is built, all non-split CSGPart pointers in the range dangle silently. No lifetime token enforces the constraint | CSGMesh/ModelToCSGMesh.hpp:non-split-path:raw-pointer-to-vol-mesh-dangling | High | P1 |
| 966 | Split negative volume in `model_to_csgmesh()`: shells are emitted as Union sub-parts under a Difference Push sentinel. If the shells overlap, they are first unioned and then subtracted — which differs from a direct subtraction of the combined mesh if shells partially cancel each other. Non-split negative volumes are subtracted directly; the two paths are geometrically non-equivalent for overlapping shells | CSGMesh/ModelToCSGMesh.hpp:split-negative-volume-shell-union-before-difference-inequivalent | Medium | P2 |
| 967 | In `PerformCSGMeshBooleans.hpp`, `csgidx` (the index into `cgalmeshes`/`McutMeshes`) is incremented in the serial reduce loop without bounds-checking against the vector size. A refactor that modifies `csgrange` between the parallel build phase and serial reduce phase would cause an out-of-bounds vector access with no diagnostic | CSGMesh/PerformCSGMeshBooleans.hpp:csgidx-no-bounds-check | Low | P3 |
| 968 | `perform_csg()` for both CGAL and mcut silently does nothing when `dst` or `src` is null. A null `dst` at a non-Push step indicates a previous boolean failed silently (exception caught, `ret = nullptr`). The result mesh is then incomplete — wrong geometry — with no error propagated to the caller | CSGMesh/PerformCSGMeshBooleans.hpp:perform_csg:null-operand-silent-no-op-incomplete-result | Medium | P2 |
| 969 | `check_csgmesh_booleans()` uses TBB `for_each` to validate all parts in parallel, but writes into `fail_reason` and `fail_part_name` (shared non-atomic variables) from multiple threads without synchronisation. Concurrent failure-writes constitute a data race and undefined behaviour under the C++ memory model | CSGMesh/PerformCSGMeshBooleans.hpp:check_csgmesh_booleans:unsynchronised-writes-fail_reason-fail_part_name | High | P1 |
| 970 | In `perform_csgmesh_booleans_cgal/mcut`, for a Push sentinel part, the local variable `op` is overridden to Union (line 188/239) but the subsequent `perform_csg` call uses `get_operation(csgpart)` (the pre-override value) directly. The net effect is a no-op for empty sentinel meshes — latent rather than observable — but the variable divergence creates a misleading code pattern that will confuse future maintenance | CSGMesh/PerformCSGMeshBooleans.hpp:push-sentinel-op-variable-not-used-in-perform_csg | Low | P3 |
| 971 | The mcut `check_csgmesh_booleans` overload (`use_mcut=true`) does NOT check `does_bound_a_volume` or `does_self_intersect` — only emptiness is tested. Non-manifold or self-intersecting meshes passed to the mcut boolean engine will produce corrupt output or crash inside mcut with no prior validation gate | CSGMesh/PerformCSGMeshBooleans.hpp:mcut-check-skips-manifold-self-intersect | Medium | P2 |
| 972 | In `SliceCSGMesh.hpp`, the TBB `for_each` lambda that calls `merge_slices` captures `top` as a raw pointer to `opstack.top()`. If the outer loop is ever parallelised or if the opstack is modified concurrently, `top` becomes a dangling pointer. Safe as written (serial outer loop), but brittle — any future parallelisation of the outer loop produces a dangling-pointer data race | CSGMesh/SliceCSGMesh.hpp:merge_slices:top-raw-pointer-brittle-on-future-parallelise | Medium | P2 |
| 973 | In `slice_csgmesh_ex`, if `slicegrid` is empty, `collect_nonempty_indices` produces an empty `indices` vector and the `for_each` over it is a no-op. The result is an empty `ExPolygons` vector returned without any error. Callers that pass an empty slicegrid receive silently empty output | CSGMesh/SliceCSGMesh.hpp:empty-slicegrid-silent-empty-result | Low | P3 |
| 974 | The final `union_ex` pass in `slice_csgmesh_ex` carries a "TODO: verify if this part can be omitted or not" comment. The semantic necessity is unresolved in the source. Omitting it in a port may produce overlapping ExPolygons that break downstream Clipper operations; including it may alter touching/coincident geometry from the merge steps | CSGMesh/SliceCSGMesh.hpp:final-union_ex-TODO-uncertain-semantics | Medium | P2 |
| 975 | `merge_slices` for `CSGType::Union` appends source ExPolygons directly to the target without calling `union_ex`. Union parts accumulate overlapping polygons throughout the loop; they are resolved only by the post-loop `union_ex` pass. This means peak memory for Union-heavy CSG scenes scales with the number of Union parts × polygon count rather than just total polygon count | CSGMesh/SliceCSGMesh.hpp:Union-merge_slices:overlapping-polygons-accumulate-until-end | Medium | P2 |
| 976 | `get_mesh(const TriangleMesh* const part)` in `TriangleMeshAdapter.hpp` returns `&part->its` without checking whether `part` is null. Passing a null `TriangleMesh*` to any CSG algorithm that calls `get_mesh` via this overload produces UB from dereferencing a null pointer | CSGMesh/TriangleMeshAdapter.hpp:get_mesh-TriangleMesh-pointer:no-null-check-UB | Medium | P2 |
| 977 | All four adapter types (`indexed_triangle_set`, `indexed_triangle_set*`, `TriangleMesh`, `TriangleMesh*`) always return `Transform3f::Identity()`. Any code that needs to use a positioned mesh as a CSG part must manually apply the transform to the ITS first. Using these adapters for world-space meshes silently places geometry at the origin, with no compile-time or runtime error | CSGMesh/TriangleMeshAdapter.hpp:get_transform-always-identity-no-transform-support | Medium | P2 |
| 978 | `get_voxelgrid()` in `VoxelizeCSGMesh.hpp` mutates `params.trafo` in-place on a by-value copy — safe today because the TBB lambda captures `params` by value. If the capture is ever changed to `[&]`, all parallel threads will race on the shared `params` object, corrupting the transform applied to each mesh | CSGMesh/VoxelizeCSGMesh.hpp:get_voxelgrid:params-trafo-mutated-latent-race-if-ref-capture | Medium | P2 |
| 979 | Cancellation in `voxelize_csgmesh()` via `statusfn()(-1)` exits the serial reduce loop early, leaving `opstack` with more than one frame. `opstack.top().grid` from the partially-processed base frame is returned as the result. The caller receives a partial/wrong voxel grid with no error code indicating cancellation | CSGMesh/VoxelizeCSGMesh.hpp:voxelize_csgmesh:cancel-returns-partial-grid-no-error | Medium | P2 |
| 980 | `detail::perform_csg()` for voxels does not guard against a null `dst` pointer (unlike the CGAL/mcut equivalents). If `mesh_to_grid` returns null (e.g., OpenVDB allocation failure), the initial frame grid is null, and `grid_union/difference/intersection` dereference it, producing a crash instead of a graceful error | CSGMesh/VoxelizeCSGMesh.hpp:perform_csg:null-dst-no-guard-crash | High | P1 |
| 981 | `grid_union(*dst, *src)` in `perform_csg` is called even when `src` is non-empty but `dst` already contains data. OpenVDB's `grid_union` with an empty src should be a no-op, but this is implementation-defined behaviour. A port must verify the edge-case semantics of the target voxel union primitive for the empty-operand case | CSGMesh/VoxelizeCSGMesh.hpp:grid_union-empty-src-edge-case-implementation-defined | Low | P3 |


| 982 | `fuzzy_polyline()` and `fuzzy_extrusion_line()` modify their input polygon/junction list in-place. Callers that need to preserve the original geometry must copy before calling — there is no API to return a new object. A port exposing immutable input semantics must add a copy internally or change the call sites | Feature/FuzzySkin/FuzzySkin.cpp:fuzzy_polyline-in-place-modification-no-copy | Medium | P2 |
| 983 | `fuzzy_extrusion_line` for `FuzzySkinMode::Combined` computes new extrusion width as `p1.w + r + min_extrusion_width` where `min_extrusion_width = 0.01` (hardcoded mm, not coord_t). The comment calls this a "workaround" and the real formula should reference layer height. The magic constant produces wrong widths if the layer height changes | Feature/FuzzySkin/FuzzySkin.cpp:fuzzy_extrusion_line:min_extrusion_width-magic-constant-not-tied-to-layer-height | Medium | P2 |
| 984 | `apply_fuzzy_skin(Polygon)` single-region optimization path fuzzifies the full polygon without splitting by region boundaries. Multi-region prints with a single fuzzy-region config silently apply fuzzy to areas that may extend outside the configured region | Feature/FuzzySkin/FuzzySkin.cpp:apply_fuzzy_skin:single-region-optimization-skips-region-clip | Low | P3 |
| 985 | `apply_fuzzy_skin(ExtrusionLine*)` single-region optimization path does not copy junctions before modifying. If the caller needs the original junctions for anything after this call, the data is lost. The multi-region path does make a copy (`current_ext`) but the single-region path does not — asymmetric contract | Feature/FuzzySkin/FuzzySkin.cpp:apply_fuzzy_skin:single-region-no-copy-asymmetric-contract | Medium | P2 |
| 986 | `VoxelUtils::walkLine` has no maximum iteration limit on its `while(true)` inner loop. A cell_size_ of 0 in any dimension causes integer division-by-zero in `toGridCoord` and an infinite loop in `walkLine`. No validation of cell_size_ is performed at construction | Feature/Interlocking/VoxelUtils.cpp:walkLine-while-true-no-max-iterations-zero-cell-size-UB | Medium | P2 |
| 987 | `VoxelUtils::walkPolygons` processes voxels at polygon vertex corners potentially multiple times (once per incident edge). Any non-idempotent `process_cell_func` (e.g. a counter) will double-count corner cells. The warning is in the docstring but there is no runtime guard | Feature/Interlocking/VoxelUtils.hpp:walkPolygons-voxels-processed-multiple-times-at-corners | Low | P3 |
| 988 | `VoxelUtils::toGridCoord` uses `coord / cell_size - (coord < 0)` for floor division. This relies on two's-complement signed integer arithmetic for negative coords. Pre-C++20 this was technically implementation-defined for negative division results; C++20 mandates two's complement. Any port to a language or platform without this guarantee must verify or replace the floor-divide formula | Feature/Interlocking/VoxelUtils.hpp:toGridCoord-floor-divide-relies-on-twos-complement | Medium | P2 |
| 989 | `GridPoint3` is a plain typedef for `Vec3crd`. There is no type distinction between grid-space coordinates (voxel indices) and world-space coord_t values (nanometers). Mixing the two compiles silently and produces wrong voxel lookups. Callers must manually ensure they are using the correct coordinate space | Feature/Interlocking/VoxelUtils.hpp:GridPoint3-typedef-Vec3crd-no-type-safety | Medium | P2 |
| 990 | `DilationKernel` constructor does not validate that kernel_size components are odd. An even kernel produces an asymmetric dilation (the center of the kernel is off by half a cell). The algorithm in InterlockingGenerator uses even kernels in some paths (e.g. PRISM with even depth), potentially misplacing the dilation | Feature/Interlocking/VoxelUtils.hpp:DilationKernel-no-odd-kernel-validation | Medium | P2 |
| 991 | `spreadDotsArea` static helper in VoxelUtils.cpp uses `Fill::new_from_type(ipAlignedRectilinear)` to sample a regular grid of points inside a polygon. This brings in the full Fill subsystem (heap allocation, virtual dispatch) for what amounts to a simple grid enumeration. A port should replace this with a direct grid-point loop | Feature/Interlocking/VoxelUtils.cpp:spreadDotsArea-uses-Fill-subsystem-heavy-dependency | Low | P3 |
| 992 | `walkLine` has an unreachable `return true` statement after the `while(true)` loop. Some compilers will warn about unreachable code; the loop can only exit via the internal `return` statements | Feature/Interlocking/VoxelUtils.cpp:walkLine-unreachable-return-after-while-true | Low | P3 |
| 993 | `std::hash<Slic3r::GridPoint3>` specialization in InterlockingGenerator.cpp uses `int` multiplication with a fixed prime (31), accumulating into `int result`. For coordinate values above ~sqrt(INT_MAX/31) ≈ 8M, the multiplication overflows `int` — C++ signed integer overflow is UB. The hash also produces very poor collision resistance for large or negative grid coordinates | Feature/Interlocking/InterlockingGenerator.cpp:GridPoint3-hash-signed-integer-overflow-UB-poor-quality | Medium | P2 |
| 994 | `InterlockingGenerator::generateInterlockingStructure` calls `has_any_mesh.merge(has_all_meshes)`. This intentionally cannibalizes `has_all_meshes` (nodes moved to `has_any_mesh` only if not already present). Post-merge, `has_all_meshes` contains only the intersection. This invariant is not documented and relies on `std::unordered_set::merge` move semantics — a port must replicate this exact behaviour or use an explicit intersection + union algorithm | Feature/Interlocking/InterlockingGenerator.cpp:generateInterlockingStructure-merge-cannibalizes-intersection | Medium | P2 |
| 995 | `cell_size.z()` is set to `2 * beam_layer_count` (a layer count, not a physical nm distance). Throughout InterlockingGenerator the voxel Z coordinate is used as a raw layer index. This conflates layer-count space with coord_t world space. The Z voxel size is not in the same units as the XY voxel size. A port must carefully distinguish layer-index arithmetic from physical-coordinate arithmetic | Feature/Interlocking/InterlockingGenerator.cpp:cell_size_z-layer-count-not-physical-distance-unit-mismatch | Medium | P2 |
| 996 | `ignored_gap_` is declared as `const coord_t` with value `100u` (unsigned literal). The unsigned-to-signed implicit conversion is benign for this value (100 < INT_MAX) but the `u` suffix is a style hazard indicating the author confused coord_t with unsigned types | Feature/Interlocking/InterlockingGenerator.hpp:ignored_gap_-unsigned-literal-assigned-to-signed-coord_t | Low | P3 |
| 997 | `applyMicrostructureToOutlines` divides `layer_nr` by `beam_layer_count` inside a loop. If `beam_layer_count` (coord_t) is 0 due to misconfiguration, this is integer division by zero — crash with no guard or validation | Feature/Interlocking/InterlockingGenerator.cpp:applyMicrostructureToOutlines-beam_layer_count-zero-division | Medium | P2 |
| 998 | `generateMicrostructure` hardcodes exactly 2 mesh indices and 2 layer alternation types. Any extension to 3+ material interlocking (e.g. a three-material sandwich) requires a structural rewrite — the API and data model do not support N materials | Feature/Interlocking/InterlockingGenerator.cpp:generateMicrostructure-hardcoded-2-materials | Low | P3 |

## Session 71 — Orient, OpenVDBUtils, MeshBoolean (H999–H1021)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 999 | `OrientParams` and `OrientParamsArea` are two nearly-identical structs (24+ float fields each) with different default values and no documentation of what distinguishes them conceptually. Both are passed to `AutoOrienter::process()` — the area-weighted variant is used for SLA, the plain variant for FDM. Without comments or type names, callers cannot determine which to use | Orient.hpp:OrientParams-vs-OrientParamsArea-undocumented-distinction | Medium | P2 |
| 1000 | `orient(ModelObject*)` carries a source comment "this function should be deleted" but remains in the public API and is compiled. Calling it invokes deprecated logic that may diverge from the current `AutoOrienter::process()` behaviour | Orient.hpp/Orient.cpp:orient-ModelObject-deprecated-not-removed | Low | P3 |
| 1001 | `fun_dir` (Eigen::Vector3f) exists in both `OrientParams` and `OrientParamsArea` but is never read inside `AutoOrienter::process()`. It is a dead field that will confuse a port — appears to be a direction constraint input that was never implemented | Orient.hpp:fun_dir-dead-field-never-read | Low | P3 |
| 1002 | `progressfn` callback in `OrientParams`/`OrientParamsArea` is invoked from TBB worker threads inside `AutoOrienter::process()`. Any GUI progress callback must be thread-safe. There is no documentation of this threading contract | Orient.cpp:progressfn-called-from-TBB-thread-GUI-callback-must-be-thread-safe | High | P1 |
| 1003 | `VecHash` (used to deduplicate orientation candidates) uses an additive linear hash on quantized normals: `h = (int)(v[0]*100) + (int)(v[1]*100)*100 + (int)(v[2]*100)*10000`. For dense meshes this produces extremely poor collision resistance — many distinct normals map to the same bucket, degrading deduplication | Orient.cpp:VecHash-poor-collision-resistance-linear-additive-hash | Medium | P2 |
| 1004 | `AutoOrienter::process()` unconditionally writes progress to both `BOOST_LOG_TRIVIAL(info)` and `std::cout` for every orientation evaluated. On a mesh with 1000 orientation candidates this produces 1000 stdout lines in production builds — verbose noise that may interfere with piped toolchains | Orient.cpp:process-stdout-noise-every-orientation | Medium | P2 |
| 1005 | `auto its = mesh->its;` in `preprocess()` copies the entire indexed_triangle_set; `project_vertices()` is called on every orientation candidate (potentially 100+) and also copies the ITS internally. This is O(face_count × candidate_count) in memory allocations | Orient.cpp:preprocess-project_vertices-O(N_faces*N_candidates)-copy-allocations | Medium | P2 |
| 1006 | After `remove_duplicates()` in `process()`, `results_vector` may be empty if all computed orientations were zero or duplicates. The subsequent `results_vector[0]` access is an out-of-bounds access — undefined behaviour with no guard | Orient.cpp:process-results_vector-0-OOB-if-empty-after-dedup | Low | P3 |
| 1007 | `its.get_vertex(i, v)` usage in Orient.cpp requires verification that `i` is a triangle index and the sub-index `v` (0–2) is a vertex within that triangle. If the ITS API uses a flat vertex index instead, all orientation computations are reading wrong vertices silently | Orient.cpp:its_get_vertex-triangle-vs-vertex-index-ambiguity | Low | P3 |
| 1008 | `costs.area_total` is set to `mesh->bounding_box().area()` — the surface area of the bounding box, NOT the sum of triangle face areas. The name `area_total` strongly implies total mesh surface area. This misrepresentation affects the normalisation of cost functions that divide by `area_total` | Orient.cpp:costs_area_total-bounding-box-area-not-mesh-surface-area | Low | P3 |
| 1009 | The contour metric is computed as `4 * sqrt(bottom)` — the perimeter of a square with equal area to the bottom projection. A commented-out `#else` path contains a more geometrically accurate perimeter computation that is permanently disabled | Orient.cpp:contour-metric-equal-area-square-approximation-accurate-path-disabled | Low | P3 |
| 1010 | The `target_function` denominator includes `TAR_D` (~0.63) which prevents division by zero but causes the cost function to lose geometric grounding for spherical or fully convex objects (where the bottom projection is always near-zero) | Orient.cpp:target_function-denominator-TAR_D-breaks-for-spherical-objects | Medium | P2 |
| 1011 | `(costs.bottom < BOTTOM_MIN) * 100` is a magic stability penalty multiplied into the score. For very small objects whose flat area is naturally below `BOTTOM_MIN`, this permanently rejects all orientations regardless of geometric quality. No minimum-object-size exemption exists | Orient.cpp:BOTTOM_MIN-magic-stability-penalty-rejects-all-orientations-for-small-objects | Low | P3 |
| 1012 | `openvdb::initialize()` is called at the top of both `mesh_to_grid()` and `grid_to_mesh()`. The OpenVDB initialisation function acquires an internal mutex on every call. Calling it repeatedly from hot paths (e.g. support generation for many objects) adds unnecessary lock contention. It should be called once at application startup | OpenVDBUtils.cpp:openvdb_initialize-called-per-operation-mutex-overhead | Medium | P2 |
| 1013 | In `mesh_to_grid()`, if `its_split` returns non-empty parts but the volume filter (`volume > 0`) removes ALL of them, `grid` remains `nullptr`. The subsequent `grid->insertMeta(...)` unconditionally dereferences it — null pointer dereference crash | OpenVDBUtils.cpp:mesh_to_grid-grid-null-deref-if-all-parts-filtered | High | P1 |
| 1014 | `grid_to_mesh()` wraps the `grid.metaValue<float>("voxel_scale")` call in a bare `catch(...){}` that silently defaults to `scale = 1.0`. A grid whose voxel_scale metadata was set to 2.0 will produce a mesh scaled to 2x the intended size with no warning or error | OpenVDBUtils.cpp:grid_to_mesh-voxel_scale-silent-default-on-exception | Medium | P2 |
| 1015 | `redistance_grid()` passes `ext_range` and `int_range` (typed as `double`) to `openvdb::tools::levelSetRebuild` which accepts `float` — the values are silently narrowed. For very thin features (sub-voxel ext_range), the float truncation may change the sign of the narrow-band extent | OpenVDBUtils.cpp:redistance_grid-double-to-float-truncation-ext_range-int_range | Medium | P2 |
| 1016 | `triangle_mesh_to_eigen()` accesses `mesh.its.vertices.front()` and `mesh.its.indices.front()` with NO empty check. Calling on an empty TriangleMesh is undefined behaviour (front() on empty vector). All callers of the igl/EigenMesh boolean path must guarantee non-empty input | MeshBoolean.cpp:triangle_mesh_to_eigen:front-UB-on-empty-mesh | High | P1 |
| 1017 | `_cgal_do()` uses `try_catch_signal({SIGSEGV, SIGFPE}, ...)` to catch CGAL internal crashes via longjmp/signal handlers. This is non-portable (no-op or UB on Windows), leaves CGAL's internal heap state inconsistent, and bypasses RAII destructors — the result mesh after a caught SIGSEGV is in an indeterminate state | MeshBoolean.cpp:_cgal_do:try_catch_signal-longjmp-non-portable-bypasses-RAII | High | P1 |
| 1018 | `triangle_mesh_to_cgal(TriangleMesh)` throws `RuntimeError("Mesh not watertight")` for open meshes. The `triangle_mesh_to_cgal(V, F)` overload does NOT check or throw — asymmetric exception semantics. Callers that mix both overloads will see inconsistent failure modes for the same open-mesh input | MeshBoolean.cpp:triangle_mesh_to_cgal-overload-asymmetric-exception-semantics | High | P1 |
| 1019 | `merge_mcut_meshes()` round-trips both McutMesh inputs through TriangleMesh → ITS → McutMesh as a fallback. This introduces two rounds of double→float→double precision truncation and two full mesh heap allocations. The round-trip should instead directly append the flat vertex/face arrays | MeshBoolean.cpp:merge_mcut_meshes-round-trip-precision-truncation-expensive | Medium | P2 |
| 1020 | `do_boolean_single()` dereferences `booleanOpts.find(boolean_opts)->second` without checking whether the iterator equals `end()`. An unknown string key (e.g. "XOR") will dereference an end iterator — undefined behaviour. No error is thrown or logged for an unrecognised operation name | MeshBoolean.cpp:do_boolean_single:booleanOpts-find-no-end-check-UB | Medium | P2 |
| 1021 | `do_boolean()` splits both src and cut meshes into connected components, then runs `do_boolean_single` on every (src_part, cut_part) pair — O(N_src × N_cut) mcut context create/destroy cycles. For N=100 shells each side, this is 10,000 mcut operations. No batching or early-exit strategy exists | MeshBoolean.cpp:do_boolean:O(N_src*N_cut)-quadratic-mcut-pairs | Medium | P2 |

## Session 72 — Emboss, CutSurface, Shape/TextShape (H1022–H1049)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1022 | `FontFile::operator==` uses `==` instead of `!=` for the `descent` and `linegap` fields. The intended logic is to return false if any field differs; the current code returns true when descent and linegap match (inverted). This is a likely copy-paste bug — equality semantics for `FontFile` are silently wrong for any collection with matching ascent but different descent | Emboss.hpp:FontFile::operator==-inverted-equality-descent-linegap | High | P1 |
| 1023 | `FontFileWithCache::cache` is a raw `Glyphs*` (pointer to an unordered_map). The threading contract — main thread writes to cache, job threads read — is enforced only by call-site discipline with no lock, atomic, or documented ownership rule. A port must add a synchronisation primitive or isolate the cache per-thread | Emboss.hpp:FontFileWithCache-cache-raw-pointer-no-sync-threading-contract | Medium | P2 |
| 1024 | `UNION_DELTA = 50.0f` in `EmbossShape` controls glyph union expansion in Clipper units (scaled coordinates, approx 0.05 mm). The value has "approximate semantics" with no link to printer resolution or font size. Refactoring must expose this as a configurable parameter | Emboss.hpp:UNION_DELTA-magic-constant-approximate-semantics | Medium | P2 |
| 1025 | `ProjectTransform::project()` ignores `m_tr` (the stored Transform3d) and returns `point` unchanged. `create_front_back()` in the same class DOES apply `m_tr`. This inconsistency means any caller that uses `project()` directly (rather than `create_front_back()`) gets untransformed 3D points | Emboss.hpp:ProjectTransform-project-ignores-m_tr-inconsistent-with-create_front_back | Medium | P2 |
| 1026 | **CRITICAL — THIRD COORDINATE SYSTEM.** OrcaSlicer operates in three distinct coordinate systems: (1) print-scale mm (coord_t = int64, 1 unit = 1e-6 mm), (2) world mm (double), and (3) glyph-scale (int32, 1 unit = `SHAPE_SCALE` = 0.001 mm). `polygons2model_unique/duplicit` receives Points in glyph-scale, then calls `projection.create_front_back()` to convert to world mm. A refactor that passes world-mm coordinates to these functions will produce geometry 1000x too large with no error | Emboss.cpp/Emboss.hpp:SHAPE_SCALE-third-coordinate-system-1-unit-0.001mm-silent-1000x-scale-error | Critical | P0 |
| 1027 | `heal_dupl_inter()` fallback: when `heal_expolygons()` fails to fix a degenerate glyph after `MAX_HEAL_ITERATION` attempts, the fallback replaces the entire ExPolygon with a bounding-rectangle hollow shape. This silently corrupts glyph geometry in Release builds — the only indication is a `BOOST_LOG_TRIVIAL(error)` message | Emboss.cpp:heal_dupl_inter-fallback-bounding-rect-hollow-corrupts-glyph | High | P1 |
| 1028 | `get_font_list_by_enumeration()` and `get_font_list_by_folder()` are defined and compiled on Windows but are never called from `get_font_list()` — which unconditionally calls only `get_font_list_by_register()`. The two enumeration functions are permanently unreachable dead code that will confuse a refactoring agent | Emboss.cpp:get_font_list_by_enumeration-and-folder-dead-code-never-called | Medium | P2 |
| 1029 | `const float RESOLUTION = 0.0125f` in `get_glyph()` controls the flatness of stbtt curve flattening (TrueType bezier → polyline approximation). A TODO comment to derive this from printer resolution has never been resolved. Fine-pitch printers would benefit from a smaller value; the constant produces slightly coarser glyph outlines than necessary | Emboss.cpp:get_glyph-RESOLUTION-hardcoded-magic-constant-TODO-unresolved | Low | P3 |
| 1030 | In `text2vshapes()`, the `was_canceled()` function is polled only every `CANCEL_CHECK = 10` characters. For a 500-character text block, there is up to 10-character latency (potentially hundreds of ms) before a UI cancel is honoured | Emboss.cpp:text2vshapes-cancel-checked-every-10-chars-only | Low | P3 |
| 1031 | `EnumFamCallBack` is a `CALLBACK`-qualified free function (Windows only). The `UNREFERENCED_PARAMETER(lpntm)` statement that follows the early `return true` is permanently unreachable dead code. The function itself is also dead — it is called only from `get_font_list_by_enumeration()` which is never called | Emboss.cpp:EnumFamCallBack-UNREFERENCED_PARAMETER-unreachable-after-early-return | Low | P3 |
| 1032 | `get_glyph()` (stbtt inner version) uses C-heap `free()` via `ScopeGuard` for stbtt-allocated vertices/contours. This correctly manages the C-heap memory but in a refactoring to Rust/Swift/etc., the pattern must be replaced with a language-appropriate custom allocator or deleter — `free()` is not compatible with C++ `delete` or new-language allocators | Emboss.cpp:get_glyph-C-heap-ScopeGuard-stbtt-vertices-needs-custom-deleter | Medium | P2 |
| 1033 | The `#define REMOVE_SPIKES` macro is commented out at the top of `Emboss.cpp` and is never defined in the build system. The entire `remove_spikes(Polygon&)`, `remove_spikes(Polygons&)`, and `remove_spikes(ExPolygons&)` implementation block (~150 lines) inside `#ifdef REMOVE_SPIKES` is permanently dead code | Emboss.cpp:REMOVE_SPIKES-permanently-dead-code-150-lines | Medium | P2 |
| 1034 | `g_occt_fonts_maps` is a `static std::map<std::string, std::string>` defined at file scope in `TextShape.cpp`. It is written by the main thread (via `get_occt_fonts()` → `get_font_path_by_name()`) and read by job threads executing `Prism()`. There is no mutex, atomic, or documented memory ordering — concurrent reads during a map rebuild are a data race | Shape/TextShape.cpp:g_occt_fonts_maps-global-static-no-sync-main-vs-job-threads | High | P1 |
| 1035 | `Prism()` in `TextShape.cpp` allocates `BRepPrimAPI_MakePrism` with `new` but never deletes it. The raw pointer is captured to call `.Shape()` and then abandoned. Every call to `Prism()` leaks one heap allocation | Shape/TextShape.cpp:Prism-BRepPrimAPI_MakePrism-new-never-deleted-memory-leak | High | P1 |
| 1036 | The OCCT tessellation path in `TextShape.cpp` copies geometry twice: OCCT BRep → StlMesh_MeshExplorer (STL intermediate) → Slic3r TriangleMesh. The STL intermediate is a full in-memory copy of the triangle data. A direct OCCT→Poly_Triangulation path would avoid one copy and reduce peak memory by ~50% for large glyphs | Shape/TextShape.cpp:tessellation-two-copy-path-OCCT-stl_file-TriangleMesh | Medium | P2 |
| 1037 | `fonts_suffix` exclusion list in `get_occt_fonts()` uses `std::string::find()` from the end (`SearchFromEnd`) — any font whose name ends with any suffix in the list is excluded. The string "ExtraBold" appears twice in the list (duplicate entry). More importantly, legitimate font families named e.g. "HarmonyOS Sans SC ExtraBold" are permanently excluded | Shape/TextShape.cpp:fonts_suffix-SearchFromEnd-ExtraBold-duplicate-may-hide-valid-fonts | Medium | P2 |
| 1038 | The map key for the HarmonyOS font variant is stored as `"HarmoneyOS Sans SC"` (with an extra 'e' in "Harmoney") but the display name is `"HarmonyOS Sans SC"`. The map lookup by display name will always fail to find this entry — the HarmonyOS font is effectively inaccessible via the OCCT path | Shape/TextShape.cpp:g_occt_fonts_maps-typo-HarmoneyOS-vs-HarmonyOS | Medium | P2 |
| 1039 | `its_cut_AoI()` in `CutSurface.hpp` performs AOI filtration using only a 2D XY bounding box (derived from `shapes_bb`). The Z extent of the projection is not checked. Triangles far outside the intended projection depth range but within the XY footprint are included in the CGAL mesh, making corefine more expensive and potentially producing spurious intersections | CutSurface.hpp:its_cut_AoI-2D-XY-only-no-Z-check | Medium | P2 |
| 1040 | `set_skip_for_out_of_aoi()` in `CutSurface.cpp` filters model triangles using only the 2D XY bounding box of the glyph shapes. No Z (depth) range check is performed. Triangles far outside the emboss depth but within the XY footprint are included, increasing CGAL mesh size and corefine complexity unnecessarily | CutSurface.cpp:set_skip_for_out_of_aoi-no-Z-range-filter-oversized-CGAL-mesh | Medium | P2 |
| 1041 | `IntersectingElement::attr` packs `Type` (bits 0-2), `is_first` (bit 3), and `is_last` (bit 4) into a single `unsigned char` using arithmetic `+` and `-` instead of bitwise `|`/`&` operations. `set_type()` performs `attr += (int)t - (int)get_type()`, which is safe only when called in the exact construction order used in `to_cgal(ExPolygons)`. Out-of-order calls or calls on a partially-set `attr` silently corrupt the bitfield | CutSurface.cpp:IntersectingElement-attr-arithmetic-bitfield-ops-fragile-ordering | Medium | P2 |
| 1042 | `flood_fill_inner()` uses face iteration order (CGAL internal) to break ties when a `not_constrained` face is adjacent to two separate `inside` regions. The result depends on non-deterministic CGAL mesh face ordering, producing different inside/outside classifications across CGAL versions or after mesh compaction | CutSurface.cpp:flood_fill_inner-ambiguous-face-classification-at-glyph-adjacency | Medium | P2 |
| 1043 | `polygons2model_duplicit()` skips the side-wall quad (`continue` when `prev == index`) for any pair of consecutive polygon vertices that deduplicated to the same index. At small font sizes or low SHAPE_SCALE resolution, many vertices collapse, producing non-manifold gaps in the side wall. Downstream CGAL corefine may silently produce incorrect results on non-manifold inputs | Emboss.cpp:polygons2model_duplicit-dedup-skips-side-wall-quad-non-manifold | Medium | P2 |
| 1044 | `create_transformation_onto_surface()` computes `axis_view = emboss_dir.cross(normal)` and then calls `axis_view.normalize()`. When `normal` is very close (but not equal) to `-Vec3d::UnitZ()`, `axis_view` is nearly zero and `normalize()` produces NaN/Inf. Only the exact equality `normal == -Vec3d::UnitZ()` triggers the safe special case | Emboss.cpp:create_transformation_onto_surface-near-degenerate-axis-no-guard | Medium | P2 |
| 1045 | `sample_slice()` uses `is_reverse=true` to mean "walk forward" (calls `point_in_distance`) and `is_reverse=false` to mean "walk backward" (calls `point_in_reverse_distance`). The naming inversion exists in the implementation and was noted as intentional but is a significant source of confusion for any future maintainer or port | Emboss.cpp:sample_slice-is_reverse-naming-inversion-confusing | Low | P3 |
| 1046 | `align_shape()` contains `assert(shapes.size() == text.length())` as its only precondition guard. In Release builds (where `NDEBUG` is defined), this assert is compiled out. A caller that passes mismatched shapes/text lengths will silently misalign glyphs with no diagnostic | Emboss.cpp:align_shape-assert-only-precondition-no-Release-guard | Low | P3 |
| 1047 | `select_patches()` uses `extend_delta = 1% of average(shapes_bb width, height)` to expand back-projected patch polygons before union. When two adjacent glyph ExPolygons are closer than `2 * extend_delta`, the extension causes patches from one glyph to fill gaps in another, producing incorrect glyph coverage | CutSurface.cpp:select_patches-extend_delta-cross-glyph-bleed | Medium | P2 |
| 1048 | `assert(!exist_duplicit_vertex(cgal_shape))` in `cut_from_model()` is debug-only (compiled out in Release). In Release, duplicate vertices in the CGAL shape mesh are silently passed to CGAL corefine, which may produce topologically incorrect intersection edges and wrong flood-fill results | CutSurface.cpp:exist_duplicit_vertex-assert-debug-only-Release-passes-dupes-to-CGAL | Medium | P2 |
| 1049 | `// #define DEBUG_OUTPUT_DIR std::string("C:/data/temp/cutSurface/")` is permanently commented out. All ~150 lines of `#ifdef DEBUG_OUTPUT_DIR` debug-store helpers (store(), prepare_dir(), etc.) are permanently dead code compiled into the binary only in Debug builds when the define is manually re-enabled. The hardcoded Windows path inside an active string literal at line 50 leaks a developer machine path | CutSurface.cpp:DEBUG_OUTPUT_DIR-commented-out-150-dead-debug-helpers-hardcoded-path | Low | P3 |

## Session 73 — MultiPoint, GCodeSender, calib, PerimeterGenerator.hpp (H1050–H1068)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1050 | `MultiPoint::scale(factor_x, factor_y)` casts each product to `coord_t` (int64) individually before storing. Each cast truncates the fractional part. When called repeatedly in a loop (e.g. animated transforms), rounding error accumulates — a point that should land at exact integer coords after N steps may drift by up to N ULP. A refactor should accumulate in floating-point and convert once | MultiPoint.hpp:scale-coord_t-cast-truncation-rounding-accumulation | Medium | P2 |
| 1051 | `MultiPoint::closest_point_index()` iterates over vertices only, using `Point::distance_to()`. It does NOT check edge interiors. For a caller that expects "closest point on the polyline/polygon geometry", this function returns the closest vertex, which may be significantly farther than the closest point on the nearest edge. The semantic mismatch is undocumented | MultiPoint.hpp:closest_point_index-vertices-only-not-edge-interiors | Medium | P2 |
| 1052 | `MultiPoint::distance_to()` calls `closest_point()` (which returns `std::optional<Point>`) and dereferences it with `*` without checking whether the optional is engaged. On an empty `MultiPoint` (no points), `closest_point()` returns `std::nullopt` and the dereference is undefined behaviour — typically a crash | MultiPoint.hpp:distance_to-dereferences-closest_point-optional-no-empty-check | Medium | P2 |
| 1053 | `MultiPoint::visivalingam()` (Visvalingam–Whyatt simplification) calls `std::make_heap()` to rebuild the entire priority queue after every node update. Each call is O(n) per node update, making the total complexity O(n²). A correct Visvalingam implementation would use a decrease-key operation (O(log n)) instead. For large polygons (n > 1000) this degrades performance noticeably | MultiPoint.hpp:visivalingam-make_heap-after-every-update-O(n^2) | Medium | P2 |
| 1054 | `area(const Points& pts)` in `MultiPoint.hpp` computes the Shoelace sum but does **not** divide by 2. The function returns twice the signed area. Any caller expecting the true signed area will be off by a factor of 2. Grep confirms callers in `Polygon::area()` do divide by 2 in their own wrappers, but callers of the free function `area(const Points&)` directly are at risk | MultiPoint.hpp:area-returns-2x-signed-area-no-divide-by-2 | High | P1 |
| 1055 | Inside `_douglas_peucker()` there is a dead `#if 0` block containing SVG export debug code. This block was presumably used during development for visual debugging of the Douglas-Peucker simplification output and was never removed. It adds noise to the source and must be cleaned up before a refactor | MultiPoint.hpp:_douglas_peucker-dead-if0-SVG-debug-block | Low | P3 |
| 1056 | The parameter name `tolerence` in `concave_hull_2d()` is a misspelling of `tolerance`. The typo is present in both the declaration (`MultiPoint.hpp`) and implementation (`MultiPoint.cpp`). A refactor that adds a configuration struct or named parameter will propagate this typo unless corrected | MultiPoint.hpp:concave_hull_2d-tolerence-typo | Medium | P2 |
| 1057 | `GCodeSender` uses three separate mutexes: `m_send_mutex` (protects the sending state), `m_queue_mutex` (protects the send queue), and `m_error_mutex` (protects the error string). There is no single compound lock for reading "is queue empty AND not currently sending" atomically. Any external code that needs to determine a quiescent state must acquire multiple mutexes in a fixed order, or a TOCTOU race is possible | GCodeSender.hpp:three-separate-mutexes-no-atomic-compound-read | Medium | P2 |
| 1058 | `GCodeSender.cpp` defines `DEBUG_SERIAL` as `if(0)` — a C-style "disable" idiom. Any developer searching for logging during a debugging session and changing `if(0)` to `if(1)` will accidentally enable serial debug output globally (including high-frequency `on_read` callbacks) with no warning. The pattern should use `#if 0` or a proper logger | GCodeSender.cpp:DEBUG_SERIAL-if(0)-accidental-enable-risk | Low | P3 |
| 1059 | `calib.hpp` contains `#ifdef calib_pressure_advance_dd` preprocessor guard and a `CalibPressureAdvanceDD` class forward-declaration inside it. `calib_pressure_advance_dd` is never defined in any build system file or header. The entire feature branch and class are permanently dead code | calib.hpp:calib_pressure_advance_dd-dead-feature-flag-macro | Low | P3 |
| 1060 | `CalibPressureAdvancePattern::m_encroachment` is initialised to `1/3` (integer division → 0 in C++). This is almost certainly a bug — the intended value is `1.0/3.0 ≈ 0.333`. The field controls how far the glyph pattern overlaps adjacent objects. With `m_encroachment = 0`, the encroachment feature is silently disabled. There is no config option to change this value | calib.hpp:m_encroachment-integer-division-1/3-equals-zero | Medium | P2 |
| 1061 | Multiple geometry constants in `CalibPressureAdvanceLine` and `CalibPressureAdvancePattern` are hardcoded with no configuration path: `digit_segment_len=2`, `wall_side_length=30`, `corner_angle=90`, `num_layers=4`, `handle_xy_size=5`, `handle_spacing=1.2`. These values were tuned for a specific printer/nozzle size and may be inappropriate for other hardware | calib.hpp:multiple-hardcoded-geometry-constants-no-config | Low | P3 |
| 1062 | `CalibPressureAdvanceLine` stores default values for `m_layer_height=0.2`, `m_line_width=0.6`, and related parameters as `double` fields with hardcoded initialisers. These defaults are not derived from the active print config at construction time — they are only updated when `CalibPressureAdvanceLine::generate_polylines()` is called with an actual config. A caller that reads the fields before calling `generate_polylines()` sees stale defaults | calib.hpp:CalibPressureAdvanceLine-hardcoded-default-flow-layer-params | Medium | P2 |
| 1063 | `CalibPressureAdvancePattern::generate_custom_gcodes()` reads multiple config options via `config.option<ConfigOptionFloat>("key")->value`. There is no null-pointer guard before the `->value` dereference. If `config.option<T>()` returns nullptr (unknown or missing key), the dereference is undefined behaviour. The OrcaSlicer config system does not guarantee all keys are present for all printer profiles | calib.hpp/calib.cpp:generate_custom_gcodes-config-option-no-null-guard | Medium | P2 |
| 1064 | `CalibPressureAdvancePattern::draw_box()` implements a 45° diagonal infill pattern. The non-trivial branching logic for diagonal fill segments was acknowledged in a source comment as "not robust" — it can generate slightly incorrect fill at box corners when the box dimensions are not evenly divisible by the diagonal step. This may produce visual artefacts in the calibration print output | calib.cpp:draw_box-45deg-fill-not-robust-corner-artefacts | Medium | P2 |
| 1065 | `CalibPressureAdvancePattern::generate_custom_gcodes()` calls `refresh_setup()` once inside the constructor (at object creation) and then again as the first statement in `generate_custom_gcodes()` itself. This double initialisation is redundant — the constructor call is wasted work unless the object is constructed and then `generate_custom_gcodes()` is called with a different config. No documentation explains when the constructor-time call is needed | calib.cpp:generate_custom_gcodes-double-refresh_setup-call-redundant | Medium | P2 |
| 1066 | Both `CalibPressureAdvancePattern::refresh_setup()` and `_refresh_writer()` access `object.volumes.front()` without checking whether `object.volumes` is empty. If a `ModelObject` is constructed with no volumes (e.g. programmatically during testing or in an error recovery path), this call is undefined behaviour — a dereference of `end()` on an empty vector | calib.cpp:refresh_setup-_refresh_writer-volumes.front-no-empty-check | Medium | P2 |
| 1067 | `PerimeterGenerator` exposes nine raw non-owning pointer input fields (`slices`, `compatible_regions`, `upper_slices`, `upper_slices_same_region`, `lower_slices`, `config`, `object_config`, `print_config`, and `fill_no_overlap`) plus four raw non-owning pointer output fields (`loops`, `gap_fill`, `fill_surfaces`, `fill_no_overlap`). None are guarded by smart pointers or precondition assertions. The caller (`LayerRegion::make_perimeters`) must ensure all pointed-to objects outlive the generator; this contract is implicit and unchecked | PerimeterGenerator.hpp:nine-raw-pointer-inputs-four-raw-pointer-outputs-no-smart-pointer-protection | Medium | P2 |
| 1068 | `std::hash<FuzzySkinConfig>` in `PerimeterGenerator.hpp` hashes `type`, `thickness`, `point_distance`, `fuzzy_first_layer`, `noise_type`, `noise_scale`, `noise_octaves`, and `noise_persistence` — but NOT `mode`. The `operator==` DOES compare `mode`. Two `FuzzySkinConfig` objects that differ only in `mode` produce the same hash but compare unequal — a valid (but potentially performance-degrading) hash collision. In `regions_by_fuzzify`, such configs land in the same bucket; lookup correctness is preserved by `operator==` but hash quality degrades for workloads with multiple mode variants | PerimeterGenerator.hpp:hash-FuzzySkinConfig-omits-mode-field-latent-collision | Medium | P2 |

## Session 74 — PerimeterGenerator.cpp Lower Half (H1069–H1086)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1069 | `paths_touch()` builds two `AABBTreeLines::LinesDistancer` objects (one per path) on every call — O(n) construction each. It is called O(n²) times from `sort_extra_perimeters()` for all path pairs in the overhang perimeter set. No spatial index is shared or cached across calls. For large overhang layers with many extra perimeter fragments, this inner loop dominates annotation processing time | PerimeterGenerator.cpp:paths_touch-AABB-rebuild-per-call-O(n^2)-hot-loop | Low | P3 |
| 1070 | `reconnect_polylines()` uses a nested O(n²) loop: outer iterates all polylines as base, inner iterates all later polylines as candidates. For the small fragment counts produced by `intersection_pl()` on normal overhang geometry this is acceptable. For pathological layers with many thin overhang arcs (high resolution, complex overhangs) the O(n²) cost becomes a performance concern | PerimeterGenerator.cpp:reconnect_polylines-O(n^2)-merge-loop | Medium | P2 |
| 1071 | `reconnect_polylines()` appends polylines end-to-end based purely on endpoint proximity; it does not check whether the resulting merged polyline self-intersects. Merging two polylines that cross each other produces an invalid self-intersecting path. No downstream validation catches this | PerimeterGenerator.cpp:reconnect_polylines-no-self-intersection-check-on-merged-result | Medium | P2 |
| 1072 | `sort_extra_perimeters()` iterates the dependency graph with a `while(change)` loop. If the dependency graph has a cycle (possible for ring-shaped overhangs where paths A→B→A are mutually adjacent), the loop terminates with some paths still having non-empty dependency sets. Those paths are excluded from `sorted_paths` and silently dropped — producing missing extrusion segments in the printed layer with no diagnostic | PerimeterGenerator.cpp:sort_extra_perimeters-cycle-in-dependency-graph-silently-drops-paths | Medium | P2 |
| 1073 | `sort_extra_perimeters()` uses `size_t(-1)` as the `null_idx` sentinel (≈ 1.8×10¹⁹ on 64-bit). After a path is "consumed", its dependency set is replaced by `{null_idx}`. If `path_idx` ever equals `null_idx` (impossible in practice on 64-bit systems but theoretically possible on hypothetical 16/32-bit targets), the sentinel comparison yields a false positive and terminates the sort early | PerimeterGenerator.cpp:sort_extra_perimeters-null_idx-size_t(-1)-sentinel-theoretical-wrap | Low | P3 |
| 1074 | `generate_extra_perimeters_over_overhangs()` uses `continuation_loops = 2` to control how many inward-offset passes are generated after the overhang is fully covered. This magic constant is not exposed to user configuration. A value of 2 was tuned for typical FDM overhangs; for very gradual slopes or exotic materials more passes may improve quality while fewer would suffice for steep walls | PerimeterGenerator.cpp:generate_extra_perimeters-continuation_loops=2-magic-constant | Low | P3 |
| 1075 | The bridgeability gate in `generate_extra_perimeters_over_overhangs()` skips overhang perimeter generation when `unbridgeable_area < 20% of real_overhang_area AND unsupp_dist < 20% of total_length`. These two 20% thresholds were tuned empirically and may mis-classify medium-difficulty overhangs — skipping extra perimeters where they would improve quality, or generating unnecessary passes where bridging would succeed | PerimeterGenerator.cpp:generate_extra_perimeters-20pct-bridgeable-threshold-may-misclassify | Medium | P2 |
| 1076 | `apply_extra_perimeters()` calls `this->loops->entities.back()` and performs a `static_cast<ExtrusionEntityCollection*>` on it without any guard for `loops->entities.empty()`. If the current island produced zero regular perimeters (e.g. the surface was too small for any loop), `entities.back()` is undefined behaviour on an empty vector. The function is called once per island after the perimeter generation loop | PerimeterGenerator.cpp:apply_extra_perimeters-no-guard-loops-entities-empty | High | P1 |
| 1077 | `reorient_perimeters()` iterates `entity->is_loop()` on the top-level `entities` collection. It does not recurse into nested `ExtrusionEntityCollection` children. In Arachne output, loops may be stored nested inside sub-collections. Those nested loops are not visited by `reorient_perimeters()`, so steep-overhang winding reversal is silently not applied to them | PerimeterGenerator.cpp:reorient_perimeters-not-recursive-skips-nested-collections | Medium | P2 |
| 1078 | `add_infill_contour_for_arachne()` silently clears `infill_contour` (sets it to empty) and skips appending to `fill_surfaces` when `offset_ex(infill_contour, -spacing/2)` returns an empty result. This can be triggered by a Clipper floating-point precision glitch on a valid geometry that is borderline with respect to the offset threshold. The result is a hollow island with no infill and no diagnostic message | PerimeterGenerator.cpp:add_infill_contour_for_arachne-silent-discard-on-empty-offset | Medium | P2 |
| 1079 | `process_no_bridge()` iterates `all_surfaces` by index while conditionally calling `all_surfaces.push_back()` inside the loop (when multiple sub-surfaces are split from a bridgeable surface). A `push_back` that triggers reallocation invalidates all iterators and pointers into `all_surfaces`. The code uses raw index arithmetic to compensate, but the `surface` pointer (`Surface* surface = &all_surfaces[surface_idx]`) is not refreshed after `push_back` — if reallocation occurs, `surface` is a dangling pointer | PerimeterGenerator.cpp:process_no_bridge-all_surfaces-push_back-dangling-surface-pointer | High | P1 |
| 1080 | `process_no_bridge()` contains a commented-out block that would check whether a bridged hole would be naturally covered by the upper layer. The check is disabled with no fallback. Open (uncovered) holes and holes that would be roofed by the next layer are treated identically — both receive bridge infill even when the upper layer makes bridging unnecessary | PerimeterGenerator.cpp:process_no_bridge-disabled-upper_slices-check | Low | P3 |
| 1081 | `process_no_bridge()` uses a convexity gate (`contour_simplified.size() == 1 && concave_points().size() == 0`) to qualify holes for chbFilled bridging. Any counterbore hole whose simplified contour produces more than one polygon OR has any concave vertex is silently excluded. STL tessellation of circular holes commonly produces slightly non-convex polygon chains, so many real counterbore holes never receive bridge infill | PerimeterGenerator.cpp:process_no_bridge-convexity-gate-excludes-tessellated-counterbores | Medium | P2 |
| 1082 | `findAllTouchingPerimeters()` has O(|referenceIndices| × |entities|) cost per call. `reorderPerimetersByProximity()` calls it once per BFS level (incrementing `currentInsetIndex` each time) and there can be up to max_inset_idx levels. For an island with N Arachne extrusions and D depth levels, total cost is O(D × N²). For complex islands with many variable-width segments this accumulates significantly within a single layer | PerimeterGenerator.cpp:findAllTouchingPerimeters-O(n^2)-per-level-accumulates-across-depths | Medium | P2 |
| 1083 | `findAllTouchingPerimeters()` unconditionally skips any entity with `extrusion->inset_idx == 0`, with the comment "Ignore inset index 0 (external) perimeters from the re-ordering". This means outer-wall extrusions are never included in the proximity results, so they are never moved from their original position by `reorderPerimetersByProximity()`. The outer-wall ordering is independently controlled by `bringContoursToFront()` | PerimeterGenerator.cpp:findAllTouchingPerimeters-unconditionally-skips-inset_idx-0 | Medium | P2 |
| 1084 | `reorderPerimetersByProximity()` contains a comment "Bring the largest first level perimeter to the front" but the code calls `std::iter_swap(maxIt, firstLevelTouchingIndices.end() - 1)` — swapping the largest element to the **last** position (end-1), not the front. The largest first-level neighbour is therefore printed **last** among the first-level group, not first. This is either an intentional inverted heuristic or a bug | PerimeterGenerator.cpp:reorderPerimetersByProximity-bring-largest-to-front-comment-vs-end-1 | Low | P3 |
| 1085 | `bringContoursToFront()` is called from `process_arachne()` to move outer-wall contours (inset_idx==0, is_contour()==true) to the front of the extrusion list. This is the correct behaviour for OuterInner wall ordering. However, the call site in `process_arachne()` is not guarded by a wall_sequence check — when `wall_sequence == InnerOuter` (is_outer_wall_first==false), bringing contours to the front contradicts the intended inner-first ordering | PerimeterGenerator.cpp:bringContoursToFront-called-regardless-of-wall_sequence | Medium | P2 |
| 1086 | `generate_lower_polygons_series()` calls `offset_series.reserve(2)` and then two `push_back()` calls (always producing exactly 2 entries). The subsequent `for (int i = 0; i < offset_series.size(); i++)` loop is correct. However, the coupling between the hardcoded `reserve(2)` / two push_backs and the loop is implicit. Any future change that adds a third sample point must update both the push_back calls and any callers that index the result by position [0] and [1] | PerimeterGenerator.cpp:generate_lower_polygons_series-reserve(2)-size-coupling-fragility | Low | P3 |

## Session 75 — PrintObject.cpp Full Annotation (H1087–H1099)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1087 | `_transform_hole_to_polyholes()` iterates `layer.regions()` and calls `create_polyholes()` on each region. The code stores a raw `Layer*` from `m_layers` that is also being traversed. If `create_polyholes()` triggers a resize of `m_layers` (it should not, but no assert enforces this), the raw pointer becomes dangling. Additionally the phase uses the same `Layer*` across the inner region loop — any code change that makes region processing add layers would create aliasing bugs silently | PrintObject.cpp:_transform_hole_to_polyholes-raw-pointer-phase-aliasing-risk | Medium | P2 |
| 1088 | `detect_extruder_geometric_unprintables()` applies the area transform using only `m_instances.front().shift` — the shift of the first instance. For multi-instance objects, all copies share the same object geometry but their real positions on the plate differ. The detection result (whether a region is printable) is computed only for instance 0 and then applied to all instances. For objects placed near the plate boundary where printability depends on position, other instances may be misclassified | PrintObject.cpp:detect_extruder_geometric_unprintables-first-instance-only-shift | Medium | P2 |
| 1089 | `invalidate_state_by_config_options()` is a ~270-line hand-maintained switch statement mapping config option keys to the slicing steps they invalidate. Any new config key added to PrintObjectConfig or PrintRegionConfig that affects slicing output MUST also be added to this switch, or the corresponding slicing step will silently use stale cached output. There is no compile-time enforcement of this mapping — omissions produce incorrect incremental slicing with no error | PrintObject.cpp:invalidate_state_by_config_options-hand-maintained-mapping-silent-stale-output | High | P1 |
| 1090 | `detect_surfaces_type()` introduces the Orca-specific `stInternalAfterExternalBridge` interim surface type during detection. This type is immediately reclassified to `stInternal` in a second pass within the same function. If the reclassification pass is ever removed (e.g. conditionally disabled), `stInternalAfterExternalBridge` surfaces would propagate into downstream steps that only recognise the standard `SurfaceType` enum values — causing silent incorrect fill-surface classification | PrintObject.cpp:detect_surfaces_type-stInternalAfterExternalBridge-interim-type-unchecked-propagation | Medium | P2 |
| 1091 | `discover_vertical_shells()` uses `offset2(polygons, -expand, +expand)` (opening) to regularise thin regions. When `expand` is larger than the half-width of a thin wall, the opening silently deletes that wall from the vertical shell computation. The result is a hole in the top/bottom shell coverage for thin features. No warning is emitted | PrintObject.cpp:discover_vertical_shells-offset2-silently-deletes-thin-walls | Medium | P2 |
| 1092 | `bridge_over_infill()` saves lightning infill tree data via `std::move` before processing a layer, then restores it via `std::move` after. If an exception is thrown between the two moves (e.g. from a Clipper operation inside the processing), the lightning data is moved out but never moved back — `fill_surfaces` for that layer is left in a valid-but-empty state. With C++ exceptions disabled this cannot happen, but the pattern is fragile if exception handling is re-enabled | PrintObject.cpp:bridge_over_infill-lightning-move-exception-leaves-fill_surfaces-empty | High | P1 |
| 1093 | `bridge_over_infill()` constructs `CandidateSurface` structs that hold a raw `const Surface*` pointer into a `LayerRegion::fill_surfaces` vector. Immediately after building this list, the code calls `fill_surfaces.remove_types()` and `fill_surfaces.append()` on the same vector. These mutations may trigger vector reallocation, which invalidates all raw pointers stored in the `CandidateSurface` list — creating dangling pointer access on any subsequent use of those pointers | PrintObject.cpp:bridge_over_infill-CandidateSurface-raw-pointer-dangling-on-fill_surfaces-realloc | Medium | P2 |
| 1094 | `discover_horizontal_shells()` uses a `goto EXTERNAL` label (at the bottom of the outer layer loop) to skip expensive shell-promotion work for layers that need no changes. In a refactored port this must be converted to a `break` / `continue` with equivalent control flow, or a boolean flag. Accidentally omitting the goto semantics would cause all layers to undergo full shell promotion even when no changes are needed, corrupting the surface classification | PrintObject.cpp:discover_horizontal_shells-goto-EXTERNAL-must-be-ported-explicitly | Medium | P2 |
| 1095 | `combine_infill()` encodes combined-infill height into `Surface::thickness` and the number of combined layers into `Surface::thickness_layers`. These same fields are used elsewhere in the codebase to encode bridging extrusion height and layer count. A refactoring tool that treats `thickness` as a simple geometric measurement may conflate the combined-infill height encoding with the bridge-layer encoding, producing incorrect extrusion widths for either feature | PrintObject.cpp:combine_infill-Surface-thickness-dual-use-encoding | Medium | P2 |
| 1096 | `update_layer_height_profile()` has a comment noting "the assignment is crashing on ASAN OsX" and uses a vector constructor copy (`std::vector<coordf_t>(model_object.layer_height_profile.get())`) instead of a direct assignment to work around the crash. This ASAN-detected issue was never resolved — the root cause (likely an ownership or aliasing issue in `layer_height_profile.get()`) is masked by the workaround. A port must investigate whether the issue is in the ObservableBuffer or ModelObject ownership chain | PrintObject.cpp:update_layer_height_profile-ASAN-workaround-hides-ownership-aliasing-issue | Medium | P2 |
| 1097 | `remove_bridges_from_contacts<T>()` dispatches on `typeid(overhang_regions)` at runtime to down-cast the template parameter `PolysType*` to either `Polygons*` or `ExPolygons*` and perform type-specific diff operations. This `typeid()` dispatch is fragile: adding a new `PolysType` specialisation without a matching `typeid` branch silently falls through with no diff applied to the overhang regions. The pattern should be replaced with template specialisation or a `std::variant`-based dispatch | PrintObject.cpp:remove_bridges_from_contacts-typeid-runtime-dispatch-fragile | Medium | P2 |
| 1098 | `is_support_necessary()` constructs a full `TreeSupport` object, forces `support_type = stTreeAuto`, runs a complete `detect_overhangs()` pass, then calls `clear_support_layers()` — discarding any previously computed support layers as a side effect. This function is called from the UI support-advisor background thread to show a "support needed" icon. Calling it while a slicing pass is in progress (which also uses support layers) introduces a race on `m_support_layers` | PrintObject.cpp:is_support_necessary-expensive-side-effect-clears-support-layers | Medium | P2 |
| 1099 | `get_layer_at_bottomz()` searches for a layer by `bottom_z` using a linear scan (`for` over `m_layers`). All other layer-lookup functions use `lower_bound_by_predicate()` (binary search, O(log n)). For objects with many layers (e.g. high-resolution prints with thousands of layers), this function is O(n). It is called from support material generation; the performance difference is unlikely to be noticed at current layer counts but becomes a concern for very tall fine-resolution objects | PrintObject.cpp:get_layer_at_bottomz-linear-O(n)-scan-where-binary-search-possible | Low | P3 |

## Session 76 — PrintConfig.hpp Structured-Tag Pass (H690–H708)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 690 | `NozzleTypeEumnToStr` and `NozzleTypeStrToEumn` are both declared `static` (internal linkage) in a header. Every translation unit that includes `PrintConfig.hpp` gets its own independent copy of these maps. This is ODR-safe but wastes memory proportional to the number of TUs. Additionally, both names contain the typo "Eumn" instead of "Enum" — any port must choose whether to perpetuate or correct the name | PrintConfig.hpp:NozzleTypeEumnToStr-NozzleTypeStrToEumn-static-header-per-TU-copy-typo | Low | P3 |
| 691 | `SupportType::is_tree()` constructs a `std::set<SupportType>` on every call to check membership. This function is called on every region during perimeter generation to decide whether to use tree support geometry. On a print with many objects and many layers, this is called thousands of times per slice — each call allocates, populates, and destroys a heap-allocated set. The set should be replaced by an inline boolean check | PrintConfig.hpp:SupportType-is_tree-constructs-set-on-every-call-hot-path-alloc | Medium | P2 |
| 692 | `SupportType::is_auto()` constructs a `std::set<SupportType>` on every call, with the same hot-path allocation hazard as H691. Called alongside `is_tree()` in the same perimeter decision points | PrintConfig.hpp:SupportType-is_auto-constructs-set-on-every-call-hot-path-alloc | Medium | P2 |
| 693 | `bed_type_to_gcode_string()` is defined as a `static` function in the header. Every TU gets its own copy — same pattern as H690. The function is simple (switch over enum) and should be moved to a `.cpp` file or made `inline` | PrintConfig.hpp:bed_type_to_gcode_string-static-function-in-header-per-TU-copy | Low | P3 |
| 694 | `DynamicPrintConfig::validate()` returns a `std::string` containing error messages, but callers are not required by the type system to check the return value. Many call sites call `validate()` and discard the result. If a config loaded from a file contains invalid values, slicing proceeds silently with the invalid config | PrintConfig.hpp:validate-returns-error-callers-may-silently-ignore | Medium | P2 |
| 695 | `StaticPrintConfig::optptr()` resolves a config key to a field pointer using `reinterpret_cast<void*>((char*)this + offset)`, where `offset` is baked into a static table. This is valid only for simple single-inheritance flat struct layouts. If `StaticPrintConfig` is ever given a virtual function or mixed into a multiple-inheritance hierarchy with non-trivial layout, the baked offsets become wrong and `optptr()` returns a pointer to arbitrary memory | PrintConfig.hpp:optptr-reinterpret_cast-byte-offset-breaks-under-virtual-or-multi-inheritance | High | P1 |
| 696 | Several acceleration/jerk config fields were migrated from `PrintConfig` to `PrintObjectConfig` in a refactoring. The `handle_legacy()` function must correctly reroute old project-file keys to the new locations. If any key was missed or misnamed in `handle_legacy()`, loading an old project file silently zeros the field — producing a print with incorrect acceleration or jerk settings with no diagnostic | PrintConfig.hpp:acceleration-jerk-migration-PrintConfig-to-PrintObjectConfig-handle_legacy-silent-zero | Medium | P2 |
| 697 | Several `filament_ironing_*` options (e.g. `filament_ironing_speed`) are nullable and indexed by filament slot. The resolution logic — checking whether the per-filament override is set, then falling back to the object-level ironing speed — must be replicated exactly in any port. A port that reads only the non-nullable path will silently use the wrong ironing parameters for all filaments that have a per-slot override | PrintConfig.hpp:filament_ironing-nullable-slot-indexed-resolution-must-be-replicated-exactly | Medium | P2 |
| 698 | `adaptive_pressure_advance_model` is a `ConfigOptionStrings` (vector of strings) where each string is a JSON object or mathematical expression describing a pressure advance model. There is no schema validation at load time — any malformed string silently produces a model that evaluates to 0 or crashes the expression parser. A port must implement the same lazy-parse-at-use pattern or add upfront validation | PrintConfig.hpp:adaptive_pressure_advance_model-no-schema-validation-at-load | Medium | P2 |
| 699 | `flush_volumes_matrix` is a `ConfigOptionFloats` (flat `std::vector<float>`) encoding an N×N purge volume matrix, where N is the number of filaments. N is reconstructed at use time by `sqrt(values.size())`. If `values.size()` is not a perfect square (e.g. a filament was added without resizing the matrix), `sqrt()` returns a non-integer, which is silently truncated to the wrong N. All subsequent purge volume lookups use incorrect indices with no error | PrintConfig.hpp:flush_volumes_matrix-flat-array-N-from-sqrt-non-square-silent-mis-index | High | P1 |
| 700 | `flush_volumes_matrix` and `flush_volumes_vector` live in `GCodeConfig`, while `wipe_tower_*` parameters live in `PrintConfig` (a derived class). The wipe tower planner (`WipeTower2`) needs both sets. The split means they travel different code paths to reach the planner — any refactoring that extracts the planner into its own context must ensure both sets are passed together | PrintConfig.hpp:flush_volumes-in-GCodeConfig-wipe_tower-in-PrintConfig-split-must-reach-planner | Medium | P2 |
| 701 | `wipe_tower_x` and `wipe_tower_y` are `ConfigOptionFloats` (vectors), indexed by plate number. If the number of plates changes (a plate is added or removed) without resizing these vectors, the plate-to-position mapping silently shifts by one — every plate after the insertion/deletion point gets the wrong tower position. There is no runtime check for vector length vs plate count | PrintConfig.hpp:wipe_tower_x-y-plate-indexed-vectors-no-length-vs-plate-count-check | Medium | P2 |
| 702 | `FullPrintConfig` uses diamond inheritance from `PrintConfig`, `PrintObjectConfig`, `PrintRegionConfig`, `GCodeConfig`, and `MachineEnvelopeConfig`. If two of these base classes define an option with the same key string, `FullPrintConfig` contains two distinct fields for that key. The `optptr()` offset table has a debug-only `assert` that fires on key collisions; in Release builds the collision silently returns the offset from whichever base registered last — producing wrong option values with no diagnostic | PrintConfig.hpp:FullPrintConfig-diamond-inheritance-key-collision-debug-only-assert | High | P1 |
| 703 | `ModelConfig::assign_config()` short-circuits the assignment when the source config's `m_timestamp` matches the destination's `m_timestamp`, under the assumption that equal timestamps imply equal content. This assumption fails when two configs are independently modified to the same value (producing matching content but different timestamps) — or in the inverse case, when timestamps are reset without content changes. Silent wrong config is possible if timestamp management is ever loosened | PrintConfig.hpp:ModelConfig-assign_config-timestamp-short-circuit-silent-wrong-config | Medium | P2 |
| 704 | `get_flush_volumes_matrix(size_t extruder_id = size_t(-1))` uses `size_t(-1)` (which wraps to `SIZE_MAX` = 18446744073709551615 on 64-bit) as a sentinel meaning "return full matrix". The comparison `if (extruder_id == size_t(-1))` in the implementation is correct. However, any caller that passes an `int` value of `-1` without explicit cast silently passes SIZE_MAX by implicit conversion — which still works correctly only because the sentinel check happens first | PrintConfig.hpp:get_flush_volumes_matrix-extruder_id-size_t-minus1-sentinel-implicit-cast | Medium | P2 |
| 705 | `set_flush_volumes_matrix()` declares a local variable `bool is_multi_extruder` but never reads or uses it after assignment. The variable is dead code — a likely leftover from an earlier implementation where it gated a code path. It adds noise but has no functional effect | PrintConfig.hpp:set_flush_volumes_matrix-is_multi_extruder-declared-but-never-used | Low | P3 |
| 706 | Config options are serialised to cereal by ordinal (integer index into the registration order), not by key string. If a new option is inserted between two existing ones, all subsequent options get shifted ordinals. Any project file saved before the insertion maps its stored ordinals to the wrong options after the change — silently loading wrong values into every option from the insertion point onward | PrintConfig.hpp:cereal-ordinal-serialisation-insertion-shifts-all-subsequent-ordinals | High | P1 |
| 707 | The cereal `load()` function contains `assert(opt != nullptr)` when looking up an option by ordinal from a saved project file. In Debug builds this fires and reports the issue. In Release builds (`NDEBUG` defined), the assert is compiled out and the immediately following `opt->load(ar)` dereferences null — undefined behaviour that typically crashes. Any project file saved by a newer OrcaSlicer version and loaded by an older one will crash in Release | PrintConfig.hpp:cereal-load-null-ptr-deref-on-unknown-ordinal-debug-only-assert | High | P1 |
| 708 | `MachineEnvelopeConfig` exposes `resonance_avoidance_freq_start` and `resonance_avoidance_freq_end` as independent float options with no cross-validation enforcing `start <= end`. In `CoolingBuffer`, the avoidance range is tested as `freq >= start && freq <= end`. When `start > end` (user misconfiguration or a loaded preset with swapped values), this condition is always false — resonance avoidance is silently disabled entirely with no warning | PrintConfig.hpp:MachineEnvelopeConfig-resonance-band-start-gt-end-silent-disable | Medium | P2 |

## Session 76 (continued) — Brim.cpp Structured-Tag Pass (H749–H760)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 749 | `make_brim()` accumulates a global `brim_area` polygon across all objects and, for each object, computes its brim as the complement `brim_object_area - brim_area`. The brim for object N is therefore `brim_object_area[N] minus all brim already assigned to objects 0..N-1`. This ordering dependency means later objects in the list receive less brim (up to none) when earlier objects' brims overlap the same region. Re-ordering objects in the UI changes which objects get full brim coverage | Brim.cpp:make_brim-per-instance-diff-against-global-accumulator-ordering-dependency | Medium | P2 |
| 750 | `get_adhesion_coeff_for_support()` iterates all volumes and all extruders, storing a `coeff` that is overwritten on every match. Only the last matching `(volume, extruder)` pair's coefficient is returned. For objects with multiple volumes or multiple extruders whose adhesion coefficients differ, the returned value is implementation-defined (dependent on iteration order) rather than the maximum or minimum | Brim.cpp:get_adhesion_coeff_for_support-last-match-wins-multi-volume-multi-extruder | Medium | P2 |
| 751 | In `get_adhesion_coeff_for_support()`, there is a block of code that returns `adhesionCoeff` immediately after computing it, followed by ~20 lines of logic that checks filament names against a hardcoded list. The `return` on the path that finds a coefficient makes the filament-name check unreachable on the normal code path. This is confirmed dead code | Brim.cpp:get_adhesion_coeff_for_support-dead-code-after-return | Low | P3 |
| 752 | `compSecondMoment()` computes the second moment of area using Clipper scaled integer coordinates. The returned values are in `(scaled_units)^4`. Callers must apply `SCALING_FACTOR^4` (i.e. `1e-24`) to convert to mm^4. This scaling requirement is not documented at either the call site or the function definition — a port that omits the conversion will compute brim widths that are off by a factor of 10^24 | Brim.cpp:compSecondMoment-returns-scaled-coords-4th-power-undocumented | High | P1 |
| 753 | When `Ixx` or `Iyy` (second moments of area) are near-zero (degenerate polygon — e.g. a needle-thin object), the brim width formula divides by `Ixx` or `Iyy`, producing an extremely large brim width. This is capped at 18 mm by a hardcoded limit. The cap prevents catastrophic output but the root cause (degenerate polygon) is not reported | Brim.cpp:compSecondMoment-near-zero-Ixx-Iyy-division-produces-extreme-width-capped-18mm | Medium | P2 |
| 754 | The formula `height_to_area = speed * 1920` appears in `get_adhesion_coeff_for_support()`. The constant `1920` is not documented. It appears to encode a time-to-area conversion that depends on an implicit assumption about the relationship between print speed and extrusion area. If speed units or the extrusion width model ever change, this constant silently becomes incorrect | Brim.cpp:get_adhesion_coeff-magic-constant-1920-undocumented-speed-to-area | Medium | P2 |
| 755 | `connect_brim_lines()` uses a connectivity filter that computes the number of shared endpoints between brim lines. For very small isolated objects (whose brim lines are shorter than the connection threshold), the filter may classify the entire brim as "unconnected" and zero it out — producing no brim at all for the smallest objects, the ones that most need adhesion assistance | Brim.cpp:connect_brim_lines-connectivity-filter-may-zero-small-object-brim | Medium | P2 |
| 756 | The support brim code path in `make_brim()` contains multiple large blocks of commented-out logic (approximately 80 lines) that were the original support-brim implementation. The active replacement is much simpler. The `supportBrimAreaMap` that feeds the support brim path may always be empty (if the commented-out population code was never replaced), meaning the support brim feature may be silently no-op for all prints | Brim.cpp:support-brim-path-commented-out-supportBrimAreaMap-may-always-be-empty | Medium | P2 |
| 757 | `tryExPolygonOffset()` calls `offset_ex()` in a loop, incrementing the offset magnitude by `delta` on each iteration until a non-empty result is obtained. There is no maximum iteration count. For a pathological polygon (e.g. a perfectly regular star shape where all offsets up to a large magnitude produce empty results), this loop runs indefinitely — a potential infinite loop in production | Brim.cpp:tryExPolygonOffset-no-max-iteration-cap-potential-infinite-loop | High | P1 |
| 758 | In `make_brim()`, the "robust offset" strategy applies a negative offset of `-1.3 * spacing` followed by a positive offset of `+0.3 * spacing` (net: `-1.0 * spacing`) to avoid polygon self-intersection. The split ratio `1.3/0.3` is an undocumented magic number. A port that applies only the net `-1.0 * spacing` offset will produce different (and likely more self-intersecting) results | Brim.cpp:robust-offset-1.3-0.3-split-undocumented-magic-ratio | Medium | P2 |
| 759 | The plate offset is applied to brim polygons after `connect_brim_lines()`. If the call order is ever changed (e.g. connect_brim_lines is moved after plate offset application), the brim would be offset twice — once by plate offset and once again inside connect_brim_lines which also applies a plate-relative transform | Brim.cpp:plate-offset-applied-after-connect_brim_lines-double-offset-risk | Medium | P2 |
| 760 | `make_brim()` calls `const_cast<PrintObject*>(obj)->firstLayerObjectBrimBoundingBox = ...` to store the computed brim bounding box back into the `PrintObject`. This write bypasses const correctness and is not protected by any mutex. If `make_brim()` is ever called from multiple threads for different plates simultaneously (possible in multi-plate parallel export), this produces a data race on `firstLayerObjectBrimBoundingBox` | Brim.cpp:const_cast-firstLayerObjectBrimBoundingBox-unsynchronized-write | Medium | P2 |

## Session 77 — Print.cpp Full Structured-Tag Pass (H1100–H1102)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1100 | Lines 707–809 of `Print.cpp` contain a large `#if 0` block (~100 lines) that implements an automatic topological sort algorithm for sequential print ordering. The block is entirely dead code — the active code path simply uses the object's UI list order. The dead block contains Chinese developer comments explaining the intended heuristic, indicating an abandoned experimental feature. A porting tool must not translate this block as live logic; it should be deleted or moved to a design document | Print.cpp:sequential_print-if0-dead-topological-sort-block-do-not-port | Medium | P2 |
| 1101 | `Print::wipe_tower_data(size_t filaments_cnt)` is declared `const` but uses `const_cast<Print*>(this)->m_wipe_tower_data.depth = ...` and `...brim_width = ...` to lazily compute and cache depth/brim estimates. This side-effecting const method is not thread-safe: if two threads call `wipe_tower_data()` before `psWipeTower` has completed (e.g. one from UI preview and one from a background validation check), both may enter the lazy-init branch simultaneously and produce a data race on `m_wipe_tower_data` | Print.cpp:wipe_tower_data-const-method-uses-const_cast-to-mutate-not-thread-safe | High | P1 |
| 1102 | `Print::get_filament_temp_type()` stores a `static std::unordered_map` that is lazily initialized from `resources/info/filament_info.json` on the first call. The initialization check is `if (filament_temp_type_map.empty())` with no mutex. If two threads call `get_filament_temp_type()` for the first time simultaneously (possible in the TBB-parallelised support-generation loop), the map is populated twice, producing a data race on the static variable. All calls after first initialization are safe | Print.cpp:get_filament_temp_type-static-map-lazy-init-no-mutex-data-race-on-first-call | High | P1 |

## Session 79 — TriangleSelector.cpp Structured-Tag Pass (H1103–H1107)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1103 | `TriangleSelector`'s free-list for unused triangle/vertex slots is encoded by storing a raw `int` (the next-free index) directly into the `Vec3f::v[0]` float storage of a `Vertex`, and into `Triangle::verts_idxs[0]` int storage. The `Vertex` case uses `memcpy` to alias `int` bits into a `float` field — strictly undefined behaviour in C++ (violates the strict aliasing rule). It works universally in practice on IEEE 754 platforms, but a port must replace this with a proper union or a separate free-list vector to avoid UB | TriangleSelector.cpp:free-list-memcpy-int-into-float-field-strict-aliasing-UB | High | P1 |
| 1104 | In `TriangleSelector::perform_split()`, the function takes a `Triangle &tr` reference into `m_triangles`, then calls `push_triangle()` up to 3 times to create child triangles. Each `push_triangle()` may trigger `m_triangles.reserve()` / reallocation if the vector is at capacity. A reallocation invalidates all existing references and pointers into the vector — including the `tr` reference that `perform_split()` continues to read after the call. The function guards against this with a `m_triangles.reserve(m_triangles.size() + 3)` call at the top of `perform_split()`, making the actual allocations no-ops. Any port that removes or forgets this reserve call turns the remaining `tr` reads into use-after-realloc undefined behaviour | TriangleSelector.cpp:perform_split-ref-invalidated-by-push_triangle-reserve-contract | High | P1 |
| 1105 | `TriangleSelector::serialize()` and `deserialize()` write/read a 4-bit nibble bitstream with no version header or schema identifier. The serialized format is also deliberately compatible with PrusaSlicer 2.3.1's format (children are stored in reverse order specifically for that compatibility). Any change to the subdivision depth, the nibble encoding, or the child ordering will silently corrupt all project files saved by older versions. A port must preserve the exact nibble layout, child reversal, and bit-packing or bump a version field | TriangleSelector.cpp:serialize-no-version-field-PrusaSlicer-2.3.1-compat-child-reversal | High | P1 |
| 1106 | `TriangleSelector::HeightRange::is_pointer_in_triangle()` is a stub that always returns `false`. This means the `HeightRange` cursor never reports a direct triangle hit. The calling code in `select_patch()` falls back to an O(N) brute-force scan of all original triangles on every cursor query (N = number of original mesh triangles). For meshes with tens of thousands of triangles and repeated cursor movements, this produces a severe performance cliff. A port must either implement a real point-in-triangle test or replace the fallback with a spatial index | TriangleSelector.cpp:HeightRange-is_pointer_in_triangle-always-false-O-N-brute-force | High | P1 |
| 1107 | Within the `HeightRange` cursor path in `select_patch()`, the edge-splitting threshold (`edge_limit`) is hardcoded to `0.1` regardless of the cursor's actual radius or the mesh's triangle size. For a cursor with radius 5 mm, triangles up to 50× larger than the cursor are accepted without subdivision, producing coarse and geometrically inaccurate height-range selections. For a cursor with radius 0.01 mm, `edge_limit = 0.1` forces subdivision of triangles far larger than the cursor with no benefit. The value should scale with cursor size | TriangleSelector.cpp:HeightRange-edge_limit-hardcoded-0.1-ignores-cursor-radius | Medium | P2 |

## Session 80 — PresetBundle.cpp Full Structured-Tag Pass (H1108–H1117)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1108 | `PresetBundle::construct_full_config()` (static, for external callers) and `PresetBundle::full_fff_config()` (instance method, for the GUI editor) share approximately 80% identical multi-filament merge logic. Both flatten printer + print + per-slot filament configs + project_config into a single `DynamicPrintConfig`. Any change to the merge algorithm (e.g. how vector options are concatenated for multi-filament, how `filament_map` indices are handled, how `apply_extruder` dispatch works) must be mirrored manually in both functions. There is no shared helper — the duplication is structural. Port as a single unified function with a mode enum or a thin wrapper that delegates to it | PresetBundle.cpp:construct_full_config-full_fff_config-~80pct-duplicated-merge-logic | High | P1 |
| 1109 | `PresetBundle::get_required_hrc_by_filament_type()` contains a `static std::unordered_map<string,int>` that is lazily initialised on the first call by scanning all BBL vendor filament presets. The initialization guard is `if (filament_type_to_hrc.empty())` with no mutex. This is the same data-race pattern as H1102 (`Print::get_filament_temp_type()`): two threads entering the function simultaneously with an empty map both populate it concurrently, producing a data race on the static variable. Identical fix applies: wrap the initialisation block in `std::call_once` or a mutex-protected flag | PresetBundle.cpp:get_required_hrc_by_filament_type-static-map-lazy-init-no-mutex-data-race | High | P1 |
| 1110 | `PresetBundle::update_system_preset_setting_ids()` contains three consecutive `if` branches (lines 1266–1271) all checking for `PRESET_IOT_PRINTER_TYPE`. The intent was to also handle `PRESET_IOT_FILAMENT_TYPE` and `PRESET_IOT_PRINT_TYPE`, but each branch was copy-pasted and the condition was never updated. The filament and print type branches are dead code — they can never be reached, because the preceding printer-type branch matches first. Only printer preset setting IDs are ever updated by this function; filament and print IDs are silently dropped | PresetBundle.cpp:update_system_preset_setting_ids-three-if-branches-all-check-printer-type-filament-and-print-dead | Medium | P2 |
| 1111 | `PresetBundle::load_selections()` and `PresetBundle::update_selections()` contain nearly-identical serialization/deserialization blocks for `flush_volumes_matrix`, `flush_volumes_vector`, `flush_multiplier`, `filament_colours`, and related per-project fields. Any new per-project option must be added to both functions manually. Because neither function calls the other and there is no shared helper, a missing entry in either function causes silent data loss on save or silent default-value substitution on load | PresetBundle.cpp:load_selections-update_selections-duplicated-per-project-config-blocks | Medium | P2 |
| 1112 | `PresetBundle::sync_ams_list()` is a ~400-line function implementing three mutually exclusive AMS reconciliation modes controlled by two bool flags (`use_map`, `color_only`). The effective flag matrix has four combinations (FF, TF, FT, TT) of which only three are well-tested (the fourth — `use_map=true, color_only=true` simultaneously — has no documented semantics). High cyclomatic complexity makes the function difficult to reason about or test systematically. Port should split into three clearly named functions: `sync_ams_by_color()`, `sync_ams_by_map()`, `sync_ams_wholesale()` | PresetBundle.cpp:sync_ams_list-three-modes-in-one-function-via-bool-flags-high-cyclomatic | Medium | P2 |
| 1113 | `PresetBundle::sync_ams_list()` uses the bitmask `0x10000` (= 65536) to encode left/right extruder affinity for the Bambu H2D dual-extruder machine in `filament_ams_list` keys. There is no named constant or enum for this magic number — it is used directly in bit-tests (`key & 0x10000` vs `key & ~0x10000`). A port reading only the integer comparison without understanding the bitmask encoding will misroute AMS slot assignments for dual-extruder machines | PresetBundle.cpp:sync_ams_list-0x10000-bitmask-magic-constant-left-right-extruder-H2D | Medium | P2 |
| 1114 | The file-scope `static std::map<string, map<string, string>> filament_preset_convert` (around line 3597) encodes a hardcoded migration table for Bambu H2D filament preset renames (e.g. "Bambu PLA Basic @BBL H2D" → "Bambu PLA Basic @BBL H2D 0.6 nozzle"). This is BBL-specific business logic baked into the slicer core. Any new hardware variant that splits a filament preset requires a source code change. Port should instead read this mapping from a versioned migration JSON in the vendor bundle directory | PresetBundle.cpp:filament_preset_convert-static-global-BBL-H2D-migration-baked-into-core | Low | P3 |
| 1115 | `load_config_file_config()` (user preset load path, separate from `load_vendor_configs_from_json()`) contains a `#if 0` block covering the original `num_extruders` calculation that was replaced by a BBS `filament_colour`-based extruder count. The dead block is never compiled. A port must use only the filament_colour-based count; accidentally activating or mixing in the `#if 0` path produces a different extruder count that silently misconfigures all multi-material print jobs | PresetBundle.cpp:load_config_file_config-if0-num_extruders-dead-path | Low | P3 |
| 1116 | `update_multi_material_filament_presets()` has a `#if 0` block (labelled `// BBS`) that disables per-slot nozzle-diameter-based validation of `filament_presets[]` entries. The active replacement only checks slot count and resizes/reorders the flush matrix, but never verifies that each `filament_presets[i]` name still resolves to a valid, loaded preset. Slots referencing renamed or deleted presets accumulate silently. A port should implement the validation that this dead code intended | PresetBundle.cpp:update_multi_material_filament_presets-if0-disables-slot-preset-validity-check | Medium | P2 |
| 1117 | `update_compatible()` defines four local helper classes (`PreferedProfileMatch`, `PreferedPrintProfileMatch`, `PreferedFilamentProfileMatch`, `PreferedFilamentsProfileMatch`) inline as local structs inside the function body. The preset-selection scoring formula (alias match = INT_MAX > name match = 1, scaled by ×10 for layer height / filament type match, +1 for visible) is the only authoritative definition of selection priority but is buried in a 200-line function. A port must replicate this scoring exactly or change-of-printer will select subtly wrong presets | PresetBundle.cpp:update_compatible-local-struct-scoring-formula-buried-only-authoritative-copy | Medium | P2 |

## Session 81 — AppConfig.cpp Structured-Tag Pass (H1118–H1124)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1118 | `AppConfig::set_defaults()` is ~430 lines long and contains dozens of `#ifdef` guards for feature macros (`SUPPORT_AUTO_CENTER`, `SUPPORT_BACKGROUND_PROCESSING`, `SUPPORT_REMEMBER_OUTPUT_PATH`, `SUPPORT_DARK_MODE`, `SUPPORT_3D_CONNEXION`, `SUPPORT_SYS_MENU`, `ENABLE_ENVIRONMENT_MAP`) that are **never defined** in the OrcaSlicer build. The guarded blocks are always-dead code, interleaved with the live defaults in a single large function. A reader cannot determine which defaults actually apply without mechanically resolving every `#ifdef`. A port must strip the dead branches first and only implement the surviving defaults | AppConfig.cpp:set_defaults-430-lines-dead-ifdef-blocks-interleaved-with-live-defaults | Medium | P2 |
| 1119 | `appconfig_md5_hash_line()` uses `boost::uuids::detail::md5` to compute a file-integrity checksum appended to the config file on Windows. `boost::uuids::detail` is an **internal/unstable namespace** — Boost documentation explicitly states it may change across releases. The comment in the file acknowledges this (`"boost::uuids::detail::md5 is an internal namespace thus it may change in the future"`). A port should replace this with any stable MD5 or SHA-256 implementation (std, OpenSSL, or a single-file vendored hash) | AppConfig.cpp:appconfig_md5_hash_line-uses-boost-uuids-detail-md5-internal-unstable-namespace | Low | P3 |
| 1120 | `AppConfig::load()` has two entirely separate implementations: the active JSON path (`#ifdef USE_JSON_CONFIG`) using nlohmann::json, and the legacy INI path (`#else`) using boost::property_tree. The two have diverged significantly: the JSON path serializes `filament_presets`, `filament_colors`, `filament_multi_colors`, and `filament_color_types` as JSON arrays inside the `"app"` section; the INI path has no concept of these arrays and would silently lose all multi-filament preset data if `USE_JSON_CONFIG` were ever removed. The two paths share only the `set_defaults()` call at the end | AppConfig.cpp:load-two-diverged-implementations-JSON-active-INI-legacy-disabling-loses-multi-filament | High | P1 |
| 1121 | On Windows, `AppConfig::load()` strips the trailing MD5 checksum line by finding the last `}` in the buffer and splitting: `left_string = substr(0, last_pos+1)`, `right_string = substr(last_pos+2)`. The `+2` assumes exactly one `\n` separates `}` from the MD5 comment. If the file was saved with CRLF line endings (`\r\n`), `last_pos+2` points to the `\n` character rather than the start of the MD5 line — the MD5 comparison always fails and `left_string` includes a trailing `\r` that corrupts JSON parsing | AppConfig.cpp:load-WIN32-substr-last_pos+2-assumes-LF-only-CRLF-corrupts-JSON-parse | Medium | P2 |
| 1122 | `AppConfig::save()` serializes `m_filament_presets`, `m_filament_colors`, `m_filament_multi_colors`, and `m_filament_color_types` as arrays nested inside `j["app"]` (e.g. `j["app"]["filament_presets"]`). On load, these arrays are read back from inside the `"app"` JSON object via `iter.key() == "filament_presets"` branches. The round-trip is correct but fragile: if any future refactor moves these keys to the top level of the JSON document, `load()` would find them in the wrong branch and silently fall through to `BOOST_LOG_TRIVIAL(warning) << "load config warning..."` | AppConfig.cpp:filament_presets-colors-serialized-nested-in-app-section-fragile-round-trip | Medium | P2 |
| 1123 | `AppConfig::get_country_code()` maps the IoT region string to a 2-letter code for network API routing. The branch `else if (region == "Europe") return "US"` returns `"US"` for European users — this is almost certainly a copy-paste error (the preceding branch for `"USA"` also returns `"US"`). The correct return for Europe should be `"EU"` or `"Others"`. All European OrcaSlicer users who have region set to `"Europe"` are silently routed to the US Bambu cloud endpoint | AppConfig.cpp:get_country_code-Europe-maps-to-US-copy-paste-bug-wrong-endpoint | High | P1 |
| 1124 | `AppConfig::save_custom_color_to_config()` calls `get_section("custom_color_list")` which returns a `const std::map<std::string, std::string>&`. The code immediately applies `const_cast<std::map<...>*>(&data)` to obtain a mutable pointer and passes it to `set_colors()`. `get_section()` returns a const reference to internal storage, so the `const_cast` is technically removing const from a member field — this is UB if the original object is actually const (it is not in this context, so it works). The intent is simply a mutable copy; the `const_cast` pattern is misleading and should be replaced with a local `auto data = get_section(...)` (value copy) | AppConfig.cpp:save_custom_color_to_config-const_cast-on-get_section-return-misleading-pattern | Low | P3 |

## Session 81 — Measure.cpp Structured-Tag Pass (H1125–H1129)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1125 | `MeasuringImpl::update_planes()` walks plane border polygons using a TBB `parallel_for` lambda. Inside the lambda, when a broken mesh edge or infinite loop is detected, the code executes `goto PLANE_FAILURE` which jumps to a label that calls `planes[plane_id].borders.clear()`. The `goto` exits the inner `for` loops and continues the TBB range iteration. No error is logged and no flag is set — planes derived from non-manifold meshes silently produce zero borders, and thus zero detected features, with no diagnostic to the user or developer | Measure.cpp:update_planes-goto-PLANE_FAILURE-silently-clears-borders-no-error-log | Medium | P2 |
| 1126 | The `is_same_normal` lambda in `update_planes()` groups triangles into planes when `|a[i] - b[i]| < 0.001` for all three normal components. This per-component tolerance of 0.001 is hardcoded and has no documented basis. On curved surfaces or finely tessellated meshes, adjacent triangles whose normals differ by more than 0.001 per component each form their own plane — potentially producing thousands of tiny single-triangle planes. This degrades both performance (TBB loop over many planes) and feature-detection quality (no circle fit is possible on a single-triangle plane) | Measure.cpp:update_planes-is_same_normal-hardcoded-0.001-per-component-many-tiny-planes-on-curved | Low | P3 |
| 1127 | `extract_features()` is called lazily from `get_feature()` (GUI thread) the first time a plane is hovered. `update_planes()` runs a `tbb::parallel_for` on plane data in the constructor. Although in practice TBB finishes before any GUI hover event, there is **no synchronization barrier** between the constructor's TBB work and the first `get_feature()` call. If the `MeasuringImpl` object is constructed on a background thread and a GUI event fires before the constructor returns, `get_feature()` and `update_planes()`'s TBB workers may both access `m_planes[i].borders` simultaneously — a data race | Measure.cpp:extract_features-no-sync-barrier-between-TBB-update_planes-and-GUI-get_feature | Medium | P2 |
| 1128 | `Measuring::get_feature()` (the public facade, not `MeasuringImpl::get_feature()`) contains a hardcoded debug check: `if (face_idx == 7516 \|\| face_idx == 7517) { std::cout << ""; }`. This is a leftover from a specific mesh debugging session. The `std::cout << ""` is a no-op but the conditional is evaluated on every mouse hover event over the mesh, adding unnecessary overhead and constituting dead code noise | Measure.cpp:get_feature-debug-if-face_idx-7516-7517-leftover-dead-code-hits-every-hover | Low | P3 |
| 1129 | `get_measurement()` dispatches on all 10 type-combination pairs (Point×Point, Point×Edge, Point×Circle, Point×Plane, Edge×Edge, Edge×Circle, Edge×Plane, Circle×Circle, Circle×Plane, Plane×Plane). Not all combinations are fully implemented — notably `Point×Plane` only sets `distance_infinite` and explicitly leaves `distance_strict` as a TODO comment (`// TODO: result.distance_strict =`). The caller `get_feature()` checks `if (res.distance_strict)` before using the result; for `Point×Plane`, `distance_strict` is always null so the measurement is silently skipped. No error or warning is emitted. A port must audit all 10 combinations and either implement or explicitly discard each | Measure.cpp:get_measurement-Point-Plane-distance_strict-TODO-silently-skipped-no-error | Medium | P2 |

## Session 82 — ExtrusionSimulator.cpp, QuadricEdgeCollapse.cpp, utils.cpp, CutUtils.cpp (H1130–H1141)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1130 | `gcode_paint_layer()` reads `path.height` from each ExtrusionPath to determine extrusion cross-section dimensions. However, immediately after reading it, `height` is silently overridden to `0.5f` via a hardcoded constant. This means the actual layer height stored in the extrusion path is ignored — all simulated extrusion cross-sections use 0.5 mm height regardless of slicer settings. If the simulation is ever used for visual feedback or flux calculations, results will be incorrect for any layer height other than 0.5 mm | ExtrusionSimulator.cpp:gcode_paint_layer-height-hardcoded-0.5f-overrides-path.height | Medium | P2 |
| 1131 | `set_image_size()` initializes the simulation image by filling alternating 8-pixel columns with `(255,0,0)` (red). A `//FIXME` comment on that block says "fill with red" — this is clearly a debug initialization leftover. In production, the image background should be transparent or black. Any UI component that uses the simulation image before a full paint pass will display red vertical stripes | ExtrusionSimulator.cpp:set_image_size-debug-red-column-fill-FIXME-leftover | Low | P3 |
| 1132 | `gcode_spread_points()` contains a large `#if 0` block (~50 lines) implementing a span-based circle fill for excess material spreading. The active code uses a different accumulator approach. Additionally, `evaluate_accumulator()` has an `#if 1 / #else` block where the `#else` branch (an alternative decay formula) is permanently dead. A port must implement only the `#if 1` path for `evaluate_accumulator()` and ignore the `#if 0` block in `gcode_spread_points()` | ExtrusionSimulator.cpp:gcode_spread_points-dead-if0-span-circle-fill-evaluate_accumulator-dead-else | Low | P3 |
| 1133 | ExtrusionSimulator.cpp as a whole uses `Boost.Geometry` types (`boost::geometry::model::box`, `boost::geometry::model::d2::point_xy`) exclusively — no Eigen, no Clipper. This is the only production file in libslic3r that depends on Boost.Geometry for its core data model. A port that replaces or removes Boost.Geometry must handle ExtrusionSimulator separately from all other geometry code | ExtrusionSimulator.cpp:only-file-in-libslic3r-using-boost-geometry-as-primary-data-model | Low | P3 |
| 1134 | `TriangleInfo::is_deleted()` / `set_deleted()` encode triangle deletion by setting `n.x() > 2.f` — the same normal-field aliasing pattern used in TriangleSelector.cpp (H1103). A valid mesh triangle normal always has `|n.x()| ≤ 1.0`. Deleted status is encoded by writing `n.x() = 3.0f`. This is an implicit encoding contract not enforced by the type system. Any code path that re-normalizes triangle normals will silently resurrect deleted triangles | QuadricEdgeCollapse.cpp:TriangleInfo-is_deleted-encodes-deletion-in-nx-gt-2-normal-field-aliasing | High | P1 |
| 1135 | `change_neighbors()` is the most complex function in the file (~100 lines). It performs in-place recompaction of the `e_infos` edge array during a collapse: removes edges referencing the deleted vertex, updates all remaining edges that referenced either of the two collapsed vertices, and rebuilds neighbor links. The recompaction uses multiple `erase()+insert()` operations on a flat array, making the index arithmetic subtle. The function has no internal assertions on invariants. A port must be extremely careful about the order of erase vs insert operations or it will corrupt the edge topology | QuadricEdgeCollapse.cpp:change_neighbors-complex-erase-insert-recompaction-no-invariant-checks | High | P1 |
| 1136 | On Windows, the `copy_file()` fallback error path (when `CopyFileW` fails) constructs `error_message = "Error: " + errCode` where `errCode` is a `DWORD` (unsigned 32-bit integer). Adding an integer to a `std::string` in C++ invokes `string + char` (casting the DWORD to a char), not numeric-to-string conversion. The error message contains a garbage single character rather than the numeric error code. Diagnosis of copy failures on Windows is thus broken | utils.cpp:copy_file-WIN32-error_message-DWORD-concat-garbage-char-not-numeric-string | Medium | P2 |
| 1137 | `normalize_utf8_nfc()` stores `static std::locale locale_utf8` — a function-local static initialized on first call. In C++11, function-local statics are guaranteed to be initialized exactly once, thread-safely, by the standard. However, the `boost::locale::generator` call inside the initializer invokes ICU or system locale infrastructure. If that infrastructure itself is not re-entrant (e.g. on some Windows ICU builds), concurrent first calls can deadlock inside the Boost locale generator even though the static guard itself is safe | utils.cpp:normalize_utf8_nfc-static-locale-boost-locale-generator-ICU-reentrance-risk | Low | P3 |
| 1138 | `Cut::apply_tolerance()` applies a `z -= 0.05` offset to Plug and Snap connectors. This -0.05 mm magic constant is not documented — no comment explains whether it compensates for FDM layer resolution, connector clearance, or a specific slicer artifact. A port that uses a different unit system or layer resolution may need to adjust this value, but without documentation it is impossible to know the correct scaling | CutUtils.cpp:apply_tolerance-0.05mm-magic-Z-offset-for-Plug-Snap-undocumented | Medium | P2 |
| 1139 | `process_connector_cut()` calls `its_make_cylinder(1.0, 1.0, PI/180.)` for Snap connectors. The cylinder radius (1.0 mm), height (1.0 mm), and angular resolution (PI/180 = 360 faces) are all hardcoded magic constants. If snap connector geometry is ever adjusted for printer resolution or connector strength, all three values must be found and updated in this single call site | CutUtils.cpp:process_connector_cut-its_make_cylinder-hardcoded-radius-height-angular-resolution | Low | P3 |
| 1140 | `perform_by_contour()` contains explicit `delete ptr;` on raw `ModelVolume*` pointers extracted from a `std::vector` before calling `vector::erase()`. This pattern is fragile: if any code path between the pointer extraction and the `delete` throws an exception, the pointers leak. The correct pattern is to use `std::unique_ptr` or to rely on the owning container's destructor | CutUtils.cpp:perform_by_contour-manual-delete-raw-ModelVolume-ptr-before-erase-exception-unsafe | Medium | P2 |
| 1141 | In `post_process()`, the branch `if (lower_obj && cut_part == CutPart::Lower)` sets `flip = true` when `place_on_cut_lower` is the only flag set. This may be intentional (a standalone `PlaceOnCutLower` without `PlaceOnCutUpper` implies the lower piece should be flipped upright after cutting), but it is not documented. A port that naively copies the conditional logic without understanding the physical semantics may produce inverted lower objects | CutUtils.cpp:post_process-PlaceOnCutLower-alone-implies-flip-true-undocumented-physical-intent | Low | P3 |

## Session 83 — MutablePolygon.cpp, Triangulation.cpp, FlushVolPredictor.cpp, FilamentGroupUtils.cpp, Clipper2Utils.cpp, ModelArrange.cpp (H1142–H1154)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1142 | `Triangulation::triangulate(const Points&, const HalfEdges&)` checks all preconditions (sorted edges, no duplicates, no bidirectional pairs, unique points, no self-intersections) via `assert()` only. In release builds all checks are stripped. CGAL's CDT behavior with violated preconditions (duplicate spatial positions, crossing constraints) is undefined — it may hang, crash, or silently produce incorrect triangulation. There is no runtime guard for production use | Triangulation.cpp:triangulate-core-all-precondition-checks-are-assert-only-stripped-in-release | High | P1 |
| 1143 | `VISUALIZE_TRIANGULATION` is an `#ifdef` block containing hardcoded Windows absolute paths (`"C:/data/temp/triangulation_without_floodfill.obj"` and `"C:/data/temp/triangulation.obj"`). The macro is undefined in production but if ever enabled on a non-Windows build or a Windows machine without that exact path, the `its_write_obj` calls silently fail (file open fails) and no error is reported | Triangulation.cpp:VISUALIZE_TRIANGULATION-ifdef-hardcoded-Windows-absolute-paths-not-portable | Low | P3 |
| 1144 | `predictor_instances` in FlushVolPredictor.cpp is a file-scope `static std::unordered_map<int, FlushVolPredictor>`. `GenericFlushPredictor` constructor inserts into this map without any mutex. Two threads constructing `GenericFlushPredictor(0)` simultaneously will race on the map — this is the same lazy-init data-race pattern as H1109 (`PresetBundle::get_required_hrc_by_filament_type()`) and H1102 (`Print::get_filament_temp_type()`). Fix: `std::call_once` or a mutex-protected insert | FlushVolPredictor.cpp:predictor_instances-static-unordered_map-lazy-init-no-mutex-data-race | High | P1 |
| 1145 | `FlushVolPredictor::predict()` performs a linear scan through `m_colors` for the closest color match, returning the first color within the default threshold (8.0 ΔE). It does not find the *nearest* color — it finds the first color that passes the threshold, ordered by file load order. For input colors equidistant from two palette entries, the result depends on the order of entries in the flush data file. A port using a different data structure (k-d tree, sorted array) may produce different match results for ambiguous inputs | FlushVolPredictor.cpp:predict-linear-scan-first-within-threshold-not-nearest-neighbor-order-dependent | Low | P3 |
| 1146 | `build_full_machine_filaments()`, `build_machine_filaments()`, `calc_max_group_size()`, and `collect_unprintable_limits()` all hardcode the machine extruder group count as 2 via `std::vector<...>(2, ...)` or `resize(2)`. No named constant or configuration parameter controls this. A future 3+ extruder machine requires finding and updating all four sites. The `// add default value to 2` comment in `calc_max_group_size()` acknowledges the hardcoding but does not explain why 2 is correct | FilamentGroupUtils.cpp:hardcoded-2-extruder-group-count-in-four-separate-sites | Medium | P2 |
| 1147 | `build_full_machine_filaments()` detects external-tray filaments by checking `tray_name == "Ext"` (case-sensitive string literal). Any AMS slot whose `tray_name` field does not exactly match `"Ext"` is treated as a normal AMS slot. No enum or named constant documents this sentinel value. A port must replicate this exact byte-comparison; changing to case-insensitive or a different string will silently route external-filament slots to AMS assignment | FilamentGroupUtils.cpp:build_full_machine_filaments-tray_name-Ext-hardcoded-string-sentinel | Medium | P2 |
| 1148 | `calc_max_group_size()` has a `// TODO: add explanation` comment on the function itself, indicating the author was unsure of or did not document the function's semantics at time of writing. The accumulation `group_size[idx] += iter->first * iter->second` (AMS-unit-count × quantity) is intended to produce the maximum number of filament slots per extruder group but the formula's basis (what `iter->first` and `iter->second` represent in physical terms) is not documented anywhere in the file or its header | FilamentGroupUtils.cpp:calc_max_group_size-TODO-add-explanation-comment-undocumented-formula | Low | P3 |
| 1149 | `Paths64_to_polylines()` and the inline Point64→Point conversion at line ~47 both wrap trivially-copyable integer values in `std::move()`. For trivial types `std::move` has no effect but creates misleading code implying non-trivial move semantics. Both conversion functions carry `//BBS: FIXME` comments suggesting incomplete design. A port should remove the spurious `std::move` calls and clarify the conversion intent | Clipper2Utils.cpp:Paths64_to_polylines-spurious-std-move-on-trivial-types-BBS-FIXME-comments | Low | P3 |
| 1150 | `PolyTreeToExPolygons()` uses a recursive helper `PolyTreeToExPolygonsRecursive()`. Recursion depth equals the polygon nesting depth (polygons within holes within polygons, etc.). For deeply nested geometry — common in carved lattice models or imported SVG outlines — the recursion is unbounded and can cause a stack overflow. No depth limit or iterative fallback is implemented | Clipper2Utils.cpp:PolyTreeToExPolygons-recursive-helper-unbounded-recursion-depth-stack-overflow | Medium | P2 |
| 1151 | `offset2_ex_2()` calls `SimplifyPolyTree(polytree, SCALED_EPSILON, polytree2)` between the two offset passes. `SCALED_EPSILON` is 1e-3 mm in scaled integer coordinates — an extremely small simplification threshold that will simplify almost no edges in practice. The comment says "simplify the result" but there is no explanation of why SCALED_EPSILON was chosen or what minimum simplification is needed to prevent Clipper2 artifacts in the second offset pass | Clipper2Utils.cpp:offset2_ex_2-SimplifyPolyTree-SCALED_EPSILON-threshold-too-small-intent-unclear | Low | P3 |
| 1152 | `get_arrange_poly(const Model&)` accumulates all instance contour points into a shared `ap.poly.contour`, then in each loop iteration calls `ap.poly.contour.rotate(obj_ap.rotation)` and `ap.poly.contour.translate(...)` on the accumulator. After the first instance is processed, the previously appended points are re-transformed by each subsequent instance's rotation and translation. This almost certainly produces an incorrect convex hull — the intent was to transform each instance's contour before appending, not to re-transform the growing accumulator | ModelArrange.cpp:get_arrange_poly-Model-overload-re-rotates-accumulator-instead-of-per-instance-copy | High | P1 |
| 1153 | `get_instance_arrange_poly()` sets `ap.brim_width = 24.0` for tree-support objects with the comment `// 2*MAX_BRANCH_RADIUS_FIRST_LAYER`. This magic constant is not derived from `MAX_BRANCH_RADIUS_FIRST_LAYER` at compile time — it is a hardcoded literal. If the tree-support first-layer branch radius parameter changes (e.g. in TreeSupport3D.cpp), this brim width value will silently desync and objects may overlap or leave incorrect spacing on the plate | ModelArrange.cpp:get_instance_arrange_poly-brim_width-24.0-magic-not-derived-from-TreeSupport-const | Medium | P2 |
| 1154 | `get_instance_arrange_poly()` accesses `ap.extrude_ids.front()` at lines ~131, 135, 139, 141, 144, and 152 to index into per-extruder config arrays. There is no check that `ap.extrude_ids` is non-empty before calling `front()`. The only guard is `config.has("key")` which verifies key existence, not extruder assignment. For a model instance with no assigned extruder (empty `extrude_ids`), all six accesses are UB (undefined behavior) | ModelArrange.cpp:get_instance_arrange_poly-extrude_ids-front-no-empty-check-UB-on-unassigned | High | P1 |

## Session 83 Batch 2 — Circle.cpp, Color.cpp, CustomGCode.cpp, ExPolygonCollection.cpp, FaceDetector.cpp, FlushVolCalc.cpp, LocalesUtils.cpp, MaterialType.cpp, MinAreaBoundingBox.cpp, MinimumSpanningTree.cpp, NormalUtils.cpp (H1155–H1171)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1155 | `circle_ransac()` has a hardcoded `constexpr double RANSAC_ITERATIONS = 1000` with no documented basis. For very small sample sets (< 3 points), the function correctly returns false, but for sets with many outliers, 1000 iterations may be insufficient to guarantee finding an inlier triple with high probability. There is no RANSAC theoretical bound calculation anywhere in the file. A port should derive the iteration count from the inlier ratio if that is known, or document why 1000 is sufficient | Circle.cpp:circle_ransac-hardcoded-1000-iterations-no-theoretical-bound | Low | P3 |
| 1156 | `fit_circle()` uses a 2D Jacobi eigenvalue solver (`Eigen::SelfAdjointEigenSolver`) to find the principal axis of the point scatter. The solver is not given a convergence tolerance; it uses Eigen defaults. For nearly degenerate point sets (all points collinear or near-collinear), the solver may return a numerically unstable eigenvector. The caller does not check `solver.info() == Eigen::Success`. A port must add an info() check | Circle.cpp:fit_circle-no-check-SelfAdjointEigenSolver-info-success-degenerate-input | Medium | P2 |
| 1157 | `circle_ransac()` stores the best model as three raw `Point` values rather than a typed struct. The inlier threshold (`RANSAC_THRESHOLD = 0.1 mm` in scaled coordinates) and circle-fit residual (`best_score`) accumulate floating-point values in the scaled integer domain, mixing `double` and `coord_t` arithmetic. A port must carefully track which values are in mm vs scaled units | Circle.cpp:circle_ransac-inlier-threshold-mixes-scaled-coord_t-and-double-arithmetic | Medium | P2 |
| 1158 | `circle_taubin_fit()` (Taubin algebraic circle fit) returns a `bool` but never returns `false` in the current implementation — if the system matrix is degenerate, `Eigen::SelfAdjointEigenSolver` still produces output (smallest eigenvalue near zero, which corresponds to a degenerate circle). The caller in `fit_circle()` treats the return value as always-true without checking. A port must decide what degenerate-input behaviour is correct | Circle.cpp:circle_taubin_fit-never-returns-false-degenerate-matrix-silently-accepted | Medium | P2 |
| 1159 | `ColorRGB::operator+(const ColorRGB&)` and `operator*(float)` do not clamp the result to [0.0, 1.0]. Adding or multiplying two colors may produce component values > 1.0 or < 0.0. The `to_bytes()` / `to_hex_str()` conversion functions do not clamp either, so out-of-range values produce wrap-around or undefined OpenGL color values. A port should either enforce the invariant in the arithmetic operators or document that callers must clamp | Color.cpp:ColorRGB-operator-plus-multiply-no-clamp-OOB-components-undefined-rendering | Medium | P2 |
| 1160 | `CustomGCode::check_mode_for_custom_gcode_and_model_value()` operates on a `CustomGCode::Info` value by reference but is a free function, not a method. The function modifies `info.mode` based on `info.gcodes` contents. The function must be called before any code that reads `info.mode`, but there is no type-level or runtime enforcement of this ordering. A port that changes the call site order will produce incorrect multi-material mode detection | CustomGCode.cpp:check_mode-must-be-called-before-mode-read-no-ordering-enforcement | Low | P3 |
| 1161 | `CustomGCode::Info` is serialized via cereal in the print config. The cereal archive version for `Info` is hardcoded to 1. If new fields are added to `Info` in the future, the version number must be incremented and an upgrade path provided; but because the increment is manual and undocumented, it is frequently forgotten in the OrcaSlicer lineage (same issue as H1010 in PrintConfig.cpp). A port should use a schema-versioned serialization mechanism | CustomGCode.cpp:Info-cereal-version-1-hardcoded-manual-increment-risk | Low | P3 |
| 1162 | `ExPolygonCollection` is entirely implemented in its header as inline functions; ExPolygonCollection.cpp only contains `#include "ExPolygonCollection.hpp"` and nothing else. This file is a historical artifact. Porting tools that process .cpp files one-by-one may incorrectly report ExPolygonCollection as having no implementation | ExPolygonCollection.cpp:empty-implementation-file-all-logic-in-header | Low | P3 |
| 1163 | `FaceDetector::detect_exterior_face()` iterates over `m_model_object->volumes` and for each volume calls `its_face_neighbors()` which allocates a full `std::vector<Vec3i32>` neighbor array per volume per call, allocated on every detection run. For large multi-volume objects, this is a potentially large repeated allocation. No caching mechanism exists. A port should cache `its_face_neighbors()` results or use an incremental neighbor structure | FaceDetector.cpp:detect_exterior_face-its_face_neighbors-full-alloc-per-volume-per-call | Low | P3 |
| 1164 | `FaceDetector::is_face_on_seam()` detects seam faces by checking whether a triangle's vertices project onto a shared edge between two different ModelVolumes. The seam detection ignores `volume->is_modifier()` — modifier volumes are included in the seam search, so any modifier that shares a face position with a printable volume can incorrectly mark that printable face as a seam face, affecting paint-on-seam behaviour | FaceDetector.cpp:is_face_on_seam-includes-modifier-volumes-in-seam-detection | Medium | P2 |
| 1165 | `FlushVolCalc::calc_flush_vol()` returns early with `{0,0}` if either filament name is empty. However, it also returns early with `{0,0}` if the from-color and to-color are identical (after snapping to the nearest palette entry). A transition from any filament to itself returns zero flush volume, even though retraction/prime cycles at a wipe tower still require some purge volume. A port should verify this is intentional for same-filament transitions | FlushVolCalc.cpp:calc_flush_vol-same-color-transition-returns-zero-flush-may-be-incorrect | Medium | P2 |
| 1166 | `get_utf8_sequence_length()` is declared in LocalesUtils.hpp but the implementation in LocalesUtils.cpp returns `int` (signed). For a lead byte that encodes a 4-byte UTF-8 sequence (0xF0–0xF7), the function returns 4 as expected. For any byte value >= 0xF8 (invalid UTF-8), it returns -1 as an error sentinel. Callers in PerimeterGenerator.cpp and GCode.cpp do not check for -1 before using the return value as an array index offset, leading to decrement of the string position and an off-by-one reverse walk through the string | LocalesUtils.cpp:get_utf8_sequence_length-negative-return-unchecked-by-callers-off-by-one | High | P1 |
| 1167 | `MaterialType.cpp` implements `get_filament_map_from_config()` which converts an `int` value from the config key `"filament_type"` to a `FilamentType` enum. The mapping is done via a large `switch` statement with a `default: return FilamentType::ftUnknown`. Any new filament type added to the config `ConfigOptionEnum<FilamentType>` definition but not added to this switch silently maps to `ftUnknown`, affecting temperature and compatibility logic. No static_assert or compile-time check enforces completeness | MaterialType.cpp:get_filament_map-switch-default-ftUnknown-no-completeness-check | Medium | P2 |
| 1168 | `MinAreaBoundingBox` uses `boost::multiprecision::int128_t` for intermediate area calculations to avoid 64-bit overflow. If Boost.Multiprecision is unavailable or replaced in a port, naive `int64_t` arithmetic will silently overflow for polygons larger than ~92 mm² in scaled coordinates (about 0.09 mm² real — very small). The port must either retain Boost.Multiprecision, use __int128 on GCC/Clang, or re-derive the overflow threshold | MinAreaBoundingBox.cpp:uses-boost-multiprecision-int128-overflow-if-replaced | High | P1 |
| 1169 | `MinimumSpanningTree::build()` uses `boost::graph::kruskal_minimum_spanning_tree()` with a `boost::adjacency_list` graph. The graph is built by adding all O(n²) edges between input points (fully connected), so build time is O(n² log n) for Kruskal on a dense graph. For large point sets (n > ~1000), this is noticeably slow. No upper bound on n is enforced at the call sites; a port with a large point set may exhibit unexpected performance degradation | MinimumSpanningTree.cpp:build-O(n²)-edges-fully-connected-graph-Kruskal-slow-for-large-n | Medium | P2 |
| 1170 | `MinimumSpanningTree` stores `PointIndexPairs` (pairs of point indices) as its output but also stores the full `boost::adjacency_list` internally for the duration of the build. The adjacency list is not freed until the MST object is destroyed. For a long-lived MST object over a large point set, this holds O(n²) edge data in memory even though only O(n) MST edges are needed after construction. A port can free the graph after MST extraction | MinimumSpanningTree.cpp:adjacency_list-O(n²)-held-in-memory-after-build | Low | P3 |
| 1171 | `NormalUtils::compute_normal()` estimates per-vertex normals by area-weighting surrounding face normals. For zero-area triangles (degenerate faces), the cross-product is the zero vector. The function does not check for zero-area before accumulating; the zero vector is added to the vertex normal accumulator, which is then normalized. For a vertex surrounded only by degenerate triangles, the accumulator remains zero and `normalize()` returns NaN. The caller `compute_normals_per_vertex()` does not check for NaN | NormalUtils.cpp:compute_normal-zero-area-triangle-produces-NaN-normal-via-normalize-of-zero-vec | Medium | P2 |

## Session 84 — NSVGUtils.cpp, ObjColorUtils.cpp, ObjectID.cpp, ParameterUtils.cpp, Platform.cpp, PNGReadWrite.cpp, PolygonTrimmer.cpp, PrincipalComponents2D.cpp, ProjectTask.cpp, Semver.cpp, ShortEdgeCollapse.cpp (H1172–H1188)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1172 | `NSVGUtils.cpp` contains a large commented-out `save()` function block that includes `<charconv>` as a dependency solely to support that dead code. PrusaSlicer copyright is referenced in the SVG comment block. In a port, the `<charconv>` include and the dead `save()` block can both be omitted | NSVGUtils.cpp:dead-save-function-charconv-include-only-for-dead-code | Low | P3 |
| 1173 | `stroke_to_expolygons()` computes ArcTolerance for Clipper's jtRound joint style as `mitter = cbrt(tesselation_tolerance)` and passes it as `ArcTolerance`. The variable is named `mitter` (a value that Clipper uses for MiterLimit semantics in jtMiter joins, not ArcTolerance). ArcTolerance controls the maximum deviation of a round join from the true circle, measured in scaled coordinate units. Using a cube-root formula for this has no documented geometric basis and will produce under-rounded joins at non-unity tolerance values. A port must understand and replicate or correct this formula | NSVGUtils.cpp:stroke_to_expolygons-ArcTolerance-computed-as-cbrt(tesselation_tolerance)-semantics-wrong | Medium | P2 |
| 1174 | `wipe_tower_object_id()` and `wipe_tower_instance_id()` each declare a `static ObjectBase mine` inside the function body. These are two separate static instances with IDs assigned on first call. Both must receive distinct, stable IDs; the current implementation relies on `ObjectBase` constructor incrementing a global counter. A port must preserve this two-static-instance pattern or otherwise guarantee the wipe tower object and instance always have distinct, stable IDs that differ from all model object/instance IDs | ObjectID.cpp:wipe_tower_object_id-instance_id-two-separate-static-ObjectBase-must-remain-distinct | Medium | P2 |
| 1175 | `get_index_for_extruder_parameter()` has `assert(false); return 0` as the fallback when `variant_index < 0`. In release builds the assert is stripped, so the function silently returns 0 (the first extruder's index). Any multi-extruder config query with an invalid variant_index will use extruder 0's parameters without any error signal. A port should return an error/optional or at minimum log | ParameterUtils.cpp:get_index_for_extruder_parameter-assert-false-return-0-silent-fallback | Medium | P2 |
| 1176 | `Platform.cpp` uses `static_assert(false, "Unknown platform")` on the `#else` branch of the platform detection chain. This is a compile-time error, not a runtime error — which is correct for compile-time platform detection. However, lines 93-94 contain dead assignment statements `platform = Platform::...` that can never be reached because the `static_assert(false)` terminates compilation. The dead assignments are misleading noise | Platform.cpp:static_assert-false-dead-assignments-after-it-on-lines-93-94 | Low | P3 |
| 1177 | `Platform.cpp` WSL detection reads at most 4095 bytes of `/proc/version` via `fgets(buf, 4096, f)`. If the kernel version string is longer than 4095 bytes (uncommon but possible with heavily patched custom kernels), the string `"microsoft"` may be truncated and WSL goes undetected. The code silently falls through to `Platform::Linux`. A port should use a whole-file read or `strstr` on the full content | Platform.cpp:WSL-detection-fgets-4095-byte-limit-truncation-microsoft-undetected | Low | P3 |
| 1178 | `decode_png()` returns `false` without any log message for non-8-bit-GRAY PNGs. A caller receiving `false` has no diagnostic to distinguish "file not found", "corrupt PNG", "wrong color type", or "wrong bit depth". A port should add error detail to the return or use an error-string out-parameter | PNGReadWrite.cpp:decode_png-silent-false-for-non-8-bit-GRAY-no-error-message | Low | P3 |
| 1179 | `write_rgb_or_gray_to_file()` uses C-style goto-cleanup with libpng's `setjmp`/`longjmp` error handling. If `longjmp` fires while C++ objects with non-trivial destructors (e.g. `std::vector`, `std::string`) are on the stack between the `setjmp` call and the `longjmp` target, those destructors are not called — undefined behaviour in C++. The current code keeps the C++ objects in a separate scope above the `setjmp` to mitigate this, but the pattern is fragile and must be carefully preserved in any port that uses libpng | PNGReadWrite.cpp:write_rgb_or_gray_to_file-libpng-setjmp-longjmp-UB-with-cpp-objects-on-stack | High | P1 |
| 1180 | `decode_colored_png()` reads rows in **reverse order** (bottom-to-top, `row_ptrs[height - 1 - i]`) to match OpenGL's bottom-up pixel convention. Any port that reads rows top-to-bottom will produce vertically flipped textures. This is an intentional non-standard convention that must be documented and preserved | PNGReadWrite.cpp:decode_colored_png-rows-read-bottom-to-top-OpenGL-convention-port-must-preserve | Medium | P2 |
| 1181 | `PolygonTrimmer.cpp::trim_loop()` is a **non-functional stub**. The visitor correctly detects intersections between the loop and the trimming set, but the visitor body is a single-line empty comment (`// TODO`). The function always returns an empty `TrimmedLoop`. The only caller in the codebase is in a `#if 0` block in `PerimeterGenerator.cpp`. A port can omit this file entirely | PolygonTrimmer.cpp:trim_loop-non-functional-stub-empty-visitor-body-always-returns-empty | Low | P3 |
| 1182 | `compute_principal_components()` in PrincipalComponents2D.cpp returns unnormalised eigenvectors scaled by the eigenvalue when the covariance matrix is near-zero (degenerate input). Callers that assume unit-length eigenvectors (e.g. to extract a rotation angle) will receive wrong rotation values for degenerate point sets | PrincipalComponents2D.cpp:compute_principal_components-returns-unnormalised-eigenvectors-for-degenerate-input | Medium | P2 |
| 1183 | Dead `#if 0` debug `std::cout` blocks in PrincipalComponents2D.cpp (lines 98-103 and 126-131) are noise for port analysis. They show intermediate covariance matrix values and were left from development. A port should omit them | PrincipalComponents2D.cpp:dead-if0-debug-cout-blocks-lines-98-131 | Low | P3 |
| 1184 | `BBLTask::parse_content_json()` uses raw `new BBLSubTask` inside a loop with manual `delete` on failure. An exception between the `new` and the failure-branch `delete` (e.g., from `json.at()` throwing for a missing key) leaves the `BBLSubTask` allocated but not tracked — a memory leak. A port should use `std::unique_ptr<BBLSubTask>` and `emplace_back` | ProjectTask.cpp:parse_content_json-raw-new-BBLSubTask-manual-delete-exception-unsafe-leak | Medium | P2 |
| 1185 | `BBLSubTask::parse_status()` maps unrecognised status strings to `TASK_CREATED`. Future server-side task statuses (e.g. `"TASK_WAITING_FOR_SLOT"`) will silently appear as `TASK_CREATED` in the UI, potentially triggering start-print logic on a task that is actually queued. A port should use an explicit unknown-status sentinel rather than defaulting to TASK_CREATED | ProjectTask.cpp:parse_status-unknown-status-string-silently-becomes-TASK_CREATED | Medium | P2 |
| 1186 | `its_short_edge_collpase()` — note the typo "collpase" not "collapse". This is the public API symbol name declared in ShortEdgeCollapse.hpp and defined in ShortEdgeCollapse.cpp. Any port or binding that needs to match the binary ABI must preserve the typo exactly, or provide an alias. The typo was presumably introduced at authorship and has persisted because changing it would be an ABI break | ShortEdgeCollapse.cpp:its_short_edge_collpase-public-API-typo-collpase-must-be-preserved-or-aliased | Low | P3 |
| 1187 | `flatten_queue` in `its_short_edge_collpase()` is a `std::vector<int>` captured by reference in a lambda used for union-find path compression. This vector is local to a single-threaded call, but if the function were ever called concurrently on the same mesh object (sharing the union-find state), the vector would be accessed from multiple threads without synchronization | ShortEdgeCollapse.cpp:flatten_queue-shared-vector-by-ref-not-safe-for-concurrent-calls | Low | P3 |
| 1188 | The `edge_len` growth formula in `its_short_edge_collpase()` is `edge_len += target_edge_length * (1.f - decimation_ratio) / decimation_ratio`. At low decimation_ratio (e.g. 0.01), this evaluates to `target_edge_length * 99` — a massive threshold increase. At decimation_ratio near 1.0, the increase approaches zero. This is counterintuitive: more aggressive decimation (lower ratio) → larger threshold increase per collapsed edge. The formula must be replicated exactly in a port to produce identical mesh decimation behaviour | ShortEdgeCollapse.cpp:edge_len-growth-formula-counterintuitive-low-ratio-large-threshold | Medium | P2 |

## Session 84 — SlicesToTriangleMesh.cpp, SVG.cpp, Thread.cpp, TriangleSetSampling.cpp (H1189–H1190)

| ID | Description | Location | Severity | Priority |
|----|-------------|----------|----------|----------|
| 1189 | `SVG::draw_grid()` (BBS addition, SVG.cpp line 332): the horizontal-lines loop initializes `end_pt` as `Point(bbox.max(1), bbox.min(1))` — the x-coordinate is `bbox.max(1)` (the Y-max value) rather than `bbox.max(0)` (X-max). This is a copy-paste bug: horizontal grid lines have an incorrect x-extent equal to the bbox y-range instead of the x-range. For non-square bounding boxes, horizontal lines will be either too long or too short. The vertical-lines loop that follows is correct. Additionally, `slices_to_mesh()` in SlicesToTriangleMesh.cpp carries a FIXME (line 114) acknowledging that `its_merge_vertices` + `its_remove_degenerate_faces` do not fully heal the mesh — cracks remain at layer boundaries. The mesh is still used downstream | SVG.cpp:draw_grid-end_pt-x-init-bbox.max(1)-copy-paste-bug-wrong-horizontal-extent; SlicesToTriangleMesh.cpp:slices_to_mesh-FIXME-repair-does-not-fix-cracks | Medium | P2 |
| 1190 | `name_tbb_thread_pool_threads_set_locale()` in Thread.cpp uses a plain `static bool initialized` (line 214) without atomic or mutex protection. The comment on `WindowsGetSetThreadNameAPIInitialize()` (line 36) explicitly states "Not thread safe!" for the same reason. If two threads call `name_tbb_thread_pool_threads_set_locale()` simultaneously before initialization completes, both see `initialized == false` and both run the TBB parallel_for — causing double locale setting and potential TBB task arena corruption. Both functions must be called from the main thread before any worker threads are spawned | Thread.cpp:name_tbb_thread_pool_threads_set_locale-static-bool-initialized-not-thread-safe | High | P1 |
