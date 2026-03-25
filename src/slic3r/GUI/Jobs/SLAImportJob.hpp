// [INTENT]
// This header file declares the `SLAImportJob` class and its associated view
// interface, `SLAImportJobView`. This is part of the system for importing SLA
// archives (`.sl1`, `.sl1s`, `.zip`) into the application.
//
// - `SLAImportJobView`: A pure virtual interface that defines the contract for
//   any UI element (like a dialog) that can provide the necessary information
//   for an SLA import. This decouples the background job from the specific UI
//   implementation.
//
// - `SLAImportJob`: The background job that performs the actual import. It
//   takes an `SLAImportJobView` to get the user's settings, and then runs the
//   import process on a worker thread to avoid blocking the UI.
//
// [UNITY]
// In a Unity port:
// - `SLAImportJobView` would be a C# interface (`ISLAImportView`) implemented
//   by a UI script.
// - `SLAImportJob` would be replaced by a C# class that manages the import
//   process using an async Task.
//
// [PORTING_HAZARD:P2]
// - The `SLAImportJobView` interface uses C++ types like `Vec2i32` and `std::string`
//   that will need C# counterparts in a ported API.
// - `SLAImportJob` inherits from `Job`, a GUI-layer base class; the Unity equivalent
//   must be a custom MonoBehaviour or a unified Task-based infrastructure.

#ifndef SLAIMPORTJOB_HPP
#define SLAIMPORTJOB_HPP

#include "Job.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] An interface class that defines the contract for a UI view that
// provides the necessary data for an SLA import job. This decouples the job
// from the specific UI implementation.
// [UNITY] This would be a C# interface (e.g., `ISLAImportDataProvider`) that
// the UI panel for SLA import would implement.
class SLAImportJobView
{
public:
    enum Sel { modelAndProfile, profileOnly, modelOnly };

    virtual ~SLAImportJobView() = default;

    // [INTENT] Gets the user's selection for what to import.
    virtual Sel get_selection() const = 0;
    // [INTENT] Gets the window size for the marching squares algorithm, based on the quality setting.
    virtual Vec2i32 get_marchsq_windowsize() const = 0;
    // [INTENT] Gets the path to the SLA archive file.
    virtual std::string get_path() const = 0;
};

class Plater;

// [INTENT] A background job for importing SLA archives. It uses the pimpl idiom
// to hide its implementation details.
class SLAImportJob : public Job
{
    class priv;

    // [STATE] A pointer to the private implementation, holding the job's state.
    std::unique_ptr<priv> p;
    using Sel = SLAImportJobView::Sel;

public:
    // [INTENT] Prepares the job by getting data from the view.
    // [THREAD] This is called on the main UI thread before the worker thread starts.
    void prepare();
    // [INTENT] The main worker method that performs the import.
    // [THREAD] This runs on a worker thread.
    void process(Ctl& ctl) override;
    // [INTENT] Finalizes the job on the main thread, applying the results.
    // [THREAD] This runs on the main UI thread.
    void finalize(bool canceled, std::exception_ptr&) override;

    // [INTENT] Constructs the job with a view from which to get the import settings.
    SLAImportJob(const SLAImportJobView*);
    ~SLAImportJob();

    // [INTENT] Resets the job's state so it can be reused.
    void reset();
};

}} // namespace Slic3r::GUI

#endif // SLAIMPORTJOB_HPP
