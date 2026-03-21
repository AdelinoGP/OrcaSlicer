#ifndef slic3r_GLGizmoHollow_hpp_
#define slic3r_GLGizmoHollow_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include <libslic3r/SLA/Hollowing.hpp>
#include <libslic3r/ObjectID.hpp>
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>

namespace Slic3r {

class ConfigOption;
class ConfigOptionDef;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;

// [INTENT] Coordinates SLA hollow editing controls by combining input, selection rectangles, and preview rendering so operators can punch
// drain holes on meshes with immediate visual feedback. [UNITY] Replace this with a MonoBehaviour that drives an Input System/GraphicRaycaster
// bridge, UI Toolkit settings panel, and MeshCollider-based selection logic backed by a ScriptableObject data model.
class GLGizmoHollow : public GLGizmoBase
{
private:
    // [INTENT][THREAD] Project the latest mouse position into the SLA mesh to find a candidate hole center and normal while still on the
    // UI/GL thread.
    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

public:
    // [STATE] Initializes the gear icon metadata so the toolbar knows which sprite and tooltip belong to the hollow tool.
    // [UNITY] This would become a MonoBehaviour bootstrap that references ScriptableObject icons and registers with a UI Toolkit toolbar controller.
    GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoHollow() = default;
    // [EVENT] Refreshes the current ModelObject pointer and hollow selection whenever the support extraction service provides new data.
    void set_sla_support_data(ModelObject* model_object, const Selection& selection);
    // [EVENT] Handles SLAGizmoEventType actions emitted by the canvas (add hole, remove, drag) and respects modifier keys for precision.
    // [UNITY] Mirror these gestures with Input System Drag/Click actions routed through a VisualElement controller that updates the hole state.
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    // [STATE] Deletes every hole flagged by the selection mask and flags the model as dirty.
    void delete_selected_points();
    // [STATE] Let the base know when the marquee-selection rectangle is active so other input handlers can stay disabled.
    bool is_selection_rectangle_dragging() const { return m_selection_rectangle.is_dragging(); }

private:
    // [INTENT][EVENT] Prepare the meshes, selection cache, and config options when the gizmo opens so the UI can start fresh.
    bool on_init() override;
    // [THREAD] Runs every frame on the GL thread to update selection state, highlight inserts, and reschedule hole generation when live
    // values change.
    void on_update(const UpdateData& data) override;
    // [OPENGL] Draws preview cylinders, selection spheres, and the selection rectangle overlay inside the render thread.
    void on_render() override;

    // [OPENGL][EVENT] Emit the visual handles for each drain hole and optionally switch to picking colors so the selection logic can sample them.
    void render_points(const Selection& selection, bool picking = false);
    // [INTENT][THREAD][PORTING_HAZARD:P2] Invoke libslic3r::sla hollowing using the cached hole set; Unity needs an async job that mirrors
    // this logic and resyncs holes when the native slicer updates.
    void hollow_mesh(bool postpone_error_messages = false);
    // [STATE] Reports whether the hole cache differs from the saved mesh so the UI can warn about unsaved edits.
    bool unsaved_changes() const;

    // [STATE] Remember the last ModelObject ID so we can detect swaps and trigger cache reloads when a new object enters focus.
    ObjectID m_old_mo_id = -1;

    // [OPENGL][STATE] Cylinder mesh used to preview hole scale and orientation; recreated when radius/height sliders change.
    GLModel m_cylinder;

    // [STATE] Radius/height defaults for new holes; the UI binds sliders to these values before calling `hollow_mesh`.
    float m_new_hole_radius = 2.f;
    float m_new_hole_height = 6.f;
    // [STATE] Selection mask parallel to the drain hole list so the render and delete helpers know which handles are active.
    mutable std::vector<bool> m_selected;

    // [STATE] Gate exposed to the preference UI so the tool can temporarily turn off hole generation without dismantling selection data.
    bool m_enable_hollowing = true;

    // Stashes to keep data for undo redo. Is taken after the editing
    // is done, the data are updated continuously.
    // [STATE] Snapshot of the UI-controlled sliders so we can roll back or compare before committing to the drainers.
    float m_offset_stash    = 3.0f;
    float m_quality_stash   = 0.5f;
    float m_closing_d_stash = 2.f;
    // [STATE] Anchor of the currently dragged hole so we can reuse it when the user releases the mouse.
    Vec3f m_hole_before_drag = Vec3f::Zero();
    // [STATE] The latest computed drain holes inside the mesh; kept so the renderer can draw them and serialization can persist them.
    sla::DrainHoles m_holes_in_drilled_mesh;

    // [STATE] Backup of the last known `m_holes_in_drilled_mesh` so undo and reload can restore previous layouts.
    sla::DrainHoles m_holes_stash;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // [STATE][UNITY] Localization cache for UI labels; in Unity this should map to a localization table or Smart Localization ScriptableObject.
    std::map<std::string, wxString> m_desc;

    // [EVENT][PORTING_HAZARD:P3] Marquee selection helper that integrates with the camera and mouse state; Unity must replace this with a
    // GraphicRaycaster + PointerEventData drag layer.
    GLSelectionRectangle m_selection_rectangle;

    // [STATE] Guard to prevent reentrancy while waiting for mouse release events from the selection rectangle.
    bool m_wait_for_up_event = false;
    // [STATE] Quick flag indicating no points are selected so delete/dissemination can avoid work.
    bool m_selection_empty = true;
    // [STATE] Track the previous GLGizmo state so we know when the tool closes and can clean up the selection rectangle.
    EState m_old_state = Off;

    // [EVENT] Utility that pulls config options by key for the UI section rendered in `on_render_input_window`.
    std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> get_config_options(const std::vector<std::string>& keys) const;
    // [STATE] Guard to keep generated holes inside clipping bounds before they are added to the selection.
    bool is_mesh_point_clipped(const Vec3d& point) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    // [STATE] Toggle a specific hole index to update the mask used by `render_points` and `delete_selected_points`.
    void select_point(int i);
    // [STATE] Clear the selection flag for a hole when the user clicks empty space or deselects.
    void unselect_point(int i);
    // [STATE] Synchronize the cached hole list with the last ModelObject data after reloading or undo operations.
    void reload_cache();

protected:
    // [EVENT] Reset selection rect and caches when GLGizmoBase notifies the tool about a state transition.
    void on_set_state() override;
    // [EVENT] Update highlight state when the mouse hovers over a different hole id.
    void on_set_hover_id() override;
    // [EVENT] Begin marquee drag; this hooks into GLSelectionRectangle so pointer events are captured for selection draws.
    void on_start_dragging() override;
    // [EVENT] Release marquee drag and apply selection changes.
    void on_stop_dragging() override;
    // [INTENT][UNITY] Render the small input window with radius/height sliders and checkbox toggles; in Unity this becomes a UI Toolkit
    // VisualElement overlay.
    void on_render_input_window(float x, float y, float bottom_limit) override;
    // [STATE] Declare the dependencies (like the GLModel and input feeds) that must be loaded before the gizmo runs.
    virtual CommonGizmosDataID on_get_requirements() const override;

    // [INTENT] Return the human-friendly title that appears in the toolbar/tip.
    std::string on_get_name() const override;
    // [STATE] Determine whether the current object/selection allows activation.
    bool on_is_activable() const override;
    // [STATE] Control whether the tool shows selection outlines when hovered.
    bool on_is_selectable() const override;
    // [PORTING_HAZARD:P3][UNITY] Binary `cereal` archives persist hole positions; Unity will need a JSON/ScriptableObject equivalent to
    // restore these offsets.
    void on_load(cereal::BinaryInputArchive& ar) override;
    // [PORTING_HAZARD:P3] Symmetrically serialize hole caches so the Unity port can mimic the same persistence contract.
    void on_save(cereal::BinaryOutputArchive& ar) const override;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHollow_hpp_
