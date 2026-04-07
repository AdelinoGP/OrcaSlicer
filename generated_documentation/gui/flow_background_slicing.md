# Flow: Background Slicing

## Flow Purpose

This flow explains how workspace edits become background slicing work, how the worker synchronizes with the UI thread, and how completion/cancellation returns to the plater.

Primary evidence comes from `src/slic3r/GUI/Plater.cpp:L7677-L7727`, `src/slic3r/GUI/Plater.cpp:L7863-L8020`, `src/slic3r/GUI/Plater.cpp:L8067-L8105`, `src/slic3r/GUI/Plater.cpp:L9972-L10007`, `src/slic3r/GUI/Plater.cpp:L10174-L10347`, `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L175-L327`, and `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L314-L381`.

## Participating Source Files And Anchors

- `src/slic3r/GUI/Plater.cpp:L7677-L7727`
- `src/slic3r/GUI/Plater.cpp:L7863-L8020`
- `src/slic3r/GUI/Plater.cpp:L8067-L8105`
- `src/slic3r/GUI/Plater.cpp:L9972-L10007`
- `src/slic3r/GUI/Plater.cpp:L10174-L10347`
- `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L175-L327`
- `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L314-L381`
- `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L552-L667`

## Numbered Flow

1. A workspace/config change makes the current slice potentially stale.
   Marker: state change.
2. `Plater` debounces reslice requests with a timer rather than starting immediately.
   Marker: UI thread, timer.
3. When the timer or explicit action fires, `update_background_process()` reconciles the current model/config/preview invalidation state.
   Marker: UI thread, state reconciliation.
4. If a running slice must be interrupted, `Plater` marks restart intent and requests cancellation.
   Marker: UI thread -> worker cancellation.
5. Once conditions are valid and the worker is idle, `restart_background_process()` starts the next background pass.
   Marker: UI thread scheduling.
6. `BackgroundSlicingProcess` transitions through its worker state machine and performs slicing / G-code generation.
   Marker: worker thread.
7. If background work needs a UI-side operation, it schedules a `UITask` and blocks until the main thread finishes or cancels it.
   Marker: cross-thread synchronous handoff.
8. Progress and completion are queued back to the UI with wx events.
   Marker: worker thread -> UI thread event.
9. `Plater` updates progress UI, plate progress, and notifications.
   Marker: UI thread.
10. Completion handling resets export state, updates notifications, refreshes workspace state, and may immediately trigger a deferred restart.
    Marker: UI thread, state machine completion.

## Lifecycle Table

| Phase | Owner | Thread | Notes |
| --- | --- | --- | --- |
| request/debounce | `Plater` | UI | groups rapid edits |
| validate/reconcile | `Plater` | UI | checks whether slicing should start |
| slice/process | `BackgroundSlicingProcess` | worker | may call back into UI via `UITask` |
| progress reporting | worker -> `Plater` | cross-thread | queued event bridge |
| completion/cancel | `Plater` | UI | updates notifications and restart logic |

## Unity Implementation Notes

- Preserve the debounce/restart semantics; they are part of the user-visible responsiveness contract.
- Replace `UITask` blocking with awaited main-thread continuations.
- Model cancellation as explicit workflow states, including internal invalidation versus user cancel.
- Keep completion handling centralized so notifications, preview invalidation, and deferred restart stay consistent.
