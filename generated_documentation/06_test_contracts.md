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

### test_appconfig.cpp

**Source under test:** `src/libslic3r/AppConfig.cpp`

**Fixture / test data:** none (all inline data)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| AppConfig network version helpers | `[AppConfig]` | **Skipped versions management**: <br> 1. **Empty state**: Initially, `get_skipped_network_versions()` must return an empty list. <br> 2. **Add and check**: After adding version "02.01.01.52", `is_network_version_skipped("02.01.01.52")` must return true, and `is_network_version_skipped("02.03.00.62")` must return false. <br> 3. **Multiple versions**: Adding multiple versions ("02.01.01.52", "02.00.02.50") must result in `get_skipped_network_versions().size() == 2` and both must be recognized as skipped. <br> 4. **Clear**: Calling `clear_skipped_network_versions()` must remove all versions, so `is_network_version_skipped("02.01.01.52")` returns false. <br> 5. **Idempotency**: Adding the same version twice must result in a single entry (size 1). |

### test_placeholder_parser.cpp

**Source under test:** `src/libslic3r/PlaceholderParser.cpp`

**Fixture / test data:** none (all inline data)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Placeholder parser scripting | `[PlaceholderParser]` | **Scripting features**: <br> 1. **Nested config options**: Supports legacy `[nozzle_temperature[foo]]` and modern `{nozzle_temperature[foo]}` syntax. <br> 2. **Math expressions**: Supports basic arithmetic (`2*3`, `2*3/6`), floating-point arithmetic (`2.*3/12`), modulo (`10%2.5`), and functions (`min`, `max`, `int`, `round`, `digits`, `zdigits`, `interpolate_table`). <br> 3. **Floating-point tolerance**: **[CRITICAL]** Uses `Catch::Approx` for floating-point comparisons (lines 38-40, 44, 47-48, 65-67, 71-72, 76-78). Porting agent must reproduce tolerance. Exact tolerance values not specified in source - standard floating-point precision applies. <br> 4. **Line width substitutions**: Tests `coFloatOrPercent` substitutions for `line_width`, `min_width_top_surface`, `small_perimeter_speed`, `infill_anchor`. <br> 5. **Exception handling**: `scarf_joint_speed` set to percent must throw exception when referenced (no context for percent resolution). <br> 6. **Boolean expression parser**: Supports equality, inequality, regex matching (`=~`), logical operators (`and`, `or`, `not`, `&&`, `||`), comparison operators (`<`, `>`, `<=`, `>=`), and ternary operators (`? :`). |
| Placeholder parser variables | `[PlaceholderParser]` | **Variable management**: <br> 1. **Local/global variables**: Supports creation of int, string, and bool variables via `local` and `global` keywords. <br> 2. **Variable overwriting**: Variables can be reassigned after creation. <br> 3. **Variable redefinition**: `local` keyword can redefine existing variables. <br> 4. **Array initialization**: Supports `repeat()` function and initializer lists for creating arrays. <br> 5. **Array access**: Supports index-based access to array elements (e.g., `myint[5]`). <br> 6. **Vector operations**: `size()` and `empty()` functions work correctly on vectors. <br> 7. **Conditional logic**: `if`/`else`/`endif` blocks support variable creation within scopes. |

### test_3mf.cpp

**Source under test:** `src/libslic3r/Format/3mf.cpp` + `src/libslic3r/Format/bbs_3mf.cpp`

**Fixture / test data:** `tests/data/test_3mf/`

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Reading 3mf file | `[3mf]` | **Unicode path support**: 3MF files with non-ASCII characters in path (e.g., "Geräte/") and filename (e.g., "Büchse.3mf") must load successfully via `load_3mf()` and return true. |
| Export+Import geometry to/from 3mf file cycle | `[3mf]` | **Geometry transformation preservation**: When a model with specific transformations (offset, rotation, scaling, mirroring) on both volumes and instances is saved to 3MF and reloaded, the vertex coordinates must match the original within **Eigen's default tolerance** (typically 1e-9). <br><br>**Contract**: The `isApprox()` method on `Eigen::Vector3d` is used for comparison. Porting agent must ensure vertex coordinate comparison uses equivalent tolerance (absolute or relative difference < 1e-9). |
| 2D convex hull of sinking object | `[3mf][.]` | **[DISABLED — not built; do not port until re-enabled]** Testing 2D convex hull calculation on a transformed object. The test is disabled (marked with `[.]`). The expected result points are provided but the test logic contains `CHECK` instead of `REQUIRE` for coordinate comparison, and the logic seems inverted (`> 1` check). This test likely fails or is under development. |

**Special notes:**
- **[DISABLED]** `2D convex hull of sinking object` test is suppressed with `[.]` tag and is not built.
- **Eigen usage**: Uses Eigen library for matrix transformations and quaternion string conversion.
- **Fixture data**: Requires `tests/data/test_3mf/Prusa.stl` and `tests/data/test_3mf/Geräte/Büchse.3mf`. |

### test_meshboolean.cpp

**Source under test:** `src/libslic3r/MeshBoolean.cpp`

**Fixture / test data:** `tests/data/` (sphere generation via `make_sphere`)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| CGAL and TriangleMesh conversions | `[MeshBoolean]` | **CGAL conversion round-trip**: Converting a TriangleMesh to CGAL format and back must preserve vertex and index counts exactly. <br><br>**Volume preservation**: The volume of the converted mesh must match the original volume within **floating-point tolerance** (uses `Catch::Approx`). Porting agent must reproduce tolerance. Exact tolerance value not specified in source - standard floating-point precision applies. <br><br>**Self-intersection check**: The CGAL mesh and converted TriangleMesh must not self-intersect. |

**Special notes:**
- **Catch::Approx usage**: Line 22 uses `Catch::Approx` for volume comparison. Porting agent must provide floating-point tolerance.
- **CGAL dependency**: Uses CGAL library for mesh boolean operations.
- **Sphere generation**: Uses `make_sphere(1.)` to create test geometry. |

### test_marchingsquares.cpp

**Source under test:** `src/libslic3r/MarchingSquares.cpp` + `src/libslic3r/SLA/RasterToPolygons.cpp`

**Fixture / test data:** none (all inline data, programmatic raster generation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Empty raster should result in empty polygons | `[MarchingSquares]` | **Empty input handling**: An empty raster must produce an empty `ExPolygons` vector (size 0). |
| Marching squares directions | `[MarchingSquares]` | **Direction step logic**: Marching squares algorithm direction steps must produce correct coordinate changes: <br> - `left`: (0, -1) <br> - `down`: (1, -1) <br> - `right`: (1, 0) <br> - `up`: (0, 0) <br> - Steps with magnitude 7 and negative magnitude -3 must also work correctly. |
| Fully covered raster should result in a rectangle | `[MarchingSquares]` | **Full coverage extraction**: A fully covered 4x4 raster must extract a single rectangle polygon. Tests with both full accuracy (1x1 pixel window) and half accuracy (2x2 pixel window). |
| 4x4 raster with one ring | `[MarchingSquares]` | **Ring extraction**: A 4x4 raster with one ring shape must extract the correct polygon structure. |
| 10x10 raster with two rings | `[MarchingSquares]` | **Ambiguous case handling**: Two overlapping rings in a 10x10 raster produce ambiguous marching squares cases. The algorithm must handle these gracefully (test uses `strict=false` to skip exact polygon count checks). |
| Square with hole in the middle | `[MarchingSquares]` | **Hole preservation**: A square with a hole must be correctly extracted with hole preserved. Tests multiple raster configurations: <br> - Proportional, landscape, portrait rasters <br> - Different pixel sizes (1x1 mm, 2x2 mm, 0.5x0.5 mm) <br> - Full and half accuracy windows <br><br>**Area tolerance**: Uses `WithinRel(reference_area, pixel_len * 0.05) \|\| WithinAbs(reference_area, pixel_area)` for raster area validation. Porting agent must reproduce these tolerances. |
| Circle with hole in the middle | `[MarchingSquares]` | **Circular hole extraction**: A circle with a hole must be correctly extracted. |
| Recreate object from rasters | `[SL1Import]` | **Round-trip reconstruction**: Loading a mesh, slicing it into layers, rasterizing each layer, and extracting polygons back must preserve geometry within tolerance. <br> **Tolerance**: `diff <= 0.1 * layer_area \|\| diff < scaled<double>(1.) * scaled<double>(1.)`. Porting agent must reproduce this tolerance. |
| Benchmark gyroid cube period 10.0mm | `[MarchingSquares]` | **Performance baseline**: Gyroid infill generation must complete within reasonable time (benchmark test). No specific tolerance, just performance validation. |
| Benchmark gyroid cube period 5.0mm | `[MarchingSquares]` | **Performance baseline**: Gyroid infill generation with smaller period must complete within reasonable time. |

**Special notes:**
- **Allowed matchers**: Uses `WithinRel` and `WithinAbs` (Catch2 matchers) for floating-point comparisons - **allowed** per CLAUDE.md.
- **No Catch::Approx**: This file does NOT use `Catch::Approx`.
- **SVG/PNG export**: Tests generate debug SVG and PNG files in debug builds (`#ifndef NDEBUG`).
- **Performance tests**: Benchmark tests use `BENCHMARK` macro (Catch2 v2.9.0+).
- **Gyroid generation**: Uses marching squares algorithm for gyroid infill extraction with configurable period, frequency, and window size. |

### test_optimizers.cpp

**Source under test:** `src/libslic3r/Optimize/`

**Fixture / test data:** none (inline functions)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Test brute force optimzer for basic 1D and 2D functions | `[Opt]` | **1D optimization (sin)**: The bruteforce optimizer must find the minimum (-1.0) and maximum (1.0) of `sin(phi)` within `[0, 2*PI]`. <br> **Tolerance**: Absolute error < 1e-2 OR relative error < 1e-4. <br><br> **2D optimization (sphere)**: The bruteforce optimizer must minimize `x^2 + y^2 + 1.0` to a score of 1.0 within bounds `[-1, 1]`. <br> **Tolerance**: Absolute error < 1e-2 OR relative error < 1e-4. |

**Special notes:**
- **Custom tolerance function**: Uses `check_opt_result()` which checks `abs_diff < abs_err || rel_diff < rel_err`. Porting agent must reproduce these specific tolerance values (1e-2 absolute, 1e-4 relative).
- **No Catch::Approx**: Uses custom tolerance logic instead of Catch2 matchers.
- **Optimizer interface**: Tests `Slic3r::opt::Optimizer` with `AlgBruteForce` algorithm.
- **Functional testing**: Tests optimization of simple mathematical functions (sin, sphere) to verify optimizer correctness. |

### test_mutable_priority_queue.cpp

**Source under test:** `src/libslic3r/MutablePriorityQueue.hpp`

**Fixture / test data:** none (inline functions and random number generation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Skip addressing | `[MutableSkipHeapPriorityQueue]` | **Block root detection**: `SkipHeapAddressing<8>::is_block_root()` correctly identifies block root indices (1, 9, 17, 73, ...). <br> **Block leaf detection**: `SkipHeapAddressing<8>::is_block_leaf()` correctly identifies block leaf indices (4, 5, 6, 7, 28, 29, 30, 255, ...). <br> **Child/parent navigation**: `child_of()` and `parent_of()` correctly compute hierarchical relationships for indices. |
| Mutable priority queue - basic tests | `[MutableSkipHeapPriorityQueue]` | **Empty queue**: Default constructed queue is empty with size 0. <br> **Insertion**: Queue becomes non-empty after inserting one element. <br> **Top element**: Queue with one element has that element on top. <br> **Pop behavior**: Popping the only element makes queue empty. <br> **Sorted insertion**: Elements inserted in sorted order maintain sorted order when popped. <br> **Random insertion**: 36,000 randomly inserted elements are popped in sorted order. |
| Mutable priority queue - reshedule first | `[MutableSkipHeapPriorityQueue]` | **Reschedule top with highest priority**: Updating the top element to a value that remains highest priority leaves order unchanged. <br> **Reschedule to mid-range**: Updating the top element to a mid-range value moves it to the correct position. <br> **Reschedule to last**: Updating the top element to the last priority moves it to the correct position. <br> **Reschedule 2-element queue**: Updating top of 2 elements to last priority changes top to the second element. <br> **Reschedule 3-element queue**: Updating top to mid-range values correctly reorders the queue. <br> **Random reschedule consistency**: Rescheduling top elements produces same result as pop/push operations. |
| Mutable priority queue - first pop | `[MutableSkipHeapPriorityQueue]` | **Large queue behavior**: Queue with 50,000 elements maintains valid internal indices after first pop. |
| Mutable priority queue complex | `[MutableSkipHeapPriorityQueue]` | **Complex operations**: Queue supports push, pop, remove, and update operations on 5,000 elements with random values. <br> **Index validity**: Internal indices remain valid (less than 3x count) throughout complex operations. <br> **Element consistency**: Retrieved elements match expected IDs throughout operations. |

**Special notes:**
- **No floating-point comparisons**: All tests use integer or exact floating-point comparisons (no `Catch::Approx`).
- **Random number generation**: Tests use `std::mt19937` and `std::uniform_int_distribution` for reproducible randomness.
- **Large dataset testing**: Tests include 36,000 and 50,000 element scenarios for performance validation.
- **Reference implementation**: Based on https://raw.githubusercontent.com/rollbear/prio_queue/master/self_test.cpp (Boost Software License). |

### test_timeutils.cpp

**Source under test:** `src/libslic3r/Time.cpp`

**Fixture / test data:** none (inline time values)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| ISO8601Z | `[Timeutils]` | **ISO 8601 UTC format**: `time2str()` and `str2time()` round-trip conversion preserves exact `time_t` value for UTC timezone. <br> **Invalid string handling**: `str2time()` returns `time_t(-1)` for invalid input strings. <br> **Specific date parsing**: `parse_iso_utc_timestamp("20190710T085000Z")` must produce the same timestamp as `iso_utc_timestamp()`. |
| Slic3r_UTC_Time_Format | `[Timeutils]` | **G-code format**: `time2str()` and `str2time()` round-trip conversion preserves exact `time_t` value for gcode format. <br> **UTC timestamp format**: `utc_timestamp()` must produce the exact string "2019-07-10 at 08:50:00 UTC" for the given timestamp. <br> **Invalid string handling**: `str2time()` returns `time_t(-1)` for invalid input strings. |

**Special notes:**
- **Exact comparisons**: All tests use `REQUIRE` for exact `time_t` and string comparisons (no floating-point tolerances).
- **Time zones**: Tests cover both local and UTC time zones.
- **Multiple formats**: Tests ISO 8601 UTC and Slic3r gcode time formats.
- **Error handling**: Invalid strings must return `time_t(-1)`. |

### test_aabbindirect.cpp

**Source under test:** `src/libslic3r/AABBTreeIndirect.hpp`

**Fixture / test data:** none (inline cube generation via `make_cube`)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Building a tree over a box, ray caster and closest query | `[AABBIndirect]` | **Tree building**: AABB tree built over a 1x1x1 cube must be non-empty. <br> **Single ray intersection**: Ray from (0.5, 0.5, -5) in direction (0, 0, 1) must intersect the cube at distance 5.0. <br> **Multiple ray intersections**: Ray from (0.3, 0.5, -5) in direction (0, 0, 1) must intersect the cube at two points with distances 5.0 and 6.0. <br> **Squared distance to set**: Point (0.3, 0.5, -5) must have squared distance 25.0 to the cube, with closest point (0.3, 0.5, 0.0). <br> **Squared distance from outside**: Point (0.3, 0.5, 5) must have squared distance 16.0 to the cube, with closest point (0.3, 0.5, 1.0). |

**Special notes:**
- **Catch::Approx usage**: All floating-point comparisons use `Catch::Approx` (lines 25, 36, 37, 46, 47, 48, 49, 56, 57, 58, 59). Porting agent must reproduce tolerance (default Catch2 Approx tolerance).
- **Cube geometry**: Uses `make_cube(1., 1., 1.)` to generate test mesh.
- **Ray casting**: Tests `intersect_ray_first_hit()` and `intersect_ray_all_hits()` functions.
- **Closest point queries**: Tests `squared_distance_to_indexed_triangle_set()` for distance and closest point computation.
- **AABB tree structure**: Uses `AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set()` for tree construction. |

### test_hollowing.cpp

**Source under test:** `src/libslic3r/SLA/Hollowing.cpp` + `src/libslic3r/SLA/OpenVDBUtils.cpp`

**Fixture / test data:** none (inline sphere generation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Hollow two overlapping spheres | (none) | **Mesh hollowing**: Two overlapping spheres (radius 10) can be hollowed using `sla::hollow_mesh()`. <br> **Flag application**: `hfRemoveInsideTriangles` flag removes interior triangles from the hollowed mesh. <br> **Output generation**: Hollowed mesh is written to `twospheres.obj` file. |

**Special notes:**
- **OpenVDB dependency**: This test is only built when `TARGET OpenVDB::openvdb` is present in CMake.
- **Conditional compilation**: Porting environment must provide an equivalent volumetric SDF library (OpenVDB or alternative).
- **No floating-point assertions**: The test only performs mesh operations and file output without numeric assertions.
- **Source files**: `src/libslic3r/SLA/Hollowing.cpp` (hollowing algorithm) and `src/libslic3r/SLA/OpenVDBUtils.cpp` (OpenVDB integration). |

### test_bambu_networking.cpp

**Source under test:** `src/slic3r/Utils/bambu_networking.hpp`

**Fixture / test data:** none (inline version strings)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| extract_base_version | `[BambuNetworking]` | **Version without suffix**: Returns the full version string unchanged (e.g., "02.03.00.62"). <br> **Version with suffix**: Returns only the base version, stripping the suffix after the dash (e.g., "02.03.00.62-mod" → "02.03.00.62"). <br> **Empty string**: Returns empty string. <br> **Suffix only**: Returns empty string for input "-mod". |
| extract_suffix | `[BambuNetworking]` | **Version without suffix**: Returns empty string. <br> **Version with suffix**: Returns suffix without the leading dash (e.g., "02.03.00.62-mod" → "mod"). <br> **Multiple dashes**: Returns everything after the first dash (e.g., "02.03.00.62-test-build" → "test-build"). <br> **Empty string**: Returns empty string. <br> **Suffix only**: Returns suffix without leading dash (e.g., "-mod" → "mod"). |
| NetworkLibraryVersionInfo::from_static | `[BambuNetworking]` | **Static version conversion**: Converts static version info to `NetworkLibraryVersionInfo` with correct fields (version, base_version, suffix, display_name, url_override, is_latest, warning, is_discovered). <br> **Warning handling**: Preserves warning message from static version. <br> **URL override**: Preserves URL override from static version. |
| NetworkLibraryVersionInfo::from_discovered | `[BambuNetworking]` | **Discovered version conversion**: Creates `NetworkLibraryVersionInfo` from discovered version strings with correct fields. <br> **Suffix extraction**: Correctly extracts suffix from version string. <br> **Discovered flag**: Sets `is_discovered` to true. |

**Special notes:**
- **Exact string comparisons**: All tests use `REQUIRE` for exact string comparisons (no floating-point tolerances).
- **Version parsing**: Tests version string parsing for Bambu networking library management.
- **Static vs discovered**: Tests both static version info and discovered version info conversion.
- **Source file**: `src/slic3r/Utils/bambu_networking.hpp`. |

## Suite: fff_print/

### test_data.cpp + test_data.hpp

**Source under test:** `src/libslic3r/` (various modules)

**Fixture / test data:** Shared fixture builder providing test meshes, models, and print initialization functions.

**Role:** Shared fixture builder.

**Fixtures provided:**

| Function | What it creates | Used by |
|---|---|---|
| `mesh(TestMesh m)` | TriangleMesh from predefined test mesh enum (e.g., `cube_20x20x20`, `sphere_50mm`, `bridge`) | All tests requiring specific test meshes |
| `mesh(TestMesh m, Vec3d translate, Vec3d scale)` | Translated and scaled TriangleMesh from test mesh enum | Tests requiring positioned/scaled meshes |
| `mesh(TestMesh m, Vec3d translate, double scale)` | Translated and uniformly scaled TriangleMesh | Tests requiring positioned/scaled meshes |
| `model(const std::string& model_name, TriangleMesh&& _mesh)` | Slic3r::Model with one object containing the mesh | Tests requiring Model objects |
| `init_print(std::vector<TriangleMesh>&&, Print&, Model&, const DynamicPrintConfig&, bool)` | Initializes Print with meshes, config, and arrangement | Tests requiring print initialization |
| `init_print(std::initializer_list<TestMesh>, Print&, Model&, const DynamicPrintConfig&, bool)` | Initializes Print with test mesh enums | Tests requiring print initialization |
| `init_print(std::initializer_list<TriangleMesh>, Print&, Model&, const DynamicPrintConfig&, bool)` | Initializes Print with triangle meshes | Tests requiring print initialization |
| `init_print(std::initializer_list<TestMesh>, Print&, Model&, std::initializer_list<ConfigBase::SetDeserializeItem>, bool)` | Initializes Print with test meshes and config items | Tests requiring print initialization |
| `init_print(std::initializer_list<TriangleMesh>, Print&, Model&, std::initializer_list<ConfigBase::SetDeserializeItem>, bool)` | Initializes Print with meshes and config items | Tests requiring print initialization |
| `init_and_process_print(std::initializer_list<TestMesh>, Print&, const DynamicPrintConfig&, bool)` | Initializes and processes print with test meshes | Tests requiring full print pipeline |
| `init_and_process_print(std::initializer_list<TriangleMesh>, Print&, const DynamicPrintConfig&, bool)` | Initializes and processes print with meshes | Tests requiring full print pipeline |
| `init_and_process_print(std::initializer_list<TestMesh>, Print&, std::initializer_list<ConfigBase::SetDeserializeItem>, bool)` | Initializes and processes print with test meshes and config items | Tests requiring full print pipeline |
| `init_and_process_print(std::initializer_list<TriangleMesh>, Print&, std::initializer_list<ConfigBase::SetDeserializeItem>, bool)` | Initializes and processes print with meshes and config items | Tests requiring full print pipeline |
| `gcode(Print& print)` | Generates G-code string from Print object | Tests requiring G-code output |
| `slice(std::initializer_list<TestMesh>, const DynamicPrintConfig&, bool)` | Slices test meshes and returns G-code string | Tests requiring G-code output |
| `slice(std::initializer_list<TriangleMesh>, const DynamicPrintConfig&, bool)` | Slices meshes and returns G-code string | Tests requiring G-code output |
| `slice(std::initializer_list<TestMesh>, std::initializer_list<ConfigBase::SetDeserializeItem>, bool)` | Slices test meshes with config items and returns G-code string | Tests requiring G-code output |
| `slice(std::initializer_list<TriangleMesh>, std::initializer_list<ConfigBase::SetDeserializeItem>, bool)` | Slices meshes with config items and returns G-code string | Tests requiring G-code output |

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| init_print functionality | `[test_data][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Single mesh initialization**: `init_print()` with one mesh creates one print object. <br> **Process safety**: `print.process()` does not throw exceptions. <br> **G-code output**: `gcode()` produces non-empty output. |

**Special notes:**
- **[DISABLED]** The only test case in this file is disabled (`[.]` tag) and not built by default.
- **Fixture builder**: This file provides shared utilities for other tests, not domain-specific logic.
- **TestMesh enumeration**: Defines 19 predefined test meshes (cubes, spheres, bridges, etc.) for consistent test data.
- **No Catch::Approx**: No floating-point comparisons in fixture builder functions. |

### test_flow.cpp

**Source under test:** `src/libslic3r/Flow.cpp`

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Extrusion width specifics | `[Flow][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **First layer width**: When `first_layer_extrusion_width` is set to 2mm, first layer extrusion uses this width for all extrusions on the first layer. <br> **Upper layer behavior**: First layer width does not apply to upper layers. <br> **Tolerance**: Uses `Catch::Approx` with margin 0.01 for Z-coordinate comparison. |
| Bridge flow specifics | `[Flow]` | **Bridge flow ratio**: Tests that bridge flow ratio settings (0.5, 1.0, 2.0) produce expected output flow. <br> **Fixed extrusion width**: Tests bridge flow with fixed extrusion width of 0.4mm. <br> **[DISABLED]** Test bodies are empty (placeholder for future implementation). |
| Flow: Flow math for non-bridges | `[Flow]` | **External perimeter spacing**: External perimeter flow spacing is fixed to 1.125 × nozzle_diameter - layer_height × (1.0 - PI/4.0). <br> **Internal perimeter spacing**: Internal perimeter flow spacing is fixed to 1.125 × nozzle_diameter - layer_height × (1.0 - PI/4.0). <br> **Supplied width spacing**: For supplied width, spacing is width.value - layer_height × (1.0 - PI/4.0). <br> **Min/max width**: For nozzle diameter 0.25mm, max/min width is set to 1.125 × nozzle_diameter. <br> **[DISABLED]** Edge case test for spacing = 0 (lines 138-152) is commented out. <br> **Tolerance**: All comparisons use `Catch::Approx`. |
| Flow: Flow math for bridges | `[Flow]` | **Bridge width**: Bridge width equals nozzle diameter. <br> **Bridge spacing**: Bridge spacing equals nozzle diameter + BRIDGE_EXTRA_SPACING. <br> **Tolerance**: All comparisons use `Catch::Approx`. |

**Special notes:**
- **Catch::Approx usage**: Extensive use of `Catch::Approx` for floating-point comparisons (lines 42, 52, 104, 109, 113, 115, 126, 133, 164, 167). Porting agent must reproduce tolerance (default Catch2 Approx tolerance).
- **Disabled tests**: Several tests are disabled (`[.]` tag or `#if 0`).
- **Flow calculations**: Tests validate mathematical formulas for extrusion width, spacing, and flow ratios.
- **Bridge flow**: Tests bridge-specific flow calculations with extra spacing. |

### test_fill.cpp

**Source under test:** `src/libslic3r/Fill/`

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Fill: Pattern Path Length | `[Fill]` | **Rectilinear fill**: Rectilinear fill of a square produces one continuous path with expected length (above scale(3*100 + 2*50) + scaled_epsilon). <br> **Diamond with endpoints on grid**: Rectilinear fill of a diamond shape produces one continuous path. <br> **Square with hole**: Rectilinear fill of a square with a hole produces 1-3 continuous paths that do not cross the hole. <br> **Regression: Missing infill segments**: Rectilinear fill of a specific polygon produces one continuous path with expected length. <br> **Rotated square**: Rectilinear fill of a rotated square produces one continuous path for 0° and 45° rotations. <br> **[DISABLED]** `adjusted solid distance` test (lines 20-27) is commented out. |
| Solid surface fill | `[Fill]` | **Solid surface filling**: Various solid surfaces are fully filled by rectilinear infill. <br> **Multiple sizes**: Solid surface fill works for different polygon sizes and scales. <br> **Different angles**: Solid surface fill works for different fill angles (0°, 45°, 90°). <br> **[DISABLED]** Precision-sensitive test (lines 142-159) is commented out due to Mac VM issues. |

**Special notes:**
- **SCALED_EPSILON usage**: Uses `SCALED_EPSILON` for length comparisons (lines 56, 118).
- **Disabled tests**: Several tests are disabled (`#if 0` blocks).
- **Fill patterns**: Tests rectilinear infill pattern generation and path length calculations.
- **Solid surface filling**: Tests that solid surfaces are completely filled without gaps.
- **Polygon operations**: Uses Clipper operations (`diff_pl`, `offset`) to verify infill boundaries. |

### test_extrusion_entity.cpp

**Source under test:** `src/libslic3r/ExtrusionEntity.cpp`

**Fixture / test data:** none (inline random path generation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| ExtrusionEntityCollection: Polygon flattening | `[ExtrusionEntity]` | **Flatten with default options**: Flattening an `ExtrusionEntityCollection` with `preserve_order=false` produces an output collection containing no child collections. <br> **Flatten with preservation**: Flattening with `preserve_order=true` preserves the order of elements and produces an output collection containing exactly one child collection (the one marked `no_sort`). <br> **Order preservation**: The ordered child collection contains the same sequence of extrusion paths as the original `no_sort` collection, matching first and last points for each path. |

**Special notes:**
- **No Catch::Approx**: All comparisons are exact (point equality, size checks).
- **Random seed**: Uses `srand(0xDEADBEEF)` for reproducible random path generation.
- **Collection flattening**: Tests `ExtrusionEntityCollection::flatten()` with and without order preservation.
- **No sorting**: Tests handling of `no_sort` flag in child collections. |

### test_model.cpp

**Source under test:** `src/libslic3r/Model.cpp`

**Fixture / test data:** none (inline mesh generation via `make_cube`)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Model construction | `[Model][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Model object addition**: Adding a model object increases the model object list size to 1. <br> **Model volume addition**: Adding a volume to a model object increases the volume list size to 1. <br> **Volume type**: Model volumes are marked as model parts. <br> **Mesh equivalence**: The mesh vertices in the model volume match the input mesh vertices (within EPSILON tolerance). <br> **Print generation**: Print process completes successfully and exports G-code to a temporary file. <br> **G-code file**: Temporary G-code file exists, is a regular file, and has non-zero size. |

**Special notes:**
- **EPSILON tolerance**: Mesh vertex comparison uses `EPSILON` (line 41) - exact tolerance value not specified in source.
- **Disabled test**: Test is marked with `[.]` tag and not built by default.
- **Print pipeline**: Tests full print pipeline including arrangement, extruder assignment, G-code export.
- **File operations**: Uses Boost filesystem for temporary file creation and cleanup. |

### test_print.cpp

**Source under test:** `src/libslic3r/Print.cpp`

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| PrintObject: Perimeter generation | `[PrintObject][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Layer count**: 20mm cube with 0.25mm layer height produces 66 layers. <br> **Perimeter islands**: Every layer has exactly 1 island of perimeters in region 0. <br> **Perimeter paths**: Every layer has exactly 3 paths in its perimeters list. |
| Print: Skirt generation | `[Print][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Skirt loops**: Setting skirts to 2 loops produces exactly 2 loops in the skirt extrusion collection. |
| Print: Changing number of solid surfaces does not cause all surfaces to become internal | `[Print][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Solid top layers**: Changing `top_solid_layers` from 2 to 3 correctly updates the number of solid top layers. <br> **Solid bottom layers**: Solid bottom layers remain unchanged when top solid layers are modified. |
| Print: Brim generation | `[Print][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Brim width**: 3mm brim produces exactly 3 loops. <br> **Brim width**: 6mm brim produces exactly 6 loops. <br> **Brim with extrusion width**: 6mm brim with 0.5mm extrusion width produces 14 loops. |

**Special notes:**
- **All tests disabled**: Every test case in this file is marked with `[.]` tag and not built by default.
- **Integer comparisons**: All assertions use exact integer comparisons (no floating-point tolerances).
- **Layer counting**: Tests validate layer count and perimeter structure for 20mm cube.
- **Skirt and brim**: Tests validate skirt and brim loop counts based on configuration.
- **Solid surface handling**: Tests validate that changing solid layer counts doesn't corrupt surface types. |

### test_printobject.cpp

**Source under test:** `src/libslic3r/PrintObject.cpp`

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| PrintObject: object layer heights | `[PrintObject][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **2mm layer height (nozzle 3mm)**: 20mm cube with 2mm layer height and 3mm nozzle produces 10 layers, each exactly 2mm above the previous Z. <br> **10mm layer height (nozzle 11mm)**: 20mm cube with 10mm layer height and 11mm nozzle produces 3 layers: first at 2mm, second at 12mm. <br> **15mm layer height (nozzle 16mm)**: 20mm cube with 15mm layer height and 16mm nozzle produces 2 layers: first at 2mm, second at 17mm. <br> **[DISABLED]** 15mm layer height with 5mm nozzle test is commented out (lines 69-87). |

**Special notes:**
- **Catch::Approx usage**: All floating-point comparisons use `Catch::Approx` (lines 28, 45, 48, 63, 66). Porting agent must reproduce tolerance (default Catch2 Approx tolerance).
- **Disabled tests**: All tests are marked with `[.]` tag, and one test case is commented out with `#if 0`.
- **Layer height generation**: Tests validate that layer heights are correctly calculated based on nozzle diameter and configured layer height.
- **Z-coordinate precision**: Tests verify exact Z-coordinates for each layer. |

### test_trianglemesh.cpp

**Source under test:** `src/libslic3r/TriangleMesh.cpp`

**Fixture / test data:** none (inline mesh generation via `make_cube`, `make_cylinder`, `make_sphere`)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| TriangleMesh: Basic mesh statistics | (none) | **Volume calculation**: 20mm cube volume is 8000mm³ (within 1e-2 tolerance). <br> **Vertex array matches input**: Mesh vertices match input vertex array exactly. <br> **Facet array matches input**: Mesh facets match input facet array exactly. <br> **Facet count**: Facet count matches input array size. <br> **Center calculation**: Cube center is at (10, 10, 10). <br> **Size calculation**: Cube size is (20, 20, 20). |
| TriangleMesh: Transformation functions affect mesh as expected | (none) | **Uniform scaling**: 200% uniform scaling produces 40x40x40 cube (volume 64000). <br> **X-axis scaling**: 200% X scaling doubles volume and sets X coordinate to 40. <br> **X-axis scaling 25%**: 25% X scaling reduces volume to 25% and sets X coordinate to 5. <br> **Rotation**: 45° Z-rotation sets X size to sqrt(2)*20. <br> **Translation**: Translation moves vertices correctly. <br> **Align to origin**: Aligning to origin sets first vertex to (0,0,0). |
| TriangleMesh: slice behavior | (none) | **Cube slicing**: Slicing 20mm cube at various Z heights produces one polygon per layer with correct area (400 / SCALING_FACTOR²). <br> **Irregular shape slicing**: Slicing irregular shape produces polygons with positive area. <br> **Transformed mesh slicing**: Mirrored mesh slices correctly at negative Z heights. |
| make_xxx functions produce meshes | (none) | **make_cube**: Cube has one vertex at (0,0,0), volume 8000, 12 facets. <br> **make_cylinder**: Cylinder has vertices at (0,0,0) and (0,0,10), correct vertex/facet counts, volume ~3141.59. <br> **make_sphere**: Sphere has vertices at (0,0,-10) and (0,0,10), volume ~4188.79. |
| TriangleMesh: split functionality | (none) | **Single mesh split**: Splitting a single mesh produces one output mesh with same bounding box. <br> **Merged mesh split**: Merging two cubes and splitting produces two output meshes. |
| TriangleMesh: Mesh merge functions | (none) | **Mesh merge**: Merging two cubes doubles facet count. |
| TriangleMeshSlicer: Cut behavior | (none) | **Cut at bottom**: Cutting at Z=0 produces upper mesh with all facets, lower mesh with no facets. <br> **Cut at center**: Cutting at Z=10 produces upper and lower meshes each with 20 facets (2+12+6). |
| Regression test for issue #4486 | `[Performance]` | **[DISABLED]** Requires `TEST_PERFORMANCE` define. Tests that slicing 100,000 facet mesh completes within 120 seconds. |
| Profile test for issue #4486 | `[Performance]` | **[DISABLED]** Requires `BUILD_PROFILE` define. Tests that slicing 10,000 facet mesh completes successfully. |

**Special notes:**
- **Exact comparisons**: Most tests use exact integer or floating-point comparisons (no `Catch::Approx`).
- **Volume tolerances**: Some volume tests use absolute tolerance (1e-2 or 1).
- **is_approx usage**: Sphere vertex tests use `is_approx()` for floating-point comparison.
- **Conditional tests**: Performance tests require special build flags (`TEST_PERFORMANCE`, `BUILD_PROFILE`).
- **SCALING_FACTOR**: Area calculations use `SCALING_FACTOR` constant for coordinate scaling. |

### test_gcode.cpp

**Source under test:** `src/libslic3r/GCode.cpp`

**Fixture / test data:** none (inline GCode object creation)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Origin manipulation | `[GCode]` | **Set origin**: Setting origin to (10, 0) updates `gcodegen.origin()` to (10, 0). <br> **Set origin and translate**: Setting origin to (10, 0) then adding (5, 5) updates origin to (15, 5). |

**Special notes:**
- **Exact comparisons**: All tests use exact `Vec2d` comparisons (no floating-point tolerances).
- **Simple tests**: Only tests basic origin manipulation in `GCode` class.
- **No Catch::Approx**: No floating-point comparisons used. |

### test_gcodewriter.cpp

**Source under test:** `src/libslic3r/GCodeWriter.cpp`

**Fixture / test data:** `tests/data/fff_print_tests/test_gcodewriter/config_lift_unlift.ini`

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| lift() is not ignored after unlift() at normal values of Z | `[GCodeWriter][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Lift after unlift**: Calling `lazy_lift()` after `unlift()` at the same Z height produces G-code (non-empty string). <br> **No redundant moves**: Moving to the same Z height after lift produces no additional G-code (empty string). <br> **Multiple Z values**: Test validates behavior at Z=203, Z=500003, and Z=10.3. |
| set_speed emits values with fixed-point output | `[GCodeWriter]` | **Speed formatting**: `set_speed()` outputs G-code with correct fixed-point precision: <br> - 99999.123 → "G1 F99999.123\n" <br> - 1.0 → "G1 F1\n" <br> - 203.200022 → "G1 F203.2\n" <br> - 203.200522 → "G1 F203.201\n" |

**Special notes:**
- **String comparison**: Uses `Catch::Matchers::Equals` for exact string comparison (no floating-point tolerances).
- **Disabled test**: First test case is marked with `[.]` tag and not built by default.
- **Config file**: First test loads configuration from `config_lift_unlift.ini`.
- **Fixed-point output**: Tests verify that speed values are formatted with appropriate precision. |

### test_printgcode.cpp

**Source under test:** `src/libslic3r/GCode.cpp` (full print→gcode pipeline)

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| PrintGCode basic functionality | `[PrintGCode][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **G-code generation**: Print generates non-empty G-code output. <br> **Version info**: G-code contains Slic3R_VERSION string. <br> **Extrusion statistics**: G-code contains extrusion width comments for perimeters, infill, solid infill, and top infill. <br> **Cooling markers**: G-code does not contain consumed cooling markers (`;_EXTRUDE_SET_SPEED`). <br> **G-code preamble**: G-code contains `G21 ; set units to millimeters`. <br> **Config options**: G-code contains comments for first_layer_temperature, layer_height, fill_density. <br> **Infill, perimeters, skirt**: G-code contains infill, perimeters, and skirt extrusions (validated via regex). <br> **Final Z height**: Final Z height is 20.0mm (uses `Catch::Approx`). <br> **Complete objects**: Two objects printed with `complete_objects=true` produce between-object G-code. <br> **Z height reset**: Z height resets on object change (validated via GCodeReader). <br> **Shorter object first**: Shorter object is printed before taller object. <br> **Support material**: Support material and raft are emitted when enabled. <br> **First layer extrusion width**: Separate first layer extrusion width is reflected in G-code comments. <br> **Cooling**: Fan disable G-code (`M107`) is emitted when cooling enabled. <br> **Layer variables**: `layer_num` and `layer_z` variables are processed in end G-code. <br> **Current extruder**: `current_extruder` variable is processed in start G-code. <br> **Layer index**: Layer index from z=0 is correct for multiple objects. |

**Special notes:**
- **Catch::Approx usage**: Uses `Catch::Approx` for Z-height comparisons (lines 92, 134, 270, 277).
- **Regex matching**: Uses Boost regex to validate presence of infill, perimeters, and skirt extrusions.
- **Disabled tests**: All test cases are marked with `[.]` tag and not built by default.
- **GCodeReader**: Uses `GCodeReader` to parse G-code and validate Z heights.
- **Multiple objects**: Tests validate behavior with multiple objects and `complete_objects` mode. |

### test_skirt_brim.cpp

**Source under test:** `src/libslic3r/Brim.cpp`

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Skirt height is honored | `[Skirt][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Single object**: Skirt is generated on exactly `skirt_height` layers (5 layers). <br> **Multiple objects**: Skirt is generated on exactly `skirt_height` layers for multiple objects. <br> **Tolerance**: Uses `Catch::Approx` for speed comparison (line 56). |
| Original Slic3r Skirt/Brim tests | `[SkirtBrim][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Brim generation**: Brim is generated when `brim_width` is set to 5mm. <br> **Skirt smaller than brim**: G-code is generated when skirt area is smaller than brim. <br> **Skirt height 0**: G-code is generated when `skirt_height` is 0 but `skirts` > 0. <br> **Brim line count**: 2 brim lines are generated when `brim_width` is 1mm and `first_layer_extrusion_width` is 0.5mm. <br> **Overhang with brim**: G-code is generated for object with overhang support and brim. <br> **Large minimum skirt length**: G-code generation doesn't crash with large `min_skirt_length`. <br> **[DISABLED]** Several test cases are commented out with `#if 0` (brim extruder selection, brim ears). |

**Special notes:**
- **Catch::Approx usage**: Uses `Catch::Approx` for floating-point comparisons (lines 56, 92, 93, 240, 245, 252, 253).
- **Disabled tests**: All test cases are marked with `[.]` tag and not built by default.
- **GCodeReader**: Uses `GCodeReader` to parse G-code and validate skirt/brim generation.
- **Brim map**: Tests validate brim entities count using `print.get_brimMap()`.
- **Multiple extruders**: Tests validate brim generation with multiple extruders. |

### test_support_material.cpp

**Source under test:** `src/libslic3r/Support/SupportMaterial.cpp`

**Fixture / test data:** Uses `test_data.hpp` for mesh generation and print initialization.

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| SupportMaterial: Three raft layers created | `[SupportMaterial][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **Raft layer count**: Enabling support material with 3 raft layers produces exactly 3 support layers. |
| SupportMaterial: support_layers_z and contact_distance | `[SupportMaterial][.]` | **[DISABLED]** This test is marked with `[.]` tag and is not built by default. <br> **First layer height**: First support layer height matches `first_layer_height` configuration. <br> **Layer height bounds**: Support layers satisfy minimum and maximum layer height constraints. <br> **No null/negative layers**: All support layers have positive height. <br> **No excessive layer thickness**: No support layer exceeds nozzle diameter. |

**Special notes:**
- **EPSILON tolerance**: Uses `EPSILON` for floating-point comparisons (lines 41, 43).
- **Disabled tests**: All test cases are marked with `[.]` tag or commented out with `#if 0`.
- **Support layer validation**: Tests validate support layer heights and contact distances.
- **No Catch::Approx**: Uses direct floating-point comparisons with `EPSILON`. |

## Suite: sla_print/

### sla_print_tests.cpp

**Source under test:** `src/libslic3r/SLAPrint.cpp` + `src/libslic3r/SLAPrintSteps.cpp`

**Fixture / test data:** Uses `tests/data/` (model files: `20mm_cube.obj`, `V.obj`, `frog_legs.obj`, etc.)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Pillar pairhash should be unique | `[SLASupportGeneration]` | **Pairhash uniqueness**: `test_pairhash()` validates that pairhash template generates unique hashes for various type combinations. |
| Support point generator should be deterministic if seeded | `[SLASupportGeneration], [SLAPointGen]` | **Deterministic generation**: Support point generator produces identical output when seeded with same value. <br> **Point count**: Generator produces non-zero number of support points. <br> **Checksum validation**: Generated points produce consistent checksum across multiple runs. |
| Flat pad geometry is valid | `[SLASupportGeneration]` | **Flat pad generation**: Pad geometry is valid for test objects without wings (`wall_height_mm = 0`). |
| WingedPadGeometryIsValid | `[SLASupportGeneration]` | **Winged pad generation**: Pad geometry with wings (`wall_height_mm = 1`) is valid for test objects. |
| FlatPadAroundObjectIsValid | `[SLASupportGeneration]` | **Flat pad around object**: Pad geometry embedding object is valid without wings. |
| WingedPadAroundObjectIsValid | `[SLASupportGeneration]` | **Winged pad around object**: Pad geometry embedding object with wings is valid. |
| ElevatedSupportGeometryIsValid | `[SLASupportGeneration]` | **Elevated support generation**: Support tree geometry with object elevation is valid. |
| FloorSupportGeometryIsValid | `[SLASupportGeneration]` | **Floor support generation**: Support tree geometry without elevation is valid. |
| ElevatedSupportsDoNotPierceModel | `[SLASupportGeneration]` | **No collision (elevated)**: Elevated supports do not intersect the model geometry. |
| FloorSupportsDoNotPierceModel | `[SLASupportGeneration]` | **No collision (floor)**: Floor supports do not intersect the model geometry. |
| InitializedRasterShouldBeNONEmpty | `[SLARasterOutput]` | **Raster initialization**: Raster object is initialized with correct resolution and pixel dimensions. <br> **Tolerance**: Uses `Catch::Approx` for pixel dimension comparisons. |
| MirroringShouldBeCorrect | `[SLARasterOutput]` | **Raster transformations**: Raster mirroring and orientation transformations are correct. |
| RasterizedPolygonAreaShouldMatch | `[SLARasterOutput]` | **Raster area accuracy**: Rasterized polygon area matches expected area within predicted error tolerance. |
| halfcone test | `[halfcone]` | **Halfcone mesh generation**: DiffBridge mesh generation produces valid geometry (exports to OBJ). |
| Test concurrency | (none) | **Concurrency correctness**: Parallel accumulation produces same result as sequential accumulation. <br> **Tolerance**: Uses `Catch::Approx` for floating-point comparison. |

**Special notes:**
- **Catch::Approx usage**: Used on lines 168, 169, 243 for pixel dimensions and concurrency results.
- **Test data files**: Uses model files from `tests/data/` (20mm_cube.obj, V.obj, frog_legs.obj, etc.).
- **SLA-specific**: Tests validate SLA support generation, pad geometry, and raster output.
- **No disabled tests**: All tests in this file are enabled (no `[.]` tag). |

### sla_raycast_tests.cpp

**Source under test:** `src/libslic3r/SLA/IndexedMesh.cpp` + `src/libslic3r/SLA/AABBMesh.cpp`

**Fixture / test data:** Uses `tests/data/` (model file: `20mm_cube.obj`)

**Tests:**

| TEST_CASE name | Tags | What it contractually guarantees |
|---|---|---|
| Raycaster - find intersections of a line and cylinder | (none) | **Hole raycaster**: Drain hole raycaster correctly finds intersections between a line and a cylindrical hole. <br> **Entry/exit distances**: Entry and exit distances are calculated with tolerance 0.001f. |
| Raycaster with loaded drillholes | `[sla_raycast]` | **[CONDITIONAL]** Requires `SLIC3R_HOLE_RAYCASTER` define. <br> **Ray hit distance**: Ray from cube center hits interior wall at expected distance (min thickness). <br> **Hole side hit**: Ray from hole center hits hole side at radius distance. <br> **Back side hit**: Ray from outside hits back side of cube interior. <br> **Downward ray**: Ray downward through hole cylinder hits expected distance. <br> **Support collision**: Support tree generation with drain holes does not collide with model. <br> **Tolerance**: Uses `Catch::Approx` for all distance comparisons. |

**Special notes:**
- **Catch::Approx usage**: Used for all distance comparisons (lines 23, 24, 73, 79, 84, 91).
- **Conditional compilation**: Second test requires `SLIC3R_HOLE_RAYCASTER` define.
- **Drain hole geometry**: Tests validate raycasting through cylindrical drain holes in hollowed mesh.
- **Mesh operations**: Tests use `sla::IndexedMesh` and `sla::DrainHole` classes. |

















