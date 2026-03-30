# Plan - Phase 1: GUI Annotation

Current objective: Annotate GUI source files for Unity port prep.

## Status Review
- T801, T800, T816 completed.
- T815 (UndoRedo.cpp) verified as completed.
- T758 (Flashforge.hpp) completed.
- T757 (Flashforge.cpp) completed.
- Registry updated for T757, T758, T764, T800, T801, T815, T816.

## Next Task
- **T763 annotate: src/slic3r/Utils/Http.cpp**
- Priority: High
- Rationale: Follows the recommended sequence in handoff.md.

## Steps
1. Read `src/slic3r/Utils/Http.cpp`.
2. Annotate implementation with `[STATE]`, `[UNITY]`, `[THREAD]`, etc.
3. Update handoff.md.
4. Update ralph-tasks.md.
5. Commit changes.

## Completed T763
- Annotated `src/slic3r/Utils/Http.cpp` with [INTENT], [STATE], [THREAD], [UNITY], and [PORTING_HAZARD].
- Noted P1 hazard for synchronous HTTP requests.
- Updated registry and handoff.
- Committed changes.

## Next Step
- T765 annotate: src/slic3r/Utils/ICloudServiceAgent.hpp

## Completed T765
- Annotated `src/slic3r/Utils/ICloudServiceAgent.hpp` with ~60 tags.
- Documented P1 Mega-Facade hazard and main-thread marshaling dependency.
- Proposed Unity split into specialized services.
- Updated handoff.
- Committed changes.

## Next Step
- T766 annotate: src/slic3r/Utils/InstanceID.cpp
