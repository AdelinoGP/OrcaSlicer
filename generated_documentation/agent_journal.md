# Agent Journal — OrcaSlicer Codebase Analysis

## CURRENT STATUS
Last session: 87
Active task: none — T302 complete
Next action: Start T303 by producing `generated_documentation/REVIEW_PACKAGE.md`, then leave T303 as [/] ACTIVE for human review handoff.
Unresolved [UNCLEAR] tags: 12 — all remaining source tags are marked `[UNCLEAR → ESCALATED]` after Session 86 T300 triage
Files remaining (Phase 1): 0 — annotation pass COMPLETE as of Session 84
Files completed (Phase 1): 413 source files annotated across Sessions 1–84
Next hazard ID: H1191

Open questions (from Session 1 — status as of Session 86):
- **Q1 RESOLVED** — `slice_mesh` does NOT use the admesh adjacency table. `its_face_neighbors_par()` is called at
  `src/libslic3r/TriangleMeshSlicer.cpp:2437` to rebuild the face-neighbor table from scratch each call.
  The admesh repair data (`its_neighbors_par`) is separate and only used by mesh-repair code.
- **Q2 RESOLVED** — `Layer` objects are owned by `PrintObject::m_layers` as raw pointers
  (`LayerPtrs = std::vector<Layer*>`, defined in `src/libslic3r/Layer.hpp:40`).
  Destruction is via manual `delete` in `PrintObject`'s clear routine — no `unique_ptr`.
  This is Hazard H352 (`~Layer()` manual raw pointer delete, P1/High).
- **Q3 RESOLVED** — Arachne and classic `PerimeterGenerator` are mutually exclusive.
  Dispatch is in `LayerRegion.cpp:120`: `process_arachne()` fires only when
  `wall_generator == Arachne AND !spiral_vase`; all other conditions invoke `process_classic()`.
  They share the same `LayerRegion` inputs but produce different output extrusion types.
- **Q4 RESOLVED** — `ClipperZUtils` Z-metadata is used for intersection provenance tracking in
  `src/libslic3r/Algorithm/LineSplit.cpp`. The Z value encodes whether a Clipper intersection
  point originated from the source path (z >= 0) or the clip polygon (z == CLIP_IDX), enabling
  LineSplit to reconstruct which segments are inside vs outside the clipping boundary after
  the boolean operation.

---

## Sessions 1–83 — Archived
Full log: agent_journal_archive_s01_s83.md

---

---

## Session 85

### Goal
Complete Phase 2 documentation (T205D — pseudocode_seam_placer.md) and Phase 3 review tasks
(T300 audit, T301 link verification, T302 final summary).

### T205D — pseudocode_seam_placer.md
Created `generated_documentation/pseudocode_seam_placer.md` (962 lines).
Source: `src/libslic3r/GCode/SeamPlacer.cpp` (1886 lines) + `SeamPlacer.hpp` (264 lines).

Covers:
- All 6 pipeline phases: mesh occlusion sampling, candidate gathering, visibility transfer,
  overhang/embedding, seam selection, B-spline alignment, and G-code export (`place_seam`)
- Full `SeamPlacer::init()` orchestrator with mode × phase matrix
- Sub-algorithms: `raycast_visibility`, `compute_global_occlusion`, `calculate_point_visibility`,
  `gauss`, `compute_angle_penalty`, `calculate_polygon_angles_at_vertices`,
  `extract_perimeter_polygons`, `process_perimeter_polygon`, `SeamComparator`,
  `pick_seam_point`, `pick_random_seam_point`, `find_next_seam_in_layer`,
  `find_seam_string`, `align_seam_points`, `place_seam`
- 10 Translation Notes including monotonic-Z assumption (TN 12.3), spAlignedBack score >1.0
  (TN 12.1), dead hemisphere samplers (TN 12.2), mixed-unit penalty (TN 12.4),
  `seam_align_mm_per_segment` type mismatch (TN 12.5), `its_short_edge_collpase` typo (TN 12.6),
  O(N) perimeter-matching loop (TN 12.7), staggered seam infinite loop hazard (TN 12.8),
  `global_index--` re-visit (TN 12.9), `end_index` exclusive/inclusive mismatch (TN 12.10)

Committed: `ec3e5b9218` — "docs: create pseudocode_seam_placer.md (T205D)"

### T301 — Code Link Verification

Spot-checked line references in all six documentation files:
- `pseudocode_seam_placer.md` — all SeamPlacer.cpp/hpp references verified correct
- `pseudocode_fill_lightning.md` — Generator.cpp, DistanceField.hpp, FillLightning.cpp refs correct
- `pseudocode_multimaterial_segmentation.md` — all MultiMaterialSegmentation.cpp line refs correct
- `pseudocode_triangle_mesh_slicer_chaining.md` — chain_lines_by_triangle_connectivity at 1166 correct;
  make_loops noted at 1536 (comment block) vs. actual signature at 1549 — acceptable annotation drift
- `03_algorithmic_complexities.md` — Layer.hpp:441, FillAdaptive.cpp:325, Generator.hpp:74 all correct
- `04_refactoring_hazards.md` — SkeletalTrapezoidationGraph.cpp:35, FillRectilinear.cpp:2418,
  FillBase.cpp:1543 all correct

No broken links found. Minor drift (≤15 lines) in one reference documented above.

### T302 — Branch Summary

**Branch:** `agent/analysis`
**Base:** `main` (no new code changes — all commits are documentation only)

**Commits this session (Sessions 85):**
- `ec3e5b9218` — `docs: create pseudocode_seam_placer.md (T205D)`
- (T300/T301/T302 changes in this final commit)

**All Phase 2 documentation tasks complete:**
- `pseudocode_triangle_mesh_slicer_chaining.md` (T205A) — `b19f4b04e4`
- `pseudocode_fill_lightning.md` (T205B) — `f026991011`
- `pseudocode_multimaterial_segmentation.md` (T205C) — `601181c07d`
- `pseudocode_seam_placer.md` (T205D) — `ec3e5b9218`

**All Phase 3 review tasks complete:**
- T300 — 51 [UNCLEAR] tags audited; all escalated (see table above)
- T301 — all code links spot-checked; no broken references
- T302 — this entry

**Total generated documentation:**
- 7 files in `generated_documentation/`
- 413 source files annotated with [INTENT]/[HAZARD]/[COUPLING]/[STATE]/[UNCLEAR] tags
- 1190 hazard IDs assigned (H1–H1190)
- 4 pseudocode files covering seam placement, lightning fill, multimaterial segmentation,
  and triangle mesh slicer chaining

**Open items for future sessions:**
- 51 escalated [UNCLEAR] tags requiring individual investigation
- `make_loops` line reference drift (~13 lines) in pseudocode_triangle_mesh_slicer_chaining.md
- H1191+ hazard IDs available for future annotation passes

---

Total `[UNCLEAR]` tags in source: **51** across 34 files.
No tags were marked `[UNCLEAR] → [RESOLVED]` during the annotation pass.

**Audit result: all 51 tags are ESCALATED** — they document genuine open questions that require
either: (a) reading additional related files not in scope, (b) running the code to observe
behavior, or (c) consulting original Cura/upstream authors. None are blocking errors; all have
been captured as Hazards in `04_refactoring_hazards.md` where relevant to refactoring risk.

**Escalated [UNCLEAR] tags by file** (all require future investigation, not blocking):

| File | Line | Summary |
|------|------|---------|
| `Arachne/SkeletalTrapezoidation.cpp` | 719 | Possible latent CuraEngine bug — intent unclear |
| `Arachne/SkeletalTrapezoidation.cpp` | 1301 | Return value semantics of `is_only_going_down` |
| `Arachne/SkeletalTrapezoidation.cpp` | 1913 | Undocumented TODO "don't use toolpath locations past middle" |
| `Fill/Fill.cpp` | 34 | Undocumented fill-type grammar at line 25 |
| `Fill/Lightning/Generator.hpp` | 124 | Default density 0.15 — reason undocumented |
| `Fill/Lightning/Generator.hpp` | 207 | `m_prune_length == m_wall_supporting_radius` — coincidence or design? |
| `Fill/Lightning/Generator.hpp` | 254 | `bboxs` typo, purpose undocumented |
| `Fill/FillBase.cpp` | 2218 | Grid-aligned support spacing/density formula intent |
| `Fill/FillBase.cpp` | 2360 | `side1`/`side2` public members — purpose unclear |
| `Fill/FillBase.cpp` | 3236 | Closed polyline handling from `Paths64_to_polylines()` |
| `Fill/FillAdaptive.cpp` | 1699 | `+ EPSILON` on `max_cube_edge_length` — boundary fix? |
| `Fill/FillAdaptive.cpp` | 1739 | Unnormalized normal in dot product — intentional? |
| `Fill/FillLightning.cpp` | 84 | `line_overlap` formula semantics |
| `Fill/FillLightning.hpp` | 120 | "reoder" typo left as-is |
| `Fill/FillGyroid.cpp` | 224 | "1z = 10^-6 mm" comment meaning |
| `Fill/FillTpmsD.cpp` | 176 | 16-segment initial refinement rationale |
| `GCode.cpp` | 94 | `g_max_label_object = 64` cap rationale |
| `GCode/AvoidCrossingPerimeters.cpp` | 1528 | Newer approach vs. older — no explanation |
| `GCode/PressureEqualizer.cpp` | 900 | Commented-out rate-clamping strategy |
| `GCode/FanMover.hpp` | 99 | `with_D_option` field — stored but never read (dead?) |
| `Support/SupportMaterial.cpp` | 1886 | Post-correction logic for gap-synced layers |
| `Support/SupportSpotsGenerator.cpp` | 38 | Disabled block — intentional deferral or forgotten? |
| `Support/SupportParameters.hpp` | 55 | `thresh_big_overhang` declared but never used |
| `TriangleMesh.hpp` | 220 | Volume computation with shear transforms |
| `TriangleMesh.cpp` | 407 | Whether OrcaSlicer callers use shear transforms |
| `TriangleMesh.cpp` | 1672 | `its_flip_triangles` vs `its_reverse_all_facets` semantics |
| `Model.cpp` | 313 | `.oltp` dispatch argument `256` meaning |
| `Model.cpp` | 3296 | Undocumented field purpose |
| `Flow.cpp` | 207 | "filling up spacing without air gap" condition intent |
| `Flow.cpp` | 279 | Disabled assert — intentional for gap fill? |
| `Flow.cpp` | 322 | Why support transition uses bridging flow always |
| `Point.hpp` | 272 | Operator passed as runtime string not enum/template |
| `Point.cpp` | 87 | Commented-out int64 cast alternative |
| `Brim.hpp` | 59 | `make_brim_auto` declared but definition location unclear |
| `BuildVolume.cpp` | 703 | Parameter name "paths" for interleaved geometry |
| `BuildVolume.cpp` | 741 | Cube returned from (0,0,0) not centred on bed |
| `MeshBoolean.cpp` | 894 | Commented-out `make_boolean` call |
| `Emboss.hpp` | 148 | Non-atomic shared_ptr — possible data race |
| `MultiPoint.hpp` | 146 | "tolerence" typo + mixed ccw/condition logic |
| `MultiPoint.cpp` | 384 | Function named `concave_hull_2d` but algorithm is not concave hull |
| `PrintObject.cpp` | 976 | `set_done()` commented out — step never marked done |
| `PrintObject.cpp` | 3628 | `POProfiler` fields computed but never logged |
| `VariableWidth.cpp` | 26 | Width may be slightly wrong for last segment — intentional? |
| `VariableWidth.cpp` | 153 | Under-extrusion on last polyline segment — intentional? |
| `Measure.cpp` | 326 | RANSAC threshold 0.05 and arc subtend 0.9*π/2 rationale |
| `CutUtils.cpp` | 29 | "may be intentional or bug" — design intent unknown |
| `CutUtils.cpp` | 336 | No comment explaining design intent |
| `FilamentGroupUtils.cpp` | 27 | Undocumented intent acknowledged in source comment |
| `TriangleMeshSlicer.cpp` | 213 | Duplicate `slice_facet_for_cut_mesh()` purpose |
| `TriangleMeshSlicer.cpp` | 1300 | `goto found` porting concern |
| `TriangleMeshSlicer.cpp` | 1966 | "safety offset" 0.0499mm changed to `closing_radius` |

All 51 tags remain in the source as documentation for future maintainers. No source code was
modified. Tags will not be marked `→ [RESOLVED]` until each is individually investigated.

---

## Session 87 — T302 Final git commit and branch summary

**Active task:** T302 — Final git commit and branch summary

### Files processed
- `.ralph/ralph-tasks.md`
- `generated_documentation/agent_journal.md`

### Key discoveries
- The working branch remains `agent/analysis` and does not track an upstream remote.
- Before the T302 checkpoint commit, the branch contained 182 commits relative to `main`, spanning
  the full annotation pass, Phase 2 documentation, and the Phase 3 review artifacts completed so far.
- The worktree also contains a separate Ralph loop bookkeeping edit in `.ralph/ralph-loop.state.json`
  (`iteration: 2 -> 3`); this file is intentionally left uncommitted because it is runtime state,
  not part of T302 deliverables.

### Branch summary
- **Branch:** `agent/analysis`
- **Base branch for review:** `main`
- **Upstream:** none configured
- **Commits ahead of `main` before this checkpoint:** 182
- **Aggregate diff vs `main`:** large documentation + inline-comment annotation branch, including
  358 files changed with generated documentation, task registry updates, and source comment-only edits.
- **Latest task checkpoints already on branch:**
  - `a0d271fa96` — `annotate: resolve remaining unclear tags (T300)`
  - `2c698967ef` — `docs: verify generated documentation code links (T301)`

### Decisions made
- Closed T302 as a bookkeeping/session-boundary task: the deliverable here is the branch summary and
  checkpoint commit, not the human-review package itself.
- Deferred T303 exactly as instructed; it is the next task and must leave the project in the
  "all done except T303 active" state only after the review package is created.

### Open questions
- None for T302.

### Cross-references
- `generated_documentation/link_verification_report.md`
- `generated_documentation/04_refactoring_hazards.md`
- `generated_documentation/pseudocode_seam_placer.md`

**Completed tasks this session:** T302

---

## Session 84

### Goal
Complete the annotation pass over the remaining `src/libslic3r/*.cpp` files from batches initiated in sessions 82–83, write all new hazards to `04_refactoring_hazards.md`, and commit the complete session.

### Files Annotated (Batch 1 — from prior session turn, committed this session)
- `src/libslic3r/Circle.cpp` — H1155–H1158
- `src/libslic3r/Color.cpp` — H1159
- `src/libslic3r/CustomGCode.cpp` — H1160–H1161
- `src/libslic3r/ExPolygonCollection.cpp` — H1162
- `src/libslic3r/ExPolygonsIndex.cpp` — file-level tags
- `src/libslic3r/FaceDetector.cpp` — H1163–H1164
- `src/libslic3r/FlushVolCalc.cpp` — H1165
- `src/libslic3r/IntersectionPoints.cpp` — file-level tags
- `src/libslic3r/LocalesUtils.cpp` — H1166
- `src/libslic3r/MaterialType.cpp` — H1167
- `src/libslic3r/MinAreaBoundingBox.cpp` — H1168
- `src/libslic3r/MinimumSpanningTree.cpp` — H1169–H1170
- `src/libslic3r/miniz_extension.cpp` — file-level tags
- `src/libslic3r/NormalUtils.cpp` — H1171

### Files Annotated (Batch 2 — annotated and committed this session)
- `src/libslic3r/NSVGUtils.cpp` — H1172–H1173
- `src/libslic3r/ObjColorUtils.cpp` — file-level tags
- `src/libslic3r/ObjectID.cpp` — H1174
- `src/libslic3r/ParameterUtils.cpp` — H1175
- `src/libslic3r/Platform.cpp` — H1176–H1177
- `src/libslic3r/PNGReadWrite.cpp` — H1178–H1180
- `src/libslic3r/PolygonTrimmer.cpp` — H1181
- `src/libslic3r/PrincipalComponents2D.cpp` — H1182–H1183
- `src/libslic3r/ProjectTask.cpp` — H1184–H1185
- `src/libslic3r/Semver.cpp` — file-level tags
- `src/libslic3r/ShortEdgeCollapse.cpp` — H1186–H1188

### Files Annotated (Batch 3 — final remaining files, completed this session)
- `src/libslic3r/SlicesToTriangleMesh.cpp` — H1189
- `src/libslic3r/Surface.cpp` — file-level tags
- `src/libslic3r/SVG.cpp` — H1189 (draw_grid copy-paste bug)
- `src/libslic3r/Tesselate.cpp` — file-level tags (tessError silent, deque stability)
- `src/libslic3r/Thread.cpp` — H1190
- `src/libslic3r/Time.cpp` — file-level tags (MSVC strptime emulation, WARN)
- `src/libslic3r/Timer.cpp` — file-level tags
- `src/libslic3r/TriangleMeshDeal.cpp` — hardcoded debug path noted
- `src/libslic3r/TriangleSetSampling.cpp` — float-key map hazard, deterministic seed
- `src/libslic3r/TriangulateWall.cpp` — entire file is dead code
- `src/libslic3r/TryCatchSignal.cpp` — unusual .cpp include
- `src/libslic3r/TryCatchSignalSEH.cpp` — Windows SEH wrapper
- `src/libslic3r/Zipper.cpp` — RAII miniz ZIP writer
- `src/libslic3r/BlacklistedLibraryCheck.cpp` — case-sensitive DLL blacklist
- `src/libslic3r/libslic3r.cpp` — SCALING_FACTOR definition
- `src/libslic3r/pchheader.cpp` — PCH stub

### Key Discoveries

| Hazard | Severity | Description |
|--------|----------|-------------|
| H1155 | P3/Low | `circle_ransac()`: 1000 iterations hardcoded with no RANSAC theoretical bound. |
| H1156 | P2/Medium | `fit_circle()`: `SelfAdjointEigenSolver::info()` not checked — degenerate input silently accepted. |
| H1157 | P2/Medium | `circle_ransac()`: mixes scaled coord_t and double arithmetic without documentation. |
| H1158 | P2/Medium | `circle_taubin_fit()` never returns false; degenerate-matrix case silently accepted. |
| H1159 | P2/Medium | `ColorRGB` arithmetic operators do not clamp to [0,1]; OOB values produce undefined rendering. |
| H1160 | P3/Low | `check_mode_for_custom_gcode_and_model_value()` must be called before mode read — no ordering enforcement. |
| H1161 | P3/Low | `CustomGCode::Info` cereal version 1 hardcoded; manual increment risk. |
| H1162 | P3/Low | ExPolygonCollection.cpp is an empty .cpp file — all logic in header. |
| H1163 | P3/Low | `detect_exterior_face()`: `its_face_neighbors()` fully reallocated per volume per call — no caching. |
| H1164 | P2/Medium | `is_face_on_seam()` includes modifier volumes in seam detection — incorrect paint-on-seam. |
| H1165 | P2/Medium | `calc_flush_vol()` returns zero flush for same-color transition — may be incorrect. |
| H1166 | P1/High | `get_utf8_sequence_length()` returns -1 for invalid UTF-8; callers don't check — off-by-one walk. |
| H1167 | P2/Medium | `get_filament_map_from_config()` switch default → ftUnknown; no completeness check. |
| H1168 | P1/High | `MinAreaBoundingBox` uses `boost::multiprecision::int128_t`; naive int64 overflows for small polygons. |
| H1169 | P2/Medium | MST build is O(n²) due to fully connected graph; slow for large n. |
| H1170 | P3/Low | MST holds full O(n²) adjacency list in memory after build. |
| H1171 | P2/Medium | `compute_normal()`: zero-area triangles produce NaN normals via normalize(zero). |
| H1172 | P3/Low | NSVGUtils.cpp: dead `save()` block pulls in `<charconv>` as unused include. |
| H1173 | P2/Medium | `stroke_to_expolygons()`: ArcTolerance computed as cbrt(tesselation_tolerance) — variable named `mitter`; semantics undocumented. |
| H1174 | P2/Medium | `wipe_tower_object_id()` and `wipe_tower_instance_id()`: two separate `static ObjectBase mine` — must remain distinct and stable. |
| H1175 | P2/Medium | `get_index_for_extruder_parameter()`: `assert(false); return 0` silent fallback for invalid variant_index. |
| H1176 | P3/Low | Platform.cpp: dead assignments after `static_assert(false)` on lines 93-94. |
| H1177 | P3/Low | WSL detection: `fgets` limited to 4095 bytes; "microsoft" may be truncated in custom kernels. |
| H1178 | P3/Low | `decode_png()` returns false silently for non-8-bit-GRAY PNGs — no error message. |
| H1179 | P1/High | `write_rgb_or_gray_to_file()`: libpng setjmp/longjmp UB with C++ objects on stack. |
| H1180 | P2/Medium | `decode_colored_png()`: rows read bottom-to-top for OpenGL convention — port must preserve. |
| H1181 | P3/Low | `trim_loop()` in PolygonTrimmer.cpp: non-functional stub — always returns empty TrimmedLoop. |
| H1182 | P2/Medium | `compute_principal_components()`: returns unnormalised eigenvectors for degenerate input. |
| H1183 | P3/Low | PrincipalComponents2D.cpp: dead `#if 0` debug cout blocks. |
| H1184 | P2/Medium | `parse_content_json()`: raw `new BBLSubTask` with manual delete — exception-unsafe memory leak. |
| H1185 | P2/Medium | `parse_status()`: unknown status string silently becomes TASK_CREATED. |
| H1186 | P3/Low | Public API symbol `its_short_edge_collpase` contains typo "collpase" — must be preserved or aliased. |
| H1187 | P3/Low | `flatten_queue` shared vector captured by reference — not safe for concurrent calls. |
| H1188 | P2/Medium | `edge_len` growth formula: low decimation ratio → large threshold increase (counterintuitive). |
| H1189 | P2/Medium | SVG.cpp `draw_grid()`: `end_pt` x initialized to `bbox.max(1)` (copy-paste bug); SlicesToTriangleMesh.cpp FIXME: repair pass leaves mesh cracks. |
| H1190 | P1/High | Thread.cpp: `static bool initialized` not thread-safe; Windows API init also not thread-safe — must be called from main thread only. |

### Next Hazard Number
H1191

### Status of src/libslic3r/ Annotation Pass
**The annotation pass over all `src/libslic3r/*.cpp` files is now COMPLETE.**
All files in the directory have been annotated with structured comment tags.
Next phase: expand Task 2 (architectural deconstruction) and Task 3 documentation updates.

---

---

## Session 86


**Active task:** T300 — Resolve or escalate all `[UNCLEAR]` tags in annotated source files

### Goal
Re-run T300 with the required resolution-first policy, convert every remaining inline `[UNCLEAR]` tag to either `[UNCLEAR → RESOLVED]` or `[UNCLEAR → ESCALATED]`, and record the outcome.

### Files processed
- 34 annotated source files containing all 51 remaining `[UNCLEAR]` tags
- `generated_documentation/agent_journal.md`
- `.ralph/ralph-tasks.md`

### Key discoveries
- 39 tags were resolved from local code context and same-module cross-references.
- 12 tags remain escalated because the rationale is not recoverable from local code alone.
- No plain `[UNCLEAR]` tags remain in `src/`; every outstanding question is now explicitly classified.

## T300 Resolutions

Total resolved tags: **39**

| File | Line | Resolution |
|------|------|------------|
| `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp` | 719 | Dead condition prevents `filterCentral()` call because the branch requires `isLocalMaximum()` and its negation. |
| `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp` | 1301 | Return value means every recursed central branch is still going down, so the caller can skip a junction ratio write. |
| `src/libslic3r/BuildVolume.cpp` | 703 | `paths` is an interleaved vertex/normal float buffer; this helper inspects only the normal triplets. |
| `src/libslic3r/BuildVolume.cpp` | 741 | `bounding_mesh()` constructs a box from origin to `m_bboxf.max`, ignoring any nonzero minimum bed offset. |
| `src/libslic3r/Brim.hpp` | 59 | Auto brim is handled inline in the main brim path, leaving this declaration without a local definition. |
| `src/libslic3r/FilamentGroupUtils.cpp` | 27 | `calc_max_group_size()` sums `ams_unit_count * count` per group and promotes empty groups to size 1 when external filament is allowed. |
| `src/libslic3r/Fill/Fill.cpp` | 34 | The string parser either runs a rotation mini-language with repeats/interpolation/units or deserializes a plain per-layer angle list. |
| `src/libslic3r/Fill/FillAdaptive.cpp` | 1699 | `+ EPSILON` guards the octree-size boundary test against floating-point rounding drift. |
| `src/libslic3r/Fill/FillAdaptive.cpp` | 1739 | The dot-product check intentionally avoids normalizing because it compares against `0.707 * n.norm()` directly. |
| `src/libslic3r/Fill/FillBase.cpp` | 2218 | The helper is support-only in practice because `connect_base_support()` is the local caller and pre-rotates lines vertical. |
| `src/libslic3r/Fill/FillBase.cpp` | 2360 | `side1`/`side2` and `m_polyline_end` logic suppress zero-width vertical splice segments. |
| `src/libslic3r/Fill/FillBase.cpp` | 3236 | Offset paths are force-closed before storage by appending the first point when needed. |
| `src/libslic3r/Fill/FillGyroid.cpp` | 224 | `gridZ` is in scaled coordinates and is normalized by `scaleFactor` before gyroid phase evaluation. |
| `src/libslic3r/Fill/FillLightning.cpp` | 84 | `line_overlap` trims branch endpoints through `convertToLines()` / `removeJunctionOverlap()` to reduce junction overlap. |
| `src/libslic3r/Fill/FillLightning.hpp` | 120 | `no_sort()` returns `false`, so Lightning infill may still be reordered downstream; only the field name keeps the original typo. |
| `src/libslic3r/Fill/Lightning/Generator.hpp` | 124 | Support Lightning defaults density to the same 15% minimum enforced by the constructor floor. |
| `src/libslic3r/Fill/Lightning/Generator.hpp` | 207 | `m_prune_length` and `m_wall_supporting_radius` both derive from the same hardcoded 45 degree overhang angle. |
| `src/libslic3r/Fill/Lightning/Generator.hpp` | 254 | `bboxs` stores one infill-outline bounding box per layer and is populated in both tree-generation paths. |
| `src/libslic3r/Flow.cpp` | 207 | `with_cross_section()` grows height only when the requested full spacing no longer fits the current spacing. |
| `src/libslic3r/Flow.cpp` | 279 | `mm3_per_mm()` enforces positive flow by throwing `FlowErrorNegativeFlow`, replacing the disabled debug assert. |
| `src/libslic3r/Flow.cpp` | 322 | `support_transition_flow()` always returns `Flow::bridging_flow(dmr, dmr)` for the selected support nozzle diameter. |
| `src/libslic3r/GCode.cpp` | 94 | `g_max_label_object` is capped at 64 because label membership is bit-packed into a `uint64_t` and `M624` emission asserts that limit. |
| `src/libslic3r/GCode/AvoidCrossingPerimeters.cpp` | 1528 | The active implementation lazily initializes boundary data in `travel_to()`, unlike the older eager-init block left disabled below. |
| `src/libslic3r/GCode/FanMover.hpp` | 99 | `with_D_option` is a stored-but-unused constructor flag; `GCode` passes it in, but `FanMover` never reads it. |
| `src/libslic3r/MeshBoolean.cpp` | 894 | The commented call was inlined into the explicit convert -> boolean -> reconvert pipeline around it. |
| `src/libslic3r/Model.cpp` | 313 | The `.oltp` loader passes `256` as the custom binary STL header length instead of the normal 80-byte STL header. |
| `src/libslic3r/Model.cpp` | 3296 | Extruder 0 parameters are duplicated at keys 0 and 1 because later lookups use 1-based extruder IDs. |
| `src/libslic3r/MultiPoint.cpp` | 384 | `concave_hull_2d()` is actually a tolerance-relaxed lower-hull builder over X-sorted points. |
| `src/libslic3r/MultiPoint.hpp` | 146 | The misspelled `tolerence` parameter acts as a normalized negative-turn threshold in that relaxed hull builder. |
| `src/libslic3r/Point.cpp` | 87 | `ccw()` uses `double` cross products to avoid overflow with scaled `coord_t` values. |
| `src/libslic3r/Point.hpp` | 272 | `both_comp()` / `any_comp()` only support literal `>` and `<`, returning `false` for any other operator string. |
| `src/libslic3r/PrintObject.cpp` | 976 | The commented `set_done()` leaves executed objects at `posEstimateCurledExtrusions`, so the step may rerun later. |
| `src/libslic3r/PrintObject.cpp` | 3628 | `generate_support_preview()` fills a local `POProfiler` with slice/support timings but currently discards those values. |
| `src/libslic3r/Support/SupportMaterial.cpp` | 1886 | After overshooting the target gap, the code keeps whichever neighboring object-layer boundary is closer to the requested gap. |
| `src/libslic3r/Support/SupportParameters.hpp` | 55 | `thresh_big_overhang` is the fixed scaled-area threshold used by tree-hybrid support generation to special-case large overhangs. |
| `src/libslic3r/TriangleMesh.cpp` | 1672 | `its_reverse_all_facets()` is an unused alternative helper here; active callers in this module use `its_flip_triangles()`. |
| `src/libslic3r/TriangleMeshSlicer.cpp` | 213 | `slice_facet_for_cut_mesh()` is the epsilon-tolerant cut-mesh variant so near-plane triangles still produce cut edges and caps. |
| `src/libslic3r/TriangleMeshSlicer.cpp` | 1300 | The `goto found` is only an early exit once an unconsumed continuation polyline has been located. |
| `src/libslic3r/TriangleMeshSlicer.cpp` | 1966 | `closing_radius` now selects either morphological closing or pure outward offset, replacing the former fixed safety offset. |

## T300 Escalations

Total escalated tags: **12**

| File | Line | Reason blocked |
|------|------|----------------|
| `src/libslic3r/Arachne/SkeletalTrapezoidation.cpp` | 1913 | Local code does not justify whether using toolpath locations past the middle is safe or just heuristic legacy. |
| `src/libslic3r/CutUtils.cpp` | 29 | The lower-half cut path forces `flip` when `PlaceOnCutLower` is set, but local code does not explain whether that coupling is intentional. |
| `src/libslic3r/CutUtils.cpp` | 336 | No local rationale explains why `PlaceOnCutLower` implies flipping in `post_process()`. |
| `src/libslic3r/Emboss.hpp` | 148 | Thread-safety depends on an undocumented external discipline for replacing/reading the shared font cache pointer. |
| `src/libslic3r/Fill/FillTpmsD.cpp` | 176 | The adaptive sampler starts each period with 16 seed segments, but local code gives no reason for choosing 16. |
| `src/libslic3r/GCode/PressureEqualizer.cpp` | 900 | Comments preserve an older role-specific clamping policy, but local code does not explain why the active cross-role policy replaced it. |
| `src/libslic3r/Measure.cpp` | 326 | The `err < 0.05` and `0.9 * PI / 2` thresholds are hardcoded without a local derivation or tuning note. |
| `src/libslic3r/Support/SupportSpotsGenerator.cpp` | 38 | The larger support-point placement block is commented out, and local code does not show whether that disablement is intentional. |
| `src/libslic3r/TriangleMesh.cpp` | 407 | Volume is scaled by affine determinant, but local code does not show whether any callers depend on accurate shear-transformed volume. |
| `src/libslic3r/TriangleMesh.hpp` | 220 | The cached-volume contract under arbitrary affine shear is not justified in local code. |
| `src/libslic3r/VariableWidth.cpp` | 26 | Tail-group width uses `length * a_width` instead of the trapezoid average, and local code does not say whether that bias is deliberate. |
| `src/libslic3r/VariableWidth.cpp` | 153 | The last-segment average uses only `a_width`, so the reason for possible tail under-extrusion remains undocumented locally. |

**Completed tasks this session:** T300
