#ifndef slic3r_GLGizmoSlaSupports_hpp_
#define slic3r_GLGizmoSlaSupports_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"

#include "libslic3r/SLA/SupportPoint.hpp"
#include "libslic3r/ObjectID.hpp"
#include <wx/dialog.h>

#include <cereal/types/vector.hpp>

namespace Slic3r {

class ConfigOption;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;

// [INTENT] Encapsulates SLA-specific support point editing on top of the shared gizmo stack so SLA builds can paint and manipulate
// auxiliary points without affecting the other contexts. [UNITY] Bring this in as a MonoBehaviour that owns a RenderTexture overlay (for
// non-UI preview) or UI Toolkit VisualElement with child MeshInstances; each point becomes a prefab (cylinder/cone/sphere) whose
// transforms/hitboxes mirror the original geometry. [PORTING_HAZARD:P2] Editing flows rely on sequential GL event ordering (hover -> drag
// -> release) plus cached selection rectangles, so Unity will need to sequence InputSystem presses carefully to avoid false state
// transitions.
class GLGizmoSlaSupports : public GLGizmoBase
{
private:
    // [EVENT] Maps the current mouse position to a surface point/normal when the SLA gizmo needs to spawn or drag support points.
    // [UNITY] This mirrors Unity's ScreenPointToRay plus MeshCollider.Raycast combo in a MonoBehaviour, so preserve the raycast order when porting.
    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

    // [STATE] Basis scale for point rendering so Unity can reuse the same multiplier for prefab scaling.
    const float RenderPointScale = 1.f;

    // [STATE] Holds cached SLA support points plus selection/normal metadata so the gizmo can restore edits/undo-redo snapshots.
    // [UNITY] Mirror this as a serializable struct that can be pushed into Unity's Undo/Prefab system when editing begins.
    class CacheEntry
    {
    public:
        CacheEntry() : support_point(sla::SupportPoint()), selected(false), normal(Vec3f::Zero()) {}

        CacheEntry(const sla::SupportPoint& point, bool sel = false, const Vec3f& norm = Vec3f::Zero())
            : support_point(point), selected(sel), normal(norm)
        {}

        bool operator==(const CacheEntry& rhs) const { return (support_point == rhs.support_point); }

        bool operator!=(const CacheEntry& rhs) const { return !((*this) == rhs); }

        sla::SupportPoint support_point; // [STATE] Persisted mesh coordinates for this support point entry.
        bool              selected;      // [STATE] Part of the current multi-selection bucket.
        Vec3f             normal;        // [STATE] Cached normal used for rendering and extra heuristics.

        template<class Archive> void serialize(Archive& ar) { ar(support_point, selected, normal); }
    };

public:
    GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoSlaSupports() = default;
    // [STATE] Mirrors slider/config deltas from the SLA backend before entering editing mode so that edits stay aligned with the ModelObject.
    void set_sla_support_data(ModelObject* model_object, const Selection& selection);
    // [EVENT] Receives action+mouse_position from GLGizmosManager and translates them into support point additions, drags, or context
    // actions. [UNITY] In Unity, route these through InputSystem actions (Press/Drag/Release) to keep state transitions deterministic.
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    // [EVENT] Deletes selection following either menu commands or hotkeys and defers to backend removal logic.
    void delete_selected_points(bool force = false);
    // ClippingPlane get_sla_clipping_plane() const;

    bool is_in_editing_mode() const { return m_editing_mode; }
    bool is_selection_rectangle_dragging() const { return m_selection_rectangle.is_dragging(); }
    bool has_backend_supports() const;
    // [THREAD] Triggers backend SLA recalculation; [UNITY] implement as async/await that marshals results back to the Unity main thread for
    // error reporting.
    void reslice_SLA_supports(bool postpone_error_messages = false) const;

    bool        wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return "Entering SLA support points"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving SLA support points"; }

private:
    // [INTENT] Allocate GLModel meshes (cone/cylinder/sphere) and localized text before the gizmo appears so rendering can stay fast.
    bool on_init() override;
    // [EVENT] Called every frame to mirror the backend state and respond to selection rectangles or editing input.
    void on_update(const UpdateData& data) override;
    // [OPENGL] Executes the OpenGL rendering pipeline for support point markers via cached GLModel instances.
    void on_render() override;

    // [OPENGL] Draws the cone/cylinder/sphere representations for every point, with optional color for picking.
    void render_points(const Selection& selection, bool picking = false);
    // [STATE] Tells the UI whether editing cache differs from the backend so Save/Cancel banners show correctly.
    bool unsaved_changes() const;

    // [STATE] Prevents editing across multiple disconnected islands while resizing or moving points.
    bool m_lock_unique_islands = false;
    // [STATE] Guard to expose whether we are currently in editing lifecycle.
    bool m_editing_mode = false;
    // [STATE] Tracks diameter captured while the user is about to place a point.
    float m_new_point_head_diameter;
    // [STATE] Stores the most recent point before drag operations to support undo/redo.
    CacheEntry m_point_before_drag;
    // [STATE] Keep consistent diameter/density stash for dialog restores, ensuring sliders can revert.
    float m_old_point_head_diameter      = 0.;
    float m_minimal_point_distance_stash = 0.f;
    float m_density_stash                = 0.f;
    // [STATE] Temporary editing cache of support points and their selection bits.
    mutable std::vector<CacheEntry> m_editing_cache;
    // [STATE] Normal cache holds backend normals for discarding edits without losing surface alignment.
    std::vector<sla::SupportPoint> m_normal_cache;
    ObjectID                       m_old_mo_id;

    // [OPENGL] Prebuilt meshes for the visual affordances (cone/cylinder/sphere) reused every frame.
    // [UNITY] Port as cached Mesh + Material assets instantiated via MeshFilter/MeshRenderer per support point.
    GLModel m_cone;
    GLModel m_cylinder;
    GLModel m_sphere;

    // [STATE] Stores translated description text used during layout; Unity would host this in a LocalizationService so tooltips remain
    // language-aware.
    std::map<std::string, wxString> m_desc;

    // [EVENT] Tracks mouse drag rectangles used for multi-point selection; needs a UI Toolkit RectangleSelector in Unity.
    GLSelectionRectangle m_selection_rectangle;

    // [STATE] Guards around dragging lifecycle to prevent duplicate release events.
    bool   m_wait_for_up_event = false;
    bool   m_selection_empty   = true;
    EState m_old_state         = Off; // [STATE] Helps detect the just-closed transition when on_set_state fires.

    // [INTENT] Materializes ConfigOption references needed for dialogs that alter density/spacing.
    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;
    // [STATE] Guards mesh-space constraints so we avoid placing supports inside forbidden geometry.
    bool is_mesh_point_clipped(const Vec3d& point) const;
    bool is_point_in_hole(const Vec3f& pt) const;
    // void find_intersecting_facets(const igl::AABB<Eigen::MatrixXf, 3>* aabb, const Vec3f& normal, double offset, std::vector<unsigned
    // int>& out) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    // [EVENT] Adjusts the selection bitset for point i.
    void select_point(int i);
    void unselect_point(int i);
    // [EVENT] Commit/discard editing mode edits back to the backend caches and notify the ModelObject.
    void editing_mode_apply_changes();
    void editing_mode_discard_changes();
    void reload_cache();
    void get_data_from_backend();
    void auto_generate();
    void switch_to_editing_mode();
    void disable_editing_mode();
    // [EVENT] Prompts the user and schedules the deferred callbacks for yes/no paths.
    void ask_about_changes_call_after(std::function<void()> on_yes, std::function<void()> on_no);

protected:
    // [STATE] Performs cleanup when the gizmo is disabled to keep cached selection consistent.
    void on_set_state() override;
    // [EVENT] Keeps the hover index valid as the editing cache length fluctuates.
    void on_set_hover_id() override

    {
        if (!m_editing_mode || (int) m_editing_cache.size() <= m_hover_id)
            m_hover_id = -1;
    }
    // [EVENT] GLSelectionRectangle fires this when dragging begins.
    void on_start_dragging() override;
    void on_stop_dragging() override;
    // [OPENGL] Draws the floating input window shown while moving or resizing support points.
    void on_render_input_window(float x, float y, float bottom_limit) override;

    // [EVENT] Provides metadata required by GLGizmosManager to show names/tooltips.
    std::string                on_get_name() const override;
    bool                       on_is_activable() const override;
    bool                       on_is_selectable() const override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    // [THREAD] Serialize/deserialize editing cache snapshots so undo survives reloads without stalling render.
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;
};

// [INTENT] Simple help dialog explaining SLA support workflow.
// [UNITY] Replace this with a UI Toolkit modal (VisualElement with Labels and Button) showing the same guidance.
class SlaGizmoHelpDialog : public wxDialog
{
public:
    SlaGizmoHelpDialog();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoSlaSupports_hpp_
