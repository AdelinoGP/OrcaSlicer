#ifndef slic3r_GLGizmoPainterBase_hpp_
#define slic3r_GLGizmoPainterBase_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/GLModel.hpp"

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Model.hpp"

#include <cereal/types/vector.hpp>
#include <GL/glew.h>

#include <memory>

namespace Slic3r::GUI {

enum class SLAGizmoEventType : unsigned char;
class ClippingPlane;
struct Camera;
class GLGizmoMmuSegmentation;
class Selection;

enum class PainterGizmoType { FDM_SUPPORTS, SEAM, MM_SEGMENTATION, FUZZY_SKIN };

// [INTENT] Wrap TriangleSelector state so painter gizmos can re-render hit/selection geometry on demand.
// [UNITY] Translate to a Mesh/Graphics.DrawMeshNow combo with a dedicated MonoBehaviour caching the selection vertices.
class TriangleSelectorGUI : public TriangleSelector
{
public:
    explicit TriangleSelectorGUI(const TriangleMesh& mesh, float edge_limit = 0.6f) : TriangleSelector(mesh, edge_limit) {}
    virtual ~TriangleSelectorGUI() = default;

    virtual void render(ImGuiWrapper* imgui, const Transform3d& matrix);
    // void         render(const Transform3d& matrix) { this->render(nullptr, matrix); }
    void set_wireframe_needed(bool need_wireframe) { m_need_wireframe = need_wireframe; }
    bool get_wireframe_needed() { return m_need_wireframe; }

    // BBS
    // [EVENT] Called when triangle selector paint data changes so GL buffers are refreshed before next render.
    void request_update_render_data(bool paint_changed = false)
    {
        m_update_render_data = true;
        m_paint_changed |= paint_changed;
    }

    // BBS
    static ColorRGBA enforcers_color;
    static ColorRGBA blockers_color;

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    void render_debug(ImGuiWrapper* imgui);
    bool m_show_triangles{false};
    bool m_show_invalid{false};
#endif

protected:
    // [STATE] When true, GL buffers should be rebuilt before the next frame; set by request_update_render_data.
    bool m_update_render_data = false;
    // BBS
    // [STATE] True when the paint color/layout changed; used in draw routines to detect stale data.
    bool m_paint_changed = true;

    static ColorRGBA get_seed_fill_color(const ColorRGBA& base_color);

private:
    void update_render_data();

    GLModel                m_iva_enforcers;
    GLModel                m_iva_blockers;
    std::array<GLModel, 3> m_iva_seed_fills;
#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    std::array<GLModel, 3> m_varrays;
#endif // PRUSASLICER_TRIANGLE_SELECTOR_DEBUG

protected:
    GLModel m_paint_contour;

    void update_paint_contour();
    void render_paint_contour(const Transform3d& matrix);

    // [STATE] Enables optional wireframe rendering of triangle selectors for parity with the wx overlay UI.
    bool m_need_wireframe{false};
};

// [STATE] TrianglePatch bundles vertices/triangles/facet indices per enforcer/blocker so PainterBase can stream them to GL.
// [UNITY] Represent this as a struct of Unity Mesh submesh arrays (vertices + indices) used by Graphics.DrawMesh.
// BBS
struct TrianglePatch
{
    std::vector<float>            patch_vertices;
    std::vector<int>              triangle_indices;
    std::vector<int>              facet_indices;
    EnforcerBlockerType           type = EnforcerBlockerType::NONE;
    std::set<EnforcerBlockerType> neighbor_types;
    // if area is larger than GapAreaMax, stop accumulate left triangle areas to improve performance
    float area = 0.f;

    bool is_fragment() const;
};

// [INTENT] Capture per-type triangle patches so painter gizmos can upload only affected regions to the GPU.
// [UNITY] Mirror via a MonoBehaviour that pools Mesh + MeshCollider data per patch type.
class TriangleSelectorPatch : public TriangleSelectorGUI
{
public:
    explicit TriangleSelectorPatch(const TriangleMesh& mesh, const std::vector<ColorRGBA> ebt_colors, float edge_limit = 0.6f)
        : TriangleSelectorGUI(mesh, edge_limit), m_ebt_colors(ebt_colors)
    {}
    virtual ~TriangleSelectorPatch() = default;

    // Render current selection. Transformation matrices are supposed
    // to be already set.
    // [OPENGL] Draws patched selectors so the painter overlay matches selection mode; Unity uses Graphics.DrawMesh in a similar pass.
    void render(ImGuiWrapper* imgui, const Transform3d& matrix) override;
    // TriangleSelector.m_triangles => m_gizmo_scene.triangle_patches
    void update_triangles_per_type();
    // m_gizmo_scene.triangle_patches => TriangleSelector.m_triangles
    void update_selector_triangles();
    void update_triangles_per_patch();

    void set_ebt_colors(const std::vector<ColorRGBA> ebt_colors) { m_ebt_colors = ebt_colors; }
    void set_filter_state(bool is_filter_state);

    // [STATE] Gap area slider bounds; large thresholds stop triangle accumulation to protect performance.
    constexpr static float GapAreaMin  = 0.f;
    constexpr static float GapAreaMax  = 5.f;
    constexpr static float GapAreaStep = 0.2f;

    // [PORTING_HAZARD:P3] gap_area is shared mutable state, so the Unity port should centralize the slider value in a ScriptableObject.
    static float gap_area;

protected:
    // [OPENGL] Release VAO/VBO handles before rebuilding patch geometry; Unity would call Mesh.Clear and reassign vertex/index buffers.
    void release_geometry();
    // Finalize the initialization of the geometry, upload the geometry to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_vertices();
    // Finalize the initialization of the indices, upload the indices to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_triangle_indices();

    void clear()
    {
        // BBS
        this->m_vertices_VBO_ids.clear();
        this->m_triangle_indices_VBO_ids.clear();
        this->m_triangle_indices_sizes.clear();

        for (TrianglePatch& patch : this->m_triangle_patches) {
            patch.patch_vertices.clear();
            patch.triangle_indices.clear();
        }
        this->m_triangle_patches.clear();
    }

    [[nodiscard]] inline bool has_VBOs(size_t triangle_indices_idx) const
    {
        assert(triangle_indices_idx < this->m_triangle_patches.size());
        return this->m_triangle_indices_VBO_ids[triangle_indices_idx] != 0;
    }

    // std::vector<float>          m_patch_vertices;
    std::vector<TrianglePatch> m_triangle_patches;

    // When the triangle indices are loaded into the graphics card as Vertex Buffer Objects,
    // the above mentioned std::vectors are cleared and the following variables keep their original length.
    std::vector<size_t> m_triangle_indices_sizes;

    // IDs of the Vertex Array Objects, into which the geometry has been loaded.
    // Zero if the VBOs are not sent to GPU yet.
    unsigned int m_vertices_VAO_id{0};
    // unsigned int                m_vertices_VBO_id{ 0 };
    std::vector<unsigned int> m_vertices_VBO_ids;
    std::vector<unsigned int> m_triangle_indices_VBO_ids;

    std::vector<ColorRGBA> m_ebt_colors;

    bool m_filter_state = false;

private:
    void update_render_data();
    void render(int buffer_idx, bool show_wireframe = false);
};

// Following class is a base class for a gizmo with ability to paint on mesh
// using circular blush (such as FDM supports gizmo and seam painting gizmo).
// The purpose is not to duplicate code related to mesh painting.
// [INTENT] Centralize render/input/state helpers used by all painter gizmos to keep cursor/triangle overlays in sync with model data.
// [UNITY] Port to a MonoBehaviour owning Mesh/Material references, feeding data into Graphics.DrawMesh and the new Input System.
class GLGizmoPainterBase : public GLGizmoBase
{
private:
    ObjectID m_old_mo_id;
    size_t   m_old_volumes_size = 0;
    void     on_render() override {}

public:
    GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoPainterBase() override;
    void data_changed(bool is_serializing) override;
    // [EVENT] Receives high-level gizmo events (mouse/keyboard input) from the manager before the painter processes them.
    virtual bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    // Following function renders the triangles and cursor. Having this separated
    // from usual on_render method allows to render them before transparent
    // objects, so they can be seen inside them. The usual on_render is called
    // after all volumes (including transparent ones) are rendered.
    // [OPENGL] Render triangles/cursor before transparent volumes so the gizmo stays visible; translated to Unity DrawMesh in a dedicated
    // overlay pass.
    virtual void render_painter_gizmo() = 0;

    virtual const float get_cursor_radius_min() const { return CursorRadiusMin; }
    virtual const float get_cursor_radius_max() const { return CursorRadiusMax; }
    virtual const float get_cursor_radius_step() const { return CursorRadiusStep; }

    // BBS: just for CursorType::HeightRange
    virtual const float get_cursor_height_min() const { return CursorHeightMin; }
    virtual const float get_cursor_height_max() const { return CursorHeightMax; }
    virtual const float get_cursor_height_step() const { return CursorHeightStep; }

    /// <summary>
    /// Implement when want to process mouse events in gizmo
    /// Click, Right click, move, drag, ...
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information and don't want to
    /// propagate it otherwise False.</returns>
    // [EVENT] Mouse hook used to capture drag/click gestures before other gizmos react.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

protected:
    // [OPENGL] Paint the triangle selectors collected from `selection` so the overlay matches mesh highlights.
    virtual void render_triangles(const Selection& selection) const;
    // [OPENGL] Draw the cursor circle and sphere via GLModel caches so that Unity can swap in Graphics.DrawMesh calls.
    void render_cursor();
    void render_cursor_circle();
    void render_cursor_sphere(const Transform3d& trafo) const;
    // BBS
    void render_cursor_height_range(const Transform3d& trafo) const;
    // BBS: add logic to distinguish the first_time_update and later_update
    virtual void update_model_object()                       = 0;
    virtual void update_from_model_object(bool first_update) = 0;

    virtual ColorRGBA get_cursor_sphere_left_button_color() const { return {0.0f, 0.0f, 1.0f, 0.25f}; }
    virtual ColorRGBA get_cursor_sphere_right_button_color() const { return {1.0f, 0.0f, 0.0f, 0.25f}; }
    // BBS
    virtual ColorRGBA get_cursor_hover_color() const { return {0.f, 0.f, 0.f, 0.25f}; }

    virtual EnforcerBlockerType get_left_button_state_type() const { return EnforcerBlockerType::ENFORCER; }
    virtual EnforcerBlockerType get_right_button_state_type() const { return EnforcerBlockerType::BLOCKER; }

    // [STATE] Brush size state shared by toolbar controls and cursor drawing routines.
    float m_cursor_radius = 1.f;
    // BBS
    float                  m_cursor_height  = 0.2f;
    static constexpr float CursorRadiusMin  = 0.4f; // cannot be zero
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;
    static constexpr float CursorHeightMin  = 0.1f; // cannot be zero
    static constexpr float CursorHeightMax  = 8.f;
    static constexpr float CursorHeightStep = 0.2f;

    // For each model-part volume, store status and division of the triangles.
    // [STATE] Keep triangle selectors per mesh volume so painter overlays stay synced when the user switches parts.
    std::vector<std::unique_ptr<TriangleSelectorGUI>> m_triangle_selectors;

    // [STATE] Controls cursor geometry (sphere vs height range) and drives render_cursor_* helpers.
    TriangleSelector::CursorType m_cursor_type = TriangleSelector::SPHERE;

    // [STATE] Determines which painting mode is active so input/shortcut changes are shared with Unity toolbar bindings.
    enum class ToolType {
        BRUSH,
        BUCKET_FILL,
        SMART_FILL,
        // BBS
        GAP_FILL,
    };

    // [STATE] Stores the latest hit tests per mesh; used to reconcile raycast results with render and paint updates.
    struct ProjectedMousePosition
    {
        Vec3f  mesh_hit;
        int    mesh_idx;
        size_t facet_idx;
    };

    // BBS: projected result of mouse height range for a mesh
    // [STATE] Stores per-mesh height-range hits when painting vertical cursors; used by the height range cursor renderer.
    struct ProjectedHeightRange
    {
        float  z_world;
        int    mesh_idx;
        size_t first_facet_idx;
    };

    // [STATE] Enables splitting authoring triangles to keep brush strokes responsive.
    bool m_triangle_splitting_enabled = true;
    // [STATE] Active tool for the painter gizmo; Unity toolbar needs to mutate this enum via actions.
    ToolType m_tool_type = ToolType::BRUSH;
    // [STATE] Smart fill angle slider value persists here for interpolation with UI controls.
    float m_smart_fill_angle = 30.f;

    // [STATE] Overhang-only flag + angle threshold used to bias highlight coloration while painting.
    bool  m_paint_on_overhangs_only          = false;
    float m_highlight_by_angle_threshold_deg = 0.f;

    // [OPENGL] Cached circle mesh for cursor rendering; keep center/radius to detect when rebuilds are needed.
    GLModel                m_circle;
    Vec2d                  m_old_center{Vec2d::Zero()};
    float                  m_old_cursor_radius{0.0f};
    static constexpr float SmartFillAngleMin  = 0.0f;
    static constexpr float SmartFillAngleMax  = 90.f;
    static constexpr float SmartFillAngleStep = 1.f;

    // Orca: paint behavior enhancement
    // [STATE] Vertical/horizontal filters limit the cursor to specific normals; Unity port should expose them as toggleable buttons.
    bool m_vertical_only   = false;
    bool m_horizontal_only = false;

    // It stores the value of the previous mesh_id to which the seed fill was applied.
    // [STATE] Detects when the cursor has jumped to another volume so caches can reset.
    int m_seed_fill_last_mesh_id = -1;

    // [STATE] Remembers which mouse button is down to differentiate enforcer vs blocker behavior.
    enum class Button { None, Left, Right };

    // [STATE] Stores clip-plane data needed by triangle selectors; Unity needs the same plane info for mesh projection tests.
    struct ClippingPlaneDataWrapper
    {
        std::array<float, 4> clp_dataf;
        std::array<float, 2> z_range;
    };

    ClippingPlaneDataWrapper get_clipping_plane_data() const;

    TriangleSelector::ClippingPlane get_clipping_plane_in_volume_coordinates(const Transform3d& trafo) const;

private:
    std::vector<std::vector<ProjectedMousePosition>> get_projected_mouse_positions(const Vec2d&                    mouse_position,
                                                                                   double                          resolution,
                                                                                   const std::vector<Transform3d>& trafo_matrices) const;

    std::vector<ProjectedHeightRange> get_projected_height_range(const Vec2d&                           mouse_position,
                                                                 double                                 resolution,
                                                                 const std::vector<const ModelVolume*>& part_volumes,
                                                                 const std::vector<Transform3d>&        trafo_matrices) const;

    bool is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const;
    // [THREAD] Cache raycast queries performed during rendering so repeated hits reuse the same CPU work (Unity must do the same on the
    // main thread).
    void update_raycast_cache(const Vec2d& mouse_position, const Camera& camera, const std::vector<Transform3d>& trafo_matrices) const;

    // [OPENGL][PORTING_HAZARD:P2] Shared sphere GLModel ensures only one VAO/VBO pair exists; Unity must mimic this with a cached
    // Mesh/Material asset.
    static std::shared_ptr<GLModel> s_sphere;

    // [STATE] Tracks whether `painter` is within an update stack to avoid reentrant calls.
    bool m_internal_stack_active = false;
    // [STATE] When set, forces a refresh on the next render loop via the scheduler.
    bool m_schedule_update = false;
    // [STATE] Last recorded mouse click for drag detection (Unity InputSystem should replicate this state).
    Vec2d m_last_mouse_click = Vec2d::Zero();

    Button m_button_down = Button::None;
    EState m_old_state   = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    // Following cache holds result of a raycast query. The queries are asked
    // during rendering the sphere cursor and painting, this saves repeated
    // raycasts when the mouse position is the same as before. [THREAD] this cache lives on the render thread.
    struct RaycastResult
    {
        Vec2d  mouse_position;
        int    mesh_id;
        Vec3f  hit;
        size_t facet;
    };
    mutable RaycastResult m_rr = {Vec2d::Zero(), -1, Vec3f::Zero(), 0};

    // BBS
    // [STATE] Stores the cached contours + shift offsets to resume cutting operations without recomputing the mesh.
    struct CutContours
    {
        TriangleMesh mesh;
        GLModel      contours;
        double       cut_z{0.0};
        Vec3d        position{Vec3d::Zero()};
        Vec3d        shift{Vec3d::Zero()};
        ObjectID     object_id;
        int          instance_idx{-1};
    };
    // [STATE] Cached cut contours updated by paint operations; Unity needs to store these in a ScriptableObject or similar data container.
    mutable std::vector<CutContours> m_cut_contours;
    // [STATE] Raycast + ImGui interactions keep track of the last touched volume, cursor height, and GUI slider state.
    mutable int     m_volumes_index = 0;
    mutable float   m_cursor_z{0};
    mutable double  m_height_start_z_in_imgui{0};
    mutable bool    m_is_set_height_start_z_by_imgui{false};
    mutable Vec2i32 m_height_start_pos{0, 0};
    mutable bool    m_is_cursor_in_imgui{false};
    BoundingBoxf3   bounding_box() const;
    void            update_contours(int i, const TriangleMesh& vol_mesh, float cursor_z, float max_z, float min_z) const;

protected:
    // [EVENT] Called when GLGizmoBase changes state so painter can refresh cursor overlays and modifiers.
    void on_set_state() override;
    // [EVENT] Abstract hooks called when the gizmo is enabled/disabled so derived classes can init or tear down painters.
    virtual void             on_opening()             = 0;
    virtual void             on_shutdown()            = 0;
    virtual PainterGizmoType get_painter_type() const = 0;

    bool               on_is_activable() const override;
    bool               on_is_selectable() const override;
    void               on_load(cereal::BinaryInputArchive& ar) override;
    void               on_save(cereal::BinaryOutputArchive& ar) const override {}
    CommonGizmosDataID on_get_requirements() const override;
    bool               wants_enter_leave_snapshots() const override { return true; }

    // [EVENT] Snapshot button naming ensures Unity UI actions match wxWidgets shortcuts when capturing state.
    virtual wxString handle_snapshot_action_name(bool shift_down, Button button_down) const = 0;

    friend class ::Slic3r::GUI::GLGizmoMmuSegmentation;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoPainterBase_hpp_
