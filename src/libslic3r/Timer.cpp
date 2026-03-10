// [INTENT] RAII scoped-timer that logs elapsed milliseconds at destruction
// (debug level) and a time-limit alarm that logs an error when exceeded.
// [STATE] m_start captured at construction; no shared state.
// [COUPLING] Boost.Log trivial logging only.
#include "Timer.hpp"
#include <boost/log/trivial.hpp>

using namespace std::chrono;

Slic3r::Timer::Timer(const std::string& name) : m_name(name), m_start(steady_clock::now()) {}

Slic3r::Timer::~Timer()
{
    BOOST_LOG_TRIVIAL(debug) << "Timer '" << m_name << "' spend " << duration_cast<milliseconds>(steady_clock::now() - m_start).count()
                             << "ms";
}

namespace Slic3r::Timing {

void TimeLimitAlarm::report_time_exceeded() const
{
    BOOST_LOG_TRIVIAL(error) << "Time limit exceeded for " << m_limit_exceeded_message << ": " << m_timer.elapsed_seconds() << "s";
}

} // namespace Slic3r::Timing
