// [INTENT] Serial port enumeration and communication for direct printer connections.
// [COUPLING] Uses Slic3r::RuntimeError from libslic3r/Exception.hpp for error reporting.
// [HAZARD] Heavy platform-specific code: Windows SetupAPI, macOS IOKit, Linux sysfs.
//          Each path has different error handling conventions and memory management.

#include "Serial.hpp"

#include "libslic3r/Exception.hpp"

#include <algorithm>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <fstream>
#include <exception>
#include <stdexcept>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>

#if _WIN32
#include <Windows.h>
#include <Setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <regex>
// [HAZARD] Windows.h defines min/max macros that conflict with std::min/std::max.
//          Must undefine before including standard library headers.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "boost/nowide/convert.hpp"
#pragma comment(lib, "user32.lib")
#elif __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFString.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>
#include <sys/syslimits.h>
#endif

#ifndef _WIN32
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <sys/select.h>
#endif

#if defined(__APPLE__) || defined(__OpenBSD__)
#include <termios.h>
#elif defined __linux__
#include <fcntl.h>
#include <asm-generic/ioctls.h>
#endif

using boost::optional;

namespace Slic3r { namespace Utils {

// [INTENT] Heuristic to detect Prusa printers by friendly name.
// [COUPLING] Used by scan_serial_ports_extended() on all platforms.
static bool looks_like_printer(const std::string& friendly_name) { return friendly_name.find("Original Prusa") != std::string::npos; }

#if _WIN32
// [INTENT] Parse USB VID/PID from Windows hardware ID string using regex.
// [HAZARD] Regex pattern assumes specific USB\\VID_xxxx&PID_xxxx format.
void parse_hardware_id(const std::string& hardware_id, SerialPortInfo& spi)
{
    unsigned    vid, pid;
    std::regex  pattern("USB\\\\.*VID_([[:xdigit:]]+)&PID_([[:xdigit:]]+).*");
    std::smatch matches;
    if (std::regex_match(hardware_id, matches, pattern)) {
        vid            = std::stoul(matches[1].str(), 0, 16);
        pid            = std::stoul(matches[2].str(), 0, 16);
        spi.id_vendor  = vid;
        spi.id_product = pid;
    }
}
#endif

#ifdef __linux__
// [INTENT] Read sysfs property for a TTY device (e.g., idVendor, idProduct).
// [HAZARD] Path traversal relies on kernel sysfs structure - fragile across kernel versions.
optional<std::string> sysfs_tty_prop(const std::string& tty_dev, const std::string& name)
{
    const auto    prop_path = (boost::format("/sys/class/tty/%1%/device/../%2%") % tty_dev % name).str();
    std::ifstream file(prop_path);
    std::string   res;

    std::getline(file, res);
    if (file.good()) {
        return res;
    } else {
        return boost::none;
    }
}

// [INTENT] Read hex property from sysfs, parse as unsigned long.
optional<unsigned long> sysfs_tty_prop_hex(const std::string& tty_dev, const std::string& name)
{
    auto prop = sysfs_tty_prop(tty_dev, name);
    if (!prop) {
        return boost::none;
    }

    try {
        return std::stoul(*prop, 0, 16);
    } catch (const std::exception&) {
        return boost::none;
    }
}
#endif

#ifdef __linux__
optional<std::string> sysfs_tty_prop(const std::string& tty_dev, const std::string& name)
{
    const auto    prop_path = (boost::format("/sys/class/tty/%1%/device/../%2%") % tty_dev % name).str();
    std::ifstream file(prop_path);
    std::string   res;

    std::getline(file, res);
    if (file.good()) {
        return res;
    } else {
        return boost::none;
    }
}

optional<unsigned long> sysfs_tty_prop_hex(const std::string& tty_dev, const std::string& name)
{
    auto prop = sysfs_tty_prop(tty_dev, name);
    if (!prop) {
        return boost::none;
    }

    try {
        return std::stoul(*prop, 0, 16);
    } catch (const std::exception&) {
        return boost::none;
    }
}
#endif

// [INTENT] Enumerate all available serial ports on the system.
// [HAZARD] Platform-specific implementations: Windows SetupAPI, macOS IOKit, Linux /dev scanning.
// [MEMORY] Returns vector by value; SerialPortInfo is a simple struct with string fields.
std::vector<SerialPortInfo> scan_serial_ports_extended()
{
    std::vector<SerialPortInfo> output;

#ifdef _WIN32
    // [INTENT] Windows: use SetupAPI to enumerate PORTS device class.
    // [HAZARD] SetupAPI handles must be closed; registry keys must be closed.
    // [MEMORY] HDEVINFO handle owned by this scope - cleaned up implicitly by OS.
    SP_DEVINFO_DATA devInfoData = {0};
    devInfoData.cbSize          = sizeof(devInfoData);
    HDEVINFO hDeviceInfo        = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, nullptr, DIGCF_PRESENT);
    if (hDeviceInfo != INVALID_HANDLE_VALUE) {
        for (int nDevice = 0; SetupDiEnumDeviceInfo(hDeviceInfo, nDevice, &devInfoData); ++nDevice) {
            SerialPortInfo port_info;
            HKEY           hDeviceKey = SetupDiOpenDevRegKey(hDeviceInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
            if (hDeviceKey) {
                wchar_t pszPortName[4096];
                DWORD   dwSize = sizeof(pszPortName);
                DWORD   dwType = 0;
                if (RegQueryValueEx(hDeviceKey, L"PortName", NULL, &dwType, (LPBYTE) pszPortName, &dwSize) == ERROR_SUCCESS)
                    port_info.port = boost::nowide::narrow(pszPortName);
                RegCloseKey(hDeviceKey);
                if (port_info.port.empty())
                    continue;
            }

            // Find the size required to hold the device info.
            DWORD regDataType;
            DWORD reqSize = 0;
            SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, nullptr, 0, &reqSize);
            std::vector<wchar_t> hardware_id(reqSize > 1 ? reqSize : 1);
            // Now store it in a buffer.
            if (!SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_HARDWAREID, &regDataType, (BYTE*) hardware_id.data(),
                                                  reqSize, nullptr))
                continue;
            parse_hardware_id(boost::nowide::narrow(hardware_id.data()), port_info);

            // Find the size required to hold the friendly name.
            reqSize = 0;
            SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_FRIENDLYNAME, nullptr, nullptr, 0, &reqSize);
            std::vector<wchar_t> friendly_name;
            friendly_name.reserve(reqSize > 1 ? reqSize : 1);
            // Now store it in a buffer.
            if (!SetupDiGetDeviceRegistryProperty(hDeviceInfo, &devInfoData, SPDRP_FRIENDLYNAME, nullptr, (BYTE*) friendly_name.data(),
                                                  reqSize, nullptr)) {
                port_info.friendly_name = port_info.port;
            } else {
                port_info.friendly_name = boost::nowide::narrow(friendly_name.data());
                port_info.is_printer    = looks_like_printer(port_info.friendly_name);
            }
            output.emplace_back(std::move(port_info));
        }
    }
#elif __APPLE__
    // [INTENT] macOS: use IOKit to enumerate serial ports via IOSerialBSD service matching.
    // [HAZARD] CoreFoundation objects have manual reference counting (CFRelease must be called).
    // [MEMORY] CFTypeRef objects are owned by caller - must CFRelease each to avoid leaks.
    CFMutableDictionaryRef classes = IOServiceMatching(kIOSerialBSDServiceValue);
    if (classes != 0) {
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMasterPortDefault, classes, &iter) == KERN_SUCCESS) {
            io_object_t port;
            while ((port = IOIteratorNext(iter)) != 0) {
                CFTypeRef cf_property = IORegistryEntryCreateCFProperty(port, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
                if (cf_property) {
                    char    path[PATH_MAX];
                    Boolean result = CFStringGetCString((CFStringRef) cf_property, path, sizeof(path), kCFStringEncodingUTF8);
                    CFRelease(cf_property);
                    if (result) {
                        SerialPortInfo port_info;
                        port_info.port = path;

                        // [INTENT] Try multiple property names to find friendly name.
                        // Cascades from USB-specific names to generic TTY device name.
                        if ((cf_property = IORegistryEntrySearchCFProperty(port, kIOServicePlane, CFSTR("USB Interface Name"),
                                                                           kCFAllocatorDefault,
                                                                           kIORegistryIterateRecursively | kIORegistryIterateParents)) ||
                            (cf_property = IORegistryEntrySearchCFProperty(port, kIOServicePlane, CFSTR("USB Product Name"),
                                                                           kCFAllocatorDefault,
                                                                           kIORegistryIterateRecursively | kIORegistryIterateParents)) ||
                            (cf_property = IORegistryEntrySearchCFProperty(port, kIOServicePlane, CFSTR("Product Name"), kCFAllocatorDefault,
                                                                           kIORegistryIterateRecursively | kIORegistryIterateParents)) ||
                            (cf_property = IORegistryEntryCreateCFProperty(port, CFSTR(kIOTTYDeviceKey), kCFAllocatorDefault, 0))) {
                            char description[128];
                            if (CFStringGetCString((CFStringRef) cf_property, description, sizeof(description), kCFStringEncodingUTF8)) {
                                port_info.friendly_name = std::string(description) + " (" + port_info.port + ")";
                                port_info.is_printer    = looks_like_printer(port_info.friendly_name);
                            }
                            CFRelease(cf_property);
                        }
                        if (port_info.friendly_name.empty())
                            port_info.friendly_name = port_info.port;

                        // [INTENT] Extract USB VID/PID from IOKit registry for device identification.
                        int  vid, pid;
                        auto cf_vendor  = IORegistryEntrySearchCFProperty(port, kIOServicePlane, CFSTR("idVendor"), kCFAllocatorDefault,
                                                                          kIORegistryIterateRecursively | kIORegistryIterateParents);
                        auto cf_product = IORegistryEntrySearchCFProperty(port, kIOServicePlane, CFSTR("idProduct"), kCFAllocatorDefault,
                                                                          kIORegistryIterateRecursively | kIORegistryIterateParents);
                        if (cf_vendor && cf_product) {
                            if (CFNumberGetValue((CFNumberRef) cf_vendor, kCFNumberIntType, &vid) &&
                                CFNumberGetValue((CFNumberRef) cf_product, kCFNumberIntType, &pid)) {
                                port_info.id_vendor  = vid;
                                port_info.id_product = pid;
                            }
                        }
                        if (cf_vendor) {
                            CFRelease(cf_vendor);
                        }
                        if (cf_product) {
                            CFRelease(cf_product);
                        }

                        output.emplace_back(std::move(port_info));
                    }
                }
                IOObjectRelease(port);
            }
        }
    }
#else
    // [INTENT] Linux/Unix: scan /dev for common serial port device name prefixes.
    // [HAZARD] Relies on filesystem naming conventions - may miss non-standard ports.
    std::initializer_list<const char*> prefixes{"ttyUSB", "ttyACM", "tty.", "cu.", "rfcomm"};
    for (auto& dir_entry : boost::filesystem::directory_iterator(boost::filesystem::path("/dev"))) {
        std::string name = dir_entry.path().filename().string();
        for (const char* prefix : prefixes) {
            if (boost::starts_with(name, prefix)) {
                const auto     path = dir_entry.path().string();
                SerialPortInfo spi;
                spi.port = path;
#ifdef __linux__
                auto friendly_name = sysfs_tty_prop(name, "product");
                if (friendly_name) {
                    spi.is_printer    = looks_like_printer(*friendly_name);
                    spi.friendly_name = (boost::format("%1% (%2%)") % *friendly_name % path).str();
                } else {
                    spi.friendly_name = path;
                }
                auto vid = sysfs_tty_prop_hex(name, "idVendor");
                auto pid = sysfs_tty_prop_hex(name, "idProduct");
                if (vid && pid) {
                    spi.id_vendor  = *vid;
                    spi.id_product = *pid;
                }
#else
                spi.friendly_name = path;
#endif
                output.emplace_back(std::move(spi));
                break;
            }
        }
    }
#endif

    // [INTENT] Filter out Bluetooth and FireFly virtual ports (not physical printers).
    output.erase(std::remove_if(output.begin(), output.end(),
                                [](const SerialPortInfo& info) {
                                    return boost::starts_with(info.port, "Bluetooth") || boost::starts_with(info.port, "FireFly");
                                }),
                 output.end());
    return output;
}

// [INTENT] Simplified wrapper returning just port paths without metadata.
std::vector<std::string> scan_serial_ports()
{
    std::vector<SerialPortInfo> ports = scan_serial_ports_extended();
    std::vector<std::string>    output;
    output.reserve(ports.size());
    for (const SerialPortInfo& spi : ports)
        output.emplace_back(std::move(spi.port));
    return output;
}

// [INTENT] Serial class wraps Boost.Asio serial_port for async I/O.
// [COUPLING] Inherits from boost::asio::serial_port - tight coupling to Boost.Asio API.
// [HAZARD] Asio API varies by Boost version (get_io_service vs get_executor).
namespace asio = boost::asio;
using boost::system::error_code;

Serial::Serial(asio::io_service& io_service) : asio::serial_port(io_service) {}

Serial::Serial(asio::io_service& io_service, const std::string& name, unsigned baud_rate) : asio::serial_port(io_service, name)
{
    set_baud_rate(baud_rate);
}

Serial::~Serial() {}

// [INTENT] Set custom baud rate, including rates > 115200 which ASIO doesn't support natively.
// [HAZARD] Platform-specific ioctl calls for non-standard baud rates.
//          Falls back to termios/ioctl when ASIO's set_option fails.
// [COUPLING] Throws Slic3r::RuntimeError from libslic3r on failure.
void Serial::set_baud_rate(unsigned baud_rate)
{
    try {
        // This does not support speeds > 115200
        set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
    } catch (boost::system::system_error&) {
        auto handle = native_handle();

        // [INTENT] Lambda wraps errno check, throws Slic3r::RuntimeError on failure.
        auto handle_errno = [](int retval) {
            if (retval != 0) {
                throw Slic3r::RuntimeError((boost::format("Could not set baud rate: %1%") % strerror(errno)).str());
            }
        };

#if __APPLE__
        termios ios;
        handle_errno(::tcgetattr(handle, &ios));
        handle_errno(::cfsetspeed(&ios, baud_rate));
        speed_t newSpeed = baud_rate;
        handle_errno(::ioctl(handle, IOSSIOSPEED, &newSpeed));
        handle_errno(::tcsetattr(handle, TCSANOW, &ios));
#elif __linux__

        // [INTENT] Linux: use termios2 struct with TCSETS2/TCGETS2 ioctls for arbitrary baud rates.
        // [HAZARD] Inline struct definition to avoid conflict with system termios (see comment).
        //          This is a kernel ABI dependency - termios2 layout must match kernel's struct.
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

        termios2 ios;
        handle_errno(::ioctl(handle, TCGETS2, &ios));
        ios.c_ispeed = ios.c_ospeed = baud_rate;
        ios.c_cflag &= ~CBAUD;
        ios.c_cflag |= BOTHER | CLOCAL | CREAD;
        ios.c_cc[VMIN]  = 1; // Minimum of characters to read, prevents eof errors when 0 bytes are read
        ios.c_cc[VTIME] = 1;
        handle_errno(::ioctl(handle, TCSETS2, &ios));

#elif __OpenBSD__
        struct termios ios;
        handle_errno(::tcgetattr(handle, &ios));
        handle_errno(::cfsetspeed(&ios, baud_rate));
        handle_errno(::tcsetattr(handle, TCSAFLUSH, &ios));
#else
        throw Slic3r::RuntimeError("Custom baud rates are not currently supported on this OS");
#endif
    }
}

// [INTENT] Commented-out code block contains legacy printer communication methods.
// [HAZARD] set_DTR() uses platform-specific modem control lines (DTR signal).
//          Proper reset sequence requires timing delays between DTR toggles.
/*
void Serial::set_DTR(bool on)
{
    auto handle = native_handle();
#if defined(_WIN32) && !defined(__SYMBIAN32__)
    if (! EscapeCommFunction(handle, on ? SETDTR : CLRDTR)) {
        throw Slic3r::RuntimeError("Could not set serial port DTR");
    }
#else
    int status;
    if (::ioctl(handle, TIOCMGET, &status) == 0) {
        on ? status |= TIOCM_DTR : status &= ~TIOCM_DTR;
        if (::ioctl(handle, TIOCMSET, &status) == 0) {
            return;
        }
    }

    throw Slic3r::RuntimeError(
        (boost::format("Could not set serial port DTR: %1%") % strerror(errno)).str()
    );
#endif
}

void Serial::reset_line_num()
{
    // See https://github.com/MarlinFirmware/Marlin/wiki/M110
    write_string("M110 N0\n");
    m_line_num = 0;
}

// [INTENT] Async read with timeout - reads one character at a time until newline.
// [HAZARD] Boost.Asio API changed in 1.70+: get_executor().context() vs get_io_service().
//          The FIXME comment indicates this may be incorrect for newer Boost versions.
// [CONCURRENCY] Uses io_service.run() in a loop - not truly async, blocks current thread.
bool Serial::read_line(unsigned timeout, std::string &line, error_code &ec)
{
    auto& io_service =
#if BOOST_VERSION >= 107000
        //FIXME this is most certainly wrong!
        (boost::asio::io_context&)this->get_executor().context();
 #else
        this->get_io_service();
#endif
    asio::deadline_timer timer(io_service);
    char c = 0;
    bool fail = false;

    while (true) {
        io_service.reset();

        asio::async_read(*this, boost::asio::buffer(&c, 1), [&](const error_code &read_ec, size_t size) {
            if (ec || size == 0) {
                fail = true;
                ec = read_ec;   // FIXME: only if operation not aborted
            }
            timer.cancel();   // FIXME: ditto
        });

        if (timeout > 0) {
            timer.expires_from_now(boost::posix_time::milliseconds(timeout));
            timer.async_wait([&](const error_code &ec) {
                // Ignore timer aborts
                if (!ec) {
                    fail = true;
                    this->cancel();
                }
            });
        }

        io_service.run();

        if (fail) {
            return false;
        } else if (c != '\n') {
            line += c;
        } else {
            return true;
        }
    }
}

void Serial::printer_setup()
{
    printer_reset();
    write_string("\r\r\r\r\r\r\r\r\r\r");    // Gets rid of line noise, if any
}

size_t Serial::write_string(const std::string &str)
{
    // TODO: might be wise to timeout here as well
    return asio::write(*this, asio::buffer(str));
}

// [INTENT] Wait for printer to respond with "ok" - part of Marlin serial protocol.
// [HAZARD] Relies on line numbering (M110/M111) which must be synchronized with printer.
bool Serial::printer_ready_wait(unsigned retries, unsigned timeout)
{
    std::string line;
    error_code ec;

    for (; retries > 0; retries--) {
        reset_line_num();

        while (read_line(timeout, line, ec)) {
            if (line == "ok") {
                return true;
            }
            line.clear();
        }

        line.clear();
    }

    return false;
}

// [INTENT] Send line with checksum (N### line\r\n format for Marlin).
size_t Serial::printer_write_line(const std::string &line, unsigned line_num)
{
    const auto formatted_line = Utils::Serial::printer_format_line(line, line_num);
    return write_string(formatted_line);
}

size_t Serial::printer_write_line(const std::string &line)
{
    m_line_num++;
    return printer_write_line(line, m_line_num);
}

// [INTENT] Hardware reset via DTR toggle - resets Arduino/printer controllers.
// [HAZARD] Relies on precise timing delays; may not work on all USB-serial adapters.
void Serial::printer_reset()
{
    this->set_DTR(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    this->set_DTR(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    this->set_DTR(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

// [INTENT] Compute Marlin-style checksum: XOR of all bytes in "N### line".
std::string Serial::printer_format_line(const std::string &line, unsigned line_num)
{
    const auto line_num_str = std::to_string(line_num);

    unsigned checksum = 'N';
    for (auto c : line_num_str) { checksum ^= c; }
    checksum ^= ' ';
    for (auto c : line) { checksum ^= c; }

    return (boost::format("N%1% %2%*%3%\n") % line_num_str % line % checksum).str();
}
*/

}} // namespace Slic3r::Utils
