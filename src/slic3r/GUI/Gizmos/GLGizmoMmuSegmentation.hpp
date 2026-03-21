#ifndef slic3r_GLGizmoMmuSegmentation_hpp_
#define slic3r_GLGizmoMmuSegmentation_hpp_

#include "GLGizmoPainterBase.hpp"

namespace Slic3r::GUI {

// [INTENT] Encapsulates the tessellated mesh that the MMU segmentation painter reuses for rendering and hit-testing chunks.
// [PORTING_HAZARD:P2] Unity will need a similar mesh cache (Mesh/ComputeBuffer pair) because wx/OpenGL VBO lifetimes are manual here.
class GLMmSegmentationGizmo3DScene
{
public:
    GLMmSegmentationGizmo3DScene() = delete;

    explicit GLMmSegmentationGizmo3DScene(size_t triangle_indices_buffers_count) {}

    virtual ~GLMmSegmentationGizmo3DScene() { release_geometry(); }

    // [STATE] Answers whether the cached triangle indices have been uploaded to the GPU yet.
    [[nodiscard]] inline bool has_VBOs(size_t triangle_indices_idx) const
    {
        assert(triangle_indices_idx < this->triangle_patches.size());
        return this->triangle_indices_VBO_ids[triangle_indices_idx] != 0;
    }

    // [OPENGL][THREAD] Must release GPU objects on the GL context thread before the supporting gizmo shuts down.
    // Release the geometry data, release OpenGL VBOs.
    void release_geometry();
    // Finalize the initialization of the geometry, upload the geometry to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_vertices();
    // Finalize the initialization of the indices, upload the indices to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_triangle_indices();

    // [STATE] Drops all CPU-side caches so the next finalize_* call rebuilds them from scratch.
    void clear()
    {
        this->vertices.clear();
        // BBS
        this->triangle_indices_VBO_ids.clear();
        this->triangle_indices_sizes.clear();

        for (TrianglePatch& patch : this->triangle_patches)
            patch.triangle_indices.clear();
        this->triangle_patches.clear();
    }

    // [OPENGL][THREAD] Called from the main GL render loop to bind the selected VBO and draw the patches.
    // [UNITY] Map this to a MeshRenderer + Graphics.DrawMesh call tied to the RenderTexture camera that draws the gizmo overlay.
    void render(size_t triangle_indices_idx) const;

    // [STATE] Linear vertex buffer emitted at finalize_vertices and cleared once uploaded.
    std::vector<float> vertices;
    // std::vector<std::vector<int>> triangle_indices;

    // BBS
    // [STATE] Triangle patches keep CPU-side indices for repeated remapping before GPU upload.
    std::vector<TrianglePatch> triangle_patches;

    // When the triangle indices are loaded into the graphics card as Vertex Buffer Objects,
    // the above mentioned std::vectors are cleared and the following variables keep their original length.
    // [STATE][PORTING_HAZARD:P2] Unity's Mesh/IndexBuffer must mirror the size vector to keep per-buffer ranges consistent.
    std::vector<size_t> triangle_indices_sizes;

    // IDs of the Vertex Array Objects, into which the geometry has been loaded.
    // Zero if the VBOs are not sent to GPU yet.
    unsigned int              vertices_VAO_id{0};
    unsigned int              vertices_VBO_id{0};
    std::vector<unsigned int> triangle_indices_VBO_ids;
};

// [INTENT] Specialized painter gizmo that overlays MMU segmentation controls on the GLCanvas and routes key input to the TriangleSelector.
// [UNITY] Target Unity replacement should be a MonoBehaviour that owns a MeshCollider + Custom Editor input bridge for segmented faces.
class GLGizmoMmuSegmentation : public GLGizmoPainterBase
{
public:
    GLGizmoMmuSegmentation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoMmuSegmentation() override = default;

    // [OPENGL][THREAD] Called during the GLCanvas draw pass to update segmentation overlays before ImGui draws frameless controls.
    void render_painter_gizmo() override;

    // [EVENT] Invoked when project data changes or serialization runs; this is where cached triangle selectors are refreshed.
    void data_changed(bool is_serializing) override;

    // TriangleSelector::serialization/deserialization has a limit to store 19 different states.
    // EXTRUDER_LIMIT + 1 states are used to storing the painting because also uncolored triangles are stored.
    // When increasing EXTRUDER_LIMIT, it needs to ensure that TriangleSelector::serialization/deserialization
    // will be also extended to support additional states, requiring at least one state to remain free out of 19 states.
    // [STATE][PORTING_HAZARD:P2] Serialization limits require a reserved slot, so Unity must respect the 19-state cap when replicating
    // TriangleSelector storage.
    static const constexpr size_t EXTRUDERS_LIMIT = 16;

    const float get_cursor_radius_min() const override { return CursorRadiusMin; }

    // BBS
    // [EVENT] Toolbar number keys swap the painting tool / extruder selection without needing a cursor tap.
    bool on_number_key_down(int number);
    bool on_key_down_select_tool_type(int keyCode);

protected:
    // BBS
    // [STATE] Hover color depends on the active extruder to keep visual feedback synchronized with TriangleSelector.
    ColorRGBA get_cursor_hover_color() const override;
    // [STATE][EVENT] Called when the painter enters/leaves to cache brush radii and update command states.
    void on_set_state() override;

    EnforcerBlockerType get_left_button_state_type() const override { return EnforcerBlockerType(m_selected_extruder_idx + 1); }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType(-1); }

    // [EVENT][UNITY] Draws the IMGUI input window anchored to the gizmo; Unity should swap this for a UI Toolkit Popup with GraphicRaycaster.
    void        on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;
    void        show_tooltip_information(float caption_max, float x, float y);
    bool        on_is_selectable() const override;
    bool        on_is_activable() const override;

    // [EVENT] Returns the localized label that appears in the macro snapshot action list when persistence is toggled.
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    // [INTENT] Strings shown to the user when the painter becomes active.
    std::string get_gizmo_entering_text() const override { return "Entering color painting"; }
    std::string get_gizmo_leaving_text() const override { return "Leaving color painting"; }
    std::string get_action_snapshot_name() const override { return "Color painting editing"; }

    // BBS
    // [STATE] Tracks which extruder the user is painting so the triangle selectors know which color index to write.
    size_t m_selected_extruder_idx = 0;
    // [STATE] Cached colors allow the painter to keep consistent tooltip colors even when settings change.
    std::vector<ColorRGBA> m_extruders_colors;
    // [STATE] Mesh volumes mapped to extruder indexes for shader updates.
    std::vector<int> m_volumes_extruder_idxs;

    // BBS
    // [STATE] Current tool oscillator (brush, bucket, edge) with explicit Unicode key binding mapping.
    wchar_t m_current_tool = 0;
    // [STATE] Toggle that affects edge snapping while painting, ensuring brush handles abide by camera occlusion.
    bool m_detect_geometry_edge = true;

    // Filament remap feature
    // [STATE][EVENT] When true, the floating remap UI is rendered; toggled from toolbar buttons.
    bool m_show_remap_panel = false;
    // [STATE][PORTING_HAZARD:P3] Remapping tracks a many-to-one map that Unity must preserve when translating filament indexes to color sets.
    std::vector<size_t> m_extruder_remap; // index → target extruder index
    // ORCA: Cache used filaments to filter UI
    // [STATE] Keeps track of filaments referenced by the current project to avoid showing bogus options.
    std::set<size_t> m_used_filaments; // Set of used filament indices (cached)

    static const constexpr float CursorRadiusMin = 0.1f; // cannot be zero

private:
    // [THREAD] Runs on the main thread as part of gizmo construction; load models or set up GL context here.
    bool on_init() override;

    // BBS. remove const.
    // [EVENT] Syncs the painter state with the GLModel instance when jobs or serialization change the mesh.
    void update_model_object() override;
    // BBS: add logic to distinguish the first_time_update and later_update
    //  [EVENT][STATE] Populates cached selectors the first time and toggles dirty flags on subsequent calls.
    void update_from_model_object(bool first_update = false) override;
    // [EVENT] Transitions internal input state when wxWidgets reports a different bachelor/brush binding.
    void tool_changed(wchar_t old_tool, wchar_t new_tool);

    // [THREAD][EVENT] Ensures GL resources are rebuilt when the gizmo becomes visible for painting.
    void on_opening() override;
    // [THREAD][EVENT] Tears down GL resources so Unity's Destroy path can mimic resource release.
    void on_shutdown() override;
    // [STATE] Identifies this painter type to the manager so it can route input to the MMU toolset.
    PainterGizmoType get_painter_type() const override;

    // [STATE] Creates TriangleSelector instances that own geometry + remap caches before painting begins.
    void init_model_triangle_selectors();

    // BBS
    // [STATE] Keeps glyph selector colors in sync when extruder palettes change.
    void update_triangle_selectors_colors();
    // [STATE] Prepares color/selection caches so render passes read consistent per-extruder data.
    void init_extruders_data();

    // Filament remapping methods
    // [EVENT] Applies remap data to TriangleSelector objects before each paint pass.
    void remap_filament_assignments();
    // [EVENT][UNITY] Renders the remap dialog inside ImGui; Unity should replace this with UI Toolkit windows anchored to the gizmo.
    void render_filament_remap_ui(float window_width, float max_tooltip_width);
    // ORCA: Helper to update the cache of used filaments
    // [STATE] Rebuilds the cached filament set after slicing or filament configuration changes.
    void update_used_filaments();

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // [STATE][UNITY] Mirrors a localization dictionary; Unity should back this with a ScriptableObject lookup that refreshes on locale change.
    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoMmuSegmentation_hpp_
