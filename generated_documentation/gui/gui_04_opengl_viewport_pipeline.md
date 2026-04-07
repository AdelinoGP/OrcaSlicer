# OpenGL Viewport Pipeline

## Purpose

This document explains the OrcaSlicer viewport/rendering pipeline deeply enough to guide a Unity reimplementation. This is one of the two highest-priority Phase 2 documents because the current viewport is a bespoke rendering runtime rather than a standard desktop widget.

Primary evidence comes from `src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`, `src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3310`, `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`, `src/slic3r/GUI/GLCanvas3D.cpp:L4865-L4905`, `src/slic3r/GUI/GLCanvas3D.cpp:L7208-L7277`, `src/slic3r/GUI/GLCanvas3D.cpp:L7629-L7862`, `src/slic3r/GUI/3DScene.cpp:L949-L1156`, `src/slic3r/GUI/GLModel.hpp:L128-L152`, `src/slic3r/GUI/GLModel.hpp:L190-L233`, `src/slic3r/GUI/GLModel.cpp:L549-L577`, `src/slic3r/GUI/GLModel.cpp:L791-L862`, `src/slic3r/GUI/GLShader.cpp:L25-L90`, `src/slic3r/GUI/GLShader.cpp:L120-L340`, `src/slic3r/GUI/GLShadersManager.cpp:L22-L114`, `src/slic3r/GUI/GLTexture.cpp:L36-L117`, `src/slic3r/GUI/GLTexture.cpp:L499-L514`, `src/slic3r/GUI/GCodeViewer.hpp:L177-L253`, and `src/slic3r/GUI/GCodeViewer.cpp:L1044-L1229`.

## Render Loop Trace

### Entry And Scheduling

The viewport is event-driven, but not in the usual retained-UI sense. `GLCanvas3D` binds wx size, idle, paint, keyboard, mouse, focus, and gesture events into one canvas controller (`src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`). Idle work also refreshes toolbars, notifications, and async texture-compression progress, then requests more idle frames when needed (`src/slic3r/GUI/GLCanvas3D.cpp:L3264-L3310`).

Paint behavior is unusual: the first paint renders immediately, later paints often just mark the canvas dirty (`src/slic3r/GUI/GLCanvas3D.cpp:L4865-L4872`). There is also a direct `render()` path on resize/show to avoid latency on some platforms (`src/slic3r/GUI/GLCanvas3D.cpp:L7195-L7204`).

### Core Render Function

`GLCanvas3D::render()` performs these steps (`src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`):

1. guard against re-entrant render calls
2. ensure the wx GL canvas is shown/current and OpenGL is initialized
3. lazily initialize canvas-local state
4. update camera viewport and projection
5. start an ImGui frame
6. perform a picking pass if picking is enabled
7. clear color/depth buffers
8. render scene passes according to canvas mode
9. compute/update scene-space mouse position for later dragging
10. render sidebar hints, gizmos, overlays, tooltips, notifications, daily tips, and ImGui
11. swap buffers

## Viewport Scene Composition

### Pass Ordering

The current renderer uses explicit pass order, not a generic scene graph. The order in the main 3D view is approximately:

```text
clear
-> background
-> opaque object volumes
-> SLA slices
-> selection overlays
-> bed
-> plate list
-> transparent object volumes
-> sequential-clearance overlays
-> sidebar hints
-> active gizmo
-> rectangle selection overlay
-> notifications/tooltips/other overlays
-> ImGui
```

Source: `src/slic3r/GUI/GLCanvas3D.cpp:L2063-L2150`.

Preview mode diverges by appending `_render_gcode()` after the model/bed/plate passes (`src/slic3r/GUI/GLCanvas3D.cpp:L2094-L2104`). Assemble mode replaces the bed/plate composition with a plane-oriented assembly view (`src/slic3r/GUI/GLCanvas3D.cpp:L2105-L2119`).

### Scene Elements

The viewport composes several independently significant renderers:

- object volumes via `GLVolumeCollection`
- SLA slice overlays
- selection overlays
- bed and plate list renderers
- current gizmo renderer
- G-code preview renderer
- screen-space overlays and notifications

Object render ordering is not arbitrary. `3DScene.cpp` sorts transparent volumes by transformed Z and biases selected opaque objects first (`src/slic3r/GUI/3DScene.cpp:L949-L983`). Actual per-volume draws then apply culling, blending, clipping uniforms, slope/print-volume data, environment-map inputs, and outline-vs-normal draw behavior (`src/slic3r/GUI/3DScene.cpp:L994-L1156`).

## GL Resource Lifetime And Ownership

### Ownership Layers

| Resource type | Current owner | Lifetime behavior | Unity implication |
| --- | --- | --- | --- |
| GL context / capabilities | `OpenGLManager` | app-level init/shutdown | Unity owns graphics device; preserve only capability decisions |
| Shader programs | `GLShadersManager` + `GLShaderProgram` | compiled/linked at runtime, deleted on shutdown/destruction | Map to shaders/materials or SRP shader variants |
| Mesh buffers | `GLModel::RenderData` | uploaded lazily, CPU arrays may be freed after upload | Map to Mesh assets/runtime meshes |
| Textures | `GLTexture` | worker-assisted compression, main-thread upload, explicit delete/reset | Separate CPU preparation from GPU upload |
| Canvas-scoped renderers | `GLCanvas3D` | viewport-lifetime state | View-specific controller and overlay state |

### GLModel

`GLModel` is the mesh bridge between CPU-side geometry and GPU-side buffers (`src/slic3r/GUI/GLModel.hpp:L128-L152`, `src/slic3r/GUI/GLModel.hpp:L190-L233`). Its reset path deletes VAO/VBO/IBO handles (`src/slic3r/GUI/GLModel.cpp:L549-L577`), and upload creates GPU objects, may downcast index widths, then can clear CPU-side arrays (`src/slic3r/GUI/GLModel.cpp:L791-L862`).

Unity implication: retain the distinction between mesh generation/preparation and GPU-ready runtime meshes, especially for generated preview and gizmo geometry.

### Shader Programs

Shaders are assembled, compiled, linked, and then wrapped by a program abstraction with cached uniforms and attributes (`src/slic3r/GUI/GLShader.cpp:L25-L90`, `src/slic3r/GUI/GLShader.cpp:L120-L340`). `GLShadersManager` selects and warms named programs by GL capability tier (`src/slic3r/GUI/GLShadersManager.cpp:L22-L114`).

Unity implication: build an explicit material/shader catalog for viewport passes. Do not bury shader selection inside view code.

### Textures

`GLTexture` splits background compression from main-thread upload and cleans up the GL texture explicitly on reset/destruction (`src/slic3r/GUI/GLTexture.cpp:L36-L117`, `src/slic3r/GUI/GLTexture.cpp:L499-L514`).

Unity implication: any expensive thumbnail/icon/preview texture preparation should remain off the main thread, but final texture creation must stay synchronized with Unity's render thread constraints.

## User Interaction Model

### Input Surface

The viewport binds mouse, wheel, keyboard, focus, timer, and touch gestures into one controller (`src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`). Hover hit-testing is resolved against volumes, gizmos, and bed/plate surfaces (`src/slic3r/GUI/GLCanvas3D.cpp:L7208-L7277`).

### Interaction State Machine

The main mouse handler is a large interaction state machine handling:

- ImGui capture
- toolbar capture
- gizmo capture
- rectangle selection
- volume selection
- object dragging
- camera orbit/pan
- plate selection
- context menus

Source: `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`.

The camera also uses preference-sensitive semantics such as swapped buttons and touchpad mode (`src/slic3r/GUI/GLCanvas3D.cpp:L4598-L4707`, `src/slic3r/GUI/GLCanvas3D.cpp:L4889-L4905`).

Unity implication: model viewport input as an explicit tool/controller state machine, not scattered event handlers on scene objects.

## Shader And Material Considerations

The current renderer behaves more like a custom material system than like a stock desktop widget. Named shader programs cover background, flat, thumbnail, gouraud/lighted, printbed, instanced, and ImGui-style draws (`src/slic3r/GUI/GLShadersManager.cpp:L22-L114`). Runtime behavior depends heavily on uniform-driven mode switches (`src/slic3r/GUI/3DScene.cpp:L1048-L1123`).

Important concerns for Unity:

1. clipping plane support
2. print-volume visualization
3. slope/diagnostic shading
4. outline and selection highlighting
5. overlay-safe pass ordering
6. environment-map-dependent shading where enabled

## G-Code Visualization Behavior

The G-code preview is partially separate from the normal scene path. `GCodeViewer` owns libvgcode-based state, sliders, sequential markers/windows, shell preview data, legend state, and view-type selection (`src/slic3r/GUI/GCodeViewer.hpp:L177-L253`).

The load path resets previous preview state, prepares input from slicing results, and initializes the libvgcode viewer with current GL capabilities (`src/slic3r/GUI/GCodeViewer.cpp:L1044-L1229`). Rendering then layers shell preview, toolpaths, legend, markers/windows, and sliders (`src/slic3r/GUI/GCodeViewer.cpp:L1595-L1661`, `src/slic3r/GUI/GCodeViewer.cpp:L1694-L1868`, `src/slic3r/GUI/GCodeViewer.cpp:L2305-L2319`).

Unity implication: treat toolpath preview as its own rendering product. It may share the scene camera, but it should not be forced into the same implementation as solid-model rendering.

## Unity Strategy Options

### Option 1: Standard Unity Scene Objects + Overlay UI

Implement bed, model volumes, plates, and gizmos with regular scene objects and use a screen-space overlay for notifications and selection widgets.

Pros:

- easiest to prototype
- aligns with Unity tooling
- clean split between world and overlay UI

Cons:

- pass ordering and clipping behavior become harder to preserve
- draw-call count may rise sharply for volume-heavy scenes
- G-code preview may not scale well

### Option 2: Custom URP/SRP Renderer Feature For Viewport Passes

Implement the viewport as explicit opaque/transparent/gizmo/overlay passes inside a custom render feature.

Pros:

- closest match to the current architecture
- preserves pass ordering, clipping, and highlighting rules
- more room for preview-specific optimizations

Cons:

- highest engineering cost
- requires strong rendering expertise
- shader/material authoring becomes more specialized

### Option 3: Hybrid Scene Renderer + Native/Plugin G-Code Preview Path

Keep model/bed/gizmo rendering in Unity while using a plugin or separate specialized renderer for G-code preview.

Pros:

- allows the normal scene to feel idiomatic in Unity
- isolates the densest toolpath-rendering problem

Cons:

- input synchronization and visual integration are harder
- interop increases maintenance risk

### Option 4: GPU-Instanced / Procedural Preview Path Inside Unity

Use Unity's rendering APIs to build an instanced/procedural toolpath renderer for preview while keeping the rest in normal scene objects.

Pros:

- avoids native plugin dependence
- potentially scales well for large G-code datasets

Cons:

- higher implementation risk than plain MeshRenderers
- debugging and authoring are more complex

## Preferred Strategy

Prefer a hybrid of Option 2 and Option 4:

1. use Unity scene objects and normal meshes for bed, plates, models, and many gizmos
2. use an explicit URP/SRP render feature for pass ordering, clipping, outlines, and overlay-sensitive viewport rendering
3. implement G-code preview as a dedicated procedural/instanced subsystem within Unity rather than forcing it through the same mesh path

This best matches the current code structure, where the viewport already behaves like a custom render pipeline and the G-code preview is architecturally distinct.

## Unresolved Ambiguities

- Source alone does not decide whether libvgcode should be wrapped natively, reimplemented, or replaced in the Unity port.
- Some platform-specific GL behavior is capability-driven and may disappear in Unity, but equivalent performance constraints could still matter.
- The best long-term representation for gizmo rendering depends on whether the team wants editor-style handles or runtime-in-world controls.
