#include "HttpServer.hpp"
#include <boost/log/trivial.hpp>
#include "GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

namespace Slic3r { namespace GUI {

// [INTENT] Expose a local loopback HTTP endpoint that bridges OAuth callbacks and service notifications back into the GUI.
// [UNITY] Build the equivalent with a background Task running an HttpListener/UnityWebRequest server so the render thread stays responsive
// to frames and input.

std::string url_get_param(const std::string& url, const std::string& key)
{
    size_t start = url.find(key);
    // [INTENT] Minimalistic binder that extracts query params by substring; used for OAuth redirects, so portability needs stricter
    // parsing. [PORTING_HAZARD:P3] This heuristic assumes well-formed "key=value" pairs and no URL-encoded ampersands, so future ports
    // should use a proper query parser.
    if (start == std::string::npos)
        return "";
    size_t eq = url.find('=', start);
    if (eq == std::string::npos)
        return "";
    std::string key_str = url.substr(start, eq - start);
    if (key_str != key)
        return "";
    start += key.size() + 1;
    size_t end = url.find('&', start);
    if (end == std::string::npos)
        end = url.length(); // Last param
    std::string result = url.substr(start, end - start);
    return result;
}

void session::start()
{
    // [EVENT][THREAD] Queue the first line read so the Asio IO thread parses the HTTP request without blocking the caller.
    read_first_line();
}

void session::stop()
{
    // [THREAD] Tear down the socket from any IO thread that owns it, ensuring Boost.Asio stops without leaking handles.
    boost::system::error_code ignored_ec;
    socket.shutdown(boost::asio::socket_base::shutdown_both, ignored_ec);
    socket.close(ignored_ec);
}

void session::read_first_line()
{
    auto self(shared_from_this());

    // [EVENT] Async read collects the HTTP request line; headers accumulate on the same IO thread, so order matters.
    async_read_until(socket, buff, '\r', [this, self](const boost::beast::error_code& e, std::size_t s) {
        if (!e) {
            std::string  line, ignore;
            std::istream stream{&buff};
            std::getline(stream, line, '\r');
            std::getline(stream, ignore, '\n');
            headers.on_read_request_line(line);
            read_next_line();
        } else if (e != boost::asio::error::operation_aborted) {
            server.stop(self);
        }
    });
}

void session::read_body()
{
    auto self(shared_from_this());

    // [STATE][UNCLEAR][THREAD] Reads but discards the body into a throwaway buffer on the IO thread; content-length is not respected, so
    // bodies are currently ignored and the worker just drops whatever is left to keep the pipeline idle. [UNITY] Unity's
    // HttpListener/UnityWebRequest must still drain the declared Content-Length (or cancel) before reusing the socket so Reactors do not
    // see trailing bytes on the next request.
    int                                nbuffer = 1000;
    std::shared_ptr<std::vector<char>> bufptr  = std::make_shared<std::vector<char>>(nbuffer);
    async_read(socket, boost::asio::buffer(*bufptr, nbuffer),
               [this, self](const boost::beast::error_code& e, std::size_t s) { server.stop(self); });
}

void session::read_next_line()
{
    auto self(shared_from_this());

    // [STATE][EVENT] Header lines accumulate until we hit an empty line, then the registered request handler dispatches the decoded URL on
    // the worker thread.
    async_read_until(socket, buff, '\r', [this, self](const boost::beast::error_code& e, std::size_t s) {
        if (!e) {
            std::string  line, ignore;
            std::istream stream{&buff};
            std::getline(stream, line, '\r');
            std::getline(stream, ignore, '\n');
            headers.on_read_header(line);

            if (line.length() == 0) {
                if (headers.content_length() == 0) {
                    std::cout << "Request received: " << headers.method << " " << headers.get_url();
                    if (headers.method == "OPTIONS") {
                        // Ignore http OPTIONS
                        server.stop(self);
                        return;
                    }

                    const std::string url_str = Http::url_decode(headers.get_url());
                    // [THREAD][UNITY][PORTING_HAZARD:P2] `m_request_handler` executes on the HTTP thread; any wx state it touches must be
                    // marshaled back via `CallAfter`/`MainThreadDispatcher` so the render thread isn't mutated directly. Unity should pair
                    // this handler with a Task that posts UI updates through a dispatcher before touching shared state.
                    const auto        resp = server.server.m_request_handler(url_str);
                    std::stringstream ssOut;
                    resp->write_response(ssOut);
                    std::shared_ptr<std::string> str = std::make_shared<std::string>(ssOut.str());
                    async_write(socket, boost::asio::buffer(str->c_str(), str->length()),
                                [this, self, str](const boost::beast::error_code& e, std::size_t s) {
                                    // [STATE] Capture `str` until the write callback runs so the buffer stays alive; Unity ports likewise
                                    // must retain the upload result until the HTTP pipeline flush completes.
                                    std::cout << "done" << std::endl;
                                    server.stop(self);
                                });
                } else {
                    read_body();
                }
            } else {
                read_next_line();
            }
        } else if (e != boost::asio::error::operation_aborted) {
            server.stop(self);
        }
    });
}

void HttpServer::IOServer::do_accept()
{
    // [THREAD][EVENT] Continuously accept new sockets on the IO thread so each session can parse its HTTP handshake.
    acceptor.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!acceptor.is_open()) {
            return;
        }

        if (!ec) {
            const auto ss = std::make_shared<session>(*this, std::move(socket));
            start(ss);
        }

        do_accept();
    });
}

void HttpServer::IOServer::start(std::shared_ptr<session> session)
{
    // [STATE] Track active sessions so we can stop them during shutdown without leaking sockets.
    sessions.insert(session);
    session->start();
}

void HttpServer::IOServer::stop(std::shared_ptr<session> session)
{
    // [STATE] Remove the session from the live set and shut it down once the connection finishes or aborts.
    sessions.erase(session);
    session->stop();
}

void HttpServer::IOServer::stop_all()
{
    // [STATE][THREAD] Flood all active sockets with shutdown so the server can exit cleanly before destroying the IO context.
    for (auto s : sessions) {
        s->stop();
    }
    sessions.clear();
}

HttpServer::HttpServer(boost::asio::ip::port_type port) : port(port) {}

void HttpServer::start()
{
    // [INTENT] Start the IO server and spin a dedicated thread so OAuth callbacks and other local hooks can reach the GUI without blocking
    // the main frame. [STATE] `start_http_server` flips to true while the thread keeps running; this flag guards repeated starts. [UNITY]
    // Port this to Unity by running a `HttpListener` on a Task and dispatching replies via `MainThreadDispatcher.Invoke` when GUI state
    // changes. [OPENGL] Keep the HTTP worker entirely off the OpenGL render thread to avoid stalling shader compilation and UI refreshes.
    BOOST_LOG_TRIVIAL(info) << "start_http_service...";
    start_http_server    = true;
    m_http_server_thread = create_thread([this] {
        set_current_thread_name("http_server");
        server_ = std::make_unique<IOServer>(*this);
        server_->acceptor.listen();

        // [STATE] The acceptor keeps the socket bound for the thread's lifetime; restarting the server must rebind the same port or cleanly
        // signal the Unity listener to release it.

        server_->do_accept();

        server_->io_service.run();
    });
}

void HttpServer::stop()
{
    // [STATE][THREAD] Flip the start flag, close acceptor, and join the thread so the GUI can safely tear down without dangling BIO threads.
    start_http_server = false;
    if (server_) {
        server_->acceptor.close();
        server_->stop_all();
        server_->io_service.stop();
    }
    if (m_http_server_thread.joinable())
        m_http_server_thread.join();
    server_.reset();
}

void HttpServer::set_request_handler(const std::function<std::shared_ptr<Response>(const std::string&)>& request_handler)
{
    // [STATE] Point the server to the current request handler (default is the BBL auth flow, but tests or other features may override).
    // [THREAD][PORTING_HAZARD:P3] Worker threads capture this function pointer without synchronization while the main thread can swap it,
    // so ports should persist it through a concurrent-safe container or marshal the setter via the dispatcher to avoid races.
    this->m_request_handler = request_handler;
}

std::shared_ptr<HttpServer::Response> HttpServer::bbl_auth_handle_request(const std::string& url)
{
    // [INTENT] Handle the OAuth callback that arrives on the loopback port, exchange tokens, update GUI state, and close the login dialog.
    // [STATE] Reads the `code` or `access_token` params, talks to `NetworkAgent`, and toggles `wxGetApp()` login state.
    // [THREAD] Runs on the HTTP worker thread, so GUI calls must be routed back via `GUI::wxGetApp().CallAfter` to avoid race conditions.
    // [UNITY] Unity ports should mirror this with `UnityWebRequest` for the token exchange + `MainThreadDispatcher` to hide UI once the
    // task completes. [PORTING_HAZARD:P2] This path tightly couples to `wxGetApp`, so Unity reimplementers must rewire credential storage
    // and `CallAfter` logic to their main-thread dispatcher.
    BOOST_LOG_TRIVIAL(info) << "thirdparty_login: get_response";

    const std::string auth_code = url_get_param(url, "code");
    if (!auth_code.empty()) {
        std::string   state = url_get_param(url, "state");
        NetworkAgent* agent = wxGetApp().getAgent();
        if (!agent) {
            return std::make_shared<ResponseNotFound>();
        }

        json payload;
        payload["command"]       = "user_login";
        payload["data"]["code"]  = auth_code;
        payload["data"]["state"] = state;

        agent->change_user(payload.dump());
        const bool login_ok = agent->is_user_login();
        if (login_ok) {
            wxGetApp().request_user_login(1);
            GUI::wxGetApp().CallAfter([] { wxGetApp().ShowUserLogin(false); });
        }

        const std::string title   = login_ok ? "Authentication complete" : "Authentication failed";
        const std::string message = login_ok ? "You can return to OrcaSlicer. This window will close automatically." :
                                               "Something went wrong. Please return to OrcaSlicer and try again.";
        const std::string html    = "<html><head><meta charset=\"utf-8\">"
                                    "<style>body{font-family:Arial,sans-serif;background:#f7f7f7;color:#222;margin:32px;}"
                                    "a.button{display:inline-block;padding:10px "
                                    "16px;margin-top:12px;background:#0f8bff;color:#fff;text-decoration:none;border-radius:6px;}"
                                    "</style></head><body><div class=\"container\">"
                                    "<h2>" +
                                 title +
                                 "</h2>"
                                 "<p>" +
                                 message +
                                 "</p>"
                                 "<script>setTimeout(function(){try{window.close();}catch(e){}},1500);</script>"
                                 "</div></body></html>";
        return std::make_shared<ResponseHtml>(html);
    }

    if (boost::contains(url, "access_token")) {
        std::string   redirect_url           = url_get_param(url, "redirect_url");
        std::string   access_token           = url_get_param(url, "access_token");
        std::string   refresh_token          = url_get_param(url, "refresh_token");
        std::string   expires_in_str         = url_get_param(url, "expires_in");
        std::string   refresh_expires_in_str = url_get_param(url, "refresh_expires_in");
        NetworkAgent* agent                  = wxGetApp().getAgent();

        unsigned int http_code;
        std::string  http_body;
        int          result = agent->get_my_profile(access_token, &http_code, &http_body);
        if (result == 0) {
            std::string user_id;
            std::string user_name;
            std::string user_account;
            std::string user_avatar;
            try {
                json user_j = json::parse(http_body);
                if (user_j.contains("uidStr"))
                    user_id = user_j["uidStr"].get<std::string>();
                if (user_j.contains("name"))
                    user_name = user_j["name"].get<std::string>();
                if (user_j.contains("avatar"))
                    user_avatar = user_j["avatar"].get<std::string>();
                if (user_j.contains("account"))
                    user_account = user_j["account"].get<std::string>();
            } catch (...) {
                ;
            }
            json j;
            j["data"]["refresh_token"]      = refresh_token;
            j["data"]["token"]              = access_token;
            j["data"]["expires_in"]         = expires_in_str;
            j["data"]["refresh_expires_in"] = refresh_expires_in_str;
            j["data"]["user"]["uid"]        = user_id;
            j["data"]["user"]["name"]       = user_name;
            j["data"]["user"]["account"]    = user_account;
            j["data"]["user"]["avatar"]     = user_avatar;
            agent->change_user(j.dump());
            if (agent->is_user_login()) {
                wxGetApp().request_user_login(1);
            }
            GUI::wxGetApp().CallAfter([] { wxGetApp().ShowUserLogin(false); });
            std::string location_str = (boost::format("%1%?result=success") % redirect_url).str();
            return std::make_shared<ResponseRedirect>(location_str);
        } else {
            std::string error_str    = "get_user_profile_error_" + std::to_string(result);
            std::string location_str = (boost::format("%1%?result=fail&error=%2%") % redirect_url % error_str).str();
            return std::make_shared<ResponseRedirect>(location_str);
        }
    } else {
        return std::make_shared<ResponseNotFound>();
    }
}

void HttpServer::ResponseNotFound::write_response(std::stringstream& ssOut)
{
    // [INTENT] Keep the fallback response simple so any unknown URL just returns an HTML 404 without touching GUI state.
    const std::string sHTML = "<html><body><h1>404 Not Found</h1><p>There's nothing here.</p></body></html>";
    ssOut << "HTTP/1.1 404 Not Found" << std::endl;
    ssOut << "content-type: text/html" << std::endl;
    ssOut << "content-length: " << sHTML.length() << std::endl;
    ssOut << std::endl;
    ssOut << sHTML;
}

void HttpServer::ResponseRedirect::write_response(std::stringstream& ssOut)
{
    // [INTENT] Serve the HTML redirect page so browser windows close themselves once OAuth finishes.
    // [UNITY] Unity ports should either send the Location header directly or provide a tiny HTML page that calls `Application.Quit()` after
    // the redirect.
    const std::string sHTML = "<html><head><meta charset=\"utf-8\">"
                              "<meta http-equiv=\"refresh\" content=\"0;url=" +
                              location_str +
                              "\">"
                              "<style>body{font-family:Arial,sans-serif;background:#f7f7f7;color:#222;margin:32px;}"
                              "a.button{display:inline-block;padding:10px "
                              "16px;margin-top:12px;background:#0f8bff;color:#fff;text-decoration:none;border-radius:6px;}"
                              "</style></head><body><div class=\"container\">"
                              "<h2>Authentication complete</h2>"
                              "<p>You can return to OrcaSlicer. If your browser does not redirect automatically, use the button below.</p>"
                              "<a class=\"button\" href=\"" +
                              location_str +
                              "\">Continue</a>"
                              "<script>setTimeout(function(){try{window.close();}catch(e){}},1500);</script>"
                              "</div></body></html>";
    ssOut << "HTTP/1.1 302 Found" << std::endl;
    ssOut << "Location: " << location_str << std::endl;
    ssOut << "content-type: text/html" << std::endl;
    ssOut << "content-length: " << sHTML.length() << std::endl;
    ssOut << std::endl;
    ssOut << sHTML;
}

void HttpServer::ResponseHtml::write_response(std::stringstream& ssOut)
{
    // [INTENT] Stream the prepared HTML payload for success notifications without additional logic.
    // [UNITY] If the Unity port hosts this endpoint, the generated HTML can be sent through `UnityWebRequest` and the UI dismissed on completion.
    ssOut << "HTTP/1.1 200 OK" << std::endl;
    ssOut << "content-type: text/html" << std::endl;
    ssOut << "content-length: " << html.length() << std::endl;
    ssOut << std::endl;
    ssOut << html;
}

}} // namespace Slic3r::GUI
