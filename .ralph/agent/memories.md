# Memories

## Patterns

### mem-1774735565-184a
> AMSItem.cpp is the composite AMS dashboard: it owns tray cards, refresh indicators, humidity badges, route overlays, and selection fan-out, so Unity should split it into a retained root prefab with child tray/route view controllers.
<!-- tags: gui, unity, widgets, ams | created: 2026-03-28 -->

### mem-1774685334-3cdf
> AMSControl.hpp is the declaration boundary for the retained AMS dashboard: it caches current slot/page state, owns popup/controller pointers, and should port as a presenter MonoBehaviour with separate overlay controllers.
<!-- tags: gui, unity, widgets, ams | created: 2026-03-28 -->

### mem-1774684653-cce1
> AMSControl.cpp is a stateful AMS dashboard: it owns preview pages, item widgets, humidity popups, and the load/unload event bridge, so Unity should model it as a presenter with retained slot groups and explicit selection/state transitions.
<!-- tags: gui, unity, widgets, ams | created: 2026-03-28 -->

### mem-1774684358-1b8d
> WebViewDialog.hpp is the declaration boundary for the embedded browser host: it owns the browser, menu chrome, login timer, and cached script state, so Unity should model it as a persistent web-host controller plus a typed command router.
<!-- tags: gui, unity, webview, header | created: 2026-03-28 -->

### mem-1774683866-a0c8
> WebViewDialog.cpp is a retained browser host: it mixes navigation gating, login polling, page-to-native JS commands, and queued response delivery, so Unity should split the host shell from a typed command router and main-thread response service.
<!-- tags: gui, unity, webview, threading | created: 2026-03-28 -->

### mem-1774683455-52a7
> WebUserLoginDialog.hpp is the declaration boundary for the embedded login modal: it retains browser/timer/auth state, routes webview events through the UI thread, and should port to a modal browser shell with a typed command bridge.
<!-- tags: gui, unity, webview, auth, threading | created: 2026-03-28 -->

### mem-1774683100-d04c
> WebUserLoginDialog.cpp is a dual-mode modal login host: it either shows a network-plugin-missing notice or embeds a wxWebView auth flow. JS messages are the command surface (login setup, autotest token, localhost handoff, third-party login, new_webpage), and modal completion is carefully deferred via EndModal + CallAfter to avoid reentrancy.
<!-- tags: gui, unity, webview, auth, threading | created: 2026-03-28 -->

### mem-1774681882-3462
> WebGuideDialog is the web-based setup wizard controller: embedded page JS posts typed-ish JSON commands for onboarding, while a background preset loader populates the shared profile model and posts back to the UI thread. Unity should split this into a modal browser host plus a cancellable async import service.
<!-- tags: gui, unity, webview, dialog, threading | created: 2026-03-28 -->

### mem-1774680782-d56a
> WebDownPluginDlg uses wxWebView as a retained plugin-install host: page JS sends JSON command packets that directly trigger download/install/restart/close/file-open actions through GUI_App callbacks, so a Unity port needs a typed, validated command bridge plus main-thread marshaling.
<!-- tags: gui, unity, webview, plugins | created: 2026-03-28 -->

### mem-1774679484-2513
> UserManager.cpp is a thin network-auth adapter: it only handles bind-success JSON and directly mutates GUI/DeviceManager state, so a Unity port should route typed auth-result messages through a main-thread completion handler instead of parsing in the view layer.
<!-- tags: gui, unity, network, auth | created: 2026-03-28 -->

### mem-1774678922-eca9
> UpgradePanel.hpp is the declaration boundary for the firmware-upgrade dashboard: root scroller plus machine card, accessory rows, confirm dialogs, and dynamic show/hide state. Unity should use a scrollable retained controller with reusable row prefabs and modal prompt overlays.
<!-- tags: gui, unity, dialog | created: 2026-03-28 -->

### mem-1774678502-281b
> UpgradePanel.cpp is a firmware-upgrade dashboard: the root panel hosts a MachineInfoPanel plus accessory rows (AMS, extra AMS, extension board, air pump, cutting, laser, extinguish), and the Unity split should be a retained controller with reusable machine/accessory views and a status/progress state machine.
<!-- tags: gui, unity, dialog, firmware | created: 2026-03-28 -->

### mem-1774677630-28d4
> UpdateDialogs.hpp is the declaration boundary for the update/incompatibility modal cluster: it reuses one modal shell across update, forced-update, incompatible-data, and no-update cases, with opt-out checkbox state, hyperlink events, and force-before-wizard gating.
<!-- tags: gui, unity, dialog, update | created: 2026-03-28 -->

### mem-1774677170-07d0
> UpdateDialogs.cpp groups four update-related modal flows: update notice, config update, forced incompatibility gate, and no-update info popup. Unity should split the shared update/version service from the dialog variants, because the current code mixes startup-blocking compatibility checks with lightweight informational prompts.
<!-- tags: gui, unity, dialog, update | created: 2026-03-28 -->

### mem-1774676974-98b2
> UnsavedChangesDialog.hpp is the declaration boundary for the preset-diff modal: DiffModel/DiffViewCtrl wrap wxDataViewModel semantics, while the dialog owns the action state and paired compare flow. Unity should split this into a retained diff-tree model, modal decision controller, and separate preset-comparison workspace because wxGTK container and PresetBundle snapshot behavior do not map 1:1.
<!-- tags: gui, unity, dialog, preset | created: 2026-03-28 -->

### mem-1774676456-8a68
> UnsavedChangesDialog.cpp is a modal preset-diff workflow: it builds a toggleable diff tree, a full-text compare popup, and a paired preset comparison dialog that can post a transfer event back to the host. Unity should split this into a modal controller, retained diff-tree view-model, and separate preset-save/compatibility subflows.
<!-- tags: gui, unity, dialog, preset | created: 2026-03-28 -->

### mem-1774676030-0e9a
> TickCode.hpp is the declaration boundary for tick marker editing: it owns a sorted std::set of markers, keeps a non-owning extruder-color palette pointer, and should split marker edits from color resolution in Unity.
<!-- tags: gui, unity, annotation | created: 2026-03-28 -->

### mem-1774675777-6edd
> TickCodeInfo derives marker colors from a sorted tick set and neighboring ColorChange entries; Unity should keep this as a retained marker model plus a pure color-resolution service.
<!-- tags: gui, unity, color | created: 2026-03-28 -->

### mem-1774675403-0a0a
> ThermalPreconditioningDialog.hpp is the declaration boundary for the thermal countdown modal: it owns the UI-thread wxTimer, device-id lookup, and dismiss/update controls, so Unity should use a modal overlay controller with a scheduled tick and explicit lifetime ownership.
<!-- tags: gui, unity, dialog, threading | created: 2026-03-28 -->

### mem-1774675021-483b
> ThermalPreconditioningDialog is a UI-thread modal countdown that polls DeviceManager with wxTimer ticks; stage_curr == 58 is the thermal-preconditioning gate and should become an explicit state enum in Unity.
<!-- tags: gui, unity, dialog, threading | created: 2026-03-28 -->

### mem-1774674524-ac00
> TextLinesModel rebuilds embossed text previews by slicing model volumes into per-line contours, then renders cached GLModel geometry through the flat shader; Unity should split mesh generation into a worker/job service and keep render-time drawing on a dedicated material path.
<!-- tags: gui, unity, opengl | created: 2026-03-28 -->

### mem-1774673200-968a
> Tab.hpp is the preset-controller boundary: Page owns per-preset option groups and visibility state, Tab caches preset/page/dirty-state machinery, and the Unity split should use a retained tab controller with derived printer/filament pages.
<!-- tags: gui, unity, tabs | created: 2026-03-28 -->

### mem-1774672930-fc5e
> TabButton.hpp is the declaration boundary for the custom tab button widget: it caches label/icon metrics, repaints on property changes, and forwards mouse clicks as command-style selection events, so Unity should use a retained toggle row with explicit layout invalidation.
<!-- tags: gui, unity, tabs | created: 2026-03-28 -->

### mem-1774672492-cc09
> TabButton.cpp is a custom-painted sidebar tab control: it measures label/icon content, re-emits mouse clicks as command events, and should map to a retained UI Toolkit button/toggle with separate badge visuals and parent-owned selection state.
<!-- tags: gui, unity, tabs | created: 2026-03-28 -->

### mem-1774671570-142f
> TabButtonsListCtrl is a custom left-rail notebook shell: it reparents a caller-owned sizer, paints only the selected-page chrome, posts wxCUSTOMEVT_TABBOOK_SEL_CHANGED on button click, and a Unity port should use a vertical tab rail with shared selection state and a separate footer row.
<!-- tags: gui, unity, tabs | created: 2026-03-28 -->

### mem-1774671282-eb43
> SysInfoDialog.hpp is the declaration boundary for the system-information modal: it owns the rescaled logo bitmap, summary/report panes, and clipboard command wiring, so Unity should split report text into a retained model and keep process inspection/clipboard behind services.
<!-- tags: gui, unity, dialog | created: 2026-03-28 -->

### mem-1774670979-3aaa
> SysInfoDialog.cpp is a modal system-report dialog: it should map to a retained popup/overlay with a reusable report-text service, a platform inspection service for process/graphics metadata, and a separate clipboard command path.
<!-- tags: gui, unity, dialog, clipboard | created: 2026-03-28 -->

### mem-1774670381-c481
> SyncAmsInfoDialog.hpp is the declaration boundary for the AMS sync modal: it owns printer selection, async refresh state, thumbnail preview composition, and two transparent overlay frames, so Unity should split it into a modal controller plus anchored overlay prefabs.
<!-- tags: gui, unity, annotation, threading | created: 2026-03-28 -->

### mem-1774669510-f143
> SurfaceDrag.hpp is the declaration boundary for the transient drag session: it owns only non-owning drag state, raycast filters, and fixed-transform helpers, so Unity should model it as a pointer-drag controller plus a pure geometry/service layer.
<!-- tags: gui, unity, annotation | created: 2026-03-28 -->

### mem-1774669212-16c8
> SurfaceDrag.cpp is a transient drag controller: it caches cursor offsets, raycast filters, fix transforms, and initial angle/distance, then replays pointer motion through a pure transform helper; Unity should implement this as a dedicated drag tool with a scene-query service and explicit hit-test state.
<!-- tags: gui, unity, selection | created: 2026-03-28 -->

### mem-1774668037-5c54
> StepMeshDialog couples STEP import validation, slider/text sync, and a worker-thread triangle-count preview; Unity should split the dialog from the async mesh-estimation service because the current code blocks on join during cancel/confirm.
<!-- tags: gui, unity, threading, step | created: 2026-03-28 -->

### mem-1774667694-6e46
> StatusPanel.hpp is the declaration boundary for the full machine-status dashboard: it owns extruder-image state, score upload state, task-panel actions, and async wxWebRequest thumbnail refresh, so Unity should split it into a retained dashboard root with popup/dialog services.
<!-- tags: gui, unity, annotation, threading | created: 2026-03-28 -->

### mem-1774666506-0f05
> SlicingProgressNotification.hpp is the declaration boundary for the UI-thread-owned slicing HUD: it owns the progress mode, fade policy, cancel callback, and embedded DailyTipsPanel, so the Unity port should keep it as a retained screen-space controller with a reusable child panel.
<!-- tags: gui, unity, annotation, threading | created: 2026-03-28 -->

### mem-1774666014-194f
> SlicingProgressNotification is a UI-thread-owned immediate-mode ImGui overlay: it maintains a small progress state machine, embeds Daily Tips, and should become a retained Unity HUD controller with explicit state transitions.
<!-- tags: gui, unity, notification, immediate-mode | created: 2026-03-28 -->

### mem-1774665649-c9b3
> SliceInfoPanel.hpp is the declaration boundary for the slice summary card and hover popup pair: it owns a shared transient popover, an async wxWebRequest pipeline, and should become a retained summary card plus separate popover controller in Unity.
<!-- tags: gui, unity, annotation, threading | created: 2026-03-28 -->

### mem-1774665471-696b
> SliceInfoPanel.cpp is a compact summary card plus detail popup; async thumbnail fetch must cancel stale responses before update, and the Unity port should keep preview loading outside the view tree.
<!-- tags: gui, unity, annotation, threading | created: 2026-03-28 -->

### mem-1774664516-b4ab
> SkipPartCanvas is a color-encoded OpenGL selection canvas that decodes pick images into contour meshes, toggles part states via custom wx events, and should split into a Unity controller with RenderTexture hit data plus a background 3MF metadata parser.
<!-- tags: gui, unity, opengl, annotation | created: 2026-03-28 -->

### mem-1774664219-9944
> SingleChoiceDialog.hpp is a thin declaration boundary: the dialog owns a transient ComboBox selection, exposes a raw ComboBox accessor, and should be ported as a modal dropdown controller with empty-list validation before open.
<!-- tags: gui, unity, dialog | created: 2026-03-28 -->

### mem-1774663962-4e0f
> SingleChoiceDialog.cpp is a thin modal selector wrapper: the combo is widget-owned until OK, Cancel returns -1, and the constructor assumes a non-empty choices array, so Unity should validate empty input before opening.
<!-- tags: gui, unity, dialog | created: 2026-03-28 -->

### mem-1774663654-9136
> SendToPrinter.hpp is the declaration boundary for the modal send workflow: it owns printer/device selection state, transfer-job lifetimes, timer-driven refresh, and the event surface that the cpp wires up. Unity should split it into a modal controller with an async upload service and keep protocol/state handling out of view widgets.
<!-- tags: gui, unity, dialog, threading | created: 2026-03-28 -->

### mem-1774663115-fe5d
> SendToPrinter.cpp is a modal send workflow: printer discovery, storage selection, rename validation, and upload progress all re-enter the dialog on the UI thread; Unity should model it as a modal controller plus async tunnel/upload service with explicit teardown.
<!-- tags: gui, unity, dialog, threading | created: 2026-03-28 -->

### mem-1774662170-3fd3
> SendSystemInfoDialog.cpp is a privacy-sensitive telemetry consent modal: it snapshots OS, hardware, display, and OpenGL details up front, then uploads from a worker thread while a progress dialog polls for completion. Unity should model it as a modal opt-in controller plus an async upload service with a reviewed payload schema.
<!-- tags: gui, unity, dialog, threading, privacy | created: 2026-03-28 -->

### mem-1774661877-6d3d
> SendMultiMachinePage.hpp is the declaration boundary for the send-to-multi-printer modal: it owns the recyclable device-row controller, AMS mapping state, rename/title state, and timer-driven refresh plumbing, so a Unity port should split it into a modal shell plus data-bound subviews and a main-thread refresh bridge.
<!-- tags: gui, unity, dialog, popup | created: 2026-03-28 -->

### mem-1774661067-af34
> SelectMachinePop.hpp is the declaration boundary for the send-print popup shell: keep the row widget state, refresh timer, manual click routing, and rename dialog mapping documented there; Unity should treat it as a floating controller with recycled list items and explicit main-thread dismissal events.
<!-- tags: gui, unity, popup, dialog | created: 2026-03-28 -->

### mem-1774660644-b3ac
> SelectMachinePop.cpp is a popup controller with pooled row widgets, async cloud fetches, and manual hit-testing; Unity should use a floating panel with recycled rows and standard UI event routing.
<!-- tags: gui, unity, popup | created: 2026-03-28 -->

### mem-1774660405-7e26
> SelectMachine.hpp is the declaration boundary for the send-print modal: it owns printer-selection, AMS mapping, thumbnail preview, mode-switch, and printer-header subwidgets, so Unity should split it into a modal controller with reusable preview and selector views.
<!-- tags: gui, unity, dialog | created: 2026-03-28 -->

### mem-1774659581-0d00
> SelectMachine.cpp is a monolithic send-print modal that mixes printer selection, AMS mapping, validation/status flow, and thumbnail recoloring; Unity should split it into a modal controller plus a CPU texture-compositing preview step.
<!-- tags: gui, unity, dialog, thumbnail | created: 2026-03-28 -->

### mem-1774658678-9856
> Selection.hpp is the selection controller boundary: it owns selection indices, drag caches, retained GL overlay helpers, clipboard payload state, and sibling-instance sync fan-out; Unity should model that as a controller plus explicit overlay renderer and payload-based clipboard.
<!-- tags: gui, unity, selection | created: 2026-03-28 -->

### mem-1774657337-a5a1
> Search.hpp declares the preset/object search popup boundary: keep the floating search panel, filtered list, and custom event handoff separate from the search index. Unity should use a reusable overlay controller with a persistent ListView and explicit dismissal state.
<!-- tags: gui, unity, search | created: 2026-03-28 -->

### mem-1774657010-7b78
> Search.cpp models preset-option search and object search as two popup controllers sharing fuzzy filtering, custom-painted rows, and wx event handoff; Unity should use a floating search panel with a persistent filtered list and explicit dismiss/focus handling.
<!-- tags: gui, unity, annotation | created: 2026-03-28 -->

### mem-1774603084-cb06
> SceneRaycaster uses bucket-ordered mesh raycasting with a sticky selected-volume bias and a debug-only OpenGL hit overlay; Unity should preserve pick priority explicitly in a scene-query service.
<!-- tags: gui, unity, raycasting | created: 2026-03-27 -->

### mem-1774602321-2d97
> SavePresetDialog.hpp is the declaration boundary for the preset-save modal: keep row ownership, validation flags, printer-context state, and Unity-mapping notes in the header; the save flow can trigger overwrite and printer/project side effects from one confirm action.
<!-- tags: gui, unity, dialog, preset | created: 2026-03-27 -->

### mem-1774602034-4a17
> SavePresetDialog.cpp uses per-row live validation, project-embedded toggle state, and a printer-binding action panel; warning acceptance can delete the existing preset and trigger cloud cleanup, so Unity needs a modal controller with explicit async side effects.
<!-- tags: gui, unity, dialog, preset, threading | created: 2026-03-27 -->

### mem-1774601068-786b
> SafetyOptionsDialog is a compact safety modal that directly dispatches MachineObject/PrintOptions commands from UI toggles; the unavailable idle-heating state is explained via a timer-driven popup toast, so Unity should use a modal settings panel with an anchored overlay toast rather than a separate native popup.
<!-- tags: gui, unity, dialog, porting | created: 2026-03-27 -->

### mem-1774600646-742b
> RemovableDriveManager.hpp defines the declaration boundary for the removable-drive service: cached drive snapshots, update/eject events, polling worker ownership, and macOS callback bridge all belong behind a platform service with main-thread UI dispatch in Unity.
<!-- tags: gui, unity, threading | created: 2026-03-27 -->

### mem-1774599749-961e
> RemovableDriveManager mixes platform-specific drive discovery with a polling worker thread and OS callbacks; Unity should use a cached service plus main-thread completion events.
<!-- tags: gui, unity, threading | created: 2026-03-27 -->

### mem-1774599147-2a93
> ReleaseNote.hpp groups multiple modal update/confirmation dialogs: webview-backed release notes, dynamic error prompts, and an async IP onboarding wizard. Unity port should split them into reusable modal controllers and preserve the worker-thread handoff for IP checks.
<!-- tags: gui, unity, modal, threading | created: 2026-03-27 -->

### mem-1774598719-3415
> ReleaseNote.cpp bundles several update/confirmation dialogs: markdown release notes, webview-backed version preview, and printer/IP setup flows. The Unity port should split them into reusable modal controllers, with async task/coroutine handling for the network probe and web image fetches.
<!-- tags: gui, unity, modal, threading | created: 2026-03-27 -->

### mem-1774598508-b438
> RecenterDialog.hpp is the declaration boundary for a custom-painted modal dialog; keep ownership, event, and Unity migration notes in the header while leaving pixel-measurement hazards in the cpp.
<!-- tags: gui, unity, annotation, header | created: 2026-03-27 -->

### mem-1774598082-f9c7
> RecenterDialog.cpp is a custom-painted DPIDialog: owner-drawn wxPaintDC rendering, hand-wrapped localized text, and DPI-rescaled home icon state. Unity port should use a modal confirmation controller with a shared icon asset and locale-aware layout helper instead of the current width-measurement heuristic.
<!-- tags: gui, unity, annotation | created: 2026-03-27 -->

### mem-1774597298-62af
> RammingChart.cpp is a retained-state wx chart: paint(), drag handlers, and recalculate_line() form one immediate-mode control, so a Unity port should use a custom chart controller with separate curve model, sampled preview cache, and change-event callback.
<!-- tags: gui, unity, annotation | created: 2026-03-27 -->

### mem-1774596584-7d37
> PublishDialog.hpp is a thin declaration layer for the publish flow: it exposes a non-owning Plater* back-pointer, sticky cancel state, and stepper/progress UI methods that should map to a modal Unity controller with an async progress overlay.
<!-- tags: gui, unity, publish | created: 2026-03-27 -->

### mem-1774596198-fe0e
> PublishDialog uses queued EVT_PUBLISH events plus wxEventLoopBase::YieldFor reentry; a Unity port should model it as a modal progress state machine with async/coroutine marshaling.
<!-- tags: gui, unity, publish | created: 2026-03-27 -->

### mem-1774595899-fa60
> src/slic3r/GUI/calib_dlg.hpp groups several modal calibration dialogs that share Calib_Params + non-owning Plater* state; Unity port should factor a shared calibration view-model/controller and keep per-test dispatch logic separate.
<!-- tags: gui, unity, calibration | created: 2026-03-27 -->

### mem-1774595450-ae77
> ProjectPanel is a wxWebView + AuxiliaryPanel bridge: it loads bundled file:// HTML, relays JSON script messages, and scans auxiliary asset folders on a worker thread before posting UI updates back via CallAfter.
<!-- tags: gui, webview, threading, unity | created: 2026-03-27 -->

### mem-1774594971-c054
> ProjectDirtyStateManager.hpp is annotated with class-level [INTENT]/[EVENT]/[UNITY]/[PORTING_HAZARD] comments and member-level [STATE] notes; dirty-state comparison is snapshot-based across plater, presets, and project_config.
<!-- tags: gui, annotation, unity, state | created: 2026-03-27 -->

### mem-1774594677-4dae
> calib_dlg.cpp groups multiple modal calibration forms that directly call Plater calibration methods; Unity port should treat them as controller-backed modal panels with firmware-aware axis mirroring/hiding.
<!-- tags: gui, unity, calibration | created: 2026-03-27 -->

### mem-1774582388-03fa
> Header file annotation pattern: Insert class-level [INTENT], [STATE], [EVENT], [UNITY], [PORTING_HAZARD] comments after includes, before namespace. Use sed for surgical edits when edit tool fails.
<!-- tags: gui, annotation, unity, header | created: 2026-03-27 -->

### mem-1774581101-7879
> When edit tool fails due to file modified errors, use sed -i to insert annotation comments at specific line numbers. This works for adding [INTENT], [STATE], etc. tags to C++ headers.
<!-- tags: gui, annotation, tooling | created: 2026-03-27 -->

### mem-1774580460-aa88
> Empty source files (0 lines) can be marked as skip-trivial; they contain no GUI logic and have no porting impact.
<!-- tags: gui, skip | created: 2026-03-27 -->

### mem-1774478311-f0ae
> OG_CustomCtrl uses custom wxDC drawing for OptionsGroup UI; Unity replacement requires UI Toolkit custom VisualElement or IMGUI with careful layout management.
<!-- tags: gui, custom-drawing, unity, porting | created: 2026-03-25 -->

### mem-1774420576-b412
> Annotation pattern for UI Panel classes: [INTENT] class purpose, [STATE] important UI state variables, [UNITY] concrete migration mapping for wxWidgets components to UI Toolkit.
<!-- tags: gui, annotation, unity | created: 2026-03-25 -->

### mem-1773981659-cd27
> SKIP_TRIVIAL: src/slic3r/GUI/AboutDialog.hpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981636-e5ed
> SKIP_TRIVIAL: src/slic3r/GUI/AboutDialog.cpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981616-d20e
> SKIP_TRIVIAL: src/slic3r/GUI/3DBed.cpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981590-bd5c
> SKIP_TRIVIAL: src/slic3r/GUI/2DBed.hpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773981566-14a6
> SKIP_TRIVIAL: src/slic3r/GUI/2DBed.cpp
<!-- tags: gui, annotation, skip | created: 2026-03-20 -->

### mem-1773855069-f68b
> MainFrame.cpp annotated: maps to MainUIController managing the main application panels (Prepare, Preview, Monitor, etc.). Includes complex configuration change propagation and parallel thumbnail loading.
<!-- tags: gui, unity, mainframe, threading | created: 2026-03-18 -->

### mem-1773854191-51bd
> MainFrame.hpp annotated: Maps to MainUIController MonoBehaviour managing various UI Panels.
<!-- tags: gui, unity, mainframe | created: 2026-03-18 -->

### mem-1773803554-99cc
> wxWidgets to Unity porting: Use [UNITY] tag with specific component names (e.g., MonoBehaviour, UnityWebRequest, Job System)
<!-- tags: unity, porting, wxwidgets | created: 2026-03-18 -->

### mem-1773637307-3ea2
> Voronoi offset operations require careful handling of distance parameters and polygon counts
<!-- tags: voronoi, offset, polygons | created: 2026-03-16 -->

### mem-1773637302-2f9e
> Voronoi tests use Boost Polygon library with specific issue tickets (#12067, #12707, #12903, #12139)
<!-- tags: voronoi, boost, issues | created: 2026-03-16 -->

## Decisions

## Fixes

### mem-1774682896-6b59
> failure: cmd=/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools task list --format json | python - <<'PY' ...\nPY, exit=1, error=JSONDecodeError from empty stdin; next=parse task list output from saved file or use a direct JSON parse without stdin confusion
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774682239-61df
> failure: cmd=/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools task list --format json | python - <<'PY'\nimport sys, json\nitems=json.load(sys.stdin)\nPY, exit=1, error=JSONDecodeError from empty stdin; cmd=/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools task list --status in_progress --format table | python - <<'PY'\nimport sys\nfor line in sys.stdin:\n    if 'WebGuideDialog' in line:\n        print(line.rstrip())\nPY, exit=1, error=SyntaxError from piping table output into heredoc python; next=parse task output via temp file or table text, not JSON
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774673200-a877
> failure: cmd=read /mnt/vm_shared/slicerProject/OrcaSlicerDocumented_tests/src/slic3r/GUI/Widgets/TabButton.hpp, exit=1, error=File not found; correct file is src/slic3r/GUI/TabButton.hpp, next=use the actual repo-relative path or glob before reading
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774669621-68df
> failure: cmd=/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools task list --format json | python - <<'PY'\nimport sys, json\nitems=json.load(sys.stdin)\nPY, exit=1, error=JSONDecodeError from empty stdin, next=inspect task list output format before piping
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774664045-93eb
> failure: cmd="/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools task start task-1773880087-1f?", exit=1, error=zsh glob pattern mistake in task id, next=start the intended task with the exact runtime id task-1773880087-aff1
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774661946-6480
> failure: cmd=ralph tools skill load ralph-tools, exit=1, error=Skill "ralph-tools" not found; available skills: none, next=proceed without loading the skill and use existing task/memory tooling
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774661014-2997
> failure: cmd=skill load ralph-tools, exit=1, error=Skill "ralph-tools" not found; available skills: none, next=proceed without loading a skill
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774657753-8baa
> failure: cmd=/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools skill load ralph-tools, exit=1, error=Skill "ralph-tools" not found; available skills: none, next=proceed without loading a skill
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774657490-2aee
> failure: cmd=apply_patch scratchpad update, exit=1, error=Failed to find expected lines in .ralph/agent/scratchpad.md, next=read the file first and patch against the actual current lines
<!-- tags: tooling, error-handling | created: 2026-03-28 -->

### mem-1774595683-7aa6
> failure: cmd=ralph tools task start gui:T188, exit=1, error=Task gui:T188 not found, next=start the reused task by its returned id task-1773880086-cb14
<!-- tags: tooling, error-handling | created: 2026-03-27 -->

### mem-1774594784-7833
> failure: cmd=/home/admin/.config/nvm/versions/node/v24.14.0/lib/node_modules/@ralph-orchestrator/ralph-cli/node_modules/.bin_real/ralph tools task start T555, exit=1, error=Task T555 not found, next=resolve the runtime task id via task list before starting the Phase 1 task
<!-- tags: tooling, error-handling | created: 2026-03-27 -->

### mem-1774579653-d970
> failure: cmd=edit, error=File modified since last read (even after re-read) on src/slic3r/GUI/calib_dlg.cpp, next=edit tool inconsistent with GUI files; skip annotation for now and move to another task
<!-- tags: gui, tooling, edit | created: 2026-03-27 -->

### mem-1774512577-2f42
> failure: cmd=edit, error=File modified since last read on PrintHostDialogs.hpp, next=moving to T547
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774512486-ae2d
> failure: cmd=edit, error=File modified since last read on PrintHostDialogs.cpp, next=moving to T546
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774511836-0967
> failure: cmd=edit, error=File modified since last read on PrinterFileSystem.cpp/.h, next=marking as blocked due to file locking issues, moving to next available task
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774510376-8889
> failure: cmd=edit, error=File modified since last read (even after re-read) on src/slic3r/GUI/PrinterCloudAuthDialog.cpp, next=annotation cannot proceed due to persistent lock or file system conflict, skipping T540
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774510118-5779
> failure: cmd=edit, error=File modified since last read (even after re-read) on src/slic3r/GUI/PresetHints.hpp, next=annotation cannot proceed due to persistent lock or file system conflict, skipping T539
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774509774-4b39
> failure: cmd=edit/write, error=File modified/LSP errors despite file existing, next=annotate in smaller, more surgical edits OR check tool environment/locking. Marking tasks blocked until tooling stabilizes.
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774509360-c640
> failure: cmd=edit, error=File modified since last read (even after re-read), next=edit tool seems inconsistent or failing on GUI files, skipping for now
<!-- tags: gui, edit, tooling | created: 2026-03-26 -->

### mem-1774508513-ea80
> failure: cmd=edit/write, error=File modified since last read, next=the edit tool seems inconsistent or failing on GUI files, skipping for now and continuing Phase 1
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774508237-72b3
> failure: cmd=edit, error=File modified since last read, next=the edit tool seems inconsistent or failing on GUI files, skipping for now and continuing Phase 1
<!-- tags: gui, edit, tooling | created: 2026-03-26 -->

### mem-1774507782-6c00
> failure: cmd=edit, error=File modified since last read (even after re-read), next=inspect if lock file exists or if multiple iterations are corrupting the file state.
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774507126-4025
> failure: cmd=edit, error=File modified since last read (even after re-read), next=inspect if lock file exists or if multiple iterations are corrupting the file state.
<!-- tags: gui, tooling, edit | created: 2026-03-26 -->

### mem-1774420863-9009
> failure: cmd=edit, error=LSP compilation errors (inconsistent file state or header removal), next=annotate in smaller, more surgical edits
<!-- tags: gui, tooling, edit | created: 2026-03-25 -->

### mem-1774417597-ab4d
> failure: src/slic3r/GUI/Jobs/OAuthJob.cpp is corrupted, contains duplicated and mangled code at the end of the file, next=require developer to clean up the file before annotation
<!-- tags: gui, corruption | created: 2026-03-25 -->

### mem-1774402745-eb20
> failure: cmd=read src/slic3r/GUI/DPIFrame.cpp, error=File not found, next=reconcile handoff and manifest as this file is missing
<!-- tags: gui, missing-file | created: 2026-03-25 -->

### mem-1774279479-de76
> failure: cmd=cmake -S . -B build -DCMAKE_BUILD_TYPE=Release, error=source directory does not contain CMakeLists.txt, next=confirm build instructions or skip runtime tests for documentation-only work
<!-- tags: tooling, build | created: 2026-03-23 -->

### mem-1774268687-da18
> failure: cmd=cmake -S . -B build -DCMAKE_BUILD_TYPE=Release, error=source directory does not contain CMakeLists.txt, next=verify build instructions or skip build for this repo
<!-- tags: tooling, build | created: 2026-03-23 -->

### mem-1774054791-9e67
> failure: cmd=read src/slic3r/GUI/Files/SVG.hpp, error=File not found, next=confirm manifest entry is stale or remove task
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774054728-14ba
> failure: cmd=read src/slic3r/GUI/Files/SVG.cpp, error=File not found, next=confirm manifest or mark gui:T201 blocked
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774054659-a328
> failure: cmd=read src/slic3r/GUI/Jobs/SVGFileJob.hpp, error=File not found, next=confirm manifest or mark gui:T200 blocked
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774053407-7791
> failure: cmd=read src/slic3r/GUI/Jobs/SVGFileJob.cpp, error=File not found, next=verify actual SVG job files or mark gui:T199 blocked
<!-- tags: gui, missing-file | created: 2026-03-21 -->

### mem-1774050430-757b
> failure: cmd=read src/slic3r/GUI/Jobs/RotoptJob.hpp, error=File not found, next=align tasks to RotoptimizeJob files
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774050410-47df
> failure: cmd=read src/slic3r/GUI/Jobs/RotoptJob.cpp, error=File not found, next=confirm whether the job renamed to Rotoptimize or document blocker
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774050367-c43a
> failure: cmd=read src/slic3r/GUI/Jobs/MedialAxisJob.hpp, error=File not found, next=check if header exists elsewhere or mark T194 blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047878-3f83
> failure: cmd=read src/slic3r/GUI/Jobs/LightJob.hpp, error=File not found, next=check for renamed LightJob classes or mark gui:T192 blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047667-757c
> failure: cmd=read src/slic3r/GUI/Jobs/MedialAxisJob.cpp, error=File not found, next=confirm path or mark gui:T193 as blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047571-33c9
> failure: cmd=read src/slic3r/GUI/Jobs/LightJob.cpp, error=File not found, next=confirm actual path or mark gui:T191 as blocked
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774047463-8c02
> failure: cmd=read src/slic3r/GUI/Jobs/JobList.hpp, error=File not found, next=confirm correct path or raise blocker
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045921-8109
> failure: cmd=read src/slic3r/GUI/Jobs/JobList.cpp, error=File not found, next=confirm if JobList exists elsewhere or mark gui JobList task as failed
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045479-639a
> failure: cmd=read src/slic3r/GUI/Jobs/Job.cpp, error=File not found, next=confirm whether Job.cpp exists elsewhere or adjust tasks
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045265-c455
> failure: cmd=read src/slic3r/GUI/Jobs/EmbossUpdateJob.hpp, error=File not found, next=check if header exists in repo or update manifest
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1774045174-d236
> failure: cmd=read src/slic3r/GUI/Jobs/EmbossUpdateJob.cpp, error=File not found, next=confirm file path or task manifest and either create skip entry or locate actual file
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1773994151-0897
> failure: cmd=read src/slic3r/GUI/PalmTree.hpp, error=File not found. task-1773880086-94d5 failed.
<!-- tags: gui, missing-file | created: 2026-03-20 -->

### mem-1773988211-13e6
> failure: cmd=read src/slic3r/GUI/PalmTree.cpp, error=File not found and no PalmTree match under src, next=fail stale runtime task and reconcile runtime queue with .ralph/ralph-tasks.md before selecting next annotation target
<!-- tags: gui, missing-file, tasking | created: 2026-03-20 -->

### mem-1773981545-7bab
> failure: cmd=edit, error=LSP compilation errors (inconsistent file state or header removal/missing includes), next=annotate in smaller, more surgical edits
<!-- tags: gui, tooling, edit | created: 2026-03-20 -->

### mem-1773963015-6a1e
> failure: cmd=edit, error=LSP compilation errors (inconsistent file state or header removal), next=annotate in smaller, more surgical edits
<!-- tags: gui, tooling, edit | created: 2026-03-19 -->

### mem-1773962783-01f5
> File src/slic3r/GUI/DPIFrame.cpp listed in task manifest does not exist in src/slic3r/GUI/.
<!-- tags: gui, missing-file | created: 2026-03-19 -->

### mem-1773799068-f3c8
> failure: cmd=git add agent_journal_gui.md .ralph/agent/scratchpad.md && git diff --cached -- agent_journal_gui.md .ralph/agent/scratchpad.md, exit=128, error=Unable to create .git/index.lock because a lock file already exists, next=inspect whether the lock is stale before retrying git staging
<!-- tags: git, error-handling, tooling | created: 2026-03-18 -->

### mem-1773637309-e58b
> Missing Voronoi vertices can be repaired via rotation-based mechanism with angles π/6, π/5, π/7, π/11
<!-- tags: voronoi, repair, rotation | created: 2026-03-16 -->

## Context

### mem-1774477084-669f
> T177 annotate: src/slic3r/GUI/BedShapeDialog.cpp completed (verified existing annotations). T180 started.
<!-- tags: gui, annotation, progress | created: 2026-03-25 -->
