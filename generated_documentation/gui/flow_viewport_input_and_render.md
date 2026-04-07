# Flow: Viewport Input And Render

## Flow Purpose

This flow documents how viewport input, picking, camera updates, and rendering interact in the current implementation.

Primary evidence comes from `src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`, `src/slic3r/GUI/GLCanvas3D.cpp:L3264-L3310`, `src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`, `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`, and `src/slic3r/GUI/GLCanvas3D.cpp:L7208-L7277`.

## Participating Source Files And Anchors

- `src/slic3r/GUI/GLCanvas3D.cpp:L3185-L3224`
- `src/slic3r/GUI/GLCanvas3D.cpp:L3264-L3310`
- `src/slic3r/GUI/GLCanvas3D.cpp:L1968-L2242`
- `src/slic3r/GUI/GLCanvas3D.cpp:L4249-L4777`
- `src/slic3r/GUI/GLCanvas3D.cpp:L4865-L4905`
- `src/slic3r/GUI/GLCanvas3D.cpp:L7208-L7277`

## Numbered Flow

1. wx input events arrive on the GL canvas: mouse, wheel, keyboard, focus, timer, and gestures.
   Marker: UI thread input.
2. The canvas resolves who owns input first: ImGui, toolbars, gizmos, rectangle selection, object drag, or camera.
   Marker: UI thread interaction arbitration.
3. Hover/picking state is refreshed to determine current object, gizmo, bed, or plate target.
   Marker: scene query / picking.
4. The active interaction updates camera state, selection state, drag state, or gizmo state.
   Marker: state mutation.
5. The canvas becomes dirty or directly renders depending on event type and platform behavior.
   Marker: render scheduling.
6. `render()` ensures the GL context is current, updates camera viewport/projection, runs a picking pass, and clears buffers.
   Marker: render thread on UI thread.
7. The scene renders in mode-specific pass order: objects, selection, bed/plates, preview-specific G-code, gizmos, overlays, notifications.
   Marker: explicit render passes.
8. Tooltips, notifications, daily tips, and ImGui overlays render last, then the canvas swaps buffers.
   Marker: overlay composition.

## Sequence Diagram

```text
Input event
  -> interaction arbitration
    -> picking / hover resolution
      -> state update (camera/selection/gizmo)
        -> render scheduling
          -> render()
            -> pass-ordered scene draw
              -> overlays / notifications / swap buffers
```

## Unity Implementation Notes

- Treat input arbitration as a dedicated controller, not as incidental per-object callbacks.
- Keep scene picking and overlay UI as coordinated but separate systems.
- Preserve explicit pass order where it affects selection, clipping, preview, and overlays.
- Expect the viewport to remain a specialized subsystem even inside Unity.
