#ifndef slic3r_GLGizmoMeshBoolean_hpp_
#define slic3r_GLGizmoMeshBoolean_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r { namespace GUI {

// [STATE] Tracks whether the user is currently picking the source or tool volume so the pointer context can lock to one mesh at a time.
enum class MeshBooleanSelectingState {
    Undef,
    SelectSource,
    SelectTool,

};
// [INTENT] Defines which boolean operation pairs the source/tool volumes; Unity should mirror this enum as a serialized
// `MeshBooleanOperation` field exposed to the editor. [PORTING_HAZARD:P2] Different render pipelines may treat subtraction differently
// (winding/order), so preserve the axis of evaluation when porting to C#.
enum class MeshBooleanOperation {
    Undef,
    Union,
    Difference,
    Intersection,
};
// [STATE] Caches the selected ModelVolume, its index, and the transform that keeps the gizmo overlay aligned to the source mesh.
// [UNITY] Mirror as a lightweight struct referencing the Scene's `MeshFilter` + `Transform` pair (or a ScriptableObject reference) so Unity
// knows which GameObjects to toggle.
struct VolumeInfo
{
    ModelVolume* mv{nullptr};
    int          volume_idx{-1};
    Transform3d  trafo;
    void         reset()
    {
        mv         = nullptr;
        volume_idx = -1;
        trafo      = Transform3d::Identity();
    };
    template<class Archive> void serialize(Archive& ar) { ar(volume_idx, trafo); }
};
// [INTENT] Mesh boolean tool that reuses GLGizmoBase to draw overlay handles, respond to selection clicks, and emit boolean operations
// between two volumes. [UNITY] Map to a MonoBehaviour with two `MeshFilter` references, GraphicRaycaster pointer handling, and a
// `MeshBooleanOperation` enum field. [PORTING_HAZARD:P2] wxWidgets/GLGizmoBase lifecycle differs from Unity's component model, so drive
// this behavior through UnityEvents and direct raycasts instead of wx events.
class GLGizmoMeshBoolean : public GLGizmoBase
{
public:
    GLGizmoMeshBoolean(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoMeshBoolean();

    void set_enable(bool enable) { m_enable = enable; }
    bool get_enable() { return m_enable; }
    // [STATE] Gate to turn the boolean gizmo active/inactive without tearing down its cached volumes; Unity can mirror via a `bool enabled`
    // property.
    MeshBooleanSelectingState get_selecting_state() { return m_selecting_state; }
    void                      set_src_volume(ModelVolume* mv)
    {
        m_src.mv = mv;
        if (m_src.mv == m_tool.mv)
            m_tool.reset();
    }
    // [STATE] Ensures source and tool buffers never point to the same ModelVolume and resets the conflicting slot.
    void set_tool_volume(ModelVolume* mv)
    {
        m_tool.mv = mv;
        if (m_tool.mv == m_src.mv)
            m_src.reset();
    }
    // [EVENT] Primary entry point for GLGizmoBase to route mouse/key actions; port to Unity by calling this from the pointer event bridge
    // before consuming the event. [THREAD] Runs on the GL thread that owns the canvas, so avoid hitting multi-threaded data before
    // marshalling back to the main thread. [PORTING_HAZARD:P3] Unity's event order may differ, so replay the `shift/alt/control` flags to
    // keep modifier-dependent logic stable.
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    /// <summary>
    /// Implement when want to process mouse events in gizmo
    /// Click, Right click, move, drag, ...
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information and don't want to
    /// propagate it otherwise False.</returns>
    // [EVENT] Receives wx mouse events after `gizmo_event` lets the base class intercept them; Unity should map this to pointer callbacks
    // on the overlay GameObject.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

protected:
    // [INTENT] GLGizmoBase lifecycle hooks covering initialization, rendering, and serialization for the boolean overlay.
    // [UNITY] These are analogous to Awake/OnEnable/WAN render pipeline methods on a MonoBehaviour.
    virtual bool on_init() override;
    // [STATE] Provides the label used in toolbar menus and config panels so the Unity menu system can reuse the same string.
    virtual std::string on_get_name() const override;
    // [STATE] Guards whether the gizmo should accept input, mirroring the `isActiveAndEnabled` semantics of Unity components.
    virtual bool on_is_activable() const override;
    // [OPENGL] Draws the boolean handles and helper geometry; Unity should drive this from OnRenderObject using `Graphics.DrawMesh` and
    // maintained MeshFilters.
    virtual void on_render() override;
    // [STATE] Synchronizes modifier states and visibility bits that `on_render` consumes to reduce jitter.
    virtual void on_set_state() override;
    // [EVENT] Declares which shared data buffers (selections, transforms) this gizmo reads so the manager can upload them once per frame.
    virtual CommonGizmosDataID on_get_requirements() const override;
    // [EVENT] Shows the ImGui overlay for boolean controls; convert to UI Toolkit VisualElement + event bridge when porting.
    virtual void on_render_input_window(float x, float y, float bottom_limit);

    // [THREAD] Serialization hooks run off the render loop when we load/save projects so they must not issue GL calls directly.
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;

private:
    // [STATE] Master enable flag that keeps the gizmo reactive when the GL context is ready.
    bool m_enable{false};
    // [STATE] Tracks which boolean operator is selected in the ImGui window.
    MeshBooleanOperation m_operation_mode;
    // [STATE] Which axis-of-picking the pointer belongs to, so we know if the next click should update source or tool.
    MeshBooleanSelectingState m_selecting_state;
    // [STATE] Toggles whether the difference branch should delete the original mesh after creation.
    bool m_diff_delete_input = false;
    // [STATE] Variant toggle for intersection cleanup behavior.
    bool m_inter_delete_input = false;
    // [STATE] Snapshot of the source volume used for the boolean.
    VolumeInfo m_src;
    // [STATE] Snapshot of the tool volume used for the boolean.
    VolumeInfo m_tool;

    // [OPENGL] Builds/commits a new TriangleMesh for the scene while optionally removing the input; Unity should mirror this by updating
    // MeshFilters and re-uploading to the GPU on the main thread. [PORTING_HAZARD:P2] Mesh replacements require synchronizing physics
    // colliders and shared pointers to avoid leaks.
    void generate_new_volume(bool delete_input, const TriangleMesh& mesh_result);
};

}} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoMeshBoolean_hpp_
