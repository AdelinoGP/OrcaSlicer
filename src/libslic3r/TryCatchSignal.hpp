#ifndef TRY_CATCH_SIGNAL_HPP
#define TRY_CATCH_SIGNAL_HPP

// [INTENT] Cross-platform signal handling wrapper for exception catching.
// Uses SEH on Windows (structured exception handling), standard signals elsewhere.
// [COUPLING] Platform-specific implementation - Windows uses SEH, others use csignal.
// [HAZARD] On non-Windows, the catch callback is unused - dead code path.

#ifdef _MSC_VER
#include "TryCatchSignalSEH.hpp"
#else

#include <csignal>

using SignalT = decltype(SIGSEGV);

// [INTENT] Template wrapper - on non-MSVC, just executes fn() without signal handling.
// The signal handler setup is essentially a no-op on POSIX systems in this implementation.
template<class TryFn, class CatchFn, int N> void try_catch_signal(const SignalT (& /*sigs*/)[N], TryFn&& fn, CatchFn&& /*cfn*/) { fn(); }
#endif

#endif // TRY_CATCH_SIGNAL_HPP
