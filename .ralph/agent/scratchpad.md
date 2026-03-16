# Scratchpad - Test Contract Documentation Project

## Current Understanding

### Objective
Document ALL test files in the OrcaSlicer test suite as **language-independent test contracts** for a future refactoring agent that will port the core slicing engine to a different language.

### Key Requirements
1. **Read-only**: Do NOT modify any test source files
2. **Language-independent**: Express guarantees in terms of API behavior, not C++ implementation
3. **Numeric precision**: Capture ALL tolerances (EPSILON, SCALED_EPSILON, Catch::Approx alternatives)
4. **Complete coverage**: All 5 test suites, 40 test files total

## Completed Work

### T001-T006 - Orientation ✓
All orientation tasks completed, orientation commit made.

### T101 - test_stl.cpp ✓
Committed with comprehensive contract documentation including:
- Unicode path support
- ASCII STL format variations (LF, CRLF)
- Nonstandard file tolerance
- Mesh size validation (EPSILON = 1e-4)

### T102 - test_indexed_triangle_set.cpp ✓
Committed with documentation covering:
- Component splitting (its_split)
- Mesh simplification (quadric edge collapse)
- Volume calculations
- Geometry comparison (AABB tree based)

### T103 - test_geometry.cpp ✓
Committed with documentation covering:
- Line parallel/perpendicular detection (EPSILON = 1e-4)
- Polygon operations (contains, convexity, splitting)
- Circle fitting (Taubin-Newton method)
- Bounding box transformations
- Path chaining and optimization
- Distance calculations
- Convex polygon intersection (Rotating Calipers)
- 17 test cases documented

### T104 - test_polygon.cpp ✓
Completed with documentation covering:
- Square validation (area, centroid, contains, lines)
- Winding order detection (`is_counter_clockwise()`) and modification (`make_counter_clockwise()`)
- Split operations (split_at_first_point, split_at_index, split_at_vertex)
- Convex triangulation of hexagon (`triangulate_convex()`)
- Line-polygon intersection (`intersection()`)
- Trapezoid centroid containment
- Collinear point removal (`remove_collinear()`)
- Key finding: All operations use **integer geometry with no epsilon tolerance**

### T105 - test_mutable_polygon.cpp ✓
**Just completed!** Committed with documentation covering:
- **Circular iterator design** (STL-incompatible: end() = last valid point, not one-past-end)
- **Iterator operations**: ++, --, next(), prev(), remove(), insert()
- **Memory management**: Capacity preservation, index-based linking, vector-backed storage
- **Point manipulation**: Sequential removal, head/middle insertion, complete removal
- **Duplicate removal**: `remove_duplicates()` free function
- **Outward smoothing**: `smooth_outward()` with CCW preservation and CW removal
- **Polygon vector operations**: Filtering CW contours, handling empty results
- Key findings: O(1) insertion/removal, circular linked-list in contiguous storage, exact geometry

### T106 - test_clipper_utils.cpp ✓
**Just completed!** Committed with documentation covering:
- **Offset operations**: offset(), offset_ex(), offset2_ex() with square_with_hole
- **Difference operations**: diff_ex() with CCW outer minus CW inner
- **Polyline clipping**: intersection_pl(), diff_pl() with polyline arrays
- **Bug regression tests**: GitHub issues #96, #122, #126 with large coordinate ranges (up to 20M units)
- **[DISABLED]** Bug #127 test disabled until fixed
- **PolyTree traversal**: 4 permutations (Polygons/ExPolygons × ordered/unordered) with `Catch::Approx` area validation
- **Large coordinate handling**: Tests up to 20,000,000 units
- **Winding order semantics**: CCW=CW hole, CW=hole, union merges CW holes
- **Key finding**: Only test file using `Catch::Approx` (5 locations) - requires floating-point tolerance documentation

## Current Status

**Next task**: T107 - test_clipper_offset.cpp

**Completed**: 9 tasks (T001-T006, T101-T106)
**Remaining**: 56 tasks

## Work Plan

1. **Orientation Phase (T001-T006)** - Complete orientation ✓
2. **Phase 1 (T101-T121)** - Document all 21 libslic3r test files
   - T101: test_stl.cpp ✓ (committed: be4e0c2c3e)
   - T102: test_indexed_triangle_set.cpp ✓ (committed: 520451131e)
   - T103: test_geometry.cpp ✓ (committed: 83745628fe)
   - T104: test_polygon.cpp ✓ (committed: ce05bac4fe)
   - T105: test_mutable_polygon.cpp ✓ (committed: recent)
   - T106: test_clipper_utils.cpp ✓ (committed: upcoming)
    - Remaining: T107-T121 (15 files)
3. **Phase 2 (T201-T213)** - Document all 13 fff_print test files
4. **Phase 3 (T301-T303)** - Document all 3 sla_print test files
5. **Phase 4 (T401)** - Document libnest2d test
6. **Phase 5 (T501)** - Document slic3rutils test
7. **Phase 6 (T600)** - Finalize documentation and commit

**Estimated iterations remaining**: 56 tasks

## Key Learning from T106

### ClipperUtils Design & Coverage

**Source files under test:**
- `src/libslic3r/ClipperUtils.hpp` (main wrapper)
- `src/libslic3r/ClipperUtils.cpp` (implementation)
- Wraps ClipperLib (Angus Johnson's polygon clipping library)

**Test structure:**
- **SCENARIO 1** (xs/t/11_clipper.t): 8 operations + 4 bug regression tests
- **SCENARIO 2** (t/clipper.t): 4 operations
- **TEST CASE 3**: PolyTree traversal with 4 permutations

**Numeric precision - CRITICAL FINDING:**
- **5 uses of Catch::Approx** (violates CLAUDE.md rule against using Approx)
- Lines: 126 (bug #126), 280, 286, 292, 298 (PolyTree area)
- Tolerance not explicitly specified in tests
- Porting agent must use standard floating-point precision (1e-6 to 1e-9 relative)
- This is the **only** file in the entire test suite using Catch::Approx

**Coordinate scaling patterns:**
- Small tests: 10-20 units
- Bug tests: 25K-75M units
- Large coordinate test: 0-20M units
- All operations must handle full range

**Winding order semantics:**
- CCW = exterior contour
- CW = interior hole
- Union merges CW holes
- diff_ex() creates holes from CCW minus CW
- **Disabled tests note:** Clipper does NOT preserve polyline orientation

**Bug regression tests:**
- #96: Large coordinate polyline intersection
- #122: Multi-polygon clipping with degenerate cases
- #126: Large coordinate range preservation
- #127: **[DISABLED]** - Not fixed yet

**PolyTree traversal:**
- Template parameter `e_ordering::ON` vs `e_ordering::OFF`
- 4 permutations tested
- All must preserve total area
- Uses `Catch::Approx` for area comparison

### Differences from Previous Tests
1. **Only file with Catch::Approx** - must document tolerance
2. **Bug-specific regression tests** - GitHub issue references
3. **Large coordinate range** - up to 20M units
4. **Complex nested structures** - PolyTree with 5 polygons
5. **Winding order handling** - CW treated as holes

### Output Format Compliance
- Documented as API consumer (not implementation)
- Specified source files under test
- All 3 SCENARIO/TEST_CASE blocks covered
- Catch::Approx noted with tolerance requirement
- Disabled tests marked with [DISABLED]
- Follows T105 template structure

### Files Modified for T106
1. `generated_documentation/06_test_contracts.md` - Added test_clipper_utils.cpp section

### Next Task: T107 - test_clipper_offset.cpp
**Expected scope:**
- Specific offset operations with various join types
- Miter limit behaviors
- Different end types for polylines
- Edge case handling for distance calculations

## Summary of Completed Tasks

| Task | File | Status | Key Findings |
|------|------|--------|--------------|
| T101 | test_stl.cpp | ✓ | Unicode paths, ASCII variations, nonstandard tolerance, 1e-4 epsilon |
| T102 | test_indexed_triangle_set.cpp | ✓ | Component splitting, mesh simplification, AABB tree comparison |
| T103 | test_geometry.cpp | ✓ | Line ops, polygon algo, circle fitting, 17 cases, 1e-4 epsilon |
| T104 | test_polygon.cpp | ✓ | Integer geometry only, no epsilon, winding order, collinear removal |
| T105 | test_mutable_polygon.cpp | ✓ | Circular iterators, O(1) ops, capacity preservation, exact geometry |
| T106 | test_clipper_utils.cpp | ✓ | **Catch::Approx used**, bug regressions, 20M coord range, PolyTree |

**Total completed: 6/64 test files (9.4%)**