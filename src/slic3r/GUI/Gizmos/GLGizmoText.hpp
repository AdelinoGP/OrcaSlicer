#ifndef slic3r_GLGizmoText_hpp_
#define slic3r_GLGizmoText_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "../GLTexture.hpp"
#include "../Camera.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

enum class ModelVolumeType : int;
class ModelVolume;

namespace GUI {

enum class SLAGizmoEventType : unsigned char;
class GLGizmoText : public GLGizmoBase
{
private:
    // [INTENT] Manage the text stamp workflow: toolbar controls, GL-backed preview, and surface/volume placement state.
    std::vector<std::string> m_avail_font_names;
    // [STATE] Ring buffer of the characters the user is typing before committing to a mesh.
    char m_text[1024] = {0};
    // [STATE][UNITY] Font selection mirrors a Unity TMP FontAsset choice stored as a string reference.
    std::string m_font_name;
    // [STATE] Numeric style knobs that drive both the preview texture and final mesh tessellation.
    float                 m_font_size       = 16.f;
    int                   m_curr_font_idx   = 0;
    bool                  m_bold            = true;
    bool                  m_italic          = false;
    float                 m_thickness       = 2.f;
    float                 m_embeded_depth   = 0.f;
    float                 m_rotate_angle    = 0;
    float                 m_text_gap        = 0.f;
    bool                  m_is_surface_text = false;
    bool                  m_keep_horizontal = false;
    mutable RaycastResult m_rr;

    float m_combo_height = 0.0f;
    float m_combo_width  = 0.0f;
    float m_scale;

    // [STATE] Mouse/keyboard drive the current drag session and need to sync with the main thread (wxWidgets event loop).
    Vec2d m_mouse_position        = Vec2d::Zero();
    Vec2d m_origin_mouse_position = Vec2d::Zero();
    bool  m_shift_down            = false;

    class TextureInfo
    {
    public:
        GLTexture* texture{nullptr};
        int        h;
        int        w;
        int        hl;

        std::string font_name;
    };

    // [OPENGL][STATE] Cache textures for every glyph so the renderer can reuse them instead of rebuilding on each frame.
    std::vector<TextureInfo> m_textures;

    // [STATE] Mirror of the wx-font list used for dropdown sync. Unity would keep these in a ScriptableObject-backed catalog.
    std::vector<std::string> m_font_names;

    bool m_is_modify        = false;
    bool m_need_update_text = false;

    int m_object_idx = -1;
    int m_volume_idx = -1;

    int m_preview_text_volume_id = -1;

    // [STATE] Cached raycast origin and normal are used by clipping and preview placement decisions.
    Vec3d m_mouse_position_world = Vec3d::Zero();
    Vec3d m_mouse_normal_world   = Vec3d::Zero();

    Vec3d m_cut_plane_dir = Vec3d::UnitZ();

    std::vector<Vec3d> m_position_points;
    std::vector<Vec3d> m_normal_points;

    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    // [PORTING_HAZARD:P3] Unity will need a separate localization cache; wxString ties this map to wxWidgets.
    std::map<std::string, wxString> m_desc;

public:
    GLGizmoText(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoText();

    // [OPENGL] Rebuild the atlas textures shared by text previews, widget buttons, and final extrusion meshes.
    void update_font_texture();

    // [EVENT] Toolbar/shortcut events mutate the text, font, and placement modes before the render loop reacts.
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    // [EVENT][THREAD] Mouse events trigger drag state changes on the UI thread before the GL renderer reads them.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    bool          is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const;
    BoundingBoxf3 bounding_box() const;

protected:
    virtual bool               on_init() override;
    virtual std::string        on_get_name() const override;
    virtual bool               on_is_activable() const override;
    virtual void               on_render() override;
    virtual void               on_dragging(const UpdateData& data) override;
    void                       push_combo_style(const float scale);
    void                       pop_combo_style();
    void                       push_button_style(bool pressed);
    void                       pop_button_style();
    virtual void               on_set_state() override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void               on_render_input_window(float x, float y, float bottom_limit);
    virtual void               on_register_raycasters_for_picking() override;
    virtual void               on_unregister_raycasters_for_picking() override;

    // [UNITY] Tooltip rendering parallels a UI Toolkit `Tooltip` VisualElement pinned to the InteractiveUnity `SceneView` overlay.
    void show_tooltip_information(float x, float y);

private:
    // [STATE] Query the current rig selection so the text gizmo knows where to anchor the mesh.
    ModelVolume* get_selected_single_volume(int& out_object_idx, int& out_volume_idx) const;
    void         reset_text_info();
    bool         update_text_positions(const std::vector<std::string>& texts);
    // [OPENGL][UNITY] Build the final Text mesh as a TriangleMesh so Unity can map it to a MeshFilter + MeshCollider pair.
    TriangleMesh get_text_mesh(const char* text_str, const Vec3d& position, const Vec3d& normal, const Vec3d& text_up_dir);

    // [THREAD] Raycast cache is refreshed on the UI thread for the renderer to reuse without re-casting every frame.
    bool update_raycast_cache(const Vec2d& mouse_position, const Camera& camera, const std::vector<Transform3d>& trafo_matrices);
    // [OPENGL][STATE] Temp volumes feed the preview layer before the user commits; deleting them keeps the shared scene clean.
    void generate_text_volume(bool is_temp = true);
    void delete_temp_preview_text_volume();

    TextInfo get_text_info();
    void     load_from_text_info(const TextInfo& text_info);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoText_hpp_
