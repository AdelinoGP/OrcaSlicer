#ifndef slic3r_GCodeSender_hpp_
#define slic3r_GCodeSender_hpp_

// [INTENT] GCodeSender manages a serial (USB/UART) connection to a 3D printer firmware
//   (Marlin, Grbl, etc.) for direct print-over-serial. It implements the full
//   Marlin line-number + checksum protocol with resend/recovery.
// [CONCURRENCY] Uses a Boost.ASIO io_service running on a single background_thread.
//   All queue/priority-queue mutations are guarded by queue_mutex; log/T/B by log_mutex;
//   error flag by error_mutex. Three separate mutexes — no single lock covers all state.
// [COUPLING] Depends on Boost.ASIO + Boost.Thread (not std::thread). Platform-specific
//   baud-rate code branches on __APPLE__, __linux__, __OpenBSD__, _WIN32.
// [STATE] Key invariants:
//   - open == serial socket is connected
//   - connected == printer has responded (sent "start", "ok", or "T:")
//   - can_send == waiting for "ok" ACK from printer (one in-flight command at a time)
//   - sent == monotonic line number counter, used for Marlin N/checksum protocol

#include "libslic3r.h"
#include <queue>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread.hpp>

namespace Slic3r {

namespace asio = boost::asio;

class GCodeSender : private boost::noncopyable
{
public:
    GCodeSender();
    ~GCodeSender();
    // [INTENT] Open the serial port and start the background io_service read loop.
    //   Sends a DTR toggle (reset) to the printer before beginning.
    bool connect(std::string devname, unsigned int baud_rate);
    // [INTENT] Enqueue one or more G-code lines. priority=true inserts at front of
    //   priqueue (used for emergency commands like M112 or resends).
    void send(const std::vector<std::string>& lines, bool priority = false);
    void send(const std::string& s, bool priority = false);
    void disconnect();
    bool error_status() const;
    bool is_connected() const;
    // [INTENT] Busy-waits in 100ms sleep increments until connected==true or timeout.
    bool   wait_connected(unsigned int timeout = 3) const;
    size_t queue_size() const;
    void   pause_queue();
    void   resume_queue();
    // [INTENT] Purge normal queue or priority queue depending on `priority` flag.
    //   Also resets queue_paused when purging the normal queue.
    void purge_queue(bool priority = false);
    // [INTENT] Drain and return all accumulated printer log lines under log_mutex.
    std::vector<std::string> purge_log();
    std::string              getT() const;
    std::string              getB() const;
    void                     set_DTR(bool on);
    // [INTENT] Hardware reset via DTR toggle: LOW 200ms → HIGH 200ms → LOW 500ms.
    //   Resets most Marlin/Grbl boards. Sleep values hardcoded.
    void reset();

private:
    // [STATE] io_service + serial_port are the Boost.ASIO core objects.
    //   Only background_thread runs io.run(); main thread uses io.post() to dispatch.
    asio::io_service       io;
    asio::serial_port      serial;
    boost::thread          background_thread;
    boost::asio::streambuf read_buffer, write_buffer;
    bool                   open;      // whether the serial socket is connected
    bool                   connected; // whether the printer is online
    bool                   error;
    mutable boost::mutex   error_mutex;

    // [CONCURRENCY] queue_mutex guards: queue, priqueue, can_send, queue_paused, sent, last_sent.
    //   Held briefly per operation; background thread and main thread both acquire it.
    // [HAZARD] H1057 P2/Medium: Three separate mutexes (error_mutex, queue_mutex, log_mutex).
    //   No compound operation is atomic across mutexes. E.g. reading error+connected
    //   together is not protected from a race between the two locks.
    // this mutex guards queue, priqueue, can_send, queue_paused, sent, last_sent
    mutable boost::mutex    queue_mutex;
    std::queue<std::string> queue;
    // [INTENT] priqueue is a std::list so front-insertion (insert at begin) is O(1).
    //   Normal queue is std::queue (deque-backed) for O(1) push/pop.
    std::list<std::string> priqueue;
    bool                   can_send;
    bool                   queue_paused;
    // [STATE] sent = monotonically increasing line counter for Marlin N+checksum protocol.
    //   last_sent = sliding window of KEEP_SENT=20 recently sent lines for resend recovery.
    size_t                  sent;
    std::deque<std::string> last_sent;

    // this mutex guards log, T, B
    mutable boost::mutex    log_mutex;
    std::queue<std::string> log;
    // [STATE] T = last seen hotend temperature string; B = last seen bed temperature string.
    //   Extracted from firmware "T:xxx B:xxx" response lines.
    std::string T, B;

    void set_baud_rate(unsigned int baud_rate);
    void set_error_status(bool e);
    void do_send();
    void on_write(const boost::system::error_code& error, size_t bytes_transferred);
    void do_close();
    void do_read();
    void on_read(const boost::system::error_code& error, size_t bytes_transferred);
    void send();
};

} // namespace Slic3r

#endif /* slic3r_GCodeSender_hpp_ */
