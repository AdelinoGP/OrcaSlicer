#ifndef slic3r_GLGizmoAssembly_hpp_
#define slic3r_GLGizmoAssembly_hpp_

#include "GLGizmoMeasure.hpp"

namespace Slic3r { namespace GUI {
// [INTENT] Provide a focused assembly editing gizmo layered on GLGizmoMeasure so slicer operators can tweak assembly-specific transforms
// while the rest of the canvas still renders the main viewport. [UNITY] Map this to a UI Toolkit VisualElement overlay (panel + dropdown)
// backed by a MonoBehaviour that mirrors the AssemblyMode state and issues commands through UnityEvents.
class GLGizmoAssembly : public GLGizmoMeasure
{
public:
    // [STATE] Retain the canvas reference plus icon/sprite identifiers so this gizmo can keep its toolbar glyph and mode selection in sync
    // with the GL canvas lifecycle.
    GLGizmoAssembly(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    /// <summary>
    /// Apply rotation on select plane
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    // bool on_mouse(const wxMouseEvent &mouse_event) override;
    // void data_changed(bool is_serializing) override;
    // bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down) override;

    // [EVENT] Signal that this gizmo needs the generic enter/leave snapshot flow to capture pre- and post- assembly edits for undo/redo.
    bool wants_enter_leave_snapshots() const override { return true; }
    // [STATE] Provide the localized status hints when entering/leaving the assembly workflow for the hover infobars.
    std::string get_gizmo_entering_text() const override { return _u8L("Entering Assembly gizmo"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving Assembly gizmo"); }

protected:
    // [THREAD] Called when this gizmo is attached to the GL canvas; runs on the main/UI thread while the GL context is active.
    // [OPENGL] Reserve any additional state (textures, uniform tweaks) needed for assembly-specific indicators before rendering begins.
    bool on_init() override;
    // [STATE] Return the label used on the toolbar or inspector to keep the assembly mode recognizable when activated.
    std::string on_get_name() const override;
    // [STATE] Gate the gizmo activation based on current selection/assembly availability so the combo only appears when meaningful.
    bool on_is_activable() const override;
    // void on_render() override;
    // void on_set_state() override;
    //  [OPENGL] Compose the input dialog inside the GL overlay (buttons, fields, warnings) during the gizmo render pass; runs while the GL
    //  context is bound.
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

    // [PORTING_HAZARD:P2] Warns the user via immediate-mode GL text when the assembly still targets the same model; Unity must translate
    // this to a UI Toolkit tooltip or notification panel on the UI thread.
    // [UNITY] Translate this to a hint label or tooltip managed by the UI Toolkit overlay so it can be toggled from the assembly panel.
    void render_input_window_warning(bool same_model_object) override;
    // [STATE] Renders the dropdown for switching AssemblyMode enums; called during the input window render pass to keep selected mode
    // cached for subsequent mouse events.
    // [EVENT] Selecting a mode emits the same state-change event that other gizmos observe, so the Unity port should hook a
    // Dropdown.onValueChanged callback.
    // [UNITY] Mirror this combo with a UI Toolkit ListView or ComboBox that updates the Gizmo controller and raises UnityEvents.
    bool render_assembly_mode_combo(double label_width, float item_width);

    // [STATE] Updates the active AssemblyMode; this touches cached transformation state and should trigger the gizmo re-render and tool
    // workflow to flush new parameters.
    // [PORTING_HAZARD:P3] Relies on GLGizmoMeasure's immediate-mode rendering and direct AssemblyMode enum; Unity will need to mirror
    // the mode enum in a ScriptableObject and dispatch events through its JobSystem or async UI controller.
    // [UNITY] Implement the switch as a MonoBehaviour call that toggles a ScriptableObject-backed mode registry shared with the canvas UI.
    void switch_to_mode(AssemblyMode new_mode);
};

}} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoAssembly_hpp_
