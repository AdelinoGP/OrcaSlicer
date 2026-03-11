#pragma once

// [INTENT] Provide deterministic baseline defaults for unit tests that exercise PrintConfig initialization paths.
// [COUPLING] Tests and helper fixtures may compile these macros directly; changing values can invalidate golden outputs.
// [HAZARD] Macro constants are unscoped and typeless, so they can silently bypass type safety compared to constexpr values.

#define INITIAL_LAYER_HEIGHT 0.2
#define INITIAL_RAFT_LAYERS 0
#define INITIAL_REDUCE_CROSSING_WALL false
