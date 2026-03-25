// [INTENT]
// This file implements the `OAuthJob` class, a background job that handles the
// OAuth 2.0 authorization code flow. This is used for authenticating with cloud
// services like print hosts (e.g., OctoPrint, Duet).
//
// The process works as follows:
// 1. The user is directed to the service's authorization URL in their browser.
// 2. After authorizing, the service redirects the browser to a local callback URL
//    (e.g., http://127.0.0.1:12345/callback).
// 3. The `OAuthJob` starts a temporary local HTTP server (`HttpServer`) to listen
//    on this callback port.
// 4. The `process()` method sets up a request handler for the local server. When
//    the callback URL is hit, the handler extracts the authorization `code` and
//    `state` from the URL.
// 5. It then makes a POST request to the service's token endpoint to exchange the
//    authorization code for an access token and a refresh token.
// 6. The result (success or failure, tokens, error messages) is put into a
//    thread-safe queue.
// 7. The main job thread waits on this queue. Once a result is received, or the
//    job is canceled, the `process()` method finishes.
// 8. The `finalize()` method, running on the main UI thread, stops the local
//    server and posts a wx event (`EVT_OAUTH_COMPLETE_MESSAGE`) to notify the
//    application that the authentication process is complete.
//
// [UNITY]
// In a Unity port, this flow would be managed by a C# script.
// - A local web server could be implemented using `HttpListener` or a library
//   like EmbedIO.
// - The flow would be initiated and managed using `async/await` Tasks instead of
//   a `Job` class.
// - The HTTP request to the token endpoint would be made using `UnityWebRequest`.
// - The result would be communicated back to the main thread using a callback or
//   by awaiting the task, where the UI would be updated. There would be no need
//   for a separate `finalize` step or a wx-style event system.

#include "OAuthJob.hpp"

#include "Http.hpp"
#include "ThreadSafeQueue.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "nlohmann/json.hpp"
#include <boost/algorithm/string.hpp>
#include <thread>

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_OAUTH_COMPLETE_MESSAGE, wxCommandEvent);

// [INTENT] Constructs the OAuthJob.
// [PARAM] input: Contains all the necessary parameters for the OAuth flow,
// such as URLs, client ID, and the result pointer.
OAuthJob::OAuthJob(const OAuthData& input) : local_authorization_server(input.params.callback_port), _data(input) {}

// [INTENT] Parses the JSON response from the token endpoint.
// [PARAM] body: The JSON string from the HTTP response.
// [PARAM] error: A flag indicating whether the response is an error response.
// [PARAM] result: The `OAuthResult` struct to be populated with the tokens or error message.
void OAuthJob::parse_token_response(const std::string& body, bool error, OAuthResult& result)
{
    const auto j = nlohmann::json::parse(body, nullptr, false, true);
    if (j.is_discarded()) {
        BOOST_LOG_TRIVIAL(warning) << "Invalid or no JSON data on token response: " << body;
        result.error_message = _u8L("Unknown error");
    } else if (error) {
        if (j.contains("error_description")) {
            j.at("error_description").get_to(result.error_message);
        } else {
            result.error_message = _u8L("Unknown error");
        }
    } else {
        j.at("access_token").get_to(result.access_token);
        j.at("refresh_token").get_to(result.refresh_token);
        result.success = true;
    }
}

// [INTENT] The main worker method for the job. It starts a local HTTP server
// to handle the OAuth callback and exchanges the authorization code for an
// access token.
// [THREAD] This method is executed on a worker thread.
void OAuthJob::process(Ctl& ctl)
{
    // Prepare auth process
    // [THREAD] A thread-safe queue to communicate the result from the HTTP server
    // handler (running in its own thread) to this job thread.
    std::shared_ptr<ThreadSafeQueueSPSC<OAuthResult>> queue = std::make_shared<ThreadSafeQueueSPSC<OAuthResult>>();

    // Setup auth server to receive OAuth code from callback url
    // [INTENT] The request handler for the local HTTP server. This lambda is
    // executed when the browser is redirected to the callback URL.
    local_authorization_server.set_request_handler([this, queue](const std::string& url) -> std::shared_ptr<HttpServer::Response> {
        if (boost::contains(url, "/callback")) {
            const auto code  = url_get_param(url, "code");
            const auto state = url_get_param(url, "state");

            const auto handle_auth_fail = [this, queue](const std::string& message) -> std::shared_ptr<HttpServer::ResponseRedirect> {
                queue->push(OAuthResult{false, message});
                return std::make_shared<HttpServer::ResponseRedirect>(this->_data.params.auth_fail_redirect_url);
            };

            // [SECURITY] The state parameter is checked to prevent CSRF attacks.
            if (state != _data.params.state) {
                BOOST_LOG_TRIVIAL(warning) << "The provided state was not correct. Got " << state << " and expected " << _data.params.state;
                return handle_auth_fail(_u8L("The provided state is not correct."));
            }

            if (code.empty()) {
                const auto error_code = url_get_param(url, "error_code");
                if (error_code == "user_denied") {
                    BOOST_LOG_TRIVIAL(debug) << "User did not give the required permission when authorizing this application";
                    return handle_auth_fail(_u8L("Please give the required permissions when authorizing this application."));
                }

                BOOST_LOG_TRIVIAL(warning) << "Unexpected error when logging in. Error_code: " << error_code << ", State: " << state;
                return handle_auth_fail(_u8L("Something unexpected happened when trying to log in, please try again."));
            }

            OAuthResult r;
            // [INTENT] Exchange the authorization code for an access token by making a
            // POST request to the token endpoint.
            auto http = Http::post(_data.params.token_url);
            http.timeout_connect(5)
                .timeout_max(5)
                .form_add("client_id", _data.params.client_id)
                .form_add("redirect_uri", _data.params.callback_url)
                .form_add("grant_type", "authorization_code")
                .form_add("code", code)
                .form_add("code_verifier", _data.params.verification_code)
                .form_add("scope", _data.params.scope)
                .on_complete([&](std::string body, unsigned status) { parse_token_response(body, false, r); })
                .on_error([&](std::string body, std::string error, unsigned status) { parse_token_response(body, true, r); })
                .perform_sync();

            queue->push(r);
            // [INTENT] Redirect the user's browser to a success or failure page.
            return std::make_shared<HttpServer::ResponseRedirect>(r.success ? _data.params.auth_success_redirect_url :
                                                                              _data.params.auth_fail_redirect_url);
        } else {
            queue->push(OAuthResult{false});
            return std::make_shared<HttpServer::ResponseNotFound>();
        }
    });

    // Run the local server
    local_authorization_server.start();

    // [THREAD] Wait until the result is received from the queue, or the job is canceled.
    bool received = false;
    while (!ctl.was_canceled() && !received) {
        queue->consume_one(BlockingWait{1000}, [this, &received](const OAuthResult& result) {
            *_data.result = result;
            received      = true;
        });
    }

    // Handle timeout
    if (!received && ctl.was_canceled()) {
        _data.result->error_message = _u8L("User canceled.");
    } else {
        // Wait a while to ensure the response has sent
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
}

// [INTENT] This method is called on the main UI thread after the job finishes.
// It cleans up resources and notifies the UI.
// [THREAD] This method is executed on the main UI thread.
void OAuthJob::finalize(bool canceled, std::exception_ptr& e)
{
    // Make sure it's stopped
    local_authorization_server.stop();

    // [EVENT] Post an event to notify the UI that the OAuth process is complete.
    wxCommandEvent event(EVT_OAUTH_COMPLETE_MESSAGE);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}
}
else
{
    j.at("access_token").get_to(result.access_token);
    j.at("refresh_token").get_to(result.refresh_token);
    result.success = true;
}
}

void OAuthJob::process(Ctl& ctl)
{
    // Prepare auth process
    std::shared_ptr<ThreadSafeQueueSPSC<OAuthResult>> queue = std::make_shared<ThreadSafeQueueSPSC<OAuthResult>>();

    // Setup auth server to receive OAuth code from callback url
    local_authorization_server.set_request_handler([this, queue](const std::string& url) -> std::shared_ptr<HttpServer::Response> {
        if (boost::contains(url, "/callback")) {
            const auto code  = url_get_param(url, "code");
            const auto state = url_get_param(url, "state");

            const auto handle_auth_fail = [this, queue](const std::string& message) -> std::shared_ptr<HttpServer::ResponseRedirect> {
                queue->push(OAuthResult{false, message});
                return std::make_shared<HttpServer::ResponseRedirect>(this->_data.params.auth_fail_redirect_url);
            };

            if (state != _data.params.state) {
                BOOST_LOG_TRIVIAL(warning) << "The provided state was not correct. Got " << state << " and expected " << _data.params.state;
                return handle_auth_fail(_u8L("The provided state is not correct."));
            }

            if (code.empty()) {
                const auto error_code = url_get_param(url, "error_code");
                if (error_code == "user_denied") {
                    BOOST_LOG_TRIVIAL(debug) << "User did not give the required permission when authorizing this application";
                    return handle_auth_fail(_u8L("Please give the required permissions when authorizing this application."));
                }

                BOOST_LOG_TRIVIAL(warning) << "Unexpected error when logging in. Error_code: " << error_code << ", State: " << state;
                return handle_auth_fail(_u8L("Something unexpected happened when trying to log in, please try again."));
            }

            OAuthResult r;
            // Request the access token from the authorization server.
            auto http = Http::post(_data.params.token_url);
            http.timeout_connect(5)
                .timeout_max(5)
                .form_add("client_id", _data.params.client_id)
                .form_add("redirect_uri", _data.params.callback_url)
                .form_add("grant_type", "authorization_code")
                .form_add("code", code)
                .form_add("code_verifier", _data.params.verification_code)
                .form_add("scope", _data.params.scope)
                .on_complete([&](std::string body, unsigned status) { parse_token_response(body, false, r); })
                .on_error([&](std::string body, std::string error, unsigned status) { parse_token_response(body, true, r); })
                .perform_sync();

            queue->push(r);
            return std::make_shared<HttpServer::ResponseRedirect>(r.success ? _data.params.auth_success_redirect_url :
                                                                              _data.params.auth_fail_redirect_url);
        } else {
            queue->push(OAuthResult{false});
            return std::make_shared<HttpServer::ResponseNotFound>();
        }
    });

    // Run the local server
    local_authorization_server.start();

    // Wait until we received the result
    bool received = false;
    while (!ctl.was_canceled() && !received) {
        queue->consume_one(BlockingWait{1000}, [this, &received](const OAuthResult& result) {
            *_data.result = result;
            received      = true;
        });
    }

    // Handle timeout
    if (!received && ctl.was_canceled()) {
        _data.result->error_message = _u8L("User canceled.");
    } else {
        // Wait a while to ensure the response has sent
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
}

void OAuthJob::finalize(bool canceled, std::exception_ptr& e)
{
    // Make sure it's stopped
    local_authorization_server.stop();

    wxCommandEvent event(EVT_OAUTH_COMPLETE_MESSAGE);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}
}
} // namespace Slic3r::GUI
