#ifndef slic3r_GUI_PnpSlicingProcess_hpp_
#define slic3r_GUI_PnpSlicingProcess_hpp_

// PNP fork (wayfinder ticket F04): drop-in replacement for
// BackgroundSlicingProcess that slices through the external `pnp_cli`
// subprocess instead of Print::process() (design: wayfinder ticket 003).
//
// The public surface mirrors BSP minus all SLA paths, so Plater's call sites
// reduce to a type rename (the swap itself is ticket F09; BSP stays untouched
// until then). `Print::apply()` is retained purely as change detection /
// config holder — Print::process() is never called.
//
// Threading model: no persistent worker thread, no condition-variable state
// machine. start() prepares the pnp_cli inputs synchronously on the UI thread
// (per-slice temp subdir with the exported plate 3MF + config.json), then
// spawns one worker thread per slice which launches pnp_cli, pumps its stderr
// JSONL progress stream through PnpProgressParser, waits, and posts the same
// wx events BSP posts (SlicingStatusEvent via the Print status callback,
// wxCommandEvent for slicing-completed / export-began / export-finished,
// SlicingProcessCompletedEvent with an exception_ptr on failure).
//
// States: IDLE -> STARTED at start(), RUNNING once the child is spawned,
// FINISHED / CANCELED at thread end. v1 cancel is child.terminate() + delete
// of the partial output G-code; an internal cancel (from Print::apply() via
// the cancel callback) posts no completion event, matching BSP.

#include <atomic>
#include <mutex>
#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/thread.hpp>

#include <wx/event.h>

#include "libslic3r/PrintBase.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "PartPlate.hpp"
// Reuse the slicing-process event classes (SlicingStatusEvent,
// SlicingProcessCompletedEvent) so Plater's existing handlers survive the swap.
#include "SlicingProcessEvents.hpp"

namespace boost { namespace process { class child; } }

namespace Slic3r {

class DynamicPrintConfig;
class Model;

namespace GUI {

class PnpSlicingProcess
{
public:
	PnpSlicingProcess() = default;
	// Stop any running slice and join the worker thread.
	~PnpSlicingProcess();

	void set_fff_print(Print *print) { m_fff_print = print; m_print = print; }
	void set_gcode_result(GCodeProcessorResult *result) { m_gcode_result = result; }

	//BBS-compatible partplate related logic (mirrors BSP).
	bool switch_print_preprocess() { return true; }
	bool can_switch_print();
	void set_current_plate(GUI::PartPlate *plate) { m_current_plate = plate; }
	GUI::PartPlate       *get_current_plate() { return m_current_plate; }
	GCodeProcessorResult *get_current_gcode_result() { return m_gcode_result; }

	// wxCommandEvent ids posted asynchronously to the Plater (same ids /
	// semantics as BSP; see Plater::priv EVT_* bindings).
	void set_slicing_completed_event(int event_id) { m_event_slicing_completed_id = event_id; }
	void set_finished_event(int event_id) { m_event_finished_id = event_id; }
	void set_export_began_event(int event_id) { m_event_export_began_id = event_id; }
	void set_export_finished_event(int event_id) { m_event_export_finished_id = event_id; }

	// FFF only; asserts on anything else. Returns true if the technology changed.
	bool                select_technology(PrinterTechnology tech);
	PrinterTechnology   current_printer_technology() const { return m_printer_tech; }
	const PrintBase    *current_print() const { return m_print; }
	const Print        *fff_print() const { return m_fff_print; }
	Print              *fff_print() { return m_fff_print; }
	// Take the project path (if provided), extract the name of the project, run it through the
	// macro processor and save it next to the project file. Empty project_path = plain output_filepath().
	std::string         output_filepath_for_project(const boost::filesystem::path &project_path);

	// Start slicing the current plate through pnp_cli. Exports the plate 3MF +
	// config.json synchronously on the UI thread, then spawns the worker.
	// Returns false if a slice is already running or there is nothing to slice.
	bool start();
	// Cancel the running slice (terminate pnp_cli, delete partial output) and
	// join the worker. Returns false if nothing was ever started.
	bool stop();
	// stop() + reset_export().
	bool reset();

	// Apply config over the print — pure change detection, identical to BSP.
	PrintBase::ApplyStatus apply(const Model &model, const DynamicPrintConfig &config);
	void set_task(const PrintBase::TaskParams &params);
	bool empty() const;
	StringObjectException validate(std::vector<StringObjectException> *warnings = nullptr,
	                               Polygons *collison_polygons = nullptr,
	                               std::vector<std::pair<Polygon, float>> *height_polygons = nullptr);

	// Schedule the final G-code copy / print-host upload after the slice.
	void schedule_export(const std::string &path, bool export_path_on_removable_media);
	void schedule_upload(Slic3r::PrintHostJob upload_job);
	void reset_export();
	bool is_export_scheduled() const { return !m_export_path.empty(); }
	bool is_upload_scheduled() const { return !m_upload_job.empty(); }

	enum State {
		// Nothing has been started yet.
		STATE_INITIAL = 0,
		// Ready to start a slice (also the state after stop()).
		STATE_IDLE,
		// start() accepted; inputs are being prepared / worker not spawned yet.
		STATE_STARTED,
		// The pnp_cli child (or reuse worker) is running.
		STATE_RUNNING,
		// Worker finished (success or error); stop() resets to IDLE.
		STATE_FINISHED,
		// Worker was canceled; stop() resets to IDLE.
		STATE_CANCELED,
	};
	State state() const { return m_state; }
	bool  idle() const { return m_state == STATE_IDLE; }
	bool  running() const { return m_state == STATE_STARTED || m_state == STATE_RUNNING || m_state == STATE_FINISHED || m_state == STATE_CANCELED; }
	// Print::finished() can never be true anymore (Print::process() is never
	// called); the pnp equivalent keys off the plate slice-result flag plus a
	// non-empty gcode result, preserving BSP's observable semantics.
	bool  finished() const { return m_current_plate != nullptr && m_current_plate->is_slice_result_valid() && m_gcode_result != nullptr && !m_gcode_result->moves.empty(); }
	bool  is_internal_cancelled() { return m_internal_cancelled; }

	// Plater needs stop_internal(), as with BSP.
	friend class Plater;

private:
	// Everything the worker thread needs, captured on the UI thread in start().
	struct SliceJob
	{
		boost::filesystem::path input_dir;    // per-slice temp subdir (model + config.json)
		boost::filesystem::path model_path;   // exported plate 3MF
		boost::filesystem::path config_path;  // translated flat PNP config
		std::string             output_path;  // PartPlate::get_tmp_gcode_path()
		boost::filesystem::path thumbnail_path; // F14: PNG rendered on the UI thread; empty = no thumbnail
		int                     plate_idx { -1 };
		int                     estimated_layer_count { 1 };
		bool                    reuse { false }; // skip the subprocess, straight to finalize
	};

	// To be called by Print::apply() through the cancel callback: cancel
	// without posting any completion event.
	void stop_internal();
	// Shared cancel path; `internal` suppresses the completion event.
	bool cancel_and_join(bool internal);

	// Worker thread body: spawn/pump/wait pnp_cli, then finalize + post events.
	void worker_main(SliceJob job);
	// Launch pnp_cli and pump its stderr; throws SlicingError on any failure,
	// returns silently when the child was terminated by a cancel.
	void run_pnp_cli(const SliceJob &job);
	// F10: parse the pnp `slice_stats` JSONL event (captured verbatim by the
	// progress parser) into m_fff_print->print_statistics() and compute cost
	// fork-side from the Orca preset. When the event is absent all fields stay
	// 0 and the legend's zero-guards hide the weight/cost rows.
	void apply_slice_stats();
	// Copy the temp G-code to m_export_path (throws ExportError on failure).
	void finalize_export();
	// Enqueue the scheduled print-host upload job.
	void prepare_upload();

	// GUI-side fallback layer count for the progress parser: tallest print
	// object height / layer_height, min 1.
	int  estimate_layer_count() const;

	// Non-owned print objects (FFF only).
	PrintBase                  *m_print = nullptr;
	Print                      *m_fff_print = nullptr;
	GCodeProcessorResult       *m_gcode_result = nullptr;
	GUI::PartPlate             *m_current_plate = nullptr;
	PrinterTechnology           m_printer_tech = ptUnknown;

	// Temporary G-code produced by pnp_cli (= plate's tmp gcode path).
	std::string                 m_temp_output_path;
	// F10: raw JSON of the pnp `slice_stats` event from the last run_pnp_cli()
	// (empty when pnp did not emit one). Written and read on the worker thread.
	std::string                 m_slice_stats_json;
	// Output path provided by the user; once set it cannot be re-set.
	std::string                 m_export_path;
	bool                        m_export_path_on_removable_media = false;
	// Print host upload job to schedule after slicing completes.
	PrintHostJob                m_upload_job;

	// Worker thread of the in-flight slice (one per slice, joined in stop()).
	boost::thread               m_thread;
	// Guards m_state / m_child against the UI thread.
	std::mutex                  m_mutex;
	State                       m_state = STATE_INITIAL;
	// The running pnp_cli child, only valid while the worker owns one; used by
	// stop()/stop_internal() to terminate it. Guarded by m_mutex.
	boost::process::child      *m_child = nullptr;
	std::atomic<bool>           m_canceled { false };
	bool                        m_internal_cancelled = false;

	// Result of the last apply(); part of the slice-reuse condition.
	PrintBase::ApplyStatus      m_last_apply_status = PrintBase::APPLY_STATUS_INVALIDATED;

	// wxWidgets command ids posted to the Plater (same roles as in BSP).
	int                         m_event_slicing_completed_id = 0;
	int                         m_event_finished_id = 0;
	int                         m_event_export_began_id = 0;
	int                         m_event_export_finished_id = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_GUI_PnpSlicingProcess_hpp_ */
