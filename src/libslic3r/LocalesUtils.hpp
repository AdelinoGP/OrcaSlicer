#ifndef slic3r_LocalesUtils_hpp_
#define slic3r_LocalesUtils_hpp_

#include <string>
#include <clocale>
#include <iomanip>
#include <cassert>
#include <string_view>

#ifdef __APPLE__
#include <xlocale.h>
#endif

namespace Slic3r {

// [INTENT] Scope-bound locale guard for numeric parsing/formatting paths that must
//          stay culture-invariant (decimal point '.') while preserving caller locale.
// [STATE]  Constructor mutates process/thread locale state, destructor restores it.
// [CONCURRENCY] Locale mutation semantics are platform specific: Win32 uses per-thread
//               locale mode, POSIX uses thread-local locale handles via uselocale().
// [MEMORY] POSIX variants own locale_t handles that must be released in the dtor.
// [HAZARD] Failure in platform locale APIs can leave invalid locale_t handles; callers
//          assume construction succeeded and do not have an explicit error channel.
class CNumericLocalesSetter
{
public:
    CNumericLocalesSetter();
    ~CNumericLocalesSetter();

private:
#ifdef _WIN32
    std::string m_orig_numeric_locale;
#else
    locale_t m_original_locale;
    locale_t m_new_locale;
#endif
};

// A function to check that current C locale uses decimal point as a separator.
// Intended mostly for asserts.
bool is_decimal_separator_point();

// A substitute for std::to_string that works according to
// C++ locales, not C locale. Meant to be used when we need
// to be sure that decimal point is used as a separator.
// (We use user C locales and "C" C++ locales in most of the code.)
// [COUPLING] Shared by config/G-code serializers that require locale-agnostic
//            decimal formatting independent of OS/user locale preferences.
std::string float_to_string_decimal_point(double value, int precision = -1);
// std::string float_to_string_decimal_point(float value,  int precision = -1);
//  [STATE] Optional out-parameter 'pos' reports parse stop position to caller.
//  [HAZARD] Parser expects '.' decimal separator and intentionally does not accept ','.
double string_to_double_decimal_point(const std::string_view str, size_t* pos = nullptr);

} // namespace Slic3r

#endif // slic3r_LocalesUtils_hpp_
