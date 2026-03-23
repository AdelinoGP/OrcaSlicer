#include "GLSelectionRectangle.hpp"
#include "Camera.hpp"
#include "CameraUtils.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include <igl/project.h>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

// [INTENT] Begin a Select/Deselect drag when the user presses the mouse down in the viewport.
// [STATE] Recording `m_state` plus both corners so release logic can perform the right operation.
// [EVENT] Routed from the GLCanvas3D mouse-down handler before any selection job starts.
// [UNITY] Hook Unity's Input System `PointerDown` + UI Toolkit `VisualElement` drag overlay so the drag rectangles match.
void GLSelectionRectangle::start_dragging(const Vec2d& mouse_position, EState state)
{
    if (is_dragging() || (state == Off))
        return;

    m_state        = state;
    m_start_corner = mouse_position;
    m_end_corner   = mouse_position;
}

// [INTENT] Keep the live rectangle corners in sync with the mouse move event while dragging.
// [EVENT] Called by the mouse move handler bound to the GLCanvas3D capture.
// [UNITY] Mirror Unity's Input System `PointerMove` + `VisualElement` manipulator updates while the pointer remains down.
void GLSelectionRectangle::dragging(const Vec2d& mouse_position)
{
    if (!is_dragging())
        return;

    m_end_corner = mouse_position;
}

// [INTENT] Determine which 3D points land inside the current drag rectangle after release.
// [EVENT] Invoked once the drag ends (selection/deselection command) so the GUI can highlight survivors.
// [THREAD] Runs on the UI thread immediately after pointer release so it can safely read the camera and selection caches.
// [UNITY] Mirror this logic with `Camera.WorldToScreenPoint` + a `Rect` intersection inside a UI Toolkit drag tracker.
std::vector<unsigned int> GLSelectionRectangle::contains(const std::vector<Vec3d>& points) const
{
    std::vector<unsigned int> out;

    // bounding box created from the rectangle corners - will take care of order of the corners
    const BoundingBox rectangle(Points{Point(m_start_corner.cast<coord_t>()), Point(m_end_corner.cast<coord_t>())});
    // [STATE] `rectangle` caches the current drag bounds so the projection loop can do a simple containment test.

    // Iterate over all points and determine whether they're in the rectangle.
    const Camera& camera = wxGetApp().plater()->get_camera();
    // [STATE] Always read the live `Plater` camera so viewport zoom/orbit adjustments stay in sync with the drag projection.
    Points points_2d = CameraUtils::project(camera, points);
    // [PORTING_HAZARD:P3] Requires the same viewport camera as GLCanvas3D; Unity port must keep camera filters in sync before projecting.
    unsigned int size = static_cast<unsigned int>(points.size());
    for (unsigned int i = 0; i < size; ++i)
        if (rectangle.contains(points_2d[i]))
            out.push_back(i);

    return out;
}

// [EVENT] Called from the mouse-up handler to signal the selection rectangle is done.
// [STATE] Clearing `m_state` to `Off` stops `render` and lets selection logic evaluate the dragged points.
// [UNITY] Translate into releasing the Input System pointer capture and hiding the UI Toolkit drag VisualElement.
void GLSelectionRectangle::stop_dragging()
{
    if (is_dragging())
        m_state = Off;
}

// [INTENT] Draw the in-progress rectangle overlay whenever a drag is active in the viewport.
// [EVENT] Tied directly to GLCanvas3D's render loop so it runs per frame on the UI render thread.
// [THREAD] Must execute on the GLCanvas3D render thread because it manipulates GL state.
void GLSelectionRectangle::render(const GLCanvas3D& canvas)
{
    if (!is_dragging())
        return;

    // [STATE] Read the canvas dimensions once per frame so the rectangle stays in sync with viewport resizing.
    const Size  cnv_size   = canvas.get_canvas_size();
    const float cnv_width  = (float) cnv_size.get_width();
    const float cnv_height = (float) cnv_size.get_height();
    if (cnv_width == 0.0f || cnv_height == 0.0f)
        return;

    const float cnv_inv_width  = 1.0f / cnv_width;
    const float cnv_inv_height = 1.0f / cnv_height;
    // [OPENGL] Map pixel coordinates to NDC so the rectangle can be rendered with a simple shader.
    const float left   = 2.0f * (get_left() * cnv_inv_width - 0.5f);
    const float right  = 2.0f * (get_right() * cnv_inv_width - 0.5f);
    const float top    = -2.0f * (get_top() * cnv_inv_height - 0.5f);
    const float bottom = -2.0f * (get_bottom() * cnv_inv_height - 0.5f);
    // [PORTING_HAZARD:P3] Unity must duplicate this pixel-to-clip conversion to keep the overlay aligned as the canvas size or DPI changes.

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile()) {
        // [OPENGL] Compatibility profile uses thicker line width to make the rectangle visible without tesselation.
        glsafe(::glLineWidth(1.5f));
    }
#endif // !SLIC3R_OPENGL_ES

    // [OPENGL] Force depth test off so the overlay stays on top of 3D content without z-fighting.
    glsafe(::glDisable(GL_DEPTH_TEST));

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile()) {
        glsafe(::glPushAttrib(GL_ENABLE_BIT));
        // [PORTING_HAZARD:P2] Legacy `glLineStipple` is unavailable in modern graphics pipelines; Unity needs a dashed material or
        // procedural texture to mimic it.
        glsafe(::glLineStipple(4, 0xAAAA));
        glsafe(::glEnable(GL_LINE_STIPPLE));
    }
#endif // !SLIC3R_OPENGL_ES

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    // [UNITY] Unity would bind a LineRenderer or procedural dash shader per camera rather than switching manual GLSL passes.
    // [PORTING_HAZARD:P3] Core-profile vs compatibility selection changes the shader footprint; port must replicate the dashed-thick vs
    // flat behavior conditionally.
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") :
                                                                               wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr) {
        shader->start_using();

        // [STATE] Rebuild the cached GLModel only when the corners actually change to avoid re-uploading vertex buffers.
        if (!m_rectangle.is_initialized() || !m_old_start_corner.isApprox(m_start_corner) || !m_old_end_corner.isApprox(m_end_corner)) {
            m_old_start_corner = m_start_corner;
            m_old_end_corner   = m_end_corner;
            m_rectangle.reset();

            GLModel::Geometry init_data;
            // [OPENGL] Mesh data encodes a thin loop of lines in normalized device space; Unity would use a Mesh + LineRenderer with the
            // same corner math.
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P4};
                init_data.reserve_vertices(5);
                init_data.reserve_indices(8);
#if !SLIC3R_OPENGL_ES
            } else {
                init_data.format = {GLModel::Geometry::EPrimitiveType::LineLoop, GLModel::Geometry::EVertexLayout::P2};
                init_data.reserve_vertices(4);
                init_data.reserve_indices(4);
            }
#endif // !SLIC3R_OPENGL_ES

            // vertices
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const float width     = right - left;
                const float height    = top - bottom;
                float       perimeter = 0.0f;

                init_data.add_vertex(Vec4f(left, bottom, 0.0f, perimeter));
                perimeter += width;
                init_data.add_vertex(Vec4f(right, bottom, 0.0f, perimeter));
                perimeter += height;
                init_data.add_vertex(Vec4f(right, top, 0.0f, perimeter));
                perimeter += width;
                init_data.add_vertex(Vec4f(left, top, 0.0f, perimeter));
                perimeter += height;
                init_data.add_vertex(Vec4f(left, bottom, 0.0f, perimeter));

                // indices
                init_data.add_line(0, 1);
                init_data.add_line(1, 2);
                init_data.add_line(2, 3);
                init_data.add_line(3, 4);
#if !SLIC3R_OPENGL_ES
            } else {
                init_data.add_vertex(Vec2f(left, bottom));
                init_data.add_vertex(Vec2f(right, bottom));
                init_data.add_vertex(Vec2f(right, top));
                init_data.add_vertex(Vec2f(left, top));

                // indices
                init_data.add_index(0);
                init_data.add_index(1);
                init_data.add_index(2);
                init_data.add_index(3);
            }
#endif // !SLIC3R_OPENGL_ES

            m_rectangle.init_from(std::move(init_data));
        }

        // [OPENGL] Use identity matrices because the rectangle is already in clip space; update uniforms when viewport or dash parameters change.
        shader->set_uniform("view_model_matrix", Transform3d::Identity());
        shader->set_uniform("projection_matrix", Transform3d::Identity());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            const std::array<int, 4>& viewport = wxGetApp().plater()->get_camera().get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.25f);
            shader->set_uniform("dash_size", 0.01f);
            shader->set_uniform("gap_size", 0.0075f);
            // [PORTING_HAZARD:P2] These dash/width uniforms live in code rather than config, so ensure the Unity line material exposes
            // matching knobs when the design system needs tweaking.
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES

        // [UNITY] Unity counterpart would set the LineRenderer material color per selection theme before drawing.
        // [OPENGL] The GLModel render call streams the cached vertex/index buffers as lines.
        m_rectangle.set_color(ColorRGBA::ORCA()); // ORCA: use orca color for selection rectangle
        m_rectangle.render();
        shader->stop_using();
    }

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        // [OPENGL] Pop attributes so the rest of the scene rendering is unaffected by the overlay adjustments.
        glsafe(::glPopAttrib());
#endif // !SLIC3R_OPENGL_ES
}

} // namespace GUI
} // namespace Slic3r
