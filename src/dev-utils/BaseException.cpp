// [INTENT] Windows-specific crash/exception handler that captures stack traces,
// register dumps, and module information when the application crashes. Produces
// a timestamped .log file in the configured log folder for post-mortem debugging.
// [HAZARD] Windows-only code using WinAPI, DbgHelp, and structured exception handling.
// Translation requires platform-specific crash handling implementation.

#include "BaseException.h"
#include <iomanip>
#include <string>
#include <sstream>
#include <iostream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <mutex>

#include "libslic3r_version.h"

// [STATE] Global mutable state for crash log configuration.
// Set once at startup via set_log_folder() before any crash occurs.
// [HAZARD] Non-atomic access pattern; designed for single-threaded init only.
static std::string g_log_folder;
// [STATE] Atomic counter ensures unique crash log filenames even for rapid crashes.
static std::atomic<int> g_crash_log_count = 0;
// [CONCURRENCY] Mutex prevents concurrent crash handlers from interleaving output.
// [HAZARD] Using lock/unlock directly instead of std::lock_guard — if an exception
// occurs between lock() and unlock(), the mutex stays locked.
static std::mutex g_dump_mutex;

// [INTENT] Constructor initializes the exception handler and creates the crash log file.
// Opens an output stream to {log_folder}/log/crash_<timestamp>_<count>.log
// [MEMORY] Owns m_pEp via new (EXCEPTION_POINTERS copy) — released in destructor.
// [MEMORY] Owns output_file via new (ofstream*) — closed and deleted in destructor.
// [COUPLING] Inherits from CStackWalker for symbol resolution capabilities.
CBaseException::CBaseException(HANDLE hProcess, WORD wPID, LPCTSTR lpSymbolPath, PEXCEPTION_POINTERS pEp)
    : CStackWalker(hProcess, wPID, lpSymbolPath)
{
    if (NULL != pEp) {
        // [MEMORY] Deep copy of exception pointers — original may be invalid after handler returns.
        m_pEp = new EXCEPTION_POINTERS;
        CopyMemory(m_pEp, pEp, sizeof(EXCEPTION_POINTERS));
    }
    // [MEMORY] Heap-allocated ofstream — must match delete in destructor.
    output_file                = new boost::nowide::ofstream();
    std::time_t       t        = std::time(0);
    std::tm*          now_time = std::localtime(&t);
    std::stringstream buf;

    if (!g_log_folder.empty()) {
        // [INTENT] Build unique crash log filename: crash_<weekday>_<month>_<day>_<hour>_<min>_<sec>_<count>.log
        buf << std::put_time(now_time, "crash_%a_%b_%d_%H_%M_%S_") << g_crash_log_count++ << ".log";
        auto log_folder = (boost::filesystem::path(g_log_folder) / "log").make_preferred();
        // [INTENT] Create log directory if it doesn't exist — ensures crash logs have a place to go.
        if (!boost::filesystem::exists(log_folder)) {
            boost::filesystem::create_directory(log_folder);
        }
        auto        crash_log_path = boost::filesystem::path(log_folder / buf.str()).make_preferred();
        std::string log_filename   = crash_log_path.string();
        output_file->open(log_filename, std::ios::out | std::ios::app);

        // Output app build info in crash log so we could look for the correct PDB files
        // [INTENT] Embed version info at start of crash log for matching PDB files during debugging.
        OutputString(_T("%s\n\n"), _T(SLIC3R_APP_NAME " " SoftFever_VERSION " Build " GIT_COMMIT_HASH));
    }
}

// [MEMORY] Destructor releases heap-allocated members.
// Closes and deletes the output file stream.
CBaseException::~CBaseException(void)
{
    if (output_file) {
        output_file->close();
        delete output_file;
    }
}

// BBS set crash log folder
//  [INTENT] Configure where crash logs are written. Must be called during app initialization.
//  [STATE] Modifies global g_log_folder — not thread-safe, call before any crash can occur.
void CBaseException::set_log_folder(std::string log_folder) { g_log_folder = log_folder; }

// [INTENT] Formatted string output to crash log file. Printf-style API for convenience.
// [HAZARD] Uses _vsntprintf_s with fixed 2048-byte buffer — potential truncation for very long messages.
void CBaseException::OutputString(LPCTSTR lpszFormat, ...)
{
    TCHAR   szBuf[2048] = _T("");
    va_list args;
    va_start(args, lpszFormat);
    _vsntprintf_s(szBuf, 2048, lpszFormat, args);
    va_end(args);

    // WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), szBuf, _tcslen(szBuf), NULL, NULL);

    // output it to the current directory of binary
    //  [COUPLING] textconv_helper::T2A_ converts TCHAR string to ANSI for file output.
    std::string output_str = textconv_helper::T2A_(szBuf);
    *output_file << output_str;
    output_file->flush();
}

// [INTENT] List all loaded modules (DLLs/exes) with their addresses, sizes, and versions.
// Useful for identifying which versions of DLLs were loaded at crash time.
void CBaseException::ShowLoadModules()
{
    LoadSymbol();
    LPMODULE_INFO pHead = GetLoadModules();
    LPMODULE_INFO pmi   = pHead;

    TCHAR szBuf[MAX_COMPUTERNAME_LENGTH] = _T("");
    DWORD dwSize                         = MAX_COMPUTERNAME_LENGTH;
    GetUserName(szBuf, &dwSize);
    OutputString(_T("Current User:%s\r\n"), szBuf);
    OutputString(_T("BaseAddress:\tSize:\tName\tPath\tSymbolPath\tVersion\r\n"));
    while (NULL != pmi) {
        OutputString(_T("%08x\t%d\t%s\t%s\t%s\t%s\r\n"), (unsigned long) (pmi->ModuleAddress), pmi->dwModSize, pmi->szModuleName,
                     pmi->szModulePath, pmi->szSymbolPath, pmi->szVersion);
        pmi = pmi->pNext;
    }

    // [MEMORY] Caller owns pHead — must free via FreeModuleInformations.
    FreeModuleInformations(pHead);
}

// [INTENT] Capture and dump the call stack at crash point.
// Outputs logical addresses (section:offset) for manual symbol resolution via WinDBG.
void CBaseException::ShowCallstack(HANDLE hThread, const CONTEXT* context)
{
    OutputString(_T("Show CallStack:\n"));
    LPSTACKINFO phead = StackWalker(hThread, context);

    // Show RVA of each call stack, so we can locate the symbol using pdb file
    // To show the symbol, load the <szFaultingModule> in WinDBG with pdb file, then type the following commands:
    // > lm                                                                   which gives you the start address of each module, as well as
    // module names > !dh <module name>                                                    list all module headers. Find the <virtual
    // address> of the section given by
    //                                                                        the <section> output in the crash log
    // > ln <module start address> + <section virtual address> + <offset>     reveals the debug symbol
    OutputString(_T("\nLogical Address:\n"));
    TCHAR szFaultingModule[MAX_PATH];
    DWORD section, offset;
    for (LPSTACKINFO ps = phead; ps != nullptr; ps = ps->pNext) {
        if (GetLogicalAddress((PVOID) ps->szFncAddr, szFaultingModule, sizeof(szFaultingModule), section, offset)) {
            OutputString(_T("0x%X 0x%X:0x%X %s\n"), ps->szFncAddr, section, offset, szFaultingModule);
        } else {
            OutputString(_T("0x%X Unknown\n"), ps->szFncAddr);
        }
    }

    // [MEMORY] Caller owns phead — must free via FreeStackInformations.
    FreeStackInformations(phead);
}

// [INTENT] Translate Windows exception codes to human-readable strings.
// Covers standard SEH exceptions like ACCESS_VIOLATION, STACK_OVERFLOW, etc.
// [HAZARD] Switch statement covers the common cases; falls back to FormatMessage for others.
void CBaseException::ShowExceptionResoult(DWORD dwExceptionCode)
{
    OutputString(_T("Exception Code :%08x "), dwExceptionCode);
// BBS: to be checked
#if 1
    switch (dwExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION: {
        // OutputString(_T("ACCESS_VIOLATION(%s)\r\n"), _T("??д?????ڴ?"));
        OutputString(_T("ACCESS_VIOLATION\r\n"));
    }
        return;
    case EXCEPTION_DATATYPE_MISALIGNMENT: {
        // OutputString(_T("DATATYPE_MISALIGNMENT(%s)\r\n"), _T("?߳???ͼ?ڲ?֧?ֶ??Ӳ????϶?дδ?????????"));
        OutputString(_T("DATATYPE_MISALIGNMENT\r\n"));
    }
        return;
    case EXCEPTION_BREAKPOINT: {
        // OutputString(_T("BREAKPOINT(%s)\r\n"), _T("????һ???ϵ?"));
        OutputString(_T("BREAKPOINT\r\n"));
    }
        return;
    case EXCEPTION_SINGLE_STEP: {
        // OutputString(_T("SINGLE_STEP(%s)\r\n"), _T("????")); //һ???Ƿ????ڵ????¼???
        OutputString(_T("SINGLE_STEP\r\n"));
    }
        return;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: {
        // OutputString(_T("ARRAY_BOUNDS_EXCEEDED(%s)\r\n"), _T("???????Խ??"));
        OutputString(_T("ARRAY_BOUNDS_EXCEEDED\r\n"));
    }
        return;
    case EXCEPTION_FLT_DENORMAL_OPERAND: {
        // OutputString(_T("FLT_DENORMAL_OPERAND(%s)\r\n"), _T("?????????һ????????????棬?????ĸ????޷???ʾ")); //????????????
        OutputString(_T("FLT_DENORMAL_OPERAND\r\n"));
    }
        return;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: {
        // OutputString(_T("FLT_DIVIDE_BY_ZERO(%s)\r\n"), _T("????????0????"));
        OutputString(_T("FLT_DIVIDE_BY_ZERO\r\n"));
    }
        return;
    case EXCEPTION_FLT_INEXACT_RESULT: {
        // OutputString(_T("FLT_INEXACT_RESULT(%s)\r\n"), _T("??????????Ľ?޷???ʾ")); //?޷???ʾһ????????̫С?????????????ʾ?ķ?Χ,
        // ?????֮?????Ľ???쳣
        OutputString(_T("FLT_INEXACT_RESULT\r\n"));
    }
        return;
    case EXCEPTION_FLT_INVALID_OPERATION: {
        // OutputString(_T("FLT_INVALID_OPERATION(%s)\r\n"), _T("???????????쳣"));
        OutputString(_T("FLT_INVALID_OPERATION\r\n"));
    }
        return;
    case EXCEPTION_FLT_OVERFLOW: {
        // OutputString(_T("FLT_OVERFLOW(%s)\r\n"), _T("?????????ָ????????Ӧ???͵????ֵ"));
        OutputString(_T("FLT_OVERFLOW\r\n"));
    }
        return;
    case EXCEPTION_FLT_STACK_CHECK: {
        // OutputString(_T("STACK_CHECK(%s)\r\n"), _T("ջԽ????ջ??????"));
        OutputString(_T("STACK_CHECK\r\n"));
    }
        return;
    case EXCEPTION_INT_DIVIDE_BY_ZERO: {
        // OutputString(_T("INT_DIVIDE_BY_ZERO(%s)\r\n"), _T("??????0?쳣"));
        OutputString(_T("INT_DIVIDE_BY_ZERO\r\n"));
    }
        return;
    case EXCEPTION_INVALID_HANDLE: {
        // OutputString(_T("INVALID_HANDLE(%s)\r\n"), _T("?????Ч"));
        OutputString(_T("INVALID_HANDLE\r\n"));
    }
        return;
    case EXCEPTION_PRIV_INSTRUCTION: {
        // OutputString(_T("PRIV_INSTRUCTION(%s)\r\n"), _T("?߳???ͼִ?е?ǰ????ģʽ??֧?ֵ?ָ??"));
        OutputString(_T("PRIV_INSTRUCTION\r\n"));
    }
        return;
    case EXCEPTION_IN_PAGE_ERROR: {
        // OutputString(_T("IN_PAGE_ERROR(%s)\r\n"), _T("?߳???ͼ????δ???ص??????ڴ?ҳ???߲???ܼ??ص??????ڴ?ҳ"));
        OutputString(_T("IN_PAGE_ERROR\r\n"));
    }
        return;
    case EXCEPTION_ILLEGAL_INSTRUCTION: {
        // OutputString(_T("ILLEGAL_INSTRUCTION(%s)\r\n"), _T("?߳???ͼִ????Чָ??"));
        OutputString(_T("ILLEGAL_INSTRUCTION\r\n"));
    }
        return;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: {
        // OutputString(_T("NONCONTINUABLE_EXCEPTION(%s)\r\n"), _T("?߳???ͼ??һ?????ɼ???ִ?е??쳣?????????ִ??"));
        OutputString(_T("NONCONTINUABLE_EXCEPTION\r\n"));
    }
        return;
    case EXCEPTION_STACK_OVERFLOW: {
        // OutputString(_T("STACK_OVERFLOW(%s)\r\n"), _T("ջ???"));
        OutputString(_T("STACK_OVERFLOW\r\n"));
    }
        return;
    case EXCEPTION_INVALID_DISPOSITION: {
        // OutputString(_T("INVALID_DISPOSITION(%s)\r\n"), _T("?쳣??????쳣????????????һ???Ч????")); //ʹ?ø߼????Ա?д?ĳ?????Զ?????????????쳣
        OutputString(_T("INVALID_DISPOSITION\r\n"));
    }
        return;
    case EXCEPTION_FLT_UNDERFLOW: {
        // OutputString(_T("FLT_UNDERFLOW(%s)\r\n"), _T("???????????ָ??С??Ӧ???͵???Сֵ"));
        OutputString(_T("FLT_UNDERFLOW\r\n"));
    }
        return;
    case EXCEPTION_INT_OVERFLOW: {
        // OutputString(_T("INT_OVERFLOW(%s)\r\n"), _T("????????Խ??"));
        OutputString(_T("INT_OVERFLOW\r\n"));
    }
        return;
    }

    TCHAR szBuffer[512] = {0};

    // [INTENT] Fallback: use NTDLL's FormatMessage to describe unknown exception codes.
    FormatMessage(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE, GetModuleHandle(_T("NTDLL.DLL")), dwExceptionCode, 0,
                  szBuffer, sizeof(szBuffer), 0);

    OutputString(_T("%s"), szBuffer);
    OutputString(_T("\r\n"));
#endif
}

// [INTENT] Global unhandled exception filter — called by Windows when no handler catches an exception.
// [COUPLING] Installed via SetUnhandledExceptionFilter at app startup.
// [HAZARD] Filters out non-fatal exceptions (< 0x80000000) and CLR exceptions (0xe0434352).
// [CONCURRENCY] Uses manual mutex lock/unlock — potential deadlock if crash occurs during lock hold.
LONG WINAPI CBaseException::UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    if (pExceptionInfo->ExceptionRecord->ExceptionCode < 0x80000000
        // BBS: Load project on computers with SDC may trigger this exception (in ShowModal()),
        //      It's not fatal and should be ignored, or there will be lots of meaningless crash logs
        || pExceptionInfo->ExceptionRecord->ExceptionCode == 0xe0434352)
    // BBS: ignore the exception when copy preset
    //|| pExceptionInfo->ExceptionRecord->ExceptionCode==0xe06d7363)
    {
        // BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": got an ExceptionCode %1%, skip it!") %
        // pExceptionInfo->ExceptionRecord->ExceptionCode;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    g_dump_mutex.lock();
    CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pExceptionInfo);
    base.ShowExceptionInformation();
    g_dump_mutex.unlock();

    return EXCEPTION_CONTINUE_SEARCH;
}

// [INTENT] Alternative exception filter without mutex protection — used where locking is unsafe.
// [HAZARD] No synchronization — if multiple threads crash simultaneously, output may interleave.
LONG WINAPI CBaseException::UnhandledExceptionFilter2(PEXCEPTION_POINTERS pExceptionInfo)
{
    CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pExceptionInfo);
    base.ShowExceptionInformation();

    return EXCEPTION_CONTINUE_SEARCH;
}

// [INTENT] Convert a raw memory address to module-relative (section:offset) format.
// Used for manual symbol lookup when PDB files are available but not loaded at crash time.
// [HAZARD] Parses PE image headers directly — assumes valid module at address.
BOOL CBaseException::GetLogicalAddress(PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
        return FALSE;

    DWORD_PTR hMod = (DWORD_PTR) mbi.AllocationBase;

    if (!GetModuleFileName((HMODULE) hMod, szModule, len))
        return FALSE;

    if (!hMod)
        return FALSE;

    // [INTENT] Walk PE section headers to find which section contains the address.
    PIMAGE_DOS_HEADER     pDosHdr  = (PIMAGE_DOS_HEADER) hMod;
    PIMAGE_NT_HEADERS     pNtHdr   = (PIMAGE_NT_HEADERS) (hMod + pDosHdr->e_lfanew);
    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHdr);

    DWORD_PTR rva = (DWORD_PTR) addr - hMod;

    //?????????ַ????ڼ?????
    // [INTENT] Linear search through sections — O(n) where n = number of PE sections.
    for (unsigned i = 0; i < pNtHdr->FileHeader.NumberOfSections; i++, pSection++) {
        DWORD sectionStart = pSection->VirtualAddress;
        DWORD sectionEnd   = sectionStart + std::max(pSection->SizeOfRawData, pSection->Misc.VirtualSize);

        if ((rva >= sectionStart) && (rva <= sectionEnd)) {
            section = i + 1;
            offset  = rva - sectionStart;
            return TRUE;
        }
    }

    return FALSE; // Should never get here!
}

// [INTENT] Dump CPU register state at crash point — different formats for x86 vs x64.
// [HAZARD] Platform-specific code; uses #if defined(_M_IX86) / _M_X64.
void CBaseException::ShowRegistorInformation(PCONTEXT pCtx)
{
#if defined(_M_IX86) // Intel Only!
    OutputString(_T("\nRegisters:\r\n"));

    OutputString(_T("EAX:%08X\r\nEBX:%08X\r\nECX:%08X\r\nEDX:%08X\r\nESI:%08X\r\nEDI:%08X\r\n"), pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx,
                 pCtx->Esi, pCtx->Edi);

    OutputString(_T("CS:EIP:%04X:%08X\r\n"), pCtx->SegCs, pCtx->Eip);
    OutputString(_T("SS:ESP:%04X:%08X  EBP:%08X\r\n"), pCtx->SegSs, pCtx->Esp, pCtx->Ebp);
    OutputString(_T("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n"), pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs);
    OutputString(_T("Flags:%08X\r\n"), pCtx->EFlags);
#elif defined(_M_X64)
    OutputString(_T("\nRegisters:\r\n"));

    OutputString(_T("RAX:%016llX\r\nRBX:%016llX\r\nRCX:%016llX\r\nRDX:%016llX\r\nRSI:%016llX\r\nRDI:%016llX\r\n"), pCtx->Rax, pCtx->Rbx,
                 pCtx->Rcx, pCtx->Rdx, pCtx->Rsi, pCtx->Rdi);

    OutputString(
        _T("R8:%016llX\r\nR9:%016llX\r\nR10:%016llX\r\nR11:%016llX\r\nR12:%016llX\r\nR13:%016llX\r\nR14:%016llX\r\nR15:%016llX\r\n"),
        pCtx->R8, pCtx->R9, pCtx->R10, pCtx->R11, pCtx->R12, pCtx->R13, pCtx->R14, pCtx->R15);

    OutputString(_T("CS:RIP:%04X:%016llX\r\n"), pCtx->SegCs, pCtx->Rip);
    OutputString(_T("SS:RSP:%04X:%016llX  RBP:%016llX\r\n"), pCtx->SegSs, pCtx->Rsp, pCtx->Rbp);
    OutputString(_T("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n"), pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs);
    OutputString(_T("Flags:%08X\r\n"), pCtx->EFlags);
#endif

    OutputString(_T("\r\n"));
}

// [INTENT] Structured exception translator — converts SEH to C++ exception.
// Registered via _set_se_translator for use with C++ try/catch blocks.
// [HAZARD] Throws a CBaseException object by value — copy constructor dependency.
void CBaseException::STF(unsigned int ui, PEXCEPTION_POINTERS pEp)
{
    CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pEp);
    throw base;
}

// [INTENT] Main entry point for crash dump generation. Orchestrates all dump components.
void CBaseException::ShowExceptionInformation()
{
    OutputString(_T("Exceptions:\r\n"));
    ShowExceptionResoult(m_pEp->ExceptionRecord->ExceptionCode);

    OutputString(_T("Exception Flag :0x%x "), m_pEp->ExceptionRecord->ExceptionFlags);
    OutputString(_T("NumberParameters :%ld \n"), m_pEp->ExceptionRecord->NumberParameters);
    for (int i = 0; i < m_pEp->ExceptionRecord->NumberParameters; i++) {
        OutputString(_T("Param %d :0x%x \n"), i, m_pEp->ExceptionRecord->ExceptionInformation[i]);
    }
    OutputString(_T("Context :%p \n"), m_pEp->ContextRecord);
    OutputString(_T("ContextFlag : 0x%x, EFlags: 0x%x \n"), m_pEp->ContextRecord->ContextFlags, m_pEp->ContextRecord->EFlags);

    TCHAR szFaultingModule[MAX_PATH];
    DWORD section, offset;
    GetLogicalAddress(m_pEp->ExceptionRecord->ExceptionAddress, szFaultingModule, sizeof(szFaultingModule), section, offset);
    OutputString(_T("Fault address:  0x%X 0x%X:0x%X %s\r\n"), m_pEp->ExceptionRecord->ExceptionAddress, section, offset, szFaultingModule);

    ShowRegistorInformation(m_pEp->ContextRecord);

    ShowCallstack(GetCurrentThread(), m_pEp->ContextRecord);
}
