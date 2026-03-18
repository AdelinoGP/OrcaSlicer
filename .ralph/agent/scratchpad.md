# Ralph Scratchpad — OrcaSlicer GUI Analysis Agent

## Current Status
- Phase 1: Annotation Loop in progress.
- MainFrame.cpp (4572 lines) is FULLY documented and committed (8c89439e87, acc2a7ed0a).
- Plater.hpp (T120) is fully documented and committed (617436cdc7).
- Cumulative annotated files: 8/719. (Note: GUI_App.cpp, MainFrame.cpp, and Plater.hpp are counted).

## Next Task
- `T121-part1: annotate src/slic3r/GUI/Plater.cpp (1-2000)`.
- `Plater.cpp` is 18,178 lines. Splitting into ~9 parts (2000 lines each).

## Analysis Notes (T121: Plater.cpp)
- `Plater.cpp` is the implementation of the core 3D workspace.
- Expect heavy wxWidgets event binding (`Bind()`), OpenGL context management, and model manipulation logic.
- Key Unity Mapping:
    - `Plater` implementation -> `SceneController` logic, `InputManager` for 3D interactions.
    - Model selection/manipulation -> Unity's `Raycast` and `Selection` system.
- Porting Hazard: High complexity in event propagation between the 3D scene and the sidebar.

## Plan
1. Update `ralph-tasks.md` to mark T120 DONE and split T121 into parts.
2. Inject annotations into `src/slic3r/GUI/Plater.cpp` (lines 1-2000).
3. Update `agent_journal_gui.md` with T121-part1 completion evidence.
4. Commit and close T121-part1.
