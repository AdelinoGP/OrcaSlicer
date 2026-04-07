# Unity Porting Hazards

## Purpose

This document concentrates the highest-risk migration hazards for a Unity/C# reimplementation of the GUI.

Primary evidence comes from `src/slic3r/GUI/GUI_App.hpp:L219-L330`, `src/slic3r/GUI/MainFrame.cpp:L1417-L1419`, `src/slic3r/GUI/Plater.hpp:L291-L410`, `src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`, `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`, `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L277-L320`, `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L1-L12`, `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L63-L189`, `src/slic3r/GUI/Project.cpp:L240-L306`, `src/slic3r/GUI/PrinterWebView.cpp:L117-L139`, `src/slic3r/GUI/BonjourDialog.cpp:L116-L144`, and `src/slic3r/GUI/Tab.hpp:L255-L315`.

## Critical Blockers

| Hazard | Severity | Why it matters | Evidence |
| --- | --- | --- | --- |
| Custom viewport pipeline, not a stock widget | P1 | Unity must recreate pass ordering, picking, overlays, gizmos, and preview behavior | `src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242` |
| wx idle/paint driven worker completion pump | P1 | Current async correctness depends on wx event-loop behavior | `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L1-L12`, `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L63-L189` |
| Background thread can synchronously require UI work | P1 | Unity async model must support explicit main-thread continuations without deadlock | `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L277-L320` |
| Global singleton app state and `wxGetApp()` reachability | P1 | Hard to test, hard to decompose, easy to recreate as an anti-pattern in Unity | `src/slic3r/GUI/GUI_App.hpp:L219-L330` |

## Hazard Catalog Grouped By Subsystem

### App Shell And Navigation

| Hazard | Severity | Impact | Likely mitigation |
| --- | --- | --- | --- |
| `GUI_App` as service locator + mutable state store | P1 | port may recreate a God object in C# | split bootstrap from state stores/services |
| Tab index coupling and dynamic insertion | P2 | route bugs and brittle shell logic | use route IDs and declarative page config |
| Platform-specific titlebar/window behavior in shell | P3 | desktop parity work on each target platform | isolate native window shell concerns |

Evidence: `src/slic3r/GUI/GUI_App.hpp:L219-L330`, `src/slic3r/GUI/MainFrame.cpp:L1417-L1419`.

### Workspace And Viewport

| Hazard | Severity | Impact | Likely mitigation |
| --- | --- | --- | --- |
| Monolithic `Plater` ownership | P1 | difficult decomposition and testability | split workspace services and controllers |
| Explicit pass ordering and custom picking | P1 | visual/interaction regressions if treated as plain scene objects | design viewport architecture first |
| Large viewport interaction state machine | P1 | subtle input regressions in selection/gizmo/camera behavior | implement explicit input tool state machine |

Evidence: `src/slic3r/GUI/Plater.hpp:L291-L410`, `src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`, `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`.

### Async / Threading

| Hazard | Severity | Impact | Likely mitigation |
| --- | --- | --- | --- |
| worker completion depends on wx idle/paint wakeups | P1 | race conditions and stuck jobs if ported naively | central dispatcher flushed each frame |
| blocking UI-task bridge from background thread | P1 | deadlock or frozen UI risk | explicit awaited main-thread continuations |
| mixed user-cancel and internal-cancel semantics | P2 | incorrect notifications and state leaks | dedicated workflow state machine |

Evidence: `src/slic3r/GUI/Jobs/PlaterWorker.hpp:L1-L12`, `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L277-L320`.

### Presets And Settings

| Hazard | Severity | Impact | Likely mitigation |
| --- | --- | --- | --- |
| `Tab` mixes preset state, dirty logic, and view lifecycle | P2 | hard-to-reason save/discard behavior | edit-session model separate from UI |
| recursive update guard patterns | P2 | state-update loops and hidden coupling | transactional state updates / reducers |

Evidence: `src/slic3r/GUI/Tab.hpp:L255-L315`.

### Web And Device Bridges

| Hazard | Severity | Impact | Likely mitigation |
| --- | --- | --- | --- |
| browser script-message bridge directly triggers native actions | P1 | security and maintenance risk | typed validated command bridge |
| printer webview patches browser `fetch` for auth | P1 | brittle and platform-specific behavior | move auth/session injection to service layer |
| async discovery callbacks can outlive dialogs | P2 | use-after-free/lifetime bugs | scoped subscriptions and disposable lifetimes |

Evidence: `src/slic3r/GUI/Project.cpp:L240-L306`, `src/slic3r/GUI/PrinterWebView.cpp:L117-L139`, `src/slic3r/GUI/BonjourDialog.cpp:L116-L144`.

## Dependencies Between Hazards

1. The viewport architecture decision affects input, overlays, G-code preview, and notification placement.
2. The async-runtime redesign affects slicing, uploads, device refresh, and web/native bridge callbacks.
3. The app-shell decomposition affects every screen because global singleton access is pervasive.
4. The state-model redesign affects presets, workspace dirty-state, and shell actions.

## Recommended Order For Burning Down Risk

1. Define bootstrap/service architecture to replace `wxGetApp()` reachability.
2. Define async dispatch/cancellation model to replace wx idle/paint completion assumptions.
3. Choose the viewport/rendering strategy before implementing core workspace UX.
4. Split workspace and preset state models away from view classes.
5. Decide browser-backed versus native replacements for project/printer/home flows.
6. Port lower-risk screens only after those architectural seams are stable.

## Residual Risk Notes

- Even after architecture choices are made, the viewport and G-code preview will remain the highest technical-risk implementation area.
- Device/web flows are less GPU-intensive but carry protocol/auth/lifetime hazards that can still block release quality.

## Unresolved Ambiguities

- The source makes the hazards visible, but not all intended long-term product decisions; some risk burn-down depends on whether the Unity rewrite preserves desktop-specific features or simplifies them.
