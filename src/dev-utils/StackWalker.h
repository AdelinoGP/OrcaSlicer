// [INTENT] Header for Windows stack walking utility using DbgHelp API.
// Provides call stack capture, symbol resolution, and module enumeration.
// [HAZARD] Windows-only — uses WinAPI types and DbgHelp structures.
// Translation requires platform-specific crash handling (e.g., breakpad on Linux/macOS).

#pragma once
#include <Windows.h>
#include <tchar.h>
#include <vector>

// [INTENT] Text conversion helpers for ANSI/Unicode interoperability.
// Simplified ATL-style converters without heavy ATL dependency.
namespace textconv_helper {
// Forward declarations of our classes. They are defined later.
class CA2A_;
class CA2W_;
class CW2A_;
class CW2W_;
class CA2BSTR_;
class CW2BSTR_;

// typedefs for the well known text conversions
typedef CA2W_ A2W_;
typedef CW2A_ W2A_;
// typedef CW2BSTR_ W2BSTR_;
// typedef CA2BSTR_ A2BSTR_;
typedef CW2A_ BSTR2A_;
typedef CW2W_ BSTR2W_;

// [INTENT] Unicode/ANSI selection based on build configuration.
#ifdef _UNICODE
typedef CA2W_ A2T_;
typedef CW2A_ T2A_;
typedef CW2W_ T2W_;
typedef CW2W_ W2T_;
// typedef CW2BSTR_ T2BSTR_;
// typedef BSTR2W_ BSTR2T_;
#else
typedef CA2A_    A2T_;
typedef CA2A_    T2A_;
typedef CA2W_    T2W_;
typedef CW2A_    W2T_;
typedef CA2BSTR_ T2BSTR_;
typedef BSTR2A_  BSTR2T_;
#endif

typedef A2W_  A2OLE_;
typedef T2W_  T2OLE_;
typedef CW2W_ W2OLE_;
typedef W2A_  OLE2A_;
typedef W2T_  OLE2T_;
typedef CW2W_ OLE2W_;

// [INTENT] Convert ANSI string to wide string (UTF-16).
// [MEMORY] Uses std::vector internally for buffer management.
class CA2W_
{
public:
    // [INTENT] Constructor performs conversion at construction time.
    // codePage defaults to CP_ACP (system default ANSI code page).
    CA2W_(LPCSTR pStr, UINT codePage = CP_ACP) : m_pStr(pStr)
    {
        if (pStr) {
            // Resize the vector and assign null WCHAR to each element
            int length = MultiByteToWideChar(codePage, 0, pStr, -1, NULL, 0) + 1;
            m_vWideArray.assign(length, L'\0');

            // Fill our vector with the converted WCHAR array
            MultiByteToWideChar(codePage, 0, pStr, -1, &m_vWideArray[0], length);
        }
    }
    ~CA2W_() {}
    operator LPCWSTR() { return m_pStr ? &m_vWideArray[0] : NULL; }
    // operator LPOLESTR() { return m_pStr ? (LPOLESTR)&m_vWideArray[0] : (LPOLESTR)NULL; }

private:
    // [INTENT] Private copy constructor/assignment — non-copyable.
    CA2W_(const CA2W_&);
    CA2W_&               operator=(const CA2W_&);
    std::vector<wchar_t> m_vWideArray;
    LPCSTR               m_pStr;
};

// [INTENT] Convert wide string (UTF-16) to ANSI or UTF-8.
// codePage parameter allows specifying CP_UTF8 for UTF-8 output.
class CW2A_
{
public:
    // Usage:
    //   CW2A_ ansiString(L"Some Text");
    //   CW2A_ utf8String(L"Some Text", CP_UTF8);
    //
    // or
    //   SetWindowTextA( W2A(L"Some Text") ); The ANSI version of SetWindowText
    CW2A_(LPCWSTR pWStr, UINT codePage = CP_ACP) : m_pWStr(pWStr)
    {
        // Resize the vector and assign null char to each element
        int length = WideCharToMultiByte(codePage, 0, pWStr, -1, NULL, 0, NULL, NULL) + 1;
        m_vAnsiArray.assign(length, '\0');

        // Fill our vector with the converted char array
        WideCharToMultiByte(codePage, 0, pWStr, -1, &m_vAnsiArray[0], length, NULL, NULL);
    }

    ~CW2A_() { m_pWStr = 0; }
    operator LPCSTR() { return m_pWStr ? &m_vAnsiArray[0] : NULL; }

private:
    CW2A_(const CW2A_&);
    CW2A_&            operator=(const CW2A_&);
    std::vector<char> m_vAnsiArray;
    LPCWSTR           m_pWStr;
};

// [INTENT] Wide-to-wide pass-through (identity conversion).
class CW2W_
{
public:
    CW2W_(LPCWSTR pWStr) : m_pWStr(pWStr) {}
    operator LPCWSTR() { return const_cast<LPWSTR>(m_pWStr); }
    // operator LPOLESTR() { return const_cast<LPOLESTR>(m_pWStr); }

private:
    CW2W_(const CW2W_&);
    CW2W_& operator=(const CW2W_&);

    LPCWSTR m_pWStr;
};

// [INTENT] ANSI-to-ANSI pass-through (identity conversion).
class CA2A_
{
public:
    CA2A_(LPCSTR pStr) : m_pStr(pStr) {}
    operator LPCSTR() { return (LPSTR) m_pStr; }

private:
    CA2A_(const CA2A_&);
    CA2A_& operator=(const CA2A_&);

    LPCSTR m_pStr;
};

/*class CW2BSTR_
{
public:
    CW2BSTR_(LPCWSTR pWStr) { m_bstrString = ::SysAllocString(pWStr); }
    ~CW2BSTR_() { ::SysFreeString(m_bstrString); }
    operator BSTR() { return m_bstrString; }

private:
    CW2BSTR_(const CW2BSTR_&);
    CW2BSTR_& operator= (const CW2BSTR_&);
    BSTR m_bstrString;
};

class CA2BSTR_
{
public:
    CA2BSTR_(LPCSTR pStr) { m_bstrString = ::SysAllocString(textconv_helper::CA2W_(pStr)); }
    ~CA2BSTR_() { ::SysFreeString(m_bstrString); }
    operator BSTR() { return m_bstrString; }

private:
    CA2BSTR_(const CA2BSTR_&);
    CA2BSTR_& operator= (const CA2BSTR_&);
    BSTR m_bstrString;
};*/
} // namespace textconv_helper

// [INTENT] Constants for symbol path and module info limits.
#define MAX_SYMBOL_PATH 1024
#define MAX_MODULE_NAME32 255
#define TH32CS_SNAPMODULE 0x00000008
#define MAX_VERSION_LENGTH 512
#define STACKWALK_MAX_NAMELEN 1024

// [INTENT] Simple assertion macro that triggers debugger break.
#define ASSERT(judge) \
    { \
        if (!(judge)) { \
            DebugBreak(); \
        } \
    }

// [INTENT] MODULEENTRY32 structure for ToolHelp API module enumeration.
typedef struct tagMODULEENTRY32
{
    DWORD   dwSize;
    DWORD   th32ModuleID;  // This module
    DWORD   th32ProcessID; // owning process
    DWORD   GlblcntUsage;  // Global usage count on the module
    DWORD   ProccntUsage;  // Module usage count in th32ProcessID's context
    BYTE*   modBaseAddr;   // Base address of module in th32ProcessID's context
    DWORD   modBaseSize;   // Size in bytes of module starting at modBaseAddr
    HMODULE hModule;       // The hModule of this module in th32ProcessID's context
    TCHAR   szModule[MAX_MODULE_NAME32 + 1];
    TCHAR   szExePath[MAX_PATH];
} MODULEENTRY32;

// [INTENT] MODULEINFO structure from PSAPI for module information.
typedef struct _MODULEINFO
{
    LPVOID lpBaseOfDll;
    DWORD  SizeOfImage;
    LPVOID EntryPoint;
} MODULEINFO, *LPMODULEINFO;

typedef MODULEENTRY32* PMODULEENTRY32;
typedef MODULEENTRY32* LPMODULEENTRY32;

// [INTENT] Linked list node for module information.
// Used by GetLoadModules() to return module list to caller.
typedef struct _tag_MODULE_INFO
{
    DWORD64                  ModuleAddress;                       // Base address in process memory
    DWORD                    dwModSize;                           // Size in bytes
    TCHAR                    szModuleName[MAX_MODULE_NAME32 + 1]; // Module name (e.g., "kernel32.dll")
    TCHAR                    szModulePath[MAX_PATH];              // Full path to module
    TCHAR                    szSymbolPath[MAX_PATH];              // Path to PDB file
    TCHAR                    szVersion[MAX_VERSION_LENGTH];       // File version string
    struct _tag_MODULE_INFO* pNext;                               // Next module in linked list
} MODULE_INFO, *LPMODULE_INFO;

// [INTENT] Linked list node for call stack frame information.
typedef struct tagSTACKINFO
{
    DWORD64       szFncAddr;                          // Function address (instruction pointer)
    TCHAR         szFileName[MAX_PATH];               // Source file name
    TCHAR         szFncName[MAX_PATH];                // Function name (raw)
    unsigned long uFileNum;                           // Line number in source file
    TCHAR         undName[STACKWALK_MAX_NAMELEN];     // Undecorated function name (short)
    TCHAR         undFullName[STACKWALK_MAX_NAMELEN]; // Undecorated function name (full signature)
    tagSTACKINFO* pNext;                              // Next frame in linked list
} STACKINFO, *LPSTACKINFO;

// [INTENT] Stack walking class using Windows DbgHelp API.
// Provides symbol loading, module enumeration, and call stack capture.
class CStackWalker
{
public:
    // [INTENT] Constructor takes process handle and optional symbol search path.
    CStackWalker(HANDLE hProcess = GetCurrentProcess(), WORD wPID = GetCurrentProcessId(), LPCTSTR lpSymbolPath = NULL);
    ~CStackWalker(void);

    // [INTENT] Load symbols for the process. Called automatically before stack walking.
    BOOL LoadSymbol();

    // [INTENT] Get list of loaded modules (DLLs/exes).
    LPMODULE_INFO GetLoadModules();

    // [INTENT] Fill in version and symbol info for a module.
    void GetModuleInformation(LPMODULE_INFO pmi);

    // [INTENT] Free module list returned by GetLoadModules.
    void FreeModuleInformations(LPMODULE_INFO pmi);

    // [INTENT] Output printf-style message to debug console.
    virtual void OutputString(LPCTSTR lpszFormat, ...);

    // [INTENT] Capture call stack from given thread.
    // Returns linked list of stack frames — caller must free via FreeStackInformations.
    LPSTACKINFO StackWalker(HANDLE hThread = GetCurrentThread(), const CONTEXT* context = NULL);

    // [INTENT] Free stack frame list returned by StackWalker.
    void FreeStackInformations(LPSTACKINFO psi);

protected:
    // [INTENT] Module enumeration using ToolHelp API.
    LPMODULE_INFO GetModulesTH32();
    // [INTENT] Module enumeration using PSAPI (fallback).
    LPMODULE_INFO GetModulesPSAPI();

protected:
    HANDLE m_hProcess;       // Process handle for symbol and stack operations
    WORD   m_wPID;           // Process ID
    LPTSTR m_lpszSymbolPath; // Symbol search path
    BOOL   m_bSymbolLoaded;  // Flag indicating symbols have been loaded
};

// [INTENT] Macro to capture current thread context.
// Platform-specific: uses different techniques for x86, x64, and IA64.
// [HAZARD] Uses inline assembly on x86; RtlCaptureContext on x64/IA64.
#if defined(_M_IX86)
#ifdef CURRENT_THREAD_VIA_EXCEPTION
// [INTENT] Alternative method: trigger exception and capture context from handler.
#define GET_CURRENT_THREAD_CONTEXT(c, contextFlags) \
    do { \
        memset(&c, 0, sizeof(CONTEXT)); \
        EXCEPTION_POINTERS* pExp = NULL; \
        __try { \
            throw 0; \
        } __except (((pExp = GetExceptionInformation()) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER)) {} \
        if (pExp != NULL) \
            memcpy(&c, pExp->ContextRecord, sizeof(CONTEXT)); \
        c.ContextFlags = contextFlags; \
    } while (0);
#else
// [INTENT] Standard method for x86: inline assembly to capture registers.
// [HAZARD] Uses x86-specific inline assembly (__asm directives).
#define GET_CURRENT_THREAD_CONTEXT(c, contextFlags) \
    do { \
        memset(&c, 0, sizeof(CONTEXT)); \
        c.ContextFlags = contextFlags; \
        __asm call $ + 5 __asm pop eax __asm mov c.Eip, eax __asm mov c.Ebp, ebp __asm mov c.Esp, esp \
    } while (0)
#endif

#else
// [INTENT] x64/IA64: use RtlCaptureContext Windows function.
#define GET_CURRENT_THREAD_CONTEXT(c, contextFlags) \
    do { \
        memset(&c, 0, sizeof(CONTEXT)); \
        c.ContextFlags = contextFlags; \
        RtlCaptureContext(&c); \
    } while (0);
#endif
