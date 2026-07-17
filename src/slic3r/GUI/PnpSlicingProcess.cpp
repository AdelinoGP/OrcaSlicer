#include "PnpSlicingProcess.hpp"

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "I18N.hpp"
#include "format.hpp"

#include "PnpBackend.hpp"
#include "PnpModelExport.hpp"
#include "PnpProgress.hpp"
#include "PnpConfigTranslator.hpp"
#include "PnpConfigWarningsLog.hpp"

#include <wx/app.h>
#include <wx/stdpaths.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/process.hpp>
#ifdef _WIN32
#include <boost/process/windows.hpp>
#endif

namespace Slic3r {
namespace GUI {

namespace {

// Keep only the last `cap` bytes of the raw stderr stream for error reports.
void append_capped_tail(std::string &tail, const std::string &line, size_t cap = 4096)
{
	tail.append(line).append("\n");
	if (tail.size() > cap)
		tail.erase(0, tail.size() - cap);
}

} // anonymous namespace

PnpSlicingProcess::~PnpSlicingProcess()
{
	this->stop();
	if (m_thread.joinable())
		m_thread.join();
}

bool PnpSlicingProcess::can_switch_print()
{
	// Mirrors BSP: while a slice is executing the plate must not be switched.
	if (m_state == STATE_RUNNING || m_state == STATE_STARTED) {
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": slicing plate's plate_id %1%, on slicing, can not switch print")
		                               % (m_current_plate != nullptr ? m_current_plate->get_index() : -1);
		return false;
	}
	return true;
}

bool PnpSlicingProcess::select_technology(PrinterTechnology tech)
{
	bool changed = false;
	if (m_printer_tech != tech) {
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": change the printer technology from %1% to %2%") % m_printer_tech % tech;
		m_printer_tech = tech;
		if (m_print != nullptr)
			this->reset();
		changed = true;
	}
	// The pnp backend is FFF only; SLA was dropped with BSP (design ticket 003).
	assert(tech == ptFFF);
	m_print = m_fff_print;
	assert(m_print != nullptr);
	return changed;
}

std::string PnpSlicingProcess::output_filepath_for_project(const boost::filesystem::path &project_path)
{
	assert(m_print != nullptr);
	if (project_path.empty())
		return m_print->output_filepath("");
	return m_print->output_filepath(project_path.parent_path().string(), project_path.stem().string());
}

int PnpSlicingProcess::estimate_layer_count() const
{
	double max_height_mm = 0.;
	if (m_fff_print != nullptr)
		for (const PrintObject *object : m_fff_print->objects())
			max_height_mm = std::max(max_height_mm, double(object->height()) * SCALING_FACTOR);
	double layer_height = 0.;
	if (m_print != nullptr && m_print->full_print_config().has("layer_height"))
		layer_height = m_print->full_print_config().opt_float("layer_height");
	if (layer_height <= 0.)
		layer_height = 0.2;
	return std::max(1, int(max_height_mm / layer_height));
}

bool PnpSlicingProcess::start()
{
	assert(m_print != nullptr && m_print == m_fff_print);
	if (m_print->empty()) {
		if (m_current_plate == nullptr || !m_current_plate->is_slice_result_valid())
			// The print is empty (no object in Model, or all objects are out of the print bed).
			return false;
	}
	if (this->running())
		// A previous slice is running or its result has not been picked up via stop() yet.
		return false;
	if (m_current_plate == nullptr) {
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": no current plate set, cannot slice";
		return false;
	}

	PnpBackend &backend = PnpBackend::get();
	if (!backend.available()) {
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": pnp backend unavailable: " << backend.failure_reason();
		backend.show_failure_notification();
		return false;
	}

	// Join the (already finished) worker of the previous slice, if any.
	if (m_thread.joinable())
		m_thread.join();

	SliceJob job;
	job.plate_idx             = m_current_plate->get_index();
	job.output_path           = m_current_plate->get_tmp_gcode_path();
	job.estimated_layer_count = this->estimate_layer_count();

	// Already-sliced reuse: skip the subprocess and go straight to finalize.
	job.reuse = m_current_plate->is_slice_result_valid()
	         && boost::filesystem::exists(job.output_path)
	         && m_last_apply_status == PrintBase::APPLY_STATUS_UNCHANGED;

	if (!job.reuse) {
		// Prepare the pnp_cli inputs synchronously on the UI thread: the plate
		// 3MF export reads live Plater model state (F05 contract), and the
		// config translation reads the preset bundle.
		boost::filesystem::path temp_root(wxStandardPaths::Get().GetTempDir().utf8_str().data());
		job.input_dir = temp_root / boost::filesystem::unique_path(
			(boost::format("pnp-slice-%1%-plate%2%-%%%%%%%%") % get_current_pid() % (job.plate_idx + 1)).str());
		boost::system::error_code ec;
		boost::filesystem::create_directories(job.input_dir, ec);
		if (ec) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to create pnp input directory "
			                         << job.input_dir.string() << ": " << ec.message();
			return false;
		}
		job.model_path  = job.input_dir / "model.3mf";
		job.config_path = job.input_dir / "config.json";

		std::string export_error;
		if (!export_plate_3mf_for_pnp(job.plate_idx, job.model_path, &export_error)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": plate 3MF export failed: " << export_error;
			boost::filesystem::remove_all(job.input_dir, ec);
			return false;
		}

		const DynamicPrintConfig &full_config = m_print->full_print_config();
		PnpTranslationResult      translated  = PnpConfigTranslator::translate(full_config);
		// F03: dev-instrument sink for unmapped/lossy keys.
		log_pnp_config_warnings(full_config, std::move(translated.warnings), job.plate_idx);
		try {
			boost::nowide::ofstream config_file(job.config_path.string().c_str(), std::ios::binary | std::ios::trunc);
			config_file << translated.json.dump(2);
			config_file.close();
			if (!config_file)
				throw std::runtime_error("write failed");
		} catch (const std::exception &ex) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to write " << job.config_path.string() << ": " << ex.what();
			boost::filesystem::remove_all(job.input_dir, ec);
			return false;
		}
	}

	{
		std::unique_lock<std::mutex> lck(m_mutex);
		m_state              = STATE_STARTED;
		m_canceled           = false;
		m_internal_cancelled = false;
	}
	m_temp_output_path = job.output_path;
	// Let Print::apply() cancel the running pnp slice when it invalidates data,
	// exactly like BSP wires its stop_internal().
	m_print->set_cancel_callback([this]() { this->stop_internal(); });

	m_thread = create_thread([this, job]() { this->worker_main(job); });
	return true;
}

bool PnpSlicingProcess::stop()
{
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", enter";
	if (m_state == STATE_INITIAL)
		return false;
	bool result = this->cancel_and_join(false);
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", exit";
	return result;
}

void PnpSlicingProcess::stop_internal()
{
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", enter";
	if (m_state == STATE_INITIAL || m_state == STATE_IDLE)
		return;
	this->cancel_and_join(true);
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", exit";
}

bool PnpSlicingProcess::cancel_and_join(bool internal)
{
	namespace bp = boost::process;
	{
		std::unique_lock<std::mutex> lck(m_mutex);
		if (m_state == STATE_STARTED || m_state == STATE_RUNNING) {
			if (internal)
				m_internal_cancelled = true;
			m_canceled = true;
			if (m_child != nullptr) {
				std::error_code ec;
				m_child->terminate(ec);
				if (ec)
					BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": terminate failed: " << ec.message();
			}
		}
	}
	// Join outside the lock; the worker takes m_mutex to update state / m_child.
	if (m_thread.joinable())
		m_thread.join();
	{
		std::unique_lock<std::mutex> lck(m_mutex);
		m_state = STATE_IDLE;
	}
	if (m_print != nullptr)
		m_print->set_cancel_callback([]() {});
	return true;
}

bool PnpSlicingProcess::reset()
{
	bool stopped = this->stop();
	this->reset_export();
	return stopped;
}

void PnpSlicingProcess::worker_main(SliceJob job)
{
	set_current_thread_name("pnp_SlcPcs");

	std::exception_ptr exception;
	try {
		if (job.reuse) {
			std::unique_lock<std::mutex> lck(m_mutex);
			m_state = STATE_RUNNING;
		} else {
			this->run_pnp_cli(job); // sets STATE_RUNNING once the child is spawned
		}

		if (!m_canceled) {
			// SEAM (ticket F07 / B3): fill the plate's GCodeProcessorResult here
			// by parsing job.output_path (Plater::load_gcode ->
			// GCodeProcessor::process_file is the proven ingredient). Until F07
			// lands, completion is posted with the gcode at
			// PartPlate::get_tmp_gcode_path() only.

			// Let the G-code viewer know slicing proper is done (same event BSP
			// posts before its G-code export phase; the int payload is unused by
			// Plater::priv::on_slicing_completed).
			wxCommandEvent evt(m_event_slicing_completed_id);
			evt.SetInt(0);
			wxQueueEvent(wxGetApp().mainframe->m_plater, evt.Clone());

			if (!m_export_path.empty()) {
				wxQueueEvent(wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_export_began_id));
				this->finalize_export();
			} else if (!m_upload_job.empty()) {
				wxQueueEvent(wxGetApp().mainframe->m_plater, new wxCommandEvent(m_event_export_began_id));
				this->prepare_upload();
			} else {
				m_print->set_status(100, _u8L("Slicing complete"));
			}
		}
	} catch (...) {
		exception = std::current_exception();
	}

	const bool canceled = m_canceled;
	if (canceled) {
		// Delete the partial output G-code; pnp_cli is stateless, a rerun is cheap.
		boost::system::error_code ec;
		boost::filesystem::remove(job.output_path, ec);
	} else if (!exception && !job.reuse) {
		// Success: the per-slice inputs are no longer needed. Retained on failure for diagnosis.
		boost::system::error_code ec;
		boost::filesystem::remove_all(job.input_dir, ec);
	}

	{
		std::unique_lock<std::mutex> lck(m_mutex);
		m_state = canceled ? STATE_CANCELED : STATE_FINISHED;
	}

	if (canceled && m_internal_cancelled) {
		// Canceled from Print::apply(): the UI thread is mutating the model and
		// must not be notified (mirrors BSP's CANCELED_INTERNAL branch).
		return;
	}
	SlicingProcessCompletedEvent evt(m_event_finished_id, 0,
		canceled  ? SlicingProcessCompletedEvent::Cancelled :
		exception ? SlicingProcessCompletedEvent::Error : SlicingProcessCompletedEvent::Finished,
		exception);
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": send SlicingProcessCompletedEvent to main, status %1%") % evt.status();
	wxQueueEvent(wxGetApp().mainframe->m_plater, evt.Clone());
}

void PnpSlicingProcess::run_pnp_cli(const SliceJob &job)
{
	namespace bp = boost::process;
	namespace fs = boost::filesystem;

	PnpBackend &backend = PnpBackend::get();
	// --instrument-stderr is required for any progress events: the current
	// pnp_cli emits no JSONL stream by default (docs/09's default-on stream is
	// not shipped yet), only human-readable log lines that the parser skips.
	const std::vector<std::string> args {
		"slice",
		"--model",      job.model_path.string(),
		"--config",     job.config_path.string(),
		"--module-dir", backend.module_dir().string(),
		"--output",     job.output_path,
		"--instrument-stderr",
	};
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << backend.cli_path().string()
	                        << " slice --model " << job.model_path.string()
	                        << " --config " << job.config_path.string()
	                        << " --module-dir " << backend.module_dir().string()
	                        << " --output " << job.output_path
	                        << " --instrument-stderr";

	PnpProgressParser parser(job.estimated_layer_count);
	parser.set_update_callback([this](int percent, const std::string &text) {
		if (m_canceled)
			return;
		// PartPlate wired the Print status callback to queue a SlicingStatusEvent
		// (EVT_SLICING_UPDATE) to the Plater — the exact channel BSP uses.
		m_print->set_status(std::min(percent, 99), text);
	});

	std::string stderr_tail;
	int         exit_code = -1;
	try {
		bp::ipstream err_stream;
		bp::child    child(backend.cli_path().string(), bp::args(args),
		                   bp::std_out > bp::null, bp::std_err > err_stream, bp::std_in < bp::null,
#ifdef _WIN32
		                   bp::windows::create_no_window,
#endif
		                   bp::limit_handles);
		// Clear m_child before `child` is destroyed on every exit path (incl.
		// exceptions), so stop() can never terminate a dangling pointer.
		struct ChildGuard {
			PnpSlicingProcess *self;
			~ChildGuard() { std::unique_lock<std::mutex> lck(self->m_mutex); self->m_child = nullptr; }
		} child_guard { this };
		{
			std::unique_lock<std::mutex> lck(m_mutex);
			if (m_canceled) {
				// stop() raced the spawn and could not terminate a child that did
				// not exist yet; do it ourselves.
				std::error_code ec;
				child.terminate(ec);
			} else {
				m_child = &child;
				m_state = STATE_RUNNING;
			}
		}
		// Pump the JSONL progress stream. Blocking reads are fine: this thread
		// has nothing else to do until the child exits, and the pipe reaches
		// EOF when it does (including after terminate()).
		std::string line;
		while (err_stream && std::getline(err_stream, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			append_capped_tail(stderr_tail, line);
			parser.feed_line(line);
		}
		parser.finish();
		child.wait();
		exit_code = child.exit_code();
	} catch (const std::exception &ex) {
		throw Slic3r::SlicingError(Slic3r::format(_u8L("Failed to run the PNP slicer %1%: %2%"),
		                                          backend.cli_path().string(), ex.what()));
	}

	if (m_canceled)
		// Terminated by stop()/stop_internal(); the caller handles cleanup.
		return;

	auto with_tail = [&stderr_tail](std::string msg) {
		if (!stderr_tail.empty())
			msg += "\n\n" + _u8L("PNP slicer output (tail):") + "\n" + stderr_tail;
		return msg;
	};
	if (exit_code != 0) {
		std::string msg = parser.has_fatal_error()
			? parser.fatal_error_message()
			: Slic3r::format(_u8L("The PNP slicer exited with code %1%."), exit_code);
		throw Slic3r::SlicingError(with_tail(std::move(msg)));
	}
	if (parser.has_fatal_error())
		throw Slic3r::SlicingError(with_tail(parser.fatal_error_message()));
	if (!fs::exists(fs::path(job.output_path)))
		throw Slic3r::SlicingError(with_tail(
			Slic3r::format(_u8L("The PNP slicer reported success but produced no G-code at %1%."), job.output_path)));

	if (!parser.warnings().empty())
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": pnp_cli reported " << parser.warnings().size()
		                           << " degraded-slice warning(s); aggregation UX is ticket F08";
}

// Copy the temporary G-code to the user-selected export location.
// SEAM: post-processing scripts (run_post_process_scripts) are intentionally
// not run on pnp output in v1.
void PnpSlicingProcess::finalize_export()
{
	std::string export_path = m_fff_print->print_statistics().finalize_output_path(m_export_path);
	std::string output_path = m_temp_output_path;

	std::string error_message;
	int         copy_ret_val = CopyFileResult::SUCCESS;
	try {
		copy_ret_val = copy_file(output_path, export_path, error_message, m_export_path_on_removable_media);
	} catch (...) {
		throw Slic3r::ExportError(_u8L("Unknown error occurred during exporting G-code."));
	}
	switch (copy_ret_val) {
	case CopyFileResult::SUCCESS: break; // no error
	case CopyFileResult::FAIL_COPY_FILE:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code to the output G-code failed. Maybe the SD card is write locked?\nError message: %1%"), error_message));
	case CopyFileResult::FAIL_FILES_DIFFERENT:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code to the output G-code failed. There might be problem with target device, please try exporting again or using different device. The corrupted output G-code is at %1%.tmp."), export_path));
	case CopyFileResult::FAIL_RENAMING:
		throw Slic3r::ExportError(GUI::format(_L("Renaming of the G-code after copying to the selected destination folder has failed. Current path is %1%.tmp. Please try exporting again."), export_path));
	case CopyFileResult::FAIL_CHECK_ORIGIN_NOT_OPENED:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code has finished but the original code at %1% couldn't be opened during copy check. The output G-code is at %2%.tmp."), output_path, export_path));
	case CopyFileResult::FAIL_CHECK_TARGET_NOT_OPENED:
		throw Slic3r::ExportError(GUI::format(_L("Copying of the temporary G-code has finished but the exported code couldn't be opened during copy check. The output G-code is at %1%.tmp."), export_path));
	default:
		throw Slic3r::ExportError(_u8L("Unknown error occurred during exporting G-code."));
	}

	auto evt = new wxCommandEvent(m_event_export_finished_id, wxGetApp().mainframe->m_plater->GetId());
	evt->SetString(wxString::FromUTF8(export_path.c_str(), export_path.length()));
	wxQueueEvent(wxGetApp().mainframe->m_plater, evt);

	m_print->set_status(100, GUI::format(_L("G-code file exported to %1%"), export_path));
}

// A print host upload job has been scheduled, enqueue it to the printhost job queue.
void PnpSlicingProcess::prepare_upload()
{
	boost::filesystem::path source_path = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("." SLIC3R_APP_KEY ".upload.%%%%-%%%%-%%%%-%%%%");

	if (m_upload_job.upload_data.use_3mf) {
		source_path = m_upload_job.upload_data.source_path;
	} else {
		std::string error_message;
		if (copy_file(m_temp_output_path, source_path.string(), error_message) != SUCCESS)
			throw Slic3r::RuntimeError(_u8L("Copying of the temporary G-code to the output G-code failed."));
		// NOTE: pnp does not fill Print's print_statistics (Print::process()
		// never runs), so statistic placeholders in the upload filename resolve
		// to their defaults. Acceptable for v1.
		m_upload_job.upload_data.upload_path = m_fff_print->print_statistics().finalize_output_path(m_upload_job.upload_data.upload_path.string());
	}

	m_print->set_status(100, GUI::format(_L("Scheduling upload to `%1%`. See Window -> Print Host Upload Queue"), m_upload_job.printhost->get_host()));

	m_upload_job.upload_data.source_path = std::move(source_path);
	wxGetApp().printhost_job_queue().enqueue(std::move(m_upload_job));
}

bool PnpSlicingProcess::empty() const
{
	assert(m_print != nullptr);
	return m_print->empty();
}

StringObjectException PnpSlicingProcess::validate(std::vector<StringObjectException> *warnings,
                                                  Polygons *collison_polygons,
                                                  std::vector<std::pair<Polygon, float>> *height_polygons)
{
	assert(m_print != nullptr && m_print == m_fff_print);
	m_fff_print->is_BBL_printer() = wxGetApp().preset_bundle->is_bbl_vendor();
	return m_print->validate(warnings, collison_polygons, height_polygons);
}

// Apply config over the print. Pure change detection — identical to BSP; the
// resulting Print state feeds pnp reuse decisions and plate invalidation, but
// Print::process() is never called.
PrintBase::ApplyStatus PnpSlicingProcess::apply(const Model &model, const DynamicPrintConfig &config)
{
	assert(m_print != nullptr);
	assert(config.opt_enum<PrinterTechnology>("printer_technology") == m_print->technology());
	DynamicPrintConfig new_config = config;
	new_config.apply(*m_current_plate->config());
	PrintBase::ApplyStatus invalidated = m_print->apply(model, new_config);

	// Orca: prevent resetting under gcode viewer mode
	if (invalidated != PrintBase::APPLY_STATUS_UNCHANGED) {
		const auto plater = wxGetApp().mainframe->m_plater;
		if (plater && plater->only_gcode_mode())
			invalidated = PrintBase::APPLY_STATUS_UNCHANGED;
	}

	if ((invalidated & PrintBase::APPLY_STATUS_INVALIDATED) != 0 && m_print->technology() == ptFFF &&
	    !m_fff_print->is_step_done(psGCodeExport)) {
		// Let the G-code preview UI know that the final G-code preview is not valid.
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": invalidated gcode result %1%, will reset soon") % m_gcode_result;
		if (m_gcode_result != nullptr)
			m_gcode_result->reset();
	}
	m_last_apply_status = invalidated;
	return invalidated;
}

void PnpSlicingProcess::set_task(const PrintBase::TaskParams &params)
{
	assert(m_print != nullptr);
	m_print->set_task(params);
}

void PnpSlicingProcess::schedule_export(const std::string &path, bool export_path_on_removable_media)
{
	assert(m_export_path.empty());
	if (!m_export_path.empty())
		return;
	std::unique_lock<std::mutex> lck(m_mutex);
	m_export_path                    = path;
	m_export_path_on_removable_media = export_path_on_removable_media;
}

void PnpSlicingProcess::schedule_upload(Slic3r::PrintHostJob upload_job)
{
	assert(m_export_path.empty());
	if (!m_export_path.empty())
		return;
	std::unique_lock<std::mutex> lck(m_mutex);
	m_export_path.clear();
	m_upload_job = std::move(upload_job);
}

void PnpSlicingProcess::reset_export()
{
	assert(!this->running());
	if (!this->running()) {
		m_export_path.clear();
		m_export_path_on_removable_media = false;
	}
}

} // namespace GUI
} // namespace Slic3r
