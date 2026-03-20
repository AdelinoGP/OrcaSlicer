#include "RotoptimizeJob.hpp"

#include "libslic3r/MTUtils.hpp"
#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/MinAreaBoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] RotoptimizeJob orchestrates the SLA auto-rotation pipeline that inspects each selected model, chooses the best orientation, and
// then applies that transform back to the scene. [THREAD] Instances are queued by the GUI job system so `prepare()` runs on the main thread
// before `process()` executes asynchronously; `finalize()` serializes results back onto the UI thread. [PORTING_HAZARD:P2] Relies on global
// `wxGetApp()` singletons (`app_config`, `preset_bundle`, `plater`) which need Unity-friendly equivalents (e.g., a ScriptableObject
// configuration service + MonoBehaviour scheduler) before porting any logic. [UNITY] Translate this job into a MonoBehaviour that records
// selection/config data, schedules a Unity Job System handle for `process()`, and marshals the cached results back to the main thread for
// `finalize()`.

void RotoptimizeJob::prepare()
{
    // [STATE] `m_accuracy` and `m_method_id` mirror configurable SLA auto-rotate settings; `m_default_print_cfg` and
    // `m_selected_object_ids` become immutable job inputs for `process()`. [INTENT] This synchronization step detaches the heavy compute
    // loop from any live UI selection so the worker thread can run without touching wxWidgets handles. [PORTING_HAZARD:P3] Unity must
    // ensure a similar data-copy stage happens on the main thread, since GameObject references can only be read there; consider cloning
    // Mesh/Transform handles before launching the compute job.
    std::string accuracy_str = wxGetApp().app_config->get("sla_auto_rotate", "accuracy");

    std::string method_str = wxGetApp().app_config->get("sla_auto_rotate", "method_id");

    if (!accuracy_str.empty())
        m_accuracy = std::stof(accuracy_str);

    if (!method_str.empty())
        m_method_id = std::stoi(method_str);

    m_accuracy  = std::max(0.f, std::min(m_accuracy, 1.f));
    m_method_id = std::max(size_t(0), std::min(get_methods_count() - 1, m_method_id));

    m_default_print_cfg = wxGetApp().preset_bundle->full_config();

    const auto& sel = m_plater->get_selection().get_content();

    m_selected_object_ids.clear();
    m_selected_object_ids.reserve(sel.size());

    for (const auto& s : sel) {
        int obj_id;
        std::tie(obj_id, std::ignore) = s;
        m_selected_object_ids.emplace_back(obj_id);
    }
}

// [THREAD] `process()` runs on the background worker bound to the job queue; `Ctl` carries the cancellation/progress channel back to the
// GUI. [EVENT] `statucb` keeps the UI informed and maps to Unity progress/cancel callbacks, while the worker probes `ctl.was_canceled()`
// every iteration. [INTENT] Compute orientation candidates via the selected `Methods` entry, storing the winning rotation per cached
// `ObjRot` so `finalize()` can replay the result. [STATE] `prev_status` approximates progress windows, and `m_selected_object_ids` becomes
// `RUNNING` state as each `rot` field is filled. [PORTING_HAZARD:P3] `Methods[m_method_id].findfn` may hit heavy math and currently runs
// inline; Unity should push this to a dedicated job (IJobParallelFor or Task) rather than the MonoBehaviour main thread. [UNITY] Equivalent
// in Unity is a MonoBehaviour-triggered Worker that runs a Job/System while reporting `IProgress<float>` and `CancellationToken`, then
// marshals `NativeArray<ObjRot>` back to main thread.
void RotoptimizeJob::process(Ctl& ctl)
{
    int  prev_status = 0;
    auto statustxt   = _u8L("Searching for optimal orientation");
    ctl.update_status(0, statustxt);

    auto params = sla::RotOptimizeParams{}
                      .accuracy(m_accuracy)
                      .print_config(&m_default_print_cfg)
                      .statucb([this, &prev_status, &ctl /*, &statustxt*/](int s) { return !ctl.was_canceled(); });

    for (ObjRot& objrot : m_selected_object_ids) {
        ModelObject* o = m_plater->model().objects[size_t(objrot.idx)];
        if (!o)
            continue;

        if (Methods[m_method_id].findfn)
            objrot.rot = Methods[m_method_id].findfn(*o, params);

        prev_status += 100 / m_selected_object_ids.size();

        if (ctl.was_canceled())
            break;
    }

    ctl.update_status(100, ctl.was_canceled() ? _u8L("Orientation search canceled.") : _u8L("Orientation found."));
}

RotoptimizeJob::RotoptimizeJob() : m_plater{wxGetApp().plater()} { prepare(); }

// [THREAD] `finalize()` executes on the UI thread after the worker completes; its job is to apply cached rotations and fire GUI updates
// when the completion event fires. [EVENT] Triggered by the job framework only when `process()` returns without cancellation and `eptr` is
// null so redundant transforms are avoided. [PORTING_HAZARD:P2] Relies on mutable `ModelObject`/`ModelInstance` transforms,
// `MinAreaBoundigBox`, and `Plater::update()`, so Unity must reproduce the same geometry math (convex hull + rotation) before refreshing
// the canvas/camera. [OPENGL] Calling `m_plater->update()` invalidates the OpenGL canvas; Unity should call `SceneViewRedraw` or re-render
// the camera after changing `Transform`s instead. [UNITY] Replace with a main-thread callback that updates Unity GameObjects (via
// `Transform.rotation` and `Bounds` helpers) and then triggers `RenderPipeline.BeginFrameRendering` or `Camera.Render()` as needed.
void RotoptimizeJob::finalize(bool canceled, std::exception_ptr& eptr)
{
    if (canceled || eptr)
        return;

    for (const ObjRot& objrot : m_selected_object_ids) {
        ModelObject* o = m_plater->model().objects[size_t(objrot.idx)];
        if (!o)
            continue;

        for (ModelInstance* oi : o->instances) {
            if (objrot.rot)
                oi->set_rotation({objrot.rot->x(), objrot.rot->y(), 0.});

            auto    trmatrix = oi->get_transformation().get_matrix();
            Polygon trchull  = o->convex_hull_2d(trmatrix);

            MinAreaBoundigBox rotbb(trchull, MinAreaBoundigBox::pcConvex);
            double            phi = rotbb.angle_to_X();

            // The box should be landscape
            if (rotbb.width() < rotbb.height())
                phi += PI / 2;

            Vec3d rt = oi->get_rotation();
            rt(Z) += phi;

            oi->set_rotation(rt);
        }

        // Correct the z offset of the object which was corrupted be
        // the rotation
        o->ensure_on_bed();

        //        m_plater->find_new_position(o->instances);
    }

    if (!canceled)
        m_plater->update();
}

}} // namespace Slic3r::GUI
