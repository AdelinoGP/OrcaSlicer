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

- Reconciled runtime task state for T188 after `ralph tools task ready` returned no ready tasks: ensured `gui:T188`, started reused task `task-1773880086-cb14`, and annotated `calib_dlg.hpp` as the next atomic Phase 1 step.

- Current iteration focus: T557 annotate `src/slic3r/GUI/PublishDialog.cpp`.
- Chosen because it coordinates a publish-step progress UI, queued Plater events, and reentrant progress callbacks that are directly relevant to Unity coroutine/state-machine mapping.
- Next step: append durable annotations for modal lifecycle, event routing, cancel semantics, and wx event-loop reentry, then record handoff evidence and close the task.
- Completed T558 on `src/slic3r/GUI/PublishDialog.hpp` by adding class/method/member annotations for workflow state, event flow, UI-thread progress callbacks, and Unity migration guidance.
- Next recommended task is T559 on `src/slic3r/GUI/RammingChart.cpp`; it should likely need explicit render/update and OpenGL state annotations if it drives charts or live preview.

- Current iteration focus: T559 annotate `src/slic3r/GUI/RammingChart.cpp`.
- Chosen because it is a self-contained interactive chart with immediate-mode drawing, mouse-driven point editing, spline recalculation, and wx event dispatch that need concrete Unity mapping.
- Next step: add boundary-level annotations for rendering, drag handling, spline math, and event emission, then record handoff evidence and close the task.

- Completed T559 with chart-control annotations covering paint flow, drag/edit events, spline rebuilds, export helpers, and the wx event-table bridge.
- Next recommended task remains T560 annotate `src/slic3r/GUI/RammingChart.hpp`.

- Completed T560 with header-level annotations for the event bridge, constructor state, coordinate transforms, hit-testing, and derived-cache ownership.
- Next recommended task is T561 annotate `src/slic3r/GUI/RecenterDialog.cpp`.

- Completed T561 with annotations for modal intent, cached home icon state, owner-drawn paint flow, brittle locale-sensitive wrapping, Go Home/Close event semantics, and DPI relayout handling.
- Next recommended task is T562 annotate `src/slic3r/GUI/RecenterDialog.hpp`.

- Current iteration focus: T562 annotate `src/slic3r/GUI/RecenterDialog.hpp`.
- Chosen because the header is the declaration boundary for an already-annotated custom-painted modal dialog, so it can carry durable ownership/state/event/Unity notes without expanding scope.
- Next step: finish the header annotations, record completion evidence in handoff, and close the task as one atomic annotation step.

- Completed T562 with class/member/event annotations and a Unity mapping note that keeps the dialog's modal confirm/close semantics explicit.
- Next recommended task is T563 annotate `src/slic3r/GUI/ReleaseNote.cpp`.

- Completed T563 on `src/slic3r/GUI/ReleaseNote.cpp`; the file is now annotated as a bundle of modal update, confirmation, print-error, and printer-connection flows.
- Key migration takeaway: the version/release-note path uses embedded web content and script injection, while the IP setup path uses a worker thread plus UI-thread event reentry.
- Next recommended task is T564 annotate `src/slic3r/GUI/ReleaseNote.hpp`.

- Completed T564 on `src/slic3r/GUI/ReleaseNote.hpp` with class-level annotations for seven dialogs plus the shared event declarations.
- Key migration takeaway: the header mixes webview-backed release notes, dynamic confirmation dialogs, and an async IP onboarding wizard, so Unity needs reusable modal controllers plus a real background-task bridge.
- Next recommended task is T565 annotate `src/slic3r/GUI/RemovableDriveManager.cpp`.
