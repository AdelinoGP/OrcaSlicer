#ifndef slic3r_format_hpp_
#define slic3r_format_hpp_

// [INTENT] Small variadic wrapper around `boost::format` so the rest of Orca can use function-call syntax
// instead of `%` chaining. This keeps legacy formatting call sites terse while the codebase remains on C++17.
// [COUPLING] GUI code can specialize `internal::format::cook()` to adapt foreign string types (for example
// wxString) before they reach Boost.Format.
// [HAZARD] `boost::format` throws on placeholder/type mismatches, so this helper is not a `fmt`-style
// constexpr-safe formatter and carries runtime overhead on every call.

// Functional wrapper around boost::format.
// One day we may replace this wrapper with C++20 format
// https://en.cppreference.com/w/cpp/utility/format/format
// though C++20 format uses a different template pattern for position independent parameters.
//
// Boost::format works around the missing variadic templates by an ugly % chaining operator. The usage of boost::format looks like this:
// (boost::format("template") % arg1 %arg2).str()
// This wrapper allows for a nicer syntax:
// Slic3r::format("template", arg1, arg2)
// One can also override Slic3r::internal::format::cook() function to convert a Slic3r::format() argument to something that
// boost::format may convert to string, see slic3r/GUI/I18N.hpp for a "cook" function to convert wxString to UTF8.

#include <boost/format.hpp>

namespace Slic3r {

// https://gist.github.com/gchudnov/6a90d51af004d97337ec
namespace internal { namespace format {
// [INTENT] Default adapter hook: pass arguments through unchanged unless another module overloads
// `cook()` for a more GUI-specific type conversion.
// Default "cook" function - just forward.
template<typename T> inline T&& cook(T&& arg) { return std::forward<T>(arg); }

// End of the recursive chain.
inline std::string format_recursive(boost::format& message) { return message.str(); }

template<typename TValue, typename... TArgs> std::string format_recursive(boost::format& message, TValue&& arg, TArgs&&... args)
{
    // [INTENT] Feed one argument at a time into Boost.Format because pre-C++20 code could not express
    // this as a clean fold over a safer formatting backend.
    // Format, possibly convert the argument by the "cook" function.
    message % cook(std::forward<TValue>(arg));
    return format_recursive(message, std::forward<TArgs>(args)...);
}
}}; // namespace internal::format

template<typename... TArgs> inline std::string format(const char* fmt, TArgs&&... args)
{
    // [INTENT] Construct a fresh formatter per call. `boost::format` keeps mutable placeholder state, so
    // sharing one instance across calls would be error-prone and thread-unsafe.
    boost::format message(fmt);
    return internal::format::format_recursive(message, std::forward<TArgs>(args)...);
}

template<typename... TArgs> inline std::string format(const std::string& fmt, TArgs&&... args)
{
    boost::format message(fmt);
    return internal::format::format_recursive(message, std::forward<TArgs>(args)...);
}

} // namespace Slic3r

#endif // slic3r_format_hpp_
