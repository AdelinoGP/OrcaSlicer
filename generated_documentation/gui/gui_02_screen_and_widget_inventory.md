# GUI Screen And Widget Inventory

## Purpose

This inventory lists the major GUI screens, panes, and widget clusters that a Unity port must account for, with ownership and migration notes.

Primary evidence comes from `src/slic3r/GUI/MainFrame.hpp:L209-L219`, `src/slic3r/GUI/MainFrame.hpp:L361-L405`, `src/slic3r/GUI/MainFrame.cpp:L1056-L1060`, `src/slic3r/GUI/MainFrame.cpp:L1318-L1365`, `src/slic3r/GUI/Monitor.cpp:L204-L225`, `src/slic3r/GUI/Monitor.hpp:L132-L185`, `src/slic3r/GUI/Plater.hpp:L291-L410`, `src/slic3r/GUI/Tab.hpp:L478-L727`, `src/slic3r/GUI/Project.hpp:L63-L90`, `src/slic3r/GUI/CalibrationPanel.cpp:L521-L530`, and `src/slic3r/GUI/CalibrationWizard.hpp:L53-L123`.

## Top-Level Windows, Tabs, Panes, And Dialogs

### Top-Level Window

The main desktop shell is `MainFrame`, which owns the tab notebook, top bar, parameter/sidebar surfaces, modal dialogs, and the main screen instances (`src/slic3r/GUI/MainFrame.hpp:L361-L405`).

### Main Tabs

The primary tab positions are defined in `MainFrame::TabPosition`:

- Home
- Prepare
- Preview
- Device
- Multi-device
- Project
- Calibration
- Auxiliary
- Debug tool

Source: `src/slic3r/GUI/MainFrame.hpp:L209-L219`.

### Significant Dialog Families

The main window also owns or launches settings, diff/publish, print-host queue, printer dialogs, calibration dialogs, and many workflow-specific popups (`src/slic3r/GUI/MainFrame.hpp:L351-L390`). The GUI contains a large catalog of custom dialogs beyond the shell-level ones, but the port should first preserve workflow categories rather than one-for-one native window types.

## Inventory Table Keyed By Source Anchors

| Screen or widget group | Current owner | Ownership/lifecycle notes | Unity equivalent | Complexity / hotspot | Source anchors |
| --- | --- | --- | --- | --- | --- |
| Main application window | `MainFrame` | Created during `GUI_App::on_init_inner()`, lives for app lifetime | Root scene canvas + router | High: owns navigation and many cross-screen actions | `src/slic3r/GUI/GUI_App.cpp:L3113-L3126`, `src/slic3r/GUI/MainFrame.hpp:L361-L405` |
| Top bar / chrome | `BBLTopbar` inside `MainFrame` | Platform-sensitive titlebar replacement | Custom Unity header bar | Medium: native window affordances differ by platform | `src/slic3r/GUI/MainFrame.hpp:L361-L362` |
| Notebook / tab host | `Notebook` in `MainFrame` | Central page switcher, index-based routing | Tab/page controller | Medium: dynamic tab visibility and index coupling | `src/slic3r/GUI/MainFrame.hpp:L380-L385`, `src/slic3r/GUI/MainFrame.cpp:L1318-L1365` |
| Home panel | `WebViewPanel` | Browser-backed start surface | WebView shell or native dashboard | Medium: depends on embedded web tech | `src/slic3r/GUI/MainFrame.hpp:L374-L381`, `src/slic3r/GUI/MainFrame.cpp:L1318-L1324` |
| Prepare workspace | `Plater` | Shared controller with Preview | Workspace scene + inspector layout | Very high: project/model/slicing/view interactions | `src/slic3r/GUI/Plater.hpp:L291-L410`, `src/slic3r/GUI/MainFrame.cpp:L1056-L1060` |
| Preview workspace | `Plater` + `GLCanvas3D` + `GCodeViewer` | Same owner as Prepare, different canvas mode | Same workspace with mode switch | Very high: render path diverges for G-code preview | `src/slic3r/GUI/GLCanvas3D.cpp:L2080-L2104`, `src/slic3r/GUI/GCodeViewer.hpp:L177-L253` |
| Right-side parameter panel | `ParamsPanel` | Tightly coupled to plater/editor context | Inspector/sidebar panel | High: many control types and config bindings | `src/slic3r/GUI/MainFrame.hpp:L383-L385` |
| Device monitor | `MonitorPanel` / `Monitor` | Long-lived page with timer-driven refresh | Device dashboard | High: live device state and async operations | `src/slic3r/GUI/MainFrame.hpp:L366-L367`, `src/slic3r/GUI/Monitor.hpp:L132-L185` |
| Multi-device page | `MultiMachinePage` | Separate notebook page | Fleet dashboard | Medium/high: printer orchestration and AMS mapping | `src/slic3r/GUI/MainFrame.hpp:L370-L371` |
| Project page | `ProjectPanel` | Browser + native auxiliary bridge | Mixed-content page controller | High: filesystem/web/native bridge | `src/slic3r/GUI/MainFrame.hpp:L370-L373`, `src/slic3r/GUI/Project.hpp:L63-L90` |
| Calibration page | `CalibrationPanel` | Hosts calibration wizards and dialogs | Workflow hub + wizard pages | Medium/high: many specialized flows | `src/slic3r/GUI/MainFrame.hpp:L373-L373`, `src/slic3r/GUI/CalibrationPanel.cpp:L521-L530` |
| Preset tabs | `Tab` subclasses | Created under settings flows, shared preset bundle assumptions | Reusable settings screens | High: dirty-state and compatibility logic | `src/slic3r/GUI/Tab.hpp:L478-L727` |
| Printer webview | `PrinterWebView` | Separate web-based printer surface | WebView or native printer page | High if retained, lower if replaced | `src/slic3r/GUI/MainFrame.hpp:L375-L377` |
| Print-host queue dialog | `PrintHostQueueDialog` | MainFrame-owned modal/service surface | Upload queue overlay | Medium: should become service-driven | `src/slic3r/GUI/MainFrame.hpp:L362-L390` |

## Screen-By-Screen Notes

### Main Shell

The shell is not only navigation. It also stores current slice/print action modes, tab selection behavior, and references to page-level dialogs and buttons (`src/slic3r/GUI/MainFrame.hpp:L392-L409`).

Unity recommendation: make these shell concerns explicit in a `MainShellState` model instead of distributing them across widgets.

### Prepare / Preview Workspace

`Plater` handles model/project IO, dirty state, thumbnails, updates, worker interaction, and multiple view modes (`src/slic3r/GUI/Plater.hpp:L306-L410`). This is the largest single screen family in the GUI.

Migration hotspot: the workspace is currently a fused screen made of viewport, sidebar, object/plate data, notifications, gizmos, and slicing lifecycle.

### Device Monitor

The monitor assembles status, storage, firmware, and HMS tabs in one screen (`src/slic3r/GUI/Monitor.cpp:L204-L225`). It is timer-driven and uses printer-specific popup flows.

Migration hotspot: live printer state currently reaches deeply into the view layer.

### Project Page

The project page owns a browser, an auxiliary panel, root URL/path state, and JS bridge state (`src/slic3r/GUI/Project.hpp:L63-L90`).

Migration hotspot: Unity must decide between preserving the web stack or folding project features into native screens.

### Calibration

Calibration is a family of wizard-driven workflows rather than one simple panel. The dashboard launches multiple wizard types, and the wizard host carries step/state semantics (`src/slic3r/GUI/CalibrationWizard.hpp:L53-L123`).

Migration hotspot: port as an explicit state machine, not as wx scrolled pages and ad hoc dialogs.

## Widget Patterns Worth Preserving As Reusable Unity Components

| Widget pattern | Current examples | Unity replacement |
| --- | --- | --- |
| Routed tab/page host | `Notebook`, `Tab`, monitor tab strip | Shared page router with selected-page state |
| Inspector row / parameter control | `ParamsPanel`, `Tab` pages, custom widgets in `Widgets/` | Reusable property rows and field binders |
| Floating modal / popup | print host queue, calibration dialogs, warnings, selection popups | Overlay stack with typed modal controllers |
| Browser-backed surface | home/project/printer webviews | WebView wrapper or native screen implementations |
| Scene overlay | notifications, gizmos, selection rectangle, slider overlays | Screen-space overlay canvas tied to scene state |

## Ownership And Lifecycle Summary

1. `GUI_App` owns process-wide managers and creates `MainFrame`.
2. `MainFrame` owns persistent top-level pages and shared shell dialogs.
3. `Plater` owns the largest stateful workspace subtree.
4. Preset and calibration flows mix persistent models with transient dialogs and wizard pages.
5. Device and web pages depend on long-lived managers but also keep screen-local timers and transient bridge state.

## Unresolved Ambiguities

- Some smaller popup/dialog inventories are distributed across many files; a later porting sprint may need a dedicated modal catalog.
- The Auxiliary and Debug Tool screens are visible in navigation enums but are less clearly documented as end-user surfaces in the same files.
