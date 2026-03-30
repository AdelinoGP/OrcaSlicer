#ifndef slic3r_FlashAir_hpp_
#define slic3r_FlashAir_hpp_

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"


// [INTENT] Toshiba FlashAir SD card upload protocol implementation.
// [UNITY] Port as a C# class implementing IPrintHost, using UnityWebRequest.
// Replace synchronous calls with async Task/await.
namespace Slic3r {

class DynamicPrintConfig;
class Http;

class FlashAir : public PrintHost
{
public:
	FlashAir(DynamicPrintConfig *config);
	~FlashAir() override = default;

	const char* get_name() const override;

	bool test(wxString &curl_msg) const override;
	wxString get_test_ok_msg() const override;
	wxString get_test_failed_msg(wxString &msg) const override;
	bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn, InfoFn info_fn) const override;
	bool has_auto_discovery() const override { return false; }
	bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return {}; }
	std::string get_host() const override { return host; }
    
	// [STATE] stores host IP or hostname
private:
	// [PORTING_HAZARD:P2] FlashAir is not a printer-native API but an SD-card CGI protocol with FAT-specific assumptions, so
	// ports must preserve its URL and timestamp quirks exactly or uploads become card-firmware dependent.
	std::string host;

	std::string timestamp_str() const;
	std::string make_url(const std::string &path) const;
	std::string make_url(const std::string &path, const std::string &arg, const std::string &val) const;
};

}

#endif
