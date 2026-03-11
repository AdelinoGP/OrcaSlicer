// [INTENT] HTTP client wrapper abstracting libcurl backend operations.
// [COUPLING] Provides async HTTP operations for print-host uploaders and network agents.
// [HAZARD] Uses std::enable_shared_from_this - requires object be owned by shared_ptr before
//          calling shared_from_this(), otherwise undefined behavior (std::bad_weak_ptr).
// [STATE] Static extra_headers map provides global state affecting all HTTP requests.
//         Thread-unsafe if modified during concurrent requests.

#ifndef __Http_hpp__
#define __Http_hpp__

#include <map>
#include <memory>
#include <string>
#include <functional>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>

#include "libslic3r/Exception.hpp"
#include "libslic3r_version.h"

#define MAX_SIZE_TO_FILE 3 * 1024

namespace Slic3r {
// [INTENT] BBS-specific error codes for HTTP operations, extending standard HTTP status codes.
// [COUPLING] These codes are used by network agents to map service-specific errors.
enum HttpErrorCode {
    HttpErrorResourcesNotFound    = 2,
    HtttErrorNoDevice             = 3,
    HttpErrorRequestLogin         = 4,
    HttpErrorResourcesNotExists   = 6,
    HttpErrorMQTTError            = 7,
    HttpErrorResourcesForbidden   = 8,
    HttpErrorInternalRequestError = 9,
    HttpErrorInternalError        = 10,
    HttpErrorFileFormatError      = 11,
    HttpErrorResoucesConflict     = 12,
    HttpErrorTimeout              = 13,
    HttpErrorResourcesExhaust     = 14,
    HttpErrorVersionLimited       = 15,
};

/// Represents a Http request
// [MEMORY] Uses pimpl idiom (struct priv) to hide libcurl implementation details.
//          priv is heap-allocated via unique_ptr and owns curl handle lifecycle.
// [HAZARD] enable_shared_from_this requires Http instances be managed by shared_ptr.
//          Creating on stack then calling perform() (which uses shared_from_this)
//          will throw std::bad_weak_ptr.
class Http : public std::enable_shared_from_this<Http>
{
private:
    struct priv;

public:
    // [INTENT] Progress callback data for upload/download tracking.
    // [STATE] buffer reference is non-owning - points to priv's internal buffer.
    struct Progress
    {
        size_t             dltotal; // Total bytes to download
        size_t             dlnow;   // Bytes downloaded so far
        size_t             ultotal; // Total bytes to upload
        size_t             ulnow;   // Bytes uploaded so far
        const std::string& buffer;  // reference to buffer containing all data
        double             upload_spd{0.0f};

        Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow, const std::string& buffer)
            : dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow), buffer(buffer)
        {}

        Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow, const std::string& buffer, double ulspd)
            : dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow), buffer(buffer), upload_spd(ulspd)
        {}
    };

    typedef std::shared_ptr<Http>                                                   Ptr;
    typedef std::function<void(std::string /* body */, unsigned /* http_status */)> CompleteFn;

    // [INTENT] Error callback distinguishes between network-level failures (error populated,
    //          http_status=0) and HTTP error responses (error empty, http_status>=400).
    typedef std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> ErrorFn;

    // [INTENT] Progress callback receives transfer stats; setting cancel=true aborts request.
    // [STATE] cancel is an output parameter - caller sets it to abort ongoing transfer.
    typedef std::function<void(Progress, bool& /* cancel */)> ProgressFn;

    typedef std::function<void(std::string /* address */)> IPResolveFn;

    typedef std::function<void(std::string headers)> HeaderCallbackFn;

    Http(Http&& other);

    // Note: strings are expected to be UTF-8-encoded

    // [INTENT] Factory methods return Http by value; caller must immediately wrap in shared_ptr
    //          before calling perform() or risk bad_weak_ptr exception.
    static Http get(std::string url);
    static Http post(std::string url);
    static Http put(std::string url);
    static Http del(std::string url);

    // BBS additions for REST API compatibility
    static Http put2(std::string url);
    static Http patch(std::string url);

    // [STATE] Static extra_headers is global mutable state affecting ALL Http requests.
    // [HAZARD] Not thread-safe - concurrent modification could corrupt the map or cause races.
    static void                               set_extra_headers(std::map<std::string, std::string> headers);
    static std::map<std::string, std::string> get_extra_headers();

    ~Http();

    Http(const Http&)            = delete;
    Http& operator=(const Http&) = delete;
    Http& operator=(Http&&)      = delete;

    // [INTENT] Method-chaining pattern (return *this) for fluent API configuration.
    // [STATE] Each method mutates internal curl state in priv struct.
    Http& timeout_connect(long timeout);
    Http& timeout_max(long timeout);
    Http& size_limit(size_t sizeLimit);
    Http& set_range(const std::string& range);
    // Sets a HTTP header field.
    Http& header(std::string name, const std::string& value);
    // Removes a header field.
    Http& remove_header(std::string name);
    // Authorization by HTTP digest, based on RFC2617.
    Http& auth_digest(const std::string& user, const std::string& password);
    // Basic HTTP authorization
    Http& auth_basic(const std::string& user, const std::string& password);
    // Sets a CA certificate file for usage with HTTPS. This is only supported on some backends,
    // specifically, this is supported with OpenSSL and NOT supported with Windows and OS X native certificate store.
    // See also ca_file_supported().
    Http& ca_file(const std::string& filename);

    Http& form_clear();
    // Add a HTTP multipart form field
    Http& form_add(const std::string& name, const std::string& contents);
    // Add a HTTP multipart form file data contents, `name` is the name of the part
    Http& form_add_file(const std::string&                    name,
                        const boost::filesystem::path&        path,
                        boost::filesystem::ifstream::off_type offset = 0,
                        size_t                                length = 0);
    // Add a HTTP mime form field
    Http& mime_form_add_text(std::string& name, std::string& value);
    // Add a HTTP mime form file
    Http& mime_form_add_file(std::string& name, const char* path);
    // Same as above except also override the file's filename with a wstring type
    Http& form_add_file(const std::wstring&                   name,
                        const boost::filesystem::path&        path,
                        boost::filesystem::ifstream::off_type offset = 0,
                        size_t                                length = 0);
    // Same as above except also override the file's filename with a custom one
    Http& form_add_file(const std::string&                    name,
                        const boost::filesystem::path&        path,
                        const std::string&                    filename,
                        boost::filesystem::ifstream::off_type offset = 0,
                        size_t                                length = 0);

#ifdef WIN32
    // Tells libcurl to ignore certificate revocation checks in case of missing or offline distribution points for those SSL backends where
    // such behavior is present. This option is only supported for Schannel (the native Windows SSL library).
    Http& ssl_revoke_best_effort(bool set);
#endif // WIN32

    // Set the file contents as a POST request body.
    // The data is used verbatim, it is not additionally encoded in any way.
    // This can be used for hosts which do not support multipart requests.
    Http& set_post_body(const boost::filesystem::path& path);

    // Set the POST request body.
    // The data is used verbatim, it is not additionally encoded in any way.
    // This can be used for hosts which do not support multipart requests.
    Http& set_post_body(const std::string& body);

    // Set the file contents as a PUT request body.
    // The data is used verbatim, it is not additionally encoded in any way.
    // This can be used for hosts which do not support multipart requests.
    Http& set_put_body(const boost::filesystem::path& path);

    // Set the file contents as a DELETE request body.
    // The data is used verbatim, it is not additionally encoded in any way.
    // This can be used for hosts which do not support multipart requests.
    Http& set_del_body(const std::string& body);

    // [INTENT] Callbacks stored in priv, invoked by curl callbacks during transfer.
    // [STATE] Callbacks capture external state - be aware of lifetime issues with captured refs.
    Http& on_complete(CompleteFn fn);
    Http& on_error(ErrorFn fn);
    Http& on_progress(ProgressFn fn);
    Http& on_ip_resolve(IPResolveFn fn);
    Http& on_header_callback(HeaderCallbackFn fn);

    // [INTENT] perform() spawns background thread and returns shared_ptr for lifetime management.
    // [MEMORY] The returned Ptr keeps Http alive until request completes or is cancelled.
    // [HAZARD] Calling perform() on stack-allocated Http will crash (uses shared_from_this()).
    // [CONCURRENCY] Background thread calls curl_multi_perform; callbacks execute in that thread.
    Ptr  perform();
    void perform_sync();
    void cancel();

    // Print the request as a curl command for debugging
    void print() const;

    static bool ca_file_supported();

    // [INTENT] TLS initialization - must call before any HTTPS requests (affects curl global state).
    static std::string tls_global_init();
    static std::string tls_system_cert_store();

    // [INTENT] URL encoding/decoding utilities for query string construction.
    static std::string url_encode(const std::string& str);
    static std::string url_decode(const std::string& str);

    static std::string get_filename_from_url(const std::string& url);

private:
    Http(const std::string& url);

    // [MEMORY] Pimpl pattern: priv contains curl handle and all transfer state.
    //          unique_ptr ensures proper cleanup in destructor.
    std::unique_ptr<priv> p;
};

std::ostream& operator<<(std::ostream&, const Http::Progress&);

} // namespace Slic3r

#endif
