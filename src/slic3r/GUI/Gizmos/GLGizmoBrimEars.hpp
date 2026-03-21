#ifndef slic3r_GLGizmoBrimEars_hpp_
#define slic3r_GLGizmoBrimEars_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLSelectionRectangle.hpp"
#include "libslic3r/BrimEarsPoint.hpp"
#include "libslic3r/ObjectID.hpp"

namespace Slic3r {

class ConfigOption;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;

class GLGizmoBrimEars : public GLGizmoBase
// [INTENT] Specializes the base gizmo to surface BrimEars control points, bridging raycast editing, undo/redo cache, and localized info
// overlays for the SLA workflow. [UNITY] Map to a MonoBehaviour that spawns interactive handle meshes via Graphics.DrawMesh, routes
// `PointerEventData` through UI Toolkit overlays, and keeps a ScriptableObject state model synced with slicer data. [PORTING_HAZARD:P2]
// wxMouseEvent-based input and GLVolume pointer lifetimes require a dedicated Unity picking/selection subsystem before reusing this logic.
{
private:
    using PickRaycaster = SceneRaycasterItem;

    // [EVENT] Converts mouse positions to world-space intersections for brim ears edit gestures.
    bool unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);
    bool unproject_on_mesh2(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal);

    // [STATE][OPENGL] Uniform scale factor for how large the rendered handle spheres appear on screen.
    const float RenderPointScale = 1.f;

    // [STATE] Stores brim point attributes, hover/selection flags, and normals used by undo/redo and render cache.
    class CacheEntry
    {
    public:
        CacheEntry() : brim_point(BrimPoint()), selected(false), normal(Vec3f(0, 0, 1)), is_hover(false), is_error(false) {}

        CacheEntry(const BrimPoint& point, bool sel = false, const Vec3f& norm = Vec3f(0, 0, 1), bool hover = false, bool error = false)
            : brim_point(point), selected(sel), normal(norm), is_hover(hover), is_error(error)
        {}

        bool operator==(const CacheEntry& rhs) const { return (brim_point == rhs.brim_point); }

        bool operator!=(const CacheEntry& rhs) const { return !((*this) == rhs); }

        inline bool pos_is_zero() { return brim_point.pos.isZero(); }

        void set_empty()
        {
            brim_point = BrimPoint();
            selected   = false;
            normal.setZero();
            is_hover = false;
            is_error = false;
        }

        BrimPoint brim_point;
        bool      selected; // whether the point is selected
        bool      is_hover; // show mouse hover cylinder
        bool      is_error;
        Vec3f     normal;

        template<class Archive> void serialize(Archive& ar) { ar(brim_point, selected, normal); }
    };

public:
    GLGizmoBrimEars(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoBrimEars() = default;
    // [EVENT][STATE] Rebuilds cached brim data or serialization state when the underlying object/config changes.
    void data_changed(bool is_serializing) override;
    // [STATE] Regenerates the cached brim ears from the slicer data before rendering.
    void set_brim_data();
    // [EVENT] Handles wx mouse events by controlling hit detection and hover/detection state.
    bool on_mouse(const wxMouseEvent& mouse_event) override;
    // [EVENT] Responds to gizmo-level commands, including modifier flags, from the SLA UI. [UNITY] Translate into UnityEvent invocations
    // from the UI Toolkit controls.
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);
    // [STATE] Erases selected handles from the editing cache and triggers model update.
    void delete_selected_points();
    // [STATE] Pushes cached changes back into the model to trigger slicer reflow.
    void update_model_object();
    // ClippingPlane get_sla_clipping_plane() const;

    bool is_selection_rectangle_dragging() const { return m_selection_rectangle.is_dragging(); }

    bool        wants_enter_leave_snapshots() const override { return true; }
    std::string get_gizmo_entering_text() const override { return "Entering Brim Ears"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving Brim Ears"; }

private:
    bool on_init() override;
    // [EVENT] Called by GLGizmoBase when the user drags handles; updates cached selection state.
    void on_dragging(const UpdateData& data) override;
    // [OPENGL] Hooks the renderer to draw brim handles each frame after update.
    void on_render() override;

    // [OPENGL] Draws handles, hover cylinders, and selection highlights for the current frame. [UNITY] Mirror with Graphics.DrawMesh on a
    // dedicated overlay camera to visualize cylinders/points.
    void render_points(const Selection& selection);

    // [STATE] Diameter used when spawning a new brim handle (rendered via render_points).
    float m_new_point_head_diameter; // Size of a new point.
    // [STATE] Maximum angle a brim ear can span before being clamped.
    float m_max_angle = 125.f;
    // [STATE] Distance around a handle considered for picking.
    float  m_detection_radius     = 1.f;
    double m_detection_radius_max = .0f;
    // [STATE] Snapshot for undo/redo so edits can revert to the previous point.
    CacheEntry m_point_before_drag; // undo/redo - so we know what state was edited
    // [STATE] Previous head diameter persisted while adjusting new handles.
    float m_old_point_head_diameter = 0.; // the same
    // [STATE][THREAD] Editing cache mutated by mouse interactions to keep selection metadata.
    mutable std::vector<CacheEntry> m_editing_cache; // a support point and whether it is currently selectedchanges or undo/redo
    // [STATE] Stores a single brim entry when running specialized alignment logic.
    std::map<int, CacheEntry> m_single_brim;
    // [STATE] Tracks the ModelObject id currently being edited to detect swaps.
    ObjectID    m_old_mo_id;
    const Vec3d m_world_normal = {0, 0, 1};
    // [STATE][THREAD][PORTING_HAZARD:P2] Map of GLVolume pointers to pick raycasters; Unity needs comparable MeshCollider references and
    // safe disposal.
    std::map<GLVolume*, std::shared_ptr<PickRaycaster>> m_mesh_raycaster_map;
    // [STATE] Stores last volume used for hit tests so consecutive picks stay consistent.
    GLVolume* m_last_hit_volume;
    // [STATE][OPENGL] Optional hover preview entry rendered by render_points.
    std::optional<CacheEntry> render_hover_point;

    // [STATE] Indicates localized tooltip text is being hovered for a hint overlay.
    bool m_link_text_hover = false;

    // [OPENGL] Cylinder mesh reused when drawing handles or hover indicators.
    PickingModel m_cylinder;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // [STATE] Cached localized strings used by tooltip/location calculations.
    std::map<std::string, wxString> m_desc;

    // [EVENT][OPENGL] Handles marquee selection drawing and translation from screen coordinates to selection bounds. [UNITY] Unity needs a
    // PointerDragSystem that keeps a Rect overlay + `EventSystem.current.RaycastAll` results.
    GLSelectionRectangle m_selection_rectangle;

    // [STATE] First-layer polygons used to generate brim ears relative to the selected model.
    ExPolygons m_first_layer;

    // [STATE][EVENT] True while waiting for the mouse-up event so drag edits finalize cleanly.
    bool m_wait_for_up_event = false;
    // [STATE] Tracks whether any handles remain selected for enabling delete/clear commands.
    bool m_selection_empty = true;
    // [STATE] Caches the prior gizmo state so on_set_state knows when the gizmo close transition happened.
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    // [INTENT] Helper to resolve config options that drive detection radius, angle, and display strings.
    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;
    // [STATE] Detects when a brim point is outside the slicer's clipping plane.
    bool is_mesh_point_clipped(const Vec3d& point) const;

    // [INTENT] Helpers that sync the editing cache, select/deselect points, and regenerate brim ears geometry.
    enum {
        AllPoints = -2,
        NoPoints,
    };
    // [STATE] Toggle individual handle selection/hover state.
    void select_point(int i);
    void unselect_point(int i);
    // [STATE] Fully reloads the cached brim data when the object or config changes.
    void reload_cache();
    // [INTENT] Generates brim handles from polygon slices and detection parameters.
    Points generate_points(Polygon& obj_polygon, float ear_detection_length, float brim_ears_max_angle, bool is_outer);
    // [INTENT] Auto-populates handles using slicer heuristics.
    void auto_generate();
    // [EVENT] Refreshes the first-layer polygons from the latest slicing result.
    void first_layer_slicer();
    // [STATE] Computes the maximum detection radius allowed for picks.
    void get_detection_radius_max();
    // [THREAD][EVENT] Rebuilds pick raycasters whenever GL volumes or brush states change.
    void update_raycasters();

    // [STATE] Begins manual radius edit, updating state along the way.
    void begin_radius_change(float initial_value);
    // [STATE] Keeps the cache radius in sync while dragging the slider.
    void update_cache_radius();
    // [STATE] Commits the changed radius back to the model and triggers rerender.
    void apply_radius_change();

protected:
    // [EVENT][STATE] Called when the gizmo transitions open/close to reset caches and raycasters.
    void on_set_state() override;
    void on_set_hover_id() override

    {
        if ((int) m_editing_cache.size() <= m_hover_id)
            m_hover_id = -1;
    }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    // [OPENGL][EVENT] Draws the floating info window that reports gizmo state in the viewport. [UNITY] Recreate with UI Toolkit
    // VisualElement anchored to the overlay camera.
    void on_render_input_window(float x, float y, float bottom_limit) override;
    // [EVENT] Populates tooltip text depending on the current brimming context. [UNITY] Mirror via Tooltip VisualElement attached to the
    // overlay tree.
    void show_tooltip_information(float x, float y);

    // [INTENT] Supplies the gizmo name displayed in the UI.
    std::string on_get_name() const override;
    // [STATE] Determines if the gizmo can activate based on the current context.
    bool on_is_activable() const override;
    // bool on_is_selectable() const override;
    // [STATE] Declares the data requirements GLGizmoBase provides to this gizmo.
    virtual CommonGizmosDataID on_get_requirements() const override;
    // [STATE] Persistence hooks invoked by cereal archives to rebuild or serialize the cache.
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override;
    // [THREAD][EVENT] Raycaster lifecycle hooks tied to gizmo activation.
    virtual void on_register_raycasters_for_picking() override;
    virtual void on_unregister_raycasters_for_picking() override;
    // [THREAD] Binds a single mesh pick helper per GLVolume when the gizmo enters edit mode.
    void register_single_mesh_pick();
    // void update_single_mesh_pick(GLVolume* v);
    // [THREAD][STATE] Clears all raycaster bindings when the gizmo deactivates.
    void reset_all_pick();
    // [STATE] Adds or updates brim points when the user edits the mesh interactively.
    bool add_point_to_cache(Vec3f pos, float head_radius, bool selected, Vec3f normal);
    // [INTENT] Provides the default radius heuristic used when adding new brim handles.
    float get_brim_default_radius() const;
    // [INTENT] Converts a brim point into a polygon for render or detection.
    ExPolygon make_polygon(BrimPoint point, const Geometry::Transformation& trsf);
    // [INTENT] Finds the single brim point matching current selection (used in auto mode).
    void find_single();
};

// [EVENT][THREAD] Worker-done event used to refresh the gizmo after background tasks complete; Unity would use `async/await` + dispatcher
// hooks instead.
wxDECLARE_EVENT(wxEVT_THREAD_DONE, wxCommandEvent);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBrimEars_hpp_
