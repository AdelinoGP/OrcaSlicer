# 06 — Test Contracts

## Purpose

This document provides **language-independent test contracts** for the OrcaSlicer test suite. Each test is described in terms of:
- **Required behavior** that must be preserved during refactoring/porting
- **Exact numeric tolerances** that must be reproduced
- **API contracts** without C++ implementation details

The target audience is a refactoring agent porting the core slicing engine to a different language. This document serves as the authoritative Test-Driven Development (TDD) contract.

## Build & Run Reference

### Build Commands
```bash
# Build all tests
cd build && make -j$(nproc)

# Build specific test suite
cd build && make libslic3r_tests

# Build with CMake (out-of-source)
cd /path/to/project
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target libslic3r_tests
```

### Test Execution (Catch2 v2)

**Required flags for all test runs:**
```bash
./tests/libslic3r/libslic3r_tests --order rand --warn NoAssertions
```

**Complete command pattern:**
```bash
# Run all libslic3r tests
cd build && ./tests/libslic3r/libslic3r_tests --order rand --warn NoAssertions

# Run specific test with filtering
cd build && ./tests/libslic3r/libslic3r_tests "Reading an STL file" --order rand --warn NoAssertions

# Run with tag filter
cd build && ./tests/libslic3r/libslic3r_tests "[stl]" --order rand --warn NoAssertions

# Generate XML output for CI
cd build && ./tests/libslic3r/libslic3r_tests --order rand --warn NoAssertions --reporter junit::out=test_results.xml
```

## Catch2 Safety Rules (from tests/CLAUDE.md)

### CRITICAL MUST-FOLLOW RULES

1. **Floating Point**: NEVER use `Catch::Approx` (deprecated, asymmetric)
   - Use: `WithinAbs(value, tolerance)`, `WithinRel(value, percent)`, `WithinULP(value, ulps)`
   - OrcaSlicer's `is_approx()` uses absolute tolerance (EPSILON = 1e-4)

2. **Thread Safety**: Assertions are NOT thread-safe in Catch2 v2
   - Collect results in threads, assert on main thread only
   - Example: use `std::atomic` to count passes, then `REQUIRE(count == expected)`

3. **Section Naming**: In loops, use `DYNAMIC_SECTION` to avoid duplicate names
   - NEVER reuse SECTION names within the same TEST_CASE

4. **Expression Decomposition**: Avoid binary operators in assertions
   - `REQUIRE(a > 0 && b < 10)` → split into two separate assertions
   - Each assertion shows individual values on failure

5. **Test Order**: Always use `--order rand --warn NoAssertions`
   - Required for CI/CD and to detect test interdependencies

### OrcaSlicer-Specific Tolerances

| Tolerance | Value | Use Case |
|-----------|-------|----------|
| EPSILON | 1e-4 | Floating point comparisons (default for is_approx) |
| SCALED_EPSILON | EPSILON / SCALING_FACTOR | Scaled coordinate comparisons |

**Note**: `is_approx()` functions exist for Point, Vec2f, Vec2d, Vec3f, Vec3d and use absolute difference comparison.

---

## Suite: libslic3r/

### test_stl.cpp

**Source under test:** `src/libslic3r/Format/STL.cpp`

**Fixture / test data:** `tests/data/test_stl/`

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Reading an STL file (SCENARIO) | `[stl]` | **Unicode path support**: STL files with non-ASCII characters in path (e.g., "Geräte/") and filename (e.g., "20mmbox-čřšřěá.stl") must load successfully via `Slic3r::load_stl()` and return true.<br><br>**ASCII format support**: ASCII STL files must load regardless of line endings:<br>- LF line endings (Unix): file "ASCII/20mmbox-LF.stl" loads successfully<br>- CRLF line endings (Windows): file "ASCII/20mmbox-CRLF.stl" loads successfully<br><br>**Nonstandard file tolerance**: ASCII STL files with invalid data (text after ending tags, invalid normals like infinities) must still load successfully from file "ASCII/20mmbox-nonstandard.stl".<br><br>**Mesh size validation**: All loaded meshes must have a bounding box size of approximately (20, 20, 20) units. The tolerance is **absolute epsilon of 1e-4** (EPSILON) for each component (X, Y, Z). Implemented via `is_approx(result_size, Vec3d(20, 20, 20))` which checks `|result - expected| < epsilon` for all three dimensions.<br><br>**[DISABLED]** *CR line endings*: ASCII STL files with only CR (old Mac) line endings are NOT supported. This is intentionally disabled via `#if 0` block. Do not port until re-enabled. |

### test_indexed_triangle_set.cpp

**Source under test:** `src/libslic3r/TriangleMesh.cpp`

**Fixture / test data:** `tests/data/frog_legs.obj`, `tests/data/simplification.obj`

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Split empty mesh | `[its_split][its]` | **Empty input handling**: Calling `its_split()` on a default-constructed (empty) `indexed_triangle_set` must return an empty `std::vector<indexed_triangle_set>` with size 0. |
| Split simple mesh consisting of one part | `[its_split][its]` | **Single part preservation**: Splitting a watertight cube (created by `its_make_cube(10., 10., 10.)`) must return exactly one part. The returned part must have identical index count and vertex count to the input cube. |
| Split two non-watertight mesh | `[its_split][its]` | **Non-watertight component separation**: A merged set of two overlapping cubes (with each cube missing one triangle) must split into exactly 2 parts. Both parts must have same index count and vertex count. Each part's index/vertex counts must match the original cube2 indices/vertices size. |
| Split non-manifold mesh | `[its_split][its]` | **Non-manifold topology handling**: Two cubes offset such that their vertices are merged (`its_merge_vertices()`) must split into exactly 2 parts. Both parts must have identical index/vertex counts matching the offset cube. |
| Split two watertight meshes | `[its_split][its]` | **Watertight component separation**: Two non-overlapping spheres must split into exactly 2 parts. Both parts must have the same number of triangles and vertices. Each part must match the original sphere's index/vertex sizes. |
| Reduce one edge by Quadric Edge Collapse | `[its]` | **Single edge collapse**: A tetrahedron with 5 vertices and 6 triangles must, after quadric edge collapse with target of 5 triangles, have exactly 4 triangles and 4 vertices. The 3 original triangles (indices 0-2) remain unchanged. Vertex 2 (new) must be positioned between the old vertex 2 and removed vertex 4 in all x,y,z coordinates (verified by: `min(v2, v4) < v_new < max(v2, v4)` for each dimension). The simplified mesh must remain geometrically similar to original within `max_average_distance=0.014` and `max_distance=0.75` using AABB-tree point distance measurements. |
| Simplify mesh by Quadric edge collapse to 5% | `[its]` | **Aggressive simplification**: The frog_legs.obj model must be simplified to ≤5% of original triangle count. The mesh must not become empty. The volume difference between original and simplified must be less than **33.0** units (absolute tolerance). The simplified mesh must be geometrically similar to original within `max_average_distance=0.043` and `max_distance=0.32`. |
| Simplify trouble case | `[its]` | **Invalid triangle prevention**: The simplification.obj model, when simplified, must NOT contain triangles with duplicate vertices (where any two of three indices are equal). This is a regression test for a specific mesh corruption bug. |
| Simplified cube should not be empty. | `[its]` | **Non-empty result**: A cube mesh must NOT become empty after quadric edge collapse, even with target count of 0 (allow maximal simplification). |

**Special notes:**
- **[DISABLED]** `create_random_generator()` function marked unused by clang compiler
- **[DEBUG]** `debug_write_obj()` helper writes intermediate results only in debug builds (`#ifndef NDEBUG`)
- Custom tolerance configuration `CompareConfig` is used instead of generic EPSILON values
- Two disabled code blocks exist to avoid compiler warnings for unused functions
- External model files required: `frog_legs.obj` and `simplification.obj`

### test_geometry.cpp

**Source under test:** `src/libslic3r/Geometry.cpp` + `src/libslic3r/Geometry/*.cpp` + core geometric types

**Fixture / test data:** none (all inline data)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Line::parallel_to | `[Geometry]` | **Parallel line detection**: Two lines are parallel if they share the same direction (or opposite). With epsilon angle tolerance of **EPSILON (1e-4)** radians, lines rotated by up to 0.9*EPSILON from each other are considered parallel. Lines rotated by 1.1*EPSILON are not parallel. **Critical note**: Lines shorter than 100 units rotated by EPSILON may not be rotated at all due to numerical precision (scalar rounding prevents measurable change). The check is transitive across translations. |
| Line::perpendicular_to | `[Geometry]` | **Perpendicular line detection**: Lines are perpendicular if their direction vectors have dot product of 0. With epsilon angle tolerance of **EPSILON** radians, perpendicularity is preserved within ±0.9*EPSILON. The check has same numerical precision limitations as parallel_to for very short lines. Transitive under translation. |
| Polygon::contains works properly | `[Geometry]` | **Point-in-polygon test**: A specific test polygon (10 vertices from GH #1950 regression) must correctly contain a test point. This is a regression test for Windows-specific bug. No explicit tolerance - uses exact integer geometry comparison. |
| Intersections of line segments | `[Geometry]` | **Line-line intersection**: Two line segments must return correct intersection point. Integer coordinates intersect exactly. Scaled coordinates (divided by 0.00001) must still register intersection success. The method returns boolean success status AND writes to output pointer parameter. |
| polygon_is_convex works | `[Geometry]` | **Convexity detection**: Square with CCW winding must be identified as convex; CW winding square must be identified as NOT convex (due to winding order). Concave polygon (L-shape with indent) must be identified as NOT convex. Uses polygon_is_convex() function. |
| Creating a polyline generates the obvious lines | `[Geometry]` | **Polyline construction**: Converting 3 collinear points to Polyline must create exactly 2 Line segments: (0,0)-(10,0) and (10,0)-(20,0). Uses lines() method. |
| Splitting a Polygon generates a polyline correctly | `[Geometry]` | **Polygon splitting**: Splitting triangle at vertex index 1 must produce polyline with 4 points: starting at index 1, going to index 2, then index 0, then back to index 1 (closing the loop). Tests split_at_index() method. |
| Bounding boxes are scaled appropriately | `[Geometry]` | **BoundingBox scaling**: Scaling a bounding box by factor 2 must multiply both min and max points' coordinates by 2. Bounding box min(0,1), max(10,2) becomes (0,2), (40,4). Tests BoundingBox::scale() method. |
| Offsetting a line generates a polygon correctly | `[Geometry]` | **Polyline offsetting**: Line segment from (10,10)-(20,10) offset by 5 units must equal polygon with 4 vertices forming a 10x10 rectangle. Uses offset() function with absolute offset distance. No epsilon tolerance. |
| Circle Fit, TaubinFit with Newton's method | `[Geometry]` | **Circle center fitting**: Given sample points forming half-circles with known centers, the Taubin-Newton circle fit algorithm must return center within **EPSILON (1e-4)** of expected. Tests with different input ranges (full array, first 4 points, middle 4 points). Works for Vec2d and Point types. `is_approx()` used for validation. |
| smallest_enclosing_circle_welzl | `[Geometry]` | **Minimum enclosing circle**: Must find smallest circle enclosing all test points. Returns radius inflated by **SCALED_EPSILON** to ensure all points are inside. After removing 2.1*SCALED_EPSILON from radius, exactly 3 points must lie on or near boundary. Uses SCALED_EPSILON (EPSILON / SCALING_FACTOR) for containment checks. |
| Path chaining | `[Geometry]` | **Point ordering optimization**: For 8 grid-aligned points, chain_points() must produce path with total length ≤26 units per segment (no diagonals). For gyroid infill endpoints, chained polylines must have connection length < 85,206,000 units. For loop pieces with specified start point, all pieces must connect without gaps (back() == front()). |
| Line distances | `[Geometry]` | **Distance to line**: Points on line segment (endpoints and interior) must have distance 0. Points off line must have exact Manhattan or Euclidean distances as specified (10, 30). Tests Line::distance_to() method with exact integer geometry. |
| Polygon convex/concave detection with angle thresholds | `[Geometry]` | **Convex/concave vertex classification**: Square has 4 convex vertices CCW, 4 concave CW (winding-dependent). Thresholds affect classification: > 90° (4/3 π): all vertices invisible; < 60° (π/3): 4 convex visible; = 90° (π/2): all invisible. Default (no threshold): 4 convex CCW, 4 concave CW. Tests convex_points() and concave_points() with optional angle parameter in radians. |
| Triangle Simplification does not result in less than 3 points | `[Geometry]` | **Minimum point preservation**: Simplifying a triangle polygon with 3 vertices using tolerance 250000 must return polygon with at least 3 points. Uses simplify() method with aggressive tolerance. |
| Ported from xs/t/14_geometry.t | `[Geometry]` | **Convex hull and utilities**: Convex hull of 5 points (including interior) must have 4 points. arrange() must place 4 items with 20x20 size and 5 spacing. directions_parallel() must correctly identify parallel vectors within angular tolerance. |
| Convex polygon intersection tests | `[Rotcalip]` | **Convex polygon intersection detection**: Multiple test cases using Rotating Calipers algorithm. Disjoint squares (translated apart) → must return false. Overlapping squares → must return true. Touching edges → false. Touching vertex → false. Exact overlap → true. All tests use scaled coordinates (divided by SCALING_FACTOR). Each test case uses Geometry::convex_polygons_intersect(A, B) and compares with Clipper intersection result. |
| Convex polygon intersection test prusa polygons | `[Rotcalip]` | **Rotating Calipers vs Clipper validation**: Self-intersection of printer part polygons must work (same polygon intersects itself). All pairs of printer parts must produce identical results between Rotating Calipers and Clipper intersection. First test validates disjoint separations, second test validates overlapping configurations. Uses PRINTER_PART_POLYGONS from printer_parts.hpp. |

**Special notes:**
- **[DISABLED]** Lines 14-15, 998-998: Random generator function and benchmark infrastructure commented out with `//` and `#if 0`.
- **[DISABLED]** Lines 477: SVG debug output for failing test (only in failing case, wrapped in if statement).
- **EPSILON** is the base tolerance (1e-4) for floating-point comparisons in geometry.
- **SCALED_EPSILON** = EPSILON / SCALING_FACTOR is used for scaled coordinate systems.
- All polygon/line operations use integer coord_t internally; floating-point only in circle fitting and distance calculations.
- `is_approx()` functions use absolute difference comparison with EPSILON.

### test_polygon.cpp

**Source under test:** `src/libslic3r/Polygon.cpp`

**Fixture / test data:** none (all inline data)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Converted Perl tests - ccw_square | `[Polygon]` | **Square corner point validation**: A square with points (100,100), (200,100), (200,200), (100,200) in CCW order must be valid via `is_valid()`.<br><br>**Signed area calculation**: CCW square area must equal 10000 (100×100). CW square area must equal -10000 (negative). Area is signed based on winding order.<br><br>**Centroid calculation**: Both CCW and CW squares must return centroid at (150, 150).<br><br>**Point containment**: Both CCW and CW squares must contain point (150, 150).<br><br>**Conversion to lines**: CCW square must convert to exactly 4 Line segments: (100,100)-(200,100), (200,100)-(200,200), (200,200)-(100,200), (100,200)-(100,100).<br><br>**Split operations**: All split methods must return Polyline with 5 points (start + 3 corners + back to start):<br>- `split_at_first_point()`: starts at index 0<br>- `split_at_index(2)`: starts at index 2<br>- `split_at_vertex(ccw_square[2])`: starts at specific vertex<br><br>**Winding order detection**: `is_counter_clockwise()` returns true for CCW, false for CW.<br><br>**Winding order modification**: `make_counter_clockwise()` must convert CW to CCW. Calling twice on CW (making CCW, then again) must still return CCW (idempotent for already-CCW).<br><br>**First point reference**: `first_point()` must return reference to `points.front()`, not a copy. |
| Converted Perl tests - Triangulating hexagon | `[Polygon]` | **Convex triangulation**: A regular hexagon (6 vertices, center at origin, radius 100) triangulated must produce exactly 4 triangles. All triangles must be CCW (no clockwise triangles found). Uses `triangulate_convex()` method. |
| Converted Perl tests - General triangle intersection | `[Polygon]` | **Line-polygon intersection**: Triangle with vertices (50000000,100000000), (300000000,102000000), (50000000,104000000) must intersect line from (175992032,102000000) to (47983964,102000000). Intersection must return true and point must be exactly (50000000,102000000). Tests `Polygon::intersection()` method with output pointer parameter. |
| Centroid of Trapezoid must be inside | `[Polygon][Utils]` | **Centroid containment for irregular shapes**: Trapezoid with vertices (4702134,1124765853), (-4702134,1124765853), (-9404268,1049531706), (9404268,1049531706) must have its centroid contained within the polygon. Uses `contains()` method. All calculations use exact integer geometry. |
| Remove collinear points from Polygon - Leading/trailing collinear points | `[Polygon]` | **Collinear point removal**: Polygon with points that form a "circle" shape with multiple collinear sequences must have leading and trailing collinear points removed. Input: 14 points with collinear sequences at beginning (3 points), middle (2 points), end (3 points).<br><br>**Result validation**:<br>- Leading collinear points removed: resulting polygon's first point must equal (20,0) scaled<br>- Trailing collinear points removed: resulting polygon's last point must equal (-20,0) scaled<br>- Total points preserved: exactly 7 points remaining (non-collinear vertices only)<br><br>Uses free function `remove_collinear()` which operates in-place on polygon. All coordinate calculations use scaled integer geometry (`Point::new_scale()`). |
| Remove collinear points from Polygon - Number of remaining points | `[Polygon]` | **Point count preservation**: After collinear removal, final polygon must have exactly 7 points. This validates that only truly collinear points (those lying exactly on line segments) are removed, while corners are preserved. |

**Special notes:**
- All polygon operations use **integer coord_t** internally (scaled coordinates)
- No tolerance values used - exact arithmetic throughout
- `area()` returns signed value: positive for CCW, negative for CW
- `contains()` uses exact geometry predicates (no epsilon)
- `triangulate_convex()` assumes input is convex - behavior undefined for non-convex
- `remove_collinear()` is a free function, not a method
- All test coordinates use `Point::new_scale()` which applies SCALING_FACTOR (typically 0.00001)
- No floating-point comparisons in test assertions

### test_mutable_polygon.cpp

**Source under test:** `src/libslic3r/MutablePolygon.cpp`

**Fixture / test data:** none (all inline data)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Iterators - Iterating upwards | `[MutablePolygon]` | **Circular iteration**: For a MutablePolygon with 3 points, iterator must navigate circularly via `++` operator:<br>- `++begin` ≠ `begin`<br>- `++begin` ≠ `end`<br>- `++(++begin)` ≠ `begin`<br>- `++(++begin)` == `end`<br>- `++(++(++begin))` == `begin` (wraps around)<br>- `++(++(++begin))` ≠ `end`<br><br>**Critical note**: MutablePolygon iterators are circular, not STL-style. The container is a doubly-linked list where `end()` points to the **last valid element**, not one-past-the-end. |
| Iterators - Iterating downwards | `[MutablePolygon]` | **Reverse circular iteration**: Pre-decrement (`--`) must navigate backward circularly:<br>- `--begin` ≠ `begin` (wraps to `end`)<br>- `--begin` == `end`<br>- `--(--begin)` ≠ `begin`<br>- `--(--begin)` ≠ `end`<br>- `--(--(--begin))` == `begin`<br>- `--(--(--begin))` ≠ `end`<br><br>`end().prev()` returns iterator to second-to-last element. |
| Iterators - Deleting 1st point | `[MutablePolygon]` | **Point removal at head**: Removing the first point via `begin().remove()` must:<br>- Reduce size from 3 to 2<br>- Return iterator equal to the original second point (`it_2nd`)<br>- Make returned iterator equal to new `begin()`<br><br>**Iterator validity**: Returned iterator remains valid after removal and points to the next element. |
| Iterators - Deleting 2nd point | `[MutablePolygon]` | **Point removal at middle**: Removing the second point must:<br>- Reduce size from 3 to 2<br>- Leave `begin()` unchanged<br>- Return iterator that equals the **removed point's own iterator** (from `.remove()` call)<br>- `it_1st == p.begin()` still holds<br><br>**Behavior note**: `it.remove()` returns iterator pointing to the element that followed the removed element, but since we removed the middle element and stored it as `it_2nd`, the comparison `it_2nd.remove() == it_2nd` holds because of circular list mechanics. |
| Iterators - Deleting two points | `[MutablePolygon]` | **Sequential removal**: Chaining `.remove()` calls reduces size to 1:<br>- `p.begin().remove().remove()` yields size 1<br>- Single-element polygon has `begin().next() == begin()`<br>- Single-element polygon has `begin().prev() == begin()` |
| Iterators - Deleting all points | `[MutablePolygon]` | **Complete removal**: Removing all 3 points must:<br>- Result in size 0 and `empty() == true`<br>- Make `begin()` invalid (`!begin().valid()`)<br>- Make the returned final iterator invalid (`!it.valid()`)<br><br>**Iterator invalidation**: After last point removal, any iterators to the polygon become invalid. |
| Iterators - Inserting a point at the beginning | `[MutablePolygon]` | **Insert at head**: `insert(begin(), {3, 4})` must:<br>- Add point to front of circular list<br>- Result in sequence: `[ {3,4}, {0,0}, {0,1}, {1,0} ]`<br>- Use `==` operator for validation (tests entire polygon content) |
| Iterators - Inserting a point at the 2nd position | `[MutablePolygon]` | **Insert at middle**: `insert(++begin(), {3, 4})` must:<br>- Insert between first and second point<br>- Result in sequence: `[ {0,0}, {3,4}, {0,1}, {1,0} ]`<br><br>**Increment behavior**: `++begin()` advances to second element, insertion occurs before that position. |
| Iterators - Inserting a point after a point was removed | `[MutablePolygon]` | **Capacity preservation**: Initial capacity is 3. After:<br>1. Removing 1st point: capacity remains 3, content becomes `[{0,1}, {1,0}]`<br>2. Inserting at head: content becomes `[{0,1}, {1,0}, {5,6}]`, capacity **still 3** (no reallocation)<br><br>**Memory behavior**: Capacity is not reduced by removal; insertion reuses freed slots. This is a linked-list within vector design. |
| Remove degenerate points from MutablePolygon - Duplicate points are removed | `[MutablePolygon]` | **Duplicate removal**: Input polygon with 12 points containing duplicate sequences must be reduced to 8 unique points:<br>- Remove 3 consecutive `{0,100}` → keep 1<br>- Remove 2 consecutive `{180,200}` → keep 1<br>- Preserve order of first occurrence<br><br>Result: `[{0,0}, {0,100}, {0,150}, {0,200}, {200,200}, {180,200}, {180,20}, {180,0}]`<br><br>Uses free function `remove_duplicates(MutablePolygon&)`. |
| smooth_outward - Convex polygon | `[MutablePolygon]` | **Convex preservation**: A CCW triangle must remain unmodified when `smooth_outward()` is applied with `scaled<double>(10.)` clip distance.<br><br>**Input**: `{ {0,0}, scaled(10.), 0 }, { 0, scaled(10.) } }`<br>**Output**: Identical (unmodified) |
| smooth_outward - Sharp tiny concave polygon (hole) | `[MutablePolygon]` | **Hole elimination**: A small CCW triangle with a tiny CW "hole" (concave indentation) must become empty.<br><br>**Input**: `{ {0,0}, {0, scaled(5.) }, { scaled(10.), 0 } }` (3 points, 2nd point creates concavity)<br>**Output**: `empty() == true`<br><br>The algorithm clips the polygon inward by scaled 10 units, collapsing the shape. |
| smooth_outward - Two polygons | `[MutablePolygon]` | **Polygon vector processing**: Input: vector of 2 polygons (1 CCW, 1 CW). After `smooth_outward()`:<br>- CCW contour remains unchanged (size 3)<br>- CW contour is removed (becomes empty, filtered out)<br>- Result: vector with only 1 polygon `{ {0,0}, scaled(10.), 0 }, { 0, scaled(10.) } }`<br><br>**Orientation sensitivity**: CCW contours preserved; CW contours removed. Uses `smooth_outward(Polygons&, coord_t)` free function. |

**Special notes:**
- All iterator operations are circular (STL incompatible behavior)
- `end()` points to last valid element, not one-past-the-end
- Internal structure: singly-linked list within contiguous vector using indices
- `capacity()` persists across removals (memory efficiency design)
- `operator==` compares entire polygon content (all point coordinates)
- Coordinates use `scaled<T>()` with SCALING_FACTOR (typically 0.00001)
- `smooth_outward()` uses scaled clip distance parameter
- `remove_duplicates()` has multiple overloads with different epsilon/angle parameters
- All operations use exact geometry with no epsilon tolerance

### test_clipper_utils.cpp

**Source under test:** `src/libslic3r/ClipperUtils.hpp` + `src/libslic3r/ClipperUtils.cpp`

**Fixture / test data:** none (all inline data)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Various Clipper operations - xs/t/11_clipper.t | `[ClipperUtils]` | **Offset operations**: Given a CCW square with CW hole (square_with_hole expolygon):<br>- `offset(square_with_hole, 5.f)` must return polygons: outer square expanded to (205,205)-(95,95) and inner hole contracted to (155,145)-(145,155)<br>- `offset_ex(square_with_hole, 5.f)` must return expolygon with CCW exterior (205,205)-(95,95) and CW interior hole (145,145)-(155,155)<br>- `offset2_ex({square_with_hole}, 5.f, -2.f)` performs offset-out by 5 then offset-in by 2 (net +3): must return expolygon with exterior (203,203)-(97,97) and interior (143,143)-(157,157)<br><br>**Large coordinate handling**: Square from 0 to 20,000,000 with hole from 5,000,000 to 15,000,000, `offset2_ex(expolygons, -1.f, 1.f)` must return 1 expolygon with same area as input. Tests scaling of large coordinates.<br><br>**Difference operations**: Square minus hole must return expolygon with identical area to the original square_with_hole expolygon. Tests `diff_ex()` with CCW outer minus CW inner.<br><br>**Polyline clipping**: Horizontal polyline from (50,150) to (300,150) clipping against square+hole:<br>- `intersection_pl({polyline}, {square, hole})` must return 2 polylines, each exactly 40 units long<br>- `diff_pl({polyline}, {square, hole})` must return 3 polylines with lengths: exactly 50 (left), exactly 100 (right), and exactly 20 (center) in any order<br><br>**GitHub issue regression tests**:<br>- **Bug #96 / Slic3r #2028**: Subject polyline with large complex zigzag pattern (coordinates 25M-75M) against rectangular clip must yield exactly 1 intersection polyline (not empty)<br>- **Bug #122**: Subject polyline forming square (1975-25) against two rectangles (outer 2025 to -25, inner 525-1475) must yield exactly 1 polyline with 5 points<br>- **Bug #126**: Subject polyline with large coordinates (200K-20M) against complex polygon must yield 1 polyline with **length equal to subject length** using `Catch::Approx` (tolerance not specified in test, but typical floating-point precision applies)<br><br>**[DISABLED]** Lines 129-172: Perl test code for bug #127 with large coordinate polylines and circular clip. Disabled until bug fixed. Do not port.<br>**[DISABLED]** Lines 130-136, 138-143: Tests checking polyline orientation preservation after clipping. Disabled because Clipper does not preserve polyline orientation. |
| Various Clipper operations - t/clipper.t | `[ClipperUtils]` | **Intersection with hole preservation**: Polygons {square, hole_in_square} intersected with square2 must yield 1 expolygon with area equal to match.expolygon area. Hole must be preserved in intersection result. Tests `intersection_ex()` with nested polygons.<br><br>**Union operations**: Union of 2 CCW squares and 1 CW hole must yield exactly 1 expolygon (no holes). The CW hole is merged/removed during union. Tests that union correctly handles winding order differences.<br><br>**Difference with hole creation**: Difference of (square + square2) minus hole must yield 1 expolygon with 1 hole. Area must equal: (outer rectangle 40×40) minus (inner hole). Tests `diff_ex()` creating holes from CCW subject and CW clip.<br><br>**No-op polyline diff**: Diff of a polyline (extracted from square) with empty polygon set must return exactly 1 polyline with same point count as input. Tests `diff_pl()` with empty clip - should return unmodified input polyline. |
| Traversing Clipper PolyTree | `[ClipperUtils]` | **PolyTree traversal and area accumulation**: Reference union of 5 polygons (box frame + 2 holes + 2 inner boxes) creates complex PolyTree structure. All 4 traversal modes must preserve total area:<br><br>1. **Polygons WITHOUT spatial ordering**: `polytree_area<OFF>(tree, &polygons)` must return area equal to sum of all 5 component areas using `Catch::Approx` (tolerance unspecified but typical for accumulated floating-point operations). Output vector must have exactly 5 polygons (1 per component).<br><br>2. **ExPolygons WITHOUT spatial ordering**: `polytree_area<OFF>(tree, &expolygons)` must return same total area. Output must contain count_polys(expolygons) = 5 (1 outer + 4 holes, assuming holes are flattened).<br><br>3. **Polygons WITH spatial ordering**: `polytree_area<ON>(tree, &polygons)` must return same total area and 5 polygons. Spatial ordering may change polygon sequence.<br><br>4. **ExPolygons WITH spatial ordering**: `polytree_area<ON>(tree, &expolygons)` must return same total area and count_polys = 5.<br><br>**Critical template parameter**: `e_ordering::ON` vs `e_ordering::OFF` controls whether traversal respects spatial ordering of tree nodes. All four permutations must produce identical area sums. |
| **Role:** Shared fixture builder. | N/A | N/A |

**Special notes:**
- **Catch::Approx usage**: Line 126 (bug #126) and lines 280, 286, 292, 298 (PolyTree tests) use `Catch::Approx` for floating-point area comparisons. Porting agent **must reproduce tolerance** but exact tolerance value is not explicitly specified in test - standard floating-point precision applies (likely 1e-6 to 1e-9 relative tolerance).
- **Bug regression tests**: Tests 96, 122, 126, (and disabled 127) represent specific GitHub issues where Clipper operations failed with unusual coordinate ranges or polygon configurations. These are critical regression tests.
- **Large coordinates**: Multiple tests use coordinates from 20,000 to 20,000,000 (20 million units) to verify scaling behavior and integer overflow prevention in ClipperLib.
- **Winding order semantics**: Union handles CW vs CCW differently (CW holes are merged). This is fundamental ClipperLib behavior that must be preserved.
- **PolyTree structure**: The `traverse_pt` template with `e_ordering` enum affects how nested polygon trees are flattened into ExPolygons or Polygons. All four combinations are tested to ensure area preservation regardless of traversal order.

### test_clipper_offset.cpp

**Source under test:** `src/libslic3r/ClipperUtils.cpp` (offset path operations)

**Fixture / test data:** none (all inline data, uses programmatic polygon generation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Constant offset (SCENARIO: "Constant offset") | `[ClipperUtils]` | **20mm solid box offset operations**: A 20×20 square (scaled by 1,000,000 units per mm) must produce correct offset results for all tested configurations:<br><br>**Outward offset (+1mm, all miter limits 2.0/1.5/1.2)**: `offset(box20mm, 1000000, jtMiter, miter)` must return 1 polygon with area equal to 22×22 mm² = **484,000,000,000,000** scaled units (4.84e14). This uses `Catch::Approx` for comparison - porting agent must provide floating-point tolerance.<br><br>**Inward offset (-1mm, all miter limits)**: `offset(box20mm, -1000000, jtMiter, miter)` must return 1 polygon with area equal to 18×18 mm² = **324,000,000,000,000** scaled units (3.24e14). Uses `Catch::Approx` semantics.<br><br>**Variable outward offset**: `variable_offset_outer(box20mm, {deltas_plus}, miter)` with per-vertex deltas of +1mm must also produce 22×22 mm² area (4.84e14). Identical area to constant offset.<br><br>**Variable inward offset**: `variable_offset_inner(box20mm, {deltas_minus}, miter)` with per-vertex deltas of -1mm must produce 18×18 mm² area (3.24e14). Identical area to constant offset.<br><br>**Join type independence**: All three miter limit values (2.0, 1.5, 1.2) must produce identical results for the simple 20mm square (no sharp corners to taper). Results must be exactly 1 output polygon per operation.<br><br>**Contract**: Offset operations on simple axis-aligned boxes with standard miter limits must produce mathematically correct expansion/contraction with area = (side ± offset)^2. |
| Constant offset (SCENARIO: "Constant offset") - Box with hole | `[ClipperUtils]` | **20mm box with 10mm hole offset operations**: ExPolygon with 20×20mm outer contour and 10×10mm inner hole at (5,5) corner offset:<br><br>**Outward offset (+1mm, all miter limits)**: `offset_ex(box20mm, 1000000, jtMiter, miter)` must return 1 ExPolygon with area = (22² - 8²) mm² = 420 mm² = **4.20e14** scaled units. Hole expands by 2mm total (1mm each side), so hole dimension becomes 8×8mm (20-10-2). Uses `Catch::Approx`.<br><br>**Inward offset (-1mm, all miter limits)**: `offset_ex(box20mm, -1000000, jtMiter, miter)` must return 1 ExPolygon with area = (18² - 12²) mm² = 180 mm² = **1.80e14** scaled units. Hole contracts by 2mm total, becoming 12×12mm. Uses `Catch::Approx`.<br><br>**Variable outward**: `variable_offset_outer_ex(box20mm, {deltas_plus, deltas_plus}, miter)` must produce identical 420 mm² area.<br><br>**Variable inward**: `variable_offset_inner_ex(box20mm, {deltas_minus, deltas_minus}, miter)` must produce identical 180 mm² area.<br><br>**Contract**: Offset operations on holed shapes must correctly handle both exterior contour AND interior holes. All offsets applied to both outer and inner boundaries. Miter limits tested but don't affect results on this geometry. |
| Constant offset (SCENARIO: "Constant offset") - Triangle | `[ClipperUtils]` | **20mm right-angle triangle offset operation**: Right triangle with vertices at (0,0), (20,0), (0,20):<br><br>**Outward offset (+1mm, constant)**: `offset(triangle20mm, 1000000, jtMiter, 2.0)` must return 1 polygon with area matching calculated value **area_offsetted**. Formula accounts for mitered sharp corner:<br>- Base triangle side after offset: 20 + 1×(1 + 1/tan(22.5°)) ≈ 24.828mm<br>- Area before tapering: 0.5 × (24.828)² ≈ 308.18mm²<br>- Minus 2 × area_tapered (from sharp corner) where `area_tapered = offset² × (1/sin(22.5°) - 1)² × tan(22.5°)`<br>- Result ≈ 294.5mm² (exact value pre-calculated in test)<br>- Must use `Catch::Approx` for floating-point area comparison.<br><br>**Outward variable offset**: `variable_offset_outer(triangle20mm, {deltas}, 2.0)` with per-vertex deltas of +1mm must produce **identical area** to constant offset (same mathematical result).<br><br>**Join type independence**: All three miter limits tested, but only miter limit 2.0 actually used in calculations (see line 179, 198 - hardcoded to 2.0).<br><br>**Contract**: Sharp corners (45° angle bisector π/8) must produce correct miter length extension. Miter limit of 2.0 is sufficient to prevent clipping for this geometry. Output polygon count must be 1. Area match validates correct trigonometric calculations in offset algorithm. |
| Shared fixture builder | N/A | **Role:** Shared fixture builder.<br><br>**Fixtures provided:**<br>| Function | What it creates | Used by |<br>|---|---|---|<br>| `offset()` | Polygons offset by absolute distance, join type configurable | test_clipper_utils.cpp, test_clipper_offset.cpp |<br>| `offset_ex()` | ExPolygons offset by absolute distance (preserves holes) | test_clipper_utils.cpp, test_clipper_offset.cpp |<br>| `offset2_ex()` | Double offset (out then in) on ExPolygons | test_clipper_utils.cpp |<br>| `variable_offset_outer()` | Polygons offset with per-vertex distances | test_clipper_offset.cpp |<br>| `variable_offset_inner()` | Polygons offset with per-vertex distances (inward) | test_clipper_offset.cpp |<br>| `variable_offset_outer_ex()` | ExPolygons offset with per-vertex distances | test_clipper_offset.cpp |<br>| `variable_offset_inner_ex()` | ExPolygons offset with per-vertex distances (inward) | test_clipper_offset.cpp |<br>| `diff_ex()` | ExPolygon difference (subject − clipping) | test_clipper_utils.cpp |<br>| `intersection_ex()` | ExPolygon intersection (subject ∩ clipping) | test_clipper_utils.cpp |<br>| `union_ex()` | ExPolygon union of multiple shapes | test_clipper_utils.cpp |<br>| `diff_pl()` | Polyline difference (subject − clipping) | test_clipper_utils.cpp |<br>| `intersection_pl()` | Polyline intersection (subject ∩ clipping) | test_clipper_utils.cpp |<br>| `polytree_area<ON/OFF>` | Area accumulation from Clipper PolyTree structure | test_clipper_utils.cpp |

**Special notes:**
- **Catch::Approx usage**: This file uses `Catch::Approx` in **15 instances** across all test sections (lines 35, 49, 67, 81, 109, 123, 143, 157, 189, 208). This is the second file in the test suite with extensive Approx usage. Porting agent must **reproduce floating-point tolerance** but exact tolerance values are not specified in the source code. Recommend using standard FP comparison precision (1e-6 to 1e-9 relative tolerance) or OrcaSlicer's EPSILON (1e-4 absolute) for area validation.
- **Mathematical validation**: Triangle test uses pre-calculated expected area formula that accounts for mitered corner behavior. Algorithm must use identical trigonometric constants: angle_bisector = π/8 (22.5°), calculations: `(1/sin(angle) - 1)² × tan(angle)`.
- **Coordinate scaling**: All coordinates scaled by 1,000,000 units/mm. Test validates 20,000,000 unit shapes with 1,000,000 unit offsets. Confirms integer overflow prevention and floating-point precision in large coordinate ranges.
- **Variable vs. constant equivalence**: Tests validate that variable-offset operations (per-vertex deltas) produce identical results to constant offsets on regular shapes. All vertices have same delta value.
- **Join type tested, but limited impact**: Miter limits (2.0, 1.5, 1.2) all tested, but geometry lacks extreme angles that would cause miter clipping. Only miter limit 2.0 actually used in calculations (hardcoded in triangle test, loop iterates but uses same value).
- **BDD structure**: Uses SCENARIO/GIVEN/WHEN/THEN/DYNAMIC_SECTION pattern extensively. All iterations use DYNAMIC_SECTION to avoid duplicate section names per Catch2 rules.
- **No disabled tests**: All tests in this file are enabled and compiled.
- **No external fixtures**: All data generated programmatically, no test data files required.
- **SVG export capability**: Tests support `TESTS_EXPORT_SVGS` macro for visual debugging, but this is not part of the contract.

**Hazard identification:**
- **H427**: Area calculations use Catch::Approx without explicit tolerance - reproducibility requires either (a) standard FP tolerance or (b) reverse-engineering from test behavior
- **H428**: Triangle test hardcodes miter limit 2.0 in calculations (line 179, 198) but loops over {2.0, 1.5, 1.2} - may be bug or intentional parameter study
- **H429**: Variable offset tests use same delta for all vertices - doesn't test truly variable behavior (would need asymmetric deltas)
- **H430**: Large coordinates (20M units) validated, but extreme coordinates (75M+ from clipper_utils) not present in offset tests

**Recommendation for porting agent:**
1. Implement area() function that computes polygon area using shoelace formula
2. Implement offset() function with configurable join type (jtMiter, jtRound, jtSquare) and miter limit
3. Implement variable_offset() variants with per-vertex delta vectors
4. **Critical**: All area comparisons must use floating-point tolerance. Use `abs(result - expected) / expected < 1e-9` or similar for relative tolerance
5. Validate large coordinate handling (20M units) to prevent overflow
6. For triangle: implement exact trigonometric calculation for expected area formula

### test_voronoi.cpp

**Source under test:** `src/libslic3r/Geometry/Voronoi.cpp` + `src/libslic3r/Geometry/VoronoiOffset.hpp`

**Fixture / test data:** none (all inline data, uses programmatic polygon generation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Voronoi missing edges - points 12067 | `[Voronoi]` | **Boost Voronoi library issue #12067**: Given 6 specific points forming a hexagon-like shape, constructing a Voronoi diagram must not produce missing edges. The Boost Voronoi builder must successfully generate edges for all input points without crashes or incomplete diagrams. |
| Voronoi missing edges - Alessandro gapfill 12707 | `[Voronoi]` | **Boost Voronoi library issue #12707**: Given multiple sets of line segments (lines0, lines1, lines2, lines3, lines4) forming a complex polygon, constructing a Voronoi diagram must not produce missing edges. The Boost Voronoi builder must successfully generate edges for all input segments. Also verifies that `intersecting_edges({poly}).empty()` returns true (no self-intersections). |
| Voronoi weirdness | `[Voronoi]` | **Complex polygon handling**: Given multiple complex polygons (poly2, poly5, poly7, poly) with large coordinates and non-standard shapes, constructing a Voronoi diagram must not produce missing edges. The Boost Voronoi builder must handle large coordinate ranges (up to 35M units) and complex polygon geometries. Verifies `intersecting_edges({poly}).empty()` returns true. |
| Voronoi division by zero 12903 | `[Voronoi]` | **Division by zero recovery**: Given 12 points with potential division-by-zero issues in floating-point calculations, constructing a Voronoi diagram must handle the issue gracefully. The Boost Voronoi library must recover from division-by-zero by using extended precision (interval of validity). No missing edges should be produced. |
| Voronoi NaN coordinates 12139 | `[Voronoi][.][!mayfail]` | **NaN coordinate handling**: Given lines with NaN coordinates (invalid input), constructing a Voronoi diagram must handle NaN coordinates gracefully. This test is suppressed (marked with `[.]` and `[!mayfail]`) because the input contains self-intersections that are expected to fail. **[DISABLED]** Test is suppressed and not built. |
| Voronoi offset | `[VoronoiOffset]` | **Voronoi offset operations**: Given a polygon with a hole, constructing a Voronoi diagram and applying offset operations at various distances must produce correct output polygon counts. For offset distances: scale_(0.2), scale_(0.4), scale_(0.5), scale_(0.505), scale_(0.51), scale_(0.52), scale_(0.53), scale_(0.54), scale_(0.55), the number of outer polygons must match expected values (1 outer polygon for most distances, 1 outer for distance 0.55). The number of inner polygons must match expected values (1-2 inner polygons depending on distance). |
| Voronoi offset 2 | `[VoronoiOffset]` | **Voronoi offset with multiple polygons**: Given 2 polygons (one with 8 vertices, one with 8 vertices), constructing a Voronoi diagram and applying offset operations must produce correct output polygon counts. For offset distances: scale_(0.2), scale_(0.4), scale_(0.45), scale_(0.48), scale_(0.5), scale_(0.505), scale_(0.7), scale_(0.8), the number of outer polygons must be 2 for most distances (1 for distance 0.8). The number of inner polygons must match expected values (2-4 inner polygons depending on distance). |
| Voronoi offset 3 | `[VoronoiOffset]` | **Voronoi offset with complex polygons**: Given 2 complex polygons (one with 12 vertices, one with 12 vertices), constructing a Voronoi diagram and applying offset operations must produce correct output polygon counts. For offset distances: scale_(0.2) through scale_(1.01), the number of outer polygons must be 2 for most distances (1 for distances 0.99, 1.0, 1.01). The number of inner polygons must match expected values (2-6 inner polygons depending on distance). |
| Voronoi offset with edge collapse | `[VoronoiOffset4]` | **Voronoi offset with edge collapse**: Given a complex polygon with multiple holes (outer contour + 2 holes), constructing a Voronoi diagram and applying offset operations must produce correct output polygon counts. For offset distances: scale_(0.2) through scale_(1.01), the number of outer polygons must be 2 for most distances (1 for distance 0.99). The number of inner polygons must match expected values (2-3 inner polygons depending on distance). |
| Voronoi offset 5 | `[VoronoiOffset5]` | **Voronoi offset with large coordinates**: Given a polygon with large coordinates (extracted from medallion_printable_fixed-teeth.stl), constructing a Voronoi diagram and applying offset operations at distances scale_(2.8), scale_(2.9), scale_(3.0) must produce correct output polygon counts (1 outer, 1 inner for all distances). This test specifically addresses an assert failure in `first_circle_segment_intersection_parameter` for offset distances >= 2.9. |
| Voronoi skeleton | `[VoronoiSkeleton]` | **Skeleton edge extraction**: Given 2 polygons (one with 8 vertices, one with 8 vertices), constructing a Voronoi diagram and annotating inside/outside must produce skeleton edges. The `skeleton_edges_rough()` function must return a non-empty vector of skeleton edges when given a threshold angle of π/12 (30 degrees). |
| Voronoi missing vertex 1 | `[VoronoiMissingVertex1]` | **Missing vertex detection (single polygon)**: Given a polygon with a point on an edge (dividing the edge into two parts), constructing a Voronoi diagram must handle the missing vertex case. The polygon area must be positive and have no intersecting edges. The Voronoi diagram construction must succeed (no assert failures). |
| Voronoi missing vertex 2 | `[VoronoiMissingVertex2]` | **Missing vertex detection (contour + hole)**: Given a polygon with a contour and a hole, where one edge is divided by a point, constructing a Voronoi diagram must handle the missing vertex case. The combined polygon area must be positive and have no intersecting edges. The Voronoi diagram construction must succeed (no assert failures). |
| Voronoi missing vertex 3 | `[VoronoiMissingVertex3]` | **Missing vertex detection (two polygons)**: Given 2 polygons where one edge is divided by a point, constructing a Voronoi diagram must handle the missing vertex case. The combined polygon area must be positive and have no intersecting edges. The Voronoi diagram construction must succeed (no assert failures). |
| Duplicate Voronoi vertices | `[Voronoi]` | **Duplicate vertex detection**: Given a polygon with potential duplicate Voronoi vertices, constructing a Voronoi diagram must handle duplicate vertices gracefully. The polygon area must be positive and have no intersecting edges. The Voronoi diagram construction must succeed (no assert failures). The test includes a lambda function `has_duplicate_vertices` to detect duplicates (commented out in assertions). |
| Intersecting Voronoi edges | `[Voronoi]` | **Edge intersection detection**: Given a polygon with potential intersecting Voronoi edges, constructing a Voronoi diagram must handle edge intersections gracefully. The polygon area must be positive and have no intersecting edges. The Voronoi diagram construction must succeed (no assert failures). The test includes a lambda function `has_intersecting_edges` to detect intersections (commented out in assertions). |

**Special notes:**
- **[DISABLED]** `Voronoi NaN coordinates 12139` test is suppressed with `[.][!mayfail]` tags and is not built.
- **[DEBUG]** Several tests use `VORONOI_DEBUG_OUT` macro for SVG visualization (not part of contract).
- **Boost Voronoi library**: Tests use Boost Polygon Voronoi library for diagram construction.
- **Rotation-based repair**: Voronoi diagram construction includes rotation-based repair mechanism for degenerate cases (angles: π/6, π/5, π/7, π/11).
- **Large coordinates**: Multiple tests use coordinates up to 35M units to verify handling of large coordinate ranges.
- **Missing vertex repair**: Tests verify that missing Voronoi vertices can be detected and repaired via rotation.

**Hazard identification:**
- **H431**: Boost Voronoi library may produce missing edges for certain input configurations (tests verify workarounds).
- **H432**: Division by zero may occur in floating-point calculations (recovered via extended precision in Boost library).
- **H433**: NaN coordinates in input may produce invalid Voronoi diagrams (test suppressed, input contains self-intersections).
- **H434**: Missing Voronoi vertices may require rotation-based repair (multiple angles tested in repair mechanism).

**Source files:**
- `src/libslic3r/Geometry/Voronoi.cpp` - Voronoi diagram implementation with repair mechanisms
- `src/libslic3r/Geometry/Voronoi.hpp` - Voronoi diagram interface
- `src/libslic3r/Geometry/VoronoiOffset.hpp` - Offset operations
- `src/libslic3r/Geometry/VoronoiUtils.hpp` - Voronoi utility functions
- `src/libslic3r/Geometry/VoronoiVisualUtils.hpp` - Visualization utilities


### test_elephant_foot_compensation.cpp

**Source under test:** `src/libslic3r/ElephantFootCompensation.cpp`

**Fixture / test data:** Defined in test file (spirograph\_gear\_1mm, box\_with\_hole\_close\_to\_wall, thin\_ring, vase\_with\_fins, contour\_with\_hole)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Elephant foot compensation (Contour with hole) | `[ElephantFoot]` | Compensating a contour with a hole results in a polygon with strictly smaller area and valid orientation (CCW contour, CW holes). |
| Elephant foot compensation (Tiny contour) | `[ElephantFoot]` | Compensating a tiny contour (dimensions near zero or below threshold) results in no change (identity). |
| Elephant foot compensation (Large box) | `[ElephantFoot]` | Compensating a large solid box results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Thin ring) | `[ElephantFoot]` | Compensating a thin ring (GH issue #2085) results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Rectangle with narrow part, Partial) | `[ElephantFoot]` | Partially compensating a rectangle with a narrow protrusion results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Rectangle with narrow part, Full) | `[ElephantFoot]` | Fully compensating a rectangle with a narrow protrusion results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Box with hole close to wall) | `[ElephantFoot]` | Compensating a box with a hole close to the wall (GH issue #2998) results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Spirograph wheel, Partial) | `[ElephantFoot]` | Partially compensating a spirograph wheel results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Spirograph wheel, Full) | `[ElephantFoot]` | Fully compensating a spirograph wheel results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Spirograph wheel, Brutal) | `[ElephantFoot]` | Brutally compensating a spirograph wheel (large offset) results in a polygon with strictly smaller area and valid orientation. |
| Elephant foot compensation (Vase with fins) | `[ElephantFoot]` | Compensating a vase with fins results in a polygon with strictly smaller area and valid orientation. |
| [DISABLED] Varying inner offset | `[ElephantFoot]` | `[DISABLED — not built; do not port until re-enabled]` Tests varying inner offset operations using `mittered_offset_path_scaled_points` with `SCALED_EPSILON` simplification. |

### test_config.cpp

**Source under test:** `src/libslic3r/Config.cpp`

**Fixture / test data:** `tests/data/test_config/new_from_ini.ini` (for ini load test)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Generic config validation performs as expected | `[Config]` | **Valid percentage value**: Setting `outer_wall_line_width` to "250%" must result in a valid config (validate() returns empty). <br> **Invalid negative value**: Setting `outer_wall_line_width` to -10 must result in an invalid config (validate() returns non-empty). <br> **Invalid negative integer**: Setting `wall_loops` to -10 must result in an invalid config (validate() returns non-empty). |
| Config accessor functions perform as expected | `[Config]` | **Boolean assignment**: Setting a boolean option to a boolean value must store the value correctly. <br> **Boolean from string "1"**: Setting a boolean option to "1" (string) must store true. <br> **Boolean from invalid string**: Setting a boolean option to "Z" (string) must throw `BadOptionTypeException` and leave the value unchanged (default false). <br> **Boolean from int**: Setting a boolean option to an integer must throw `BadOptionTypeException`. <br> **Integer from string**: Setting an integer option from a serialized string must store the integer value correctly. <br> **Integer from int**: Setting an integer option via integer interface must store the value correctly. <br> **Float from int**: Setting a float option via integer interface must store the value as a float (e.g., 10 -> 10.0). <br> **Float from double**: Setting a float option via double interface must store the value correctly. <br> **Integer from float (invalid)**: Setting an integer option via double interface must throw `BadOptionTypeException`. <br> **Invalid string for float**: Setting a float option to a non-numeric string must throw `BadOptionValueException` and leave the value unchanged. <br> **String from string**: Setting a string option via string interface must store the string correctly. <br> **String from int**: Setting a string option via integer interface must convert and store the string representation (e.g., 100 -> "100"). <br> **String from double**: Setting a string option via double interface must convert and store the string representation (using decimal point). <br> **FloatOrPercent from percent string**: Setting a `FloatOrPercent` option to "100%" must set value=100 and percent=true. <br> **FloatOrPercent from float string**: Setting a `FloatOrPercent` option to "100" (string) must set value=100 and percent=false. <br> **FloatOrPercent from int**: Setting a `FloatOrPercent` option via integer interface must set value=100 and percent=false. <br> **FloatOrPercent from double**: Setting a `FloatOrPercent` option via double interface must set value=100.5 and percent=false. <br> **Integer vector from string**: Setting an integer vector option from a serialized string "10,20" must store the values correctly (index 0=10, index 1=20). <br> **Integer vector from set_key_value**: Setting an integer vector option via `set_key_value` must store the values correctly. <br> **Invalid option during set**: Requesting an invalid option name during set must throw `UnknownOptionException`. <br> **Invalid option during get**: Requesting an invalid option name during get/`opt` must throw `UnknownOptionException`. <br> **Default values for unset options**: `opt_float`, `opt_int`, `opt_bool` must return default constants (`INITIAL_LAYER_HEIGHT`, etc.) for unset options. <br> **Get float for set option**: `opt_float` must return the set value for an option that has been set. |
| Config ini load/save interface | `[Config]` | **Load from ini**: Loading a config from `new_from_ini.ini` must populate the config object with the options defined in the file (e.g., `filament_colour` must contain "#ABCD"). |
| DynamicPrintConfig serialization | `[Config]` | **Binary serialization round-trip**: A `DynamicPrintConfig` object serialized to binary and deserialized must be equal to the original (using `==` operator). |
| [DISABLED] DynamicPrintConfig JSON serialization | `[Config]` | `[DISABLED — not built; do not port until re-enabled]` Tests JSON serialization of `DynamicPrintConfig` (commented out in source). |

