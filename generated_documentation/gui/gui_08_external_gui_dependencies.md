# External GUI Dependencies

## Purpose

This document catalogs the external dependencies that materially affect the GUI port and recommends whether Unity should keep, adapt, or replace them.

Primary evidence comes from `src/slic3r/GUI/GUI_App.hpp:L22-L28`, `src/slic3r/GUI/GUI_App.hpp:L277-L330`, `src/slic3r/GUI/MainFrame.hpp:L86-L109`, `src/slic3r/GUI/Project.hpp:L9-L27`, `src/slic3r/GUI/PrinterWebView.hpp:L14-L21`, `src/slic3r/GUI/Widgets/WebView.hpp:L4-L24`, `src/slic3r/GUI/BonjourDialog.cpp:L8-L16`, `src/slic3r/GUI/BonjourDialog.cpp:L128-L144`, `src/slic3r/GUI/Project.cpp:L240-L246`, `src/slic3r/GUI/Printer/PrinterFileSystem.cpp:L26-L27`, `src/slic3r/GUI/GCodeViewer.hpp:L44-L57`, `src/slic3r/GUI/GCodeViewer.cpp:L1044-L1098`, and `src/slic3r/GUI/GLShadersManager.cpp:L22-L114`.

## Dependency Inventory

| Dependency | How GUI uses it | Unity replacement path | Recommendation | Notes |
| --- | --- | --- | --- | --- |
| wxWidgets | windowing, controls, events, timers, dialogs, app shell | Unity UI Toolkit/uGUI + platform window integration | Replace | Core GUI framework dependency |
| wxWebView | embedded browser surfaces for home/project/printer/update flows | Unity WebView plugin or native screen rewrite | Adapt or replace per flow | Browser-backed features need product decision |
| OpenGL | viewport rendering, shaders, textures, picking, overlays | Unity rendering stack (URP/SRP/custom shaders) | Replace | Preserve rendering intent, not API |
| ImGui | in-viewport debug/tool overlays | Unity IMGUI/editor tools or custom debug HUD | Replace | Mostly tooling/debug concern |
| Boost threads / async support | worker threads, network sync, job queues | C# `Task`, jobs, channels, dispatcher | Replace | Porting seam for async runtime |
| nlohmann/json | browser/device message parsing | `System.Text.Json` or similar | Replace | Use typed DTOs |
| libvgcode | G-code preview rendering/data path | native plugin, wrapper, or reimplementation | Adapt or replace | High-value decision point |
| printer/device/network stack | device monitor, uploads, storage, HMS, auth flows | service layer over retained native/managed protocols | Adapt | Depends on what backend stays native |
| Bonjour / mDNS stack | discovery callbacks in dialogs and device flows | platform networking/discovery service | Adapt or replace | UI should not own callback lifetime |

## Dependency Details

### wxWidgets

wx is the current GUI substrate: app lifecycle, windows, notebook/pages, timers, dialogs, and most event routing (`src/slic3r/GUI/GUI_App.hpp:L22-L28`, `src/slic3r/GUI/MainFrame.hpp:L86-L109`).

Recommendation: replace entirely. Do not build a Unity port that tries to emulate wx idioms directly.

### wxWebView

Browser surfaces are used for multiple important workflows (`src/slic3r/GUI/Project.hpp:L9-L27`, `src/slic3r/GUI/PrinterWebView.hpp:L14-L21`, `src/slic3r/GUI/Widgets/WebView.hpp:L4-L24`).

Recommendation: evaluate screen-by-screen.

- Keep/adapt where existing web content is strategically important.
- Replace where the browser is just compensating for missing native UI.

### OpenGL / Shader Stack

The GUI owns a named shader catalog and custom rendering path (`src/slic3r/GUI/GLShadersManager.cpp:L22-L114`).

Recommendation: replace API-level implementation, preserve render contracts and pass structure.

### Async / Threading Dependencies

App-level threads and worker frameworks are embedded in GUI ownership (`src/slic3r/GUI/GUI_App.hpp:L283-L330`, `src/slic3r/GUI/BonjourDialog.cpp:L128-L144`).

Recommendation: replace with a unified managed async runtime. Keep native threads only where a retained C++ backend truly requires them.

### JSON / Browser Command Payloads

JSON appears in browser and device-facing message paths (`src/slic3r/GUI/Project.cpp:L240-L246`, `src/slic3r/GUI/Printer/PrinterFileSystem.cpp:L26-L27`).

Recommendation: replace with typed request/response contracts in C# and validate all browser-originated messages.

### libvgcode

`GCodeViewer` depends on libvgcode for preview data/rendering (`src/slic3r/GUI/GCodeViewer.hpp:L44-L57`, `src/slic3r/GUI/GCodeViewer.cpp:L1044-L1098`).

Recommendation: this is a strategic dependency decision, not an implementation detail. Resolve early whether to:

1. wrap it natively
2. port core concepts
3. replace it with a Unity-native preview path

## Keep / Adapt / Replace Summary

| Category | Decision |
| --- | --- |
| Desktop UI framework | Replace |
| Rendering API | Replace |
| Async runtime | Replace |
| Browser-backed flows | Adapt or replace per flow |
| Device/network backend | Adapt behind services |
| G-code preview engine | Early architecture decision required |

## Licensing And Integration Concerns Visible From Source Context

- wxWebView behavior is platform-dependent, which implies integration variance even when the API is nominally the same.
- Native preview/device components likely increase packaging and platform QA cost if retained.
- Browser-backed flows introduce an additional trust boundary and require review of command surfaces.

The source visible in this pass is not sufficient for a full license audit, so legal/package review remains a follow-up task.

## Unresolved Ambiguities

- The source shows dependency usage but not the full product intent for each browser-backed screen.
- Some device/network dependencies may already be shared outside the GUI layer, which would change whether Unity should wrap or replace them.
