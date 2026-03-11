#ifndef slic3r_Duet_hpp_
#define slic3r_Duet_hpp_

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"

namespace Slic3r {

class DynamicPrintConfig;
class Http;

class Duet : public PrintHost
{
public:
    explicit Duet(DynamicPrintConfig *config);
	~Duet() override = default;

	const char* get_name() const override;

	bool test(wxString &curl_msg) const override;
	wxString get_test_ok_msg() const override;
	wxString get_test_failed_msg(wxString &msg) const override;
	bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
	bool has_auto_discovery() const override { return false; }
	bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint | PrintHostPostUploadAction::StartSimulation; }
	std::string get_host() const override { return host; }
   
private:
	// [INTENT] Duet has two incompatible control planes (legacy RRF and newer DSF), so the adapter records which
	// one answered during connect() and routes every later upload/start call through that branch.
	enum class ConnectionType { rrf, dsf, error };
	// [STATE] Connection credentials are copied out of PrintConfig once because background uploads must run against
	// a stable host/password snapshot even if the user changes settings mid-queue.
	std::string host;
	std::string password;

	std::string get_upload_url(const std::string &filename, ConnectionType connectionType) const;
	std::string get_connect_url(const bool dsfUrl) const;
	std::string get_base_url() const;
	std::string timestamp_str() const;
	ConnectionType connect(wxString &msg) const;
	void disconnect(ConnectionType connectionType) const;
	bool start_print(wxString &msg, const std::string &filename, ConnectionType connectionType, bool simulationMode) const;
	int get_err_code_from_body(const std::string &body) const;
};

}

#endif
