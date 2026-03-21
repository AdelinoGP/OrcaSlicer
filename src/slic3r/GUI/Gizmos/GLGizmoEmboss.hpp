#ifndef slic3r_GLGizmoEmboss_hpp_
#define slic3r_GLGizmoEmboss_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/IconManager.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "slic3r/GUI/I18N.hpp" // TODO: not needed
#include "slic3r/GUI/TextLines.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"

#include <optional>
#include <memory>
#include <atomic>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TextConfiguration.hpp"

#include <imgui/imgui.h>
#include <GL/glew.h>

class wxFont;
namespace Slic3r {
class AppConfig;
class GLVolume;
enum class ModelVolumeType : int;
} // namespace Slic3r

namespace Slic3r::GUI {
class GLGizmoEmboss : public GLGizmoBase
{
public:
    // [INTENT] Encapsulate the emboss text creation workflow (style picker, font cache, placement helpers) on top of the GL canvas.
    // [STATE] Owns the toolbox state (selected style, advanced options flag, selected volume pointer, text buffers) so the UI can rehydrate
    // after a slice change. [UNITY] Translate to a MonoBehaviour that binds UI Toolkit controls to 3D surface placement via Canvas +
    // GraphicRaycaster + command queue for Mesh generation. [PORTING_HAZARD:P2] Heavy coupling to wxWidgets events, GLGizmoBase lifecycle,
    // and direct ModelVolume pointers means Unity must mediate lifecycle with managed GameObject references.
    explicit GLGizmoEmboss(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    /// <summary>
    /// Create new embossed text volume by type on position of mouse
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <param name="mouse_pos">Define position of new volume</param>
    // [EVENT] Hooked to toolbar/hotkey dispatch so Unity should surface this via a button command that raycasts to the surface before
    // invoking the controller. [UNITY] Expect this to map to a UI Toolkit button that calls a MonoBehaviour method which performs
    // SurfaceDrag-style world localization before scheduling the new emboss job.
    bool create_volume(ModelVolumeType volume_type, const Vec2d& mouse_pos);

    /// <summary>
    /// Create new text without given position
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    // [EVENT] Invoked when the user wants the default Z origin creation (toolbar shortcut or drop-down) without explicit cursor location.
    bool create_volume(ModelVolumeType volume_type);

    /// <summary>
    /// Handle pressing of shortcut
    /// </summary>
    // [EVENT] Hotkey stream from GUI_App should pass through this so Unity can hook InputSystem actions to the emboss workflow.
    void on_shortcut_key();

    /// <summary>
    /// Mirroring from object manipulation panel
    /// !! Emboss gizmo must be active
    /// </summary>
    /// <param name="axis">Axis for mirroring must be one of {0,1,2}</param>
    /// <returns>True on success start job otherwise False</returns>
    // [STATE] Uses the axis parameter to sync with the object manipulation panel state, so Unity needs to keep shared intent between the
    // two UI pieces. [UNITY] Mirror buttons can reuse the MonoBehaviour command that wraps ModelVolume mirror operations.
    bool do_mirror(size_t axis);

    /// <summary>
    /// Call on change inside of object conatining projected volume
    /// </summary>
    /// <param name="job_cancel">Way to stop re_emboss job</param>
    /// <returns>True on success otherwise False</returns>
    // [THREAD] Runs off the main thread to rebuild text meshes but uses std::atomic to allow job cancellation between rapid edits.
    // [PORTING_HAZARD:P2] Unity must not let the background worker touch ModelVolume pointers without dispatching back to the main loop.
    static bool re_emboss(const ModelVolume& text, std::shared_ptr<std::atomic<bool>> job_cancel = nullptr);

protected:
    // [INTENT] Wire up the emboss UI state after GLGizmoBase init (style manager, font textures, RaycastManager) so the Unity controller
    // can mimic the lifecycle.
    bool on_init() override;
    // [STATE] Name used for selection/deselection UI text so Unity can reuse the same label when the gizmo becomes active.
    std::string on_get_name() const override;
    // [OPENGL] Draw the 3D handles and overlay icons; Unity will map this to Graphics.DrawMesh calls in the Gizmo MonoBehaviour.
    void on_render() override;
    // [EVENT] Register the RaycastManager so mouse hits can select the text handles; Unity must hook this to Physics.Raycast + EventSystem.
    void on_register_raycasters_for_picking() override;
    // [EVENT] Unregister the pickable mesh when the gizmo is hidden to avoid stale hits.
    void on_unregister_raycasters_for_picking() override;
    // [OPENGL] Render the ImGui window on top of the viewport; Unity should drive an equivalent overlay via UI Toolkit overlay canvas.
    void on_render_input_window(float x, float y, float bottom_limit) override;
    // [STATE] Syncs the internal style/text caches when the gizmo becomes the active tool.
    void on_set_state() override;
    // [EVENT] Respond to selection or serialization events so Unity can reload textbox state before editing.
    void data_changed(bool is_serializing) override; // selection changed
    void on_set_hover_id() override { m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    // [EVENT] Mouse drag lifecycle triggered between pointer down/up; Unity should map to OnBeginDrag/OnEndDrag events that re-route into the tool.
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData& data) override;
    // [OPENGL] Temporarily tweak ImGui button palettes so Unity can replicate the pressed/hover states on its toolbar buttons.
    void push_button_style(bool pressed);
    void pop_button_style();

    /// <summary>
    /// Rotate by text on dragging rotate grabers
    /// </summary>
    /// <param name="mouse_event">Information about mouse</param>
    /// <returns>Propagete normaly return false.</returns>
    // [EVENT] Mouse event handled by wx so Unity should receive pointer events that emulate the same rotation drag.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    // [STATE] Signals whether the gizmo wants undo/redo snapshots when the mouse enters or leaves so Unity can mirror the undo stack hooks.
    bool wants_enter_leave_snapshots() const override;
    // [INTENT] Provide localized tooltips when the gizmo becomes active/inactive.
    std::string get_gizmo_entering_text() const override;
    std::string get_gizmo_leaving_text() const override;
    // [EVENT] Snapshot name used for the undo history; Unity should push the same string when executing the emboss command.
    std::string get_action_snapshot_name() const override;

private:
    void volume_transformation_changing();
    // [STATE] Notifies the UI before transform updates so Unity can cache the last matrix before coordinate changes.
    void volume_transformation_changed();

    static EmbossStyles create_default_styles();
    // localized default text
    bool init_create(ModelVolumeType volume_type);

    void set_volume_by_selection();
    void reset_volume();

    // create volume from text - main functionality
    // [INTENT] Core text processing path that builds meshes, updates undo state, and triggers GL updates; Unity should mirror this with a
    // coroutine/async job.
    bool process(bool make_snapshot = true);
    // [EVENT] Cleans up the emboss state when the user cancels or closes the window (used by toolbar to return to idle state).
    void close();
    void draw_window();
    void draw_text_input();
    void draw_model_type();
    void draw_style_list();
    void draw_delete_style_button();
    void draw_style_rename_popup();
    void draw_style_rename_button();
    void draw_style_save_button(bool is_modified);
    void draw_style_save_as_popup();
    void draw_style_add_button();
    // [OPENGL] Uploads font glyph texture once so Unity can maintain a Texture2D and reuse it in UI rendering.
    void init_font_name_texture();
    void draw_font_list_line();
    void draw_font_list();
    void draw_height(bool use_inch);
    void draw_depth(bool use_inch);

    // call after set m_style_manager.get_style().prop.size_in_mm
    bool set_height();

    bool draw_italic_button();
    bool draw_bold_button();
    void draw_advanced();

    bool select_facename(const wxString& facename);

    template<typename T>
    bool rev_input_mm(const std::string&          name,
                      T&                          value,
                      const T*                    default_value,
                      const std::string&          undo_tooltip,
                      T                           step,
                      T                           step_fast,
                      const char*                 format,
                      bool                        use_inch,
                      const std::optional<float>& scale) const;

    /// <summary>
    /// Reversible input float with option to restor default value
    /// TODO: make more general, static and move to ImGuiWrapper
    /// </summary>
    /// <returns>True when value changed otherwise FALSE.</returns>
    template<typename T>
    bool rev_input(const std::string&  name,
                   T&                  value,
                   const T*            default_value,
                   const std::string&  undo_tooltip,
                   T                   step,
                   T                   step_fast,
                   const char*         format,
                   ImGuiInputTextFlags flags = 0) const;
    bool rev_checkbox(const std::string& name, bool& value, const bool* default_value, const std::string& undo_tooltip) const;
    bool rev_slider(const std::string&        name,
                    std::optional<int>&       value,
                    const std::optional<int>* default_value,
                    const std::string&        undo_tooltip,
                    int                       v_min,
                    int                       v_max,
                    const std::string&        format,
                    const wxString&           tooltip) const;
    bool rev_slider(const std::string&          name,
                    std::optional<float>&       value,
                    const std::optional<float>* default_value,
                    const std::string&          undo_tooltip,
                    float                       v_min,
                    float                       v_max,
                    const std::string&          format,
                    const wxString&             tooltip) const;
    bool rev_slider(const std::string& name,
                    float&             value,
                    const float*       default_value,
                    const std::string& undo_tooltip,
                    float              v_min,
                    float              v_max,
                    const std::string& format,
                    const wxString&    tooltip) const;
    template<typename T, typename Draw>
    bool revertible(
        const std::string& name, T& value, const T* default_value, const std::string& undo_tooltip, float undo_offset, Draw draw) const;

    // process mouse event
    bool on_mouse_for_rotation(const wxMouseEvent& mouse_event);
    bool on_mouse_for_translate(const wxMouseEvent& mouse_event);
    void on_mouse_change_selection(const wxMouseEvent& mouse_event);

    // When open text loaded from .3mf it could be written with unknown font
    // [STATE] Triggers the invalid font notification so Unity can replicate the alert overlay.
    bool m_is_unknown_font = false;
    void create_notification_not_valid_font(const TextConfiguration& tc);
    void create_notification_not_valid_font(const std::string& text);
    void remove_notification_not_valid_font();

    struct GuiCfg;
    // [STATE] Holds configuration picked from the GUI tree; treat as immutable shared data so Unity can cache scriptable config descriptors.
    std::unique_ptr<const GuiCfg> m_gui_cfg;

    // Is open tree with advanced options
    bool m_is_advanced_edit_style = false;

    // Keep information about stored styles and loaded actual style to compare with
    // [STATE] Style manager caches style versions for diffing when the user toggles advanced edit mode.
    Emboss::StyleManager m_style_manager;

    // pImpl to hide implementation of FaceNames to .cpp file
    struct Facenames; // forward declaration
    std::unique_ptr<Facenames> m_face_names;

    // Text to emboss
    std::string m_text; // Sequence of Unicode UTF8 symbols
    // [STATE] Mirrors the text field contents so Unity can tie the TMP_InputField value to this buffer.

    // When true keep up vector otherwise relative rotation
    bool m_keep_up = true;

    // current selected volume
    // NOTE: Be carefull could be uninitialized (removed from Model)
    // [PORTING_HAZARD:P3] ModelVolume pointer can become dangling when the Model removes the object; Unity must guard accesses with managed refs.
    ModelVolume* m_volume = nullptr;

    // When work with undo redo stack there could be situation that
    // m_volume point to unexisting volume so One need also objectID
    // [STATE] ObjectID acts as a fallback to rediscover the ModelVolume in Unity when the pointer is invalidated.
    ObjectID m_volume_id;

    // True when m_text contain character unknown by selected font
    bool m_text_contain_unknown_glyph = false;

    // cancel for previous update of volume to cancel finalize part
    // [THREAD] Cancellation token shared between successive emboss jobs prevents racing background updates.
    std::shared_ptr<std::atomic<bool>> m_job_cancel = nullptr;

    // Keep information about curvature of text line around surface
    TextLinesModel m_text_lines;
    void           reinit_text_lines(unsigned count_lines = 0);

    // Rotation gizmo
    // [OPENGL] Delegates to a nested rotate gizmo so Unity should reuse its MonoBehaviour rotation helper for consistency.
    GLGizmoRotate m_rotate_gizmo;
    // Value is set only when dragging rotation to calculate actual angle
    std::optional<float> m_rotate_start_angle;

    // Keep data about dragging only during drag&drop
    // [STATE] SurfaceDrag caches drag deltas; Unity will need an equivalent structure for two-phase drag and drop.
    std::optional<SurfaceDrag> m_surface_drag;

    // Keep old scene triangle data in AABB trees,
    // all the time it need actualize before use.
    // [UNITY] Wrap this in a Physics.Raycast cache component to avoid repeating tetrahedral queries.
    RaycastManager m_raycast_manager;

    // For text on scaled objects
    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;
    void                 calculate_scale();

    // drawing icons
    // [STATE] Stores icon atlas handles so Unity can mirror the Texture2D resources in a ScriptableObject atlas.
    IconManager         m_icon_manager;
    IconManager::VIcons m_icons;
    void                init_icons();

    // only temporary solution
    static const std::string M_ICON_FILENAME;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoEmboss_hpp_
