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

## Current Status

**Next task**: T106 - test_clipper_utils.cpp

**Completed**: 8 tasks (T001-T006, T101-T105)
**Remaining**: 57 tasks

## Work Plan

1. **Orientation Phase (T001-T006)** - Complete orientation ✓
2. **Phase 1 (T101-T121)** - Document all 21 libslic3r test files
   - T101: test_stl.cpp ✓ (committed: be4e0c2c3e)
   - T102: test_indexed_triangle_set.cpp ✓ (committed: 520451131e)
   - T103: test_geometry.cpp ✓ (committed: 83745628fe)
   - T104: test_polygon.cpp ✓ (committed: ce05bac4fe)
    - Remaining: T106-T121 (16 files)
3. **Phase 2 (T201-T213)** - Document all 13 fff_print test files
4. **Phase 3 (T301-T303)** - Document all 3 sla_print test files
5. **Phase 4 (T401)** - Document libnest2d test
6. **Phase 5 (T501)** - Document slic3rutils test
7. **Phase 6 (T600)** - Finalize documentation and commit

**Estimated iterations remaining**: 57 tasks

## Key Learning from T104

### Test Pattern Discovery
- test_polygon.cpp focuses on **Polygon class methods**
- Uses **exact integer geometry** throughout - no epsilon tolerance
- Signed area based on winding order (CCW = positive, CW = negative)
- `remove_collinear()` is a **free function**, not a method
- All operations preserve topology and topology properties

### Source Methods Documented
- `is_valid()`, `area()`, `centroid()`, `contains()`
- `lines()`, `split_at_*()`, `is_counter_clockwise()`, `make_counter_clockwise()`
- `first_point()`, `triangulate_convex()`, `intersection()`
- Free function: `remove_collinear()`

### Next File: T105 - test_mutable_polygon.cpp
Expected to test in-place polygon modifications, likely covering:
- `clear()`, `append()`, `insert()`, `erase()`
- `reverse()`, `rotate()`, `translate()`, `scale()`
- In-place simplification operations
- Memory management and reference semantics

## Learning from T103

### Test structure patterns observed:
- test_geometry.cpp uses both TEST_CASE and BDD-style SCENARIO/GIVEN/WHEN/WHEN
- Mix of direct function tests and behavioral/algorithmic tests
- Multiple disabled code blocks for debug/unused features
- No external fixture data - all inline test data
- Heavy use of geometric primitives: Line, Point, Polygon, Polyline, BoundingBox

### Key source files under test:
- src/libslic3r/Geometry.cpp (main namespace)
- src/libslic3r/Geometry/Circle.cpp
- src/libslic3r/Geometry/ConvexHull.cpp
- src/libslic3r/ClipperUtils.cpp
- src/libslic3r/ShortestPath.cpp
- Core types: Point.cpp, Line.cpp, Polygon.cpp, Polyline.cpp, BoundingBox.cpp

### Numeric precision requirements:
- EPSILON: 1e-4 (absolute tolerance for floating-point)
- SCALED_EPSILON: EPSILON / SCALING_FACTOR (~1e-7)
- is_approx() used extensively for Vec2d, Point comparisons
- Exact integer arithmetic for geometry predicates (intersection, containment)
- Angle tolerance in radians: 0.9*EPSILON vs 1.1*EPSILON for parallel/perpendicular

### Bot patterns to continue:
- Extract all TEST_CASE/SCENARIO names
- Identify tags for each test
- Document each section separately in guarantees
- Note disabled blocks with [DISABLED] prefix
- Record specific tolerance values (EPSILON, SCALED_EPSILON)
- Specify which source files are covered
- Mention when external fixtures are needed (none for geometry test)
- Use API consumer language (not implementation details)
- Be explicit about return values, input requirements, and tolerances

## Current Task Analysis: T104 - test_polygon.cpp

### File Structure Observed
- Uses BDD-style SCENARIO/GIVEN/WHEN/THEN
- 2 main SCENARIO blocks:
  1. "Converted Perl tests" with ccw_square, hexagon, general triangle
  2. "Remove collinear points from Polygon"
- 1 standalone TEST_CASE for trapezoid centroid
- All inline test data, no external fixtures
- Tests cover: area, centroid, contains, lines conversion, split operations, winding order, triangulation, intersection, collinear point removal

### Source Files Under Test
- src/libslic3r/Polygon.cpp (primary)
- src/libslic3r/Point.hpp (for Point type)
- Likely uses: src/libslic3r/Polyline.cpp, src/libslic3r/Line.hpp

### Key Methods Being Tested
- is_valid()
- area() - signed area
- centroid()
- contains()
- lines() - conversion to lines
- split_at_first_point()
- split_at_index()
- split_at_vertex()
- is_counter_clockwise()
- make_counter_clockwise()
- first_point()
- triangulate_convex()
- intersection() - line-polygon intersection
- remove_collinear() - free function

### Numeric Precision Notes
- No explicit tolerance used - all integer geometry
- Area calculations: exact integer arithmetic
- Centroid: uses floating point internally but exact comparison for square
- All coordinate values are integers in test cases

### Anticipated test file patterns:
- **T104 (test_polygon.cpp)**: ✓ Currently analyzing
- **T105 (test_mutable_polygon.cpp)**: Tests mutable polygon operations (in-place modifications)
- **T106 (test_clipper_utils.cpp)**: Tests Clipper wrapper functions (offset, intersection, union)
- **T107 (test_clipper_offset.cpp)**: Specific tests for offset operations
- Continue pattern of reading file, extracting tests, documenting contracts per test

## Current Task Analysis: T105 - test_mutable_polygon.cpp

### File Structure Observed
- Uses BDD-style SCENARIO/GIVEN/WHEN/THEN
- 3 SCENARIO blocks:
  1. "Iterators" - Tests iterator operations (++, --, remove, insert)
  2. "Remove degenerate points from MutablePolygon" - Tests remove_duplicates()
  3. "smooth_outward" - Tests outward smoothing algorithm
- All inline test data, no external fixtures
- Tests cover: iterator manipulation, point insertion/removal, duplicate removal, polygon smoothing

### Source Files Under Test
- src/libslic3r/MutablePolygon.hpp (primary interface)
- src/libslic3r/MutablePolygon.cpp (implementation)
- Related: src/libslic3r/Point.hpp, src/libslic3r/Polygon.hpp (conversion)

### Key Methods Being Tested
**MutablePolygon class:**
- Constructor with initializer list
- `begin()`, `end()` - iterator access
- `size()`, `empty()`, `capacity()` - state queries
- `insert(iterator, Point)` - point insertion
- `remove(iterator)` - point removal

**MutablePolygon::iterator class:**
- `operator++`, `operator--` - forward/backward navigation
- `next()`, `prev()` - relative navigation
- `valid()` - iterator validity check
- `remove()` - removes current point, returns iterator to next
- `insert(Point)` - inserts point at iterator position

**Free functions:**
- `remove_duplicates(MutablePolygon&)` - removes duplicate consecutive points
- `smooth_outward(MutablePolygon&, double)` - polygon smoothing
- `smooth_outward(Polygons&, double)` - polygons smoothing (returns modified vector)

### Numeric Precision Notes
- Coordinates use `scaled<coord_t>()` and `scaled<double>()` for test data
- `scaled<T>(value)` applies SCALING_FACTOR (typically 0.00001)
- Smoothing uses `scaled<double>(10.)` - converts 10 to scaled coordinate
- No explicit epsilon tolerance mentioned - uses exact geometry
- Comparison operators use `==` for entire MutablePolygon objects

### Key Implementation Details (from header)
- **Data structure**: Single vector with linked-list semantics using indices
- **IndexType**: int32_t
- **Iterator design**: Circular doubly-linked list with sentinel behavior
- **Important**: `end()` is INCLUSIVE (last valid point), not STL one-past-the-end
- **Range class**: Used in smooth_outward for tracking unprocessed items
- **Invariants**: All elements in one vector, indices reference previous/next
- **Performance**: O(1) insert/remove, O(n) iteration

### Test Pattern Analysis
**Iterator tests:**
- Validates circular navigation (++, --)
- Tests removal at different positions
- Verifies capacity preservation after removal
- Checks iterator validity after all points removed

**Duplicate removal:**
- Input: Polygon with 12 points, many duplicates (3 consecutive at start, 2 in middle, 2 at end)
- Output: 8 unique points in order
- Uses `remove_duplicates()` free function

**Smooth outward:**
- Tests convex polygon (unmodified)
- Tests sharp tiny concave polygon (hole closed - becomes empty)
- Tests vector of polygons (keeps CCW, removes CW)
- No explicit tolerance visible - uses exact geometry comparisons

### Next Steps for Documentation
1. Format tests into contract table
2. Note iterator circular behavior (critical difference from STL)
3. Document end() is inclusive behavior
4. Note scaling requirements for coordinates
5. Describe smooth_outward behavior with concurrency (returns new vector vs. in-place)
6. Highlight that capacity() behavior after removal is tested but not modified