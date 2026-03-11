#ifndef slic3r_Utils_Time_hpp_
#define slic3r_Utils_Time_hpp_

#include <string>
#include <ctime>

namespace Slic3r { namespace Utils {

// [INTENT] Time and timestamp utilities for the slicer.
// Provides UTC/local time conversion, ISO 8601 formatting, and millisecond-precision timestamps.
// Used for: G-code timestamps, file naming, cloud sync protocols.
// [COUPLING] Uses standard C time library (ctime). Thread-safe implementations use internal locking.

// [INTENT] Get current time as UTC time_t. Thread-safe implementation required.
// [MEMORY] Returns time_t by value - 4 or 8 bytes depending on platform.
time_t get_current_time_utc();

enum class TimeZone { local, utc };
enum class TimeFormat { gcode, iso8601Z };

// [INTENT] Convert time_t to formatted string with timezone and format options.
// [MEMORY] Returns string by value - SSO expected for typical timestamp formats.
std::string time2str(const time_t& t, TimeZone zone, TimeFormat fmt);

inline std::string time2str(TimeZone zone, TimeFormat fmt) { return time2str(get_current_time_utc(), zone, fmt); }

inline std::string utc_timestamp(time_t t) { return time2str(t, TimeZone::utc, TimeFormat::gcode); }

inline std::string utc_timestamp() { return utc_timestamp(get_current_time_utc()); }

inline std::string local_timestamp(TimeFormat fmt = TimeFormat::gcode) { return time2str(get_current_time_utc(), TimeZone::local, fmt); }

// [INTENT] Parse string to time_t. Returns -1 on parse failure.
// [HAZARD] Error handling via sentinel value (-1) - may conflict with valid timestamps on some platforms.
time_t str2time(const std::string& str, TimeZone zone, TimeFormat fmt);

// /////////////////////////////////////////////////////////////////////////////
// Utilities to convert an UTC time_t to/from an ISO8601 time format,
// useful for putting timestamps into file and directory names.
// Returns (time_t)-1 on error.

// Use these functions to convert safely to and from the ISO8601 format on
// all platforms

inline std::string iso_utc_timestamp(time_t t) { return time2str(t, TimeZone::utc, TimeFormat::iso8601Z); }

inline std::string iso_utc_timestamp() { return iso_utc_timestamp(get_current_time_utc()); }

inline time_t parse_iso_utc_timestamp(const std::string& str) { return str2time(str, TimeZone::utc, TimeFormat::iso8601Z); }

// /////////////////////////////////////////////////////////////////////////////
// Millisecond timestamps for cloud sync protocol
// Format: "2025-11-28T14:30:00.123Z" (ISO 8601 with milliseconds)

// [INTENT] Millisecond-precision timestamps for cloud sync protocol.
// ISO 8601 format with milliseconds: "YYYY-MM-DDTHH:MM:SS.sssZ"
// Used by Bambu Cloud and other network protocols requiring sub-second precision.
// [HAZARD] Uses long long (64-bit) - portable but assumes Unix epoch.
std::string millis_to_iso8601(long long unix_millis);
long long   iso8601_to_millis(const std::string& iso_time); // Returns -1 on parse error

// /////////////////////////////////////////////////////////////////////////////

}} // namespace Slic3r::Utils

#endif /* slic3r_Utils_Time_hpp_ */
