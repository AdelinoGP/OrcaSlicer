#ifndef ROTOPTIMIZEJOB_HPP
#define ROTOPTIMIZEJOB_HPP

#include "Job.hpp"

#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

class Plater;

// [INTENT] Coordinate the GUI's heuristic rotation strategies with the Job queue so the renderer can reorient models before slicing.
// [UNITY] Model this as a ScriptableObject-backed job descriptor that a MonoBehaviour schedules (Task/Job System) and marshals completion
// back to the main thread. [PORTING_HAZARD:P3] The wxWidgets Job base assumes prepare->process->finalize dispatch order; Unity must respect
// that sequence when bridging coroutines and main-thread callbacks.
class RotoptimizeJob : public Job
{
    using FindFn = std::function<Vec2d(const ModelObject& mo, const sla::RotOptimizeParams& params)>;

    struct FindMethod
    {
        std::string name;
        FindFn      findfn;
        std::string descr;
    };

    static inline const FindMethod Methods[] = {{L("Best surface quality"), sla::find_best_misalignment_rotation,
                                                 L("Optimize object rotation for best surface quality.")},
                                                {L("Reduced overhang slopes"), sla::find_least_supports_rotation,
                                                 L("Optimize object rotation to have minimum amount of overhangs needing support "
                                                   "structures.\nNote that this method will try to find the best surface of the object "
                                                   "for touching the print bed if no elevation is set.")},
                                                // Just a min area bounding box that is done for all methods anyway.
                                                {L("Lowest Z height"), sla::find_min_z_height_rotation,
                                                 L("Rotate the model to have the lowest z height for faster print time.")}};

    // [STATE][UNITY] Static metadata that populates the dropdown in the main view and mirrors a Unity dropdown+ListSource collection with callables.

    size_t m_method_id = 0;
    float  m_accuracy  = 0.75;
    // [STATE] UI slider or spinner controls this accuracy threshold; the worker honours it while scoring rotations.
    // [UNITY] Mirror as a serialized field on the MonoBehaviour/controller so designers can tweak accuracy without recompiling.

    DynamicPrintConfig m_default_print_cfg;
    // [STATE] Cached print config read from the GUI before processing so the worker has a consistent snapshot.

    struct ObjRot
    {
        size_t               idx;
        std::optional<Vec2d> rot;
        ObjRot(size_t id) : idx{id}, rot{} {}
    };

    // [STATE] Stores selection indexes plus optional rotation result; keep copies because the worker thread cannot touch wxWidgets objects directly.

    std::vector<ObjRot> m_selected_object_ids;
    // [THREAD][PORTING_HAZARD:P3] Built on the main thread and read by the worker; Unity must copy the list before scheduling to avoid
    // selection races.
    Plater* m_plater;
    // [THREAD][PORTING_HAZARD:P3] Plater is a UI owner pointer that only exists on the main thread, so finalize must marshal any callbacks
    // to the UI dispatcher.

public:
    // [EVENT][THREAD] Runs on the UI thread before the worker starts to snapshot selection and method; this is where dropdown events seed
    // the job state.
    void prepare();
    // [THREAD] Executes on a background worker via Job::process, using `Ctl` for progress/cancel and `Methods[m_method_id]` to compute
    // rotations. [UNITY] Translate into a `Task.Run`/Unity Job that reports progress via a main-thread dispatcher or `IProgress<T>` bridge.
    void process(Ctl& ctl) override;

    RotoptimizeJob();

    // [EVENT][THREAD] Finalizes on the main thread once work completes so meshes and slices can read the new orientations safely.
    // [PORTING_HAZARD:P2] Because finalize writes back into `Plater`, Unity must ensure the callback runs on the GameObject owning the view.
    void finalize(bool canceled, std::exception_ptr&) override;

    static constexpr size_t get_methods_count() { return std::size(Methods); }

    static std::string get_method_name(size_t i) { return _utf8(Methods[i].name); }

    static std::string get_method_description(size_t i) { return _utf8(Methods[i].descr); }
};

}} // namespace Slic3r::GUI

#endif // ROTOPTIMIZEJOB_HPP
