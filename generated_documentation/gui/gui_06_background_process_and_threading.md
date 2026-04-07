# Background Process And Threading

## Purpose

This document explains how background work interacts with the GUI, especially around slicing, uploads, worker queues, and main-thread marshaling. This is one of the two highest-priority Phase 2 documents because many GUI behaviors depend on implicit wx event-loop assumptions that will not carry over to Unity automatically.

Primary evidence comes from `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L175-L327`, `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L193-L267`, `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L314-L381`, `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L517-L579`, `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L593-L710`, `src/slic3r/GUI/Plater.cpp:L182-L191`, `src/slic3r/GUI/Plater.cpp:L4995-L5002`, `src/slic3r/GUI/Plater.cpp:L7677-L7727`, `src/slic3r/GUI/Plater.cpp:L7863-L8020`, `src/slic3r/GUI/Plater.cpp:L8067-L8105`, `src/slic3r/GUI/Plater.cpp:L9972-L10007`, `src/slic3r/GUI/Plater.cpp:L10174-L10347`, `src/slic3r/GUI/Jobs/Worker.hpp:L10-L15`, `src/slic3r/GUI/Jobs/Worker.hpp:L32-L43`, `src/slic3r/GUI/Jobs/BoostThreadWorker.hpp:L16-L27`, `src/slic3r/GUI/Jobs/BoostThreadWorker.hpp:L80-L117`, `src/slic3r/GUI/Jobs/BoostThreadWorker.cpp:L41-L83`, `src/slic3r/GUI/Jobs/BoostThreadWorker.cpp:L132-L181`, `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L1-L12`, `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L63-L189`, `src/slic3r/GUI/NotificationManager.cpp:L2417-L2555`, `src/slic3r/Utils/PrintHost.cpp:L148-L279`, `src/slic3r/GUI/PrintHostDialogs.hpp:L116-L133`, and `src/slic3r/GUI/PrintHostDialogs.cpp:L277-L303`.

## Thread And Process Inventory

| Worker / async subsystem | Current role | Main-thread handoff pattern | Port note |
| --- | --- | --- | --- |
| `BackgroundSlicingProcess` | slicing, export, upload preparation | wx queued events + synchronous UI task bridge | highest-value async workflow to redesign |
| `Worker` / `BoostThreadWorker` | generic queued jobs | output queue drained on UI thread | replace with one async runtime abstraction |
| `PlaterWorker` | plater-specific worker wrapper | `wxWakeUpIdle()` + idle/paint draining | explicit Unity dispatcher needed |
| Print host queue | upload/send jobs | custom queued events to dialog | should become service-driven |
| Downloader | background file retrieval | callbacks / screen integration | use cancellable tasks |
| Printer filesystem / discovery | remote data refresh | conditional post-to-main-thread | isolate from view ownership |
| App-level network checks / sync | global background threads | singleton-managed state flags | fold into scoped services |

## Background Slicing And Job Orchestration

### Slicing Worker State Machine

`BackgroundSlicingProcess` defines an explicit worker state machine from `STATE_INITIAL` through `STATE_EXITED` (`src/slic3r/GUI/BackgroundSlicingProcess.hpp:L175-L190`). This already encodes an important design truth: slicing is not fire-and-forget. The UI expects to observe and sometimes synchronously coordinate with worker state.

### Plater As Orchestrator

`Plater` defines the event contract for scheduling, updating, and completing slicing/export work (`src/slic3r/GUI/Plater.cpp:L182-L191`). It binds the background process and related handlers into the workspace event mesh (`src/slic3r/GUI/Plater.cpp:L4995-L5002`).

Two key behaviors matter for a Unity rewrite:

1. reslice requests are debounced by a timer (`src/slic3r/GUI/Plater.cpp:L7677-L7686`)
2. auto-reslice may cancel current work, set a restart condition, and retrigger after the completion path finishes (`src/slic3r/GUI/Plater.cpp:L7688-L7727`)

`update_background_process()` is the reconciliation point where model/config changes, preview invalidation, validation, and synthetic cancellation handling all come together (`src/slic3r/GUI/Plater.cpp:L7863-L8020`). `restart_background_process()` then gates actual restart on validity and worker-idle conditions (`src/slic3r/GUI/Plater.cpp:L8067-L8105`).

### Worker-Side Processing

The background process handles slicing, G-code generation, export, and upload preparation in one workflow family (`src/slic3r/GUI/BackgroundSlicingProcess.cpp:L193-L267`). This means the current code couples geometric processing, file generation, and downstream send workflow more tightly than a clean service split would.

## Main-Thread Marshaling Patterns

### Queued UI Notification

Completion/progress paths use queued wx events to tell `Plater` that slicing finished or changed state (`src/slic3r/GUI/BackgroundSlicingProcess.cpp:L314-L381`).

### Synchronous UI Task From Worker

The most important marshaling pattern is `execute_ui_task()`, where the background thread schedules a UI operation, blocks on a condition variable, and resumes only after the UI marks the task finished or canceled (`src/slic3r/GUI/BackgroundSlicingProcess.hpp:L277-L320`, `src/slic3r/GUI/BackgroundSlicingProcess.cpp:L625-L667`).

This is a major Unity porting hazard because it means some background work depends on the main thread not just for completion, but for mid-flight participation.

### Generic Worker Queue Pattern

`Worker` defines a main-thread `process_events()` contract and warns that waits can block the UI (`src/slic3r/GUI/Jobs/Worker.hpp:L32-L43`). `BoostThreadWorker` then implements queued status/finalize/main-thread-call messages drained by the UI thread (`src/slic3r/GUI/Jobs/BoostThreadWorker.cpp:L41-L83`, `src/slic3r/GUI/Jobs/BoostThreadWorker.cpp:L132-L181`).

### wx Idle/Paint Wakeup Bridge

`PlaterWorker` is especially revealing: it forces `wxWakeUpIdle()` from progress and main-thread-call callbacks, then drains worker completions from `wxEVT_IDLE` and `wxEVT_PAINT` (`src/slic3r/GUI/Jobs/PlaterWorker.hpp:L1-L12`, `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L63-L189`).

That is a direct dependency on wx's event loop behavior.

## UI Thread Safety Constraints

Several components state the rule explicitly:

- `Worker::process_events()` is main-thread-only (`src/slic3r/GUI/Jobs/Worker.hpp:L32-L43`)
- `PlaterJob::finalize()` is expected to run on the main thread (`src/slic3r/GUI/Jobs/PlaterWorker.hpp:L114-L140`)
- progress and status UI are UI-thread-owned
- dialogs and views use guards because background callbacks may outlive them (`src/slic3r/GUI/PrintHostDialogs.hpp:L116-L133`)

The code also sometimes checks whether it is already on the main thread and posts if not, instead of assuming any callback is safe for UI work. That is the correct conceptual boundary to preserve.

## Async Cancellation And Progress Behavior

### Background Slicing

Cancellation is nuanced, not binary:

- user stop waits for the worker to reach `STATE_CANCELED`, then resets to idle (`src/slic3r/GUI/BackgroundSlicingProcess.cpp:L552-L579`)
- internal cancellation during `Print::apply()` suppresses normal UI completion handling and carefully unwinds worker state (`src/slic3r/GUI/BackgroundSlicingProcess.cpp:L593-L623`)
- completion reporting distinguishes user cancel, internal cancel, and success (`src/slic3r/GUI/BackgroundSlicingProcess.cpp:L352-L371`)

Progress then fans into workspace and notification UIs (`src/slic3r/GUI/Plater.cpp:L9972-L10007`, `src/slic3r/GUI/NotificationManager.cpp:L2417-L2555`). Final completion handling resets export state, updates notifications, refreshes scene state, and restarts deferred reslices (`src/slic3r/GUI/Plater.cpp:L10174-L10347`).

### Print Host Uploads

Upload jobs also expose progress, cancel, and error events through queued UI notifications (`src/slic3r/Utils/PrintHost.cpp:L148-L279`, `src/slic3r/GUI/PrintHostDialogs.cpp:L277-L303`). The print-host dialog uses an `EventGuard` to avoid delivery to dead UI owners (`src/slic3r/GUI/PrintHostDialogs.hpp:L116-L133`).

## Unity Equivalents

### Recommended Replacement Stack

| Current mechanism | Unity equivalent |
| --- | --- |
| `boost::thread` workers | `Task.Run`, dedicated async services, or C# job wrappers |
| queued wx events | main-thread dispatcher / `SynchronizationContext` |
| synchronous `execute_ui_task()` | awaited main-thread continuations with explicit task boundaries |
| `wxWakeUpIdle()` pump | frame-driven dispatcher flush |
| ad hoc cancellation booleans/state machine | `CancellationToken` + explicit workflow state model |
| direct widget progress mutation | view-model progress state + observers |

### Preferred Architecture

```text
UI controller
  -> async workflow service
    -> background task / worker
      -> progress channel
      -> completion channel
      -> cancellation token
  -> main-thread dispatcher applies UI/domain changes
```

### Key Rules For The Port

1. One explicit main-thread dispatcher for all GUI-affecting async completions.
2. No UI updates from background threads.
3. Separate slicing, export, and upload into composable workflow stages.
4. Keep cancellation state explicit and distinguish user cancel from internal invalidation.
5. Do not rely on rendering or idle callbacks to flush job completions.

## Unresolved Ambiguities

- Some long-lived background subsystems outside slicing/uploads are only partially surveyed here; a full async service inventory may still be needed during implementation.
- The source does not by itself settle whether slicing should remain native and be bridged, or be rehosted differently for Unity.
