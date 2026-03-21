#ifndef slic3r_GLGizmoFlatten_hpp_
#define slic3r_GLGizmoFlatten_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/MeshUtils.hpp"

namespace Slic3r {

enum class ModelVolumeType : int;

namespace GUI {

// [INTENT] Flatten tool that projects meshes onto planar patches so users can level a selection before slicing.
// [UNITY] Implement as a MonoBehaviour that holds a `MeshFilter` for the preview planes, a `MeshCollider` for picking, and a shared
// ScriptableObject selection state.
class GLGizmoFlatten : public GLGizmoBase
{
    // [STATE] m_hover_id associates with internal plane polygons instead of grabbers, matching GLGizmoFlatten's own picking data.

private:
    struct PlaneData
    {
        std::vector<Vec3d> vertices; // should be in fact local in update_planes()
        // [OPENGL] VBO + normal/area cache used while drawing this plane overlay.
        PickingModel vbo;
        Vec3d        normal;
        float        area;
        // [EVENT] Picking ID ties each plane to the SceneRaycaster rather than a grabber set.
        int picking_id{-1};
    };

    // This holds information to decide whether recalculation is necessary:
    // [STATE] Matrices/types record the last known transform per volume so we rebuild only when something moved.
    std::vector<Transform3d>     m_volumes_matrices;
    std::vector<ModelVolumeType> m_volumes_types;
    Vec3d                        m_first_instance_scale;
    Vec3d                        m_first_instance_mirror;

    std::vector<PlaneData> m_planes;
    // [THREAD] Raycasters live on the UI thread; keep shared_ptrs so removal happens safely during cleanup.
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_planes_casters;
    // [STATE] Track the previous object/instance so plane recomputation only happens when selection changes.
    const ModelObject* m_old_model_object = nullptr;
    int                m_old_instance_id{-1};

    // [STATE] Rebuilds the plane meshes/colliders from the underlying GLModel geometry when transforms drift.
    // [PORTING_HAZARD:P2] Unity needs to track the same mesh/instance transforms, otherwise flatten visuals go stale.
    void update_planes();
    // [STATE] Compares the cached matrices/types against the active model to avoid redundant updates.
    bool is_plane_update_necessary() const;

public:
    GLGizmoFlatten(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    // [INTENT] Cache the model/instance to keep the flatten planes aligned with the selected mesh geometry.
    // [UNITY] Listen to a `ScriptableObject` selection model so the flatten MonoBehaviour knows when to rebuild.
    void set_flattening_data(const ModelObject* model_object, int instance_id);

    /// <summary>
    /// Apply rotation on select plane
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    // [EVENT] Consume mouse events and rotate the selected flatten plane while preventing other gizmos from reacting.
    // [THREAD] Runs on wxWidgets' UI thread, so maintain the expectation of no background thread calls.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    // [STATE] Triggered after serialization/undo or when the mesh mutates so caches refresh.
    void data_changed(bool is_serializing) override;

protected:
    bool        on_init() override;
    std::string on_get_name() const override;
    bool        on_is_activable() const override;
    // [OPENGL] Draw the cached planes/stencils after the main mesh render to keep the overlay aligned with the scene.
    void on_render() override;
    // [EVENT] Replace grabbers with SceneRaycaster entries, mapping clicks to internal plane IDs.
    // [UNITY] Equivalent to toggling a `GraphicRaycaster` layer and `Physics.Raycast` on a Plane collider.
    void on_register_raycasters_for_picking() override;
    // [EVENT] Remove the raycaster hooks immediately when the tool loses focus to avoid stale picks.
    void on_unregister_raycasters_for_picking() override;
    // [STATE] Reset caches when activation state changes (called from the base on toggle).
    void               on_set_state() override;
    CommonGizmosDataID on_get_requirements() const override;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoFlatten_hpp_
