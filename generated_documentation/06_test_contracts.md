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
