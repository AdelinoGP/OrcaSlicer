#ifndef slic3r_GLGizmoSVG_hpp_
#define slic3r_GLGizmoSVG_hpp_

// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code,
// which overrides our localization "L" macro.
#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "slic3r/GUI/IconManager.hpp"

#include <optional>
#include <memory>
#include <atomic>

#include "libslic3r/Emboss.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"

#include <imgui/imgui.h>
#include <GL/glew.h>

namespace Slic3r{
class ModelVolume;
enum class ModelVolumeType : int;
}

namespace Slic3r::GUI {

// [OPENGL] Captures the GLuint handle plus dimensions for the cached SVG preview so `draw_preview()` can rebind without reloading the file
// each frame. [STATE] Width/height metadata mirror the preview sliders so the UI can gate stale uploads before repainting.
struct Texture
{
    unsigned id{0};
    unsigned width{0};
    unsigned height{0};
};

// [INTENT] Hosts the SVG emboss UI, toolbar buttons, and rotation/translation grabbers so the emboss workflow stays centralized.
// [STATE] Owns the selection pointer, rotation gizmo, cancel token, and GUI config that keep SVG inputs and preview synchronized.
// [UNITY] Port this as a MonoBehaviour + UI Toolkit panel that routes GraphicRaycaster hits + MeshCollider updates through the same
// controller. [PORTING_HAZARD:P2] Depends on wxWidgets globals (L macro translations, IconManager atlas, `m_parent` selection) and
// `m_volume_id`, so Unity must snapshot the mesh/volume references before spinning up worker jobs.
class GLGizmoSVG : public GLGizmoBase
{
public:
    explicit GLGizmoSVG(GLCanvas3D & parent);

    /// <summary>
    /// Create new embossed text volume by type on position of mouse
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <param name="mouse_pos">Define position of new volume</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    // [EVENT] Triggered by toolbar buttons, drop targets, or menu shortcuts to bake an emboss volume from the current SVG selection.
    // [THREAD] Each overload funnels through `start_create_volume`, serializing the snapshot before spinning up a worker job.
    // [PORTING_HAZARD:P3] Emboss jobs assume the original selection stays valid, so Unity must snapshot `m_volume_id` and the
    // GraphicRaycaster hit before queuing.
    bool create_volume(ModelVolumeType volume_type, const Vec2d& mouse_pos); // first open file dialog

    /// <summary>
    /// Create new text without given position
    /// </summary>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    // [EVENT] Called when the emboss toolbar's general button fires without a specific cursor target.
    // [STATE] Reuses cached rotation/distance values so repeated invocations keep the previous orientation.
    // [UNITY] Map to the UI Toolkit button event that forwards the cached config to the MonoBehaviour controller.
    bool create_volume(ModelVolumeType volume_type); // first open file dialog

    /// <summary>
    /// Create volume from already selected svg file
    /// </summary>
    /// <param name="svg_file">File path</param>
    /// <param name="mouse_pos">Position on screen where to create volume</param>
    /// <param name="volume_type">Object part / Negative volume / Modifier</param>
    /// <returns>True on succesfull start creation otherwise False</returns>
    // [EVENT] Drag/drop/resolved icon double-clicks call these overloads after IconManager pushes a file path.
    // [THREAD] File loading and GL upload run in workers; the UI thread just caches `m_filename_preview` and geometry metadata.
    // [STATE] `m_shape_bb` and preview texture guards determine if `start_create_volume` can fire immediately.
    bool create_volume(std::string_view svg_file, const Vec2d& mouse_pos, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);
    bool create_volume(std::string_view svg_file, ModelVolumeType volume_type = ModelVolumeType::MODEL_PART);

    /// <summary>
    /// Check whether volume is object containing only emboss volume
    /// </summary>
    /// <param name="volume">Pointer to volume</param>
    /// <returns>True when object otherwise False</returns>
    // [STATE] Keeps the selection filters in sync so emboss-specific controls only show when SVG volumes are active.
    static bool is_svg_object(const ModelVolume& volume);

    /// <summary>
    /// Check whether volume has emboss data
    /// </summary>
    /// <param name="volume">Pointer to volume</param>
    /// <returns>True when constain emboss data otherwise False</returns>
    // [STATE] Signals when the volume already owns emboss geometry so the UI can highlight the pre-existing paths.
    static bool is_svg(const ModelVolume& volume);

protected:
    // [EVENT] These overrides drive the activation/render/input handshake with GLCanvas3D and the raycasting layer.
    bool        on_init() override;
    std::string on_get_name() const override;
    // [OPENGL] Paints the rotation handles and SVG preview overlay while guarding against stale selection/drag states.
    // [UNITY] Mirror with `Graphics.DrawMesh` plus UI Toolkit overlays chained through the same dirty flags.
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    bool on_is_activable() const override { return true; }
    bool on_is_selectable() const override { return false; }
    void on_set_state() override;
    void data_changed(bool is_serializing) override; // selection changed
    void on_set_hover_id() override { m_rotate_gizmo.set_hover_id(m_hover_id); }
    void on_enable_grabber(unsigned int id) override { m_rotate_gizmo.enable_grabber(); }
    void on_disable_grabber(unsigned int id) override { m_rotate_gizmo.disable_grabber(); }
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData& data) override;

    /// <summary>
    /// Rotate by text on dragging rotate grabers
    /// </summary>
    /// <param name="mouse_event">Information about mouse</param>
    /// <returns>Propagete normaly return false.</returns>
    // [EVENT] Converts wxMouseEvent drags into rotation/translation deltas for the rotate handles.
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    bool        wants_enter_leave_snapshots() const override;
    std::string get_gizmo_entering_text() const override;
    std::string get_gizmo_leaving_text() const override;
    std::string get_action_snapshot_name() const override;

private:
    // [INTENT] Keep the GUI aligned with the selected mesh volume so emboss updates hit the right object.
    void set_volume_by_selection();
    // [INTENT] Reset preview state, cancel jobs, and clear cached geometry when the overlay closes.
    void reset_volume();

    // create volume from text - main functionality
    // [INTENT] Drive the emboss preview/creation lifecycle while optionally capturing undo snapshots and posting the job.
    // [THREAD] Runs on the UI thread but keeps the worker-cancellation token ready for background execution.
    bool process(bool make_snapshot = true);
    // [EVENT] Closes the UI when the user cancels or when the job completes to avoid stale state.
    void close();
    // [OPENGL] Renders the floating panel frame and ImGui controls that accompany the SVG preview.
    void draw_window();
    // [OPENGL] Binds `m_texture` and draws the cached preview, mirroring Unity's RenderTexture + Mesh overlay approach.
    void draw_preview();
    // [STATE] Shows the filename so transfers can warn about missing assets.
    void draw_filename();
    // [STATE] Keeps the depth slider/label consistent with the running emboss distance.
    void draw_depth();
    // [STATE] Displays width/height controls used before job dispatch.
    void draw_size();
    // [EVENT] Toggles whether the emboss surface snaps to the mesh or floats freely.
    void draw_use_surface();
    // [STATE] Renders the spacing slider that governs emboss detail.
    void draw_distance();
    // [STATE] Shows rotation handles state via `m_rotate_gizmo`.
    void draw_rotation();
    // [STATE] Mirroring toggles apply symmetry before job creation.
    void draw_mirroring();
    // [STATE] Forces the text to face the camera when enabled.
    void draw_face_the_camera();
    // [STATE] Dropdown mapping to `ModelVolumeType` for part/modifier/negative volumes.
    void draw_model_type();

    // process mouse event
    // [EVENT] Differentiates rotation vs translation drags so the appropriate handler updates transform state.
    bool on_mouse_for_rotation(const wxMouseEvent& mouse_event);
    bool on_mouse_for_translate(const wxMouseEvent& mouse_event);

    // [EVENT] Refreshes cached bounding boxes and preview dirty flags whenever transforms change.
    void volume_transformation_changed();

    struct GuiCfg;
    // [STATE] Captures the GUI config snapshot so the panel does not re-read globals mid-operation.
    std::unique_ptr<const GuiCfg> m_gui_cfg;

    // actual selected only one volume - with emboss data
    // [STATE] Points to the mesh volume currently edited by the emboss UI.
    ModelVolume* m_volume = nullptr;

    // Is used to edit eboss and send changes to job
    // Inside volume is current state of shape WRT Volume
    // [STATE] Local copy of the emboss shape so undo/redo can revert without mutating the source mesh.
    EmbossShape m_volume_shape; // copy from m_volume for edit

    // same index as volumes in
    // [STATE] Stores warnings presented in the UI when the shape violates constraints.
    std::vector<std::string> m_shape_warnings;

    // When work with undo redo stack there could be situation that
    // m_volume point to unexisting volume so One need also objectID
    // [STATE] Tracks the object ID so undo/redo can rebind the right mesh after deletions.
    ObjectID m_volume_id;

    // cancel for previous update of volume to cancel finalize part
    // [THREAD] UI sliders flip this flag while the EmbossJob polls it for cancel/resume semantics.
    std::shared_ptr<std::atomic<bool>> m_job_cancel = nullptr;

    // Rotation gizmo
    // [STATE] Tracks rotation handles; `m_angle` and `m_distance` capture the last deltas for smooth preview updates.
    // [UNITY] Mirror this with a `RotateHandlesController` MonoBehaviour that feeds UI Toolkit events.
    GLGizmoRotate        m_rotate_gizmo;
    std::optional<float> m_angle;
    std::optional<float> m_distance;

    // Value is set only when dragging rotation to calculate actual angle
    // [STATE] Holds the starting angle so delta calculations stay stable during drags.
    std::optional<float> m_rotate_start_angle;

    // TODO: it should be accessible by other gizmo too.
    // May be move to plater?
    // [EVENT] Raycaster registrations mirror GraphicRaycaster + MeshCollider selection so Unity can reuse the same picks.
    RaycastManager m_raycast_manager;

    // When true keep up vector otherwise relative rotation
    // [STATE] Keeps the glyph up vector locked so swap sequences stay predictable.
    bool m_keep_up = true;

    // Keep size aspect ratio when True.
    // [STATE] Ensures width/height edits respect the toggle when enabled.
    bool m_keep_ratio = true;

    // Keep data about dragging only during drag&drop
    // [STATE] Captures drag offsets so the preview stays anchored during drop operations.
    std::optional<SurfaceDrag> m_surface_drag;

    // For volume on scaled objects
    std::optional<float> m_scale_width;
    std::optional<float> m_scale_height;
    std::optional<float> m_scale_depth;
    // [STATE] Captures scaled dimensions so tolerance checks respect the current stretch.
    void calculate_scale();
    // [INTENT] Computes a scale-aware tolerance so emboss depth remains stable on stretched meshes.
    float get_scale_for_tolerance();

    // keep SVG data rendered on GPU
    // [OPENGL] Unity can replace this with a Texture2D/RenderTexture pair refreshed when `m_filename_preview` or `m_shape_bb` changes.
    Texture m_texture;

    // bounding box of shape
    // Note: Scaled mm to int value by m_volume_shape.scale
    // [STATE] Drives the preview dirty flag so redraws only when the bounds change.
    BoundingBox m_shape_bb;

    std::string m_filename_preview;
    // [STATE] Tracks the active SVG path so the UI can display status and detect reloads.

    IconManager         m_icon_manager;
    IconManager::VIcons m_icons;
    // [UNITY] Replace the icon atlas with SpriteAtlas-based assets shared via Addressables.

    // only temporary solution
    static const std::string M_ICON_FILENAME;
};
} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoSVG_hpp_
