## 2026-03-28

- Picked Phase 1 task T679 for `src/slic3r/GUI/Widgets/RadioGroup.cpp`.
- This widget is a composite radio-list: bitmap + text per option, wrapped keyboard navigation, hover/focus styling, and a custom radio-selection event.
- Plan: annotate the cpp with explicit `[INTENT]`, `[STATE]`, `[EVENT]`, `[UNITY]`, and porting-hazard notes, then record completion evidence and move to T680.
- Completed the annotation pass and recorded evidence in the handoff; next step is commit + task close for T679.

- Picked Phase 1 task T680 for `src/slic3r/GUI/Widgets/RadioGroup.hpp` after the cpp task.
- The header now carries the declaration-boundary intent, state ownership, event surface, and Unity migration notes that the cpp implementation relies on.
- Next step: commit this atomic header annotation, close T680, and move to T681.

- Picked Phase 1 task T681 for `src/slic3r/GUI/Widgets/RoundedRectangle.cpp`.
- The file is a minimal custom-painted wxWindow: constructor stores fill/border mode, color, and radius; paint handler redraws from current bounds.
- Unity mapping is straightforward: a retained rounded-rect control with style-driven fill/outline behavior, plus a small hazard note for the ad-hoc integer mode.
- Next step: commit the annotation, close T681, and hand off T682 for the header boundary.

- Picked Phase 1 task T682 for `src/slic3r/GUI/Widgets/RoundedRectangle.hpp`.
- This declaration boundary keeps the retained style inputs and paint-only surface explicit for the Unity port.
- Next step: verify the header annotation, record the handoff evidence, close T682, and continue to the next widget file.

- Picked Phase 1 task T683 for `src/slic3r/GUI/Widgets/Scrollbar.cpp`.
- The widget is a custom-painted scroll controller: it computes thumb geometry from virtual vs. visible size, captures mouse drags, and forwards scroll positions back into `ScrolledWindow` on wheel/drag input.
- Unity mapping should be a retained scroll bridge with a normalized scroll model, not a pixel-faithful port of the current math.
- Next step: commit this atomic annotation, close T683, and hand off T684 for `Scrollbar.hpp`.

- Picked Phase 1 task T684 for `src/slic3r/GUI/Widgets/Scrollbar.hpp`.
- The header now carries the declaration-boundary intent, cached scroll state, and Unity migration notes that the cpp relies on.
- Next step: record completion evidence, close T684, and move to T685.

- Picked Phase 1 task T685 for `src/slic3r/GUI/Widgets/ScrolledWindow.cpp`.
- This composite wrapper owns the content viewport plus custom scrollbars, mirrors virtual size into wxScrolled and the custom chrome, and resizes by hand to hide/show scrollbars as content fits.
- The porting risk is the tight coupling between splitter geometry, SetViewStart mirroring, and scrollbar refresh/update calls; Unity should model this as one retained controller with shared scroll state.
- Next step: commit the annotation, close T685, and hand off T686 for `ScrolledWindow.hpp`.

- Picked Phase 1 task T686 for `src/slic3r/GUI/Widgets/ScrolledWindow.hpp`.
- The header is the declaration boundary for the composite scroll wrapper: it exposes the hosted panel, the mirrored scrollbar/splitter state, and the wheel/size/scroll event surface that the cpp drives.
- Unity mapping needs a single retained scroll-container controller with a shared normalized scroll model plus separate chrome prefabs; the biggest hazard is the manual splitter and SetViewStart synchronization logic.

- Picked Phase 1 task T687 for `src/slic3r/GUI/Widgets/SideButton.cpp`.
- This control is a skinned button-like wxWindow with optional icon+label content, state-colored surfaces, manual min-size math, and explicit mouse capture/release click translation.
- Plan: annotate the render/layout/event boundaries with concrete `[INTENT]`, `[STATE]`, `[EVENT]`, `[UNITY]`, and porting-hazard notes, then record evidence and move to the next widget task.

- Picked Phase 1 task T688 for `src/slic3r/GUI/Widgets/SideButton.hpp`.
- The header is the declaration boundary for the skinned side button: it exposes layout/orientation state, palette mutation entry points, and the custom event surface that the cpp drives.
- Plan: keep the header annotations focused on retained state, event flow, and Unity mapping, then close the task and continue with the next widget file.

- Picked Phase 1 task T689 for `src/slic3r/GUI/Widgets/SideMenuPopup.cpp`.
- The popup is a transient floating shell around a caller-populated stack of SideButton children; it measures max child width, reflows every button to that width, and clamps placement against the current display bounds.
- Unity mapping is a retained popup/container controller with a vertical layout and shared open/close state; the main hazard is the app-global side-menu visibility flag and the focus-relative positioning logic.
- Completed the annotation pass for T689 and recorded evidence in the handoff; next step is commit + task close for T689, then hand off T690.

- Picked Phase 1 task T690 for `src/slic3r/GUI/Widgets/SideMenuPopup.hpp`.
- The header already carries the needed annotation block: transient popup intent, caller-owned button list state, popup/show/dismiss event surface, Unity mapping, and a placement hazard.
- Next step is to close the metadata gap for T690 by recording completion evidence, marking the task done, and committing the atomic task update.

- Picked Phase 1 task T691 for `src/slic3r/GUI/Widgets/SideTools.cpp`.
- This file is a composite sidebar/status module: a custom-painted monitor header strip, a collapsible connection-error drawer, and presenter methods that translate `MachineObject`/`MonitorStatus` into banner, detail, and wifi-signal state.
- I annotated the file with explicit `[INTENT]`, `[STATE]`, `[EVENT]`, `[UNITY]`, and `[PORTING_HAZARD]` notes around the header strip, debounce timer, paint path, composite wrapper, and status translation methods.
- Next step is to record the handoff evidence, close T691, and move to T692 for `src/slic3r/GUI/Widgets/SideTools.hpp`.

- Picked Phase 1 task T692 for `src/slic3r/GUI/Widgets/SideTools.hpp`.
- This header is the declaration boundary for the sidebar status stack: `SideToolsPanel` owns the monitor strip state, interval timer gate, and paint/mouse callbacks, while `SideTools` owns the composite status/error drawer and presenter-facing update surface.
- The annotation pass added class-level `[INTENT]`, `[STATE]`, `[EVENT]`, `[UNITY]`, and `[PORTING_HAZARD:P2]` notes so the later Unity port can split it into a retained status card plus a presenter-driven model.
- Next step is to capture the handoff evidence, commit the atomic header annotation, close T692, and continue with T693.
