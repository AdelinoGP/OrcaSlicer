# GUI Architecture Overview

## Purpose

This document maps the OrcaSlicer GUI as it exists in the wxWidgets/OpenGL implementation and frames the major subsystem boundaries a Unity/C# rewrite must preserve.

Primary evidence comes from `src/OrcaSlicer.cpp:L1232-L1394`, `src/slic3r/GUI/GUI_Init.cpp:L28-L66`, `src/slic3r/GUI/GUI_App.hpp:L219-L338`, `src/slic3r/GUI/GUI_App.cpp:L2598-L3126`, `src/slic3r/GUI/MainFrame.hpp:L209-L219`, `src/slic3r/GUI/MainFrame.hpp:L361-L405`, `src/slic3r/GUI/MainFrame.cpp:L1056-L1060`, `src/slic3r/GUI/MainFrame.cpp:L1318-L1365`, `src/slic3r/GUI/Plater.hpp:L291-L325`, `src/slic3r/GUI/Tab.hpp:L132-L138`, and `src/slic3r/GUI/Monitor.hpp:L81-L123`.

## System Boundary And Module Map

OrcaSlicer's GUI layer is not a thin view shell. It owns application startup, top-level navigation, OpenGL viewport composition, preset editing, printer/device operations, webview-backed screens, and multiple asynchronous services. `GUI_App` is the singleton root that keeps these services alive, while `MainFrame` is the main window and tab host.

```text
main()/CLI
  -> GUI_Run
    -> wxEntry / GUI_App::OnInit
      -> MainFrame
        -> Notebook tabs
          -> Home/WebView
          -> Prepare/Preview (Plater)
          -> Device/Monitor
          -> Multi-device
          -> Project
        -> Calibration
        -> Auxiliary
        -> Debug tool

Cross-cutting services owned from app shell:
- OpenGL manager and shader catalog
- background slicing/export workers
- downloader / print host / network agent
- device manager / user manager / task manager
- localization, theme, fonts, settings
```

### Subsystem Map

| Subsystem | Current owner | Responsibility | Unity shape |
| --- | --- | --- | --- |
| Application shell | `GUI_App` | Process lifetime, singleton services, theme/font/i18n, OpenGL bootstrap, background managers | Persistent bootstrap scene + service container |
| Main navigation | `MainFrame` + `Notebook` | Top bar, tabs, menus, cross-screen routing | Root HUD/controller with page router |
| Workspace | `Plater` | Model/project state, sidebar, viewport modes, slicing orchestration | Workspace controller + domain services |
| Viewport | `GLCanvas3D`, `GLModel`, `GLShadersManager`, `GCodeViewer` | 3D rendering, picking, gizmos, overlays, preview toolpaths | Scene renderer + custom render pipeline pieces |
| Preset editing | `Tab` and related pages | Print/filament/printer settings UI and dirty-state propagation | Data-bound inspector screens |
| Device operations | `Monitor`, `DeviceManager`, printer webviews | Printer status, storage, firmware, HMS, remote actions | Device dashboard + async service layer |
| Web-backed panels | `ProjectPanel`, `WebViewPanel`, `PrinterWebView` | Embedded browser UX and JS/native bridge | WebView wrapper or native replacement |
| Jobs/background work | `BackgroundSlicingProcess`, `Worker`, `BoostThreadWorker`, `PlaterWorker` | Slicing, uploads, async jobs, UI marshaling | Task-based async runtime + explicit dispatcher |

## Startup Path And Lifetime Overview

The GUI starts only after the CLI path decides there are no command-line-only actions to run. `CLI::run()` branches into `GUI_Run()` for desktop mode, which constructs `GUI_App`, passes init parameters, and enters `wxEntry()` so wxWidgets can call `GUI_App::OnInit()` (`src/OrcaSlicer.cpp:L1232-L1394`, `src/slic3r/GUI/GUI_Init.cpp:L28-L66`).

`GUI_App::on_init_inner()` then performs logging, font/image initialization, theme setup, TLS setup, and finally creates and shows `MainFrame` (`src/slic3r/GUI/GUI_App.cpp:L2701-L3126`). `GUI_App` also owns long-lived managers such as `OpenGLManager`, `RemovableDriveManager`, `ImGuiWrapper`, `PrintHostJobQueue`, `Downloader`, `DeviceManager`, `UserManager`, and `NetworkAgent` (`src/slic3r/GUI/GUI_App.hpp:L277-L330`).

`MainFrame` constructs the notebook-driven shell and creates the major pages, with `Plater` serving both Prepare and Preview modes while Device, Multi-device, Project, and Calibration live as sibling panels (`src/slic3r/GUI/MainFrame.cpp:L1056-L1060`, `src/slic3r/GUI/MainFrame.cpp:L1318-L1365`).

## Major GUI Subsystems And Responsibilities

### Application Shell

`GUI_App` is the de facto composition root. It centralizes app lifecycle flags, palette/font state, locale state, OpenGL lifecycle, background managers, and global service pointers (`src/slic3r/GUI/GUI_App.hpp:L229-L330`).

Unity implication: this should become a much thinner bootstrapper. Keep service lifetime here, but move screen state and business workflows out of a global app singleton.

### Main Window And Navigation

`MainFrame` defines the top-level tab enumeration and owns the major screen instances: home, prepare, preview, monitor, multi-device, project, calibration, auxiliary, and debug-tool surfaces, plus printer webviews, settings dialogs, and the print queue dialog (`src/slic3r/GUI/MainFrame.hpp:L209-L219`, `src/slic3r/GUI/MainFrame.hpp:L361-L405`).

Unity implication: use an explicit router with stable page IDs instead of notebook indices and dynamic tab insertion.

### Workspace And Project Editing

`Plater` is the core editor panel. It owns the model, active print objects, viewport, project loading/saving, G-code loading, plate thumbnails, update triggers, and the worker surface for arrangement/slicing-related jobs (`src/slic3r/GUI/Plater.hpp:L291-L410`).

Unity implication: split `Plater` into:

- workspace scene/controller
- project/model domain service
- slicing orchestration service
- preview/render controller

### Preset Editing And Parameter UX

`Tab` is the preset editor shell for print, filament, printer, and SLA variants, with multiple pages and local dirty/update guards (`src/slic3r/GUI/Tab.hpp:L132-L138`, `src/slic3r/GUI/Tab.hpp:L478-L727`).

Unity implication: use data-bound settings presenters with a shared config model, not one giant wx page tree with manual enable/hide logic.

### Device Monitoring And Printer Flows

`Monitor` and related DeviceCore/printer subsystems expose printer status, storage, firmware, and HMS workflows from the main GUI (`src/slic3r/GUI/Monitor.hpp:L81-L123`, `src/slic3r/GUI/Monitor.cpp:L204-L225`).

Unity implication: move printer/network protocols below the UI boundary and make the monitor screen a view over a device session model.

### Embedded Web Experiences

The GUI includes browser-hosted surfaces such as the home panel, project panel, and printer webview. These are not passive embeds; they exchange commands and state with native code (`src/slic3r/GUI/Project.hpp:L63-L90`, `src/slic3r/GUI/Project.cpp:L55-L71`, `src/slic3r/GUI/Project.cpp:L240-L306`).

Unity implication: decide early whether each flow stays browser-backed or is rewritten natively, because this changes auth, asset, and event architecture.

### Rendering And Interaction Pipeline

`GLCanvas3D` is a custom rendering runtime with its own pass ordering, picking, overlays, gizmos, tooltips, and G-code preview integration (`src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`). This is a first-class subsystem, not a simple control.

Unity implication: the viewport port is a rendering-architecture project, not a widget rewrite.

## Cross-Cutting Concerns

### Undo/Redo And Dirty State

The workspace and preset editors both track dirty state and save prompts. `Plater` exposes project dirtiness and save/reset behavior (`src/slic3r/GUI/Plater.hpp:L306-L315`), while `Tab` keeps dirty/non-system/update counters and compatibility flows (`src/slic3r/GUI/Tab.hpp:L255-L315`).

Unity recommendation: unify these behind one document/preset change-tracking service so screen controllers do not each invent their own dirty-state rules.

### Internationalization

`GUI_App` owns locale selection and language references (`src/slic3r/GUI/GUI_App.hpp:L270-L276`).

Unity recommendation: use a single localization service with string tables and make view models provide translation keys instead of assembled UI text where possible.

### Theming And Fonts

Theme colors and fonts are cached in `GUI_App` and updated globally (`src/slic3r/GUI/GUI_App.hpp:L247-L265`).

Unity recommendation: express theme through ScriptableObject design tokens or a centralized style system, not ad hoc widget palette caches.

### Settings And Persistence

App-wide settings, presets, project metadata, and printer/device state cross the application shell, plater, and tab subsystems. The current design mixes persistent state with live UI ownership.

Unity recommendation: split persistent settings, session state, and domain state into distinct stores.

### Background Work

Slicing, uploads, network checks, downloads, and other async flows are woven through the GUI shell and workspace (`src/slic3r/GUI/GUI_App.hpp:L283-L330`, `src/slic3r/GUI/BackgroundSlicingProcess.hpp:L175-L327`).

Unity recommendation: standardize on cancellable tasks with explicit main-thread dispatch instead of wx idle/paint wakeups.

## Unity Migration Summary By Subsystem

| Subsystem | Keep conceptually | Replace directly | Notes |
| --- | --- | --- | --- |
| App shell | Service lifetime, startup ordering | `wxApp` singleton/global access | Thin bootstrap, not global God object |
| Main window | Page routing and global actions | `Notebook`, wx menubar/titlebar coupling | Prefer explicit route/state model |
| Plater | Workspace orchestration | monolithic panel/PIMPL | Split into scene, workflow, and data services |
| Preset tabs | config editing flows | wx tree/page mechanics | Data-bound forms and reusable setting widgets |
| Viewport | pass ordering, picking, gizmos | raw OpenGL/wx canvas/ImGui plumbing | Highest technical-risk subsystem |
| Device monitor | dashboard semantics | UI-bound protocol code | Move protocol/network code below UI |
| Web panels | flow semantics where needed | wxWebView-specific bridge | Decide browser-vs-native per flow |
| Background jobs | async workflows and cancellation | wx event-loop marshaling | Standard async dispatcher/service layer |

## Recommended Port Order

1. Establish Unity app shell, routing, settings, localization, and shared service boundaries.
2. Port stateful but less rendering-heavy screens: home shell, project shell, monitor shell, calibration shell.
3. Port preset-editing flows and shared setting widgets.
4. Build the async runtime for slicing, uploads, downloads, and printer refresh.
5. Port the viewport and gizmo stack with the final rendering architecture.
6. Rebuild high-risk web/native bridge flows that remain after screen shells exist.

This order reduces the chance that the viewport port has to solve service lifetime, persistence, and async architecture at the same time.

## Unresolved Ambiguities

- The exact long-term scope of browser-backed flows versus native Unity screens is not determined from source alone.
- Some business logic still lives behind singleton access patterns (`wxGetApp()`) and will need a design decision on whether to centralize or redistribute it.
- The current codebase mixes editor-mode and viewer-mode concerns in the app shell; a Unity rewrite may choose to split products or scenes earlier.
