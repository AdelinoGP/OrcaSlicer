// [INTENT] Cross-platform signal/exception trapping facade.
// [HAZARD] On MSVC this file directly #includes TryCatchSignalSEH.cpp (line 4)
// – an unusual direct .cpp inclusion used to ensure SEH (__try/__except) is
// compiled in the same translation unit as the call site; this is required
// because MSVC prohibits mixing C++ object destruction with SEH in the same
// function, so the wrapper must not contain C++ destructors.
// On non-MSVC platforms the header-only implementation in TryCatchSignal.hpp
// handles signals via POSIX sigsetjmp/siglongjmp.
#include "TryCatchSignal.hpp"

#ifdef _MSC_VER
#include "TryCatchSignalSEH.cpp"
#endif
