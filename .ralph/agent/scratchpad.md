# Ralph Scratchpad — OrcaSlicer GUI Analysis Agent

## Current Status
- Phase 1: Annotation Loop in progress.
- Task T101 (GUI_App.cpp) is fully annotated (parts 1-4) and committed (d158c0a5f0).
- Task T102 (GUI_App.hpp) and T103 (GUI_Init.cpp/hpp or PlaterWorker.hpp depending on mapping) are documented as DONE and committed.
- Git log confirms `d158c0a5f0` (GUI_App.cpp 6000-8070) is complete.

## Next Task
- `T110: annotate src/slic3r/GUI/MainFrame.hpp`.
- This is part of Group 2: Window Management.

## Analysis Notes (T110)
- `MainFrame` is the primary application window.
- I'll look for:
  - wxFrame inheritance and Unity equivalent (MonoBehaviour on a Canvas or similar).
  - Member variables holding UI state (side panels, tab managers).
  - Event handlers for window events (close, resize, menu events).
  - Initialization sequence for child widgets.
- Key Unity mapping: `MainFrame` likely maps to a "MainWindowController" MonoBehaviour managing a set of UI Panels.
