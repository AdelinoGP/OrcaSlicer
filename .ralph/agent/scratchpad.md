# Scratchpad

- Selected T574 (`src/slic3r/GUI/Search.hpp`) as the next ready Phase 1 task.
- The file combines two popup search controllers: preset options and object search.
- Main Unity migration note: model the current wxPopupWindow + custom-painted rows as a floating controller with a persistent filtered list view and explicit dismiss/focus handling.
- Search.hpp now carries declaration-level annotations for the shared popup boundary, option/result DTOs, manual row painting, and both dialog controllers.

- Switched to T575 (`src/slic3r/GUI/Selection.cpp`) for the next atomic annotation pass.
- Selection.cpp is the mutable GLCanvas3D selection model: it snapshots undo state, mutates GLVolume/Model state, caches transforms/bounds, renders GL overlays, and fans edits out to sibling instances/volumes.
- Plan: add a small set of high-value [INTENT]/[STATE]/[EVENT]/[OPENGL]/[UNITY]/[PORTING_HAZARD] comments around initialization, selection mutation, clipboard, rendering, and synchronization, then verify and close the task.

- Started T576 (`src/slic3r/GUI/Selection.hpp`) after the runtime queue advanced; the header now captures selection ownership, drag-cache lifetime, overlay rendering, clipboard payload semantics, and Unity migration boundaries.

- Switched to T577 (`src/slic3r/GUI/SelectMachine.cpp`) for the next atomic annotation pass.
- SelectMachine.cpp is the send-print modal: it coordinates printer selection, AMS/extruder mapping, validation/status flow, and a custom thumbnail compositor.
- Plan: keep a small set of high-value [INTENT]/[STATE]/[EVENT]/[THREAD]/[OPENGL]/[UNITY] comments around lifecycle, event wiring, selection resets, preview composition, and widget boundaries, then close the task.
