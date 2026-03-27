# Scratchpad

- Iteration focus: T721 annotate `src/slic3r/GUI/wxMediaCtrl2.cpp`.
- Chosen because the file is a self-contained media wrapper with clear Unity port implications: platform backends, error signaling, and asynchronous event flow.
- Next step: insert durable `[INTENT]`, `[STATE]`, `[EVENT]`, `[THREAD]`, `[UNITY]`, and `[PORTING_HAZARD]` annotations in the cpp, then record handoff evidence and close the task.
- Completed T721 with cross-platform media wrapper annotations; the Windows registry/codec dependency is the main porting hazard.
- T187 completed on `src/slic3r/GUI/calib_dlg.cpp`; the file is a cluster of modal calibration forms with shared preset-driven helpers, direct `Plater` dispatch, and firmware-sensitive axis hiding/mirroring that a Unity port should model through controller-backed forms.
- Iteration focus: T555 annotate `src/slic3r/GUI/ProjectDirtyStateManager.hpp`.
- Chosen because the header defines the dirty-state baseline used by the already-annotated cpp and is small enough for a precise atomic update.
- Next step: add class/member annotations for snapshot-based dirty comparisons, then record completion evidence and move to the next Phase 1 task.

- Completed T556 on Project.hpp with class/state/event/thread annotations; next focus is the publish dialog path.
