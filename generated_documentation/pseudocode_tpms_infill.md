# Pseudocode: TPMS Infill Cross-Section Extraction (T4042)

**Source files:**
- `src/libslic3r/Fill/FillTpmsD.cpp` - analytic Schwartz-D cross-section generation
- `src/libslic3r/Fill/FillTpmsD.hpp` - density and tolerance constants
- `src/libslic3r/Fill/FillTpmsFK.cpp` - sampled Fischer-Koch-S field extraction
- `src/libslic3r/Fill/FillTpmsFK.hpp` - FK pattern contract and rotation behavior

**Purpose:** Describe how OrcaSlicer converts two 3D TPMS implicit surfaces into 2D per-layer infill polylines.

---

## 1. Shared Wrapper

1. `function build_tpms_infill(pattern_type, expolygon, params, layer_z, angle, spacing)` - `O(pattern_core + clip + connect)`
2. Compute `infill_angle = angle - 45 degrees`. - `O(1)`
3. Rotate `expolygon` by `-infill_angle` so the TPMS pattern stays aligned to printer-preferred axes. - `O(V)`
4. If `pattern_type == TPMS_D`, call `generate_tpms_d_curves(...)`. - `O(pattern_core_D)`
5. If `pattern_type == TPMS_FK`, call `generate_tpms_fk_curves(...)`. - `O(pattern_core_FK)`
6. If multiline infill is requested, offset each generated curve into `params.multiline` parallel passes. - `O(P)`
7. Clip all generated curves against `expolygon`. - `O((P + V) log V)` typical
8. Delete fragments shorter than `0.8 * spacing`. - `O(P)`
9. Connect or chain remaining curves according to the pattern-specific rule. - `O(P log P)` typical
10. Rotate newly-added output curves back by `+infill_angle`. - `O(P)`
11. Return the printable polylines.

---

## 2. TPMS D: Analytic Schwartz-D Waves

### 2.1 Density Mapping

1. `function generate_tpms_d_curves(expolygon, params, layer_z, spacing)` - `O(R x S + clip)`
2. Compute `density_adjusted = max(0, params.density * 2.1 / params.multiline)`. - `O(1)`
3. Compute `scale_factor = scale(spacing) / density_adjusted`. - `O(1)`
4. Align the rotated bounding box origin to a `2pi * distance` grid so adjacent layers reuse a stable tiling origin. - `O(1)`
5. Estimate how many TPMS periods are needed to cover the bounding box in X and Y. - `O(1)`

### 2.2 Convert The 3D Surface Into A 2D Wave Equation

6. Start from the implicit surface:

```text
sin(x)sin(y)sin(z) - cos(x)cos(y)cos(z) = 0
```

7. Rewrite it as:

```text
a * cos(u) = b * cos(v)
where
  u = x - y
  v = x + y
  a = sin(z) - cos(z)
  b = sin(z) + cos(z)
```

8. If `abs(a) > abs(b)`, swap the meanings of `u` and `v` so the later `acos()` input stays within `[-1, 1]`. - `O(1)`
9. Define one explicit branch of the layer curve as `v(u) = acos((a / b) * cos(u))`. - `O(1)`

### 2.3 Adaptive Sampling Of One Period

10. Seed one `2pi` period with 16 uniformly-spaced sample points. - `O(16)`
11. Repeat until no new points are inserted: - `O(S)` per pass
12. For each adjacent sample pair `(u1, v1)` and `(u2, v2)`: - `O(S)`
13. Evaluate the true midpoint `middle_v = v((u1 + u2) / 2)`. - `O(1)`
14. Evaluate the linear midpoint estimate `(v1 + v2) / 2`. - `O(1)`
15. If the deviation exceeds `min(spacing / 2, PatternTolerance) / scale_factor`, insert the true midpoint between those samples. - `O(S)` worst-case due to vector insertion
16. Otherwise keep the segment as-is. - `O(1)`

Result: one curvature-adapted sample chain for a single TPMS period.

### 2.4 Tile The Wave Across The Layer

17. Copy the refined period repeatedly by adding `2pi` to `u` until the sampled period spans the whole bbox width. - `O(P x S)`
18. For every `v_shift` band needed to cover the bbox height: - `O(R)`
19. Emit two branches: one with `+v`, one with `-v`. - `O(1)`
20. Back-transform every `(u, v)` sample into `(x, y)` using:

```text
x = (u + v) / 2
y = (v - u) / 2
```

21. If a `u/v` swap happened earlier, flip the sign needed to restore the original coordinate frame. - `O(1)`
22. Scale the points back into Slic3r coordinates and append them as polylines. - `O(R x P x S)`
23. Return all tiled branches for clipping and connection.

---

## 3. TPMS FK: Sampled Fischer-Koch-S Field

### 3.1 Density Mapping And Field Setup

1. `function generate_tpms_fk_curves(expolygon, params, layer_z, spacing)` - `O(G + C + clip)`
2. Compute `density_factor = min(0.9, params.density)`. - `O(1)`
3. Compute `period = 4.18 * spacing * params.multiline / density_factor`. - `O(1)`
4. Expand the rotated bbox by `(params.multiline + 1) * spacing` so contours near the border are still complete before clipping. - `O(1)`
5. Build a scalar field object with:
   - raster pixel size `0.004 mm`
   - marching grid size `0.40 mm`
   - field frequency `2pi / period`
   - iso-value `0`

### 3.2 Evaluate The Implicit Field

6. Define the FK scalar function:

```text
f(x, y, z) = cos(2x)sin(y)cos(z)
           + cos(2y)sin(z)cos(x)
           + cos(2z)sin(x)cos(y)
```

7. For each raster point queried by Marching Squares, convert raster coordinates back to world coordinates and evaluate `f(x, y, layer_z)`. - `O(1)` per sample

### 3.3 Extract The 2D Cross-Section With Marching Squares

8. Run Marching Squares over the scalar field with TBB parallelism. - `O(G)`
9. Treat every zero-isocontour ring returned by Marching Squares as one FK loop. - `O(C)`
10. Convert every raster ring vertex back into scaled Slic3r coordinates. - `O(C)`
11. Append the first point again so each contour becomes a closed polyline. - `O(number_of_rings)`
12. Simplify each polyline with sparse-infill tolerance (`~0.1 mm`). - `O(C log C)` typical
13. Return all contours for multiline widening, clipping, and connection.

### 3.4 Why FK Uses Direct Connection Instead Of Chain-Or-Connect

14. Use `connect_infill`, not `chain_or_connect_infill`. - `O(P log P)` typical
15. Reason: FK can produce nested internal islands, and the generic chaining heuristic may route through the wrong loop ordering or introduce crossings.

---

## 4. Algorithm Selection Guidance

1. Use the TPMS D path when the implicit surface can be reduced to an explicit wave family at fixed `z`. - `O(1)`
2. Use the TPMS FK path when the layer cross-section has more complex topology and needs numeric contour extraction. - `O(1)`
3. Both rely on the same downstream clipping and infill-connection pipeline, so the translation boundary is the cross-section generator, not the printer-path finalization.

---

## Translation Notes

- `TN 1` TPMS D is not "just sample the implicit field." Its core contract is the `u = x - y`, `v = x + y` transform plus midpoint-refinement on an analytic branch. Replacing it with naive raster sampling will change density, continuity, and inter-layer phase behavior.
- `TN 2` TPMS FK is not "just Marching Squares." The print output depends on OrcaSlicer's exact sampling scale choices: `0.40 mm` marching cells, `0.004 mm` raster precision, bbox expansion before clipping, and the hard 90% density cap.
- `TN 3` Both patterns depend on stable phase anchoring. The `-45 degree` rotation and grid/bbox alignment rules are what keep the TPMS pattern visually continuous across layers even when the polygon bbox shifts.
