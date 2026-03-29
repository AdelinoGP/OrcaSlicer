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
