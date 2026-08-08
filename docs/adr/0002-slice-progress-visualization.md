# Slice progress visualization on the plater

While pnp_cli slices, the plater colors model volumes by per-global-plate-layer status (pending / in-progress / complete / degraded) using a dedicated Z-LUT shader cloned from `variable_layer_height`: a 1D status texture sampled by plate-absolute world Z, multiplied by the standard two-light intensity. `PnpProgressParser` gains a status array fed by `layer_start`/`layer_complete` events; updates are coalesced to at most one texture re-upload per frame.

## Considered Options

- **Vertex-color bake** — rejected: O(vertices) CPU work per update and per-pixel Z resolution lost on tall triangles.
- **Reusing `variable_layer_height` as-is** — rejected: conflates two unrelated features and inherits stripe/blend visuals not wanted here.
- **Bool (done/not-done) state model** — rejected: pnp emits layers out of order across the parallel tier, and the whole point of the feature is showing which layers are in flight.
- **Per-event UI posts** — rejected: a large slice with parallel layers can flood the wx event queue; frame-rate coalescing caps the cost.

## Consequences

- **Z mapping is a uniform-height estimate.** The pnp stream carries no per-layer z data, so layer i is mapped to z ∈ [i·H/N, (i+1)·H/N). With adaptive layer heights, stripe boundaries can drift from true layer boundaries. Fixing this requires a pnp schema change (per-layer z on `layer_start`); if that ever ships, only the LUT construction changes.
- **The visualization does not appear until `phase_start(per_layer)`** delivers the real `layer_count` (falling back to the estimated layer count when the optional field is absent). Validation/prepass phases show status-bar text only. This was chosen over showing an estimate that visibly re-scales mid-slice.
- **In-progress is a static accent color, not animated** — no time uniform, so the canvas only repaints on actual status changes.
- **On failure or cancel the coloring freezes**: completed layers stay green, degraded layers stay amber, everything else turns red, showing how far the slice got. The freeze survives the completion handler's scene reload (the canvas re-applies it from the retained snapshot) and is cleared by the next slice start or a later scene reload. On success the normal shading returns; nothing persists into the Preview.
- **Only the plate being sliced is colored.** The 3D view shows every plate at once, so the canvas splits the opaque pass: volumes on the slicing plate (matched by `PartPlate::contain_instance`) get the status shader, all others keep normal shading. Slice-all therefore animates only the currently-slicing plate.
- The feature depends on `layer_start`/`layer_complete` events, which are mandatory in progress schema 1.x but unchecked beyond the major version gate — a future pnp major bump could silently remove them.
