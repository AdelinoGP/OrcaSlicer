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
