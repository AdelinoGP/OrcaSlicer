# Ralph Task Registry — OrcaSlicer Test Contract Agent
Last updated: 2026-03-11T00:00:00Z

## Legend
- [ ] PENDING   — not started
- [~] ACTIVE    — in progress (only ONE task should be ACTIVE at a time)
- [x] DONE      — complete and committed
- [!] BLOCKED   — cannot proceed without resolution (add reason inline)

---

## Phase 0 — Orientation

- [x] T001  Read ralph-tasks.md and identify resume point
- [x] T002  Read tests/CLAUDE.md (Catch2 safety rules)
- [x] T003  Read CMakeLists.txt for all 5 in-scope suites
- [x] T004  Read tests/test_utils.hpp
- [x] T005  Update agent_journal.md living status block
- [x] T006  Orientation commit: `orient: test-contract pass — orientation complete`

---

## Phase 1 — libslic3r Suite

- [x] T101  document: tests/libslic3r/test_stl.cpp
            Source under test: src/libslic3r/Format/STL.cpp
            Commit: `docs: test_stl.cpp contracts (T101)`

- [x] T102  document: tests/libslic3r/test_indexed_triangle_set.cpp
            Source under test: src/libslic3r/TriangleMesh.cpp
            Commit: `docs: test_indexed_triangle_set.cpp contracts (T102)`

- [x] T103  document: tests/libslic3r/test_geometry.cpp
            Source under test: src/libslic3r/Geometry.cpp + Geometry/
            Commit: `docs: test_geometry.cpp contracts (T103)`

- [x] T104  document: tests/libslic3r/test_polygon.cpp
            Source under test: src/libslic3r/Polygon.cpp
            Commit: `docs: test_polygon.cpp contracts (T104)`

- [x] T105  document: tests/libslic3r/test_mutable_polygon.cpp
            Source under test: src/libslic3r/MutablePolygon.cpp
            Commit: `docs: test_mutable_polygon.cpp contracts (T105)`

- [x] T106  document: tests/libslic3r/test_clipper_utils.cpp
            Source under test: src/libslic3r/ClipperUtils.cpp
            Commit: `docs: test_clipper_utils.cpp contracts (T106)`

- [x] T107  document: tests/libslic3r/test_clipper_offset.cpp
            Source under test: src/libslic3r/ClipperUtils.cpp (offset path)
            Commit: `docs: test_clipper_offset.cpp contracts (T107)`

- [x] T108  document: tests/libslic3r/test_voronoi.cpp
            Source under test: src/libslic3r/Geometry/Voronoi.cpp
            Commit: `docs: test_voronoi.cpp contracts (T108)`

- [x] T109  document: tests/libslic3r/test_elephant_foot_compensation.cpp
            Source under test: src/libslic3r/ElephantFootCompensation.cpp
            Commit: `docs: test_elephant_foot_compensation.cpp contracts (T109)`

- [x] T110  document: tests/libslic3r/test_config.cpp
            Source under test: src/libslic3r/Config.cpp
            Commit: `docs: test_config.cpp contracts (T110)`

- [x] T111  document: tests/libslic3r/test_appconfig.cpp
            Source under test: src/libslic3r/AppConfig.cpp
            Commit: `docs: test_appconfig.cpp contracts (T111)`

- [x] T112  document: tests/libslic3r/test_placeholder_parser.cpp
            Source under test: src/libslic3r/PlaceholderParser.cpp
            Commit: `docs: test_placeholder_parser.cpp contracts (T112)`

- [x] T113  document: tests/libslic3r/test_3mf.cpp
            Source under test: src/libslic3r/Format/3mf.cpp + bbs_3mf.cpp
            Fixture data: tests/data/test_3mf/
            Commit: `docs: test_3mf.cpp contracts (T113)`

- [x] T114  document: tests/libslic3r/test_meshboolean.cpp
            Source under test: src/libslic3r/MeshBoolean.cpp
            Commit: `docs: test_meshboolean.cpp contracts (T114)`

- [x] T115  document: tests/libslic3r/test_marchingsquares.cpp
            Source under test: src/libslic3r/ (marching squares / raster ops)
            Commit: `docs: test_marchingsquares.cpp contracts (T115)`

- [x] T116  document: tests/libslic3r/test_optimizers.cpp
            Source under test: src/libslic3r/Optimize/
            Commit: `docs: test_optimizers.cpp contracts (T116)`

- [x] T117  document: tests/libslic3r/test_mutable_priority_queue.cpp
            Source under test: src/libslic3r/MutablePriorityQueue.hpp
            Commit: `docs: test_mutable_priority_queue.cpp contracts (T117)`

- [x] T118  document: tests/libslic3r/test_timeutils.cpp
            Source under test: src/libslic3r/Time.cpp
            Commit: `docs: test_timeutils.cpp contracts (T118)`

- [x] T119  document: tests/libslic3r/test_aabbindirect.cpp
            Source under test: src/libslic3r/AABBTreeIndirect.hpp
            Commit: `docs: test_aabbindirect.cpp contracts (T119)`

- [x] T120  document: tests/libslic3r/test_hollowing.cpp
             Source under test: src/libslic3r/SLA/Hollowing.cpp + OpenVDBUtils.cpp
             Note: OpenVDB-conditional — only built when TARGET OpenVDB::openvdb present
             Commit: `docs: test_hollowing.cpp contracts (T120)`

- [x] T121  document: tests/libslic3r/test_bambu_networking.cpp
             Source under test: src/slic3r/Utils/ networking layer
             Commit: `docs: test_bambu_networking.cpp contracts (T121)`

---

## Phase 2 — fff_print Suite

- [x] T201  document: tests/fff_print/test_data.cpp + test_data.hpp
            Role: shared fixture builder — special format (fixture table, not test table)
            Commit: `docs: test_data fixture inventory (T201)`

- [ ] T202  document: tests/fff_print/test_flow.cpp
            Source under test: src/libslic3r/Flow.cpp
            Commit: `docs: test_flow.cpp contracts (T202)`

- [ ] T203  document: tests/fff_print/test_fill.cpp
            Source under test: src/libslic3r/Fill/
            Commit: `docs: test_fill.cpp contracts (T203)`

- [ ] T204  document: tests/fff_print/test_extrusion_entity.cpp
            Source under test: src/libslic3r/ExtrusionEntity.cpp
            Commit: `docs: test_extrusion_entity.cpp contracts (T204)`

- [ ] T205  document: tests/fff_print/test_model.cpp
            Source under test: src/libslic3r/Model.cpp
            Commit: `docs: test_model.cpp contracts (T205)`

- [ ] T206  document: tests/fff_print/test_print.cpp
            Source under test: src/libslic3r/Print.cpp
            Commit: `docs: test_print.cpp contracts (T206)`

- [ ] T207  document: tests/fff_print/test_printobject.cpp
            Source under test: src/libslic3r/PrintObject.cpp
            Commit: `docs: test_printobject.cpp contracts (T207)`

- [ ] T208  document: tests/fff_print/test_trianglemesh.cpp
            Source under test: src/libslic3r/TriangleMesh.cpp
            Commit: `docs: test_trianglemesh.cpp contracts (T208)`

- [ ] T209  document: tests/fff_print/test_gcode.cpp
            Source under test: src/libslic3r/GCode.cpp
            Commit: `docs: test_gcode.cpp contracts (T209)`

- [ ] T210  document: tests/fff_print/test_gcodewriter.cpp
            Source under test: src/libslic3r/GCodeWriter.cpp
            Fixture data: tests/data/fff_print_tests/test_gcodewriter/
            Commit: `docs: test_gcodewriter.cpp contracts (T210)`

- [ ] T211  document: tests/fff_print/test_printgcode.cpp
            Source under test: src/libslic3r/GCode.cpp (full print→gcode pipeline)
            Commit: `docs: test_printgcode.cpp contracts (T211)`

- [ ] T212  document: tests/fff_print/test_skirt_brim.cpp
            Source under test: src/libslic3r/Brim.cpp
            Commit: `docs: test_skirt_brim.cpp contracts (T212)`

- [ ] T213  document: tests/fff_print/test_support_material.cpp
            Source under test: src/libslic3r/Support/SupportMaterial.cpp
            Commit: `docs: test_support_material.cpp contracts (T213)`

---

## Phase 3 — sla_print Suite

- [ ] T301  document: tests/sla_print/sla_print_tests.cpp
            Source under test: src/libslic3r/SLAPrint.cpp + SLAPrintSteps.cpp
            Commit: `docs: sla_print_tests.cpp contracts (T301)`

- [ ] T302  document: tests/sla_print/sla_raycast_tests.cpp
            Source under test: src/libslic3r/SLA/IndexedMesh.cpp + AABBMesh.cpp
            Commit: `docs: sla_raycast_tests.cpp contracts (T302)`

- [ ] T303  document: tests/sla_print/sla_supptgen_tests.cpp
            Source under test: src/libslic3r/SLA/SupportPointGenerator.cpp
            Commit: `docs: sla_supptgen_tests.cpp contracts (T303)`

---

## Phase 4 — libnest2d Suite

- [ ] T401  document: tests/libnest2d/libnest2d_tests_main.cpp
            Source under test: deps_src/libnest2d/ (2D bin-packing)
            Fixture: tests/libnest2d/printer_parts.cpp
            Commit: `docs: libnest2d contracts (T401)`

---

## Phase 5 — slic3rutils Suite

- [ ] T501  document: tests/slic3rutils/slic3rutils_tests_main.cpp
            Source under test: src/libslic3r/ utility functions
            Commit: `docs: slic3rutils contracts (T501)`

---

## Phase 6 — Finalization

- [ ] T600  Final commit: update agent_journal.md status block, write
            06_test_contracts.md header sections (Purpose, Build & Run Reference,
            Catch2 Safety Rules), commit all.
            Commit: `docs: complete test contracts — 06_test_contracts.md (T600)`
```