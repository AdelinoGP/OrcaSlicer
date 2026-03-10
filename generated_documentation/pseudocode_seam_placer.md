# Pseudocode: SeamPlacer Pipeline
**Source:** `src/libslic3r/GCode/SeamPlacer.cpp` · `src/libslic3r/GCode/SeamPlacer.hpp`
**Task:** T205D
**Generated:** 2026-03-10

---

## Table of Contents
1. [Overview](#1-overview)
2. [Data Structures](#2-data-structures)
3. [Utility Functions](#3-utility-functions)
4. [Phase 0 — Mesh Occlusion Sampling](#4-phase-0--mesh-occlusion-sampling)
5. [Phase 1 — Candidate Gathering](#5-phase-1--candidate-gathering)
6. [Phase 2 — Visibility Transfer](#6-phase-2--visibility-transfer)
7. [Phase 3 — Overhang and Embedding](#7-phase-3--overhang-and-embedding)
8. [Phase 4 — Seam Selection](#8-phase-4--seam-selection)
9. [Phase 5 — B-Spline Alignment](#9-phase-5--b-spline-alignment)
10. [Phase 6 — G-code Export: place_seam()](#10-phase-6--g-code-export-place_seam)
11. [Top-Level Orchestrator: SeamPlacer::init()](#11-top-level-orchestrator-seamplacerinit)
12. [Translation Notes](#12-translation-notes)

---

## 1. Overview

SeamPlacer decides where each extruder loop starts and stops (the "seam" — the visible scar on
the print surface). It runs once before G-code export (`init()`), then is queried per loop
(`place_seam()`).

**Six seam modes** (`SeamPosition` enum):
- `spAligned` — seam traces a smooth vertical line on the least-visible surface
- `spAlignedBack` — same but biased toward the back (+Y) face of the object
- `spNearest` — seam placed closest to current nozzle position (decided at G-code time)
- `spRear` — seam maximizes Y coordinate (always on the back)
- `spRandom` — seam placed at a random perimeter position, different each layer
- `spCustom` (implicit via enforcers/blockers) — user-painted regions override scoring

**Pipeline phases:**
```
init():
  for each PrintObject:
    1. gather_enforcers_blockers()          — load painted regions into AABB trees
    2. compute_global_occlusion()           — mesh sampling + hemisphere raycasting
    3. gather_seam_candidates()             — extract perimeter vertices + score prep
    4. calculate_candidates_visibility()   — transfer visibility scores to candidates
    [GlobalModelInfo freed here — large structure no longer needed]
    5. calculate_overhangs_and_layer_embedding()
    6. pick_seam_point() / pick_random_seam_point()  — per-perimeter seam selection
    7. align_seam_points()                 — B-spline smoothing across layers

place_seam():                              — called per ExtrusionLoop at G-code time
    find closest pre-computed perimeter
    retrieve seam position (finalized or live)
    split loop at seam point
```

**Key compile-time constants** (`SeamPlacer.hpp:176-213`):
```
raycasting_visibility_samples_count = 30000   // mesh sample points
sqr_rays_per_sample_point           = 5       // → 25 rays per sample (5×5 grid)
fast_decimation_triangle_count_target = 16000 // mesh simplification cap
sharp_angle_snapping_threshold      = 55° (0.959 rad)
overhang_angle_threshold            = 45° (π/4 rad)
angle_importance_aligned            = 0.6
angle_importance_nearest            = 1.0
enforcer_oversampling_distance      = 0.2 mm
seam_align_score_tolerance          = 0.3
seam_align_tolerable_dist_factor    = 4.0     // search radius = 4 × flow_width
seam_align_minimum_string_seams     = 6
seam_align_mm_per_segment           = 4       // [HAZARD: declared size_t, see §12]
```

---

## 2. Data Structures

### 2.1 `EnforcedBlockedSeamPoint` enum (`SeamPlacer.hpp:53`)
```
Blocked  = 0   // user painted "avoid seam here"
Neutral  = 1   // no user annotation
Enforced = 2   // user painted "put seam here"
```

### 2.2 `Perimeter` struct (`SeamPlacer.hpp:71`)
Shared by all SeamCandidates of one loop. Stored in `deque<Perimeter>` (stable references).
```
struct Perimeter:
  start_index       : size_t    // first SeamCandidate index in layer.points[]
  end_index         : size_t    // past-the-end index (exclusive, despite "inclusive!" comment)
  seam_index        : size_t    // chosen seam candidate index (set by pick_seam_point)
  flow_width        : float     // extrusion width for this perimeter (mm)
  finalized         : bool      // true iff align_seam_points() has stored final_seam_position
  final_seam_position : Vec3f   // B-spline fitted position (may lie off-perimeter)
```
[HAZARD] Comment says `end_index` is "inclusive!" but all iteration uses `index < end_index`
(exclusive). (`SeamPlacer.hpp:64`)

### 2.3 `SeamCandidate` struct (`SeamPlacer.hpp:101`)
One per perimeter vertex.
```
struct SeamCandidate:
  position          : Vec3f (mm, mesh coords)
  perimeter         : &Perimeter       // mutable non-owning ref; shared across loop points
  visibility        : float [0, 1+]    // 0=fully occluded (good), 1=fully visible (bad)
  overhang          : float (mm, ≥0)   // distance past previous-layer outline past threshold
  unsupported_dist  : float (mm)       // raw distance to previous-layer outline (signed)
  embedded_distance : float (mm)       // <-0.5 = hidden inside print (good for seam)
  local_ccw_angle   : float (rad)      // <0 = concave corner (good), >0 = convex (bad)
  type              : EnforcedBlockedSeamPoint
  central_enforcer  : bool             // true for anchor point of longest enforced segment
```

### 2.4 `PrintObjectSeamData` / `LayerSeams` (`SeamPlacer.hpp:142`)
```
PrintObjectSeamData:
  layers : vector<LayerSeams>          // one entry per physical layer

LayerSeams:
  perimeters  : deque<Perimeter>       // deque for stable &references
  points      : vector<SeamCandidate>  // all candidates, all perimeters, flat
  points_tree : unique_ptr<KDTree3f>   // 3D KD-tree over points[i].position
```

### 2.5 `GlobalModelInfo` struct (`SeamPlacer.cpp:392`)
Temporary; stack-allocated inside `init()`, destroyed after `calculate_candidates_visibility()`.
```
struct GlobalModelInfo:
  mesh_samples            : vector<Vec3f>   // 30000 uniform surface sample positions
  mesh_samples_visibility : vector<float>   // visibility score per sample [0,1+]
  mesh_samples_tree       : KDTree3f        // for O(log N) lookup in calculate_point_visibility
  mesh_samples_radius     : float           // Poisson search radius (≈ mm)
  enforcers               : ExPolygons      // painted "force seam here" regions
  blockers                : ExPolygons      // painted "avoid seam here" regions
  enforcers_tree          : AABBTree2D
  blockers_tree           : AABBTree2D
```

---

## 3. Utility Functions

### 3.1 `gauss(x, mean, falloff_speed, mean_value)` (`SeamPlacer.cpp:70`)
Bell curve scaled to [0, mean_value]:
```
gauss(x, mean, falloff_speed, mean_value):
  return mean_value * (exp(1 / (falloff_speed * (x - mean)^2 + 1)) - 1) / (e - 1)
```
Used for distance-weighted scoring and as a component of angle penalty.

### 3.2 `compute_angle_penalty(ccw_angle)` (`SeamPlacer.cpp:84`)
Combines Gaussian and sigmoid so concave corners are cheap and convex are costly:
```
compute_angle_penalty(x):
  return gauss(x, 0, 1, 3) + 1 / (2 + exp(-x))
```
- x < 0 (concave): low penalty
- x > 0 (convex): higher penalty, rises asymptotically

### 3.3 `calculate_polygon_angles_at_vertices(polygon, min_arm_length)` (`SeamPlacer.cpp:311`)
O(N) sliding-window chord-angle measurement:
```
calculate_polygon_angles_at_vertices(polygon, min_arm_length):
  result = vector of float[N]
  for each vertex i (circular):
    // walk backward until arm length >= min_arm_length
    prev = i - 1; arm_len = 0
    while arm_len < min_arm_length:
      arm_len += dist(polygon[prev], polygon[prev+1])
      prev--
    // walk forward similarly
    next = i + 1; arm_len = 0
    while arm_len < min_arm_length:
      arm_len += dist(polygon[next-1], polygon[next])
      next++
    // cross product z-component gives signed angle (CCW positive)
    v1 = polygon[i] - polygon[prev]
    v2 = polygon[next] - polygon[i]
    result[i] = atan2(cross2D(v1, v2), dot(v1, v2))
  return result
// Note: CCW polygon → positive angle = convex, negative = concave
```

---

## 4. Phase 0 — Mesh Occlusion Sampling

### 4.1 `raycast_visibility(samples, AABB_tree, negative_volumes_start_index)` (`SeamPlacer.cpp:200`)

For each sample point (run TBB parallel):
```
raycast_visibility(samples, tree, neg_vol_start):
  result = vector<float>[num_samples]
  for i in parallel over samples:
    score = 1.0   // start: assume fully visible (worst for seam)

    if mode == spAlignedBack:
      // bias toward back-facing surfaces (+Y normal = rear of object)
      front_adj = clamp((normal[i] · (0,-1,0) + 1.2) * 0.5, 0, 1)
      score += front_adj   // [HAZARD: score can exceed 1.0 for spAlignedBack]

    frame = Frame(normal[i])   // local coordinate system aligned to surface normal

    // 5×5 stratified hemisphere grid (sqr_rays_per_sample_point = 5)
    for row in 0..4:
      for col in 0..4:
        ray_dir = frame.transform(precomputed_hemisphere_sample[row][col])
        hit = tree.intersect_ray_first_hit(samples[i] + epsilon*normal[i], ray_dir)
        if hit and hit.face is front-facing:
          score -= 1.0 / 25.0   // ray escaped → less visible → better seam candidate

    if negative_volume_exists:
      // CSG membership parity check for negative volumes
      all_hits = tree.intersect_ray_all_hits(...)
      parity = 0
      for hit in reversed(all_hits):
        if hit.face_index >= neg_vol_start:
          parity = parity XOR sgn(dot(ray_dir, hit.normal))
      if parity != 0: score -= 1.0 / 25.0

    result[i] = score   // ~[0, 1]; 0 = fully occluded = best for seam

  return result
```
[HAZARD] Dead code in file: `sample_sphere_uniform()` and `sample_power_cosine_hemisphere()` are
defined but never called. Only `sample_hemisphere_uniform()` is used. (`SeamPlacer.cpp:~160`)

### 4.2 `compute_global_occlusion(global_model_info, po, ...)` (`SeamPlacer.cpp:785`)
```
compute_global_occlusion(info, po, throw_if_canceled, mode):
  // Merge all MODEL_PART volumes
  triangle_set = merge(all MODEL_PART volumes of po)

  // Merge NEGATIVE_VOLUME volumes separately
  negative_volumes_set = merge(all NEGATIVE_VOLUME volumes of po)
  neg_vol_start_index = triangle_set.indices.size()  // boundary between model and neg-vol triangles

  // Decimate to ≤ 16000 triangles each (fast_decimation_triangle_count_target)
  triangle_set = its_short_edge_collapse(triangle_set, 16000)
  negative_volumes_set = its_short_edge_collapse(negative_volumes_set, 16000)
  // [HAZARD: "its_short_edge_collpase" — method name has typo "collpase" in source]

  // Merge into single mesh; apply object transform
  triangle_set.merge(negative_volumes_set)
  triangle_set.apply_transform(obj_transform)

  // Sample 30000 uniform surface points (TBB parallel)
  info.mesh_samples = sample_its_uniform_parallel(30000, triangle_set)

  // Build KD-tree over sample positions
  info.mesh_samples_tree = KDTree3f(info.mesh_samples)

  // Compute Poisson search radius:
  //   density = num_samples / surface_area
  //   search_area s.t. P(≥4 samples in area) ≥ 0.9 under Poisson assumption
  //   search_area = 4 / (-ln(0.9) * density)
  //   radius = sqrt(search_area / π)
  info.mesh_samples_radius = sqrt(4 / (-ln(0.9) * density) / PI)

  // Build AABB tree for ray casting
  aabb_tree = AABBTree(triangle_set)

  // Cast 25 rays from each of 30000 samples = 750 000 total rays
  info.mesh_samples_visibility = raycast_visibility(info.mesh_samples, aabb_tree, neg_vol_start_index)
```

### 4.3 `GlobalModelInfo::calculate_point_visibility(point)` (`SeamPlacer.cpp:431`)
Transfers mesh-sample visibility to an arbitrary 3D point via weighted interpolation:
```
calculate_point_visibility(point, radius):
  neighbors = mesh_samples_tree.find_within_radius(point, radius)
  if neighbors is empty:
    return 1.0  // conservative: assume visible if no data

  total_weight = 0
  weighted_sum = 0
  for each neighbor n in neighbors:
    plane_dist  = abs(dot(point - n.pos, n.normal))
    eucl_dist   = (point - n.pos).norm()
    weight = (radius - plane_dist) + (radius - eucl_dist)
    // [HAZARD: weight can be negative when plane_dist > radius for off-plane points]
    weighted_sum += weight * mesh_samples_visibility[n.index]
    total_weight += weight

  if total_weight <= 0:
    return 1.0  // no valid data
  return weighted_sum / total_weight
```

---

## 5. Phase 1 — Candidate Gathering

### 5.1 `extract_perimeter_polygons(layer, global_model_info)` (`SeamPlacer.cpp:528`)
Walks the ExtrusionEntity tree to collect only external perimeter loops:
```
extract_perimeter_polygons(layer, info) -> vector<(polygon, region, is_external)>:
  result = []
  for region in layer.regions():
    for entity in region.perimeters.entities:
      loops = collect_loops(entity)  // handles ExtrusionLoop and ExtrusionEntityCollection
      for loop in loops:
        if loop.role == erExternalPerimeter:
          result.append((loop.polygon(), region, true))
  if result is empty:
    result.append(({0,0}, nullptr, false))  // dummy entry so alignment code has no null checks
  return result
```

### 5.2 `process_perimeter_polygon(polygon, z, region, global_model_info)` (`SeamPlacer.cpp:598`)
Builds SeamCandidate list for one perimeter loop:
```
process_perimeter_polygon(polygon, z, region, info) -> vector<SeamCandidate>:
  // Compute per-vertex angles
  angles = calculate_polygon_angles_at_vertices(polygon, nozzle_diameter)

  // Ensure CCW winding (CCW → positive angles at convex corners, preferred convention)
  if polygon is CW:
    negate all angles
    reverse polygon

  // Build candidate list with oversampling near enforcer zones
  candidates = []
  longest_enforced_run = 0, longest_start = -1
  for each vertex v_i:
    // Check enforcer/blocker
    type = Neutral
    if info.enforcers_tree.contains(v_i.xy): type = Enforced
    if info.blockers_tree.contains(v_i.xy):  type = Blocked

    candidates.append(SeamCandidate(v_i.xyz, perimeter, angles[i], type))

    // Oversample edge [v_i → v_{i+1}] at 0.2mm intervals if near enforcer boundary
    edge_len = dist(v_i, v_{i+1})
    if near_enforcer(v_i, v_{i+1}, info):
      for t in linspace(0, 1, ceil(edge_len / 0.2)):
        p = lerp(v_i, v_{i+1}, t)
        type_p = classify(p, info)
        candidates.append(SeamCandidate(p.xyz, perimeter, interp_angle, type_p))

  // Find longest continuous Enforced run → mark middle as central_enforcer
  // (central_enforcer = True for the single sharpest/central point in longest enforced segment)
  if any Enforced points:
    find run of consecutive Enforced candidates with max length
    candidates[middle_of_run].central_enforcer = true

  return candidates
```

### 5.3 `gather_seam_candidates(po, global_model_info)` (`SeamPlacer.cpp:1204`)
```
gather_seam_candidates(po, info):
  layers = m_seam_per_object[po].layers
  resize layers to po.layer_count()

  // TBB parallel_for over layers
  for layer_idx in parallel:
    polygons = extract_perimeter_polygons(po.layers[layer_idx], info)
    for (polygon, region, is_ext) in polygons:
      perimeter = new Perimeter(start_index=..., flow_width=region.flow_width)
      layers[layer_idx].perimeters.push_back(perimeter)
      candidates = process_perimeter_polygon(polygon, layer.slice_z, region, info)
      perimeter.end_index = candidates.size() (exclusive)
      layers[layer_idx].points.extend(candidates)

    // Build KD-tree over all candidates in this layer
    layers[layer_idx].points_tree = KDTree3f(layers[layer_idx].points)
```

---

## 6. Phase 2 — Visibility Transfer

### 6.1 `calculate_candidates_visibility(po, global_model_info)` (`SeamPlacer.cpp:1232`)
```
calculate_candidates_visibility(po, info):
  layers = m_seam_per_object[po].layers

  // TBB parallel_for over layers
  for layer_idx in parallel:
    for point in layers[layer_idx].points:
      point.visibility = info.calculate_point_visibility(point.position)
```

After this, `GlobalModelInfo` is destroyed (it holds the 30000-sample mesh data).

---

## 7. Phase 3 — Overhang and Embedding

### 7.1 `calculate_overhangs_and_layer_embedding(po)` (`SeamPlacer.cpp:1248`)

Uses `AABBTreeLines::LinesDistancer` (AABB line tree over layer outline segments) to measure
how far each candidate overhangs its support and whether it is hidden inside the merged print.

[MONOTONIC-Z ASSUMPTION] The TBB `blocked_range` over layers assumes layers are contiguous in
index: each TBB chunk initializes `prev_layer_distancer` from `r.begin() - 1`. If layers were
non-monotonic or there were gaps (e.g., raft-only prefix), the distancer would be stale.
In practice, `r.begin() > 0` only skips the check on the first layer of each chunk, which is
correct because physical layers are always contiguous. See Translation Note §12.3.

```
calculate_overhangs_and_layer_embedding(po):
  layers = m_seam_per_object[po].layers

  // TBB parallel_for over layers (each range block runs sequentially within itself)
  for range r in layers (parallel):
    prev_distancer = (r.begin > 0) ?
        PerimeterDistancer(po.layers[r.begin-1].lslices) : nullptr

    for layer_idx in r.begin .. r.end:
      regions_with_perimeter = count of regions with non-empty perimeters
      should_compute_embedding = (regions_with_perimeter > 1)  // multi-material layer

      current_distancer = PerimeterDistancer(po.layers[layer_idx].lslices)

      for point in layers[layer_idx].points:
        p2d = point.position.xy

        if prev_distancer != nullptr:
          _dist = prev_distancer.distance_from_lines<signed=true>(p2d)
          // overhang formula: how far past supported zone at 45° threshold
          point.overhang = max(0,
              _dist
              + 0.65 * point.perimeter.flow_width
              - tan(overhang_angle_threshold) * layer_height)
          point.unsupported_dist = _dist + 0.4 * point.perimeter.flow_width

        if should_compute_embedding:
          // embedded_distance < 0 means point is inside merged print region
          // (e.g. multi-material interface — ideal hidden seam location)
          point.embedded_distance =
              current_distancer.distance_from_lines<signed=true>(p2d)
              + 0.65 * point.perimeter.flow_width

      prev_distancer = current_distancer
```

---

## 8. Phase 4 — Seam Selection

### 8.1 `SeamComparator` (`SeamPlacer.cpp:921`)
Multi-criteria comparator. `is_first_better(a, b, ref_point)` returns true if `a` is a better
seam position than `b`:
```
is_first_better(a, b, ref_point):
  // Priority 1: central enforcer wins unconditionally (spAligned/spAlignedBack only)
  if a.central_enforcer and not b.central_enforcer: return true
  if b.central_enforcer and not a.central_enforcer: return false

  // Priority 2: Enforced > Neutral > Blocked
  if a.type != b.type: return a.type > b.type  // Enforced=2, Neutral=1, Blocked=0

  // Priority 3: avoid overhangs
  if a.overhang != b.overhang: return a.overhang < b.overhang

  // Priority 4: prefer embedded points (negative embedded_distance = hidden inside print)
  if a.embedded_distance < -0.5mm and b.embedded_distance >= -0.5mm: return true
  if b.embedded_distance < -0.5mm and a.embedded_distance >= -0.5mm: return false

  // Priority 5 (spRear only): maximize Y coordinate
  if mode == spRear: return a.position.y > b.position.y

  // Priority 6: weighted penalty sum (lower = better)
  angle_importance = (mode == spAligned || spAlignedBack || spRear) ?
                         angle_importance_aligned : angle_importance_nearest  // 0.6 or 1.0

  penalty_a = a.overhang
            + a.visibility
            + angle_importance * compute_angle_penalty(a.local_ccw_angle)
            + distance_penalty(a, ref_point)  // small penalty for distance from reference

  penalty_b = (same for b)
  return penalty_a < penalty_b
  // [HAZARD: mixed units — overhang in mm, visibility in [0,1], angle in dimensionless,
  //  distance in mm-derived. Empirically tuned; not dimensionally consistent.]
```

`is_first_not_much_worse(a, b)`: relaxed comparator for alignment tolerance:
```
is_first_not_much_worse(a, b):
  return (penalty_a - penalty_b) < seam_align_score_tolerance  // 0.3
```

### 8.2 `pick_seam_point(layer_points, perimeter_start, comparator)` (`SeamPlacer.cpp:1096`)
```
pick_seam_point(points, start, comparator):
  best_index = start
  i = start + 1
  while i < points[start].perimeter.end_index:
    if comparator.is_first_better(points[i], points[best_index]):
      best_index = i
    i++
  points[best_index].perimeter.seam_index = best_index
```

### 8.3 `pick_random_seam_point(layer_points, perimeter_start)` (`SeamPlacer.cpp:1135`)
Streaming length-weighted random selection (reservoir sampling):
```
pick_random_seam_point(points, start):
  // Deterministic hash seed from first vertex position
  seed = sin(points[start].position · (12.99, 78.23, 133.33)) * 43758.5
  rng = SeededRNG(seed)

  total_length = 0
  chosen = start
  i = start
  while i < points[start].perimeter.end_index:
    edge_len = dist(points[i].position, points[i+1].position)
    total_length += edge_len
    if rng.uniform(0, total_length) < edge_len:
      chosen = i
    i++

  points[start].perimeter.seam_index = chosen
  points[start].perimeter.finalized = true   // prevents alignment from touching this perimeter
```
[COUPLING] Setting `finalized=true` here blocks `align_seam_points()` from clustering this
perimeter. This is intentional — random seam mode should stay random across layers.

---

## 9. Phase 5 — B-Spline Alignment

Applies to `spAligned`, `spAlignedBack`, `spRear` only. Skipped for `spRandom` and `spNearest`.

### 9.1 `find_next_seam_in_layer(layers, projected_pos, layer_idx, max_dist, comparator)` (`SeamPlacer.cpp:1304`)
```
find_next_seam_in_layer(layers, proj_pos, layer_idx, max_dist, comparator):
  nearby = KD-tree radius query on layers[layer_idx].points_tree within max_dist of proj_pos

  if nearby is empty: return {}

  best_nearby = nearby[0]
  nearest     = nearby[0]

  for idx in nearby:
    p = layers[layer_idx].points[idx]
    if p.perimeter.finalized: continue   // skip already-aligned perimeters

    if comparator.is_first_better(p, best_nearby): best_nearby = idx
    if dist(p, proj_pos) < dist(nearest, proj_pos): nearest = idx

  if layers[layer_idx].points[nearest].perimeter.finalized: return {}

  // The seam previously chosen for this perimeter
  current_seam = layers[layer_idx].points[nearest.perimeter.seam_index]

  // Enforcer anchor has extended radius (3×) priority
  if current_seam.central_enforcer and dist(current_seam, proj_pos) < 3 * max_dist:
    return (layer_idx, nearest.perimeter.seam_index)

  // Accept nearest if not much worse than current seam choice
  if comparator.is_first_not_much_worse(nearest_point, current_seam):
    return (layer_idx, nearest_index)

  // Fallback: accept best nearby
  if comparator.is_first_not_much_worse(best_nearby_point, current_seam):
    return (layer_idx, best_nearby_index)

  return {}
```

### 9.2 `find_seam_string(po, start_seam, comparator)` (`SeamPlacer.cpp:1368`)
Grows a cluster of seam points up and down from a starting layer:
```
find_seam_string(po, start_seam, comparator):
  (layer_idx, seam_idx) = start_seam
  string = [start_seam]
  prev_point = start_seam
  step = +1 (upward first)
  next_layer = layer_idx + 1

  // max_distance = seam_align_tolerable_dist_factor * flow_width = 4 × flow_width
  while next_layer >= 0:
    if next_layer >= layer_count:
      // Hit top; reverse direction, search downward
      step = -1; prev_point = start_seam; next_layer = layer_idx - 1
      if next_layer < 0: break

    max_dist = 4 * layers[start_seam.first].points[start_seam.second].perimeter.flow_width
    proj_pos = prev_point.position with z = po.layer(next_layer).slice_z

    maybe_next = find_next_seam_in_layer(layers, proj_pos, next_layer, max_dist, comparator)

    if maybe_next has value:
      string.append(maybe_next)
      prev_point = string.back()
    else:
      if step == +1:
        // Gap going up; reverse
        step = -1; prev_point = start_seam; next_layer = layer_idx - 1
        if next_layer < 0: break
      else:
        break  // Gap going down too; stop

    next_layer += step

  return string
```

### 9.3 `align_seam_points(po, comparator)` (`SeamPlacer.cpp:1458`)
```
align_seam_points(po, comparator):
  layers = m_seam_per_object[po].layers

  // Collect one seam (perimeter.seam_index) per perimeter, across all layers
  seams = []
  for layer_idx in layers:
    i = 0
    while i < layers[layer_idx].points.size():
      seams.append((layer_idx, layers[layer_idx].points[i].perimeter.seam_index))
      i = layers[layer_idx].points[i].perimeter.end_index

  // Sort best-first so greedy cluster assignment captures good seams early
  stable_sort seams by comparator.is_first_better

  // Greedy alignment pass
  global_index = 0
  while global_index < seams.size():
    (layer_idx, seam_idx) = seams[global_index]
    global_index++

    if layers[layer_idx].points[seam_idx].perimeter.finalized:
      continue   // already handled by a previous cluster

    seam_string = find_seam_string(po, (layer_idx, seam_idx), comparator)

    // Try alternative starting points sampled from string at stride = 1 + len/20
    step_size = 1 + seam_string.size() / 20
    for alt_start in range(0, seam_string.size(), step_size):
      alt_string = find_seam_string(po, seam_string[alt_start], comparator)
      if alt_string.size() > seam_string.size():
        seam_string = alt_string

    if seam_string.size() < seam_align_minimum_string_seams (6):
      continue   // too short to be worth aligning

    sort seam_string by layer_idx (ascending)

    // [HAZARD] global_index-- here re-visits this seam after alternative string may have displaced it.
    // Necessary to avoid skipping a seam that gets re-queued by a longer alternative. (SeamPlacer.cpp:1544)
    global_index--

    // ---- B-spline fitting ----

    // Compute per-point angle-based weights and total signed path length
    total_length = 0
    last_pos = seam_string[0].position
    for index in seam_string:
      point = ...
      // Angle between consecutive string segments (3D)
      layer_angle = angle_3d(point.pos - prev.pos, next.pos - point.pos)  // 0 for endpoints

      angle_weight = 1.0 / (0.1 + compute_angle_penalty(point.local_ccw_angle))

      // "Curling" penalty: if string bends sharply, reduce contribution to total_length
      curling_influence = (layer_angle > 2 × |point.local_ccw_angle|) ? -0.8 : 1.0
      if point.type == Enforced:
        curling_influence = 1.0           // forced straight through enforcer
        angle_weight += 3.0              // enforcer gets weight bonus

      total_length += curling_influence × dist(last_pos, point.pos)
      last_pos = point.pos

      observations[index]       = point.position.xy
      observation_points[index] = point.position.z
      weights[index]            = angle_weight

    if mode == spRear:
      total_length *= 0.3   // spRear uses fewer segments → smoother path

    // Number of B-spline segments proportional to total path length
    number_of_segments = max(1, floor(max(0, total_length) / seam_align_mm_per_segment))
    // seam_align_mm_per_segment = 4 mm → one segment per 4 mm of height

    // Fit cubic B-spline: X,Y as observations, Z as parameterization variable
    curve = Geometry::fit_cubic_bspline(
        observations,       // Vec2f per seam point (XY)
        observation_points, // float per seam point (Z — parameterization)
        weights,
        number_of_segments)

    // ---- Apply alignment ----
    for index in seam_string:
      point = layers[index.layer].points[index.seam]

      // Snapping factor: t=1 → stay at original position (sharp corners)
      //                  t=0 → move to spline (smooth points)
      t = clamp(|point.local_ccw_angle| / sharp_angle_snapping_threshold)^3, 0, 1)
      if point.type == Enforced:
        t = max(0.4, t)   // enforced points keep at least 40% original position

      fitted_xy = curve.get_fitted_value(point.position.z)
      final_pos = t * point.position + (1 - t) * (fitted_xy, point.position.z)

      // Store in Perimeter (shared by all points of this loop)
      perimeter.seam_index         = index.seam
      perimeter.final_seam_position = final_pos
      perimeter.finalized           = true
```

[MONOTONIC-Z NOTE] `observation_points` use Z as the spline parameterization variable. This
requires Z to be monotonically increasing within the string — guaranteed because `find_seam_string`
walks layers sequentially and the string is sorted by `layer_idx` before fitting. If object has
multiple solids at different Z ranges on the same layer (unlikely), this assumption still holds
because each solid gets its own perimeter and its own string.

---

## 10. Phase 6 — G-code Export: `place_seam()`

### 10.1 `place_seam(layer, loop, last_pos, overhang)` (`SeamPlacer.cpp:1754`)
Called once per `ExtrusionLoop` during G-code export. Hot path.
```
place_seam(layer, loop, last_pos, overhang):
  po = layer.object()
  layer_index = layer.id() - po.slicing_parameters().raft_layers()
  unscaled_z  = layer.slice_z

  layer_perimeters = m_seam_per_object[po].layers[layer_index]

  // ---- Perimeter matching ----
  // Iterate loop points until two consecutive agree on the same closest perimeter
  // (handles Arachne T-junctions where one point may snap to wrong perimeter)
  closest_perimeter = nullptr
  current_point = loop.paths[0].polyline.points[0]
  points_count = total points in loop
  for i in 0 .. points_count:
    p2d = unscale(current_point)
    pt_idx = KD-tree find_closest(layer_perimeters.points_tree, (p2d, unscaled_z))
    if &layer_perimeters.points[pt_idx].perimeter != closest_perimeter:
      closest_perimeter = &layer_perimeters.points[pt_idx].perimeter
      current_point = next_loop_point(current_point)
    else:
      break   // two consecutive points agree → this is the right perimeter
  // closest_perimeter_point_index = pt_idx

  // [HAZARD] O(N × log M) worst case for very long loops. See Translation Notes §12.5.

  // ---- Seam position lookup ----
  perimeter = layer_perimeters.points[closest_perimeter_point_index].perimeter

  if perimeter.finalized:
    seam_position = perimeter.final_seam_position    // from B-spline alignment
    seam_index    = perimeter.seam_index
  else if mode == spNearest:
    seam_index    = pick_nearest_seam_point_index(layer_perimeters.points,
                        perimeter.start_index, unscale(last_pos))
    seam_position = layer_perimeters.points[seam_index].position
  else:
    seam_index    = perimeter.seam_index              // from pick_seam_point()
    seam_position = layer_perimeters.points[seam_index].position

  overhang = layer_perimeters.points[seam_index].unsupported_dist  // passed back to caller

  seam_point_scaled = Point::new_scale(seam_position.xy)

  // ---- Inner perimeter staggering ----
  if loop.role == erPerimeter:  // inner perimeter
    projected = loop.get_closest_path_and_point(seam_point_scaled)
    depth = unscale(seam_point_scaled - projected.foot_pt).norm()   // distance off-perimeter
    beta = cos(perimeter_point.local_ccw_angle / 2)

    if seam is near perimeter vertex AND angle is concave:
      // Push into concave corner: compute bisector direction and adjust depth
      dir_to_middle = 0.5 * (normalize(v - v_prev) + normalize(v - v_next))
      depth = 1.4142 * depth / beta
      final_pos = perimeter_point.pos.xy + depth * dir_to_middle
      projected = loop.get_closest_path_and_point(scale(final_pos))
    else:
      // Convex corner: scale depth to perpendicular component
      depth = depth * beta / 1.4142

    seam_point_scaled = projected.foot_pt

    if staggered_inner_seams:
      // Walk forward along loop by `depth` mm so inner seam offsets from outer seam
      depth = max(loop.path_width, depth)
      while depth > 0:
        next_pt = next_loop_point(projected)
        dist_to_next = dist(projected.foot_pt, next_pt.foot_pt)
        if dist_to_next > depth:
          final_pos = lerp(projected.foot_pt, next_pt.foot_pt, depth / dist_to_next)
          next_pt.foot_pt = final_pos
        depth -= dist_to_next
        projected = next_pt
      seam_point_scaled = projected.foot_pt
      // [HAZARD] Zero-length segment → infinite loop. See Translation Notes §12.6.

  // ---- Loop splitting ----
  // Prefer vertex split (no new point) if seam_point is within 1.5µm of existing vertex
  if not loop.split_at_vertex(seam_point_scaled, tolerance=1.5µm):
    loop.split_at(seam_point_scaled, insert=true)   // insert new point
```

---

## 11. Top-Level Orchestrator: `SeamPlacer::init()`

`SeamPlacer.cpp:1648`

```
SeamPlacer::init(print, throw_if_canceled):
  m_seam_per_object.clear()

  for po in print.objects():
    throw_if_canceled()
    mode = po.config().seam_position   // spAligned / spRandom / spNearest / spRear / etc.
    comparator = SeamComparator(mode)

    {  // LocalScope — GlobalModelInfo lives here, freed on exit
      info = GlobalModelInfo()
      gather_enforcers_blockers(info, po)         // load painted AABB trees
      throw_if_canceled()

      if mode in (spAligned, spNearest, spAlignedBack):
        compute_global_occlusion(info, po, ...)   // Phase 0: mesh sampling + raycasting

      throw_if_canceled()
      gather_seam_candidates(po, info)            // Phase 1: extract candidates
      throw_if_canceled()

      if mode in (spAligned, spNearest, spAlignedBack):
        calculate_candidates_visibility(po, info) // Phase 2: visibility transfer

    }  // GlobalModelInfo freed here (30000 samples + KD-tree released)

    throw_if_canceled()
    calculate_overhangs_and_layer_embedding(po)   // Phase 3: overhang + embedding

    throw_if_canceled()
    if mode != spNearest:                         // spNearest defers to place_seam()
      // Phase 4: per-perimeter seam selection (TBB parallel)
      for layer_idx in parallel:
        for each perimeter in layer:
          if mode == spRandom:
            pick_random_seam_point(...)            // sets finalized=true, blocks alignment
          else:
            pick_seam_point(...)                   // sets seam_index

    throw_if_canceled()
    if mode in (spAligned, spRear, spAlignedBack): // Phase 5: B-spline alignment
      align_seam_points(po, comparator)

    // m_seam_per_object[po] is now fully populated
```

**Mode × Phase matrix:**

| Phase                          | spAligned | spAlignedBack | spNearest | spRear | spRandom |
|-------------------------------|-----------|---------------|-----------|--------|----------|
| compute_global_occlusion       | YES       | YES           | YES       | NO     | NO       |
| calculate_candidates_visibility| YES       | YES           | YES       | NO     | NO       |
| pick_seam_point (init time)    | YES       | YES           | NO        | YES    | NO       |
| pick_random_seam_point         | NO        | NO            | NO        | NO     | YES      |
| align_seam_points              | YES       | YES           | NO        | YES    | NO       |
| pick_nearest (place_seam time) | NO        | NO            | YES       | NO     | NO       |

---

## 12. Translation Notes

### 12.1 `spAlignedBack` visibility score exceeds 1.0
**Source:** `SeamPlacer.cpp:~220`
For `spAlignedBack` mode, `raycast_visibility()` adds `front_adjustment` (up to 1.0) to the
starting score of 1.0, so the final score can be in [0, 2.0] instead of [0, 1.0]. Any downstream
code that clamps or normalizes visibility assuming [0, 1] range will produce incorrect results
for this mode. The `SeamComparator` penalty sum does not clamp, so the penalty for back-facing
points is inflated — which is intentional (makes them less desirable as a seam when they face
forward). But any external visualization or export that assumes visibility ∈ [0,1] will show
incorrect values.

### 12.2 Dead code: unused hemisphere samplers
**Source:** `SeamPlacer.cpp:~160`
Two functions are defined but never called:
- `sample_sphere_uniform()` — uniform sphere sampling (unused)
- `sample_power_cosine_hemisphere()` — cosine-weighted hemisphere (unused)
Only `sample_hemisphere_uniform()` is actually invoked in `raycast_visibility()`.
These can be safely removed without changing behavior.

### 12.3 Monotonic-Z assumption in `calculate_overhangs_and_layer_embedding()`
**Source:** `SeamPlacer.cpp:1253`
TBB `parallel_for` divides layers into contiguous ranges. Each range initializes
`prev_layer_distancer` from `r.begin() - 1` (the layer just before the range starts). This is
correct only because physical layers are always indexed 0, 1, 2, … N-1 without gaps. If the
layer vector were reordered or sparse, the distancer would reference the wrong layer and produce
incorrect overhang values silently.

The same sequential dependency (current layer's distancer becomes next layer's previous-layer
distancer via `.swap()`) means the inner loop within each TBB range is inherently sequential.
This limits parallelism: if TBB assigns one range per layer, there is no speedup from threading.

### 12.4 Mixed-unit penalty formula in `SeamComparator`
**Source:** `SeamPlacer.cpp:~960`
The weighted penalty sum combines:
- `overhang` in mm (unbounded positive real)
- `visibility` in [0, 1+] (dimensionless fraction)
- `angle_importance * compute_angle_penalty(angle)` in [0, ~4] (dimensionless)
- `distance_penalty` in roughly mm-equivalent (small)

These are summed directly despite being in different units. The `seam_align_score_tolerance`
constant (0.3) and `angle_importance_aligned` (0.6) have been empirically tuned to make the
contributions roughly comparable in typical prints. Changing nozzle diameter, layer height, or
object scale can shift the relative magnitudes and change seam quality.

### 12.5 `seam_align_mm_per_segment` type mismatch
**Source:** `SeamPlacer.hpp:213`
```cpp
static constexpr size_t seam_align_mm_per_segment = 4.0f;
```
Declared as `size_t` but initialized with `4.0f` (float literal). The compiler silently truncates
to `4`. The division `total_length / seam_align_mm_per_segment` is performed in float context
(because `total_length` is `float`), so the result is numerically correct. However, if the intent
changes to a non-integer value (e.g. `3.5f`), the silent truncation to `3` would cause more
spline segments than intended, without a compile-time diagnostic.

### 12.6 `its_short_edge_collpase` typo in method name
**Source:** `SeamPlacer.cpp:~805` (and the method definition in `TriangleMesh.cpp`)
The mesh decimation function name is spelled `its_short_edge_collpase` (note: "collpase" instead
of "collapse"). This is a stable API name in the codebase — renaming it would require updating
all call sites. Flag for future rename during a broader API cleanup, not in isolation.

### 12.7 `place_seam()` O(N) perimeter-matching loop
**Source:** `SeamPlacer.cpp:1788`
The loop that finds the closest perimeter for a given `ExtrusionLoop` iterates up to `points_count`
times (all vertices in the loop), each performing a KD-tree query at O(log M). For complex prints
with thousands of vertices per loop, this is the hot path during G-code export. In practice, the
loop terminates after 1–2 iterations for most simple loops. But for Arachne-generated T-junction
loops, it may need several iterations.

### 12.8 Staggered inner seams: infinite loop on zero-length segment
**Source:** `SeamPlacer.cpp:1860`
```cpp
while (depth > 0.0f) {
    ...
    float dist = (a - b).norm();
    depth -= dist;
```
If a loop contains two identical consecutive points (zero-length segment), `dist = 0` and
`depth` never decreases. The outer condition `depth > 0` never becomes false, producing an
infinite loop at G-code export time (hang). G-code resolution typically removes near-zero segments
during path processing, but the check happens at `split_at` time, after this loop. No defensive
guard exists in this loop itself.

### 12.9 `global_index--` subtle re-visit in `align_seam_points()`
**Source:** `SeamPlacer.cpp:1544`
After a longer alternative string is found and aligned, `global_index` is decremented by 1 so
the current seed seam is re-visited on the next iteration. This is intentional: the seed seam may
have been incorporated into the alternative string and finalized, so the next visit immediately
skips it via the `finalized` check. The decrement is needed to prevent the seam from being skipped
by the `global_index++` at the top of the loop. Removing or misunderstanding this decrement would
silently leave some seams unaligned without any error, since the `finalized` guard would prevent
double-alignment anyway.

### 12.10 `end_index` comment says "inclusive" but used as exclusive
**Source:** `SeamPlacer.hpp:73`
The `Perimeter::end_index` field has the comment `//inclusive!` but all iteration in the codebase
uses `index < end_index` (exclusive) or `perimeter.end_index - 1` when the last element is
needed. The comment is misleading and should be corrected. Any new code that reads "inclusive" and
writes `index <= end_index` will access one element past the end of the points vector.
