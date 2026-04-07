# GUI Final Review

## Artifact Inventory

The Phase 2 core documentation set is present and audit-ready:

| Artifact | Status | Notes |
| --- | --- | --- |
| `generated_documentation/gui/gui_01_architecture_overview.md` | present | Updated in Phase 3 to include `Auxiliary` and `Debug tool` in the top-level shell map. |
| `generated_documentation/gui/gui_02_screen_and_widget_inventory.md` | present | Screen inventory still matches `MainFrame::TabPosition`. |
| `generated_documentation/gui/gui_03_state_management.md` | present | State taxonomy still matches the documented shell/workspace/preset split. |
| `generated_documentation/gui/gui_04_opengl_viewport_pipeline.md` | present | Viewport remains the highest-risk subsystem. |
| `generated_documentation/gui/gui_05_event_and_callback_model.md` | present | Event/callback seams still align with source. |
| `generated_documentation/gui/gui_06_background_process_and_threading.md` | present | Threading hazards remain current. |
| `generated_documentation/gui/gui_07_unity_porting_hazards.md` | present | Hazard priorities still match the audited source. |
| `generated_documentation/gui/gui_08_external_gui_dependencies.md` | present | Dependency decisions are still actionable. |
| `generated_documentation/gui/flow_background_slicing.md` | present | Flow still matches `Plater` and `BackgroundSlicingProcess`. |
| `generated_documentation/gui/flow_viewport_input_and_render.md` | present | Flow still matches `GLCanvas3D` pass ordering and input arbitration. |
| `generated_documentation/gui/entry_point_trace.md` | present | Orientation artifact retained. |
| `generated_documentation/gui/gui_app_class.md` | present | Orientation artifact retained. |
| `generated_documentation/gui/main_window_class.md` | present | Orientation artifact retained. |

## Unresolved Ambiguities

### Classification Summary

- Total `[UNCLEAR]` annotations audited in scope: 73
- Resolved during later work: 0
- Still unresolved but low-risk: 39
- Still unresolved and porting-relevant: 34

### Resolved During Later Work

None. The later documentation pass improved context around several ambiguities, but none of the Phase 1 `[UNCLEAR]` annotations could be retired as fully resolved from source evidence alone.

### Still Unresolved And Porting-Relevant

| Source anchor | Short description | Current best hypothesis | Expected Unity impact | Recommendation |
| --- | --- | --- | --- | --- |
| `src/slic3r/GUI/3DBed.hpp:L227-L227` | Bed/model heuristics decide whether the bed should be procedural. | The viewport uses implicit bed-presence heuristics rather than one explicit mode flag. | Wrong recreation changes empty-scene behavior and platform bed rendering. | Preserve the current heuristic first, then replace it with an explicit mode only after parity tests. |
| `src/slic3r/GUI/BBLStatusBarBind.cpp:L57-L57` | Bind-status pulse hook is currently a no-op. | The UI contract exists even though the current implementation is inert. | A Unity status bar may omit a subtle state transition if this is dropped blindly. | Decide explicitly whether to remove or restore the pulse behavior during UX design. |
| `src/slic3r/GUI/BBLStatusBarBind.cpp:L97-L97` | Error-detail path is a placeholder. | The flow likely expected a richer error surface that never shipped. | Printer bind failures may lose diagnostic affordances in the port. | Audit production error paths before finalizing the bind-status UI. |
| `src/slic3r/GUI/BBLStatusBarBind.cpp:L155-L155` | Cancel wiring relies on a disabled override. | The button contract matters more than the disabled helper. | Porting the widget literally could miss cancel semantics. | Bind cancel behavior from the controller layer, not from dormant widget overrides. |
| `src/slic3r/GUI/BBLStatusBarBind.cpp:L165-L165` | Extra message label may be optional. | Some BBL messages may be vestigial. | Impacts UI chrome and printer-flow parity. | Keep the message slot configurable until bind-flow coverage is confirmed. |
| `src/slic3r/GUI/GUI_Factories.cpp:L1637-L1637` | Auto-rotate TODO has ambiguous routing intent. | The current call may be a placeholder for a more formal preparer workflow. | Object-prep actions could land in the wrong state machine. | Keep the existing route initially and document the ambiguity in implementation planning. |
| `src/slic3r/GUI/GUI_ObjectTableSettings.hpp:L77-L77` | Selection-difference counter semantics are inferred. | The field likely tracks how many selected objects diverge on one property. | Mixed-selection inspector behavior can regress subtly. | Preserve the mixed-value semantics before simplifying the table-settings model. |
| `src/slic3r/GUI/Gizmos/GLGizmoCut.hpp:L147-L147` | Connector geometry ratios are not fully explained. | The ratios likely encode visual and collision expectations for cut connectors. | Cut gizmo parity can drift if Unity chooses different mesh proportions. | Validate the ratios with sample cuts before redesigning the gizmo visuals. |
| `src/slic3r/GUI/Gizmos/GLGizmoMove.cpp:L337-L337` | Snap step units are ambiguous. | The code may mix world and screen-space expectations. | Move-gizmo snapping can feel wrong even if math compiles. | Treat snapping as a parity-critical behavior and verify with user gestures early. |
| `src/slic3r/GUI/HttpServer.cpp:L72-L72` | IO thread discards request bodies and ignores `content-length`. | The path looks intentionally minimal and possibly unsafe. | Any retained HTTP bridge needs a stricter transport contract. | Replace this behavior with a validated request parser instead of porting it literally. |
| `src/slic3r/GUI/IMSlider.hpp:L121-L121` | Multi-extruder preview lock semantics are unclear. | Slider ownership likely constrains color-change preview interaction. | Preview controls may desync from extruder-specific state. | Keep one authoritative preview-selection model and verify color-change flows early. |
| `src/slic3r/GUI/IconManager.cpp:L321-L321` | Texture/icon cleanup is only sketched. | The native implementation relies on explicit GL lifetime cleanup. | Unity still needs deterministic disposal for generated icon textures. | Keep texture lifetime explicit in the Unity asset/runtime layer. |
| `src/slic3r/GUI/IconManager.hpp:L115-L115` | `m_id` field semantics are inferred. | It likely tracks texture invalidation or rebind state. | Icon refresh bugs can appear during theme/DPI changes. | Map this to an explicit revision counter if the port keeps dynamic atlas rebuilds. |
| `src/slic3r/GUI/ImageGrid.cpp:L475-L475` | Sign-flipping math preserves scroll position during regrouping. | The grid tries to pin the viewport while item groups reshape. | Scroll behavior can feel unstable on asset-heavy pages. | Write a parity test around regrouping and scroll retention before refactoring. |
| `src/slic3r/GUI/Jobs/EmbossJob.cpp:L68-L68` | Hardcoded `0.015mm` emboss tolerance lacks rationale. | The value is likely empirically tuned. | Geometry fidelity may shift if Unity picks a new tolerance casually. | Carry the constant forward initially and revisit only with mesh-quality comparisons. |
| `src/slic3r/GUI/Jobs/NotificationProgressIndicator.cpp:L15-L15` | Native path never clears an in-progress notification. | The current HUD depends on explicit completion cleanup elsewhere. | Long-running jobs can leave stale HUD state. | Make notification teardown an explicit completion responsibility in Unity. |
| `src/slic3r/GUI/Jobs/Worker.hpp:L11-L11` | Worker queue may need priority support. | GUI job ordering probably matters more than the interface admits. | A flat async queue could reorder visible workflows incorrectly. | Reserve space for job priority or workflow lanes in the Unity async runtime. |
| `src/slic3r/GUI/MonitorBasePanel.cpp:L281-L281` | Some handlers depend on splitter state. | The monitor layout owns behavior, not just presentation. | Splitting device dashboards into separate panes can break callbacks. | Keep layout-state ownership explicit in the device dashboard controller. |
| `src/slic3r/GUI/Notebook.hpp:L104-L104` | Commented mode-button state still hints at route behavior. | Legacy mode-selection UI may have informed tab affordances. | Shell navigation ports risk deleting a subtle mode concept. | Confirm that modern builds truly no longer depend on the commented mode controls. |
| `src/slic3r/GUI/PublishDialog.cpp:L169-L169` | Publish callbacks appear to assume UI-thread delivery. | The dialog likely expects progress callbacks to be marshaled back before mutating modal state. | Porting the publish flow without thread guarantees can corrupt modal state. | Route publish progress through the same main-thread dispatcher as slicing/upload flows. |
| `src/slic3r/GUI/PublishDialog.cpp:L170-L170` | Publish worker progress reaches the dialog through the dialog/plater event chain. | The current flow likely depends on one established UI-thread callback route. | Replacing the path casually can desynchronize the publish UI. | Keep one explicit callback route for publish progress and completion. |
| `src/slic3r/GUI/RecenterDialog.cpp:L73-L73` | Localized CJK width check is brittle. | The current heuristic compensates for layout differences with a byte-level shortcut. | Native text layout changes can regress multilingual dialogs. | Replace the heuristic with measured text/layout behavior in Unity. |
| `src/slic3r/GUI/RemovableDriveManager.hpp:L155-L155` | Save-path state is only meaningful after verification. | The member becomes authoritative only after `set_and_verify_last_save_path()`. | Export/save UX can mis-handle removable-media defaults. | Preserve the verified-path concept as explicit state, not a nullable convenience field. |
| `src/slic3r/GUI/RemovableDriveManager.hpp:L163-L163` | Initial authoritative save-path assignment is only implied. | `init()` or the verify path probably establishes the first valid value. | Save/export defaults can drift during startup or removable-media changes. | Make initialization of the verified path explicit in the service contract. |
| `src/slic3r/GUI/SavePresetDialog.cpp:L435-L435` | Three radio choices depend on downstream printer persistence semantics. | The dialog is coupled to printer profile side effects. | Save/overwrite decisions can mutate more than the modal suggests. | Rebuild this dialog around explicit save intents and side-effect descriptions. |
| `src/slic3r/GUI/SceneRaycaster.hpp:L48-L48` | Fallback gizmos use a secondary pick-priority bucket. | The raycaster biases grab targets to preserve manipulability. | Picking parity can regress even if visuals look correct. | Keep the pick-priority contract explicit in the Unity scene-query layer. |
| `src/slic3r/GUI/SelectMachinePop.cpp:L839-L839` | LAN discovery hook is stubbed out. | The popup once likely toggled SSDP/LAN discovery behavior. | Printer-discovery UX can ship incomplete if the stub is ignored. | Decide whether LAN discovery is still a product requirement before removing the hook. |
| `src/slic3r/GUI/Selection.cpp:L2192-L2192` | Selection type lattice is legacy-heavy and BBS-specific. | The classification logic carries product behavior that is not obvious from names. | Selection/edit operations can break across multiple object types. | Preserve the existing classification logic verbatim until the new selection model is proven. |
| `src/slic3r/GUI/Tabbook.hpp:L57-L57` | Public button-pointer state looks legacy but may still leak route semantics. | The header exposes notebook state more broadly than intended. | Ports may accidentally remove a hidden shell dependency. | Audit call sites before collapsing this into a private field. |
| `src/slic3r/GUI/TaskManager.cpp:L299-L299` | Remote task identifiers are only partially canonicalized. | JSON payloads are mapped into UI rows through an asymmetric ID model. | Fleet/task history views may duplicate or misgroup remote jobs. | Introduce a typed canonical task identity in any rewritten task dashboard. |
| `src/slic3r/GUI/ThermalPreconditioningDialog.cpp:L128-L128` | `stage_curr == 58` acts as a thermal-preconditioning phase code. | The workflow currently depends on a magic device-stage integer. | Device-state polling can break if the phase code is not centralized. | Replace the magic number with a named enum at the service boundary. |
| `src/slic3r/GUI/Widgets/FilamentLoad.cpp:L50-L50` | Selection/layout/hide sequence implies transient-overlay use. | The wizard host may double as an overlay-like state container. | Straight page-for-page recreation could miss visibility semantics. | Port this as an explicit workflow state machine rather than hidden-page manipulation. |
| `src/slic3r/GUI/Widgets/FilamentLoad.cpp:L193-L193` | `show` parameter is ignored. | The API looks like a compatibility shim over state-driven visibility. | Porting the method literally would preserve misleading API shape. | Collapse this into explicit state transitions in Unity. |
| `src/slic3r/GUI/Widgets/TempInput.cpp:L498-L498` | Round warning icon is recreated every paint. | The repaint path likely keeps dark-mode/theme assets synchronized. | Theme/DPI-sensitive validation rows can drift if caching is over-optimized. | Keep dynamic icon refresh behavior until theme handling is validated. |

### Still Unresolved But Low-Risk

| Source anchor | Short description | Current best hypothesis | Expected Unity impact | Recommendation |
| --- | --- | --- | --- | --- |
| `src/libvgcode/src/PathVertex.cpp:L31-L31` | `option` is vague naming for print-path event markers. | The comment is terminology debt, not behavior ambiguity. | Minimal. | Keep as terminology context only. |
| `src/libvgcode/src/Settings.cpp:L10-L10` | Data-only settings struct may or may not need validation. | Validation likely lives outside the struct. | Minimal. | Do not infer missing runtime validation without downstream evidence. |
| `src/libvgcode/src/Settings.hpp:L70-L70` | Formatting inconsistency suggests merge-artifact history. | Source hygiene issue rather than runtime behavior. | None for Unity architecture. | Ignore unless the file is actively refactored. |
| `src/libvgcode/src/Settings.hpp:L71-L71` | Right-aligned inline comments suggest contributor-history noise. | Comment style drift rather than runtime ambiguity. | None for Unity architecture. | Ignore unless the file is actively refactored. |
| `src/libvgcode/src/Shaders.hpp:L168-L168` | Platform scaling-factor rationale is undocumented. | Historical GL tuning likely drove the difference. | Low unless libvgcode is kept natively. | Revisit only if preview rendering is wrapped instead of reimplemented. |
| `src/slic3r/GUI/AmsWidgets.cpp:L168-L168` | Some `DevAmsTray` fields are currently ignored by UX. | Product chose not to expose every field. | Low. | Preserve only fields that surface in the active UX. |
| `src/slic3r/GUI/GUI_ObjectTableSettings.cpp:L114-L114` | `display_multiple` is unused downstream. | The flag is vestigial. | Low. | Drop if no runtime use appears during inspector rewrite. |
| `src/slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.cpp:L247-L247` | Assert compares `extruders_colors` to itself. | Likely a typo in debug validation. | Low. | Treat as a source-cleanup note, not a port blocker. |
| `src/slic3r/GUI/Gizmos/GLGizmoSeam.cpp:L173-L173` | Toolbar anchor flag semantics are inferred. | The flag probably just flips a window edge. | Low. | Confirm during seam-toolbar recreation. |
| `src/slic3r/GUI/ImageDPIFrame.cpp:L112-L112` | DPI hook is stubbed. | Intent is an overlay refresh on monitor-scale changes. | Low. | Rebuild with normal Unity layout scaling rather than preserving the stub. |
| `src/slic3r/GUI/MultiPrintJob.hpp:L11-L11` | Empty header lacks context. | Likely placeholder or dead declaration. | Low. | Ignore unless the surrounding subsystem is revived. |
| `src/slic3r/GUI/MultiTaskManagerPage.hpp:L112-L112` | Commented `m_sent_time` field looks unused. | The field was likely removed without fully cleaning the header. | Low. | Do not recreate dead storage. |
| `src/slic3r/GUI/NetworkTestDialog.hpp:L25-L25` | `wx/grid.h` may be unused. | Include drift rather than behavior risk. | None. | Ignore for the port. |
| `src/slic3r/GUI/NetworkTestDialog.hpp:L27-L27` | `wx/srchctrl.h` may be unused. | Include drift rather than behavior risk. | None. | Ignore for the port. |
| `src/slic3r/GUI/Notebook.cpp:L121-L121` | Compiled-out mode-button highlight block is legacy. | Old chrome behavior no longer participates in current UX. | Low. | Do not preserve unless product wants the old affordance back. |
| `src/slic3r/GUI/Notebook.cpp:L147-L147` | `m_mode_sizer` is commented out. | Deprecated shell UI. | Low. | Treat as dead code context. |
| `src/slic3r/GUI/Notebook.cpp:L277-L277` | Page-button bitmap set is commented out. | The visual path was disabled intentionally. | Low. | Keep only if shell design requires iconized tabs. |
| `src/slic3r/GUI/Notebook.hpp:L58-L58` | One notebook function looks deprecated or unused. | Header still carries unused shell history. | Low. | Avoid carrying legacy notebook internals into the port. |
| `src/slic3r/GUI/Notebook.hpp:L79-L79` | Notebook bitmap-setting path is commented out in implementation. | The header exposes a visual hook that no longer appears active. | Low. | Keep only if the new shell design needs iconized tabs. |
| `src/slic3r/GUI/RemovableDriveManager.hpp:L86-L86` | Success timing depends on platform event timing. | Callers likely trust follow-up events more than immediate returns. | Low to moderate. | Preserve event-driven completion semantics but not the exact API shape. |
| `src/slic3r/GUI/Search.cpp:L73-L73` | Dead branch sits behind an early return. | Legacy search behavior was left in place. | Low. | Omit unless a later audit finds active call paths. |
| `src/slic3r/GUI/SendSystemInfoDialog.cpp:L162-L162` | Last-sent version gate is stubbed. | Current behavior approximates a newer-build check only. | Low. | Re-spec this dialog by product/privacy requirements rather than current stub behavior. |
| `src/slic3r/GUI/SkipPartCanvas.hpp:L186-L186` | Expat callback build path is inferred. | Metadata parsing is probably synchronous. | Low by itself. | Keep as a parser-implementation note. |
| `src/slic3r/GUI/TaskManager.hpp:L109-L109` | `sent_time` and `profile_id` are shared across task types. | The DTO likely serves both upload and timelapse history rows. | Low. | Clean up naming and split task DTOs if the dashboard is rebuilt. |
| `src/slic3r/GUI/TaskManager.hpp:L135-L135` | Timeout comment says 60 seconds while the value is 180. | The inline comment is stale. | Low. | Treat as documentation drift when rewriting the task model. |
| `src/slic3r/GUI/UnsavedChangesDialog.cpp:L1071-L1071` | Hover/help helper returns immediately. | UX affordance was disabled but code remained. | Low. | Do not recreate dormant help-line behavior unless requested. |
| `src/slic3r/GUI/UnsavedChangesDialog.hpp:L91-L91` | `m_container` wxGTK workaround is underexplained. | GTK container quirks drove the flag. | Low in Unity. | Ignore unless a retained native GTK path survives. |
| `src/slic3r/GUI/UpdateDialogs.cpp:L88-L88` | Checkbox-backed opt-out flow is commented out. | The modern update path stopped exposing this option. | Low. | Rebuild update dialogs from current product requirements, not the dormant branch. |
| `src/slic3r/GUI/UpdateDialogs.cpp:L306-L306` | Legacy update UX now appears to be only a finalized shell. | The old flow was reduced without deleting all scaffolding. | Low. | Preserve only the active update flow in the port. |
| `src/slic3r/GUI/UserManager.cpp:L31-L31` | Bind-success parser ignores most payload variants. | The path is intentionally narrow. | Low unless the auth flow is rewritten in detail. | Re-spec the auth DTOs instead of porting this permissive parser. |
| `src/slic3r/GUI/WebDownPluginDlg.cpp:L288-L288` | Info-bar path is intentionally disabled. | Old plugin-install UI was simplified. | Low. | Preserve only the active install flow. |
| `src/slic3r/GUI/WebGuideDialog.cpp:L1571-L1571` | Status UI remains a stub. | The workflow lacks a polished feedback layer. | Low to moderate. | Add feedback based on onboarding requirements rather than mirroring the stub. |
| `src/slic3r/GUI/Widgets/AnimaController.cpp:L83-L83` | Playback branch looks permanently enabled. | Feature gating was removed or never finished. | Low. | Keep the always-on behavior unless product requirements say otherwise. |
| `src/slic3r/GUI/Widgets/LabeledStaticBox.hpp:L23-L23` | `PickDC()` backend choice is inferred. | Platform-specific paint backend helper. | Low. | Rebuild as a standard retained UI container. |
| `src/slic3r/GUI/Widgets/PopupWindow.hpp:L37-L37` | GTK activation-helper name is misspelled. | Naming issue only. | None. | Ignore beyond preserving dismissal semantics. |
| `src/slic3r/GUI/Widgets/ScrolledWindow.cpp:L78-L78` | Old scroll math is commented out. | Live behavior uses the newer path entirely. | Low. | Port the active path only. |
| `src/slic3r/GUI/Widgets/StepCtrl.cpp:L102-L102` | Index `0` likely acts as a fallback first step. | Defensive default for step selection. | Low. | Preserve the fallback until step-controller tests exist. |
| `src/slic3r/GUI/Widgets/TabCtrl.hpp:L75-L75` | Visibility API is linear over a non-linear control. | Historical API shape outlived the internal model. | Low. | Replace with a clearer tab-state API in Unity. |
| `src/slic3r/GUI/Widgets/TempInput.cpp:L722-L722` | Empty handlers may only satisfy event-table structure. | Reserved/no-op hooks. | Low. | Omit unless active interaction requires them. |

## Source Reference Validation Summary

- Audited all 322 line-based source anchors found under `generated_documentation/gui/`.
- Result: 322 of 322 anchors resolved to existing files and valid line ranges.
- Spot-checked high-risk references covering shell startup, viewport rendering, background slicing, worker marshaling, and browser bridges.
- No stale or broken source references required correction in Phase 3.

## Documentation Consistency Summary

- All required Phase 2 core documents still exist.
- `gui_07_unity_porting_hazards.md` remains consistent with `gui_01_architecture_overview.md` and `gui_06_background_process_and_threading.md` on the top P1 risks: viewport architecture, wx idle/paint worker pumping, blocking UI-task marshaling, and `GUI_App` global reachability.
- Viewport claims in `gui_04_opengl_viewport_pipeline.md` and `flow_viewport_input_and_render.md` remain aligned with the annotated `GLCanvas3D` and `3DScene` source.
- Screen/state docs remain aligned after correcting one discrepancy: `gui_01_architecture_overview.md` now includes the `Auxiliary` and `Debug tool` shell tabs already documented in `gui_02_screen_and_widget_inventory.md` and present in `MainFrame::TabPosition`.
- No additional Phase 2 source-correction drift was discovered during this audit.

## Critical Blockers

1. The viewport is still a custom rendering runtime, not a normal widget. The Unity port must decide pass ordering, picking, overlay composition, and G-code preview architecture before deep workspace UI work starts.
2. Background slicing still depends on wx-specific completion pumping and synchronous UI participation. A single explicit main-thread dispatcher and cancellable workflow model are mandatory before porting async flows.
3. `GUI_App` and `wxGetApp()` still form a broad global reachability surface. The port needs a bootstrap/service boundary before rebuilding screens.
4. Browser-backed and preview-heavy subsystems still require product decisions: native-vs-web replacements, libvgcode strategy, and retained device/network backend shape.

## Recommended Implementation Order

1. Define the Unity bootstrap, service container, and main-thread dispatcher.
2. Establish shell routing, top-level page state, and shared modal/overlay infrastructure.
3. Port device, project, and calibration shells behind typed service boundaries.
4. Port preset/stateful form workflows with explicit edit-session models.
5. Build workspace domain/state services and then the Prepare/Preview shell.
6. Implement the viewport/G-code rendering architecture after the surrounding state and async contracts are stable.

## Estimated Effort By Subsystem

| Subsystem | Estimated effort | Notes |
| --- | --- | --- |
| App shell, routing, shared services | 2-3 weeks | Foundational; blocks most other work. |
| Device/project/web shell replacement | 4-6 weeks | Depends on backend retention decisions. |
| Preset editing and state-model rewrite | 4-6 weeks | Dirty-state and compatibility flows remain non-trivial. |
| Workspace shell and slicing orchestration | 6-8 weeks | Large controller/state split even before final rendering work. |
| Viewport and G-code preview | 8-12+ weeks | Highest technical-risk area with the most parity hazards. |

## Suggested First Unity Milestones

1. Bootstrap scene with routing, settings, localization, and one explicit main-thread dispatcher.
2. Non-viewport shell prototype that can navigate between Home, Device, Project, and Calibration placeholders.
3. Async proof-of-concept reproducing background-job progress, cancellation, and main-thread continuations without wx-style idle pumping.
4. Viewport spike that proves pass ordering, picking, and overlay composition on a reduced scene before full workspace porting begins.

## Known Assumptions And What Should Be Verified First During Implementation

- Assumption: browser-backed surfaces may be replaced selectively rather than uniformly. Verify product ownership for Home, Project, Printer, and update-related web flows first.
- Assumption: the Unity port can centralize async completion on one dispatcher. Verify retained native libraries do not require incompatible callback threading rules.
- Assumption: viewport and G-code preview can share camera/session context while using distinct render implementations. Verify this with an early preview spike.
- Assumption: current low-risk `[UNCLEAR]` notes are mostly dormant or cosmetic. Verify against real user workflows before deleting legacy affordances.
- Assumption: the existing screen inventory is complete enough for implementation sequencing. Verify whether Auxiliary and Debug Tool are still product-required or internal-only.
