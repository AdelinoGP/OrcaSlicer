#ifndef SLAIMPORTJOB_HPP
#define SLAIMPORTJOB_HPP

#include "Job.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Extract the customers decisions (model vs profile, slice window size, file path) that drive the SLA import workflow.
// [STATE] Exposes the dropdown selection, march square canvas size, and file path that the job will snapshot before running.
// [EVENT] Implemented by the dialog/panel so Apply/Start buttons grab this interface, and changing UI state should stay on the UI thread to
// avoid races. [THREAD] These getters execute on the UI thread; the job must marshal the captured values before the worker thread reads
// them. [UNITY] Mirror this contract via a MonoBehaviour (e.g., a SerializedScriptableObject + controller) that exposes SelectionMode,
// CanvasSize, and SourcePath fields for the async job kick-off. [PORTING_HAZARD:P3] UI state can mutate between query and worker start; the
// Unity port must snapshot or lock the view to keep the worker consistent.
class SLAImportJobView
{
public:
    enum Sel { modelAndProfile, profileOnly, modelOnly };

    virtual ~SLAImportJobView() = default;

    virtual Sel         get_selection() const          = 0;
    virtual Vec2i32     get_marchsq_windowsize() const = 0;
    virtual std::string get_path() const               = 0;
};

class Plater;

class SLAImportJob : public Job
{
    class priv;

    // [STATE] Holds the captured selection, file path, and any job-specific caches until completion.
    std::unique_ptr<priv> p;
    using Sel = SLAImportJobView::Sel;

public:
    // [EVENT] Called on the UI thread when the job is registered so that the view snapshot can be stored before the worker launches.
    void prepare();
    // [THREAD] Held by the worker thread; runs the heavy lifting of moving the selection into the SLA importer while avoiding UI touches.
    // [UNITY] Map to a Unity Job System worker that feeds data into the SLA render pipeline and then queues completion via
    // UnityMainThreadDispatcher.
    void process(Ctl& ctl) override;
    // [THREAD] Fired back on the UI thread to deliver completion/cancellation signals and clean up the texture upload handoff.
    // [EVENT] Notify the progress indicator/preview controller with the final status.
    // [PORTING_HAZARD:P2] Unity will need an explicit scheduler for job completion callbacks rather than relying on wxWidgets event loops.
    void finalize(bool canceled, std::exception_ptr&) override;

    // [INTENT] Build the job around the provided view so UI state can be captured lazily via prepare().
    SLAImportJob(const SLAImportJobView*);
    ~SLAImportJob();

    // [STATE] Clears any cached selections so the job can be reused without dangling file handles.
    void reset();
};

}} // namespace Slic3r::GUI

#endif // SLAIMPORTJOB_HPP
