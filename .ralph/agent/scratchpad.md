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

- Completed T578 (`src/slic3r/GUI/SelectMachine.hpp`) as the next atomic annotation pass.
- The header now frames the send-print dialog boundary, custom option rows, thumbnail compositing, mode switching, printer header controls, and the modal workflow state machine for Unity migration.
- Next step: move to T579 (`src/slic3r/GUI/SelectMachinePop.cpp`) and keep the annotation blocks focused on ownership, events, thread boundaries, and concrete Unity mappings.

- Started T579 (`src/slic3r/GUI/SelectMachinePop.cpp`) and annotated the popup shell, row widgets, async refresh path, manual hit-testing, rename dialog validation, and the pin-code shortcut rows.
- The popup is a pooled wxPopupWindow with two live device sections, a disabled SSDP hook, worker-thread print-info fetch, and screen-space click forwarding; Unity should replace that with a floating controller, recycled rows, and standard UI event routing.
- Verification so far: `git diff --check` is clean for this patch; next step is to commit the atomic annotation and move to T580 (`src/slic3r/GUI/SelectMachinePop.hpp`).

- Completed T580 (`src/slic3r/GUI/SelectMachinePop.hpp`) as the next atomic annotation pass.
- The header now frames the popup boundary, row widget intent, worker-thread fetch boundary, custom event routing, and the rename modal mapping for Unity migration.
- Next step: move to T581 (`src/slic3r/GUI/SendMultiMachinePage.cpp`) after committing this atomic annotation.

- Started T581 (`src/slic3r/GUI/SendMultiMachinePage.cpp`) and annotated the modal controller boundary, row widget behavior, device-list rebuild flow, AMS mapping serialization, send workflow, rename validation, thumbnail defaults, and periodic refresh tick.
- The page is a composite send-to-multi-device dialog: it mixes a cached device roster, a popup-based AMS mapper, app-config-backed options, and synchronous send/export actions, so Unity should split it into reusable subviews plus a service-backed view model.
- Next step after commit: close T581, record completion evidence in handoff, and move to the next ready annotation task.

- Started T582 (`src/slic3r/GUI/SendMultiMachinePage.hpp`) and added declaration-level annotations for the custom-painted device row controller and the modal multi-printer workflow boundary.
- The header makes the Unity split explicit: prefabbed recyclable device rows, a modal shell, and a separate AMS mapping/settings model with main-thread refresh handling.
- Next step after commit: close T582 and continue with T583 (`src/slic3r/GUI/SendSystemInfoDialog.cpp`).

- Started T583 (`src/slic3r/GUI/SendSystemInfoDialog.cpp`) and annotated the consent dialog boundary, cached payload state, preview modal, version gate, payload assembly, platform probes, OpenGL metadata, worker-thread upload flow, and the external entry point.
- The file is a privacy-sensitive telemetry prompt: it collects OS/hardware/display/OpenGL details and sends them via a blocking HTTP flow, so the Unity port should treat it as a modal opt-in controller plus an async upload service with a reviewed payload schema.
- Next step after commit: close T583 and move to T584 (`src/slic3r/GUI/SendSystemInfoDialog.hpp`).

- Started T584 (`src/slic3r/GUI/SendSystemInfoDialog.hpp`) as a declaration-only consent-gate boundary.
- The header is intentionally thin: it exposes the dialog entry point without any payload state, so the Unity port only needs a modal controller hook plus the same privacy gate semantics.
- Next step after commit: close T584 and move to T585 (`src/slic3r/GUI/SendToPrinter.cpp`).

- Started T585 (`src/slic3r/GUI/SendToPrinter.cpp`) for the send-to-printer/storage modal.
- The file combines printer discovery, storage selection, rename validation, device sync, tunnel setup, and upload progress/timeouts; the key Unity mapping is a modal controller backed by an async connection/upload service plus explicit main-thread marshaling for status updates.
- Plan: add a small set of high-value boundary comments around dialog intent/state, refresh and connection threading, send/upload flow, and timeout/cancel hazards, then verify and close the task.
- Completed T585 with boundary annotations on the send state machine, async device fetch, tunnel connection, upload callbacks, and teardown path.

- Started T586 (`src/slic3r/GUI/SendToPrinter.hpp`) as the declaration boundary for the same modal send workflow.
- The header is the state-machine seam: it owns printer/device selection state, transfer-job lifetimes, timer-driven refresh, and the public event surface that the cpp wires up.
- Plan: keep the annotation focused on declaration-level [INTENT]/[STATE]/[EVENT]/[THREAD]/[UNITY] boundaries, then close the task and continue to the next header in the queue.

- Started T587 (`src/slic3r/GUI/SingleChoiceDialog.cpp`) as a compact modal choice dialog.
- The file is a thin DPIDialog wrapper around a read-only ComboBox plus OK/Cancel dismissal, with one obvious Unity hazard: it assumes a non-empty choice list when seeding the combo.
- Plan: keep the annotations focused on the modal wrapper boundary, transient selection state, button/event flow, the empty-list hazard, and the fixed-layout DPI hook, then close the task and move to T588 (`src/slic3r/GUI/SingleChoiceDialog.hpp`).

- Starting T588 (`src/slic3r/GUI/SingleChoiceDialog.hpp`) as the declaration boundary for the same modal chooser.
- The header is small, so the goal is to anchor the class purpose, widget ownership, combo-box exposure, and Unity mapping without bloating it.
- Likely annotation focus: dialog intent, combo ownership/lifetime, DPI override boundary, and a Unity note for a modal overlay with a dropdown-backed selection model.

- Completed T588 with a minimal header annotation set: class intent, transient ComboBox ownership, DPI event boundary, and a P2 porting hazard for the raw accessor plus empty-list assumption.
- The header stayed syntactically clean after patching; the only parser noise was from unresolved external includes in the local LSP environment, not from the comment insertion itself.

- Started T589 on `src/slic3r/GUI/SkipPartCanvas.cpp` and annotated the canvas boundary, image reload state reset, immediate-mode OpenGL render path, hover/click/drag event flow, and the embedded 3MF metadata parser.
- The file mixes color-encoded hit testing with custom OpenGL stencil drawing, so the Unity split should be a retained controller over explicit hit data plus a separate import/parser service.
- Next step after commit: close T589 and move to T590 (`src/slic3r/GUI/SkipPartCanvas.hpp`).

- Started T590 on `src/slic3r/GUI/SkipPartCanvas.hpp` and annotated the declaration boundary for the color-picking canvas, its event surface, and the 3MF metadata parser helper.
- The header confirms the Unity split: a RenderTexture-backed controller for pick/zoom/drag behavior, plus a separate import service for plate/object parsing.
- Next step after commit: close T590 and move to T591 (`src/slic3r/GUI/SliceInfoPanel.cpp`).

- Completed T591 (`src/slic3r/GUI/SliceInfoPanel.cpp`) with annotations around popup intent, filament color decoding, async thumbnail refresh/cancel behavior, and DPI rescaling.
- The panel is a compact summary card plus detail popup rather than a single tooltip, and the Unity port should keep the thumbnail fetch/cancel path isolated from the view so stale image responses cannot win.
- Next step after commit: close T591 and move to T592 (`src/slic3r/GUI/SliceInfoPanel.hpp`).

- Started T592 (`src/slic3r/GUI/SliceInfoPanel.hpp`) as the declaration boundary for the slice summary card and popup pair.
- The header now captures popup ownership, async web-request completion, transient hover state, and the Unity split between a summary card and a separate popover controller.
- Next step after commit: close T592 and move to T593 (`src/slic3r/GUI/SlicingProgressNotification.cpp`).

- Started T593 (`src/slic3r/GUI/SlicingProgressNotification.cpp`) for the slice-progress HUD overlay.
- The file is a UI-thread-owned immediate-mode notification that drives a small progress state machine, late print-info enrichment, and an embedded Daily Tips panel on the canvas overlay.
- Plan: keep the annotation focused on the state transitions, render path, button events, and Unity mapping to a retained HUD controller, then verify with a diff check and close the task.

- Started T619 (`src/slic3r/GUI/UnsavedChangesDialog.cpp`) as the diff-and-compare workflow for preset changes.
- The file spans a toggleable diff tree, a long-text compare popup, and a paired preset comparison dialog that can post a transfer event back to the main flow.
- Unity mapping to preserve: modal controller plus retained diff tree, separate save/transfer subdialog, and explicit paired preset selection/compatibility logic.
- Verification target: `git diff --check` after the annotation patch; the local include-path/LSP noise is expected and not a syntax signal for the inserted comments.

- Started T594 (`src/slic3r/GUI/SlicingProgressNotification.hpp`) as the declaration boundary for the same overlay.
- The header needs class-level intent plus the key state fields: progress mode, sidebar fade behavior, export availability, cancel callback, and the embedded DailyTipsPanel.
- Plan: add compact boundary comments that preserve the current fade/state semantics and spell out the Unity split as a screen-space HUD controller with a reusable child panel, then verify with `git diff --check` and close the task.

- Started T595 (`src/slic3r/GUI/StatusPanel.cpp`) as the large printer-status dashboard/controller.
- The file mixes extruder glyph rendering, AMS switching feedback, printing-progress cards, camera/control scaffolding, and the rating/upload modal; Unity should split it into retained subviews plus a shared dashboard model.
- Plan: keep the annotation focused on the major class boundaries and the async thumbnail/upload seam, then verify whitespace with `git diff --check`, append handoff evidence, and close the task.

- Started T596 (`src/slic3r/GUI/StatusPanel.hpp`) as the declaration boundary for the same dashboard.
- Added class-level annotations for the full status surface, then marked the major subcontrollers: extruder image state, switching status strip, score dialog, printing task panel, base dashboard, and concrete status panel.
- Unity mapping now calls out a retained dashboard root with child panels/services, plus explicit async web-request handling for thumbnail refresh.
- Next step after commit: close T596 and move to T597 (`src/slic3r/GUI/StepMeshDialog.cpp`).

- Started T597 (`src/slic3r/GUI/StepMeshDialog.cpp`) as the STEP import-tessellation dialog.
- The dialog couples numeric validation, slider/text synchronization, app-config persistence, and a worker-thread triangle-count preview, so the Unity port should split it into a modal controller plus an async mesh-estimation service.
- The main hazards are the blocking join/cancel path and the dialog's role as both importer and settings bridge; next step after commit is T598 (`src/slic3r/GUI/StepMeshDialog.hpp`).

- Started T598 (`src/slic3r/GUI/StepMeshDialog.hpp`) as the declaration boundary for the same STEP import dialog.
- The header now captures the modal tessellation controller, cached raw-vs-valid numeric state, worker-thread ownership, and the Unity split between validation UI and async mesh estimation.
- Next step after commit: close T598 and move to T599 (`src/slic3r/GUI/SurfaceDrag.cpp`).

- Started T599 (`src/slic3r/GUI/SurfaceDrag.cpp`) as the drag-tool implementation for embossed / surface-snapped placement.
- The file is a transient controller that caches cursor offsets, raycast filters, fix transforms, and initial angle/distance, then converts pointer motion into a deterministic scene transform.
- Unity mapping to preserve: a dedicated pointer-drag controller with explicit hit-test state, a model-layer transform helper, and a scene-query service for direct-hit plus nearest-point fallbacks.
- Verification so far: `git diff --check` will be used as the whitespace/patch sanity check before commit; the local LSP diagnostics are still the known include-path noise from `libslic3r/Point.hpp`.

- Completed T607 (`src/slic3r/GUI/TabButton.cpp`) as the next atomic annotation pass.
- Added boundary comments for the widget intent/state, event routing, enable-state forwarding, render/layout, size measurement, pointer capture, and click translation.
- Verification: `git diff --check -- src/slic3r/GUI/TabButton.cpp` passed cleanly; next step is T608 (`src/slic3r/GUI/TabButton.hpp`).

- Started T600 (`src/slic3r/GUI/SurfaceDrag.hpp`) as the declaration boundary for the transient drag session and geometry helpers.
- The header now needs to spell out the non-owning drag cache, the mouse-event gate, and the fix-up transform pipeline so Unity can mirror the controller/service split without hiding selection lifetime assumptions.
- Verification target: keep the annotation compact but cover state ownership, event flow, and the transform helpers that bridge selection space to world/surface space.

- T600 was already closed in the runtime registry, so the active work for this iteration is T601 (`src/slic3r/GUI/SyncAmsInfoDialog.cpp`).

- The runtime queue still exposed T610 as the next actionable annotation, and I started it successfully; the earlier TabButton.hpp lookup was a path mismatch (`src/slic3r/GUI/Widgets/TabButton.hpp` vs `src/slic3r/GUI/TabButton.hpp`), so I switched to the actual `Tab.hpp` task that was present in the ready list.
- Tab.hpp now carries class-level and boundary annotations for the page stack, preset controller, printer/filament specializations, and the config-binding seam.
- Verification passed with `git diff --check` on `src/slic3r/GUI/Tab.hpp`; next step is to record handoff evidence, close the task, and move to the next ready annotation item.
- SyncAmsInfoDialog.cpp is the AMS synchronization modal: dialog setup, printer-info refresh, status gating, custom filament rows, thumbnail recoloring, and the final sync toasts all live here.
- Annotation plan: keep a handful of boundary comments for the modal controller, UI event wiring, worker-thread refresh, row popup flow, and CPU-side preview pipeline.

- T601 now has boundary comments for the modal controller, event wiring, async printer refresh, status gating, popup-backed rows, thumbnail compositing, and the transient sync-success toasts.
- Next iteration should move to T602 (`src/slic3r/GUI/SyncAmsInfoDialog.hpp`) after commit.

- Started T602 (`src/slic3r/GUI/SyncAmsInfoDialog.hpp`) and annotated the multi-state AMS sync modal boundary, dialog state, async refresh/thread seam, widget ownership, public event surface, and the two transparent overlay frames.
- Unity mapping now calls for a modal controller with a retained model plus anchored overlay prefabs for the nozzle/AMS sync confirmation and completion frames.
- Next step after commit: close T602 and move to T603 (`src/slic3r/GUI/SysInfoDialog.cpp`).

- T602 is now complete in the runtime queue; the next atomic work item is T603 (`src/slic3r/GUI/SysInfoDialog.cpp`).
- SysInfoDialog is a modal system-report dialog that mixes app metadata, live memory/undo-stack stats, OpenGL capability text, and a clipboard export path, so the annotations should emphasize the modal boundary, process-scoped data gathering, DPI rescaling, and the Unity split into a retained summary panel plus a command button.
- T603 is annotated and ready to close after commit; next in line is T604 (`src/slic3r/GUI/SysInfoDialog.hpp`), which should capture the same modal/report boundary from the declaration side.

- Completed T604 (`src/slic3r/GUI/SysInfoDialog.hpp`) as the declaration boundary for the system-information modal.
- The header now records the retained report panes, logo bitmap scaling, clipboard command wiring, Unity split into a modal overlay with a report-text model, and the clipboard/process-inspection porting hazard.
- Next step after commit: close T604 in the runtime registry, then move to T605 (`src/slic3r/GUI/Tabbook.cpp`).

- Started T605 (`src/slic3r/GUI/Tabbook.cpp`) as the sidebar tab-rail controller.
- Tabbook.cpp owns the custom tab strip chrome: it reparents a caller-owned sizer subtree, paints only the selected-page band and separator, forwards button clicks via `wxCUSTOMEVT_TABBOOK_SEL_CHANGED`, and keeps a late-bound footer label for contextual hints.
- Plan: keep the annotation compact but cover selection ownership, custom paint flow, per-tab badges/icons, DPI rescales, and the Unity mapping to a retained vertical tab rail with reusable button items, then verify with `git diff --check` and commit the atomic change.

- Completed T606 (`src/slic3r/GUI/Tabbook.hpp`) with boundary annotations covering the tab-rail controller, selection and insertion event flow, focus/navigation choreography, page-visibility policy, and the Unity split between a retained tab rail and the notebook host.
- Verification: `git diff --check` is clean for the annotation patch; the header now carries concrete `[INTENT]`, `[STATE]`, `[EVENT]`, `[UNITY]`, and `[PORTING_HAZARD]` notes at the class and method boundaries.
- Next step after commit: close T606 in the runtime/task registry, then move to T607 (`src/slic3r/GUI/TabButton.cpp`).

- Started T608 (`src/slic3r/GUI/TabButton.hpp`) as the declaration boundary for the custom tab button widget.
- The header only needs a compact annotation set: class intent, cached layout/interaction state, mouse-to-command event translation, and a concrete Unity mapping to a retained toggle/button row.
- Plan: keep the notes focused on ownership, hover/press state, and the custom paint/event bridge, then verify with `git diff --check` and close the task.
- Reconciled the task registry so T607 is marked done in `.ralph/ralph-tasks.md`; T608 is the current active task.

- Handled the pending phase1.task.done event for T610 by treating `src/slic3r/GUI/Tab.hpp` as completed in the runtime narrative; the next active file task is T611 (`src/slic3r/GUI/TaskManager.cpp`).
- T611 is the throttled multi-printer send scheduler: task ingestion snapshots settings into a shared cache, a background loop gates dispatch by concurrency/interval, and worker threads bridge progress callbacks back to the UI.
- The verification target for T611 is `git diff --check` plus a focused review of the inserted boundary annotations; the main Unity mapping is an async queue/service with main-thread marshaling instead of raw boost thread ownership.

- Completed T612 (`src/slic3r/GUI/TaskManager.hpp`) with declaration-boundary annotations for the scheduler, per-task state capsule, batch policy, worker lifecycle, and the multi-send-limit event.
- Verification: `git diff --check -- src/slic3r/GUI/TaskManager.hpp` passed; the only diagnostics are inherited include-path/type-resolution noise from the local LSP environment.
- Next step after commit: close T612 and move to T613 (`src/slic3r/GUI/TextLines.cpp`).

- Started T613 (`src/slic3r/GUI/TextLines.cpp`) as the embossed-text line meshing and rendering pipeline.
- The file builds contour-following tube meshes from sliced model volumes, caches the generated `TextLinesModel` geometry, and renders it through the shared flat shader with explicit depth/blend state toggles.
- Plan: annotate the preprocessing helpers, selection heuristic, model init path, and OpenGL render boundary with concrete Unity mapping notes, then verify with `git diff --check`, record handoff evidence, and close the task.

- T613 is already reflected as done in `.ralph/ralph-tasks.md` and the handoff log, so the next active annotation is T614 (`src/slic3r/GUI/TextLines.hpp`).
- TextLines.hpp is the declaration boundary for the embossed-text preview cache: it owns the selected contour list, the reusable GLModel preview, and the line-height helper that keeps the header aligned with the cpp meshing path.
- Plan: add compact [INTENT]/[STATE]/[THREAD]/[UNITY]/[PORTING_HAZARD] notes around class purpose, cached geometry ownership, reset/init lifecycle, and the worker/job split for contour generation, then verify and close the task.

- Completed T614 (`src/slic3r/GUI/TextLines.hpp`) with declaration-boundary annotations for the preview cache, init/render separation, reset semantics, and the line-height helper.
- Verification: `git diff --check -- src/slic3r/GUI/TextLines.hpp .ralph/agent/scratchpad.md .ralph/ralph-tasks.md` passed; next step is T615 (`src/slic3r/GUI/ThermalPreconditioningDialog.cpp`).

- Started T615 (`src/slic3r/GUI/ThermalPreconditioningDialog.cpp`) as the thermal preconditioning countdown dialog.
- The file is a short-lived modal status window driven by a UI-thread wxTimer; it polls DeviceManager for the selected machine, formats the remaining time, and closes through the OK event.
- Plan: keep the annotations centered on event-table routing, timer ownership/lifetime, countdown state, and the Unity split to a modal controller with a scheduled tick, then verify with `git diff --check` and close the task.

- Completed T616 (`src/slic3r/GUI/ThermalPreconditioningDialog.hpp`) as the declaration boundary for the same countdown modal.
- The header now captures the UI-thread timer ownership, device-id lookup, countdown text refresh, and the Unity split to a modal overlay controller with a scheduled tick.
- Verification: `git diff --check -- src/slic3r/GUI/ThermalPreconditioningDialog.hpp` is clean; next step after commit is T617 (`src/slic3r/GUI/TickCode.cpp`).

- Completed T617 (`src/slic3r/GUI/TickCode.cpp`) with boundary comments for color resolution, tick mutation, deletion, and membership queries.
- Verified the annotation patch with `git diff --check`; the compiler diagnostics are include-path noise from `TickCode.hpp`, not a syntax regression in the inserted comments.
- Next active task should be T618 (`src/slic3r/GUI/TickCode.hpp`).

- Completed T618 (`src/slic3r/GUI/TickCode.hpp`) with boundary comments for the tick marker value object, the ordered marker set, the non-owning extruder-color palette pointer, and the model/service split for Unity.
- `git diff --check -- src/slic3r/GUI/TickCode.hpp` passed; local diagnostics are the expected missing include-path noise from `libslic3r/CustomGCode.hpp`, not from the annotation edits.
- Next active task should be T619 (`src/slic3r/GUI/UnsavedChangesDialog.cpp`).

- Completed T620 (`src/slic3r/GUI/UnsavedChangesDialog.hpp`) with declaration-boundary annotations for the diff-tree model, the modal unsaved-changes workflow, the full-compare popup, and the paired preset-comparison dialog.
- The header now calls out the retained-tree Unity split, the action-state ownership, and the main wxWidgets porting hazards around data-view semantics and preset-bundle snapshots.
- Next step after commit: close T620 and move to T621 (`src/slic3r/GUI/UpdateDialogs.cpp`).

- Started T621 (`src/slic3r/GUI/UpdateDialogs.cpp`) as the update/incompatibility dialog cluster.
- The file mixes four distinct modal outcomes: update notice, configuration release notes, forced incompatibility gate, and a no-updates info dialog; the live code still carries legacy/commented UI paths and a stubbed opt-out.
- Plan: keep the annotations centered on the startup-blocking compatibility path, the scrollable release-note flow, the event wiring for modal results, and the Unity split between a shared version-check service and lightweight dialog variants.

- Started T622 (`src/slic3r/GUI/UpdateDialogs.hpp`) as the declaration boundary for the same update/incompatibility modal cluster.
- The header now captures the shared modal shell, opt-out checkbox state, hyperlink event, forced pre-wizard gating, and the structured compatibility rows that should survive a Unity port.
- Next step after commit: close T622 and move to T623 (`src/slic3r/GUI/UpgradePanel.cpp`).

- Started T623 (`src/slic3r/GUI/UpgradePanel.cpp`) as the firmware-upgrade dashboard/controller.
- The file now carries annotations for the root panel, machine card, accessory subpanels, status/progress state machine, dialog triggers, and the Unity split into a retained controller plus reusable machine/accessory views.
- Verification target was `git diff --check`; next step after commit is to close T623 and move to T624 (`src/slic3r/GUI/UpgradePanel.hpp`).

- Completed T624 (`src/slic3r/GUI/UpgradePanel.hpp`) with declaration-boundary annotations for the root scroller, machine card, accessory row prefabs, progress block, and confirmation dialogs.
- The header now calls out the retained-scroll-view Unity split and the dynamic row/show-hide hazard that will need explicit state in C#.
- Next step after commit: close T624 and move to T625 (`src/slic3r/GUI/UserManager.cpp`).

- Started T625 (`src/slic3r/GUI/UserManager.cpp`) as the network-auth payload adapter.
- The file is a thin JSON parser that only reacts to `bind` success, updates the device selection through GUI singletons, and may be called from a network callback path.
- Unity mapping: typed auth-result message + main-thread completion handler; the transport layer should not own modal dialog closure or selected-machine state.

- Started T626 (`src/slic3r/GUI/UserManager.hpp`) as the declaration boundary for the same auth adapter.
- The header is intentionally small but still needs explicit comments for the non-owning `NetworkAgent*`, callback thread affinity, and the typed message bridge that Unity should use instead of view-layer JSON parsing.
- Verification target: `git diff --check -- src/slic3r/GUI/UserManager.hpp` after annotation, then commit the atomic header change and close the task.

- T627 turned out to be a namespace-only 5-line stub, so the correct classification is `skip-trivial`. The next real behavior boundary should be `UserNotification.hpp` rather than this .cpp file.

- T628 (`src/slic3r/GUI/UserNotification.hpp`) is also semantically inert: the header only exposes a small enum plus an empty constructor shell, so it was normalized from `annotate` to `skip-trivial` and marked done.
- Next recommended task after commit is T629 (`src/slic3r/GUI/WebDownPluginDlg.cpp`).

## T629 plan

- Active file is `src/slic3r/GUI/WebDownPluginDlg.cpp`, the web-based plugin download/install dialog.
- The file mixes wxWebView lifecycle, JS command dispatch, app-level download/install callbacks, and fullscreen/new-window handling, so the Unity mapping should call out a retained web-content host plus a main-thread command bridge.
- I will annotate the dialog boundary, webview event flow, script-message contract, and plugin progress callback path, then record completion evidence and commit this atomic file only.
- The page is not just informational: JS commands can cancel, restart, install, and open folders, so the Unity port needs a typed command schema with validation before it reaches app services.

- Recovery note: T629 is already marked done in `.ralph/ralph-tasks.md`, so the active file task for this iteration is T630 (`src/slic3r/GUI/WebDownPluginDlg.hpp`).
- The header is the declaration boundary for the same plugin-download dialog: it should capture ownership/lifetime of the browser widget, the exposed web callbacks, the install/download/status bridge, and the Unity split between a retained web-content host and a typed command router.
- Plan: add compact [INTENT]/[STATE]/[EVENT]/[THREAD]/[UNITY]/[PORTING_HAZARD] comments around class purpose, browser ownership, event handlers, script bridge, and progress callback state, then verify with `git diff --check` and close the task.
- T630 annotation is complete in the working tree; next step is to commit the atomic header change and move to T631 (`src/slic3r/GUI/WebGuideDialog.cpp`).

- T631 was already annotated in the source tree, but the runtime registry still had the task marked active in `.ralph/ralph-tasks.md`; I reconciled that state by treating the file as complete and syncing the task registry/handoff evidence.
- WebGuideDialog.cpp is the web-based setup wizard controller: embedded navigation, JSON script commands, async preset loading, and config/preset commit side effects all live in one modal flow.
- Current work is bookkeeping only for this iteration; the next real code task is T632 (`src/slic3r/GUI/WebGuideDialog.hpp`).

- Started T631 (`src/slic3r/GUI/WebGuideDialog.cpp`) as the web-based setup wizard controller.
- This file mixes embedded web navigation, JSON script-command handling, preset bundle import/export, and async preset loading, so the annotations need to call out the webview bridge, the shared wizard state, the worker-thread handoff, and the config/preset side effects that Unity must isolate.
- Memory search for `WebGuideDialog` returned no existing reusable pattern, so I am annotating the file directly and will record the wizard/controller pattern if it holds after verification.

- T632 is now annotated in `src/slic3r/GUI/WebGuideDialog.hpp` with class-level intent/state/event/thread/Unity/porting-hazard notes plus retained-state comments for the browser host, startup loader, and script round-trip fields.
- Verification passed with `git diff --check -- src/slic3r/GUI/WebGuideDialog.hpp`; next recommended file is T635 (`src/slic3r/GUI/WebUserLoginDialog.cpp`).

- Completed T635 (`src/slic3r/GUI/WebUserLoginDialog.cpp`) as the embedded login host / fallback error flow.
- The file now calls out the dual-mode modal shell, browser event surface, UI-thread timeout, command-style JS bridge, cached script state, and the error-page fallback.
- Next step after commit: close T635 and move to T636 (`src/slic3r/GUI/WebUserLoginDialog.hpp`).

- Completed T636 (`src/slic3r/GUI/WebUserLoginDialog.hpp`) as the declaration boundary for the same login modal.
- The header now captures retained browser/timer/session state, the event-table bridge, the UI-thread modal lifecycle, and a concrete Unity mapping to a browser-hosted login shell plus typed command router.
- Next step after commit: close T636 and move to T637 (`src/slic3r/GUI/WebViewDialog.cpp`).

- Started T637 (`src/slic3r/GUI/WebViewDialog.cpp`) as the retained browser-host and JS bridge implementation.
- The file mixes navigation gating, login polling, main-thread response reentry, developer tools, and page-to-native command handling, so the annotations emphasize the host/controller split, timer ownership, and the hardening required for Unity.
- Plan: keep the comments concentrated on the browser host boundary, script-message bridge, login refresh seam, navigation/error flow, and the source-view modal, then verify and close the task.

- Completed T638 (`src/slic3r/GUI/WebViewDialog.hpp`) as the declaration boundary for the retained browser host.
- The header now records the browser/menu/timer ownership, the navigation and JS bridge handlers, the cached script state, and the Unity split between a persistent web host shell and a typed command router.
- Next step after commit: close T638 in the runtime registry, then move to T639 (`src/slic3r/GUI/Widgets/AMSControl.cpp`).
