#ifndef slic3r_GUI_SlicingProcessEvents_hpp_
#define slic3r_GUI_SlicingProcessEvents_hpp_

// PNP fork (wayfinder ticket F13): the wx event classes that the slicing
// process posts to the Plater used to live in BackgroundSlicingProcess.hpp.
// BSP is deleted with the native FFF pipeline; PnpSlicingProcess reuses these
// event types unchanged, so they graduate into their own small header rather
// than dying with BSP. No slicing dependency — just wx + PrintBase::SlicingStatus.

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <wx/event.h>

#include "libslic3r/PrintBase.hpp"

namespace Slic3r {

// PNP fork (ADR-0002): one snapshot of the per-layer slice progress, posted
// from the slicing worker thread to the Plater (and forwarded to the 3D
// canvas). `status` holds one LayerStatus (PnpProgress.hpp) per layer; the
// canvas maps them to colors and, on failure/cancel, recolors pending and
// in-progress layers to its UI-only "failed" state.
struct LayerStatusSnapshot
{
    enum Result : int
    {
        Running  = 0, // slice in progress; active == true
        Success  = 1, // slice finished; visualization reverts
        Failed   = 2, // slice failed; visualization freezes with failed layers
        Canceled = 3, // slice canceled; visualization freezes with failed layers
    };

    bool   active { false };
    Result result { Running };
    // Plate being sliced; the canvas colors only volumes on it (slice-all
    // shows every plate at once). -1 = unknown (color everything).
    int    plate_idx { -1 };
    int    layer_count { 0 };
    float  z_min { 0.f };
    float  z_max { 0.f };
    std::vector<uint8_t> status;
};

class SlicingStatusEvent : public wxEvent
{
public:
	SlicingStatusEvent(wxEventType eventType, int winid, const PrintBase::SlicingStatus &status) :
		wxEvent(winid, eventType), status(std::move(status)) {}
	virtual wxEvent *Clone() const { return new SlicingStatusEvent(*this); }

	PrintBase::SlicingStatus status;
};

class SlicingLayerStatusEvent : public wxEvent
{
public:
	SlicingLayerStatusEvent(wxEventType eventType, int winid, LayerStatusSnapshot snapshot) :
		wxEvent(winid, eventType), snapshot(std::move(snapshot)) {}
	virtual wxEvent *Clone() const { return new SlicingLayerStatusEvent(*this); }

	LayerStatusSnapshot snapshot;
};

class SlicingProcessCompletedEvent : public wxEvent
{
public:
	enum StatusType {
		Finished,
		Cancelled,
		Error
	};

	SlicingProcessCompletedEvent(wxEventType eventType, int winid, StatusType status, std::exception_ptr exception) :
		wxEvent(winid, eventType), m_status(status), m_exception(exception) {}
	virtual wxEvent* Clone() const { return new SlicingProcessCompletedEvent(*this); }

	StatusType 	status()    const { return m_status; }
	bool 		finished()  const { return m_status == Finished; }
	bool 		success()   const { return m_status == Finished; }
	bool 		cancelled() const { return m_status == Cancelled; }
	bool		error() 	const { return m_status == Error; }
	// Unhandled error produced by stdlib or a Win32 structured exception, or unhandled Slic3r's own critical exception.
	bool 		critical_error() const;
	// Critical errors does invalidate plater except CopyFileError.
	bool        invalidate_plater() const;
	// Only valid if error()
	void 		rethrow_exception() const { assert(this->error()); assert(m_exception); std::rethrow_exception(m_exception); }
	// Produce a human readable message to be displayed by a notification or a message box.
	// 2nd parameter defines whether the output should be displayed with a monospace font.
    std::pair<std::string, std::vector<size_t>> format_error_message() const;

private:
	StatusType 			m_status;
	std::exception_ptr 	m_exception;
};

}; // namespace Slic3r

#endif /* slic3r_GUI_SlicingProcessEvents_hpp_ */
