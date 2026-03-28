# Session Handoff

_Generated: 2026-03-26 08:10:14 UTC_

## Git Context

- **Branch:** `agent/gui-analysis`
- **HEAD:** 67ddccd62d: chore: auto-commit before merge (loop primary)

## Phase 1 - Task T585 complete

- Task type: annotate
- File: src/slic3r/GUI/SendToPrinter.cpp
- Deliverables: src/slic3r/GUI/SendToPrinter.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md
- Substantive additions: 9 boundary comments covering error translation, workflow state, async fetch, connection retries, upload progress, and teardown
- Verification excerpt: `[THREAD] Upload completion also returns on the UI thread; success closes the send flow by posting back into Plater.`
- Unity-impact summary: modal controller + async tunnel/upload service; explicit main-thread marshaling; state machine splits cleanly into prepare/sending/finish
- Hazards found: P2 x2 (connection retry path, teardown/cancellation cleanup)
- Git: Annotate SendToPrinter send workflow
- Next recommended Phase 1 task: T586 annotate: src/slic3r/GUI/SendToPrinter.hpp

## Phase 1 - Task T586 complete

- Task type: annotate
- File: src/slic3r/GUI/SendToPrinter.hpp
- Deliverables: src/slic3r/GUI/SendToPrinter.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 4 boundary comments covering dialog intent, shared state, event surface, thread handoff, Unity mapping, and porting hazard
- Verification excerpt: `[UNITY] Map this to a modal controller with a ScriptableObject-backed printer/device model and an async upload service.`
- Unity-impact summary: modal controller boundary is explicit; background transfer jobs stay behind the dialog; state machine and widget ownership are called out separately
- Hazards found: P2 x1 (protocol selection and dialog re-entry are interleaved with widget ownership)
- Git: Annotate SendToPrinter.hpp boundary
- Next recommended Phase 1 task: T587 annotate: src/slic3r/GUI/SingleChoiceDialog.cpp

## Phase 1 - Task T587 complete

- Task type: annotate
- File: src/slic3r/GUI/SingleChoiceDialog.cpp
- Deliverables: src/slic3r/GUI/SingleChoiceDialog.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 5 boundary comments covering modal intent, transient selection state, empty-list hazard, button events, and the fixed-layout DPI hook
- Verification excerpt: `[PORTING_HAZARD:P2] The constructor assumes at least one choice; the Unity port should validate or normalize empty lists before opening.`
- Unity-impact summary: modal choice dialog maps cleanly to a dismissible overlay; the selection stays widget-owned until confirm; layout can be responsive without a manual DPI callback
- Hazards found: P2 x1 (empty choice-list assumption)
- Git: Annotate SingleChoiceDialog selection flow
- Next recommended Phase 1 task: T588 annotate: src/slic3r/GUI/SingleChoiceDialog.hpp

## Phase 1 - Task T588 complete

- Task type: annotate
- File: src/slic3r/GUI/SingleChoiceDialog.hpp
- Deliverables: src/slic3r/GUI/SingleChoiceDialog.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 4 boundary comments covering dialog intent, transient combo-box state, Unity mapping, and a raw-pointer/empty-list porting hazard
- Verification excerpt: `[PORTING_HAZARD:P2] The public raw ComboBox accessor and the constructor's choice-list assumption make validation and ownership boundaries implicit; a Unity port should normalize empty lists before opening.`
- Unity-impact summary: modal chooser maps to a dropdown-backed controller; selection ownership stays inside the dialog; callers should not rely on widget internals in the Unity port
- Hazards found: P2 x1 (raw ComboBox exposure and empty-list assumption)
- Git: Annotate SingleChoiceDialog.hpp boundary
- Next recommended Phase 1 task: T589 annotate: src/slic3r/GUI/SkipPartCanvas.cpp

## Phase 1 - Task T589 complete

- Task type: annotate
- File: src/slic3r/GUI/SkipPartCanvas.cpp
- Deliverables: src/slic3r/GUI/SkipPartCanvas.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 6 boundary comments covering canvas intent, encoded-image state reset, OpenGL stencil rendering, hover/click/drag routing, and the 3MF metadata parser boundary
- Verification excerpt: `[UNITY] Port this as a custom controller over a RenderTexture-backed image view with explicit hit-test data,`
- Unity-impact summary: color-encoded hit testing becomes a retained controller with explicit pick data; the immediate-mode stencil pass maps to a mesh/shader overlay; 3MF parsing should move behind an import/service layer
- Hazards found: P2 x1 (immediate-mode GL interaction model), P3 x1 (image-color ID pipeline stability)
- Git: Annotate SkipPartCanvas canvas and parser boundary
- Next recommended Phase 1 task: T590 annotate: src/slic3r/GUI/SkipPartCanvas.hpp

## Phase 1 - Task T590 complete

- Task type: annotate
- File: src/slic3r/GUI/SkipPartCanvas.hpp
- Deliverables: src/slic3r/GUI/SkipPartCanvas.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 5 boundary comments covering the GL canvas contract, wx event routing, selection/zoom notifications, parser threading, and Unity migration split
- Verification excerpt: `[UNITY] Port this as a custom controller over a RenderTexture-backed image view with explicit`
- Unity-impact summary: color-picking stays outside the widget tree; mouse/zoom events map to a standard input bridge; 3MF parsing should become an import service
- Hazards found: P2 x1 (color-coded pick interaction model), P3 x1 (UI/import coupling in the header)
- Git: Annotate SkipPartCanvas.hpp boundary
- Next recommended Phase 1 task: T591 annotate: src/slic3r/GUI/SliceInfoPanel.cpp

## Phase 1 - Task T592 complete

- Task type: annotate
- File: src/slic3r/GUI/SliceInfoPanel.hpp
- Deliverables: src/slic3r/GUI/SliceInfoPanel.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 4 boundary comments covering popup ownership, async web-request completion, transient hover state, and the Unity split between the summary card and popover controller
- Verification excerpt: `[UNITY] Port as a summary card plus a separate popover controller, with thumbnail loading outside the view tree.`
- Unity-impact summary: summary card and detail popover become separate UI pieces; async fetch remains outside the view; hover state stays transient
- Hazards found: P2 x1 (async thumbnail completion can outlive the panel)
- Git: annotate SliceInfoPanel header boundary
- Next recommended Phase 1 task: T593 annotate: src/slic3r/GUI/SlicingProgressNotification.cpp

## Phase 1 - Task T596 complete

- Task type: annotate
- File: src/slic3r/GUI/StatusPanel.hpp
- Deliverables: src/slic3r/GUI/StatusPanel.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 6 boundary comments covering file intent, extruder-state painting, switching-status feedback, score upload state, task-panel actions, base dashboard layout, and concrete status-panel orchestration
- Verification excerpt: `[THREAD] Thumbnail refresh uses wxWebRequest and related callbacks, so response handling must preserve UI-thread ownership when ported.`
- Unity-impact summary:
  - Retained dashboard root should be split into reusable child panels plus popup/dialog services
  - Async web-request and thumbnail lifecycle need explicit main-thread marshaling in Unity
  - The base panel's many virtual hooks make a monolithic MonoBehaviour risky
- Hazards found: P2 x2, P3 x1
- Git: pending commit for StatusPanel.hpp annotation
- Next recommended Phase 1 task: T597 annotate: src/slic3r/GUI/StepMeshDialog.cpp

## Phase 1 - Task T610 complete

- Task type: annotate
- File: src/slic3r/GUI/Tab.hpp
- Deliverables: src/slic3r/GUI/Tab.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 9 boundary comments covering page ownership, tab-controller state, event flow, background update seams, per-tab specialization, and printer/filament model splits
- Verification excerpt: `[PORTING_HAZARD:P1] ScalableButton, wxTreeCtrl, and the custom page-switch workflow are tightly coupled to wx event semantics and need a bespoke Unity interaction layer.`
- Unity-impact summary: retained tab-controller with page stack; ScriptableObject-backed preset/config model; derived printer/filament page controllers instead of one monolith
- Hazards found: P1 x1, P2 x2, P3 x1
- Git: Annotate Tab preset controller boundary
- Next recommended Phase 1 task: T611 annotate: src/slic3r/GUI/TaskManager.cpp

## Phase 1 - Task T618 complete

- Task type: annotate
- File: src/slic3r/GUI/TickCode.hpp
- Deliverables: src/slic3r/GUI/TickCode.hpp, .ralph/agent/scratchpad.md
- Substantive additions: 4 boundary comments covering the tick marker value object, ordered set semantics, non-owning palette state, and the Unity model/service split
- Verification excerpt: `[PORTING_HAZARD:P2] The class mixes marker-model mutations with palette resolution and UI suppression rules, so a Unity port should split editing from color lookup.`
- Unity-impact summary: marker data stays serializable and model-driven; color lookup becomes a separate service; the palette pointer should not be preserved as a raw ownership contract
- Hazards found: P2 x1, P3 x1
- Git: Annotate TickCode.hpp boundary
- Next recommended Phase 1 task: T619 annotate: src/slic3r/GUI/UnsavedChangesDialog.cpp

## Phase 1 - Task T619 complete

- Task type: annotate
- File: src/slic3r/GUI/UnsavedChangesDialog.cpp
- Deliverables: src/slic3r/GUI/UnsavedChangesDialog.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 7 boundary comment blocks covering diff-tree ownership, the data-view controller, the unsaved-changes modal, the refresh path, the full-text compare popup, and the preset-compare dialog
- Verification excerpt: `[UNITY] Model this as a modal controller with a retained diff tree, a separate preset-save subdialog, and explicit confirmation actions bound to command buttons.`
- Unity-impact summary: retained diff-tree model; modal decision flow stays separate from preset save/transfer side effects; compare presets becomes a paired selection workspace
- Hazards found: P2 x2, P3 x2
- Git: Annotate UnsavedChangesDialog workflow
- Next recommended Phase 1 task: T620 annotate: src/slic3r/GUI/UnsavedChangesDialog.hpp

## Phase 1 - Task T620 complete

- Task type: annotate
- File: src/slic3r/GUI/UnsavedChangesDialog.hpp
- Deliverables: src/slic3r/GUI/UnsavedChangesDialog.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 5 boundary comment blocks covering the shared diff-tree boundary, DiffModel, DiffViewCtrl, the modal unsaved-changes workflow, the full-compare popup, and the paired preset-comparison dialog
- Verification excerpt: `[UNITY] Port this as a modal controller with a retained diff-tree view-model, a separate compare-presets workspace, and command buttons bound to explicit transfer/save/discard actions.`
- Unity-impact summary: retained diff-tree model; modal decision flow stays separate from preset save/transfer side effects; compare presets remains a paired data-bound workspace
- Hazards found: P2 x3, UNCLEAR x1
- Git: Annotate UnsavedChangesDialog.hpp boundary
- Next recommended Phase 1 task: T621 annotate: src/slic3r/GUI/UpdateDialogs.cpp

## Phase 1 - Task T597 complete

- Task type: annotate
- File: src/slic3r/GUI/StepMeshDialog.cpp
- Deliverables: src/slic3r/GUI/StepMeshDialog.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 7 boundary comments covering dialog intent, modal state, slider/text sync, config persistence, UI-thread event routing, worker cancellation, and Unity migration guidance
- Verification excerpt: `[THREAD] The current implementation uses a stop flag plus blocking join; Unity should replace this with cancellable async work.`
- Unity-impact summary: modal controller plus async mesh-estimation service; keep validation and persistence in a typed view model; avoid blocking joins in the confirm/cancel path
- Hazards found: P2 x1, P3 x1
- Git: Annotate StepMeshDialog import dialog
- Next recommended Phase 1 task: T598 annotate: src/slic3r/GUI/StepMeshDialog.hpp

## Phase 1 - Task T598 complete

- Task type: annotate
- File: src/slic3r/GUI/StepMeshDialog.hpp
- Deliverables: `src/slic3r/GUI/StepMeshDialog.hpp`, `.ralph/agent/scratchpad.md`
- Substantive additions: 6 comment blocks covering dialog intent, cached validation state, worker-thread ownership, Unity mapping, and lifecycle hazard
- Verification excerpt: `[UNITY] Map this to a modal controller with text fields, validation state, and an async mesh estimate service`
- Unity-impact summary:
  - Keep validation UI separate from async mesh estimation.
  - Preserve raw-text vs last-valid numeric state in the view-model.
  - Treat worker lifetime as explicit cancel/join behavior on close.
- Hazards found: P2 x1
- Git: Annotate StepMeshDialog header for Unity port
- Next recommended Phase 1 task: T599 annotate: src/slic3r/GUI/SurfaceDrag.cpp

## Reconciliation note

- `.ralph/ralph-tasks.md` still showed T598 as open after the completion event had already been processed; I reconciled the manifest by marking T598 done before starting T599.

## Phase 1 - Task T599 complete

- Task type: annotate
- File: src/slic3r/GUI/SurfaceDrag.cpp
- Deliverables: src/slic3r/GUI/SurfaceDrag.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 6 boundary comments covering the drag-tool state model, mouse event gate, raycast-driven surface snapping, fix-transform wrapping, camera-facing emboss orientation, and drag-update flow
- Verification excerpt: `[UNITY] This should become a drag-controller update tick that reuses the cached hit filter and writes the resulting transform back through the scene model.`
- Unity-impact summary: explicit pointer-drag controller; scene-query service for direct-hit and nearest-point fallbacks; model-layer transform helpers instead of view mutation
- Hazards found: P2 x2 (hover-gated gesture start, embossed volume/object policy split)
- Git: Annotate SurfaceDrag drag controller
- Next recommended Phase 1 task: T600 annotate: src/slic3r/GUI/SurfaceDrag.hpp

## Phase 1 - Task T600 complete

- Task type: annotate
- File: src/slic3r/GUI/SurfaceDrag.hpp
- Deliverables: src/slic3r/GUI/SurfaceDrag.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 4 boundary comments covering the transient drag session, mouse-event gate, geometry helper layer, and fixed-transform pipeline
- Verification excerpt: `[UNITY] The closest match is a pointer-drag controller fed by IPointerDown/Drag/Up events plus a scene-query service that owns the cached hit-test filter and transform math.`
- Unity-impact summary: transient drag-session model; pure geometry/service layer for transform math; explicit selection/hover precondition for gesture start
- Hazards found: P2 x2 (raw-pointer/session lifetime, hover-gated drag start)
- Git: SurfaceDrag header annotations
- Next recommended Phase 1 task: T601 annotate: src/slic3r/GUI/SyncAmsInfoDialog.cpp

## Phase 1 - Task T601 complete

- Task type: annotate
- File: src/slic3r/GUI/SyncAmsInfoDialog.cpp
- Deliverables: src/slic3r/GUI/SyncAmsInfoDialog.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 7 boundary comments covering the modal controller, UI event wiring, worker-thread printer refresh, status gating, row-popup flow, thumbnail compositing, and sync-toast frames
- Verification excerpt: `[THREAD] Fetches remote print info on a worker thread, then posts back to mutate dialog state and repopulate the printer list.`
- Unity-impact summary: modal controller should split from data-bound views; async printer refresh needs a main-thread completion bridge; thumbnail preview math is CPU-side and should move to a texture job/shader path
- Hazards found: P2 x2, P3 x1
- Git: SyncAmsInfoDialog.cpp annotations
- Next recommended Phase 1 task: T602 annotate: src/slic3r/GUI/SyncAmsInfoDialog.hpp

## Phase 1 - Task T593 complete

- Task type: annotate
- File: src/slic3r/GUI/SlicingProgressNotification.cpp
- Deliverables: src/slic3r/GUI/SlicingProgressNotification.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 19 boundary comments covering the progress state machine, theme updates, late print-info enrichment, canvas overlay rendering, button events, and Unity mapping
- Verification excerpt: `[UNITY] Port this as a screen-space overlay controller that reflows from state, not a custom-painted native window.`
- Unity-impact summary: retained HUD controller over the viewport; Daily Tips remains a separate panel; cancel/close and late metadata are explicit state transitions
- Hazards found: P2 x1 (immediate-mode overlay mixes lifecycle, rendering, and actions)
- Git: annotate SlicingProgressNotification overlay
- Next recommended Phase 1 task: T594 annotate: src/slic3r/GUI/SlicingProgressNotification.hpp

## Phase 1 - Task T594 complete

- Task type: annotate
- File: src/slic3r/GUI/SlicingProgressNotification.hpp
- Deliverables: src/slic3r/GUI/SlicingProgressNotification.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 6 boundary comments covering class intent, transient state, event flow, UI-thread ownership, Unity mapping, and a P2 porting hazard; plus state-field annotations for the notification mode and fade behavior
- Verification excerpt: `[THREAD] The notification is UI-thread-owned; the cancel callback and late-state updates must re-enter through the notification manager rather than mutate rendering state from workers.`
- Unity-impact summary: screen-space HUD controller keeps the state machine explicit; DailyTips stays as a reusable child panel; worker-thread mutation remains out of band
- Hazards found: P2 x1 (lifecycle, rendering, and interaction are coupled in one overlay class)
- Git: Annotate SlicingProgressNotification header boundary
- Next recommended Phase 1 task: T596 annotate: src/slic3r/GUI/StatusPanel.hpp

## Phase 1 - Task T595 complete

- Task type: annotate
- File: src/slic3r/GUI/StatusPanel.cpp
- Deliverables: src/slic3r/GUI/StatusPanel.cpp, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 6 boundary comments covering dashboard intent, retained Unity mapping, subordinate widget ownership, transient confirmation flow, shared monitoring-page state, and the concrete action wiring; plus state notes for the nozzle selector and related cached UI state
- Verification excerpt: `[INTENT] This file is the printer-status dashboard: it composes the monitor/printing cards,`
- Unity-impact summary: split the dashboard into a retained page controller with reusable child panels and explicit subview state instead of one monolithic wxWidgets panel
- Hazards found: P2 x0 in the new annotations; the main integration risk is the size and cross-cutting nature of the retained dashboard

## Phase 1 - Task T616 complete

- Task type: annotate
- File: src/slic3r/GUI/ThermalPreconditioningDialog.hpp
- Deliverables: src/slic3r/GUI/ThermalPreconditioningDialog.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 7 boundary comments covering modal intent, countdown state, event flow, UI-thread timer ownership, Unity mapping, and a P2 porting hazard; plus member-level state notes
- Verification excerpt: `[UNITY] Map this to a modal overlay controller with a scheduled tick (eg. coroutine/InvokeRepeating) plus bound text fields.`
- Unity-impact summary: the timer-driven countdown becomes an explicit scheduled tick; dialog state stays in a retained controller; close/update actions need clear lifetime ownership in Unity
- Hazards found: P2 x1 (timer/lifetime coupling)
- Git: Annotate ThermalPreconditioningDialog.hpp boundary
- Next recommended Phase 1 task: T617 annotate: src/slic3r/GUI/TickCode.cpp
- Git: annotate StatusPanel dashboard boundary
- Next recommended Phase 1 task: T596 annotate: src/slic3r/GUI/StatusPanel.hpp

## Phase 1 - Task T611 complete

- Task type: annotate
- File: src/slic3r/GUI/TaskManager.cpp
- Deliverables: src/slic3r/GUI/TaskManager.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 6 boundary comments covering scheduler intent, task cancel flow, pacing policy, ingestion, worker-thread dispatch, remote task sync, and local state lookup
- Verification excerpt: `[THREAD][PORTING_HAZARD:P2] Scheduling spins up a dedicated worker thread per send`
- Unity-impact summary: retained queue/service for throttled sends; async job/coroutine replacement for boost threads; main-thread marshaling for UI callbacks
- Hazards found: P2 x1, UNCLEAR x1
- Git: Annotate TaskManager scheduler boundary
- Next recommended Phase 1 task: T612 annotate: src/slic3r/GUI/TaskManager.hpp

## Phase 1 - Task T615 complete

- Task type: annotate
- File: src/slic3r/GUI/ThermalPreconditioningDialog.cpp
- Deliverables: src/slic3r/GUI/ThermalPreconditioningDialog.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md, .ralph/agent/memories.md
- Substantive additions: 7 boundary comments covering event routing, modal intent/state, timer lifetime, UI layout, confirm dismissal, countdown recomputation, and timer stop conditions
- Verification excerpt: `[PORTING_HAZARD:P2] stage_curr == 58 is a protocol magic value, and get_my_machine() is assumed to succeed without a null check.`
- Unity-impact summary: modal controller with a scheduled tick; retained countdown label bound to device-state data; explicit state/service split for machine lookup and stage gating
- Hazards found: P2 x1, UNCLEAR x1
- Git: Annotate ThermalPreconditioningDialog countdown dialog
- Next recommended Phase 1 task: T616 annotate: src/slic3r/GUI/ThermalPreconditioningDialog.hpp

## Tasks

### Completed

- [x] document: tests/libslic3r/test_mutable_polygon.cpp (T105)
- [x] document: tests/libslic3r/test_clipper_utils.cpp (T106)
- [x] document: tests/libslic3r/test_voronoi.cpp (T108)
- [x] document: tests/libslic3r/test_elephant_foot_compensation.cpp
- [x] document: tests/libslic3r/test_config.cpp
- [x] document: tests/libslic3r/test_appconfig.cpp
- [x] document: tests/libslic3r/test_placeholder_parser.cpp
- [x] document: tests/libslic3r/test_3mf.cpp
- [x] document: tests/libslic3r/test_meshboolean.cpp
- [x] document: tests/libslic3r/test_marchingsquares.cpp
- [x] P0-T001 Repository state verification
- [x] P0-T002 Create working branch
- [x] P0-T003 GUI directory census
- [x] P0-T004 Entry point trace
- [x] P0-T005 Application class identification
- [x] P0-T006 Main window class identification
- [x] P0-T007 Create output directories
- [x] P0-T008 Initialize task registry
- [x] P0-T009 Commit orientation complete
- [x] Populate Phase 1 tasks - Create 719 annotation tasks from manifest
- [x] T101 annotate: src/slic3r/GUI/GUI_App.cpp
- [x] T101-part2 annotate: src/slic3r/GUI/GUI_App.cpp (part 2: lines 3257-7965, remaining functions)
- [x] T104 annotate: src/slic3r/GUI/MainFrame.cpp
- [x] Verify and document P0-T003 GUI directory census
- [x] Commit orientation complete
- [x] P0-T001: Repository state verification
- [x] P0-T003: GUI Directory Census
- [x] P0-T004: Entry Point Trace
- [x] P0-T005: Application Class Identification
- [x] P0-T006: Main Window Class Identification
- [x] P0-T007: Create Output Directories
- [x] P0-T008: Initialize Task Registry
- [x] P0-T009: Commit orientation complete
- [x] T101-part2: annotate src/slic3r/GUI/GUI_App.cpp (2000-4000)
- [x] T101-part3: annotate src/slic3r/GUI/GUI_App.cpp (4000-6000)
- [x] T101-part4: annotate src/slic3r/GUI/GUI_App.cpp (6000-7972)
- [x] T110: annotate src/slic3r/GUI/MainFrame.hpp
- [x] T111: annotate src/slic3r/GUI/MainFrame.cpp
- [x] T120: annotate src/slic3r/GUI/Plater.hpp
- [x] T121: annotate src/slic3r/GUI/Plater.cpp
- [x] T111-part2: annotate src/slic3r/GUI/MainFrame.cpp (2000-4000)
- [x] T111-part3: annotate src/slic3r/GUI/MainFrame.cpp (4000-4307)
- [x] T121-part2: annotate src/slic3r/GUI/Plater.cpp (2001-4000)
- [x] T121-part3: annotate src/slic3r/GUI/Plater.cpp (4001-6000)
- [x] T121-part4: src/slic3r/GUI/Plater.cpp (6001-8000)

## Phase 1 - Task T603 complete

- Task type: annotate
- File: src/slic3r/GUI/SysInfoDialog.cpp
- Deliverables: src/slic3r/GUI/SysInfoDialog.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 7 boundary comments covering report-text assembly, live memory/undo-stack stats, modal dialog ownership, Windows-only blacklist inspection, DPI rescaling, clipboard export, and modal dismissal
- Verification excerpt: `[UNITY] Map this to a modal overlay or popup with a retained text model, two scroll views, and a command button.`
- Unity-impact summary: the dialog becomes a retained modal shell with reusable report text; platform-specific process inspection should move behind a service boundary; clipboard export remains an explicit command path
- Hazards found: P2 x3, P3 x1
- Git: Annotate SysInfoDialog.cpp system report dialog
- Next recommended Phase 1 task: T604 annotate: src/slic3r/GUI/SysInfoDialog.hpp

## Phase 1 - Task T604 complete

- Task type: annotate
- File: src/slic3r/GUI/SysInfoDialog.hpp
- Deliverables: src/slic3r/GUI/SysInfoDialog.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 6 boundary comments covering modal intent, retained state, Unity mapping, porting hazard, DPI refresh, and clipboard close/export handlers
- Verification excerpt: `[UNITY] Map this to a modal overlay/popup with a retained report-text model, two scrollable text panes, and a command button.`
- Unity-impact summary: header now defines the retained report-panes boundary; clipboard/export remains a distinct command path; process-inspection data stays behind a service in the Unity split
- Hazards found: P2 x1 (live process inspection plus clipboard access coupled to the dialog)
- Git: e21686b45f Annotate SysInfoDialog.hpp boundary
- Next recommended Phase 1 task: T605 annotate: src/slic3r/GUI/Tabbook.cpp

## Phase 1 - Task T602 complete

- Task type: annotate
- File: src/slic3r/GUI/SyncAmsInfoDialog.hpp
- Deliverables: src/slic3r/GUI/SyncAmsInfoDialog.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md
- Substantive additions: 6 comment blocks covering modal intent, dialog state, widget ownership, event flow, payload state, and overlay-frame mapping
- Verification excerpt: `[UNITY] Split into a modal controller backed by a retained data model, with separate popup/overlay prefabs for the sync confirm and completion banners.`
- Unity-impact summary: modal controller + retained model split; anchored overlay prefabs for sync confirmation/completion; explicit async refresh/thread seam
- Hazards found: P2 x1
- Git: Annotate SyncAmsInfoDialog.hpp
- Next recommended Phase 1 task: T603 annotate: src/slic3r/GUI/SysInfoDialog.cpp
- [x] T121-part5: annotate src/slic3r/GUI/Plater.cpp (8000-10000)
- [x] T121-part6: Plater.cpp part 6
- [x] T121-part7: Plater.cpp (12000-14000)
- [x] annotate: src/slic3r/GUI/Plater.cpp (12000-14000)
- [x] P0-T003: GUI Directory Census
- [x] P0-T004: Entry Point Trace
- [x] P0-T005: Application Class Identification
- [x] P0-T006: Main Window Class Identification
- [x] P0-T007: Create Output Directories
- [x] P0-T008: Initialize Task Registry
- [x] P0-T009: Commit Orientation Complete
- [x] P0-T004: Entry Point Trace
- [x] P0-T005: Application Class Identification
- [x] P0-T007: Create Output Directories
- [x] P0-T008: Initialize Task Registry
- [x] P0-T009: Commit Orientation Complete
- [x] T101 annotate: src/libvgcode/include/ColorPrint.hpp
- [x] T102 annotate: src/libvgcode/include/ColorRange.hpp
- [x] T103 annotate: src/libvgcode/include/GCodeInputData.hpp
- [x] T104 annotate: src/libvgcode/include/PathVertex.hpp
- [x] T105 annotate: src/libvgcode/include/Types.hpp
- [x] T107 annotate: src/libvgcode/src/Bitset.cpp
- [x] T108 annotate: src/libvgcode/src/Bitset.hpp
- [x] T109 annotate: src/libvgcode/src/CogMarker.cpp
- [x] T110 annotate: src/libvgcode/src/CogMarker.hpp
- [x] T111 annotate: src/libvgcode/src/ColorPrint.cpp
- [x] T112 annotate: src/libvgcode/src/ColorRange.cpp
- [x] T113 annotate: src/libvgcode/src/ExtrusionRoles.cpp
- [x] T114 annotate: src/libvgcode/src/ExtrusionRoles.hpp
- [x] T115 annotate: src/libvgcode/src/GCodeInputData.cpp
- [x] T116 annotate: src/libvgcode/src/Layers.cpp
- [x] T117 annotate: src/libvgcode/src/Layers.hpp
- [x] T118 annotate: src/libvgcode/src/OpenGLUtils.cpp
- [x] T119 annotate: src/libvgcode/src/OpenGLUtils.hpp
- [x] T120 annotate: src/libvgcode/src/OptionTemplate.cpp
- [x] T121 annotate: src/libvgcode/src/OptionTemplate.hpp
- [x] T122 annotate: src/libvgcode/src/PathVertex.cpp
- [x] T123 annotate: src/libvgcode/src/Range.cpp
- [x] T124 annotate: src/libvgcode/src/Range.hpp
- [x] T125 annotate: src/libvgcode/src/SegmentTemplate.cpp
- [x] T126 annotate: src/libvgcode/src/SegmentTemplate.hpp
- [x] T127 annotate: src/libvgcode/src/Settings.cpp
- [x] T128 annotate: src/libvgcode/src/Settings.hpp
- [x] T129 annotate: src/libvgcode/src/ShadersES.hpp
- [x] T130 annotate: src/libvgcode/src/Shaders.hpp
- [x] T131 annotate: src/libvgcode/src/ToolMarker.cpp
- [x] T133 annotate: src/libvgcode/src/Types.cpp
- [x] T134 annotate: src/libvgcode/src/Utils.cpp
- [x] T135 annotate: src/libvgcode/src/Utils.hpp
- [x] T136 annotate: src/libvgcode/src/Viewer.cpp
- [x] T137 annotate: src/libvgcode/src/ViewerImpl.cpp
- [x] T138 annotate: src/libvgcode/src/ViewerImpl.hpp
- [x] T139 annotate: src/libvgcode/src/ViewRange.cpp
- [x] T140 annotate: src/libvgcode/src/ViewRange.hpp
- [x] T141 annotate: src/slic3r/GUI/2DBed.cpp
- [x] T142 annotate: src/slic3r/GUI/2DBed.hpp
- [x] T143 annotate: src/slic3r/GUI/3DBed.cpp
- [x] T144 annotate: src/slic3r/GUI/AboutDialog.hpp
- [x] T145 annotate: src/slic3r/GUI/ConfigWizard.cpp
- [x] T146 annotate: src/slic3r/GUI/ConfigWizard.hpp
- [x] T147 annotate: src/slic3r/GUI/InstanceCheck.cpp
- [x] T148 annotate: src/slic3r/GUI/InstanceCheck.hpp
- [x] T149 annotate: src/slic3r/GUI/KBShortcutsDialog.cpp
- [x] T150 annotate: src/slic3r/GUI/KBShortcutsDialog.hpp
- [x] T151 annotate: src/slic3r/GUI/MsgDialog.cpp
- [x] T152 annotate: src/slic3r/GUI/MsgDialog.hpp
- [x] T154 annotate: src/slic3r/GUI/PresetComboBoxes.hpp
- [x] T155 annotate: src/slic3r/GUI/AmsWidgets.cpp
- [x] T156 annotate: src/slic3r/GUI/Tab.hpp
- [x] T157 annotate: src/slic3r/GUI/UpdateDialogs.cpp
- [x] T158 annotate: src/slic3r/GUI/UpdateDialogs.hpp
- [x] T159 annotate: src/slic3r/GUI/WipeTowerDialog.cpp
- [x] T160 annotate: src/slic3r/GUI/WipeTowerDialog.hpp
- [x] T163 annotate: src/slic3r/GUI/GUI.cpp
- [x] T164 annotate: src/slic3r/GUI/GUI.hpp
- [x] T165 annotate: src/slic3r/GUI/GUI_App.cpp
- [x] T166 annotate: src/slic3r/GUI/GUI_App.hpp
- [x] T167 annotate: src/slic3r/GUI/GUI_ObjectList.cpp
- [x] T168 annotate: src/slic3r/GUI/BBLStatusBarBind.hpp
- [x] T171 annotate: src/slic3r/GUI/PartPlate.cpp
- [x] T175 annotate: src/slic3r/GUI/ImGuiWrapper.cpp
- [x] T176 annotate: src/slic3r/GUI/BBLTopbar.hpp
- [x] T177 annotate: src/slic3r/GUI/BedShapeDialog.cpp
- [x] T178 annotate: src/slic3r/GUI/BedShapeDialog.hpp
- [x] T179 annotate: src/slic3r/GUI/Jobs/ArrangeJob.cpp
- [x] T180 annotate: src/slic3r/GUI/BindDialog.hpp
- [x] T181 annotate: src/slic3r/GUI/Jobs/BackgroundSlicingProcessJob.cpp
- [x] T182 annotate: src/slic3r/GUI/Jobs/BackgroundSlicingProcessJob.hpp
- [x] T183 annotate: src/slic3r/GUI/BitmapComboBox.cpp
- [x] T184 annotate: src/slic3r/GUI/BitmapComboBox.hpp
- [x] annotate: src/slic3r/GUI/BonjourDialog.hpp
- [x] T188 annotate: src/slic3r/GUI/Jobs/Job.hpp
- [x] T197 annotate: src/slic3r/GUI/Jobs/SLAImportJob.cpp
- [x] T198 annotate: src/slic3r/GUI/Jobs/SLAImportJob.hpp
- [x] T205 annotate: src/slic3r/GUI/Gizmos/GLGizmoBase.cpp

## Phase 1 - Task T187 complete

- Task type: annotate
- File: src/slic3r/GUI/calib_dlg.cpp
- Deliverables: src/slic3r/GUI/calib_dlg.cpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 17 annotation blocks covering shared helpers, eight calibration dialogs, and direct Plater dispatch paths

## Phase 1 - Task T571 complete

- Task type: annotate
- File: src/slic3r/GUI/SceneRaycaster.cpp
- Deliverables: src/slic3r/GUI/SceneRaycaster.cpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 5 annotation blocks covering scene-picking intent, pick registry state, UI-thread hit queries, selected-volume bias, and OpenGL debug overlay mapping
- Verification excerpt: `[UNITY] Port this as a dedicated scene-query service backed by collider/raycast layers plus an explicit selection-priority policy.`
- Unity-impact summary:
  - Preserve bucket ordering for gizmos, fallback gizmos, beds, and volumes.
  - Carry the selected-volume bias into the Unity selection controller.
  - Render debug hit markers through a gizmo/debug pass instead of gameplay rendering.
- Hazards found: P2=1, P3=0, P1=0
- Git: chore: annotate SceneRaycaster raycast flow
- Next recommended Phase 1 task: T572 annotate: src/slic3r/GUI/SceneRaycaster.hpp
- Verification excerpt: `[PORTING_HAZARD:P2] The dialog reconfigures controls live based on firmware and method selection`
- Unity-impact summary:
  - Model each calibration dialog as a controller-backed modal form.
  - Preserve firmware-aware axis hiding/mirroring in the Unity view model.
  - Treat calibration submission as a synchronous command payload to the printer pipeline.
- Hazards found: P2 x3, P3 x1
- Git: annotate calib_dlg calibration dialogs
- Next recommended Phase 1 task: T188 annotate: src/slic3r/GUI/calib_dlg.hpp
- [x] T559 annotate: src/slic3r/GUI/RammingChart.cpp

## Phase 1 - Task T578 complete

- Task type: annotate
- File: src/slic3r/GUI/SelectMachine.hpp
- Deliverables: src/slic3r/GUI/SelectMachine.hpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 8 annotation blocks covering the dialog boundary, thumbnail luminance helper, option rows, preview panel, mode switch, modal workflow, and printer header control
- Verification excerpt: `[UNITY] Port this as a modal controller with step views, a printer picker, async job/status events, and a dedicated preview panel.`
- Unity-impact summary:
  - Keep the send-print flow as a modal controller with explicit step states.
  - Split the painted option/thumbnail widgets into reusable Unity subviews.
  - Marshal worker/status callbacks back to the UI thread before touching UI state.
- Hazards found: P2=1, P3=4
- Git: annotate SelectMachine header
- Next recommended Phase 1 task: T579 annotate: src/slic3r/GUI/SelectMachinePop.cpp

## Phase 1 - Task T580 complete

- Task type: annotate
- File: src/slic3r/GUI/SelectMachinePop.hpp
- Deliverables: src/slic3r/GUI/SelectMachinePop.hpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md, .ralph/agent/scratchpad.md
- Substantive additions: 8 annotation blocks covering the popup boundary, row widget intent, row state, popup lifecycle, worker-thread boundary, event routing, and rename dialog mapping
- Verification excerpt: `[UNITY] Port as a non-modal floating controller with recycled row views and explicit focus-loss dismissal instead of wxPopupWindow + manual mouse forwarding.`
- Unity-impact summary:
  - Model the popup as a controller owning a recycled device list and filter state.
  - Replace manual click forwarding and timer refreshes with explicit main-thread events.
  - Treat the rename flow as a simple modal dialog controller with validation state.
- Hazards found: P2=1, P3=1
- Git: annotate SelectMachinePop header
- Next recommended Phase 1 task: T581 annotate: src/slic3r/GUI/SendMultiMachinePage.cpp

## Phase 1 - Task T581 complete

- Task type: annotate
- File: src/slic3r/GUI/SendMultiMachinePage.cpp
- Deliverables: src/slic3r/GUI/SendMultiMachinePage.cpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 11 annotation blocks covering the page boundary, row widget behavior, device-list refresh, print payload assembly, send workflow, AMS mapping callback, page composition, filament rebuild, thumbnail defaults, rename validation, and polling refresh
- Verification excerpt: `[UNITY] Port as a modal controller with a scrollable list of recyclable device-row prefabs, a dedicated AMS-mapping subpanel, and a ScriptableObject-backed settings model.`
- Unity-impact summary:
  - Split the page into a modal shell plus reusable list-row, mapping, and settings subviews.
  - Move device polling/subscription into a cached service that refreshes the UI on the main thread.
  - Replace pipe-delimited mapping payloads with a typed DTO boundary in Unity.
- Hazards found: P2=2, P3=2, P1=0
- Git: pending commit
- Next recommended Phase 1 task: T582 annotate: src/slic3r/GUI/SendMultiMachinePage.hpp

## Phase 1 - Task T582 complete

- Task type: annotate
- File: src/slic3r/GUI/SendMultiMachinePage.hpp
- Deliverables: src/slic3r/GUI/SendMultiMachinePage.hpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 2 annotation blocks covering the custom-painted device row controller and the modal send-to-multi-printer workflow boundary
- Verification excerpt: `[PORTING_HAZARD:P1] The page mixes persistence, custom row widgets, timer refresh, and modal send/export side effects, so Unity should not treat it as a single monolithic window.`
- Unity-impact summary:
  - Split the page into a modal shell, a recyclable device list, and an AMS mapping panel.
  - Treat the row widget as a prefab/controller pair with explicit highlight and click handling.
  - Marshal refresh and discovery state back onto the Unity main thread.
- Hazards found: P1=1, P2=1, P3=0
- Git: annotate SendMultiMachinePage header
- Next recommended Phase 1 task: T583 annotate: src/slic3r/GUI/SendSystemInfoDialog.cpp

## Phase 1 - Task T583 complete

- Task type: annotate
- File: src/slic3r/GUI/SendSystemInfoDialog.cpp
- Deliverables: src/slic3r/GUI/SendSystemInfoDialog.cpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 12 annotation blocks covering dialog purpose, cached payload state, preview modal, version gate, payload assembly, platform probes, OpenGL metadata, worker-thread upload flow, and the external entry point
- Verification excerpt: `[PORTING_HAZARD:P2] The payload is assembled from OS, hardware, OpenGL, and installed-library probes, so the Unity port needs a privacy-reviewed data contract rather than a 1:1 UI swap.`
- Unity-impact summary:
  - Model the prompt as a modal opt-in controller with a read-only payload preview.
  - Move upload behavior behind an async service/coroutine boundary instead of a nested modal worker thread.
  - Treat the system-info schema as a reviewed payload contract, not just a UI translation.
- Hazards found: P2=2, P3=0, P1=0
- Git: annotate SendSystemInfoDialog system-info flow
- Next recommended Phase 1 task: T584 annotate: src/slic3r/GUI/SendSystemInfoDialog.hpp

## Phase 1 - Task T584 complete

- Task type: annotate
- File: src/slic3r/GUI/SendSystemInfoDialog.hpp
- Deliverables: src/slic3r/GUI/SendSystemInfoDialog.hpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 3 annotation lines covering the privacy-consent entry point, Unity migration hook, and network-I/O hazard
- Verification excerpt: `[UNITY] Model this as a modal controller entry method that can open a consent panel and then hand off to an async upload service.`
- Unity-impact summary:
  - Keep the consent gate as a distinct modal entry rather than inlining it into a generic settings screen.
  - Marshal any future upload work behind an async service boundary in Unity.
  - Preserve the privacy boundary between UI consent and data collection.
- Hazards found: P2=1, P3=0, P1=0
- Git: annotate SendSystemInfoDialog header
- Next recommended Phase 1 task: T585 annotate: src/slic3r/GUI/SendToPrinter.cpp

## Phase 1 - Task T568 complete

- Task type: annotate
- File: src/slic3r/GUI/SafetyOptionsDialog.hpp
- Deliverables: src/slic3r/GUI/SafetyOptionsDialog.hpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 10 annotation blocks covering dialog intent, state ownership, transient toast behavior, printer-object sync, and Unity migration guidance
- Verification excerpt: `[PORTING_HAZARD:P2] The dialog mixes local UI toggles with live device capability checks and a wxPopupWindow + wxTimer feedback path.`
- Unity-impact summary:
  - Model the dialog as a scrollable modal settings panel, not a native popup tree.
  - Keep printer state in a controller-backed model and treat the toast as an overlay/notice.
  - Preserve UI-thread-only timer behavior when porting the unavailable-state feedback.
- Hazards found: P2 x1, P3 x1
- Git: annotate SafetyOptionsDialog header
- Next recommended Phase 1 task: T569 annotate: src/slic3r/GUI/SavePresetDialog.cpp

## Phase 1 - Task T560 complete

- Task type: annotate
- File: src/slic3r/GUI/RammingChart.hpp
- Deliverables: src/slic3r/GUI/RammingChart.hpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 12 annotation blocks covering event bridge, constructor state, accessors, hit-testing, coordinate transforms, and derived-cache ownership
- Verification excerpt: `[PORTING_HAZARD:P2] The current design mixes input, curve mutation, and redraw triggers inside the widget.`
- Unity-impact summary:
  - Port the chart as a dedicated controller/renderer rather than a stock form field.
  - Keep math-space and screen-space conversion explicit in the Unity implementation.
  - Surface curve refreshes through a custom event or callback path.
- Hazards found: P2 x1
- Git: annotate RammingChart chart header
- Next recommended Phase 1 task: T561 annotate: src/slic3r/GUI/RecenterDialog.cpp
- [x] T307 annotate: src/slic3r/GUI/Gizmos/GLGizmoAssembly.cpp
- [x] T308 annotate: src/slic3r/GUI/Gizmos/GLGizmoAssembly.hpp
- [x] T309 annotate: src/slic3r/GUI/Gizmos/GLGizmoBase.cpp
- [x] T310 annotate: src/slic3r/GUI/Gizmos/GLGizmoBase.hpp
- [x] T311 annotate: src/slic3r/GUI/Gizmos/GLGizmoBrimEars.cpp
- [x] T312 annotate: src/slic3r/GUI/Gizmos/GLGizmoBrimEars.hpp
- [x] T314 annotate: src/slic3r/GUI/Gizmos/GLGizmoCut.hpp
- [x] T315 annotate: src/slic3r/GUI/Gizmos/GLGizmoEmboss.cpp
- [x] T316 annotate: src/slic3r/GUI/Gizmos/GLGizmoEmboss.hpp
- [x] T317 annotate: src/slic3r/GUI/Gizmos/GLGizmoFaceDetector.cpp
- [x] T318 annotate: src/slic3r/GUI/Gizmos/GLGizmoFaceDetector.hpp
- [x] T319 annotate: src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.cpp
- [x] T320 annotate: src/slic3r/GUI/Gizmos/GLGizmoFdmSupports.hpp
- [x] T321 annotate: src/slic3r/GUI/Gizmos/GLGizmoFlatten.cpp
- [x] T322 annotate: src/slic3r/GUI/Gizmos/GLGizmoFlatten.hpp
- [x] T323 annotate: src/slic3r/GUI/Gizmos/GLGizmoFuzzySkin.cpp
- [x] T324 annotate: src/slic3r/GUI/Gizmos/GLGizmoFuzzySkin.hpp
- [x] T325 annotate: src/slic3r/GUI/Gizmos/GLGizmoHollow.cpp
- [x] T326 annotate: src/slic3r/GUI/Gizmos/GLGizmoHollow.hpp
- [x] T327 annotate: src/slic3r/GUI/Gizmos/GLGizmoMeasure.cpp
- [x] T328 annotate: src/slic3r/GUI/Gizmos/GLGizmoMeasure.hpp
- [x] T329 annotate: src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.cpp
- [x] T330 annotate: src/slic3r/GUI/Gizmos/GLGizmoMeshBoolean.hpp
- [x] T331 annotate: src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.cpp
- [x] T332 annotate: src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp
- [x] T334 annotate: src/slic3r/GUI/Gizmos/GLGizmoMove.hpp
- [x] T336 annotate: src/slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp
- [x] T337 annotate: src/slic3r/GUI/Gizmos/GLGizmoRotate.cpp
- [x] T338 annotate: src/slic3r/GUI/Gizmos/GLGizmoRotate.hpp
- [x] T339 annotate: src/slic3r/GUI/Gizmos/GLGizmoScale.cpp
- [x] T340 annotate: src/slic3r/GUI/Gizmos/GLGizmoScale.hpp
- [x] T341 annotate: src/slic3r/GUI/Gizmos/GLGizmosCommon.cpp
- [x] T344 annotate: src/slic3r/GUI/Gizmos/GLGizmoSeam.hpp
- [x] T345 annotate: src/slic3r/GUI/Gizmos/GLGizmos.hpp
- [x] T346 annotate: src/slic3r/GUI/Gizmos/GLGizmoSimplify.cpp
- [x] T347 annotate: src/slic3r/GUI/Gizmos/GLGizmoSimplify.hpp
- [x] T349 annotate: src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp
- [x] T350 annotate: src/slic3r/GUI/Gizmos/GLGizmosManager.cpp
- [x] T353 annotate: src/slic3r/GUI/Gizmos/GLGizmoSVG.hpp
- [x] T354 annotate: src/slic3r/GUI/Gizmos/GLGizmoText.cpp
- [x] T355 annotate: src/slic3r/GUI/Gizmos/GLGizmoText.hpp
- [x] T356 annotate: src/slic3r/GUI/GLCanvas3D.cpp
- [x] T357 annotate: src/slic3r/GUI/GLCanvas3D.hpp
- [x] T359 annotate: src/slic3r/GUI/GLModel.hpp
- [x] T360 annotate: src/slic3r/GUI/GLSelectionRectangle.cpp
- [x] T361 annotate: src/slic3r/GUI/GLSelectionRectangle.hpp
- [x] T362 annotate: src/slic3r/GUI/GLShader.cpp
- [x] T363 annotate: src/slic3r/GUI/GLShader.hpp
- [x] T364 annotate: src/slic3r/GUI/GLShadersManager.cpp
- [x] T365 annotate: src/slic3r/GUI/GLShadersManager.hpp
- [x] T366 annotate: src/slic3r/GUI/GLTexture.cpp
- [x] T367 annotate: src/slic3r/GUI/GLTexture.hpp
- [x] T368 annotate: src/slic3r/GUI/GLToolbar.cpp
- [x] T370 annotate: src/slic3r/GUI/GUI_App.cpp
- [x] T371 annotate: src/slic3r/GUI/GUI_App.hpp
- [x] T373 annotate: src/slic3r/GUI/GUI_AuxiliaryList.hpp
- [x] T374 annotate: src/slic3r/GUI/GuiColor.cpp
- [x] T375 annotate: src/slic3r/GUI/GuiColor.hpp
- [x] T376 annotate: src/slic3r/GUI/GUI_Colors.cpp
- [x] T377 annotate: src/slic3r/GUI/GUI_Colors.hpp
- [x] T378 annotate: src/slic3r/GUI/GUI.cpp

## Phase 1 - Task T570 complete

- Task type: annotate
- File: src/slic3r/GUI/SavePresetDialog.hpp
- Deliverables: src/slic3r/GUI/SavePresetDialog.hpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 2 annotation blocks covering dialog ownership/state/event flow and nested Item row migration guidance
- Verification excerpt: `// [UNITY] Map this to a modal controller with a repeated row prefab, explicit validation badges, and confirm/cancel`
- Unity-impact summary:
  - The header now captures the modal preset-save controller boundary instead of leaving row semantics implicit in the cpp.
  - Per-row validation, project-save toggles, and detach state are called out as separate Unity view-model concerns.
  - The dialog's printer-context side effects are flagged as a command-driven migration hazard.
- Hazards found: P2=1, P3=1
- Git: pending commit "Annotate SavePresetDialog header"
- Next recommended Phase 1 task: T571 annotate `src/slic3r/GUI/SceneRaycaster.cpp`
- [x] T379 annotate: src/slic3r/GUI/GUI_Factories.cpp
- [x] T380 annotate: src/slic3r/GUI/GUI_Factories.hpp
- [x] T381 annotate: src/slic3r/GUI/GUI_Geometry.cpp
- [x] T382 annotate: src/slic3r/GUI/GUI_Geometry.hpp
- [x] T384 annotate: src/slic3r/GUI/GUI_Init.cpp
- [x] T385 annotate: src/slic3r/GUI/GUI_Init.hpp
- [x] T386 annotate: src/slic3r/GUI/GUI_ObjectLayers.cpp
- [x] T387 annotate: src/slic3r/GUI/GUI_ObjectLayers.hpp
- [x] T388 annotate: src/slic3r/GUI/GUI_ObjectList.cpp
- [x] T389 annotate: src/slic3r/GUI/GUI_ObjectList.hpp
- [x] T390 annotate: src/slic3r/GUI/GUI_ObjectSettings.cpp
- [x] T391 annotate: src/slic3r/GUI/GUI_ObjectSettings.hpp
- [x] T392 annotate: src/slic3r/GUI/GUI_ObjectTable.cpp
- [x] T393 annotate: src/slic3r/GUI/GUI_ObjectTable.hpp
- [x] T394 annotate: src/slic3r/GUI/GUI_ObjectTableSettings.cpp

## Phase 1 - Task T563 complete

- Task type: annotate
- File: src/slic3r/GUI/ReleaseNote.cpp
- Deliverables: src/slic3r/GUI/ReleaseNote.cpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 16 annotation blocks covering release-note display, plug-in/version update flows, secondary confirmation routing, print-error reconfiguration, IP setup threading, and failure handling
- Verification excerpt: `[THREAD] The network probe runs on a boost thread and posts results back through wx events and CallAfter.`
- Unity-impact summary:
  - Split the file into reusable modal controllers instead of one monolithic wx dialog source.
  - Move async network/image work behind main-thread marshaling in the Unity port.
  - Preserve the markdown/web preview behavior with a dedicated rich-text or webview component.
- Hazards found: P2 x3, P3 x2
- Git: annotate ReleaseNote dialogs
- Next recommended Phase 1 task: T564 annotate: src/slic3r/GUI/ReleaseNote.hpp
- [x] T395 annotate: src/slic3r/GUI/GUI_ObjectTableSettings.hpp
- [x] T396 annotate: src/slic3r/GUI/GUI_Preview.cpp
- [x] T397 annotate: src/slic3r/GUI/GUI_Preview.hpp
- [x] T398 annotate: src/slic3r/GUI/GUI_Utils.cpp
- [x] T399 annotate: src/slic3r/GUI/GUI_Utils.hpp
- [x] T400 annotate: src/slic3r/GUI/HintNotification.cpp
- [x] T401 annotate: src/slic3r/GUI/HintNotification.hpp
- [x] T402 annotate: src/slic3r/GUI/HMS.cpp
- [x] T403 annotate: src/slic3r/GUI/HMS.hpp
- [x] T404 annotate: src/slic3r/GUI/HMSPanel.cpp
- [x] T405 annotate: src/slic3r/GUI/HMSPanel.hpp
- [x] T406 annotate: src/slic3r/GUI/HttpServer.cpp
- [x] T407 annotate: src/slic3r/GUI/HttpServer.hpp
- [x] T408 annotate: src/slic3r/GUI/I18N.cpp
- [x] T409 annotate: src/slic3r/GUI/I18N.hpp
- [x] T410 annotate: src/slic3r/GUI/IconManager.cpp
- [x] T411 annotate: src/slic3r/GUI/IconManager.hpp
- [x] T412 annotate: src/slic3r/GUI/ImageDPIFrame.cpp
- [x] T413 annotate: src/slic3r/GUI/ImageDPIFrame.hpp
- [x] T414 annotate: src/slic3r/GUI/ImageGrid.cpp
- [x] T415 annotate: src/slic3r/GUI/ImGuiWrapper.cpp
- [x] T416 annotate: src/slic3r/GUI/ImGuiWrapper.hpp
- [x] T417 annotate: src/slic3r/GUI/IMSlider.cpp
- [x] T418 annotate: src/slic3r/GUI/IMSlider.hpp
- [x] T419 annotate: src/slic3r/GUI/IMToolbar.cpp
- [x] T420 annotate: src/slic3r/GUI/IMToolbar.hpp
- [x] T421 annotate: src/slic3r/GUI/InstanceCheck.cpp
- [x] T422 annotate: src/slic3r/GUI/InstanceCheck.hpp
- [x] T423 annotate: src/slic3r/GUI/Jobs/ArrangeJob.cpp
- [x] T424 annotate: src/slic3r/GUI/Jobs/ArrangeJob.hpp
- [x] T425 annotate: src/slic3r/GUI/Jobs/BindJob.cpp
- [x] T426 annotate: src/slic3r/GUI/Jobs/BindJob.hpp
- [x] T427 annotate: src/slic3r/GUI/Jobs/BoostThreadWorker.cpp
- [x] T428 annotate: src/slic3r/GUI/Jobs/BoostThreadWorker.hpp
- [x] T429 annotate: src/slic3r/GUI/Jobs/BusyCursorJob.hpp
- [x] T430 annotate: src/slic3r/GUI/Jobs/CreateFontNameImageJob.cpp
- [x] T431 annotate: src/slic3r/GUI/Jobs/CreateFontNameImageJob.hpp
- [x] T432 annotate: src/slic3r/GUI/Jobs/CreateFontStyleImagesJob.cpp
- [x] T433 annotate: src/slic3r/GUI/Jobs/CreateFontStyleImagesJob.hpp
- [x] T434 annotate: src/slic3r/GUI/Jobs/EmbossJob.cpp
- [x] T435 annotate: src/slic3r/GUI/Jobs/EmbossJob.hpp
- [x] T436 annotate: src/slic3r/GUI/Jobs/FillBedJob.cpp
- [x] T437 annotate: src/slic3r/GUI/Jobs/FillBedJob.hpp
- [x] T438 annotate: src/slic3r/GUI/Jobs/Job.hpp
- [x] T439 annotate: src/slic3r/GUI/Jobs/NotificationProgressIndicator.cpp
- [x] T440 annotate: src/slic3r/GUI/Jobs/NotificationProgressIndicator.hpp
- [x] T442 annotate: src/slic3r/GUI/Jobs/OAuthJob.hpp
- [x] T443 annotate: src/slic3r/GUI/Jobs/OrientJob.cpp
- [x] T444 annotate: src/slic3r/GUI/Jobs/OrientJob.hpp
- [x] T445 annotate: src/slic3r/GUI/Jobs/PlaterWorker.hpp
- [x] T446 annotate: src/slic3r/GUI/Jobs/PrintJob.cpp
- [x] T447 annotate: src/slic3r/GUI/Jobs/PrintJob.hpp
- [x] T448 annotate: src/slic3r/GUI/Jobs/ProgressIndicator.hpp
- [x] T449 annotate: src/slic3r/GUI/Jobs/RotoptimizeJob.cpp
- [x] T450 annotate: src/slic3r/GUI/Jobs/RotoptimizeJob.hpp
- [x] T451 annotate: src/slic3r/GUI/Jobs/SendJob.cpp
- [x] T452 annotate: src/slic3r/GUI/Jobs/SendJob.hpp
- [x] T453 annotate: src/slic3r/GUI/Jobs/SLAImportDialog.hpp
- [x] T454 annotate: src/slic3r/GUI/Jobs/SLAImportJob.cpp
- [x] T455 annotate: src/slic3r/GUI/Jobs/SLAImportJob.hpp
- [x] T456 annotate: src/slic3r/GUI/Jobs/ThreadSafeQueue.hpp
- [x] T457 annotate: src/slic3r/GUI/Jobs/UpgradeNetworkJob.cpp
- [x] T458 annotate: src/slic3r/GUI/Jobs/UpgradeNetworkJob.hpp
- [x] annotate: src/slic3r/GUI/Jobs/Worker.hpp
- [x] T460 annotate: src/slic3r/GUI/KBShortcutsDialog.cpp
- [x] T461 annotate: src/slic3r/GUI/KBShortcutsDialog.hpp
- [x] T462 annotate: src/slic3r/GUI/LibVGCode/LibVGCodeWrapper.cpp
- [x] T463 annotate: src/slic3r/GUI/LibVGCode/LibVGCodeWrapper.hpp
- [x] T464 annotate: src/slic3r/GUI/MainFrame.cpp
- [x] T465 annotate: src/slic3r/GUI/MainFrame.hpp
- [x] annotate: src/slic3r/GUI/MarkdownTip.cpp
- [x] T467 annotate: src/slic3r/GUI/MarkdownTip.hpp
- [x] T468 annotate: src/slic3r/GUI/MediaFilePanel.cpp
- [x] T469 annotate: src/slic3r/GUI/MediaPlayCtrl.cpp
- [x] T470 annotate: src/slic3r/GUI/MeshUtils.cpp
- [x] T471 annotate: src/slic3r/GUI/MeshUtils.hpp
- [x] T472 annotate: src/slic3r/GUI/ModelMall.cpp
- [x] T473 annotate: src/slic3r/GUI/ModelMall.hpp
- [x] T474 annotate: src/slic3r/GUI/MonitorBasePanel.cpp
- [x] T475 annotate: src/slic3r/GUI/Monitor.cpp
- [x] T476 annotate: src/slic3r/GUI/Monitor.hpp
- [x] T477 annotate: src/slic3r/GUI/MonitorPage.cpp
- [x] T478 annotate: src/slic3r/GUI/MonitorPage.hpp
- [x] T479 annotate: src/slic3r/GUI/Mouse3DController.cpp
- [x] T480 annotate: src/slic3r/GUI/Mouse3DController.hpp
- [x] T481 annotate: src/slic3r/GUI/MsgDialog.cpp
- [x] T483 annotate: src/slic3r/GUI/MultiMachine.cpp
- [x] T484 annotate: src/slic3r/GUI/MultiMachine.hpp
- [x] T485 annotate: src/slic3r/GUI/MultiMachineManagerPage.cpp
- [x] T486 annotate: src/slic3r/GUI/MultiMachineManagerPage.hpp
- [x] T487 annotate: src/slic3r/GUI/MultiMachinePage.cpp
- [x] T489 annotate: src/slic3r/GUI/MultiPrintJob.cpp
- [x] T490 annotate: src/slic3r/GUI/MultiPrintJob.hpp
- [x] T491 annotate: src/slic3r/GUI/MultiSendMachineModel.cpp
- [x] T492 annotate: src/slic3r/GUI/MultiSendMachineModel.hpp
- [x] T494 annotate: src/slic3r/GUI/MultiTaskManagerPage.hpp
- [x] T495 annotate: src/slic3r/GUI/MultiTaskModel.cpp
- [x] T496 annotate: src/slic3r/GUI/MultiTaskModel.hpp
- [x] T498 annotate: src/slic3r/GUI/NetworkPluginDialog.hpp
- [x] T499 annotate: src/slic3r/GUI/NetworkTestDialog.cpp
- [x] T501 annotate: src/slic3r/GUI/Notebook.cpp
- [x] T502 annotate: src/slic3r/GUI/Notebook.hpp
- [x] T505 annotate: src/slic3r/GUI/OAuthDialog.cpp
- [x] T506 annotate: src/slic3r/GUI/OAuthDialog.hpp
- [x] T507 annotate: src/slic3r/GUI/ObjColorDialog.cpp
- [x] T508 annotate: src/slic3r/GUI/ObjColorDialog.hpp
- [x] T509 annotate: src/slic3r/GUI/ObjectDataViewModel.cpp
- [x] T510 annotate: src/slic3r/GUI/ObjectDataViewModel.hpp
- [x] T511 annotate: src/slic3r/GUI/OG_CustomCtrl.cpp
- [x] T512 annotate: src/slic3r/GUI/OG_CustomCtrl.hpp
- [x] T513 annotate: src/slic3r/GUI/OpenGLManager.cpp
- [x] T515 annotate: src/slic3r/GUI/OptionsGroup.cpp
- [x] T516 annotate: src/slic3r/GUI/OptionsGroup.hpp
- [x] T518 annotate: src/slic3r/GUI/ParamsDialog.hpp
- [x] T520 annotate: src/slic3r/GUI/ParamsPanel.hpp
- [x] T522 annotate: src/slic3r/GUI/PartPlate.hpp
- [x] T523 annotate: src/slic3r/GUI/PartSkipCommon.hpp
- [x] T524 annotate: src/slic3r/GUI/PartSkipDialog.cpp
- [x] T525 annotate: src/slic3r/GUI/PartSkipDialog.hpp
- [x] T526 annotate: src/slic3r/GUI/PhysicalPrinterDialog.cpp
- [x] T528 annotate: src/slic3r/GUI/Plater.cpp
- [x] T539 annotate: src/slic3r/GUI/PresetHints.hpp
- [x] T540 annotate: src/slic3r/GUI/PrinterCloudAuthDialog.cpp
- [x] T541 annotate: src/slic3r/GUI/PrinterCloudAuthDialog.hpp
- [x] T639 annotate: src/slic3r/GUI/Widgets/AMSControl.cpp
- [x] T647 annotate: src/slic3r/GUI/Widgets/Button.cpp
- [x] T648 annotate: src/slic3r/GUI/Widgets/Button.hpp
- [x] T649 annotate: src/slic3r/GUI/Widgets/CheckBox.cpp
- [x] T650 annotate: src/slic3r/GUI/Widgets/CheckBox.hpp
- [x] T651 annotate: src/slic3r/GUI/Widgets/ComboBox.cpp
- [x] T667 annotate: src/slic3r/GUI/Widgets/Label.cpp
- [x] T696 annotate: src/slic3r/GUI/Widgets/StateColor.hpp
- [x] T704 annotate: src/slic3r/GUI/Widgets/StaticLine.hpp
- [x] T723 annotate: src/slic3r/Utils/ASCIIFolding.hpp
- [x] T170 skip-trivial: src/slic3r/GUI/GUI_ObjectManipulation.hpp
- [x] annotate: src/slic3r/GUI/IMToolbar.cpp
- [x] T528 annotate: src/slic3r/GUI/Plater.cpp
- [x] annotate: src/slic3r/GUI/MsgDialog.cpp

### Remaining

- [ ] P0-T006: Main Window Class Identification
- [ ] T106 annotate: src/libvgcode/include/Viewer.hpp
- [ ] T132 annotate: src/libvgcode/src/ToolMarker.hpp
- [ ] T153 annotate: src/slic3r/GUI/PresetComboBoxes.cpp
- [~] T161 annotate: src/slic3r/GUI/DPIFrame.cpp
- [~] T162 annotate: src/slic3r/GUI/DPIFrame.hpp
- [~] T169 annotate: src/slic3r/GUI/GUI_ObjectManipulation.cpp
- [~] T170 annotate: src/slic3r/GUI/GUI_ObjectManipulation.hpp
- [ ] T172 annotate: src/slic3r/GUI/PartPlate.hpp
- [~] T173 annotate: src/slic3r/GUI/PalmTree.cpp
- [~] T174 annotate: src/slic3r/GUI/PalmTree.hpp
- [ ] T185 annotate: src/slic3r/GUI/BonjourDialog.cpp
- [ ] T187 annotate: src/slic3r/GUI/Jobs/Job.cpp
- [~] T189 annotate: src/slic3r/GUI/Jobs/JobList.cpp
- [~] T190 annotate: src/slic3r/GUI/Jobs/JobList.hpp
- [~] T191 annotate: src/slic3r/GUI/Jobs/LightJob.cpp
- [~] T192 annotate: src/slic3r/GUI/Jobs/LightJob.hpp
- [~] T193 annotate: src/slic3r/GUI/Jobs/MedialAxisJob.cpp
- [~] T194 annotate: src/slic3r/GUI/Jobs/MedialAxisJob.hpp
- [~] T195 annotate: src/slic3r/GUI/Jobs/RotoptJob.cpp
- [~] T196 annotate: src/slic3r/GUI/Jobs/RotoptJob.hpp
- [~] T199 annotate: src/slic3r/GUI/Jobs/SVGFileJob.cpp
- [~] T200 annotate: src/slic3r/GUI/Jobs/SVGFileJob.hpp
- [~] T201 annotate: src/slic3r/GUI/Files/SVG.cpp
- [~] T202 annotate: src/slic3r/GUI/Files/SVG.hpp
- [ ] T203 annotate: src/slic3r/GUI/3DScene.cpp
- [ ] T204 annotate: src/slic3r/GUI/Gizmos/3DScene.hpp
- [ ] T206 annotate: src/slic3r/GUI/Gizmos/GLGizmoBase.hpp
- [ ] T313 annotate: src/slic3r/GUI/Gizmos/GLGizmoCut.cpp
- [ ] T333 annotate: src/slic3r/GUI/Gizmos/GLGizmoMove.cpp
- [ ] T335 annotate: src/slic3r/GUI/Gizmos/GLGizmoPainterBase.cpp
- [ ] T342 annotate: src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp
- [ ] T343 annotate: src/slic3r/GUI/Gizmos/GLGizmoSeam.cpp
- [ ] T348 annotate: src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp
- [ ] T351 annotate: src/slic3r/GUI/Gizmos/GLGizmosManager.hpp
- [ ] T352 annotate: src/slic3r/GUI/Gizmos/GLGizmoSVG.cpp
- [~] T358 annotate: src/slic3r/GUI/GLModel.cpp
- [ ] T369 annotate: src/slic3r/GUI/GLToolbar.hpp
- [ ] T372 annotate: src/slic3r/GUI/GUI_AuxiliaryList.cpp
- [ ] T383 annotate: src/slic3r/GUI/GUI.hpp
- [~] T441 annotate: src/slic3r/GUI/Jobs/OAuthJob.cpp
- [ ] T482 annotate: src/slic3r/GUI/MsgDialog.hpp
- [ ] T488 annotate: src/slic3r/GUI/MultiMachinePage.hpp
- [ ] T493 annotate: src/slic3r/GUI/MultiTaskManagerPage.cpp
- [ ] T497 annotate: src/slic3r/GUI/NetworkPluginDialog.cpp
- [ ] T500 annotate: src/slic3r/GUI/NetworkTestDialog.hpp
- [ ] T503 annotate: src/slic3r/GUI/NotificationManager.cpp
- [ ] T504 annotate: src/slic3r/GUI/NotificationManager.hpp
- [ ] T514 annotate: src/slic3r/GUI/OpenGLManager.hpp
- [ ] T517 annotate: src/slic3r/GUI/ParamsDialog.cpp
- [ ] T519 annotate: src/slic3r/GUI/ParamsPanel.cpp
- [~] T521 annotate: src/slic3r/GUI/PartPlate.cpp
- [~] T527 annotate: src/slic3r/GUI/PhysicalPrinterDialog.hpp
- [ ] T529 annotate: src/slic3r/GUI/Plater.hpp
- [~] T530 annotate: src/slic3r/GUI/PlateSettingsDialog.cpp
- [ ] T531 annotate: src/slic3r/GUI/PlateSettingsDialog.hpp
- [ ] T532 annotate: src/slic3r/GUI/Preferences.cpp
- [~] T533 annotate: src/slic3r/GUI/Preferences.hpp
- [ ] T534 annotate: src/slic3r/GUI/PrePrintChecker.cpp
- [~] T535 annotate: src/slic3r/GUI/PrePrintChecker.hpp
- [~] T536 annotate: src/slic3r/GUI/PresetComboBoxes.cpp
- [~] T537 annotate: src/slic3r/GUI/PresetComboBoxes.hpp
- [~] T538 annotate: src/slic3r/GUI/PresetHints.cpp
- [~] T542 annotate: src/slic3r/GUI/Printer/PrinterFileSystem.cpp
- [ ] T543 annotate: src/slic3r/GUI/PrinterWebView.cpp
- [ ] T544 annotate: src/slic3r/GUI/PrinterWebView.hpp
- [~] T545 annotate: src/slic3r/GUI/PrintHostDialogs.cpp
- [~] T546 annotate: src/slic3r/GUI/PrintHostDialogs.hpp
- [ ] T547 annotate: src/slic3r/GUI/PrintOptionsDialog.cpp
- [ ] T548 annotate: src/slic3r/GUI/PrintOptionsDialog.hpp
- [ ] T549 annotate: src/slic3r/GUI/PrivacyUpdateDialog.cpp
- [ ] T550 annotate: src/slic3r/GUI/PrivacyUpdateDialog.hpp
- [ ] T551 annotate: src/slic3r/GUI/ProgressStatusBar.cpp
- [ ] T552 annotate: src/slic3r/GUI/ProgressStatusBar.hpp
- [~] T553 annotate: src/slic3r/GUI/Project.cpp
- [ ] T554 annotate: src/slic3r/GUI/ProjectDirtyStateManager.cpp
- [x] T555 annotate: src/slic3r/GUI/ProjectDirtyStateManager.hpp
- [ ] T556 annotate: src/slic3r/GUI/Project.hpp
- [ ] T557 annotate: src/slic3r/GUI/PublishDialog.cpp
- [ ] T558 annotate: src/slic3r/GUI/PublishDialog.hpp
- [ ] T559 annotate: src/slic3r/GUI/RammingChart.cpp
- [ ] T560 annotate: src/slic3r/GUI/RammingChart.hpp
- [ ] T561 annotate: src/slic3r/GUI/RecenterDialog.cpp
- [ ] T562 annotate: src/slic3r/GUI/RecenterDialog.hpp
- [ ] T563 annotate: src/slic3r/GUI/ReleaseNote.cpp
- [ ] T564 annotate: src/slic3r/GUI/ReleaseNote.hpp

## Phase 1 - Task T564 complete

- Task type: annotate
- File: src/slic3r/GUI/ReleaseNote.hpp
- Deliverables: src/slic3r/GUI/ReleaseNote.hpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 9 annotation blocks covering the release-note, plugin-update, version-update, confirmation, print-error, send-confirm, and IP-setup dialogs
- Verification excerpt: `[PORTING_HAZARD:P1] This dialog crosses UI/network/thread boundaries and depends on timed close behavior, so the port needs a real async state machine.`
- Unity-impact summary:
  - Map the webview-driven version/update flow to a UI Toolkit controller with an embedded WebView plugin or browser fallback.
  - Treat the IP onboarding path as an async wizard with explicit main-thread marshaling and cancellation.
  - Rebuild the confirmation/error dialogs as reusable modal panels with data-driven button rows.
- Hazards found: P1 x1, P2 x2, P3 x2
- Git: annotate ReleaseNote.hpp
- Next recommended Phase 1 task: T565 annotate: src/slic3r/GUI/RemovableDriveManager.cpp
- [ ] T565 annotate: src/slic3r/GUI/RemovableDriveManager.cpp
- [ ] T566 annotate: src/slic3r/GUI/RemovableDriveManager.hpp
- [ ] T567 annotate: src/slic3r/GUI/SafetyOptionsDialog.cpp
- [ ] T568 annotate: src/slic3r/GUI/SafetyOptionsDialog.hpp
- [ ] T569 annotate: src/slic3r/GUI/SavePresetDialog.cpp
- [ ] T570 annotate: src/slic3r/GUI/SavePresetDialog.hpp
- [ ] T571 annotate: src/slic3r/GUI/SceneRaycaster.cpp
- [ ] T572 annotate: src/slic3r/GUI/SceneRaycaster.hpp
- [ ] T573 annotate: src/slic3r/GUI/Search.cpp
- [ ] T574 annotate: src/slic3r/GUI/Search.hpp
- [ ] T575 annotate: src/slic3r/GUI/Selection.cpp
- [ ] T576 annotate: src/slic3r/GUI/Selection.hpp
- [ ] T577 annotate: src/slic3r/GUI/SelectMachine.cpp
- [ ] T578 annotate: src/slic3r/GUI/SelectMachine.hpp
- [ ] T579 annotate: src/slic3r/GUI/SelectMachinePop.cpp
- [ ] T580 annotate: src/slic3r/GUI/SelectMachinePop.hpp
- [ ] T581 annotate: src/slic3r/GUI/SendMultiMachinePage.cpp
- [ ] T582 annotate: src/slic3r/GUI/SendMultiMachinePage.hpp
- [ ] T583 annotate: src/slic3r/GUI/SendSystemInfoDialog.cpp
- [ ] T584 annotate: src/slic3r/GUI/SendSystemInfoDialog.hpp
- [ ] T585 annotate: src/slic3r/GUI/SendToPrinter.cpp
- [ ] T586 annotate: src/slic3r/GUI/SendToPrinter.hpp
- [ ] T587 annotate: src/slic3r/GUI/SingleChoiceDialog.cpp
- [ ] T588 annotate: src/slic3r/GUI/SingleChoiceDialog.hpp
- [ ] T589 annotate: src/slic3r/GUI/SkipPartCanvas.cpp
- [ ] T590 annotate: src/slic3r/GUI/SkipPartCanvas.hpp
- [ ] T591 annotate: src/slic3r/GUI/SliceInfoPanel.cpp
- [ ] T592 annotate: src/slic3r/GUI/SliceInfoPanel.hpp
- [ ] T593 annotate: src/slic3r/GUI/SlicingProgressNotification.cpp
- [ ] T594 annotate: src/slic3r/GUI/SlicingProgressNotification.hpp
- [x] T595 annotate: src/slic3r/GUI/StatusPanel.cpp

## Phase 1 - Task T595 complete

- Task type: annotate
- File: src/slic3r/GUI/StatusPanel.cpp
- Deliverables: src/slic3r/GUI/StatusPanel.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md
- Substantive additions: 6 comment blocks covering file intent, extruder glyph state, AMS switching UI, printing task card, dashboard ownership, and async score thumbnails
- Verification excerpt: `[UNITY] Port this as a retained dashboard page with reusable child panels and a data-bound view model rather than one monolithic panel class.`
- Unity-impact summary: split the dashboard into reusable subviews; keep async thumbnail fetch/upload off the view tree; route printer actions through explicit commands
- Hazards found: P2 x1
- Git: Annotate StatusPanel.cpp for Unity port
- Next recommended Phase 1 task: T596 annotate: src/slic3r/GUI/StatusPanel.hpp
- [x] T595 annotate: src/slic3r/GUI/StatusPanel.cpp
- [ ] T596 annotate: src/slic3r/GUI/StatusPanel.hpp
- [ ] T597 annotate: src/slic3r/GUI/StepMeshDialog.cpp
- [ ] T598 annotate: src/slic3r/GUI/StepMeshDialog.hpp
- [ ] T599 annotate: src/slic3r/GUI/SurfaceDrag.cpp
- [ ] T600 annotate: src/slic3r/GUI/SurfaceDrag.hpp
- [ ] T601 annotate: src/slic3r/GUI/SyncAmsInfoDialog.cpp
- [ ] T602 annotate: src/slic3r/GUI/SyncAmsInfoDialog.hpp
- [ ] T603 annotate: src/slic3r/GUI/SysInfoDialog.cpp
- [ ] T604 annotate: src/slic3r/GUI/SysInfoDialog.hpp
- [ ] T605 annotate: src/slic3r/GUI/Tabbook.cpp
- [ ] T606 annotate: src/slic3r/GUI/Tabbook.hpp
- [ ] T607 annotate: src/slic3r/GUI/TabButton.cpp
- [ ] T608 annotate: src/slic3r/GUI/TabButton.hpp
- [ ] T609 annotate: src/slic3r/GUI/Tab.cpp
- [ ] T610 annotate: src/slic3r/GUI/Tab.hpp
- [ ] T611 annotate: src/slic3r/GUI/TaskManager.cpp
- [ ] T612 annotate: src/slic3r/GUI/TaskManager.hpp
- [ ] T613 annotate: src/slic3r/GUI/TextLines.cpp
- [ ] T614 annotate: src/slic3r/GUI/TextLines.hpp
- [ ] T615 annotate: src/slic3r/GUI/ThermalPreconditioningDialog.cpp
- [ ] T616 annotate: src/slic3r/GUI/ThermalPreconditioningDialog.hpp
- [ ] T617 annotate: src/slic3r/GUI/TickCode.cpp
- [ ] T618 annotate: src/slic3r/GUI/TickCode.hpp
- [ ] T619 annotate: src/slic3r/GUI/UnsavedChangesDialog.cpp
- [ ] T620 annotate: src/slic3r/GUI/UnsavedChangesDialog.hpp
- [ ] T621 annotate: src/slic3r/GUI/UpdateDialogs.cpp
- [ ] T622 annotate: src/slic3r/GUI/UpdateDialogs.hpp
- [ ] T623 annotate: src/slic3r/GUI/UpgradePanel.cpp
- [ ] T624 annotate: src/slic3r/GUI/UpgradePanel.hpp
- [ ] T625 annotate: src/slic3r/GUI/UserManager.cpp
- [ ] T626 annotate: src/slic3r/GUI/UserManager.hpp
- [ ] T627 annotate: src/slic3r/GUI/UserNotification.cpp
- [ ] T628 annotate: src/slic3r/GUI/UserNotification.hpp
- [ ] T629 annotate: src/slic3r/GUI/WebDownPluginDlg.cpp
- [ ] T630 annotate: src/slic3r/GUI/WebDownPluginDlg.hpp
- [ ] T631 annotate: src/slic3r/GUI/WebGuideDialog.cpp
- [ ] T632 annotate: src/slic3r/GUI/WebGuideDialog.hpp
- [ ] T633 annotate: src/slic3r/GUI/WebUpdatePlugin.cpp
- [ ] T634 annotate: src/slic3r/GUI/WebUpdatePlugin.hpp
- [ ] T635 annotate: src/slic3r/GUI/WebUserLoginDialog.cpp
- [ ] T636 annotate: src/slic3r/GUI/WebUserLoginDialog.hpp
- [ ] T637 annotate: src/slic3r/GUI/WebViewDialog.cpp
- [ ] T638 annotate: src/slic3r/GUI/WebViewDialog.hpp
- [ ] T640 annotate: src/slic3r/GUI/Widgets/AMSControl.hpp
- [ ] T641 annotate: src/slic3r/GUI/Widgets/AMSItem.cpp
- [ ] T642 annotate: src/slic3r/GUI/Widgets/AMSItem.hpp
- [ ] T643 annotate: src/slic3r/GUI/Widgets/AnimaController.cpp
- [ ] T644 annotate: src/slic3r/GUI/Widgets/AnimaController.hpp
- [ ] T645 annotate: src/slic3r/GUI/Widgets/AxisCtrlButton.cpp
- [ ] T646 annotate: src/slic3r/GUI/Widgets/AxisCtrlButton.hpp
- [ ] T652 annotate: src/slic3r/GUI/Widgets/ComboBox.hpp
- [ ] T653 annotate: src/slic3r/GUI/Widgets/DialogButtons.cpp
- [ ] T654 annotate: src/slic3r/GUI/Widgets/DialogButtons.hpp
- [ ] T655 annotate: src/slic3r/GUI/Widgets/DropDown.cpp
- [ ] T656 annotate: src/slic3r/GUI/Widgets/DropDown.hpp
- [ ] T657 annotate: src/slic3r/GUI/Widgets/ErrorMsgStaticText.cpp
- [ ] T658 annotate: src/slic3r/GUI/Widgets/ErrorMsgStaticText.hpp
- [ ] T659 annotate: src/slic3r/GUI/Widgets/FanControl.cpp
- [ ] T660 annotate: src/slic3r/GUI/Widgets/FanControl.hpp
- [ ] T661 annotate: src/slic3r/GUI/Widgets/FilamentLoad.cpp
- [ ] T662 annotate: src/slic3r/GUI/Widgets/FilamentLoad.hpp
- [ ] T663 annotate: src/slic3r/GUI/Widgets/HyperLink.cpp
- [ ] T664 annotate: src/slic3r/GUI/Widgets/HyperLink.hpp
- [ ] T665 annotate: src/slic3r/GUI/Widgets/ImageSwitchButton.cpp
- [ ] T666 annotate: src/slic3r/GUI/Widgets/ImageSwitchButton.hpp
- [ ] T668 annotate: src/slic3r/GUI/Widgets/LabeledStaticBox.cpp
- [ ] T669 annotate: src/slic3r/GUI/Widgets/LabeledStaticBox.hpp
- [ ] T670 annotate: src/slic3r/GUI/Widgets/Label.hpp
- [ ] T671 annotate: src/slic3r/GUI/Widgets/PopupWindow.cpp
- [ ] T672 annotate: src/slic3r/GUI/Widgets/PopupWindow.hpp
- [ ] T673 annotate: src/slic3r/GUI/Widgets/ProgressBar.cpp
- [ ] T674 annotate: src/slic3r/GUI/Widgets/ProgressBar.hpp
- [ ] T675 annotate: src/slic3r/GUI/Widgets/ProgressDialog.cpp
- [ ] T676 annotate: src/slic3r/GUI/Widgets/ProgressDialog.hpp
- [ ] T677 annotate: src/slic3r/GUI/Widgets/RadioBox.cpp
- [ ] T678 annotate: src/slic3r/GUI/Widgets/RadioBox.hpp
- [ ] T679 annotate: src/slic3r/GUI/Widgets/RadioGroup.cpp
- [ ] T680 annotate: src/slic3r/GUI/Widgets/RadioGroup.hpp
- [ ] T681 annotate: src/slic3r/GUI/Widgets/RoundedRectangle.cpp
- [ ] T682 annotate: src/slic3r/GUI/Widgets/RoundedRectangle.hpp
- [ ] T683 annotate: src/slic3r/GUI/Widgets/Scrollbar.cpp
- [ ] T684 annotate: src/slic3r/GUI/Widgets/Scrollbar.hpp
- [ ] T685 annotate: src/slic3r/GUI/Widgets/ScrolledWindow.cpp
- [ ] T686 annotate: src/slic3r/GUI/Widgets/ScrolledWindow.hpp
- [ ] T687 annotate: src/slic3r/GUI/Widgets/SideButton.cpp
- [ ] T688 annotate: src/slic3r/GUI/Widgets/SideButton.hpp
- [ ] T689 annotate: src/slic3r/GUI/Widgets/SideMenuPopup.cpp
- [ ] T690 annotate: src/slic3r/GUI/Widgets/SideMenuPopup.hpp
- [ ] T691 annotate: src/slic3r/GUI/Widgets/SideTools.cpp
- [ ] T692 annotate: src/slic3r/GUI/Widgets/SideTools.hpp
- [ ] T693 annotate: src/slic3r/GUI/Widgets/SpinInput.cpp
- [ ] T694 annotate: src/slic3r/GUI/Widgets/SpinInput.hpp
- [ ] T695 annotate: src/slic3r/GUI/Widgets/StateColor.cpp
- [ ] T697 annotate: src/slic3r/GUI/Widgets/StateHandler.cpp
- [ ] T698 annotate: src/slic3r/GUI/Widgets/StateHandler.hpp
- [ ] T699 annotate: src/slic3r/GUI/Widgets/StaticBox.cpp
- [ ] T700 annotate: src/slic3r/GUI/Widgets/StaticBox.hpp
- [ ] T701 annotate: src/slic3r/GUI/Widgets/StaticGroup.cpp
- [ ] T702 annotate: src/slic3r/GUI/Widgets/StaticGroup.hpp
- [ ] T703 annotate: src/slic3r/GUI/Widgets/StaticLine.cpp
- [ ] T705 annotate: src/slic3r/GUI/Widgets/StepCtrl.cpp
- [ ] T706 annotate: src/slic3r/GUI/Widgets/StepCtrl.hpp
- [ ] T707 annotate: src/slic3r/GUI/Widgets/SwitchButton.cpp
- [ ] T708 annotate: src/slic3r/GUI/Widgets/SwitchButton.hpp
- [ ] T709 annotate: src/slic3r/GUI/Widgets/TabCtrl.cpp
- [ ] T710 annotate: src/slic3r/GUI/Widgets/TabCtrl.hpp
- [ ] T711 annotate: src/slic3r/GUI/Widgets/TempInput.cpp
- [ ] T712 annotate: src/slic3r/GUI/Widgets/TempInput.hpp
- [ ] T713 annotate: src/slic3r/GUI/Widgets/TextInput.cpp
- [ ] T714 annotate: src/slic3r/GUI/Widgets/TextInput.hpp
- [ ] T715 annotate: src/slic3r/GUI/Widgets/WebView.cpp
- [ ] T716 annotate: src/slic3r/GUI/Widgets/WebView.hpp
- [ ] T717 annotate: src/slic3r/GUI/WipeTowerDialog.cpp
- [ ] T718 annotate: src/slic3r/GUI/WipeTowerDialog.hpp
- [ ] T719 annotate: src/slic3r/GUI/wxExtensions.cpp
- [ ] T720 annotate: src/slic3r/GUI/wxExtensions.hpp
- [ ] T721 annotate: src/slic3r/GUI/wxMediaCtrl2.cpp
- [ ] T722 annotate: src/slic3r/Utils/ASCIIFolding.cpp
- [ ] T724 annotate: src/slic3r/Utils/AstroBox.cpp
- [ ] T725 annotate: src/slic3r/Utils/AstroBox.hpp
- [ ] T726 annotate: src/slic3r/Utils/bambu_networking.hpp
- [ ] T727 annotate: src/slic3r/Utils/BBLCloudServiceAgent.cpp
- [ ] T728 annotate: src/slic3r/Utils/BBLCloudServiceAgent.hpp
- [ ] T729 annotate: src/slic3r/Utils/BBLNetworkPlugin.cpp
- [ ] T730 annotate: src/slic3r/Utils/BBLNetworkPlugin.hpp
- [ ] T731 annotate: src/slic3r/Utils/BBLPrinterAgent.cpp
- [ ] T732 annotate: src/slic3r/Utils/BBLPrinterAgent.hpp
- [ ] T733 annotate: src/slic3r/Utils/Bonjour.cpp
- [ ] T734 annotate: src/slic3r/Utils/Bonjour.hpp
- [ ] T735 annotate: src/slic3r/Utils/CalibUtils.cpp
- [ ] T736 annotate: src/slic3r/Utils/CalibUtils.hpp
- [ ] T737 annotate: src/slic3r/Utils/ColorSpaceConvert.cpp
- [ ] T738 annotate: src/slic3r/Utils/ColorSpaceConvert.hpp
- [ ] T739 annotate: src/slic3r/Utils/CrealityPrint.cpp
- [ ] T740 annotate: src/slic3r/Utils/CrealityPrint.hpp
- [ ] T741 annotate: src/slic3r/Utils/Duet.cpp
- [ ] T742 annotate: src/slic3r/Utils/Duet.hpp
- [ ] T743 annotate: src/slic3r/Utils/ElegooLink.cpp
- [ ] T744 annotate: src/slic3r/Utils/ElegooLink.hpp
- [ ] T745 annotate: src/slic3r/Utils/EmbossStyleManager.cpp
- [ ] T746 annotate: src/slic3r/Utils/EmbossStyleManager.hpp
- [ ] T747 annotate: src/slic3r/Utils/ESP3D.cpp
- [ ] T748 annotate: src/slic3r/Utils/ESP3D.hpp
- [ ] T749 annotate: src/slic3r/Utils/FileHelp.cpp
- [ ] T750 annotate: src/slic3r/Utils/FileHelp.hpp
- [ ] T751 annotate: src/slic3r/Utils/FileTransferUtils.cpp
- [ ] T752 annotate: src/slic3r/Utils/FileTransferUtils.hpp
- [ ] T753 annotate: src/slic3r/Utils/FixModelByWin10.cpp
- [ ] T754 annotate: src/slic3r/Utils/FixModelByWin10.hpp
- [ ] T755 annotate: src/slic3r/Utils/FlashAir.cpp
- [ ] T756 annotate: src/slic3r/Utils/FlashAir.hpp
- [ ] T757 annotate: src/slic3r/Utils/Flashforge.cpp
- [ ] T758 annotate: src/slic3r/Utils/Flashforge.hpp
- [ ] T759 annotate: src/slic3r/Utils/FontConfigHelp.cpp
- [ ] T760 annotate: src/slic3r/Utils/FontConfigHelp.hpp
- [ ] T761 annotate: src/slic3r/Utils/HexFile.cpp
- [ ] T762 annotate: src/slic3r/Utils/HexFile.hpp
- [ ] T763 annotate: src/slic3r/Utils/Http.cpp
- [ ] T764 annotate: src/slic3r/Utils/Http.hpp
- [ ] T765 annotate: src/slic3r/Utils/ICloudServiceAgent.hpp
- [ ] T766 annotate: src/slic3r/Utils/InstanceID.cpp
- [ ] T767 annotate: src/slic3r/Utils/InstanceID.hpp
- [ ] T768 annotate: src/slic3r/Utils/IPrinterAgent.hpp
- [ ] T769 annotate: src/slic3r/Utils/json_diff.cpp
- [ ] T770 annotate: src/slic3r/Utils/json_diff.hpp
- [ ] T771 annotate: src/slic3r/Utils/MacDarkMode.hpp
- [ ] T772 annotate: src/slic3r/Utils/minilzo_extension.cpp
- [ ] T773 annotate: src/slic3r/Utils/minilzo_extension.hpp
- [ ] T774 annotate: src/slic3r/Utils/MKS.cpp
- [ ] T775 annotate: src/slic3r/Utils/MKS.hpp
- [ ] T776 annotate: src/slic3r/Utils/MoonrakerPrinterAgent.cpp
- [ ] T777 annotate: src/slic3r/Utils/MoonrakerPrinterAgent.hpp
- [ ] T778 annotate: src/slic3r/Utils/NetworkAgent.cpp
- [ ] T779 annotate: src/slic3r/Utils/NetworkAgentFactory.cpp
- [ ] T780 annotate: src/slic3r/Utils/NetworkAgentFactory.hpp
- [ ] T781 annotate: src/slic3r/Utils/NetworkAgent.hpp
- [ ] T782 annotate: src/slic3r/Utils/Obico.cpp
- [ ] T783 annotate: src/slic3r/Utils/Obico.hpp
- [ ] T784 annotate: src/slic3r/Utils/OctoPrint.cpp
- [ ] T785 annotate: src/slic3r/Utils/OctoPrint.hpp
- [ ] T786 annotate: src/slic3r/Utils/OrcaCloudServiceAgent.cpp
- [ ] T787 annotate: src/slic3r/Utils/OrcaCloudServiceAgent.hpp
- [ ] T788 annotate: src/slic3r/Utils/OrcaPrinterAgent.cpp
- [ ] T789 annotate: src/slic3r/Utils/OrcaPrinterAgent.hpp
- [ ] T790 annotate: src/slic3r/Utils/PresetUpdater.cpp
- [ ] T791 annotate: src/slic3r/Utils/PresetUpdater.hpp
- [ ] T792 annotate: src/slic3r/Utils/PrintHost.cpp
- [ ] T793 annotate: src/slic3r/Utils/PrintHost.hpp
- [ ] T794 annotate: src/slic3r/Utils/Process.cpp
- [ ] T795 annotate: src/slic3r/Utils/Process.hpp
- [ ] T796 annotate: src/slic3r/Utils/ProfileDescription.hpp
- [ ] T797 annotate: src/slic3r/Utils/Profile.hpp
- [ ] T798 annotate: src/slic3r/Utils/QidiPrinterAgent.cpp
- [ ] T799 annotate: src/slic3r/Utils/QidiPrinterAgent.hpp
- [ ] T800 annotate: src/slic3r/Utils/RaycastManager.cpp
- [ ] T801 annotate: src/slic3r/Utils/RaycastManager.hpp
- [ ] T802 annotate: src/slic3r/Utils/Repetier.cpp
- [ ] T803 annotate: src/slic3r/Utils/Repetier.hpp
- [ ] T804 annotate: src/slic3r/Utils/RetinaHelper.hpp
- [ ] T805 annotate: src/slic3r/Utils/Serial.cpp
- [ ] T806 annotate: src/slic3r/Utils/Serial.hpp
- [ ] T807 annotate: src/slic3r/Utils/SerialMessage.hpp
- [ ] T808 annotate: src/slic3r/Utils/SerialMessageType.hpp
- [ ] T809 annotate: src/slic3r/Utils/SimplyPrint.cpp
- [ ] T810 annotate: src/slic3r/Utils/SimplyPrint.hpp
- [ ] T811 annotate: src/slic3r/Utils/SnapmakerPrinterAgent.cpp
- [ ] T812 annotate: src/slic3r/Utils/SnapmakerPrinterAgent.hpp
- [ ] T813 annotate: src/slic3r/Utils/TCPConsole.cpp
- [ ] T814 annotate: src/slic3r/Utils/TCPConsole.hpp
- [ ] T815 annotate: src/slic3r/Utils/UndoRedo.cpp
- [ ] T816 annotate: src/slic3r/Utils/UndoRedo.hpp
- [ ] T817 annotate: src/slic3r/Utils/WebSocketClient.hpp
- [ ] T818 annotate: src/slic3r/Utils/WxFontUtils.cpp
- [ ] T819 annotate: src/slic3r/Utils/WxFontUtils.hpp
- [ ] annotate: src/slic3r/GUI/2DBed.cpp
- [~] annotate: src/slic3r/GUI/2DBed.hpp
- [~] annotate: src/slic3r/GUI/3DBed.cpp
- [~] annotate: src/slic3r/GUI/AboutDialog.cpp
- [~] annotate: src/slic3r/GUI/AboutDialog.hpp

## Phase 1 - Task T557 complete

- Task type: annotate
- File: src/slic3r/GUI/PublishDialog.cpp
- Deliverables: src/slic3r/GUI/PublishDialog.cpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 8 annotation blocks covering modal lifecycle, queued publish events, cancel/reset state, reentrant progress callbacks, step rail construction, and DPI behavior
- Verification excerpt: `[PORTING_HAZARD:P2] The implementation relies on wx event-loop reentry (`YieldFor`) during a`
- Unity-impact summary:
  - Model this as a modal progress state machine backed by async/coroutine orchestration.
  - Route publish start/stop through a controller layer rather than direct widget-to-Plater ownership.
  - Replace wx steprail/sizer composition with a Unity stepper/list UI and main-thread marshaling.
- Hazards found: P2 x1, P3 x0
- Git: Annotate publish dialog workflow
- Next recommended Phase 1 task: T558 annotate: src/slic3r/GUI/PublishDialog.hpp

## Phase 1 - Task T562 complete

- Task type: annotate
- File: src/slic3r/GUI/RecenterDialog.hpp
- Deliverables: `src/slic3r/GUI/RecenterDialog.hpp`, `.ralph/ralph-tasks.md`, `.ralph/agent/handoff.md`, `.ralph/agent/scratchpad.md`
- Substantive additions: 5 annotation blocks covering class intent, member ownership, event handlers, Unity mapping, and DPI/layout hazard
- Verification excerpt: `[UNITY] Port as a modal confirmation panel/controller with a shared icon asset, explicit button callbacks, and a scale-change relayout hook.`
- Unity-impact summary: modal controller with explicit confirm/close actions; layout-driven replacement for pixel-measurement wrapping; DPI-change hook must refresh shared art and relayout
- Hazards found: P2: 1
- Git: Annotate RecenterDialog header for Unity port
- Next recommended Phase 1 task: T563 annotate `src/slic3r/GUI/ReleaseNote.cpp`

## Key Files

Recently modified:

- `.ralph/agent/handoff.md`
- `.ralph/agent/memories.md`
- `.ralph/agent/scratchpad.md`
- `.ralph/agent/summary.md`
- `.ralph/agent/tasks.jsonl`
- `.ralph/current-events`
- `.ralph/current-loop-id`
- `.ralph/events-20260326-071218.jsonl`
- `.ralph/events-20260326-075232.jsonl`
- `.ralph/history.jsonl`

## Next Session

The following prompt can be used to continue where this session left off:

```
Continue the previous work. Remaining tasks (336):
- P0-T006: Main Window Class Identification
- T106 annotate: src/libvgcode/include/Viewer.hpp
- T132 annotate: src/libvgcode/src/ToolMarker.hpp
- T153 annotate: src/slic3r/GUI/PresetComboBoxes.cpp
- T161 annotate: src/slic3r/GUI/DPIFrame.cpp
- T162 annotate: src/slic3r/GUI/DPIFrame.hpp
- T169 annotate: src/slic3r/GUI/GUI_ObjectManipulation.cpp
- T170 annotate: src/slic3r/GUI/GUI_ObjectManipulation.hpp
- T172 annotate: src/slic3r/GUI/PartPlate.hpp
- T173 annotate: src/slic3r/GUI/PalmTree.cpp
- T174 annotate: src/slic3r/GUI/PalmTree.hpp
- T185 annotate: src/slic3r/GUI/BonjourDialog.cpp
- T187 annotate: src/slic3r/GUI/Jobs/Job.cpp
- T189 annotate: src/slic3r/GUI/Jobs/JobList.cpp
- T190 annotate: src/slic3r/GUI/Jobs/JobList.hpp
- T191 annotate: src/slic3r/GUI/Jobs/LightJob.cpp
- T192 annotate: src/slic3r/GUI/Jobs/LightJob.hpp
- T193 annotate: src/slic3r/GUI/Jobs/MedialAxisJob.cpp
- T194 annotate: src/slic3r/GUI/Jobs/MedialAxisJob.hpp
- T195 annotate: src/slic3r/GUI/Jobs/RotoptJob.cpp
- T196 annotate: src/slic3r/GUI/Jobs/RotoptJob.hpp
- T199 annotate: src/slic3r/GUI/Jobs/SVGFileJob.cpp
- T200 annotate: src/slic3r/GUI/Jobs/SVGFileJob.hpp
- T201 annotate: src/slic3r/GUI/Files/SVG.cpp
- T202 annotate: src/slic3r/GUI/Files/SVG.hpp
- T203 annotate: src/slic3r/GUI/3DScene.cpp
- T204 annotate: src/slic3r/GUI/Gizmos/3DScene.hpp
- T206 annotate: src/slic3r/GUI/Gizmos/GLGizmoBase.hpp
- T313 annotate: src/slic3r/GUI/Gizmos/GLGizmoCut.cpp
- T333 annotate: src/slic3r/GUI/Gizmos/GLGizmoMove.cpp
- T335 annotate: src/slic3r/GUI/Gizmos/GLGizmoPainterBase.cpp
- T342 annotate: src/slic3r/GUI/Gizmos/GLGizmosCommon.hpp
- T343 annotate: src/slic3r/GUI/Gizmos/GLGizmoSeam.cpp
- T348 annotate: src/slic3r/GUI/Gizmos/GLGizmoSlaSupports.cpp
- T351 annotate: src/slic3r/GUI/Gizmos/GLGizmosManager.hpp
- T352 annotate: src/slic3r/GUI/Gizmos/GLGizmoSVG.cpp
- T358 annotate: src/slic3r/GUI/GLModel.cpp
- T369 annotate: src/slic3r/GUI/GLToolbar.hpp
- T372 annotate: src/slic3r/GUI/GUI_AuxiliaryList.cpp
- T383 annotate: src/slic3r/GUI/GUI.hpp
- T441 annotate: src/slic3r/GUI/Jobs/OAuthJob.cpp
- T482 annotate: src/slic3r/GUI/MsgDialog.hpp
- T488 annotate: src/slic3r/GUI/MultiMachinePage.hpp
- T493 annotate: src/slic3r/GUI/MultiTaskManagerPage.cpp
- T497 annotate: src/slic3r/GUI/NetworkPluginDialog.cpp
- T500 annotate: src/slic3r/GUI/NetworkTestDialog.hpp
- T503 annotate: src/slic3r/GUI/NotificationManager.cpp
- T504 annotate: src/slic3r/GUI/NotificationManager.hpp
- T514 annotate: src/slic3r/GUI/OpenGLManager.hpp
- T517 annotate: src/slic3r/GUI/ParamsDialog.cpp
- T519 annotate: src/slic3r/GUI/ParamsPanel.cpp
- T521 annotate: src/slic3r/GUI/PartPlate.cpp
- T527 annotate: src/slic3r/GUI/PhysicalPrinterDialog.hpp
- T529 annotate: src/slic3r/GUI/Plater.hpp
- T530 annotate: src/slic3r/GUI/PlateSettingsDialog.cpp
- T531 annotate: src/slic3r/GUI/PlateSettingsDialog.hpp
- T532 annotate: src/slic3r/GUI/Preferences.cpp
- T533 annotate: src/slic3r/GUI/Preferences.hpp
- T534 annotate: src/slic3r/GUI/PrePrintChecker.cpp
- T535 annotate: src/slic3r/GUI/PrePrintChecker.hpp
- T536 annotate: src/slic3r/GUI/PresetComboBoxes.cpp
- T537 annotate: src/slic3r/GUI/PresetComboBoxes.hpp
- T538 annotate: src/slic3r/GUI/PresetHints.cpp
- T542 annotate: src/slic3r/GUI/Printer/PrinterFileSystem.cpp
- T543 annotate: src/slic3r/GUI/PrinterWebView.cpp
- T544 annotate: src/slic3r/GUI/PrinterWebView.hpp
- T545 annotate: src/slic3r/GUI/PrintHostDialogs.cpp
- T546 annotate: src/slic3r/GUI/PrintHostDialogs.hpp
- T547 annotate: src/slic3r/GUI/PrintOptionsDialog.cpp
- T548 annotate: src/slic3r/GUI/PrintOptionsDialog.hpp
- T549 annotate: src/slic3r/GUI/PrivacyUpdateDialog.cpp
- T550 annotate: src/slic3r/GUI/PrivacyUpdateDialog.hpp
- T551 annotate: src/slic3r/GUI/ProgressStatusBar.cpp
- T552 annotate: src/slic3r/GUI/ProgressStatusBar.hpp
- T553 annotate: src/slic3r/GUI/Project.cpp
- T554 annotate: src/slic3r/GUI/ProjectDirtyStateManager.cpp
- T555 annotate: src/slic3r/GUI/ProjectDirtyStateManager.hpp
- T556 annotate: src/slic3r/GUI/Project.hpp
- T557 annotate: src/slic3r/GUI/PublishDialog.cpp
- T558 annotate: src/slic3r/GUI/PublishDialog.hpp
- T559 annotate: src/slic3r/GUI/RammingChart.cpp
- T560 annotate: src/slic3r/GUI/RammingChart.hpp
- T561 annotate: src/slic3r/GUI/RecenterDialog.cpp
- T562 annotate: src/slic3r/GUI/RecenterDialog.hpp
- T563 annotate: src/slic3r/GUI/ReleaseNote.cpp
- T564 annotate: src/slic3r/GUI/ReleaseNote.hpp
- T565 annotate: src/slic3r/GUI/RemovableDriveManager.cpp
- T566 annotate: src/slic3r/GUI/RemovableDriveManager.hpp
- T567 annotate: src/slic3r/GUI/SafetyOptionsDialog.cpp
- T568 annotate: src/slic3r/GUI/SafetyOptionsDialog.hpp
- T569 annotate: src/slic3r/GUI/SavePresetDialog.cpp
- T570 annotate: src/slic3r/GUI/SavePresetDialog.hpp
- T571 annotate: src/slic3r/GUI/SceneRaycaster.cpp
- T572 annotate: src/slic3r/GUI/SceneRaycaster.hpp
- T573 annotate: src/slic3r/GUI/Search.cpp
- T574 annotate: src/slic3r/GUI/Search.hpp
- T575 annotate: src/slic3r/GUI/Selection.cpp
- T576 annotate: src/slic3r/GUI/Selection.hpp
- T577 annotate: src/slic3r/GUI/SelectMachine.cpp
- T578 annotate: src/slic3r/GUI/SelectMachine.hpp
- T579 annotate: src/slic3r/GUI/SelectMachinePop.cpp
- T580 annotate: src/slic3r/GUI/SelectMachinePop.hpp
- T581 annotate: src/slic3r/GUI/SendMultiMachinePage.cpp
- T582 annotate: src/slic3r/GUI/SendMultiMachinePage.hpp
- T583 annotate: src/slic3r/GUI/SendSystemInfoDialog.cpp
- T584 annotate: src/slic3r/GUI/SendSystemInfoDialog.hpp
- T585 annotate: src/slic3r/GUI/SendToPrinter.cpp
- T586 annotate: src/slic3r/GUI/SendToPrinter.hpp
- T587 annotate: src/slic3r/GUI/SingleChoiceDialog.cpp
- T588 annotate: src/slic3r/GUI/SingleChoiceDialog.hpp
- T589 annotate: src/slic3r/GUI/SkipPartCanvas.cpp
- T590 annotate: src/slic3r/GUI/SkipPartCanvas.hpp
- T591 annotate: src/slic3r/GUI/SliceInfoPanel.cpp
- T592 annotate: src/slic3r/GUI/SliceInfoPanel.hpp
- T593 annotate: src/slic3r/GUI/SlicingProgressNotification.cpp
- T594 annotate: src/slic3r/GUI/SlicingProgressNotification.hpp
- [x] T595 annotate: src/slic3r/GUI/StatusPanel.cpp
- T596 annotate: src/slic3r/GUI/StatusPanel.hpp
- T597 annotate: src/slic3r/GUI/StepMeshDialog.cpp
- T598 annotate: src/slic3r/GUI/StepMeshDialog.hpp
- T599 annotate: src/slic3r/GUI/SurfaceDrag.cpp
- T600 annotate: src/slic3r/GUI/SurfaceDrag.hpp
- T601 annotate: src/slic3r/GUI/SyncAmsInfoDialog.cpp
- T602 annotate: src/slic3r/GUI/SyncAmsInfoDialog.hpp
- T603 annotate: src/slic3r/GUI/SysInfoDialog.cpp
- T604 annotate: src/slic3r/GUI/SysInfoDialog.hpp
- T605 annotate: src/slic3r/GUI/Tabbook.cpp
- T606 annotate: src/slic3r/GUI/Tabbook.hpp
- T607 annotate: src/slic3r/GUI/TabButton.cpp
- T608 annotate: src/slic3r/GUI/TabButton.hpp
- T609 annotate: src/slic3r/GUI/Tab.cpp
- T610 annotate: src/slic3r/GUI/Tab.hpp
- T611 annotate: src/slic3r/GUI/TaskManager.cpp
- T612 annotate: src/slic3r/GUI/TaskManager.hpp
- T613 annotate: src/slic3r/GUI/TextLines.cpp
- T614 annotate: src/slic3r/GUI/TextLines.hpp
- T615 annotate: src/slic3r/GUI/ThermalPreconditioningDialog.cpp
- T616 annotate: src/slic3r/GUI/ThermalPreconditioningDialog.hpp
- T617 annotate: src/slic3r/GUI/TickCode.cpp
- T618 annotate: src/slic3r/GUI/TickCode.hpp
- T619 annotate: src/slic3r/GUI/UnsavedChangesDialog.cpp
- T620 annotate: src/slic3r/GUI/UnsavedChangesDialog.hpp
- T621 annotate: src/slic3r/GUI/UpdateDialogs.cpp
- T622 annotate: src/slic3r/GUI/UpdateDialogs.hpp
- T623 annotate: src/slic3r/GUI/UpgradePanel.cpp
- T624 annotate: src/slic3r/GUI/UpgradePanel.hpp
- T625 annotate: src/slic3r/GUI/UserManager.cpp
- T626 annotate: src/slic3r/GUI/UserManager.hpp
- T627 annotate: src/slic3r/GUI/UserNotification.cpp
- T628 annotate: src/slic3r/GUI/UserNotification.hpp
- T629 annotate: src/slic3r/GUI/WebDownPluginDlg.cpp
- T630 annotate: src/slic3r/GUI/WebDownPluginDlg.hpp
- T631 annotate: src/slic3r/GUI/WebGuideDialog.cpp
- T632 annotate: src/slic3r/GUI/WebGuideDialog.hpp
- T633 annotate: src/slic3r/GUI/WebUpdatePlugin.cpp
- T634 annotate: src/slic3r/GUI/WebUpdatePlugin.hpp
- T635 annotate: src/slic3r/GUI/WebUserLoginDialog.cpp
- T636 annotate: src/slic3r/GUI/WebUserLoginDialog.hpp
- T637 annotate: src/slic3r/GUI/WebViewDialog.cpp
- T638 annotate: src/slic3r/GUI/WebViewDialog.hpp
- T640 annotate: src/slic3r/GUI/Widgets/AMSControl.hpp
- T641 annotate: src/slic3r/GUI/Widgets/AMSItem.cpp
- T642 annotate: src/slic3r/GUI/Widgets/AMSItem.hpp
- T643 annotate: src/slic3r/GUI/Widgets/AnimaController.cpp
- T644 annotate: src/slic3r/GUI/Widgets/AnimaController.hpp
- T645 annotate: src/slic3r/GUI/Widgets/AxisCtrlButton.cpp
- T646 annotate: src/slic3r/GUI/Widgets/AxisCtrlButton.hpp
- T652 annotate: src/slic3r/GUI/Widgets/ComboBox.hpp
- T653 annotate: src/slic3r/GUI/Widgets/DialogButtons.cpp
- T654 annotate: src/slic3r/GUI/Widgets/DialogButtons.hpp
- T655 annotate: src/slic3r/GUI/Widgets/DropDown.cpp
- T656 annotate: src/slic3r/GUI/Widgets/DropDown.hpp
- T657 annotate: src/slic3r/GUI/Widgets/ErrorMsgStaticText.cpp
- T658 annotate: src/slic3r/GUI/Widgets/ErrorMsgStaticText.hpp
- T659 annotate: src/slic3r/GUI/Widgets/FanControl.cpp
- T660 annotate: src/slic3r/GUI/Widgets/FanControl.hpp
- T661 annotate: src/slic3r/GUI/Widgets/FilamentLoad.cpp
- T662 annotate: src/slic3r/GUI/Widgets/FilamentLoad.hpp
- T663 annotate: src/slic3r/GUI/Widgets/HyperLink.cpp
- T664 annotate: src/slic3r/GUI/Widgets/HyperLink.hpp
- T665 annotate: src/slic3r/GUI/Widgets/ImageSwitchButton.cpp
- T666 annotate: src/slic3r/GUI/Widgets/ImageSwitchButton.hpp
- T668 annotate: src/slic3r/GUI/Widgets/LabeledStaticBox.cpp
- T669 annotate: src/slic3r/GUI/Widgets/LabeledStaticBox.hpp
- T670 annotate: src/slic3r/GUI/Widgets/Label.hpp
- T671 annotate: src/slic3r/GUI/Widgets/PopupWindow.cpp
- T672 annotate: src/slic3r/GUI/Widgets/PopupWindow.hpp
- T673 annotate: src/slic3r/GUI/Widgets/ProgressBar.cpp
- T674 annotate: src/slic3r/GUI/Widgets/ProgressBar.hpp
- T675 annotate: src/slic3r/GUI/Widgets/ProgressDialog.cpp
- T676 annotate: src/slic3r/GUI/Widgets/ProgressDialog.hpp
- T677 annotate: src/slic3r/GUI/Widgets/RadioBox.cpp
- T678 annotate: src/slic3r/GUI/Widgets/RadioBox.hpp
- T679 annotate: src/slic3r/GUI/Widgets/RadioGroup.cpp
- T680 annotate: src/slic3r/GUI/Widgets/RadioGroup.hpp
- T681 annotate: src/slic3r/GUI/Widgets/RoundedRectangle.cpp
- T682 annotate: src/slic3r/GUI/Widgets/RoundedRectangle.hpp
- T683 annotate: src/slic3r/GUI/Widgets/Scrollbar.cpp
- T684 annotate: src/slic3r/GUI/Widgets/Scrollbar.hpp
- T685 annotate: src/slic3r/GUI/Widgets/ScrolledWindow.cpp
- T686 annotate: src/slic3r/GUI/Widgets/ScrolledWindow.hpp
- T687 annotate: src/slic3r/GUI/Widgets/SideButton.cpp
- T688 annotate: src/slic3r/GUI/Widgets/SideButton.hpp
- T689 annotate: src/slic3r/GUI/Widgets/SideMenuPopup.cpp
- T690 annotate: src/slic3r/GUI/Widgets/SideMenuPopup.hpp
- T691 annotate: src/slic3r/GUI/Widgets/SideTools.cpp
- T692 annotate: src/slic3r/GUI/Widgets/SideTools.hpp
- T693 annotate: src/slic3r/GUI/Widgets/SpinInput.cpp
- T694 annotate: src/slic3r/GUI/Widgets/SpinInput.hpp
- T695 annotate: src/slic3r/GUI/Widgets/StateColor.cpp
- T697 annotate: src/slic3r/GUI/Widgets/StateHandler.cpp
- T698 annotate: src/slic3r/GUI/Widgets/StateHandler.hpp
- T699 annotate: src/slic3r/GUI/Widgets/StaticBox.cpp
- T700 annotate: src/slic3r/GUI/Widgets/StaticBox.hpp
- T701 annotate: src/slic3r/GUI/Widgets/StaticGroup.cpp
- T702 annotate: src/slic3r/GUI/Widgets/StaticGroup.hpp
- T703 annotate: src/slic3r/GUI/Widgets/StaticLine.cpp
- T705 annotate: src/slic3r/GUI/Widgets/StepCtrl.cpp
- T706 annotate: src/slic3r/GUI/Widgets/StepCtrl.hpp
- T707 annotate: src/slic3r/GUI/Widgets/SwitchButton.cpp
- T708 annotate: src/slic3r/GUI/Widgets/SwitchButton.hpp
- T709 annotate: src/slic3r/GUI/Widgets/TabCtrl.cpp
- T710 annotate: src/slic3r/GUI/Widgets/TabCtrl.hpp
- T711 annotate: src/slic3r/GUI/Widgets/TempInput.cpp
- T712 annotate: src/slic3r/GUI/Widgets/TempInput.hpp
- T713 annotate: src/slic3r/GUI/Widgets/TextInput.cpp
- T714 annotate: src/slic3r/GUI/Widgets/TextInput.hpp
- T715 annotate: src/slic3r/GUI/Widgets/WebView.cpp
- T716 annotate: src/slic3r/GUI/Widgets/WebView.hpp
- T717 annotate: src/slic3r/GUI/WipeTowerDialog.cpp
- T718 annotate: src/slic3r/GUI/WipeTowerDialog.hpp
- T719 annotate: src/slic3r/GUI/wxExtensions.cpp
- T720 annotate: src/slic3r/GUI/wxExtensions.hpp
- T721 annotate: src/slic3r/GUI/wxMediaCtrl2.cpp

## Phase 1 - Task T721 complete

- Task type: annotate
- File: src/slic3r/GUI/wxMediaCtrl2.cpp
- Deliverables: src/slic3r/GUI/wxMediaCtrl2.cpp, .ralph/agent/handoff.md, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md
- Substantive additions: 7 annotation blocks covering intent, state, events, threading, Unity mapping, and porting hazards
- Verification excerpt: [PORTING_HAZARD:P1] The Windows branch depends on WMP being installed and on a custom CLSID / registry registration for BambuSource
- Unity-impact summary: native-plugin-backed media bridge; UI-side state mirror for load/error; platform capability checks instead of registry mutation
- Hazards found: P1: 1, P2: 1
- Git: annotate wxMediaCtrl2 media shim
- Next recommended Phase 1 task: T720 src/slic3r/GUI/wxExtensions.hpp
- T722 annotate: src/slic3r/Utils/ASCIIFolding.cpp
- T724 annotate: src/slic3r/Utils/AstroBox.cpp
- T725 annotate: src/slic3r/Utils/AstroBox.hpp
- T726 annotate: src/slic3r/Utils/bambu_networking.hpp
- T727 annotate: src/slic3r/Utils/BBLCloudServiceAgent.cpp
- T728 annotate: src/slic3r/Utils/BBLCloudServiceAgent.hpp
- T729 annotate: src/slic3r/Utils/BBLNetworkPlugin.cpp
- T730 annotate: src/slic3r/Utils/BBLNetworkPlugin.hpp
- T731 annotate: src/slic3r/Utils/BBLPrinterAgent.cpp
- T732 annotate: src/slic3r/Utils/BBLPrinterAgent.hpp
- T733 annotate: src/slic3r/Utils/Bonjour.cpp
- T734 annotate: src/slic3r/Utils/Bonjour.hpp
- T735 annotate: src/slic3r/Utils/CalibUtils.cpp
- T736 annotate: src/slic3r/Utils/CalibUtils.hpp
- T737 annotate: src/slic3r/Utils/ColorSpaceConvert.cpp
- T738 annotate: src/slic3r/Utils/ColorSpaceConvert.hpp
- T739 annotate: src/slic3r/Utils/CrealityPrint.cpp
- T740 annotate: src/slic3r/Utils/CrealityPrint.hpp
- T741 annotate: src/slic3r/Utils/Duet.cpp
- T742 annotate: src/slic3r/Utils/Duet.hpp
- T743 annotate: src/slic3r/Utils/ElegooLink.cpp
- T744 annotate: src/slic3r/Utils/ElegooLink.hpp
- T745 annotate: src/slic3r/Utils/EmbossStyleManager.cpp
- T746 annotate: src/slic3r/Utils/EmbossStyleManager.hpp
- T747 annotate: src/slic3r/Utils/ESP3D.cpp
- T748 annotate: src/slic3r/Utils/ESP3D.hpp
- T749 annotate: src/slic3r/Utils/FileHelp.cpp
- T750 annotate: src/slic3r/Utils/FileHelp.hpp
- T751 annotate: src/slic3r/Utils/FileTransferUtils.cpp
- T752 annotate: src/slic3r/Utils/FileTransferUtils.hpp
- T753 annotate: src/slic3r/Utils/FixModelByWin10.cpp
- T754 annotate: src/slic3r/Utils/FixModelByWin10.hpp
- T755 annotate: src/slic3r/Utils/FlashAir.cpp
- T756 annotate: src/slic3r/Utils/FlashAir.hpp
- T757 annotate: src/slic3r/Utils/Flashforge.cpp
- T758 annotate: src/slic3r/Utils/Flashforge.hpp
- T759 annotate: src/slic3r/Utils/FontConfigHelp.cpp
- T760 annotate: src/slic3r/Utils/FontConfigHelp.hpp
- T761 annotate: src/slic3r/Utils/HexFile.cpp
- T762 annotate: src/slic3r/Utils/HexFile.hpp
- T763 annotate: src/slic3r/Utils/Http.cpp
- T764 annotate: src/slic3r/Utils/Http.hpp
- T765 annotate: src/slic3r/Utils/ICloudServiceAgent.hpp
- T766 annotate: src/slic3r/Utils/InstanceID.cpp
- T767 annotate: src/slic3r/Utils/InstanceID.hpp
- T768 annotate: src/slic3r/Utils/IPrinterAgent.hpp
- T769 annotate: src/slic3r/Utils/json_diff.cpp
- T770 annotate: src/slic3r/Utils/json_diff.hpp
- T771 annotate: src/slic3r/Utils/MacDarkMode.hpp
- T772 annotate: src/slic3r/Utils/minilzo_extension.cpp
- T773 annotate: src/slic3r/Utils/minilzo_extension.hpp
- T774 annotate: src/slic3r/Utils/MKS.cpp
- T775 annotate: src/slic3r/Utils/MKS.hpp
- T776 annotate: src/slic3r/Utils/MoonrakerPrinterAgent.cpp
- T777 annotate: src/slic3r/Utils/MoonrakerPrinterAgent.hpp
- T778 annotate: src/slic3r/Utils/NetworkAgent.cpp
- T779 annotate: src/slic3r/Utils/NetworkAgentFactory.cpp
- T780 annotate: src/slic3r/Utils/NetworkAgentFactory.hpp
- T781 annotate: src/slic3r/Utils/NetworkAgent.hpp
- T782 annotate: src/slic3r/Utils/Obico.cpp
- T783 annotate: src/slic3r/Utils/Obico.hpp
- T784 annotate: src/slic3r/Utils/OctoPrint.cpp
- T785 annotate: src/slic3r/Utils/OctoPrint.hpp
- T786 annotate: src/slic3r/Utils/OrcaCloudServiceAgent.cpp
- T787 annotate: src/slic3r/Utils/OrcaCloudServiceAgent.hpp
- T788 annotate: src/slic3r/Utils/OrcaPrinterAgent.cpp
- T789 annotate: src/slic3r/Utils/OrcaPrinterAgent.hpp
- T790 annotate: src/slic3r/Utils/PresetUpdater.cpp
- T791 annotate: src/slic3r/Utils/PresetUpdater.hpp
- T792 annotate: src/slic3r/Utils/PrintHost.cpp
- T793 annotate: src/slic3r/Utils/PrintHost.hpp
- T794 annotate: src/slic3r/Utils/Process.cpp
- T795 annotate: src/slic3r/Utils/Process.hpp
- T796 annotate: src/slic3r/Utils/ProfileDescription.hpp
- T797 annotate: src/slic3r/Utils/Profile.hpp
- T798 annotate: src/slic3r/Utils/QidiPrinterAgent.cpp
- T799 annotate: src/slic3r/Utils/QidiPrinterAgent.hpp
- T800 annotate: src/slic3r/Utils/RaycastManager.cpp
- T801 annotate: src/slic3r/Utils/RaycastManager.hpp
- T802 annotate: src/slic3r/Utils/Repetier.cpp
- T803 annotate: src/slic3r/Utils/Repetier.hpp
- T804 annotate: src/slic3r/Utils/RetinaHelper.hpp
- T805 annotate: src/slic3r/Utils/Serial.cpp
- T806 annotate: src/slic3r/Utils/Serial.hpp
- T807 annotate: src/slic3r/Utils/SerialMessage.hpp
- T808 annotate: src/slic3r/Utils/SerialMessageType.hpp
- T809 annotate: src/slic3r/Utils/SimplyPrint.cpp
- T810 annotate: src/slic3r/Utils/SimplyPrint.hpp
- T811 annotate: src/slic3r/Utils/SnapmakerPrinterAgent.cpp
- T812 annotate: src/slic3r/Utils/SnapmakerPrinterAgent.hpp
- T813 annotate: src/slic3r/Utils/TCPConsole.cpp
- T814 annotate: src/slic3r/Utils/TCPConsole.hpp
- T815 annotate: src/slic3r/Utils/UndoRedo.cpp
- T816 annotate: src/slic3r/Utils/UndoRedo.hpp
- T817 annotate: src/slic3r/Utils/WebSocketClient.hpp
- T818 annotate: src/slic3r/Utils/WxFontUtils.cpp
- T819 annotate: src/slic3r/Utils/WxFontUtils.hpp

## Phase 1 - Task T591 complete

- Task type: annotate
- File: src/slic3r/GUI/SliceInfoPanel.cpp
- Deliverables: src/slic3r/GUI/SliceInfoPanel.cpp
- Substantive additions: 7 comments covering popup intent, color decoding state, async thumbnail flow, stale-request cancellation, and DPI rescale behavior
- Verification excerpt: `// [THREAD] Thumbnail loading is asynchronous; on completion the response stream is converted to`
- Unity-impact summary: preview popup becomes a floating detail view; async thumbnail fetch/cancel stays outside the view; DPI sizing remains a layout concern.
- Hazards found: none
- Git: working tree updated, commit pending
- Next recommended Phase 1 task: T592 src/slic3r/GUI/SliceInfoPanel.hpp
- annotate: src/slic3r/GUI/2DBed.cpp
- annotate: src/slic3r/GUI/2DBed.hpp
- annotate: src/slic3r/GUI/3DBed.cpp
- annotate: src/slic3r/GUI/AboutDialog.cpp
- annotate: src/slic3r/GUI/AboutDialog.hpp

Original objective: # PROMPT - Phase 1: GUI File-by-File Annotation for Unity Port Preparation



## Phase Boundary



This prompt governs **Phase 1 only**.



- Phase 0 is already complete.

- Do **not** revisit Phase 0...
```

## Phase 1 - Task T633 complete

- Task type: skip-trivial
- File: src/slic3r/GUI/WebUpdatePlugin.cpp
- Deliverables: updated ralph-tasks.md
- Substantive additions: 0 (file empty)
- Verification excerpt: file is empty (0 lines)
- Unity-impact summary: No UI logic, trivial empty file; skip.
- Hazards found: 0
- Git: (pending commit)
- Next recommended Phase 1 task: T634 skip-trivial: src/slic3r/GUI/WebUpdatePlugin.hpp (also empty)

## Phase 1 - Task T634 complete

- Task type: skip-trivial
- File: src/slic3r/GUI/WebUpdatePlugin.hpp
- Deliverables: updated ralph-tasks.md
- Substantive additions: 0 (file empty)
- Verification excerpt: file is empty (0 lines)
- Unity-impact summary: No UI logic, trivial empty file; skip.
- Hazards found: 0
- Git: (pending commit)
- Next recommended Phase 1 task: T635 annotate: src/slic3r/GUI/WebUserLoginDialog.cpp (or skip-trivial if empty)

## Phase 1 - Task T258 complete

- Task type: skip-trivial
- File: src/slic3r/GUI/DeviceCore/DevPrintTaskInfo.cpp
- Deliverables: updated ralph-tasks.md
- Substantive additions: 0 (file empty)
- Verification excerpt: file is empty (0 lines)
- Unity-impact summary: No UI logic, trivial empty file; skip.
- Hazards found: 0
- Git: (pending commit)
- Next recommended Phase 1 task: T257 annotate: src/slic3r/GUI/DeviceCore/DevPrintOptions.cpp (or next pending)

## Phase 1 - Task T548 complete

- Task type: annotate
- File: src/slic3r/GUI/PrintOptionsDialog.hpp
- Deliverables: annotated header file with [INTENT], [STATE], [EVENT], [UNITY], [PORTING_HAZARD] comments.
- Substantive additions: 10 lines of annotations across PrinterPartsDialog and PrintOptionsDialog classes.
- Verification excerpt: class PrinterPartsDialog : public DPIDialog with annotation block.
- Unity-impact summary: Both dialogs rely on wxWidgets DPIDialog and custom widgets; Unity port requires custom Dialog UI with ScriptableObject state and UI Toolkit or Canvas replacements.
- Hazards found: 1 (P2) - heavy wxWidgets dependency.
- Git: b6c5e8b7d785eb4b8bfa205c07a6a1cffb906cc3
- Next recommended Phase 1 task: T549 annotate: src/slic3r/GUI/PrivacyUpdateDialog.cpp (or next pending)

## Phase 1 - Task T549 complete

- Task type: annotate
- File: src/slic3r/GUI/PrivacyUpdateDialog.cpp
- Deliverables: annotated source file with [INTENT], [STATE], [EVENT], [UNITY], [PORTING_HAZARD] comments.
- Substantive additions: 5 lines of annotations across class, constructor, event definitions, and RunScript method.
- Verification excerpt: // [INTENT] PrivacyUpdateDialog: Modal dialog to present privacy policy updates with webview content and accept/log out actions.
- Unity-impact summary: Replace wxWebView with Unity WebView2/browser plugin; use UI Toolkit VisualElement or Canvas for dialog; require JavaScript interop for markdown rendering.
- Hazards found: 1 (P2) - wxWebView and DPIDialog are wxWidgets-specific.
- Git: db91875bc0
- Next recommended Phase 1 task: T550 annotate: src/slic3r/GUI/PrivacyUpdateDialog.hpp (or next pending)

## Phase 1 - Task T550 complete

- Task type: annotate
- File: src/slic3r/GUI/PrivacyUpdateDialog.hpp
- Deliverables: annotated header file with [INTENT], [STATE], [EVENT], [UNITY], [PORTING_HAZARD] comments.
- Substantive additions: 6 lines of annotations at top of header.
- Verification excerpt: // [INTENT] PrivacyUpdateDialog displays privacy policy updates and release notes using a webview.
- Unity-impact summary: Replace wxWebView with Unity UI Toolkit WebView or HTML-like UI Document; Buttons with UI Toolkit Button; DPIDialog with custom Dialog.
- Hazards found: 1 (P2) - wxWebView dependency; Unity lacks built-in webview.
- Git: 6b398a2655
- Next recommended Phase 1 task: T551 annotate: src/slic3r/GUI/ProgressStatusBar.cpp (or next pending)

## Phase 1 - Task T552 complete

- Task type: annotate
- File: src/slic3r/GUI/ProgressStatusBar.hpp
- Deliverables: annotated header file with [INTENT], [STATE], [EVENT], [UNITY] comments.
- Substantive additions: 5 lines of annotations.
- Verification excerpt: // [INTENT] The ProgressStatusBar class is the widgets occupying the lower area
- Unity-impact summary: Map to Unity UI Toolkit VisualElement with ProgressBar and Button; wxStatusBar wrapper replaced with custom UI component.
- Hazards found: 0
- Git: fd37a3eb61
- Next recommended Phase 1 task: T551 annotate: src/slic3r/GUI/ProgressStatusBar.cpp (or next pending)

## Phase 1 - Task T554 complete

- Task type: annotate
- File: src/slic3r/GUI/ProjectDirtyStateManager.cpp
- Deliverables: annotated source file with [INTENT], [EVENT], [STATE], [UNITY] comments.
- Substantive additions: 4 lines of annotations at top and for each method.
- Verification excerpt: // [INTENT] ProjectDirtyStateManager tracks whether the current project has unsaved changes.
- Unity-impact summary: Map to C# class observing UndoRedo stack changes and preset bundle changes; events notify UI of dirty state; debug window uses ImGui (replace with Unity IMGUI or custom EditorWindow).
- Hazards found: 1 (P2) - Heavy dependency on wxGetApp() global singleton and wxWidgets types; Unity requires dependency injection or static service locator.
- Git: 7e1278f7e5
- Next recommended Phase 1 task: T555 annotate: src/slic3r/GUI/ProjectDirtyStateManager.hpp (or next pending)

## Phase 1 - Task T555 complete

- Task type: annotate
- File: src/slic3r/GUI/ProjectDirtyStateManager.hpp
- Deliverables: annotated header file plus synchronized task/handoff status updates.
- Substantive additions: 7 annotation lines covering class intent, event flow, Unity mapping, and baseline state members.
- Verification excerpt: // [INTENT] Tracks whether the active project has diverged from its saved baseline.
- Unity-impact summary: Convert the header into a C# dirty-state service backed by project/preset events.
- Unity-impact summary: Preserve saved-baseline snapshot comparisons for presets and project_config.
- Hazards found: 1 (P2) - equality-based dirty detection depends on stable snapshot serialization.
- Git: Annotate ProjectDirtyStateManager header
- Next recommended Phase 1 task: T556 annotate: src/slic3r/GUI/Project.hpp

## Phase 1 - Task T556 complete

- Task type: annotate
- File: src/slic3r/GUI/Project.hpp
- Deliverables: annotated header file plus synchronized task status update.
- Substantive additions: 20+ annotation lines covering class intent, state, events, threading, Unity mapping, and porting hazards.
- Verification excerpt: // [UNITY] Likely maps to a UI Toolkit panel with a dedicated web-content surface or embedded webview plugin plus a C# controller
- Unity-impact summary: Preserve the embedded webview + native editor split as an explicit bridge layer.
- Unity-impact summary: Keep background filesystem scans off the UI thread and marshal results back through a message bus.
- Hazards found: 1 (P2) - browser scripting, file scanning, and delayed UI swaps are tightly coupled.
- Git: Annotate Project.hpp
- Next recommended Phase 1 task: T557 annotate: src/slic3r/GUI/PublishDialog.cpp

## Phase 1 - Task T188 complete

- Task type: annotate
- File: src/slic3r/GUI/calib_dlg.hpp
- Deliverables: src/slic3r/GUI/calib_dlg.hpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 9 annotation blocks covering dialog-family intent, state, events, Unity mapping, and porting hazards
- Verification excerpt: `[UNITY] Recreate these as controller-backed modal UI Toolkit pages with validated inputs, explicit confirm actions, and a shared calibration view-model.`
- Unity-impact summary: shared calibration view-model; modal dialog/controller split; preserve per-test dispatch semantics
- Hazards found: P2 x 5, P3 x 3
- Git: Annotate calibration dialog header
- Next recommended Phase 1 task: T189 annotate: src/slic3r/GUI/Calibration.cpp

## Phase 1 - Task T558 complete

- Task type: annotate
- File: src/slic3r/GUI/PublishDialog.hpp
- Deliverables: src/slic3r/GUI/PublishDialog.hpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 10 annotation blocks covering class intent, publish-step state, UI-thread callbacks, cancel/close flow, and Unity mapping
- Verification excerpt: // [PORTING_HAZARD:P2] The wx version assumes the dialog can remain alive while queued publish events
- Unity-impact summary: model as a modal controller plus async progress overlay/stepper
- Unity-impact summary: marshal publish status updates onto the main thread instead of yielding inside the dialog
- Hazards found: P2 x2
- Git: annotate PublishDialog header
- Next recommended Phase 1 task: T559 annotate: src/slic3r/GUI/RammingChart.cpp

## Phase 1 - Task T559 complete

- Task type: annotate
- File: src/slic3r/GUI/RammingChart.cpp
- Deliverables: `.ralph/agent/scratchpad.md`, `.ralph/ralph-tasks.md`, `src/slic3r/GUI/RammingChart.cpp`
- Substantive additions: 9 boundary comments covering paint flow, event flow, drag handling, spline rebuilds, export helpers, and wx event-table routing
- Verification excerpt: `[PORTING_HAZARD:P2] The view mixes model sampling with pixel-space drawing`
- Unity-impact summary:
  - Replace the chart with a custom Unity control backed by retained math-space curve data
  - Split drag/edit interactions from sampled rendering so zoom and export stay deterministic
  - Keep chart-change notifications as controller events for the wipe-tower workflow
- Hazards found: P2=1
- Git: Annotate ramming chart interaction flow
- Next recommended Phase 1 task: T560 annotate: src/slic3r/GUI/RammingChart.hpp

## Phase 1 - Task T561 complete

- Task type: annotate
- File: src/slic3r/GUI/RecenterDialog.cpp
- Deliverables: src/slic3r/GUI/RecenterDialog.cpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 6 annotation blocks covering dialog intent/state, owner-drawn paint flow, brittle text wrapping, button event semantics, and DPI refresh handling
- Verification excerpt: `[PORTING_HAZARD:P2] The layout is hand-built from pixel measurements, so a Unity port should replace this with a locale-aware layout helper instead of copying the wrapping heuristic.`
- Unity-impact summary:
  - Port as a modal confirmation controller with explicit Go Home and Close actions.
  - Replace the paint-time text measurement/wrapping with a reusable locale-aware layout helper.
  - Refresh icon assets and layout on scale-factor changes.
- Hazards found: P2 x1, P3 x1
- Git: Annotate RecenterDialog confirmation flow
- Next recommended Phase 1 task: T562 annotate: src/slic3r/GUI/RecenterDialog.hpp

## Phase 1 - Task T565 complete

- Task type: annotate
- File: src/slic3r/GUI/RemovableDriveManager.cpp
- Deliverables: src/slic3r/GUI/RemovableDriveManager.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 12 annotation blocks covering module scope, Windows eject flow, Unix/macOS discovery and eject flow, lifecycle, status, update, polling, and cleanup
- Verification excerpt: `[UNITY] Replace with a platform service + main-thread UI controller, backed by async device enumeration and explicit eject-result callbacks.`
- Unity-impact summary:
  - Model removable-drive state as a cached service rather than per-widget polling.
  - Marshal eject completion and drive-change events back to the main thread.
  - Replace blocking OS commands with async tasks or native plugin calls.
- Hazards found: P2 x4, P3 x1
- Git: Annotate removable drive manager lifecycle
- Next recommended Phase 1 task: T566 annotate: src/slic3r/GUI/RemovableDriveManager.hpp

## Phase 1 - Task T566 complete

- Task type: annotate
- File: src/slic3r/GUI/RemovableDriveManager.hpp
- Deliverables: src/slic3r/GUI/RemovableDriveManager.hpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 16 annotation blocks covering drive identity, events, lifecycle, platform threading, cache state, and macOS/Windows portability
- Verification excerpt: `[PORTING_HAZARD:P2] Platform behavior diverges: macOS uses notifications, Windows can callback from volume events, and Unix/OSX eject may block.`
- Unity-impact summary:
  - Model the manager as a long-lived platform service with main-thread UI callbacks.
  - Keep drive snapshots and eject completion as explicit async state rather than direct widget logic.
  - Split macOS/Windows/worker behavior behind a native plugin boundary.
- Hazards found: P2 x2, P3 x1
- Git: annotate removable drive manager header
- Next recommended Phase 1 task: T567 annotate: src/slic3r/GUI/SafetyOptionsDialog.cpp
## Phase 1 - Task T567 complete

- Task type: annotate
- File: src/slic3r/GUI/SafetyOptionsDialog.cpp
- Deliverables: `src/slic3r/GUI/SafetyOptionsDialog.cpp`, `.ralph/agent/scratchpad.md`
- Substantive additions: 7 comment blocks covering dialog intent, DPI/layout refresh, UI-thread refresh flow, direct device-command event handlers, unavailable-state rendering, settings-group composition, and toast lifetime/porting hazards
- Verification excerpt: `[PORTING_HAZARD:P3] wxPopupWindow positioning and lifetime are bespoke; Unity should use an overlay toast anchored to the same row instead of a separate floating native window.`
- Unity-impact summary: modal settings panel; direct device command bridge; overlay toast for unavailable idle-heating mode
- Hazards found: 1 P2, 1 P3
- Git: SafetyOptionsDialog annotations
- Next recommended Phase 1 task: T568 annotate `src/slic3r/GUI/SafetyOptionsDialog.hpp`

## Phase 1 - Task T569 complete

- Task type: annotate
- File: src/slic3r/GUI/SavePresetDialog.cpp
- Deliverables: `src/slic3r/GUI/SavePresetDialog.cpp`, `.ralph/ralph-tasks.md`, `.ralph/agent/scratchpad.md`, `.ralph/agent/handoff.md`
- Substantive additions: 7 annotation blocks covering per-row validation, live naming rules, modal construction, printer-specific action routing, destructive overwrite handling, and synchronous printer rebinding
- Verification excerpt: `[PORTING_HAZARD:P1] The confirmation path is not just a local dialog close; it can trigger remote preset cleanup`
- Unity-impact summary:
  - Port the dialog as a modal controller with row-level view-models.
  - Preserve live validation and printer-binding side effects as explicit controller state.
  - Treat cloud delete and printer rebinding as separate async side effects in Unity.
- Hazards found: P1 x1, P2 x3, P3 x1
- Git: Annotate SavePresetDialog preset save flow
- Next recommended Phase 1 task: T570 annotate `src/slic3r/GUI/SavePresetDialog.hpp`

## Phase 1 - Task T572 complete

- Task type: annotate
- File: src/slic3r/GUI/SceneRaycaster.hpp
- Deliverables: src/slic3r/GUI/SceneRaycaster.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 12 annotation blocks covering item ownership, bucketed picking state, hit-result metadata, debug-only GL overlay members, query/mutation APIs, and id-encoding contract
- Verification excerpt: `[UNITY] Replace with a scene-picking service plus explicit priority buckets, fed by the input bridge and queried from the main thread.`
- Unity-impact summary:
  - Keep pick priority as explicit buckets for beds, volumes, gizmos, and fallback gizmos.
  - Centralize encoded-id mapping in one adapter so Unity can preserve the legacy selection contract.
  - Treat debug hit visualization as a separate overlay path, not part of normal rendering.
- Hazards found: P2 x1, P3 x1, P1 x0
- Git: annotate SceneRaycaster header
- Next recommended Phase 1 task: T573 annotate `src/slic3r/GUI/Search.cpp`

## Phase 1 - Task T573 complete

- Task type: annotate
- File: src/slic3r/GUI/Search.cpp
- Deliverables: src/slic3r/GUI/Search.cpp, .ralph/ralph-tasks.md, .ralph/agent/handoff.md, .ralph/agent/scratchpad.md
- Substantive additions: 8 annotation blocks covering shared search intent, option-cache state, popup row rendering, preset popup lifecycle, query rebuild flow, virtual list modeling, object popup lifecycle, and object refresh flow
- Verification excerpt: [UNITY] Port this as a query-owned floating panel: a UI Toolkit SearchField driving a filtered ListView, with row selection callbacks into the preset/object controllers.
- Unity-impact summary:
  - Split preset-search and object-search into separate Unity controllers that share a filtered list widget.
  - Preserve the manual highlight/selection semantics in a persistent row template instead of string-rebuilding widgets.
  - Model dismissal and focus loss explicitly to replace wxPopupWindow behavior.
- Hazards found: P2 x4, P3 x1, P1 x0
- Git: annotate Search popup flow
- Next recommended Phase 1 task: T574 annotate `src/slic3r/GUI/Search.hpp`
## Phase 1 - Task T574 complete

- Task type: annotate
- File: src/slic3r/GUI/Search.hpp
- Deliverables: src/slic3r/GUI/Search.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 11 declaration-level comment blocks covering popup boundary, DTO/state, manual row painting, and both dialog controllers
- Verification excerpt: `[UNITY] Model these as query-driven overlay controllers backed by a reusable filtered list view and explicit dismiss/focus state.`
- Unity-impact summary: floating overlay controller; persistent list view adapter; explicit focus-loss dismissal state
- Hazards found: 1 P2
- Git: Annotate search popup boundary in Search.hpp
- Next recommended Phase 1 task: T575 annotate: src/slic3r/GUI/Selection.cpp

## Phase 1 - Task T575 complete

- Task type: annotate

- File: src/slic3r/GUI/Selection.cpp

- Deliverables: src/slic3r/GUI/Selection.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md

- Substantive additions: 14 boundary comments covering lifecycle, selection mutation, undo/snapshot flow, clipboard copy/paste, cached transform math, GL overlay rendering, sibling synchronization, and bed-clamping hazards

- Verification excerpt: `[PORTING_HAZARD:P2] This file mixes undo snapshots, model mutation, object-list refreshes, and GL preview rendering.`

- Unity-impact summary:
  - Model selection as a transaction-backed edit service, not a rendered-node owner
  - Split overlay rendering from selection state and command dispatch
  - Preserve sibling fan-out and plate-aware paste heuristics explicitly

- Hazards found: P2 x4, P3 x3, UNCLEAR x1

- Git: Annotate Selection selection and render flow

- Next recommended Phase 1 task: T576 annotate: src/slic3r/GUI/Selection.hpp

## Phase 1 - Task T576 complete

- Task type: annotate
- File: src/slic3r/GUI/Selection.hpp
- Deliverables: src/slic3r/GUI/Selection.hpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 12 annotation blocks covering selection ownership, clipboard state, drag cache lifetime, overlay rendering, synchronization, and Unity migration guidance
- Verification excerpt: `[PORTING_HAZARD:P2] Drag session state is implicit here, so a Unity port should make the gesture lifetime explicit.`
- Unity-impact summary:
  - Model the file as a scene-selection controller with explicit selection and clipboard view-model state.
  - Replace retained GL overlay helpers with a dedicated gizmo/overlay renderer.
  - Preserve index rebasing and sibling-instance synchronization as explicit controller actions.
- Hazards found: P2 x2, P3 x1
- Git: Annotate Selection header
- Next recommended Phase 1 task: T577 annotate: src/slic3r/GUI/SelectMachine.cpp

## Phase 1 - Task T577 complete

- Task type: annotate
- File: src/slic3r/GUI/SelectMachine.cpp
- Deliverables: src/slic3r/GUI/SelectMachine.cpp, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 11 annotation blocks covering dialog lifecycle, event wiring, mode transitions, thumbnail compositing, and custom widget boundaries
- Verification excerpt: `[UNITY] In Unity this would be a texture-processing step feeding a preview RawImage; keep the original and recolored variants separate.`
- Unity-impact summary:
  - Model the send workflow as a modal controller with explicit state transitions and printer-selection reset semantics.
  - Treat the thumbnail pipeline as a CPU texture-compositing step feeding a UI preview image.
  - Replace the painted segmented controls and printer selector compound widget with reusable UI Toolkit controls.
- Hazards found: P2 x1
- Git: Annotate SelectMachine send-print flow
- Next recommended Phase 1 task: T578 annotate: src/slic3r/GUI/SelectMachine.hpp

## Phase 1 - Task T579 complete

- Task type: annotate
- File: src/slic3r/GUI/SelectMachinePop.cpp
- Deliverables: src/slic3r/GUI/SelectMachinePop.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 10 comment blocks covering popup ownership, row rendering, async refresh, dismissal, list rebuilds, manual hit-testing, rename validation, and the pin-code shortcut rows
- Verification excerpt: `// [UNITY] Model this as a retained controller plus scrollable item list with reusable row views.`
- Unity-impact summary:
  - Floating popup controller with pooled rows and two live device sections
  - Standard UI event routing should replace screen-space click forwarding
  - Worker-thread fetches and timer refreshes need explicit main-thread marshaling in Unity
- Hazards found: P1 x1, P2 x4, P3 x3
- Git: Annotate SelectMachinePop popup controller
- Next recommended Phase 1 task: T580 annotate: src/slic3r/GUI/SelectMachinePop.hpp

## Phase 1 - Task T592 complete

- Task type: annotate
- File: src/slic3r/GUI/SliceInfoPanel.hpp
- Deliverables: src/slic3r/GUI/SliceInfoPanel.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 11 boundary comments covering popup intent/state, async web-request threading, hover-driven transient overlays, and Unity migration guidance
- Verification excerpt: `[THREAD] \`wxWebRequest\` callbacks may arrive asynchronously, so image updates must be treated as UI-thread completion events.`
- Unity-impact summary: retained summary card plus separate popover controller; async thumbnail loading stays outside the view tree; widget ownership boundaries are now explicit
- Hazards found: P2 x1 (stale async thumbnail responses can race popup state)
- Git: Annotate SliceInfoPanel header boundary
- Next recommended Phase 1 task: T593 annotate: src/slic3r/GUI/SlicingProgressNotification.cpp

## Phase 1 - Task T605 complete

- Task type: annotate
- File: src/slic3r/GUI/Tabbook.cpp
- Deliverables: src/slic3r/GUI/Tabbook.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 8 boundary comments covering ownership, paint flow, DPI rescale, selection/event flow, tab badge state, page icon/text state, padding, and footer behavior
- Verification excerpt: [UNITY] Map this to a vertical tab rail (ScrollRect/ListView or a button column) with a shared selection model and reusable button prefabs rather than reparenting live widgets.
- Unity-impact summary: retained selection model; reusable tab button items; separate footer/status row
- Hazards found: 1 P2
- Git: Annotate Tabbook sidebar tab rail
- Next recommended Phase 1 task: T606 annotate: src/slic3r/GUI/Tabbook.hpp

## Phase 1 - Task T606 complete

- Task type: annotate
- File: src/slic3r/GUI/Tabbook.hpp
- Deliverables: src/slic3r/GUI/Tabbook.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 14 boundary comments covering tab-rail ownership, selection and insertion events, page lifetime, focus traversal, page transitions, and Unity migration guidance
- Verification excerpt: `[UNITY] Port as a page-host controller with a separate tab-rail view and explicit page swap events rather than a single monolithic notebook widget.`
- Unity-impact summary: retain the tab rail as its own view; keep page swap and focus routing explicit; preserve page transition state separately from selection
- Hazards found: P2 x2, P3 x1
- Git: Annotate Tabbook.hpp boundary
- Next recommended Phase 1 task: T607 annotate: src/slic3r/GUI/TabButton.cpp

## Phase 1 - Task T607 complete

- Task type: annotate
- File: src/slic3r/GUI/TabButton.cpp
- Deliverables: src/slic3r/GUI/TabButton.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 6 comment blocks covering widget intent/state, event routing, enable-state forwarding, render layout, size measurement, and click translation.
- Verification excerpt: `[UNITY] Port as a UI Toolkit Button/Toggle with custom visuals, shared selection state, and a separate badge overlay.`
- Unity-impact summary: retained tab-item control; anchored text/icon/badge layout; selection styling and enable-state changes should live in a parent controller.
- Hazards found: 1 P3 (manual capture/release click semantics).
- Git: Annotate TabButton sidebar control
- Next recommended Phase 1 task: T608 annotate: src/slic3r/GUI/TabButton.hpp

## Phase 1 - Task T608 complete

- Task type: annotate
- File: src/slic3r/GUI/TabButton.hpp
- Deliverables: src/slic3r/GUI/TabButton.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 4 boundary comments covering cached layout/interaction state, class intent, property mutators, and event forwarding
- Verification excerpt: `[UNITY] Map to a retained UI Toolkit Button/Toggle with icon+label visuals and parent-owned selection state.`
- Unity-impact summary: the tab button becomes a retained toggle row; bitmap/label changes should invalidate layout explicitly; mouse presses still translate into command-style selection events
- Hazards found: P2 x1 (implicit click model and custom paint/event bridge)
- Git: Annotate TabButton header boundary
- Next recommended Phase 1 task: T609 annotate: src/slic3r/GUI/Tab.cpp

## Phase 1 - Task T612 complete

- Task type: annotate
- File: src/slic3r/GUI/TaskManager.hpp
- Deliverables: src/slic3r/GUI/TaskManager.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 9 boundary comments covering the file-level scheduler boundary, per-task state capsule, metadata payload, scheduler policy, batch grouping, queue/service split, worker-thread lifecycle, cached queue state, and the limit event
- Verification excerpt: `[UNITY] Model this as an async job-queue service plus a main-thread event channel, not a widget-owned controller.`
- Unity-impact summary: service-backed queue with explicit concurrency policy; job state becomes a DTO/view-model; UI reacts via events instead of owning worker threads
- Hazards found: P2 x1, UNCLEAR x1
- Git: Annotate TaskManager scheduler boundary
- Next recommended Phase 1 task: T613 annotate: src/slic3r/GUI/TextLines.cpp


## Phase 1 - Task T613 complete

- Task type: annotate
- File: src/slic3r/GUI/TextLines.cpp
- Deliverables: src/slic3r/GUI/TextLines.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 7 boundary comments covering mesh generation, contour selection, GLModel conversion, CPU rebuild, render path, and line-height derivation
- Verification excerpt: `[OPENGL] Render the cached preview mesh through the shared flat shader, temporarily enabling depth test and blending around the draw.`
- Unity-impact summary:
  - Geometry generation should move to a worker/job service.
  - Render-time code should consume cached Mesh data via a dedicated material.

## Phase 1 - Task T614 complete

- Task type: annotate
- File: src/slic3r/GUI/TextLines.hpp
- Deliverables: src/slic3r/GUI/TextLines.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 8 boundary comments covering class intent/state, init/render boundaries, reset/cache ownership, line-height helper, and the Unity job/service split
- Verification excerpt: `[UNITY] Model this as a ScriptableObject-backed geometry cache plus a worker/job service that emits a finished Mesh for a render-only view.`
- Unity-impact summary:
  - Separate the preview cache from the expensive contour builder.
  - Keep render-only consumption on the main thread/material path.
  - Preserve the line-height helper as pure layout math.
- Hazards found: P2 x1
- Git: Annotate TextLines header boundary
- Next recommended Phase 1 task: T615 annotate: src/slic3r/GUI/ThermalPreconditioningDialog.cpp
  - Contour selection stays isolated as a replaceable heuristic service.
- Hazards found: 1 P2, 1 P3
- Git: TextLines.cpp annotate: emboss preview mesh boundary annotations
- Next recommended Phase 1 task: T614 `src/slic3r/GUI/TextLines.hpp`

## Phase 1 - Task T617 complete

- Task type: annotate
- File: src/slic3r/GUI/TickCode.cpp
- Deliverables: src/slic3r/GUI/TickCode.cpp, .ralph/agent/handoff.md, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md
- Substantive additions: 6 boundary comments covering color resolution, add/edit/switch mutation, deletion, and membership queries
- Verification excerpt: `[UNITY] Model this as a retained marker view-model plus a pure color-resolution service, so row edits do not own palette math.`
- Unity-impact summary: sorted-set marker model stays explicit; color derivation should live in a shared service; edit/erase semantics depend on container ordering
- Hazards found: P2 x1, P3 x1
- Git: pending commit
- Next recommended Phase 1 task: T618 annotate: src/slic3r/GUI/TickCode.hpp

## Phase 1 - Task T621 complete

- Task type: annotate
- File: src/slic3r/GUI/UpdateDialogs.cpp
- Deliverables: src/slic3r/GUI/UpdateDialogs.cpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 7 boundary comments covering update notice state, opt-out uncertainty, config-update layout/events, startup incompatibility gating, and no-update fallback UX
- Verification excerpt: `[PORTING_HAZARD:P1] This gate runs before the app can continue, so Unity must preserve the startup-blocking compatibility check.`
- Unity-impact summary: shared version-check service; modal config-update panel with scrollable details; separate startup-blocking incompatibility dialog
- Hazards found: P1=1, P2=3, P3=1
- Git: Annotate update dialogs for Unity port
- Next recommended Phase 1 task: T622 annotate: src/slic3r/GUI/UpdateDialogs.hpp

## Phase 1 - Task T622 complete

- Task type: annotate
- File: src/slic3r/GUI/UpdateDialogs.hpp
- Deliverables: src/slic3r/GUI/UpdateDialogs.hpp, .ralph/agent/handoff.md, .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md
- Substantive additions: 11 boundary comments covering update modal intent, opt-out state, hyperlink event flow, config-update force gating, compatibility hazards, and the shared no-update shell
- Verification excerpt: `[UNITY] Reuse the same modal shell with a text-only content panel and standard dismiss button.`
- Unity-impact summary:
  - Keep the update/update-forced/update-incompatible dialogs on one reusable modal shell.
  - Preserve the opt-out toggle and pre-wizard gating as explicit retained state.
  - Treat compatibility summaries as structured rows, not a flattened string blob.
- Hazards found: P2 x1
- Git: Annotate UpdateDialogs.hpp update-flow boundary
- Next recommended Phase 1 task: T623 annotate: src/slic3r/GUI/UpgradePanel.cpp

## Phase 1 - Task T623 complete

- Task type: annotate
- File: src/slic3r/GUI/UpgradePanel.cpp
- Deliverables: src/slic3r/GUI/UpgradePanel.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 20+ boundary annotations covering the root dashboard, machine card, accessory rows, status/progress state machine, and upgrade confirmation flows
- Verification excerpt: `[PORTING_HAZARD:P2] This routine mixes module discovery, naming rules, and dynamic row creation, so it needs a cleaner data model in Unity.`
- Unity-impact summary:
  - Map the screen to a retained controller with reusable machine/accessory subviews.
  - Keep firmware status and upgrade progress in a dedicated view-model/state machine.
  - Preserve confirmation dialogs as modal overlays, not inline widget branches.
- Hazards found: P2 x1
- Git: Annotate upgrade panel firmware dashboard
- Next recommended Phase 1 task: T624 annotate: src/slic3r/GUI/UpgradePanel.hpp

## Phase 1 - Task T624 complete

- Task type: annotate
- File: src/slic3r/GUI/UpgradePanel.hpp
- Deliverables: src/slic3r/GUI/UpgradePanel.hpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 8 boundary annotations covering the dashboard root, accessory cards, machine-card state, progress block, upgrade command flow, and hint-dialog gating
- Verification excerpt: `[UNITY] Port as a scroll view with a single lazily-created machine card prefab and modal prompt layer.`
- Unity-impact summary:
  - Keep the upgrade page as a retained scroll view with one lazily created machine-card controller.
  - Model accessory rows as reusable prefabs bound to version/status view-models.
  - Preserve force/consistency dialogs as modal overlays, not inline branches.
- Hazards found: P2 x2, P3 x1
- Git: Annotate UpgradePanel.hpp upgrade dashboard boundary
- Next recommended Phase 1 task: T625 annotate: src/slic3r/GUI/UserManager.cpp

## Phase 1 - Task T625 complete

- Task type: annotate
- File: src/slic3r/GUI/UserManager.cpp
- Deliverables: src/slic3r/GUI/UserManager.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 3 boundary comment blocks covering transport/session ownership, dependency injection, and bind-success payload parsing
- Verification excerpt: `[UNITY] Model this as a typed auth-result message plus a UI-thread completion callback rather than parsing JSON in the view layer.`
- Unity-impact summary: auth success becomes event-driven; UI mutations need main-thread marshaling; transport should not own modal dialog closure or machine selection
- Hazards found: P2 x1, UNCLEAR x1
- Git: Annotate UserManager auth payload parsing
- Next recommended Phase 1 task: T626 annotate: src/slic3r/GUI/UserManager.hpp

## Phase 1 - Task T626 complete

- Task type: annotate
- File: src/slic3r/GUI/UserManager.hpp
- Deliverables: src/slic3r/GUI/UserManager.hpp, .ralph/agent/handoff.md, .ralph/agent/scratchpad.md
- Substantive additions: 7 boundary comments covering class intent, non-owning agent state, thread affinity, Unity mapping, porting hazard, and the two method contracts
- Verification excerpt: `[UNITY] Replace with a typed auth-result message handler plus a main-thread completion bridge; keep transport parsing out of view code.`
- Unity-impact summary:
  - Treat the header as a transport adapter boundary, not a UI controller.
  - Marshal agent-driven UI changes back to the Unity main thread.
  - Preserve external ownership of `NetworkAgent` and make the coupling explicit in C#.
- Hazards found: P2 x1
- Git: pending commit `Annotate UserManager header for auth adapter`
- Next recommended Phase 1 task: T627 annotate: src/slic3r/GUI/UserNotification.cpp

## Phase 1 - Task T627 complete

- Task type: skip-trivial
- File: src/slic3r/GUI/UserNotification.cpp
- Deliverables: .ralph/ralph-tasks.md, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md
- Substantive additions: 0; the file is a 5-line namespace-only stub with no GUI logic or state
- Verification excerpt: `namespace Slic3r {` / `} // namespace Slic3r`
- Unity-impact summary:
  - No first-party behavior to port in this file.
  - Keep the real migration notes on `src/slic3r/GUI/UserNotification.hpp`.
- Hazards found: none
- Git: Skip trivial UserNotification.cpp
- Next recommended Phase 1 task: T628 annotate: src/slic3r/GUI/UserNotification.hpp

## Phase 1 - Task T628 complete

- Task type: skip-trivial
- File: src/slic3r/GUI/UserNotification.hpp
- Deliverables: .ralph/ralph-tasks.md, .ralph/agent/handoff.md, .ralph/agent/scratchpad.md
- Substantive additions: 0; the header is a 20-line enum + empty shell with no behavior, state, or event flow to port
- Verification excerpt: `class UserNotification { public: UserNotification() {} };`
- Unity-impact summary:
  - No first-party GUI behavior is present in this header.
  - Keep migration notes focused on the real implementation file and surrounding callers.
  - The task title was normalized from `annotate` to `skip-trivial` to match the file's semantics.
- Hazards found: none
- Git: pending commit `Skip trivial UserNotification header`
- Next recommended Phase 1 task: T629 annotate: src/slic3r/GUI/WebDownPluginDlg.cpp

## Phase 1 - Task T629 complete

- Task type: annotate
- File: src/slic3r/GUI/WebDownPluginDlg.cpp
- Deliverables: src/slic3r/GUI/WebDownPluginDlg.cpp, .ralph/agent/scratchpad.md, .ralph/ralph-tasks.md, .ralph/agent/handoff.md
- Substantive additions: 9 boundary comment blocks covering the dialog host boundary, browser event bindings, URL reload flow, external-window routing, JS command dispatch, plugin download/install bridges, progress reporting, and error handling
- Verification excerpt: `[PORTING_HAZARD:P1] This is a privileged command surface; a Unity port needs a typed, validated message schema.`
- Unity-impact summary: retained WebView host plus typed command router; plugin transfer/install should move behind async services; external links stay outside the embedded view
- Hazards found: P1 x1, P2 x2, UNCLEAR x1
- Git: Annotate WebDownPluginDlg plugin web host
- Next recommended Phase 1 task: T630 annotate: src/slic3r/GUI/WebDownPluginDlg.hpp

## Phase 1 - Task T630 complete

- Task type: annotate
- File: src/slic3r/GUI/WebDownPluginDlg.hpp
- Deliverables: src/slic3r/GUI/WebDownPluginDlg.hpp, .ralph/agent/scratchpad.md, .ralph/agent/handoff.md, .ralph/ralph-tasks.md
- Substantive additions: 7 boundary comments covering dialog intent, browser ownership, page-load flow, browser/script event routing, install/download bridge, and progress state
- Verification excerpt: `[PORTING_HAZARD:P1] The web page can trigger install/restart/file-open behavior, so the message surface is privileged rather than informational.`
- Unity-impact summary: retained WebView host panel; typed command router instead of raw JS dispatch; progress updates should stay on a main-thread service boundary
- Hazards found: P1 x1, P2 x1
- Git: Annotate WebDownPluginDlg.hpp boundary
- Next recommended Phase 1 task: T631 annotate: src/slic3r/GUI/WebGuideDialog.cpp
