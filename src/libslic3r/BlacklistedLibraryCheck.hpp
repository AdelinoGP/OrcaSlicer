#ifndef slic3r_BlacklistedLibraryCheck_hpp_
#define slic3r_BlacklistedLibraryCheck_hpp_

#ifdef WIN32
#include <windows.h>
#include <vector>
#include <string>
#endif // WIN32

namespace Slic3r {

#ifdef WIN32
// [INTENT] Runtime guard against known-incompatible injected DLLs on Windows.
// The check runs before critical subsystems initialize to fail fast with a
// deterministic error instead of hard-to-debug crashes.
// [COUPLING] Windows-only implementation depends on module enumeration APIs.
// [HAZARD] Singleton stores mutable process-wide state and assumes single-threaded
// initialization ordering.
class BlacklistedLibraryCheck
{
public:
    static BlacklistedLibraryCheck& get_instance()
    {
        static BlacklistedLibraryCheck instance;

        return instance;
    }

private:
    BlacklistedLibraryCheck() = default;

    // [STATE] m_found caches blacklist hits from the latest scan.
    std::vector<std::wstring> m_found;

public:
    BlacklistedLibraryCheck(BlacklistedLibraryCheck const&) = delete;
    void operator=(BlacklistedLibraryCheck const&)          = delete;
    // returns all found blacklisted dlls
    bool         get_blacklisted(std::vector<std::wstring>& names);
    std::wstring get_blacklisted_string();
    // returns true if enumerating found blacklisted dll
    bool perform_check();

    // UTF-8 encoded path
    // [INTENT] Dual overload keeps path checks consistent for UTF-8 sources and
    // Win32 wide-char APIs without forcing callers to convert eagerly.
    static bool is_blacklisted(const std::string& dllpath);
    static bool is_blacklisted(const std::wstring& dllpath);

private:
    static const std::vector<std::wstring> blacklist;
};

#endif // WIN32

} // namespace Slic3r

#endif // slic3r_BlacklistedLibraryCheck_hpp_
