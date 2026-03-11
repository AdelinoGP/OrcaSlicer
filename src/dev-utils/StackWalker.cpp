// [INTENT] Windows stack walking utility using DbgHelp API for capturing call stacks.
// Used by CBaseException for crash dump generation. Provides symbol resolution and
// module enumeration for debugging crashed applications.
// [HAZARD] Windows-only — uses WinAPI, DbgHelp, PSAPI, and ToolHelp APIs.
// [HAZARD] Dynamically loads DLLs (kernel32.dll, psapi.dll, tlhelp32.dll) at runtime.
// Translation requires platform-specific stack capture mechanism.

#include "StackWalker.h"
#include <strsafe.h>
// #include <atlconv.h>
#include <dbghelp.h>
#pragma comment(lib, "version.lib")
#pragma comment(lib, "dbghelp.lib")

// [INTENT] Initialize the stack walker with process handle and optional symbol path.
// [MEMORY] Owns m_lpszSymbolPath via new[] — deleted in destructor.
CStackWalker::CStackWalker(HANDLE hProcess, WORD wPID, LPCTSTR lpSymbolPath)
    : m_hProcess(hProcess), m_wPID(wPID), m_bSymbolLoaded(FALSE), m_lpszSymbolPath(NULL)
{
    if (NULL != lpSymbolPath) {
        size_t dwLength = 0;
        StringCchLength(lpSymbolPath, MAX_SYMBOL_PATH, &dwLength);
        m_lpszSymbolPath = new TCHAR[dwLength + 1];
        ZeroMemory(m_lpszSymbolPath, sizeof(TCHAR) * (dwLength + 1));
        StringCchCopy(m_lpszSymbolPath, dwLength, lpSymbolPath);
    }
}

// [MEMORY] Destructor releases symbol path string and cleans up DbgHelp if loaded.
CStackWalker::~CStackWalker(void)
{
    if (NULL != m_lpszSymbolPath) {
        delete[] m_lpszSymbolPath;
    }

    if (m_bSymbolLoaded) {
        // [INTENT] Release symbol handler resources in DbgHelp.
        SymCleanup(m_hProcess);
    }
}

// [INTENT] Initialize DbgHelp symbol handler with search paths.
// Builds comprehensive symbol path: current dir, app dir, env vars, and Microsoft symbol server.
// [HAZARD] Complex symbol path construction — concatenates multiple sources.
// [STATE] Sets m_bSymbolLoaded to TRUE on success.
BOOL CStackWalker::LoadSymbol()
{
    // USES_CONVERSION;
    //???????
    //  [INTENT] Only load symbols once per CStackWalker instance.
    if (m_bSymbolLoaded) {
        return m_bSymbolLoaded;
    }

    if (NULL != m_lpszSymbolPath) {
        m_bSymbolLoaded = SymInitialize(m_hProcess, textconv_helper::T2A_(m_lpszSymbolPath), FALSE);
        return m_bSymbolLoaded;
    }

    //????????
    // [INTENT] Build default symbol search path from multiple sources.
    TCHAR szSymbolPath[MAX_SYMBOL_PATH] = _T("");
    StringCchCopy(szSymbolPath, MAX_SYMBOL_PATH, _T(".;"));

    //????????
    // [INTENT] Add current working directory to search path.
    TCHAR szTemp[MAX_PATH] = _T("");
    if (GetCurrentDirectory(MAX_PATH, szTemp) > 0) {
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T(";"));
    }

    //??????????
    // [INTENT] Add main executable's directory to search path.
    ZeroMemory(szTemp, MAX_PATH * sizeof(TCHAR));
    if (GetModuleFileName(NULL, szTemp, MAX_PATH) > 0) {
        size_t sLength = 0;
        StringCchLength(szTemp, MAX_PATH, &sLength);
        // [INTENT] Strip filename from path, keeping only directory.
        for (int i = sLength; i >= 0; i--) {
            if (szTemp[i] == _T('\\') || szTemp[i] == _T('/') || szTemp[i] == _T(':')) {
                szTemp[i] = _T('\0');
                break;
            }
        }
    }

    StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
    StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T(";"));

    // [INTENT] Add _NT_SYMBOL_PATH environment variable to search.
    ZeroMemory(szTemp, MAX_PATH * sizeof(TCHAR));
    if (GetEnvironmentVariable(_T("_NT_SYMBOL_PATH"), szTemp, MAX_PATH) > 0) {
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T(";"));
    }

    // [INTENT] Add _NT_ALTERNATE_SYMBOL_PATH environment variable.
    ZeroMemory(szTemp, MAX_PATH * sizeof(TCHAR));
    if (GetEnvironmentVariable(_T("_NT_ALTERNATE_SYMBOL_PATH"), szTemp, MAX_PATH) > 0) {
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T(";"));
    }

    // [INTENT] Add SYSTEMROOT and system32 directories.
    ZeroMemory(szTemp, MAX_PATH * sizeof(TCHAR));
    if (GetEnvironmentVariable(_T("SYSTEMROOT"), szTemp, MAX_PATH) > 0) {
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T(";"));
        // also add the "system32"-directory:
        StringCchCat(szTemp, MAX_PATH, _T("\\system32"));
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T(";"));
    }

    // [INTENT] Add Microsoft public symbol server fallback.
    ZeroMemory(szTemp, MAX_PATH * sizeof(TCHAR));
    if (GetEnvironmentVariable(_T("SYSTEMDRIVE"), szTemp, MAX_PATH) > 0) {
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T("SRV*"));
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, szTemp);
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T("\\websymbols"));
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T("*http://msdl.microsoft.com/download/symbols;"));
    } else {
        StringCchCat(szSymbolPath, MAX_SYMBOL_PATH, _T("SRV*c:\\websymbols*http://msdl.microsoft.com/download/symbols;"));
    }

    size_t sLength = 0;
    StringCchLength(szSymbolPath, MAX_SYMBOL_PATH, &sLength);
    if (sLength > 0) {
        // [MEMORY] Allocate copy of symbol path string.
        m_lpszSymbolPath = new TCHAR[sLength + 1];
        ZeroMemory(m_lpszSymbolPath, sizeof(TCHAR) * (sLength + 1));
        StringCchCopy(m_lpszSymbolPath, sLength, szSymbolPath);
    }

    if (NULL != m_lpszSymbolPath) {
        // [INTENT] Initialize symbol handler with fInvadeProcess=TRUE to load symbols for all modules.
        m_bSymbolLoaded = SymInitialize(m_hProcess, textconv_helper::T2A_(m_lpszSymbolPath), TRUE); //??????????????
    }

    // [INTENT] Configure symbol options: load line info, fail on critical errors, debug output.
    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_LOAD_LINES;
    symOptions |= SYMOPT_FAIL_CRITICAL_ERRORS;
    symOptions |= SYMOPT_DEBUG;
    SymSetOptions(symOptions);

    return m_bSymbolLoaded;
}

// [INTENT] Get list of loaded modules (DLLs/exes) in the process.
// Tries ToolHelp API first, falls back to PSAPI if that fails.
LPMODULE_INFO CStackWalker::GetLoadModules()
{
    LPMODULE_INFO pHead = GetModulesTH32();
    if (NULL == pHead) {
        pHead = GetModulesPSAPI();
    }

    return pHead;
}

// [INTENT] Free linked list of MODULE_INFO structures.
// [MEMORY] Caller is responsible for freeing returned module list.
void CStackWalker::FreeModuleInformations(LPMODULE_INFO pmi)
{
    LPMODULE_INFO head = pmi;
    while (NULL != head) {
        pmi = pmi->pNext;
        delete head;
        head = pmi;
    }
}

// [INTENT] Enumerate modules using ToolHelp API (CreateToolhelp32Snapshot).
// [HAZARD] Dynamically loads kernel32.dll/tlhelp32.dll to avoid static dependency.
// [MEMORY] Returns linked list of MODULE_INFO — caller must free via FreeModuleInformations.
LPMODULE_INFO CStackWalker::GetModulesTH32()
{
    //?????????Toolhelp.dll???????,???????????
    // [INTENT] Dynamic loading to avoid linking issues on older Windows versions.
    LPMODULE_INFO pHead = NULL;
    LPMODULE_INFO pTail = pHead;

    typedef HANDLE(WINAPI * pfnCreateToolhelp32Snapshot)(DWORD dwFlags, DWORD th32ProcessID);
    typedef BOOL(WINAPI * pfnModule32First)(HANDLE hSnapshot, LPMODULEENTRY32 lpme);
    typedef BOOL(WINAPI * pfnModule32Next)(HANDLE hSnapshot, LPMODULEENTRY32 lpme);

    const TCHAR* dllname[] = {_T("kernel32.dll"), _T("tlhelp32.dll")};
    HINSTANCE    hToolhelp = NULL;

    pfnCreateToolhelp32Snapshot CreateToolhelp32Snapshot = NULL;
    pfnModule32First            Module32First            = NULL;
    pfnModule32Next             Module32Next             = NULL;

    HANDLE        hSnap;
    MODULEENTRY32 me;
    me.dwSize = sizeof(me);
    BOOL   keepGoing;
    size_t i;

    // [INTENT] Try loading from each DLL until we find the functions.
    for (i = 0; i < (sizeof(dllname) / sizeof(dllname[0])); i++) {
        hToolhelp = LoadLibrary(dllname[i]);
        if (hToolhelp == NULL)
            continue;
        CreateToolhelp32Snapshot = (pfnCreateToolhelp32Snapshot) GetProcAddress(hToolhelp, "CreateToolhelp32Snapshot");
#ifdef UNICODE
        Module32First = (pfnModule32First) GetProcAddress(hToolhelp, "Module32FirstW");
        Module32Next  = (pfnModule32Next) GetProcAddress(hToolhelp, "Module32NextW");
#else
        Module32First = (pfnModule32First) GetProcAddress(hToolhelp, "Module32FirstA");
        Module32Next  = (pfnModule32Next) GetProcAddress(hToolhelp, "Module32NextA");
#endif

        if ((CreateToolhelp32Snapshot != NULL) && (Module32First != NULL) && (Module32Next != NULL))
            break;

        FreeLibrary(hToolhelp);
        hToolhelp = NULL;
    }

    if (hToolhelp == NULL)
        return pHead;

    // [INTENT] Take snapshot of all modules in the process.
    hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, m_wPID);

    if (hSnap == INVALID_HANDLE_VALUE) {
        FreeLibrary(hToolhelp);
        return pHead;
    }

    keepGoing = Module32First(hSnap, &me);

    while (keepGoing) {
        // [MEMORY] Allocate MODULE_INFO for each module found.
        LPMODULE_INFO pmi = new MODULE_INFO;
        ZeroMemory(pmi, sizeof(MODULE_INFO));

        pmi->dwModSize     = me.modBaseSize;
        pmi->ModuleAddress = (DWORD64) me.modBaseAddr;
        StringCchCopy(pmi->szModuleName, MAX_MODULE_NAME32, me.szModule);
        StringCchCopy(pmi->szModulePath, MAX_PATH, me.szExePath);
        // [INTENT] Fill in symbol path and version info for this module.
        GetModuleInformation(pmi);
        if (pHead == NULL) {
            pHead = pmi;
            pTail = pHead;
        } else {
            pTail->pNext = pmi;
            pTail        = pmi;
        }

        keepGoing = Module32Next(hSnap, &me);
    }

    CloseHandle(hSnap);
    FreeLibrary(hToolhelp);
    return pHead;
}

// [INTENT] Enumerate modules using PSAPI (fallback when ToolHelp unavailable).
// [HAZARD] Dynamically loads psapi.dll.
// [MEMORY] Returns linked list of MODULE_INFO — caller must free.
LPMODULE_INFO CStackWalker::GetModulesPSAPI()
{
    LPMODULE_INFO pHead = NULL;
    LPMODULE_INFO pTail = pHead;
    typedef BOOL(WINAPI * pfnEnumProcessModules)(HANDLE hProcess, HMODULE * lphModule, DWORD cb, LPDWORD lpcbNeeded);
    typedef DWORD(WINAPI * pfnGetModuleFileNameEx)(HANDLE hProcess, HMODULE hModule, LPTSTR lpFilename, DWORD nSize);
    typedef DWORD(WINAPI * pfnGetModuleBaseName)(HANDLE hProcess, HMODULE hModule, LPTSTR lpFilename, DWORD nSize);
    typedef BOOL(WINAPI * pfnGetModuleInformation)(HANDLE hProcess, HMODULE hModule, LPMODULEINFO pmi, DWORD nSize);

    HINSTANCE               hPsapi;
    pfnEnumProcessModules   EnumProcessModules   = NULL;
    pfnGetModuleFileNameEx  GetModuleFileNameEx  = NULL;
    pfnGetModuleBaseName    GetModuleBaseName    = NULL;
    pfnGetModuleInformation GetModuleInformation = NULL;

    DWORD i;
    // ModuleEntry e;
    DWORD      cbNeeded;
    MODULEINFO mi;
    HMODULE*   hMods                               = NULL;
    TCHAR      szModuleName[MAX_MODULE_NAME32 + 1] = _T("");
    TCHAR      szModulePath[MAX_PATH]              = _T("");

    hPsapi = LoadLibrary(_T("psapi.dll"));
    if (hPsapi == NULL) {
        return pHead;
    }

    // [INTENT] Get function pointers from psapi.dll.
    EnumProcessModules = (pfnEnumProcessModules) GetProcAddress(hPsapi, "EnumProcessModules");
#ifdef UNICODE
    GetModuleFileNameEx = (pfnGetModuleFileNameEx) GetProcAddress(hPsapi, "GetModuleFileNameExW");
    GetModuleBaseName   = (pfnGetModuleBaseName) GetProcAddress(hPsapi, "GetModuleBaseNameW");
#else
    GetModuleFileNameEx = (pfnGetModuleFileNameEx) GetProcAddress(hPsapi, "GetModuleFileNameExA");
    GetModuleBaseName   = (pfnGetModuleBaseName) GetProcAddress(hPsapi, "GetModuleBaseNameA");
#endif
    GetModuleInformation = (pfnGetModuleInformation) GetProcAddress(hPsapi, "GetModuleInformation");
    if ((EnumProcessModules == NULL) || (GetModuleFileNameEx == NULL) || (GetModuleBaseName == NULL) || (GetModuleInformation == NULL)) {
        FreeLibrary(hPsapi);
        return pHead;
    }

    // [INTENT] First call to get required buffer size.
    EnumProcessModules(m_hProcess, hMods, 0, &cbNeeded);
    // [MEMORY] Allocate HMODULE array sized for all modules.
    hMods = new HMODULE[cbNeeded / sizeof(HMODULE)];
    ASSERT(NULL != hMods);
    ZeroMemory(hMods, cbNeeded);

    if (!EnumProcessModules(m_hProcess, hMods, cbNeeded, &cbNeeded)) {
        goto cleanup;
    }

    for (i = 0; i < cbNeeded / sizeof(HMODULE); i++) {
        GetModuleInformation(m_hProcess, hMods[i], &mi, sizeof(mi));
        GetModuleFileNameEx(m_hProcess, hMods[i], szModulePath, MAX_PATH);
        GetModuleBaseName(m_hProcess, hMods[i], szModuleName, MAX_MODULE_NAME32);
        LPMODULE_INFO pmi = new MODULE_INFO;
        ZeroMemory(pmi, sizeof(MODULE_INFO));
        pmi->dwModSize     = mi.SizeOfImage;
        pmi->ModuleAddress = (DWORD64) mi.lpBaseOfDll;
        StringCchCopy(pmi->szModuleName, MAX_MODULE_NAME32, szModuleName);
        StringCchCopy(pmi->szModulePath, MAX_PATH, szModulePath);
        this->GetModuleInformation(pmi);
        if (pHead == NULL) {
            pHead = pmi;
            pTail = pHead;
        } else {
            pTail->pNext = pmi;
            pTail        = pmi;
        }
    }

cleanup:
    if (hPsapi != NULL) {
        FreeLibrary(hPsapi);
    }
    if (hMods != NULL) {
        delete[] hMods;
    }

    return pHead;
}

// [INTENT] Printf-style output to debug console (OutputDebugString).
void CStackWalker::OutputString(LPCTSTR lpszFormat, ...)
{
    TCHAR   szBuf[1024] = _T("");
    va_list args;
    va_start(args, lpszFormat);
    _vsntprintf_s(szBuf, 1024, lpszFormat, args);
    va_end(args);

    OutputDebugString(szBuf);
}

// [INTENT] Fill in version info and symbol path for a module.
// [HAZARD] Uses GetFileVersionInfo and SymGetModuleInfo64.
void CStackWalker::GetModuleInformation(LPMODULE_INFO pmi)
{
    // USES_CONVERSION;
    IMAGEHLP_MODULE64 im = {0};
    im.SizeOfStruct      = sizeof(IMAGEHLP_MODULE64);

    VS_FIXEDFILEINFO* pvfi       = NULL;
    DWORD             dwHandle   = 0;
    DWORD             dwInfoSize = 0;
    dwInfoSize                   = GetFileVersionInfoSize(pmi->szModulePath, &dwHandle);

    if (dwInfoSize > 0) {
        // [MEMORY] Temporary buffer for version info.
        LPVOID lpData = new byte[dwInfoSize];
        ZeroMemory(lpData, dwInfoSize * sizeof(byte));

        if (GetFileVersionInfo(pmi->szModulePath, dwHandle, dwInfoSize, lpData) > 0) {
            TCHAR szBlock[] = _T("\\");
            UINT  len;
            if (VerQueryValue(lpData, szBlock, (LPVOID*) &pvfi, &len)) {
                WORD v1 = HIWORD(pvfi->dwFileVersionMS);
                WORD v2 = LOWORD(pvfi->dwFileVersionMS);
                WORD v3 = HIWORD(pvfi->dwFileVersionLS);
                WORD v4 = LOWORD(pvfi->dwFileVersionLS);
                _stprintf_s(pmi->szVersion, MAX_VERSION_LENGTH, _T("%d.%d.%d.%d"), v1, v2, v3, v4);
            }
        }

        delete[] lpData;
    }

    // [INTENT] Get PDB symbol path from DbgHelp.
    SymGetModuleInfo64(m_hProcess, pmi->ModuleAddress, &im);
    StringCchCopy(pmi->szSymbolPath, MAX_PATH, textconv_helper::A2T_(im.LoadedPdbName));
}

// [INTENT] Capture and return call stack from given thread context.
// [MEMORY] Returns linked list of STACKINFO — caller must free via FreeStackInformations.
// [HAZARD] Platform-specific: handles x86, x64, and IA64 architectures differently.
LPSTACKINFO CStackWalker::StackWalker(HANDLE hThread, const CONTEXT* context)
{
    // USES_CONVERSION;
    //???????
    //  [INTENT] Ensure symbols are loaded before walking stack.
    LoadSymbol();

    LPSTACKINFO pHead = NULL;
    LPSTACKINFO pTail = pHead;

    //??????????????
    // [INTENT] Get thread context for stack walk starting point.
    CONTEXT c = {0};
    if (context == NULL) {
#if _WIN32_WINNT <= 0x0501
        if (hThread == GetCurrentThread())
#else
        if (GetThreadId(hThread) == GetCurrentThreadId())
#endif
        {
            // [INTENT] Capture context of current thread using inline assembly/macro.
            GET_CURRENT_THREAD_CONTEXT(c, CONTEXT_FULL);
        } else {
            // [INTENT] For non-current thread: suspend, capture context, resume.
            //????????,????????????????????
            SuspendThread(hThread);
            memset(&c, 0, sizeof(CONTEXT));
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(hThread, &c) == FALSE) {
                ResumeThread(hThread);
                return NULL;
            }
        }
    } else
        c = *context;

    // [INTENT] Initialize STACKFRAME64 for StackWalk64.
    STACKFRAME64 sf = {0};
    DWORD        imageType;

// intel X86
#ifdef _M_IX86
    imageType           = IMAGE_FILE_MACHINE_I386;
    sf.AddrPC.Offset    = c.Eip;
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrFrame.Offset = c.Ebp;
    sf.AddrFrame.Mode   = AddrModeFlat;
    sf.AddrStack.Offset = c.Esp;
    sf.AddrStack.Mode   = AddrModeFlat;
    // AMD
#elif _M_X64
    imageType           = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset    = c.Rip;
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrFrame.Offset = c.Rsp;
    sf.AddrFrame.Mode   = AddrModeFlat;
    sf.AddrStack.Offset = c.Rsp;
    sf.AddrStack.Mode   = AddrModeFlat;
    ////intel Itanium(??)
#elif _M_IA64
    imageType            = IMAGE_FILE_MACHINE_IA64;
    sf.AddrPC.Offset     = c.StIIP;
    sf.AddrPC.Mode       = AddrModeFlat;
    sf.AddrFrame.Offset  = c.IntSp;
    sf.AddrFrame.Mode    = AddrModeFlat;
    sf.AddrBStore.Offset = c.RsBSP;
    sf.AddrBStore.Mode   = AddrModeFlat;
    sf.AddrStack.Offset  = c.IntSp;
    sf.AddrStack.Mode    = AddrModeFlat;
#else
#error "Platform not supported!"
#endif

    DWORD64 dwDisplayment = 0;
    // [MEMORY] Temporary buffers for symbol lookup.
    PIMAGEHLP_SYMBOL64 pSym  = (PIMAGEHLP_SYMBOL64) new BYTE[sizeof(IMAGEHLP_SYMBOL64) + STACKWALK_MAX_NAMELEN];
    PIMAGEHLP_LINE64   pLine = new IMAGEHLP_LINE64;

    // [INTENT] Walk the stack frame by frame using StackWalk64.
    while (StackWalk64(imageType, m_hProcess, hThread, &sf, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
        ZeroMemory(pSym, sizeof(IMAGEHLP_SYMBOL64) + STACKWALK_MAX_NAMELEN);
        ZeroMemory(pLine, sizeof(IMAGEHLP_LINE64));

        pSym->SizeOfStruct  = sizeof(IMAGEHLP_SYMBOL64);
        pSym->MaxNameLength = STACKWALK_MAX_NAMELEN;
        pLine->SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        LPSTACKINFO pCallStack = new STACKINFO;
        ZeroMemory(pCallStack, sizeof(STACKINFO));
        pCallStack->szFncAddr = sf.AddrPC.Offset;
        if (sf.AddrPC.Offset != 0) {
            // [INTENT] Look up symbol name for this address.
            if (SymGetSymFromAddr64(m_hProcess, sf.AddrPC.Offset, &dwDisplayment, pSym)) {
                char szName[STACKWALK_MAX_NAMELEN] = "";
                StringCchCopy(pCallStack->szFncName, STACKWALK_MAX_NAMELEN, textconv_helper::A2T_(pSym->Name));
                // [INTENT] Get undecorated (human-readable) C++ symbol name.
                UnDecorateSymbolName(pSym->Name, szName, STACKWALK_MAX_NAMELEN, UNDNAME_COMPLETE);
                StringCchCopy(pCallStack->undFullName, STACKWALK_MAX_NAMELEN, textconv_helper::A2T_(szName));
                ZeroMemory(szName, STACKWALK_MAX_NAMELEN * sizeof(char));
                UnDecorateSymbolName(pSym->Name, szName, STACKWALK_MAX_NAMELEN, UNDNAME_NAME_ONLY);
                StringCchCopy(pCallStack->undName, STACKWALK_MAX_NAMELEN, textconv_helper::A2T_(szName));
            } else {
                //????487(?????????????????/????????????????)
                // this->OutputString(_T("Call SymGetSymFromAddr64 ,Address %08x Error:%08x\n"), sf.AddrPC.Offset, GetLastError());

                StringCchCopy(pCallStack->undFullName, STACKWALK_MAX_NAMELEN, textconv_helper::A2T_("Unknown"));
            }

            // [INTENT] Look up source file and line number.
            if (SymGetLineFromAddr64(m_hProcess, sf.AddrPC.Offset, (DWORD*) &dwDisplayment, pLine)) {
                StringCchCopy(pCallStack->szFileName, MAX_PATH, textconv_helper::A2T_(pLine->FileName));
                pCallStack->uFileNum = pLine->LineNumber;
            } else {
                // this->OutputString(_T("Call SymGetLineFromAddr64 ,Address %08x Error:%08x\n"), sf.AddrPC.Offset, GetLastError());

                StringCchCopy(pCallStack->szFileName, MAX_PATH, textconv_helper::A2T_("Unknown file"));
                pCallStack->uFileNum = -1;
            }

            // [INTENT] Output stack frame info: address, function name, file, line.
            //???????????????????????????????????????
            this->OutputString(_T("%08llx:%s [%s][%ld]\n"), pCallStack->szFncAddr, pCallStack->undFullName, pCallStack->szFileName,
                               pCallStack->uFileNum);
            if (NULL == pHead) {
                pHead = pCallStack;
                pTail = pHead;
            } else {
                pTail->pNext = pCallStack;
                pTail        = pCallStack;
            }
        }
    }

    delete[] pSym;
    delete pLine;

    return pHead;
}

// [INTENT] Free linked list of STACKINFO structures.
void CStackWalker::FreeStackInformations(LPSTACKINFO psi)
{
    LPSTACKINFO head = psi;
    while (NULL != head) {
        psi = psi->pNext;
        delete head;
        head = psi;
    }
}
