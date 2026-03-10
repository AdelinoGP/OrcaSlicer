# Pseudocode — Triangle Mesh Slicer: Phase 2 Line-Chaining Algorithm

**Task:** T205A  
**Source file:** `src/libslic3r/TriangleMeshSlicer.cpp`  
**Entry point:** `chain_lines_by_triangle_connectivity()` at line 1166  
**Caller:** `make_loops()` at line 1536  
**See also:** Section 1 of `03_algorithmic_complexities.md` for Phase 1 (triangle intersection).

---

## Context and Purpose

After Phase 1 produces a flat `IntersectionLines` array for each layer — one `IntersectionLine` per triangle/plane intersection — Phase 2 assembles those line segments into closed polygon loops (or open polylines for non-manifold meshes).

The key insight: two adjacent triangles share an edge, so if triangle A's intersection produces a line segment ending on mesh edge E, and triangle B's intersection produces a line segment starting on the same mesh edge E, those two segments are topologically adjacent with **zero distance** between their shared endpoint. Phase 2 exploits this by matching topological IDs (not XY coordinates), making the inner lookup O(log N) via binary search on sorted arrays.

---

## Data Structures

### `IntersectionLine`

```
IntersectionLine:
    a, b          : Point (coord_t scaled XY — the 2D segment endpoints)
    a_id, b_id    : int   — mesh vertex index (-1 if endpoint is on a mesh edge, not a vertex)
    edge_a_id, edge_b_id : int — mesh edge index (-1 if endpoint is on a vertex, not an edge midpoint)
    flags         : uint32 — bitmask:
                      EDGE0_NO_NEIGHBOR = 0x001  — edge 0 of source triangle had no neighbor
                      EDGE1_NO_NEIGHBOR = 0x002
                      EDGE2_NO_NEIGHBOR = 0x004
                      EDGE0_FOLD        = 0x010  — edge 0 was a fold with a horizontal face
                      EDGE1_FOLD        = 0x020
                      EDGE2_FOLD        = 0x040
                      NO_SEED           = 0x100  — this line must not be used as a loop seed
                      SKIP              = 0x200  — this line has already been consumed
    edge_type     : FacetEdgeType (General | Top | Bottom | TopBottom | Horizontal | Slab)
```

**Endpoint ID encoding (implicit rule):**
- Each endpoint carries **exactly one** of `{a_id, edge_a_id}` set (non -1). The other is -1.
- If the intersection point falls exactly on a mesh **vertex**, `a_id` = the mesh vertex index and `edge_a_id = -1`.
- If the intersection point falls on the **interior of a mesh edge**, `edge_a_id` = the mesh edge index and `a_id = -1`.
- This encoding means two adjacent triangle intersection segments always share either the same `b_id`/`a_id` pair (vertex-to-vertex) or the same `edge_b_id`/`edge_a_id` pair (edge-to-edge).

---

## Phase 2 Pseudocode

### `chain_lines_by_triangle_connectivity(lines, loops, open_polylines)`

```
INPUT:  lines         — vector<IntersectionLine> for one Z layer (mutated in-place via SKIP flags)
OUTPUT: loops         — vector<Polygon> (closed output polygons, appended to)
        open_polylines — vector<OpenPolyline> (unclosed chains, appended to)

// --- Step 1: Build sorted lookup arrays ---
by_edge_a_id = []
by_a_id      = []

FOR each line IN lines:
    IF line.flags has SKIP: CONTINUE          // already consumed, exclude from lookup
    IF line.edge_a_id != -1:
        APPEND &line TO by_edge_a_id          // this line's 'a' end is on a mesh edge
    IF line.a_id != -1:
        APPEND &line TO by_a_id               // this line's 'a' end is on a mesh vertex

SORT by_edge_a_id by line.edge_a_id ascending  // O(N log N)
SORT by_a_id      by line.a_id ascending        // O(N log N)

// --- Step 2: Greedy loop assembly ---
seed_cursor = lines.begin()

LOOP:
    // Find the next unconsumed seed line
    first_line = NULL
    WHILE seed_cursor != lines.end():
        IF seed_cursor.is_seed_candidate():    // NOT(NO_SEED) AND NOT(SKIP)
            first_line = seed_cursor
            seed_cursor.set_skip()             // mark consumed
            ADVANCE seed_cursor
            BREAK
        ADVANCE seed_cursor

    IF first_line == NULL:
        BREAK  // no more seeds: all lines consumed or flagged NO_SEED

    loop_pts = [first_line.a]
    last_line = first_line

    // Walk the chain from this seed
    LOOP:
        next_line = NULL

        // --- Lookup by mesh edge ID (primary) ---
        IF last_line.edge_b_id != -1:
            // Search by_edge_a_id for any line where edge_a_id == last_line.edge_b_id
            range = binary_search_equal_range(by_edge_a_id, key.edge_a_id = last_line.edge_b_id)
            FOR candidate IN range:
                IF NOT candidate.skip():
                    next_line = candidate
                    BREAK

        // --- Lookup by mesh vertex ID (fallback) ---
        IF next_line == NULL AND last_line.b_id != -1:
            // Search by_a_id for any line where a_id == last_line.b_id
            range = binary_search_equal_range(by_a_id, key.a_id = last_line.b_id)
            FOR candidate IN range:
                IF NOT candidate.skip():
                    next_line = candidate
                    BREAK

        IF next_line == NULL:
            // --- No continuation found ---
            // Test if the chain wraps back to the start (closed loop)
            closed = (first_line.edge_a_id != -1 AND first_line.edge_a_id == last_line.edge_b_id)
                  OR (first_line.a_id      != -1 AND first_line.a_id      == last_line.b_id)

            IF closed:
                ASSERT first_line.a == last_line.b  (debug only)
                APPEND Polygon(loop_pts) TO loops
            ELSE:
                APPEND last_line.b TO loop_pts
                APPEND OpenPolyline(start=first_line.start_ref,
                                    end=last_line.end_ref,
                                    points=loop_pts) TO open_polylines
            BREAK  // done with this chain

        // Continue the chain
        ASSERT last_line.b == next_line.a  (debug only — geometric consistency)
        APPEND next_line.a TO loop_pts
        last_line = next_line
        next_line.set_skip()
    END LOOP
END LOOP

// Note: lines[] is left with SKIP flags set on consumed segments.
// NO_SEED lines that were never used as seeds remain with SKIP=false;
// they were consumed as continuations when their a_id/edge_a_id matched.
```

### Complexity

| Step | Complexity |
|------|------------|
| Build by_edge_a_id, by_a_id | O(N) |
| Sort both arrays | O(N log N) |
| Seed scan | O(N) total across all chains |
| Inner lookup per step | O(log N + duplicates) — binary search + linear scan of equal range |
| Total per layer | **O(N log N)** |

Where N = number of `IntersectionLine` segments for the layer.

---

## Phase 2b: Open Polyline Stitching

### `chain_open_polylines_exact(open_polylines, loops, try_connect_reversed)`

After the greedy pass, some open polylines remain (typically from non-manifold meshes, mesh boundaries, or horizontal faces). This second pass attempts to stitch them:

```
INPUT:  open_polylines         — vector<OpenPolyline> produced by chain_lines_by_triangle_connectivity
        loops                  — vector<Polygon> to append completed loops to
        try_connect_reversed   — bool: if true, allow joining segments of opposite orientation

// Build lookup of polyline endpoints sorted by their IntersectionReference ID
by_id = []
FOR each opl IN open_polylines:
    IF NOT opl.consumed:
        APPEND OpenPolylineEnd(opl, start=true)  TO by_id
        APPEND OpenPolylineEnd(opl, start=false) TO by_id

SORT by_id by computed_id() ascending
    WHERE computed_id() = +point_id  if endpoint is on a vertex
                        = -edge_id   if endpoint is on an edge interior

// Iterate longest polylines first (better chance of forming a complete loop)
sorted = open_polylines sorted by length descending, excluding consumed

FOR each seed_opl IN sorted:
    IF seed_opl.consumed: CONTINUE

    // Try to extend seed_opl until it closes or no continuation exists
    LOOP:
        end_ref = seed_opl.end  // the IntersectionReference of the chain's current tail

        // Search by_id for a polyline whose start matches end_ref
        range = binary_search_equal_range(by_id, id matching end_ref)
        found = NULL
        FOR candidate IN range:
            IF candidate.polyline != seed_opl AND NOT candidate.polyline.consumed:
                IF candidate.start == true:           // head matches → can append forward
                    found = (candidate.polyline, forward)
                    BREAK
                ELIF try_connect_reversed:            // tail matches → can append reversed
                    found = (candidate.polyline, reversed)
                    BREAK

        IF found == NULL: BREAK  // cannot extend further

        // Extend seed_opl by appending found polyline (forward or reversed)
        MERGE found.polyline INTO seed_opl
        found.polyline.consumed = true
        UPDATE by_id entries for merged endpoints

        // Test closure: seed_opl closes if its start IntersectionReference == end IntersectionReference
        IF seed_opl.start.id == seed_opl.end.id:
            polygon = Polygon(seed_opl.points)
            IF try_connect_reversed AND polygon.area() < 0:
                REVERSE polygon.points   // recover CCW winding for reversed-join case
            APPEND polygon TO loops
            seed_opl.consumed = true
            BREAK
    END LOOP
```

**Important:** The inner loop uses a `goto found` in the source — a C++ implementation artifact. A port should replace this with a named function or a boolean flag.

---

## Translation Notes for Ports

### 1. Implicit endpoint ID encoding
The dual `{a_id, edge_a_id}` fields are not a union — they are two separate integers where exactly one is non-negative. A port must preserve this invariant. A cleaner representation would be `variant<VertexID, EdgeID>` or a tagged union, but the existing code relies on direct integer comparison to -1.

### 2. The NO_SEED flag and fold detection
`NO_SEED` is set on `IntersectionLine` entries that originate from **fold edges** (a horizontal triangle joined to an upward-facing or downward-facing triangle at a crease). These fold lines are valid edges in the mesh topology and must participate as chain continuations, but starting a new polygon loop from a fold can produce incorrect winding order. A port must replicate this distinction: fold lines are connectible but not seedable.

### 3. The SKIP flag state machine
`SKIP` is the consumed/used flag. The state machine is:
```
SKIP=0, NO_SEED=0 → eligible as seed; may be consumed as seed or continuation
SKIP=0, NO_SEED=1 → not eligible as seed; may be consumed as continuation
SKIP=1            → consumed (either as seed or continuation); excluded from all lookups
```
Note: `is_seed_candidate()` = NOT(NO_SEED) AND NOT(SKIP). Lines with NO_SEED=1 will never become a seed, but they can still appear in `by_edge_a_id` / `by_a_id` lookup arrays and be found as continuations. They get `set_skip()` called when consumed as a continuation.

### 4. Per-layer mutex scheme (Phase 1 → Phase 2 boundary)
Phase 1 runs `tbb::parallel_for` over triangles; each triangle writes to `lines[layer_idx]` under a per-layer-pair mutex (`lines_mutex[(layer_idx / 2) % mutex_count]`). This means:
- Two different layers may share the same mutex (stride-2 bucketing).
- By the time Phase 2 starts (`chain_lines_by_triangle_connectivity`), all Phase 1 writes are complete and the mutexes are no longer needed.
- Phase 2 runs **single-threaded per layer** — no mutex is held during chaining.
- Across layers, Phase 2 can be parallelized (and is, via `tbb::parallel_for` in `slice_mesh_ex`).

### 5. Degenerate discard
Zero-length segments (where `a == b` after rounding to `coord_t`) are not explicitly filtered during chaining. They produce zero-length `Polygon` entries in `loops`. The Clipper union step in Phase 3 (`slice_mesh_ex`) discards these automatically, but a port that skips the Clipper union must explicitly filter them.

### 6. T-junction handling
T-junctions occur when a mesh edge is shared by more than two triangles (non-manifold geometry). In this case, `by_edge_a_id` may contain **multiple** lines with the same `edge_a_id`. The inner `for (auto it_line = it_begin; it_line != it_end; ++it_line)` loop picks the first non-skipped one. This is the implicit T-junction disambiguation: the first valid continuation wins. No ordering guarantee is provided among equal-ID candidates; the result may differ between runs if the input order changes.
