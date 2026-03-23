#ifndef slic3r_Http_App_hpp_
#define slic3r_Http_App_hpp_

#include <iostream>
#include <mutex>
#include <stack>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <string>
#include <set>
#include <memory>
#include <utility>

#define LOCALHOST_PORT      13618
#define LOCALHOST_URL       "http://localhost:"

// [STATE] Default binding values for the internal GUI HTTP bridge; the Unity port should expose these via a
// ScriptableObject configuration (not macros) so the address can float per profile without rebuilds.

namespace Slic3r { namespace GUI {

class session;

class http_headers
{
    std::string method;
    std::string url;
    std::string version;

    std::map<std::string, std::string> headers;

    friend class session;

public:
    // [INTENT] Parses the first request line + headers into reusable state so session::read_body can stay simple.
    std::string get_url() { return url; }

    int content_length()
    {
        // [STATE] returns the parsed Content-Length header so downstream code can size body buffers without blocking.
        auto request = headers.find("content-length");
        if (request != headers.end()) {
            std::stringstream ssLength(request->second);
            int               content_length;
            ssLength >> content_length;
            return content_length;
        }
        return 0;
    }

    void on_read_header(std::string line)
    {
        // [EVENT] invoked once per header line; stores the header/value pair for the request pipeline.
        // std::cout << "header: " << line << std::endl;

        std::stringstream ssHeader(line);
        std::string       headerName;
        std::getline(ssHeader, headerName, ':');

        std::string value;
        std::getline(ssHeader, value);
        headers[headerName] = value;
    }

    void on_read_request_line(std::string line)
    {
        // [EVENT] called when the first HTTP request line arrives; distills method/URL/version for the dispatcher.
        std::stringstream ssRequestLine(line);
        ssRequestLine >> method;
        ssRequestLine >> url;
        ssRequestLine >> version;

        std::cout << "request for resource: " << url << std::endl;
    }
};

// [INTENT] Hosts a tiny loop-back HTTP broker so GUI panels can react to local requests.
// [UNITY] Map this logic to a Unity `UnityWebRequest` handler + `MainThreadDispatcher` for marshaling callbacks.
class HttpServer
{
    boost::asio::ip::port_type port;

public:
    // [INTENT] Abstraction for streaming status + body payloads back to HTTP clients; Unity should translate this into `UnityWebRequest`
    // completion delegates.
    class Response
    {
    public:
        virtual ~Response()                                   = default;
        virtual void write_response(std::stringstream& ssOut) = 0;
    };

    class ResponseNotFound : public Response
    {
    public:
        ~ResponseNotFound() override = default;
        void write_response(std::stringstream& ssOut) override;
    };

    // [UNITY] Unity port can emit a redirect via `UnityWebRequestAsyncOperation` by setting `result` to `HttpRequestStatus.Redirect`.
    class ResponseRedirect : public Response
    {
        const std::string location_str;

    public:
        ResponseRedirect(const std::string& location) : location_str(location) {}
        ~ResponseRedirect() override = default;
        void write_response(std::stringstream& ssOut) override;
    };

    class ResponseHtml : public Response
    {
        const std::string html;

    public:
        explicit ResponseHtml(std::string html) : html(std::move(html)) {}
        ~ResponseHtml() override = default;
        void write_response(std::stringstream& ssOut) override;
    };

    HttpServer(boost::asio::ip::port_type port = LOCALHOST_PORT);

    // [THREAD] Background thread running the ASIO event loop; Unity porters should offload this into a `Task`/`Coroutine` and marshal
    // via `MainThreadDispatcher` when handlers finish. [PORTING_HAZARD:P2] Boost threads cannot live on Unity's managed main-thread so we
    // need a safe dispatcher.
    boost::thread m_http_server_thread;
    // [STATE] Guards double-start/stop races triggered from GUI controls.
    bool start_http_server = false;

    bool is_started() { return start_http_server; }
    // [EVENT] kicks off the async listener thread.
    void start();
    // [EVENT][THREAD] cancels the IO context and joins the worker.
    void                       stop();
    void                       set_port(boost::asio::ip::port_type new_port) { port = new_port; }
    boost::asio::ip::port_type get_port() const { return port; }
    // [EVENT] GUI layers inject their handler here; Unity will map this to an `Action<string, Response>` bound to the port selector.
    void set_request_handler(const std::function<std::shared_ptr<Response>(const std::string&)>& m_request_handler);

    // [INTENT] Default handler providing bbl auth coverage when no other callback is registered.
    static std::shared_ptr<Response> bbl_auth_handle_request(const std::string& url);

private:
    // [INTENT] Manages the asio acceptor, the active sessions, and the middle man between the io_service and HttpServer.
    class IOServer
    {
    public:
        HttpServer&                        server;
        boost::asio::io_service            io_service;
        boost::asio::ip::tcp::acceptor     acceptor;
        std::set<std::shared_ptr<session>> sessions;

        IOServer(HttpServer& server) : server(server), acceptor(io_service, {boost::asio::ip::tcp::v4(), server.port}) {}

        // [THREAD] Called inside the ASIO loop to accept the next connection.
        void do_accept();

        // [THREAD] Adds a session to the set and begins async read.
        void start(std::shared_ptr<session> session);
        // [THREAD] Removes a session when it finishes or errors.
        void stop(std::shared_ptr<session> session);
        // [THREAD] Drains all sessions (used during shutdown).
        void stop_all();
    };
    friend class session;

    // [INTENT] Represents a single TCP session; request parsing occurs here before the HttpServer response pipeline.
    // [THREAD] Owned by the ASIO IO thread and must marshal parsed events back to the GUI thread.

    // [STATE] Owning pointer to the asio acceptor/session manager; this is allocated once per server instance.
    std::unique_ptr<IOServer> server_{nullptr};

    // [STATE][EVENT] Current request handler delegate; defaults to `bbl_auth_handle_request`. Unity should swap in a delegate bound to
    // a serialized `UnityEvent` so the web bridge can respond to user-driven routes.
    std::function<std::shared_ptr<Response>(const std::string&)> m_request_handler{&HttpServer::bbl_auth_handle_request};
};

// [INTENT] Wraps query parsing for simple URL parameters used by the HTTP bridge; Unity can reuse the `Uri` class for the same purpose.
class session : public std::enable_shared_from_this<session>
{
    HttpServer::IOServer&        server;
    boost::asio::ip::tcp::socket socket;

    boost::asio::streambuf buff;
    http_headers           headers;

    void read_first_line();
    void read_next_line();
    void read_body();

public:
    session(HttpServer::IOServer& server, boost::asio::ip::tcp::socket socket) : server(server), socket(std::move(socket)) {}

    // [EVENT] Kicks off the acceptor thread.
    void start();
    // [EVENT][THREAD] Signals shutdown and waits for `m_http_server_thread` to join.
    void stop();
};

// [INTENT] Parses simple query parameters; Unity can reuse `System.Uri`/`WWWForm` helpers for the same job.
std::string url_get_param(const std::string& url, const std::string& key);

}}; // namespace Slic3r::GUI

#endif
