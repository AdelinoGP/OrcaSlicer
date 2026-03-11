#ifndef libslic3r_Timer_hpp_
#define libslic3r_Timer_hpp_

#include <string>
#include <chrono>

namespace Slic3r {

// [INTENT] RAII timer for measuring code block execution time.
// Automatically starts timing on construction and logs elapsed time on destruction.
// [MEMORY] Uses std::chrono::steady_clock - monotonic timer not affected by system clock changes.
// [COUPLING] Likely uses Boost::log for output - check implementation for logging dependency.
class Timer
{
    std::string                           m_name;  // [STATE] Stored name for debug output
    std::chrono::steady_clock::time_point m_start; // [MEMORY] Captured at construction
public:
    Timer(const std::string& name);
    ~Timer();
};

// [INTENT] Timing utilities for performance measurement and testing.
// [COUPLING] Borrowed from Catch2 unit testing library - may need replacement for non-C++ targets.
namespace Timing {

// [INTENT] Get current time in nanoseconds since epoch using high-resolution clock.
// [MEMORY] Returns uint64_t - safe until year 292277026596 (far future).
// [HAZARD] Uses std::chrono::high_resolution_clock which may be implementation-defined
// and could alias to steady_clock or system_clock.
static inline uint64_t nanoseconds_since_epoch()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

// [INTENT] Simple timer class for manual start/stop timing.
// Used in unit tests and performance measurement.
class Timer
{
public:
    void start() { m_nanoseconds = nanoseconds_since_epoch(); }

    // [MEMORY] Computed difference - no state mutation
    uint64_t elapsed_nanoseconds() const { return nanoseconds_since_epoch() - m_nanoseconds; }
    uint64_t elapsed_microseconds() const { return elapsed_nanoseconds() / 1000; }

    // [HAZARD] Conversion to unsigned int may truncate on very long durations.
    unsigned int elapsed_milliseconds() const { return static_cast<unsigned int>(elapsed_microseconds() / 1000); }

    double elapsed_seconds() const { return elapsed_microseconds() / 1000000.0; }

private:
    uint64_t m_nanoseconds = 0;
};

// [INTENT] RAII timer that checks if code exceeds time limit - logs error if exceeded.
// Useful for performance regression testing and detecting slow operations.
// [COUPLING] Uses Boost::log for error reporting - must be ported or replaced.
class TimeLimitAlarm
{
public:
    // [MEMORY] Constructor starts timer via m_timer.start() - RAII pattern.
    TimeLimitAlarm(uint64_t time_limit_nanoseconds, std::string_view limit_exceeded_message)
        : m_time_limit_nanoseconds(time_limit_nanoseconds), m_limit_exceeded_message(limit_exceeded_message)
    {
        m_timer.start();
    }

    // [INTENT] Destructor checks elapsed time - if exceeded, reports to logging system.
    ~TimeLimitAlarm()
    {
        auto elapsed = m_timer.elapsed_nanoseconds();
        if (elapsed > m_time_limit_nanoseconds)
            this->report_time_exceeded();
    }

    // Factory methods for different time units - improves readability at call site.
    static TimeLimitAlarm new_nanos(uint64_t time_limit_nanoseconds, std::string_view limit_exceeded_message)
    {
        return TimeLimitAlarm(time_limit_nanoseconds, limit_exceeded_message);
    }
    static TimeLimitAlarm new_milis(uint64_t time_limit_milis, std::string_view limit_exceeded_message)
    {
        return TimeLimitAlarm(uint64_t(time_limit_milis) * 1000000l, limit_exceeded_message);
    }
    static TimeLimitAlarm new_seconds(uint64_t time_limit_seconds, std::string_view limit_exceeded_message)
    {
        return TimeLimitAlarm(uint64_t(time_limit_seconds) * 1000000000l, limit_exceeded_message);
    }

private:
    void report_time_exceeded() const;

    Timer            m_timer;
    uint64_t         m_time_limit_nanoseconds;
    std::string_view m_limit_exceeded_message;
};

} // namespace Timing

} // namespace Slic3r

#endif // libslic3r_Timer_hpp_
