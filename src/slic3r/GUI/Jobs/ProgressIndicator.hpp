// [INTENT]
// This header defines the `ProgressIndicator` class, which is a pure virtual
// interface (an abstract base class) for any UI element that can display
// progress. It provides a generic contract for updating progress, setting status
// text, and handling cancellation. This allows background jobs to report
// progress without being tightly coupled to a specific UI implementation (e.g.,
// a status bar, a progress dialog).
//
// [UNITY]
// In Unity, this interface would be replaced by a C# interface (e.g.,
// `IProgressReporter`) with methods like `UpdateProgress(float progress)`,
// `SetStatus(string status)`, and an event like `OnCancelRequested`. UI elements
// like progress bars would implement this interface, and background tasks
// would be given a reference to it to report their progress.
//
// [PORTING_HAZARD:P3]
// The `show_error_info` method uses `wxString` which will require a conversion
// layer when mapping to Unity's `string` (which is UTF-16).

#ifndef IPROGRESSINDICATOR_HPP
#define IPROGRESSINDICATOR_HPP

#include <string>
#include <functional>
#include <wx/string.h>

namespace Slic3r {

/**
 * @brief Generic progress indication interface.
 */
class ProgressIndicator
{
public:
    /// Cancel callback function type
    using CancelFn = std::function<void()>;

    virtual ~ProgressIndicator() = default;

    // [INTENT] Clears the percentage display from the progress indicator.
    virtual void clear_percent() = 0;
    // [INTENT] Shows an error message in the progress indicator.
    virtual void show_error_info(wxString msg, int code, wxString description, wxString extra) = 0;
    // [INTENT] Sets the maximum value for the progress range (e.g., 100 for percentage).
    virtual void set_range(int range) = 0;
    // [INTENT] Sets a callback function to be called when the user requests to cancel the operation.
    virtual void set_cancel_callback(CancelFn = CancelFn()) = 0;
    // [INTENT] Sets the current progress value.
    virtual void set_progress(int pr) = 0;
    // [INTENT] Sets the status text message to be displayed.
    virtual void set_status_text(const char*) = 0; // utf8 char array
    // [INTENT] Gets the maximum value of the progress range.
    virtual int get_range() const = 0;
};

} // namespace Slic3r

#endif // IPROGRESSINDICATOR_HPP
