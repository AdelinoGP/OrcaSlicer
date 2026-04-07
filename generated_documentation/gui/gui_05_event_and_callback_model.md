# Event And Callback Model

## Purpose

This document explains how wxWidgets events, custom events, callbacks, and screen-to-screen event flows currently operate in the GUI, and how those flows should map into Unity.

Primary evidence comes from `src/slic3r/GUI/MainFrame.cpp:L82-L97`, `src/slic3r/GUI/MainFrame.cpp:L1261-L1316`, `src/slic3r/GUI/MainFrame.cpp:L1861-L2107`, `src/slic3r/GUI/Plater.cpp:L182-L221`, `src/slic3r/GUI/Plater.cpp:L4995-L5053`, `src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`, `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`, `src/slic3r/GUI/Tab.cpp:L228-L239`, `src/slic3r/GUI/Tab.cpp:L492-L521`, `src/slic3r/GUI/Tab.cpp:L5760-L6069`, `src/slic3r/GUI/Project.cpp:L55-L71`, `src/slic3r/GUI/Project.cpp:L240-L306`, `src/slic3r/GUI/Monitor.cpp:L131-L151`, `src/slic3r/GUI/BonjourDialog.cpp:L42-L45`, and `src/slic3r/GUI/Printer/PrinterFileSystem.cpp:L47-L58`.

## Event Model Primer For This Codebase

The GUI uses several overlapping dispatch styles:

1. wx native window/control events
2. custom wx events declared at file scope
3. direct callback lambdas bound during construction
4. worker-to-UI posted events
5. browser-to-native script message bridges

This means there is no single event bus. Instead, each subsystem uses the mechanism that was easiest to attach to its owner.

## Important Bind Sites And Handlers

| Area | Key bind or event definition sites | What they control |
| --- | --- | --- |
| Main shell | `src/slic3r/GUI/MainFrame.cpp:L82-L97`, `src/slic3r/GUI/MainFrame.cpp:L1261-L1316` | top-level custom events, tab changes, shell routing |
| Slice/print actions | `src/slic3r/GUI/MainFrame.cpp:L1861-L2107` | user action buttons fan into plater workflows |
| Workspace | `src/slic3r/GUI/Plater.cpp:L182-L221`, `src/slic3r/GUI/Plater.cpp:L4995-L5053` | slicing/export/project/plugin/fullscreen custom events and timers |
| Viewport | `src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`, `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777` | direct input event binding and interaction routing |
| Preset editor | `src/slic3r/GUI/Tab.cpp:L228-L239`, `src/slic3r/GUI/Tab.cpp:L492-L521` | preset selection, page switching, tree routing |
| Project web bridge | `src/slic3r/GUI/Project.cpp:L55-L71`, `src/slic3r/GUI/Project.cpp:L240-L306` | browser events, JS messages, native side effects |
| Monitor | `src/slic3r/GUI/Monitor.cpp:L131-L151` | timer refresh, popup completion, machine-selection events |
| Device discovery/storage | `src/slic3r/GUI/BonjourDialog.cpp:L42-L45`, `src/slic3r/GUI/Printer/PrinterFileSystem.cpp:L47-L58` | async replies and custom filesystem events |

## Custom Events And App-Specific Dispatch Patterns

### MainFrame / Plater Custom Events

The shell and workspace define many custom event types at file scope, which makes event topology hard to discover but also shows how much coordination exists outside normal control callbacks (`src/slic3r/GUI/MainFrame.cpp:L82-L97`, `src/slic3r/GUI/Plater.cpp:L182-L221`).

These custom events cover things like:

- slicing schedule/update/completion
- export started/finished
- project open/save state
- full-screen and plugin-related changes
- shell-level action/status changes

### Worker-To-UI Posting

Background workflows post completion and progress back into the GUI with wx events or callback queues. This is a core pattern for slicing and uploads, not an exception.

### Browser Message Bridges

The project panel and printer-related webviews receive browser events and script messages, then translate them into native actions (`src/slic3r/GUI/Project.cpp:L240-L306`).

This is effectively an event bridge between two runtimes.

## Critical User Flows As Numbered Sequences

### Flow 1: Slice / Print Button To Workspace Action

1. User clicks a shell-level slice or print action in `MainFrame`.
2. `MainFrame` interprets the current print/slice selection mode and routes to the correct workflow (`src/slic3r/GUI/MainFrame.cpp:L1861-L2107`).
3. `Plater` receives the action through its event/callback surface.
4. `Plater` updates local state, schedules or restarts background slicing, and posts downstream status changes (`src/slic3r/GUI/Plater.cpp:L4995-L5053`).
5. Notification/UI state is then updated by later progress/completion events.

### Flow 2: Viewport Input To Scene Mutation

1. wx mouse/keyboard/gesture events arrive on `GLCanvas3D` (`src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`).
2. The canvas resolves capture precedence between ImGui, toolbars, gizmos, selection, and camera (`src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`).
3. Picking/hover state determines the active volume, bed target, or gizmo target.
4. Scene or camera state changes are applied.
5. The canvas marks dirty or renders again, and overlays/tooltips are recomputed.

### Flow 3: Preset Selection / Dirty-State Decision Tree

1. User selects a preset or page in `Tab` (`src/slic3r/GUI/Tab.cpp:L228-L239`, `src/slic3r/GUI/Tab.cpp:L492-L521`).
2. The tab checks current dirty/config state.
3. Save/discard/transfer/compatibility dialogs may interrupt the flow (`src/slic3r/GUI/Tab.cpp:L5760-L6069`).
4. Only after the decision tree resolves does the selected preset/page become active.

### Flow 4: Project Browser Message To Native Action

1. Browser events are bound during project-panel construction (`src/slic3r/GUI/Project.cpp:L55-L71`).
2. Script messages arrive from embedded web content (`src/slic3r/GUI/Project.cpp:L240-L306`).
3. The panel decodes the message and chooses a native action.
4. Native UI state and auxiliary panel visibility may change.
5. Additional browser commands may be issued back to the web surface.

## Unity Equivalents

### Recommended Event Layers

| Current pattern | Unity equivalent |
| --- | --- |
| wx control events | UI Toolkit / uGUI event callbacks |
| custom wx events | typed C# events or domain event classes |
| ad hoc lambdas during construction | presenter/controller subscriptions |
| worker-posted UI events | main-thread dispatcher + async completion events |
| browser script messages | typed bridge interface with validated commands |

### Recommended Architecture

1. Keep view-local UI events local.
2. Use typed domain/application events for cross-screen workflows.
3. Route background completion through one main-thread dispatcher.
4. Give browser bridges their own typed command channel.
5. Avoid a global stringly-typed event bus unless it is strongly constrained.

## Migration Notes

- The current code often relies on the event loop itself to create ordering guarantees. Unity should make those ordering rules explicit in controller logic.
- Several important flows are split across direct callbacks and custom events; a Unity rewrite should collapse these into fewer, more visible event seams.

## Unresolved Ambiguities

- Some event definitions are distributed widely enough that a full event catalog would require a dedicated follow-up pass.
- The right balance between direct controller calls and a shared bus in Unity depends on whether the port prioritizes local simplicity or systemic decoupling.
