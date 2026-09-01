// PNP fork: the runner half of the support-preview overlay (see header).
// Parsing and mesh building live in PnpSupportPreviewDoc.cpp, which stays
// GUI-free so tests/pnp can compile it directly.

#include "PnpSupportPreview.hpp"

#include "PnpBackend.hpp"
#include "PnpSlicingProcess.hpp" // pnp_job_object()

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/process.hpp>

#ifdef _WIN32
#include <windows.h>
#include <boost/process/windows.hpp>
#endif

namespace bp = boost::process;

namespace Slic3r { namespace GUI {

PnpSupportPreviewRun run_support_preview(const boost::filesystem::path &model_3mf,
                                         double                         fallback_layer_height_mm,
                                         const std::atomic<bool>       &cancel)
{
	PnpSupportPreviewRun result;

	PnpBackend &backend = PnpBackend::get();
	if (!backend.available()) {
		result.error = "the pnp_cli backend is not available";
		return result;
	}

	const boost::filesystem::path out_path = model_3mf.parent_path() / "support-preview.json";

	const std::vector<std::string> args {
		"support-preview",
		"--input",      model_3mf.string(),
		"--module-dir", backend.module_dir().string(),
		"--output",     out_path.string(),
	};
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << backend.cli_path().string()
	                        << " support-preview --input " << model_3mf.string()
	                        << " --module-dir " << backend.module_dir().string()
	                        << " --output " << out_path.string();

	int         exit_code = -1;
	std::string stderr_tail;
	try {
		bp::ipstream err_stream;
		bp::child    child(backend.cli_path().string(), bp::args(args),
		                   bp::std_out > bp::null, bp::std_err > err_stream, bp::std_in < bp::null,
#ifdef _WIN32
		                   bp::windows::create_no_window,
#endif
		                   bp::limit_handles);
#ifdef _WIN32
		if (HANDLE job = static_cast<HANDLE>(pnp_job_object()); job != nullptr)
			if (!::AssignProcessToJobObject(job, child.native_handle()))
				BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": AssignProcessToJobObject failed, error "
				                           << ::GetLastError();
#endif
		while (child.running()) {
			if (cancel) {
				std::error_code ec;
				child.terminate(ec);
				BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": canceled, child terminated";
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if (cancel) {
			// A cancel is not a failure: no error text, so the caller stays
			// quiet rather than raising a notification.
			boost::system::error_code remove_ec;
			boost::filesystem::remove(out_path, remove_ec);
			return result;
		}
		child.wait();
		exit_code = child.exit_code();
		// pnp's diagnostics are the only clue when the run fails; keep the
		// tail, not the whole stream (the DAG warnings alone run to dozens).
		for (std::string line; std::getline(err_stream, line);) {
			stderr_tail += line;
			stderr_tail += '\n';
			if (stderr_tail.size() > 4096)
				stderr_tail.erase(0, stderr_tail.size() - 4096);
		}
	} catch (const std::exception &ex) {
		result.error = std::string("failed to run pnp_cli support-preview: ") + ex.what();
		return result;
	}

	if (exit_code != 0) {
		result.error = "pnp_cli support-preview exited with code " + std::to_string(exit_code);
		if (!stderr_tail.empty())
			result.error += ": " + stderr_tail;
		return result;
	}

	boost::nowide::ifstream in(out_path.string().c_str(), std::ios::binary);
	if (!in) {
		result.error = "support-preview produced no output at " + out_path.string();
		return result;
	}
	const std::string text { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
	in.close();

	const PnpSupportPreviewParse parsed = parse_support_preview(text);
	if (!parsed.ok) {
		result.error = parsed.error;
		return result;
	}

	result.layer_count     = parsed.doc.layers.size();
	result.expolygon_count = parsed.doc.expolygon_count();
	result.meshes          = build_support_preview_meshes(parsed.doc, fallback_layer_height_mm);
	result.ok              = true;

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << result.layer_count << " layers, "
	                        << result.expolygon_count << " expolygons, "
	                        << result.meshes.body.facets_count() << " body facets, "
	                        << result.meshes.interface_mesh.facets_count() << " interface facets";
	return result;
}

} } // namespace Slic3r::GUI
