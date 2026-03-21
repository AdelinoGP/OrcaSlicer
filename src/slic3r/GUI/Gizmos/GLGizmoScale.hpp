#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"
// BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"

#include "libslic3r/BoundingBox.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Exposes axis and uniform scale handles for the currently selected object, tracks drag deltas, and writes transformed state back
// to the canvas; [UNITY] this should be a ScaleHandleController MonoBehaviour that stocks MeshCollider handles, GraphicRaycaster hits, and
// a shared transform update loop. [PORTING_HAZARD:P3] relies on GizmoObjectManipulation being injected externally, so Unity must provision
// a similar coordination service before enabling the handles.
class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    // [STATE] Snapshots recorded at drag start—scale vector, camera-aligned pivots, ctrl modifier, and plane data—so axis/uniform deltas
    // remain deterministic across the drag. Unity should mimic this by caching Vector3 previews before scaling animations.
    struct StartingData
    {
        Vec3d         scale;
        Vec3d         drag_position;
        Vec3d         constraint_position;
        Vec3d         center{Vec3d::Zero()}; // sphere bounding box center
        Vec3d         instance_center{Vec3d::Zero()};
        Vec3d         plane_center; // keep the relative center position for scale in the bottom plane
        Vec3d         plane_nromal; // keep the bottom plane
        BoundingBoxf3 box;
        Vec3d         pivots[6]; // Vec3d constraint_position{Vec3d::Zero()};
        Vec3d         local_pivots[6];
        bool          ctrl_down;

        StartingData() : scale(Vec3d::Ones()), drag_position(Vec3d::Zero()), ctrl_down(false)
        {
            for (int i = 0; i < 5; ++i) {
                pivots[i] = Vec3d::Zero();
            }
        }
    };

    // [STATE] Bounding box cache for the selection; drives handle positioning and uniform scale limits before each render.
    mutable BoundingBoxf3 m_bounding_box;
    // [STATE] Transform describing the grabber mesh placement; kept in sync with the selection so Unity would keep dedicated child
    // Transforms properly aligned.
    Geometry::Transformation m_grabbers_tran; // m_grabbers_transform
    // [STATE] Current selection center and instanced center that anchor scale pivots; Unity equivalents would be cached Transform.position values.
    Vec3d m_center{Vec3d::Zero()};
    Vec3d m_instance_center{Vec3d::Zero()};
    // [STATE] Last known scale applied via this gizmo; used by other UI panels for display and persisted between drags.
    Vec3d m_scale;
    // [STATE] Offset from the bounding box center to support non-uniform handles; Mirror this in Unity via local delta offsets per handle.
    Vec3d m_offset;
    // [STATE] Step size for snapping; Unity must surface the same quantized increments in its NumericField components to avoid drift.
    double m_snap_step;
    // [STATE] Active drag snapshot referenced by axis/uniform routines.
    StartingData m_starting;

    struct GrabberConnection
    {
        GLModel                               model;
        std::pair<unsigned int, unsigned int> grabber_indices;
        Vec3d                                 old_v1{Vec3d::Zero()};
        Vec3d                                 old_v2{Vec3d::Zero()};
    };
    // [STATE] Seven handle connections, one per axis pair plus a uniform handles; Unity should materialize the same count via child
    // GameObjects + MeshCollider references.
    std::array<GrabberConnection, 7> m_grabber_connections;

    // BBS: add size adjust related
    // [STATE] External manipulation service that owns the actual object scale state; porting hazard if Unity cannot reproduce its lifetime
    // before this gizmo activates.
    GizmoObjectManipulation* m_object_manipulation;

public:
    // [EVENT] Instantiated by the canvas with an optional GizmoObjectManipulation bridge so drag callbacks can trigger the shared object
    // state update; Unity should create this controller whenever a selection lands under the camera. BBS: add obj manipulation logic
    // GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);

    double get_snap_step(double step) const { return m_snap_step; }
    void   set_snap_step(double step) { m_snap_step = step; }

    // [STATE] Returns the cached scale vector so UI panels and Unity inspectors can read the last applied transformation.
    const Vec3d& get_scale();
    // [STATE] Synchronizes both the cached scale and the drag snapshot so subsequent drags start from this value.
    void set_scale(const Vec3d& scale)
    {
        m_starting.scale = scale;
        m_scale          = scale;
    }

    // [STATE] Exposes the handle offset from the selection center for the floating input UI.
    const Vec3d& get_offset() const { return m_offset; }

    std::string get_tooltip() const override;

    /// <summary>
    /// Postpone to Grabber for scale
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    // [EVENT] Routes wxWidgets mouse signals to the grabber so handle picking can activate the proper axis; Unity would call the same logic
    // from a GraphicRaycaster/PointerEvent bridge.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    // [THREAD] Fired on the GL/UI thread when serialized or programmatic data changes, forcing the cached bounds to refresh.
    void data_changed(bool is_serializing) override;
    // [STATE] Enables the axis-specific mode (false) or uniform lock (true); Unity should expose this as a toggle in the inspector.
    void enable_ununiversal_scale(bool enable);

protected:
    // [STATE] Allocates GLModel handles, initializes grabber transforms, and caches textures before the first render.
    virtual bool on_init() override;
    // [INTENT] Supplies a readable name for panel listings and Unity GameObject labels.
    virtual std::string on_get_name() const override;
    // [STATE] Determines whether the gizmo can engage based on the selection context.
    virtual bool on_is_activable() const override;
    // [EVENT] Resets drag snapshots and updates handle colors when entering the active state.
    virtual void on_set_state() override;
    // [EVENT] Captures `StartingData` when the user begins dragging (main thread only).
    virtual void on_start_dragging() override;
    // [EVENT] Clears cached drag, network, and render state when the drag completes.
    virtual void on_stop_dragging() override;
    // [OPENGL] Recomputes scale ratios per frame and writes to the selection transform while dragging.
    virtual void on_dragging(const UpdateData& data) override;
    // [OPENGL] Renders grabbers and axis helpers; Unity will replicate this through LineRenderer/MeshRenderer code.
    virtual void on_render() override;
    // [EVENT] Registers wx raycasters for handle picking; Unity equivalent attaches MeshColliders to each handle.
    virtual void on_register_raycasters_for_picking() override;
    // [EVENT] Removes pickers to avoid stale hits when toggling visibility.
    virtual void on_unregister_raycasters_for_picking() override;
    // BBS: GUI refactor: add object manipulation
    // [OPENGL] Draws the floating input window with axis/snap hints; Unity should render a UI Toolkit overlay synchronized with the scene camera.
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    // [OPENGL] Draws the wire between two handles so the user sees axis constraints; Unity should replace this with LineRenderer paths.
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color);

    // [STATE][EVENT] Computes axis-specific scaling deltas using StartingData and the drag event.
    void do_scale_along_axis(Axis axis, const UpdateData& data);
    // [STATE][EVENT] Applies uniform scaling based on the largest axis delta, honoring m_snap_step.
    void do_scale_uniform(const UpdateData& data);

    // [STATE] Helper used by axis/uniform routines to obey snap increments.
    double calc_ratio(const UpdateData& data) const;
    // [STATE] Moves the grabber models to match the bounding box and bottom plane before rendering.
    void update_grabbers_data();
    // [STATE] Recomputes the coordinate system whenever the selection shifts so handles stay aligned.
    void change_cs_by_selection(); // cs mean Coordinate System
private:
    // [STATE] Remembers selection indexes so repeated renders skip heavy recalculations.
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoScale_hpp_
