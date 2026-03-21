#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"
// BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Provides the move gizmo (3D grab handles) that translates selections and exposes axis-specific hit areas.
// [UNITY] Should map to a MonoBehaviour containing small axis meshes + MeshColliders managed by a RuntimeHandleManager.
// [THREAD] Runs entirely on the GLCanvas3D/UI thread, so Unity translation must keep equivalent updates on the main thread.
// BBS: GUI refactor: add object manipulation
class GizmoObjectManipulation;
class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    // [STATE] Current translation delta previewed while dragging; resets once the move is committed.
    Vec3d m_displacement{Vec3d::Zero()};
    // [STATE] Geometry center used to align the grabbers when the gizmo becomes active.
    Vec3d m_center{Vec3d::Zero()};
    // [STATE] Cached selection bounds used to size handles and compute selection-dependent snapping.
    BoundingBoxf3 m_bounding_box;
    // [STATE] Active snap increment that drives both UI controls and the actual delta applied to the selection.
    double m_snap_step{1.0};
    // [STATE] World-space cursor origin captured when the drag sequence begins.
    Vec3d m_starting_drag_position{Vec3d::Zero()};
    // [STATE] Selection center at the start of drag, used to compute `m_displacement`.
    Vec3d m_starting_box_center{Vec3d::Zero()};
    // [STATE] Auxiliary corner needed when toggling between bottom vs. center drags.
    Vec3d m_starting_box_bottom_center{Vec3d::Zero()};

    struct GrabberConnection
    {
        GLModel model;
        Vec3d   old_center{Vec3d::Zero()};
    };
    std::array<GrabberConnection, 3> m_grabber_connections;
    // [STATE] Axis-aligned grabbers with cached centers.
    // [UNITY] Unity should recreate these as axis GameObjects tied to Transform handles and MeshColliders.

    // BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;
    // [STATE] Optional controller for coordinating manipulations such as size adjustments that also share the move lifecycle.

public:
    // BBS: add obj manipulation logic
    // GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);
    virtual ~GLGizmoMove3D() = default;

    double get_snap_step(double step) const { return m_snap_step; }
    void   set_snap_step(double step) { m_snap_step = step; }

    // [INTENT] Supplies the localized tooltip for the toolbar so Unity can mirror the same label in its UI tree.
    std::string get_tooltip() const override;

    /// <summary>
    /// Postpone to Grabber for move
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    // [EVENT] Consumes wxMouseEvent to decide which grabber owns the drag.
    // [UNITY] Replace this with Input.GetMouseButtonDown + GraphicRaycaster hits inside Unity's input bridge.
    // [PORTING_HAZARD:P2] Mouse coordinates are in wxWidgets GLCanvas space; Unity must transform screen space events with GraphicRaycaster.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    /// <summary>
    /// Detect reduction of move for wipetover on selection change
    /// </summary>
    // [EVENT] Called from GLGizmoBase whenever the selection changes so cached bounds and handles align.
    void data_changed(bool is_serializing) override;

protected:
    // [INTENT] Prepare mesh models and event bindings for the move handles.
    bool on_init() override;
    // [INTENT] Provide the localized gizmo label used by the toolbar button.
    std::string on_get_name() const override;
    // [EVENT] Check whether the current selection can be moved (volumes present, selection type, etc.).
    bool on_is_activable() const override;
    // [STATE] Reset caches when the gizmo's active state changes.
    virtual void on_set_state() override;
    // [EVENT] Capture cursor/projection when dragging starts.
    void on_start_dragging() override;
    // [EVENT] Release drag state to keep selection consistent.
    void on_stop_dragging() override;
    // [EVENT] Apply the ongoing drag to selection transforms per frame.
    void on_dragging(const UpdateData& data) override;
    // [OPENGL] Render the axis models (GLModel) and any helper lines for the gizmo each frame.
    void on_render() override;
    // [EVENT] Register GL raycasters so GLCanvas3D can translate clicks into axis hits.
    // [UNITY] Mirror this by enabling MeshColliders on each axis handle so Unity's raycasters see them.
    void on_register_raycasters_for_picking() override;
    // [EVENT] Unregister those raycasters when the gizmo deactivates.
    // [UNITY] Mirror this by disabling MeshColliders until the gizmo is shown again.
    void on_unregister_raycasters_for_picking() override;
    // BBS: GUI refactor: add object manipulation
    // [OPENGL] Draws the floating input widget.
    // [UNITY] Replace with a world-space Canvas + TextMeshPro panel that follows the selection.
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    // [STATE] Projects cursor deltas onto each axis to resolve the translation value.
    double calc_projection(const UpdateData& data) const;
    // [INTENT] Refreshes coordinate system (CS) alignment when the selection or its origin moves.
    void change_cs_by_selection(); // cs mean Coordinate System
private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
    // [STATE] Track selection indices so the transform resets when users pick a different object or volume.
};

}} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoMove_hpp_
