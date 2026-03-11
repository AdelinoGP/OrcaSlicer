// [INTENT] Header for Windows crash/exception handler that captures stack traces.
// Extends CStackWalker to add exception-specific dump functionality.
// [HAZARD] Windows-only — uses WinAPI types (HANDLE, LPCTSTR, PEXCEPTION_POINTERS).
// Translation requires equivalent crash handling mechanism on target platform.

#pragma once
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include "stackwalker.h"
#include <eh.h>

// [INTENT] Exception handler class that produces crash log files with full diagnostic info.
// [COUPLING] Inherits CStackWalker for symbol resolution and stack walking.
// [MEMORY] Owns output_file (ofstream*) and m_pEp (EXCEPTION_POINTERS*).
class CBaseException : public CStackWalker
{
public:
    // [INTENT] Constructor takes process/thread info and exception context.
    // Creates and opens crash log file if g_log_folder is configured.
    CBaseException(HANDLE              hProcess     = GetCurrentProcess(),
                   WORD                wPID         = GetCurrentProcessId(),
                   LPCTSTR             lpSymbolPath = NULL,
                   PEXCEPTION_POINTERS pEp          = NULL);
    ~CBaseException(void);

    // [INTENT] Printf-style output to crash log file.
    virtual void OutputString(LPCTSTR lpszFormat, ...);

    // [INTENT] Enumerate loaded modules with versions and symbol paths.
    virtual void ShowLoadModules();

    // [INTENT] Capture and dump the call stack.
    virtual void ShowCallstack(HANDLE hThread = GetCurrentThread(), const CONTEXT* context = NULL);

    // [INTENT] Translate exception code to human-readable description.
    virtual void ShowExceptionResoult(DWORD dwExceptionCode);

    // [INTENT] Convert address to module-relative (section:offset) format.
    virtual BOOL GetLogicalAddress(PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset);

    // [INTENT] Dump CPU register state.
    virtual void ShowRegistorInformation(PCONTEXT pCtx);

    // [INTENT] Orchestrates full exception dump (exception code, registers, callstack).
    virtual void ShowExceptionInformation();

    // [INTENT] Global unhandled exception filter (with mutex protection).
    // [COUPLING] Installed via SetUnhandledExceptionFilter() at app startup.
    static LONG WINAPI UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo);

    // [INTENT] Alternative exception filter without mutex (use when locking is unsafe).
    static LONG WINAPI UnhandledExceptionFilter2(PEXCEPTION_POINTERS pExceptionInfo);

    // [INTENT] SEH-to-C++ exception translator for use with _set_se_translator.
    static void STF(unsigned int ui, PEXCEPTION_POINTERS pEp);

    // BBS set crash log folder
    //  [INTENT] Configure crash log output directory. Call during app init.
    static void set_log_folder(std::string log_folder);

protected:
    // [MEMORY] Pointer to copied exception context — owned by this instance.
    PEXCEPTION_POINTERS m_pEp;

    // [MEMORY] Heap-allocated output stream for crash log file.
    boost::nowide::ofstream* output_file;
};

// [INTENT] Macro to install the default exception filter at app startup.
// Usage: SET_DEFULTER_HANDLER() in main() before any exception-generating code.
#define SET_DEFULTER_HANDLER() SetUnhandledExceptionFilter(CBaseException::UnhandledExceptionFilter)

// [INTENT] Macro to install SEH-to-C++ translator for using try/catch with SEH.
// Usage: SET_DEFAUL_EXCEPTION() after SET_DEFULTER_HANDLER().
#define SET_DEFAUL_EXCEPTION() _set_se_translator(CBaseException::STF)
