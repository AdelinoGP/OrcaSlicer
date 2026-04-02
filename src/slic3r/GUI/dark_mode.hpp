// [ANNOTATED]
// [INTENT] Public interface for Windows-specific Dark Mode support.
// [STATE] Provides access to dark-mode status and color constants.
// [UNITY] COMPLETELY REDUNDANT. Unity handles themes via UI Toolkit (USS) or Canvas-based styling.
// [PORTING_HAZARD:P1] Deeply coupled to Win32 types (HWND, COLORREF). Ignore for Unity port.

#pragma once

#include <Windows.h>

namespace NppDarkMode {
bool IsEnabled();
bool IsSupported();
bool IsSystemMenuEnabled();

COLORREF InvertLightness(COLORREF c);
COLORREF InvertLightnessSofter(COLORREF c);

COLORREF GetBackgroundColor();
COLORREF GetSofterBackgroundColor();
COLORREF GetTextColor();
COLORREF GetDarkerTextColor();
COLORREF GetEdgeColor();

HBRUSH GetBackgroundBrush();
HBRUSH GetSofterBackgroundBrush();

// handle events
bool OnSettingChange(HWND hwnd, LPARAM lParam); // true if dark mode toggled

// processes messages related to UAH / custom menubar drawing.
// return true if handled, false to continue with normal processing in your wndproc
bool UAHWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* lr);

void DrawUAHMenuNCBottomLine(HWND hWnd);

// from DarkMode.h
void InitDarkMode(bool set_dark_mode, bool set_sys_menu);
void SetDarkMode(bool set_dark_mode);
void SetSystemMenuForApp(bool set_sys_menu);
void AllowDarkModeForApp(bool allow);
bool AllowDarkModeForWindow(HWND hWnd, bool allow);
void RefreshTitleBarThemeColor(HWND hWnd);

// enhancements to DarkMode.h
void EnableDarkScrollBarForWindowAndChildren(HWND hwnd);

void SetDarkTitleBar(HWND hwnd);
void SetDarkExplorerTheme(HWND hwnd);
void SetDarkListView(HWND hwnd);
void SetDarkListViewHeader(HWND hwnd);
void AutoSubclassAndThemeChildControls(HWND hwndParent, bool subclass = true, bool theme = true);

} // namespace NppDarkMode
