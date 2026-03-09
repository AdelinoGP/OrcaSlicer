#include "GCodeSender.hpp"
#include <iostream>
#include <istream>
#include <string>
#include <thread>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>

// [INTENT] GCodeSender.cpp: Serial print-over-USB implementation using Boost.ASIO.
//   Implements the Marlin line-number + XOR-checksum protocol:
//     Send: "N<line_num> <gcode>*<checksum>\n"
//     On "ok" ACK: advance to next command
//     On "Resend: N": re-inject from last_sent window
// [CONCURRENCY] Single background Boost.ASIO thread for async read/write.
//   All callbacks (on_read, on_write, do_send) execute exclusively on that thread.
//   Main thread uses io.post() to safely schedule do_send/do_close into ASIO's thread.
// [COUPLING] Platform-specific baud-rate code: __APPLE__ (IOSSIOSPEED ioctl),
//   __linux__ (termios2 / TCSETS2), __OpenBSD__ (cfsetspeed), WIN32 (EscapeCommFunction).
//   Missing: FreeBSD, Solaris, Android — silent no-op for custom baud rates on those.

#if defined(__APPLE__) || defined(__OpenBSD__)
#include <termios.h>
#endif
#ifdef __APPLE__
#include <sys/ioctl.h>
#include <IOKit/serial/ioss.h>
#endif
#ifdef __linux__
#include <sys/ioctl.h>
#include <fcntl.h>
#include "/usr/include/asm-generic/ioctls.h"

/* The following definitions are kindly borrowed from:
   /usr/include/asm-generic/termbits.h
   Unfortunately we cannot just include that one because
   it would redefine the "struct termios" already defined
   the <termios.h> already included by Boost.ASIO. */
#define K_NCCS 19
struct termios2
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[K_NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};
#define BOTHER CBAUDEX

#endif

// #define DEBUG_SERIAL
// [INTENT] DEBUG_SERIAL mode: logs all serial traffic to "serial.txt" and randomly
//   injects bad line numbers (1 in 4 chance) to test the resend recovery path.
// [HAZARD] H1058 P3/Low: DEBUG_SERIAL is disabled by default (commented-out #define).
//   The global `fs` std::fstream would be a file-scope object in non-TU builds.
//   If DEBUG_SERIAL were ever accidentally enabled in production, the file "serial.txt"
//   would grow unboundedly and the random bad-line injection could corrupt prints.
#ifdef DEBUG_SERIAL
#include <cstdlib>
#include <fstream>
std::fstream fs;
#endif

// [STATE] KEEP_SENT: sliding window size for resend recovery. Printer can request
//   any line in [sent - KEEP_SENT, sent]. Lines older than 20 are unrecoverable.
#define KEEP_SENT 20

namespace Slic3r {

GCodeSender::GCodeSender() : io(), serial(io), can_send(false), sent(0), open(false), error(false), connected(false), queue_paused(false)
{
#ifdef DEBUG_SERIAL
    std::srand(std::time(nullptr));
#endif
}

GCodeSender::~GCodeSender() { this->disconnect(); }

// [INTENT] connect(): Opens the serial port and initializes the Boost.ASIO background thread.
//   Two-phase port open: first open with odd parity (some firmware requires odd for auto-baud),
//   call set_baud_rate(), then re-open with none parity — because Boost's set_option() resets
//   baud rate after parity change, so baud rate is re-applied a second time.
//   After successful open, posts do_read() to the io_service and launches background_thread.
// [STATE] After connect(): open=true, sent=0, last_sent empty, can_send=false until firmware
//   sends "start"/"ok"/"T:" greeting detected in on_read().
// [HAZARD] FIXME (from source comment): Commented-out M105 temperature probe sent too early
//   causes line number sync issues. Firmware receives numbered command before sent=0 is
//   reset → Resend requests that can never be fulfilled. This path remains disabled.
// [CONCURRENCY] background_thread is Boost thread running io.run() — it must not be joined
//   before io.post(do_close) completes. See disconnect() for safe teardown sequence.
bool GCodeSender::connect(std::string devname, unsigned int baud_rate)
{
    this->disconnect();

    this->set_error_status(false);
    try {
        this->serial.open(devname);

        this->serial.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::odd));
        this->serial.set_option(boost::asio::serial_port_base::character_size(boost::asio::serial_port_base::character_size(8)));
        this->serial.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        this->serial.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        this->set_baud_rate(baud_rate);

        this->serial.close();
        this->serial.open(devname);
        this->serial.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));

        // set baud rate again because set_option overwrote it
        this->set_baud_rate(baud_rate);
        this->open = true;
        this->reset();
    } catch (boost::system::system_error&) {
        this->set_error_status(true);
        return false;
    }

    // a reset firmware expect line numbers to start again from 1
    this->sent = 0;
    this->last_sent.clear();

    /* Initialize debugger */
#ifdef DEBUG_SERIAL
    fs.open("serial.txt", std::fstream::out | std::fstream::trunc);
#endif

    // this gives some work to the io_service before it is started
    // (post() runs the supplied function in its thread)
    this->io.post(boost::bind(&GCodeSender::do_read, this));

    // start reading in the background thread
    boost::thread t(boost::bind(&boost::asio::io_service::run, &this->io));
    this->background_thread.swap(t);

    // always send a M105 to check for connection because firmware might be silent on connect
    // FIXME Vojtech: This is being sent too early, leading to line number synchronization issues,
    // from which the GCodeSender never recovers.
    // this->send("M105", true);

    return true;
}

// [INTENT] set_baud_rate(): Sets baud rate on the serial port, working around the Boost.ASIO
//   limitation that only supports standard baud rates up to 115200.
//   For non-standard baud rates (e.g., 250000 for Marlin), falls through to OS-specific ioctls.
// [COUPLING] Platform matrix:
//   macOS:   IOSSIOSPEED ioctl (IOKit) — sets arbitrary baud rate
//   Linux:   TCGETS2/TCSETS2 with termios2 struct and BOTHER flag
//   OpenBSD: cfsetspeed() — standard POSIX
//   WIN32:   Not handled here (Boost handles standard rates; EscapeCommFunction not called)
//   Others:  Silent no-op — baud rate silently falls back to whatever Boost last set.
// [HAZARD] The Linux path prints errors to stdout via printf() (not stderr, not a logger).
//   On a headless server these messages are silently swallowed.
void GCodeSender::set_baud_rate(unsigned int baud_rate)
{
    try {
        // This does not support speeds > 115200
        this->serial.set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
    } catch (boost::system::system_error&) {
        boost::asio::serial_port::native_handle_type handle = this->serial.native_handle();

#if __APPLE__
        termios ios;
        ::tcgetattr(handle, &ios);
        ::cfsetspeed(&ios, baud_rate);
        speed_t newSpeed = baud_rate;
        ioctl(handle, IOSSIOSPEED, &newSpeed);
        ::tcsetattr(handle, TCSANOW, &ios);
#elif __linux__
        termios2 ios;
        if (ioctl(handle, TCGETS2, &ios))
            printf("Error in TCGETS2: %s\n", strerror(errno));
        ios.c_ispeed = ios.c_ospeed = baud_rate;
        ios.c_cflag &= ~CBAUD;
        ios.c_cflag |= BOTHER | CLOCAL | CREAD;
        ios.c_cc[VMIN]  = 1; // Minimum of characters to read, prevents eof errors when 0 bytes are read
        ios.c_cc[VTIME] = 1;
        if (ioctl(handle, TCSETS2, &ios))
            printf("Error in TCSETS2: %s\n", strerror(errno));

#elif __OpenBSD__
        struct termios ios;
        ::tcgetattr(handle, &ios);
        ::cfsetspeed(&ios, baud_rate);
        if (::tcsetattr(handle, TCSAFLUSH, &ios) != 0)
            printf("Failed to set baud rate: %s\n", strerror(errno));
#else
        // throw Slic3r::InvalidArgument("OS does not currently support custom bauds");
#endif
    }
}

void GCodeSender::disconnect()
{
    if (!this->open)
        return;
    this->open      = false;
    this->connected = false;
    this->io.post(boost::bind(&GCodeSender::do_close, this));
    this->background_thread.join();
    this->io.reset();
    /*
    if (this->error_status()) {
        throw(boost::system::system_error(boost::system::error_code(),
            "Error while closing the device"));
    }
    */

#ifdef DEBUG_SERIAL
    fs << "DISCONNECTED" << std::endl << std::flush;
    fs.close();
#endif
}

bool GCodeSender::is_connected() const { return this->connected; }

bool GCodeSender::wait_connected(unsigned int timeout) const
{
    using namespace boost::posix_time;
    ptime t0 = second_clock::local_time() + seconds(timeout);
    while (!this->connected) {
        if (second_clock::local_time() > t0)
            return false;
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }
    return true;
}

size_t GCodeSender::queue_size() const
{
    boost::lock_guard<boost::mutex> l(this->queue_mutex);
    return this->queue.size();
}

void GCodeSender::pause_queue()
{
    boost::lock_guard<boost::mutex> l(this->queue_mutex);
    this->queue_paused = true;
}

void GCodeSender::resume_queue()
{
    {
        boost::lock_guard<boost::mutex> l(this->queue_mutex);
        this->queue_paused = false;
    }
    this->send();
}

void GCodeSender::purge_queue(bool priority)
{
    boost::lock_guard<boost::mutex> l(this->queue_mutex);
    if (priority) {
        // clear priority queue
        std::list<std::string> empty;
        std::swap(this->priqueue, empty);
    } else {
        // clear queue
        std::queue<std::string> empty;
        std::swap(this->queue, empty);
        this->queue_paused = false;
    }
}

// purge log and return its contents
std::vector<std::string> GCodeSender::purge_log()
{
    boost::lock_guard<boost::mutex> l(this->log_mutex);
    std::vector<std::string>        retval;
    retval.reserve(this->log.size());
    while (!this->log.empty()) {
        retval.push_back(this->log.front());
        this->log.pop();
    }
    return retval;
}

std::string GCodeSender::getT() const
{
    boost::lock_guard<boost::mutex> l(this->log_mutex);
    return this->T;
}

std::string GCodeSender::getB() const
{
    boost::lock_guard<boost::mutex> l(this->log_mutex);
    return this->B;
}

void GCodeSender::do_close()
{
    this->set_error_status(false);
    boost::system::error_code ec;
    this->serial.cancel(ec);
    if (ec)
        this->set_error_status(true);
    this->serial.close(ec);
    if (ec)
        this->set_error_status(true);
}

void GCodeSender::set_error_status(bool e)
{
    boost::lock_guard<boost::mutex> l(this->error_mutex);
    this->error = e;
}

bool GCodeSender::error_status() const
{
    boost::lock_guard<boost::mutex> l(this->error_mutex);
    return this->error;
}

void GCodeSender::do_read()
{
    // read one line
    boost::asio::async_read_until(this->serial, this->read_buffer, '\n',
                                  boost::bind(&GCodeSender::on_read, this, boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred));
}

// [INTENT] on_read(): ASIO callback for each '\n'-terminated line received from firmware.
//   Implements the Marlin ACK state machine:
//     1. Connection detection: "start", "Grbl ", "ok", or any line containing "T:" → connected=true
//     2. ACK processing: "ok" → can_send=true → triggers do_send() for next queued command
//     3. Resend handling: "Resend: N" or "rs N" → re-injects lines from last_sent[] into priqueue,
//        resets sent counter to N-1 so numbering resumes from N
//     4. Temperature scraping: scans every line for "T:" and "B:" → updates T and B fields
//     5. Other lines: pushed to log queue for display by UI
// [CONCURRENCY] Executes exclusively on ASIO background thread. can_send and queue state
//   are accessed under queue_mutex; T/B/log under log_mutex.
// [HAZARD] Connection heuristic is fragile: "T:" can appear in many firmware error messages
//   (e.g., "Error:Temperature sensor T:0 disconnected") — falsely triggering connected=true.
// [HAZARD] boost::starts_with(line, "ok") checks before resend check, meaning a line like
//   "ok T:200.0 B:60.0" correctly sets can_send AND updates temperature in one pass.
//   But "Resend:" lines never set can_send — firmware will deadlock if resend is never resolved.
// [HAZARD] Resend window check: `toresend > this->sent - this->last_sent.size()` may underflow
//   if sent < last_sent.size() (e.g., very early in print). This is an unsigned subtraction hazard.
// [STATE] macOS: error code 45 (ECANCELED from OS X Boost bug) triggers a silent retry
//   via another do_read() rather than closing — avoids false disconnect on macOS.
void GCodeSender::on_read(const boost::system::error_code& error, size_t bytes_transferred)
{
    this->set_error_status(false);
    if (error) {
#ifdef __APPLE__
        if (error.value() == 45) {
            // OS X bug: http://osdir.com/ml/lib.boost.asio.user/2008-08/msg00004.html
            this->do_read();
            return;
        }
#endif

        // printf("ERROR: [%d] %s\n", error.value(), error.message().c_str());
        // error can be true even because the serial port was closed.
        // In this case it is not a real error, so ignore.
        if (this->open) {
            this->do_close();
            this->set_error_status(true);
        }
        return;
    }

    std::istream is(&this->read_buffer);
    std::string  line;
    std::getline(is, line);
    if (!line.empty()) {
#ifdef DEBUG_SERIAL
        fs << "<< " << line << std::endl << std::flush;
#endif

        // note that line might contain \r at its end
        // parse incoming line
        if (!this->connected && (boost::starts_with(line, "start") || boost::starts_with(line, "Grbl ") || boost::starts_with(line, "ok") ||
                                 boost::contains(line, "T:"))) {
            this->connected = true;
            {
                boost::lock_guard<boost::mutex> l(this->queue_mutex);
                this->can_send = true;
            }
            this->send();
        } else if (boost::starts_with(line, "ok")) {
            {
                boost::lock_guard<boost::mutex> l(this->queue_mutex);
                this->can_send = true;
            }
            this->send();
        } else if (boost::istarts_with(line, "resend") // Marlin uses "Resend: "
                   || boost::istarts_with(line, "rs")) {
            // extract the first number from line
            boost::algorithm::trim_left_if(line, !boost::algorithm::is_digit());
            size_t toresend = boost::lexical_cast<size_t>(line.substr(0, line.find_first_not_of("0123456789")));

#ifdef DEBUG_SERIAL
            fs << "!! line num out of sync: toresend = " << toresend << ", sent = " << sent << ", last_sent.size = " << last_sent.size()
               << std::endl;
#endif

            if (toresend > this->sent - this->last_sent.size() && toresend <= this->sent) {
                {
                    boost::lock_guard<boost::mutex> l(this->queue_mutex);

                    const auto lines_to_resend = this->sent - toresend + 1;
#ifdef DEBUG_SERIAL
                    fs << "!! resending " << lines_to_resend << " lines" << std::endl;
#endif
                    // move the unsent lines to priqueue
                    this->priqueue.insert(this->priqueue.begin(), // insert at the beginning
                                          this->last_sent.begin() + this->last_sent.size() - lines_to_resend, this->last_sent.end());

                    // we can empty last_sent because it's not useful anymore
                    this->last_sent.clear();

                    // start resending with the requested line number
                    this->sent     = toresend - 1;
                    this->can_send = true;
                }
                this->send();
            } else {
                printf("Cannot resend %zu (oldest we have is %zu)\n", toresend, this->sent - this->last_sent.size());
            }
        } else if (boost::starts_with(line, "wait")) {
            // ignore
        } else {
            // push any other line into the log
            boost::lock_guard<boost::mutex> l(this->log_mutex);
            this->log.push(line);
        }

        // parse temperature info
        {
            size_t pos = line.find("T:");
            if (pos != std::string::npos && line.size() > pos + 2) {
                // we got temperature info
                boost::lock_guard<boost::mutex> l(this->log_mutex);
                this->T = line.substr(pos + 2, line.find_first_not_of("0123456789.", pos + 2) - (pos + 2));

                pos = line.find("B:");
                if (pos != std::string::npos && line.size() > pos + 2) {
                    // we got bed temperature info
                    this->B = line.substr(pos + 2, line.find_first_not_of("0123456789.", pos + 2) - (pos + 2));
                }
            }
        }
    }
    this->do_read();
}

void GCodeSender::send(const std::vector<std::string>& lines, bool priority)
{
    // append lines to queue
    {
        boost::lock_guard<boost::mutex> l(this->queue_mutex);
        for (std::vector<std::string>::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            if (priority) {
                this->priqueue.push_back(*line);
            } else {
                this->queue.push(*line);
            }
        }
    }
    this->send();
}

void GCodeSender::send(const std::string& line, bool priority)
{
    // append line to queue
    {
        boost::lock_guard<boost::mutex> l(this->queue_mutex);
        if (priority) {
            this->priqueue.push_back(line);
        } else {
            this->queue.push(line);
        }
    }
    this->send();
}

void GCodeSender::send() { this->io.post(boost::bind(&GCodeSender::do_send, this)); }

// [INTENT] do_send(): Core send-one-command function. Executes on ASIO thread via io.post().
//   Implements stop-and-wait: can_send must be true; set to false immediately on send.
//   Priority queue (priqueue, std::list) drains before normal queue (queue, std::queue).
//   Empty lines and comment-only lines are silently discarded here (not counted).
//   Builds Marlin line-numbered checksum format: "N<num> <cmd>*<cs>\n"
//   XOR checksum computed over "N<num> <cmd>" — standard Marlin protocol.
// [MEMORY] write_buffer is a boost::asio::streambuf (persistent across calls) — the full_line
//   string must be written to the streambuf before async_write() because stack allocation
//   would be freed before the async write completes. Source comment explicitly warns about this.
// [CONCURRENCY] Holds queue_mutex for entire duration of send setup. Because on_write()
//   calls do_send() (also on ASIO thread), recursive locking would deadlock — this is safe
//   only because ASIO single-threads all callbacks.
// [HAZARD] last_sent stores the raw command (without N/checksum). pop_front() on last_sent
//   when size > KEEP_SENT means commands older than 20 lines cannot be resent — firmware
//   resend requests for those lines will log an error and cannot be fulfilled.
void GCodeSender::do_send()
{
    boost::lock_guard<boost::mutex> l(this->queue_mutex);

    // printer is not connected or we're still waiting for the previous ack
    if (!this->can_send)
        return;

    std::string line;
    while (!this->priqueue.empty() || (!this->queue.empty() && !this->queue_paused)) {
        if (!this->priqueue.empty()) {
            line = this->priqueue.front();
            this->priqueue.pop_front();
        } else {
            line = this->queue.front();
            this->queue.pop();
        }

        // strip comments
        size_t comment_pos = line.find_first_of(';');
        if (comment_pos != std::string::npos)
            line.erase(comment_pos, std::string::npos);
        boost::algorithm::trim(line);

        // if line is not empty, send it
        if (!line.empty())
            break;
        // if line is empty, process next item in queue
    }
    if (line.empty())
        return;

    // compute full line
    ++this->sent;
#ifndef DEBUG_SERIAL
    const auto line_num = this->sent;
#else
    // In DEBUG_SERIAL mode, test line re-synchronization by sending bad line number 1/4 of the time
    const auto line_num = std::rand() < RAND_MAX / 4 ? 0 : this->sent;
#endif
    std::string full_line = "N" + boost::lexical_cast<std::string>(line_num) + " " + line;

    // calculate checksum
    int cs = 0;
    for (std::string::const_iterator it = full_line.begin(); it != full_line.end(); ++it)
        cs = cs ^ *it;

    // write line to device
    full_line += "*";
    full_line += boost::lexical_cast<std::string>(cs);
    full_line += "\n";

#ifdef DEBUG_SERIAL
    fs << ">> " << full_line << std::flush;
#endif

    this->last_sent.push_back(line);
    this->can_send = false;

    while (this->last_sent.size() > KEEP_SENT) {
        this->last_sent.pop_front();
    }

    // we can't supply boost::asio::buffer(full_line) to async_write() because full_line is on the
    // stack and the buffer would lose its underlying storage causing memory corruption
    std::ostream os(&this->write_buffer);
    os << full_line;
    boost::asio::async_write(this->serial, this->write_buffer,
                             boost::bind(&GCodeSender::on_write, this, boost::asio::placeholders::error,
                                         boost::asio::placeholders::bytes_transferred));
}

void GCodeSender::on_write(const boost::system::error_code& error, size_t bytes_transferred)
{
    this->set_error_status(false);
    if (error) {
        if (this->open) {
            this->do_close();
            this->set_error_status(true);
        }
        return;
    }

    this->do_send();
}

// [INTENT] set_DTR(): Asserts or de-asserts the DTR (Data Terminal Ready) line on the serial port.
//   Used to trigger hardware reset on Arduino/RAMPS-based printers: toggling DTR low-high-low
//   causes the ATmega to reset via the capacitor on the RESET pin.
// [COUPLING] WIN32 uses EscapeCommFunction(SETDTR/CLRDTR); POSIX uses TIOCMGET/TIOCMSET ioctl.
//   POSIX path performs a read-modify-write of the modem control register — not atomic.
// [CONCURRENCY] Called from connect() on the main thread (before background_thread starts).
//   Must not be called concurrently with ASIO thread once port is open.
void GCodeSender::set_DTR(bool on)
{
#if defined(_WIN32) && !defined(__SYMBIAN32__)
    boost::asio::serial_port_service::native_handle_type handle = this->serial.native_handle();
    if (on)
        EscapeCommFunction(handle, SETDTR);
    else
        EscapeCommFunction(handle, CLRDTR);
#else
    int fd = this->serial.native_handle();
    int status;
    ioctl(fd, TIOCMGET, &status);
    if (on)
        status |= TIOCM_DTR;
    else
        status &= ~TIOCM_DTR;
    ioctl(fd, TIOCMSET, &status);
#endif
}

// [INTENT] reset(): Performs the DTR toggle sequence to hardware-reset the connected printer.
//   Sequence: DTR low 200ms → DTR high 200ms → DTR low 500ms.
//   Timing mimics the Arduino bootloader reset dance. The 500ms final low gives firmware
//   time to boot before connect() posts the first do_read().
// [CONCURRENCY] Calls std::this_thread::sleep_for() — blocking the calling thread (main thread
//   during connect()), not the ASIO background thread.
void GCodeSender::reset()
{
    set_DTR(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    set_DTR(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    set_DTR(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

} // namespace Slic3r
