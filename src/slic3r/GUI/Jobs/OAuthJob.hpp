// [INTENT]
// This header file defines the data structures and the `OAuthJob` class for
// handling the OAuth 2.0 authorization code flow in a background job.
// It declares the necessary parameters, the structure for the result, and the
// job class that orchestrates the process.
//
// [UNITY]
// In a Unity port, these data structures would be replaced by C# classes or
// structs. The `OAuthJob` class itself would not have a direct equivalent but
// would be replaced by a C# script that manages the OAuth flow using async/await
// Tasks and a local web server implementation (e.g., using `HttpListener`).

#ifndef __OAuthJob_HPP__
#define __OAuthJob_HPP__

#include "Job.hpp"
#include "slic3r/GUI/HttpServer.hpp"
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

class Plater;

// [INTENT] This struct holds all the necessary parameters for initiating an
// OAuth 2.0 authorization code flow.
// [UNITY] This would be a C# class or struct, likely populated from a
// configuration file or a service discovery mechanism.
struct OAuthParams
{
    // [STATE] The URL of the authorization endpoint.
    std::string login_url;
    // [STATE] The client ID for the application.
    std::string client_id;
    // [STATE] The port for the local callback server.
    boost::asio::ip::port_type callback_port;
    // [STATE] The full URL for the local callback.
    std::string callback_url;
    // [STATE] The requested scopes for the authorization.
    std::string scope;
    // [STATE] The response type, typically "code" for the authorization code flow.
    std::string response_type;
    // [STATE] The URL to redirect the browser to on success.
    std::string auth_success_redirect_url;
    // [STATE] The URL to redirect the browser to on failure.
    std::string auth_fail_redirect_url;
    // [STATE] The URL of the token endpoint.
    std::string token_url;
    // [STATE] The PKCE code verifier.
    std::string verification_code;
    // [STATE] A random string to prevent CSRF attacks.
    std::string state;
};

// [INTENT] This struct holds the result of the OAuth flow.
// [UNITY] A simple C# class to hold the token data or error information.
struct OAuthResult
{
    // [STATE] True if the authentication was successful.
    bool success{false};
    // [STATE] An error message if the authentication failed.
    std::string error_message{""};
    // [STATE] The access token received from the server.
    std::string access_token{""};
    // [STATE] The refresh token received from the server.
    std::string refresh_token{""};
};

// [INTENT] A container for the OAuth parameters and a shared pointer to the result.
// [UNITY] This could be a simple C# class that holds references to the params and result objects.
struct OAuthData
{
    OAuthParams                  params;
    std::shared_ptr<OAuthResult> result;
};

// [INTENT] The `OAuthJob` class encapsulates the entire OAuth 2.0 flow in a
// background job to avoid blocking the UI thread.
// [THREAD] The `process` method is executed on a worker thread, while `finalize`
// is executed on the main UI thread.
// [PORTING_HAZARD:P2] The `Job` base class and the `wxPostEvent` mechanism for
// UI notification will need to be replaced with a C# `Task` and `Task`-based
// completion notification system.
class OAuthJob : public Job
{
    // [STATE] The local HTTP server to handle the OAuth callback.
    HttpServer local_authorization_server;
    // [STATE] The data (parameters and result pointer) for the OAuth flow.
    OAuthData _data;
    // [EVENT] A handle to the UI window to post events to when the job is complete.
    wxWindow* m_event_handle{nullptr};

public:
    explicit OAuthJob(const OAuthData& input);

    void process(Ctl& ctl) override;
    void finalize(bool canceled, std::exception_ptr& e) override;

    void set_event_handle(wxWindow* hanle) { m_event_handle = hanle; }

    static void parse_token_response(const std::string& body, bool error, OAuthResult& result);
};

wxDECLARE_EVENT(EVT_OAUTH_COMPLETE_MESSAGE, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif // OAUTHJOB_HPP
