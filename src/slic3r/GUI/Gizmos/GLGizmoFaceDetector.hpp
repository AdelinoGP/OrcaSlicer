#ifndef slic3r_GLGizmoFaceDetector_hpp_
#define slic3r_GLGizmoFaceDetector_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"

namespace Slic3r {

namespace GUI {

// [INTENT] Face detector overlays the selection so downstream gizmos can operate on a single exterior face.
// [UNITY] Map this to a MonoBehaviour driven by MeshFilter + MeshCollider data, using the Graphics API for overlays.
// [PORTING_HAZARD:P2] Depends on GLModel adjacency metadata that wx/OpenGL precomputes, so Unity must regenerate face/edge connectivity itself.
class GLGizmoFaceDetector : public GLGizmoBase
{
public:
    GLGizmoFaceDetector(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
        : GLGizmoBase(parent, icon_filename, sprite_id)
    {}

protected:
    // [EVENT] Called every frame via GLGizmoBase when the canvas redraws and the gizmo is active.
    // [OPENGL] Highlights the detected face with GLModel draw calls, so it must play nicely with other GL state.
    // [UNITY] Replace with Graphics.DrawMesh/CommandBuffer from a MonoBehaviour that visualizes the selected face.
    void on_render() override;
    void on_render_for_picking() override {}
    // [EVENT] Builds the small input window for this gizmo; the base class invokes it when focus drifts in.
    // [UNITY] Recreate as a UI Toolkit VisualElement tied to the viewport camera so pointer events can adjust sampling.
    void on_render_input_window(float x, float y, float bottom_limit) override;
    // [STATE] Exposes the gizmo name so toolbars and Unity button bindings can toggle the same behavior.
    std::string on_get_name() const override;
    // [EVENT] Syncs internal caches when the gizmo becomes active or inactive; called by GLGizmoBase after toolbar events.
    // [THREAD] Runs on the UI thread, so Unity should marshal the toggle through the main thread dispatcher.
    void on_set_state() override;
    // [STATE] Gates activatability (only when a selection contains faces) so buttons stay in sync with user context.
    bool on_is_activable() const override;
    // [STATE] Declares dependent data (selection, models) so the parent knows when this gizmo can run.
    CommonGizmosDataID on_get_requirements() const override;

private:
    // [STATE] Prepares GLModel resources and registers any needed callbacks before the first render.
    bool on_init() override;
    // [EVENT] Triggered whenever the selection changes and the gizmo is running, scans vertices to pick the exterior face.
    // [THREAD] Executes on the UI thread, so Unity must mirror that safety when reading mesh data (e.g., safe `MeshFilter` access in
    // `Update` or batched via `UnityMainThreadDispatcher`).
    void perform_recognition(const Selection& selection);
    // [INTENT] Updates the GL overlay to highlight the face that was found and caches that choice.
    // [UNITY] In Unity, swap the highlighted `MeshRenderer` material or draw a gizmo outline around the computed face.
    void display_exterior_face();

    // [STATE] GPU-side mesh cache used when sampling normals/edges for face picking.
    // [PORTING_HAZARD:P3] Unity needs to re-sample from MeshFilter data because GLModel is tied to wxWidgets' context.
    GUI::GLModel model;
    // [STATE] Controls how densely the detection scans the surface; tuning trades off latency and accuracy.
    // [PORTING_HAZARD:P3] Keep Unity's sampling resolution aligned (e.g., compute shader or CPU iteration) to avoid hysteresis.
    double m_sample_interval = {0.5};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoFaceDetector_hpp_
