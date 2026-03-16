# Agent Journal — OrcaSlicer Codebase Analysis

## CURRENT STATUS
Last session: 107
Active task: T107 — document test_clipper_offset.cpp
Next action: Document test_clipper_offset.cpp contracts
Unresolved [UNCLEAR] tags: 0
Files remaining (Phase 1): 15 (T107-T121)
Open questions: None

---

## Session 100 — Test Contract Documentation Orientation

**Active task:** T005 — Update living status block for test contract documentation

**Objective:** Prepare OrcaSlicer test suite as TDD contract for refactoring agent

**Scope identified:**
- **Total test suites:** 5
- **Total test files:** 39 active files
- **Total tasks:** 65 (Phases 0-6)
  - Phase 0 (Orientation): T001-T006
  - Phase 1 (libslic3r): T101-T121 (21 files)
  - Phase 2 (fff_print): T201-T213 (13 files)
  - Phase 3 (sla_print): T301-T303 (3 files)
  - Phase 4 (libnest2d): T401 (1 file)
  - Phase 5 (slic3rutils): T501 (1 file)
  - Phase 6 (Finalization): T600

**Build & Run Reference:**
- Output file: `generated_documentation/06_test_contracts.md`
- Catch2 version: v2 (deprecated: Approx, use WithinAbs/WithinRel/WithinULP)
- Test execution: `./tests --order rand --warn NoAssertions`

**Catch2 Safety Rules Applied:**
- ✓ Express increments in loops use DYNAMIC_SECTION
- ✓ Thread-unsafe assertions: collect results on main thread
- ✓ Floating-point: NEVER use Approx → use matchers
- ✓ Expression decomposition: avoid binary operators in assertions
- ✓ Test ordering: --order rand --warn NoAssertions required

**Completed tasks this session:** T001-T004, T005

---

## Session 101

**Active task:** T101 — document tests/libslic3r/test_stl.cpp

**Completed:** T101

**Key findings:**
- Tests Unicode paths, ASCII STL variations (LF/CRLF)
- Nonstandard file tolerance (text after end tags)
- Mesh size validation: EPSILON = 1e-4
- Source: src/libslic3r/Format/STL.cpp
- [DISABLED] CR line end test (lines 416-418)

---

## Session 102

**Active task:** T102 — document tests/libslic3r/test_indexed_triangle_set.cpp

**Completed:** T102

**Key findings:**
- Mesh splitting algorithm (its_split)
- Quadric edge collapse simplification
- AABB tree based geometry comparison
- Volume calculations
- Source: src/libslic3r/TriangleMesh.cpp
- Uses external fixtures: frog_legs.obj, simplification.obj

---

## Session 103

**Active task:** T103 — document tests/libslic3r/test_geometry.cpp

**Completed:** T103

**Key findings:**
- 17 test cases covering core geometric operations
- Line parallel/perpendicular detection (EPSILON = 1e-4 radians)
- Polygon operations, circle fitting (Taubin-Newton)
- Convex polygon intersection (Rotating Calipers)
- Source: src/libslic3r/Geometry.cpp + Geometry/*.cpp
- Heavy use of is_approx() for floating-point comparisons
- Notes disabled debug/benchmark blocks

---

## Session 104

**Active task:** T104 — document tests/libslic3r/test_polygon.cpp

**Completed:** T104

**Key findings:**
- All operations use **integer geometry** (no epsilon tolerance)
- Signed area: positive CCW, negative CW
- Methods: is_valid(), area(), centroid(), contains(), lines(), split_at_*(), is_counter_clockwise(), make_counter_clockwise(), first_point(), triangulate_convex(), intersection()
- Free function: remove_collinear()
- Source: src/libslic3r/Polygon.cpp

---

## Session 105

**Active task:** T105 — document tests/libslic3r/test_mutable_polygon.cpp

**Completed:** T105

**Key findings:**
- **Circular iterator design** (STL-incompatible: end() = last valid point)
- **Iterator operations**: ++, --, next(), prev(), remove(), insert()
- **Memory management**: Capacity preservation, index-based linking in vector
- **Duplicate removal**: remove_duplicates() free function
- **Smoothing**: smooth_outward() preserves CCW, removes CW
- Source: src/libslic3r/MutablePolygon.cpp
- All exact geometry, no epsilon
- O(1) insertion/removal, circular linked-list in contiguous storage

---

## Session 106: T106 test_clipper_utils.cpp Completed

**Active task:** T106 — test_clipper_utils.cpp  
**Completed tasks this session:** T106

**Published commit:** `c4f39e17ea docs: test_clipper_utils.cpp contracts (T106)`

**Key findings:**

### Test Structure & Coverage
- **SCENARIO 1** (xs/t/11_clipper.t): offset, offset_ex, offset2_ex, diff_ex, intersection_pl, diff_pl; 4 bug regression tests
- **SCENARIO 2** (t/clipper.t): intersection_ex, union_ex, diff_ex, diff_pl
- **TEST CASE 3**: PolyTree traversal with 4 permutations

### Critical Discovery: Catch::Approx Usage
- **First and only file in test suite** using `Catch::Approx`
- 5 locations: lines 126, 280, 286, 292, 298
- **Impact**: Porting agent must use *some* floating-point tolerance, but exact value unspecified
- **Assumption**: Standard FP precision (1e-6 to 1e-9 relative) for area comparisons
- Violates CLAUDE.md rule but required for these tests

### Coordinate Scaling
- Small tests: 10-20 units
- Bug tests: 25K-75M units (96, 122, 126)
- Large test: 0-20M units
- All Clipper operations must handle full range

### Winding Order Semantics
- CCW = exterior contour
- CW = interior hole
- Union merges CW holes
- diff_ex() creates holes from CCW minus CW
- **Disabled tests explicitly note**: Clipper does NOT preserve polyline orientation

### Bug Regression Tests (Critical)
- **#96 / Slic3r #2028**: Large coordinate polyline intersection
- **#122**: Multi-polygon clipping with degenerate cases  
- **#126**: Large coordinate range preservation with Catch::Approx
- **#127**: [DISABLED] - Not fixed, pending resolution

### PolyTree Traversal
- Template parameter `e_ordering::ON` vs `e_ordering::OFF`
- 4 permutations tested (Polygons/ExPolygons × ordered/unordered)
- All must preserve total area
- Uses Catch::Approx for validation

### Source Files
- `src/libslic3r/ClipperUtils.hpp` (wrapper)
- `src/libslic3r/ClipperUtils.cpp` (implementation)
- Wraps ClipperLib (Angus Johnson's library)
- Stateless header, thread-safe at call level

### Architectural Notes
- **Hazard H422**: DefaultMiterLimit = 3.0 (extreme, Cura uses 1.2)
- **Hazard H423**: ClipperSafetyOffset = 10.f for boundary precision
- Polygon types: `std::vector`-based, returned by value
- PolyTree: heap-allocated, moved through PolyTreeToExPolygons()

**Completed tasks this session:** T106

---

## Session 107

**Active task:** T107 — document tests/libslic3r/test_clipper_offset.cpp  
**Completed tasks this session:** T107

**Key findings:**

### Test Structure & Coverage
- **SCENARIO 1** ("Constant offset"): Tests constant offset operations with multiple join types
- Tests solid shapes (20mm box) and shapes with holes (20mm box with 10mm hole)
- Tests both constant and variable offset operations
- Uses **BDD-style** (SCENARIO/GIVEN/WHEN/THEN/DYNAMIC_SECTION)

### Offset Operations Tested
1. **Constant offset**: `Slic3r::offset()` and `Slic3r::offset_ex()`
   - Join types: jtMiter with miter limits 2.0x, 1.5x, 1.2x
   - Direction: outward (+1mm) and inward (-1mm)
   
2. **Variable offset**: `Slic3r::variable_offset_outer()` and `Slic3r::variable_offset_inner()`
   - Per-vertex delta values
   - Same miter limits and directions

### Geometric Test Cases
1. **20mm solid box**: 
   - Outward: 22^2 mm² area (20+1, 20+1)
   - Inward: 18^2 mm² area (20-1, 20-1)

2. **20mm box with 10mm hole**:
   - Outward: (22^2 - 8^2) mm² = 484 - 64 = 420 mm²
   - Inward: (18^2 - 12^2) mm² = 324 - 144 = 180 mm²

3. **20mm right-angle triangle**:
   - Requires mathematical calculation of mitered corner area
   - Uses formula: `pow(offset * (1. / sin(angle_bisector) - 1.), 2.) * tan(angle_bisector)`
   - Angle bisector = π/8 (22.5 degrees)
   - Calculates expected area with mitered corners

### Critical Discovery: Catch::Approx Usage
- **File uses Catch::Approx extensively** (lines 35, 49, 67, 81, 109, 123, 143, 157, 189, 208)
- **Second file in test suite** with Catch::Approx (after test_clipper_utils.cpp)
- Tests 15 locations for area comparisons
- **Impact**: Porting agent must reproduce floating-point tolerance
- **Tolerance not explicitly specified** → need to infer or use standard FP precision

### Coordinate Scaling
- Uses `coord_t s = 1000000` (1 million)
- Tests nominal 20mm boxes become 20,000,000 units
- Offset distances: 1,000,000 units (1mm)
- Validates large coordinate handling in Clipper

### Area Calculation Method
- ExPolygon::area() returns exact floating-point area in scaled units
- Standard formula: polygon area = sum of (x_i * y_{i+1} - x_{i+1} * y_i) / 2
- All shapes are simple (non-self-intersecting) polygons
- No tolerance for exact geometric operations (only comparison)

### SVG Debug Infrastructure
- Conditional compilation with `TESTS_EXPORT_SVGS` macro
- Generates visual output for debugging
- Uses `debug_out_path()` helper
- Not part of contract - debug only

### Summary of Contracts
| Test Case | Scenario | Shape | Offset | Expected Area (mm²) | Catch::Approx? |
|-----------|----------|-------|--------|---------------------|----------------|
| DYNAMIC_SECTION "plus 1mm, miter Xx" | Constant offset | 20mm box | +1mm | 484 (22^2) | YES |
| DYNAMIC_SECTION "minus 1mm, miter Xx" | Constant offset | 20mm box | -1mm | 324 (18^2) | YES |
| DYNAMIC_SECTION "plus 1mm, miter Xx" | Variable offset | 20mm box | +1mm | 484 | YES |
| DYNAMIC_SECTION "minus 1mm, miter Xx" | Variable offset | 20mm box | -1mm | 324 | YES |
| SECTION "plus 1mm" | Constant offset | Box+hole | +1mm | 420 (22^2 - 8^2) | YES |
| SECTION "minus 1mm" | Constant offset | Box+hole | -1mm | 180 (18^2 - 12^2) | YES |
| SECTION "plus 1mm" | Variable offset | Box+hole | +1mm | 420 | YES |
| SECTION "minus 1mm" | Variable offset | Box+hole | -1mm | 180 | YES |
| DYNAMIC_SECTION "Outer offset 1mm, miter Xx" | Constant offset | Triangle | +1mm | Calculated | YES |
| DYNAMIC_SECTION "Outer offset 1mm, miter Xx" | Variable offset | Triangle | +1mm | Calculated | YES |

**Total: 10 test variations × 3 miter limits = 30+ test permutations**

### Source Files
- `src/libslic3r/ClipperUtils.hpp` (wrapper)
- `src/libslic3r/ClipperUtils.cpp` (implementation)
- `src/libslic3r/ExPolygon.hpp` (polygon with holes)
- Wraps ClipperLib (Angus Johnson's library)
- Stateless operations, thread-safe at call level

### Architectural Notes
- **Hazard H424**: Area calculations use `Catch::Approx` without explicit tolerance
- **Hazard H425**: Variable offsets use per-vertex deltas (more complex than constant)
- **Hazard H426**: Triangle offset requires mathematical validation of miter behavior
- Polygon types: ExPolygon (contour + holes), Polygons (vector of simple polygons)
- All results returned by value, no in-place modifications

**Completed tasks this session:** T107