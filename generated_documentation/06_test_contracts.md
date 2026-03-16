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
