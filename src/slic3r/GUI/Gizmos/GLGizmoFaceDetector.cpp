#include "GLGizmoFaceDetector.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/FaceDetector.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <GL/glew.h>

#ifdef __WINDOWS__
#include <windows.h>
#include <stdio.h>
#endif

namespace Slic3r { namespace GUI {

// [INTENT] Provide a face-detection gizmo overlay that can be toggled in the GL canvas for debugging/preview.
// [STATE] Relies on the `model` mesh cache defined in the header to hold translated exterior faces.
// [UNITY] Unity analog: replace with a dedicated `MonoBehaviour` that spawns a `MeshFilter`/`MeshRenderer` with a translucent material when
// detection is active.

bool GLGizmoFaceDetector::on_init() { return true; }

std::string GLGizmoFaceDetector::on_get_name() const
{
    // [INTENT] Expose a descriptive name that ties this gizmo to the face recognition panel so the UI button slug matches Unity UI toolbar
    // text. [UNITY] Mirror this string in UI Toolkit (e.g., a Button label in a ToolSettings panel) so Unity porters can map the command
    // verbatim.
    return (_L("Face recognition") + " [P]").ToUTF8().data();
}

void GLGizmoFaceDetector::on_render()
{
    if (model.is_initialized()) {
        // [STATE] The cached exterior mesh is tinted here before every render; it is built on demand instead of per frame.
        // [OPENGL] GLModel handles VAO/VBO binding and draw calls so the highlight renders after the main mesh.
        // [UNITY] Unity port should update a `MeshFilter` + translucent `Material` and rely on the graphics layer to draw it—no manual GL calls.
        model.set_color({0.f, 0.f, 1.f, 0.4f});
        model.render();
    }
}

void GLGizmoFaceDetector::on_render_input_window(float x, float y, float bottom_limit)
{
#if 0
    if (!m_c->selection_info() || !m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(14.0f);
    y = std::min(y, bottom_limit - approx_height);
    //BBS: GUI refactor: move gizmo to the right
#if BBS_TOOLBAR_ON_TOP
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 0.5f, 0.0f);
#else
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    ImGuiWrapper::push_toolbar_style();
    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::PushItemWidth(m_imgui->get_style_scaling() * 150);
    ImGui::InputDouble("Sample interval", &m_sample_interval, 0.0f, 0.0f, "%.2f");

    bool btn_clicked = m_imgui->button(_L("Perform Recognition"));
    if (btn_clicked) {
        perform_recognition(m_parent.get_selection());
    }

    m_imgui->end();
    ImGuiWrapper::pop_toolbar_style();
#endif
    // [EVENT] Input window is stubbed out, so there is currently no ImGui UI for launching detection.
    // [UNITY] Replace this with a UI Toolkit toolbar entry + `Button`/`FloatField` bound to `m_sample_interval` via a ScriptableObject
    // settings asset. [PORTING_HAZARD:P3] The wxWidgets/ImGui combo here would need a separate asynchronous panel in Unity, so keep UX
    // state in a shared controller.
}

void GLGizmoFaceDetector::on_set_state()
{
    // [EVENT] Called every time the user toggles the gizmo; ensures geometry is up to date before drawing.
    if (get_state() == On) {
        model.reset();
        display_exterior_face();
    }
}

bool GLGizmoFaceDetector::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    // [STATE] Aligns activation availability with selection constraints so Unity can reuse the same predicates (single instance & not wipe tower).
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

void GLGizmoFaceDetector::perform_recognition(const Selection& selection)
{
    ModelObject* mo = m_c->selection_info()->model_object();
    // [INTENT] Would run face-detection sampling when the button is pressed; currently disabled pending upstream face detector usage.
    // [STATE] Uses `m_sample_interval` to control sampling density and stores results in `FaceDetector` caches.
    // [UNITY] In Unity this logic belongs to a `FaceDetectionController` coroutine that marshals work to a background job via the Job
    // System and reports back on the main thread. [PORTING_HAZARD:P2] The wxWidgets-based job scheduler is absent in Unity, so porters will
    // need a custom async dispatcher plus cancellation handling.
    // FaceDetector face_detector(mo, m_sample_interval);

    // face_detector.detect_exterior_face();
}

void GLGizmoFaceDetector::display_exterior_face()
{
    // [INTENT] Rebuilds an explicit mesh of exterior faces from the current scene so the overlay always matches the latest selection.
    // [THREAD] Must run on the main GUI thread because it reads wxGetApp().model() and touches scene geometry.
    int cnt = 0;
    model.reset();

    GLModel::Geometry init_data;
    init_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3,
                        GLModel::Geometry::EIndexType::UINT};

    const ModelObjectPtrs& objects = wxGetApp().model().objects;
    // [PORTING_HAZARD:P3] Directly iterates `ModelObject` storage via wx app globals; Unity port should read from the shared `ModelStore`
    // or convert `Scene` assets instead.
    for (ModelObject* mo : objects) {
        const ModelInstance* mi           = mo->instances[0];
        Transform3d          inst_transfo = mi->get_matrix();
        for (ModelVolume* mv : mo->volumes) {
            TriangleMesh mesh_temp = mv->mesh();
            mesh_temp.transform(mv->get_matrix() * inst_transfo);
            indexed_triangle_set& mv_its = mesh_temp.its;
            for (int facet_idx = 0; facet_idx < mv_its.indices.size(); facet_idx++) {
                const stl_triangle_vertex_indices& facet_vert_idxs = mv_its.indices[facet_idx];
                if (mv_its.get_property(facet_idx).type != eExteriorAppearance)
                    continue;

                for (int i = 0; i < 3; ++i) {
                    init_data.add_vertex((Vec3f) mv_its.vertices[facet_vert_idxs[i]].cast<float>(), Vec3f{0.0f, 0.0f, 1.0f});
                }

                init_data.add_uint_triangle(cnt, cnt + 1, cnt + 2);
                cnt += 3;
            }
        }
    }
    // [STATE] The generated vertex/index buffers become the `GLModel` cached mesh which persists until the gizmo state resets.
    // [OPENGL] `init_from` uploads the computed geometry to GPU buffers; any Unity equivalent must mirror this by uploading to
    // `Mesh.SetVertices`/`SetTriangles`. [UNITY] In Unity, update a shared `Mesh` asset per selection and hide/show it using a
    // `MeshRenderer` + translucent `Material` on a dedicated GameObject in the scene hierarchy.
    model.init_from(std::move(init_data));
}

CommonGizmosDataID GLGizmoFaceDetector::on_get_requirements() const
{
    // [STATE] Declares that this gizmo depends on selection metadata so porters must ensure the same data is populated before activation.
    return CommonGizmosDataID::SelectionInfo;
}

}} // namespace Slic3r::GUI
