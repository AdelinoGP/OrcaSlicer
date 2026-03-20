# Ralph Scratchpad — OrcaSlicer GUI Analysis

## Current Understanding (2026-03-20)
- Objective: Phase 1 — GUI File-by-File Annotation for Unity Port Preparation.
- Target: `src/slic3r/GUI/3DScene.hpp` (Task ID: `task-1773880086-41c7`, Key: `gui:T204`).
- Priority: 2 (Viewport / OpenGL rendering and input).
- File location: Found at `src/slic3r/GUI/3DScene.hpp` via `glob`.

## Plan
1. Mark task `task-1773880086-41c7` as `in_progress`.
2. Read `src/slic3r/GUI/3DScene.hpp` to understand its logic, state, and event flow.
3. Annotate with `[INTENT]`, `[STATE]`, `[EVENT]`, `[THREAD]`, `[OPENGL]`, `[UNITY]`, and `[PORTING_HAZARD]`.
4. Verify the annotations meet the standard (all required categories).
5. Append completion evidence to `handoff.md`.
6. Commit the annotated file.
7. Close the task.
