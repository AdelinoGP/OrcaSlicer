#ifndef slic3r_GUI_GLGizmosCommon_hpp_
#define slic3r_GUI_GLGizmosCommon_hpp_

#include <memory>
#include <map>

#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/MeshUtils.hpp"

namespace Slic3r {

class ModelObject;
class ModelInstance;
class SLAPrintObject;
class ModelVolume;

namespace GUI {

class GLCanvas3D;

// [INTENT] Share the common canvas-facing data (selection, clipping, raycasts) once per frame so multiple gizmos can reuse it without
// recomputing. [UNITY] Map to a persistent MonoBehaviour (e.g., `GizmoResourcePool`) tied to a ScriptableObject cache that
// `GizmoController` commands during Unity's Update.

// [EVENT] Discrete input events fired by `GLCanvas3D` and consumed by each gizmo implementation, driving drag/selection gestures and
// modifier keys.
enum class SLAGizmoEventType : unsigned char {
    LeftDown = 1,
    LeftUp,
    RightDown,
    RightUp,
    Dragging,
    Delete,
    SelectAll,
    CtrlDown,
    CtrlUp,
    ShiftDown,
    ShiftUp,
    AltUp,
    Escape,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane,
    Moving
};

class CommonGizmosDataBase;
class AssembleViewDataBase;
namespace CommonGizmosDataObjects {
class SelectionInfo;
class InstancesHider;
class HollowedMesh;
class Raycaster;
class ObjectClipper;
class SupportsClipper;
} // namespace CommonGizmosDataObjects

namespace AssembleViewDataObjects {
class ModelObjectsInfo;
class ModelObjectsClipper;
} // namespace AssembleViewDataObjects

// Some of the gizmos use the same data that need to be updated ocassionally.
// It is also desirable that the data are not recalculated when the gizmos
// are just switched, but on the other hand, they should be released when
// they are not in use by any gizmo anymore.

// [STATE] Bitmask describing which shared data objects a gizmo currently needs; used to update/release just the required subsets without
// rebuilding everything. [UNITY] Corresponds to a `[Flags]` enum or `EnumMaskField` in Unity so the C# controller can request cached
// resources per-gizmo mode.
enum class CommonGizmosDataID {
    None           = 0,
    SelectionInfo  = 1 << 0,
    InstancesHider = 1 << 1,
    Raycaster      = 1 << 3,
    ObjectClipper  = 1 << 4,

};

// Following class holds pointers to the common data objects and triggers
// their updating/releasing. There is just one object of this type (managed
// by GLGizmoManager, the gizmos keep a pointer to it.
// [INTENT] Lazily refresh shared gizmo resources only when a mode needs them, avoiding redundant per-gizmo recomputation between
// activations. [THREAD] Supplied `update()` calls must run on the GL/UI thread because each resource currently ties directly to
// `GLCanvas3D` state. [UNITY] Port as a `GizmoResourcePool` MonoBehaviour that updates cached compute meshes and selection state during
// Unity's Update/LateUpdate. [PORTING_HAZARD:P2] Direct pointer to `GLCanvas3D` means lifetime management currently expects wxGLCanvas, so
// a C# bridge that owns the RenderTexture/camera is required.
class CommonGizmosDataPool
{
public:
    explicit CommonGizmosDataPool(GLCanvas3D* canvas);

    // Update all resources and release what is not used.
    // Accepts a bitmask of currently required resources.
    // [EVENT] GLGizmoManager feeds this bitmask every frame to synchronize pool contents with the active gizmo set.
    void update(CommonGizmosDataID required);

    // Getters for the data that need to be accessed from the gizmos directly.
    CommonGizmosDataObjects::SelectionInfo*  selection_info() const;
    CommonGizmosDataObjects::InstancesHider* instances_hider() const;
    //    CommonGizmosDataObjects::HollowedMesh* hollowed_mesh() const;
    CommonGizmosDataObjects::Raycaster*     raycaster_ptr();
    CommonGizmosDataObjects::Raycaster*     raycaster() const;
    CommonGizmosDataObjects::ObjectClipper* object_clipper() const;
    // CommonGizmosDataObjects::SupportsClipper* supports_clipper() const;

    GLCanvas3D* get_canvas() const { return m_canvas; }

private:
    std::map<CommonGizmosDataID, std::unique_ptr<CommonGizmosDataBase>> m_data;
    GLCanvas3D*                                                         m_canvas;

#ifndef NDEBUG
    bool check_dependencies(CommonGizmosDataID required) const;
#endif
};

// Base class for a wrapper object managing a single resource.
// Each of the enum values above (safe None) will have an object of this kind.
class CommonGizmosDataBase
{
public:
    // Pass a backpointer to the pool, so the individual
    // objects can communicate with one another.
    explicit CommonGizmosDataBase(CommonGizmosDataPool* cgdp) : m_common{cgdp} {}
    virtual ~CommonGizmosDataBase() {}

    // Update the resource.
    void update()
    {
        on_update();
        m_is_valid = true;
    }

    // Release any data that are stored internally.
    void release()
    {
        on_release();
        m_is_valid = false;
    }

    // Returns whether the resource is currently maintained.
    bool is_valid() const { return m_is_valid; }

#ifndef NDEBUG
    // Return a bitmask of all resources that this one relies on.
    // The dependent resource must have higher ID than the one
    // it depends on.
    virtual CommonGizmosDataID get_dependencies() const { return CommonGizmosDataID::None; }
#endif // NDEBUG

protected:
    virtual void          on_release() = 0;
    virtual void          on_update()  = 0;
    CommonGizmosDataPool* get_pool() const { return m_common; }

private:
    bool                  m_is_valid = false;
    CommonGizmosDataPool* m_common   = nullptr;
};

// [STATE] Specializations for the main gizmo pool's cached helpers live here to avoid GUI namespace clashes.
// [UNITY] Treat these as frame-snapshot services owned by the gizmo cache rather than standalone scene objects.
namespace CommonGizmosDataObjects {

// [STATE] Caches the active model object, instance index, and SLA shift for selection-sensitive gizmo helpers.
// [UNITY] This becomes a selection snapshot service that other gizmos read from a shared frame cache.
class SelectionInfo : public CommonGizmosDataBase
{
public:
    explicit SelectionInfo(CommonGizmosDataPool* cgdp) : CommonGizmosDataBase(cgdp) {}

    // [STATE] Returns the currently selected model object, or null when nothing is active.
    ModelObject* model_object() const { return m_model_object; }
    // [STATE] Returns the active instance index for selection-aware clipping and raycasting.
    int get_active_instance() const;
    // [STATE] Returns the cached SLA Z shift used when drawing object cuts.
    float get_sla_shift() const { return m_z_shift; }

protected:
    void on_update() override;
    void on_release() override;

private:
    ModelObject* m_model_object = nullptr;
    // int m_active_inst = -1;
    float m_z_shift = 0.f;
};

// [INTENT] Hide all non-active instances while rendering object-specific cut geometry in the gizmo views.
// [STATE] Rebuilds per-mesh clipper objects when the active model mesh set changes.
// [UNITY] Mirror this with renderer visibility masks plus per-mesh clipping materials.
class InstancesHider : public CommonGizmosDataBase
{
public:
    explicit InstancesHider(CommonGizmosDataPool* cgdp) : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    // [OPENGL] Draws the active instance cut mesh after the visibility mask has been applied.
    void render_cut() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*>          m_old_meshes;
    std::vector<std::unique_ptr<MeshClipper>> m_clippers;
};

// [INTENT] Rebuild raycasters for the meshes in the current selection so gizmos can perform hit tests and picking.
// [STATE] Owns a mesh-raycaster list plus the current mesh snapshot used to detect rebuilds.
// [UNITY] Port to a raycast service or collider cache that updates when the selection mesh set changes.
class Raycaster : public CommonGizmosDataBase
{
public:
    explicit Raycaster(CommonGizmosDataPool* cgdp) : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    // [STATE] Returns the primary mesh raycaster for the current selection snapshot.
    const MeshRaycaster* raycaster() const
    {
        assert(m_raycasters.size() == 1);
        return m_raycasters.front().get();
    }
    // [STATE] Returns all active raycasters, including one per mesh when the selection is split.
    std::vector<const MeshRaycaster*> raycasters() const;
    // [STATE] Controls whether the raycaster should only consider support-model parts.
    void set_only_support_model_part_flag(bool);

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<std::unique_ptr<MeshRaycaster>> m_raycasters;
    std::vector<const TriangleMesh*>            m_old_meshes;
    bool                                        m_only_support_model_part{true};
};

// [INTENT] Own and update the active clipping plane used by object cut views and auxiliary gizmo previews.
// [STATE] Stores the current clip ratio, bounding radius, and cached cut geometry helpers.
// [UNITY] Port as a clipping-plane controller plus a shader-driven cut renderer.
class ObjectClipper : public CommonGizmosDataBase
{
public:
    explicit ObjectClipper(CommonGizmosDataPool* cgdp) : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG
    // [STATE] Exposes the normalized clip position used by UI controls and downstream renderers.
    double get_position() const { return m_clp_ratio; }
    // [EVENT] Resets the clipping plane to the first visible layer and marks the canvas dirty.
    void set_position_to_init_layer();
    // [STATE] Returns the active clipping plane so other helpers can mirror or invert it.
    const ClippingPlane* get_clipping_plane(bool ignore_hide_clipped = false) const;
    // [OPENGL] Draws the clipped mesh surfaces for the current object selection.
    void render_cut(const std::vector<size_t>* ignore_idxs = nullptr) const;
    // [EVENT] Updates the cut ratio from a UI drag or slider while preserving the plane orientation.
    void set_position_by_ratio(double pos, bool keep_normal, bool vertical_normal = false);
    // [EVENT] Recomputes the clipping range and position from a normalized direction vector and offset.
    void set_range_and_pos(const Vec3d& cpl_normal, double cpl_offset, double pos);
    // [STATE] Toggles whether clipped geometry is hidden, filled, or outlined.
    void set_behavior(bool hide_clipped, bool fill_cut, double contour_width);

    // [STATE] Returns the number of generated contours in the current clipping plane.
    int get_number_of_contours() const;
    // [STATE] Returns sampled contour points for UI overlays or downstream export.
    std::vector<Vec3d> point_per_contour() const;

    // [STATE] Tests whether a point projects inside the active cut region.
    int is_projection_inside_cut(const Vec3d& point_in) const;
    // [STATE] Reports whether the clip geometry has been initialized and contains valid contour data.
    bool has_valid_contour() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*>                                               m_old_meshes;
    std::vector<std::pair<std::unique_ptr<MeshClipper>, Geometry::Transformation>> m_clippers;
    std::unique_ptr<ClippingPlane>                                                 m_clp;
    double                                                                         m_clp_ratio             = 0.;
    double                                                                         m_active_inst_bb_radius = 0.;
    bool                                                                           m_hide_clipped          = true;
};

} // namespace CommonGizmosDataObjects

// [STATE] Enumeration of the assemble-view resource kinds that are cached separately from the main selection pool.
// [UNITY] Use a second cache mask or view-mode enum so assembly-only helpers can be refreshed independently.
enum class AssembleViewDataID {
    None                = 0,
    ModelObjectsInfo    = 1 << 0,
    ModelObjectsClipper = 1 << 4,
};

class AssembleViewDataPool
{
public:
    AssembleViewDataPool(GLCanvas3D* canvas);

    // [EVENT] Refreshes the assemble-view helpers requested by the active controller and releases the rest.
    void update(AssembleViewDataID required);

    // [STATE] Accessors for the assemble-view snapshot and clipping helpers.
    AssembleViewDataObjects::ModelObjectsInfo*    model_objects_info() const;
    AssembleViewDataObjects::ModelObjectsClipper* model_objects_clipper() const;

    GLCanvas3D* get_canvas() const { return m_canvas; }

private:
    std::map<AssembleViewDataID, std::unique_ptr<AssembleViewDataBase>> m_data;
    GLCanvas3D*                                                         m_canvas;

#ifndef NDEBUG
    bool check_dependencies(AssembleViewDataID required) const;
#endif
};

// [INTENT] Wrap one assemble-view resource with the same lazy update/release lifecycle as the main gizmo pool.
// [STATE] Holds the owning pool back-pointer and validity bit for the cached helper.
class AssembleViewDataBase
{
public:
    // Pass a backpointer to the pool, so the individual
    // objects can communicate with one another.
    explicit AssembleViewDataBase(AssembleViewDataPool* cgdp) : m_common{cgdp} {}
    virtual ~AssembleViewDataBase() {}

    // Update the resource.
    void update()
    {
        on_update();
        m_is_valid = true;
    }

    // Release any data that are stored internally.
    void release()
    {
        on_release();
        m_is_valid = false;
    }

    // Returns whether the resource is currently maintained.
    bool is_valid() const { return m_is_valid; }

#ifndef NDEBUG
    // Return a bitmask of all resources that this one relies on.
    // The dependent resource must have higher ID than the one
    // it depends on.
    virtual AssembleViewDataID get_dependencies() const { return AssembleViewDataID::None; }
#endif // NDEBUG

protected:
    virtual void          on_release() = 0;
    virtual void          on_update()  = 0;
    AssembleViewDataPool* get_pool() const { return m_common; }

private:
    bool                  m_is_valid = false;
    AssembleViewDataPool* m_common   = nullptr;
};

namespace AssembleViewDataObjects {

// [STATE] Caches the current model objects and SLA shift used by assembly-view clipping and selection helpers.
// [UNITY] Model this as a list snapshot service driven by the active assembly view controller.
class ModelObjectsInfo : public AssembleViewDataBase
{
public:
    explicit ModelObjectsInfo(AssembleViewDataPool* cgdp) : AssembleViewDataBase(cgdp) {}

    // [STATE] Returns the cached model-object list for the current assembly snapshot.
    ModelObjectPtrs model_objects() const { return m_model_objects; }
    // int get_active_instance() const;
    // [STATE] Returns the per-object SLA shift used by clipping/render offsets.
    float get_sla_shift() const { return m_z_shift; }

protected:
    void on_update() override;
    void on_release() override;

private:
    ModelObjectPtrs m_model_objects;
    float           m_z_shift = 0.f;
};

// [INTENT] Maintain assemble-view clipping geometry for the current object set.
// [STATE] Owns the cut plane, generated clippers, and the current clipping ratio/radius used by the assembly UI.
// [UNITY] Port as a cut-plane controller that rebuilds per-object preview meshes when the view changes.
class ModelObjectsClipper : public AssembleViewDataBase
{
public:
    explicit ModelObjectsClipper(AssembleViewDataPool* cgdp) : AssembleViewDataBase(cgdp) {}
#ifndef NDEBUG
    AssembleViewDataID get_dependencies() const override { return AssembleViewDataID::ModelObjectsInfo; }
#endif // NDEBUG

    // [EVENT] Moves the clipping plane from the UI while optionally preserving the previous normal.
    void set_position(double pos, bool keep_normal);
    // [STATE] Returns the normalized clipping ratio used by the assembly controls.
    double get_position() const { return m_clp_ratio; }
    // [STATE] Returns the current clipping plane, or null when no cut plane is active.
    ClippingPlane* get_clipping_plane() const { return m_clp.get(); }
    // [OPENGL] Renders the clipped object outlines for the assembly view.
    void render_cut() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*>          m_old_meshes;
    std::vector<std::unique_ptr<MeshClipper>> m_clippers;
    std::unique_ptr<ClippingPlane>            m_clp;
    double                                    m_clp_ratio             = 0.;
    double                                    m_active_inst_bb_radius = 0.;
};
} // namespace AssembleViewDataObjects

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_GLGizmosCommon_hpp_
