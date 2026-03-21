#include "GLGizmoScale.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <GL/glew.h>

#include <wx/utils.h>

namespace Slic3r { namespace GUI {

// [INTENT] Overlay a scale gizmo on the active selection so users can stretch model axes with mouse/keyboard input.
// [STATE] Tracks `m_scale`, `m_offset`, grabber hover/drag flags, and the cached selection bounding box inherited from `GLGizmoBase`.
// [UNITY] Equivalent to a dedicated `MonoBehaviour` that renders axis handles with a late-stage overlay camera and dispatches
// `Physics.Raycast` hits via `GraphicRaycaster`. [PORTING_HAZARD:P2] Relies on wxWidgets mouse events + immediate-mode GL state, so Unity
// must rewire input tokens and layer the new gizmo on a render-order-controlled overlay.

const float GLGizmoScale3D::Offset = 5.0f;

// [INTENT] Translate mouse rays into plane intersections so drag deltas remain consistent across the gizmo surface.
// [OPENGL] Runs in camera space before feeding geometry uniforms, keeping input in sync with the overlay render pass.
Vec3d GetIntersectionOfRayAndPlane(Vec3d ray_position, Vec3d ray_dir, Vec3d plane_position, Vec3d plane_normal)
{
    double t            = (plane_normal.dot(plane_position) - plane_normal.dot(ray_position)) / (plane_normal.dot(ray_dir));
    Vec3d  intersection = ray_position + t * ray_dir;
    return intersection;
}

// [INTENT] Wire the scale gizmo into the parent canvas, sprite atlas, and optional object manipulation controller so user actions propagate
// to the selection. [STATE] Initializes scale, offset, snap step, and grabber links that determine which axes move together during uniform
// drags. [EVENT] Registers grabber hit sets for axis-based constraints and sets the `Control+S` shortcut via wxWidgets input tables.
// [THREAD] Always invoked on the UI/main loop when gizmo objects load or the camera switches, so all state touches happen sequentially.
// [UNITY] The Unity port should instantiate a `MonoBehaviour` tied to a `GraphicRaycaster` event loop, mapping axes to child colliders and
// storing `snapStep` on a `ScriptableObject`. [PORTING_HAZARD:P2] wxWidgets shortcut binding and `GizmoObjectManipulation` integration have
// no direct analog, requiring a new Unity input layer and shared manipulation cache. BBS: GUI refactor: add obj manipulation
GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D&              parent,
                               const std::string&       icon_filename,
                               unsigned int             sprite_id,
                               GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_offset(Vec3d::Zero())
    , m_snap_step(0.05)
    // BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    m_grabber_connections[0].grabber_indices = {0, 1};
    m_grabber_connections[1].grabber_indices = {2, 3};
    m_grabber_connections[2].grabber_indices = {4, 5};
    m_grabber_connections[3].grabber_indices = {6, 7};
    m_grabber_connections[4].grabber_indices = {7, 8};
    m_grabber_connections[5].grabber_indices = {8, 9};
    m_grabber_connections[6].grabber_indices = {9, 6};
}
// [STATE] Normalizes the cached gizmo scale with the current selection and clamps ratios through `GizmoObjectManipulation` before
// returning. [THREAD] Prefers the main UI thread because it reads/writes shared selection/cache structures.
const Vec3d& GLGizmoScale3D::get_scale()
{
    if (m_object_manipulation) {
        Vec3d cache_scale = m_object_manipulation->get_cache().scale.cwiseQuotient(Vec3d(100, 100, 100));
        Vec3d temp_scale  = cache_scale.cwiseProduct(m_scale);
        m_object_manipulation->limit_scaling_ratio(temp_scale);
        m_scale = temp_scale.cwiseQuotient(cache_scale);
    }
    return m_scale;
}

// [STATE] Reads selection metrics plus hover/drag flags to show axis-specific scale percentages.
// [EVENT] Invoked by the UI overlay when the mouse hovers over grabbers so the tooltip matches the active axis.
// [UNITY] The Unity port should mirror this logic in a tooltip `VisualElement` bound to `OnPointerEnter`/`OnPointerExit` events.
std::string GLGizmoScale3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();

    bool single_instance = selection.is_single_full_instance();
    bool single_volume   = selection.is_single_volume_or_modifier();

    Vec3f scale = 100.0f * Vec3f::Ones();
    if (single_instance)
        scale = 100.0f * selection.get_first_volume()->get_instance_scaling_factor().cast<float>();
    else if (single_volume)
        scale = 100.0f * selection.get_first_volume()->get_volume_scaling_factor().cast<float>();

    if (m_hover_id == 0 || m_hover_id == 1 || m_grabbers[0].dragging || m_grabbers[1].dragging)
        return "X: " + format(scale.x(), 4) + "%";
    else if (m_hover_id == 2 || m_hover_id == 3 || m_grabbers[2].dragging || m_grabbers[3].dragging)
        return "Y: " + format(scale.y(), 4) + "%";
    else if (m_hover_id == 4 || m_hover_id == 5 || m_grabbers[4].dragging || m_grabbers[5].dragging)
        return "Z: " + format(scale.z(), 4) + "%";
    else if (m_hover_id == 6 || m_hover_id == 7 || m_hover_id == 8 || m_hover_id == 9 || m_grabbers[6].dragging || m_grabbers[7].dragging ||
             m_grabbers[8].dragging || m_grabbers[9].dragging) {
        std::string tooltip = "X: " + format(scale.x(), 2) + "%\n";
        tooltip += "Y: " + format(scale.y(), 2) + "%\n";
        tooltip += "Z: " + format(scale.z(), 2) + "%";
        return tooltip;
    } else
        return "";
}

// [EVENT] Mouse drags deflect into selection scaling by calling `Selection::scale_and_translate` when a grabber is active.
// [STATE] Uses `m_dragging`, `m_hover_id`, and `m_scale/m_offset` to compute the scaled transform on-the-fly.
// [THREAD] Must stay on the wxWidgets UI thread because it mutates selection data shared with the renderer.
// [UNITY] Replace with a Unity `MonoBehaviour` handling `IPointerDownHandler`/`IDragHandler` events and applying scale deltas via
// `Transform` updates.
bool GLGizmoScale3D::on_mouse(const wxMouseEvent& mouse_event)
{
    if (mouse_event.Dragging()) {
        if (m_dragging) {
            // Apply new temporary scale factors
            TransformationType transformation_type;
            if (wxGetApp().obj_manipul()->is_local_coordinates())
                transformation_type.set_local();
            else if (wxGetApp().obj_manipul()->is_instance_coordinates())
                transformation_type.set_instance();

            transformation_type.set_relative();

            if (mouse_event.AltDown())
                transformation_type.set_independent();

            Selection& selection = m_parent.get_selection();
            selection.scale_and_translate(get_scale(), get_offset(), transformation_type);
        }
    }
    return use_grabbers(mouse_event);
}

// [EVENT] Triggered when the underlying selection changes so the gizmo can toggle visibility and reset offsets.
// [STATE] Recomputes grabber enablement, resets `m_scale` to identity, and refreshes the coordinate system tracker.
// [UNITY] Unity should call the analogous handler from its selection-change event stream before enabling handle colliders.
void GLGizmoScale3D::data_changed(bool is_serializing)
{
    const Selection& selection        = m_parent.get_selection();
    bool             enable_scale_xyz = selection.is_single_full_instance() || selection.is_single_volume_or_modifier();
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable_scale_xyz;

    set_scale(Vec3d::Ones());

    change_cs_by_selection();
}

// [STATE] Exposes a master toggle for axis grabbers when non-uniform scaling must remain disabled.
void GLGizmoScale3D::enable_ununiversal_scale(bool enable)
{
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable;
}

// [INTENT] Prepares grabbers, shortcut keys, and uniform controls when the gizmo registers with the canvas.
// [STATE] Seeds `m_grabbers` plus the Control+S shortcut so uniform scaling can be toggled by the user.
// [UNITY] Unity should build a similar initialization step when the gizmo GameObject activates, constructing colliders and binding to
// `InputAction` assets.
bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i) {
        m_grabbers.push_back(Grabber());
    }

    double half_pi = 0.5 * (double) PI;

    // BBS
    m_grabbers[4].enabled = false;
    m_shortcut_key        = WXK_CONTROL_S;

    return true;
}

// [STATE] Reports feedback for the gizmo button, warning users when activation is not allowed due to empty selection.
// [INTENT] Drives ImGui button labeling so the user always understands whether the gizmo is ready.
// [UNITY] Unity port can mirror this via a `UI Toolkit` label that checks `SelectionManager.IsEmpty` before updating text.
std::string GLGizmoScale3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Scale") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Scale");
    }
}

// [STATE] Validates the current selection to ensure the scale gizmo only activates when meaningful objects exist.
// [THREAD] Runs on the GL event thread; must avoid background work because selection queries read shared data.
bool GLGizmoScale3D::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower();
}

// [EVENT] Resets selection tracking whenever the gizmo turns on to avoid carrying stale pivot info.
// [STATE] Clears cached indices and recomputes the selected coordinate space so manipulations stay consistent.
void GLGizmoScale3D::on_set_state()
{
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
}

// [STATE] Maps grabber IDs to their constraint counterparts so constrained drags use the correct axis pair.
static int constraint_id(int grabber_id)
{
    static const std::vector<int> id_map = {1, 0, 3, 2, 5, 4, 8, 9, 6, 7};
    return (0 <= grabber_id && grabber_id < (int) id_map.size()) ? id_map[grabber_id] : -1;
}

// [EVENT] Snapshots grabber positions/pivots when the user begins dragging so subsequent deltas compute ratios relative to the start frame.
// [STATE] Stores plane normals, constraint flags, bounding box half-sizes, and offset vectors used during drag updates.
// [UNITY] Unity should cache the initial transform and pointer state in the `OnBeginDrag` handler and reuse in later `OnDrag` grabs.
void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        auto grabbers_transform  = m_grabbers_tran.get_matrix();
        m_starting.drag_position = grabbers_transform * m_grabbers[m_hover_id].center;
        m_starting.plane_center  = grabbers_transform * m_grabbers[4].center; // plane_center = bottom center
        m_starting.plane_nromal  = (grabbers_transform * m_grabbers[5].center - grabbers_transform * m_grabbers[4].center).normalized();
        m_starting.ctrl_down     = wxGetKeyState(WXK_CONTROL);
        m_starting.box           = m_bounding_box;

        m_starting.center          = m_center;
        m_starting.instance_center = m_instance_center;

        const Vec3d box_half_size = 0.5 * m_bounding_box.size();

        m_starting.local_pivots[0] = Vec3d(box_half_size.x(), 0.0, -box_half_size.z());
        m_starting.local_pivots[1] = Vec3d(-box_half_size.x(), 0.0, -box_half_size.z());
        m_starting.local_pivots[2] = Vec3d(0.0, box_half_size.y(), -box_half_size.z());
        m_starting.local_pivots[3] = Vec3d(0.0, -box_half_size.y(), -box_half_size.z());
        m_starting.local_pivots[4] = Vec3d(0.0, 0.0, box_half_size.z());
        m_starting.local_pivots[5] = Vec3d(0.0, 0.0, -box_half_size.z());
        for (size_t i = 0; i < 6; i++) {
            m_starting.pivots[i] = grabbers_transform * m_starting.local_pivots[i]; // todo delete
        }
        m_starting.constraint_position = grabbers_transform * m_grabbers[constraint_id(m_hover_id)].center;
        m_scale = m_starting.scale = Vec3d::Ones();
        m_offset                   = Vec3d::Zero();
    }
}

// [EVENT] Dispatches the final scale command to the parent when the drag ends and resets modifier flags.
// [STATE] Clears the CTRL modifier flag so future drags start clean.
void GLGizmoScale3D::on_stop_dragging()
{
    m_parent.do_scale(L("Gizmo-Scale"));
    m_starting.ctrl_down = false;
}

// [EVENT] Invoked each frame while dragging to route axis-specific or uniform scaling logic.
// [STATE] Reads `m_hover_id` to pick the correct axis helpers and avoid mixed-mode movement.
void GLGizmoScale3D::on_dragging(const UpdateData& data)
{
    if ((m_hover_id == 0) || (m_hover_id == 1))
        do_scale_along_axis(X, data);
    else if ((m_hover_id == 2) || (m_hover_id == 3))
        do_scale_along_axis(Y, data);
    else if ((m_hover_id == 4) || (m_hover_id == 5))
        do_scale_along_axis(Z, data);
    else if (m_hover_id >= 6)
        do_scale_uniform(data);
}

// [STATE] Recomputes grabber positions/colors every frame based on the selection bounding box and modifier keys.
// [EVENT] Called in `on_render` to keep the gizmo locked to the selection transform before OpenGL draws the handles.
// [UNITY] Unity should refresh handle transforms in `LateUpdate` using cached selection bounds and `Input.GetKey(KeyCode.LeftControl)` for
// constraints.
void GLGizmoScale3D::update_grabbers_data()
{
    const Selection& selection   = m_parent.get_selection();
    const auto& [box, box_trafo] = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box               = box;
    m_center                     = box_trafo.translation();
    m_grabbers_tran.set_matrix(box_trafo);
    m_instance_center = (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) ?
                            selection.get_first_volume()->get_instance_offset() :
                            m_center;

    const Vec3d box_half_size = 0.5 * m_bounding_box.size();
    bool        ctrl_down     = wxGetKeyState(WXK_CONTROL);

    bool single_instance = selection.is_single_full_instance();
    bool single_volume   = selection.is_single_modifier() || selection.is_single_volume();

    // x axis
    m_grabbers[0].center = Vec3d(-(box_half_size.x()), 0.0, -box_half_size.z());
    m_grabbers[0].color  = (ctrl_down && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = Vec3d(box_half_size.x(), 0.0, -box_half_size.z());
    m_grabbers[1].color  = (ctrl_down && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    // y axis
    m_grabbers[2].center = Vec3d(0.0, -(box_half_size.y()), -box_half_size.z());
    m_grabbers[2].color  = (ctrl_down && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = Vec3d(0.0, box_half_size.y(), -box_half_size.z());
    m_grabbers[3].color  = (ctrl_down && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    // z axis do not show 4
    m_grabbers[4].center  = Vec3d(0.0, 0.0, -(box_half_size.z()));
    m_grabbers[4].enabled = false;

    m_grabbers[5].center = Vec3d(0.0, 0.0, box_half_size.z());
    m_grabbers[5].color  = (ctrl_down && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    // uniform
    m_grabbers[6].center = Vec3d(-box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[6].color  = (ctrl_down && m_hover_id == 8) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[7].center = Vec3d(box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[7].color  = (ctrl_down && m_hover_id == 9) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[8].center = Vec3d(box_half_size.x(), box_half_size.y(), -box_half_size.z());
    m_grabbers[8].color  = (ctrl_down && m_hover_id == 6) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[9].center = Vec3d(-box_half_size.x(), box_half_size.y(), -box_half_size.z());
    m_grabbers[9].color  = (ctrl_down && m_hover_id == 7) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    for (int i = 0; i < 6; ++i) {
        // m_grabbers[i].color       = AXES_COLOR[i / 2];
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i / 2];
    }
    for (int i = 6; i < 10; ++i) {
        // m_grabbers[i].color       = GRABBER_UNIFORM_COL;
        m_grabbers[i].hover_color = GRABBER_UNIFORM_HOVER_COL;
    }

    for (int i = 0; i < 10; ++i) {
        m_grabbers[i].matrix = m_grabbers_tran.get_matrix();
    }
}

// [STATE] Switches the coordinate system helper depending on whether the selection spans multiple objects or a single volume.
// [UNITY] Unity should trigger the same logic whenever `SelectionManager.SelectedVolume` changes, setting a shared `CoordinateSystemState`.
void GLGizmoScale3D::change_cs_by_selection()
{
    int          obejct_idx, volume_idx;
    ModelVolume* model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx) {
        return;
    }
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    } else if (model_volume) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Local);
    }
}

// [OPENGL] Clears depth, enables depth testing, sets shader uniforms, and renders axis grabbers and connection lines with the appropriate
// pipeline. [STATE] Ensures grabber data is up to date before issuing draw calls so the gizmo stays in sync with user selection each frame.
// [UNITY] Unity should mirror this by using a dedicated overlay camera + `Graphics.DrawMesh` sequence and keep per-handle transforms
// updated in `LateUpdate`.
void GLGizmoScale3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    update_grabbers_data();

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
#endif // !SLIC3R_OPENGL_ES

    const float grabber_mean_size = (float) ((m_bounding_box.size().x() + m_bounding_box.size().y() + m_bounding_box.size().z()) / 3.0);

    // draw connections
#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") :
                                                                               wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr) {
        shader->start_using();
        // BBS: when select multiple objects, uniform scale can be deselected, display the connection(4,5)
        // if (single_instance || single_volume) {
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_tran.get_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            const std::array<int, 4>& viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.25f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES
        if (m_grabbers[4].enabled && m_grabbers[5].enabled)
            render_grabbers_connection(4, 5, m_grabbers[4].color);
        render_grabbers_connection(6, 7, m_grabbers[2].color);
        render_grabbers_connection(7, 8, m_grabbers[2].color);
        render_grabbers_connection(8, 9, m_grabbers[0].color);
        render_grabbers_connection(9, 6, m_grabbers[0].color);
        shader->stop_using();
    }

    // draw grabbers
    shader = wxGetApp().get_shader("gouraud_light");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("emission_factor", 0.1f);
        render_grabbers(grabber_mean_size);
        shader->stop_using();
    }
}

// [EVENT] Promotes gizmo grabbers to the top of raycast picking so they intercept input before regular scene geometry.
// [UNITY] Unity should update the `RaycastLayerMask` or `SortingLayer` so gizmo colliders process before models.
void GLGizmoScale3D::on_register_raycasters_for_picking()
{
    // the gizmo grabbers are rendered on top of the scene, so the raytraced picker should take it into account
    m_parent.set_raycaster_gizmos_on_top(true);
}

// [EVENT] Restores the raycast priority after drawing so subsequent picks fallback to the scene geometry.
void GLGizmoScale3D::on_unregister_raycasters_for_picking() { m_parent.set_raycaster_gizmos_on_top(false); }

// [OPENGL] Builds/updates GLModel line geometry, toggles line stipple when available, and renders axis connection lines.
// [PORTING_HAZARD:P3] Uses legacy `glLineStipple` which Unity cannot replicate directly; the port must replicate dashed visuals with shader
// UV tricks.
void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color)
{
    auto grabber_connection = [this](unsigned int id_1, unsigned int id_2) {
        for (int i = 0; i < int(m_grabber_connections.size()); ++i) {
            if (m_grabber_connections[i].grabber_indices.first == id_1 && m_grabber_connections[i].grabber_indices.second == id_2)
                return i;
        }
        return -1;
    };

    const int id = grabber_connection(id_1, id_2);
    if (id == -1)
        return;

    if (!m_grabber_connections[id].model.is_initialized() || !m_grabber_connections[id].old_v1.isApprox(m_grabbers[id_1].center) ||
        !m_grabber_connections[id].old_v2.isApprox(m_grabbers[id_2].center)) {
        m_grabber_connections[id].old_v1 = m_grabbers[id_1].center;
        m_grabber_connections[id].old_v2 = m_grabbers[id_2].center;
        m_grabber_connections[id].model.reset();

        GLModel::Geometry init_data;
        init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex((Vec3f) m_grabbers[id_1].center.cast<float>());
        init_data.add_vertex((Vec3f) m_grabbers[id_2].center.cast<float>());

        // indices
        init_data.add_line(0, 1);

        m_grabber_connections[id].model.init_from(std::move(init_data));
    }

    m_grabber_connections[id].model.set_color(color);
    // ORCA: OpenGL Core Profile
#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile()) {
        glLineStipple(1, 0x0FFF);
        glEnable(GL_LINE_STIPPLE);
    }
#endif // !SLIC3R_OPENGL_ES
    m_grabber_connections[id].model.render();
#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glDisable(GL_LINE_STIPPLE);
#endif // !SLIC3R_OPENGL_ES
}

// BBS: add input window for move
// [EVENT] Delegates to `GizmoObjectManipulation` to show the scale edit window when the user requests input fields.
// [UNITY] Unity should show a floating `UI Toolkit` window by invoking the same helper from the equivalent MonoBehaviour.
void GLGizmoScale3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_scale_input_window(m_imgui, "Scale", x, y, bottom_limit);
}

// [STATE] Adjusts axis-specific scale and potential offset when the user drags the corresponding grabber.
// [UNITY] Unity should call this logic from an axis-specific handler that applies delta scaling to `Transform.localScale`.
void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        m_scale(axis) = m_starting.scale(axis) * ratio;
        if (m_starting.ctrl_down && abs(ratio - 1.0f) > 0.001) {
            double local_offset = 0.5 * (m_scale(axis) - m_starting.scale(axis)) * m_starting.box.size()(axis);
            if (m_hover_id == 2 * axis) {
                local_offset *= -1.0;
            }
            Vec3d local_offset_vec;
            switch (axis) {
            case X: {
                local_offset_vec = local_offset * Vec3d::UnitX();
                break;
            }
            case Y: {
                local_offset_vec = local_offset * Vec3d::UnitY();
                break;
            }
            case Z: {
                local_offset_vec = local_offset * Vec3d::UnitZ();
                break;
            }
            default: break;
            }
            if (m_object_manipulation->is_world_coordinates()) {
                m_offset = local_offset_vec;
            } else { // if (m_object_manipulation->is_instance_coordinates())
                m_offset = m_grabbers_tran.get_matrix_no_offset() * local_offset_vec;
            }
        } else
            m_offset = Vec3d::Zero();
    }
}

// [STATE] Applies the same ratio to every axis so uniform scaling keeps the initial aspect ratio.
// [PORTING_HAZARD:P3] Mirrors uniform drags through `m_scale` but Unity may need to disable non-uniform handles when uniform lock is active.
void GLGizmoScale3D::do_scale_uniform(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        m_scale  = m_starting.scale * ratio;
        m_offset = Vec3d::Zero();
    }
}

// [STATE] Calculates the ratio between the drag ray and starting vector, including shift snapping and CTRL constraints.
// [PORTING_HAZARD:P3] Relies on manual ray-plane math and an undocumented snap step, so Unity must reproduce this carefully to avoid jumpy scaling.
double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;

    Vec3d  pivot            = (m_starting.ctrl_down && (m_hover_id < 6)) ? m_starting.constraint_position :
                                                                           m_starting.plane_center; // plane_center = bottom center
    Vec3d  starting_vec     = m_starting.drag_position - pivot;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        Vec3d mouse_dir    = data.mouse_ray.unit_vector();
        Vec3d plane_normal = m_starting.plane_nromal;
        if (m_hover_id == 5) {
            // get z-axis moving plane normal
            Vec3d plane_vec = mouse_dir.cross(m_starting.plane_nromal);
            plane_normal    = plane_vec.cross(m_starting.plane_nromal);
        }
        plane_normal = plane_normal.normalized();
        // finds the intersection of the mouse ray with the plane that the drag point moves
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection
        auto dot_value          = (plane_normal.dot(mouse_dir));
        auto angle              = Geometry::rad2deg(acos(dot_value));
        auto big_than_min_angle = abs(angle) < 95 && abs(angle) > 85;
        if (big_than_min_angle) {
            return 1;
        }
        Vec3d inters = GetIntersectionOfRayAndPlane(data.mouse_ray.a, mouse_dir, m_starting.drag_position, plane_normal);

        Vec3d inters_vec = inters - m_starting.drag_position;

        // finds projection of the vector along the staring direction
        double proj = inters_vec.dot(starting_vec.normalized());

        ratio = (len_starting_vec + proj) / len_starting_vec;
    }

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * (double) std::round(ratio / m_snap_step);

    return ratio;
}

}} // namespace Slic3r::GUI
